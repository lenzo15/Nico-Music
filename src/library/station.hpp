#pragma once
#include "../audio/player.hpp"
#include "../plugins/sources.hpp"
#include <shellapi.h>
#include <algorithm>
#include <charconv>
#include <fstream>
#include <deque>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace fs = std::filesystem;
using std::cout;
using std::string;
struct Track
{
    string url, name;
    std::optional<string> thumbnail; // nullopt: not checked; empty: no cover
};

static fs::path appExecutable()
{
    std::wstring buf(32768, L'\0');
    auto n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (!n || n >= buf.size())
        throw std::runtime_error("无法定位程序目录");
    buf.resize(n);
    return fs::path(buf);
}
static fs::path appDir()
{
    return appExecutable().parent_path();
}
static fs::path envPath(const wchar_t *key)
{
    DWORD len = GetEnvironmentVariableW(key, nullptr, 0);
    if (!len)
        return {};
    std::wstring value(len, L'\0');
    DWORD n = GetEnvironmentVariableW(key, value.data(), len);
    if (!n || n >= len)
        return {};
    value.resize(n);
    return fs::path(value);
}
static fs::path libraryFile()
{
    auto dir = envPath(L"NICOMUSIC_HOME");
    if (!dir.empty())
        return dir / L"library.tsv";
    dir = envPath(L"LOCALAPPDATA");
    if (dir.empty())
        return appDir() / L"library.tsv";
    // Reuse the entire profile in place (including WebView2 storage) on upgrade.
    auto current = dir / L"NicoMusic", legacy = dir / L"BiliMusic";
    if (!fs::exists(current) && fs::is_directory(legacy))
        return legacy / L"library.tsv";
    return current / L"library.tsv";
}
static string trim(const string &s)
{
    auto i = s.find_first_not_of(" \t\r\n");
    return i == string::npos ? "" : s.substr(i, s.find_last_not_of(" \t\r\n") - i + 1);
}
static std::pair<string, string> split(const string &s)
{
    auto i = s.find_first_of(" \t");
    return i == string::npos ? std::make_pair(s, string{})
                             : std::make_pair(s.substr(0, i), trim(s.substr(i + 1)));
}
static string printable(string s)
{
    for (auto &c : s)
        if (static_cast<unsigned char>(c) < 32 || c == 127)
            c = ' ';
    return s;
}
static bool urlOK(const string &url)
{
    size_t start = url.rfind("https://", 0) == 0 ? 8 : url.rfind("http://", 0) == 0 ? 7
                                                                                    : 0;
    if (!start || url.size() > 8192 || url.find_first_of("\\\"<>") != string::npos)
        return false;
    auto host = url.substr(start, url.find_first_of("/?#", start) - start);
    return !host.empty() && host.find('@') == string::npos &&
           std::none_of(url.begin(), url.end(), [](unsigned char c)
                        { return c <= 32 || c == 127; });
}
static int number(const string &s, int low, int high)
{
    int v = 0;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || v < low || v > high)
        throw std::runtime_error("数字超出范围或格式错误");
    return v;
}
static void browse(const string &url)
{
    if (!urlOK(url))
        throw std::runtime_error("需要完整的 http(s) 网页地址");
    auto w = fs::u8path(url).wstring();
    if (reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        throw std::runtime_error("无法打开默认浏览器");
    cout << "已打开浏览器。复制视频地址并粘贴回终端即可播放。\n";
}
static string encode(const string &s)
{
    static const char hex[] = "0123456789ABCDEF";
    string out;
    for (unsigned char c : s)
    {
        out += '%';
        out += hex[c >> 4];
        out += hex[c & 15];
    }
    return out;
}
class Station
{
    fs::path file = libraryFile();
    SourceRegistry sources{appDir() / L"plugins"};
    std::vector<Track> tracks;
    std::unique_ptr<Player> player;
    Player::WarningChanges playbackWarnings; // session-only; playback errors are not library metadata
    std::deque<string> metadataQueue;
    string repeat = "off";
    static void applyRepeat(Player &p, const string &mode)
    {
        p.command({"set", "loop-file", mode == "one" ? "inf" : "no"});
        p.command({"set", "loop-playlist", mode == "all" ? "inf" : "no"});
    }
    int cursor = 0;
    std::vector<int> queue; // mpv positions -> library indices
    bool queueValid = false, wholeLibrary = true;

public:
    const SourceRegistry &providers() const { return sources; }
    // mpv loads Lua hooks at initialization. Swap the complete registry and retire
    // the old engine (including outstanding metadata/collection callbacks).
    void reloadSources(SourceRegistry next, bool rescanMetadata = true) {
        int index = active();
        auto order = playbackQueue();
        const bool paused = player && player->str("pause") == "yes";
        const double position = index >= 0 ? player->seconds("time-pos") : -1;
        const auto volume = player ? player->integer("volume", 70) : 70;
        const bool hadEngine = bool(player);
        player.reset(); queueValid = false; queue.clear(); metadataQueue.clear();
        sources = std::move(next);
        if (!hadEngine) { if (rescanMetadata) queueMissingCovers(); return; }
        queueMissingCovers();
        auto& p = engine();
        p.command({"set", "volume", std::to_string(volume).c_str()});
        if (index >= 0 && !sources.route(tracks[index].url).empty()) {
            playSelection(index, order, wholeLibrary);
            if (paused) p.command({"set", "pause", "yes"});
            p.resumeAfterLoad(sources.route(tracks[index].url), position);
        }
    }
    int count() const { return static_cast<int>(tracks.size()); }
    Player &engine()
    {
        if (!player)
        {
            auto p = std::make_unique<Player>(appDir(), sources);
            if (repeat != "off")
                applyRepeat(*p, repeat);
            player = std::move(p);
        }
        return *player;
    }

private:
    static std::runtime_error unavailable(const string &url)
    {
        if (url.rfind("file:", 0) != 0)
            return std::runtime_error("未安装支持此网页的来源插件");
        std::error_code ec;
        if (!fs::is_regular_file(SourceRegistry::localFile(url), ec))
            return std::runtime_error("找不到本地文件，可能已移动或删除");
        return std::runtime_error("未安装支持此文件格式的来源插件");
    }
    void appendForPlayback(Player &p, const string &url)
    {
        auto target = sources.route(url);
        if (target.empty())
            throw unavailable(url);
        p.appendPlaylistEntry(target, url);
    }
    void queueMetadata(const string &url)
    {
        if (!sources.match(url))
            return;
        if (std::find(metadataQueue.begin(), metadataQueue.end(), url) == metadataQueue.end())
            metadataQueue.push_back(url);
    }
    void pumpMetadata()
    {
        if (metadataQueue.empty() || (player && player->metadataBusy()))
            return;
        auto url = std::move(metadataQueue.front());
        metadataQueue.pop_front();
        try
        {
            auto target = sources.route(url);
            if (!target.empty())
                engine().requestMetadata(url, target);
        }
        catch (const std::exception &)
        {
        }
    }
    void save(const std::vector<Track> &entries)
    {
        fs::create_directories(file.parent_path());
        fs::path tmp = file;
        tmp += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        for (auto &t : entries)
        {
            out << t.url << '\t' << t.name;
            if (t.thumbnail)
                out << '\t' << *t.thumbnail;
            out << '\n';
        }
        out.close();
        if (!out || !MoveFileExW(tmp.c_str(), file.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(tmp.c_str());
            throw std::runtime_error("歌单保存失败，请检查数据目录权限");
        }
    }

public:
    const auto &entries() const { return tracks; }
    string playbackWarning(const string& url) const {
        const auto found = playbackWarnings.find(url);
        return found == playbackWarnings.end() ? string{} : found->second;
    }
    const Player *audio() const { return player.get(); }
    void queueMissingCovers()
    {
        for (const auto &t : tracks)
            if (!t.thumbnail && sources.match(t.url))
                metadataQueue.push_back(t.url);
    }
    std::string errors(bool *libraryChanged = nullptr, std::optional<Player::PlaybackError>* playback = nullptr,
                       const Player::ClientMessageSink& sink = {})
    {
        pumpMetadata();
        if (!player)
            return {};
        std::vector<Player::Metadata> updates;
        Player::WarningChanges warningChanges;
        auto failure = player->poll(updates, warningChanges, sink);
        for (const auto& [url, message] : warningChanges) {
            bool changed = false;
            if (message.empty()) changed = playbackWarnings.erase(url) != 0;
            else if (playbackWarning(url) != message && std::any_of(tracks.begin(), tracks.end(),
                        [&](const Track& track) { return track.url == url; })) {
                playbackWarnings[url] = message;
                changed = true;
            }
            if (changed && libraryChanged) *libraryChanged = true;
        }
        std::string err;
        if (playback) *playback = std::move(failure);
        else if (failure) err = std::move(failure->message);
        if (updates.empty())
            return err;
        auto copy = tracks;
        bool changed = false;
        for (const auto &[url, raw, image] : updates)
        {
            auto title = trim(printable(raw));
            for (auto &t : copy)
            {
                if (t.url != url)
                    continue;
                if (t.name == url && !title.empty() && title != url)
                {
                    t.name = title;
                    changed = true;
                }
                if (image)
                {
                    auto src = trim(*image);
                    if (!urlOK(src))
                        src.clear();
                    if (t.thumbnail != src)
                    {
                        t.thumbnail = src;
                        changed = true;
                    }
                }
            }
        }
        if (changed)
        {
            try
            {
                save(copy);
            }
            catch (const std::exception &e)
            {
                return e.what();
            }
            tracks.swap(copy);
            if (libraryChanged)
                *libraryChanged = true;
        }
        return err;
    }
    int queuePosition() const
    {
        if (!queueValid || !player)
            return -1;
        auto pos = player->integer("playlist-pos");
        return pos >= 0 && pos < static_cast<int>(queue.size()) ? static_cast<int>(pos) : -1;
    }
    int active() const
    {
        int pos = queuePosition();
        return pos < 0 ? -1 : queue[pos];
    }
    int queueSize() const { return static_cast<int>(queue.size()); }
    const std::vector<int> &playbackQueue() const
    {
        static const std::vector<int> empty;
        return queueValid ? queue : empty;
    }
    void playQueued(const string &url)
    {
        for (int i : playbackQueue())
        {
            if (tracks[i].url == url)
            {
                playSelection(i, queue, wholeLibrary);
                return;
            }
        }
        throw std::runtime_error("这首歌已不在播放列表中");
    }
    const string &repeatMode() const { return repeat; }
    void setRepeat(const string &mode)
    {
        if (mode != "off" && mode != "one" && mode != "all")
            throw std::runtime_error("无效的循环模式");
        if (mode == repeat)
            return;
        if (player)
            applyRepeat(*player, mode);
        repeat = mode;
    }
    int adjacent(int pos, int delta) const
    {
        if (pos < 0 || queueSize() < 2)
            return -1;
        int next = pos + delta;
        if (next >= 0 && next < queueSize())
            return next;
        if (repeat != "all")
            return -1;
        return delta > 0 ? 0 : queueSize() - 1;
    }
    void step(int delta)
    {
        int next = adjacent(queuePosition(), delta);
        if (next < 0)
            return;
        player->command({"playlist-play-index", std::to_string(next).c_str()});
        player->command({"set", "pause", "no"});
        cursor = queue[next];
    }
    void playSelection(int i, const std::vector<int> &order, bool full = false)
    {
        // Old library entries survive removal of their plugin but must not be
        // loaded, even indirectly via autoplay or a saved batch selection.
        const auto& url = tracks.at(i).url;
        if (sources.route(url).empty()) {
            const auto failure = unavailable(url);
            playbackWarnings[url] = failure.what();
            throw failure;
        }
        std::vector<int> playable;
        for (int index : order)
            if (!sources.route(tracks.at(index).url).empty())
                playable.push_back(index);
        const auto it = std::find(playable.begin(), playable.end(), i);
        if (it == playable.end())
            throw std::runtime_error("曲目不在当前歌单中");
        try {
            auto &p = engine();
            if (!queueValid || queue != playable || p.integer("playlist-count") != static_cast<int>(playable.size()))
            {
                queueValid = false;
                p.command({"stop"});
                for (int index : playable)
                    appendForPlayback(p, tracks.at(index).url);
                queue = playable;
                queueValid = true;
            }
            wholeLibrary = full;
            p.command({"playlist-play-index", std::to_string(it - playable.begin()).c_str()});
            p.command({"set", "pause", "no"});
            cursor = i;
        } catch (const std::exception& failure) {
            playbackWarnings[url] = failure.what();
            throw;
        }
    }
    void play(int i, bool newestFirst = false)
    {
        if (i < 0 || i >= count())
            throw std::runtime_error("没有这首歌，请查看歌单编号");
        std::vector<int> order(count());
        if (newestFirst)
            std::iota(order.rbegin(), order.rend(), 0);
        else
            std::iota(order.begin(), order.end(), 0);
        playSelection(i, order, true);
    }
    bool beginCollection(const string &args) {
        auto url = split(args).first;
        if (!urlOK(url) || !sources.isCollection(url)) return false;
        engine().requestCollection(sources.route(url));
        return true;
    }
    // Preview is detached from the live library: indices can change while the user decides.
    std::optional<std::vector<Track>> previewCollection() {
        auto rows = engine().takeCollection();
        if (!rows) return {};
        std::unordered_set<string> seen;
        std::vector<Track> preview;
        std::istringstream input(*rows);
        unsigned lines = 0;
        for (string line; std::getline(input, line);) {
            if (++lines > 500) throw std::runtime_error("单个合集最多支持 500 首");
            auto tab = line.find('\t');
            auto url = line.substr(0, tab);
            if (tab == string::npos || !urlOK(url) || sources.route(url).empty() || sources.isCollection(url))
                throw std::runtime_error("合集包含不支持的歌曲链接");
            if (!seen.insert(url).second) continue;
            auto title = trim(printable(line.substr(tab + 1)));
            preview.push_back({url, title.empty() ? url : title, std::nullopt});
        }
        if (preview.empty()) throw std::runtime_error("合集中没有可导入的歌曲");
        return preview;
    }
    std::vector<int> commitCollection(const std::vector<Track>& preview) {
        auto copy = tracks;
        std::unordered_map<string, int> indices;
        for (int i = 0; i < count(); ++i) indices.emplace(copy[i].url, i);
        std::vector<int> order;
        for (const auto& song : preview) {
            auto [it, fresh] = indices.emplace(song.url, static_cast<int>(copy.size()));
            if (fresh) copy.push_back(song);
            order.push_back(it->second);
        }
        const int before = count();
        if (copy.size() != tracks.size()) {
            save(copy);
            tracks.swap(copy);
            for (int i = before; i < count(); ++i) queueMetadata(tracks[i].url);
            if (wholeLibrary && queueValid && player->integer("playlist-count") == queueSize()) {
                queueValid = false;
                try {
                    for (int i = before; i < count(); ++i) {
                        if (sources.route(tracks[i].url).empty()) continue;
                        appendForPlayback(*player, tracks[i].url);
                        queue.push_back(i);
                    }
                    queueValid = true;
                } catch (const std::exception &) { /* Rebuild the queue on the next play request. */ }
            }
        }
        return order;
    }
    int add(const string &args)
    {
        auto [url, name] = split(args);
        if (url.rfind("file:", 0) == 0)
        {
            auto path = SourceRegistry::localFile(url);
            if (path.empty())
                throw std::runtime_error("需要本机磁盘上的音频文件（用法：add file:///C:/Music/song.mp3 曲名）");
            if (name.empty() && !sources.match(url))
                name = path.stem().u8string();
        }
        else if (!urlOK(url))
            throw std::runtime_error("请输入完整的 http(s) 视频或音频地址（用法：add 网址 曲名）");
        if (sources.route(url).empty())
            throw unavailable(url);
        auto lookup = [&]
        { queueMetadata(url); pumpMetadata(); };
        for (int i = 0; i < count(); ++i)
            if (tracks[i].url == url)
            {
                if (tracks[i].name == url && !name.empty())
                {
                    auto copy = tracks;
                    copy[i].name = printable(name);
                    save(copy);
                    tracks.swap(copy);
                }
                if (!tracks[i].thumbnail || tracks[i].name == url)
                    lookup();
                return i;
            }
        auto copy = tracks;
        copy.push_back({url, name.empty() ? url : printable(name), std::nullopt});
        save(copy);
        tracks.swap(copy);
        if (wholeLibrary && queueValid && player && player->integer("playlist-count") == queueSize())
        {
            queueValid = false;
            appendForPlayback(*player, url);
            queue.push_back(count() - 1);
            queueValid = true;
        }
        lookup();
        return count() - 1;
    }

public:
    Station()
    {
        std::ifstream in(file, std::ios::binary);
        string line;
        if (!in && fs::exists(file))
            throw std::runtime_error("无法读取歌单");
        while (std::getline(in, line))
        {
            // Keep a trailing tab: checked but coverless, distinct from a legacy entry.
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            auto first = line.find('\t');
            auto second = first == string::npos ? string::npos : line.find('\t', first + 1);
            auto [url, name] = split(trim(line.substr(0, second)));
            std::optional<string> thumb;
            if (second != string::npos)
            {
                auto src = line.substr(second + 1);
                thumb = urlOK(src) ? src : "";
            }
            if (urlOK(url) || !SourceRegistry::localFile(url).empty())
                tracks.push_back({url, name.empty() ? url : printable(name), thumb});
        }
    }
    void stop()
    {
        if (player)
        {
            int i = active();
            if (i >= 0)
                cursor = i;
            player->command({"stop"});
            queueValid = false;
        }
    }
    void erase(int i)
    {
        if (i < 0 || i >= count())
            throw std::runtime_error("没有这首歌");
        eraseUrls({tracks[i].url});
    }
    void eraseUrls(const std::unordered_set<string> &urls)
    {
        if (urls.empty())
            throw std::runtime_error("未选择音乐");
        std::vector<Track> kept;
        for (const auto& track : tracks)
            if (!urls.count(track.url)) kept.push_back(track);
        if (tracks.size() - kept.size() != urls.size())
            throw std::runtime_error("音乐库已变化，请重新选择曲目");
        replaceLibrary(kept);
    }
    void replaceLibrary(std::vector<Track> replacement)
    {
        std::unordered_map<string, int> indices;
        for (int i = 0; i < static_cast<int>(replacement.size()); ++i)
            if (!indices.emplace(replacement[i].url, i).second)
                throw std::runtime_error("音乐库包含重复曲目");
        std::vector<int> remap(count(), -1);
        for (int i = 0; i < count(); ++i)
            if (auto found = indices.find(tracks[i].url); found != indices.end()) remap[i] = found->second;
        bool mirrored = queueValid && player && player->integer("playlist-count") == queueSize();
        save(replacement); // Persist once; no in-memory or queue change on disk failure.
        tracks.swap(replacement);
        for (auto it = playbackWarnings.begin(); it != playbackWarnings.end();) {
            if (!indices.count(it->first)) it = playbackWarnings.erase(it);
            else ++it;
        }
        metadataQueue.erase(std::remove_if(metadataQueue.begin(), metadataQueue.end(),
                                           [&](const string &url) { return !indices.count(url); }),
                            metadataQueue.end());
        cursor = 0;
        queueValid = false;
        if (mirrored)
        {
            try
            {
                // Remove from the back so positions passed to mpv remain valid.
                for (int pos = queueSize() - 1; pos >= 0; --pos)
                {
                    if (remap[queue[pos]] < 0)
                        player->removePlaylistEntry(pos);
                }
                for (int &i : queue)
                    i = remap[i];
                queue.erase(std::remove(queue.begin(), queue.end(), -1), queue.end());
                queueValid = true;
            }
            catch (...)
            {
                // The edit is already durable. A queue failure must not hide it from undo.
                try { player->command({"stop"}); } catch (...) {}
                queueValid = false;
                queue.clear();
            }
        }
        else if (player) {
            try { player->command({"stop"}); } catch (...) {}
            queue.clear();
        }
        for (const auto& track : tracks)
            if (!track.thumbnail) queueMetadata(track.url);
    }
    int lastIndex() const { return cursor; }
};
