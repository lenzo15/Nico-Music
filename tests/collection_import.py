"""Offline collection protocol integration test: python tests/collection_import.py path/to/NicoMusicCLI.exe"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    for filename in (exe.name, 'libmpv-2.dll'):
        shutil.copy2(exe.parent / filename, root / filename)
    plugin = root / 'plugins' / 'fixture'
    plugin.mkdir(parents=True)
    plugin.joinpath('source.ini').write_text('api=2\nname=fixture\nhosts=fixture.test\nhome=https://fixture.test/\ncollection_paths=/lists/,#/playlist\n')
    plugin.joinpath('main.lua').write_text(r'''
mp.register_script_message('bm-collection', function(token, target)
    local rows = 'https://fixture.test/b\tSecond\nhttps://fixture.test/a\tFirst\nhttps://fixture.test/b\tDuplicate'
    if target:match('/cancel$') then rows = 'https://fixture.test/cancelled\tCancelled' end
    if target:match('/bad$') then rows = 'https://fixture.test/new\tNew\nfile:///C:/secret.wav\tInvalid' end
    if target:match('/many$') then rows = string.rep('https://fixture.test/new\tNew\n', 501) end
    if target:match('/empty$') then rows = '' end
    mp.commandv('script-message', 'bm-collection-result', token, rows, '')
end)
mp.register_script_message('bm-metadata', function(token)
    mp.commandv('script-message', 'bm-metadata-result', token, '', '')
end)
''', encoding='utf8')
    parent = 'https://fixture.test/lists/'
    # Even an existing entry participates in the result. Never insert the parent URL,
    # duplicate a song, or commit any part of an invalid or oversized reply.
    urls = ['https://fixture.test/a', parent+'cancel', parent+'ok', 'https://fixture.test/#/playlist?id=42', parent+'bad', parent+'many', parent+'empty',
            'https://fixture.test/single?next=/lists/']
    proc = subprocess.run([root / exe.name], input=''.join('add '+url+'\n' + ('y\n' if url.endswith('/ok') or '#/playlist?' in url else '\n' if url.endswith('/cancel') else '') for url in urls)+'list\nquit\n',
                          env=dict(os.environ, NICOMUSIC_HOME=str(root/'profile')), capture_output=True,
                          encoding='utf8', timeout=25, check=True)
    assert proc.stdout.count('合集已导入 2 首') == 2, proc.stdout
    assert proc.stdout.count('[y/N]') == 3 and '已取消导入' in proc.stdout, proc.stdout
    for expected in ('合集包含不支持的歌曲链接', '最多支持 500 首', '合集中没有可导入的歌曲'):
        assert expected in proc.stdout, proc.stdout
    rows = [line.split('\t') for line in (root/'profile/library.tsv').read_text(encoding='utf8').splitlines()]
    assert [r[0] for r in rows] == [urls[0], 'https://fixture.test/b', urls[-1]], rows
    assert rows[1][1] == 'Second', rows
    print('OK: real mpv plugin dispatch, collection results, duplicates, validation, atomic rejection and single-link fallback')
