"""Headless plugin-dialog test using a loopback stand-in for WebView2's virtual hosts."""
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
import websocket

repo = Path(__file__).resolve().parents[1]
browser = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(os.environ['PROGRAMFILES(X86)'])/'Microsoft/Edge/Application/msedge.exe'
with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
    root = Path(tmp)
    server = ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(SimpleHTTPRequestHandler, directory=tmp))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    host = f'http://127.0.0.1:{server.server_port}'
    helper = (repo/'ui/scripts/collection-confirm.js').read_text(encoding='utf8')
    (root/'bridge.js').write_text(helper.replace('https://${source}.collection.bilimusic.local', host+'/${source}'), encoding='utf8')
    plugin = (repo/'plugins/bilibili/confirm/index.html').read_text(encoding='utf8').replace('https://studio.bilimusic.local', host)
    for action in ('accept', 'cancel', 'escape'):
        folder = root/action
        folder.mkdir()
        click = f'document.getElementById("{action}").click()' if action != 'escape' else 'dispatchEvent(new KeyboardEvent("keydown", {key:"Escape"}))'
        # The driver runs inside the sandbox; the parent cannot inspect the iframe DOM.
        driver = '''addEventListener('message', e => {
          if (e.data?.type === 'collection-preview' && document.getElementById('summary').textContent.includes('3 首歌曲加入「Test」')) ACTION;
        });'''.replace('ACTION', click)
        (folder/'index.html').write_text(plugin.replace('send("ready");', driver+'\nsend("ready");'), encoding='utf8')
    (root/'index.html').write_text('''<!doctype html><meta charset="utf-8"><body><output>WAIT</output><script src="bridge.js"></script><script>
      (async()=>{const log=[];for (const source of ['accept','cancel','escape','missing']) {
        try { log.push(await confirmPluginCollection({source,count:3,play:true},'Test')); }
        catch { log.push('failed-closed'); }
      } document.querySelector('output').textContent=log.join(','); })();</script>''', encoding='utf8')
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
    page = host+'/index.html'
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
        while time.monotonic() < deadline:
            ws.send(json.dumps({'id':1,'method':'Runtime.evaluate','params':{'expression':"document.querySelector('output')?.textContent"}}))
            reply = json.loads(ws.recv())['result']['result'].get('value')
            if reply not in (None, 'WAIT'): break
            time.sleep(.1)
        assert reply == 'true,false,false,failed-closed', reply
        print('OK: Bilibili preview count and destination, accept/cancel/Esc, fail closed without plugin UI')
    finally:
        if ws:
            ws.send(json.dumps({'id':2,'method':'Browser.close'})); ws.close()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill(); proc.wait()
        server.shutdown()
