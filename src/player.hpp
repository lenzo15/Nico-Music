#pragma once
#include <windows.h>
#include <mpv/client.h>
#include <filesystem>
#include <initializer_list>
#include <chrono>
#include "sources.hpp"
#include <optional>
#include <vector>
#include <string>

// Resolve the stable C client API at run time; browsing does not need the DLL.
class Player {
    HMODULE module = nullptr;
    mpv_handle* ctx = nullptr;
    decltype(&mpv_create) create{};
    decltype(&mpv_initialize) initialize{};
    decltype(&mpv_terminate_destroy) destroy{};
    decltype(&mpv_set_option_string) option{};
    decltype(&mpv_command) run{};
    uint64_t nextRequest = 1;
    std::string pendingToken, pendingUrl;
    std::chrono::steady_clock::time_point deadline;
    std::string collectionToken;
    std::chrono::steady_clock::time_point collectionDeadline;
    struct CollectionReply { std::string rows, error; };
    std::optional<CollectionReply> collectionReply;
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
    std::string str(const char* property) const;
    int64_t integer(const char* property, int64_t fallback = -1) const;
    double seconds(const char* property) const;
    struct Metadata {
        std::string url, title;
        std::optional<std::string> thumbnail; // absent for title-only playback events
    };
    void requestMetadata(const std::string& url, const std::string& target);
    void requestCollection(const std::string& target);
    std::optional<std::string> takeCollection();
    void discardCollection() { collectionToken.clear(); collectionReply.reset(); }
    bool metadataBusy() const { return !pendingToken.empty(); }
    std::string poll(std::vector<Metadata>& updates);
};
