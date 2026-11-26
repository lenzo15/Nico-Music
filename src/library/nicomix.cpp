#include "nicomix.hpp"
#include <windows.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <algorithm>
#include <fstream>
#include <unordered_set>

namespace nicomix {
namespace fs = std::filesystem;
using namespace winrt::Windows::Data::Json;
static void require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
bool isFile(const fs::path& path) { return !_wcsicmp(path.extension().c_str(), L".nicomix"); }
static bool clean(const std::string& s) {
    return std::none_of(s.begin(), s.end(), [](unsigned char c){ return c < 32 || c == 127; });
}
static bool webUrl(const std::string& s) {
    auto start = s.rfind("https://",0)==0 ? 8u : s.rfind("http://",0)==0 ? 7u : 0u;
    if (!start || s.size()>8192 || !clean(s) || s.find_first_of(" \\\"<>")!=s.npos) return false;
    auto host = s.substr(start, s.find_first_of("/?#", start)-start);
    return !host.empty() && host.find('@')==host.npos;
}
static void checkName(const winrt::hstring& s) {
    require(!s.empty() && s.size()<=60 && std::wstring_view(s).find_first_not_of(L' ')!=std::wstring_view::npos && clean(winrt::to_string(s)), "歌单名称无效");
}
Mix read(const fs::path& path) {
    require(isFile(path), "请选择 .nicomix 歌单文件");
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    auto n = in.tellg();
    require(n>0 && n<=static_cast<std::streamoff>(maxBytes), "无法读取歌单：文件为空、过大或不可访问");
    std::string bytes(static_cast<size_t>(n), '\0'); in.seekg(0);
    require(bool(in.read(bytes.data(), n)), "歌单读取失败");
    if (bytes.compare(0,3,"\xef\xbb\xbf")==0) bytes.erase(0,3);
    try {
        auto obj = JsonObject::Parse(winrt::to_hstring(bytes));
        require(obj.GetNamedString(L"format")==L"nicomix" && obj.GetNamedNumber(L"version")==1, "不支持的歌单格式或版本");
        auto name = obj.GetNamedString(L"name"); checkName(name);
        auto songs = obj.GetNamedArray(L"tracks");
        require(songs.Size()>0 && songs.Size()<=maxTracks, "歌单为空或超过 10000 首");
        Mix mix{winrt::to_string(name), {}};
        std::unordered_set<std::string> seen;
        for (const auto& entry : songs) {
            auto song = entry.GetObject();
            auto url = winrt::to_string(song.GetNamedString(L"url"));
            auto title = winrt::to_string(song.GetNamedString(L"title", L""));
            require(webUrl(url) && clean(title) && title.size()<=8192, "歌单包含无效的曲名或非 HTTP(S) 链接");
            if (seen.insert(url).second) mix.songs.push_back({url, title.empty()?url:title});
        }
        return mix;
    } catch (const winrt::hresult_error&) { throw std::runtime_error("无效的 .nicomix 歌单文件"); }
}
static std::wstring filename(std::wstring s) {
    for (auto& c : s) if (c<32 || wcschr(L"<>:\"/\\|?*", c)) c=L'_';
    while (!s.empty() && (s.back()==L'.' || s.back()==L' ')) s.pop_back();
    if (s.empty()) return L"歌单";
    // Prefix ambiguous Windows device basenames even when followed by an extension.
    auto stem=s.substr(0,s.find(L'.')); while (!stem.empty() && stem.back()==L' ') stem.pop_back();
    for (auto& c : stem) c=static_cast<wchar_t>(towupper(c));
    if (stem==L"CON" || stem==L"AUX" || stem==L"PRN" || stem==L"NUL" || stem==L"CONIN$" || stem==L"CONOUT$" ||
        (stem.size()==4 && (stem.substr(0,3)==L"COM" || stem.substr(0,3)==L"LPT") && wcschr(L"0123456789¹²³",stem.back()))) s.insert(0,1,L'_');
    return s;
}
fs::path write(const fs::path& directory, const Mix& mix) {
    auto name=winrt::to_hstring(mix.name); checkName(name);
    require(!mix.songs.empty() && mix.songs.size()<=maxTracks, "没有可分享的网页曲目，或歌单超过 10000 首");
    JsonObject obj; JsonArray songs;
    obj.SetNamedValue(L"format", JsonValue::CreateStringValue(L"nicomix"));
    obj.SetNamedValue(L"version", JsonValue::CreateNumberValue(1));
    obj.SetNamedValue(L"name", JsonValue::CreateStringValue(name));
    size_t rawSize=0;
    for (const auto& song : mix.songs) {
        require(webUrl(song.url) && clean(song.title) && song.title.size()<=8192, "歌单中有无法分享的链接或曲名");
        rawSize+=song.url.size()+song.title.size(); require(rawSize<=maxBytes, "歌单超过 8 MiB");
        JsonObject entry; entry.SetNamedValue(L"url", JsonValue::CreateStringValue(winrt::to_hstring(song.url)));
        entry.SetNamedValue(L"title", JsonValue::CreateStringValue(winrt::to_hstring(song.title))); songs.Append(entry);
    }
    obj.SetNamedValue(L"tracks", songs); auto bytes=winrt::to_string(obj.Stringify());
    require(bytes.size()<=maxBytes, "歌单超过 8 MiB");
    auto base=filename(std::wstring(name));
    for (int i=1;i<=10000;++i) {
        auto file=directory/(base+(i==1?L"":L" ("+std::to_wstring(i)+L")")+L".nicomix");
        HANDLE h=CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h==INVALID_HANDLE_VALUE && GetLastError()==ERROR_FILE_EXISTS) continue;
        require(h!=INVALID_HANDLE_VALUE, "无法创建歌单文件，请检查目录权限");
        DWORD written{}; bool ok=WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written==bytes.size() && FlushFileBuffers(h);
        CloseHandle(h); if (!ok) { DeleteFileW(file.c_str()); throw std::runtime_error("写入歌单失败，请检查可用空间"); }
        return file;
    }
    throw std::runtime_error("同名文件过多，请选择其他目录");
}
}
