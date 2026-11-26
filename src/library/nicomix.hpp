#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace nicomix {
constexpr size_t maxTracks = 10000, maxBytes = 8 * 1024 * 1024;
struct Song { std::string url, title; };
struct Mix { std::string name; std::vector<Song> songs; };
bool isFile(const std::filesystem::path& path);
Mix read(const std::filesystem::path& path);
// Always creates a new file; a name collision gets a numbered suffix.
std::filesystem::path write(const std::filesystem::path& directory, const Mix& mix);
}
