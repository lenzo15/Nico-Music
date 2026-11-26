"""Integration fixture: python tests/desktop_playlists.py build/NicoMusic.exe --nicomix (requires pywin32)."""
import json
import subprocess


def exercise(exe, root, profile, proc, js, cdp, until):
    import win32con, win32gui, win32process
    out = root/'share'; out.mkdir()
    original = (profile/'library.tsv').read_bytes()
    def picker():
        windows=[]
        def visit(hwnd,_):
            if win32process.GetWindowThreadProcessId(hwnd)[1]==proc.pid and win32gui.GetClassName(hwnd)=='#32770' and win32gui.GetWindowText(hwnd)=='选择歌单导出目录': windows.append(hwnd)
        win32gui.EnumWindows(visit,None)
        return windows[0] if windows else None
    def export(name, cancel=False):
        js("$('shareMix').click()")
        dialog = until(picker)
        if cancel:
            win32gui.PostMessage(dialog,win32con.WM_CLOSE,0,0); until(lambda: not picker()); return
        def edit_box():
            edits=[]
            def visit(hwnd,_):
                if win32gui.GetClassName(hwnd)=='Edit' and win32gui.IsWindowVisible(hwnd): edits.append(hwnd)
            win32gui.EnumChildWindows(dialog,visit,None)
            return edits[0] if edits else None
        win32gui.SendMessage(until(edit_box),win32con.WM_SETTEXT,0,str(out))
        win32gui.PostMessage(dialog,win32con.WM_COMMAND,1,win32gui.GetDlgItem(dialog,1))
        target=out/(name+'.nicomix')
        import time
        time.sleep(.5)
        if picker()==dialog:  # Shell may first navigate into the typed directory.
            win32gui.PostMessage(dialog,win32con.WM_COMMAND,1,win32gui.GetDlgItem(dialog,1))
        try: until(lambda: target.exists() and js("$('toast').textContent === " + json.dumps('已导出：'+target.name)))
        except AssertionError as e:
            raise AssertionError((target,list(out.iterdir()),picker(),js("$('toast').textContent"))) from e
        return json.loads(target.read_text(encoding='utf8'))
    def wait(expr): until(lambda:js(expr))
    def preview(path): js('send("mix-open " + '+json.dumps(path.as_uri())+')'); wait("$('mixDialog').open")
    assert js("$('shareMix').nextElementSibling===$('topAdd') && !$('shareMix').textContent.trim()")
    export('全部音乐',True); assert not list(out.iterdir())
    js("$('query').value='A';$('query').oninput()")
    doc=export('全部音乐'); assert doc['format']=='nicomix' and doc['version']==1
    assert [s['title'] for s in doc['tracks']]==['A','B','C']; assert export('全部音乐 (2)')==doc
    fixture=root/'From friend.NICOMIX'; name='分享:试听'; songs=[doc['tracks'][1], {'title':'Unavailable','url':'https://no-source.invalid/watch?id=3'},doc['tracks'][0],doc['tracks'][1]]
    fixture.write_text(json.dumps(dict(doc,name=name,tracks=songs)),encoding='utf8')
    # Select a real disk File in WebView2, just as in the file chooser.
    js("$('topAdd').click()")
    dom=cdp('DOM.getDocument',{})['root']['nodeId']
    node=cdp('DOM.querySelector',{'nodeId':dom,'selector':'#localFile'})['nodeId']
    cdp('DOM.setFileInputFiles',{'nodeId':node,'files':[str(fixture)]})
    wait("$('mixDialog').open")
    assert js("$('mixSummary').textContent.includes('3 首') && $('mixSummary').textContent.includes('1 首需要')")
    js("$('mixDialog').close();$('addDialog').close()"); assert (profile/'library.tsv').read_bytes()==original
    # Windows Open with, routed from a second launch to the active desktop instance.
    assert subprocess.run([str(exe),str(fixture)],timeout=8).returncode==0
    wait("$('mixDialog').open"); js("$('mixForm').requestSubmit()")
    wait("$('heading').textContent==='分享:试听'"); assert js("state.index===-1 && [...$('rows').querySelectorAll('.row strong')].map(el=>el.textContent).join(',')==='B,A'")
    shared=export('分享_试听'); assert [s['url'] for s in shared['tracks']]==[s['url'] for s in songs[:3]]
    js('editHistory.undo()'); wait("!lists.items.length && tracks.length===3"); js('editHistory.redo()'); wait('lists.items.length===1 && tracks.length===4');
    preview(fixture); assert js("$('mixName').value==='分享:试听 (2)'"); js("$('mixDialog').close();select('likes');updateLikes([tracks[1].url],true)")
    assert [s['title'] for s in export('我喜欢的')['tracks']]==['B']
    def merge():
        preview(fixture)
        assert js("!$('mixForm').querySelector('select') && $('mixCurrent').dataset.page===page")
        js("$('mixName').value='';$('mixCurrent').click()")  # existing destination ignores new-list name
        try: wait("!pendingImport && !$('mixDialog').open")
        except AssertionError as e:
            raise AssertionError(js("[page,mixToken,pendingImport,pendingMutation,editHistory.busy,$('mixDialog').open,$('mixCurrent').dataset.page,$('toast').textContent]")) from e
    merge()  # B was already liked; preserve it and add the other two only once.
    assert js("page==='likes' && fav.size===3 && lists.items.length===1")
    js("editHistory.undo()"); wait("fav.size===1")
    js("select('library')"); merge()
    assert js("page==='library' && lists.items.length===1 && fav.size===1")
    js("window.mergeId=lists.create('目标',[tracks[0].url,tracks[1].url]);trackOrder.save(mergeId,[tracks[1].url,tracks[0].url]);select(mergeId)")
    merge(); merge()  # second pass is a no-op
    assert js("page===mergeId && lists.items.length===2 && currentList().urls.length===4 && pageOrder().map(i=>tracks[i].name).join(',')==='B,C,A'")
    js("editHistory.undo()"); wait("currentList().urls.length===2")
    js("editHistory.redo()"); wait("currentList().urls.length===4")
    before=(profile/'library.tsv').read_bytes()
    fixture.write_text(json.dumps(dict(doc,tracks=[{'url':'file:///C:/private.wav'}])),encoding='utf8'); js('send("mix-open " + '+json.dumps(fixture.as_uri())+')'); wait("$('toast').textContent.includes('非 HTTP')")
    assert not js("$('mixDialog').open") and (profile/'library.tsv').read_bytes()==before
    fixture.write_text(json.dumps(dict(doc,version=2)),encoding='utf8'); js('send("mix-open " + '+json.dumps(fixture.as_uri())+')'); wait("$('toast').textContent.includes('不支持的歌单格式')")
    assert (profile/'library.tsv').read_bytes()==before
    local=(root/'media/C.wav').as_uri()
    js('select("library");send("add " + '+json.dumps(local)+' + " Private")'); wait('tracks.some(t=>t.name==="Private")')
    mixed=export('全部音乐 (3)'); assert len(mixed['tracks'])==4 and all(t['url'].startswith('http') for t in mixed['tracks'])
    print('OK: folder picker, cancel, 3 toolbar scopes, full order, collision-safe Unicode files, native preview/validation, open-with, dedup, hidden tracks and undo/redo')
