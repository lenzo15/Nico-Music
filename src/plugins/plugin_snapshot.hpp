#pragma once
#include "sources.hpp"
#include "focus_presets.hpp"
#include <map>

// Compare file metadata, not the contents of large private runtimes. Keep focus/
// separate: changing a stylesheet must never recreate the audio engine.
using PluginFiles = std::map<std::filesystem::path, std::pair<std::filesystem::file_time_type, uintmax_t>>;
using PluginStamps = std::map<std::string, PluginFiles>;
inline PluginFiles pluginFiles(const std::filesystem::path& dir, bool source) {
    namespace fs = std::filesystem;
    PluginFiles result;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const auto& p = it->path();
        const auto name = p.filename();
        const bool reserved = source && it.depth() == 0 && (!_wcsicmp(name.c_str(), L"focus") ||
            !_wcsicmp(name.c_str(), L"page") || !_wcsicmp(name.c_str(), L"confirm"));
        if (reserved || name == L"__pycache__" || name == L".git" ||
            (GetFileAttributesW(p.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) {
            it.disable_recursion_pending(); continue;
        }
        if (!it->is_regular_file(ec)) continue;
        const auto time = it->last_write_time(ec);
        if (ec) break;
        auto size = it->file_size(ec);
        if (!ec) result.emplace(p.lexically_relative(dir), std::make_pair(time, size));
    }
    if (ec) throw fs::filesystem_error("plugin scan", dir, ec);
    return result;
}
struct PluginSnapshot {
    SourceRegistry sources;
    std::vector<FocusPreset> presets;
    PluginStamps sourceFiles, pageFiles, focusFiles;
  private:
    void scanPage(const std::string& id, const std::filesystem::path& folder) {
        auto [entry, inserted] = pageFiles.try_emplace(id);
        if (!inserted) return; // A package may provide both a source and a preset.
        const auto dir = folder / L"page";
        const auto attrs = GetFileAttributesW(dir.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT))
            entry->second = pluginFiles(dir, false);
    }
  public:
    explicit PluginSnapshot(const std::filesystem::path& root, bool visual = true) : sources(root) {
        for (auto& s : sources.all()) {
            sourceFiles.emplace(s.id, pluginFiles(s.folder, true));
            if (visual) scanPage(s.id, s.folder);
        }
        if (visual) {
            presets = findFocusPresets(root);
            for (auto& p : presets) {
                scanPage(p.id, p.folder.parent_path());
                auto files = pluginFiles(p.folder, false);
                // VERSION is outside focus/. Track its stamp without exposing it as a web resource.
                if (!p.version.empty()) {
                    const auto marker = p.folder.parent_path() / L"VERSION";
                    files.emplace(std::filesystem::path{}, std::make_pair(std::filesystem::last_write_time(marker), std::filesystem::file_size(marker)));
                }
                focusFiles.emplace(p.id, std::move(files));
            }
        }
    }
};
