"""GUI drive helper for the renderer work (window titled "Forge Sandbox").

  python gui.py cap <out.png>          screenshot of the client area
  python gui.py key <name> [hold_ms]   single letter, or space/tab/enter/esc
  python gui.py look <dx> <dy>         relative mouse move
  python gui.py resize <w> <h>         resize the window (outer size)
  python gui.py minimize | restore
  python gui.py rect                   print client rect
"""
import ctypes
import sys
import time
from ctypes import wintypes

user32 = ctypes.windll.user32
user32.SetProcessDPIAware()
user32.FindWindowW.restype = wintypes.HWND
user32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]

TITLE = "Forge Sandbox"


def find():
    hwnd = user32.FindWindowW(None, TITLE)
    if not hwnd:
        print("NOT RUNNING")
        sys.exit(1)
    return hwnd


def client_box(hwnd):
    rect = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    pt = wintypes.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    return (pt.x, pt.y, pt.x + rect.right, pt.y + rect.bottom)


def focus(hwnd):
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.2)


def main():
    cmd = sys.argv[1]
    hwnd = find()
    if cmd == "cap":
        # PrintWindow: captures ONLY this window's own content, even when it is
        # covered by other windows. Never grabs the screen.
        from PIL import Image

        gdi32 = ctypes.windll.gdi32
        user32.GetDC.restype = wintypes.HDC
        user32.GetDC.argtypes = [wintypes.HWND]
        gdi32.CreateCompatibleDC.restype = wintypes.HDC
        gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
        gdi32.CreateCompatibleBitmap.restype = wintypes.HBITMAP
        gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
        gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
        gdi32.SelectObject.restype = wintypes.HGDIOBJ
        user32.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
        gdi32.GetDIBits.argtypes = [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT,
                                    ctypes.c_void_p, ctypes.c_void_p, wintypes.UINT]
        gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]
        gdi32.DeleteDC.argtypes = [wintypes.HDC]
        user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]

        rect = wintypes.RECT()
        user32.GetClientRect(hwnd, ctypes.byref(rect))
        w, h = rect.right, rect.bottom
        wdc = user32.GetDC(hwnd)
        mdc = gdi32.CreateCompatibleDC(wdc)
        bmp = gdi32.CreateCompatibleBitmap(wdc, w, h)
        gdi32.SelectObject(mdc, bmp)
        user32.PrintWindow(hwnd, mdc, 3)  # PW_CLIENTONLY | PW_RENDERFULLCONTENT

        class BITMAPINFOHEADER(ctypes.Structure):
            _fields_ = [("biSize", wintypes.DWORD), ("biWidth", ctypes.c_long),
                        ("biHeight", ctypes.c_long), ("biPlanes", wintypes.WORD),
                        ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                        ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                        ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wintypes.DWORD),
                        ("biClrImportant", wintypes.DWORD)]

        bi = BITMAPINFOHEADER()
        bi.biSize = ctypes.sizeof(bi)
        bi.biWidth, bi.biHeight, bi.biPlanes, bi.biBitCount = w, -h, 1, 32
        buf = ctypes.create_string_buffer(w * h * 4)
        gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
        Image.frombuffer("RGB", (w, h), buf, "raw", "BGRX", 0, 1).save(sys.argv[2])
        gdi32.DeleteObject(bmp)
        gdi32.DeleteDC(mdc)
        user32.ReleaseDC(hwnd, wdc)
        print("saved", sys.argv[2], w, h)
    elif cmd == "key":
        # Posted straight to the window: never steals focus, and can never
        # type into whatever application the user is working in.
        name = sys.argv[2].lower()
        hold = int(sys.argv[3]) if len(sys.argv) > 3 else 60
        vk = {"space": 0x20, "tab": 0x09, "enter": 0x0D, "esc": 0x1B}.get(name)
        if vk is None:
            vk = ord(name.upper())
        user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        scan = user32.MapVirtualKeyW(vk, 0)
        user32.PostMessageW(hwnd, 0x0100, vk, 1 | (scan << 16))
        time.sleep(hold / 1000.0)
        user32.PostMessageW(hwnd, 0x0101, vk, 1 | (scan << 16) | (0xC0000000))
        print("sent", name, hold)
    elif cmd == "look":
        dx, dy = int(sys.argv[2]), int(sys.argv[3])
        steps = 12
        user32.GetForegroundWindow.restype = wintypes.HWND
        if user32.GetForegroundWindow() != hwnd:
            print("window is not foreground; refusing to inject mouse input")
            sys.exit(2)
        for _ in range(steps):
            user32.mouse_event(0x0001, round(dx / steps), round(dy / steps), 0, 0)
            time.sleep(0.03)
        print("looked", dx, dy)
    elif cmd == "resize":
        w, h = int(sys.argv[2]), int(sys.argv[3])
        # SWP_NOMOVE | SWP_NOZORDER
        user32.SetWindowPos(hwnd, None, 0, 0, w, h, 0x0002 | 0x0004)
        print("resized", w, h)
    elif cmd == "minimize":
        user32.ShowWindow(hwnd, 6)
        print("minimized")
    elif cmd == "restore":
        user32.ShowWindow(hwnd, 9)
        print("restored")
    elif cmd == "close":
        user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
        print("close posted")
    elif cmd == "rect":
        print(client_box(hwnd))


main()
