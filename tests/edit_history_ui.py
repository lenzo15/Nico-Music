"""Headless undo/redo integration with a mock native host; never touches the player/profile."""
import json
import os
from pathlib import Path
import socket
import subprocess
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
        time.sleep(.03)
    raise AssertionError('timed out waiting for UI state')


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

        def key(name):
            js("$('rows').focus()")
            for kind in ('keyDown', 'keyUp'):
                cdp('Input.dispatchKeyEvent', {'type': kind, 'code': 'Key' + name.upper(), 'key': name,
                                              'modifiers': 2, 'windowsVirtualKeyCode': ord(name.upper())})

        cdp('Page.enable')
        cdp('Page.addScriptToEvaluateOnNewDocument', {'source': '''
          window.driverErrors=[]; addEventListener('error', e=>driverErrors.push(e.message));
          window.hostMessages=[]; window.hostListeners=[]; window.hostEdits=new Map(); window.hostSerial=0;
          window.song=n=>({url:`https://example.test/${n}.mp3`,name:n.toUpperCase(),thumbnail:''});
          window.hostTracks=['a','b','c'].map(song); window.confirm=()=>true;
          if(!localStorage.getItem('seeded')) {
            localStorage.setItem('seeded','yes'); localStorage.setItem('nico.autoUpdateCheck','off');
            localStorage.setItem('likes',JSON.stringify([song('a').url,song('b').url]));
            localStorage.setItem('bilimusic.playlists.v1',JSON.stringify([
              {id:'one',name:'One',urls:[song('a').url,song('b').url]}, {id:'two',name:'Two',urls:[]} ]));
          }
          window.nativeMessage=data=>hostListeners.forEach(fn=>fn({data}));
          window.hostSnapshot=extra=>({tracks:hostTracks.map(t=>({...t})),index:-1,queue:[],repeat:'off',...extra});
          window.hostEdit=(before,kind,count,extra={})=>{
            const changed=JSON.stringify(before.map(t=>t.url))!==JSON.stringify(hostTracks.map(t=>t.url));
            const id=changed?String(++hostSerial):'';
            if(id)hostEdits.set(id,{before,after:[...hostTracks]});
            nativeMessage(hostSnapshot({libraryEdit:{id,kind,count},...extra}));
          };
          window.chrome=window.chrome||{};
          window.chrome.webview={addEventListener:(type,fn)=>hostListeners.push(fn),postMessage:command=>{
            hostMessages.push(command); setTimeout(()=>{
              const [cmd,...parts]=command.split(' '), before=[...hostTracks];
              if(cmd==='ready') { nativeMessage(hostSnapshot()); nativeMessage({appVersion:'1.0.1'}); }
              else if(cmd==='remove'||cmd==='removeurls') {
                const urls=cmd==='remove'?[hostTracks[Number(parts[0])].url]:parts;
                hostTracks=hostTracks.filter(t=>!urls.includes(t.url)); hostEdit(before,'delete',urls.length);
              } else if(cmd==='add'||cmd==='start') {
                const imported=parts[0].includes('existing')?[song('b')]:[song('b'),song('d'),song('e')];
                for(const t of imported) if(!hostTracks.some(old=>old.url===t.url))hostTracks.push(t);
                hostEdit(before,'collection',imported.length,{imported:cmd,importedUrls:imported.map(t=>t.url)});
              } else if(cmd==='history-apply') {
                const [id,direction]=parts;
                if(window.failNextHistory) {
                  window.failNextHistory=false;
                  nativeMessage(hostSnapshot({historyResult:{id,direction,ok:false,error:'模拟保存失败'}}));
                } else {
                  hostTracks=[...hostEdits.get(id)[direction==='undo'?'before':'after']];
                  nativeMessage(hostSnapshot({historyResult:{id,direction,ok:true}}));
                }
              }
            },0);
          }};
        '''})
        cdp('Page.navigate', {'url': page})
        try:
            until(lambda: js("document.readyState==='complete' && typeof editHistory!=='undefined' && tracks.length===3"))
        except AssertionError:
            print(js("JSON.stringify({errors:window.driverErrors,messages:window.hostMessages})"))
            raise
        js("""window.warningFixture = '模拟播放失败 <img src=x>';
          nativeMessage(hostSnapshot({index:1, queue:[0,1,2], tracks:hostTracks.map((t,i) =>
            i===0 ? {...t, playbackWarning:warningFixture} : t)}))""")
        for view in ('library', 'likes', 'one'):
            js(f"select('{view}')")
            assert js("""(() => {
              const marks = [...$('rows').querySelectorAll('.playbackWarning')];
              return marks.length===1 && marks[0].previousElementSibling.textContent==='A' &&
                marks[0].title===warningFixture && marks[0].getAttribute('aria-label').includes(warningFixture) &&
                marks[0].querySelector('svg path') && !marks[0].querySelector('img');
            })()""")
        js("$('batchToggle').click()")
        assert js("document.querySelector('.row.pick .playbackWarning')!==null")
        js("$('viewQueue').click()")
        assert js("$('queueItems').querySelectorAll('.playbackWarning').length===1 && $('queueItems').querySelector('[data-index=\\'0\\'] .playbackWarning')!==null")
        js("nativeMessage(hostSnapshot({queue:[0,1,2]}))")
        assert js("!document.querySelector('.playbackWarning')")
        js("$('queueDialog').close(); select('library')")
        print('OK: warning icon on the affected library, batch, playlist and queue item, with reason and recovery')
        js("document.querySelectorAll('.row')[2].querySelector('[title=从音乐库移除]').click()")
        until(lambda: js("tracks.length===2 && !fav.has(song('a').url) && !lists.find('one').urls.includes(song('a').url) && !pendingMutation"))
        assert js("$('toast').classList.contains('show') && $('toast').querySelector('button')?.textContent==='撤销'"), js("JSON.stringify({errors:driverErrors,toast:$('toast').textContent,undo:editHistory.canUndo,messages:hostMessages})")
        key('z')
        until(lambda: js("tracks.length===3 && fav.has(song('a').url) && lists.find('one').urls.includes(song('a').url) && !editHistory.busy"))
        assert js("tracks.map(t=>t.name).join(',')==='A,B,C'")
        key('y')
        until(lambda: js("tracks.length===2 && !editHistory.busy"))
        js("$('toast').querySelector('button').click()")
        until(lambda: js("tracks.length===3 && !editHistory.busy"))
        print('OK: row delete, actual Ctrl+Z/Y, clickable toast, likes/list membership and original order')

        js("$('settingsButton').click(); $('editNotices').click(); $('settings').querySelector('.close').click()")
        assert js("!editHistory.notices && localStorage.getItem('nico.editNotices')==='off'")
        js("$('batchToggle').click(); $('batchAll').click(); $('batchRemove').click()")
        until(lambda: js("tracks.length===0 && !pendingMutation"))
        assert js("!$('toast').classList.contains('show')")
        key('z')
        until(lambda: js("tracks.length===3 && !editHistory.busy"))
        assert js("fav.size===2 && lists.find('one').urls.length===2 && !$('toast').classList.contains('show')")
        print('OK: batch deletion is one undo step; notices switch does not disable history')

        js("select('one'); document.querySelector('.row [title=从当前歌单移除]').click()")
        assert js("lists.find('one').urls.length===1 && tracks.length===3")
        before = js("hostMessages.filter(m=>m.startsWith('history-apply')).length")
        key('z')
        until(lambda: js("lists.find('one').urls.length===2 && !editHistory.busy"))
        assert js("hostMessages.filter(m=>m.startsWith('history-apply')).length") == before
        key('y')
        until(lambda: js("lists.find('one').urls.length===1 && !editHistory.busy"))
        key('z')
        until(lambda: js("lists.find('one').urls.length===2 && !editHistory.busy"))
        print('OK: playlist-only removal restores membership without changing the native library')

        js("$('topAdd').click(); $('url').value='https://example.test/collection'; $('addForm').requestSubmit($('addForm button[value=add]'))")
        until(lambda: js("tracks.length===5 && lists.find('one').urls.length===4 && !pendingImport"))
        key('z')
        until(lambda: js("tracks.length===3 && lists.find('one').urls.length===2 && !editHistory.busy"))
        assert js("tracks.some(t=>t.url===song('b').url)")
        key('y')
        until(lambda: js("tracks.length===5 && lists.find('one').urls.length===4 && !editHistory.busy"))
        js("select('two'); $('topAdd').click(); $('url').value='https://example.test/existing'; $('addForm').requestSubmit($('addForm button[value=add]'))")
        until(lambda: js("lists.find('two').urls.length===1 && !pendingImport"))
        before = js("hostMessages.filter(m=>m.startsWith('history-apply')).length")
        key('z')
        until(lambda: js("lists.find('two').urls.length===0 && !editHistory.busy"))
        assert js("tracks.length===5 && hostMessages.filter(m=>m.startsWith('history-apply')).length") == before
        print('OK: mixed existing/new imports, destination membership, duplicate-only import, one-step redo')

        js("$('query').focus(); window.keyTest=new KeyboardEvent('keydown',{key:'z',code:'KeyZ',ctrlKey:true,bubbles:true,cancelable:true}); $('query').dispatchEvent(keyTest)")
        assert js("!keyTest.defaultPrevented && tracks.length===5")
        # The latest operation is local-only; undo the preceding native import next.
        js("window.failNextHistory=true")
        key('z')
        until(lambda: js("!editHistory.busy && $('toast').textContent.includes('模拟保存失败')"))
        assert js("tracks.length===5 && editHistory.canUndo")
        key('z')
        until(lambda: js("tracks.length===3 && !editHistory.busy"))
        print('OK: native failure keeps undo retryable; errors remain visible with operation notices disabled; inputs keep text undo')
        js('window.reloadPending=true; location.reload()')
        until(lambda: js("!window.reloadPending && document.readyState==='complete' && typeof editHistory!=='undefined' && !!editHistory"))
        assert js("!editHistory.notices && !editHistory.canUndo && !editHistory.canRedo")
        print('OK: preference survives reload; session history does not')
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
