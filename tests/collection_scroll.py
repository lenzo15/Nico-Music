"""Layout regression: python tests/collection_scroll.py [path/to/msedge-or-chrome]
Requires websocket-client. Runs headless against ui/ with synthetic tracks; does
not start or close the music player and does not send desktop keyboard input.
"""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

import websocket


def until(check, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(.05)
    raise AssertionError(f'timed out: {check}')


candidates = [Path(sys.argv[1])] if len(sys.argv) > 1 else [
    Path(os.environ.get('PROGRAMFILES(X86)', 'C:/Program Files (x86)')) / 'Microsoft/Edge/Application/msedge.exe',
    Path(os.environ.get('PROGRAMFILES', 'C:/Program Files')) / 'Google/Chrome/Application/chrome.exe',
]
browser = next((p for p in candidates if p.is_file()), None)
if not browser:
    raise SystemExit('Pass a Chromium browser executable as the first argument')
page = (Path(__file__).resolve().parents[1] / 'ui/index.html').as_uri()
with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as profile:
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    proc = subprocess.Popen([str(browser), '--headless=new', '--disable-gpu', '--no-first-run',
                             '--no-default-browser-check', f'--user-data-dir={profile}',
                             f'--remote-debugging-port={port}', page],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ws = None
    try:
        def endpoint():
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/json/list', timeout=1) as response:
                    return next((t['webSocketDebuggerUrl'] for t in json.load(response) if t['url'] == page), None)
            except OSError:
                return None
        ws = websocket.create_connection(until(endpoint), timeout=8, suppress_origin=True)
        sequence = 0

        def cdp(method, params=None):
            global sequence
            sequence += 1
            ws.send(json.dumps({'id': sequence, 'method': method, 'params': params or {}}))
            while True:
                reply = json.loads(ws.recv())
                if reply.get('id') == sequence:
                    assert 'result' in reply and 'exceptionDetails' not in reply['result'], reply
                    return reply['result']

        def js(expression):
            return cdp('Runtime.evaluate', {'expression': expression, 'returnByValue': True})['result'].get('value')

        until(lambda: js('document.readyState === "complete"'))
        assert js("$('sidebarStatus').textContent === '随心听好音乐' && $('sidebarStatusArea').dataset.playing === 'false'")
        js("""
          tracks = Array.from({length: 100}, (_, i) => ({url: `file:///test/${i}.wav`, name: `Track ${i}`}));
          fav = new Set(tracks.slice(0, 60).map(t => t.url));
          lists.create('Scroll test', tracks.slice(0, 80).map(t => t.url));
          refreshListNav(); sync();
        """)
        assert js("$('sidebarStatus').textContent === '100 首音乐'")
        js("state={...state,index:0,playing:false,paused:false,elapsed:-1}; sync()")
        assert js("$('sidebarStatus').textContent === '正在载入…' && $('sidebarStatusArea').dataset.playing === 'false'")
        js("state={...state,playing:true,elapsed:1}; sync()")
        assert js("$('sidebarStatus').textContent === '正在播放' && $('sidebarStatusArea').dataset.playing === 'true'")
        cdp('Emulation.setEmulatedMedia', {'features': [{'name': 'prefers-reduced-motion', 'value': 'no-preference'}]})
        assert js("getComputedStyle(document.querySelector('.eq b')).animationName === 'sidebar-wave'")
        cdp('Emulation.setEmulatedMedia', {'features': [{'name': 'prefers-reduced-motion', 'value': 'reduce'}]})
        assert js("getComputedStyle(document.querySelector('.eq b')).animationName === 'none'")
        cdp('Emulation.setEmulatedMedia', {'features': []})
        js("state.paused=true; sync()")
        assert js("$('sidebarStatus').textContent === '已暂停' && $('sidebarStatusArea').dataset.playing === 'false' && getComputedStyle(document.querySelector('.eq b')).animationName === 'none'")
        js("state={...state,playing:false,paused:false}; sync()")
        assert js("$('sidebarStatus').textContent === '已停止'")
        js("state.index=-1; sync()")
        print('OK: sidebar identity, library count, loading/play/pause/stop status and reduced-motion equalizer')
        for page, newest in [('library', 99), ('likes', 59), (js('lists.items[0].id'), 79)]:
            js(f"select({json.dumps(page)})")
            assert js("[...document.querySelectorAll('.row strong')].map(n => n.textContent)") == [
                f'Track {i}' for i in range(newest, -1, -1)]
        fixed_geometry = """['.windowBar', '.topbar', '.collectionTools', '#batchBar', 'footer'].map(s => {
          const r = document.querySelector(s).getBoundingClientRect(); return [r.top, r.bottom];
        })"""
        for width, height in [(1180, 776), (900, 650), (610, 445)]:
            cdp('Emulation.setDeviceMetricsOverride', {'width': width, 'height': height,
                                                     'deviceScaleFactor': 1, 'mobile': False})
            assert js("""(() => {
              const area=$('sidebarStatusArea'), button=$('settingsButton'), aside=document.querySelector('aside');
              const a=area.getBoundingClientRect(), b=button.getBoundingClientRect(), s=aside.getBoundingClientRect();
              const identity=document.querySelector('.asideIdentity');
              return a.left>=s.left && b.right<=s.right && area.scrollWidth<=area.clientWidth &&
                (innerWidth<=720 ? getComputedStyle(identity).display==='none' :
                  identity.getBoundingClientRect().right<=b.left && identity.firstElementChild.scrollWidth<=identity.clientWidth);
            })()"""), (width, height, 'sidebar status fits')
            for batch in (False, True):
                js("select('library')" + ("; $('batchToggle').click()" if batch else ''))
                assert js("$('rows').scrollHeight > $('rows').clientHeight && $('rows').clientHeight > 30")
                assert js("""(() => {
                  const rows = $('rows').getBoundingClientRect(), main = document.querySelector('main');
                  return rows.top >= document.querySelector('.collectionTools').getBoundingClientRect().bottom &&
                    rows.bottom <= document.querySelector('footer').getBoundingClientRect().top &&
                    main.scrollHeight <= main.clientHeight + 1 && $('rows').scrollWidth <= $('rows').clientWidth;
                })()"""), (width, height, batch)
                before = js(fixed_geometry)
                row_top = js("$('rows').firstElementChild.getBoundingClientRect().top")
                x, y = js("(() => { const r=$('rows').getBoundingClientRect(); return [r.x+r.width/2,r.y+r.height/2]; })()")
                cdp('Input.dispatchMouseEvent', {'type': 'mouseWheel', 'x': x, 'y': y,
                                               'deltaX': 0, 'deltaY': 450})
                until(lambda: js("$('rows').scrollTop > 0"))
                assert js(fixed_geometry) == before, (width, height, batch)
                assert js("$('rows').firstElementChild.getBoundingClientRect().top") < row_top
                assert js("document.querySelector('main').scrollTop === 0 && document.scrollingElement.scrollTop === 0")
                js("select('likes')")
                assert js("$('rows').scrollTop === 0 && $('heading').textContent === '我喜欢的'")
                js("$('rows').scrollTop = 500; $('listNav').firstElementChild.click()")
                assert js("$('rows').scrollTop === 0 && $('heading').textContent === 'Scroll test'")
            print(f'OK: {width}x{height}, wheel scrolls only tracks, fixed controls with/without batch, page reset')
            js("$('deleteList').click()")
            assert js("""(() => {
              const d=$('deleteListDialog'), hint=$('deleteListHint');
              return d.matches(':modal') && hint.scrollHeight <= hint.clientHeight &&
                d.scrollHeight <= d.clientHeight && d.offsetWidth - d.clientWidth <= 2;
            })()""")
            js("$('deleteListHint').textContent='将删除「' + '歌'.repeat(60) + '」，其中的歌曲仍保留在音乐库中。'")
            assert js("$('deleteListHint').scrollHeight <= $('deleteListHint').clientHeight")
            x, y, bottom = js("(() => { const r=$('confirmDeleteList').getBoundingClientRect(); return [r.x+r.width/2,r.y+r.height/2,r.bottom]; })()")
            cdp('Input.dispatchMouseEvent', {'type': 'mousePressed', 'button': 'left', 'buttons': 1,
                                             'clickCount': 1, 'x': x, 'y': y})
            time.sleep(.5)  # Still pressed, past the end of the button transition.
            assert js("""(() => {
              const d=$('deleteListDialog'), b=$('confirmDeleteList');
              return d.matches(':modal') && b.matches(':active') &&
                b.getBoundingClientRect().bottom <= d.getBoundingClientRect().bottom &&
                d.scrollHeight <= d.clientHeight && d.offsetWidth - d.clientWidth <= 2;
            })()"""), (width, height, 'held delete button')
            assert js("$('confirmDeleteList').getBoundingClientRect().bottom") == bottom
            # Release over the heading so the fixture playlist is not deleted.
            y = js("$('deleteListTitle').getBoundingClientRect().top + 2")
            cdp('Input.dispatchMouseEvent', {'type': 'mouseReleased', 'button': 'left', 'x': x, 'y': y})
            # Even in a constrained dialog neither the hint nor the shell can scroll.
            assert js("""(() => {
              const d=$('deleteListDialog'), hint=$('deleteListHint');
              d.style.maxHeight='160px'; hint.scrollTop=100; d.scrollTop=100;
              const fits=hint.scrollTop === 0 && d.scrollTop === 0 &&
                $('confirmDeleteList').getBoundingClientRect().bottom <= d.getBoundingClientRect().bottom;
              d.style.maxHeight=''; $('cancelDeleteList').click(); return fits;
            })()""")
            for dialog in ('listDialog', 'assignDialog', 'addDialog', 'queueDialog', 'exitDialog'):
                assert js(f"""(() => {{ const d=$('{dialog}'); d.showModal();
                  const fits=d.scrollHeight <= d.clientHeight && d.scrollWidth <= d.clientWidth;
                  d.close(); return fits; }})()"""), (width, height, dialog)
            chrome = []
            for opener, dialog in [('settingsButton', 'settings'), ('topAdd', 'addDialog'), (None, 'mixDialog'), ('viewQueue', 'queueDialog')]:
                js(f"$('{opener}').click()" if opener else "previewMix({name:'试听',count:3,token:'1'})")
                if dialog != 'settings':
                    assert js(f"""(() => {{ const panel = $('{dialog}').querySelector('.importFields, .queueCard');
                      const s = getComputedStyle(panel);
                      return s.backgroundColor === 'rgba(0, 0, 0, 0)' && s.borderTopStyle === 'none'; }})()"""), dialog
                chrome.append(js(f"""(() => {{ const d=$('{dialog}'), h=d.querySelector('header h2'), b=d.querySelector('.close');
                  return [getComputedStyle(d).backgroundColor, getComputedStyle(d).borderRadius,
                    getComputedStyle(h).font, getComputedStyle(b).borderRadius, b.offsetWidth,
                    d.getBoundingClientRect().right - b.getBoundingClientRect().right]; }})()"""))
                assert js(f"getComputedStyle($('{dialog}').querySelector('h2')).fontSize === getComputedStyle($('heading')).fontSize")
                js(f"$('{dialog}').querySelector('.close').click()")
            assert all(style == chrome[0] for style in chrome), chrome
            print(f'OK: {width}x{height}, compact dialogs fit and all four sheets share header and surface styles')
        js("select('library'); $('rows').focus()")
        cdp('Input.dispatchKeyEvent', {'type': 'keyDown', 'key': 'PageDown', 'code': 'PageDown', 'windowsVirtualKeyCode': 34})
        cdp('Input.dispatchKeyEvent', {'type': 'keyUp', 'key': 'PageDown', 'code': 'PageDown', 'windowsVirtualKeyCode': 34})
        until(lambda: js("$('rows').scrollTop > 0"))
        # A short result needs no scrollbar. At minimum height an empty-state
        # illustration may still need to scroll, but only inside the list.
        js("$('query').value = 'Track 99'; $('query').oninput()")
        assert js("$('rows').scrollHeight <= $('rows').clientHeight")
        js("$('query').value = 'no matching tracks'; $('query').oninput()")
        assert js("""(() => {
          const main = document.querySelector('main');
          return !!$('rows').querySelector('.empty') && main.scrollTop === 0 && main.scrollHeight <= main.clientHeight + 1;
        })()""")
        print('OK: keyboard list scrolling, short search results and empty state')
    finally:
        if ws:
            try:
                cdp('Browser.close')
            except (OSError, websocket.WebSocketException):
                pass
            ws.close()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
