"""Windows integration: python tests/desktop_shortcuts.py build/Release/NicoMusic.exe [--conflict]
Requires websocket-client and no running desktop player. Uses silent local tracks,
a temporary profile and targeted messages, without sending input to the user's desktop.
"""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
import wave

import websocket
from tray import desktop_running, check_tray_restore

user32 = ctypes.windll.user32
user32.RegisterHotKey.argtypes = [wintypes.HWND, ctypes.c_int, wintypes.UINT, wintypes.UINT]
user32.UnregisterHotKey.argtypes = [wintypes.HWND, ctypes.c_int]
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
MODIFIERS = 0x4003  # MOD_CONTROL | MOD_ALT | MOD_NOREPEAT
KEYS = {1: 0x25, 2: 0x27, 3: 0x20}


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
conflict = '--conflict' in sys.argv
if desktop_running():
    raise SystemExit('Close NicoMusic before running desktop tests')
if conflict:
    assert user32.RegisterHotKey(None, 50, MODIFIERS, KEYS[2]), 'Cannot reserve conflict test chord'
try:
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
        root = Path(tmp)
        for name in 'ABC':
            with wave.open(str(root / f'{name}.wav'), 'wb') as audio:
                audio.setparams((1, 2, 8000, 0, 'NONE', 'not compressed'))
                audio.writeframes(bytes(16000 * 120))
        (root / 'library.tsv').write_text(''.join(
            f'{(root / f"{n}.wav").as_uri()}\t{n}\t\n' for n in 'ABC'), encoding='utf8')
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        env = dict(os.environ, NICOMUSIC_HOME=tmp,
                   WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=f'--remote-debugging-port={port}')
        proc = subprocess.Popen([exe], env=env)
        ws = hwnd = None
        try:
            hwnd = until(lambda: find_window(proc.pid))

            def endpoint():
                try:
                    with urllib.request.urlopen(f'http://127.0.0.1:{port}/json/list', timeout=1) as r:
                        return next((t['webSocketDebuggerUrl'] for t in json.load(r)
                                     if t['url'] == 'https://studio.bilimusic.local/index.html'), None)
                except OSError:
                    return None

            ws = websocket.create_connection(until(endpoint), timeout=8, suppress_origin=True)

            def cdp(method, params):
                ws.send(json.dumps({'id': 1, 'method': method, 'params': params}))
                while True:
                    reply = json.loads(ws.recv())
                    if reply.get('id') == 1:
                        assert 'result' in reply and 'exceptionDetails' not in reply['result'], reply
                        return reply['result']

            def js(expression):
                return cdp('Runtime.evaluate', {'expression': expression, 'returnByValue': True})['result'].get('value')

            def hotkey(identifier):
                assert user32.PostMessageW(hwnd, 0x312, identifier, (KEYS[identifier] << 16) | 3)

            until(lambda: js('document.querySelectorAll(".row").length===3'))
            until(lambda: js('$("toggle").title.includes("全局")'))
            if conflict:
                assert js('$("next").title.includes("仅窗口内") && $("shortcutHint").textContent.includes("注册失败")')
            for identifier, key in KEYS.items():
                if conflict and identifier == 2:
                    continue
                assert not user32.RegisterHotKey(None, 60, MODIFIERS, key), 'App did not register its chord'
            # An initial native toggle uses the current page, not the whole library.
            js("fav=new Set([tracks[1].url,tracks[2].url]); select('likes')")
            hotkey(3)
            until(lambda: js("state.index===1 && state.playing && state.queue.join(',')==='1,2'"))
            hotkey(3)
            until(lambda: js('state.paused'))
            hotkey(3)
            until(lambda: js('!state.paused && state.playing'))
            if conflict:
                # Browser input targets this WebView only, not the foreground app.
                cdp('Input.dispatchKeyEvent', {'type': 'keyDown', 'key': 'ArrowRight', 'code': 'ArrowRight',
                                              'modifiers': 3, 'windowsVirtualKeyCode': 0x27})
                cdp('Input.dispatchKeyEvent', {'type': 'keyUp', 'key': 'ArrowRight', 'code': 'ArrowRight',
                                              'modifiers': 3, 'windowsVirtualKeyCode': 0x27})
            else:
                hotkey(2)
            until(lambda: js('state.index===2 && state.playing'))
            hotkey(1)
            until(lambda: js('state.index===1 && state.playing'))
            hotkey(1)  # Disabled previous button at the start of the scoped queue.
            time.sleep(.5)
            assert js('state.index===1')
            user32.PostMessageW(hwnd, 0x112, 0xF020, 0)  # SC_MINIMIZE to tray
            until(lambda: not user32.IsWindowVisible(wintypes.HWND(hwnd)))
            hotkey(3)
            until(lambda: js('state.paused'))
            assert not user32.IsWindowVisible(wintypes.HWND(hwnd))
            if not conflict:
                hotkey(2)
                until(lambda: js('state.index===2 && state.playing'))
            check_tray_restore(wintypes.HWND(hwnd), until)
            assert js("$('toggle').title.includes('全局')")
            print('OK: native registration, scoped initial queue, previous/next, toggle, tray' +
                  (', conflict warning and local fallback' if conflict else ''))
        finally:
            if ws:
                ws.close()
            if hwnd:
                user32.PostMessageW(hwnd, 0x10, 0, 0)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        for identifier, key in KEYS.items():
            if conflict and identifier == 2:
                continue
            assert user32.RegisterHotKey(None, 60, MODIFIERS, key), 'Shortcut was not released on exit'
            user32.UnregisterHotKey(None, 60)
        print('OK: global shortcuts released on exit')
finally:
    if conflict:
        user32.UnregisterHotKey(None, 50)
