"""Offline integration: python tests/source_playback.py <NicoMusicCLI.exe>.
Uses real libmpv with a private runtime/profile and no website or desktop input.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import wave

exe = Path(sys.argv[1]).resolve()
repo = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
    root = Path(tmp)
    app, profile = root / 'app', root / 'profile'
    app.mkdir(); profile.mkdir()
    for file in (exe, exe.parent / 'libmpv-2.dll'):
        try:
            os.link(file, app / file.name)
        except OSError:
            shutil.copy2(file, app / file.name)
    demo = app / 'plugins/source-page'
    shutil.copytree(repo / 'examples/plugins/source-page', demo)
    slow = app / 'plugins/slow'
    slow.mkdir()
    (slow / 'source.ini').write_text('api=2\nname=Slow fixture\nfile_extensions=.bmslow\n', encoding='utf8')
    (slow / 'main.lua').write_text(r"""
local entry, job
local utils = require 'mp.utils'
local function mark(text)
    local f = assert(io.open(utils.join_path(mp.get_script_directory(), '../../events.txt'), 'a'))
    f:write(text .. '\n'); f:close()
end
mp.register_event('start-file', function(event) entry = tostring(event.playlist_entry_id) end)
mp.register_script_message('bm-playback-cancel', function(id)
    if job and job.entry == id then
        job.cancelled = true
        mark('cancel')
        mp.abort_async_command(job.request)
    end
end)
mp.add_hook('on_load', 10, function(hook)
    local target = mp.get_property('stream-open-filename', '')
    if not target:match('^bm%-slow://') then return end
    local current = {entry = entry}
    job = current
    hook:defer()
    local ps = os.getenv('SystemRoot') .. '\\System32\\WindowsPowerShell\\v1.0\\powershell.exe'
    current.request = mp.command_native_async({name='subprocess', playback_only=true,
        args={ps, '-NoProfile', '-NonInteractive', '-Command', 'Start-Sleep -Seconds 20'}}, function()
        if job == current then job = nil end
        mark('done')
        if not current.cancelled then
            mp.commandv('script-message', 'bm-playback-error', current.entry, target, 'timeout', 'Stale resolver error')
        end
        hook:cont()
    end)
    mark('started')
end)
""", encoding='utf8')
    wav, blocked, delayed = root / 'test.wav', root / 'test.bmsource', root / 'test.bmslow'
    with wave.open(str(wav), 'wb') as audio:
        audio.setparams((1, 2, 8000, 0, 'NONE', 'not compressed'))
        audio.writeframes(bytes(16000 * 20))
    blocked.write_text('{"file":"test.wav"}', encoding='utf8')
    delayed.touch()
    (profile / 'library.tsv').write_text(''.join(f'{file.as_uri()}\t{file.stem}\t\n' for file in (delayed, blocked, wav)), encoding='utf8')
    env = dict(os.environ, NICOMUSIC_HOME=str(profile))
    with subprocess.Popen([app / exe.name], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, encoding='utf8') as cli:
        def send(command):
            cli.stdin.write(command + '\n'); cli.stdin.flush()

        try:
            send('play 2')
            # Poll enough to observe END_FILE, then start the slow load.
            for _ in range(8):
                time.sleep(.1); send('')
            send('play 1')
            events = app / 'events.txt'
            deadline = time.monotonic() + 5
            while not events.exists() and time.monotonic() < deadline:
                time.sleep(.05)
            assert events.exists(), 'slow resolver never started'
            send('')  # consume start-file before switching tracks
            send('play 3')
            deadline = time.monotonic() + 5
            log = events.read_text()
            while 'done' not in log and time.monotonic() < deadline:
                time.sleep(.05); log = events.read_text()
            assert log.splitlines() == ['started', 'cancel', 'done'], log
            time.sleep(.3)
            output, _ = cli.communicate('status\nquit\n', timeout=8)
            assert '需要登录此来源' in output and 'Stale resolver error' not in output, output
            assert '播放中  #3' in output, output
        finally:
            if cli.poll() is None:
                cli.kill(); cli.wait()
print('OK: structured auth error, cancelling deferred playback and playing the replacement')
