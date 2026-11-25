"""Windows frame regression: python tests/desktop_frame.py build/NicoMusic.exe
Requires websocket-client. Uses a temporary profile; never closes other instances.
"""
import ctypes
from ctypes import wintypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import shutil
import threading
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

import websocket
from tray import check_tray_restore, desktop_running, tray_registered


user32 = ctypes.windll.user32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.SendMessageW.restype = wintypes.LPARAM


def until(check, timeout=12):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(.1)
    raise AssertionError(f'timed out: {check}')


def find_window(pid):
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(hwnd, name, len(name))
        if owner.value == pid and name.value == 'BiliMusicStudio':
            found.append(hwnd)
        return True

    user32.EnumWindows(visit, 0)
    return found[0] if found else None


exe = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as profile:
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        port = s.getsockname()[1]
    env = dict(os.environ, NICOMUSIC_HOME=profile,
               WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=f'--remote-debugging-port={port}')
    if desktop_running():
        raise SystemExit('Close NicoMusic before running desktop tests')
    # Hold one subresource open in a temporary copy of the UI to make cold
    # navigation observable without a production sleep or a slow machine.
    gate = None
    release, loading = threading.Event(), threading.Event()
    if '--startup' in sys.argv:
        class Hold(BaseHTTPRequestHandler):
            def do_GET(self):
                loading.set(); release.wait(30)
                self.send_response(204); self.end_headers()
            def log_message(self, *_): pass
        gate = ThreadingHTTPServer(('127.0.0.1', 0), Hold)
        threading.Thread(target=gate.serve_forever, daemon=True).start()
        app = Path(profile) / 'app'; app.mkdir()
        shutil.copytree(exe.parent / 'ui', app / 'ui')
        for f in (exe, exe.parent / 'libmpv-2.dll'):
            shutil.copy2(f, app / f.name)
        exe = app / exe.name
        html = app / 'ui/index.html'
        html.write_text(html.read_text(encoding='utf8').replace('</body>',
            f'<img hidden src="http://127.0.0.1:{gate.server_port}/hold"></body>'), encoding='utf8')
    def relaunch():
        subprocess.run([exe], env=env, check=True, timeout=5)
        assert proc.poll() is None  # secondary exited, primary is unchanged

    proc = subprocess.Popen([exe], env=env)
    ws = None
    hwnd = None
    try:
        until(desktop_running)
        relaunch()  # even if WebView2 is still starting
        hwnd = wintypes.HWND(until(lambda: find_window(proc.pid)))

        until(lambda: user32.IsWindowVisible(hwnd) and tray_registered(hwnd))

        def endpoint():
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/json/list', timeout=1) as r:
                    return next((t['webSocketDebuggerUrl'] for t in json.load(r)
                                 if t['url'] == 'https://studio.bilimusic.local/index.html'), None)
            except OSError:
                return None

        if gate:
            assert loading.wait(12), 'WebView2 did not begin loading the test UI'
            client = wintypes.RECT(); user32.GetClientRect(hwnd, ctypes.byref(client))
            dpi = user32.GetDpiForWindow(hwnd) / 96
            x, y = client.right // 2, client.bottom // 2 + round(54 * dpi)
            get_pixel = ctypes.windll.gdi32.GetPixel
            get_pixel.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
            user32.GetDC.restype = wintypes.HDC
            def bar_height():
                dc = user32.GetDC(hwnd)
                try:
                    return sum(get_pixel(dc, x, y + dy) == 0x5d4dd4 for dy in range(-round(12*dpi), round(12*dpi)+1))
                finally: user32.ReleaseDC(hwnd, wintypes.HDC(dc))
            first = until(bar_height)
            motion = wintypes.BOOL()
            user32.SystemParametersInfoW(0x1042, 0, ctypes.byref(motion), 0)
            if motion.value: until(lambda: 0 < bar_height() != first)
            release.set()
        ws = websocket.create_connection(until(endpoint), timeout=8, suppress_origin=True)

        def js(expression):
            ws.send(json.dumps({'id': 1, 'method': 'Runtime.evaluate', 'params': {
                'expression': expression, 'returnByValue': True}}))
            while True:
                reply = json.loads(ws.recv())
                if reply.get('id') == 1:
                    assert 'exceptionDetails' not in reply['result'], reply
                    return reply['result']['result'].get('value')

        until(lambda: js("document.readyState === 'complete' && !!document.querySelector('dialog')"))
        until(lambda: js("document.visibilityState === 'visible'"))
        if gate: print('OK: native launch indicator before navigation completes, then WebView visible')
        js("document.querySelectorAll('dialog[open]').forEach(d => d.close())")

        def geometry():
            bounds, client, origin = wintypes.RECT(), wintypes.RECT(), wintypes.POINT()
            assert user32.GetWindowRect(hwnd, ctypes.byref(bounds))
            assert user32.GetClientRect(hwnd, ctypes.byref(client))
            assert user32.ClientToScreen(hwnd, ctypes.byref(origin))
            assert origin.y == bounds.top, f'uncovered native top strip: {origin.y - bounds.top}px'
            return bounds, client, origin

        def hit(x, y):
            return user32.SendMessageW(hwnd, 0x84, 0, ((y & 0xffff) << 16) | (x & 0xffff))

        def check_restored():
            bounds, client, origin = geometry()
            # The side and bottom frame still resize natively.
            assert origin.x > bounds.left
            assert origin.y + client.bottom < bounds.bottom
            x = (bounds.left + bounds.right) // 2
            y = (bounds.top + bounds.bottom) // 2
            for point, expected in [
                ((x, bounds.top + 1), 12),  # HTTOP
                ((bounds.left + 1, bounds.top + 1), 13),  # HTTOPLEFT
                ((bounds.right - 2, bounds.top + 1), 14),  # HTTOPRIGHT
                ((bounds.left + 1, y), 10),  # HTLEFT
                ((bounds.right - 2, y), 11),  # HTRIGHT
                ((x, bounds.bottom - 2), 15),  # HTBOTTOM
            ]:
                assert hit(*point) == expected, (point, expected, hit(*point))
            # Test the signed WM_NCHITTEST coordinates used on left-hand monitors.
            width, height = bounds.right - bounds.left, bounds.bottom - bounds.top
            try:
                assert user32.SetWindowPos(hwnd, None, -200, -100, width, height, 0x14)
                assert hit(-100, -99) == 12
                assert hit(-199, -99) == 13
            finally:
                user32.SetWindowPos(hwnd, None, bounds.left, bounds.top, width, height, 0x14)

        def check_backdrops():
            geometry()
            assert js("""(() => {
                for (const d of document.querySelectorAll('dialog')) {
                    d.showModal();
                    const b = getComputedStyle(d, '::backdrop');
                    const ok = d.matches(':modal') && b.position === 'fixed' &&
                        ['top', 'right', 'bottom', 'left'].every(k => b[k] === '0px') &&
                        b.backdropFilter.includes('blur(');
                    d.close();
                    if (!ok) return false;
                }
                return true;
            })()""")
            bounds, client, _ = geometry()
            dpi_scale = user32.GetDpiForWindow(hwnd) / 96
            assert abs(js('innerHeight') * dpi_scale - client.bottom) <= 1

        check_restored()
        check_backdrops()
        js("document.querySelector('[data-window=maximize]').click()")
        until(lambda: user32.IsZoomed(hwnd))
        until(lambda: js("document.querySelector('#windowBar').classList.contains('isMaximized')"))
        check_backdrops()
        bounds, _, _ = geometry()
        assert hit((bounds.left + bounds.right) // 2, bounds.top + 1) != 12
        js("document.body.dataset.instanceTest = 'same-page'; document.querySelector('[data-window=minimize]').click()")
        check_tray_restore(hwnd, until, activate=relaunch)
        assert user32.IsZoomed(hwnd) and js("document.body.dataset.instanceTest === 'same-page'")
        js("document.querySelector('[data-window=maximize]').click()")
        until(lambda: not user32.IsZoomed(hwnd))
        until(lambda: js("!document.querySelector('#windowBar').classList.contains('isMaximized')"))
        check_restored()
        check_backdrops()
        js("document.querySelector('[data-window=minimize]').click()")
        check_tray_restore(hwnd, until)
        check_restored()
        close_button = "document.querySelector('[data-window=close]').click()"
        js(close_button)
        assert js("$('exitDialog').matches(':modal') && !$('rememberExit').checked")
        js("$('rememberExit').click(); $('exitDialog').querySelector('.close').click()")
        js(close_button)
        assert js("$('exitDialog').open && !$('rememberExit').checked")
        js("$('rememberExit').click(); $('exitForm').querySelector('[value=minimize]').click()")
        check_tray_restore(hwnd, until)
        js("location.reload()")
        until(lambda: js("document.readyState==='complete' && !!document.getElementById('resetExit') && !document.getElementById('resetExit').disabled"))
        js(close_button)  # remembered choice survives a new WebView document
        check_tray_restore(hwnd, until)
        js("$('settingsButton').click(); $('settingsTabGeneral').click(); $('resetExit').click(); $('settings').querySelector('.close').click()")
        js(close_button)
        assert js("$('exitDialog').open && $('resetExit').disabled")
        js("setTimeout(() => $('exitForm').querySelector('[value=close]').click(), 50)")
        assert proc.wait(timeout=8) == 0
        assert not desktop_running()
        until(lambda: not tray_registered(hwnd))
        print('OK: single desktop instance and second-launch tray activation; no top strip; all modal backdrops; resize edges/corners; signed coordinates; maximize/restore/minimize/close')
    finally:
        release.set()
        if gate: gate.shutdown(); gate.server_close()
        if ws:
            ws.close()
        if proc.poll() is None:
            if hwnd:
                user32.PostMessageW(hwnd, 0x10, 0, 0)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
