#pragma once
#include "station.hpp"

// Session-local structural edits. Metadata refreshes are not user edits.
class LibraryHistory {
    struct Edit {
        string id;
        std::vector<Track> before, after;
        bool applied = true;
    };
    std::deque<Edit> edits;
    uint64_t serial = 0;
public:
    void clear() { edits.clear(); }
    string record(const std::vector<Track>& before, const std::vector<Track>& after) {
        if (before.size() == after.size() && std::equal(before.begin(), before.end(), after.begin(),
            [](const Track& a, const Track& b) { return a.url == b.url; })) return {};
        const auto id = std::to_string(++serial);
        edits.push_back({id, before, after, true});
        if (edits.size() > 50) edits.pop_front();
        return id;
    }
    void apply(Station& station, const string& id, bool undo) {
        auto it = std::find_if(edits.begin(), edits.end(), [&](const auto& edit) { return edit.id == id; });
        if (it == edits.end() || it->applied != undo)
            throw std::runtime_error("撤销记录已失效，请重新操作");
        const auto& expected = undo ? it->after : it->before;
        if (expected.size() != station.entries().size() ||
            !std::equal(expected.begin(), expected.end(), station.entries().begin(),
                [](const Track& a, const Track& b) { return a.url == b.url; }))
            throw std::runtime_error("音乐库已变化，无法应用这条撤销记录");
        // Keep newly resolved titles/covers, including for tracks removed by this step.
        std::unordered_map<string, const Track*> current;
        for (const auto& track : station.entries()) current.emplace(track.url, &track);
        for (auto* snapshot : {&it->before, &it->after})
            for (auto& track : *snapshot)
                if (auto found = current.find(track.url); found != current.end()) track = *found->second;
        station.replaceLibrary(undo ? it->before : it->after);
        it->applied = !undo; // A failed disk write must not advance history.
    }
};
