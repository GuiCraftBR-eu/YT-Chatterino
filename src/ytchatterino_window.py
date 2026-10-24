"""Remember normal window bounds and maximized state without primary-screen clamping."""
import ctypes
from ctypes import wintypes
import re
import sys
import tkinter as tk

_GEOMETRY = re.compile(r'(\d{2,5})x(\d{2,5})(?:(\+-\d+|[+-]\d+)(\+-\d+|[+-]\d+))?')


def parse_geometry(value):
    match = _GEOMETRY.fullmatch(str(value))
    if not match:
        return None
    width, height = int(match[1]), int(match[2])
    x, y = (int((v or '0').replace('+-', '-')) for v in (match[3], match[4]))
    if not (100 <= width <= 32000 and 100 <= height <= 32000 and abs(x) <= 100000 and abs(y) <= 100000):
        return None
    return width, height, x, y


def valid_rect(value):
    return (isinstance(value, (list, tuple)) and len(value) == 4
        and all(type(v) is int and abs(v) <= 100000 for v in value)
        and 100 <= value[2]-value[0] <= 32000 and 100 <= value[3]-value[1] <= 32000)


class Placement(ctypes.Structure):
    _fields_ = [('length', wintypes.UINT), ('flags', wintypes.UINT), ('showCmd', wintypes.UINT),
                ('ptMinPosition', wintypes.POINT), ('ptMaxPosition', wintypes.POINT),
                ('rcNormalPosition', wintypes.RECT)]


class WindowState:
    def __init__(self, root, session):
        self.root = root
        session = session if isinstance(session, dict) else {}
        saved = session.get('window', {})
        self.saved = dict(saved) if isinstance(saved, dict) else {}
        geometry = self.saved.get('geometry') or session.get('geometry', '')
        self.geometry = str(geometry) if parse_geometry(geometry) else '520x760+0+0'
        self.has_saved = bool(parse_geometry(geometry) or valid_rect(self.saved.get('normal_rect')))
        self.mode = 'zoomed' if self.saved.get('state') == 'zoomed' else 'normal'
        self.applied = False
        self.applying = False
        self.user32 = None
        if sys.platform == 'win32':
            self.user32 = ctypes.WinDLL('user32', use_last_error=True)
            self.user32.GetAncestor.argtypes = [wintypes.HWND, wintypes.UINT]
            self.user32.GetAncestor.restype = wintypes.HWND
            for name in ('GetWindowPlacement', 'SetWindowPlacement'):
                function = getattr(self.user32, name)
                function.argtypes = [wintypes.HWND, ctypes.POINTER(Placement)]
                function.restype = wintypes.BOOL
            self.user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
            self.user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
        root.bind('<Map>', self.mapped, add='+')
        root.bind('<Configure>', self.observe, add='+')
        root.bind('<Unmap>', self.observe, add='+')

    def handle(self):
        hwnd = self.root.winfo_id()
        return self.user32.GetAncestor(hwnd, 2) or hwnd

    def native(self):
        placement = Placement();placement.length = ctypes.sizeof(Placement)
        if not self.user32.GetWindowPlacement(self.handle(), ctypes.byref(placement)):
            raise OSError(ctypes.get_last_error(), 'Could not read window placement')
        return placement

    def observe(self, event=None):
        if (event is not None and event.widget is not self.root) or self.applying or not self.applied:
            return
        try:
            state = self.root.state()
            if not self.user32 and state == 'normal':
                try:
                    if self.root.attributes('-zoomed'):
                        state = 'zoomed'
                except tk.TclError:
                    pass
            if state in ('normal', 'zoomed'):
                self.mode = state
                if state == 'normal' and parse_geometry(self.root.geometry()):
                    self.geometry = self.root.geometry()
        except tk.TclError:
            pass

    def mapped(self, event):
        if event.widget is not self.root or self.applied:
            return
        self.applied = True
        if not self.has_saved:
            self.observe()
            return
        self.applying = True
        try:
            if self.user32:
                placement = self.native()
                rect = self.saved.get('normal_rect') if self.saved.get('platform') == 'win32' else None
                if not valid_rect(rect):
                    width, height, x, y = parse_geometry(self.geometry)
                    outer, client = wintypes.RECT(), wintypes.RECT()
                    self.user32.GetWindowRect(self.handle(), ctypes.byref(outer))
                    self.user32.GetClientRect(self.handle(), ctypes.byref(client))
                    rect = [x, y, x + max(340, width) + (outer.right-outer.left-client.right),
                            y + max(300, height) + (outer.bottom-outer.top-client.bottom)]
                placement.rcNormalPosition = wintypes.RECT(*rect)
                placement.showCmd = 3 if self.mode == 'zoomed' else 1
                placement.flags = 0
                if not self.user32.SetWindowPlacement(self.handle(), ctypes.byref(placement)):
                    raise OSError(ctypes.get_last_error(), 'Could not restore window placement')
            else:
                width, height, x, y = parse_geometry(self.geometry)
                sw, sh = self.root.winfo_screenwidth(), self.root.winfo_screenheight()
                width, height = min(max(340, width), sw), min(max(300, height), sh)
                if x >= sw or y >= sh or x + width < 0 or y + height < 0:
                    x, y = 40, 40
                self.root.geometry(f'{width}x{height}{x:+d}{y:+d}')
                if self.mode == 'zoomed':
                    try:
                        self.root.state('zoomed')
                    except tk.TclError:
                        self.root.attributes('-zoomed', True)
        except (OSError, tk.TclError, TypeError, ValueError):
            self.root.geometry('520x760+40+40')
        finally:
            self.applying = False

    def snapshot(self):
        self.observe()
        result = {'state':self.mode, 'geometry':self.geometry, 'platform':sys.platform}
        try:
            if self.user32 and self.applied:
                placement = self.native()
                rect = placement.rcNormalPosition
                values = [rect.left, rect.top, rect.right, rect.bottom]
                if valid_rect(values):
                    result['normal_rect'] = values
                if placement.showCmd in (1, 3):
                    result['state'] = self.mode = 'zoomed' if placement.showCmd == 3 else 'normal'
            elif not self.applied and self.has_saved:
                result.update(self.saved)
        except (OSError, tk.TclError):
            pass
        return result
