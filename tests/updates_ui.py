"""Headless About/update UI regression: python tests/updates_ui.py [browser.exe]
Requires websocket-client. A mock native bridge exercises the real page without
opening a desktop player, save dialog, external page or network update request.
"""
import json, os, socket, subprocess, sys, tempfile, time, urllib.request
from pathlib import Path
import websocket
browser = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(os.environ.get('PROGRAMFILES(X86)', 'C:/Program Files (x86)')) / 'Microsoft/Edge/Application/msedge.exe'
page = (Path(__file__).resolve().parents[1] / 'ui/index.html').as_uri()
with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as profile:
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0)); port=sock.getsockname()[1]
    proc=subprocess.Popen([str(browser),'--headless=new','--disable-gpu','--no-first-run',f'--user-data-dir={profile}',f'--remote-debugging-port={port}','about:blank'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    ws=None
    try:
        deadline=time.monotonic()+10
        while True:
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/json/list',timeout=1) as r:
                    endpoint=next(t['webSocketDebuggerUrl'] for t in json.load(r) if t['type']=='page')
                break
            except (OSError,StopIteration):
                if time.monotonic()>deadline: raise
                time.sleep(.05)
        ws=websocket.create_connection(endpoint,timeout=8,suppress_origin=True)
        seq=0
        def cdp(method,params=None):
            global seq
            seq+=1; ws.send(json.dumps({'id':seq,'method':method,'params':params or {}}))
            while True:
                reply=json.loads(ws.recv())
                if reply.get('id')==seq:
                    assert 'result' in reply and 'exceptionDetails' not in reply['result'],reply
                    return reply['result']
        def js(code):
            return cdp('Runtime.evaluate',{'expression':code,'returnByValue':True})['result'].get('value')
        cdp('Page.enable')
        cdp('Page.addScriptToEvaluateOnNewDocument',{'source':'''window.hostMessages=[]; window.hostListeners=[]; window.openCount=0; window.open=()=>{window.openCount++};
          window.chrome=window.chrome||{}; window.chrome.webview={postMessage:s=>hostMessages.push(s),addEventListener:(type,fn)=>hostListeners.push(fn)};
          window.nativeMessage=data=>hostListeners.forEach(fn=>fn({data}));'''})
        cdp('Emulation.setDeviceMetricsOverride',{'width':1180,'height':776,'deviceScaleFactor':1,'mobile':False})
        cdp('Page.navigate',{'url':page})
        deadline=time.monotonic()+10
        while not js('document.readyState==="complete" && typeof updateChecker!=="undefined" && typeof nativeMessage==="function"'):
            assert time.monotonic()<deadline, js('[location.href, typeof nativeMessage, typeof hostMessages]'); time.sleep(.05)
        # The fixed volume bindings must use relative native commands, including
        # repeated keydown events while an editable control has focus.
        js('nativeMessage({shortcut:"volumeUp"}); nativeMessage({shortcut:"volumeDown"})')
        assert js('hostMessages.slice(-2).join(",")==="volume-step 5,volume-step -5"')
        js('$("query").dispatchEvent(new KeyboardEvent("keydown",{code:"AudioVolumeUp",repeat:true,bubbles:true,cancelable:true}))')
        assert js('hostMessages.at(-1)==="volume-step 5"')
        assert js('$("mediaKeyHint").textContent.includes("不参与组合热键编辑")')
        js('nativeMessage({appVersion:"1.0.0"})')
        assert js('hostMessages.includes("check-update")')
        release={'tag_name':'v9.0.0','draft':False,'prerelease':False,'assets':[{'name':'NicoMusic-v9.0.0-windows-x64.zip','state':'uploaded','size':1048576,'digest':'sha256:'+'a'*64,'browser_download_url':'https://github.com/lenzo15/Nico-Music/releases/download/v9.0.0/NicoMusic-v9.0.0-windows-x64.zip'}]}
        js('nativeMessage('+json.dumps({'updater':{'phase':'checked','httpStatus':200,'body':json.dumps(release)}})+'); $("info").click()')
        assert js('$("about").open && !$("downloadUpdate").hidden && $("appVersion").textContent==="v1.0.0"')
        for width,height in [(1180,776),(610,445)]:
            cdp('Emulation.setDeviceMetricsOverride',{'width':width,'height':height,'deviceScaleFactor':1,'mobile':False})
            assert js('''(()=>{ const d=$('about'); $('downloadUpdate').scrollIntoView({block:'center'}); const r=$('downloadUpdate').getBoundingClientRect(), b=d.getBoundingClientRect(); return r.top>=b.top && r.bottom<=b.bottom && b.top>=0 && b.bottom<=innerHeight; })()''')
        js('$("downloadUpdate").click()')
        assert js('hostMessages.at(-1).startsWith("download-update https://github.com/lenzo15/Nico-Music/")')
        js('nativeMessage({updater:{phase:"downloading",received:262144,total:1048576}})')
        assert js('!$("downloadProgress").hidden && $("downloadBytes").textContent.includes("25%")')
        js('$("cancelUpdate").click()')
        assert js('hostMessages.at(-1)==="cancel-update"')
        js('nativeMessage({updater:{phase:"downloaded",file:"C:/Downloads/NicoMusic.zip"}}); $("showDownload").click()')
        assert js('hostMessages.at(-1)==="show-update" && window.openCount===0')
        print('OK: full-page native bridge flow, fixed volume commands/repeat, readonly media-key help, About version/update controls, progress/cancel/reveal; controls reachable at normal/minimum size')
    finally:
        if ws:
            try: cdp('Browser.close')
            except Exception: pass
            ws.close()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill(); proc.wait()
