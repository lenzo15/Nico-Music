#include "station.hpp"
#include <ole2.h>
#include <shellapi.h>
#include <wrl.h>
#include <WebView2.h>
#include <windowsx.h>
#include "audio_meter.hpp"
#include "focus_presets.hpp"
#include "startup_splash.hpp"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace {
constexpr wchar_t page[] = L"https://studio.bilimusic.local/index.html";
HWND window{};
ComPtr<ICoreWebView2Controller> controller;
ComPtr<ICoreWebView2> view;
std::unique_ptr<Station> station;
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
bool inTray = false;

NOTIFYICONDATAW trayData(HWND h) {
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = h;
    icon.uID = trayId;
    return icon;
}
bool addTrayIcon(HWND h) {
    auto icon = trayData(h);
    icon.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    icon.hIcon = reinterpret_cast<HICON>(GetClassLongPtrW(h, GCLP_HICON));
    icon.uCallbackMessage = trayCallback;
    wcscpy_s(icon.szTip, L"NicoMusic · 点击恢复播放器");
    if (!Shell_NotifyIconW(NIM_ADD, &icon)) return false;
    icon.uVersion = NOTIFYICON_VERSION_4;
    if (Shell_NotifyIconW(NIM_SETVERSION, &icon)) return true;
    Shell_NotifyIconW(NIM_DELETE, &icon);
    return false;
}
void removeTrayIcon(HWND h) {
    if (!inTray) return;
    inTray = false;
    auto icon = trayData(h);
    Shell_NotifyIconW(NIM_DELETE, &icon);
}
void restoreWindow(HWND h) {
    removeTrayIcon(h);
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
    if (!meter || !view || inTray || IsIconic(window)) return;
    std::array<int, 3> levels{};
    switch (meter->poll(levels)) {
    case AudioMeter::Result::waiting: return;
    case AudioMeter::Result::failed: meterUnavailable(); return;
    case AudioMeter::Result::fresh:
        auto json = L"{\"levels\":[" + std::to_wstring(levels[0]) + L"," +
            std::to_wstring(levels[1]) + L"," + std::to_wstring(levels[2]) + L"]}";
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
void publish(bool library, string err = {}, const string &imported = {}, bool queueChanged = false) {
    if (!view || !ready)
        return;
    auto audioError = station->errors(&library);
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
            data += "{\"id\":" + quotedJson(preset.id) + ",\"name\":" + quotedJson(preset.name) + "}";
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
    if (!imported.empty())
        data += ",\"imported\":" + quotedJson(imported);
    view->PostWebMessageAsJson(wide(data + "}").c_str());
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
        auto json = wide("{\"localPath\":" + quotedJson(utf8(path)) + "}");
        view->PostWebMessageAsJson(json.c_str());
    } else {
        view->PostWebMessageAsJson(L"{\"error\":\"无法取得文件路径，请粘贴本机文件的完整路径\"}");
    }
    CoTaskMemFree(path);
}
void handle(const string &message) {
    if ((message.size() > 12000 && message.rfind("playset ", 0) != 0 && message.rfind("removeurls ", 0) != 0) || message.find_first_of("\r\n") != string::npos)
        return;
    auto [cmd, arg] = split(trim(message));
    string result;
    bool changed = false;
    try {
        if (cmd == "window") { windowAction(arg); return; }
        if (cmd == "levels") {
            stopMeter();
            const auto [action, id] = split(arg);
            if (action == "on" && !inTray && station->active() >= 0 &&
                std::any_of(focusPresets.begin(), focusPresets.end(), [&](const auto& p) { return p.id == id; })) {
                meter = std::make_unique<AudioMeter>();
                if (!meter->start() || !SetTimer(window, 2, 32, nullptr)) meterUnavailable();
            }
            return;
        }
        if (cmd == "ready") {
            stopMeter();
            if (!ready) station->queueMissingCovers();
            ready = true;
            if (!arg.empty()) station->setRepeat(arg);
            publishWindowState();
            changed = true;
        } else if (cmd == "start" || cmd == "add") {
            int i = station->add(arg);
            changed = true;
            if (cmd == "start")
                station->play(i);
            result = cmd;
        } else if (cmd == "play")
            station->play(number(arg, 0, station->count() - 1));
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
            station->erase(number(arg, 0, station->count() - 1));
            changed = true;
        } else if (cmd == "removeurls") {
            std::unordered_set<string> urls;
            std::istringstream input(arg);
            for (string url; input >> url;) urls.insert(url);
            station->eraseUrls(urls);
            changed = true;
        } else if (cmd == "volume") {
            auto v = std::to_string(number(arg, 0, 100));
            station->engine().command({"set", "volume", v.c_str()});
        } else if (cmd == "pause" && station->active() >= 0)
            station->engine().command({"cycle", "pause"});
        else if (cmd == "seek" && station->active() >= 0) {
            number(arg, 0, INT_MAX);
            station->engine().command({"seek", arg.c_str(), "absolute"});
        } else if (cmd == "source") {
            const auto s = station->providers().find(arg);
            if (!s) throw std::runtime_error("来源插件未安装");
            browse(s->home);
        }
        publish(changed, {}, result, cmd == "play" || cmd == "playset");
    } catch (const std::exception &e) {
        publish(true, e.what());
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
    for (const auto& preset : findFocusPresets(appDir() / L"plugins")) {
        auto host = wide(preset.id + ".focus.bilimusic.local");
        if (SUCCEEDED(v3->SetVirtualHostNameToFolderMapping(host.c_str(), preset.folder.c_str(),
                      COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS))) focusPresets.push_back(preset);
    }
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
                if (!inTray) controller->put_IsVisible(TRUE);
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
    if (taskbarCreated && m == taskbarCreated && inTray) {
        // Explorer rebuilt its notification area. Never strand a hidden window.
        if (!addTrayIcon(h)) restoreWindow(h);
        return 0;
    }
    switch (m) {
    case WM_ERASEBKGND:
        if (splash.visible()) return 1;
        break;
    case WM_PAINT:
        if (splash.visible()) { splash.paint(h); return 0; }
        break;
    case WM_SYSCOMMAND:
        if ((w & 0xfff0) == SC_MINIMIZE) {
            if (inTray) return 0;
            if (!addTrayIcon(h)) break; // normal taskbar minimize if the shell is unavailable
            inTray = true;
            if (controller) controller->put_IsVisible(FALSE);
            ShowWindow(h, SW_HIDE);
            return 0;
        }
        break;
    case activateWindow:
        restoreWindow(h);
        return 0;
    case trayCallback:
        if (inTray && HIWORD(l) == trayId) {
            if (LOWORD(l) == NIN_SELECT || LOWORD(l) == NIN_KEYSELECT) restoreWindow(h);
            else if (LOWORD(l) == WM_CONTEXTMENU) trayMenu(h, {GET_X_LPARAM(w), GET_Y_LPARAM(w)});
        }
        return 0;
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
        if (w == 1) publish(false);
        else if (w == 2) meterTick();
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
        splash.finish(h);
        RemovePropW(h, primaryMarker);
        if (activationWait) {
            UnregisterWaitEx(activationWait, INVALID_HANDLE_VALUE);
            activationWait = nullptr;
        }
        ready = false;
        removeTrayIcon(h);
        KillTimer(h, 1);
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
    HANDLE raw = CreateEventW(nullptr, FALSE, FALSE, instanceEvent);
    const bool alreadyRunning = GetLastError() == ERROR_ALREADY_EXISTS;
    std::unique_ptr<void, decltype(&CloseHandle)> activation(raw, CloseHandle);
    if (!activation) {
        MessageBoxW(nullptr, L"无法检查播放器是否已运行。", L"NicoMusic", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (alreadyRunning) {
        handOffForeground();
        return SetEvent(activation.get()) ? 0 : 1;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 1;
    int code = 0;
    try {
        if (!fs::exists(appDir() / L"ui/index.html"))
            throw std::runtime_error("缺少 ui 目录，请将界面资源放在程序旁边。");
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
        station = std::make_unique<Station>();
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
    station.reset();
    CoUninitialize();
    return code;
}
