#pragma once
#include <windows.h>
#include <mpv/client.h>
#include <filesystem>
#include <initializer_list>
#include <chrono>
#include <functional>
#include "../plugins/sources.hpp"
#include <optional>
#include <vector>
#include <string>
#include <unordered_map>

// Resolve the stable C client API at run time; browsing does not need the DLL.
class Player {
    HMODULE module = nullptr;
    mpv_handle* ctx = nullptr;
    decltype(&mpv_create) create{};
    decltype(&mpv_initialize) initialize{};
    decltype(&mpv_terminate_destroy) destroy{};
    decltype(&mpv_set_option_string) option{};
    decltype(&mpv_command) run{};
    std::string resumePath;
    double resumeTime = -1;
    uint64_t nextRequest = 1;
    std::string pendingToken, pendingUrl;
    std::chrono::steady_clock::time_point deadline;
    std::string collectionToken;
    std::chrono::steady_clock::time_point collectionDeadline;
    struct CollectionReply { std::string rows, error; };
    std::optional<CollectionReply> collectionReply;
    // Stable mpv playlist IDs identify failures even after automatic advance.
    std::unordered_map<int64_t, std::string> playlistUrls;
    std::vector<int64_t> retiredEntries;
    int64_t currentEntry = -1, loadingEntry = -1;
    std::string currentUrl;
    bool cancelledLoad = false;
    int64_t playlistId(int64_t position) const;
    std::string playlistUrl(int64_t id) const;
public:
    struct PlaybackError { std::string code, message, source, url; };
private:
    std::optional<PlaybackError> reportedError;
    decltype(&mpv_get_property) get{};
    decltype(&mpv_get_property_string) getString{};
    decltype(&mpv_free) release{};
    decltype(&mpv_error_string) error{};
    decltype(&mpv_wait_event) waitEvent{};
    void check(int code) const;
public:
    Player(const std::filesystem::path& dir, const SourceRegistry& sources);
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    void command(std::initializer_list<const char*> args);
    void appendPlaylistEntry(const std::string& target, const std::string& url);
    void removePlaylistEntry(int position);
    std::string str(const char* property) const;
    int64_t integer(const char* property, int64_t fallback = -1) const;
    double seconds(const char* property) const;
    struct Metadata {
        std::string url, title;
        std::optional<std::string> thumbnail; // absent for title-only playback events
    };
    void resumeAfterLoad(std::string path, double seconds) { resumePath = std::move(path); resumeTime = seconds; }
    void requestMetadata(const std::string& url, const std::string& target);
    void requestCollection(const std::string& target);
    std::optional<std::string> takeCollection();
    void discardCollection() { collectionToken.clear(); collectionReply.reset(); }
    bool metadataBusy() const { return !pendingToken.empty(); }
    using ClientMessageSink = std::function<void(const mpv_event_client_message&)>;
    // Last result per original URL since the previous poll; an empty message clears a warning.
    using WarningChanges = std::unordered_map<std::string, std::string>;
    std::optional<PlaybackError> poll(std::vector<Metadata>& updates, WarningChanges& warnings, const ClientMessageSink& sink = {});
};
