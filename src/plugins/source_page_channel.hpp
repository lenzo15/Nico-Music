#pragma once
#include "../audio/player.hpp"
#include <cstring>

// Desktop-only request/reply protocol over mpv's generic client-message transport.
class SourcePageChannel {
    struct Reply { std::string id, data, error; };
    std::optional<Reply> pending;
    std::string token;
    uint64_t serial = 0;
    std::chrono::steady_clock::time_point deadline;
public:
    void reset() { pending.reset(); token.clear(); }
    void request(Player& transport, const std::string& source, const std::string& id,
                 const std::string& action, const std::string& data) {
        const auto next = std::to_string(++serial);
        transport.command({"script-message", "bm-source-page", source.c_str(), next.c_str(), action.c_str(), data.c_str()});
        token = next;
        pending = Reply{id, {}, {}};
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    }
    void operator()(const mpv_event_client_message& msg) {
        if (token.empty() || msg.num_args != 4 || std::strcmp(msg.args[0], "bm-source-page-result") || token != msg.args[1]) return;
        token.clear();
        if (std::strlen(msg.args[2]) > 16384 || std::strlen(msg.args[3]) > 1024)
            pending->error = "来源页面响应过大";
        else { pending->data = msg.args[2]; pending->error = msg.args[3]; }
    }
    std::optional<Reply> take() {
        if (!pending) return {};
        if (!token.empty()) {
            if (std::chrono::steady_clock::now() < deadline) return {};
            pending->error = "来源页面请求超时";
        }
        auto result = std::move(pending);
        reset();
        return result;
    }
};
