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
user32.SetForegroundWindow.argtypes = [wintypes.HWND]
MODIFIERS = 0x4003  # MOD_CONTROL | MOD_ALT | MOD_NOREPEAT
KEYS = {1: 0x25, 2: 0x27, 3: 0x20}
MEDIA_KEYS = {4: 0xB1, 5: 0xB0, 6: 0xB3}  # Previous, Next, PlayPause
VOLUME_KEYS = {7: 0xAF, 8: 0xAE}  # Up, Down (repeat while held)
ALL_KEYS = {**KEYS, **MEDIA_KEYS, **VOLUME_KEYS}


def modifiers(identifier):
    return MODIFIERS if identifier in KEYS else 0x4000 if identifier in MEDIA_KEYS else 0


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
                assert user32.PostMessageW(hwnd, 0x312, identifier, (ALL_KEYS[identifier] << 16) | (modifiers(identifier) & 0xF))

            def media_command(command):
                assert user32.PostMessageW(hwnd, 0x319, hwnd, command << 16)  # WM_APPCOMMAND

            until(lambda: js('document.querySelectorAll(".row").length===3'))
            until(lambda: js('$("toggle").title.includes("全局")'))
            if conflict:
                assert js('$("next").title.includes("仅窗口内") && $("hotkeyAvailability").textContent.includes("注册失败")')
            for identifier, key in ALL_KEYS.items():
                if conflict and identifier == 2:
                    continue
                assert not user32.RegisterHotKey(None, 60, modifiers(identifier), key), 'App did not register its shortcut'
            # Fixed volume keys work even before a track starts. Relative engine
            # commands must accumulate when several messages precede the next UI snapshot.
            hotkey(7)
            until(lambda: js('state.volume===75'))
            hotkey(8)
            until(lambda: js('state.volume===70'))
            js("send('volume 50')")
            until(lambda: js('state.volume===50'))
            for _ in range(5): hotkey(7)
            until(lambda: js('state.volume===75'))
            js("send('volume 98')")
            until(lambda: js('state.volume===98'))
            hotkey(7)
            until(lambda: js('state.volume===100'))
            hotkey(7)
            time.sleep(.2)
            assert js('state.volume===100')
            js("send('volume 2')")
            until(lambda: js('state.volume===2'))
            hotkey(8)
            until(lambda: js('state.volume===0'))
            hotkey(8)
            time.sleep(.2)
            assert js('state.volume===0')
            media_command(10)  # APPCOMMAND_VOLUME_UP
            until(lambda: js('state.volume===5'))
            media_command(9)   # APPCOMMAND_VOLUME_DOWN
            until(lambda: js('state.volume===0'))
            js("send('volume 70')")
            until(lambda: js('state.volume===70'))
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
            hotkey(6)
            until(lambda: js('state.paused'))
            hotkey(6)
            until(lambda: js('!state.paused && state.playing'))
            hotkey(5)
            until(lambda: js('state.index===2 && state.playing'))
            hotkey(4)
            until(lambda: js('state.index===1 && state.playing'))
            media_command(14)  # APPCOMMAND_MEDIA_PLAY_PAUSE
            until(lambda: js('state.paused'))
            media_command(14)
            until(lambda: js('!state.paused && state.playing'))
            media_command(11)  # APPCOMMAND_MEDIA_NEXTTRACK
            until(lambda: js('state.index===2 && state.playing'))
            media_command(12)  # APPCOMMAND_MEDIA_PREVIOUSTRACK
            until(lambda: js('state.index===1 && state.playing'))
            user32.PostMessageW(hwnd, 0x112, 0xF020, 0)  # SC_MINIMIZE to tray
            until(lambda: not user32.IsWindowVisible(wintypes.HWND(hwnd)))
            hotkey(6)
            until(lambda: js('state.paused'))
            assert not user32.IsWindowVisible(wintypes.HWND(hwnd))
            media_command(14)
            until(lambda: js('!state.paused && state.playing'))
            hotkey(7)
            until(lambda: js('state.volume===75'))
            media_command(9)
            until(lambda: js('state.volume===70'))
            hotkey(5)
            until(lambda: js('state.index===2 && state.playing'))
            hotkey(4)
            until(lambda: js('state.index===1 && state.playing'))
            assert not user32.IsWindowVisible(wintypes.HWND(hwnd))
            check_tray_restore(wintypes.HWND(hwnd), until)
            assert js("$('toggle').title.includes('全局')")
            print('OK: native registration, scoped queue, combination shortcuts, fixed media keys, volume repeat/bounds, WM_APPCOMMAND and tray' +
                  (', conflict warning and local fallback' if conflict else ''))
            if not conflict:
                user32.SetForegroundWindow(hwnd)
                js("$('settingsButton').click(); $('record-prev').click()")
                until(lambda: js("$('hotkey-prev').value==='请按组合键…'"))
                assert user32.RegisterHotKey(None, 60, MODIFIERS, KEYS[1]), 'Recording did not release the old combination'
                user32.UnregisterHotKey(None, 60)
                assert not user32.RegisterHotKey(None, 60, modifiers(6), MEDIA_KEYS[6]), 'Recording changed a fixed media binding'
                for kind in ('keyDown', 'keyUp'):
                    cdp('Input.dispatchKeyEvent', {'type': kind, 'key': 'F8', 'code': 'F8', 'modifiers': 3, 'windowsVirtualKeyCode': 119})
                until(lambda: js("$('hotkey-prev').value==='Ctrl + Alt + F8'"))
                js("$('saveHotkeys').click()")
                until(lambda: js("$('hotkeyEditStatus').textContent.includes('已保存') && playbackShortcuts.bindings()[0].key===119"))
                assert (root / 'hotkeys.ini').read_text().splitlines()[0] == '3 119'
                assert user32.RegisterHotKey(None, 60, MODIFIERS, KEYS[1]), 'Old combination was not released after save'
                user32.UnregisterHotKey(None, 60)
                assert not user32.RegisterHotKey(None, 60, MODIFIERS, 119), 'Custom combination was not registered'
                ALL_KEYS[1] = 119
                js("$('settings').querySelector('.close').click()")
                hotkey(5)
                until(lambda: js('state.index===2'))
                hotkey(1)
                until(lambda: js('state.index===1'))
                print('OK: real capture suspension, persistent custom registration, old key release and unchanged fixed media keys')
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
        for identifier, key in ALL_KEYS.items():
            if conflict and identifier == 2:
                continue
            assert user32.RegisterHotKey(None, 60, modifiers(identifier), key), 'Shortcut was not released on exit'
            user32.UnregisterHotKey(None, 60)
        print('OK: global shortcuts released on exit')
finally:
    if conflict:
        user32.UnregisterHotKey(None, 50)
