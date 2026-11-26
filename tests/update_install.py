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
    assert all(p.name in ids for p in plugins.iterdir() if (p/'source.ini').is_file() or (p/'focus/preset.ini').is_file())
    wave = {'plugins/wavehalo/focus/'+name: (plugins/'wavehalo/focus'/name).read_bytes()
            for name in ('preset.ini', 'style.css', 'preset.js')}
    def package(extra=None):
        with zipfile.ZipFile(root/'fixture.zip', 'w', zipfile.ZIP_DEFLATED) as z:
            for name, data in {**files, **(extra or {})}.items(): z.writestr('NicoMusic/'+name, data)
        return hashlib.sha256((root/'fixture.zip').read_bytes()).hexdigest()
    def launch(digest, from_version='1.0.2'):
        return subprocess.run([harness,'--fixture-install',root,digest,from_version], capture_output=True, timeout=50)
    def clean(): return not list(root.glob('.nicomusic-update-*'))
    for extra, digest in [({}, '0'*64), ({'ui/../../escape':b'bad'}, None), ({'hotkeys.ini':b'bad'}, None)]:
        hashed = package(extra); result = launch(digest or hashed)
        assert result.returncode != 0 and clean() and exe.read_bytes()==b'original player', result
        assert (root/'hotkeys.ini').read_text()=='original' and not (root/'escape').exists()
    for bad in (b'bad row', b'wavehalo\t1.0.3\nwavehalo\t1.0.4', b'wavehalo\tnext'):
        assert launch(package({catalog_name:bad})).returncode != 0 and clean()
    print('OK: invalid SHA-256, paths and plugin catalogs rejected before shutdown')

    # Force the second replacement to fail after the first one succeeds, then
    # dismiss only the message box belonging to this specific helper process.
    (root/'NicoMusicCLI.exe').mkdir()
    exe.write_bytes(new_exe)  # valid old player to restart after rollback
    # The injected CLI collision happens after a new plugin was placed.
    packages_before_failure = files.copy()
    files = {catalog_name:catalog, **wave, **files}
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
        wait_for(lambda: (root/'relaunched').exists()); (root/'relaunched').unlink()
    finally:
        if parent.poll() is None: parent.kill(); parent.wait()
        if p.poll() is None: p.kill(); p.wait()
    print('OK: failed replacement rolls back previous files and restarts original executable')
    (root/'NicoMusicCLI.exe').rmdir()
    def upgrade(extra, version):
        (root/'relaunched').unlink(missing_ok=True)
        result=launch(package(extra), version)
        assert result.returncode==0, result.stderr.decode('utf-8',errors='replace')
        wait_for(lambda: clean() and (root/'relaunched').exists())

    files = packages_before_failure
    (root/'plugins/bilibili').mkdir()
    (root/'plugins/bilibili/source.ini').write_bytes(b'old plugin file')
    files['plugins/bilibili/source.ini'] = b'updated plugin file'
    files['plugins/custom/updated.ini'] = b'new plugin file'
    skipped = {'plugins/vinyl/focus/preset.ini':b'previously shipped', 'plugins/unlisted/source.ini':b'unknown'}
    upgrade({**wave, **skipped}, '1.0.2')  # no incoming catalog: legacy behavior
    assert not (root/'plugins/wavehalo').exists()
    upgrade({catalog_name:catalog, **wave, **skipped}, '1.0.2')
    for name,data in {**files, **wave}.items(): assert (root/name).read_bytes()==data
    assert not any((root/name).exists() for name in skipped)
    shutil.rmtree(root/'plugins/wavehalo')  # user removes the previously bundled preset
    upgrade({catalog_name:catalog, **wave}, '1.0.3')
    assert not (root/'plugins/wavehalo').exists()
    upgrade({catalog_name:catalog+b'new-test\t1.0.10\n', **wave, **skipped, 'plugins/new-test/source.ini':b'new'}, '1.0.9')
    assert (root/'plugins/new-test/source.ini').read_bytes()==b'new'
    assert not (root/'plugins/wavehalo').exists() and not any((root/name).exists() for name in skipped)
    for name in ('hotkeys.ini','library.tsv','plugins/custom/plugin.ini'): assert (root/name).read_text()=='original'
    print('OK: new bundled plugins added, installed packages refreshed, removed/unlisted plugins stay absent across upgrades')
