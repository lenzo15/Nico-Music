"""Verify tray registration and simulate Explorer selecting this test instance's icon."""
import ctypes
from ctypes import wintypes


def desktop_running():
    kernel = ctypes.windll.kernel32
    kernel.OpenEventW.restype = wintypes.HANDLE
    handle = kernel.OpenEventW(0x100000, False, r'Local\BiliMusic.Desktop.Activate.v1')
    if handle:
        kernel.CloseHandle(wintypes.HANDLE(handle))
    return bool(handle)


class IconId(ctypes.Structure):
    _fields_ = [('cbSize', wintypes.DWORD), ('hWnd', wintypes.HWND),
                ('uID', wintypes.UINT), ('guidItem', ctypes.c_byte * 16)]


def tray_registered(hwnd):
    icon = IconId(ctypes.sizeof(IconId), hwnd, 1)
    rect = wintypes.RECT()
    return ctypes.windll.shell32.Shell_NotifyIconGetRect(ctypes.byref(icon), ctypes.byref(rect)) == 0


def check_tray_restore(hwnd, until, activate=None):
    user32 = ctypes.windll.user32
    until(lambda: not user32.IsWindowVisible(hwnd) and tray_registered(hwnd))
    # NOTIFYICON_VERSION_4 packs the activation event and uID into lParam.
    if activate:
        activate()
    else:
        user32.PostMessageW(hwnd, 0x8001, 0, (1 << 16) | 0x400)  # trayCallback / NIN_SELECT
    until(lambda: user32.IsWindowVisible(hwnd) and tray_registered(hwnd))
