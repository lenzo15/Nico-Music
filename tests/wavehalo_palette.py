"""Raster test of WaveHalo paint in Edge (Pillow required); thumbnails deliberately have no CORS headers."""
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import threading
from PIL import Image


class Quiet(SimpleHTTPRequestHandler):
    def log_message(self, *_): pass


browser = Path(os.environ.get('PROGRAMFILES(X86)', 'C:/Program Files (x86)')) / 'Microsoft/Edge/Application/msedge.exe'
source = Path(__file__).resolve().parents[1] / 'plugins/wavehalo/focus'
with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
    p = Path(tmp)
    for f in ['preset.js', 'style.css']: shutil.copy2(source / f, p / f)
    colors = [(30, 40, 130), (145, 25, 30), (250, 220, 160), (110, 110, 110), (235, 235, 235)]
    for i, color in enumerate(colors): Image.new('RGB', (64, 64), color).save(p / f'{i}.png')
    server = ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Quiet, directory=tmp))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        (p / 'test.html').write_text('''<link rel="stylesheet" href="style.css"><style>
          *{box-sizing:border-box}body{margin:0}.halo-ring{width:120px;height:120px;position:static;animation:none;stroke-width:54;stroke-opacity:1}
          .halo-ring path {opacity:1;transition:none}.halo-stage{width:120px;animation:none}.halo-art,.halo-rim,.halo-caption,.halo-glow{display:none}
          </style><script src="preset.js" id="preset"></script><div id="root"></div><script>
          const url=new URL(location.href), k=Number(url.searchParams.get('i'));
          const src=k<0?'':'http://localhost:PORT/'+k+'.png';
          const root=document.querySelector('#root');const p=preset.mountFocus({root,background:document.createElement('div'),
            renderCover(el){el.dataset.cover=src},reduceMotion:{matches:false},requestLevels(){}});
          p.update({title:'test',playing:false});root.querySelector('path').setAttribute('d','M-200,0H200');
          document.body.style.background='#555';</script>'''.replace('PORT', str(server.server_port)), encoding='utf8')
        samples = []
        for k in [*range(len(colors)), -1, 404]:
            target = p / 'out.png'
            target.unlink(missing_ok=True)
            subprocess.run([browser, '--headless=new', '--disable-gpu', '--no-first-run', '--hide-scrollbars',
                            f'--user-data-dir={p / "profile"}', '--window-size=400,300', '--virtual-time-budget=1500',
                            f'--screenshot={target}', f'http://127.0.0.1:{server.server_port}/test.html?i={k}'],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
            with Image.open(target) as img: samples.append(img.convert('RGB').getpixel((60, 60)))
        def lum(rgb):
            c = [v/255 for v in rgb]
            return sum(w*(v/12.92 if v <= .04045 else ((v+.055)/1.055)**2.4) for v,w in zip(c,(.2126,.7152,.0722)))
        for cover, ink in zip(colors, samples):
            a,b=sorted([lum(cover),lum(ink)])
            assert (b+.05)/(a+.05)>=3, (cover,ink,samples)
        assert samples[0][0]>samples[0][2]+20 and samples[1][2]>samples[1][0]+20, samples
        assert samples[2][2]>samples[2][0]+20, samples
        assert samples[-2:] == [(255,255,255)]*2, samples
        print('PASS: rendered complementary hues, >=3:1 sample contrast, cross-origin, missing and broken covers:', samples)
    finally:
        server.shutdown(); server.server_close()
