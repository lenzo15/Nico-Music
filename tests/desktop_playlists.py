"""Windows integration test: python tests/desktop_playlists.py build/NicoMusic.exe
Requires websocket-client. Uses a temporary profile and local WAV fixtures.
"""
import ctypes
import functools
import http.server
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
import wave

import websocket
from tray import check_tray_restore, desktop_running


def until(check, timeout=12):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(.1)
    raise AssertionError(f'timed out: {check}')


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def copyfile(self, source, outputfile):
        try:
            super().copyfile(source, outputfile)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass  # Switching tracks can close an in-flight read.

    def log_message(self, *_):
        pass


exe = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
    root = Path(tmp)
    minimal = '--minimal' in sys.argv or '--broken-preset' in sys.argv
    if minimal:
        app = root / 'standalone'
        app.mkdir()
        for filename in (exe.name, 'libmpv-2.dll'):
            shutil.copy2(exe.parent / filename, app / filename)
        shutil.copytree(exe.parent / 'ui', app / 'ui')
        exe = app / exe.name
        if '--broken-preset' in sys.argv:
            preset = app / 'plugins/broken/focus'
            preset.mkdir(parents=True)
            (preset / 'preset.ini').write_text('api=1\nname=Broken\n')
            (preset / 'style.css').write_text('footer {visibility:hidden}')
            (preset / 'preset.js').write_text("document.currentScript.mountFocus = () => { throw Error('bad preset'); };")
    profile = root / 'profile'
    profile.mkdir()
    media = root / 'media'
    media.mkdir()
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(QuietHandler, directory=media))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    for name in 'ABC':
        with wave.open(str(media / f'{name}.wav'), 'wb') as w:
            w.setparams((1, 2, 8000, 0, 'NONE', 'not compressed'))
            if name == 'C' and '--focus' in sys.argv:
                # One second of a quiet 1 kHz tone, then two seconds of silence.
                period = b''.join(v.to_bytes(2, 'little', signed=True) for v in (0,362,512,362,0,-362,-512,-362))
                w.writeframes((period * 1000 + bytes(32000)) * 8)
            else:
                w.writeframes(bytes(16000 * (2 if name == 'A' else 24)))
    (profile / 'library.tsv').write_text(''.join(
        f'http://127.0.0.1:{server.server_port}/{n}.wav\t{n}\t\n' for n in 'ABC'), encoding='utf8')
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        port = s.getsockname()[1]
    env = dict(os.environ, NICOMUSIC_HOME=str(profile), WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=f'--remote-debugging-port={port}')
    if desktop_running():
        raise SystemExit('Close NicoMusic before running desktop tests')
    proc = subprocess.Popen([exe], env=env)
    ws = None
    try:
        def endpoint():
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/json/list', timeout=1) as r:
                    return next((t['webSocketDebuggerUrl'] for t in json.load(r) if t['url'] == 'https://studio.bilimusic.local/index.html'), None)
            except OSError:
                return None
        ws = websocket.create_connection(until(endpoint), timeout=8, suppress_origin=True)
        def cdp(method, params):
            ws.send(json.dumps({'id': 1, 'method': method, 'params': params}))
            while True:
                r = json.loads(ws.recv())
                if r.get('id') == 1:
                    assert 'result' in r and 'exceptionDetails' not in r['result'], r
                    return r['result']
        def js(expr):
            return cdp('Runtime.evaluate', {'expression': expr, 'returnByValue': True})['result'].get('value')
        until(lambda: js('document.querySelectorAll(".row").length===3'))
        js("$('viewQueue').click()")
        assert js("$('queueDialog').matches(':modal') && !$('queueEmpty').hidden && !$('queueItems').childElementCount")
        js("$('queueDialog').querySelector('.close').click()")
        js("$('newList').click()")
        assert js("!$('listForm').querySelector('input[type=checkbox]') && !$('editList')")
        js("$('listName').value='Morning'; $('listForm').requestSubmit()")
        assert js("!document.querySelector('.row')")
        # The nav only scrolls when the sidebar runs out of room; resizing the
        # window or removing playlists must not leave an empty scrollbar behind.
        assert js("""(() => {
          const nav = $('listNav'), shell = nav.closest('.shell'), height = shell.style.height;
          const fits = () => nav.scrollHeight <= nav.clientHeight && nav.scrollWidth <= nav.clientWidth;
          try {
            if (!fits()) return false;
            for (let i = 0; i < 7; i++) nav.append(nav.firstElementChild.cloneNode(true));
            shell.style.height = '900px';
            if (!fits()) return false;
            shell.style.height = '420px';
            nav.scrollTop = nav.scrollHeight;
            if (nav.scrollTop === 0) return false;
            return true;
          } finally {
            nav.replaceChildren(nav.firstElementChild);
            shell.style.height = height;
          }
        })()""")
        assert js("$('listNav').scrollHeight === $('listNav').clientHeight && !$('listNav').scrollTop")
        js("document.querySelector('nav [data-page=library]').click(); $('batchToggle').click()")
        assert js("$('batchPlay').disabled && !$('batchBar').hidden")
        js("$('query').value='A.wav'; $('query').oninput(); $('batchAll').click()")
        assert js("$('batchCount').textContent==='已选 1 首' && document.querySelectorAll('.pick').length===1")
        js("$('query').value=''; $('query').oninput()")
        assert js("$('batchAdd').disabled")
        js("document.querySelectorAll('.pick input')[0].click(); document.querySelectorAll('.pick input')[2].click(); $('batchAdd').click(); $('listChoices').firstChild.firstChild.click(); $('assignForm').requestSubmit(); $('listNav').firstChild.click()")
        assert js("$('heading').textContent==='Morning' && document.querySelectorAll('.row').length===2")
        js("$('batchToggle').click(); $('batchAll').click(); $('batchFav').click(); document.querySelector('[data-page=likes]').click()")
        assert js("document.querySelectorAll('.row').length===2")
        js("$('batchToggle').click(); $('batchAll').click(); $('batchPlay').click()")
        until(lambda: js("$('track').textContent==='A' && !$('next').disabled"))
        js("document.querySelector('nav [data-page=library]').click()")
        until(lambda: js("$('track').textContent==='C' && $('next').disabled"))  # never visits B
        js("$('viewQueue').click()")
        assert js("[...$('queueItems').querySelectorAll('button')].map(b=>b.textContent).join(',')==='A,C' && $('queueItems').querySelector('[aria-current]').textContent==='C'")
        js("$('queueItems').querySelector('button').click()")
        until(lambda: js("$('track').textContent==='A' && $('prev').disabled && $('queueItems').querySelector('[aria-current]').textContent==='A'"))
        js("$('queueDialog').querySelector('.close').click()")
        assert js("$('heading').textContent==='全部音乐'")
        js("$('next').click()")
        until(lambda: js("$('track').textContent==='C'"))
        # C is the last entry in the A,C selection, even on the library page.
        def mode(expected):
            js("$('repeat').click()")
            until(lambda: js(f"$('repeat').dataset.mode==='{expected}'"))
        def near_end():
            until(lambda: js("state.duration>=23 && state.playing"))
            js("$('seek').value=22; $('seek').onchange()")
            until(lambda: js("state.elapsed>=22"))
        mode('one'); near_end()
        until(lambda: js("state.playing && state.elapsed<2 && $('track').textContent==='C'"))
        mode('all'); near_end()
        until(lambda: js("$('track').textContent==='A' && !$('prev').disabled"))
        js("$('prev').click()")  # wraps backwards within the selection, too
        until(lambda: js("$('track').textContent==='C' && !$('next').disabled"))
        js('location.reload()')
        until(lambda: js("document.getElementById('repeat')?.dataset.mode==='all' && localStorage.getItem('repeatMode')==='all'"))
        mode('off')
        until(lambda: js("$('next').disabled && $('track').textContent==='C'"))
        print('OK: single-track EOF repeat, scoped queue wrap, repeat off and saved mode')
        if minimal:
            js("$('cover').click()")
            until(lambda: js("!$('focusView').hidden && $('focusPresetMenu').hidden && $('focusContent').textContent==='C' && state.playing"))
            assert js("!document.body.dataset.focusPreset && !document.querySelector('canvas') && getComputedStyle(document.querySelector('footer')).visibility==='visible'")
            js("$('toggle').click()")
            until(lambda: js("state.paused"))
            js("$('focusBack').click()")
            assert js("$('focusView').hidden && !$('newList').disabled")
            print('OK: basic playback and focus fallback without a functioning preset')
        if '--focus' in sys.argv:
            default_bars = js("['.windowBar','footer'].map(s=>getComputedStyle(document.querySelector(s)).background)")
            js("$('cover').click()")
            until(lambda: js("!$('focusView').hidden && $('focusTitle')?.textContent==='C' && $('focusView').classList.contains('playing')"))
            until(lambda: js("!$('auroraCanvas').hidden && $('auroraCanvas').width > 0"))
            until(lambda: js("$('auroraCanvas').hidden && $('focusView').classList.contains('playing')"))
            js("$('auroraToggle').click()")
            assert js("$('auroraToggle').getAttribute('aria-pressed')==='false' && localStorage.getItem('focusAurora')==='off'")
            js("$('auroraToggle').click()")
            until(lambda: js("!$('auroraCanvas').hidden"))
            assert all(a != b for a,b in zip(default_bars, js("['.windowBar','footer'].map(s=>getComputedStyle(document.querySelector(s)).background)")))
            js("$('toggle').click()")
            until(lambda: js("!$('focusView').classList.contains('playing') && $('auroraCanvas').hidden"))
            js("$('focusPresetTool').click()")
            assert js("!$('focusPresetMenu').hidden && document.activeElement.dataset.id==='vinyl'")
            js("document.activeElement.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true}))")
            assert js("$('focusPresetMenu').hidden && !$('focusView').hidden && document.activeElement===$('focusPresetTool')")
            js("$('focusPresetTool').click(); $('focusPreset').firstElementChild.click()")
            assert js("$('focusPresetMenu').hidden && $('focusPreset').firstElementChild.getAttribute('aria-checked')==='true'")
            assert js("!$('auroraCanvas') && $('focusContent').textContent==='C'")
            assert default_bars == js("['.windowBar','footer'].map(s=>getComputedStyle(document.querySelector(s)).background)")
            js("$('focusPresetTool').click(); $('focusPreset').querySelector('[data-id=vinyl]').click()")
            until(lambda: js("!!$('focusSleeve') && state.paused"))
            js("$('focusBack').click()")
            assert js("$('focusView').hidden && document.activeElement===$('cover')")
            assert default_bars == js("['.windowBar','footer'].map(s=>getComputedStyle(document.querySelector(s)).background)")
            js("document.querySelector('.current .rowCover').click(); document.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape'}))")
            assert js("$('focusView').hidden && state.paused")
            print('OK: focus chrome, real-audio aurora, silence and toggle, pause, return and Escape')
        js("$('newList').click(); $('listName').value='Evening'; $('listForm').requestSubmit(); document.querySelector('nav [data-page=library]').click(); document.querySelector('.row button[title=加入歌单]').click()")
        assert js("$('assignDialog').open && $('listChoices').firstChild.firstChild.checked")
        js("$('listChoices').lastChild.firstChild.checked=true; $('assignForm').requestSubmit(); location.reload()")
        until(lambda: js("document.querySelectorAll('#listNav button').length===2 && document.querySelectorAll('.row').length===3"))
        js("$('listNav').lastChild.click()")
        assert js("$('heading').textContent==='Evening' && document.querySelectorAll('.row').length===1 && document.querySelector('.row strong').textContent==='A'")
        js("$('batchToggle').click(); $('batchAll').click(); $('batchRemove').click()")
        assert js("!document.querySelector('.row')")
        js("$('listNav').firstChild.click()")
        assert js("document.querySelectorAll('.row').length===2")
        js("document.querySelector('.row button[title=加入歌单]').click(); $('createWithSong').click()")
        assert js("!$('listForm').querySelector('input[type=checkbox]')")
        js("$('listName').value='From song'; $('listForm').requestSubmit()")
        assert js("document.querySelectorAll('.row').length===1 && document.querySelector('.row strong').textContent==='A'")
        js("document.querySelector('.row button[title=从当前歌单移除]').click()")
        assert js("!document.querySelector('.row')")
        js("location.reload()")
        until(lambda: js("document.querySelectorAll('.row').length===3 && $('listNav').childElementCount===3"))
        js("$('listNav').firstChild.click()")
        assert js("document.querySelectorAll('.row').length===2 && document.querySelector('.row strong').textContent==='A'")
        js("document.querySelector('[data-page=likes]').click()")
        assert js("document.querySelectorAll('.row').length===2")
        js("$('listNav').lastChild.click()")
        assert js("!document.querySelector('.row')")
        js("$('deleteList').click()")
        assert js("$('deleteListDialog').matches(':modal') && $('deleteListHint').textContent.includes('From song') && document.activeElement === $('cancelDeleteList')")
        js("$('cancelDeleteList').click()")
        assert js("!$('deleteListDialog').open && currentList().name === 'From song'")
        js("$('deleteList').click(); $('confirmDeleteList').click()")
        assert js("$('heading').textContent==='全部音乐' && document.querySelectorAll('.row').length===3 && $('listNav').childElementCount===2")
        js("document.querySelector('[data-page=likes]').click(); $('batchToggle').click(); $('batchAll').click(); $('batchFav').click()")
        assert js("!document.querySelector('.row')")
        js("document.querySelector('[data-page=library]').click(); $('batchToggle').click(); document.querySelectorAll('.pick input')[0].click(); document.querySelectorAll('.pick input')[2].click(); window.confirm=()=>false; $('batchRemove').click()")
        assert js("$('batchCount').textContent==='已选 2 首' && document.querySelectorAll('.row').length===3")
        js("window.confirm=()=>true; $('batchRemove').click()")
        until(lambda: js("document.querySelectorAll('.row').length===1 && document.querySelector('.row strong').textContent==='B'"))
        js("$('listNav').firstChild.click()")
        assert js("!document.querySelector('.row')")
        print('OK: create/delete collections, scoped multi-select, filtered selection, batch add/like/play/remove, persistence')
        # Deliver a disk-backed File, not a synthetic blob or a basename.
        local = media / '本地 #100% Music.wav'
        shutil.copyfile(media / 'B.wav', local)
        js("const importList = page; $('topAdd').click()")
        x, y = js("(() => {const r=$('localDrop').getBoundingClientRect(); return [r.x+r.width/2,r.y+r.height/2]})()")
        for kind in ('dragEnter', 'dragOver', 'drop'):
            cdp('Input.dispatchDragEvent', {'type': kind, 'x': x, 'y': y, 'data': {
                'items': [], 'files': [str(local)], 'dragOperationsMask': 1}})
        until(lambda: js("$('url').value") == str(local))
        js("$('addForm').querySelector('[value=start]').click()")
        until(lambda: js("!$('addDialog').open && state.playing && source(current())==='本地音乐' && current().name==='本地 #100% Music'"))
        assert local.as_uri() in (profile / 'library.tsv').read_text(encoding='utf8')
        assert js("page === importList && currentList().urls.includes(current().url) && document.querySelectorAll('.row').length === 1")
        # Re-import the same library URL in Likes: add membership without duplicating a track.
        js("const importedUrl = current().url; select('likes'); $('topAdd').click(); $('url').value = importedUrl; $('addForm').querySelector('[value=add]').click()")
        until(lambda: js("!$('addDialog').open && page === 'likes' && fav.has(importedUrl)"))
        assert js("tracks.filter(t => t.url === importedUrl).length === 1 && JSON.parse(localStorage.getItem('likes')).includes(importedUrl) && lists.find(importList).urls.includes(importedUrl)")
        print('OK: local file drop, Unicode path, playback, playlist and Likes import destinations')
        if '--window' in sys.argv:
            user32 = ctypes.windll.user32
            windows = []
            @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
            def find_window(raw, _):
                h = ctypes.c_void_p(raw)
                pid = ctypes.c_ulong()
                user32.GetWindowThreadProcessId(h, ctypes.byref(pid))
                if pid.value == proc.pid:
                    cls = ctypes.create_unicode_buffer(64)
                    user32.GetClassNameW(h, cls, len(cls))
                    if cls.value == 'BiliMusicStudio': windows.append(h)
                return True
            user32.EnumWindows(find_window, 0)
            hwnd, = windows
            style = user32.GetWindowLongW(hwnd, -16)
            assert not style & 0x00C00000 and style & 0x00040000  # no WS_CAPTION, still resizable
            js("$('windowMax').click()")
            until(lambda: user32.IsZoomed(hwnd) and js("$('windowMax').title==='还原'"))
            js("$('windowMax').click()")
            until(lambda: not user32.IsZoomed(hwnd) and js("$('windowMax').title==='最大化'"))
            js("document.querySelector('[data-window=minimize]').click()")
            check_tray_restore(hwnd, until)
            js("document.querySelector('[data-window=close]').click()")
            assert js("$('exitDialog').open")
            js("$('exitForm').querySelector('[value=minimize]').click()")
            check_tray_restore(hwnd, until)
            js("document.querySelector('[data-window=close]').click()")
            assert js("$('exitDialog').open && !$('rememberExit').checked")
            js("$('rememberExit').click(); setTimeout(() => $('exitForm').querySelector('[value=close]').click(), 100)")
            assert proc.wait(timeout=8) == 0
            print('OK: caption hidden, resize frame, maximize/restore, minimize and close')
    finally:
        if ws:
            ws.close()
        # Close only the desktop instance started by this test.
        @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
        def close_window(hwnd, _):
            pid = ctypes.c_ulong()
            ctypes.windll.user32.GetWindowThreadProcessId(ctypes.c_void_p(hwnd), ctypes.byref(pid))
            if pid.value == proc.pid:
                ctypes.windll.user32.PostMessageW(ctypes.c_void_p(hwnd), 0x10, 0, 0)
            return True
        ctypes.windll.user32.EnumWindows(close_window, 0)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill(); proc.wait()
        server.shutdown(); server.server_close()
