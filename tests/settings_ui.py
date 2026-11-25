"""Headless settings regression: python tests/settings_ui.py [browser.exe] [--screenshot file.png]
Uses a mock native host and private profile. Does not register OS shortcuts or
start/close the desktop player. Native persistence/rollback is tested separately.
"""
import argparse
import base64
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.request

import websocket

parser = argparse.ArgumentParser()
parser.add_argument('browser', nargs='?')
parser.add_argument('--screenshot')
args = parser.parse_args()
browser = Path(args.browser) if args.browser else Path(os.environ.get('PROGRAMFILES(X86)', 'C:/Program Files (x86)')) / 'Microsoft/Edge/Application/msedge.exe'
page = (Path(__file__).resolve().parents[1] / 'ui/index.html').as_uri()


def until(check, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(.03)
    raise AssertionError(f'timed out: {check}')


with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as profile:
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    proc = subprocess.Popen([str(browser), '--headless=new', '--disable-gpu', '--no-first-run',
                             '--disable-background-networking', '--disable-features=HardwareMediaKeyHandling',
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

        def key(code, name=None, modifiers=0, vk=0):
            for kind in ('keyDown', 'keyUp'):
                cdp('Input.dispatchKeyEvent', {'type': kind, 'code': code, 'key': name or code,
                                              'modifiers': modifiers, 'windowsVirtualKeyCode': vk})

        cdp('Page.enable')
        cdp('Page.addScriptToEvaluateOnNewDocument', {'source': '''
          window.hostMessages=[]; window.hostListeners=[];
          try { localStorage.setItem('nico.autoUpdateCheck','off'); } catch {}
          window.hostBindings=JSON.parse(localStorage.getItem('mock.hotkeys')||'null')||[{modifiers:3,key:37},{modifiers:3,key:39},{modifiers:3,key:32}];
          window.hostCapture=false;
          window.nativeMessage=data=>hostListeners.forEach(fn=>fn({data}));
          window.hostSnapshot=(extra={})=>({hotkeyConfig:hostBindings.map(b=>({...b})),hotkeyCapture:hostCapture,hotkeyCaptureRequest:0,hotkeyWarning:'',
            hotkeys:{prev:!hostCapture&&!!hostBindings[0].key,next:!hostCapture&&!!hostBindings[1].key,toggle:!hostCapture&&!!hostBindings[2].key,
              mediaPrev:true,mediaNext:true,mediaToggle:true,volumeUp:true,volumeDown:true},...extra});
          window.chrome=window.chrome||{};
          window.chrome.webview={addEventListener:(type,fn)=>hostListeners.push(fn),postMessage:command=>{
            hostMessages.push(command);
            setTimeout(()=>{
              if(command.startsWith('ready')) { nativeMessage(hostSnapshot()); nativeMessage({appVersion:'1.0.0'}); }
              else if(command.startsWith('hotkeys-capture ')) {
                const [,mode,request]=command.split(' '); hostCapture=mode==='on';
                nativeMessage(hostSnapshot({hotkeyCaptureRequest:Number(request)}));
              } else if(command.startsWith('hotkeys-set ')) {
                const parts=command.split(' ').slice(1).map(Number), candidate=[0,1,2].map(i=>({modifiers:parts[i*2],key:parts[i*2+1]}));
                const failed=candidate[0].key===120; hostCapture=false;
                if(!failed) { hostBindings=candidate; localStorage.setItem('mock.hotkeys',JSON.stringify(candidate)); }
                nativeMessage(hostSnapshot({hotkeyResult:{ok:!failed,error:failed?'组合键被其他程序占用；原设置未修改':''}}));
              }
            },0);
          }};
        '''})
        cdp('Emulation.setDeviceMetricsOverride', {'width': 1180, 'height': 776, 'deviceScaleFactor': 1, 'mobile': False})
        cdp('Page.navigate', {'url': page})
        until(lambda: js("document.readyState==='complete' && document.getElementById('record-prev') && !document.getElementById('record-prev').disabled"))
        assert js("!document.getElementById('info') && $('settingsButton').getAttribute('aria-label')==='设置'")
        js("$('settingsButton').click()")
        assert js("$('settings').open && !$('settingsHotkeys').hidden && $('about').hidden && $('hotkey-prev').value==='Ctrl + Alt + ←'")
        assert js("$('hotkeyForm').querySelectorAll('input').length===3 && [...$('hotkeyForm').querySelectorAll('input')].every(i=>i.readOnly)")

        def record(action):
            js(f"$('record-{action}').click()")
            until(lambda: js(f"$('hotkey-{action}').value==='请按组合键…'"))

        record('prev')
        key('KeyK', 'k', 3, 75)
        until(lambda: js("$('hotkey-prev').value==='Ctrl + Alt + K' && !hostCapture"))
        assert js("playbackShortcuts.bindings()[0].key===37 && hostBindings[0].key===37")
        js("$('saveHotkeys').click()")
        until(lambda: js("$('hotkeyEditStatus').textContent.includes('已保存') && playbackShortcuts.bindings()[0].key===75"))
        assert js("$('prev').title.includes('Ctrl + Alt + K')")
        js('location.reload()')
        until(lambda: js("document.getElementById('record-prev') && !document.getElementById('record-prev').disabled"))
        js("$('settingsButton').click()")
        assert js("$('hotkey-prev').value==='Ctrl + Alt + K'")
        print('OK: gear entry, exactly three editable bindings, record/save, active fallback and reload')

        js("$('clear-next').click(); $('saveHotkeys').click()")
        until(lambda: js("playbackShortcuts.bindings()[1].key===0 && $('hotkeyEditStatus').textContent.includes('已保存')"))
        assert js("$('next').getAttribute('aria-keyshortcuts')==='MediaTrackNext'")
        js("$('resetHotkeys').click()")
        assert js("$('hotkey-next').value==='Ctrl + Alt + →' && hostBindings[1].key===0")
        js("$('saveHotkeys').click()")
        until(lambda: js("hostBindings[0].key===37 && hostBindings[1].key===39 && $('saveHotkeys').disabled"))
        record('prev')
        key('ArrowRight', modifiers=3, vk=39)
        assert js("$('hotkeyEditStatus').textContent.includes('已用于其他') && hostCapture")
        key('Escape', vk=27)
        until(lambda: js("!hostCapture && $('hotkey-prev').value==='Ctrl + Alt + ←'"))
        record('prev')
        js("$('record-next').click()")  # Switch record targets without re-registering defaults.
        js("$('query').dispatchEvent(new KeyboardEvent('keydown',{code:'AudioVolumeUp',repeat:true,bubbles:true,cancelable:true}))")
        assert js("hostMessages.at(-1)==='volume-step 5' && $('hotkey-next').value==='请按组合键…'")
        key('Escape', vk=27)
        until(lambda: js('!hostCapture'))
        print('OK: clear/restore, duplicate rejection, Escape cancellation, fixed volume keys stay active during recording')

        record('prev')
        key('F9', modifiers=3, vk=120)
        until(lambda: js("$('hotkey-prev').value==='Ctrl + Alt + F9'"))
        js("$('saveHotkeys').click()")
        until(lambda: js("$('hotkeyEditStatus').textContent.includes('占用') && !$('saveHotkeys').disabled"))
        assert js("hostBindings[0].key===37 && playbackShortcuts.bindings()[0].key===37")
        js("$('cancelHotkeyChanges').click()")
        assert js("$('hotkey-prev').value==='Ctrl + Alt + ←'")
        record('prev')
        key('Tab', vk=9)
        until(lambda: js('!hostCapture'))
        record('prev')
        js("window.dispatchEvent(new Event('blur'))")
        until(lambda: js('!hostCapture'))
        js("$('record-prev').click(); document.dispatchEvent(new KeyboardEvent('keydown',{code:'Escape',key:'Escape',bubbles:true,cancelable:true})); $('record-next').click()")
        until(lambda: js("hostCapture && $('hotkey-next').value==='请按组合键…'"))
        key('Escape', vk=27)
        until(lambda: js('!hostCapture'))
        js("nativeMessage(hostSnapshot({hotkeyWarning:'无法读取热键设置，已使用默认组合键'}))")
        assert js("!$('saveHotkeys').disabled && $('hotkeyEditStatus').textContent.includes('无法读取')")
        js("$('saveHotkeys').click()")
        until(lambda: js("$('saveHotkeys').disabled && $('hotkeyEditStatus').textContent.includes('已保存')"))
        print('OK: occupied-save rollback, discard, blur cancellation, stale capture-ack isolation and corrupt-config repair')

        js("$('clear-prev').click(); $('settings').querySelector('.close').click(); $('settingsButton').click()")
        assert js("$('hotkey-prev').value==='Ctrl + Alt + ←'")
        js("$('settingsTabAbout').click()")
        assert js("!$('about').hidden && $('settingsHotkeys').hidden && $('about').closest('dialog').id==='settings' && !!$('projectLink')")
        js("$('settingsTabGeneral').click(); localStorage.setItem('titleBarCloseAction','minimize'); window.dispatchEvent(new Event('storage')); $('resetExit').click()")
        assert js("!localStorage.getItem('titleBarCloseAction') && $('resetExit').disabled")
        js("$('settingsTabAbout').focus()")
        key('Home', vk=36)
        assert js("$('settingsTabHotkeys').getAttribute('aria-selected')==='true' && document.activeElement===$('settingsTabHotkeys')")
        for width, height in [(1180, 776), (610, 445)]:
            cdp('Emulation.setDeviceMetricsOverride', {'width': width, 'height': height, 'deviceScaleFactor': 1, 'mobile': False})
            assert js("""(() => { const d=$('settings'), b=d.getBoundingClientRect(), gear=$('settingsButton').getBoundingClientRect(), aside=document.querySelector('aside').getBoundingClientRect();
              $('record-prev').scrollIntoView({block:'center'}); const r=$('record-prev').getBoundingClientRect();
              return b.top>=0 && b.bottom<=innerHeight && d.scrollWidth<=d.clientWidth && r.top>=b.top && r.bottom<=b.bottom && gear.left>=aside.left && gear.right<=aside.right; })()"""), (width, height)
        if args.screenshot:
            cdp('Emulation.setDeviceMetricsOverride', {'width': 1180, 'height': 776, 'deviceScaleFactor': 1, 'mobile': False})
            js("$('settings').scrollTop=0")
            output = Path(args.screenshot)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(base64.b64decode(cdp('Page.captureScreenshot', {'format': 'png'})['data']))
        print('OK: close discards drafts, nested About, general preferences, keyboard tabs and minimum-window geometry')
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
