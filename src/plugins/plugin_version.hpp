#pragma once
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <string>

// One release number for the entire plugin directory (source, preset, tools and assets).
// Empty string = legacy package without VERSION; nullopt = present but invalid/unreadable.
inline std::optional<std::string> readPluginVersion(const std::filesystem::path& folder) {
    const auto file = folder / L"VERSION";
    std::error_code ec;
    if (!std::filesystem::exists(file, ec) && !ec) return std::string{};
    if (!std::filesystem::is_regular_file(file, ec)) return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    char bytes[65];
    in.read(bytes, sizeof(bytes));
    if (!in.eof() || in.bad()) return std::nullopt;
    std::string text(bytes, static_cast<size_t>(in.gcount()));
    if (text.compare(0, 3, "\xef\xbb\xbf") == 0) text.erase(0, 3);
    if (!text.empty() && text.back() == '\n') {
        text.pop_back();
        if (!text.empty() && text.back() == '\r') text.pop_back();
    }
    // Same grammar as install-update.ps1. All components fit in System.Version.
    static const std::regex format(R"((0|[1-9][0-9]{0,8})(\.(0|[1-9][0-9]{0,8})){2})");
    if (!std::regex_match(text, format)) return std::nullopt;
    return text;
}
