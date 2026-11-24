"""Runtime installation, discovery, metadata, playback and removal of a second provider."""
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import wave


class RadioServer(SimpleHTTPRequestHandler):
    # Only the provider knows where the stream is. Playback must forward its Referer.
    def do_GET(self):
        if self.path == '/feed.wav':
            if self.headers.get('Referer') != self.server.episode:
                return self.send_error(403)
            self.server.played.set()
        super().do_GET()

    def log_message(self, *_):
        pass

    def copyfile(self, source, outputfile):
        try:
            super().copyfile(source, outputfile)
        except ConnectionError:
            pass


exe = Path(sys.argv[1]).resolve()
repo = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    app = root / '音源测试, with spaces'
    app.mkdir()
    for name in (exe.name, 'libmpv-2.dll'):
        src, dest = exe.parent / name, app / name
        try:
            os.link(src, dest)
        except OSError:
            shutil.copy2(src, dest)
    shutil.copytree(exe.parent / 'plugins/bilibili', app / 'plugins/bilibili')
    radio = app / 'plugins' / 'webradio'
    shutil.copytree(repo / 'examples/plugins/webradio', radio)
    shutil.copy2(app / 'plugins/bilibili/yt-dlp.exe', radio / 'yt-dlp.exe')
    assert not (app / 'yt-dlp.exe').exists()
    manifest = radio / 'source.ini'
    manifest.write_text(manifest.read_text(encoding='utf8').replace('radio.example', '127.0.0.1'), encoding='utf8')
    env = dict(os.environ, NICOMUSIC_HOME=str(root / 'profile'))
    media = root / 'media'; media.mkdir()
    server = ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(RadioServer, directory=media))
    base = f'http://127.0.0.1:{server.server_port}'
    server.episode = base + '/episode.html?p=1&mix=2'
    server.played = threading.Event()
    (media / 'episode.html').write_text('<meta charset="utf-8"><meta property="og:title" content="本地电台节目">'
        '<meta name="radio:stream" content="/feed.wav"><meta property="og:image" content="/cover.jpg">', encoding='utf8')
    with wave.open(str(media / 'feed.wav'), 'wb') as audio:
        audio.setparams((1, 2, 8000, 0, 'NONE', 'not compressed'))
        audio.writeframes(bytes(16000 * 20))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        proc = subprocess.Popen([app / exe.name], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, encoding='utf8')
        try:
            proc.stdin.write('sources\nadd ' + server.episode + '\n'); proc.stdin.flush()
            library = root / 'profile/library.tsv'
            for _ in range(80):
                time.sleep(.15)
                proc.stdin.write('\n'); proc.stdin.flush()
                if library.exists() and '/cover.jpg' in library.read_text(encoding='utf8'):
                    break
            assert library.read_text(encoding='utf8').strip() == f'{server.episode}\t本地电台节目\t{base}/cover.jpg'
            proc.stdin.write('play 1\n'); proc.stdin.flush()
            assert server.played.wait(16), 'Source audio + playback headers did not reach media server'
            time.sleep(.7)
            output, _ = proc.communicate('status\nquit\n', timeout=8)
            assert '@webradio' in output and '@bilibili' in output and '播放中  #1' in output, output
        finally:
            if proc.poll() is None:
                proc.kill(); proc.wait()
        # Put an otherwise working binary at the legacy path. It must not be
        # used in place of the selected plugin's missing private dependency.
        (radio / 'yt-dlp.exe').rename(app / 'yt-dlp.exe')
        # Keep the old library, remove the private runtime, and try both direct selection
        # and a new import. The existing metadata must not make it playable.
        server.played.clear()
        output = subprocess.run([app / exe.name], env=env, input=f'sources\nplay 1\nadd {server.episode}\nquit\n', capture_output=True,
                                encoding='utf8', timeout=8, check=True).stdout
        assert '@webradio' not in output and '@bilibili' in output
        assert output.count('未安装支持此网页的来源插件') == 2 and not server.played.is_set(), output
        # A local-only source needs no home/hosts. Exercise the same metadata
        # and playback hooks with a percent-encoded URI, not just registration.
        local = app / 'plugins/local-link'
        shutil.copytree(repo / 'examples/plugins/local-link', local)
        link = media / '本地 + 100% #试听.BMLINK'
        link.write_text('{"title":"本地插件标题","file":"feed.wav"}', encoding='utf8')
        uri = link.as_uri()
        with subprocess.Popen([app / exe.name], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, encoding='utf8') as cli:
            cli.stdin.write(f'add {uri}\n'); cli.stdin.flush()
            for _ in range(40):
                time.sleep(.15)
                cli.stdin.write('\n'); cli.stdin.flush()
                if f'{uri}\t本地插件标题' in library.read_text(encoding='utf8'): break
            assert f'{uri}\t本地插件标题' in library.read_text(encoding='utf8')
            cli.stdin.write('play 2\n'); cli.stdin.flush()
            time.sleep(1)
            output, _ = cli.communicate('status\nquit\n', timeout=8)
            assert '播放中  #2 本地插件标题' in output, output
        shutil.rmtree(local)
        direct = (media / 'feed.wav').as_uri()
        output = subprocess.run([app / exe.name], env=env, input=f'list\nplay 2\nadd {uri}\nadd {direct}\nquit\n',
                                capture_output=True, encoding='utf8', timeout=8, check=True).stdout
        assert output.count('未安装支持此文件格式的来源插件') == 2 and '2. 本地插件标题' in output, output
        assert f'{direct}\tfeed' in library.read_text(encoding='utf8')
        shutil.rmtree(app / 'plugins')
        rejected = subprocess.run([app / exe.name], env=env, input='add https://www.bilibili.com/video/BV1GJ411x7h7/\nquit\n',
                                  capture_output=True, encoding='utf8', timeout=8, check=True)
        assert '未安装支持此网页的来源插件' in rejected.stdout
    finally:
        server.shutdown(); server.server_close()
print('OK: web and local-file plugins, metadata, playback, persistence and uninstall')
