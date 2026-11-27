"""Headless focus transition and preset regression: python tests/focus_presets_ui.py [browser.exe]
Uses the real host DOM and preset factories in a private browser profile. No
native player, audio capture or desktop input. Requires websocket-client.
"""
import base64
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

ROOT = Path(__file__).resolve().parents[1]
browser = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(os.environ.get(
    'PROGRAMFILES(X86)', 'C:/Program Files (x86)')) / 'Microsoft/Edge/Application/msedge.exe'


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
                             '--disable-background-networking', f'--user-data-dir={profile}',
                             f'--remote-debugging-port={port}', 'about:blank'],
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
            return cdp('Runtime.evaluate', {'expression': expression, 'returnByValue': True,
                                            'awaitPromise': True})['result'].get('value')

        cdp('Page.enable')
        # Local test fixtures only: production CSP is not changed.
        cdp('Page.setBypassCSP', {'enabled': True})
        cdp('Page.navigate', {'url': (ROOT / 'ui/index.html').as_uri()})
        until(lambda: js("document.readyState==='complete' && typeof cover==='function'"))
        cdp('Emulation.setEmulatedMedia', {'features': [
            {'name': 'prefers-reduced-motion', 'value': 'no-preference'}]})
        cdp('Emulation.setDeviceMetricsOverride', {'width': 1180, 'height': 776, 'deviceScaleFactor': 1, 'mobile': False})
        js("""
          tracks=Array.from({length: 50}, (_, i) => ({url:`file:///test/${i}.wav`,name:`Track ${i}`}));
          state={...state,index:49,queue:[49],playing:true,paused:false};sync();
          $('rows').scrollTop=320;
          window.testScroll=$('rows').scrollTop;
          window.focusChrome=()=>['.windowBar','footer'].map(s=>document.querySelector(s).getBoundingClientRect().toJSON());
          window.testChrome=JSON.stringify(focusChrome());
        """)
        idle = "!document.documentElement.dataset.focusTransition"
        focused = "!$('focusView').hidden && document.querySelector('.shell').hidden && $('cover').getAttribute('aria-expanded')==='true'"
        listing = "$('focusView').hidden && !document.querySelector('.shell').hidden && $('cover').getAttribute('aria-expanded')==='false'"

        def wait_for_animation(direction):
            until(lambda: js(f"document.getAnimations().some(a=>a.animationName==='focus-scene-{direction}' && a.playState==='running')"))

        js("$('cover').click()")
        wait_for_animation('in')
        assert js(f"{focused} && document.activeElement===$('focusView') && $('focusContent').textContent==='Track 49'")
        assert js("""['#toggle','#focusBack','#windowMax'].every(s=>{
          const el=document.querySelector(s), r=el.getBoundingClientRect();
          return el.contains(document.elementFromPoint(r.x+r.width/2,r.y+r.height/2));
        })""")
        until(lambda: js(idle))
        js("$('focusBack').click()")
        wait_for_animation('out')
        assert js(f"{listing} && $('focusBackground').hidden && !$('focusContent').children.length && document.activeElement===$('cover')")
        until(lambda: js(idle))
        assert js("testScroll>0 && $('rows').scrollTop===testScroll && JSON.stringify(focusChrome())===testChrome")

        esc = "document.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true}))"
        js("document.querySelector('.current .rowCover').click()")
        wait_for_animation('in')
        js(esc)  # Reverse an entrance already in progress.
        wait_for_animation('out')
        js("$('cover').click()")  # Reopen while the exit snapshot is still visible.
        wait_for_animation('in')
        until(lambda: js(f"{idle} && {focused}"))
        js("state.index=-1; sync()")
        wait_for_animation('out')
        until(lambda: js(f"{idle} && {listing} && document.activeElement===$('query')"))
        js(f"state.index=49; sync(); $('cover').click(); {esc}")  # Cancel before the entrance snapshot.
        assert js(f"{idle} && {listing}")
        print('OK: animated focus entry/exit, fixed player and scroll, rapid reversals, and automatic exit')

        cdp('Emulation.setEmulatedMedia', {'features': [{'name': 'prefers-reduced-motion', 'value': 'reduce'}]})
        assert js(f"$('cover').click(); {idle} && {focused}")
        assert js(f"{esc}; {idle} && {listing}")
        cdp('Emulation.setEmulatedMedia', {'features': [{'name': 'prefers-reduced-motion', 'value': 'no-preference'}]})
        assert js(f"""(() => {{ const start=document.startViewTransition; document.startViewTransition=undefined;
          try {{ $('cover').click(); if (!({idle} && {focused})) return false;
            $('cover').click(); return {idle} && {listing}; }} finally {{ document.startViewTransition=start; }} }})()""")
        print('OK: immediate switching with reduced motion or without View Transition support')

        # Resolve the host's preset asset requests locally, with deterministic load delays.
        js("""
          window.disposeLog=[];
          window.releaseAsset={};
          window.originalAppend=document.head.append;
          document.head.append=function(el) {
            const url=new URL(el.src || el.href || location.href), id=url.hostname.split('.')[0];
            if (!url.hostname.endsWith('.focus.bilimusic.local')) return originalAppend.call(this,el);
            if (el.tagName==='LINK') {
              el.href=URL.createObjectURL(new Blob([
                '#focusBackground { position:fixed; inset:0; z-index:-1; background:teal; }'
              ],{type:'text/css'}));
              return originalAppend.call(this,el);
            }
            releaseAsset[id]=()=>{
              el.src=URL.createObjectURL(new Blob([
                `document.currentScript.mountFocus=({root})=>{
                  root.textContent=${JSON.stringify(id)};
                  return {update(){},dispose(){disposeLog.push(${JSON.stringify(id)});}};
                };`
              ],{type:'text/javascript'}));
              originalAppend.call(this,el);
            };
          };
          localStorage.setItem('focusPreset','');
          focusView.discover(['old','new','slow'].map(id=>({id,name:id})));
          window.choosePreset=id=>{
            $('focusPresetTool').click();
            [...$('focusPreset').children].find(el=>el.dataset.id===id).click();
          };
          $('cover').click();
        """)
        until(lambda: js(f"{focused} && {idle}"))
        js("choosePreset('old'); releaseAsset.old()")
        until(lambda: js(f"document.body.dataset.focusPreset==='old' && {idle}"))
        js("choosePreset('new')")
        assert js(f"{idle} && $('focusContent').textContent==='old' && !disposeLog.length && $('focusPresetMenu').hidden")
        js('releaseAsset.new()')
        until(lambda: js("document.getAnimations().filter(a=>a.animationName==='focus-preset-out' && a.playState==='running').length===2"))
        assert js(f"{focused} && $('focusContent').textContent==='new' && disposeLog.join(',')==='old' && document.activeElement===$('focusPresetTool')")
        assert js("""['#toggle','#focusPresetTool'].every(s=>{
          const el=document.querySelector(s), r=el.getBoundingClientRect();
          return el.contains(document.elementFromPoint(r.x+r.width/2,r.y+r.height/2));
        })""")
        until(lambda: js(idle))
        cdp('Emulation.setEmulatedMedia', {'features': [{'name': 'prefers-reduced-motion', 'value': 'reduce'}]})
        assert js(f"choosePreset(''); {idle} && {focused} && $('focusContent').textContent==='Track 49' && !document.body.dataset.focusPreset")
        cdp('Emulation.setEmulatedMedia', {'features': [{'name': 'prefers-reduced-motion', 'value': 'no-preference'}]})
        js("choosePreset('slow'); $('focusBack').click()")
        until(lambda: js(f"{idle} && {listing}"))
        js('releaseAsset.slow()')
        until(lambda: js("![...document.scripts].some(s=>s.src.startsWith('blob:'))"))
        assert js(f"{idle} && {listing} && !$('focusContent').childNodes.length && !document.body.dataset.focusPreset")
        js('document.head.append=originalAppend; focusView.discover([])')
        print('OK: preset swap waits for assets, fades old scene and backdrop, respects reduced motion and discards late loads')

        js("""
          tracks=[{url:'file:///test.wav',name:'月光与唱片 · WaveHalo'}];
          state={...state,index:0,queue:[0],playing:true,paused:false,elapsed:32,duration:240};sync();
          document.querySelector('.shell').hidden=true;
          $('focusView').hidden=$('focusBackground').hidden=false;
          $('focusView').classList.add('playing');
          $('focusPresetTool').hidden=false;
          window.caption='月光与唱片 · WaveHalo';
          window.testState={title:caption, playing:true, progress:.5};
          window.testCover='';
          window.testApi={root:$('focusContent'), background:$('focusBackground'), reduceMotion,
            requestLevels(){}, renderCover(el) {
              if(el.dataset.fixture===testCover) return;
              el.dataset.fixture=testCover;
              el.innerHTML=testCover ? '<img alt="" src="'+testCover+'">' : icon('disc');
            }};
        """)

        def mount(preset):
            js("""window.presetInstance?.dispose?.(); document.querySelector('#testPresetStyle')?.remove();
              for(const el of [testApi.root,testApi.background]) {el.replaceChildren();el.className='';el.removeAttribute('style');}
              document.querySelector('.shell').hidden=true;
              $('focusView').hidden=$('focusBackground').hidden=false;
              $('focusView').classList.toggle('playing',testState.playing);
            """)
            css = (ROOT / f'plugins/{preset}/focus/style.css').read_text(encoding='utf-8')
            script = (ROOT / f'plugins/{preset}/focus/preset.js').as_uri()
            js(f"""(async()=>{{
              document.body.dataset.focusPreset={json.dumps(preset)};
              const style=document.createElement('style');style.id='testPresetStyle';
              style.textContent={json.dumps(css)};document.head.append(style);
              const script=document.createElement('script');script.src={json.dumps(script)};
              await new Promise((resolve,reject)=>{{script.onload=resolve;script.onerror=reject;document.head.append(script);}});
              window.presetInstance=script.mountFocus(testApi);script.remove();presetInstance.update(testState);
            }})()""")
            time.sleep(.5)  # let the entrance transition finish before measuring

        chrome = """(() => {
          const pick=(s,props,pseudo)=>{const c=getComputedStyle(document.querySelector(s),pseudo);return props.map(p=>c.getPropertyValue(p));};
          return [pick('.windowBar',['background-color','color','border-bottom-color']),
            pick('footer',['background-color','color','border-top-color']),
            pick('.windowControls',['background-color','border-top-color','box-shadow']),
            pick('.windowControls button',['color']),pick('.logo',['background-image','color']),
            pick('.windowBrand b',['color','font-family','font-size','font-weight','letter-spacing']),
            pick('.windowBrand .logo svg',['color','stroke','width','height']),
            pick('.toggle',['background-image','color']),pick('.timeline time',['color']),
            pick('.trackText small',['color']),pick('footer .icon',['color']),
            pick('input[type=range]',['background-image']),pick('input[type=range]',['background-color'],'::-webkit-slider-thumb'),
            pick('.focusTool',['background-color','border-top-color','color']),
            pick('#focusPresetMenu',['background-color','border-top-color','color']),
            pick('#focusView',['--menu-accent','--menu-hover','--focus-tool-hover'])];
        })()"""
        cdp('Emulation.setEmulatedMedia', {'features': [{'name': 'forced-colors', 'value': 'none'}]})
        js("focusView.discover([{id:'fixture', name:'Fixture'}]); $('focusPresetTool').click()")
        assert js("!$('focusPresetMenu').hidden && $('focusPreset').scrollHeight === $('focusPreset').clientHeight")
        js("$('focusPresetTool').click(); focusView.discover(Array.from({length:12}, (_,i)=>({id:`fixture-${i}`,name:`Preset ${i}`})))")

        def preset_scrollbar():
            return js("""(() => {
              const menu = $('focusPreset'), tool = $('focusPresetTool');
              tool.click();
              try {
                menu.dispatchEvent(new KeyboardEvent('keydown', {key:'End', bubbles:true}));
                const pseudo = part => getComputedStyle(menu, `::-webkit-scrollbar${part}`);
                if (document.activeElement !== menu.lastElementChild || menu.scrollTop <= 0 ||
                    getComputedStyle(menu).scrollbarWidth !== 'auto' || pseudo('').width !== '4px' ||
                    pseudo('-button').display !== 'none' || pseudo('-thumb').borderRadius !== '999px' ||
                    pseudo('-track').backgroundColor !== 'rgba(0, 0, 0, 0)') return null;
                return pseudo('-thumb').backgroundColor;
              } finally { tool.click(); }
            })()""")

        light_thumb = preset_scrollbar()
        mount('vinyl')
        dark_thumb = preset_scrollbar()
        assert light_thumb and dark_thumb and light_thumb != dark_thumb
        reference = js(chrome)
        caption_style = js("['font-size','font-weight','text-overflow','white-space'].map(p=>getComputedStyle($('focusTitle')).getPropertyValue(p))")
        mount('wavehalo')
        assert preset_scrollbar() == dark_thumb
        print('OK: focus preset scrollbar is slim, arrow-free, theme-aware and keyboard-scrollable on overflow')
        assert js(chrome) == reference, 'WaveHalo chrome should match Vinyl'
        assert js("['font-size','font-weight','text-overflow','white-space'].map(p=>getComputedStyle($('haloTitle')).getPropertyValue(p))") == caption_style
        assert js("getComputedStyle(document.querySelector('.halo-wash')).filter") == 'blur(5px) saturate(0.85)'
        assert js("getComputedStyle(document.querySelector('.halo-wash')).opacity") == '0.78'
        assert js("getComputedStyle(document.querySelector('.halo-wash')).top") != '-50px'
        assert js("!$('focusContent').querySelector('button,details,input')")
        print('OK: Vinyl title-bar icon/text and control/menu/caption parity, original WaveHalo background and no tuning panel')

        for width, height in [(1180, 776), (900, 650), (610, 445)]:
            cdp('Emulation.setDeviceMetricsOverride', {'width': width, 'height': height, 'deviceScaleFactor': 1, 'mobile': False})
            for title in ['月光与唱片', '<img src=x onerror=alert(1)> & 很长的曲名 ' * 30]:
                js(f'testState.title={json.dumps(title)};presetInstance.update(testState)')
                assert js("$('haloTitle').textContent===testState.title && $('haloTitle').title===testState.title && !$('haloTitle').children.length")
                assert js("""(() => {
                  const view=$('focusView').getBoundingClientRect(), stage=document.querySelector('.halo-stage').getBoundingClientRect(),
                    title=$('haloTitle').getBoundingClientRect();
                  return stage.top>=view.top && stage.bottom<=title.top && title.bottom<=view.bottom &&
                    title.left>=view.left && title.right<=view.right &&
                    $('focusContent').scrollWidth<=$('focusContent').clientWidth &&
                    $('focusContent').scrollHeight<=$('focusContent').clientHeight;
                })()"""), (width, height, title[:30])
        print('OK: three window sizes, safe long-title updates and no scene overflow')

        for playing in [False, True]:
            js(f"testState.playing={json.dumps(playing)};$('focusView').classList.toggle('playing',testState.playing);presetInstance.update(testState)")
            time.sleep(.5)
            assert js("getComputedStyle(document.querySelector('.halo-ring')).opacity") == ('1' if playing else '0')
        cdp('Emulation.setEmulatedMedia', {'features': [{'name': 'prefers-reduced-motion', 'value': 'reduce'}]})
        time.sleep(.1)
        mount('wavehalo')
        assert js("['.halo-art','.halo-ring','.halo-stage','.halo-caption'].every(s=>getComputedStyle(document.querySelector(s)).animationName==='none')")
        print('OK: spectrum play/pause fade and reduced-motion fallback')
        if os.environ.get('FOCUS_SCREENSHOT'):
            cdp('Emulation.setEmulatedMedia', {'features': []})
            cdp('Emulation.setDeviceMetricsOverride', {'width': 1180, 'height': 776, 'deviceScaleFactor': 1, 'mobile': False})
            time.sleep(.1)
            js('testState.title=caption')
            mount('wavehalo')
            assert js("!$('focusView').hidden && !!$('haloTitle') && !!document.body.dataset.focusPreset")
            Path(os.environ['FOCUS_SCREENSHOT']).write_bytes(base64.b64decode(cdp('Page.captureScreenshot', {'format': 'png'})['data']))

        cdp('Emulation.setEmulatedMedia', {'features': [{'name': 'prefers-reduced-motion', 'value': 'no-preference'}]})
        js('focusView.close()')
        wait_for_animation('out')
        assert js("""getComputedStyle(document.documentElement,'::view-transition-group(focus-backdrop)').width===`${innerWidth}px` &&
          !$('focusBackground').children.length && !document.body.dataset.focusPreset""")
        until(lambda: js(f"{idle} && {listing}"))
        print('OK: preset backdrop fades from a snapshot after live focus content is cleared')
    finally:
        if ws:
            ws.close()
        proc.terminate()
        proc.wait(timeout=10)
