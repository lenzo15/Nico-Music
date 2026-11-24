"""python tests/smoke.py build/NicoMusicCLI.exe (requires libmpv and an audio output)"""
import functools
import http.server
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading
import time
import wave

exe = Path(sys.argv[1]).resolve()


class QuietServer(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass


with tempfile.TemporaryDirectory() as tmp:
    home = Path(tmp) / 'profile'
    env = dict(os.environ, NICOMUSIC_HOME=str(home))
    for name, seconds in [('a', 2), ('b', 20)]:
        with wave.open(str(Path(tmp) / (name + '.wav')), 'wb') as wav:
            wav.setparams((1, 2, 8000, 0, 'NONE', 'not compressed'))
            wav.writeframes(bytes(16000 * seconds))
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0),
        functools.partial(QuietServer, directory=tmp))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    try:
        with subprocess.Popen([exe], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, encoding='utf-8') as proc:
            def send(line):
                proc.stdin.write(line + '\n')
                proc.stdin.flush()

            send("repeat one\nrepeat all\nrepeat off")
            send(f'add {base}/a.wav 短曲\nadd {base}/b.wav 长曲\nplay 1')
            time.sleep(4)  # mpv should advance without another terminal command
            send('status\nprev')
            time.sleep(.4)
            send('next')
            time.sleep(.6)
            send('vol 35\npause\nstop\ndel 1\nadd file:///bad\nplay 99\nquit')
            output, _ = proc.communicate(timeout=15)
        for expected in ['播放中  #2', '已选择 #1', '已选择 #2', '已暂停  #2', '音量 35%',
                         'NicoMusic > 单曲循环', 'NicoMusic > 列表循环', 'NicoMusic > 顺序播放', '已停止', '已删除', '用法：add', '数字超出范围']:
            assert expected in output, (expected, output)
        assert proc.returncode == 0, output
        restarted = subprocess.run([exe], env=env, input='list\nquit\n', capture_output=True,
                                   encoding='utf-8', timeout=10, check=True).stdout
        assert re.search(r'我的歌单 · 1 首\s+1\. 长曲', restarted), restarted

        # Even with yt-dlp installed, a page cannot be imported without a source.
        unsupported = base + '/video.html?track=/test.mp3'
        rejected = subprocess.run([exe], env=env, input=f'add {unsupported}\nquit\n',
                                  capture_output=True, encoding='utf8', timeout=8, check=True)
        assert '未安装支持此网页的来源插件' in rejected.stdout
        assert unsupported not in (home / 'library.tsv').read_text(encoding='utf8')
    finally:
        server.shutdown()
        server.server_close()
print('OK: URL validation, library persistence, HTTP playback, automatic advance and controls')
