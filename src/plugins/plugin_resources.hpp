#pragma once
#include <shlwapi.h>
#include "plugin_snapshot.hpp"

// WebView2 folder mappings are latched at navigation. Serve plugin resources
// from the current catalog instead; the player document never has to navigate.
inline std::filesystem::path pluginResource(std::wstring url, const PluginStamps& focus,
                                             const PluginStamps& sources, const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    if (url.compare(0, 8, L"https://") != 0) return {};
    auto slash = url.find(L'/', 8), dot = url.find(L'.', 8);
    if (slash == url.npos || dot >= slash) return {};
    auto suffix = url.substr(dot, slash-dot);
    bool visual = suffix == L".focus.bilimusic.local";
    const bool sourcePage = suffix == L".source.bilimusic.local";
    if (!visual && !sourcePage) return {};
    const auto id = url.substr(8, dot-8);
    if (id.empty() || id.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz0123456789-") != id.npos) return {};
    const auto& catalog = visual ? focus : sources;
    auto entry = catalog.find(std::string(id.begin(), id.end()));
    if (entry == catalog.end()) return {};
    std::wstring path = url.substr(slash+1, url.find_first_of(L"?#", slash)-slash-1);
    if (path.empty() || path.find(L"%00") != path.npos) return {};
    if (FAILED(UrlUnescapeW(path.data(), nullptr, nullptr, URL_UNESCAPE_INPLACE | URL_UNESCAPE_AS_UTF8))) return {};
    path.resize(wcslen(path.c_str()));
    if (path.find_first_of(L"\\:") != path.npos) return {};
    fs::path relative(path);
    if (relative.has_root_path()) return {};
    for (const auto& part : relative) if (part == L"." || part == L"..") return {};
    if (sourcePage) {
        const auto ext = relative.extension().wstring();
        if (ext != L".html" && ext != L".js" && ext != L".css" && ext != L".json" && ext != L".svg" &&
            ext != L".png" && ext != L".webp" && ext != L".jpg" && ext != L".jpeg" && ext != L".woff2") return {};
    }
    if (!entry->second.count(relative)) return {};
    auto file = root;
    for (const auto& part : fs::path(id) / (visual ? L"focus" : L"page") / relative) {
        file /= part;
        if (GetFileAttributesW(file.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) return {};
    }
    return file;
}
inline const wchar_t* pluginMime(const std::filesystem::path& file) {
    static const std::map<std::wstring, const wchar_t*> types = {{L".html",L"text/html; charset=utf-8"},
        {L".js",L"text/javascript; charset=utf-8"},{L".css",L"text/css; charset=utf-8"}, {L".json",L"application/json"},
        {L".svg",L"image/svg+xml"},{L".png",L"image/png"},{L".jpg",L"image/jpeg"},{L".jpeg",L"image/jpeg"},
        {L".webp",L"image/webp"},{L".gif",L"image/gif"},{L".woff2",L"font/woff2"}};
    auto it = types.find(file.extension().wstring());
    return it == types.end() ? L"application/octet-stream" : it->second;
}
