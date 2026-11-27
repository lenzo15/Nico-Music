"""Headless regression for the real host dialog and its library decision."""
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

repo = Path(__file__).resolve().parents[1]
browser = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(os.environ['PROGRAMFILES(X86)'])/'Microsoft/Edge/Application/msedge.exe'
with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
    page = (repo/'ui/index.html').as_uri()
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
    proc = subprocess.Popen([browser, '--headless=new', '--disable-gpu', '--no-first-run', f'--user-data-dir={tmp}/profile',
                             f'--remote-debugging-port={port}', page], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ws = None
    try:
        deadline = time.monotonic()+18
        while time.monotonic() < deadline:
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/json/list', timeout=1) as r:
                    url = next(t['webSocketDebuggerUrl'] for t in json.load(r) if t['url'] == page)
                break
            except (OSError, StopIteration): time.sleep(.1)
        else: raise AssertionError('headless test page did not open')
        ws = websocket.create_connection(url, timeout=5, suppress_origin=True)
        def evaluate(expr):
            ws.send(json.dumps({'id':1,'method':'Runtime.evaluate','params':{'expression':expr,'awaitPromise':True,'returnByValue':True}}))
            r = json.loads(ws.recv())['result']
            assert 'exceptionDetails' not in r, r
            return r['result'].get('value')

        while not evaluate('document.readyState === "complete"'):
            assert time.monotonic() < deadline
            time.sleep(.05)
        assert evaluate("""(async () => {
          const commands = [];
          send = msg => { commands.push(msg); return true; };
          renderSources([{id:"demo",name:"示例",hasHome:true}]);
          $("addDialog").showModal();
          for (const choice of ["accept","cancel","dismiss","stale"]) {
            pendingImport = {page:"library"};
            const done = reviewCollection({id:"1",source:"demo",count:3,play:true});
            const dialog = $("sourceImportDialog");
            if (!dialog.open || dialog.querySelector("iframe") ||
                !["示例","3 首","全部音乐","开始播放"].every(s=>$("sourceImportSummary").textContent.includes(s))) return false;
            dispatchEvent(new MessageEvent("message", {data:"collection-accept",origin:"null"}));
            if (commands.length || !dialog.open) return false;
            if (choice === "stale") pendingImport = null;
            if (choice === "dismiss") dialog.close();
            else $(choice === "cancel" ? "sourceImportCancel" : "sourceImportAccept").click();
            await done;
            if (commands.shift() !== "collection-decision 1 " + (choice === "accept" ? "accept" : "cancel")) return false;
          }
          $("addDialog").close();
          const demo = {id:"demo", name:"示例", page:true, revision:"1"};
          renderSources([demo]);
          sourceItems.get("demo").button.click();
          const frame = sourcePage.frame;
          pendingImport = {sourceView:sourcePage, mixName:"新列表", collection:true};
          const done = reviewCollection({id:"2",source:"demo",count:2,play:false});
          renderSources([{...demo,revision:"2"}]); // source itself changed presentation, not identity
          if (!$("sourceImportDialog").open || sourcePage.frame === frame || !$("sourceImportSummary").textContent.includes("新建「新列表」")) return false;
          $("sourceImportAccept").click(); await done;
          if (commands.shift() !== "collection-decision 2 accept") return false;
          pendingImport = {sourceView:sourcePage, mixName:"新列表", collection:true};
          const stale = reviewCollection({id:"3",source:"demo",count:2,play:false});
          renderSources([]); // uninstall while deciding
          await stale;
          return commands.join() === "collection-decision 3 cancel";
        })()""")
        print('OK: host-only confirmation, destination/play summary, dismissal, stale preview and ignored frame decision')
    finally:
        if ws:
            ws.send(json.dumps({'id':2,'method':'Browser.close'})); ws.close()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill(); proc.wait()
