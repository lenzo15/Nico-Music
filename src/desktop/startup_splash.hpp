#pragma once
#include <windows.h>
#include <cmath>

// Draw directly into the host: HTML/CSS cannot cover WebView2's cold start.
class StartupSplash {
    bool active = true;
    BOOL motion = FALSE;
    ULONGLONG started = 0;
    static int scale(HWND h, int dip) { return MulDiv(dip, GetDpiForWindow(h), 96); }
    static RECT bars(HWND h, const RECT& client) {
        const LONG x = client.right / 2, y = client.bottom / 2 + scale(h, 54);
        return {x - scale(h, 24), y - scale(h, 12), x + scale(h, 24), y + scale(h, 12)};
    }
public:
    static constexpr UINT_PTR timer = 3;
    static constexpr COLORREF paper = RGB(251, 248, 249);
    bool visible() const { return active; }
    void start(HWND h) {
        started = GetTickCount64();
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &motion, 0);
        if (motion) SetTimer(h, timer, 40, nullptr);
    }
    void tick(HWND h) const {
        if (!active || !IsWindowVisible(h) || IsIconic(h)) return;
        RECT client{}; GetClientRect(h, &client);
        const RECT r = bars(h, client);
        InvalidateRect(h, &r, FALSE);
    }
    void finish(HWND h) {
        active = false;
        KillTimer(h, timer);
    }
    void paint(HWND h) const {
        PAINTSTRUCT ps{};
        HDC dest = BeginPaint(h, &ps);
        RECT client{}; GetClientRect(h, &client);
        const RECT& dirty = ps.rcPaint;
        const int w = dirty.right - dirty.left, ht = dirty.bottom - dirty.top;
        if (w && ht) {
            HDC dc = CreateCompatibleDC(dest);
            HBITMAP bitmap = CreateCompatibleBitmap(dest, w, ht);
            if (dc && bitmap) {
                auto prev = SelectObject(dc, bitmap);
                SetViewportOrgEx(dc, -dirty.left, -dirty.top, nullptr);
                auto brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
                SetDCBrushColor(dc, paper); FillRect(dc, &dirty, brush);
                const int cx = client.right / 2, cy = client.bottom / 2;
                const RECT strip = bars(h, client);
                if (!EqualRect(&dirty, &strip)) {
                    const int size = scale(h, 58);
                    auto icon = reinterpret_cast<HICON>(GetClassLongPtrW(h, GCLP_HICON));
                    DrawIconEx(dc, cx - size / 2, cy - scale(h, 70), icon, size, size, 0, nullptr, DI_NORMAL);
                    HFONT font = CreateFontW(-scale(h, 25), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH, L"Segoe UI");
                    auto old = SelectObject(dc, font);
                    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(42, 38, 44));
                    RECT title{0, cy, client.right, cy + scale(h, 35)};
                    DrawTextW(dc, L"NicoMusic", -1, &title, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(dc, old); DeleteObject(font);
                }
                SelectObject(dc, brush); SelectObject(dc, GetStockObject(NULL_PEN));
                SetDCBrushColor(dc, RGB(212, 77, 93));
                const double phase = motion ? (GetTickCount64() - started) % 1200 / 1200.0 * 6.2831853 : 0;
                for (int i = 0; i < 5; ++i) {
                    const int x = cx + scale(h, (i-2)*8), y = (strip.top + strip.bottom) / 2;
                    const int half = scale(h, static_cast<int>(6 + 3 * std::cos(phase - (i-2)*0.9)));
                    const int width = scale(h, 4);
                    RoundRect(dc, x - width/2, y - half, x - width/2 + width, y + half, width, width);
                }
                BitBlt(dest, dirty.left, dirty.top, w, ht, dc, dirty.left, dirty.top, SRCCOPY);
                SelectObject(dc, prev);
            }
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
        }
        EndPaint(h, &ps);
    }
};
