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
    for path in ('library.tsv', 'hotkeys.ini', 'plugins/custom/plugin.ini', 'ui/app.js'):
        dest = root/path; dest.parent.mkdir(parents=True, exist_ok=True); dest.write_text('original')
    files = {'ui/app.js': b'new UI', 'NicoMusic.exe': new_exe, 'NicoMusicCLI.exe': b'cli', 'libmpv-2.dll': b'dll', 'ui/index.html': b'index'}
    def package(extra=None):
        with zipfile.ZipFile(root/'fixture.zip', 'w', zipfile.ZIP_DEFLATED) as z:
            for name, data in {**files, **(extra or {})}.items(): z.writestr('NicoMusic/'+name, data)
        return hashlib.sha256((root/'fixture.zip').read_bytes()).hexdigest()
    def launch(digest):
        return subprocess.run([harness,'--fixture-install',root,digest], capture_output=True, timeout=50)
    def clean(): return not list(root.glob('.nicomusic-update-*'))
    for extra, digest in [({}, '0'*64), ({'ui/../../escape':b'bad'}, None), ({'hotkeys.ini':b'bad'}, None)]:
        hashed = package(extra); result = launch(digest or hashed)
        assert result.returncode != 0 and clean() and exe.read_bytes()==b'original player', result
        assert (root/'hotkeys.ini').read_text()=='original' and not (root/'escape').exists()
    print('OK: invalid SHA-256, unsafe paths and profile entries rejected before shutdown')

    # Force the second replacement to fail after the first one succeeds, then
    # dismiss only the message box belonging to this specific helper process.
    (root/'NicoMusicCLI.exe').mkdir()
    exe.write_bytes(new_exe)  # valid old player to restart after rollback
    package()
    job = root/('.nicomusic-update-{' + str(uuid.uuid4()) + '}'); job.mkdir()
    import shutil
    shutil.copyfile(root/'fixture.zip', job/'package.zip')
    script = Path(__file__).resolve().parents[1]/'src/install-update.ps1'
    (job/'install.ps1').write_text(script.read_text(encoding='utf-8'), encoding='utf-8-sig')
    parent = subprocess.Popen([sys.executable,'-c','import sys;sys.stdin.read()'],stdin=subprocess.PIPE)
    ps = Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
    p = subprocess.Popen([ps,'-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File', job/'install.ps1',str(parent.pid),hashlib.sha256((job/'package.zip').read_bytes()).hexdigest()])
    try:
        wait_for(lambda: (job/'ready').exists() or p.poll() is not None)
        assert p.poll() is None and (root/'ui/app.js').read_text()=='original'
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
        assert p.returncode==1 and clean() and (root/'ui/app.js').read_text()=='original'
        wait_for(lambda: (root/'relaunched').exists()); (root/'relaunched').unlink()
    finally:
        if parent.poll() is None: parent.kill(); parent.wait()
        if p.poll() is None: p.kill(); p.wait()
    print('OK: failed replacement rolls back previous files and restarts original executable')
    (root/'NicoMusicCLI.exe').rmdir()
    files['plugins/custom/updated.ini'] = b'new plugin file'
    result=launch(package({'plugins/uninstalled/source.ini':b'optional'})); assert result.returncode==0, result.stderr.decode('utf-8',errors='replace')
    wait_for(lambda: clean() and (root/'relaunched').exists())
    assert not (root/'plugins/uninstalled').exists()
    for name,data in files.items(): assert (root/name).read_bytes()==data
    for name in ('hotkeys.ini','library.tsv','plugins/custom/plugin.ini'): assert (root/name).read_text()=='original'
    print('OK: native handoff, in-place update, automatic restart, profile/plugin preservation and cleanup')
