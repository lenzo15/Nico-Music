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
        until(lambda: js("document.readyState==='complete' && document.getElementById('hotkey-prev') && !document.getElementById('hotkey-prev').disabled"))
        assert js("!document.getElementById('info') && $('settingsButton').getAttribute('aria-label')==='设置'")
        js("$('settingsButton').click()")
        assert js("$('settings').open && !$('settingsHotkeys').hidden && $('about').hidden && $('hotkey-prev').value==='Ctrl + Alt + ←'")
        assert js("$('hotkeyForm').querySelectorAll('input').length===3 && [...$('hotkeyForm').querySelectorAll('input')].every(i=>i.readOnly)")
        assert js("!$('shortcutHint') && !$('mediaKeyHint') && !/默认媒体键|MediaPlayPause|MediaPrevious|MediaNext|VolumeUp|VolumeDown|组合键在后台/.test($('settings').textContent)")
        assert js("$('hotkeyAvailability').hidden && !$('hotkeyAvailability').textContent")
        assert js("!document.querySelector('[id^=record-],[id^=clear-]') && !$('hotkeyForm').querySelector('.hotkeyRow button')")

        def quiet():
            assert js("$('hotkeyEditStatus').hidden && !$('hotkeyEditStatus').textContent")

        quiet()

        def record(action):
            js(f"$('hotkey-{action}').click()")
            until(lambda: js(f"$('hotkey-{action}').value==='请按组合键…'"))
            assert js(f"$('hotkey-{action}').classList.contains('isRecording')")

        record('prev')
        key('KeyK', 'k', 3, 75)
        until(lambda: js("$('hotkey-prev').value==='Ctrl + Alt + K' && !hostCapture"))
        assert js("playbackShortcuts.bindings()[0].key===37 && hostBindings[0].key===37 && !$('hotkey-prev').classList.contains('isRecording')")
        js("$('saveHotkeys').click()")
        until(lambda: js("$('saveHotkeys').disabled && playbackShortcuts.bindings()[0].key===75"))
        quiet()
        assert js("$('prev').title.includes('Ctrl + Alt + K')")
        js('location.reload()')
        until(lambda: js("document.getElementById('hotkey-prev') && !document.getElementById('hotkey-prev').disabled"))
        js("$('settingsButton').click()")
        assert js("$('hotkey-prev').value==='Ctrl + Alt + K'")
        quiet()
        print('OK: button-free inline editing, silent normal state, save, active fallback and reload')

        record('next')
        key('Backspace', vk=8)
        until(lambda: js("$('hotkey-next').value==='未设置' && !hostCapture"))
        quiet()
        js("$('saveHotkeys').click()")
        until(lambda: js("playbackShortcuts.bindings()[1].key===0 && $('saveHotkeys').disabled"))
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
        quiet()
        js("$('hotkey-prev').focus()")
        key('Enter', vk=13)
        until(lambda: js("hostCapture && $('hotkey-prev').value==='请按组合键…'"))
        key('Escape', vk=27)
        until(lambda: js('!hostCapture'))
        quiet()
        record('prev')
        x, y = js("(() => {const r=$('hotkey-next').getBoundingClientRect();return [r.x+r.width/2,r.y+r.height/2]})()")
        for kind in ('mousePressed', 'mouseReleased'):
            cdp('Input.dispatchMouseEvent', {'type': kind, 'x': x, 'y': y, 'button': 'left', 'clickCount': 1})
        until(lambda: js("hostCapture && $('hotkey-next').value==='请按组合键…'"))
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
        quiet()
        record('prev')
        js("$('settingsTabHotkeys').focus()")
        until(lambda: js("!hostCapture && !$('hotkey-prev').classList.contains('isRecording')"))
        quiet()
        record('prev')
        js("window.dispatchEvent(new Event('blur'))")
        until(lambda: js('!hostCapture'))
        quiet()
        js("$('hotkey-prev').click(); document.dispatchEvent(new KeyboardEvent('keydown',{code:'Escape',key:'Escape',bubbles:true,cancelable:true})); $('hotkey-next').click()")
        until(lambda: js("hostCapture && $('hotkey-next').value==='请按组合键…'"))
        key('Escape', vk=27)
        until(lambda: js('!hostCapture'))
        js("nativeMessage(hostSnapshot({hotkeyWarning:'无法读取热键设置，已使用默认组合键'}))")
        assert js("!$('saveHotkeys').disabled && $('hotkeyEditStatus').textContent.includes('无法读取')")
        js("$('saveHotkeys').click()")
        until(lambda: js("$('saveHotkeys').disabled && $('saveHotkeys').textContent==='保存热键' && $('hotkeyEditStatus').hidden"))
        quiet()
        print('OK: occupied-save rollback, discard, blur cancellation, stale capture-ack isolation and corrupt-config repair')

        js("$('hotkey-prev').focus()")
        key('Delete', vk=46)
        assert js("$('hotkey-prev').value==='未设置'")
        quiet()
        js("$('settings').querySelector('.close').click(); $('settingsButton').click()")
        assert js("$('hotkey-prev').value==='Ctrl + Alt + ←'")
        js("$('settingsTabAbout').click()")
        assert js("!$('about').hidden && $('settingsHotkeys').hidden && $('about').closest('dialog').id==='settings' && !!$('projectLink')")
        assert js("$('starInvite').textContent.includes('✨') && $('starInvite').textContent.includes('Star') && $('projectLink').href==='https://github.com/lenzo15/Nico-Music'")
        js("$('settingsTabGeneral').click(); localStorage.setItem('titleBarCloseAction','minimize'); window.dispatchEvent(new Event('storage')); $('resetExit').click()")
        assert js("!localStorage.getItem('titleBarCloseAction') && $('resetExit').disabled")
        js("$('settingsTabAbout').focus()")
        key('Home', vk=36)
        assert js("$('settingsTabHotkeys').getAttribute('aria-selected')==='true' && document.activeElement===$('settingsTabHotkeys')")
        geometry = """(() => { const r=$('settings').getBoundingClientRect(), body=$('settingsBody');
          return [r.x,r.y,r.width,r.height,body.clientWidth,body.clientHeight,
            document.querySelector('.settingsHeader').getBoundingClientRect().bottom,
            document.querySelector('.settingsTabs').getBoundingClientRect().bottom]; })()"""
        for width, height in [(1180, 776), (900, 650), (610, 445)]:
            cdp('Emulation.setDeviceMetricsOverride', {'width': width, 'height': height, 'deviceScaleFactor': 1, 'mobile': False})
            js("$('settingsTabHotkeys').click()")
            baseline = js(geometry)
            assert baseline[2:4] == [min(620, width - 32), min(522, height - 32)], (width, height, baseline)
            for tab in ('General', 'About', 'Hotkeys', 'About', 'General', 'Hotkeys'):
                js(f"$('settingsTab{tab}').click()")
                assert js(geometry) == baseline, (width, height, tab)
            js("$('settingsTabAbout').click(); nativeMessage({updater:{phase:'downloading',received:512,total:1024}})")
            assert js(geometry) == baseline
            js("nativeMessage({updater:{phase:'download-error',error:'更新失败：'+'更新'.repeat(300)}})")
            assert js(geometry) == baseline
            assert js("$('settingsBody').scrollHeight>$('settingsBody').clientHeight && $('settings').scrollTop===0")
            js("$('settingsTabGeneral').click()")
            assert js(geometry) == baseline
            js("$('settingsTabHotkeys').click()")
            assert js("""(() => { const d=$('settings'), b=d.getBoundingClientRect(), gear=$('settingsButton').getBoundingClientRect(), aside=document.querySelector('aside').getBoundingClientRect();
              $('hotkey-prev').scrollIntoView({block:'center'}); const r=$('hotkey-prev').getBoundingClientRect();
              return b.top>=0 && b.bottom<=innerHeight && d.scrollWidth<=d.clientWidth && r.top>=b.top && r.bottom<=b.bottom && gear.left>=aside.left && gear.right<=aside.right; })()"""), (width, height)
        js("$('settingsTabAbout').click(); $('settingsBody').scrollTop=0")
        chrome = js("['.settingsHeader','.settingsTabs','#settings .close'].map(s=>{const r=document.querySelector(s).getBoundingClientRect();return [r.top,r.bottom]})")
        js("$('settingsBody').scrollTop=$('settingsBody').scrollHeight")
        assert js("$('settingsBody').scrollTop>0 && $('settings').scrollTop===0")
        assert js("['.settingsHeader','.settingsTabs','#settings .close'].map(s=>{const r=document.querySelector(s).getBoundingClientRect();return [r.top,r.bottom]})") == chrome
        assert js("getComputedStyle($('settings')).overflowY==='hidden' && getComputedStyle($('settingsBody'),'::-webkit-scrollbar').width==='4px' && getComputedStyle($('settingsBody'),'::-webkit-scrollbar-button').display==='none'")
        if args.screenshot:
            cdp('Emulation.setDeviceMetricsOverride', {'width': 1180, 'height': 776, 'deviceScaleFactor': 1, 'mobile': False})
            js("$('settingsTabHotkeys').click(); $('settingsBody').scrollTop=0")
            output = Path(args.screenshot)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(base64.b64decode(cdp('Page.captureScreenshot', {'format': 'png'})['data']))
        print('OK: close discards drafts, nested About, general preferences and keyboard tabs')
        print('OK: fixed dialog/body/header geometry across all tabs and download states at 3 viewport sizes; inset scrolling only')
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
