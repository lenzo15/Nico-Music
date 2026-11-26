#include "sources.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
namespace {
using std::string;
string strip(const string &s) {
    auto first = s.find_first_not_of(" \t\r\n");
    return first == string::npos ? "" : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
string lower(string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return s;
}
bool mediaPath(const string &path) {
    auto dot = path.rfind('.');
    if (dot == string::npos) return false;
    auto ext = lower(path.substr(dot));
    for (const char* allowed : {".mp3", ".m4a", ".aac", ".flac", ".wav", ".ogg", ".opus",
                                ".oga", ".aiff", ".alac", ".wma", ".webm", ".mp4"})
        if (ext == allowed) return true;
    return false;
}
int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
bool label(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; }
bool domain(const string &h) {
    return !h.empty() && h.front() != '.' && h.back() != '.' && h.find("..") == string::npos &&
           std::all_of(h.begin(), h.end(), [](char c) { return label(c) || c == '.'; });
}
string hostOf(const string &url) {
    const size_t n = url.rfind("https://", 0) == 0 ? 8 : url.rfind("http://", 0) == 0 ? 7 : 0;
    if (!n || url.find_first_of("\\\"<> \t\r\n") != string::npos)
        return {};
    auto authority = url.substr(n, url.find_first_of("/?#", n) - n);
    if (authority.find('@') != string::npos)
        return {};
    auto colon = authority.find(':');
    if (colon != string::npos) {
        auto port = authority.substr(colon + 1);
        if (port.empty() || port.find_first_not_of("0123456789") != string::npos)
            return {};
        authority.resize(colon);
    }
    if (!authority.empty() && authority.back() == '.')
        authority.pop_back();
    auto host = lower(authority);
    return domain(host) ? host : "";
}
bool matches(const string &host, const string &suffix) {
    return host == suffix || (host.size() > suffix.size() &&
                              host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0 &&
                              host[host.size() - suffix.size() - 1] == '.');
}
bool readManifest(const fs::path &folder, MusicSource &source) {
    source.id = folder.filename().u8string();
    if (source.id.empty() || source.id.size() > 64 ||
        !std::all_of(source.id.begin(), source.id.end(), label))
        return false;
    std::ifstream in(folder / L"source.ini", std::ios::binary);
    std::map<string, string> fields;
    for (string raw; std::getline(in, raw);) {
        if (raw.rfind("\xef\xbb\xbf", 0) == 0)
            raw.erase(0, 3);
        auto line = strip(raw);
        if (line.empty() || line[0] == '#')
            continue;
        auto eq = line.find('=');
        if (eq == string::npos ||
            !fields.emplace(strip(line.substr(0, eq)), strip(line.substr(eq + 1))).second)
            return false;
    }
    source.name = fields["name"];
    source.home = fields["home"];
    source.search = fields["search"];
    if (fields["api"] != "2" || source.name.empty() || (!source.home.empty() && hostOf(source.home).empty()))
        return false;
    if (!source.search.empty() &&
        (hostOf(source.search).empty() || source.search.find("{query}") == string::npos))
        return false;
    std::error_code ec;
    if (!fs::is_regular_file(folder / L"main.lua", ec)) return false;
    // Optional plugin-owned companion executable, never searched on PATH.
    if (auto dep = fields["requires"]; !dep.empty()) {
        if (dep.front() == '.' || dep.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") != string::npos ||
            !fs::is_regular_file(folder / fs::u8path(dep), ec)) return false;
    }
    std::istringstream paths(fields["collection_paths"]);
    for (string token; std::getline(paths, token, ',');) {
        auto path = strip(token);
        if (path.empty() || path.front() != '/' || path.find_first_of("?#") != string::npos) return false;
        source.collectionPaths.push_back(path);
    }
    source.folder = folder;
    std::istringstream csv(fields["hosts"]);
    for (string token; std::getline(csv, token, ',');) {
        auto h = lower(strip(token));
        if (!domain(h))
            return false;
        source.hosts.push_back(h);
    }
    std::istringstream extensions(fields["file_extensions"]);
    for (string token; std::getline(extensions, token, ',');) {
        auto ext = lower(strip(token));
        if (ext.size() < 2 || ext.size() > 32 || ext.front() != '.' ||
            ext.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789", 1) != string::npos) return false;
        source.fileExtensions.push_back(ext);
    }
    return (!source.hosts.empty() && !source.home.empty()) ||
           (source.hosts.empty() && !source.fileExtensions.empty());
}
} // namespace
SourceRegistry::SourceRegistry(const fs::path &root) {
    std::error_code ec;
    for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec))
            continue;
        MusicSource s;
        if (readManifest(it->path(), s))
            items.push_back(std::move(s));
    }
    std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
}
const MusicSource *SourceRegistry::find(const std::string &id) const {
    for (auto &s : items)
        if (s.id == id)
            return &s;
    return nullptr;
}
const MusicSource *SourceRegistry::matchFile(const fs::path &path) const {
    if (path.empty()) return nullptr;
    const auto ext = lower(path.extension().u8string());
    for (const auto &s : items)
        if (std::find(s.fileExtensions.begin(), s.fileExtensions.end(), ext) != s.fileExtensions.end())
            return &s;
    return nullptr;
}
const MusicSource *SourceRegistry::match(const std::string &url) const {
    if (url.rfind("file:", 0) == 0) return matchFile(localFile(url));
    auto host = hostOf(url);
    const MusicSource *best = nullptr;
    size_t length = 0;
    for (auto &s : items)
        for (auto &h : s.hosts)
            if (h.size() > length && matches(host, h)) {
                best = &s;
                length = h.size();
            }
    return best;
}
bool SourceRegistry::listed(const string &url) const {
    // Keep local entries visible (including moved files); explicit media URLs need no provider.
    return url.rfind("file:", 0) == 0 || !route(url).empty();
}
bool SourceRegistry::isCollection(const string &url) const {
    const auto *s = match(url);
    if (!s || s->collectionPaths.empty() || hostOf(url).empty()) return false;
    auto end = url.find_first_of("?#");
    auto start = url.find('/', url.find("://") + 3);
    if (start == string::npos || start >= end) return false;
    auto path = url.substr(start, end - start);
    for (const auto &part : s->collectionPaths) {
        auto pos = path.find(part);
        if (pos == string::npos) continue;
        auto after = pos + part.size();
        if (part.back() == '/' || after == path.size() || path[after] == '/') return true;
    }
    return false;
}
fs::path SourceRegistry::localFile(const std::string &url) {
    if (url.rfind("file:///", 0) != 0 || url.size() < 12 || url.size() > 8192 ||
        url[9] != ':' || url[10] != '/' || url.find_first_of("?#") != string::npos ||
        !((url[8] >= 'A' && url[8] <= 'Z') || (url[8] >= 'a' && url[8] <= 'z'))) return {};
    string path = url.substr(8, 3);
    for (size_t i = 11; i < url.size(); ++i) {
        unsigned char c = url[i];
        if (c == '%') {
            if (i + 2 >= url.size()) return {};
            int a = hexDigit(url[i+1]), b = hexDigit(url[i+2]);
            if (a < 0 || b < 0) return {};
            c = static_cast<unsigned char>(a * 16 + b); i += 2;
        }
        if (c < 32 || c == 127 || string("\\\":*?<>|").find(char(c)) != string::npos) return {};
        path += char(c);
    }
    try { return fs::u8path(path).lexically_normal().make_preferred(); }
    catch (const fs::filesystem_error&) { return {}; }
}
std::string SourceRegistry::route(const std::string &url) const {
    if (url.rfind("file:", 0) == 0) {
        auto path = localFile(url);
        std::error_code ec;
        if (path.empty() || !fs::is_regular_file(path, ec)) return {};
        if (auto s = matchFile(path)) return "bm-" + s->id + "://" + url;
        return mediaPath(path.u8string()) ? path.u8string() : "";
    }
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return {};
    if (auto s = match(url))
        return "bm-" + s->id + "://" + url;
    // Only explicit media files may bypass a resolver. Inspect the path, not
    // the hostname, query or fragment (a page may have "?file=track.mp3").
    auto start = url.find('/', url.find("://") + 3);
    auto end = url.find_first_of("?#");
    if (start == string::npos || start >= end) return {};
    return mediaPath(url.substr(start, end - start)) ? url : "";
}
std::string SourceRegistry::original(std::string path) {
    if (path.rfind("bm-", 0) == 0) {
        auto colon = path.find("://", 3);
        if (colon != std::string::npos)
            return path.substr(colon + 3);
    }
    return path;
}
