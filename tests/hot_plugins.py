"""Used by desktop_playlists.py --hot-plugins, always against an isolated app copy."""
import shutil


def exercise(app, root, profile, js, until):
    def wait(expr):
        try: until(lambda: js(expr))
        except AssertionError as e:
            raise AssertionError((expr, js("[document.body.dataset.focusPreset,$('focusContent').innerHTML,$('focusPreset').textContent,$('sources').textContent,state,performance.getEntriesByType('resource').slice(-4).map(r=>[r.name,r.responseStatus])]"))) from e

    js("send('play 0'); send('volume 43')")
    wait('state.playing && state.duration>=23')
    js("send('pause'); send('seek 7'); $('cover').click()")
    wait("state.paused && state.elapsed>=6 && !$('focusView').hidden")
    folder = app / 'plugins/probe/focus'
    folder.mkdir(parents=True)
    (folder/'preset.ini').write_text('api=1\nname=Probe\n')
    script = 'document.currentScript.mountFocus=({root})=>{window.mounts=(window.mounts||0)+1; root.textContent="ONE";return {update(){},dispose(){window.disposed=(window.disposed||0)+1}}}'
    (folder/'preset.js').write_text(script)
    (folder/'style.css').write_text('#focusContent {--probe: before}')
    wait("$('focusContent').textContent==='ONE'")
    (folder/'preset.js').write_text(script.replace('ONE', 'TWO'))
    (folder/'style.css').write_text('#focusContent {--probe: after}')
    wait("$('focusContent').textContent==='TWO' && getComputedStyle($('focusContent')).getPropertyValue('--probe').trim()==='after'")
    assert js('window.disposed===1 && window.mounts===2')
    folder.rename(root/'removed-focus')
    wait("!document.body.dataset.focusPreset && $('focusPresetTool').hidden && $('focusContent').textContent==='C'")
    (root/'removed-focus').rename(folder)
    wait("$('focusContent').textContent==='TWO'")
    assert js('state.paused && state.volume===43 && state.elapsed>=6 && window.mounts===3')
    print('OK: focus install, active script/style replacement, dispose, unplug and remount')

    source = app/'plugins/testsrc'; source.mkdir()
    (source/'source.ini').write_text('api=2\nname=Probe Source\nhosts=probe.invalid\nhome=https://probe.invalid/\ncollection_paths=/collection/\n')
    lua = 'mp.register_script_message("bm-metadata",function(token,url) mp.commandv("script-message","bm-metadata-result",token,"TITLE","") end)'
    (source/'confirm').mkdir()
    (source/'confirm/index.html').write_text('<script>parent.postMessage("collection-ready","*");onmessage=()=>parent.postMessage("collection-cancel","*")</script>')
    (source/'main.lua').write_text(lua.replace('TITLE','Lookup one'))
    wait("!$('sourcePanel').hidden && $('sources').textContent.includes('Probe Source')")
    wait('state.paused && state.index===0 && state.elapsed>=6 && state.volume===43')
    js("window.confirmResult='waiting';confirmPluginCollection({source:'testsrc',count:2,play:false},'Here').then(v=>window.confirmResult=v)")
    wait('window.confirmResult===false')  # sandboxed iframe served from the newly installed package
    js("send('add https://probe.invalid/one')")
    wait("tracks.some(t=>t.name==='Lookup one')")
    (source/'main.lua').write_text(lua.replace('TITLE','Lookup two'))
    # Observe a revision on the same scan as the Lua edit, before issuing a new lookup.
    (source/'source.ini').write_text((source/'source.ini').read_text().replace('Probe Source','Changed Source'))
    wait("$('sources').textContent.includes('Changed Source')")
    js("send('add https://probe.invalid/two')")
    wait("tracks.some(t=>t.name==='Lookup two')")
    saved = (profile/'library.tsv').read_bytes()
    shutil.move(source,root/'removed-source')
    wait("$('sourcePanel').hidden && tracks.some(t=>t.hidden && t.url==='https://probe.invalid/one')")
    assert (profile/'library.tsv').read_bytes() == saved
    shutil.move(root/'removed-source',source)
    wait("!$('sourcePanel').hidden && tracks.some(t=>t.name==='Lookup two' && !t.hidden)")
    assert js('window.mounts===3 && state.paused && state.index===0 && state.volume===43')
    print('OK: source install, real Lua replacement, remove/reinstall without losing library or preset')
