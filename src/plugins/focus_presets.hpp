#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// Visual presets are distinct from source.ini/yt-dlp packages. Only expose
// the preset's focus/ resources, never the install or user profile directory.
struct FocusPreset { std::string id, name; std::filesystem::path folder; };
inline std::vector<FocusPreset> findFocusPresets(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    std::vector<FocusPreset> result;
    std::error_code ec;
    for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        auto id = it->path().filename().u8string();
        if (id.empty() || id.size() > 63 || id.front() == '-' || id.back() == '-' ||
            id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos) continue;
        auto folder = it->path() / L"focus";
        std::error_code err;
        if (!fs::is_regular_file(folder / L"preset.js", err) || !fs::is_regular_file(folder / L"style.css", err)) continue;
        std::ifstream in(folder / L"preset.ini");
        std::string name, api;
        for (std::string line; std::getline(in, line);) {
            if (line.rfind("name=", 0) == 0) name = line.substr(5);
            if (line.rfind("api=", 0) == 0) api = line.substr(4);
        }
        if (api == "1" && !name.empty()) result.push_back({id, name, folder});
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return result;
}
