#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <windows.h>
#include <winhttp.h>

struct UpdateDownload {
    std::string url, name, sha256;
    std::uint64_t size = 0;
};
struct UpdateStatus {
    std::string phase = "idle", body, error;
    DWORD httpStatus = 0;
    std::uint64_t received = 0, total = 0;
    std::filesystem::path file;
};

// Network/file work stays off the window thread. Only the UI thread starts jobs;
// cancellation and status snapshots are safe while a worker is running.
class Updater {
public:
    ~Updater();
    bool check();
    bool download(const UpdateDownload& asset, const std::filesystem::path& destination);
    void cancel();
    bool busy() const { return working.load(); }
    std::optional<UpdateStatus> status(bool force = false);
    static bool validAsset(const UpdateDownload& asset);
    static bool installable(const UpdateDownload& asset, const std::string& currentVersion);

private:
    std::thread worker;
    std::atomic<bool> working{false}, cancelled{false};
    std::mutex stateMutex, requestMutex;
    UpdateStatus state;
    bool dirty = true;
    HINTERNET activeRequest = nullptr;

    void setStatus(UpdateStatus value);
    void progress(std::uint64_t bytes);
    void closeRequest(HINTERNET request);
    void interrupted() const;
    void get(const std::wstring& url, const wchar_t* accept,
             const std::function<void(const char*, DWORD)>& consume, DWORD& status);
};
