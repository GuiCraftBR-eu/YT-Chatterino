"""YT-Chatterino: a small Chatterino-style multi-tab viewer for YouTube live chat.

Requires only Python 3 (Tkinter is included on Windows). No pip installs.
Run:  python ytchatterino.py
"""
import sys
if __name__ == "__main__" and len(sys.argv) == 3 and sys.argv[1] == "--apply-update":
    from ytchatterino_update import run_helper
    raise SystemExit(run_helper(sys.argv[2]))

import hashlib
import base64
import ctypes
import ctypes.wintypes
import json
import os
import queue
import re
import socket
import ssl
import struct
import sys
import threading
import time
import tkinter as tk
import unicodedata
import tkinter.font as tkfont
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
import uuid
import weakref
import zlib
from collections import OrderedDict, deque
from concurrent.futures import ThreadPoolExecutor
from functools import lru_cache
from datetime import datetime, timedelta, timezone
from tkinter import messagebox, simpledialog, ttk
from ytchatterino_i18n import tr, set_language, install_ui, number, language, display_time, set_datetime_formats
from ytchatterino_features import FeatureController, parse_twitch_gifs, fit_photo
from ytchatterino_chat import message_limit, parse_privmsg
from ytchatterino_youtube import YouTubeWebClient, YouTubeReadError, WEB_POLL_SECONDS
from ytchatterino_window import WindowState

DATA_HOME = os.path.abspath(os.environ.get("YTCHATTERINO_DATA_DIR") or os.path.expanduser("~"))
CONFIG_PATH = os.path.join(DATA_HOME, ".ytchatterino.json")
BG, FG, PANEL, DIM = "#000000", "#efeff1", "#000000", "#8b8b94"
EDGE = "#333333"  # only used for the thin outline of the input box
TAB_BLUE, ACCENT, ALT, HEADER_BG = "#4a9cdb", "#2aa8ff", "#0e0e0e", "#111111"
PALETTE = ["#ff7f50", "#1e90ff", "#00ff7f", "#daa520", "#ff69b4", "#a070ff",
           "#00ced1", "#9acd32", "#ff5a36", "#5f9ea0", "#e08a3c", "#e05d5d"]
APP_VERSION = "1.3.0"
QUOTA_PATH = os.path.join(DATA_HOME, ".ytchatterino_quota.json")
LOG_PATH = os.path.join(DATA_HOME, ".ytchatterino_error.log")
APP_ICON_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ytchatterino.ico")
APP_USER_MODEL_ID = "Guilherme.YTChatterino"
DAILY_LIMIT = 10000  # default YouTube Data API quota per day
VIEWER_REFRESH_SECONDS = 300  # metadata refresh cadence for public readers
TWITCH_CLIENT_ID = "vbyiljlhljby8n1uwpjkpo4h5a6iiq"
TWITCH_SCOPES = "chat:read"
EMOTE_CACHE_SECONDS = 6 * 60 * 60
EMOTE_IMAGE_CACHE_BYTES = 24 * 1024 * 1024
CATALOG_POOL = ThreadPoolExecutor(max_workers=3, thread_name_prefix="chat-catalog")
IMAGE_POOL = ThreadPoolExecutor(max_workers=4, thread_name_prefix="chat-image")
CUSTOM_EMOTE = re.compile(r"(?<!\w):[A-Za-z_][\w\-]+:(?!\w)")
UNI_EMOJI = re.compile("[\U0001F000-\U0001FAFF\u2600-\u27BF\u2B50\u2B06\u2764\u2B1B\u2B1C\uFE0F\u200D]+")
EMOTE_SPLIT = re.compile("(" + CUSTOM_EMOTE.pattern + "|" + UNI_EMOJI.pattern + ")")

try:
    from zoneinfo import ZoneInfo
    _PT = ZoneInfo("America/Los_Angeles")
except Exception:  # tzdata missing on Windows: approximate with UTC-8
    _PT = timezone(timedelta(hours=-8))


# ---------------------------------------------------------------- helpers
UNMAPPED_PATH = os.path.join(os.path.expanduser("~"), ".ytchatterino_unmapped_emotes.txt")
_unmapped_seen = set()


def _gif_composited_png_frames(data, max_frames=240, with_timing=False, output_limit=None):
    """Decode GIF frames and return full-canvas PNGs with disposal applied."""
    if len(data) < 13 or data[:6] not in (b"GIF87a", b"GIF89a"):
        return []
    width, height = struct.unpack_from("<HH", data, 6)
    if not width or not height or width > 1024 or height > 1024:
        return []
    packed = data[10]
    offset = 13

    def read_palette(size):
        nonlocal offset
        end = offset + size * 3
        if end > len(data):
            raise ValueError("Truncated GIF color table")
        palette = [tuple(data[i:i + 3]) for i in range(offset, end, 3)]
        offset = end
        return palette

    global_palette = read_palette(1 << ((packed & 7) + 1)) if packed & 0x80 else []
    canvas = bytearray(width * height * 4)
    frames = []
    timings = []
    frame_delay = 90
    disposal = 0
    transparent_index = None

    def read_subblocks(pos):
        chunks = []
        while pos < len(data):
            size = data[pos]
            pos += 1
            if size == 0:
                return b"".join(chunks), pos
            end = pos + size
            if end > len(data):
                raise ValueError("Truncated GIF data block")
            chunks.append(data[pos:end])
            pos = end
        raise ValueError("Unterminated GIF data blocks")

    def decode_lzw(compressed, minimum_size, expected):
        if not 2 <= minimum_size <= 8:
            raise ValueError("Invalid GIF LZW code size")
        clear_code = 1 << minimum_size
        end_code = clear_code + 1
        code_size = minimum_size + 1
        next_code = end_code + 1
        table = [bytes([value]) for value in range(clear_code)] + [None, None]
        bit_offset = 0
        output = bytearray()
        previous = None

        def read_code(size):
            nonlocal bit_offset
            if bit_offset + size > len(compressed) * 8:
                return None
            byte = bit_offset >> 3
            shift = bit_offset & 7
            value = int.from_bytes(compressed[byte:byte + 3], "little") >> shift
            bit_offset += size
            return value & ((1 << size) - 1)

        while len(output) < expected:
            code = read_code(code_size)
            if code is None or code == end_code:
                break
            if code == clear_code:
                table = [bytes([value]) for value in range(clear_code)] + [None, None]
                code_size = minimum_size + 1
                next_code = end_code + 1
                previous = None
                continue
            if code < next_code and code < len(table) and table[code] is not None:
                entry = table[code]
            elif code == next_code and previous is not None:
                entry = previous + previous[:1]
            else:
                raise ValueError("Invalid GIF LZW code")
            output.extend(entry)
            if previous is not None and next_code < 4096:
                if next_code == len(table):
                    table.append(previous + entry[:1])
                else:
                    table[next_code] = previous + entry[:1]
                next_code += 1
                if next_code == (1 << code_size) and code_size < 12:
                    code_size += 1
            previous = entry
        if len(output) < expected:
            raise ValueError("Incomplete GIF frame")
        return output[:expected]

    def png_chunk(kind, payload):
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xffffffff)

    def make_png():
        scale = max(1, (max(width, height) + output_limit - 1) // output_limit) if output_limit else 1
        out_width, out_height = (width + scale - 1) // scale, (height + scale - 1) // scale
        rows = bytearray()
        stride = width * 4
        for y in range(0, height, scale):
            rows.append(0)
            start = y * stride
            if scale == 1:
                rows.extend(canvas[start:start + stride])
            else:
                for x in range(0, width, scale):
                    rows.extend(canvas[start + x * 4:start + x * 4 + 4])
        return (b"\x89PNG\r\n\x1a\n"
                + png_chunk(b"IHDR", struct.pack(">IIBBBBB", out_width, out_height, 8, 6, 0, 0, 0))
                + png_chunk(b"IDAT", zlib.compress(bytes(rows)))
                + png_chunk(b"IEND", b""))

    try:
        while offset < len(data) and len(frames) < max_frames:
            marker = data[offset]
            offset += 1
            if marker == 0x3B:
                break
            if marker == 0x21:
                if offset >= len(data):
                    break
                label = data[offset]
                offset += 1
                if label == 0xF9:
                    if offset + 6 > len(data) or data[offset] != 4:
                        return []
                    flags = data[offset + 1]
                    delay = struct.unpack_from("<H", data, offset + 2)[0]
                    frame_delay = max(20, delay * 10) if delay else 100
                    disposal = (flags >> 2) & 7
                    transparent_index = data[offset + 4] if flags & 1 else None
                    offset += 6
                else:
                    _, offset = read_subblocks(offset)
                continue
            if marker != 0x2C or offset + 9 > len(data):
                return []
            left, top, frame_width, frame_height = struct.unpack_from("<HHHH", data, offset)
            frame_packed = data[offset + 8]
            offset += 9
            if not frame_width or not frame_height or left + frame_width > width or top + frame_height > height:
                return []
            palette = (read_palette(1 << ((frame_packed & 7) + 1))
                       if frame_packed & 0x80 else global_palette)
            if not palette or offset >= len(data):
                return []
            minimum_size = data[offset]
            compressed, offset = read_subblocks(offset + 1)
            indices = decode_lzw(compressed, minimum_size, frame_width * frame_height)
            if frame_packed & 0x40:
                row_order = list(range(0, frame_height, 8))
                row_order += list(range(4, frame_height, 8))
                row_order += list(range(2, frame_height, 4))
                row_order += list(range(1, frame_height, 2))
            else:
                row_order = range(frame_height)
            previous_canvas = bytes(canvas) if disposal == 3 else None
            cursor = 0
            for row in row_order:
                for column in range(frame_width):
                    color_index = indices[cursor]
                    cursor += 1
                    if color_index == transparent_index:
                        continue
                    if color_index >= len(palette):
                        raise ValueError("GIF palette index out of range")
                    red, green, blue = palette[color_index]
                    pixel = ((top + row) * width + left + column) * 4
                    canvas[pixel:pixel + 4] = bytes((red, green, blue, 255))
            frames.append(make_png())
            timings.append(frame_delay)
            frame_delay = 90
            if disposal == 2:
                for row in range(top, top + frame_height):
                    start = (row * width + left) * 4
                    canvas[start:start + frame_width * 4] = bytes(frame_width * 4)
            elif disposal == 3 and previous_canvas is not None:
                canvas[:] = previous_canvas
            disposal = 0
            transparent_index = None
    except (IndexError, struct.error, ValueError, zlib.error):
        return []
    return (frames, timings) if with_timing else frames

# Names without hyphens (YouTube's special emotes). Approximate matches.
SPECIAL = {
    "yt": "\u25B6\uFE0F", "buffering": "\u23F3", "oops": "\U0001F64A", "learning": "\U0001F4DA",
    "virtualhug": "\U0001F917", "sanitizer": "\U0001F9F4", "washhands": "\U0001F9FC",
    "hydrate": "\U0001F4A7", "stayhome": "\U0001F3E0", "shelterin": "\U0001F3E0",
    "takeout": "\U0001F961", "videocall": "\U0001F4BB", "yougotthis": "\U0001F4AA",
    "thanksdoc": "\U0001FA7A", "elbowbump": "\U0001F91C", "elbowcough": "\U0001F927",
    "dothefive": "\U0001F590\uFE0F", "goodvibes": "\u270C\uFE0F", "socialdist": "\u2194\uFE0F",
    "chillwcat": "\U0001F431", "chillwdog": "\U0001F436",
}

# YouTube's built-in emojis have names like face-blue-smiling or body-blue-raised-arms.
# These are matched by keyword to the closest Unicode emoji (first match wins).
KEYWORDS = [
    ("heart-shape-eyes", "\U0001F60D"), ("heart-eyes", "\U0001F60D"), ("star-eyes", "\U0001F929"),
    ("tears-of-joy", "\U0001F602"), ("crying", "\U0001F62D"), ("tears", "\U0001F622"),
    ("biting-nails", "\U0001F62C"), ("wide-eyed", "\U0001F632"), ("droopy-eyes", "\U0001F61E"),
    ("frowning", "\u2639\uFE0F"), ("tongue", "\U0001F61B"), ("winking", "\U0001F609"),
    ("kiss", "\U0001F618"), ("laugh", "\U0001F606"), ("sleep", "\U0001F634"),
    ("scream", "\U0001F631"), ("sunglasses", "\U0001F60E"), ("angry", "\U0001F620"),
    ("raised-eyebrow", "\U0001F928"), ("thinking", "\U0001F914"), ("party", "\U0001F973"),
    ("sweat", "\U0001F605"), ("grin", "\U0001F601"), ("smiling", "\U0001F600"), ("smile", "\U0001F600"),
    ("raised-arms", "\U0001F64C"), ("waving", "\U0001F44B"), ("clapping", "\U0001F44F"),
    ("thumbs-up", "\U0001F44D"), ("thumbs-down", "\U0001F44E"), ("praying", "\U0001F64F"),
    ("flex", "\U0001F4AA"), ("muscle", "\U0001F4AA"), ("covering-eyes", "\U0001F648"),
    ("100", "\U0001F4AF"), ("fire", "\U0001F525"), ("heart", "\u2764\uFE0F"),
]


def shortcode_to_unicode(name):
    """Best-effort conversion of a YouTube :shortcode: to a Unicode emoji, or None."""
    key = name.lower()
    if key in SPECIAL:
        return SPECIAL[key]
    words = key.replace("-", " ").replace("_", " ").strip().upper()
    for cand in (words, words + " FACE"):
        try:
            return unicodedata.lookup(cand)
        except KeyError:
            pass
    if "-" in key:
        for kw, emoji in KEYWORDS:
            if kw in key:
                return emoji
    return None


def log_unmapped(name):
    if name in _unmapped_seen:
        return
    _unmapped_seen.add(name)
    try:
        with open(UNMAPPED_PATH, "a", encoding="utf-8") as f:
            f.write(f":{name}:\n")
    except Exception:
        pass


def load_config():
    try:
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            value = json.load(f)
        return value if isinstance(value, dict) else {}
    except Exception:
        return {}


def save_config(cfg):
    temp_path = CONFIG_PATH + ".tmp"
    try:
        os.makedirs(DATA_HOME, exist_ok=True)
        with open(temp_path, "w", encoding="utf-8") as file:
            json.dump(cfg, file, ensure_ascii=False)
            file.flush()
            os.fsync(file.fileno())
        os.replace(temp_path, CONFIG_PATH)
    except Exception as exc:
        try:
            os.remove(temp_path)
        except OSError:
            pass
        messagebox.showerror(tr("Could not save settings"), tr(str(exc)))


class ApiKeyStore:
    """Use Windows Credential Manager for the API key; retain a non-Windows fallback."""

    TARGET = "YT-Chatterino/YouTube Data API"

    class _Credential(ctypes.Structure):
        _fields_ = [
            ("Flags", ctypes.wintypes.DWORD),
            ("Type", ctypes.wintypes.DWORD),
            ("TargetName", ctypes.wintypes.LPWSTR),
            ("Comment", ctypes.wintypes.LPWSTR),
            ("LastWritten", ctypes.wintypes.FILETIME),
            ("CredentialBlobSize", ctypes.wintypes.DWORD),
            ("CredentialBlob", ctypes.c_void_p),
            ("Persist", ctypes.wintypes.DWORD),
            ("AttributeCount", ctypes.wintypes.DWORD),
            ("Attributes", ctypes.c_void_p),
            ("TargetAlias", ctypes.wintypes.LPWSTR),
            ("UserName", ctypes.wintypes.LPWSTR),
        ]

    def __init__(self):
        self.windows = sys.platform == "win32"

    @staticmethod
    def _linux_keyring():
        import keyring
        backend = keyring.get_keyring()
        secure = ("keyring.backends.SecretService", "keyring.backends.kwallet", "keyring.backends.libsecret")
        if backend.__class__.__module__.startswith("keyring.backends.chainer"):
            backend = next((item for item in backend.backends if item.__class__.__module__.startswith(secure)), backend)
        if not backend.__class__.__module__.startswith(secure):
            raise OSError("Unlock a Secret Service / KWallet keyring to save sign-in securely.")
        return backend

    def read(self):
        if not self.windows:
            try:
                return self._linux_keyring().get_password(self.TARGET, "YT-Chatterino")
            except Exception:
                return None
        advapi = ctypes.WinDLL("Advapi32", use_last_error=True)
        ptr = ctypes.POINTER(self._Credential)()
        advapi.CredReadW.argtypes = [ctypes.wintypes.LPCWSTR, ctypes.wintypes.DWORD,
                                     ctypes.wintypes.DWORD,
                                     ctypes.POINTER(ctypes.POINTER(self._Credential))]
        advapi.CredReadW.restype = ctypes.wintypes.BOOL
        if not advapi.CredReadW(self.TARGET, 1, 0, ctypes.byref(ptr)):
            if ctypes.get_last_error() == 1168:
                return None
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            raw = ctypes.string_at(ptr.contents.CredentialBlob,
                                   ptr.contents.CredentialBlobSize)
            return raw.decode("utf-16-le")
        finally:
            advapi.CredFree.argtypes = [ctypes.c_void_p]
            advapi.CredFree.restype = None
            advapi.CredFree(ptr)

    def write(self, value):
        if not self.windows:
            self._linux_keyring().set_password(self.TARGET, "YT-Chatterino", value)
            return True
        raw = value.encode("utf-16-le")
        blob = ctypes.create_string_buffer(raw)
        cred = self._Credential()
        cred.Type = 1
        cred.TargetName = self.TARGET
        cred.CredentialBlobSize = len(raw)
        cred.CredentialBlob = ctypes.cast(blob, ctypes.c_void_p)
        cred.Persist = 2
        cred.UserName = "YT-Chatterino"
        advapi = ctypes.WinDLL("Advapi32", use_last_error=True)
        advapi.CredWriteW.argtypes = [ctypes.POINTER(self._Credential), ctypes.wintypes.DWORD]
        advapi.CredWriteW.restype = ctypes.wintypes.BOOL
        if not advapi.CredWriteW(ctypes.byref(cred), 0):
            raise ctypes.WinError(ctypes.get_last_error())
        return True


class TwitchTokenStore(ApiKeyStore):
    """Keep Twitch OAuth tokens encrypted for the current Windows user."""

    TARGET = "YT-Chatterino/Twitch OAuth" + ("/" + hashlib.sha256(DATA_HOME.encode()).hexdigest()[:16] if os.environ.get("YTCHATTERINO_DATA_DIR") else "")
    DPAPI_PATH = os.path.join(DATA_HOME, ".ytchatterino_twitch.dpapi")

    class _DataBlob(ctypes.Structure):
        _fields_ = [("cbData", ctypes.wintypes.DWORD),
                    ("pbData", ctypes.POINTER(ctypes.c_ubyte))]

    @staticmethod
    def _protect(data, decrypt=False):
        if sys.platform != "win32":
            raise OSError("Windows Data Protection is only available on Windows.")
        crypt32 = ctypes.WinDLL("Crypt32", use_last_error=True)
        kernel32 = ctypes.WinDLL("Kernel32", use_last_error=True)
        blob_type = TwitchTokenStore._DataBlob
        source_buffer = ctypes.create_string_buffer(data)
        source = blob_type(len(data), ctypes.cast(source_buffer, ctypes.POINTER(ctypes.c_ubyte)))
        output = blob_type()
        flags = 0x1  # CRYPTPROTECT_UI_FORBIDDEN
        if decrypt:
            crypt32.CryptUnprotectData.argtypes = [
                ctypes.POINTER(blob_type), ctypes.POINTER(ctypes.wintypes.LPWSTR),
                ctypes.POINTER(blob_type), ctypes.c_void_p, ctypes.c_void_p,
                ctypes.wintypes.DWORD, ctypes.POINTER(blob_type)]
            crypt32.CryptUnprotectData.restype = ctypes.wintypes.BOOL
            ok = crypt32.CryptUnprotectData(ctypes.byref(source), None, None, None,
                                            None, flags, ctypes.byref(output))
        else:
            crypt32.CryptProtectData.argtypes = [
                ctypes.POINTER(blob_type), ctypes.wintypes.LPCWSTR,
                ctypes.POINTER(blob_type), ctypes.c_void_p, ctypes.c_void_p,
                ctypes.wintypes.DWORD, ctypes.POINTER(blob_type)]
            crypt32.CryptProtectData.restype = ctypes.wintypes.BOOL
            ok = crypt32.CryptProtectData(ctypes.byref(source), "YT-Chatterino Twitch sign-in",
                                          None, None, None, flags, ctypes.byref(output))
        if not ok:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            return ctypes.string_at(output.pbData, output.cbData)
        finally:
            kernel32.LocalFree.argtypes = [ctypes.c_void_p]
            kernel32.LocalFree.restype = ctypes.c_void_p
            kernel32.LocalFree(ctypes.cast(output.pbData, ctypes.c_void_p))

    def _read_dpapi(self):
        try:
            with open(self.DPAPI_PATH, "rb") as f:
                encrypted = f.read()
        except FileNotFoundError:
            return None
        return self._protect(encrypted, decrypt=True).decode("utf-8")

    def read(self):
        if not self.windows:
            return super().read()
        # Prefer our encrypted fallback when present; Credential Manager writes can
        # be denied by some Windows profiles even though reads are allowed.
        try:
            saved = self._read_dpapi()
            if saved:
                return saved
        except Exception:
            pass
        try:
            return super().read()
        except Exception:
            return None

    def write(self, value):
        if not self.windows:
            return super().write(value)
        try:
            super().write(value)
        except Exception:
            encrypted = self._protect(value.encode("utf-8"))
            temp_path = self.DPAPI_PATH + ".tmp"
            with open(temp_path, "wb") as f:
                f.write(encrypted)
            os.replace(temp_path, self.DPAPI_PATH)
            return True
        try:
            os.remove(self.DPAPI_PATH)
        except OSError:
            pass
        return True


def clock():
    return datetime.now().strftime("%H:%M:%S")


def video_id_from(text):
    text = text.strip().strip('"')
    if re.fullmatch(r"[\w-]{11}", text):
        return text
    u = urllib.parse.urlparse(text if "://" in text else "https://" + text)
    if "youtu.be" in u.netloc.lower():
        return u.path.strip("/").split("/")[0] or None
    q = urllib.parse.parse_qs(u.query)
    if "v" in q:
        return q["v"][0]
    m = re.match(r"/(?:live|shorts|embed)/([\w-]{11})", u.path)
    return m.group(1) if m else None


def twitch_login_from(text):
    raw = text.strip().strip('"')
    if raw.startswith("@"):
        candidate = raw[1:]
    elif raw.lower().startswith("twitch:"):
        candidate = raw.split(":", 1)[1].strip()
    else:
        url = raw if "://" in raw else "https://" + raw
        parsed = urllib.parse.urlparse(url)
        if parsed.netloc.lower().removeprefix("www.") not in ("twitch.tv", "m.twitch.tv"):
            return None
        parts = [part for part in parsed.path.split("/") if part]
        if parts and parts[0].lower() == "popout" and len(parts) >= 2:
            candidate = parts[1]
        elif not parts or parts[0].lower() in {
                "directory", "downloads", "jobs", "login", "p", "settings", "signup",
                "subscriptions", "turbo", "videos", "wallet"}:
            return None
        else:
            candidate = parts[0]
    return candidate.lower() if re.fullmatch(r"[A-Za-z0-9_]{1,25}", candidate) else None


class TwitchOAuthError(RuntimeError):
    def __init__(self, error, message=""):
        self.error = error or "oauth_error"
        self.message = message or self.error
        super().__init__(self.message)


def twitch_oauth_request(url, data=None, access_token=None):
    headers = {"Accept": "application/json"}
    payload = None
    if data is not None:
        payload = urllib.parse.urlencode(data).encode("utf-8")
        headers["Content-Type"] = "application/x-www-form-urlencoded"
    if access_token:
        headers["Authorization"] = f"OAuth {access_token}"
    request = urllib.request.Request(url, data=payload, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            return json.load(response)
    except urllib.error.HTTPError as exc:
        try:
            details = json.loads(exc.read().decode("utf-8", errors="replace"))
        except Exception:
            details = {}
        message = details.get("message") or f"HTTP {exc.code}"
        code = details.get("error") or (message if message in {
            "access_denied", "authorization_pending", "expired_token", "slow_down"} else None)
        raise TwitchOAuthError(code, message)


def twitch_validate_token(token):
    return twitch_oauth_request("https://id.twitch.tv/oauth2/validate", access_token=token)


def twitch_refresh_token(auth):
    result = twitch_oauth_request("https://id.twitch.tv/oauth2/token", {
        "client_id": TWITCH_CLIENT_ID,
        "grant_type": "refresh_token",
        "refresh_token": auth["refresh_token"],
    })
    return {**auth, **result}


class TwitchChatWorker:
    """Per-channel adapter; the shared IRC manager owns the actual connection."""

    def __init__(self, channel, out, manager, start_paused=True):
        self.channel = channel.lower()
        self.out = out
        self.manager = manager
        self.paused = bool(start_paused)
        self.closed = False
        self.room_id = None

    def start(self):
        self.manager.register(self)

    def set_paused(self, paused):
        paused = bool(paused)
        changed = self.paused != paused
        self.paused = paused
        if changed:
            self.out.put(("status", "Chat paused" if paused else "Chat resumed"))
            self.manager.changed()

    def close(self):
        self.closed = True
        self.manager.unregister(self)


class TwitchStreamInfoWorker(threading.Thread):
    """Refresh live Twitch viewer count, category, and stream title."""

    def __init__(self, channel_login, out, manager):
        super().__init__(daemon=True)
        self.channel_login = channel_login.lower()
        self.out = out
        self.manager = manager
        self.stop_event = threading.Event()
        self.refresh_event = threading.Event()
        self.broadcaster_id = None

    def refresh(self):
        with self.manager.lock:
            access_token = self.manager.auth.get("access_token")
        if not access_token:
            return
        def get_helix(endpoint, params):
            query = urllib.parse.urlencode(params)
            request = urllib.request.Request(
                "https://api.twitch.tv/helix/" + endpoint + "?" + query,
                headers={"Accept": "application/json",
                         "Client-Id": TWITCH_CLIENT_ID,
                         "Authorization": f"Bearer {access_token}",
                         "User-Agent": "YT-Chatterino/1.0"})
            with urllib.request.urlopen(request, timeout=12) as response:
                return json.loads(response.read(1024 * 1024 + 1).decode("utf-8"))

        data = get_helix("streams", {"user_login": self.channel_login})
        streams = data.get("data", []) if isinstance(data, dict) else []
        if streams:
            stream = streams[0]
            self.broadcaster_id = stream.get("user_id") or self.broadcaster_id
            info = {"live": True,
                    "channel": stream.get("user_name") or self.channel_login,
                    "viewers": stream.get("viewer_count"),
                    "category": stream.get("game_name") or "No category",
                    "title": stream.get("title") or "Untitled stream"}
        else:
            # Twitch retains a channel's most recent title/category while offline.
            channel = {}
            if not self.broadcaster_id:
                users = get_helix("users", {"login": self.channel_login}).get("data", [])
                channel = users[0] if users else {}
                self.broadcaster_id = channel.get("id")
            broadcaster_id = self.broadcaster_id
            channel_info = {}
            if broadcaster_id:
                channels = get_helix("channels", {"broadcaster_id": broadcaster_id}).get("data", [])
                channel_info = channels[0] if channels else {}
            info = {"live": False,
                    "channel": channel.get("display_name") or self.channel_login,
                    "viewers": 0,
                    "category": channel_info.get("game_name") or "Offline",
                    "title": channel_info.get("title") or "Not live"}
        info["broadcaster_id"] = self.broadcaster_id
        self.out.put(("header", info))

    def run(self):
        while not self.stop_event.is_set():
            try:
                self.refresh()
            except Exception:
                # Metadata is supplementary; keep chat working if Helix is unavailable.
                pass
            self.refresh_event.wait(VIEWER_REFRESH_SECONDS)
            self.refresh_event.clear()


class TwitchIRCManager(threading.Thread):
    """One TLS IRC session shared by all active Twitch channel tabs."""

    SERVER = "irc.chat.twitch.tv"
    PORT = 6697

    def __init__(self, auth, on_auth_updated=None):
        super().__init__(daemon=True)
        self.auth = dict(auth)
        self.on_auth_updated = on_auth_updated
        self.workers = {}
        self.lock = threading.RLock()
        self.wake = threading.Event()
        self.stop_event = threading.Event()
        self.force_disconnect = False
        self.auth_failed = False

    def register(self, worker):
        with self.lock:
            self.workers[worker.channel] = worker
        self.wake.set()

    def unregister(self, worker):
        with self.lock:
            if self.workers.get(worker.channel) is worker:
                self.workers.pop(worker.channel, None)
        self.wake.set()

    def changed(self):
        self.wake.set()

    def set_auth(self, auth):
        with self.lock:
            self.auth = dict(auth)
            self.auth_failed = False
            self.force_disconnect = True
        self.wake.set()

    def reconnect(self):
        with self.lock:
            self.auth_failed = False
            self.force_disconnect = True
        self.wake.set()

    def shutdown(self):
        self.stop_event.set()
        self.wake.set()

    def _active_workers(self):
        with self.lock:
            return {name: worker for name, worker in self.workers.items()
                    if not worker.closed and not worker.paused}

    @staticmethod
    def _status(workers, text):
        for worker in workers.values():
            worker.out.put(("status", text))

    @staticmethod
    def _decode_tags(raw):
        tags = {}
        for part in raw.split(";"):
            if "=" not in part:
                continue
            key, value = part.split("=", 1)
            value = (value.replace(r"\s", " ").replace(r"\:", ";")
                     .replace(r"\r", "\r").replace(r"\n", "\n").replace(r"\\", "\\"))
            tags[key] = value
        return tags

    @classmethod
    def parse_message(cls, line):
        return parse_privmsg(line, cls._decode_tags, parse_twitch_badges,
                             parse_twitch_emotes, parse_twitch_gifs)

    def _handle_line(self, line, sock, joined, registered):
        if line.startswith("PING "):
            sock.sendall(("PONG " + line[5:] + "\r\n").encode("utf-8"))
            return registered
        rest = line
        tags = {}
        if rest.startswith("@"):
            tag_text, _, rest = rest.partition(" ")
            tags = self._decode_tags(tag_text[1:])
        prefix = ""
        if rest.startswith(":"):
            prefix, _, rest = rest[1:].partition(" ")
        command, _, params = rest.partition(" ")
        if command == "001":
            registered = True
        elif command == "NOTICE":
            message = params.split(" :", 1)[-1].casefold()
            if "authentication failed" in message or "improperly formatted auth" in message:
                with self.lock:
                    self.auth_failed = True
                self._status(self._active_workers(), "Twitch sign-in failed. Use Twitch account… in settings.")
        elif command == "PRIVMSG":
            target, separator, body = params.partition(" :")
            channel = target.lstrip("#").lower()
            if separator and channel:
                worker = self._active_workers().get(channel)
                parsed = self.parse_message(line)
                if worker and parsed:
                    message = parsed[1]
                    if message.get("room_id") and worker.room_id != message["room_id"]:
                        worker.room_id = message["room_id"]
                        worker.out.put(("room_id", worker.room_id))
                    worker.out.put(("msg", message))
        elif command in ("CLEARMSG", "CLEARCHAT"):
            channel = params.split(" ", 1)[0].lstrip("#").lower()
            worker = self._active_workers().get(channel)
            if worker:
                value = ({"message_id": tags.get("target-msg-id")} if command == "CLEARMSG"
                         else {"user_id": tags.get("target-user-id"), "all": not tags.get("target-user-id")})
                worker.out.put(("moderation", value))
        elif command == "ROOMSTATE":
            channel = params.split(" ", 1)[0].lstrip("#").lower()
            worker = self._active_workers().get(channel)
            if worker and tags.get("room-id") and worker.room_id != tags["room-id"]:
                worker.room_id = tags["room-id"]
                worker.out.put(("room_id", tags["room-id"]))
        elif command == "RECONNECT":
            raise OSError("Twitch requested a reconnect")
        return registered

    def run(self):
        sock = None
        recv_buffer = b""
        joined = set()
        registered = False
        retry_at = 0.0
        retry_delay = 3
        refresh_retry_at = 0.0

        def close_socket():
            nonlocal sock, recv_buffer, joined, registered
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass
            sock = None
            recv_buffer = b""
            joined = set()
            registered = False

        while not self.stop_event.is_set():
            workers = self._active_workers()
            with self.lock:
                force = self.force_disconnect
                self.force_disconnect = False
                auth_failed = self.auth_failed
                auth = dict(self.auth)
            if force:
                close_socket()
                retry_at = 0
            if not workers:
                close_socket()
                self.wake.wait(0.5)
                self.wake.clear()
                continue
            if auth_failed:
                close_socket()
                self.wake.wait(0.5)
                self.wake.clear()
                continue
            expires_at = auth.get("expires_at")
            if expires_at and time.time() >= float(expires_at) - 180:
                if time.monotonic() < refresh_retry_at:
                    self.wake.wait(0.5)
                    self.wake.clear()
                    continue
                try:
                    refreshed = twitch_refresh_token(auth)
                    details = twitch_validate_token(refreshed["access_token"])
                    if (details.get("client_id") != TWITCH_CLIENT_ID
                            or TWITCH_SCOPES not in details.get("scopes", [])
                            or not details.get("login")):
                        raise TwitchOAuthError("invalid_scope", "Twitch chat access could not be refreshed.")
                    refreshed.update({"login": details["login"], "client_id": TWITCH_CLIENT_ID,
                                      "user_id": details.get("user_id"), "scope": details.get("scopes", []),
                                      "expires_in": details.get("expires_in", 14400),
                                      "expires_at": time.time() + int(details.get("expires_in", 14400))})
                    with self.lock:
                        self.auth = dict(refreshed)
                    if self.on_auth_updated:
                        self.on_auth_updated(dict(refreshed))
                    auth = refreshed
                    close_socket()
                    retry_at = 0
                    self._status(workers, "Twitch sign-in refreshed")
                    continue
                except TwitchOAuthError:
                    with self.lock:
                        self.auth_failed = True
                    close_socket()
                    self._status(workers, "Twitch sign-in expired. Use Twitch account… in settings.")
                    continue
                except Exception:
                    close_socket()
                    refresh_retry_at = time.monotonic() + 30
                    self._status(workers, "Twitch sign-in refresh is temporarily unavailable. Retrying…")
                    continue
            if sock is None:
                if time.monotonic() < retry_at:
                    self.wake.wait(min(0.5, retry_at - time.monotonic()))
                    self.wake.clear()
                    continue
                try:
                    self._status(workers, "Connecting to Twitch…")
                    raw_socket = socket.create_connection((self.SERVER, self.PORT), timeout=10)
                    sock = ssl.create_default_context().wrap_socket(raw_socket, server_hostname=self.SERVER)
                    sock.settimeout(0.4)
                    sock.sendall(f"PASS oauth:{auth['access_token']}\r\n".encode("utf-8"))
                    sock.sendall(f"NICK {auth['login'].lower()}\r\n".encode("utf-8"))
                    sock.sendall(b"CAP REQ :twitch.tv/tags twitch.tv/commands\r\n")
                    retry_delay = 3
                    retry_at = 0
                except Exception as exc:
                    close_socket()
                    self._status(workers, f"Twitch connection error: {str(exc)[:100]} Retrying…")
                    retry_at = time.monotonic() + retry_delay
                    retry_delay = min(60, retry_delay * 2)
                    continue

            try:
                workers = self._active_workers()
                desired = set(workers)
                if registered:
                    for channel in joined - desired:
                        sock.sendall(f"PART #{channel}\r\n".encode("utf-8"))
                        joined.discard(channel)
                    for channel in desired - joined:
                        sock.sendall(f"JOIN #{channel}\r\n".encode("utf-8"))
                        joined.add(channel)
                data = sock.recv(8192)
                if data:
                    recv_buffer += data
                    while b"\r\n" in recv_buffer:
                        raw_line, recv_buffer = recv_buffer.split(b"\r\n", 1)
                        line = raw_line.decode("utf-8", errors="replace")
                        was_registered = registered
                        registered = self._handle_line(line, sock, joined, registered)
                        if registered and not was_registered:
                            self._status(self._active_workers(), "Connected to Twitch")
                else:
                    raise OSError("Twitch closed the connection")
            except socket.timeout:
                pass
            except Exception as exc:
                close_socket()
                self._status(self._active_workers(), f"Twitch disconnected: {str(exc)[:90]} Retrying…")
                retry_at = time.monotonic() + retry_delay
                retry_delay = min(60, retry_delay * 2)
        close_socket()


def _pacific_date():
    return datetime.now(_PT).strftime("%Y-%m-%d")


class QuotaTracker:
    """Local ESTIMATE of API units used today (Google resets at midnight Pacific)."""

    def __init__(self):
        self.lock = threading.Lock()
        self.date, self.used, self._unsaved = _pacific_date(), 0, 0
        try:
            with open(QUOTA_PATH, "r", encoding="utf-8") as f:
                d = json.load(f)
            if d.get("date") == self.date:
                self.used = max(0, int(d.get("used", 0)))
        except Exception:
            pass

    def _roll(self):
        today = _pacific_date()
        if today != self.date:
            self.date, self.used = today, 0

    def _save(self):
        self._unsaved = 0
        try:
            with open(QUOTA_PATH + ".tmp", "w", encoding="utf-8") as f:
                json.dump({"date": self.date, "used": self.used}, f)
            os.replace(QUOTA_PATH + ".tmp", QUOTA_PATH)
        except Exception:
            pass

    def add(self, units):
        with self.lock:
            self._roll()
            self.used += max(0, int(units))
            self._save()

    def save(self):
        with self.lock:
            self._save()

    def snapshot(self):
        with self.lock:
            self._roll()
            return self.used


QUOTA = QuotaTracker()


def parse_item(item):
    """Turn a YouTube chat item into a simple dict, or None to skip it."""
    sn, au = item["snippet"], item["authorDetails"]
    kind = sn["type"]
    badges = []
    if au.get("isChatOwner"):
        badges.append("[HOST]")
    if au.get("isChatModerator"):
        badges.append("[MOD]")
    if au.get("isChatSponsor"):
        badges.append("[MEMBER]")
    name = au["displayName"]
    m = {"id": item.get("id"), "time": clock(), "name": name,
         "user_id": au.get("channelId") or sn.get("authorChannelId"),
         "badges": badges, "special": None}
    try:
        m["sent_at"] = datetime.fromisoformat(sn.get("publishedAt", "").replace("Z", "+00:00")).timestamp()
    except ValueError:
        pass
    if kind == "textMessageEvent":
        m["text"] = sn.get("displayMessage", "")
    elif kind == "superChatEvent":
        d = sn.get("superChatDetails", {})
        m["text"] = f"{d.get('amountDisplayString', '')} {d.get('userComment', '')}".strip()
        m["special"] = "super"
    elif kind == "superStickerEvent":
        d = sn.get("superStickerDetails", {})
        m["text"] = f"sent a sticker ({d.get('amountDisplayString', '')})"
        m["special"] = "super"
    elif kind in ("newSponsorEvent", "memberMilestoneChatEvent"):
        m["text"] = "became / renewed as a member"
        m["special"] = "member"
    elif kind in ("messageDeletedEvent", "userBannedEvent"):
        return {"_moderation": {"message_id": sn.get("messageDeletedDetails", {}).get("deletedMessageId"),
                                "user_id": sn.get("userBannedDetails", {}).get("bannedUserDetails", {}).get("channelId")}}
    else:
        return None
    return m


def parse_twitch_badges(raw):
    """Parse IRC badge set/version pairs for lookup in Twitch's badge catalog."""
    found = []
    for entry in (raw or "").split(","):
        set_id, separator, version = entry.rpartition("/")
        if separator and set_id and version:
            found.append({"set_id": set_id, "version": version})
    return found


def parse_twitch_emotes(raw, text):
    """Convert Twitch's emotes tag ranges to Python string offsets."""
    found = []
    for entry in (raw or "").split("/"):
        emote_id, separator, ranges = entry.partition(":")
        if not separator or not re.fullmatch(r"[A-Za-z0-9_-]{1,100}", emote_id):
            continue
        urls = (f"https://static-cdn.jtvnw.net/emoticons/v2/{emote_id}/animated/dark/2.0",
                f"https://static-cdn.jtvnw.net/emoticons/v2/{emote_id}/animated/dark/1.0",
                f"https://static-cdn.jtvnw.net/emoticons/v2/{emote_id}/default/dark/2.0",
                f"https://static-cdn.jtvnw.net/emoticons/v2/{emote_id}/default/dark/1.0",
                f"https://static-cdn.jtvnw.net/emoticons/v2/{emote_id}/static/dark/2.0",
                f"https://static-cdn.jtvnw.net/emoticons/v2/{emote_id}/static/dark/1.0")
        for item in ranges.split(","):
            start, dash, end = item.partition("-")
            if not dash:
                continue
            try:
                start, end = int(start), int(end) + 1
            except ValueError:
                continue
            if 0 <= start < end <= len(text):
                found.append({"start": start, "end": end,
                              "urls": urls, "source": "twitch"})
    return sorted(found, key=lambda item: item["start"])


def _image_urls(base, preferred="png", animated=False):
    """Return large, Tk-friendly image variants, preferring GIF for animations."""
    formats = [preferred.lower()]
    if animated:
        formats = ["gif"] + [fmt for fmt in formats if fmt != "gif"]
    for fmt in ("png", "gif", "webp"):
        if fmt not in formats:
            formats.append(fmt)
    return (tuple(f"{base}/2x.{fmt}" for fmt in formats)
            + tuple(f"{base}/1x.{fmt}" for fmt in formats)
            + (f"{base}/2x", f"{base}/1x"))


def _add_provider_emotes(target, records, source, scope="channel"):
    for record in records or []:
        if not isinstance(record, dict):
            continue
        data = record.get("data") if isinstance(record.get("data"), dict) else record
        code = record.get("code") or record.get("name") or data.get("code") or data.get("name")
        emote_id = data.get("id") or record.get("id")
        if not code or not emote_id:
            continue
        if source == "bttv":
            fmt = str(data.get("imageType") or "png").lower()
            base = f"https://cdn.betterttv.net/emote/{emote_id}"
            urls = _image_urls(base, fmt, bool(data.get("animated")))
        else:
            host = data.get("host") or {}
            host_url = host.get("url", "")
            if host_url.startswith("//"):
                host_url = "https:" + host_url
            if not host_url.startswith("https://"):
                continue
            files = host.get("files") or []
            file_names = {f.get("name") for f in files if isinstance(f, dict)}
            urls = []
            # Tk can read PNG and GIF directly. 7TV also serves those formats
            # even when its metadata advertises only WebP/AVIF variants.
            animated = bool(data.get("animated")) or any(
                isinstance(f, dict) and str(f.get("name", "")).endswith(".gif")
                for f in files)
            formats = ("gif", "png", "webp", "avif") if animated else ("png", "gif", "webp", "avif")
            for size in ("2x", "1x"):
                for fmt in formats:
                    filename = f"{size}.{fmt}"
                    if filename in file_names or fmt in ("png", "gif"):
                        urls.append(host_url.rstrip("/") + "/" + filename)
            for file in files:
                if isinstance(file, dict) and file.get("name"):
                    urls.append(host_url.rstrip("/") + "/" + file["name"])
            urls = tuple(dict.fromkeys(urls))
        if urls:
            target[str(code)] = {"urls": urls, "source": source, "scope": scope, "id": str(emote_id)}


class EmoteCatalog:
    """Fetch public BTTV/7TV emote sets; failures never interrupt chat."""

    def __init__(self):
        self.lock = threading.RLock()
        self.cache = {}
        self.inflight = {}

    @staticmethod
    def _fetch_json(url):
        request = urllib.request.Request(url, headers={
            "Accept": "application/json", "User-Agent": "YT-Chatterino/1.0"})
        with urllib.request.urlopen(request, timeout=12) as response:
            return json.loads(response.read(8 * 1024 * 1024 + 1).decode("utf-8"))

    def _cached_json(self, key, url):
        with self.lock:
            cached = self.cache.get(key)
            if cached and time.monotonic() - cached[0] < (EMOTE_CACHE_SECONDS if cached[1] else 60):
                return cached[1]
            waiting = self.inflight.get(key)
            owner = waiting is None
            if owner:
                waiting = self.inflight[key] = threading.Event()
        if not owner:
            waiting.wait(14)
            with self.lock:
                return self.cache.get(key, (0, None))[1]
        value = None
        try:
            value = self._fetch_json(url)
        except Exception:
            pass
        finally:
            with self.lock:
                self.cache[key] = (time.monotonic(), value)
                self.inflight.pop(key, None)
                waiting.set()
        return value

    @staticmethod
    def _seven_tv_set(data):
        if isinstance(data, dict):
            value = data.get("emote_set") or data.get("emoteSet") or data
            if isinstance(value, dict):
                return value.get("emotes", [])
        return []

    def load(self, platform, channel_id=None):
        platform = "youtube" if platform == "youtube" else "twitch"
        merged = {}
        bttv_global = self._cached_json(
            "bttv-global", "https://api.betterttv.net/3/cached/emotes/global")
        _add_provider_emotes(merged, bttv_global, "bttv", "global")
        seven_global = self._cached_json(
            "7tv-global", "https://7tv.io/v3/emote-sets/global")
        _add_provider_emotes(merged, self._seven_tv_set(seven_global), "7tv", "global")
        if channel_id:
            bttv_channel = self._cached_json(
                f"bttv:{platform}:{channel_id}",
                f"https://api.betterttv.net/3/cached/users/{platform}/{urllib.parse.quote(str(channel_id), safe='')}")
            if isinstance(bttv_channel, dict):
                _add_provider_emotes(merged, bttv_channel.get("channelEmotes"), "bttv")
                _add_provider_emotes(merged, bttv_channel.get("sharedEmotes"), "bttv")
            seven_channel = self._cached_json(
                f"7tv:{platform}:{channel_id}",
                f"https://7tv.io/v3/users/{platform}/{urllib.parse.quote(str(channel_id), safe='')}")
            _add_provider_emotes(merged, self._seven_tv_set(seven_channel), "7tv")
        return merged


class EmoteImageLoader:
    """Download only emotes actually used, with a small process-wide LRU."""

    PNG = b"\x89PNG\r\n\x1a\n"
    GIF = (b"GIF87a", b"GIF89a")

    def __init__(self):
        self.lock = threading.Lock()
        self.cache = OrderedDict()
        self.cache_bytes = 0
        self.pending = {}
        self.failures = {}

    @classmethod
    def _supported_image(cls, data):
        if data.startswith(cls.PNG) and len(data) >= 24:
            width, height = struct.unpack(">II", data[16:24])
        elif data.startswith(cls.GIF) and len(data) >= 10:
            width, height = struct.unpack("<HH", data[6:10])
        else:
            return False
        return 0 < width <= 1024 and 0 < height <= 1024

    def request(self, urls, callback):
        key = tuple(urls)
        if not key:
            callback(None)
            return
        with self.lock:
            cached = self.cache.get(key)
            if cached is not None:
                self.cache.move_to_end(key)
                callback(cached[0])
                return
            if self.failures.get(key, 0) > time.monotonic():
                callback(None)
                return
            if key in self.pending:
                self.pending[key].append(callback)
                return
            self.pending[key] = [callback]

        def download():
            payload = None
            cost = 0
            for url in key:
                try:
                    request = urllib.request.Request(url, headers={
                        "User-Agent": "YT-Chatterino/1.0", "Accept": "image/png,image/gif,*/*"})
                    with urllib.request.urlopen(request, timeout=8) as response:
                        raw = response.read(8 * 1024 * 1024 + 1)
                    if len(raw) > 8 * 1024 * 1024 or not self._supported_image(raw):
                        continue
                    frames, delays = [], []
                    if raw.startswith(self.GIF):
                        decoded = _gif_composited_png_frames(raw, with_timing=True,
                            output_limit=180 if any("giphy.com/" in url for url in key) else None)
                        if decoded:
                            frames, delays = decoded
                    payload = {"raw": raw, "frames": frames, "delays": delays}
                    cost = len(raw) + sum(len(frame) for frame in frames)
                    break
                except Exception:
                    continue
            with self.lock:
                callbacks = self.pending.pop(key, [])
                if payload is not None and cost <= EMOTE_IMAGE_CACHE_BYTES:
                    self.cache[key] = (payload, cost)
                    self.cache_bytes += cost
                    while self.cache_bytes > EMOTE_IMAGE_CACHE_BYTES and self.cache:
                        _, discarded = self.cache.popitem(last=False)
                        self.cache_bytes -= discarded[1]
                elif payload is None:
                    self.failures[key] = time.monotonic() + 90
                    if len(self.failures) > 1000:
                        now = time.monotonic()
                        self.failures = {item: expiry for item, expiry in self.failures.items() if expiry > now}
            for done in callbacks:
                try:
                    done(payload)
                except Exception:
                    pass
        try:
            IMAGE_POOL.submit(download)
        except RuntimeError:
            with self.lock:
                callbacks = self.pending.pop(key, [])
            for done in callbacks:
                done(None)


EMOTE_CATALOG = EmoteCatalog()
EMOTE_IMAGES = EmoteImageLoader()


class TwitchBadgeCatalog:
    """Cache Twitch global and broadcaster badge image URLs."""

    def __init__(self):
        self.lock = threading.RLock()
        self.cache = {}

    @staticmethod
    def _fetch(url, access_token):
        request = urllib.request.Request(url, headers={
            "Accept": "application/json",
            "Client-Id": TWITCH_CLIENT_ID,
            "Authorization": f"Bearer {access_token}",
            "User-Agent": "YT-Chatterino/1.0"})
        with urllib.request.urlopen(request, timeout=12) as response:
            return json.loads(response.read(4 * 1024 * 1024 + 1).decode("utf-8"))

    def _load_set(self, key, url, access_token):
        with self.lock:
            cached = self.cache.get(key)
            if cached and time.monotonic() - cached[0] < EMOTE_CACHE_SECONDS:
                return cached[1]
        try:
            data = self._fetch(url, access_token)
            records = data.get("data", []) if isinstance(data, dict) else []
        except Exception:
            records = []
        with self.lock:
            self.cache[key] = (time.monotonic(), records)
        return records

    @staticmethod
    def _index(records):
        result = {}
        for badge_set in records or []:
            if not isinstance(badge_set, dict):
                continue
            set_id = str(badge_set.get("set_id", ""))
            for version in badge_set.get("versions", []):
                if not isinstance(version, dict):
                    continue
                version_id = str(version.get("id", ""))
                urls = tuple(url for url in (
                    version.get("image_url_2x"), version.get("image_url_1x"))
                    if isinstance(url, str) and url.startswith("https://"))
                if set_id and version_id and urls:
                    result[(set_id, version_id)] = {
                        "urls": urls,
                        "title": version.get("title") or set_id}
        return result

    def load(self, broadcaster_id, access_token):
        global_records = self._load_set(
            "twitch-badges-global",
            "https://api.twitch.tv/helix/chat/badges/global", access_token)
        channel_records = self._load_set(
            f"twitch-badges:{broadcaster_id}",
            "https://api.twitch.tv/helix/chat/badges?broadcaster_id="
            + urllib.parse.quote(str(broadcaster_id), safe=""), access_token)
        result = self._index(global_records)
        result.update(self._index(channel_records))
        return result


TWITCH_BADGES = TwitchBadgeCatalog()


# ----------------------------------------------------------------- worker
class YouTubeWorkerControl(threading.Thread):
    """Pause, cancellation and request timing shared by YouTube readers."""

    def __init__(self, video_id, out, poll_interval, start_paused=False):
        super().__init__(daemon=True)
        self.video_id, self.out = video_id, out
        self.stop = threading.Event()
        self.control = threading.Event()
        self.control_lock = threading.Lock()
        self.paused = bool(start_paused)
        self.poll_interval = max(1.0, float(poll_interval))
        self.recommended_interval = 5.0
        self.last_poll = 0.0

    def set_poll_interval(self, seconds):
        with self.control_lock:
            self.poll_interval = max(1.0, float(seconds))
        self.control.set()

    def set_paused(self, paused):
        paused = bool(paused)
        with self.control_lock:
            changed = paused != self.paused
            self.paused = paused
        if changed:
            self.control.set()

    def settings(self):
        with self.control_lock:
            return self.paused, self.poll_interval

    def set_recommended_interval(self, seconds):
        with self.control_lock:
            self.recommended_interval = max(0.0, float(seconds))

    def wait_for_request(self, recommended_seconds=0):
        """Preserve the request deadline across pause, focus and interval changes."""
        paused_reported = False
        base = time.monotonic()
        while not self.stop.is_set():
            paused, interval = self.settings()
            if paused:
                if not paused_reported:
                    self.out.put(("status", "Chat paused"))
                    paused_reported = True
                self.control.wait(1)
                self.control.clear()
                continue
            if paused_reported:
                self.out.put(("status", "Chat resumed"))
                paused_reported = False
            deadline = max(base + max(0.0, float(recommended_seconds)), self.last_poll + interval)
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return True
            self.control.wait(remaining)
            self.control.clear()
        return False

class WebChatWorker(YouTubeWorkerControl):
    mode = "web"

    def __init__(self, video_id, out, poll_interval=WEB_POLL_SECONDS, start_paused=False):
        super().__init__(video_id, out, max(WEB_POLL_SECONDS, poll_interval), start_paused)

    def set_poll_interval(self, seconds):
        super().set_poll_interval(max(WEB_POLL_SECONDS, seconds))

    def estimated_units_per_hour(self):
        return 0

    def run(self):
        client, wait, last_info = None, 0, 0
        retry_delay = 5
        while self.wait_for_request(wait):
            try:
                if client is None:
                    client = YouTubeWebClient()
                    info = client.connect(self.video_id)
                    if self.stop.is_set():
                        return
                    self.out.put(("header", info))
                    self.out.put(("status", "Connected to YouTube · no API key · 0 reading units"))
                    last_info = time.monotonic()
                self.last_poll = time.monotonic()
                events, wait = client.poll()
                if self.stop.is_set():
                    return
                self.set_recommended_interval(wait)
                emotes = {}
                for kind, value in events:
                    self.out.put((kind, value))
                    if kind == "msg":
                        for item in value.get("native_emotes", []):
                            if item.get("code"):
                                emotes[item["code"]] = dict(item, scope="channel")
                if emotes:
                    self.out.put(("youtube_emotes", emotes))
                retry_delay = 5
                if time.monotonic() - last_info >= VIEWER_REFRESH_SECONDS:
                    info, _, _ = client.watch_info(self.video_id)
                    if self.stop.is_set():
                        return
                    self.out.put(("header", info))
                    last_info = time.monotonic()
                    if not info["live"]:
                        self.out.put(("status", "The live chat has ended."))
                        return
            except Exception as error:
                if self.stop.is_set():
                    return
                if client is not None and getattr(client, "info", None):
                    self.out.put(("header", client.info))
                if isinstance(error, YouTubeReadError):
                    if error.terminal:
                        self.out.put(("status", str(error)))
                        return
                    wait = max(retry_delay, error.retry)
                    reason = str(error)
                else:
                    wait = retry_delay
                    reason = "YouTube reader could not load chat."
                self.out.put(("status", f"{reason} Retrying in {wait:g}s."))
                client = None
                retry_delay = min(120, retry_delay * 2)


# -------------------------------------------------------------------- tab
class PhotoBank:
    """Share decoded Tk emotes and run one clock for visible animations."""

    LIMIT = 48 * 1024 * 1024

    def __init__(self, app):
        self.app = app
        self.entries = OrderedDict()
        self.bytes = 0
        self.timer = None
        self.closed = False

    def emote(self, urls, payload, view):
        is_gif = any("giphy.com/" in url for url in urls)
        resize = view._resize_gif_photo if is_gif else view._resize_emote_photo
        key = (urls, is_gif, view.appearance.get("emote_size", 36), getattr(view, "image_profile", "chat"))
        entry = self.entries.get(key)
        if entry is None:
            raw = payload.get("raw") if isinstance(payload, dict) else payload
            pngs = payload.get("frames", ()) if isinstance(payload, dict) else ()
            delays = payload.get("delays", ()) if isinstance(payload, dict) else ()
            frames = []
            for png in pngs:
                image = tk.PhotoImage(master=self.app.root, data=base64.b64encode(png))
                frames.append(resize(image))
            if frames:
                photo = frames[0].copy()
            else:
                photo = resize(tk.PhotoImage(
                    master=self.app.root, data=base64.b64encode(raw)))
            entry = {"photo": photo, "frames": frames, "delays": delays,
                     "index": 0, "next": time.monotonic(), "users": weakref.WeakSet(),
                     "urls": urls, "bytes": sum(image.width() * image.height() * 4 for image in frames)
                     + photo.width() * photo.height() * 4}
            self.entries[key] = entry
            self.bytes += entry["bytes"]
        self.entries.move_to_end(key)
        entry["users"].add(view)
        self._trim()
        self.wake()
        return entry["photo"]

    def _trim(self):
        for key, entry in list(self.entries.items()):
            if self.bytes <= self.LIMIT:
                break
            if any(view.active and entry["urls"] in view.used_assets for view in entry["users"]):
                continue
            self.bytes -= entry["bytes"]
            self.entries.pop(key, None)
        # A particularly busy visible chat can exceed the budget. Keep its
        # current emote image and release the least recent animation frames.
        for entry in self.entries.values():
            if self.bytes <= self.LIMIT:
                break
            if entry["frames"]:
                released = sum(image.width() * image.height() * 4 for image in entry["frames"])
                entry["frames"] = []
                entry["bytes"] -= released
                self.bytes -= released

    def wake(self):
        if not self.closed and self.timer is None:
            self.timer = self.app.root.after(20, self.tick)

    def tick(self):
        self.timer = None
        now = time.monotonic()
        playing = False
        for entry in tuple(self.entries.values()):
            frames = entry["frames"]
            if len(frames) < 2 or not any(view.active and not getattr(view, "resizing", False) and entry["urls"] in view.used_assets
                                           for view in entry["users"]):
                continue
            playing = True
            if now < entry["next"]:
                continue
            entry["index"] = (entry["index"] + 1) % len(frames)
            delays = entry["delays"]
            delay = delays[entry["index"]] if len(delays) == len(frames) else 90
            entry["next"] = now + max(20, delay) / 1000
            try:
                entry["photo"].tk.call(str(entry["photo"]), "copy",
                                        str(frames[entry["index"]]), "-compositingrule", "set")
            except tk.TclError:
                continue
        if playing:
            self.wake()

    def close(self):
        self.closed = True
        if self.timer is not None:
            self.app.root.after_cancel(self.timer)
            self.timer = None
        self.entries.clear()


class EventQueue(queue.Queue):
    """Keep network readers responsive even if a chat becomes very busy."""

    def __init__(self):
        super().__init__(maxsize=2400)

    def put(self, item, block=True, timeout=None):
        try:
            super().put(item, block=False)
        except queue.Full:
            try:
                super().get_nowait()
            except queue.Empty:
                pass
            try:
                super().put(item, block=False)
            except queue.Full:
                pass


@lru_cache(maxsize=16)
def compile_emote_pattern(codes):
    ordered = sorted(codes, key=len, reverse=True)
    return re.compile(r"(?<![A-Za-z0-9_])(?:" + "|".join(map(re.escape, ordered)) + r")(?![A-Za-z0-9_])") if ordered else None


class ChatSource:
    """One channel connection, shared by every pane that displays it."""

    def __init__(self, app, spec):
        self.app = app
        self.platform = spec["platform"]
        self.video_id = spec["id"]
        self.channel_login = self.video_id if self.platform == "twitch" else None
        self.key = ("twitch:" + self.video_id) if self.platform == "twitch" else self.video_id
        self.q = EventQueue()
        self.views = set()
        self.worker = None
        self.info_worker = None
        self.header_info = {}
        self.history = deque(maxlen=message_limit(app.cfg))
        self.history_live = deque(maxlen=message_limit(app.cfg))
        self.history_loading = False
        self.history_requested = False
        self.seen = OrderedDict()
        self.last_status = None
        self.status_times = OrderedDict()
        self.manual_paused = bool(spec.get("paused", False)) if self.platform == "youtube" else False
        self.emote_map = {}
        self.youtube_emotes = {}
        self.emote_pattern = None
        self.emote_channel_id = None
        self.emote_requests = set()
        self.emote_generation = 0
        self.emote_version = 0
        self.badge_catalog = {}
        self.badge_requests = set()
        self.closed = False

    @property
    def name(self):
        return (self.header_info.get("channel") or self.channel_login
                or self.header_info.get("title") or self.video_id)

    def spec(self):
        return {"platform": self.platform, "id": self.video_id,
                "paused": self.manual_paused}

    def ensure_worker(self):
        if self.closed or self.worker is not None:
            return
        if self.platform == "twitch":
            manager = self.app.twitch_manager
            if manager is None:
                self.publish("status", "Sign in to Twitch from settings to connect this chat.")
                return
            self.worker = TwitchChatWorker(self.channel_login, self.q, manager, start_paused=False)
            self.info_worker = TwitchStreamInfoWorker(self.channel_login, self.q, manager)
            self.info_worker.start()
        else:
            saved = self.app.cfg.get("chat_settings", {}).get(self.key, {})
            try:
                interval = max(WEB_POLL_SECONDS, float(saved.get("web_poll_interval_seconds", WEB_POLL_SECONDS)))
            except (TypeError, ValueError):
                interval = WEB_POLL_SECONDS
            self.worker = WebChatWorker(self.video_id, self.q, interval, start_paused=self.manual_paused)
        if self.platform == "twitch":
            self.app.features.chat_tools.source_history(self, TwitchIRCManager.parse_message)
        self.worker.start()
        self.request_emotes()
        self.sync_pause()

    def sync_pause(self):
        if self.worker is None:
            return
        if self.platform == "twitch":
            self.worker.set_paused(False)
        else:
            self.worker.set_paused(self.manual_paused)

    def request_emotes(self, channel_id=None, force=False):
        if channel_id:
            self.emote_channel_id = str(channel_id)
        channel_id = self.emote_channel_id
        request_key = channel_id or "global"
        if not force and request_key in self.emote_requests:
            return
        self.emote_requests.add(request_key)
        self.emote_generation += 1
        generation = self.emote_generation

        def load():
            result = EMOTE_CATALOG.load(self.platform, channel_id)
            pattern = compile_emote_pattern(tuple(sorted(code for code in result if code and len(code) <= 80 and not any(char.isspace() for char in code))))
            if not self.closed:
                self.q.put(("emote_catalog", (generation, result, pattern)))
        CATALOG_POOL.submit(load)

    def request_badges(self, broadcaster_id):
        broadcaster_id = str(broadcaster_id)
        if self.platform != "twitch" or broadcaster_id in self.badge_requests:
            return
        manager = self.app.twitch_manager
        if manager is None:
            return
        with manager.lock:
            token = manager.auth.get("access_token")
        if not token:
            return
        self.badge_requests.add(broadcaster_id)

        def load():
            catalog = TWITCH_BADGES.load(broadcaster_id, token)
            if not self.closed:
                self.q.put(("badge_catalog", catalog))
        CATALOG_POOL.submit(load)

    def publish(self, kind, value):
        if kind == "status":
            if value == self.last_status:
                return
            self.last_status = value
        if kind == "msg":
            if self.history_loading and not value.get("_history"):
                self.history_live.append(value)
                return
            message_id = value.get("id")
            if message_id:
                if message_id in self.seen:
                    return
                self.seen[message_id] = None
                if len(self.seen) > self.history.maxlen * 2:
                    self.seen.popitem(last=False)
            if self.platform == "youtube" and value.get("_youtube_web"):
                # The website and Data API can use different message IDs.
                # Pair each API acknowledgement with one website echo in order.
                for _, old_kind, old, _ in list(self.history)[-128:]:
                    if (old_kind == "msg" and old.get("_own") and old.get("_ack_at")
                            and not old.get("youtube_web_id")
                            and old.get("user_id") == value.get("user_id")
                            and old.get("text") == value.get("text")
                            and abs(value.get("sent_at", time.time()) - old["_ack_at"]) < 60):
                        api_id = old["id"]
                        ack_at = old["_ack_at"]
                        old.update(value, id=api_id, api_id=api_id, youtube_web_id=message_id,
                                   _own=True, _ack_at=ack_at)
                        for view in tuple(self.views):
                            if view.active:
                                view.load_recent()
                        return
        self.app.message_sequence += 1
        if kind == "status":
            self.status_times[self.app.message_sequence] = time.time()
            while len(self.status_times) > message_limit(self.app.cfg):
                self.status_times.popitem(last=False)
        if kind == "msg":
            value = dict(value, _seq=self.app.message_sequence)
            self.app.features.on_message(self, value)
            self.app.features.chat_tools.on_message(self, value)
        event = (self.app.message_sequence, kind, value, self)
        self.history.append(event)
        for view in tuple(self.views):
            if view.detached:
                continue
            if view.active:
                view.pending_events.append(event)
            elif kind == "msg" and view.accepts(value):
                workspace = view.workspace
                if not workspace.unread:
                    workspace.unread = True
                    self.app._style_tab(workspace, workspace is self.app.current_workspace)

    def handle_event(self, kind, value):
        if kind == "moderation":
            self.app.features.redact(self, value)
        elif kind in ("msg", "status"):
            self.publish(kind, value)
        elif kind == "header":
            self.header_info.update(value)
            self.app.features.source_header(self, value)
            if value.get("channel_id"):
                self.request_emotes(value["channel_id"])
            for view in tuple(self.views):
                view.refresh_header()
                if not view.detached:
                    self.app.refresh_workspace_title(view.workspace)
        elif kind == "room_id":
            self.request_emotes(value)
            self.request_badges(value)
        elif kind == "emote_catalog":
            generation, emotes, pattern = value
            if generation < self.emote_version:
                return
            self.emote_version = generation
            self.emote_map = emotes
            self.emote_pattern = pattern
        elif kind == "youtube_emotes":
            self.youtube_emotes.update(value)
            while len(self.youtube_emotes) > 1024:
                self.youtube_emotes.pop(next(iter(self.youtube_emotes)))
        elif kind == "badge_catalog":
            self.badge_catalog = value or {}
            for view in tuple(self.views):
                view.set_badge_catalog(self, self.badge_catalog)

    def reconnect(self):
        if self.closed:
            return
        if self.worker is not None and self.platform == "twitch":
            self.worker.manager.reconnect()
            if self.info_worker:
                self.info_worker.refresh_event.set()
            return
        if self.worker is not None:
            self.worker.stop.set()
            self.worker.control.set()
            self.worker = None
        self.last_status = None
        self.ensure_worker()

    def close(self):
        self.closed = True
        if self.info_worker:
            self.info_worker.stop_event.set()
            self.info_worker.refresh_event.set()
        if self.worker is not None:
            if self.platform == "twitch":
                self.worker.close()
            else:
                self.worker.stop.set()
                self.worker.control.set()


class ChatWorkspace(tk.Frame):
    """A tab containing a persistent tree of resizable chat panes."""

    def __init__(self, app, name="", workspace_id=None):
        super().__init__(app.content, bg=BG)
        self.app = app
        self.uid = workspace_id or uuid.uuid4().hex
        self.custom_name = name
        self.tree = None
        self.focused = None
        self.zoomed = None
        self.containers = []
        self.btn = {}
        self.unread = False
        self.closed = False
        self.layout_after_id = None
        self.needs_positions = False
        self.placing_positions = False
        self.bind("<Configure>", lambda event: self.schedule_positions())

    @property
    def panes(self):
        def walk(node):
            if node is None:
                return []
            if "pane" in node:
                return [node["pane"]]
            return [pane for child in node["children"] for pane in walk(child)]
        return walk(self.tree)

    @property
    def label(self):
        if self.custom_name:
            return self.custom_name
        names = []
        for pane in self.panes:
            for source in pane.sources:
                if source.name not in names:
                    names.append(source.name)
        return " + ".join(names) or "Chat"

    def new_pane(self, sources, pane_id=None):
        pane = ChatTab(self, sources, self.app.fonts, self.app.sb_style,
                       self.app.appearance, pane_id=pane_id)
        if self.tree is None:
            self.tree = {"pane": pane}
        self.focused = pane
        return pane

    def _replace_node(self, old_pane, replacement, node=None):
        node = self.tree if node is None else node
        if "pane" in node:
            return replacement if node["pane"] is old_pane else node
        node["children"] = [self._replace_node(old_pane, replacement, child)
                            for child in node["children"]]
        return node

    def capture_sizes(self, node=None):
        if self.zoomed or self.needs_positions:
            return
        node = self.tree if node is None else node
        if not node or "pane" in node:
            return
        widget = node.get("widget")
        if widget is not None and widget.winfo_exists():
            total = widget.winfo_width() if node["orient"] == "horizontal" else widget.winfo_height()
            if total > 10:
                try:
                    x, y = widget.sash_coord(0)
                    node["ratio"] = max(0.08, min(0.92, (x if node["orient"] == "horizontal" else y) / total))
                except tk.TclError:
                    pass
        for child in node["children"]:
            self.capture_sizes(child)

    def split(self, pane, sources, orient="horizontal"):
        if len(self.panes) >= 24:
            messagebox.showinfo(tr("Split chats"), tr("A tab can contain up to 24 panes."), parent=self.app.root)
            return pane
        self.capture_sizes()
        new_pane = self.new_pane(sources)
        replacement = {"orient": orient, "ratio": 0.5,
                       "children": [{"pane": pane}, {"pane": new_pane}]}
        self.tree = self._replace_node(pane, replacement)
        self.zoomed = None
        self.rebuild(capture=False)
        return new_pane

    def rebuild(self, capture=True):
        if capture:
            self.capture_sizes()
        for pane in self.panes:
            pane.pack_forget()
        for widget in reversed(self.containers):
            if widget.winfo_exists():
                widget.destroy()
        self.containers.clear()
        if self.zoomed not in self.panes:
            self.zoomed = None
        if self.zoomed:
            self.zoomed.pack(fill="both", expand=True)
        elif self.tree:
            def build(node):
                if "pane" in node:
                    return node["pane"]
                widget = tk.PanedWindow(self, orient=node["orient"], bg=EDGE,
                                        bd=0, sashwidth=5, sashpad=0, opaqueresize=False,
                                        showhandle=False, handlesize=0)
                self.containers.append(widget)
                node["widget"] = widget
                for child in node["children"]:
                    widget.add(build(child), minsize=110, stretch="always", padx=0, pady=0)
                widget.bind("<ButtonRelease-1>", lambda event: self.app.schedule_session_save(), add="+")
                return widget
            build(self.tree).pack(fill="both", expand=True)
        for pane in self.panes:
            pane.lift()
        self.needs_positions = self.zoomed is None
        self.schedule_positions()
        self.app.sync_visibility()

    def schedule_positions(self):
        if self.needs_positions and not self.closed and not self.placing_positions and self.layout_after_id is None:
            self.layout_after_id = self.after_idle(self.apply_positions)

    def apply_positions(self):
        self.layout_after_id = None
        if self.closed or self.zoomed or not self.winfo_ismapped() or self.winfo_width() < 10:
            return
        self.placing_positions = True
        try:
            self.update_idletasks()
            def apply(node):
                if "pane" in node:
                    return
                widget = node["widget"]
                length = widget.winfo_width() if node["orient"] == "horizontal" else widget.winfo_height()
                ratio = node.get("ratio", 0.5)
                widget.sash_place(0, int(length * ratio) if node["orient"] == "horizontal" else 0,
                                  int(length * ratio) if node["orient"] == "vertical" else 0)
                # Apply the parent's allocation before sizing nested splits.
                widget.update_idletasks()
                for child in node["children"]:
                    apply(child)
            if self.tree:
                apply(self.tree)
            self.needs_positions = False
        finally:
            self.placing_positions = False

    def remove_pane(self, pane):
        self.capture_sizes()
        def remove(node):
            if "pane" in node:
                return None if node["pane"] is pane else node
            children = [child for child in (remove(child) for child in node["children"]) if child]
            if len(children) == 1:
                return children[0]
            node["children"] = children
            return node if children else None
        self.tree = remove(self.tree)
        pane.close()
        pane.destroy()
        if self.focused is pane:
            self.focused = self.panes[0] if self.panes else None
        if self.zoomed is pane:
            self.zoomed = None
        self.rebuild(capture=False)

    def balance(self):
        def arrange(node):
            if "pane" in node:
                return 1
            sizes = [arrange(child) for child in node["children"]]
            node["ratio"] = sizes[0] / sum(sizes)
            return sum(sizes)
        if self.tree:
            arrange(self.tree)
            self.rebuild(capture=False)
            self.app.schedule_session_save()

    def serialize(self):
        self.capture_sizes()
        def encode(node):
            if "pane" in node:
                pane = node["pane"]
                return {"pane_id": pane.uid, "sources": [source.key for source in pane.sources]}
            return {"orient": node["orient"], "ratio": node.get("ratio", 0.5),
                    "children": [encode(child) for child in node["children"]]}
        return {"id": self.uid, "name": self.custom_name, "layout": encode(self.tree),
                "focused": self.focused.uid if self.focused else None}

    def close(self):
        self.closed = True
        if self.layout_after_id is not None:
            self.after_cancel(self.layout_after_id)
            self.layout_after_id = None
        for pane in self.panes:
            pane.close()



class ChatTab(tk.Frame):
    def __init__(self, parent, sources, fonts, sb_style, appearance, pane_id=None, thread_window=None):
        super().__init__(parent, bg=BG, highlightthickness=1, highlightbackground=HEADER_BG)
        self.thread_window = thread_window
        self.detached = thread_window is not None
        self.workspace = parent
        self.app = parent.app
        self.uid = pane_id or uuid.uuid4().hex
        self.sources = []
        self.platform = "mixed"
        self.video_id = "mix:" + self.uid
        self.setting_key = self.video_id
        self.channel_login = None
        self.appearance = appearance
        self.filters = {}
        self.search_query = ""
        self.search_matches = []
        self.search_index = -1
        self.search_after_id = None
        self.active = False
        self.closed = False
        self.q = EventQueue()
        self.pending_events = deque(maxlen=message_limit(self.app.cfg))
        self.last_seq = 0
        self.name_tags = set()
        self.fonts = fonts
        self.row = 0
        self.row_marks = deque()
        self.row_serial = 0
        self.last_status = None
        self.emote_photos = {}
        self.badge_photos = {}
        self.pending_badge_catalog = {}
        self.pending_badge_images = {}
        self.local_badge_photos = {}
        self.pending_emote_tags = {}
        self.image_requests = set()
        self.image_names = {}
        self.image_assets = {}
        self.photo_urls = {}
        self.used_assets = set()
        self.message_records = {}
        self.timestamp_records = {}
        self.row_color_tags = {}
        self.image_counter = 0
        self.pending_counter = 0
        self._batch = False
        self.follow_tail = True
        self._render_source = None
        self.header_info = {}
        self.resizing = False
        self.resize_after_id = None
        self.resize_restore_id = None
        self.viewport_after_id = None
        self.header_after_id = None
        self.viewport_target = None
        self.viewport_applied = None
        self._header_wrap = None
        self._jump_placed = False
        self._jump_offset = None
        self.resize_surface = None
        self.font_after_id = None
        self._reply_click = None

        self.header = tk.Frame(self, bg=HEADER_BG, pady=4, cursor="arrow")
        self.header.pack(side="top", fill="x")
        self.header_icon = tk.Canvas(self.header, width=18, height=16, bg=HEADER_BG,
                                    bd=0, highlightthickness=0)
        self.header_icon.pack(side="left", padx=(6, 5), pady=1, anchor="n")
        self.header_menu = tk.Label(self.header, text=tr("▾"), bg=HEADER_BG, fg=DIM,
                                    font=("Segoe UI", 10), padx=6, cursor="hand2")
        self.header_menu.pack(side="right", anchor="n")
        self.header_menu.bind("<Button-1>", lambda event: self.app.pane_menu(event, self))
        self.header_text = tk.Label(self.header, bg=HEADER_BG, fg=FG,
                                    font=("Segoe UI", 9, "bold"), justify="left", anchor="w")
        self.header_text.pack(side="left", fill="x", expand=True)
        self.header.bind("<Configure>", self._header_configure)
        for widget in (self.header, self.header_icon, self.header_text):
            widget.bind("<Button-1>", lambda event: self.app.focus_pane(self), add="+")
            widget.bind("<Double-Button-1>", lambda event: self.app.zoom_pane(self))
            widget.bind("<Button-3>", lambda event: self.app.pane_menu(event, self))

        self.app.features.attach_composer(self)
        self.viewport = tk.Frame(self, bg=BG, bd=0, highlightthickness=0)
        self.text = tk.Text(self.viewport, bg=BG, fg=FG, bd=0, highlightthickness=0,
                            wrap="word", state="disabled", font=fonts["normal"],
                            padx=6, pady=4, cursor="arrow", insertwidth=0,
                            spacing1=1 if appearance.get("compact_spacing") else 3,
                            spacing3=1 if appearance.get("compact_spacing") else 3,
                            selectbackground="#365f85", inactiveselectbackground="#365f85",
                            selectforeground="#ffffff", undo=False, exportselection=False)
        self.scrollbar = ttk.Scrollbar(self, orient="vertical", command=self._scrollbar_view, style=sb_style)
        self.text.configure(yscrollcommand=self._on_scroll)
        self.scrollbar.pack(side="right", fill="y")
        self.viewport.pack(side="left", fill="both", expand=True)
        self.text.place(x=0, y=0, width=1, height=1)
        self.viewport.bind("<Configure>", self._viewport_configure)
        self.jump_button = tk.Label(self, text=tr("↓ Latest"), bg="#252525", fg=FG,
                                    font=("Segoe UI", 9), padx=12, pady=4, cursor="hand2")
        self.jump_button.bind("<Button-1>", lambda event: self.jump_latest())
        self.text.tag_configure("alt", background=ALT)
        self.text.tag_configure("time", foreground=DIM,
                                elide=not appearance.get("show_timestamps", True))
        self.text.tag_configure("sys", foreground="#a1a1a8")
        self.text.tag_configure("sys_err", foreground="#ff6b6b")
        self.text.tag_configure("badge", foreground="#bf94ff", font=fonts["bold"])
        self.text.tag_configure("name", font=fonts["username"])
        self.text.tag_configure("source_twitch", foreground="#b99aff")
        self.text.tag_configure("source_youtube", foreground="#ff7777")
        self.text.tag_configure("super", background="#4a3b00", foreground="#ffd84d")
        self.text.tag_configure("member", foreground="#6dd66d")
        self.text.tag_configure("emote", foreground="#ffb347", font=fonts["emoji"])
        self.text.tag_configure("emoji", font=fonts["emoji"])
        self.text.tag_configure("search_hit", background="#806500", foreground="#ffffff")
        self.text.tag_configure("search_current", background="#ffad33", foreground="#000000")
        self.text.tag_raise("sel")
        self.text.bind("<Control-c>", self.copy_selection)
        self.text.bind("<Control-C>", self.copy_selection)
        self.text.bind("<Control-Insert>", self.copy_selection)
        self.text.bind("<Button-1>", self._left_click)
        self.text.bind("<ButtonRelease-1>", self._left_release)
        self.text.bind("<FocusIn>", lambda event: self.app.focus_pane(self), add="+")
        self.text.bind("<Button-3>", self._text_menu)
        for sequence in ("<MouseWheel>", "<Button-4>", "<Button-5>", "<Prior>", "<Next>", "<Home>", "<End>"):
            self.text.bind(sequence, lambda event: self.after_idle(self._remember_scroll), add="+")
        self.set_sources(sources, initial=True)
        self.set_appearance(appearance)
        self.app.features.refresh_composer(self)





    def set_badge_catalog(self, source, catalog):
        if self.closed:
            return
        self.text.configure(state="normal")
        for key, pending in list(self.pending_badge_catalog.items()):
            source_key, set_id, version = key
            if source_key != source.key:
                continue
            meta = catalog.get((set_id, version))
            if not meta:
                continue
            label = self._twitch_badge_label(set_id, meta)
            for marker, tags in pending:
                ranges = self.text.tag_ranges(marker)
                if len(ranges) >= 2:
                    start, end = (str(index) for index in ranges[:2])
                    self.text.delete(start, end)
                    self._insert_badge_at(start, meta["urls"], label, tags)
                self.text.tag_delete(marker)
            self.pending_badge_catalog.pop(key, None)
        self.text.configure(state="disabled")

    @staticmethod
    def _twitch_badge_label(set_id, meta=None):
        labels = {"broadcaster": "[HOST]", "moderator": "[MOD]", "vip": "[VIP]",
                  "founder": "[FOUNDER]", "subscriber": "[SUB]", "bits": "[CHEER]",
                  "staff": "[STAFF]", "admin": "[ADMIN]", "global_mod": "[GM]",
                  "premium": "[PRIME]", "turbo": "[TURBO]"}
        return labels.get(set_id, "[" + str(set_id).replace("_", " ").upper() + "]")

    def _insert_badge_at(self, index, urls, label, tags):
        urls = tuple(urls or ())
        photo = self.badge_photos.get(urls)
        self.image_counter += 1
        name = f"badge_image_{self.image_counter}"
        if photo is not None:
            self._insert_inline_image(index, photo, label, "badge", *tags)
            return
        start = self.text.index(index)
        self.text.insert(index, label, ("badge",) + tuple(tags))
        end = self.text.index(f"{start}+{len(label)}c")
        self.pending_counter += 1
        marker = f"__badge_image_pending_{self.pending_counter}"
        self.text.tag_add(marker, start, end)
        self.pending_badge_images.setdefault(urls, []).append((marker, label, tuple(tags)))
        if urls and urls not in self.image_requests:
            self.image_requests.add(urls)
            EMOTE_IMAGES.request(urls, lambda image, key=urls: self.q.put(("badge_image", (key, image))))

    def _insert_twitch_badge(self, badge, *tags):
        source = self._render_source
        key = (str(badge.get("set_id", "")), str(badge.get("version", "")))
        meta = source.badge_catalog.get(key) if source else None
        label = self._twitch_badge_label(key[0], meta)
        if meta:
            self._insert_badge_at(self.text.index("end-1c"), meta["urls"], label, tags)
        else:
            start = self.text.index("end-1c")
            self._ins(label, "badge", *tags)
            end = self.text.index("end-1c")
            self.pending_counter += 1
            marker = f"__badge_catalog_pending_{self.pending_counter}"
            self.text.tag_add(marker, start, end)
            catalog_key = (source.key if source else "",) + key
            self.pending_badge_catalog.setdefault(catalog_key, []).append((marker, tuple(tags)))
        self._ins(" ", *tags)

    def _local_badge(self, role):
        cache_key = (role, self.appearance.get("badge_size", 18))
        photo = self.local_badge_photos.get(cache_key)
        if photo is not None:
            return photo
        patterns = {
            "host": ("0000000000000000", "0000000000000000", "0010000000000100",
                     "0011000000011100", "0001100000110000", "0000110011000000",
                     "0000011110000000", "0000111111000000", "0001111111100000",
                     "0011111111110000", "0111111111111000", "0011111111110000",
                     "0000000000000000", "0000000000000000", "0000000000000000",
                     "0000000000000000"),
            "moderator": ("0000011111000000", "0001111111110000", "0011000000011000",
                          "0110000000001100", "0110000000001100", "0110000000001100",
                          "0110000000001100", "0110000000001100", "0011000000011000",
                          "0001100000110000", "0000110001100000", "0000011111000000",
                          "0000000000000000", "0000000000000000", "0000000000000000",
                          "0000000000000000"),
            "member": ("0000000110000000", "0000001111000000", "0000011111100000",
                       "0000111111110000", "0001111111111000", "0011111111111100",
                       "0001111111111000", "0000111111110000", "0000011111100000",
                       "0000001111000000", "0000000110000000", "0000000000000000",
                       "0000000000000000", "0000000000000000", "0000000000000000",
                       "0000000000000000"),
        }
        colors = {"host": "#e8b83f", "moderator": "#54b879", "member": "#498fe5"}
        photo = tk.PhotoImage(master=self.text, width=16, height=16)
        for y, row in enumerate(patterns.get(role, patterns["member"])):
            for x, pixel in enumerate(row):
                if pixel == "1":
                    photo.put(colors.get(role, colors["member"]), (x, y))
        photo = fit_photo(photo, self.appearance.get("badge_size", 18), self.appearance.get("badge_size", 18))
        self.local_badge_photos[cache_key] = photo
        return photo

    def _insert_local_badge(self, role, label, *tags):
        photo = self._local_badge(role)
        self.image_counter += 1
        name = f"badge_image_{self.image_counter}"
        start = self.text.index("end-1c")
        self.text.image_create(start, image=photo, name=name, align="center")
        self.image_names[name] = label
        self._ins(" ", *tags)

    def _insert_emote(self, code, meta, *tags):
        urls = tuple(meta.get("urls") or ())
        photo = self.emote_photos.get(urls)
        if photo is not None:
            self._insert_inline_image(self.text.index("end-1c"), photo, code, *tags)
            return
        start = self.text.index("end-1c")
        self._ins(code, "emote", *tags)
        end = self.text.index("end-1c")
        self.pending_counter += 1
        marker = f"__emote_pending_{self.pending_counter}"
        self.text.tag_add(marker, start, end)
        self.pending_emote_tags.setdefault(urls, []).append((marker, code, tags))
        if urls and urls not in self.image_requests:
            self.image_requests.add(urls)
            EMOTE_IMAGES.request(urls, lambda payload, key=urls: self.q.put(("emote_image", (key, payload))))

    def _insert_inline_image(self, start, photo, label, *tags):
        self.image_counter += 1
        name = f"chat_image_{self.image_counter}"
        self.text.image_create(start, image=photo, name=name, align="center")
        end = self.text.index(f"{start}+1c")
        for tag in tags:
            self.text.tag_add(tag, start, end)
        self.image_names[name] = label
        urls = self.photo_urls.get(str(photo))
        if urls:
            self.image_assets[name] = urls
            self.used_assets.add(urls)

    def _resize_gif_photo(self, photo):
        factor = max(1, (max(photo.width(), photo.height()) + 179) // 180)
        return photo.subsample(factor, factor) if factor > 1 else photo

    def _resize_emote_photo(self, photo):
        size = self.appearance.get("emote_size", 36)
        return fit_photo(photo, size, size * 3)

    def image_ready(self, urls, payload):
        if not urls or self.closed:
            return
        pending = self.pending_emote_tags.pop(urls, [])
        if payload is None:
            self.image_requests.discard(urls)
            for marker, _, _ in pending:
                self.text.tag_delete(marker)
            return
        try:
            photo = self.app.photo_bank.emote(urls, payload, self)
        except (tk.TclError, ValueError):
            for marker, _, _ in pending:
                self.text.tag_delete(marker)
            return
        self.emote_photos[urls] = photo
        self.photo_urls[str(photo)] = urls
        at_bottom = self.follow_tail and not self.text.tag_ranges("sel")
        self.text.configure(state="normal")
        for marker, code, tags in pending:
            ranges = self.text.tag_ranges(marker)
            if len(ranges) >= 2:
                start, end = (str(index) for index in ranges[:2])
                self.text.delete(start, end)
                self._insert_inline_image(start, photo, code, *tags)
            self.text.tag_delete(marker)
        self.text.configure(state="disabled")
        if at_bottom:
            self.text.see("end")
        self.refresh_search_later()

    def badge_image_ready(self, urls, raw_image):
        if isinstance(raw_image, dict):
            raw_image = raw_image.get("raw")
        if self.closed or not urls:
            return
        if raw_image is None:
            self.pending_badge_images.pop(urls, None)
            return
        try:
            photo = tk.PhotoImage(master=self.text, data=base64.b64encode(raw_image))
            size = self.appearance.get("badge_size", 18)
            photo = fit_photo(photo, size, size)
        except (tk.TclError, ValueError):
            self.pending_badge_images.pop(urls, None)
            return
        self.badge_photos[urls] = photo
        at_bottom = self.follow_tail and not self.text.tag_ranges("sel")
        self.text.configure(state="normal")
        for marker, label, tags in self.pending_badge_images.pop(urls, []):
            ranges = self.text.tag_ranges(marker)
            if len(ranges) >= 2:
                start, end = (str(index) for index in ranges[:2])
                self.text.delete(start, end)
                self.text.tag_delete(marker)
                self._insert_inline_image(start, photo, label, "badge", *tags)
            else:
                self.text.tag_delete(marker)
        self.text.configure(state="disabled")
        if at_bottom:
            self.text.see("end")

    def _ins(self, s, *tags):
        try:
            self.text.insert("end", s, tags)
        except tk.TclError:  # characters Tk can't draw
            self.text.insert("end", re.sub(r"[^\u0000-\uFFFF]", "?", s), tags)

    def _header_configure(self, event):
        width = max(60, event.width - 60)
        if width == self._header_wrap:
            return
        self._header_wrap = width
        if self.header_after_id is None:
            self.header_after_id = self.after(24, self._apply_header_width)

    def _apply_header_width(self):
        self.header_after_id = None
        if not self.closed:
            self.header_text.configure(wraplength=self._header_wrap)

    def _viewport_configure(self, event):
        target = (max(1, event.width), max(1, event.height))
        if self.closed or target == self.viewport_target:
            return
        self.viewport_target = target
        if self.viewport_applied is None or not self.active:
            self._apply_viewport_size()
            return
        if not self.resizing:
            if self.resize_restore_id is not None:
                self._restore_resize_view()
            self.resizing = True
            self._resize_follow = self.follow_tail and not self.text.tag_ranges("sel")
            self.text.mark_set("__resize_anchor", self.text.index("@0,0"))
            self.text.mark_gravity("__resize_anchor", "left")
            # Render a small, independent surface while dragging. The main
            # text widget keeps all data/selection/marks at its last width.
            if len(self.row_marks) > 100:
                start = self.text.index("@0,0 - 3 lines linestart")
                end = self.text.index(f"@0,{self.text.winfo_height()} + 6 lines lineend +1c")
                self._make_resize_surface(start, end)
        if self.viewport_after_id is None:
            self.viewport_after_id = self.after(16, self._apply_viewport_size)
        if self.resize_after_id is not None:
            self.after_cancel(self.resize_after_id)
        self.resize_after_id = self.after(140, self.finish_resize)

    def _apply_viewport_size(self):
        if self.viewport_after_id is not None:
            self.after_cancel(self.viewport_after_id)
            self.viewport_after_id = None
        if self.closed or self.viewport_target == self.viewport_applied:
            return
        width, height = self.viewport_target
        surface = self.resize_surface if self.resizing and self.resize_surface is not None else self.text
        surface.place_configure(width=width, height=height)
        self.viewport_applied = self.viewport_target
        if self.resizing:
            if self._resize_follow:
                surface.see("end")
            elif surface is self.text:
                self.text.yview("__resize_anchor")
            else:
                surface.yview(self._resize_local_anchor)

    def _make_resize_surface(self, start, end):
        if self.resize_surface is None:
            options = {name: self.text.cget(name) for name in (
                "bg", "fg", "font", "wrap", "padx", "pady", "spacing1", "spacing2", "spacing3",
                "selectbackground", "selectforeground", "inactiveselectbackground")}
            self.resize_surface = tk.Text(self.viewport, bd=0, highlightthickness=0,
                insertwidth=0, cursor="arrow", undo=False, exportselection=False, **options)
            self.resize_surface.bind("<Button-1>", self._resize_surface_click)
            for key in ("<Control-c>", "<Control-C>", "<Control-Insert>"):
                self.resize_surface.bind(key, self.copy_selection)
            for key in ("<MouseWheel>", "<Button-4>", "<Button-5>"):
                self.resize_surface.bind(key, self._resize_surface_scroll)
        surface = self.resize_surface
        surface.configure(state="normal", font=self.text.cget("font"),
                          spacing1=self.text.cget("spacing1"), spacing3=self.text.cget("spacing3"))
        surface.delete("1.0", "end")
        for tag in surface.tag_names():
            if tag != "sel":
                surface.tag_delete(tag)
        active = set(self.text.tag_names(start))
        used = set(active)
        records = self.text.dump(start, end, text=True, image=True, tag=True)
        for kind, value, index in records:
            if kind == "tagon":
                active.add(value);used.add(value)
            elif kind == "tagoff":
                active.discard(value)
            elif kind == "text":
                surface.insert("end", value, tuple(active))
            elif kind == "image":
                at = surface.index("end-1c")
                surface.image_create(at, image=self.text.image_cget(value, "image"), align="center")
                for tag in active:
                    surface.tag_add(tag, at, at + "+1c")
        for tag in self.text.tag_names():
            if tag in used and not tag.startswith(("message_", "__emote_pending_", "__badge_")):
                options = {name: values[-1] for name, values in self.text.tag_configure(tag).items() if values[-1] != ""}
                if options:
                    surface.tag_configure(tag, **options)
                surface.tag_raise(tag)
        first_line = int(start.split('.')[0])
        anchor_line, anchor_column = self.text.index("__resize_anchor").split('.')
        self._resize_local_anchor = str(max(1, int(anchor_line) - first_line + 1)) + "." + anchor_column
        surface.configure(state="disabled")
        self.text.place_forget()

    def _resize_surface_click(self, event):
        self.finish_resize()
        self.text.event_generate("<Button-1>", x=event.x, y=event.y)
        return "break"

    def _resize_surface_scroll(self, event):
        self.finish_resize()
        direction = -1 if getattr(event, "num", 0) == 4 else 1 if getattr(event, "num", 0) == 5 else (-1 if event.delta > 0 else 1)
        self.text.yview_scroll(direction * 3, "units")
        self._remember_scroll()
        return "break"

    def finish_resize(self):
        if self.resize_after_id is not None:
            self.after_cancel(self.resize_after_id)
            self.resize_after_id = None
        if self.closed:
            return
        if not self.resizing:
            self._apply_viewport_size()
            if self.resize_restore_id is not None:
                self._restore_resize_view()
            return
        self.resizing = False
        if self.resize_surface is not None:
            self.resize_surface.place_forget()
            self.resize_surface.configure(state="normal")
            self.resize_surface.delete("1.0", "end")
            self.resize_surface.configure(state="disabled")
        self.viewport_applied = None
        self._apply_viewport_size()
        # Place geometry is applied at idle. Restore scrolling after that
        # layout, so a narrower wrap cannot displace the latest message.
        self.resize_restore_id = self.after_idle(self._restore_resize_view)
        self.app.photo_bank.wake()

    def _restore_resize_view(self):
        if self.resize_restore_id is not None:
            self.after_cancel(self.resize_restore_id)
            self.resize_restore_id = None
        if self.closed:
            return
        if self._resize_follow:
            self.text.see("end")
        elif "__resize_anchor" in self.text.mark_names():
            self.text.yview("__resize_anchor")
        self.text.mark_unset("__resize_anchor")

    def _left_click(self, event):
        self.finish_resize()
        self.app.focus_pane(self)
        self._reply_click = None
        tools = self.app.features.chat_tools
        result = tools.click_user(self, event)
        if result:
            return result
        index = self.text.index(f"@{event.x},{event.y}")
        record = tools.record_at(self, index)
        if not self.detached and record and record[0].platform == "twitch" and record[1].get("reply"):
            self._reply_click = (event.x, event.y, record)

    def _left_release(self, event):
        pending, self._reply_click = self._reply_click, None
        if (pending and abs(pending[0]-event.x) <= 4 and abs(pending[1]-event.y) <= 4
                and not self.text.tag_ranges("sel")):
            self.app.features.chat_tools.show_thread(*pending[2])

    def prepare_font_change(self):
        self.finish_resize()
        if self.font_after_id is not None:
            self._restore_font_view()
        self._font_follow = self.follow_tail and not self.text.tag_ranges("sel")
        self.text.mark_set("__font_anchor", self.text.index("@0,0"))
        self.text.mark_gravity("__font_anchor", "left")

    def finish_font_change(self):
        self.font_after_id = self.after_idle(self._restore_font_view)

    def _restore_font_view(self):
        if self.font_after_id is not None:
            self.after_cancel(self.font_after_id)
            self.font_after_id = None
        if self.closed:
            return
        if self._font_follow:
            self.text.see("end")
        elif "__font_anchor" in self.text.mark_names():
            self.text.yview("__font_anchor")
        self.text.mark_unset("__font_anchor")

    def _begin(self, seq=None):
        self.row_serial += 1
        marker = "row_" + str(self.row_serial)
        self.text.mark_set(marker, "end-1c")
        self.text.mark_gravity(marker, "left")
        self.row_marks.append((marker, seq))
        at_bottom = (self._batch_bottom if self._batch else
                     self.follow_tail and not self.text.tag_ranges("sel"))
        alt = ("alt",) if self.appearance.get("alternate_rows", True) and self.row % 2 else ()
        self.row += 1
        if not self._batch:
            self.text.configure(state="normal")
        return at_bottom, alt

    def _end(self, at_bottom):
        if self._batch:
            return
        self.trim_messages()
        self.text.configure(state="disabled")
        if at_bottom:
            self.text.see("end")

    def trim_messages(self):
        cap = message_limit(self.app.cfg, self.setting_key)
        if len(self.row_marks) <= cap:
            return
        old = []
        while len(self.row_marks) > cap:
            old.append(self.row_marks.popleft())
        delete_to = self.text.index(self.row_marks[0][0])
        for kind, name, _ in self.text.dump("1.0", delete_to, image=True):
            if kind == "image":
                self.image_names.pop(name, None)
                self.image_assets.pop(name, None)
        self.text.delete("1.0", delete_to)
        for marker, seq in old:
            self.timestamp_records.pop(marker, None)
            self.text.mark_unset(marker)
            if seq is not None:
                self.message_records.pop(seq, None)
                self.text.tag_delete("message_" + str(seq))
                color_tag = self.row_color_tags.pop(seq, None)
                if color_tag and not self.text.tag_ranges(color_tag):
                    self.text.tag_delete(color_tag)
                    self.name_tags.discard(color_tag)
        self.used_assets = set(self.image_assets.values())
        for urls in list(self.emote_photos):
            if urls not in self.used_assets and urls not in self.pending_emote_tags:
                photo = self.emote_photos.pop(urls)
                self.photo_urls.pop(str(photo), None)
                self.image_requests.discard(urls)
        for pending in (self.pending_emote_tags, self.pending_badge_images, self.pending_badge_catalog):
            for key, items in list(pending.items()):
                live = []
                for item in items:
                    if self.text.tag_ranges(item[0]):
                        live.append(item)
                    else:
                        self.text.tag_delete(item[0])
                if live:
                    pending[key] = live
                else:
                    pending.pop(key, None)

    def add_system(self, text, sent_at=None):
        if text == self.last_status:
            return
        self.last_status = text
        at_bottom, alt = self._begin()
        err = text.startswith(("Error", "API quota", "Video not found", "The live chat"))
        self._insert_time(clock(), sent_at, alt)
        self._ins(str(tr(text)) + "\n", "sys_err" if err else "sys", *alt)
        self._end(at_bottom)
        self.refresh_search_later()

    def _render_plain(self, text, tags):
        for part in EMOTE_SPLIT.split(text):
            if not part:
                continue
            if CUSTOM_EMOTE.fullmatch(part):
                meta = self._render_source.emote_map.get(part)
                if meta:
                    self._insert_emote(part, meta, *tags)
                    continue
                uni = shortcode_to_unicode(part[1:-1])
                if uni:
                    self._ins(uni, "emoji", *tags)
                else:
                    log_unmapped(part[1:-1])
                    self._ins(part, "emote", *tags)
            elif UNI_EMOJI.fullmatch(part):
                self._ins(part, "emoji", *tags)
            else:
                self._ins(part, *tags)

    def _render_body(self, text, tags, native_emotes=None):
        matches = []
        if self._render_source.emote_pattern:
            for match in self._render_source.emote_pattern.finditer(text):
                meta = self._render_source.emote_map.get(match.group())
                if meta:
                    matches.append((match.start(), match.end(), match.group(), meta))
        for emote in native_emotes or []:
            start, end = emote.get("start", -1), emote.get("end", -1)
            if 0 <= start < end <= len(text):
                matches.append((start, end, text[start:end], emote))
        matches.sort(key=lambda item: (item[0], -(item[1] - item[0])))
        cursor = 0
        for start, end, code, meta in matches:
            if start < cursor:
                continue
            self._render_plain(text[cursor:start], tags)
            self._insert_emote(code, meta, *tags)
            cursor = end
        self._render_plain(text[cursor:], tags)

    def add(self, message, source=None):
        if not self.accepts(message):
            return False
        self._render_source = source or self.sources[0]
        m = message
        at_bottom, alt = self._begin(m.get("_seq"))
        message_start = self.text.index("end-1c")
        color = m.get("color", "")
        if not re.fullmatch(r"#[0-9a-fA-F]{6}", color):
            color = PALETTE[int(hashlib.md5(m["name"].encode()).hexdigest(), 16) % len(PALETTE)]
        tag = "u_" + color
        if tag not in self.name_tags:
            self.text.tag_configure(tag, foreground=color)
            self.name_tags.add(tag)
        reply = m.get("reply")
        if reply and not self.detached:
            self._ins(str(tr("↪ Replying to @{name}: {body}", name=reply.get("name") or reply.get("login", ""),
                             body=reply.get("text", "")[:180])) + "\n", "sys", "reply_context", *alt)
        self._insert_time(m["time"], m.get("sent_at"), alt)
        if len(self.sources) > 1:
            label = self._render_source.name
            self._ins("[" + label + "] ", "source_" + self._render_source.platform, *alt)
        if m.get("youtube_badges"):
            for badge in m["youtube_badges"]:
                if badge.get("urls"):
                    self._insert_badge_at(self.text.index("end-1c"), tuple(badge["urls"]), badge["label"], alt)
                    self._ins(" ", *alt)
                elif badge.get("role") in ("host", "moderator", "member"):
                    self._insert_local_badge(badge["role"], badge["label"], *alt)
                else:
                    self._ins(badge["label"] + " ", "badge", *alt)
        elif m.get("badge_ids"):
            for badge in m["badge_ids"]:
                self._insert_twitch_badge(badge, *alt)
        elif m.get("badges"):
            for badge in m["badges"]:
                role = {"[HOST]": "host", "[MOD]": "moderator", "[MEMBER]": "member"}.get(badge)
                if role:
                    self._insert_local_badge(role, badge, *alt)
                else:
                    self._ins(badge + " ", "badge", *alt)
        self._ins(m["name"], "name", tag, *alt)
        self._ins(" " if m.get("special") == "member" else ": ", *alt)
        special = ([m["special"]] if m.get("special") else []) + list(alt)
        body = str(tr("[message deleted]")) if m.get("deleted") else m["text"]
        self._render_body(body, special, (m.get("gifs") or []) + (m.get("native_emotes") or []))
        self._ins("\n", *special)
        if m.get("_seq") is not None:
            self.text.tag_add("message_" + str(m["_seq"]), message_start, "end-1c")
            self.message_records[m["_seq"]] = (self._render_source, m)
            self.row_color_tags[m["_seq"]] = tag
        self.app.features.apply_highlight(self, m, message_start, "end-1c")
        self.text.tag_raise("sel")
        self._end(at_bottom)
        self.refresh_search_later()
        return True

    def set_filters(self, filters):
        self.filters = filters if isinstance(filters, dict) else {}
        self._hidden_users = frozenset(str(value).casefold() for value in self.filters.get("hidden_users", []))
        self._include_terms = tuple(str(value).casefold() for value in self.filters.get("include_terms", []) if value)
        self._exclude_terms = tuple(str(value).casefold() for value in self.filters.get("exclude_terms", []) if value)

    def _insert_time(self, value, sent_at, tags):
        self.timestamp_records[self.row_marks[-1][0]] = (value, sent_at)
        self._ins(display_time(value, sent_at) + " ", "time", *tags)

    def refresh_timestamps(self):
        self.finish_resize()
        follow = self.follow_tail and not self.text.tag_ranges("sel")
        self.text.mark_set("__time_anchor", self.text.index("@0,0"))
        self.text.configure(state="normal")
        rows = list(self.row_marks)
        for i, (marker, _) in enumerate(rows):
            raw = self.timestamp_records.get(marker)
            if raw is None:
                continue
            end = rows[i+1][0] if i+1 < len(rows) else "end"
            span = self.text.tag_nextrange("time", marker, end)
            if len(span) == 2:
                start, stop = map(str, span)
                tags = self.text.tag_names(start)
                self.text.delete(start, stop)
                self.text.insert(start, display_time(*raw) + " ", tags)
        self.text.configure(state="disabled")
        if follow:
            self.text.see("end")
        else:
            self.text.yview("__time_anchor")
        self.text.mark_unset("__time_anchor")
        self.refresh_search_later()

    def set_appearance(self, appearance):
        self.finish_resize()
        self.appearance = appearance
        state = (appearance.get("compact_spacing", False), appearance.get("show_timestamps", True),
                 appearance.get("alternate_rows", True))
        old = getattr(self, "_appearance_render_state", (None, None, None))
        if state[0] != old[0]:
            gap = 1 if state[0] else 3
            self.text.configure(spacing1=gap, spacing3=gap)
        if state[1] != old[1]:
            self.text.tag_configure("time", foreground=DIM, elide=not state[1])
        if state[2] != old[2]:
            self.text.tag_remove("alt", "1.0", "end")
            if state[2]:
                rows = list(self.row_marks)
                first = self.row-len(rows)
                for i, (marker, _) in enumerate(rows):
                    if (first+i) % 2:
                        self.text.tag_add("alt", marker, rows[i+1][0] if i+1 < len(rows) else "end-1c")
        self._appearance_render_state = state
        size = (appearance.get("emote_size", 36), appearance.get("badge_size", 18))
        if getattr(self, "_image_font_size", size) != size:
            self.load_recent()
        self._image_font_size = size

    def set_search(self, query, reveal=True):
        self.finish_resize()
        previous_index = self.search_index
        self.search_query = query
        self.search_matches = []
        self.search_index = -1
        self.text.tag_remove("search_hit", "1.0", "end")
        self.text.tag_remove("search_current", "1.0", "end")
        if not query:
            return 0
        start = "1.0"
        while True:
            found = self.text.search(query, start, stopindex="end-1c", nocase=True)
            if not found:
                break
            end = self.text.index(f"{found}+{len(query)}c")
            self.search_matches.append(found)
            self.text.tag_add("search_hit", found, end)
            start = end
        if self.search_matches:
            self.search_index = 0 if reveal else max(0, min(previous_index, len(self.search_matches) - 1))
            self._show_search_match(reveal=reveal)
        return len(self.search_matches)

    def _show_search_match(self, reveal=True):
        self.text.tag_remove("search_current", "1.0", "end")
        if 0 <= self.search_index < len(self.search_matches):
            start = self.search_matches[self.search_index]
            end = self.text.index(f"{start}+{len(self.search_query)}c")
            self.text.tag_add("search_current", start, end)
            if reveal:
                self.follow_tail = False
                self.text.see(start)

    def step_search(self, direction):
        if not self.search_matches:
            return 0
        self.search_index = (self.search_index + direction) % len(self.search_matches)
        self._show_search_match()
        return self.search_index + 1


    def set_active(self, active):
        active = bool(active) and not self.closed
        if active and not self.active and not self.detached:
            events = [event for source in self.sources for event in source.history if event[0] > self.last_seq]
            cap = message_limit(self.app.cfg, self.setting_key)
            self.pending_events = deque(sorted(events, key=lambda event: event[0])[-cap:], maxlen=cap)
        self.active = active
        if active:
            self.app.photo_bank.wake()
            if self.follow_tail:
                self.after_idle(lambda: self.text.see("end") if not self.closed else None)
        for source in self.sources:
            source.sync_pause()

    def copy_selection(self, event=None):
        self.finish_resize()
        try:
            parts = []
            for kind, value, _ in self.text.dump(
                    "sel.first", "sel.last", text=True, image=True):
                if kind == "text":
                    parts.append(value)
                elif kind == "image":
                    parts.append(self.image_names.get(value, ""))
            selected = "".join(parts)
        except tk.TclError:
            return "break"
        self.clipboard_clear()
        self.clipboard_append(selected)
        return "break"

    def close(self):
        if self.closed:
            return
        for attribute in ("resize_after_id", "resize_restore_id", "viewport_after_id", "header_after_id", "font_after_id"):
            timer = getattr(self, attribute, None)
            if timer is not None:
                self.after_cancel(timer)
                setattr(self, attribute, None)
        self.closed = True
        self.active = False
        if self.search_after_id is not None:
            self.after_cancel(self.search_after_id)
        for source in self.sources:
            source.views.discard(self)
            source.sync_pause()
        self.pending_events.clear()


    @property
    def display_name(self):
        return " + ".join(source.name for source in self.sources)

    def set_sources(self, sources, initial=False):
        unique = list(dict.fromkeys(sources))
        if not unique:
            return
        if len(unique) > 32:
            messagebox.showinfo(tr("Mixed chat"), tr("A mixed feed can contain up to 32 channels."), parent=self.app.root)
            return
        for source in self.sources:
            source.views.discard(self)
            source.sync_pause()
        self.sources = unique
        single = len(unique) == 1
        self.platform = unique[0].platform if single else "mixed"
        self.video_id = unique[0].video_id if single else "mix:" + self.uid
        self.setting_key = unique[0].key if single else "mix:" + self.uid
        self.channel_login = unique[0].channel_login if single else None
        self.set_filters(self.app.cfg.get("chat_filters", {}).get(self.setting_key, {}))
        cap = message_limit(self.app.cfg, self.setting_key)
        for item in unique:
            retained = max([cap, message_limit(self.app.cfg)] + [message_limit(self.app.cfg, p.setting_key) for p in item.views])
            item.history = deque(item.history, maxlen=retained)
            item.history_live = deque(item.history_live, maxlen=retained)
        for source in unique:
            source.views.add(self)
            source.ensure_worker()
        self.refresh_header()
        if not self.detached:
            self.load_recent()
        if not initial:
            self.app.prune_sources()
            self.app.refresh_workspace_title(self.workspace)
            self.app.schedule_session_save()

    def refresh_header(self):
        self.app.features.refresh_composer(self)
        self.header_icon.delete("all")
        if self.platform == "twitch":
            self.header_icon.create_polygon(3, 0, 16, 0, 16, 10, 12, 14, 9, 14,
                                            7, 16, 5, 16, 5, 14, 1, 14, 1, 4,
                                            fill="#9146ff", outline="")
            self.header_icon.create_polygon(4, 2, 14, 2, 14, 9, 11, 12, 8, 12,
                                            6, 14, 6, 12, 4, 12, fill=HEADER_BG, outline="")
            self.header_icon.create_rectangle(7, 5, 9, 9, fill="#9146ff", outline="")
            self.header_icon.create_rectangle(11, 5, 13, 9, fill="#9146ff", outline="")
        elif self.platform == "youtube":
            self.header_icon.create_rectangle(1, 2, 17, 14, fill="#ff0033", outline="")
            self.header_icon.create_polygon(7, 4, 7, 12, 13, 8, fill="#ffffff", outline="")
        else:
            self.header_icon.create_text(9, 7, text=tr("≋"), fill=ACCENT, font=("Segoe UI", 17, "bold"))
        if len(self.sources) > 1:
            title = str(tr("Mix · {name}", name=self.display_name))
            self.header_info = {"title": title, "channel": self.display_name}
        else:
            source = self.sources[0]
            info = source.header_info
            self.header_info = info
            viewers = info.get("viewers")
            count = f"{viewers:,}" if isinstance(viewers, (int, float)) else "—"
            if source.platform == "twitch":
                if info.get("live") is False:
                    count = str(tr("Offline"))
                parts = [count, info.get("category") or "—", info.get("title") or source.name]
            else:
                parts = [count, info.get("title") or source.name]
            title = " - ".join(parts)
        self.header_text.config(text=tr(title[:237] + "…" if len(title) > 240 else title))

    def _on_scroll(self, first, last):
        if self.resizing:
            return
        self.scrollbar.set(first, last)
        if not hasattr(self, "jump_button"):
            return
        visible = float(last) < 0.995
        offset = -max(42, self.composer.winfo_height() + 8)
        if visible and (not self._jump_placed or offset != self._jump_offset):
            self.jump_button.place(relx=0.5, rely=1.0, y=offset, anchor="s")
            self._jump_placed, self._jump_offset = True, offset
        elif not visible and self._jump_placed:
            self.jump_button.place_forget()
            self._jump_placed = False

    def jump_latest(self):
        self.finish_resize()
        self.follow_tail = True
        self.text.tag_remove("sel", "1.0", "end")
        self.text.see("end")
        self.jump_button.place_forget()
        self._jump_placed = False

    def accepts(self, message):
        name = message.get("name", "").casefold()
        body = message.get("text", "").casefold()
        return (name not in self._hidden_users
                and (not self._include_terms or any(term in body for term in self._include_terms))
                and not any(term in body for term in self._exclude_terms))

    def drain(self, deadline, limit=70):
        if not self.active or self.resizing or not self.pending_events:
            return
        self._batch_bottom = self.follow_tail and not self.text.tag_ranges("sel")
        self._batch = True
        self.text.configure(state="normal")
        try:
            count = 0
            while self.pending_events and count < limit and time.monotonic() < deadline:
                sequence, kind, value, source = self.pending_events.popleft()
                if sequence <= self.last_seq:
                    continue
                self.last_seq = sequence
                self._render_source = source
                if kind == "msg":
                    self.add(value, source)
                else:
                    self.add_system((f"[{source.name}] " if len(self.sources) > 1 else "") + value, source.status_times.get(sequence))
                count += 1
        finally:
            self._batch = False
            self._end(self._batch_bottom)

    def load_recent(self):
        if self.detached:
            self.thread_window.rebuild()
            return
        self.clear_messages(reset_sequence=True)
        events = [event for source in self.sources for event in source.history]
        cap = message_limit(self.app.cfg, self.setting_key)
        self.pending_events = deque(sorted(events, key=lambda event: event[0])[-cap:], maxlen=cap)

    def clear_messages(self, reset_sequence=False):
        self.finish_resize()
        self.text.configure(state="normal")
        self.text.delete("1.0", "end")
        self.text.configure(state="disabled")
        for tag in self.text.tag_names():
            if tag.startswith(("__", "message_")):
                self.text.tag_delete(tag)
        self.pending_events.clear()
        self.last_seq = 0 if reset_sequence else self.app.message_sequence
        self.message_records.clear()
        self.timestamp_records.clear()
        self.row_color_tags.clear()
        for color_tag in self.name_tags:
            self.text.tag_delete(color_tag)
        self.name_tags.clear()
        for marker, _ in self.row_marks:
            self.text.mark_unset(marker)
        self.row_marks.clear()
        self.image_names.clear()
        self.image_assets.clear()
        self.used_assets.clear()
        self.pending_emote_tags.clear()
        self.pending_badge_images.clear()
        self.pending_badge_catalog.clear()
        self.emote_photos.clear()
        self.badge_photos.clear()
        self.photo_urls.clear()
        self.image_requests.clear()
        self.row = 0
        self.last_status = None
        self.set_search(self.search_query)

    def refresh_search_later(self):
        if self.search_query and self.search_after_id is None:
            self.search_after_id = self.after(180, self._refresh_search)

    def _refresh_search(self):
        self.search_after_id = None
        if self.closed:
            return
        self.set_search(self.search_query, reveal=False)

    def _remember_scroll(self):
        self.finish_resize()
        if not self.closed:
            self.follow_tail = self.text.yview()[1] >= 0.995

    def _scrollbar_view(self, *args):
        self.finish_resize()
        self.text.yview(*args)
        self._remember_scroll()

    def _text_menu(self, event):
        self.finish_resize()
        self.app.focus_pane(self)
        menu = self.app._menu(self)
        selected = bool(self.text.tag_ranges("sel"))
        menu.add_command(label=tr("Copy"), command=self.copy_selection,
                         state="normal" if selected else "disabled")
        index = self.text.index(f"@{event.x},{event.y}")
        start, end = self.text.index(index + " linestart"), self.text.index(index + " lineend")
        def copy_line():
            parts = [value if kind == "text" else self.image_names.get(value, "")
                     for kind, value, _ in self.text.dump(start, end, text=True, image=True)]
            self.clipboard_clear()
            self.clipboard_append("".join(parts))
        menu.add_command(label=tr("Copy message"), command=copy_line)
        menu.add_command(label=tr("Select all"), command=lambda: self.text.tag_add("sel", "1.0", "end-1c"))
        line = self.text.get(start, end)
        for url in re.findall(r"https?://[^\s<>]+", line)[:3]:
            menu.add_command(label=tr("Open " + url[:50]), command=lambda link=url: webbrowser.open(link))
        self.app.features.message_menu(self, menu, index)
        menu.add_separator()
        menu.add_command(label=tr("Find…  Ctrl+F"), command=self.app.show_search)
        menu.add_command(label=tr("Jump to latest"), command=self.jump_latest)
        menu.add_command(label=tr("Clear this view"), command=self.clear_messages)
        menu.tk_popup(event.x_root, event.y_root)


# -------------------------------------------------------------------- app
def style_titlebar(root):
    """Make the Windows title bar (and its - [] X buttons) black. Windows 11 needed for pure black."""
    if sys.platform != "win32":
        try:
            root._app_icon = tk.PhotoImage(file=os.path.join(os.path.dirname(__file__), "ytchatterino-logo.png"))
            root.iconphoto(True, root._app_icon)
        except tk.TclError:
            pass
        return
    try:
        if os.path.isfile(APP_ICON_PATH):
            root.iconbitmap(APP_ICON_PATH)
    except Exception:
        pass
    try:
        from ctypes import wintypes
        root.update_idletasks()
        hwnd = wintypes.HWND(ctypes.windll.user32.GetParent(root.winfo_id()))

        def set_attr(attr, value):
            v = ctypes.c_int(value)
            ctypes.windll.dwmapi.DwmSetWindowAttribute(hwnd, attr, ctypes.byref(v), ctypes.sizeof(v))

        set_attr(20, 1)            # dark title bar (Windows 10 2004+ / 11)
        set_attr(35, 0x000000)     # caption color: black (Windows 11)
        set_attr(36, 0xFFFFFF)     # caption text: white
        set_attr(34, 0x000000)     # window border: black
    except Exception:
        pass



def font_sizes(appearance):
    def value(key):
        try:
            return max(7, min(48, int(appearance.get(key, appearance.get("font_size", 12)))))
        except (TypeError, ValueError, OverflowError):
            return 12
    return value("message_font_size"), value("username_font_size")


class App:
    def __init__(self, root):
        self.root = root
        self._closing = False
        self._restoring = True
        self.ui_queue = queue.SimpleQueue()
        self.sources = {}
        self.message_sequence = 0
        self.current_workspace = None
        self.extra_panes = set()
        self.session_after_id = None
        self.pump_after_id = None
        self.next_quota_update = 0.0
        self._source_cursor = 0
        self._pane_cursor = 0
        self.photo_bank = PhotoBank(self)
        self.cfg = load_config()
        self.cfg.pop("youtube_reader", None)  # obsolete preference; all reading is keyless
        self.cfg.setdefault("message_limit", 2000)
        set_language(self.cfg.get("language", "en"))
        install_ui(root)
        self.twitch_store = TwitchTokenStore()
        self.twitch_manager = None
        self.features = FeatureController(self, sys.modules[__name__])
        self._twitch_auth_busy = False
        self._twitch_auth_waiters = []
        self.appearance = {"font_size": 12, "compact_spacing": False,
                           "show_timestamps": True, "alternate_rows": True, "badge_size": 18, "emote_size": 36,
                           "date_format": "DD/MM/YYYY", "time_format": "HH:mm"}
        self.appearance.update(self.cfg.get("appearance", {}))
        old_size = self.appearance.get("font_size", 12)
        self.appearance.setdefault("message_font_size", old_size)
        self.appearance.setdefault("username_font_size", old_size)
        self.appearance["time_format"] = str(self.appearance.get("time_format", "HH:mm")).replace("AM/PM", "am/pm")
        set_datetime_formats(self.appearance)
        self.tabs = []
        self.current = None
        root.title(tr("YT-Chatterino"))
        root.geometry("520x760")
        root.minsize(340, 300)
        root.configure(bg=BG)
        self.window_state = WindowState(root, self.cfg.get("session", {}))
        # default colors for every plain Tk widget, including the API-key dialog
        for opt, val in (("*Background", BG), ("*Foreground", FG), ("*activeBackground", "#1f1f1f"),
                         ("*activeForeground", FG), ("*insertBackground", FG),
                         ("*highlightBackground", BG), ("*selectBackground", "#2a2a2a"),
                         ("*selectForeground", FG)):
            root.option_add(opt, val)

        font_size, username_size = font_sizes(self.appearance)
        self.appearance.update(message_font_size=font_size, username_font_size=username_size, font_size=font_size)
        self.fonts = {"normal": tkfont.Font(family="Segoe UI", size=font_size),
                      "bold": tkfont.Font(family="Segoe UI", size=font_size, weight="bold"),
                      "username": tkfont.Font(family="Segoe UI", size=username_size, weight="bold"),
                      "emoji": tkfont.Font(family="Segoe UI Emoji", size=font_size + 5)}

        style = ttk.Style()
        style.theme_use("clam")
        style.configure("TCombobox", fieldbackground="#111111", background="#222222",
                        foreground=FG, arrowcolor=DIM, bordercolor=EDGE)
        style.map("TCombobox", fieldbackground=[("readonly", "#111111")],
                  foreground=[("readonly", FG)], selectbackground=[("readonly", "#222222")],
                  selectforeground=[("readonly", FG)])
        root.option_add("*TCombobox*Listbox.background", BG)
        root.option_add("*TCombobox*Listbox.foreground", FG)
        style.configure("Vertical.TScrollbar", background="#262626", troughcolor=BG,
                        bordercolor=BG, lightcolor="#262626", darkcolor="#262626",
                        arrowcolor=DIM, relief="flat", borderwidth=0)
        self.sb_style = "Vertical.TScrollbar"
        try:  # slim scrollbar without arrow buttons
            style.layout("Slim.Vertical.TScrollbar", [
                ("Vertical.Scrollbar.trough", {"sticky": "ns", "children": [
                    ("Vertical.Scrollbar.thumb", {"expand": "1", "sticky": "nswe"})]})])
            style.configure("Slim.Vertical.TScrollbar", background="#1c1c1c", troughcolor=BG,
                            bordercolor=BG, lightcolor="#1c1c1c", darkcolor="#1c1c1c",
                            arrowsize=8, relief="flat", borderwidth=0)
            style.map("Slim.Vertical.TScrollbar", background=[("active", "#3d3d3d")],
                      lightcolor=[("active", "#3d3d3d")], darkcolor=[("active", "#3d3d3d")])
            self.sb_style = "Slim.Vertical.TScrollbar"
        except tk.TclError:
            pass

        self.top_var = tk.BooleanVar(value=bool(self.cfg.get("topmost")))

        # ---- top strip: tabs, "+", quota, gear
        self.strip = tk.Frame(root, bg=BG)
        self.strip.pack(side="top", fill="x")
        self.gear = tk.Label(self.strip, text=tr("\u2699"), bg=BG, fg=DIM,
                             font=("Segoe UI Symbol", 12), padx=8, cursor="hand2")
        self.gear.pack(side="right")
        self.gear.bind("<Button-1>", lambda event: self.features.show_settings())
        self.gear.bind("<Button-3>", self.show_gear_menu)
        self.quota_lbl = tk.Label(self.strip, text="", bg=BG, fg=DIM, font=("Segoe UI", 8), padx=4)
        self.quota_lbl.configure(cursor="hand2")
        self.quota_lbl.bind("<Button-1>", lambda event: self.features.show_settings("YouTube quota"))
        self.update_button = tk.Label(self.strip, text="⬇", bg=BG, fg="#78c34b",
                                      font=("Segoe UI Symbol", 13), padx=6, cursor="hand2")
        from ytchatterino_update import UpdateController
        self.updater = UpdateController(self, sys.modules[__name__])
        self.update_button.bind("<Button-1>", lambda event: self.updater.show())
        self.layout_button = tk.Label(self.strip, text=tr("▦"), bg=BG, fg=DIM,
                                       font=("Segoe UI", 13), padx=7, cursor="hand2")
        self.layout_button.pack(side="right")
        self.layout_button.bind("<Button-1>", self.layout_menu)
        self.tabs_canvas = tk.Canvas(self.strip, bg=BG, bd=0, highlightthickness=0, height=30, width=1)
        self.tabs_canvas.pack(side="left", fill="x", expand=True)
        self.tabs_frame = tk.Frame(self.tabs_canvas, bg=BG)
        self.tabs_window = self.tabs_canvas.create_window((0, 0), window=self.tabs_frame, anchor="nw")
        self.tabs_frame.bind("<Configure>", lambda event: self.tabs_canvas.configure(
            scrollregion=self.tabs_canvas.bbox("all")))
        self.plus = tk.Label(self.strip, text=tr("+"), bg=BG, fg=FG, font=("Segoe UI", 13),
                             padx=10, cursor="hand2")
        self.plus.pack(side="right")
        self.plus.bind("<Button-1>", lambda e: self.add_stream_dialog())
        for widget in (self.strip, self.tabs_canvas, self.tabs_frame, self.plus):
            widget.bind("<Button-2>", lambda event: self.add_stream_dialog())

        # ---- chat area
        self.content = tk.Frame(root, bg=BG)
        self.content.pack(fill="both", expand=True)
        self.empty = tk.Label(self.content, text=tr("Click + to add chats.\nUse ▦ to split or mix channels."),
                              bg=BG, fg=DIM, font=("Segoe UI", 11), justify="center")

        self.bind_shortcut("<Control-t>", lambda e: self.add_stream_dialog())
        self.bind_shortcut("<Control-Shift-t>", lambda e: self.add_stream_dialog("horizontal", self.current))
        self.bind_shortcut("<Control-w>", lambda e: self.close_current())
        self.bind_shortcut("<Control-Shift-w>", lambda e: self.close_tab(self.current_workspace) if self.current_workspace else None)
        self.bind_shortcut("<Control-Tab>", lambda e: self.select_relative(1))
        self.bind_shortcut("<Control-Shift-Tab>", lambda e: self.select_relative(-1))
        self.bind_shortcut("<Control-Key-1>", lambda e: self.select_numbered(0))
        self.bind_shortcut("<Control-Key-2>", lambda e: self.select_numbered(1))
        self.bind_shortcut("<Control-Key-3>", lambda e: self.select_numbered(2))
        self.bind_shortcut("<Control-Key-4>", lambda e: self.select_numbered(3))
        self.bind_shortcut("<Control-Key-5>", lambda e: self.select_numbered(4))
        self.bind_shortcut("<Control-Key-6>", lambda e: self.select_numbered(5))
        self.bind_shortcut("<Control-Key-7>", lambda e: self.select_numbered(6))
        self.bind_shortcut("<Control-Key-8>", lambda e: self.select_numbered(7))
        self.bind_shortcut("<Control-Key-9>", lambda e: self.select_numbered(8, last=True))
        self.bind_shortcut("<Control-f>", lambda e: self.show_search())
        self.bind_shortcut("<Control-Shift-f>", lambda e: self.show_global_search())
        self.bind_shortcut("<Control-F5>", lambda e: self.reconnect_current())
        self.bind_shortcut("<Control-equal>", lambda e: self.change_font(1))
        self.bind_shortcut("<Control-plus>", lambda e: self.change_font(1))
        self.bind_shortcut("<Control-minus>", lambda e: self.change_font(-1))
        self.bind_shortcut("<Control-Key-0>", lambda e: self.reset_font())
        self.bind_shortcut("<Control-p>", lambda e: self.features.show_settings("Appearance"))
        self.bind_shortcut("<Control-k>", lambda e: self.quick_switcher())
        root.bind_all("<MouseWheel>", self._wheel_tabs, add="+")
        root.bind_all("<Button-4>", self._wheel_tabs, add="+")
        root.bind_all("<Button-5>", self._wheel_tabs, add="+")
        root.bind_all("<Map>", self._style_mapped_window, add="+")
        for direction in ("Left", "Right", "Up", "Down"):
            self.bind_shortcut("<Alt-" + direction + ">", lambda event, value=direction: self.focus_direction(value.lower()))
        self.bind_shortcut("<Control-Alt-m>", lambda event: self.add_stream_dialog("mix", self.current))
        root.bind("<Configure>", self.schedule_session_save, add="+")
        root.bind("<Unmap>", self.sync_visibility, add="+")
        root.bind("<Map>", self.sync_visibility, add="+")
        root.protocol("WM_DELETE_WINDOW", self.quit)
        root.report_callback_exception = self._log_exc
        root.attributes("-topmost", bool(self.cfg.get("topmost")))

        saved_auth = self._read_twitch_auth()
        if saved_auth and saved_auth.get("access_token"):
            self.twitch_manager = TwitchIRCManager(saved_auth,
                on_auth_updated=lambda updated: self.call_ui(lambda: self._save_twitch_auth(updated)))
            self.twitch_manager.start()
        self._refresh_empty()
        self.restore_session()
        self.features.start()
        self.pump()
        root.after(50, lambda: style_titlebar(root))
        root.after(400, lambda: style_titlebar(root))
        if len(sys.argv) == 3 and sys.argv[1] == "--updated":
            from ytchatterino_update import acknowledge_update
            root.after(500, lambda: acknowledge_update(sys.argv[2]))

    # ---- "+" dialog (the only way to add a stream)
    def add_stream_dialog(self, mode="tabs", anchor=None):
        existing = getattr(self, "_dlg", None)
        if existing is not None and existing.winfo_exists():
            existing.lift()
            return
        dialog = tk.Toplevel(self.root)
        self._dlg = dialog
        dialog.title(tr("Add chats"))
        dialog.configure(bg=BG)
        dialog.transient(self.root)
        dialog.resizable(True, False)
        tk.Label(dialog, text=tr("YouTube links or Twitch @channels · one per line"), bg=BG, fg=FG,
                 font=("Segoe UI", 10)).pack(anchor="w", padx=14, pady=(14, 5))
        entry = tk.Text(dialog, width=48, height=3, bg=BG, fg=FG, insertbackground=FG,
                        bd=0, highlightthickness=1, highlightbackground=EDGE, font=("Segoe UI", 10))
        entry.pack(fill="x", padx=14)
        existing_sources = list(self.sources.values())
        listing = None
        if existing_sources:
            tk.Label(dialog, text=tr("Or reuse open channels"), bg=BG, fg=DIM).pack(anchor="w", padx=14, pady=(10, 3))
            listing = tk.Listbox(dialog, selectmode="multiple", height=min(5, len(existing_sources)),
                                 bg=BG, fg=FG, selectbackground="#303030", exportselection=False,
                                 bd=0, highlightthickness=1, highlightbackground=EDGE)
            listing.pack(fill="x", padx=14)
            for source in existing_sources:
                listing.insert("end", ("Twitch · " if source.platform == "twitch" else "YouTube · ") + source.name)
            if anchor and mode == "mix":
                for index, source in enumerate(existing_sources):
                    if source in anchor.sources:
                        listing.selection_set(index)
        row = tk.Frame(dialog, bg=BG)
        row.pack(fill="x", padx=14, pady=(12, 3))
        tk.Label(row, text=tr("Layout"), bg=BG, fg=FG).pack(side="left", padx=(0, 10))
        modes = {"Separate tabs": "tabs", "Side by side": "horizontal", "Stacked": "vertical", "Mixed feed": "mix"}
        combo = ttk.Combobox(row, values=[tr(value) for value in modes], state="readonly", width=22)
        combo.current(next(i for i, value in enumerate(modes.values()) if value == mode))
        combo.pack(side="left", fill="x", expand=True)
        use_current = tk.BooleanVar(value=anchor is not None)
        current_check = tk.Checkbutton(dialog, text=tr("Use current tab"), variable=use_current,
                                       bg=BG, fg=FG, activebackground=BG, activeforeground=FG, selectcolor="#222222")
        if self.current:
            current_check.pack(anchor="w", padx=11, pady=3)
        status = tk.Label(dialog, text="", bg=BG, fg="#ff7777", anchor="w", wraplength=370)
        status.pack(fill="x", padx=14, pady=(3, 0))

        def submit(event=None):
            specs = [existing_sources[index].spec() for index in listing.curselection()] if listing else []
            for value in re.split(r"[\s,]+", entry.get("1.0", "end-1c").strip()):
                if not value:
                    continue
                spec = self.chat_spec(value)
                if spec is None:
                    status.config(text=tr("Use a YouTube video link or Twitch @channel: " + value[:70]))
                    return "break"
                specs.append(spec)
            unique = {(spec["platform"], spec["id"]): spec for spec in specs}
            specs = list(unique.values())
            if not specs:
                status.config(text=tr("Enter a link or select an open channel."))
                return "break"
            if len(specs) > 24:
                status.config(text=tr("Add up to 24 channels at a time."))
                return "break"
            chosen = list(modes.values())[combo.current()]
            selected_anchor = (anchor or self.current) if use_current.get() and chosen != "tabs" else None
            dialog.destroy()
            self.open_specs(specs, chosen, selected_anchor)
            return "break"

        buttons = tk.Frame(dialog, bg=BG)
        buttons.pack(fill="x", padx=14, pady=12)
        tk.Label(buttons, text=tr("Ctrl+Enter to open"), bg=BG, fg=DIM, font=("Segoe UI", 8)).pack(side="left")
        for label, command in (("Open", submit), ("Cancel", dialog.destroy)):
            tk.Button(buttons, text=tr(label), command=command, bg=BG, fg=FG, bd=0,
                      padx=14, pady=5, activebackground="#252525", activeforeground=FG).pack(side="right", padx=3)
        entry.bind("<Control-Return>", submit)
        dialog.bind("<Escape>", lambda event: dialog.destroy())
        dialog.update_idletasks()
        dialog.geometry(f"+{self.root.winfo_rootx() + 20}+{self.root.winfo_rooty() + 70}")
        entry.focus_set()
        dialog.grab_set()

    def _style_mapped_window(self, event):
        window = event.widget
        try:
            if not isinstance(window, tk.Misc):
                return  # Tcl-owned combobox popups have no Python widget object
            if window.winfo_toplevel() is not window:
                return
            if window is not self.root:
                window.attributes("-topmost", self.top_var.get())
            if not hasattr(window, "_titlebar_timers"):
                window._titlebar_timers = set()
                def cancel_timers(event):
                    if event.widget is window:
                        for timer in window._titlebar_timers:
                            window.after_cancel(timer)
                        window._titlebar_timers.clear()
                window.bind("<Destroy>", cancel_timers, add="+")
            for timer in window._titlebar_timers:
                window.after_cancel(timer)
            window._titlebar_timers.clear()
            for delay in (50, 400):
                def apply_style(w=window):
                    if w.winfo_exists():
                        style_titlebar(w)
                window._titlebar_timers.add(window.after(delay, apply_style))
        except tk.TclError:
            pass

    def _log_exc(self, exc, val, tb):
        import traceback
        try:
            with open(LOG_PATH, "a", encoding="utf-8") as f:
                f.write("".join(traceback.format_exception(exc, val, tb)) + "\n")
        except Exception:
            pass

    def quota_estimate(self):
        used = QUOTA.snapshot()
        try:
            limit = min(10000000, max(1, int(self.cfg.get("youtube_daily_limit", DAILY_LIMIT))))
        except (ValueError, TypeError):
            limit = DAILY_LIMIT
        adjustment = self.cfg.get("youtube_external_usage", {})
        try:
            external = max(0, int(adjustment.get("used", 0))) if adjustment.get("date") == _pacific_date() else 0
        except (ValueError, TypeError, AttributeError):
            external = 0
        left = max(0, limit - used - external)
        return {"limit": limit, "used": used, "external": external, "left": left,
                "messages": max(0, left - 1) // 20, "deletions": left // 26,
                "bans": max(0, left - 1) // 200}

    def update_quota(self):
        show = bool(self.current and not self.current.closed and any(source.platform == "youtube" for source in self.current.sources))
        if not show:
            self.quota_lbl.pack_forget()
            return
        if not self.quota_lbl.winfo_manager():
            self.quota_lbl.pack(side="right", after=self.update_button if self.update_button.winfo_manager() else self.gear)
        estimate = self.quota_estimate()
        fraction = estimate["left"] / estimate["limit"]
        color = "#ff5a5a" if fraction <= 0.1 else "#ffb347" if fraction <= 0.3 else DIM
        label = tr("YT ~{left} u", left=number(estimate["left"]))
        if self.quota_lbl.cget("text") != str(label) or self.quota_lbl.cget("fg") != color:
            self.quota_lbl.config(text=tr(label), fg=color)
        self.features.refresh_quota()

    def _menu(self, parent):
        return tk.Menu(parent, tearoff=0, bg=BG, fg=FG, activebackground="#1f1f1f",
                       activeforeground=FG, selectcolor=FG, bd=0, relief="flat",
                       font=("Segoe UI", 10))

    def show_gear_menu(self, event=None):
        menu = self._menu(self.root)
        youtube = [source for source in self.sources.values() if source.platform == "youtube"]
        paused = bool(youtube) and all(source.manual_paused for source in youtube)
        menu.add_command(label=tr("Add chats…  Ctrl+T"), command=self.add_stream_dialog)
        menu.add_command(label=tr("Split / mix chats…"), command=lambda: self.add_stream_dialog("horizontal", self.current))
        menu.add_command(label=tr("Reconnect current pane  Ctrl+F5"), command=self.reconnect_current)
        menu.add_command(label=tr("Resume all YouTube chats" if paused else "Pause all YouTube chats"),
                         command=self.toggle_all_chats, state="normal" if youtube else "disabled")
        menu.add_command(label=tr("Saved channels…"), command=self.show_saved_channels)
        menu.add_separator()
        menu.add_command(label=tr("Twitch account…"), command=self.show_twitch_account)
        menu.add_command(label=tr("Settings…  Ctrl+P"), command=self.features.show_settings)
        menu.add_command(label=tr("Appearance…"), command=lambda: self.features.show_settings("Appearance"))
        menu.add_command(label=tr("Keyboard shortcuts…"), command=lambda: self.features.show_settings("Keyboard shortcuts"))
        menu.add_checkbutton(label=tr("Always on top"), variable=self.top_var, command=self.toggle_top)
        menu.add_separator()
        menu.add_command(label=tr("Quit"), command=self.quit)
        menu.tk_popup(max(0, self.gear.winfo_rootx() - 150), self.gear.winfo_rooty() + self.gear.winfo_height())

    # ---- settings
    def _read_twitch_auth(self):
        try:
            raw = self.twitch_store.read()
            if raw:
                auth = json.loads(raw)
                if isinstance(auth, dict):
                    return auth
        except Exception:
            pass
        auth = self.cfg.get("twitch_auth")
        return auth if isinstance(auth, dict) else None

    def _save_twitch_auth(self, auth):
        saved = {key: auth[key] for key in
                 ("access_token", "refresh_token", "login", "user_id", "client_id", "scope", "expires_in", "expires_at")
                 if auth.get(key) is not None}
        try:
            if self.twitch_store.write(json.dumps(saved)):
                self.cfg.pop("twitch_auth", None)
                save_config(self.cfg)
                return
        except Exception as exc:
            if sys.platform == "win32":
                messagebox.showwarning(
                    tr("Twitch sign-in"),
                    tr("Twitch is connected for this session, but encrypted sign-in storage failed. "
                    f"You may need to sign in again next time. Details: {exc}"),
                    parent=self.root)
                return
        if sys.platform == "win32":
            messagebox.showwarning(
                tr("Twitch sign-in"),
                tr("Twitch is connected for this session, but encrypted sign-in storage is unavailable. "
                "You may need to sign in again next time."),
                parent=self.root)
            return
        self.cfg["twitch_auth"] = saved
        save_config(self.cfg)

    def ensure_twitch_auth(self, callback, force_login=False):
        self._twitch_auth_waiters.append(callback)
        if self._twitch_auth_busy:
            return
        self._twitch_auth_busy = True
        manager = self.twitch_manager
        if not force_login and manager and not manager.auth_failed:
            active_auth = manager.auth
            if active_auth and active_auth.get("access_token"):
                # The manager already owns a valid session. Reuse it for additional
                # Twitch tabs instead of starting another device-code flow.
                self._finish_twitch_auth(dict(active_auth))
                return

        def validate_saved():
            auth = self._read_twitch_auth()
            if not force_login and auth and auth.get("access_token"):
                try:
                    details = twitch_validate_token(auth["access_token"])
                    scopes = details.get("scopes", [])
                    if (details.get("client_id") == TWITCH_CLIENT_ID
                            and TWITCH_SCOPES in scopes and details.get("login")):
                        auth.update({"login": details["login"], "client_id": TWITCH_CLIENT_ID,
                                     "user_id": details.get("user_id"), "scope": scopes, "expires_in": details.get("expires_in", 14400),
                                     "expires_at": time.time() + int(details.get("expires_in", 14400))})
                        return "ready", auth
                    return "login", None
                except TwitchOAuthError:
                    if auth.get("refresh_token"):
                        try:
                            refreshed = twitch_refresh_token(auth)
                            details = twitch_validate_token(refreshed["access_token"])
                            if (details.get("client_id") == TWITCH_CLIENT_ID
                                    and TWITCH_SCOPES in details.get("scopes", [])
                                    and details.get("login")):
                                refreshed.update({"login": details["login"], "client_id": TWITCH_CLIENT_ID,
                                                  "user_id": details.get("user_id"), "scope": details.get("scopes", []),
                                                  "expires_in": details.get("expires_in", 14400),
                                                  "expires_at": time.time() + int(details.get("expires_in", 14400))})
                                return "ready", refreshed
                        except TwitchOAuthError:
                            return "login", None
                    return "login", None
                except Exception as exc:
                    return "error", str(exc)
            return "login", None

        def worker():
            outcome, value = validate_saved()
            if outcome == "ready":
                self.call_ui(lambda: self._finish_twitch_auth(value))
            elif outcome == "login":
                self.call_ui(self._begin_twitch_device_flow)
            else:
                self.call_ui(lambda: self._finish_twitch_auth(
                    error=f"Could not connect to Twitch: {value}"))

        threading.Thread(target=worker, daemon=True).start()

    def _begin_twitch_device_flow(self):
        current_scopes = set(self.twitch_manager.auth.get("scope", [])) if self.twitch_manager else set()
        requested_scopes = " ".join(sorted(current_scopes | {TWITCH_SCOPES} | self.features.twitch_required))
        if not self.root.winfo_exists():
            self._finish_twitch_auth(error="The app is closing.")
            return
        d = tk.Toplevel(self.root)
        d.title(tr("Sign in to Twitch"))
        d.configure(bg=BG)
        d.transient(self.root)
        d.resizable(False, False)
        tk.Label(d, text=tr("Connect your Twitch account"), bg=BG, fg=FG,
                 font=("Segoe UI", 10, "bold")).pack(anchor="w", padx=16, pady=(14, 5))
        storage = ("Sign-in is stored with Windows Credential Manager or encrypted for this Windows account."
                   if sys.platform == "win32"
                   else f"Sign-in is stored in {CONFIG_PATH}.")
        tk.Label(d, text=(tr("Approve the requested chat permissions. The Twitch app uses Client Type Public; "
                          "this desktop app does not use a client secret. " + storage)),
                 bg=BG, fg=DIM, wraplength=360, justify="left").pack(anchor="w", padx=16, pady=(0, 10))
        status = tk.Label(d, text=tr("Preparing Twitch sign-in…"), bg=BG, fg=FG, anchor="w")
        status.pack(fill="x", padx=16)
        code = tk.Label(d, text="", bg=BG, fg=TAB_BLUE, font=("Segoe UI", 17, "bold"), pady=8)
        code.pack()
        buttons = tk.Frame(d, bg=BG)
        buttons.pack(fill="x", padx=16, pady=(0, 14))
        cancel_event = threading.Event()
        finished = threading.Event()
        flow = {"verification_uri": None}

        def complete(auth=None, error=None):
            if finished.is_set():
                return
            finished.set()
            self.call_ui(lambda: self._finish_twitch_auth(auth, error, d))

        def open_browser():
            uri = flow["verification_uri"]
            if not uri:
                status.config(text=tr("Waiting for Twitch to prepare the sign-in code…"))
                return
            try:
                if sys.platform == "win32":
                    os.startfile(uri)
                elif not webbrowser.open(uri, new=2):
                    raise RuntimeError("No browser accepted the Twitch sign-in page.")
                status.config(text=tr("Twitch sign-in page opened in your browser."))
            except Exception as exc:
                status.config(text=tr(f"Could not open Twitch: {exc}"))

        def copy_code():
            user_code = code.cget("text").strip()
            if not user_code:
                status.config(text=tr("The sign-in code is not ready yet."))
                return
            try:
                d.clipboard_clear()
                d.clipboard_append(user_code)
                d.update()
                status.config(text=tr("Code copied. Enter it on Twitch to continue."))
            except Exception as exc:
                status.config(text=tr(f"Could not copy the code: {exc}"))

        tk.Button(buttons, text=tr("Open Twitch"), command=open_browser, bg=BG, fg=FG, bd=0,
                  highlightthickness=1, highlightbackground=EDGE, padx=10, pady=4).pack(side="left")
        tk.Button(buttons, text=tr("Copy code"), command=copy_code, bg=BG, fg=FG, bd=0,
                  highlightthickness=1, highlightbackground=EDGE, padx=10, pady=4).pack(side="left", padx=6)

        def cancel():
            cancel_event.set()
            complete()

        tk.Button(buttons, text=tr("Cancel"), command=cancel, bg=BG, fg=FG, bd=0,
                  highlightthickness=1, highlightbackground=EDGE, padx=10, pady=4).pack(side="right")
        d.protocol("WM_DELETE_WINDOW", cancel)
        d.bind("<Escape>", lambda _event: cancel())
        d.update_idletasks()
        x = self.root.winfo_rootx() + max(0, (self.root.winfo_width() - d.winfo_reqwidth()) // 2)
        y = self.root.winfo_rooty() + 80
        d.geometry(f"+{x}+{y}")
        d.after(50, lambda: style_titlebar(d))
        d.after(400, lambda: style_titlebar(d))
        d.grab_set()

        def poll_for_authorization():
            try:
                device = twitch_oauth_request("https://id.twitch.tv/oauth2/device", {
                    "client_id": TWITCH_CLIENT_ID, "scopes": requested_scopes})
                flow["verification_uri"] = device["verification_uri"]

                def show_device_code():
                    if finished.is_set() or not d.winfo_exists():
                        return
                    status.config(text=tr(f"Enter this code at {device['verification_uri']}"))
                    code.config(text=tr(device["user_code"]))
                    open_browser()

                self.call_ui(show_device_code)
                interval = max(1, int(device.get("interval", 5)))
                deadline = time.monotonic() + int(device.get("expires_in", 600))
                while not cancel_event.is_set() and time.monotonic() < deadline:
                    if cancel_event.wait(interval):
                        return
                    try:
                        token = twitch_oauth_request("https://id.twitch.tv/oauth2/token", {
                            "client_id": TWITCH_CLIENT_ID,
                            "scopes": requested_scopes,
                            "device_code": device["device_code"],
                            "grant_type": "urn:ietf:params:oauth:grant-type:device_code",
                        })
                        details = twitch_validate_token(token["access_token"])
                        if details.get("client_id") != TWITCH_CLIENT_ID or not set(requested_scopes.split()).issubset(set(details.get("scopes", []))):
                            raise TwitchOAuthError("invalid_scope", "Twitch did not grant the requested chat permissions.")
                        token.update({"login": details["login"], "client_id": TWITCH_CLIENT_ID,
                                      "user_id": details.get("user_id"), "scope": details.get("scopes", []),
                                      "expires_in": details.get("expires_in", token.get("expires_in", 14400)),
                                      "expires_at": time.time() + int(details.get("expires_in", 14400))})
                        complete(token)
                        return
                    except TwitchOAuthError as exc:
                        if exc.error == "authorization_pending":
                            continue
                        if exc.error == "slow_down":
                            interval += 5
                            continue
                        if exc.error in ("access_denied", "expired_token"):
                            raise RuntimeError("Twitch sign-in was declined or expired.")
                        raise
                if not cancel_event.is_set():
                    raise RuntimeError("Twitch sign-in code expired. Try again.")
            except Exception as exc:
                if not cancel_event.is_set():
                    complete(error=str(exc))

        threading.Thread(target=poll_for_authorization, daemon=True).start()

    def _finish_twitch_auth(self, auth=None, error=None, dialog=None):
        if dialog is not None:
            try:
                if dialog.winfo_exists():
                    dialog.grab_release()
                    dialog.destroy()
            except tk.TclError:
                pass
        self._twitch_auth_busy = False
        callbacks, self._twitch_auth_waiters = self._twitch_auth_waiters, []
        if auth:
            self._save_twitch_auth(auth)
            if self.twitch_manager is None:
                persist = lambda updated: self.call_ui(lambda: self._save_twitch_auth(updated))
                self.twitch_manager = TwitchIRCManager(auth, on_auth_updated=persist)
                self.twitch_manager.start()
            elif (self.twitch_manager.auth.get("access_token") != auth.get("access_token")
                  or self.twitch_manager.auth_failed):
                self.twitch_manager.set_auth(auth)
            for callback in callbacks:
                callback(auth)
            self.connect_pending_sources()
        elif error:
            messagebox.showerror(tr("Twitch sign-in"), tr(error), parent=self.root)

    def show_twitch_account(self):
        d = tk.Toplevel(self.root)
        d.title(tr("Twitch account"))
        d.configure(bg=BG)
        d.transient(self.root)
        d.resizable(False, False)
        auth = self._read_twitch_auth()
        if not auth and self.twitch_manager:
            auth = self.twitch_manager.auth
        account = auth.get("login") if auth else None
        tk.Label(d, text=(tr(f"Connected as {account}" if account else "No Twitch account connected")),
                 bg=BG, fg=FG, font=("Segoe UI", 10), padx=16, pady=14).pack(anchor="w")

        def connect():
            try:
                d.destroy()
            except tk.TclError:
                pass
            self.ensure_twitch_auth(lambda saved: messagebox.showinfo(
                tr("Twitch account"), tr(f"Connected as {saved['login']}."), parent=self.root),
                force_login=bool(account))

        buttons = tk.Frame(d, bg=BG)
        buttons.pack(fill="x", padx=14, pady=(0, 12))
        tk.Button(buttons, text=tr("Reauthorize" if account else "Sign in"), command=connect,
                  bg=BG, fg=FG, bd=0, padx=12, pady=4,
                  highlightthickness=1, highlightbackground=EDGE).pack(side="left")
        tk.Button(buttons, text=tr("Close"), command=d.destroy, bg=BG, fg=FG, bd=0,
                  padx=12, pady=4, highlightthickness=1, highlightbackground=EDGE).pack(side="right")
        d.bind("<Escape>", lambda _event: d.destroy())
        d.update_idletasks()
        x = self.root.winfo_rootx() + max(0, (self.root.winfo_width() - d.winfo_reqwidth()) // 2)
        y = self.root.winfo_rooty() + 80
        d.geometry(f"+{x}+{y}")
        d.after(50, lambda: style_titlebar(d))
        d.after(400, lambda: style_titlebar(d))
        d.grab_set()

    def toggle_top(self):
        enabled = self.top_var.get()
        self.cfg["topmost"] = enabled
        self.root.attributes("-topmost", enabled)
        def update_children(parent):
            for child in parent.winfo_children():
                if isinstance(child, tk.Toplevel):
                    child.attributes("-topmost", enabled)
                    if enabled:
                        child.lift()
                update_children(child)
        update_children(self.root)
        save_config(self.cfg)

    def change_font(self, d):
        self.apply_font_sizes(self.fonts["normal"].cget("size") + d,
                              self.fonts["username"].cget("size") + d)

    def reset_font(self):
        self.apply_font_sizes(12, 12)

    def apply_font_sizes(self, message, username, persist=True):
        message, username = font_sizes({"message_font_size":message, "username_font_size":username})
        changed = (message != self.fonts["normal"].cget("size") or username != self.fonts["username"].cget("size"))
        if changed:
            for pane in self.display_panes():
                pane.prepare_font_change()
            self.fonts["normal"].configure(size=message)
            self.fonts["bold"].configure(size=message)
            self.fonts["username"].configure(size=username)
            self.fonts["emoji"].configure(size=message + 5)
            for pane in self.display_panes():
                pane.finish_font_change()
        self.appearance.update(font_size=message, message_font_size=message, username_font_size=username)
        self.cfg["appearance"] = self.appearance
        if persist:
            save_config(self.cfg)
        ui = getattr(self.features, "settings_ui", None)
        page = ui.pages.get("Display") if ui else None
        if page is not None and page.winfo_exists() and hasattr(page.inner, "refresh_settings"):
            page.inner.refresh_settings()

    def show_appearance(self):
        self.features.show_settings("Appearance")

    def show_shortcuts(self):
        self.features.show_settings("Keyboard shortcuts")

    # ---- tabs
    def _refresh_empty(self):
        self.update_quota()
        if self.tabs:
            self.empty.place_forget()
        else:
            self.empty.place(relx=0.5, rely=0.4, anchor="center")

    def _make_tab_button(self, workspace):
        frame = tk.Frame(self.tabs_frame, bg=BG, cursor="hand2")
        frame.pack(side="left")
        accent = tk.Frame(frame, height=2, bg=BG)
        accent.pack(side="top", fill="x")
        inner = tk.Frame(frame, bg=BG)
        inner.pack(side="top")
        label = tk.Label(inner, bg=BG, fg=TAB_BLUE, font=("Segoe UI", 9), padx=8, pady=3)
        label.pack(side="left")
        close = tk.Label(inner, text=tr("×"), bg=BG, fg=DIM, font=("Segoe UI", 10), padx=4, cursor="hand2")
        for widget in (frame, accent, inner, label):
            widget.bind("<Button-1>", lambda event, tab=workspace: self.select(tab))
            widget.bind("<Button-2>", lambda event, tab=workspace: self.close_tab(tab))
            widget.bind("<Button-3>", lambda event, tab=workspace: self._tab_menu(event, tab))
        close.bind("<Button-1>", lambda event, tab=workspace: self.close_tab(tab))
        workspace.btn = {"frame": frame, "accent": accent, "inner": inner, "lbl": label, "x": close}
        self.features.bind_tab_drag(workspace, (frame, accent, inner, label))
        self.refresh_workspace_title(workspace)
        self._style_tab(workspace, False)

    def _style_tab(self, tab, selected):
        if not tab.btn or tab.closed:
            return
        buttons = tab.btn
        bg = "#161616" if selected else BG
        for key in ("frame", "inner", "lbl", "x"):
            buttons[key].config(bg=bg)
        buttons["accent"].config(bg=ACCENT if selected else BG)
        buttons["lbl"].config(fg=FG if selected else ("#ffffff" if tab.unread else TAB_BLUE))
        if selected:
            buttons["x"].pack(side="left")
        else:
            buttons["x"].pack_forget()

    def select(self, target):
        if isinstance(target, ChatTab):
            workspace, pane = target.workspace, target
        else:
            workspace, pane = target, target.focused
        if workspace not in self.tabs or workspace.closed:
            return
        previous = self.current_workspace
        if previous is not workspace:
            if previous:
                previous.pack_forget()
                self._style_tab(previous, False)
            self.current_workspace = workspace
            workspace.pack(fill="both", expand=True)
        if workspace.zoomed and workspace.zoomed is not pane:
            workspace.zoomed = None
            workspace.rebuild()
        workspace.unread = False
        self._style_tab(workspace, True)
        self.focus_pane(pane or workspace.panes[0], focus_text=False)
        self.sync_visibility()
        self._scroll_selected_tab()
        self.schedule_session_save()

    def open_stream(self, vid):
        self.open_specs([{"platform": "youtube", "id": vid}])

    def open_twitch(self, login):
        self.open_specs([{"platform": "twitch", "id": login.lower()}])

    def _open_twitch_authorized(self, login, auth):
        self.open_twitch(login)

    def select_relative(self, direction):
        if self.tabs:
            index = self.tabs.index(self.current_workspace) if self.current_workspace in self.tabs else 0
            self.select(self.tabs[(index + direction) % len(self.tabs)])

    def select_numbered(self, index, last=False):
        if self.tabs:
            self.select(self.tabs[-1] if last else self.tabs[min(index, len(self.tabs) - 1)])

    def toggle_all_chats(self):
        sources = [source for source in self.sources.values() if source.platform == "youtube"]
        pause = not all(source.manual_paused for source in sources)
        for source in sources:
            source.manual_paused = pause
            source.sync_pause()
        self.schedule_session_save()

    def reconnect_current(self):
        if self.current is not None:
            self.reconnect_tab(self.current)

    def reconnect_tab(self, pane):
        if pane is None or pane.closed:
            return
        for source in pane.sources:
            source.reconnect()
        if any(source.platform == "twitch" for source in pane.sources) and (
                self.twitch_manager is None or self.twitch_manager.auth_failed):
            self.ensure_twitch_auth(self.connect_pending_sources)

    def quick_switcher(self):
        if not self.tabs:
            return
        dialog = tk.Toplevel(self.root)
        dialog.title(tr("Switch chat"))
        dialog.configure(bg=BG)
        dialog.transient(self.root)
        entry = tk.Entry(dialog, bg=BG, fg=FG, insertbackground=FG, width=42)
        entry.pack(fill="x", padx=10, pady=10)
        listing = tk.Listbox(dialog, bg=BG, fg=FG, selectbackground="#303030", height=10,
                             width=55, exportselection=False, highlightthickness=0, bd=0)
        listing.pack(fill="both", expand=True, padx=10, pady=(0, 10))
        choices = []
        def refresh(event=None):
            choices.clear()
            listing.delete(0, "end")
            term = entry.get().casefold()
            for workspace in self.tabs:
                for pane in workspace.panes:
                    label = workspace.label + " · " + pane.display_name
                    if term in label.casefold():
                        choices.append(pane)
                        listing.insert("end", label[:110])
            if choices:
                listing.selection_set(0)
        def choose(event=None):
            selected = listing.curselection()
            if selected and selected[0] < len(choices) and not choices[selected[0]].closed:
                self.select(choices[selected[0]])
                dialog.destroy()
        entry.bind("<KeyRelease>", refresh)
        entry.bind("<Return>", choose)
        listing.bind("<Double-Button-1>", choose)
        dialog.bind("<Escape>", lambda event: dialog.destroy())
        refresh()
        entry.focus_set()

    def show_search(self):
        tab = self.current
        if tab is None:
            return
        existing = getattr(self, "_search_dialog", None)
        if existing is not None and existing.winfo_exists():
            if getattr(self, "_search_tab", None) is tab:
                existing.lift()
                self._search_entry.focus_set()
                self._search_entry.select_range(0, "end")
                return
            existing.destroy()
        d = tk.Toplevel(self.root)
        self._search_dialog = d
        self._search_tab = tab
        d.title(tr("Find in chat"))
        d.configure(bg=BG)
        d.transient(self.root)
        top = tk.Frame(d, bg=BG)
        top.pack(fill="x", padx=10, pady=10)
        entry = tk.Entry(top, bg=BG, fg=FG, insertbackground=FG, width=32)
        entry.pack(side="left", fill="x", expand=True)
        self._search_entry = entry
        count = tk.Label(d, text=tr("Type to search this chat"), bg=BG, fg=DIM, anchor="w")
        count.pack(fill="x", padx=10, pady=(0, 6))

        search_timer = [None]

        def update(_=None):
            search_timer[0] = None
            if tab.closed or not d.winfo_exists():
                return
            n = tab.set_search(entry.get())
            count.config(text=tr(f"{n} matches" if entry.get() else "Type to search this chat"))

        def step(direction):
            pos = tab.step_search(direction)
            count.config(text=tr(f"{pos} of {len(tab.search_matches)} matches"
                         if pos else "No matches"))

        tk.Button(top, text=tr("↑"), command=lambda: step(-1), bg=BG, fg=FG, bd=0).pack(side="left")
        tk.Button(top, text=tr("↓"), command=lambda: step(1), bg=BG, fg=FG, bd=0).pack(side="left")
        def schedule(event=None):
            if search_timer[0] is not None:
                d.after_cancel(search_timer[0])
            search_timer[0] = d.after(140, update)

        entry.bind("<KeyRelease>", schedule)
        entry.bind("<Return>", lambda e: step(1))
        entry.bind("<Shift-Return>", lambda e: step(-1))
        d.bind("<Escape>", lambda e: d.destroy())
        entry.insert(0, tab.search_query)
        update()
        entry.focus_set()

    def show_global_search(self):
        if not self.sources:
            return
        dialog = tk.Toplevel(self.root)
        dialog.title(tr("Search all chats"))
        dialog.configure(bg=BG)
        dialog.transient(self.root)
        entry = tk.Entry(dialog, bg=BG, fg=FG, insertbackground=FG, width=54)
        entry.pack(fill="x", padx=10, pady=10)
        listing = tk.Listbox(dialog, bg=BG, fg=FG, selectbackground="#303030", width=90,
                             height=14, highlightthickness=0, bd=0, exportselection=False)
        listing.pack(fill="both", expand=True, padx=10, pady=(0, 10))
        hits = []
        timer = [None]
        def refresh():
            timer[0] = None
            if not dialog.winfo_exists():
                return
            term = entry.get().casefold().strip()
            hits.clear()
            listing.delete(0, "end")
            if not term:
                return
            events = [event for source in self.sources.values() for event in source.history
                      if event[1] == "msg" and term in (event[2]["name"] + " " + event[2]["text"]).casefold()]
            for event in sorted(events, key=lambda value: value[0], reverse=True)[:300]:
                sequence, _, message, source = event
                hits.append(event)
                listing.insert("end", f"[{source.name}] {message['time']} {message['name']}: {message['text']}"[:180])
            if hits:
                listing.selection_set(0)
            else:
                listing.insert("end", "No matches in recent messages")
        def schedule(event=None):
            if timer[0] is not None:
                dialog.after_cancel(timer[0])
            timer[0] = dialog.after(150, refresh)
        def choose(event=None):
            selected = listing.curselection()
            if not selected or selected[0] >= len(hits):
                return
            sequence, _, message, source = hits[selected[0]]
            pane = next((view for view in self.all_panes() if source in view.sources and view.accepts(message)), None)
            if pane is None:
                pane = self.new_workspace([source]).focused
                pane.set_filters({})
            self.select(pane)
            term = entry.get()
            def reveal(attempt=0):
                if pane.closed:
                    return
                ranges = pane.text.tag_ranges("message_" + str(sequence))
                if not ranges and pane.pending_events and attempt < 20:
                    self.root.after(70, lambda: reveal(attempt + 1))
                    return
                pane.set_search(term, reveal=False)
                if ranges:
                    pane.follow_tail = False
                    pane.text.see(ranges[0])
            self.root.after(80, reveal)
            dialog.destroy()
        entry.bind("<KeyRelease>", schedule)
        entry.bind("<Return>", choose)
        listing.bind("<Double-Button-1>", choose)
        dialog.bind("<Escape>", lambda event: dialog.destroy())
        entry.focus_set()

    def show_filters(self, tab):
        current = tab.filters
        d = tk.Toplevel(self.root)
        d.title(tr("Chat filters"))
        d.configure(bg=BG)
        d.transient(self.root)
        tk.Label(d, text=tr("Filters apply to new messages as they arrive."), bg=BG, fg=DIM,
                 anchor="w").grid(row=0, column=0, sticky="w", padx=12, pady=(10, 2))
        fields = [
            ("Hide users (comma or line separated)", "hidden_users"),
            ("Only show messages containing any of these terms (optional)", "include_terms"),
            ("Hide messages containing any of these terms (optional)", "exclude_terms"),
        ]
        boxes = {}
        for row, (label, key) in enumerate(fields):
            tk.Label(d, text=tr(label), bg=BG, fg=FG, anchor="w").grid(
                row=row * 2 + 1, column=0, sticky="w", padx=12, pady=(8, 2))
            box = tk.Text(d, width=42, height=2, bg=BG, fg=FG, insertbackground=FG,
                          highlightthickness=1, highlightbackground=EDGE, bd=0)
            box.grid(row=row * 2 + 2, column=0, sticky="ew", padx=12)
            box.insert("1.0", ", ".join(current.get(key, [])))
            boxes[key] = box

        def save():
            filters = {}
            for key, box in boxes.items():
                raw = box.get("1.0", "end-1c")
                filters[key] = [part.strip() for part in re.split(r"[,\n]+", raw) if part.strip()]
            if tab.closed:
                d.destroy()
                return
            for pane in self.all_panes():
                if pane.setting_key == tab.setting_key:
                    pane.set_filters(filters)
            self.cfg.setdefault("chat_filters", {})[tab.setting_key] = filters
            save_config(self.cfg)
            d.destroy()

        tk.Button(d, text=tr("Save"), command=save, bg=BG, fg=FG, bd=0,
                  highlightthickness=1, highlightbackground=EDGE, padx=14, pady=5).grid(
                      row=7, column=0, sticky="e", padx=12, pady=12)

    @staticmethod
    def parse_channel_reference(text):
        raw = text.strip()
        if re.fullmatch(r"UC[\w-]{20,30}", raw):
            return "id", raw
        if "://" not in raw:
            candidate = "https://" + raw if re.match(r"(?:www\.|m\.)?youtube\.com/", raw, re.I) \
                else "https://www.youtube.com/" + raw.lstrip("/")
        else:
            candidate = raw
        parsed = urllib.parse.urlparse(candidate)
        parts = [urllib.parse.unquote(x) for x in parsed.path.strip("/").split("/") if x]
        for i, part in enumerate(parts):
            if part == "channel" and i + 1 < len(parts):
                return "id", parts[i + 1]
            if part == "user" and i + 1 < len(parts):
                return "username", parts[i + 1]
            if part.startswith("@"):
                return "handle", part[1:]
        if raw.startswith("@"):
            return "handle", raw[1:]
        return None, None

    def show_saved_channels(self):
        self.features.show_settings("Saved channels")

    def close_tab(self, target):
        workspace = target.workspace if isinstance(target, ChatTab) else target
        if workspace not in self.tabs:
            return
        index = self.tabs.index(workspace)
        workspace.close()
        self.tabs.remove(workspace)
        workspace.btn["frame"].destroy()
        workspace.destroy()
        if workspace is self.current_workspace:
            self.current_workspace = None
            self.current = None
            if self.tabs:
                self.select(self.tabs[min(index, len(self.tabs) - 1)])
        self.prune_sources()
        self._refresh_empty()
        self.schedule_session_save()

    def close_current(self):
        if self.current:
            self.close_pane(self.current)

    def _tab_menu(self, event, workspace):
        self.select(workspace)
        menu = self._menu(self.root)
        self.features.tab_browser_menu(workspace, menu)
        menu.add_command(label=tr("Add split…"), command=lambda: self.add_stream_dialog("horizontal", workspace.focused))
        menu.add_command(label=tr("Mix channels…"), command=lambda: self.add_stream_dialog("mix", workspace.focused))
        menu.add_command(label=tr("Rename tab…"), command=lambda: self.rename_tab(workspace))
        menu.add_command(label=tr("Move tab left"), command=lambda: self.move_tab(workspace, -1))
        menu.add_command(label=tr("Move tab right"), command=lambda: self.move_tab(workspace, 1))
        menu.add_separator()
        menu.add_command(label=tr("Close tab  Ctrl+Shift+W"), command=lambda: self.close_tab(workspace))
        menu.tk_popup(event.x_root, event.y_root)

    def _save_chat_interval(self, source, seconds):
        seconds = max(WEB_POLL_SECONDS, float(seconds))
        self.cfg.setdefault("chat_settings", {}).setdefault(source.key, {})["web_poll_interval_seconds"] = seconds
        save_config(self.cfg)
        if source.worker:
            source.worker.set_poll_interval(seconds)
            source.publish("status", f"Request interval: {seconds:g}s · 0 reading units")

    def set_chat_interval(self, source):
        current = source.worker.settings()[1] if source.worker else WEB_POLL_SECONDS
        seconds = simpledialog.askfloat(tr("Chat request interval"), tr("Seconds between requests for " + source.name),
                                        initialvalue=round(current, 2), minvalue=WEB_POLL_SECONDS, parent=self.root)
        if seconds is not None:
            self._save_chat_interval(source, seconds)

    def toggle_chat_pause(self, source):
        source.manual_paused = not source.manual_paused
        source.sync_pause()
        self.schedule_session_save()

    def _wheel_tabs(self, event):
        x, y = event.x_root, event.y_root
        left, top = self.tabs_canvas.winfo_rootx(), self.tabs_canvas.winfo_rooty()
        right = left + self.tabs_canvas.winfo_width()
        bottom = top + self.tabs_canvas.winfo_height()
        if not (left <= x < right and top <= y < bottom):
            return
        if getattr(event, "num", None) == 4:
            steps = 1
        elif getattr(event, "num", None) == 5:
            steps = -1
        else:
            delta = getattr(event, "delta", 0)
            if not delta:
                return
            steps = max(1, abs(delta) // 120) * (1 if delta > 0 else -1)
        direction = -1 if steps > 0 else 1
        for _ in range(abs(steps)):
            self.select_relative(direction)
        return "break"

    # ---- main loop
    def pump(self):
        if self._closing:
            return
        for _ in range(24):
            try:
                callback = self.ui_queue.get_nowait()
            except queue.Empty:
                break
            try:
                callback()
            except tk.TclError:
                pass
            except Exception:
                self._log_exc(*sys.exc_info())
        now = time.monotonic()
        if now >= self.next_quota_update:
            self.update_quota()
            self.next_quota_update = now + 1
        deadline = now + 0.018
        sources = list(self.sources.values())
        if sources:
            start = self._source_cursor % len(sources)
            sources = sources[start:] + sources[:start]
            self._source_cursor += 1
        for source in sources:
            for _ in range(60):
                if time.monotonic() >= deadline:
                    break
                try:
                    kind, value = source.q.get_nowait()
                except queue.Empty:
                    break
                try:
                    source.handle_event(kind, value)
                except Exception:
                    self._log_exc(*sys.exc_info())
        panes = [pane for pane in self.display_panes() if pane.active]
        if panes:
            start = self._pane_cursor % len(panes)
            panes = panes[start:] + panes[:start]
            self._pane_cursor += 1
        deadline = time.monotonic() + 0.018
        for pane in panes:
            try:
                pane.drain(deadline)
                if not pane.resizing and time.monotonic() < deadline:
                    for _ in range(2):
                        try:
                            kind, value = pane.q.get_nowait()
                        except queue.Empty:
                            break
                        if kind == "emote_image":
                            pane.image_ready(*value)
                        elif kind == "badge_image":
                            pane.badge_image_ready(*value)
            except Exception:
                self._log_exc(*sys.exc_info())
        busy = any(not source.q.empty() for source in sources) or any(pane.pending_events for pane in panes)
        self.pump_after_id = self.root.after(20 if busy else 60, self.pump)

    def quit(self):
        if self._closing:
            return
        self.save_session()
        self.features.close()
        self.updater.close()
        self._closing = True
        for timer in (self.session_after_id, self.pump_after_id):
            if timer is not None:
                self.root.after_cancel(timer)
        for workspace in self.tabs:
            workspace.close()
        for source in self.sources.values():
            source.close()
        self.photo_bank.close()
        if self.twitch_manager:
            self.twitch_manager.shutdown()
        CATALOG_POOL.shutdown(wait=False, cancel_futures=True)
        IMAGE_POOL.shutdown(wait=False, cancel_futures=True)
        QUOTA.save()
        # Tk can retain delayed widget callbacks after their Tcl commands are
        # destroyed. Cancel the remaining events before tearing down the UI.
        for timer in self.root.tk.call("after", "info"):
            self.root.tk.call("after", "cancel", timer)
        self.root.destroy()

    def all_panes(self):
        return [pane for workspace in self.tabs for pane in workspace.panes]

    def display_panes(self):
        return self.all_panes() + [pane for pane in self.extra_panes if not pane.closed]

    def call_ui(self, callback):
        if not self._closing:
            self.ui_queue.put(callback)

    def source_for(self, spec):
        key = ("twitch:" + spec["id"]) if spec["platform"] == "twitch" else spec["id"]
        if key not in self.sources:
            self.sources[key] = ChatSource(self, spec)
        return self.sources[key]

    def connect_pending_sources(self, auth=None):
        for source in list(self.sources.values()):
            source.ensure_worker()

    def prune_sources(self):
        for key, source in list(self.sources.items()):
            if not source.views:
                source.close()
                self.sources.pop(key, None)

    def schedule_session_save(self, event=None):
        if event is not None and event.widget is not self.root:
            return
        if self._restoring or self._closing:
            return
        if self.session_after_id is not None:
            self.root.after_cancel(self.session_after_id)
        self.session_after_id = self.root.after(650, self.save_session)

    def save_session(self):
        if self.session_after_id is not None:
            self.root.after_cancel(self.session_after_id)
        self.session_after_id = None
        if self._restoring:
            return
        window = self.window_state.snapshot()
        self.cfg["session"] = {
            "version": 1,
            "sources": [source.spec() for source in self.sources.values() if source.views],
            "tabs": [workspace.serialize() for workspace in self.tabs if workspace.tree],
            "active": self.current_workspace.uid if self.current_workspace else None,
            "geometry": window["geometry"],
            "window": window,
        }
        save_config(self.cfg)

    def restore_session(self):
        session = self.cfg.get("session", {})
        if not isinstance(session, dict) or session.get("version") != 1:
            self._restoring = False
            return
        source_map = {}
        saved_sources = session.get("sources", [])
        for spec in (saved_sources if isinstance(saved_sources, list) else [])[:128]:
            if not isinstance(spec, dict):
                continue
            platform, value = spec.get("platform"), spec.get("id", "")
            if not isinstance(value, str):
                continue
            valid = ((platform == "twitch" and re.fullmatch(r"[a-zA-Z0-9_]{1,25}", value))
                     or (platform == "youtube" and re.fullmatch(r"[\w-]{11}", value)))
            if valid:
                if platform == "twitch":
                    spec = dict(spec, id=value.lower())
                source = self.source_for(spec)
                source_map[source.key] = source
        saved_tabs = session.get("tabs", [])
        for saved in (saved_tabs if isinstance(saved_tabs, list) else [])[:48]:
            if not isinstance(saved, dict):
                continue
            workspace = ChatWorkspace(self, str(saved.get("name", ""))[:80],
                                       str(saved.get("id") or uuid.uuid4().hex))
            count = [0]
            def decode(node, depth=0):
                if not isinstance(node, dict) or depth > 32 or count[0] >= 24:
                    return None
                if "sources" in node:
                    if not isinstance(node["sources"], list):
                        return None
                    selected = [source_map[key] for key in node["sources"][:32] if key in source_map]
                    if not selected:
                        return None
                    count[0] += 1
                    pane = workspace.new_pane(selected, pane_id=str(node.get("pane_id") or uuid.uuid4().hex))
                    return {"pane": pane}
                values = node.get("children", [])
                if not isinstance(values, list):
                    return None
                children = [child for child in (decode(value, depth + 1) for value in values[:2]) if child]
                if not children:
                    return None
                if len(children) == 1:
                    return children[0]
                try:
                    ratio = max(0.08, min(0.92, float(node.get("ratio", 0.5))))
                except (TypeError, ValueError):
                    ratio = 0.5
                return {"orient": "vertical" if node.get("orient") == "vertical" else "horizontal",
                        "ratio": ratio, "children": children}
            workspace.tree = decode(saved.get("layout"))
            if not workspace.tree:
                workspace.close()
                workspace.destroy()
                continue
            self.tabs.append(workspace)
            self._make_tab_button(workspace)
            workspace.focused = next((pane for pane in workspace.panes if pane.uid == saved.get("focused")),
                                     workspace.panes[0])
            workspace.rebuild(capture=False)
        self._restoring = False
        self.prune_sources()
        active = next((workspace for workspace in self.tabs if workspace.uid == session.get("active")), None)
        if self.tabs:
            self.select(active or self.tabs[0])
        self._refresh_empty()

    def refresh_workspace_title(self, workspace):
        if workspace.btn:
            label = workspace.label
            workspace.btn["lbl"].config(text=tr(label[:23] + ("…" if len(label) > 23 else "")))

    def focus_pane(self, pane, focus_text=False):
        if pane is None or pane.closed:
            return
        if getattr(pane, "detached", False):
            if focus_text:
                pane.text.focus_set()
            return
        if pane.workspace is not self.current_workspace:
            self.select(pane)
            return
        self.current = pane
        self.update_quota()
        pane.workspace.focused = pane
        for other in pane.workspace.panes:
            other.configure(highlightbackground=ACCENT if other is pane else HEADER_BG)
        if focus_text:
            pane.text.focus_set()
        self.schedule_session_save()

    def sync_visibility(self, event=None):
        if event is not None and event.widget is not self.root:
            return
        visible = self.root.state() not in ("iconic", "withdrawn") and not self._closing
        for workspace in self.tabs:
            for pane in workspace.panes:
                active = visible and workspace is self.current_workspace and (
                    workspace.zoomed is None or workspace.zoomed is pane)
                if pane.active != active:
                    pane.set_active(active)

    def zoom_pane(self, pane):
        workspace = pane.workspace
        workspace.capture_sizes()
        workspace.zoomed = None if workspace.zoomed is pane else pane
        self.focus_pane(pane)
        workspace.rebuild(capture=False)

    def focus_direction(self, direction):
        pane = self.current
        if pane is None or len(pane.workspace.panes) < 2:
            return
        workspace = pane.workspace
        if workspace.zoomed:
            workspace.zoomed = None
            workspace.rebuild()
            self.root.update_idletasks()
        x = pane.winfo_x() + pane.winfo_width() / 2
        y = pane.winfo_y() + pane.winfo_height() / 2
        choices = []
        for other in workspace.panes:
            if other is pane:
                continue
            dx = other.winfo_x() + other.winfo_width() / 2 - x
            dy = other.winfo_y() + other.winfo_height() / 2 - y
            primary, secondary = ((dx, dy) if direction in ("left", "right") else (dy, dx))
            if (primary < 0) == (direction in ("left", "up")) and primary != 0:
                choices.append((abs(primary) + 2 * abs(secondary), other))
        if choices:
            self.focus_pane(min(choices, key=lambda item: item[0])[1], focus_text=True)

    @staticmethod
    def chat_spec(text):
        login = twitch_login_from(text)
        if login:
            return {"platform": "twitch", "id": login}
        video = video_id_from(text)
        if video and re.fullmatch(r"[\w-]{11}", video):
            return {"platform": "youtube", "id": video}
        return None

    def new_workspace(self, sources, name=""):
        if len(self.tabs) >= 48:
            raise ValueError("Close a tab before adding more than 48 tabs.")
        workspace = ChatWorkspace(self, name)
        self.tabs.append(workspace)
        pane = workspace.new_pane(sources)
        self._make_tab_button(workspace)
        workspace.rebuild(capture=False)
        self.select(pane)
        self._refresh_empty()
        return workspace

    def open_specs(self, specs, mode="tabs", anchor=None):
        if mode in ("horizontal", "vertical") and anchor and len(anchor.workspace.panes) + len(specs) > 24:
            messagebox.showinfo(tr("Split chats"), tr("A tab can contain up to 24 panes. Open another tab for more chats."), parent=self.root)
            return
        keys = {("twitch:" + spec["id"]) if spec["platform"] == "twitch" else spec["id"] for spec in specs}
        if len(set(self.sources).union(keys)) > 128:
            messagebox.showinfo(tr("Add chats"), tr("Close unused channels before opening more than 128 channels."), parent=self.root)
            return
        if mode == "tabs" and len(self.tabs) + len(specs) > 48:
            messagebox.showinfo(tr("Add chats"), tr("Close unused tabs before opening more than 48 tabs."), parent=self.root)
            return
        selected = [self.source_for(spec) for spec in specs]
        all_selected = list(selected)
        if mode == "tabs":
            for source in selected:
                existing = next((pane for pane in self.all_panes() if pane.sources == [source]), None)
                if existing:
                    self.select(existing)
                else:
                    self.new_workspace([source])
        elif mode == "mix":
            if anchor and not anchor.closed:
                anchor.set_sources(anchor.sources + selected)
                self.select(anchor)
            else:
                self.new_workspace(selected)
        else:
            if anchor is None or anchor.closed:
                workspace = self.new_workspace([selected[0]])
                anchor = workspace.focused
                selected = selected[1:]
            for source in selected:
                anchor = anchor.workspace.split(anchor, [source], mode)
            anchor.workspace.balance()
            self.select(anchor)
            if self.root.winfo_width() < 680 and len(anchor.workspace.panes) > 1:
                self.root.geometry(f"{min(960, self.root.winfo_screenwidth())}x{self.root.winfo_height()}")
        if any(source.platform == "twitch" and source.worker is None for source in all_selected) and self.twitch_manager is None:
            self.ensure_twitch_auth(self.connect_pending_sources)
        self.schedule_session_save()

    def close_pane(self, pane):
        if pane.closed:
            return
        workspace = pane.workspace
        if len(workspace.panes) == 1:
            self.close_tab(workspace)
            return
        workspace.remove_pane(pane)
        self.prune_sources()
        self.select(workspace.focused)
        self.refresh_workspace_title(workspace)
        self.schedule_session_save()

    def rename_tab(self, workspace):
        name = simpledialog.askstring(tr("Rename tab"), tr("Tab name (leave blank to use channel names)"),
                                      initialvalue=workspace.custom_name, parent=self.root)
        if name is not None:
            workspace.custom_name = name.strip()[:80]
            self.refresh_workspace_title(workspace)
            self.schedule_session_save()

    def move_tab(self, workspace, direction):
        index = self.tabs.index(workspace)
        target = max(0, min(len(self.tabs) - 1, index + direction))
        self.tabs.insert(target, self.tabs.pop(index))
        for tab in self.tabs:
            tab.btn["frame"].pack_forget()
            tab.btn["frame"].pack(side="left")
        self.schedule_session_save()

    def pane_menu(self, event, pane):
        self.focus_pane(pane)
        menu = self._menu(self.root)
        menu.add_command(label=tr("Split right…"), command=lambda: self.add_stream_dialog("horizontal", pane))
        menu.add_command(label=tr("Split below…"), command=lambda: self.add_stream_dialog("vertical", pane))
        menu.add_command(label=tr("Mix channels…"), command=lambda: self.add_stream_dialog("mix", pane))
        if len(pane.sources) > 1:
            remove = self._menu(menu)
            for source in pane.sources:
                remove.add_command(label=source.name, command=lambda item=source: pane.set_sources(
                    [other for other in pane.sources if other is not item]))
            menu.add_cascade(label=tr("Remove channel from mix"), menu=remove)
        menu.add_command(label=tr("Duplicate beside this pane"), command=lambda: self.duplicate_pane(pane))
        if len(pane.workspace.panes) > 1:
            menu.add_command(label=tr("Restore all panes" if pane.workspace.zoomed else "Focus this pane"),
                             command=lambda: self.zoom_pane(pane))
            menu.add_command(label=tr("Move pane to a new tab"), command=lambda: self.detach_pane(pane))
            menu.add_command(label=tr("Balance pane sizes"), command=pane.workspace.balance)
        menu.add_separator()
        for source in pane.sources:
            if source.platform == "youtube":
                settings = self._menu(menu)
                worker = source.worker
                if worker:
                    _, interval = worker.settings()
                    description = f"{interval:g}s minimum · 0 API units/h"
                    settings.add_command(label=tr(description), state="disabled")
                settings.add_command(label=tr("Request interval…"), command=lambda item=source: self.set_chat_interval(item))
                settings.add_command(label=tr("Resume" if source.manual_paused else "Pause"),
                                     command=lambda item=source: self.toggle_chat_pause(item))
                menu.add_cascade(label=source.name, menu=settings)
        self.features.pane_menu(pane, menu)
        menu.add_command(label=tr("Reconnect  Ctrl+F5"), command=lambda: self.reconnect_tab(pane))
        menu.add_command(label=tr("Chat filters…"), command=lambda: self.show_filters(pane))
        menu.add_command(label=tr("Reload recent messages"), command=pane.load_recent)
        menu.add_separator()
        menu.add_command(label=tr("Close pane  Ctrl+W"), command=lambda: self.close_pane(pane))
        menu.tk_popup(event.x_root, event.y_root)

    def duplicate_pane(self, pane):
        new_pane = pane.workspace.split(pane, list(pane.sources), "horizontal")
        self.select(new_pane)
        self.schedule_session_save()

    def detach_pane(self, pane):
        sources = list(pane.sources)
        workspace = self.new_workspace(sources)
        self.close_pane(pane)
        self.select(workspace)

    def layout_menu(self, event=None):
        menu = self._menu(self.root)
        pane = self.current
        menu.add_command(label=tr("Add chats in a split…"), command=lambda: self.add_stream_dialog("horizontal", pane))
        menu.add_command(label=tr("Add stacked chats…"), command=lambda: self.add_stream_dialog("vertical", pane))
        menu.add_command(label=tr("Mix channels in one feed…"), command=lambda: self.add_stream_dialog("mix", pane))
        if pane:
            menu.add_separator()
            menu.add_command(label=tr("Duplicate current pane"), command=lambda: self.duplicate_pane(pane))
            menu.add_command(label=tr("Rename tab…"), command=lambda: self.rename_tab(pane.workspace))
        menu.tk_popup(self.layout_button.winfo_rootx(), self.layout_button.winfo_rooty() + self.layout_button.winfo_height())

    def _scroll_selected_tab(self):
        if not self.current_workspace or not self.current_workspace.btn:
            return
        self.root.update_idletasks()
        frame = self.current_workspace.btn["frame"]
        total = max(1, self.tabs_frame.winfo_reqwidth())
        left = self.tabs_canvas.canvasx(0)
        width = self.tabs_canvas.winfo_width()
        if frame.winfo_x() < left:
            self.tabs_canvas.xview_moveto(frame.winfo_x() / total)
        elif frame.winfo_x() + frame.winfo_width() > left + width:
            self.tabs_canvas.xview_moveto((frame.winfo_x() + frame.winfo_width() - width) / total)

    def bind_shortcut(self, sequence, action):
        def handle(event):
            if event.widget.winfo_toplevel() is not self.root:
                return
            action(event)
            return "break"
        self.root.bind_all(sequence, handle)


if __name__ == "__main__":
    if sys.platform == "win32":
        try:
            set_app_id = ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID
            set_app_id.argtypes = [ctypes.c_wchar_p]
            set_app_id.restype = ctypes.c_long
            set_app_id(APP_USER_MODEL_ID)
        except Exception:
            pass
    root = tk.Tk(className="Ytchatterino")
    if os.path.isfile(APP_ICON_PATH):
        try:
            root.iconbitmap(APP_ICON_PATH)
        except tk.TclError:
            pass
    App(root)
    root.mainloop()
