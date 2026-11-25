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
        js("""
          tracks = Array.from({length: 100}, (_, i) => ({url: `file:///test/${i}.wav`, name: `Track ${i}`}));
          fav = new Set(tracks.slice(0, 60).map(t => t.url));
          lists.create('Scroll test', tracks.slice(0, 80).map(t => t.url));
          refreshListNav(); sync();
        """)
        fixed_geometry = """['.windowBar', '.topbar', '.collectionTools', '#batchBar', 'footer'].map(s => {
          const r = document.querySelector(s).getBoundingClientRect(); return [r.top, r.bottom];
        })"""
        for width, height in [(1180, 776), (900, 650), (610, 445)]:
            cdp('Emulation.setDeviceMetricsOverride', {'width': width, 'height': height,
                                                     'deviceScaleFactor': 1, 'mobile': False})
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
