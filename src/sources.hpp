#pragma once
#include <filesystem>
#include <string>
#include <vector>

struct MusicSource {
    std::string id, name, home, search;
    std::vector<std::string> hosts, fileExtensions;
    std::filesystem::path folder;
};

// Immutable snapshot of local manifests, shared by navigation, metadata and playback.
class SourceRegistry {
    std::vector<MusicSource> items;
    const MusicSource *matchFile(const std::filesystem::path &path) const;

  public:
    explicit SourceRegistry(const std::filesystem::path &root);
    const auto &all() const { return items; }
    const MusicSource *find(const std::string &id) const;
    const MusicSource *match(const std::string &url) const;
    // Plugin target, unchanged direct media URL, or empty if unsupported.
    std::string route(const std::string &url) const;
    // Validate/decode a drive-local file URI independently of installed plugins.
    static std::filesystem::path localFile(const std::string &url);
    static std::string original(std::string mpvPath);
};
