#include "player.hpp"
#include <cstring>
#include <stdexcept>
#include <vector>

template<class T> static void bindSymbol(HMODULE dll, T& target, const char* name) {
    auto proc = GetProcAddress(dll, name);
    if (!proc) throw std::runtime_error(std::string("libmpv 缺少接口：") + name);
    static_assert(sizeof(proc) == sizeof(target));
    std::memcpy(&target, &proc, sizeof(proc));
}

Player::Player(const std::filesystem::path& dir, const SourceRegistry& sources) {
    module = LoadLibraryExW((dir / L"libmpv-2.dll").c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) throw std::runtime_error("无法加载 libmpv-2.dll，请检查程序目录、DLL 依赖和架构。系统错误 " + std::to_string(GetLastError()));
    try {
        decltype(&mpv_client_api_version) version{};
        bindSymbol(module, version, "mpv_client_api_version");
        if ((version() >> 16) != 2) throw std::runtime_error("需要 libmpv 客户端 API 2.x");
        bindSymbol(module, create, "mpv_create");
        bindSymbol(module, initialize, "mpv_initialize");
        bindSymbol(module, destroy, "mpv_terminate_destroy");
        bindSymbol(module, option, "mpv_set_option_string");
        bindSymbol(module, run, "mpv_command");
        bindSymbol(module, get, "mpv_get_property");
        bindSymbol(module, getString, "mpv_get_property_string");
        bindSymbol(module, release, "mpv_free");
        bindSymbol(module, error, "mpv_error_string");
        bindSymbol(module, waitEvent, "mpv_wait_event");
        ctx = create();
        if (!ctx) throw std::runtime_error("无法创建音频引擎");
        for (const auto& [key, val] : std::initializer_list<std::pair<const char*, const char*>>{
                 {"config", "no"}, {"terminal", "no"}, {"vid", "no"}, {"audio-display", "no"},
                 {"idle", "yes"}, {"ytdl", "no"}, {"volume", "70"}, {"load-scripts", "no"}})
            check(option(ctx, key, val));
        // Only registered source packages can install playback hooks. A native list
        // preserves directory names containing spaces, list separators or Unicode.
        if (!sources.all().empty()) {
            decltype(&mpv_set_option) setOption{};
            bindSymbol(module, setOption, "mpv_set_option");
            std::vector<std::string> paths;
            for (const auto& source : sources.all()) paths.push_back(source.folder.u8string());
            std::vector<mpv_node> nodes(paths.size());
            for (size_t i=0; i<paths.size(); ++i) {
                nodes[i].format = MPV_FORMAT_STRING;
                nodes[i].u.string = paths[i].data();
            }
            mpv_node_list entries{static_cast<int>(nodes.size()), nodes.data(), nullptr};
            mpv_node scripts{}; scripts.format = MPV_FORMAT_NODE_ARRAY; scripts.u.list = &entries;
            check(setOption(ctx, "scripts", MPV_FORMAT_NODE, &scripts));
        }
        check(initialize(ctx));
    } catch (...) {
        if (ctx) destroy(ctx);
        FreeLibrary(module);
        throw;
    }
}
Player::~Player() { destroy(ctx); FreeLibrary(module); }
void Player::check(int code) const {
    if (code < 0) throw std::runtime_error(std::string("播放引擎：") + error(code));
}
void Player::command(std::initializer_list<const char*> args) {
    const char* name = args.size() ? *args.begin() : "";
    const bool stop = !std::strcmp(name, "stop");
    if (loadingEntry >= 0 && !cancelledLoad && (stop || !std::strcmp(name, "playlist-play-index"))) {
        cancelledLoad = true;
        const auto id = std::to_string(loadingEntry);
        const char* cancel[] = {"script-message", "bm-playback-cancel", id.c_str(), nullptr};
        run(ctx, cancel);
    }
    std::vector<const char*> argv(args);
    argv.push_back(nullptr);
    check(run(ctx, argv.data()));
    if (stop) {
        for (const auto& entry : playlistUrls) retiredEntries.push_back(entry.first);
    }
}
int64_t Player::playlistId(int64_t position) const {
    return position < 0 ? -1 : integer(("playlist/" + std::to_string(position) + "/id").c_str());
}
std::string Player::playlistUrl(int64_t id) const {
    const auto found = playlistUrls.find(id);
    return found != playlistUrls.end() ? found->second : std::string{};
}
void Player::appendPlaylistEntry(const std::string& target, const std::string& url) {
    command({"loadfile", target.c_str(), "append"});
    const auto id = playlistId(integer("playlist-count", 0) - 1);
    if (id >= 0) playlistUrls[id] = url;
}
void Player::removePlaylistEntry(int position) {
    const auto id = playlistId(position);
    command({"playlist-remove", std::to_string(position).c_str()});
    if (id >= 0) retiredEntries.push_back(id);
}
std::string Player::str(const char* property) const {
    char* value = getString(ctx, property);
    if (!value) return {};
    std::string result(value); release(value); return result;
}
int64_t Player::integer(const char* property, int64_t fallback) const {
    int64_t value{};
    return get(ctx, property, MPV_FORMAT_INT64, &value) < 0 ? fallback : value;
}
double Player::seconds(const char* property) const {
    double value{};
    return get(ctx, property, MPV_FORMAT_DOUBLE, &value) < 0 ? -1.0 : value;
}
void Player::requestMetadata(const std::string& url, const std::string& target) {
    auto token = std::to_string(nextRequest++);
    command({"script-message", "bm-metadata", token.c_str(), target.c_str()});
    pendingToken = token;
    pendingUrl = url;
    // A broken plugin must not permanently block metadata for other sources.
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
}

void Player::requestCollection(const std::string& target) {
    if (!collectionToken.empty() || collectionReply) throw std::runtime_error("正在导入另一个合集");
    auto token = std::to_string(nextRequest++);
    command({"script-message", "bm-collection", token.c_str(), target.c_str()});
    collectionToken = token;
    collectionDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
}
std::optional<std::string> Player::takeCollection() {
    if (!collectionReply) {
        if (collectionToken.empty() || std::chrono::steady_clock::now() < collectionDeadline) return {};
        collectionToken.clear();
        throw std::runtime_error("合集解析超时，请重试");
    }
    auto reply = std::move(*collectionReply);
    collectionReply.reset();
    if (!reply.error.empty()) throw std::runtime_error(reply.error);
    return std::move(reply.rows);
}

std::optional<Player::PlaybackError> Player::poll(std::vector<Metadata>& updates, WarningChanges& warnings, const ClientMessageSink& sink) {
    std::optional<PlaybackError> result;
    for (;;) {
        const auto* ev = waitEvent(ctx, 0);
        if (ev->event_id == MPV_EVENT_NONE) {
            if (metadataBusy() && std::chrono::steady_clock::now() >= deadline) pendingToken.clear();
            // Keep retired entries through this drain so already queued events still resolve.
            for (const auto id : retiredEntries) playlistUrls.erase(id);
            retiredEntries.clear();
            return result;
        }
        if (ev->event_id == MPV_EVENT_START_FILE) {
            currentEntry = loadingEntry = static_cast<const mpv_event_start_file*>(ev->data)->playlist_entry_id;
            currentUrl = playlistUrl(currentEntry);
            cancelledLoad = false;
            reportedError.reset();
        }
        if (ev->event_id == MPV_EVENT_CLIENT_MESSAGE) {
            auto* msg = static_cast<const mpv_event_client_message*>(ev->data);
            if (sink) sink(*msg);
            if (!cancelledLoad && loadingEntry >= 0 && msg->num_args == 5 &&
                !std::strcmp(msg->args[0], "bm-playback-error") && std::to_string(loadingEntry) == msg->args[1] &&
                std::strlen(msg->args[2]) <= 8192 && std::strlen(msg->args[4]) <= 512) {
                const std::string target = msg->args[2], code = msg->args[3];
                const auto url = SourceRegistry::original(target);
                const auto prefixEnd = target.find("://");
                if (url != target && prefixEnd > 3 && (currentUrl.empty() || url == currentUrl)) {
                    const char* message = code == "auth_required" ? "需要登录此来源" :
                        code == "unavailable" ? "来源暂不提供此曲目的播放" :
                        code == "network" ? "连接来源失败，请检查网络" :
                        code == "timeout" ? "链接解析超时，请重试" :
                        code == "resolve_failed" ? "来源无法解析播放地址" : nullptr;
                    if (message) {
                        std::string detail = msg->args[4];
                        for (char& c : detail) if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
                        reportedError = PlaybackError{code, detail.empty() ? message : detail, target.substr(3, prefixEnd - 3), url};
                    }
                }
            }
            if (!collectionToken.empty() && msg->num_args == 4 &&
                !std::strcmp(msg->args[0], "bm-collection-result") && collectionToken == msg->args[1]) {
                collectionToken.clear();
                if (std::strlen(msg->args[2]) > 1024 * 1024 || std::strlen(msg->args[3]) > 1024)
                    collectionReply = CollectionReply{"", "合集解析结果过大"};
                else collectionReply = CollectionReply{msg->args[2], msg->args[3]};
            }
            if (metadataBusy() && msg->num_args == 4 && !std::strcmp(msg->args[0], "bm-metadata-result")
                && pendingToken == msg->args[1]) {
                if (*msg->args[2] || *msg->args[3]) updates.push_back({pendingUrl, msg->args[2], msg->args[3]});
                pendingToken.clear();
            }
        }
        if (ev->event_id == MPV_EVENT_FILE_LOADED) {
            if (!currentUrl.empty()) warnings[currentUrl].clear();
            loadingEntry = -1; reportedError.reset(); cancelledLoad = false;
            if (resumeTime > 0 && str("path") == resumePath && str("seekable") == "yes") {
                try { command({"seek", std::to_string(resumeTime).c_str(), "absolute"}); } catch (const std::exception&) {}
            }
            resumeTime = -1;
            auto title = str("force-media-title");
            if (title.empty()) title = str("metadata/by-key/title");
            if (!title.empty()) updates.push_back({SourceRegistry::original(str("path")), title, std::nullopt});
        }
        if (ev->event_id == MPV_EVENT_END_FILE) {
            const auto* end = static_cast<const mpv_event_end_file*>(ev->data);
            const auto id = end->playlist_entry_id;
            const bool loading = loadingEntry == id;
            const auto url = currentEntry == id ? currentUrl : playlistUrl(id);
            if (end->reason == MPV_END_FILE_REASON_ERROR && !(loading && cancelledLoad)) {
                if (loading && reportedError) result = std::move(reportedError);
                else result = PlaybackError{"playback_failed", std::string("链接播放失败：") + error(end->error) +
                    "。请检查网络、网页权限和来源插件的安装及版本。", "", url};
                if (!result->url.empty()) warnings[result->url] = result->message;
            }
            if (end->reason == MPV_END_FILE_REASON_REDIRECT && !url.empty()) {
                playlistUrls.erase(id);
                for (int i = 0; i < end->playlist_insert_num_entries; ++i)
                    playlistUrls.emplace(end->playlist_insert_id + i, url);
            }
            if (currentEntry == id) { currentEntry = -1; currentUrl.clear(); }
            if (loading) { loadingEntry = -1; reportedError.reset(); cancelledLoad = false; }
        }
    }
}
