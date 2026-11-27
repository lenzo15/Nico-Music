"""Run with the Release NicoMusicUpdateTest.exe. Operates ONLY in a temp fixture.
Tests the actual embedded helper, hash verification, traversal rejection and restart.
"""
import ctypes
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import uuid
import tempfile
import time
import zipfile
import shutil

harness = Path(sys.argv[1]).resolve()
csc = Path(os.environ['SystemRoot']) / 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'

def wait_for(condition, timeout=20):
    end = time.monotonic() + timeout
    while not condition():
        assert time.monotonic() < end, 'timed out waiting for update helper'
        time.sleep(.05)

with tempfile.TemporaryDirectory(prefix='NicoMusic 更新 [test] ') as tmp:
    root = Path(tmp)
    stub = root / 'stub.cs'
    stub.write_text('using System.IO; class Stub { static void Main(){ File.WriteAllText("relaunched", "yes"); } }')
    subprocess.run([csc, '/nologo', '/target:winexe', '/out:'+str(root/'stub.exe'), stub], check=True)
    new_exe = (root/'stub.exe').read_bytes()
    exe = root/'NicoMusic.exe'
    exe.write_bytes(b'original player')
    (root/'fixture-only').touch()
    for path in ('library.tsv', 'hotkeys.ini', 'plugins/custom/plugin.ini', 'ui/scripts/app.js'):
        dest = root/path; dest.parent.mkdir(parents=True, exist_ok=True); dest.write_text('original')
    files = {'ui/scripts/app.js': b'new UI', 'NicoMusic.exe': new_exe, 'NicoMusicCLI.exe': b'cli', 'libmpv-2.dll': b'dll', 'ui/index.html': b'index'}
    plugins = Path(__file__).resolve().parents[1]/'plugins'
    catalog_name = 'plugins/bundled-plugins.tsv'
    catalog = (plugins/'bundled-plugins.tsv').read_bytes()
    ids = {line.split('\t')[0] for line in catalog.decode().splitlines() if line and not line.startswith('#')}
    # Local, independently installed packages need not be in the release catalog.
    assert all((plugins/id/'source.ini').is_file() or (plugins/id/'focus/preset.ini').is_file() for id in ids)
    wave = {'plugins/wavehalo/focus/'+name: (plugins/'wavehalo/focus'/name).read_bytes()
            for name in ('preset.ini', 'style.css', 'preset.js')}
    def package(extra=None):
        with zipfile.ZipFile(root/'fixture.zip', 'w', zipfile.ZIP_DEFLATED) as z:
            for name, data in {**files, **(extra or {})}.items(): z.writestr('NicoMusic/'+name, data)
        return hashlib.sha256((root/'fixture.zip').read_bytes()).hexdigest()
    def launch(digest, from_version='1.0.2'):
        return subprocess.run([harness,'--fixture-install',root,digest,from_version], capture_output=True, timeout=50)
    def clean(): return not list(root.glob('.nicomusic-update-*'))
    def relaunched_and_exited():
        # The stub creates its marker before the Windows image lock is released.
        if not (root/'relaunched').exists(): return False
        try:
            with exe.open('r+b'): return True
        except PermissionError: return False
    for extra, digest in [({}, '0'*64), ({'ui/../../escape':b'bad'}, None), ({'hotkeys.ini':b'bad'}, None)]:
        hashed = package(extra); result = launch(digest or hashed)
        assert result.returncode != 0 and clean() and exe.read_bytes()==b'original player', result
        assert (root/'hotkeys.ini').read_text()=='original' and not (root/'escape').exists()
    for bad in (b'', b'1.01.2', b'1.0', b'1.2.3-beta', b'1.2.3\n2.0.0'):
        assert launch(package({'plugins/bilibili/VERSION':bad})).returncode != 0 and clean()
    for bad in (b'bad row', b'wavehalo\t1.0.3\nwavehalo\t1.0.4', b'wavehalo\tnext'):
        assert launch(package({catalog_name:bad})).returncode != 0 and clean()
    print('OK: invalid SHA-256, paths and plugin catalogs rejected before shutdown')

    # Force the second replacement to fail after the first one succeeds, then
    # dismiss only the message box belonging to this specific helper process.
    (root/'NicoMusicCLI.exe').mkdir()
    exe.write_bytes(new_exe)  # valid old player to restart after rollback
    # The injected CLI collision happens after a new plugin was placed.
    packages_before_failure = files.copy()
    (root/'plugins/existing').mkdir()
    (root/'plugins/existing/retired.lua').write_bytes(b'original package')
    files = {catalog_name:catalog, **wave, 'plugins/existing/new.lua':b'new', **files}
    package()
    job = root/('.nicomusic-update-{' + str(uuid.uuid4()) + '}'); job.mkdir()
    shutil.copyfile(root/'fixture.zip', job/'package.zip')
    script = Path(__file__).resolve().parents[1]/'src/updates/install-update.ps1'
    (job/'install.ps1').write_text(script.read_text(encoding='utf-8'), encoding='utf-8-sig')
    parent = subprocess.Popen([sys.executable,'-c','import sys;sys.stdin.read()'],stdin=subprocess.PIPE)
    ps = Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
    p = subprocess.Popen([ps,'-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File', job/'install.ps1',str(parent.pid),hashlib.sha256((job/'package.zip').read_bytes()).hexdigest(),'1.0.2'])
    try:
        wait_for(lambda: (job/'ready').exists() or p.poll() is not None)
        assert p.poll() is None and (root/'ui/scripts/app.js').read_text()=='original'
        (job/'apply').touch(); parent.communicate(timeout=3)
        user = ctypes.windll.user32
        @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_ssize_t)
        def dismiss(hwnd, _):
            pid = ctypes.c_ulong(); user.GetWindowThreadProcessId(ctypes.c_void_p(hwnd), ctypes.byref(pid))
            if pid.value == p.pid:
                title = ctypes.create_unicode_buffer(128); user.GetWindowTextW(ctypes.c_void_p(hwnd), title, 128)
                if title.value=='NicoMusic 更新': user.PostMessageW(ctypes.c_void_p(hwnd), 0x10, 0, 0)
            return True
        def finished(): user.EnumWindows(dismiss,0); return p.poll() is not None
        wait_for(finished)
        assert p.returncode==1 and clean() and (root/'ui/scripts/app.js').read_text()=='original'
        assert not (root/'plugins/wavehalo').exists() and not (root/catalog_name).exists()
        assert (root/'plugins/existing/retired.lua').read_bytes() == b'original package'
        assert not (root/'plugins/existing/new.lua').exists()
        wait_for(relaunched_and_exited); (root/'relaunched').unlink()
    finally:
        if parent.poll() is None: parent.kill(); parent.wait()
        if p.poll() is None: p.kill(); p.wait()
    print('OK: failed replacement rolls back previous files and restarts original executable')
    (root/'NicoMusicCLI.exe').rmdir()
    def upgrade(extra, version):
        (root/'relaunched').unlink(missing_ok=True)
        result=launch(package(extra), version)
        assert result.returncode==0, result.stderr.decode('utf-8',errors='replace')
        wait_for(lambda: clean() and relaunched_and_exited())

    files = packages_before_failure
    (root/'plugins/bilibili').mkdir()
    (root/'plugins/bilibili/source.ini').write_bytes(b'old plugin file')
    files['plugins/bilibili/source.ini'] = b'updated plugin file'
    (root/'plugins/bilibili/retired.lua').write_bytes(b'obsolete')
    skipped = {'plugins/vinyl/focus/preset.ini':b'previously shipped', 'plugins/unlisted/source.ini':b'unknown'}
    upgrade({**wave, **skipped}, '1.0.2')  # no incoming catalog: legacy behavior
    assert not (root/'plugins/wavehalo').exists()
    upgrade({catalog_name:catalog, **wave, **skipped}, '1.0.2')
    for name,data in {**files, **wave}.items(): assert (root/name).read_bytes()==data
    assert not any((root/name).exists() for name in skipped)
    assert not (root/'plugins/bilibili/retired.lua').exists()
    shutil.rmtree(root/'plugins/wavehalo')  # user removes the previously bundled preset
    upgrade({catalog_name:catalog, **wave}, '1.0.3')
    assert not (root/'plugins/wavehalo').exists()
    upgrade({catalog_name:catalog+b'new-test\t1.0.10\n', **wave, **skipped, 'plugins/new-test/source.ini':b'new'}, '1.0.9')
    assert (root/'plugins/new-test/source.ini').read_bytes()==b'new'
    assert not (root/'plugins/wavehalo').exists() and not any((root/name).exists() for name in skipped)
    for name in ('hotkeys.ini','library.tsv','plugins/custom/plugin.ini'): assert (root/name).read_text()=='original'
    print('OK: new bundled plugins added, installed packages refreshed, removed/unlisted plugins stay absent across upgrades')

    # A single upgrade can contain older, equal, newer and legacy packages.
    incoming = {}
    cases = [('newer','1.9.9','1.10.0',True), ('older','2.0.0','1.99.9',False),
             ('equal','1.2.3','1.2.3',False), ('unversioned','1.0.0',None,False),
             ('first',None,'0.0.0',True), ('damaged','bad','2.0.0',False)]
    for name, local, remote, _ in cases:
        prefix = f'plugins/{name}/'; folder = root/prefix; folder.mkdir()
        if local is not None: (folder/'VERSION').write_bytes(local.encode())
        if remote is not None: incoming[prefix+'VERSION'] = remote.encode()
        (folder/'main.lua').write_bytes(b'old'); incoming[prefix+'main.lua'] = b'new'
        incoming[prefix+'page/icon.png'] = b'new icon'
    upgrade(incoming, '1.0.3')
    for name, local, remote, updated in cases:
        folder = root/'plugins'/name
        assert (folder/'VERSION').read_text() == (remote if updated else local)
        assert (folder/'main.lua').read_bytes() == (b'new' if updated else b'old')
        assert (folder/'page/icon.png').exists() == updated
    print('OK: numeric plugin versions protect whole packages, equal versions, downgrades and legacy updates')
