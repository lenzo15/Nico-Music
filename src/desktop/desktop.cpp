#include "../library/station.hpp"
#include "../library/library_history.hpp"
#include "../library/nicomix.hpp"
#include "nicomix_association.hpp"
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl.h>
#include <WebView2.h>
#include <windowsx.h>
#include "../audio/audio_meter.hpp"
#include "../plugins/plugin_resources.hpp"
#include <future>
#include "startup_splash.hpp"
#include "../updates/updater.hpp"
#include "../updates/update_install.hpp"
#include "hotkeys.hpp"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace {
constexpr wchar_t page[] = L"https://studio.bilimusic.local/index.html";
HWND window{};
ComPtr<ICoreWebView2Controller> controller;
ComPtr<ICoreWebView2> view;
std::unique_ptr<Station> station;
std::unique_ptr<Updater> updater;
std::unique_ptr<HotkeySettings> hotkeySettings;
std::unique_ptr<UpdateInstall> install;
string installError;
bool cancelInstall = false;
StartupSplash splash;
bool ready = false;
bool nativeDragRegion = false;
// A session-local auto-reset event is both the instance token and a wake-up.
// A launch during startup is remembered even before the primary creates its HWND.
constexpr wchar_t instanceEvent[] = L"Local\\BiliMusic.Desktop.Activate.v1";
constexpr wchar_t primaryMarker[] = L"BiliMusic.Desktop.Primary.v1";
constexpr wchar_t windowClass[] = L"BiliMusicStudio";
constexpr UINT activateWindow = WM_APP + 2;
HANDLE activationWait{};
void CALLBACK onActivation(void* context, BOOLEAN) {
    PostMessageW(static_cast<HWND>(context), activateWindow, 0, 0);
}
void handOffForeground() {
    HWND h = nullptr;
    while ((h = FindWindowExW(nullptr, h, windowClass, nullptr)) != nullptr) {
        if (!GetPropW(h, primaryMarker)) continue;
        DWORD pid{};
        if (GetWindowThreadProcessId(h, &pid)) AllowSetForegroundWindow(pid);
        break;
    }
}
constexpr UINT trayCallback = WM_APP + 1;
constexpr UINT trayId = 1;
UINT taskbarCreated{};
constexpr UINT_PTR trayRetryTimer = 4;
bool trayRegistered = false;
bool hiddenToTray = false;

struct PlaybackHotkey {
    int id;
    UINT key, modifiers;
    const char* action;
    const char* status;
    bool registered = false;
};
PlaybackHotkey playbackHotkeys[] = {
    // IDs 1–3 belong to HotkeySettings. Built-in media keys stay independent.
    {4, VK_MEDIA_PREV_TRACK, MOD_NOREPEAT, "prev", "mediaPrev"},
    {5, VK_MEDIA_NEXT_TRACK, MOD_NOREPEAT, "next", "mediaNext"},
    {6, VK_MEDIA_PLAY_PAUSE, MOD_NOREPEAT, "toggle", "mediaToggle"},
    // Volume keys intentionally repeat while held; transport keys do not.
    {7, VK_VOLUME_UP, 0, "volumeUp", "volumeUp"},
    {8, VK_VOLUME_DOWN, 0, "volumeDown", "volumeDown"}
};
void registerPlaybackHotkeys(HWND h) {
    for (auto& key : playbackHotkeys)
        key.registered = RegisterHotKey(h, key.id, key.modifiers, key.key) != FALSE;
}
void unregisterPlaybackHotkeys(HWND h) {
    for (auto& key : playbackHotkeys) {
        if (key.registered) UnregisterHotKey(h, key.id);
        key.registered = false;
    }
}
void publishHotkeys(const string& result = {}, bool hasResult = false, int captureRequest = 0);

NOTIFYICONDATAW trayData(HWND h) {
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = h;
    icon.uID = trayId;
    return icon;
}
bool ensureTrayIcon(HWND h) {
    if (trayRegistered) return true;
    auto icon = trayData(h);
    icon.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    icon.hIcon = reinterpret_cast<HICON>(GetClassLongPtrW(h, GCLP_HICON));
    icon.uCallbackMessage = trayCallback;
    wcscpy_s(icon.szTip, L"NicoMusic · 显示播放器");
    // Explorer can also broadcast TaskbarCreated after a DPI change with the icon intact.
    if (Shell_NotifyIconW(NIM_ADD, &icon) || Shell_NotifyIconW(NIM_MODIFY, &icon)) {
        icon.uVersion = NOTIFYICON_VERSION_4;
        if (Shell_NotifyIconW(NIM_SETVERSION, &icon)) {
            trayRegistered = true;
            KillTimer(h, trayRetryTimer);
            return true;
        }
        Shell_NotifyIconW(NIM_DELETE, &icon);
    }
    SetTimer(h, trayRetryTimer, 3000, nullptr);
    return false;
}
void removeTrayIcon(HWND h) {
    KillTimer(h, trayRetryTimer);
    if (!trayRegistered) return;
    trayRegistered = false;
    auto icon = trayData(h);
    Shell_NotifyIconW(NIM_DELETE, &icon);
}
void restoreWindow(HWND h) {
    hiddenToTray = false;
    ShowWindow(h, IsIconic(h) ? SW_RESTORE : SW_SHOW); // retain maximized placement
    if (controller && !splash.visible()) controller->put_IsVisible(TRUE);
    HWND target = GetLastActivePopup(h);
    if (!IsWindowVisible(target)) target = h;
    if (!SetForegroundWindow(target)) { // Windows can deny activation to background launches.
        FLASHWINFO flash{sizeof(flash), h, FLASHW_TRAY, 3, 0};
        FlashWindowEx(&flash);
    }
}
void trayMenu(HWND h, POINT point) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 1, L"显示播放器");
    AppendMenuW(menu, MF_STRING, 2, L"退出 NicoMusic");
    SetMenuDefaultItem(menu, 1, FALSE);
    if (point.x == -1 && point.y == -1) GetCursorPos(&point);
    SetForegroundWindow(h);
    const int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                      point.x, point.y, 0, h, nullptr);
    DestroyMenu(menu);
    PostMessageW(h, WM_NULL, 0, 0);
    if (choice == 1) restoreWindow(h);
    else if (choice == 2) PostMessageW(h, WM_CLOSE, 0, 0);
}

std::vector<FocusPreset> focusPresets;
PluginStamps appliedSources, appliedFocus;
std::map<string,string> focusVersions;
string sourceVersion;
std::future<PluginSnapshot> pluginScan;
std::optional<PluginSnapshot> pendingPlugins;
HANDLE pluginWatch = INVALID_HANDLE_VALUE;
std::unique_ptr<AudioMeter> meter;
void stopMeter() {
    KillTimer(window, 2);
    meter.reset();
}
void meterUnavailable() {
    stopMeter();
    view->PostWebMessageAsJson(L"{\"levels\":null}");
}
void meterTick() {
    if (!meter || !view || hiddenToTray || IsIconic(window)) return;
    std::array<int, 3> levels{};
    switch (meter->poll(levels)) {
    case AudioMeter::Result::waiting: return;
    case AudioMeter::Result::failed: meterUnavailable(); return;
    case AudioMeter::Result::fresh:
        auto json = L"{\"levels\":[" + std::to_wstring(levels[0]) + L"," +
            std::to_wstring(levels[1]) + L"," + std::to_wstring(levels[2]) + L"]";
        if (meter->spectrum) {
            json += L",\"spectrum\":[";
            for (int band : meter->spectrum->read()) { json += std::to_wstring(band); json += L','; }
            json.back() = L']';
        }
        json += L'}';
        view->PostWebMessageAsJson(json.c_str());
    }
}

void publishWindowState() {
    if (!view || !ready || IsIconic(window)) return;
    std::wstring json = L"{\"windowState\":{\"maximized\":";
    json += IsZoomed(window) ? L"true" : L"false";
    json += L",\"nativeDrag\":";
    json += nativeDragRegion ? L"true}}" : L"false}}";
    view->PostWebMessageAsJson(json.c_str());
}

void windowAction(const string& action) {
    // Dispatch outside the WebView callback; a move loop or Close() can re-enter COM.
    if (action == "minimize") PostMessageW(window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    else if (action == "maximize") PostMessageW(window, WM_SYSCOMMAND, IsZoomed(window) ? SC_RESTORE : SC_MAXIMIZE, 0);
    else if (action == "close") PostMessageW(window, WM_CLOSE, 0, 0);
    else if (action == "drag" && !nativeDragRegion && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        POINT pt{};
        GetCursorPos(&pt);
        ReleaseCapture();
        PostMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y));
    }
}

std::wstring wide(const string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    if (n)
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
string utf8(LPCWSTR w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    if (n)
        s.pop_back();
    return s;
}
string quotedJson(const string &s) {
    string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += (c < 32 ? ' ' : static_cast<char>(c));
    }
    return out + '"';
}
bool postPlaybackShortcut(const char* action) {
    if (!ready || !view) return false;
    // The page owns initial queue selection and disabled transport boundaries.
    const auto json = wide(string("{\"shortcut\":\"") + action + "\"}");
    return SUCCEEDED(view->PostWebMessageAsJson(json.c_str()));
}
void publishHotkeys(const string& result, bool hasResult, int captureRequest) {
    if (!view || !ready || !hotkeySettings) return;
    constexpr const char* actions[] = {"prev", "next", "toggle"};
    string json = "{\"hotkeys\":{";
    for (size_t i = 0; i < 3; ++i) {
        if (i) json += ',';
        json += quotedJson(actions[i]) + ":" + (hotkeySettings->registered()[i] ? "true" : "false");
    }
    for (const auto& key : playbackHotkeys)
        json += "," + quotedJson(key.status) + ":" + (key.registered ? "true" : "false");
    json += "},\"hotkeyConfig\":[";
    for (size_t i = 0; i < 3; ++i) {
        if (i) json += ',';
        const auto& binding = hotkeySettings->bindings()[i];
        json += "{\"modifiers\":" + std::to_string(binding.modifiers) + ",\"key\":" + std::to_string(binding.key) + "}";
    }
    json += string("],\"hotkeyCapture\":") + (hotkeySettings->capturing() ? "true" : "false") +
        ",\"hotkeyCaptureRequest\":" + std::to_string(captureRequest) +
        ",\"hotkeyWarning\":" + quotedJson(hotkeySettings->warning());
    if (hasResult) json += string(",\"hotkeyResult\":{\"ok\":") + (result.empty() ? "true" : "false") + ",\"error\":" + quotedJson(result) + "}";
    view->PostWebMessageAsJson(wide(json + "}").c_str());
}
void publishUpdates(bool force = false) {
    if (!view || !ready || !updater) return;
    auto state = updater->status(force || (install && install->started()));
    if (!state) return;
    // A click on Cancel may arrive after the last network write but before the UI timer.
    if (state->phase == "downloaded" && cancelInstall) state->phase = "cancelled";
    if (state->phase == "downloaded") {
        try {
            if (!install) throw std::runtime_error(installError.empty() ? "请重新下载更新" : installError);
            if (!install->started()) install->start();
            else if (install->readyToExit()) PostMessageW(window, WM_CLOSE, 0, 0);
            state->phase = "installing";
        } catch (const std::exception& e) {
            installError = e.what(); install.reset();
            state->phase = "download-error"; state->error = installError;
        }
    } else if (state->phase == "download-error" || state->phase == "cancelled") install.reset();
    const auto json = "{\"updater\":{\"phase\":" + quotedJson(state->phase) +
        ",\"body\":" + quotedJson(state->body) + ",\"error\":" + quotedJson(state->error) +
        ",\"httpStatus\":" + std::to_string(state->httpStatus) +
        ",\"received\":" + std::to_string(state->received) + ",\"total\":" + std::to_string(state->total) +
        ",\"file\":" + quotedJson(state->file.u8string()) + "}}";
    view->PostWebMessageAsJson(wide(json).c_str());
}
void updatePromptResult(const string& phase, const string& error = {}) {
    if (!view || !ready) return;
    const auto json = "{\"updater\":{\"phase\":" + quotedJson(phase) + ",\"error\":" + quotedJson(error) + "}}";
    view->PostWebMessageAsJson(wide(json).c_str());
}
string collectionMode, collectionSource;
std::vector<Track> collectionPreview;
uint64_t collectionId = 0;
std::optional<nicomix::Mix> mixPreview, mixExport;
uint64_t mixToken = 0;
fs::path initialMix;
constexpr UINT exportMixMessage = WM_APP + 3, openMixMessage = WM_APP + 4;
void mixMessage(const string& key, const string& value) {
    view->PostWebMessageAsJson(wide("{" + quotedJson(key) + ":" + quotedJson(value) + "}").c_str());
}
void openMix(const fs::path& path) {
    try {
        if (!collectionMode.empty()) throw std::runtime_error("请先完成当前合集导入");
        // Do not open network shares, devices or alternate streams supplied by another process.
        const auto p = path.native();
        if (p.size()<4 || !iswalpha(p[0]) || p[1]!=L':' || (p[2]!=L'\\' && p[2]!=L'/') || p.find(L':',2)!=p.npos)
            throw std::runtime_error("请选择本机磁盘上的 .nicomix 文件");
        auto mix = nicomix::read(path);
        size_t unavailable = 0;
        for (const auto& song : mix.songs) if (!station->providers().listed(song.url)) ++unavailable;
        auto json = "{\"mixPreview\":{\"token\":" + quotedJson(std::to_string(++mixToken)) + ",\"name\":" + quotedJson(mix.name) +
            ",\"count\":" + std::to_string(mix.songs.size()) + ",\"unavailable\":" + std::to_string(unavailable) + "}}";
        mixPreview = std::move(mix); view->PostWebMessageAsJson(wide(json).c_str());
    } catch (const std::exception& e) { mixMessage("mixNotice", e.what()); }
}
void chooseMixDirectory() {
    if (!mixExport) return;
    auto mix = std::move(*mixExport); mixExport.reset();
    try {
        ComPtr<IFileOpenDialog> dialog; FILEOPENDIALOGOPTIONS opts{};
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))) ||
            FAILED(dialog->GetOptions(&opts)) || FAILED(dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR)))
            throw std::runtime_error("无法打开目录选择器");
        dialog->SetTitle(L"选择歌单导出目录");
        auto hr = dialog->Show(window); if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
        ComPtr<IShellItem> folder; PWSTR raw{};
        if (FAILED(hr) || FAILED(dialog->GetResult(&folder)) || FAILED(folder->GetDisplayName(SIGDN_FILESYSPATH, &raw)))
            throw std::runtime_error("无法取得导出目录");
        std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(raw,CoTaskMemFree);
        auto file = nicomix::write(fs::path(raw), mix);
        mixMessage("mixNotice", "已导出：" + file.filename().u8string());
    } catch (const std::exception& e) { mixMessage("mixNotice", e.what()); }
}
LibraryHistory libraryHistory;
string pendingLibraryEdit, pendingHistoryResult;
void publish(bool library, string err = {}, string imported = {}, bool queueChanged = false, bool importFailed = false,
             std::vector<int> importedTracks = {}) {
    if (!view || !ready)
        return;
    auto audioError = station->errors(&library);
    bool showPreview = false;
    if (!collectionMode.empty() && collectionPreview.empty()) {
        try {
            if (auto preview = station->previewCollection()) {
                collectionPreview = std::move(*preview);
                showPreview = true;
            }
        } catch (const std::exception &e) {
            collectionMode.clear();
            importFailed = true;
            err = e.what();
        }
    }
    auto *p = station->audio();
    int i = station->active();
    auto seconds = [&](const char *prop) {
        double v = i < 0 ? -1 : p->seconds(prop);
        return std::isfinite(v) ? std::to_string(static_cast<long long>(v)) : "-1";
    };
    string data = "{\"index\":" + std::to_string(i) + ",\"elapsed\":" + seconds("time-pos") +
                  ",\"duration\":" + seconds("duration") +
                  ",\"volume\":" + std::to_string(p ? p->integer("volume", 70) : 70) +
                  ",\"paused\":" + (p && p->str("pause") == "yes" ? "true" : "false") +
                  ",\"playing\":" + (i >= 0 && p->str("core-idle") == "no" ? "true" : "false") +
                  ",\"title\":" + quotedJson(i >= 0 ? p->str("media-title") : "");
    auto pos = station->queuePosition();
    data += ",\"repeat\":" + quotedJson(station->repeatMode()) +
            ",\"canPrev\":" + (station->adjacent(pos,-1) >= 0 ? "true" : "false") +
            ",\"canNext\":" + (station->adjacent(pos,1) >= 0 ? "true" : "false");
    if (library || queueChanged) {
        data += ",\"queue\":[";
        bool first = true;
        for (int index : station->playbackQueue()) {
            if (!first) data += ',';
            first = false;
            data += std::to_string(index);
        }
        data += ']';
    }
    if (library) {
        data += ",\"focusPresets\":[";
        for (size_t n = 0; n < focusPresets.size(); ++n) {
            if (n) data += ',';
            const auto& preset = focusPresets[n];
            data += "{\"id\":" + quotedJson(preset.id) + ",\"name\":" + quotedJson(preset.name) +
                ",\"revision\":" + quotedJson(focusVersions.at(preset.id)) + "}";
        }
        data += "],\"sources\":[";
        bool firstSource = true;
        for (const auto& s : station->providers().all()) {
            if (!firstSource) data += ',';
            firstSource = false;
            data += "{\"id\":" + quotedJson(s.id) + ",\"name\":" + quotedJson(s.name) +
                    ",\"hasHome\":" + (s.home.empty() ? "false" : "true") + ",\"extensions\":[";
            for (size_t n = 0; n < s.fileExtensions.size(); ++n) {
                if (n) data += ',';
                data += quotedJson(s.fileExtensions[n]);
            }
            data += "]}";
        }
        data += "],\"tracks\":[";
        bool first = true;
        for (auto &t : station->entries()) {
            if (!first)
                data += ',';
            first = false;
            if (!station->providers().listed(t.url)) {
                // URL keeps memberships and native indices intact; don't expose stale titles/covers.
                data += "{\"hidden\":true,\"url\":" + quotedJson(t.url) + "}";
                continue;
            }
            const auto source = station->providers().match(t.url);
            data += "{\"source\":" + quotedJson(source ? source->name : "") + ",\"url\":" + quotedJson(t.url) + ",\"name\":" + quotedJson(t.name) +
                    ",\"thumbnail\":" + quotedJson(t.thumbnail.value_or("")) + "}";
        }
        data += ']';
    }
    if (err.empty())
        err = audioError;
    if (!err.empty())
        data += ",\"error\":" + quotedJson(err);
    if (!collectionMode.empty()) data += ",\"collectionPending\":true";
    if (showPreview) data += ",\"collectionPreview\":{\"id\":" + quotedJson(std::to_string(collectionId)) +
        ",\"source\":" + quotedJson(collectionSource) + ",\"revision\":" + quotedJson(sourceVersion) + ",\"count\":" + std::to_string(collectionPreview.size()) +
        ",\"play\":" + (collectionMode == "start" ? "true}" : "false}");
    if (importFailed) data += ",\"importFailed\":true";
    if (!importedTracks.empty()) {
        data += ",\"importedUrls\":[";
        for (size_t n = 0; n < importedTracks.size(); ++n) {
            if (n) data += ',';
            data += quotedJson(station->entries()[importedTracks[n]].url);
        }
        data += ']';
    }
    if (!imported.empty())
        data += ",\"imported\":" + quotedJson(imported);
    if (!pendingLibraryEdit.empty()) data += ",\"libraryEdit\":" + pendingLibraryEdit;
    if (!pendingHistoryResult.empty()) data += ",\"historyResult\":" + pendingHistoryResult;
    view->PostWebMessageAsJson(wide(data + "}").c_str());
    pendingLibraryEdit.clear();
    pendingHistoryResult.clear();
}
// WebView2 keeps real file paths out of JavaScript. Resolve only the File
// explicitly passed by our own page after a drop or file-picker selection.
void selectedFile(ICoreWebView2WebMessageReceivedEventArgs *args) {
    ComPtr<ICoreWebView2WebMessageReceivedEventArgs2> ext;
    ComPtr<ICoreWebView2ObjectCollectionView> files;
    ComPtr<IUnknown> item;
    ComPtr<ICoreWebView2File> file;
    LPWSTR path{};
    if (SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&ext))) &&
        SUCCEEDED(ext->get_AdditionalObjects(&files)) && files &&
        SUCCEEDED(files->GetValueAtIndex(0, &item)) && item && SUCCEEDED(item.As(&file)) &&
        SUCCEEDED(file->get_Path(&path)) && path && *path) {
        if (nicomix::isFile(path)) openMix(path);
        else {
            auto json = wide("{\"localPath\":" + quotedJson(utf8(path)) + "}");
            view->PostWebMessageAsJson(json.c_str());
        }
    } else {
        view->PostWebMessageAsJson(L"{\"error\":\"无法取得文件路径，请粘贴本机文件的完整路径\"}");
    }
    CoTaskMemFree(path);
}
void handle(const string &message) {
    if ((message.size() > 12000 && message.rfind("playset ", 0) != 0 && message.rfind("removeurls ", 0) != 0 && !(message.size()<150000 && message.rfind("mix-export ",0)==0)) || message.find_first_of("\r\n") != string::npos)
        return;
    auto [cmd, arg] = split(trim(message));
    string result;
    bool changed = false;
    std::optional<std::vector<Track>> beforeEdit;
    string editKind;
    size_t editCount = 1;
    auto recordEdit = [&](bool success) {
        if (!beforeEdit) return;
        const auto id = libraryHistory.record(*beforeEdit, station->entries());
        if (success || !id.empty()) {
            if (editKind == "delete") editCount = beforeEdit->size() - station->entries().size();
            pendingLibraryEdit = "{\"id\":" + quotedJson(id) + ",\"kind\":" + quotedJson(editKind) +
                ",\"count\":" + std::to_string(editCount) + "}";
        }
        beforeEdit.reset();
    };
    try {
        if (cmd == "mix-export") {
            const auto tab = arg.find('\t');
            if (tab==string::npos) throw std::runtime_error("没有可分享的曲目");
            nicomix::Mix mix{arg.substr(0,tab), {}}; std::istringstream ids(arg.substr(tab+1));
            for (string id; ids >> id;) {
                const auto& song = station->entries().at(number(id,0,station->count()-1));
                if (!urlOK(song.url)) throw std::runtime_error("歌单不能包含本机路径");
                if (mix.songs.size()>=nicomix::maxTracks) throw std::runtime_error("最多可分享 10000 首");
                mix.songs.push_back({song.url,song.name});
            }
            mixExport = std::move(mix); PostMessageW(window, exportMixMessage,0,0); return;
        }
        if (cmd == "mix-open") { openMix(SourceRegistry::localFile(arg)); return; }
        if (cmd == "mix-cancel") { if (arg == std::to_string(mixToken)) mixPreview.reset(); return; }
        if (cmd == "mix-import") {
            if (!mixPreview || arg != std::to_string(mixToken) || !collectionMode.empty()) throw std::runtime_error("歌单预览已失效");
            std::vector<Track> songs;
            for (auto& s : mixPreview->songs) songs.push_back({s.url,s.title,std::nullopt});
            mixPreview.reset(); beforeEdit = station->entries(); editKind = "collection"; editCount = songs.size();
            auto order = station->commitCollection(songs); recordEdit(true);
            publish(true, {}, "nicomix", false, false, std::move(order)); return;
        }
        if (cmd == "history-apply") {
            const auto [id, direction] = split(arg);
            if (direction != "undo" && direction != "redo") return;
            string error;
            try { libraryHistory.apply(*station, id, direction == "undo"); }
            catch (const std::exception& e) { error = e.what(); }
            pendingHistoryResult = "{\"id\":" + quotedJson(id) + ",\"direction\":" + quotedJson(direction) +
                ",\"ok\":" + (error.empty() ? "true" : "false") + ",\"error\":" + quotedJson(error) + "}";
            publish(true);
            return;
        }
        if (cmd == "window") { windowAction(arg); return; }
        if (cmd == "open-data-folder") {
            const auto dir = libraryFile().parent_path();
            std::error_code ec;
            fs::create_directories(dir, ec);
            if (ec) throw std::runtime_error("无法创建数据文件夹，请检查目录权限");
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                throw std::runtime_error("无法打开数据文件夹，请检查目录是否可访问");
            return;
        }
        if (cmd == "hotkeys-capture") {
            if (!hotkeySettings) return;
            const auto [mode, token] = split(arg);
            if (mode != "on" && mode != "off") return;
            const int request = token.empty() ? 0 : number(token, 0, INT_MAX);
            hotkeySettings->capture(mode == "on" && GetForegroundWindow() == window);
            publishHotkeys({}, false, request);
            return;
        }
        if (cmd == "hotkeys-set") {
            if (!hotkeySettings) return;
            HotkeyBindings bindings{};
            std::istringstream input(arg);
            bool valid = true;
            for (auto& b : bindings) if (!(input >> b.modifiers >> b.key)) valid = false;
            string extra;
            if (input >> extra) valid = false;
            const string error = valid ? hotkeySettings->apply(bindings) : "热键设置格式无效";
            publishHotkeys(error, true);
            return;
        }
        if (cmd == "check-update") {
            if (!install) updater->check();
            publishUpdates();
            return;
        }
        if (cmd == "cancel-update") {
            if (install && !install->started()) { cancelInstall = true; updater->cancel(); publishUpdates(true); }
            return;
        }
        if (cmd == "download-update") {
            if (updater->busy() || install) { publishUpdates(true); return; }
            auto [url, rest] = split(arg);
            auto [size, details] = split(rest);
            auto [hash, name] = split(details);
            UpdateDownload asset;
            asset.url = url; asset.name = name; asset.sha256 = hash == "-" ? "" : hash;
            const auto parsed = std::from_chars(size.data(), size.data() + size.size(), asset.size);
            if (parsed.ec != std::errc{} || parsed.ptr != size.data() + size.size() || !Updater::installable(asset, NICOMUSIC_VERSION)) {
                updatePromptResult("download-error", "无效的更新安装包");
                return;
            }
            try {
                wchar_t module[32768];
                const auto length = GetModuleFileNameW(nullptr, module, 32768);
                if (!length || length >= 32768 || _wcsicmp(fs::path(module).filename().c_str(), L"NicoMusic.exe"))
                    throw std::runtime_error("原地更新需要保留主程序文件名 NicoMusic.exe");
                install = std::make_unique<UpdateInstall>(fs::path(module).parent_path(), asset.sha256);
                installError.clear(); cancelInstall = false;
                if (!updater->download(asset, install->package())) throw std::runtime_error("已有更新任务正在运行");
                publishUpdates();
            } catch (const std::exception& e) {
                install.reset(); updatePromptResult("download-error", e.what());
            }
            return;
        }
        if (cmd == "levels") {
            stopMeter();
            const auto [action, id] = split(arg);
            if ((action == "on" || action == "spectrum") && !hiddenToTray && station->active() >= 0 &&
                std::any_of(focusPresets.begin(), focusPresets.end(), [&](const auto& p) { return p.id == id; })) {
                meter = std::make_unique<AudioMeter>();
                if (!meter->start(action == "spectrum") || !SetTimer(window, 2, 32, nullptr)) meterUnavailable();
            }
            return;
        }
        if (cmd == "collection-decision") {
            const auto [id, choice] = split(arg);
            if (id != std::to_string(collectionId) || collectionPreview.empty() ||
                (choice != "accept" && choice != "cancel")) return;
            auto songs = std::move(collectionPreview);
            collectionPreview.clear();
            const string mode = collectionMode;
            collectionMode.clear();
            if (choice == "cancel") return;
            beforeEdit = station->entries();
            editKind = "collection";
            editCount = songs.size();
            auto order = station->commitCollection(songs);
            recordEdit(true);
            string playbackError;
            if (mode == "start") {
                try { station->playSelection(order.front(), order); }
                catch (const std::exception& e) { playbackError = e.what(); }
            }
            publish(true, playbackError, mode, false, false, std::move(order));
            return;
        }
        if (cmd == "ready") {
            mixPreview.reset();
            libraryHistory.clear();
            pendingLibraryEdit.clear();
            pendingHistoryResult.clear();
            // A reloaded page cannot confirm an old preview.
            if (!collectionMode.empty()) {
                station->engine().discardCollection();
                collectionPreview.clear();
                collectionMode.clear();
            }
            stopMeter();
            if (!ready) station->queueMissingCovers();
            ready = true;
            if (!arg.empty()) station->setRepeat(arg);
            publishHotkeys();
            view->PostWebMessageAsJson(wide("{\"appVersion\":" + quotedJson(NICOMUSIC_VERSION) + "}").c_str());
            publishUpdates(true);
            publishWindowState();
            if (!initialMix.empty()) PostMessageW(window,openMixMessage,0,0);
            changed = true;
        } else if (cmd == "start" || cmd == "add") {
            if (!collectionMode.empty()) throw std::runtime_error("正在导入另一个合集");
            if (station->beginCollection(arg)) {
                collectionMode = cmd;
                ++collectionId;
                collectionSource = station->providers().match(split(arg).first)->id;
                publish(false);
                return;
            }
            beforeEdit = station->entries();
            editKind = "import";
            int i = station->add(arg);
            changed = true;
            if (cmd == "start")
                station->play(i, true); // Desktop lists show newest imports first.
            result = cmd;
        } else if (cmd == "play")
            station->play(number(arg, 0, station->count() - 1), true);
        else if (cmd == "playqueued") station->playQueued(arg);
        else if (cmd == "playset") {
            auto [start, indices] = split(arg);
            std::istringstream stream(indices);
            std::vector<int> order;
            std::vector<bool> seen(station->count());
            for (string token; stream >> token;) {
                int index = number(token, 0, station->count() - 1);
                if (seen[index]) throw std::runtime_error("歌单中存在重复曲目");
                seen[index] = true;
                order.push_back(index);
            }
            station->playSelection(number(start, 0, station->count() - 1), order);
        } else if (cmd == "next" || cmd == "prev") station->step(cmd == "next" ? 1 : -1);
        else if (cmd == "repeat") station->setRepeat(arg);
        else if (cmd == "remove") {
            beforeEdit = station->entries();
            editKind = "delete";
            station->erase(number(arg, 0, station->count() - 1));
            changed = true;
        } else if (cmd == "removeurls") {
            beforeEdit = station->entries();
            editKind = "delete";
            std::unordered_set<string> urls;
            std::istringstream input(arg);
            for (string url; input >> url;) urls.insert(url);
            station->eraseUrls(urls);
            changed = true;
        } else if (cmd == "volume") {
            auto v = std::to_string(number(arg, 0, 100));
            station->engine().command({"set", "volume", v.c_str()});
        } else if (cmd == "volume-step") {
            const int delta = number(arg, -100, 100);
            auto& audio = station->engine();
            const auto current = std::clamp<int64_t>(audio.integer("volume", 70), 0, 100);
            const auto value = std::to_string(std::clamp<int64_t>(current + delta, 0, 100));
            // Read the engine, not the last UI snapshot, so rapid repeats accumulate.
            audio.command({"set", "volume", value.c_str()});
        } else if (cmd == "pause" && station->active() >= 0)
            station->engine().command({"cycle", "pause"});
        else if (cmd == "seek" && station->active() >= 0) {
            number(arg, 0, INT_MAX);
            station->engine().command({"seek", arg.c_str(), "absolute"});
        } else if (cmd == "source") {
            const auto s = station->providers().find(arg);
            if (!s) throw std::runtime_error("来源插件未安装");
            browse(s->home);
        } else if (cmd == "project") {
            browse("https://github.com/lenzo15/Nico-Music");
        }
        recordEdit(true);
        publish(changed, {}, result, cmd == "play" || cmd == "playset");
    } catch (const std::exception &e) {
        recordEdit(false);
        publish(true, e.what(), {}, false, cmd == "add" || cmd == "start" || cmd == "collection-decision" || cmd == "mix-import");
    }
}
void resize() {
    if (controller) {
        RECT r;
        GetClientRect(window, &r);
        controller->put_Bounds(r);
    }
}
void failure(LPCWSTR reason) {
    MessageBoxW(window, reason, L"NicoMusic", MB_OK | MB_ICONERROR);
    PostMessageW(window, WM_CLOSE, 0, 0);
}
void applyPlugins(PluginSnapshot snapshot, bool initial) {
    // Unique asset URLs even across desktop restarts and remove/reinstall cycles.
    static auto serial = std::chrono::system_clock::now().time_since_epoch().count();
    const auto version = std::to_string(++serial);
    const bool sourcesChanged = snapshot.sourceFiles != appliedSources;
    string error;
    bool cancelled = false;
    if (sourcesChanged || initial) {
        cancelled = !collectionMode.empty();
        collectionMode.clear(); collectionPreview.clear(); ++collectionId;
        try { station->reloadSources(std::move(snapshot.sources), !initial); }
        catch (const std::exception& e) { error = e.what(); }
        appliedSources = std::move(snapshot.sourceFiles); sourceVersion = version;
        if (cancelled) error = "来源插件已更新，请重新导入合集";
    }
    for (auto it = focusVersions.begin(); it != focusVersions.end();) {
        if (snapshot.focusFiles.count(it->first)) { ++it; continue; }
        it = focusVersions.erase(it);
    }
    focusPresets.clear();
    for (auto& p : snapshot.presets) {
        if (!focusVersions.count(p.id) || appliedFocus.at(p.id) != snapshot.focusFiles.at(p.id)) focusVersions[p.id] = version;
        focusPresets.push_back(std::move(p));
    }
    appliedFocus = std::move(snapshot.focusFiles);
    if (!initial) publish(true, error, {}, false, cancelled);
}
void pollPlugins() {
    try {
        bool retry = false;
        if (pluginScan.valid()) {
            if (pluginScan.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
            auto scan = pluginScan.get();
            if (scan.sourceFiles == appliedSources && scan.focusFiles == appliedFocus) pendingPlugins.reset();
            else if (pendingPlugins && scan.sourceFiles == pendingPlugins->sourceFiles && scan.focusFiles == pendingPlugins->focusFiles) {
                pendingPlugins.reset();
                applyPlugins(std::move(scan), false);
            } else { pendingPlugins = std::move(scan); retry = true; }
        }
        const auto root = appDir() / L"plugins";
        if (pluginWatch != INVALID_HANDLE_VALUE && WaitForSingleObject(pluginWatch, 0) != WAIT_TIMEOUT) {
            retry = true;
            if (!FindNextChangeNotification(pluginWatch)) {
                FindCloseChangeNotification(pluginWatch); pluginWatch = INVALID_HANDLE_VALUE;
            }
        }
        // Rebind occasionally in case the watched root itself was renamed/replaced.
        static ULONGLONG lastBind = 0;
        if (pluginWatch == INVALID_HANDLE_VALUE || GetTickCount64() - lastBind > 15000) {
            if (pluginWatch != INVALID_HANDLE_VALUE) FindCloseChangeNotification(pluginWatch);
            pluginWatch = FindFirstChangeNotificationW(root.c_str(), TRUE, FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
            lastBind = GetTickCount64(); retry = true;
        }
        if (retry || pendingPlugins) pluginScan = std::async(std::launch::async, [root] { return PluginSnapshot(root); });
    } catch (const std::exception&) { pendingPlugins.reset(); } // Copy/rename in progress: retry next tick.
}
HRESULT setup(ICoreWebView2Controller *c) {
    controller = c;
    // Do not let the empty browser surface cover the native launch animation.
    HRESULT hide = controller->put_IsVisible(FALSE);
    if (FAILED(hide)) return hide;
    ComPtr<ICoreWebView2Controller2> c2;
    if (SUCCEEDED(controller.As(&c2))) {
        const auto bg = StartupSplash::paper;
        c2->put_DefaultBackgroundColor({255, BYTE(bg & 255), BYTE((bg >> 8) & 255), BYTE((bg >> 16) & 255)});
    }
    if (FAILED(c->get_CoreWebView2(&view)))
        return E_FAIL;
    ComPtr<ICoreWebView2_3> v3;
    if (FAILED(view.As(&v3)))
        return E_NOINTERFACE;
    HRESULT hr =
        v3->SetVirtualHostNameToFolderMapping(L"studio.bilimusic.local", (appDir() / L"ui").c_str(),
                                              COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
    if (FAILED(hr))
        return hr;
    ComPtr<ICoreWebView2Environment> env;
    if (FAILED(v3->get_Environment(&env))) return E_FAIL;
    ComPtr<ICoreWebView2_22> v22;
    view.As(&v22);
    for (auto pattern : {L"https://*.focus.bilimusic.local/*", L"https://*.collection.bilimusic.local/*"}) {
        hr = v22 ? v22->AddWebResourceRequestedFilterWithRequestSourceKinds(pattern, COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL,
                    COREWEBVIEW2_WEB_RESOURCE_REQUEST_SOURCE_KINDS_ALL) :
                    view->AddWebResourceRequestedFilter(pattern, COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        if (FAILED(hr)) return hr;
    }
    EventRegistrationToken resourceToken{};
    hr = view->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>(
        [env](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
            ComPtr<ICoreWebView2WebResourceRequest> request;
            LPWSTR raw{};
            if (FAILED(args->get_Request(&request)) || FAILED(request->get_Uri(&raw))) return E_FAIL;
            std::wstring url(raw); CoTaskMemFree(raw);
            ComPtr<IStream> stream;
            std::wstring headers = L"Cache-Control: no-store\r\nX-Content-Type-Options: nosniff";
            try {
                auto path = pluginResource(url, appliedFocus, appliedSources, appDir() / L"plugins");
                if (!path.empty() && SUCCEEDED(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_NONE,
                        FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &stream))) headers += std::wstring(L"\r\nContent-Type: ") + pluginMime(path);
            } catch (const std::exception&) {} // Reject malformed or disappearing assets locally.
            ComPtr<ICoreWebView2WebResourceResponse> response;
            auto result = env->CreateWebResourceResponse(stream.Get(), stream ? 200 : 404, stream ? L"OK" : L"Not Found", headers.c_str(), &response);
            return FAILED(result) ? result : args->put_Response(response.Get());
        }).Get(), &resourceToken);
    if (FAILED(hr)) return hr;
    try { applyPlugins(PluginSnapshot(appDir() / L"plugins"), true); }
    catch (const std::filesystem::filesystem_error&) { /* Retry after an in-progress install. */ }
    SetTimer(window, 5, 750, nullptr);
    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(view->get_Settings(&settings))) {
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
        ComPtr<ICoreWebView2Settings9> frameSettings;
        nativeDragRegion = SUCCEEDED(settings.As(&frameSettings)) &&
            SUCCEEDED(frameSettings->put_IsNonClientRegionSupportEnabled(TRUE));
    }
    EventRegistrationToken token{};
    view->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) {
                LPWSTR uri{};
                args->get_Uri(&uri);
                if (!uri || wcscmp(uri, page) != 0)
                    args->put_Cancel(TRUE);
                else if (hotkeySettings && hotkeySettings->capturing()) hotkeySettings->capture(false);
                CoTaskMemFree(uri);
                return S_OK;
            })
            .Get(),
        &token);
    hr = view->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) {
                if (!splash.visible()) return S_OK;
                BOOL ok = FALSE;
                args->get_IsSuccess(&ok);
                if (!ok) {
                    COREWEBVIEW2_WEB_ERROR_STATUS status{};
                    args->get_WebErrorStatus(&status);
                    if (status != COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED)
                        failure(L"无法加载播放器界面，请检查 ui 目录。");
                    return S_OK;
                }
                splash.finish(window);
                if (!hiddenToTray) controller->put_IsVisible(TRUE);
                return S_OK;
            }).Get(), &token);
    if (FAILED(hr)) return hr;
    view->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) {
                args->put_Handled(TRUE);
                return S_OK;
            })
            .Get(),
        &token);
    view->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) {
                LPWSTR from{}, msg{};
                args->get_Source(&from);
                if (from && wcscmp(from, page) == 0 && SUCCEEDED(args->TryGetWebMessageAsString(&msg)) &&
                    msg) {
                    if (wcscmp(msg, L"selectedFile") == 0) selectedFile(args);
                    else handle(utf8(msg));
                }
                CoTaskMemFree(from);
                CoTaskMemFree(msg);
                return S_OK;
            })
            .Get(),
        &token);
    resize();
    SetTimer(window, 1, 400, nullptr);
    return view->Navigate(page);
}
LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (taskbarCreated && m == taskbarCreated) {
        trayRegistered = false;
        if (!ensureTrayIcon(h) && hiddenToTray) restoreWindow(h);
        return 0;
    }
    switch (m) {
    case WM_HOTKEY:
        if (hotkeySettings && w >= 1 && w <= 3 && hotkeySettings->registered()[w - 1]) {
            constexpr const char* actions[] = {"prev", "next", "toggle"};
            postPlaybackShortcut(actions[w - 1]);
            return 0;
        }
        for (const auto& key : playbackHotkeys) {
            if (!key.registered || w != static_cast<WPARAM>(key.id)) continue;
            postPlaybackShortcut(key.action);
            break;
        }
        return 0;
    case WM_APPCOMMAND: {
        // Some keyboard/headset drivers emit application commands instead of
        // virtual-key events. Consume handled commands so they don't reach the shell.
        const char* action = nullptr;
        switch (GET_APPCOMMAND_LPARAM(l)) {
        case APPCOMMAND_MEDIA_PLAY_PAUSE: action = "toggle"; break;
        case APPCOMMAND_MEDIA_PREVIOUSTRACK: action = "prev"; break;
        case APPCOMMAND_MEDIA_NEXTTRACK: action = "next"; break;
        case APPCOMMAND_VOLUME_UP: action = "volumeUp"; break;
        case APPCOMMAND_VOLUME_DOWN: action = "volumeDown"; break;
        }
        if (action && postPlaybackShortcut(action)) return TRUE;
        break;
    }
    case WM_ERASEBKGND:
        if (splash.visible()) return 1;
        break;
    case WM_PAINT:
        if (splash.visible()) { splash.paint(h); return 0; }
        break;
    case WM_SYSCOMMAND:
        if ((w & 0xfff0) == SC_MINIMIZE) {
            if (hiddenToTray) return 0;
            if (!ensureTrayIcon(h)) break; // normal taskbar minimize if the shell is unavailable
            hiddenToTray = true;
            if (controller) controller->put_IsVisible(FALSE);
            ShowWindow(h, SW_HIDE);
            return 0;
        }
        break;
    case exportMixMessage: chooseMixDirectory(); return 0;
    case openMixMessage:
        if (ready && !initialMix.empty()) { auto path = std::move(initialMix); initialMix.clear(); openMix(path); }
        return 0;
    case WM_COPYDATA: {
        const auto data = reinterpret_cast<const COPYDATASTRUCT*>(l);
        if (!data || data->dwData!=openMixMessage || !data->lpData || data->cbData<4 || data->cbData>65536 || data->cbData%sizeof(wchar_t)) return FALSE;
        auto p = static_cast<const wchar_t*>(data->lpData); auto len = data->cbData/sizeof(wchar_t);
        if (wcsnlen_s(p,len)!=len-1 || !nicomix::isFile(p)) return FALSE;
        initialMix = p; if (ready) PostMessageW(h,openMixMessage,0,0); return TRUE;
    }
    case activateWindow:
        restoreWindow(h);
        return 0;
    case trayCallback:
        if (trayRegistered && HIWORD(l) == trayId) {
            if (LOWORD(l) == NIN_SELECT || LOWORD(l) == NIN_KEYSELECT) restoreWindow(h);
            else if (LOWORD(l) == WM_CONTEXTMENU) trayMenu(h, {GET_X_LPARAM(w), GET_Y_LPARAM(w)});
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE && hotkeySettings && hotkeySettings->capturing()) {
            hotkeySettings->capture(false);
            publishHotkeys();
        }
        break;
    case WM_SIZE:
        if (w != SIZE_MINIMIZED) {
            if (splash.visible()) InvalidateRect(h, nullptr, FALSE);
            resize();
            publishWindowState();
        }
        return 0;
    case WM_MOVE:
        if (controller) controller->NotifyParentWindowPositionChanged();
        break;
    case WM_TIMER:
        if (w == 1) { publish(false); publishUpdates(); }
        else if (w == 2) meterTick();
        else if (w == 5) pollPlugins();
        else if (w == trayRetryTimer) ensureTrayIcon(h);
        else if (w == StartupSplash::timer) splash.tick(h);
        return 0;
    case WM_NCCALCSIZE: {
        if (IsZoomed(h)) return 0;
        // Keep native side/bottom resize borders, but put the top edge inside
        // the WebView so dialog backdrops also cover the former frame strip.
        RECT *client = w ? &reinterpret_cast<NCCALCSIZE_PARAMS *>(l)->rgrc[0]
                         : reinterpret_cast<RECT *>(l);
        const LONG top = client->top;
        const LRESULT result = DefWindowProcW(h, m, w, l);
        client->top = top;
        return result;
    }
    case WM_NCHITTEST: {
        // The top resize border is now client area; restore its hit targets.
        if (!IsZoomed(h)) {
            RECT r{};
            GetWindowRect(h, &r);
            const auto dpi = GetDpiForWindow(h);
            const int padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            const int borderX = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + padding;
            const int borderY = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + padding;
            const POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            if (PtInRect(&r, pt) && pt.y < r.top + borderY) {
                if (pt.x < r.left + borderX) return HTTOPLEFT;
                if (pt.x >= r.right - borderX) return HTTOPRIGHT;
                return HTTOP;
            }
        }
        break;
    }
    case WM_GETMINMAXINFO: {
        auto dpi = GetDpiForWindow(h);
        auto *v = reinterpret_cast<MINMAXINFO *>(l);
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &monitor)) {
            const RECT& work = monitor.rcWork;
            v->ptMaxPosition = {work.left - monitor.rcMonitor.left, work.top - monitor.rcMonitor.top};
            v->ptMaxSize = {work.right - work.left, work.bottom - work.top};
        }
        v->ptMinTrackSize = {MulDiv(610, dpi, 96), MulDiv(445, dpi, 96)};
        return 0;
    }
    case WM_DPICHANGED: {
        auto *r = reinterpret_cast<RECT *>(l);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        if (updater) updater->cancel();
        hotkeySettings.reset();
        unregisterPlaybackHotkeys(h);
        splash.finish(h);
        RemovePropW(h, primaryMarker);
        if (activationWait) {
            UnregisterWaitEx(activationWait, INVALID_HANDLE_VALUE);
            activationWait = nullptr;
        }
        ready = false;
        removeTrayIcon(h);
        KillTimer(h, 1);
        KillTimer(h, 5);
        if (pluginWatch != INVALID_HANDLE_VALUE) { FindCloseChangeNotification(pluginWatch); pluginWatch = INVALID_HANDLE_VALUE; }
        stopMeter();
        if (controller)
            controller->Close();
        view.Reset();
        controller.Reset();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc{}; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool registerOnly = argv && argc == 2 && !wcscmp(argv[1], L"--register-nicomix");
    if (argv && argc==2 && nicomix::isFile(argv[1])) {
        std::error_code ec; initialMix = fs::absolute(argv[1],ec); if (ec) initialMix.clear();
    }
    LocalFree(argv);
    if (registerOnly) return nicomix::registerAssociation(appExecutable().native()) ? 0 : 1;
    HANDLE raw = CreateEventW(nullptr, FALSE, FALSE, instanceEvent);
    const bool alreadyRunning = GetLastError() == ERROR_ALREADY_EXISTS;
    std::unique_ptr<void, decltype(&CloseHandle)> activation(raw, CloseHandle);
    if (!activation) {
        MessageBoxW(nullptr, L"无法检查播放器是否已运行。", L"NicoMusic", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (alreadyRunning) {
        if (!initialMix.empty()) {
            HWND dest{};
            for (int i=0;i<40 && !dest;++i) {
                for (HWND h=nullptr;(h=FindWindowExW(nullptr,h,windowClass,nullptr));)
                    if (GetPropW(h,primaryMarker)) { dest=h; break; }
                if (!dest) Sleep(50);
            }
            COPYDATASTRUCT data{openMixMessage, static_cast<DWORD>((initialMix.native().size()+1)*sizeof(wchar_t)), const_cast<wchar_t*>(initialMix.c_str())};
            DWORD_PTR result{};
            if (!dest || !SendMessageTimeoutW(dest,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&data),SMTO_ABORTIFHUNG,2000,&result) || !result) return 1;
        }
        handOffForeground();
        return SetEvent(activation.get()) ? 0 : 1;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 1;
    int code = 0;
    try {
        if (!fs::exists(appDir() / L"ui/index.html"))
            throw std::runtime_error("缺少 ui 目录，请将界面资源放在程序旁边。");
        if (envPath(L"NICOMUSIC_HOME").empty()) // Isolated profiles must not claim shell integration.
            nicomix::registerAssociation(appExecutable().native());
        WNDCLASSW wc{};
        wc.hInstance = instance;
        wc.lpszClassName = windowClass;
        wc.lpfnWndProc = wndProc;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        RegisterClassW(&wc);
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        auto dpi = GetDpiForSystem();
        int width = std::min<int>(MulDiv(1180, dpi, 96), work.right - work.left),
            height = std::min<int>(MulDiv(776, dpi, 96), work.bottom - work.top);
        window = CreateWindowW(
            wc.lpszClassName, L"NicoMusic", WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, (work.right + work.left - width) / 2,
            (work.bottom + work.top - height) / 2, width, height, nullptr, nullptr, instance, nullptr);
        if (!window)
            throw std::runtime_error("窗口创建失败");
        if (!RegisterWaitForSingleObject(&activationWait, activation.get(), onActivation, window, INFINITE, WT_EXECUTEDEFAULT))
            throw std::runtime_error("无法监听播放器唤醒请求");
        SetPropW(window, primaryMarker, reinterpret_cast<HANDLE>(1));
        splash.start(window);
        ShowWindow(window, show);
        UpdateWindow(window); // first frame, before library/plugin discovery or WebView2 initialization
        ensureTrayIcon(window);
        updater = std::make_unique<Updater>();
        station = std::make_unique<Station>();
        hotkeySettings = std::make_unique<HotkeySettings>(libraryFile().parent_path() / L"hotkeys.ini",
            [](int id, const HotkeyBinding& binding) {
                return RegisterHotKey(window, id, binding.modifiers | MOD_NOREPEAT, binding.key) != FALSE;
            }, [](int id) { UnregisterHotKey(window, id); });
        registerPlaybackHotkeys(window);
        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, (libraryFile().parent_path() / L"WebView2").c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [](HRESULT hr, ICoreWebView2Environment *env) {
                    if (SUCCEEDED(hr) && env) {
                        hr = env->CreateCoreWebView2Controller(
                            window, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                                        [](HRESULT hr, ICoreWebView2Controller *c) {
                                            if (FAILED(hr) || !c || FAILED(setup(c)))
                                                failure(L"无法初始化界面，请更新 WebView2 Runtime。");
                                            return S_OK;
                                        })
                                        .Get());
                    }
                    if (FAILED(hr) || !env)
                        failure(L"无法初始化 WebView2，请检查 Runtime 和数据目录。");
                    return S_OK;
                })
                .Get());
        if (FAILED(hr))
            failure(L"需要 Microsoft Edge WebView2 Runtime。请安装后重试。");
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    } catch (const std::exception &e) {
        MessageBoxW(nullptr, wide(e.what()).c_str(), L"NicoMusic", MB_OK | MB_ICONERROR);
        code = 1;
    }
    if (IsWindow(window)) removeTrayIcon(window); // startup exception before WM_DESTROY
    hotkeySettings.reset();
    unregisterPlaybackHotkeys(window);
    updater.reset(); // join the download worker before cleaning staging files
    install.reset();
    station.reset();
    CoUninitialize();
    return code;
}
