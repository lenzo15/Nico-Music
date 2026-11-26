#pragma once
#include <windows.h>
#include <objbase.h>
#include <utility>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

// A fresh staging folder on the installation volume: creating it also checks
// write access before any download. No elevation or user-selected destination.
class UpdateInstall {
    std::filesystem::path dir;
    HANDLE process{};
    bool handedOff = false;
    std::string hash;
public:
    UpdateInstall(const std::filesystem::path& root, std::string digest) : hash(std::move(digest)) {
        GUID id{};
        wchar_t name[40];
        if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, name, 40)) throw std::runtime_error("无法初始化更新");
        dir = root / (std::wstring(L".nicomusic-update-") + name);
        std::error_code ec;
        if (!std::filesystem::create_directory(dir, ec)) throw std::runtime_error("安装目录不可写，请将播放器移至可写目录后重试");
    }
    UpdateInstall(const UpdateInstall&) = delete;
    ~UpdateInstall() {
        // A live helper cleans its own files after the parent exits.
        bool clean = !handedOff && (!process || WaitForSingleObject(process, 0) == WAIT_OBJECT_0);
        if (process) CloseHandle(process);
        std::error_code ec;
        if (clean) std::filesystem::remove_all(dir, ec);
    }
    std::filesystem::path package() const { return dir / L"package.zip"; }
    bool started() const { return process != nullptr; }
    void start() {
        auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(102), RT_RCDATA);
        auto bytes = resource ? SizeofResource(nullptr, resource) : 0;
        auto loaded = bytes ? LoadResource(nullptr, resource) : nullptr;
        auto data = loaded ? LockResource(loaded) : nullptr;
        if (!data) throw std::runtime_error("缺少内置更新程序");
        std::ofstream file(dir / L"install.ps1", std::ios::binary);
        file.write("\xef\xbb\xbf", 3); // Windows PowerShell reads UTF-8 via BOM.
        file.write(static_cast<const char*>(data), bytes);
        file.close();
        if (!file) throw std::runtime_error("无法准备更新程序，请检查磁盘空间和权限");
        wchar_t system[MAX_PATH];
        auto n = GetSystemDirectoryW(system, MAX_PATH);
        if (!n || n >= MAX_PATH) throw std::runtime_error("无法定位 Windows PowerShell");
        auto ps = std::filesystem::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
        // -File (not -Command): paths are literal process arguments, not code.
        auto cmd = L"\"" + ps.wstring() + L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" +
            (dir / L"install.ps1").wstring() + L"\" " + std::to_wstring(GetCurrentProcessId()) + L" " + std::wstring(hash.begin(), hash.end());
        STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
        if (!CreateProcessW(ps.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, system, &si, &pi))
            throw std::runtime_error("无法启动更新程序，当前版本未更改");
        CloseHandle(pi.hThread); process = pi.hProcess;
    }
    bool readyToExit() {
        if (handedOff) return false;
        if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
            std::ifstream f(dir / L"error.txt", std::ios::binary);
            char buffer[4096];
            f.read(buffer, sizeof(buffer));
            std::string detail(buffer, static_cast<size_t>(f.gcount()));
            throw std::runtime_error("更新准备失败，当前版本未更改。" + detail);
        }
        if (!std::filesystem::exists(dir / L"ready")) return false;
        std::ofstream go(dir / L"apply"); go.close();
        if (!go) throw std::runtime_error("无法确认安装，请检查磁盘空间和权限");
        handedOff = true;
        return true;
    }
};
