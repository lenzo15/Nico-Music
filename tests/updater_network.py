"""Live native updater regression (network required):
python tests/updater_network.py build-updates/Release/NicoMusicUpdateTest.exe
Build with -DNICOMUSIC_UPDATE_TESTS=ON. Downloads a small ZIP and cancels a
Windows-package transfer after its first bytes, in a private temporary directory.
Never launches a package or touches the installed player.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

exe = str(Path(sys.argv[1]).resolve())
subprocess.run([exe], check=True, timeout=10)
reply = subprocess.run([exe, '--check'], check=True, capture_output=True, timeout=60)
release = json.loads(reply.stdout)
assert release['draft'] is False and release['prerelease'] is False
print('OK: native HTTPS latest-release check:', release['tag_name'])
assets = [a for a in release['assets'] if a['state'] == 'uploaded' and a['name'].endswith('.zip')
          and 0 < a['size'] <= 2 * 1024 * 1024 and a.get('digest', '').startswith('sha256:')]
assert assets, 'This live test requires a ZIP asset of at most 2 MB with a SHA-256 digest'
asset = min(assets, key=lambda a: a['size'])
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    target = root / '更新 包.zip'
    args = [exe, '--download', asset['browser_download_url'], asset['name'], str(asset['size']), asset['digest'][7:]]
    subprocess.run([*args, str(target)], check=True, timeout=120)
    assert target.stat().st_size == asset['size']
    assert hashlib.sha256(target.read_bytes()).hexdigest() == asset['digest'][7:]
    assert not list(root.glob('*.part'))
    print('OK: native ZIP download, Unicode destination, exact size and SHA-256 verification')
    before = target.read_bytes()
    bad_hash = subprocess.run([*args[:-1], '0' * 64, str(target)], capture_output=True, timeout=120)
    assert bad_hash.returncode == 1 and b'download-error' in bad_hash.stdout, bad_hash
    assert target.read_bytes() == before and not list(root.glob('*.part'))
    print('OK: checksum failure preserves existing destination and removes temporary file')
    bad_size = subprocess.run([*args[:4], str(asset['size'] - 1), *args[5:], str(target)], capture_output=True, timeout=120)
    assert bad_size.returncode == 1 and b'download-error' in bad_size.stdout
    assert target.read_bytes() == before and not list(root.glob('*.part'))
    print('OK: incorrect package size is rejected without replacing an existing file')
    unwritable = subprocess.run([*args, str(root / 'missing' / 'update.zip')], capture_output=True, timeout=10)
    assert unwritable.returncode == 1 and b'download-error' in unwritable.stdout
    print('OK: file creation failure is reported without starting a transfer')
    cancelled = root / 'cancelled.zip'
    subprocess.run([*args, str(cancelled), '--cancel'], check=True, timeout=20)
    assert not cancelled.exists() and not list(root.glob('*.part'))
    print('OK: cancellation, worker shutdown and partial-file cleanup')
    windows = next(a for a in release['assets'] if a['name'].endswith('windows-x64.zip'))
    partial = subprocess.run([exe, '--download', windows['browser_download_url'], windows['name'],
                              str(windows['size']), windows['digest'][7:], str(cancelled), '--cancel-progress'],
                             check=True, capture_output=True, timeout=60)
    assert 0 < int(partial.stdout.split()[1]) < windows['size'], partial.stdout
    assert not cancelled.exists() and not list(root.glob('*.part'))
    print('OK: cancellation during an active Windows x64 transfer aborts the request and removes its partial file')
