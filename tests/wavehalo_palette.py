"""Raster test of in-place track changes in Edge. Requires Pillow and websocket-client."""
import base64
import functools
import io
import json
import os
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import urllib.request

from PIL import Image
import websocket


gate = threading.Event()


class Handler(SimpleHTTPRequestHandler):
    def log_message(self, *_): pass

    def do_GET(self):
        if self.path.startswith('/slow.png'):
            gate.wait(10)
        if '.png' in self.path and self.headers.get('Referer'):
            return self.send_error(403)  # cover host requires referrerPolicy=no-referrer
        super().do_GET()


def until(fn):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        result = fn()
        if result: return result
        time.sleep(.04)
    raise AssertionError('timed out: ' + str(fn))


browser = Path(os.environ.get('PROGRAMFILES(X86)', 'C:/Program Files (x86)')) / 'Microsoft/Edge/Application/msedge.exe'
source = Path(__file__).resolve().parents[1] / 'plugins/wavehalo/focus'
with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
    p = Path(tmp)
    for f in ['preset.js', 'style.css']: shutil.copy2(source / f, p / f)
    colors = [(30, 40, 130), (145, 25, 30), (250, 220, 160), (110, 110, 110), (235, 235, 235)]
    for i, color in enumerate(colors): Image.new('RGB', (64, 64), color).save(p / f'{i}.png')
    # Same proportions with the accent at opposite edges: layout must not change the ink.
    for i in [0, 1]:
        pic = Image.new('RGB', (128, 64), colors[0]); x = i * 96
        pic.paste(colors[1], (x, 0, x + 32, 64)); pic.save(p / f'mix{i}.png')
    shutil.copy2(p / '0.png', p / 'slow.png')
    server = ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Handler, directory=tmp))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    other = f'http://localhost:{server.server_port}'
    (p / 'test.html').write_text('''<link rel="stylesheet" href="OTHER/style.css"><style>
      *{box-sizing:border-box}body{margin:0;background:#555}.halo-ring{width:120px;height:120px;position:static;animation:none;stroke-width:54;stroke-opacity:1}
      .halo-ring path {opacity:1;transition:none}.halo-stage{width:120px;animation:none}.halo-art,.halo-rim,.halo-caption,.halo-glow{display:none}
      </style><script src="OTHER/preset.js" id="preset"></script><div id="root"></div><script>
      let src='';const root=document.querySelector('#root');const player=preset.mountFocus({root,background:document.createElement('div'),
        renderCover(el){if(el.dataset.cover===src)return;el.dataset.cover=src;el.replaceChildren();if(!src)return;
          const img=new Image();img.referrerPolicy='no-referrer';img.onerror=()=>img.remove();el.append(img);img.src=src},
        reduceMotion:{matches:false},requestLevels(){}});
      const play=name=>{src=name?'OTHER/'+name+'.png':'';player.update({title:name,playing:true})};play('');
      root.querySelector('path').setAttribute('d','M-200,0H200');</script>'''.replace('OTHER', other), encoding='utf8')
    page = f'http://127.0.0.1:{server.server_port}/test.html'
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
    proc = subprocess.Popen([browser, '--headless=new', '--disable-gpu', '--no-first-run', '--hide-scrollbars',
                             f'--user-data-dir={p / "profile"}', '--window-size=400,300',
                             f'--remote-debugging-port={port}', page], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ws = None
    try:
        def endpoint():
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/json/list', timeout=1) as r:
                    return next((t['webSocketDebuggerUrl'] for t in json.load(r) if t['url'] == page), None)
            except OSError: return None
        ws = websocket.create_connection(until(endpoint), timeout=8, suppress_origin=True)
        def cdp(method, **params):
            ws.send(json.dumps(dict(id=1, method=method, params=params)))
            while True:
                message = json.loads(ws.recv())
                if message.get('id') == 1:
                    result = message['result']; assert 'exceptionDetails' not in result, result
                    return result
        def js(expression): return cdp('Runtime.evaluate', expression=expression, returnByValue=True)['result'].get('value')
        def pixel():
            with Image.open(io.BytesIO(base64.b64decode(cdp('Page.captureScreenshot')['data']))) as im:
                return im.convert('RGB').getpixel((60, 60))
        def play(name): js(f'play({json.dumps(name)})')
        def loaded(): until(lambda: js("document.querySelector('.halo-art img')?.naturalWidth>0 && !!document.querySelector('.halo-ring[data-ink]')"))
        until(lambda: js("typeof play==='function'")); assert pixel() == (255,255,255)
        samples = []
        for i in range(len(colors)):
            play(str(i)); loaded(); samples.append(pixel())
        def lum(rgb):
            c = [v/255 for v in rgb]
            return sum(w*(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4) for v,w in zip(c,(.2126,.7152,.0722)))
        for cover, ink in zip(colors,samples):
            a,b=sorted([lum(cover),lum(ink)]); assert (b+.05)/(a+.05)>=3, (cover,ink,samples)
        assert samples[0][0]>samples[0][2]+20 and samples[1][2]>samples[1][0]+20 and samples[2][2]>samples[2][0]+20,samples
        play('slow'); assert pixel()==samples[-1]  # slow replacement: don't clear a good color
        play('1'); loaded(); assert pixel()==samples[1]; gate.set()
        until(lambda: js("performance.getEntriesByType('resource').some(e=>e.name.endsWith('slow.png'))")); assert pixel()==samples[1]
        play('mix0'); loaded(); mix=pixel(); play('mix1'); loaded(); assert pixel()==mix and mix not in samples, (mix,samples)
        play('broken'); until(lambda: js("!document.querySelector('.halo-art img') && !document.querySelector('.halo-ring[data-ink]')")); assert pixel()==(255,255,255)
        play('2'); loaded(); assert pixel()==samples[2]; play(''); assert pixel()==(255,255,255)
        # Fractional SVG bounds while breathing used to expose the square paint surface.
        # Check all four edges, with glow enabled, not just a pixel on the stroke.
        js("""window.ring=root.querySelector('.halo-ring'); Object.assign(ring.style,{
            position:'absolute',left:'20.25px',top:'12.75px',width:'164.5px',height:'164.5px'});
            root.querySelector('.halo-glow').style.display='block';ring.style.strokeWidth='5';""")
        for name,scale,bass in [('0',.997,False), ('2',1.002,True), ('',1.008,False)]:
            play(name)
            if name: loaded()
            x,y,r,b=js(f"""ring.style.transform='scale({scale})';ring.toggleAttribute('data-bass',{json.dumps(bass)});
              (()=>{{const r=ring.getBoundingClientRect();return [r.left,r.top,r.right,r.bottom].map(Math.round)}})()""")
            with Image.open(io.BytesIO(base64.b64decode(cdp('Page.captureScreenshot')['data']))) as shot:
                im=shot.convert('RGB'); bg=im.getpixel((0,0))
                for box in [(x-2,y-2,r+2,y+3),(x-2,b-3,r+2,b+2),(x-2,y,x+3,b),(r-3,y,r+2,b)]:
                    assert im.crop(box).getextrema()==tuple((c,c) for c in bg), (name,scale,box)
                assert im.getpixel(((x+r)//2,(y+b)//2))!=bg, 'stroke should still be visible'
        print('PASS: clean square bounds during scale/bass changes; same-page switches, slow/stale cover, cache hits, whole-image average and cross-origin contrast:',samples)
    finally:
        gate.set()
        if ws: ws.close()
        proc.terminate(); proc.wait(timeout=10); server.shutdown(); server.server_close()
