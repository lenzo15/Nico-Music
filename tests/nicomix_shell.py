"""Read-only Windows shell smoke test after normal launch or --register-nicomix.
Usage: python tests/nicomix_shell.py build/NicoMusic.exe (expects NicoMusic as handler).
"""
import ctypes
from ctypes import wintypes as W
from pathlib import Path
import sys


def association(kind):
    buf = ctypes.create_unicode_buffer(32768)
    size = W.DWORD(len(buf))
    hr = ctypes.windll.shlwapi.AssocQueryStringW(0, kind, '.nicomix', None, buf, ctypes.byref(size))
    assert hr == 0, f'AssocQueryStringW({kind}): {hr:#x}'
    return buf.value


exe = Path(sys.argv[1]).resolve()
assert Path(association(2)).samefile(exe), association(2)  # ShellExecute's chosen executable
assert association(1).casefold() == f'"{exe}" "%1"'.casefold(), association(1)
icon, index = association(15).rsplit(',', 1)
assert Path(icon.strip('"')).samefile(exe) and int(index) == -103, (icon, index)
for size in (16, 32, 48, 256):
    handle = W.HICON()
    assert ctypes.windll.user32.PrivateExtractIconsW(str(exe), int(index), size, size,
                                                     ctypes.byref(handle), None, 1, 0) == 1 and handle.value
    ctypes.windll.user32.DestroyIcon(handle)
print('OK: Windows resolves the NicoMix open command, executable and dedicated document icon')
