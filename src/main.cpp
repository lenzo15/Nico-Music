#include "station.hpp"
static string clockText(double t) {
    if (t < 0)
        return "--:--";
    auto sec = static_cast<long long>(t);
    std::ostringstream out;
    out << sec / 60 << ':' << std::setw(2) << std::setfill('0') << sec % 60;
    return out.str();
}
// Use wide console input so pasting Chinese titles does not depend on the CRT code page.
static bool readLine(string &line) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (!GetConsoleMode(in, &mode))
        return bool(std::getline(std::cin, line));
    std::wstring w;
    wchar_t buf[512];
    DWORD n;
    do {
        if (!ReadConsoleW(in, buf, 512, &n, nullptr) || n == 0)
            return false;
        w.append(buf, n);
    } while (w.back() != L'\n');
    if (w.front() == 26)
        return false;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                                    nullptr, nullptr);
    line.resize(bytes);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), line.data(), bytes, nullptr,
                        nullptr);
    return true;
}

class Terminal : public Station {
    const MusicSource& provider(const string& id, bool search = false) const {
        if (id.empty()) {
            for (const auto& s : providers().all()) if (!(search ? s.search : s.home).empty()) return s;
        } else if (auto s = providers().find(id)) return *s;
        throw std::runtime_error("没有可用的来源，输入 sources 查看已安装插件");
    }
    void play(int i) {
        Station::play(i);
        cout << "已选择 #" << i + 1 << ' ' << entries()[i].name << '\n';
    }
    void importMusic(const string &args, bool start) {
        if (beginCollection(args)) {
            cout << "正在解析合集…\n" << std::flush;
            for (;;) {
                auto err = errors();
                if (!err.empty()) cout << err << '\n';
                if (auto order = finishCollection()) {
                    cout << "合集已导入 " << order->size() << " 首\n";
                    if (start) playSelection(order->front(), *order);
                    return;
                }
                Sleep(25);
            }
        }
        int i = add(args);
        if (start) play(i);
    }
    int add(const string &args) {
        int i = Station::add(args);
        cout << "歌单 #" << i + 1 << ' ' << entries()[i].name << '\n';
        return i;
    }
    void status() {
        auto i = active();
        if (i < 0) {
            cout << "当前未播放。\n";
            return;
        }
        auto &p = *audio();
        auto title = printable(p.str("media-title"));
        auto pos = p.seconds("time-pos");
        cout << (p.str("pause") == "yes"              ? "已暂停"
                 : pos < 0                            ? "加载中"
                 : p.str("paused-for-cache") == "yes" ? "缓冲中"
                                                      : "播放中")
             << "  #" << i + 1 << ' ' << (title.empty() ? entries()[i].name : title) << '\n'
             << "  " << clockText(pos) << " / " << clockText(p.seconds("duration")) << "    音量 "
             << p.integer("volume") << "%\n";
    }

  public:
    static void help() {
        cout << "来源  sources 查看插件  |  open [@来源 或 网址]  |  search [@来源] 关键词\n"
                "歌单  add <网址> [曲名]  |  list 查看  |  del <序号> 删除（先停止）\n"
                "播放  play <序号>  |  直接粘贴网址加入并播放  |  next / prev 切歌\n"
                "循环  repeat off 顺序播放  |  repeat one 单曲循环  |  repeat all 列表循环\n"
                "控制  pause 暂停/继续  |  vol <0-100>  |  seek <相对秒数，可负>\n"
                "其他  status 状态  |  stop 停止  |  help 帮助  |  quit 退出\n";
    }
    bool dispatch(const string &input) {
        auto [cmd, arg] = split(trim(input));
        {
            auto err = errors();
            if (!err.empty())
                cout << err << '\n';
        }
        if (cmd.empty())
            return true;
        if (cmd == "quit" || cmd == "exit")
            return false;
        if (cmd == "help")
            help();
        else if (cmd == "sources") {
            if (providers().all().empty()) cout << "未安装来源插件。\n";
            for (const auto& s : providers().all()) cout << '@' << s.id << "  " << s.name << '\n';
        } else if (cmd == "open") {
            if (!arg.empty() && arg[0] != '@') browse(arg);
            else {
                const auto& home = provider(arg.empty() ? "" : arg.substr(1)).home;
                if (home.empty()) throw std::runtime_error("此来源没有提供主页");
                browse(home);
            }
        } else if (cmd == "search") {
            auto [first, rest] = split(arg);
            bool named = !first.empty() && first[0] == '@';
            auto query = named ? rest : arg;
            if (query.empty()) throw std::runtime_error("用法：search [@来源] 关键词");
            auto url = provider(named ? first.substr(1) : "", true).search;
            if (url.empty()) throw std::runtime_error("此来源没有提供搜索入口");
            url.replace(url.find("{query}"), 7, encode(query));
            browse(url);
        } else if (cmd == "add")
            importMusic(arg, false);
        else if (urlOK(cmd) || !SourceRegistry::localFile(cmd).empty())
            importMusic(trim(input), true);
        else if (cmd == "list") {
            cout << "我的歌单 · " << count() << " 首\n";
            int current = active();
            for (int i = 0; i < count(); ++i)
                cout << (i == current ? " > " : "   ") << i + 1 << ". " << entries()[i].name << '\n';
        } else if (cmd == "play")
            play(number(arg, 1, count()) - 1);
        else if (cmd == "next" || cmd == "prev") {
            int current = active();
            int delta = cmd == "next" ? 1 : -1;
            if (current >= 0) {
                if (adjacent(queuePosition(), delta) < 0) throw std::runtime_error("已到播放队列边界");
                step(delta);
                cout << "已选择 #" << lastIndex()+1 << ' ' << entries()[lastIndex()].name << '\n';
            } else {
                int next = lastIndex() + delta;
                if (repeatMode() == "all" && count()) next = (next + count()) % count();
                play(next);
            }
        } else if (cmd == "repeat") {
            if (!arg.empty()) setRepeat(arg);
            cout << (repeatMode() == "all" ? "列表循环" : repeatMode() == "one" ? "单曲循环" : "顺序播放") << '\n';
        } else if (cmd == "stop") {
            stop();
            cout << "已停止。\n";
        } else if (cmd == "del") {
            erase(number(arg, 1, count()) - 1);
            cout << "已删除。\n";
        } else if (cmd == "vol") {
            auto v = std::to_string(number(arg, 0, 100));
            engine().command({"set", "volume", v.c_str()});
            cout << "音量 " << v << "%\n";
        } else if (cmd == "pause" || cmd == "seek") {
            if (active() < 0)
                throw std::runtime_error("当前未播放");
            if (cmd == "pause") {
                engine().command({"cycle", "pause"});
                status();
            } else {
                number(arg, -86400, 86400);
                engine().command({"seek", arg.c_str(), "relative"});
            }
        } else if (cmd == "status")
            status();
        else
            cout << "未知命令，输入 help 查看用法。\n";
        return true;
    }
};
int main() {
    auto cp = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
    int code = 0;
    try {
        Terminal station;
        cout << "● NicoMusic  /  音乐播放器\n";
        Terminal::help();
        string line;
        while (cout << "\nNicoMusic > " << std::flush, readLine(line)) {
            try {
                if (!station.dispatch(line))
                    break;
            } catch (const std::exception &e) {
                cout << "提示：" << e.what() << '\n';
            }
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        code = 1;
    }
    cout << std::flush;
    if (cp)
        SetConsoleOutputCP(cp);
    return code;
}
