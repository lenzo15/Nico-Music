// Run: NicoMusicSourceTest <directory containing libmpv-2.dll> <repository root>
#include "../src/library/station.hpp"
#include "../src/plugins/plugin_resources.hpp"
#include "../src/plugins/source_page_channel.hpp"
#include "../src/desktop/source_credential.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
using namespace std::chrono_literals;
static void verify(bool ok, const char* error) { if (!ok) throw std::runtime_error(error); }

static void playbackWarnings(const fs::path& runtime) {
    const auto dir = fs::temp_directory_path() / (L"NicoMusic-warning-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { fs::path dir; ~Cleanup() { std::error_code ec; fs::remove_all(dir, ec); } } cleanup{dir};
    fs::create_directories(dir);
    const auto first = dir / L"bad.wav", second = dir / L"missing.wav", good = dir / L"good.wav";
    auto writeWav = [&](const fs::path& path) {
        // Three seconds of mono PCM silence at 8 kHz.
        constexpr unsigned char header[] = {82,73,70,70,164,187,0,0,87,65,86,69,102,109,116,32,16,0,0,0,1,0,1,0,
            64,31,0,0,128,62,0,0,2,0,16,0,100,97,116,97,128,187,0,0};
        const char frames[48000]{};
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(header), sizeof(header)).write(frames, sizeof(frames));
    };
    writeWav(good);
    std::ofstream(first) << "not audio";
    SourceRegistry sources(dir / L"plugins");
    Player p(runtime, sources);
    const string urls[] = {"file:///C:/bad.wav", "file:///C:/missing.wav", "file:///C:/good.wav"};
    p.appendPlaylistEntry(first.u8string(), urls[0]);
    p.appendPlaylistEntry(second.u8string(), urls[1]);
    p.appendPlaylistEntry(good.u8string(), urls[2]);
    p.command({"playlist-play-index", "0"});

    auto wait = [](auto check) {
        for (auto until = std::chrono::steady_clock::now() + 5s; std::chrono::steady_clock::now() < until;) {
            if (check()) return;
            std::this_thread::sleep_for(15ms);
        }
        throw std::runtime_error("playback warning fixture timed out");
    };
    // Let both failures and the next song's successful load queue up before polling.
    wait([&] { return p.seconds("time-pos") > 0.1; });
    std::vector<Player::Metadata> metadata;
    Player::WarningChanges changes;
    auto error = p.poll(metadata, changes);
    verify(error && error->url == urls[1], "latest error did not use the original URL");
    verify(changes.count(urls[0]) && !changes.at(urls[0]).empty() &&
        changes.count(urls[1]) && !changes.at(urls[1]).empty() &&
        changes.count(urls[2]) && changes.at(urls[2]).empty(), "failed rows were confused with auto-advance");

    writeWav(first);
    p.command({"playlist-play-index", "0"});
    changes.clear();
    wait([&] { p.poll(metadata, changes); return changes.count(urls[0]) != 0; });
    verify(changes.size() == 1 && changes.at(urls[0]).empty(), "retry did not clear only the recovered row");
    p.command({"stop"});
    changes.clear();
    p.poll(metadata, changes);
    verify(changes.empty(), "stopping playback must not mark a song");

    const auto profile = dir / L"profile";
    fs::create_directories(profile);
    SetEnvironmentVariableW(L"NICOMUSIC_HOME", profile.c_str());
    Station station;
    const auto path = good.generic_u8string();
    const string local = "file:///" + path.substr(0, 3) + encode(path.substr(3));
    station.add(local + " fixture");
    fs::remove(good);
    try { station.play(0); } catch (const std::exception&) {}
    verify(!station.playbackWarning(local).empty(), "missing local file did not mark its track");
    station.erase(0);
    verify(station.playbackWarning(local).empty(), "track deletion kept its warning");
    std::cout << "OK: successive playback failures, attribution after advance, successful retry, stop, missing file and deletion\n";
}

static void sourceAssetChanges(const fs::path& examples) {
    const auto root = fs::temp_directory_path() / (L"NicoMusic-assets-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { fs::path dir; ~Cleanup() { std::error_code ec; fs::remove_all(dir, ec); } } cleanup{root};
    const auto pkg = root / L"source-page";
    fs::create_directories(root);
    fs::copy(examples / L"source-page", pkg, fs::copy_options::recursive);
    const auto theme = root / "theme";
    fs::create_directories(theme / "focus");
    fs::create_directories(theme / "page");
    std::ofstream(theme / "focus/preset.ini") << "api=1\nname=Theme\n";
    std::ofstream(theme / "focus/preset.js").close();
    std::ofstream(theme / "focus/style.css").close();
    const PluginSnapshot before(root);
    std::ofstream(theme / "page/settings.html") << "theme settings";
    std::ofstream(pkg / "page/style.css", std::ios::app) << "/* changed */";
    fs::create_directory(pkg / "confirm");
    std::ofstream(pkg / "confirm/index.html") << "legacy UI";
    const PluginSnapshot assets(root), cli(root, false);
    verify(before.pageFiles != assets.pageFiles && before.sourceFiles == assets.sourceFiles &&
           cli.sourceFiles == assets.sourceFiles && cli.pageFiles.empty(), "page edit invalidated Lua");
    verify(assets.focusFiles == before.focusFiles &&
           pluginResource(L"https://theme.source.bilimusic.local/settings.html", {}, assets.pageFiles, root) == theme / "page/settings.html" &&
           assets.pageFiles.at("source-page").count("settings.html"), "settings pages missing or invalidated preset");
    verify(pluginResource(L"https://source-page.collection.bilimusic.local/index.html", {}, assets.pageFiles, root).empty(),
           "obsolete collection frame origin still served");
    std::ofstream(pkg / "main.lua", std::ios::app) << "-- changed";
    const PluginSnapshot script(root);
    verify(script.sourceFiles != assets.sourceFiles && script.pageFiles == assets.pageFiles, "Lua edit misclassified");
    std::cout << "OK: only source runtime changes invalidate playback scripts\n";
}

static void sourceCredentials() {
    using namespace sourceCredential;
    const auto dir = fs::temp_directory_path() / (L"NicoMusic credential test " + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { fs::path dir; ~Cleanup() { SetEnvironmentVariableW(L"NICOMUSIC_HOME", nullptr); std::error_code ec; fs::remove_all(dir, ec); } } cleanup{dir};
    auto install = [&](const char* id, const char* helper) {
        const auto folder = dir / id;
        fs::create_directories(folder);
        std::ofstream(folder / "main.lua").close();
        std::ofstream(folder / "source.ini") << "api=2\nname=Fixture\nfile_extensions=.fixture\ncredential_helper=" << helper
            << "\ncredential_title=Custom title\ncredential_help=Help text\ncredential_label=Label\ncredential_placeholder=Paste it\n";
        return folder;
    };
    const auto folder = install("fixture", "account.exe");
    install("bad-path", "../fixture/account.exe");
    install("absent", "missing.exe");
    fs::copy_file(appDir() / L"NicoMusicSourceTest.exe", folder / "account.exe");
    const SourceRegistry sources(dir);
    const auto* fixture = sources.find("fixture");
    verify(fixture && fixture->credentials && fixture->credentials->helper == "account.exe" &&
        fixture->credentials->title == "Custom title" && fixture->credentials->help == "Help text" &&
        fixture->credentials->label == "Label" && fixture->credentials->placeholder == "Paste it", "credential manifest not registered");
    for (const char* id : {"bad-path", "absent"}) {
        const auto* invalid = sources.find(id);
        verify(invalid && !invalid->credentials, "invalid credential helper must not remove source or be registered");
    }
    const auto helper = fixture->folder / fixture->credentials->helper;
    const auto data = dir / L"profile";
    SetEnvironmentVariableW(L"NICOMUSIC_HOME", data.c_str());
    verify(run(helper, Action::status) == Result::empty, "fresh credential profile not empty");
    verify(run(helper, Action::save, "fixture-token") == Result::saved,
        "stdio credential save failed");
    verify(run(helper, Action::status) == Result::saved, "credential not found after save");
    verify(run(helper, Action::save, "fixture-token\nmalformed") == Result::invalid &&
           run(helper, Action::save, "invalid") == Result::invalid &&
           run(helper, Action::save, std::string(maxBytes + 1, 'x')) == Result::invalid, "bad credential accepted");
    verify(run(helper, Action::status) == Result::saved, "bad input replaced the previous credential");
    verify(run(helper, Action::clear) == Result::empty && run(helper, Action::status) == Result::empty, "credential not cleared");
    verify(run(dir / L"missing.exe", Action::status) == Result::unavailable, "missing helper did not report unavailable");
    std::cout << "OK: generic credential manifest, private pipe, save/status/invalid/clear and missing helper\n";
}

int wmain(int argc, wchar_t** argv) {
    // Self-contained credential helper fixture; only a status marker is persisted.
    if (argc == 2) {
        const auto marker = appDir() / L"saved";
        const std::wstring op(argv[1]);
        if (op == L"credential-status") return fs::exists(marker) ? 20 : 21;
        if (op == L"credential-clear") { fs::remove(marker); return 21; }
        std::string value;
        if (op != L"credential-save" || !std::getline(std::cin, value) || value != "fixture-token") return 22;
        std::ofstream(marker).close();
        return 20;
    }
    if (argc != 3) return 2;
    try {
        const auto root = fs::path(argv[2]) / L"examples/plugins";
        sourceAssetChanges(root);
        PluginSnapshot snapshot(root);
        const auto* source = snapshot.sources.find("source-page");
        verify(source && source->sidebarPage && source->icon == "icon.svg" && source->home.empty(), "source manifest");
        const auto url = L"https://source-page.source.bilimusic.local/";
        auto asset = [&](const wchar_t* suffix) {
            return pluginResource(std::wstring(url) + suffix, {}, snapshot.pageFiles, root);
        };
        verify(asset(L"index.html?v=1") == source->folder / L"page/index.html", "serve page");
        verify(asset(L"icon.svg") == source->folder / L"page/icon.svg", "serve icon");
        verify(asset(L"%2e%2e/main.lua").empty() && asset(L"main.lua").empty(), "reject private resources");

        Player player(argv[1], snapshot.sources);
        SourcePageChannel channel;
        auto request = [&](const char* id, const char* action, const char* expected) {
            channel.request(player, "source-page", id, action, "null");
            std::vector<Player::Metadata> updates;
            Player::WarningChanges warnings;
            for (auto deadline = std::chrono::steady_clock::now() + 3s; std::chrono::steady_clock::now() < deadline;) {
                player.poll(updates, warnings, std::ref(channel));
                if (auto reply = channel.take()) {
                    verify(reply->id == id && reply->error.empty() && reply->data.find(expected) != std::string::npos, "wrong source response");
                    return;
                }
                std::this_thread::sleep_for(15ms);
            }
            throw std::runtime_error("no source response");
        };
        request("1", "status", "false");
        request("2", "toggle-demo", "true");
        request("3", "status", "true");
        std::cout << "OK: local page assets and live source Lua request/reply" << std::endl;
        playbackWarnings(argv[1]);
        sourceCredentials();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
