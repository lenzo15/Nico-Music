"""Headless drag-edge scrolling regression; uses an isolated browser and synthetic drag events."""
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.request
import websocket


def until(check, timeout=6):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(.025)
    raise AssertionError('timed out waiting for drag scroll')


browser = Path(os.environ.get('PROGRAMFILES(X86)', 'C:/Program Files (x86)')) / 'Microsoft/Edge/Application/msedge.exe'
page = (Path(__file__).resolve().parents[1] / 'ui/index.html').as_uri()
with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as profile:
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    proc = subprocess.Popen([str(browser), '--headless=new', '--disable-gpu', '--no-first-run',
                             f'--user-data-dir={profile}', f'--remote-debugging-port={port}', 'about:blank'],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ws = None
    try:
        def endpoint():
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/json/list', timeout=1) as response:
                    return next(t['webSocketDebuggerUrl'] for t in json.load(response) if t['type'] == 'page')
            except (OSError, StopIteration):
                return None
        until(lambda: endpoint() is not None)
        ws = websocket.create_connection(endpoint(), timeout=8, suppress_origin=True)
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

        cdp('Page.navigate', {'url': page})
        until(lambda: js("document.readyState==='complete' && typeof reorderHandles!=='undefined'"))
        js('''
          window.setupDrag=()=>{
            reorderHandles.reset();
            tracks=Array.from({length:100},(_,i)=>({url:`file:///test/${i}.wav`,name:`Track ${i}`}));
            select('library'); setBatch(true);
            const first=$('rows').firstElementChild;
            first.querySelector('input').click(); first.nextElementSibling.querySelector('input').click();
            window.dragGroup=pageOrder().slice(0,2).map(i=>tracks[i].url);
            window.dt=new DataTransfer();
            const grip=first.querySelector('.reorderGrip'), r=grip.getBoundingClientRect();
            grip.dispatchEvent(new DragEvent('dragstart',{bubbles:true,dataTransfer:dt,clientX:r.x+r.width/2,clientY:r.y+r.height/2}));
          };
          window.hover=(where)=>{
            const r=$('rows').getBoundingClientRect();
            const x=r.x+r.width/2, y=where==='bottom'?r.bottom-3:where==='top'?r.top+3:r.top+r.height/2;
            window.dragPoint={x,y};
            const target=document.elementFromPoint(x,y);
            target.dispatchEvent(new DragEvent('dragover',{bubbles:true,cancelable:true,dataTransfer:dt,clientX:x,clientY:y}));
          };
          window.endDrag=()=>document.dispatchEvent(new DragEvent('dragend',{bubbles:true,dataTransfer:dt}));
        ''')
        for width, height in [(1180, 776), (900, 650), (610, 445)]:
            cdp('Emulation.setDeviceMetricsOverride', {'width': width, 'height': height, 'deviceScaleFactor': 1, 'mobile': False})
            js('setupDrag(); hover("bottom")')
            until(lambda: js("$('rows').scrollTop>30"), timeout=2)
            first = js("$('rows').scrollTop")
            js('document.elementFromPoint(dragPoint.x,dragPoint.y).dispatchEvent(new DragEvent("dragleave",{bubbles:true,clientX:dragPoint.x,clientY:dragPoint.y}))')
            time.sleep(.4)  # No additional dragover: a stationary pointer must keep scrolling.
            assert js("$('rows').scrollTop") > first + 100
            assert js("document.querySelector('main').scrollTop===0 && document.scrollingElement.scrollTop===0")
            old_target = js("$('rows').querySelector('[data-insert]')?.dataset.url")
            time.sleep(.2)
            assert js("$('rows').querySelector('[data-insert]')?.dataset.url") != old_target
            js('hover("center")')
            stopped = js("$('rows').scrollTop")
            time.sleep(.15)
            assert js("$('rows').scrollTop") == stopped
            js('hover("top")')
            until(lambda: js(f"$('rows').scrollTop < {stopped - 25}"))
            js('endDrag()')
            stopped = js("$('rows').scrollTop")
            time.sleep(.15)
            assert js("$('rows').scrollTop") == stopped
            assert js("!$('rows').querySelector('[data-insert]')")

            # Auto-scroll even over a selected row, which is not a valid insertion target.
            js('setupDrag(); $("rows").scrollTop=100; hover("top")')
            until(lambda: js("$('rows').scrollTop===0"))
            time.sleep(.1)
            assert js("$('rows').scrollTop===0 && !$('rows').querySelector('[data-insert]')")
            js('endDrag(); setupDrag(); hover("bottom")')
            until(lambda: js("$('rows').scrollTop>180"))
            js('''(() => {
              const hint=$('rows').querySelector('[data-insert]');
              window.expected=moveVisibleTracks(pageOrder().map(i=>tracks[i].url),visibleUrls(),new Set(dragGroup),hint.dataset.url,hint.dataset.insert==='after');
              document.elementFromPoint(dragPoint.x,dragPoint.y).dispatchEvent(new DragEvent('drop',
                {bubbles:true,cancelable:true,dataTransfer:dt,clientX:dragPoint.x,clientY:dragPoint.y}));
            })()''')
            assert js("JSON.stringify(pageOrder().map(i=>tracks[i].url))===JSON.stringify(expected)")
            assert js("!$('rows').querySelector('[data-insert]') && expected.indexOf(dragGroup[1])===expected.indexOf(dragGroup[0])+1")
            stopped = js("$('rows').scrollTop")
            time.sleep(.15)
            assert js("$('rows').scrollTop") == stopped

            # Exiting the list stops; re-entering resumes; dropping outside never reorders.
            js('setupDrag(); hover("bottom")')
            until(lambda: js("$('rows').scrollTop>40"))
            js('document.querySelector(".topbar").dispatchEvent(new DragEvent("dragover",{bubbles:true,dataTransfer:dt,clientX:250,clientY:60}))')
            stopped = js("$('rows').scrollTop")
            time.sleep(.15)
            assert js("$('rows').scrollTop") == stopped
            js('hover("bottom")')
            until(lambda: js(f"$('rows').scrollTop>{stopped+30}"))
            js('document.body.dispatchEvent(new DragEvent("drop",{bubbles:true,dataTransfer:dt})); hover("bottom")')
            stopped = js("$('rows').scrollTop")
            time.sleep(.15)
            assert js("$('rows').scrollTop") == stopped

            # Reaching the end clamps cleanly, and window blur cancels the loop.
            js('setupDrag(); $("rows").scrollTop=$("rows").scrollHeight-$("rows").clientHeight-30; hover("bottom")')
            until(lambda: js("$('rows').scrollTop >= $('rows').scrollHeight-$('rows').clientHeight-1"))
            js('hover("top")')
            until(lambda: js("$('rows').scrollTop < $('rows').scrollHeight-$('rows').clientHeight-30"))
            js('window.dispatchEvent(new Event("blur"))')
            stopped = js("$('rows').scrollTop")
            time.sleep(.15)
            assert js("$('rows').scrollTop") == stopped
            print(f'OK: {width}x{height}, immediate/stationary bidirectional edge scroll, selected rows, moving hint, group drop, boundaries and cleanup')
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
