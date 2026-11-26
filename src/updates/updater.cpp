#include "updater.hpp"
#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <cwctype>
#include <charconv>
#include <string_view>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
constexpr std::uint64_t maxPackage = 2ull * 1024 * 1024 * 1024;
constexpr wchar_t latest[] = L"https://api.github.com/repos/lenzo15/Nico-Music/releases/latest";
struct Cancelled {};
struct InternetCloser { void operator()(void* handle) const { if (handle) WinHttpCloseHandle(handle); } };
using Internet = std::unique_ptr<void, InternetCloser>;
struct FileCloser { void operator()(void* handle) const { if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle); } };
using File = std::unique_ptr<void, FileCloser>;
std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return std::towlower(c); });
    return value;
}
class Sha256 {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
public:
    Sha256() {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("无法初始化下载校验");
        if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("无法初始化下载校验");
        }
    }
    ~Sha256() { BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); }
    void add(const char* bytes, DWORD count) {
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes)), count, 0) < 0)
            throw std::runtime_error("下载校验失败");
    }
    std::string finish() {
        std::array<UCHAR, 32> bytes{};
        if (BCryptFinishHash(hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0) < 0)
            throw std::runtime_error("下载校验失败");
        constexpr char hex[] = "0123456789abcdef";
        std::string result;
        for (auto b : bytes) { result += hex[b >> 4]; result += hex[b & 15]; }
        return result;
    }
};
} // namespace

Updater::~Updater() {
    cancel();
    if (worker.joinable()) worker.join();
}
void Updater::cancel() {
    cancelled = true;
    std::lock_guard<std::mutex> lock(requestMutex);
    if (activeRequest) WinHttpCloseHandle(std::exchange(activeRequest, nullptr));
}
void Updater::interrupted() const { if (cancelled) throw Cancelled{}; }
void Updater::closeRequest(HINTERNET request) {
    std::lock_guard<std::mutex> lock(requestMutex);
    if (activeRequest == request) WinHttpCloseHandle(std::exchange(activeRequest, nullptr));
}
void Updater::setStatus(UpdateStatus value) {
    std::lock_guard<std::mutex> lock(stateMutex);
    state = std::move(value);
    dirty = true;
}
void Updater::progress(std::uint64_t bytes) {
    std::lock_guard<std::mutex> lock(stateMutex);
    state.received = bytes;
    dirty = true;
}
std::optional<UpdateStatus> Updater::status(bool force) {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (!dirty && !force) return std::nullopt;
    dirty = false;
    return state;
}

void Updater::get(const std::wstring& initial, const wchar_t* accept,
                  const std::function<void(const char*, DWORD)>& consume, DWORD& statusCode) {
    Internet session(WinHttpOpen(L"NicoMusic Update/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) throw std::runtime_error("无法初始化网络连接");
    WinHttpSetTimeouts(session.get(), 5000, 8000, 10000, 10000);
    auto url = initial;
    const auto start = GetTickCount64();
    const ULONGLONG deadline = wcscmp(accept, L"application/json") == 0 ? 30000 : 15 * 60 * 1000;
    for (int redirect = 0; redirect <= 5; ++redirect) {
        interrupted();
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
        if (url.size() > 8192 || !WinHttpCrackUrl(url.c_str(), 0, 0, &parts) ||
            parts.nScheme != INTERNET_SCHEME_HTTPS || parts.nPort != INTERNET_DEFAULT_HTTPS_PORT ||
            parts.dwUserNameLength || parts.dwPasswordLength)
            throw std::runtime_error("更新地址不受支持");
        const auto host = lower(std::wstring(parts.lpszHostName, parts.dwHostNameLength));
        if (host != L"api.github.com" && host != L"github.com" &&
            host != L"release-assets.githubusercontent.com" && host != L"objects.githubusercontent.com")
            throw std::runtime_error("更新地址不受信任");
        Internet connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0));
        if (!connection) throw std::runtime_error("无法连接 GitHub");
        std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        auto raw = WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!raw) throw std::runtime_error("无法创建更新请求");
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            if (cancelled) { WinHttpCloseHandle(raw); throw Cancelled{}; }
            activeRequest = raw;
        }
        auto closer = [this](void* handle) { closeRequest(handle); };
        std::unique_ptr<void, decltype(closer)> request(raw, closer);
        DWORD disable = WINHTTP_DISABLE_REDIRECTS;
        if (!WinHttpSetOption(raw, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)))
            throw std::runtime_error("无法配置安全更新请求");
        const std::wstring headers = std::wstring(L"Accept: ") + accept + L"\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
        if (!WinHttpSendRequest(raw, headers.c_str(), static_cast<DWORD>(headers.size()),
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(raw, nullptr)) {
            interrupted();
            throw std::runtime_error("无法连接 GitHub，请检查网络后重试");
        }
        DWORD bytes = sizeof(statusCode);
        if (!WinHttpQueryHeaders(raw, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &bytes, WINHTTP_NO_HEADER_INDEX))
            throw std::runtime_error("无法读取 GitHub 响应");
        if (statusCode == 301 || statusCode == 302 || statusCode == 303 || statusCode == 307 || statusCode == 308) {
            DWORD length = 0;
            WinHttpQueryHeaders(raw, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &length, WINHTTP_NO_HEADER_INDEX);
            if (!length || length > 16384) throw std::runtime_error("无效的更新跳转地址");
            std::vector<wchar_t> location(length / sizeof(wchar_t) + 1);
            if (!WinHttpQueryHeaders(raw, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                     location.data(), &length, WINHTTP_NO_HEADER_INDEX))
                throw std::runtime_error("无法读取更新跳转地址");
            url = location.data();
            continue; // Validate HTTPS and the host again before following a redirect.
        }
        if (statusCode != 200) return;
        std::array<char, 65536> buffer{};
        for (;;) {
            interrupted();
            if (GetTickCount64() - start > deadline) throw std::runtime_error("更新请求超时，请重试");
            DWORD count = 0;
            if (!WinHttpReadData(raw, buffer.data(), static_cast<DWORD>(buffer.size()), &count)) {
                interrupted();
                throw std::runtime_error("连接中断，请检查网络后重试");
            }
            if (!count) return;
            consume(buffer.data(), count);
        }
    }
    throw std::runtime_error("更新地址跳转次数过多");
}

bool Updater::check() {
    if (working.exchange(true)) return false;
    if (worker.joinable()) worker.join();
    cancelled = false;
    setStatus({"checking"});
    try {
        worker = std::thread([this] {
            UpdateStatus result;
            try {
                get(latest, L"application/json", [&](const char* data, DWORD count) {
                    if (result.body.size() + count > 1024 * 1024) throw std::runtime_error("GitHub 响应过大");
                    result.body.append(data, count);
                }, result.httpStatus);
                interrupted();
                result.phase = "checked";
            } catch (const Cancelled&) { result.phase = "cancelled"; }
            catch (const std::exception& e) { result.phase = "check-error"; result.error = e.what(); }
            setStatus(std::move(result));
            working = false;
        });
    } catch (...) { working = false; setStatus({"check-error", {}, "无法启动更新检查"}); }
    return true;
}

bool Updater::validAsset(const UpdateDownload& asset) {
    constexpr char prefix[] = "https://github.com/lenzo15/Nico-Music/releases/download/";
    if (asset.url.rfind(prefix, 0) != 0 || asset.url.size() > 4096 || !asset.size || asset.size > maxPackage ||
        asset.name.empty() || asset.name.size() > 160 || asset.name.front() == '.' || asset.name.back() == ' ' ||
        asset.name.find("..") != std::string::npos || asset.name.size() < 5 ||
        lower(std::wstring(asset.name.begin(), asset.name.end())).substr(asset.name.size() - 4) != L".zip") return false;
    for (unsigned char c : asset.url) if (c <= 32 || c >= 127 || c == '\\' || c == '"' || c == '<' || c == '>' || c == '#' || c == '?') return false;
    for (unsigned char c : asset.name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == ' ')) return false;
    if (!asset.sha256.empty() && (asset.sha256.size() != 64 || !std::all_of(asset.sha256.begin(), asset.sha256.end(),
        [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))) return false;
    // A single tag segment and an exact escaped filename prevent dot-segment
    // URLs from escaping the repository while retaining the trusted prefix.
    const auto path = asset.url.substr(sizeof(prefix) - 1);
    const auto slash = path.find('/');
    if (slash == std::string::npos || slash == 0 || path.find('/', slash + 1) != std::string::npos) return false;
    const auto tag = path.substr(0, slash);
    if (tag == "." || tag == "..") return false;
    for (size_t i = 0; i < tag.size(); ++i) {
        const char c = tag[i];
        if (c == '%' && (tag.substr(i, 3) == "%2B" || tag.substr(i, 3) == "%2b")) { i += 2; continue; }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == '+')) return false;
    }
    std::string filename;
    for (char c : asset.name) filename += c == ' ' ? "%20" : std::string(1, c);
    return path.substr(slash + 1) == filename;
}

bool Updater::installable(const UpdateDownload& asset, const std::string& currentVersion) {
    if (!validAsset(asset) || asset.sha256.empty()) return false;
    const auto name = lower(std::wstring(asset.name.begin(), asset.name.end()));
    bool windows = false;
    for (const std::wstring suffix : {L"windows-x64.zip", L"win-x64.zip", L"windows_x64.zip", L"win_x64.zip", L"windowsx64.zip", L"winx64.zip"})
        if (name.size() >= suffix.size() && name.compare(name.size()-suffix.size(), suffix.size(), suffix) == 0) windows = true;
    if (!windows) return false;
    auto version = [](std::string_view s, std::array<unsigned,3>& parts) {
        if (!s.empty() && s.front() == 'v') s.remove_prefix(1);
        for (int i = 0; i < 3; ++i) {
            auto dot = s.find('.');
            auto part = s.substr(0, dot);
            if (part.empty() || (part.size() > 1 && part[0] == '0')) return false;
            auto end = part.data() + part.size();
            auto parsed = std::from_chars(part.data(), end, parts[i]);
            if (parsed.ec != std::errc{} || parsed.ptr != end || (dot == s.npos) != (i == 2)) return false;
            if (i < 2) s.remove_prefix(dot + 1);
        }
        return true;
    };
    auto end = asset.url.rfind('/');
    auto begin = asset.url.rfind('/', end-1) + 1;
    std::array<unsigned,3> next{}, current{};
    return version(std::string_view(asset.url).substr(begin, end-begin), next) && version(currentVersion, current) && next > current;
}

bool Updater::download(const UpdateDownload& asset, const std::filesystem::path& destination) {
    if (!validAsset(asset) || destination.empty() || lower(destination.extension().wstring()) != L".zip")
        throw std::runtime_error("无效的更新安装包或保存路径");
    if (working.exchange(true)) return false;
    if (worker.joinable()) worker.join();
    cancelled = false;
    UpdateStatus initial;
    initial.phase = "downloading"; initial.total = asset.size;
    setStatus(initial);
    try {
        worker = std::thread([this, asset, destination, initial] {
            auto result = initial;
            auto partial = destination;
            partial += L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) + L".part";
            bool created = false;
            try {
                interrupted();
                File file(CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
                if (file.get() == INVALID_HANDLE_VALUE) throw std::runtime_error("无法创建下载文件，请检查目录写入权限");
                created = true;
                Sha256 digest;
                get(std::wstring(asset.url.begin(), asset.url.end()), L"application/octet-stream", [&](const char* data, DWORD count) {
                    interrupted();
                    if (result.received + count > asset.size) throw std::runtime_error("安装包大小不符合发布信息");
                    DWORD written = 0;
                    if (!WriteFile(file.get(), data, count, &written, nullptr) || written != count)
                        throw std::runtime_error("写入下载文件失败，请检查磁盘空间和权限");
                    digest.add(data, count);
                    result.received += count;
                    progress(result.received);
                }, result.httpStatus);
                interrupted();
                if (result.httpStatus != 200) throw std::runtime_error("下载失败（HTTP " + std::to_string(result.httpStatus) + "），请稍后重试");
                if (result.received != asset.size) throw std::runtime_error("安装包下载不完整，请重试");
                if (const auto hash = digest.finish(); !asset.sha256.empty() && hash != asset.sha256)
                    throw std::runtime_error("SHA-256 校验失败，已删除下载文件，请重试");
                if (!FlushFileBuffers(file.get())) throw std::runtime_error("无法保存下载文件");
                file.reset();
                interrupted();
                if (!MoveFileExW(partial.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("无法保存安装包，目标文件可能正在使用");
                created = false;
                result.phase = "downloaded"; result.file = destination;
            } catch (const Cancelled&) { result.phase = "cancelled"; }
            catch (const std::exception& e) { result.phase = "download-error"; result.error = e.what(); }
            if (created && !DeleteFileW(partial.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
                result.phase = "download-error";
                result.error += "；无法清理临时文件，请手动删除：" + partial.u8string();
            }
            setStatus(std::move(result));
            working = false;
        });
    } catch (...) { working = false; setStatus({"download-error", {}, "无法启动下载"}); }
    return true;
}
