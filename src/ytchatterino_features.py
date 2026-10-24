"""Accounts, chat actions, highlights, alerts and settings for YT-Chatterino.

The viewer owns channel connections and rendering. This module owns authenticated
actions and their UI; networking always runs outside the Tk event loop.
"""
import base64
import hashlib
import hmac
import http.server
import json
import os
from pathlib import Path
import queue
import re
import secrets
import threading
import time
import tkinter as tk
from tkinter import colorchooser, filedialog, messagebox, simpledialog, ttk
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from ytchatterino_youtube import YouTubeWebClient
from ytchatterino_chat import ChatTools
from ytchatterino_settings import SettingsWindow
import shutil
import subprocess
import sys
from ytchatterino_i18n import tr, set_language, refresh_ui, number, language, set_datetime_formats, DATE_FORMATS, TIME_FORMATS
from concurrent.futures import ThreadPoolExecutor

GOOGLE_SCOPE = "https://www.googleapis.com/auth/youtube.force-ssl"
TWITCH_ACTION_SCOPES = (
    "user:write:chat", "moderator:manage:chat_messages",
    "moderator:manage:banned_users", "user:read:moderated_channels",
    "channel:manage:broadcast",
)
ROLE_LABELS = {"owner": "Owners", "moderator": "Moderators", "vip": "VIPs",
               "subscriber": "Subscribers / members", "founder": "Founders"}


BROWSER_ACTIONS = (("stream", "Open stream in browser"),
                   ("player", "Open player in browser"),
                   ("chat", "Open chat in browser"))


def channel_browser_url(source, destination):
    """Build public channel URLs without credentials, drafts or API calls."""
    if destination not in {kind for kind, _ in BROWSER_ACTIONS}:
        raise ValueError("Unknown browser destination.")
    if source.platform == "twitch":
        login = str(source.channel_login or "").lower()
        if not re.fullmatch(r"[a-z0-9_]{1,25}", login):
            raise ValueError("This Twitch channel has an invalid login.")
        return {
            "stream": "https://www.twitch.tv/" + login,
            "player": "https://player.twitch.tv/?channel=" + login + "&parent=twitch.tv",
            "chat": "https://www.twitch.tv/popout/" + login + "/chat?popout=&darkpopout",
        }[destination]
    if source.platform == "youtube":
        video = str(source.video_id)
        if not re.fullmatch(r"[a-zA-Z0-9_-]{11}", video):
            raise ValueError("This YouTube broadcast has an invalid video ID.")
        return {
            "stream": "https://www.youtube.com/watch?v=" + video,
            "player": "https://www.youtube.com/embed/" + video + "?autoplay=1",
            "chat": "https://www.youtube.com/live_chat?v=" + video + "&is_popout=1&dark_theme=1",
        }[destination]
    raise ValueError("This channel platform is not supported.")


class APIError(RuntimeError):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


class YouTubePlayerServer:
    """Local player page supplies the Referer required by YouTube embeds."""

    def __init__(self):
        nonce = secrets.token_urlsafe(24)

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                prefix = "/player/" + nonce + "/"
                video = self.path.removeprefix(prefix)
                if self.headers.get("Host") != "127.0.0.1:" + str(self.server.server_port):
                    self.send_error(403)
                    return
                if not self.path.startswith(prefix) or not re.fullmatch(r"[a-zA-Z0-9_-]{11}", video):
                    self.send_error(404)
                    return
                body = ('<!doctype html><html><head><meta charset="utf-8">'
                        '<meta name="viewport" content="width=device-width,initial-scale=1">'
                        '<title>YT-Chatterino · YouTube player</title>'
                        '<style>html,body{margin:0;width:100%;height:100%;background:#000}'
                        'iframe{width:100%;height:100%;border:0;display:block}</style>'
                        '</head><body><iframe title="YouTube player" '
                        'referrerpolicy="strict-origin-when-cross-origin" '
                        'allow="autoplay; encrypted-media; picture-in-picture; fullscreen" allowfullscreen '
                        'src="https://www.youtube.com/embed/' + video + '?autoplay=1">'
                        '</iframe></body></html>').encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                self.send_header("Referrer-Policy", "strict-origin-when-cross-origin")
                self.send_header("X-Content-Type-Options", "nosniff")
                self.send_header("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; frame-src https://www.youtube.com; frame-ancestors 'none'")
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *args):
                pass

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = True
        self.server.block_on_close = False
        self.prefix = "http://127.0.0.1:" + str(self.server.server_port) + "/player/" + nonce + "/"
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True, name="youtube-player")
        self.thread.start()

    def url(self, video):
        if not re.fullmatch(r"[a-zA-Z0-9_-]{11}", str(video)):
            raise ValueError("This YouTube broadcast has an invalid video ID.")
        return self.prefix + video

    def close(self):
        def shutdown():
            self.server.shutdown()
            self.server.server_close()
        threading.Thread(target=shutdown, daemon=True, name="youtube-player-close").start()


def request_json(url, method="GET", token=None, body=None, headers=None, form=False):
    headers = dict(headers or {})
    headers.setdefault("User-Agent", "YT-Chatterino/2.0")
    if token:
        headers["Authorization"] = "Bearer " + token
    data = None
    if body is not None:
        if form:
            data = urllib.parse.urlencode(body).encode()
            headers["Content-Type"] = "application/x-www-form-urlencoded"
        else:
            data = json.dumps(body).encode()
            headers["Content-Type"] = "application/json"
    try:
        with urllib.request.urlopen(urllib.request.Request(
                url, data=data, headers=headers, method=method), timeout=20) as response:
            raw = response.read(4 * 1024 * 1024 + 1)
            if len(raw) > 4 * 1024 * 1024:
                raise RuntimeError("The API response was too large.")
            return json.loads(raw) if raw else {}
    except urllib.error.HTTPError as error:
        try:
            details = json.loads(error.read(65536))
            detail = details.get("error", {})
            if isinstance(detail, dict):
                text = detail.get("message", "Request rejected")
                reasons = detail.get("errors") or []
                if reasons:
                    text += " (" + str(reasons[0].get("reason", "")) + ")"
            else:
                text = details.get("message") or details.get("error_description") or str(detail)
        except Exception:
            text = "Request rejected"
        raise APIError(error.code, f"HTTP {error.code}: {text}") from None


def query_url(base, params):
    return base + ("?" + urllib.parse.urlencode(params, doseq=True) if params else "")


def parse_twitch_gifs(raw, text):
    result = []
    for item in (raw or "").split(",")[:8]:
        try:
            span, gif_id, url = item.split("|", 2)
            left, right = map(int, span.split("-", 1))
            parsed = urllib.parse.urlsplit(url)
            host = (parsed.hostname or "").lower()
            if (parsed.scheme != "https" or parsed.username or parsed.password
                    or parsed.port not in (None, 443)
                    or not (host == "giphy.com" or host.endswith(".giphy.com"))):
                continue
            if not (0 <= left <= right < len(text)) or len(url) > 4096:
                continue
            # The full Twitch-provided URL, including its query, is mandatory.
            result.append({"start": left, "end": right + 1, "id": gif_id,
                           "urls": (url,), "source": "twitch_gif"})
        except (ValueError, TypeError):
            continue
    return result


def roles_for(message):
    roles = {item.get("set_id") for item in message.get("badge_ids", [])}
    for badge in message.get("badges", []):
        mapped = {"[HOST]": "owner", "[MOD]": "moderator", "[VIP]": "vip",
                  "[SUB]": "subscriber", "[MEMBER]": "subscriber",
                  "[FOUNDER]": "founder"}.get(badge)
        if mapped:
            roles.add(mapped)
    if "broadcaster" in roles:
        roles.add("owner")
    return roles


def word_match(text, word):
    word = str(word).strip().lstrip("@")
    return bool(word and re.search(r"(?<![\w])@?" + re.escape(word) + r"(?![\w])",
                                   text, re.IGNORECASE))


def highlight_for(message, settings, own_names=()):
    name = str(message.get("login") or message.get("name") or "").lstrip("@").casefold()
    display = str(message.get("name") or "").lstrip("@").casefold()
    user_color = next((color for user, color in settings.get("users", {}).items()
                       if str(user).lstrip("@").casefold() in (name, display)), None)
    roles = roles_for(message)
    role_color = next((color for role, color in settings.get("roles", {}).items()
                       if role in roles), None)
    text = message.get("text", "")
    terms = list(settings.get("words", []))
    if settings.get("mention_me", True):
        terms.extend(own_names)
    mentioned = any(word_match(text, term) for term in terms)
    return user_color or role_color or (settings.get("mention_color", "#227400") if mentioned else None), mentioned


class GoogleAccount:
    """A public desktop OAuth client using PKCE and a loopback-only callback."""
    def __init__(self, controller):
        self.owner = controller
        core = controller.core
        cls = type("GoogleTokenStore", (core.TwitchTokenStore,), {
            "TARGET": "YT-Chatterino/Google OAuth" + ("/" + hashlib.sha256(core.DATA_HOME.encode()).hexdigest()[:16] if os.environ.get("YTCHATTERINO_DATA_DIR") else ""),
            "DPAPI_PATH": os.path.join(core.DATA_HOME, ".ytchatterino_google.dpapi")})
        self.store = cls()
        self.lock = threading.RLock()
        self.auth = {}
        self.cancel = threading.Event()
        self.busy = False
        try:
            self.auth = json.loads(self.store.read() or "{}")
        except Exception:
            pass

    def client(self):
        default = Path(self.owner.core.__file__).resolve().with_name("google-oauth-client.json")
        path = Path(self.owner.settings.get("google_client_file") or default)
        try:
            config = json.loads(path.read_text(encoding="utf-8-sig"))
            installed = config.get("installed", {})
            if not installed.get("client_id"):
                raise ValueError()
            return installed
        except (OSError, ValueError):
            raise RuntimeError("Choose a Google Desktop OAuth JSON file in Settings > Accounts.") from None

    def save(self):
        try:
            if not self.store.write(json.dumps(self.auth)):
                raise OSError()
        except Exception:
            self.owner.app.call_ui(lambda: self.owner.notify(
                "Google is connected, but encrypted storage failed. Sign-in may be needed again next time."))

    def access(self):
        with self.lock:
            client = self.client()
            if self.auth.get("client_id") != client["client_id"]:
                raise RuntimeError("Sign in to YouTube in Settings > Accounts first.")
            if self.auth.get("access_token") and time.time() < self.auth.get("expires_at", 0) - 90:
                return self.auth["access_token"]
            if not self.auth.get("refresh_token"):
                raise RuntimeError("Sign in to YouTube in Settings > Accounts first.")
            data = {"client_id": client["client_id"], "refresh_token": self.auth["refresh_token"],
                    "grant_type": "refresh_token"}
            if client.get("client_secret"):
                data["client_secret"] = client["client_secret"]
            fresh = request_json("https://oauth2.googleapis.com/token", "POST", body=data, form=True)
            self.auth.update(fresh)
            self.auth["expires_at"] = time.time() + int(fresh.get("expires_in", 3600))
            self.save()
            return self.auth["access_token"]

    def sign_in(self, callback):
        if self.busy:
            return
        try:
            client = self.client()
        except Exception as error:
            self.owner.notify(str(error))
            return
        self.busy = True
        self.cancel.clear()
        self.owner.account_status("Complete YouTube sign-in in your browser…")

        def work():
            server = None
            try:
                state = secrets.token_urlsafe(32)
                verifier = secrets.token_urlsafe(64)
                challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
                result = {}

                class Handler(http.server.BaseHTTPRequestHandler):
                    def do_GET(handler):
                        parts = urllib.parse.urlsplit(handler.path)
                        params = urllib.parse.parse_qs(parts.query)
                        returned = params.get("state", [""])[0]
                        if parts.path != "/oauth2callback" or not hmac.compare_digest(returned, state):
                            handler.send_error(400, "Invalid sign-in response")
                            return
                        result.update({key: values[0] for key, values in params.items()})
                        body = b"Sign-in received. You can close this page and return to YT-Chatterino."
                        handler.send_response(200)
                        handler.send_header("Content-Type", "text/plain; charset=utf-8")
                        handler.send_header("Content-Length", str(len(body)))
                        handler.end_headers()
                        handler.wfile.write(body)

                    def log_message(handler, *args):
                        pass  # Never write OAuth codes to logs.

                server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
                server.timeout = 0.5
                redirect = f"http://127.0.0.1:{server.server_port}/oauth2callback"
                url = query_url("https://accounts.google.com/o/oauth2/v2/auth", {
                    "client_id": client["client_id"], "redirect_uri": redirect,
                    "response_type": "code", "scope": GOOGLE_SCOPE, "state": state,
                    "code_challenge": challenge, "code_challenge_method": "S256",
                    "access_type": "offline", "prompt": "consent"})
                self.owner.app.call_ui(lambda: self.owner.open_url(url))
                deadline = time.monotonic() + 300
                while not result and not self.cancel.is_set() and time.monotonic() < deadline:
                    server.handle_request()
                if self.cancel.is_set():
                    return
                if not result.get("code"):
                    raise RuntimeError("YouTube sign-in was declined or timed out. Try again.")
                data = {"client_id": client["client_id"], "code": result["code"],
                        "code_verifier": verifier, "redirect_uri": redirect,
                        "grant_type": "authorization_code"}
                if client.get("client_secret"):
                    data["client_secret"] = client["client_secret"]
                auth = request_json("https://oauth2.googleapis.com/token", "POST", body=data, form=True)
                channels = request_json(query_url("https://www.googleapis.com/youtube/v3/channels",
                    {"part": "snippet", "mine": "true"}), token=auth["access_token"])
                self.owner.core.QUOTA.add(1)
                items = channels.get("items", [])
                if not items:
                    raise RuntimeError("This Google account has no YouTube channel. Choose an account with a channel.")
                with self.lock:
                    self.auth = dict(auth, client_id=client["client_id"], channel_id=items[0]["id"],
                                     name=items[0]["snippet"]["title"],
                                     expires_at=time.time() + int(auth.get("expires_in", 3600)))
                    self.save()
                self.owner.app.call_ui(callback)
            except Exception as error:
                self.owner.app.call_ui(lambda message=str(error): self.owner.notify(message))
            finally:
                if server:
                    server.server_close()
                self.busy = False
                self.owner.app.call_ui(lambda: self.owner.account_status())
        threading.Thread(target=work, daemon=True, name="google-sign-in").start()


class FeatureController:
    def __init__(self, app, core):
        self.app, self.core = app, core
        self.settings = app.cfg.setdefault("features", {})
        self.settings.setdefault("highlight", {"users": {}, "roles": {}, "words": [], "mention_me": True})
        self.settings.setdefault("alerts", {"mention_sound": True, "enabled": True, "cooldown": 5, "watch": []})
        highlights = self.settings["highlight"]
        if self.settings.get("defaults_version", 0) < 2:
            if highlights.get("mention_color") in (None, "#493b16", "#293d52", "#263648"):
                highlights["mention_color"] = "#227400"
            for group in ("users", "roles"):
                for name, color in highlights.get(group, {}).items():
                    if color.lower() in ("#493b16", "#293d52", "#263648"):
                        highlights[group][name] = "#227400"
            self.settings["defaults_version"] = 2
        alerts = self.settings["alerts"]
        alerts["watch"] = [item for item in alerts.get("watch", []) if item.get("platform") == "twitch"]
        alerts.setdefault("all_twitch", True)
        alerts.setdefault("excluded", [])
        alerts.pop("search_estimate", None)
        self.emote_sets = {}
        self.pickers = {}
        self.tab_drag = None
        self.executor = ThreadPoolExecutor(max_workers=3, thread_name_prefix="chat-action")
        self.google = GoogleAccount(self)
        self.window = None
        self.account_label = None
        self.quota_widgets = {}
        self.quota_display = None
        self.settings_section = "Accounts"
        self.permissions = {}
        self.identity = None
        self.identity_token = None
        self.next_sound = 0
        self.last_send = {}
        self.watch_state = {}
        self.watch_discovery = {}
        self.watch_busy = False
        self.watch_generation = 0
        self.watch_timer = None
        self.closed = False
        self.started = time.time()
        self.pending_operations = set()
        self.twitch_required = set()
        self.chat_tools = ChatTools(self)

    def save(self):
        self.core.save_config(self.app.cfg)

    def start(self):
        self.watch_timer = self.app.root.after(1500, self.poll_watches)
        self.app.updater.start()

    def close(self):
        self.closed = True
        player_server = getattr(self, "player_server", None)
        if player_server:
            player_server.close()
        for card in tuple(self.chat_tools.cards.values()):
            card.close()
        for thread in tuple(self.chat_tools.threads.values()):
            thread.close()
        self.google.cancel.set()
        for picker in tuple(self.pickers.values()):
            picker.close()
        if self.watch_timer:
            self.app.root.after_cancel(self.watch_timer)
        self.executor.shutdown(wait=False, cancel_futures=True)

    def open_url(self, url):
        try:
            if os.name == "nt":
                os.startfile(url)
            else:
                webbrowser.open(url, new=2)
        except OSError as error:
            self.notify(str(error))

    def notify(self, text, error=False):
        if self.closed:
            return
        (messagebox.showerror if error else messagebox.showinfo)(
            "YT-Chatterino", text, parent=self.window if self.window and self.window.winfo_exists() else self.app.root)

    def async_action(self, key, work, done=None, failed=None):
        if self.closed or key in self.pending_operations:
            return False
        self.pending_operations.add(key)
        def run():
            try:
                value = work()
            except Exception as error:
                self.app.call_ui(lambda text=str(error): finish(None, text))
            else:
                self.app.call_ui(lambda: finish(value, None))
        def finish(value, error):
            self.pending_operations.discard(key)
            if self.closed:
                return
            if error:
                if failed:
                    failed(error)
                else:
                    self.notify(error, True)
            elif done:
                done(value)
        self.executor.submit(run)
        return True

    def twitch_auth(self):
        manager = self.app.twitch_manager
        if not manager:
            raise RuntimeError("Sign in to Twitch in Settings > Accounts.")
        with manager.lock:
            return dict(manager.auth)

    def ensure_twitch(self, required, callback):
        try:
            auth = self.twitch_auth()
        except RuntimeError:
            auth = {}
        if self.app.twitch_manager and self.app.twitch_manager.auth_failed:
            auth = {}
        if set(required).issubset(set(auth.get("scope", []))):
            callback()
            return
        self.twitch_required = set(required)
        self.app.ensure_twitch_auth(lambda auth: self._permissions_ready(auth, required, callback), force_login=True)

    def _permissions_ready(self, auth, required, callback):
        self.permissions.clear()
        self.twitch_required.clear()
        self.account_status()
        if set(required).issubset(set(auth.get("scope", []))):
            callback()
        else:
            self.notify("Twitch did not grant the permissions needed for this action.")

    def twitch_identity(self):
        auth = self.twitch_auth()
        if auth.get("access_token") != self.identity_token:
            identity = self.core.twitch_validate_token(auth["access_token"])
            self.identity = identity
            self.identity_token = auth["access_token"]
        return self.identity

    def helix(self, path, method="GET", params=None, body=None, scope=None):
        auth = self.twitch_auth()
        if scope and scope not in auth.get("scope", []):
            raise RuntimeError("Enable Twitch sending / moderation in Settings > Accounts.")
        return request_json(query_url("https://api.twitch.tv/helix/" + path, params or {}), method,
                            token=auth["access_token"], body=body, headers={"Client-Id": self.core.TWITCH_CLIENT_ID})

    def broadcaster(self, source):
        worker_id = getattr(source.worker, "room_id", None)
        value = source.header_info.get("broadcaster_id") or worker_id
        if not value:
            users = self.helix("users", params={"login": source.video_id}).get("data", [])
            if not users:
                raise RuntimeError("Twitch channel not found.")
            value = users[0]["id"]
        return str(value)

    def can_moderate_twitch(self, source):
        identity = self.twitch_identity()
        broadcaster = self.broadcaster(source)
        uid = identity["user_id"]
        if broadcaster == uid:
            return True
        cached = self.permissions.get((uid, broadcaster))
        if cached and cached[0] > time.monotonic():
            return cached[1]
        cursor = None
        found = False
        for _ in range(30):
            params = {"user_id": uid, "first": 100}
            if cursor:
                params["after"] = cursor
            result = self.helix("moderation/channels", params=params, scope="user:read:moderated_channels")
            if any(item["broadcaster_id"] == broadcaster for item in result.get("data", [])):
                found = True
                break
            cursor = result.get("pagination", {}).get("cursor")
            if not cursor:
                break
        self.permissions[(uid, broadcaster)] = (time.monotonic() + 60, found)
        return found

    def youtube(self, path, method="GET", params=None, body=None, units=1):
        token = self.google.access()
        self.core.QUOTA.add(units)
        try:
            return request_json(query_url("https://www.googleapis.com/youtube/v3/" + path,
                                params or {}), method, token=token, body=body)
        except APIError as error:
            if error.status == 401:
                with self.google.lock:
                    self.google.auth["expires_at"] = 0
                raise RuntimeError("YouTube sign-in expired. Retry to refresh it, or sign in again in Settings > Accounts.") from None
            raise

    def youtube_chat_id(self, source):
        chat_id = source.header_info.get("live_chat_id")
        if not chat_id:
            data = self.youtube("videos", params={"id": source.video_id, "part": "liveStreamingDetails,snippet"})
            items = data.get("items", [])
            if items:
                chat_id = items[0].get("liveStreamingDetails", {}).get("activeLiveChatId")
        if not chat_id:
            raise RuntimeError("This broadcast has no active live chat.")
        source.header_info["live_chat_id"] = chat_id
        return chat_id

    def attach_composer(self, pane):
        frame = tk.Frame(pane, bg="#111111", padx=4, pady=3)
        frame.pack(side="bottom", fill="x")
        pane.composer = frame
        pane.compose_target = tk.StringVar()
        pane.target_combo = ttk.Combobox(frame, textvariable=pane.compose_target, state="readonly", width=16)
        pane.target_combo.pack(side="left", padx=(0, 4))
        pane.target_combo.bind("<<ComboboxSelected>>", lambda event: self.update_send_label(pane))
        pane.compose_text = tk.StringVar()
        pane.compose_entry = tk.Entry(frame, textvariable=pane.compose_text, bg="#111111", fg=self.core.FG,
                                      insertbackground=self.core.FG, relief="flat", font=("Segoe UI", 10))
        pane.compose_entry.pack(side="left", fill="x", expand=True)
        pane.compose_entry.bind("<Return>", lambda event: self.submit_message(pane))
        pane.compose_entry.bind("<FocusIn>", lambda event: self.app.focus_pane(pane), add="+")
        pane.send_button = tk.Button(frame, text=tr("Send"), bg="#111111", fg=self.core.FG, bd=0,
                                      padx=6, command=lambda: self.submit_message(pane))
        pane.send_button.pack(side="right")
        pane.emote_button = tk.Button(frame, text=tr("☺"), bg="#111111", fg=self.core.FG, bd=0,
                                     font=("Segoe UI Symbol", 13), padx=6, command=lambda: self.show_emotes(pane))
        pane.emote_button.pack(side="right")
        pane.char_count = tk.Label(frame, text="", bg="#111111", fg=self.core.DIM, font=("Segoe UI", 8))
        pane.char_count.pack(side="right", padx=3)
        pane.compose_text.trace_add("write", lambda *args: self.update_character_count(pane))
        pane.compose_entry.bind("<Shift-Tab>", lambda event: self.complete_name(pane, -1))
        pane.compose_entry.bind("<ISO_Left_Tab>", lambda event: self.complete_name(pane, -1))
        pane.compose_entry.bind("<Up>", lambda event: pane.compose_text.set(getattr(pane, "last_sent_text", "")))
        pane.compose_entry.bind("<Tab>", lambda event: self.complete_name(pane))
        self.chat_tools.attach_reply_bar(pane)

    def complete_name(self, pane, direction=1):
        text = pane.compose_text.get()
        cursor = pane.compose_entry.index("insert")
        previous = getattr(pane, "completion", None)
        if previous and previous["text"] == text and previous["cursor"] == cursor:
            state = previous
            state["index"] = (state["index"] + direction) % len(state["matches"])
        else:
            match = re.search(r"\S+$", text[:cursor])
            if not match:
                return None
            prefix = match.group()
            start = match.start()
            end_match = re.search(r"\s", text[cursor:])
            end = cursor + end_match.start() if end_match else len(text)
            if prefix.startswith("@"):
                names = {value.get("login") or value.get("name", "") for source in pane.sources
                         for _, kind, value, _ in source.history if kind == "msg"}
                matches = sorted({"@" + name.lstrip("@") for name in names if name.casefold().lstrip("@").startswith(prefix[1:].casefold())}, key=str.casefold)
            else:
                index = pane.target_combo.current()
                if index < 0 or index >= len(pane.sources):
                    return None
                source = pane.sources[index]
                catalog = self.available_emotes(source)
                matches = sorted((code for code in catalog if code.casefold().startswith(prefix.casefold())), key=lambda value: (value.casefold(), value))
            if not matches:
                return None
            state = {"start": start, "tail": text[end:], "head": text[:start], "matches": matches,
                     "index": 0 if direction > 0 else len(matches) - 1}
        word = state["matches"][state["index"]]
        suffix = "" if state["tail"].startswith(" ") else " "
        value = state["head"] + word + suffix + state["tail"]
        pane.compose_text.set(value)
        position = len(state["head"]) + len(word) + (1 if suffix else 1 if state["tail"].startswith(" ") else 0)
        pane.compose_entry.icursor(position)
        state.update(text=tr(value), cursor=position)
        pane.completion = state
        return "break"

    def refresh_composer(self, pane):
        if not hasattr(pane, "target_combo"):
            return
        current = getattr(pane, "_compose_source_key", None)
        selected = pane.target_combo.current()
        prior = getattr(pane, "_compose_sources", [])
        if 0 <= selected < len(prior):
            current = prior[selected].key
        pane._compose_sources = list(pane.sources)
        values = [("TW · " if source.platform == "twitch" else "YT · ") + source.name for source in pane.sources]
        pane.target_combo.configure(values=values)
        if pane.sources:
            index = next((i for i, source in enumerate(pane.sources) if source.key == current), 0)
            pane.target_combo.current(index)
            pane._compose_source_key = pane.sources[index].key
        self.update_send_label(pane)

    def update_send_label(self, pane):
        self.chat_tools.changed_target(pane)
        if getattr(pane, "sending", False):
            return
        index = pane.target_combo.current()
        source = pane.sources[index] if 0 <= index < len(pane.sources) else None
        key = source.key if source else None
        if getattr(pane, "completion_source", None) != key:
            pane.completion = None
            pane.completion_source = key
        pane.send_button.configure(text=tr("Send · 20" if source and source.platform == "youtube" else "Send"))
        self.update_character_count(pane)

    def update_character_count(self, pane):
        if pane.closed or not getattr(pane, "char_count", None):
            return
        index = pane.target_combo.current()
        source = pane.sources[index] if 0 <= index < len(pane.sources) else None
        maximum = 200 if source and source.platform == "youtube" else 500
        count = len(pane.compose_text.get())
        pane.char_count.configure(text=tr(f"{count}/{maximum}" if count else ""),
                                  fg="#ff7777" if count > maximum else self.core.DIM)

    def open_channel_browser(self, source, destination):
        if self.closed or source.closed:
            return
        try:
            url = channel_browser_url(source, destination)
            if source.platform == "youtube" and destination == "player":
                if not getattr(self, "player_server", None):
                    self.player_server = YouTubePlayerServer()
                url = self.player_server.url(source.video_id)
        except (ValueError, OSError) as error:
            self.notify(str(error), True)
            return
        self.open_url(url)

    def add_browser_actions(self, menu, source):
        for destination, label in BROWSER_ACTIONS:
            menu.add_command(label=tr(label),
                             command=lambda kind=destination, item=source: self.open_channel_browser(item, kind))

    def tab_browser_menu(self, workspace, menu):
        sources = {}
        for pane in workspace.panes:
            for source in pane.sources:
                if not source.closed:
                    sources.setdefault(source.key, source)
        if not sources:
            return
        if len(sources) == 1:
            self.add_browser_actions(menu, next(iter(sources.values())))
        else:
            for source in sources.values():
                actions = self.app._menu(menu)
                self.add_browser_actions(actions, source)
                label = ("TW · " if source.platform == "twitch" else "YT · ") + source.name
                if source.platform == "youtube":
                    label += " · " + source.video_id
                menu.add_cascade(label=tr(label), menu=actions)
        menu.add_separator()

    def submit_message(self, pane):
        if getattr(pane, "sending", False):
            return "break"
        index = pane.target_combo.current()
        if index < 0 or index >= len(pane.sources):
            return "break"
        source = pane.sources[index]
        text = pane.compose_text.get().strip()
        if not text:
            return "break"
        maximum = 500 if source.platform == "twitch" else 200
        if len(text) > maximum or "\r" in text or "\n" in text:
            self.notify(f"Use a single message of up to {maximum} characters.")
            return "break"
        if text.startswith("/"):
            self.notify("Use the message context menu for moderation. Slash commands are not sent as ordinary messages.")
            return "break"
        if source.platform == "twitch":
            self.ensure_twitch({"user:write:chat"}, lambda: self._send(pane, source, text))
        elif not self.google.auth.get("channel_id"):
            self.google.sign_in(lambda: self._send(pane, source, text))
        else:
            self._send(pane, source, text)
        return "break"

    def send(self, source, text, reply=None):
        now = time.monotonic()
        if now - self.last_send.get(source.key, -100) < 1.2:
            raise RuntimeError("Wait a moment before sending another message.")
        self.last_send[source.key] = now
        if source.platform == "twitch":
            identity = self.twitch_identity()
            body = {"broadcaster_id": self.broadcaster(source), "sender_id": identity["user_id"], "message": text}
            if reply:
                body["reply_parent_message_id"] = reply["id"]
            response = self.helix("chat/messages", "POST", body=body, scope="user:write:chat")
            data = response.get("data", [])
            if not data or not data[0].get("is_sent"):
                reason = data[0].get("drop_reason") or {} if data else {}
                raise RuntimeError(reason.get("message") or "Twitch did not accept this message.")
            return data[0]
        data = self.youtube("liveChat/messages", "POST", params={"part": "snippet"}, units=20,
            body={"snippet": {"liveChatId": self.youtube_chat_id(source), "type": "textMessageEvent",
                              "textMessageDetails": {"messageText": text}}})
        return data

    def _send(self, pane, source, text):
        if pane.closed or source not in pane.sources:
            return
        pane.sending = True
        target = getattr(pane, "reply_target", None)
        reply = dict(target[1]) if target and target[0] == source.key else None
        sent_at = time.time()
        pane.send_button.configure(state="disabled", text=tr("…"))
        def finished(value=None, error=None):
            if pane.closed:
                return
            pane.sending = False
            pane.send_button.configure(state="normal")
            self.update_send_label(pane)
            if error:
                self.notify(error, True)
                return
            if reply and getattr(pane, "reply_target", None) == target:
                self.chat_tools.cancel_reply(pane)
            pane.last_sent_text = text
            if pane.compose_text.get().strip() == text:
                pane.compose_text.set("")
            # Prefer Twitch's richer IRC echo. During the reconnect after a
            # permission upgrade the API can accept a message before IRC rejoins;
            # show the acknowledged message if no echo arrives. Source ID dedup
            # prevents the delayed fallback from showing a second copy.
            if source.platform == "youtube":
                own = {"id": value.get("id"), "time": self.core.clock(), "name": self.google.auth.get("name", "You"),
                       "user_id": self.google.auth.get("channel_id"), "badges": [], "special": None,
                       "text": text, "_own": True, "_ack_at": sent_at, "sent_at": sent_at}
                self.publish_youtube_ack(source, own)
            else:
                auth = self.twitch_auth()
                uid = auth.get("user_id")
                own = {"id": value.get("message_id"), "time": self.core.clock(),
                       "name": auth.get("login", "You"), "login": auth.get("login", ""),
                       "user_id": uid, "badges": ["[HOST]"] if uid and uid == source.header_info.get("broadcaster_id") else [],
                       "special": None, "text": text, "_own": True, "sent_at": sent_at, "reply": reply}
                self.app.root.after(1500, lambda: source.publish("msg", own) if not source.closed else None)
            pane.compose_entry.focus_set()
        self.async_action(("send", pane.uid), lambda: self.send(source, text, reply), finished,
                          lambda error: finished(error=error))

    def publish_youtube_ack(self, source, own):
        for _, kind, message, _ in list(source.history)[-128:]:
            if (kind == "msg" and message.get("_youtube_web") and not message.get("api_id")
                    and message.get("user_id") == own.get("user_id")
                    and message.get("text") == own.get("text")
                    and abs(message.get("sent_at", 0) - own["_ack_at"]) < 60):
                message["api_id"] = own["id"]
                return
        source.publish("msg", own)

    def youtube_moderation_id(self, source, message):
        if message.get("api_id") or not message.get("_youtube_web"):
            return message.get("api_id") or message.get("id")
        # Resolve an official ID only when the user requests deletion; ordinary
        # viewing remains completely independent of the Data API and its quota.
        data = self.youtube("liveChat/messages", params={"liveChatId": self.youtube_chat_id(source),
                            "part": "snippet,authorDetails", "maxResults": 200}, units=5)
        matches = []
        for item in data.get("items", []):
            if item.get("id") == message.get("id"):
                message["api_id"] = item["id"]
                return item["id"]
            try:
                parsed = self.core.parse_item(item)
            except (KeyError, TypeError, ValueError):
                parsed = None
            if (parsed and parsed.get("user_id") == message.get("user_id")
                    and parsed.get("sent_at") and message.get("sent_at")
                    and abs(parsed["sent_at"] - message["sent_at"]) < 1.1):
                matches.append((item["id"], parsed.get("text") == message.get("text")))
        exact = [mid for mid, same_text in matches if same_text]
        if len(exact) == 1 or (not exact and len(matches) == 1):
            mid = exact[0] if exact else matches[0][0]
            message["api_id"] = mid
            return mid
        raise RuntimeError("YouTube did not provide an unambiguous deletion ID for this message. Delete it on YouTube instead.")

    def own_names(self):
        names = list(self.settings.get("nicknames", []))
        manager = self.app.twitch_manager
        if manager and manager.auth.get("login"):
            names.append(manager.auth["login"])
        if self.google.auth.get("name"):
            names.append(self.google.auth["name"])
        return names

    def on_message(self, source, message):
        if message.get("deleted") or message.get("_own") or message.get("_history"):
            return
        _, mentioned = highlight_for(message, self.settings["highlight"], self.own_names())
        own_id = (self.google.auth.get("channel_id") if source.platform == "youtube"
                  else (self.app.twitch_manager.auth.get("user_id") if self.app.twitch_manager else None))
        own_name = self.app.twitch_manager.auth.get("login", "") if self.app.twitch_manager else ""
        is_own = (own_id and message.get("user_id") == own_id) or (
            source.platform == "twitch" and message.get("login", "").casefold() == own_name.casefold() and own_name)
        if mentioned and not is_own and self.settings["alerts"].get("mention_sound", True):
            # Ignore historical messages delivered on first connection.
            timestamp = message.get("sent_at")
            if timestamp and timestamp < self.started - 3:
                return
            self.play_sound()

    def apply_highlight(self, pane, message, start, end):
        color, _ = highlight_for(message, self.settings["highlight"], self.own_names())
        if not color or not re.fullmatch(r"#[0-9A-Fa-f]{6}", str(color)):
            return
        tag = "__highlight_" + color[1:]
        pane.text.tag_configure(tag, background=color)
        pane.text.tag_add(tag, start, end)
        pane.text.tag_raise(tag)
        for higher in ("search_hit", "search_current", "sel"):
            pane.text.tag_raise(higher)

    def refresh_highlights(self):
        for pane in self.app.display_panes():
            pane.finish_resize()
            for tag in pane.text.tag_names():
                if tag.startswith("__highlight_"):
                    pane.text.tag_remove(tag, "1.0", "end")
            for seq, (source, message) in pane.message_records.items():
                ranges = pane.text.tag_ranges("message_" + str(seq))
                if len(ranges) >= 2:
                    self.apply_highlight(pane, message, ranges[0], ranges[-1])

    def highlight_user(self, name):
        users = self.settings["highlight"].setdefault("users", {})
        normalized = str(name).lstrip("@").casefold()
        if normalized in users:
            users.pop(normalized)
        else:
            users[normalized] = "#227400"
        self.save()
        self.refresh_highlights()

    def message_menu(self, pane, menu, index):
        record = None
        for tag in pane.text.tag_names(index):
            if tag.startswith("message_"):
                record = pane.message_records.get(int(tag[8:]))
                if record:
                    break
        if not record:
            return
        source, message = record
        name = message.get("login") or message.get("name", "")
        if not name:
            return
        menu.add_separator()
        menu.add_command(label=tr("@" + name + " · " + source.name), state="disabled")
        users = self.settings["highlight"].get("users", {})
        menu.add_command(label=(tr("Remove user highlight" if name.lstrip("@").casefold() in users else "Highlight this user")),
                         command=lambda: self.highlight_user(name))
        menu.add_command(label=tr("User information"), command=lambda: self.chat_tools.show_user(pane, source, message))
        if source.platform == "twitch":
            menu.add_command(label=tr("Reply to message"), command=lambda: self.chat_tools.start_reply(pane, source, message),
                             state="disabled" if not message.get("id") or message.get("deleted") else "normal")
            if message.get('reply') and not pane.detached:
                menu.add_command(label=tr('Open reply thread'), command=lambda: self.chat_tools.show_thread(source, message))
        menu.add_command(label=tr("Mention"), command=lambda: self.mention(pane, source, name))
        actions = self.app._menu(menu)
        restricted = (source.platform == "twitch" and "owner" in roles_for(message))
        deletion_units = "~25–26" if message.get("_youtube_web") and not message.get("api_id") else "20"
        deletion_label = "Delete this message" + (" · " + deletion_units + " units" if source.platform == "youtube" else "")
        actions.add_command(label=tr(deletion_label if not restricted else "Twitch does not allow deleting broadcaster messages"),
                            command=lambda: self.moderate(source, message, "delete"),
                            state="disabled" if message.get("deleted") or restricted else "normal")
        for seconds, label in ((60, "Timeout · 1 minute"), (600, "Timeout · 10 minutes"), (3600, "Timeout · 1 hour")):
            actions.add_command(label=tr(label + (" · 200 units" if source.platform == "youtube" else "")), command=lambda duration=seconds: self.moderate(source, message, "timeout", duration))
        actions.add_command(label=tr("Ban user…" + (" · 200 units" if source.platform == "youtube" else "")), command=lambda: self.moderate(source, message, "ban"))
        if source.platform == "twitch":
            actions.add_command(label=tr("Unban user"), command=lambda: self.moderate(source, message, "unban"))
        menu.add_cascade(label=tr("Moderation"), menu=actions)

    def mention(self, pane, source, name):
        if source in pane.sources:
            pane.target_combo.current(pane.sources.index(source))
        pane.compose_text.set(pane.compose_text.get() + "@" + name.lstrip("@") + " ")
        pane.compose_entry.focus_set()

    def moderate(self, source, message, action, duration=0):
        if action == "ban" and not messagebox.askyesno(tr("Ban user"), tr(f"Ban {message.get('name')} from {source.name}?"), parent=self.app.root):
            return
        def perform():
            self.async_action(("moderate", source.key, message.get("id"), action),
                lambda: self.moderation_request(source, message, action, duration),
                lambda result: self.moderation_completed(source, message, action, result))
        if source.platform == "twitch":
            scopes = {"user:read:moderated_channels", "moderator:manage:chat_messages" if action == "delete" else "moderator:manage:banned_users"}
            self.ensure_twitch(scopes, perform)
        elif not self.google.auth.get("channel_id"):
            self.google.sign_in(perform)
        else:
            perform()

    def moderation_request(self, source, message, action, duration=0):
        uid = message.get("user_id")
        message_id = message.get("id")
        if action != "delete" and not uid:
            raise RuntimeError("This message does not contain the user's channel ID.")
        if action == "delete" and not message_id:
            raise RuntimeError("This message has no moderation ID.")
        if source.platform == "twitch":
            if not self.can_moderate_twitch(source):
                raise RuntimeError("Your Twitch account is not the owner or a moderator of this channel.")
            if action == "delete" and str(uid) == self.broadcaster(source):
                raise RuntimeError("Twitch's API cannot delete the broadcaster's own messages. Remove this message on Twitch instead.")
            params = {"broadcaster_id": self.broadcaster(source), "moderator_id": self.twitch_identity()["user_id"]}
            if action == "delete":
                params["message_id"] = message_id
                return self.helix("moderation/chat", "DELETE", params=params, scope="moderator:manage:chat_messages")
            if action == "unban":
                params["user_id"] = uid
                return self.helix("moderation/bans", "DELETE", params=params, scope="moderator:manage:banned_users")
            data = {"user_id": uid}
            if action == "timeout":
                data["duration"] = int(duration)
            return self.helix("moderation/bans", "POST", params=params, body={"data": data}, scope="moderator:manage:banned_users")
        # YouTube authoritatively checks owner/moderator access on each request.
        if action == "delete":
            return self.youtube("liveChat/messages", "DELETE", params={"id": self.youtube_moderation_id(source, message)}, units=20)
        if action == "unban":
            raise RuntimeError("YouTube unbanning requires the original ban ID. Use YouTube Studio for existing bans.")
        snippet = {"liveChatId": self.youtube_chat_id(source),
                   "type": "temporary" if action == "timeout" else "permanent",
                   "bannedUserDetails": {"channelId": uid}}
        if action == "timeout":
            snippet["banDurationSeconds"] = int(duration)
        return self.youtube("liveChat/bans", "POST", params={"part": "snippet"}, body={"snippet": snippet}, units=200)

    def moderation_completed(self, source, message, action, result):
        if action == "unban":
            source.publish("status", f"Unbanned {message.get('name')}.")
        else:
            self.redact(source, {"message_id": message.get("id")} if action == "delete" else {"user_id": message.get("user_id")})
            source.publish("status", f"{action.capitalize()} succeeded: {message.get('name')}.")
        if source.platform == "youtube" and result.get("id"):
            ban_id = result["id"]
            source.publish("status", "YouTube ban applied. Use the pane's Undo last ban action if needed.")
            source.last_ban_id = ban_id

    def redact(self, source, target):
        affected = []
        for _, kind, message, _ in source.history:
            if kind != "msg" or message.get("deleted"):
                continue
            matches = (target.get("message_id") and target["message_id"] in (
                message.get("id"), message.get("api_id"), message.get("youtube_web_id")))
            matches = matches or (target.get("user_id") and message.get("user_id") == target["user_id"])
            matches = matches or target.get("all")
            if matches:
                message.update(text=tr("[message deleted]"), native_emotes=[], gifs=[], deleted=True)
                affected.append(message)
        for thread in tuple(self.chat_tools.threads.values()):
            thread.redact(source, target)
        for pane in tuple(source.views):
            for message in affected:
                seq = message.get("_seq")
                tag = "message_" + str(seq)
                ranges = pane.text.tag_ranges(tag)
                if not ranges:
                    continue
                start, end = str(ranges[0]), str(ranges[-1])
                pane.text.configure(state="normal")
                for kind, image, _ in pane.text.dump(start, end, image=True):
                    if kind == "image":
                        pane.image_names.pop(image, None)
                        pane.image_assets.pop(image, None)
                pane.text.delete(start, end)
                timestamp = self.core.display_time(message.get("time", ""), message.get("sent_at")) + " "
                prefix = ("[" + source.name + "] ") if len(pane.sources) > 1 else ""
                line = timestamp + prefix + message.get("name", "") + ": " + str(tr("[message deleted]")) + "\n"
                pane.text.insert(start, line, ("sys", tag))
                pane.text.tag_add("time", start, f"{start}+{len(timestamp)}c")
                pane.text.configure(state="disabled")
                pane.used_assets = set(pane.image_assets.values())
                pane.refresh_search_later()

    def edit_stream(self, source):
        def open_editor():
            dialog = self.dialog("Edit stream · " + source.name, 520, 220)
            title = tk.StringVar(value=source.header_info.get("title", ""))
            self.label(dialog, "Stream title").pack(anchor="w", padx=14, pady=(14, 3))
            self.entry(dialog, title).pack(fill="x", padx=14)
            category = tk.StringVar(value=source.header_info.get("category", ""))
            choices = ttk.Combobox(dialog, state="readonly")
            categories = []
            if source.platform == "twitch":
                row = tk.Frame(dialog, bg=self.core.BG)
                row.pack(fill="x", padx=14, pady=8)
                self.entry(row, category).pack(side="left", fill="x", expand=True)
                def search():
                    def ready(result):
                        if not dialog.winfo_exists():
                            return
                        categories[:] = result.get("data", [])
                        choices.configure(values=[item["name"] for item in categories])
                        if categories:
                            choices.current(0)
                    query = category.get().strip()
                    self.async_action(("categories", id(dialog)), lambda: self.helix("search/categories", params={"query": query, "first": 20}), ready)
                self.button(row, "Find category", search).pack(side="right", padx=(6, 0))
                choices.pack(fill="x", padx=14)
                self.label(dialog, "Leave category selection empty to keep the current category.", dim=True).pack(anchor="w", padx=14, pady=4)
            else:
                self.label(dialog, "YouTube requires channel ownership or supported channel authorization.", dim=True).pack(anchor="w", padx=14, pady=8)
            status = self.label(dialog, "")
            status.pack(anchor="w", padx=14)
            def apply():
                value = title.get().strip()
                limit = 140 if source.platform == "twitch" else 100
                if not value or len(value) > limit:
                    status.config(text=tr(f"Use a title of 1–{limit} characters."))
                    return
                selected = choices.current()
                game_id = categories[selected]["id"] if 0 <= selected < len(categories) else None
                def done(result):
                    source.header_info["title"] = value
                    if game_id:
                        source.header_info["category"] = categories[selected]["name"]
                    for pane in source.views:
                        pane.refresh_header()
                    if source.info_worker:
                        source.info_worker.refresh_event.set()
                    if dialog.winfo_exists():
                        dialog.destroy()
                self.async_action(("edit", source.key), lambda: self.update_stream(source, value, game_id), done)
            self.button(dialog, "Save · 50 units" if source.platform == "youtube" else "Save", apply).pack(anchor="e", padx=14, pady=12)
        if source.platform == "twitch":
            self.ensure_twitch({"channel:manage:broadcast"}, open_editor)
        elif not self.google.auth.get("channel_id"):
            self.google.sign_in(open_editor)
        else:
            open_editor()

    def update_stream(self, source, title, category_id=None):
        if source.platform == "twitch":
            bid = self.broadcaster(source)
            if bid != self.twitch_identity()["user_id"]:
                raise RuntimeError("Twitch's API requires the broadcaster's account to change title/category. Moderator or editor status alone is insufficient.")
            body = {"title": title}
            if category_id:
                body["game_id"] = category_id
            return self.helix("channels", "PATCH", params={"broadcaster_id": bid}, body=body, scope="channel:manage:broadcast")
        data = self.youtube("videos", params={"part": "snippet", "id": source.video_id})
        items = data.get("items", [])
        if not items:
            raise RuntimeError("YouTube broadcast not found.")
        snippet = items[0]["snippet"]
        # videos.update replaces supplied parts: retain every writable snippet field.
        keep = {key: snippet[key] for key in ("categoryId", "description", "tags", "defaultLanguage", "defaultAudioLanguage") if key in snippet}
        keep["title"] = title
        return self.youtube("videos", "PUT", params={"part": "snippet"}, body={"id": source.video_id, "snippet": keep}, units=50)

    def pane_menu(self, pane, menu):
        menu.add_command(label=tr("Message limit…"), command=lambda: self.chat_tools.pane_limit(pane))
        for source in pane.sources:
            actions = self.app._menu(menu)
            actions.add_command(label=tr("Edit title / category…" if source.platform == "twitch" else "Edit title…"),
                                command=lambda item=source: self.edit_stream(item))
            actions.add_command(label=tr("Save channel"), command=lambda item=source: self.notify(self.save_source(item)))
            if source.platform == "twitch":
                actions.add_command(label=tr("Enable live notification"), command=lambda item=source: self.add_watch_source(item))
            if source.platform == "youtube" and getattr(source, "last_ban_id", None):
                actions.add_command(label=tr("Undo last YouTube ban · 200 units"), command=lambda item=source: self.undo_ban(item))
            menu.add_cascade(label=tr("Channel actions · " + source.name), menu=actions)
        menu.add_separator()

    def undo_ban(self, source):
        ban_id = getattr(source, "last_ban_id", None)
        if ban_id:
            self.async_action(("unban", source.key), lambda: self.youtube("liveChat/bans", "DELETE", params={"id": ban_id}, units=200),
                              lambda result: setattr(source, "last_ban_id", None))

    def play_sound(self, force=False, live=False):
        alerts = self.settings["alerts"]
        if not force and not alerts.get("enabled", True):
            return
        now = time.monotonic()
        if not force and now < self.next_sound:
            return
        self.next_sound = now + max(1, float(alerts.get("cooldown", 5)))
        chosen = alerts.get("live_sound_file" if live else "sound_file")
        path = Path(chosen) if chosen and Path(chosen).is_file() else self.default_sound_path()
        def play():
            try:
                if sys.platform != "win32":
                    for name, args in (("pw-play", []), ("paplay", []), ("aplay", ["-q"])):
                        player = shutil.which(name)
                        if player and path.is_file():
                            subprocess.run([player, *args, str(path)], timeout=10, check=False,
                                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                            return
                    self.app.call_ui(lambda: self.app.root.bell())
                    return
                import winsound
                if path.is_file():
                    winsound.PlaySound(str(path), winsound.SND_FILENAME | winsound.SND_ASYNC | winsound.SND_NODEFAULT)
                else:
                    winsound.MessageBeep(winsound.MB_OK)
            except (ImportError, RuntimeError, OSError):
                self.app.call_ui(lambda: self.app.root.bell())
        threading.Thread(target=play, daemon=True, name="chat-alert").start()

    def add_watch_source(self, source):
        if source.platform != "twitch":
            return
        alerts = self.settings["alerts"]
        alerts["excluded"] = [item for item in alerts.get("excluded", []) if item.casefold() != source.video_id.casefold()]
        if not any(item.get("platform") == "twitch" and item.get("id", "").casefold() == source.video_id.casefold()
                   for item in alerts["watch"]):
            alerts["watch"].append({"platform": "twitch", "id": source.video_id, "name": source.name})
        self.watch_generation += 1
        self.save()

    def live_changed(self, key, live):
        previous = self.watch_state.get(key)
        self.watch_state[key] = bool(live)
        if previous is False and live:
            self.play_sound(live=True)

    def poll_watches(self):
        if self.closed:
            return
        self.watch_timer = self.app.root.after(60000, self.poll_watches)
        watches = self.effective_watches()
        if not watches or self.watch_busy or not self.app.twitch_manager:
            return
        self.watch_busy = True
        generation = self.watch_generation
        def work():
            live = set()
            for offset in range(0, len(watches), 100):
                result = self.helix("streams", params={"user_login": [item["id"] for item in watches[offset:offset + 100]], "first": 100})
                live.update(item["user_login"].casefold() for item in result.get("data", []))
            return live
        def ready(live):
            self.watch_busy = False
            if generation != self.watch_generation:
                return
            current = {item["id"].casefold() for item in self.effective_watches()}
            for item in watches:
                login = item["id"].casefold()
                if login in current:
                    self.live_changed("twitch:" + login, login in live)
            self.watch_state = {key: value for key, value in self.watch_state.items() if key.removeprefix("twitch:") in current}
            self.last_watch_error = ""
        def failed(error):
            self.watch_busy = False
            self.last_watch_error = error
        self.async_action("watch", work, ready, failed)

    # -- Consistent compact dark controls and a single settings window. --
    def dialog(self, title, width=720, height=500):
        dialog = tk.Toplevel(self.app.root)
        dialog.title(tr(title))
        dialog.configure(bg=self.core.BG)
        dialog.transient(self.app.root)
        dialog.attributes("-topmost", self.app.top_var.get())
        dialog.geometry(f"{width}x{height}+{max(0, self.app.root.winfo_rootx() + 20)}+{max(0, self.app.root.winfo_rooty() + 50)}")
        titlebar_timer = dialog.after(30, lambda: self.core.style_titlebar(dialog) if dialog.winfo_exists() else None)
        dialog.bind("<Destroy>", lambda event: dialog.after_cancel(titlebar_timer) if event.widget is dialog else None, add="+")
        dialog.bind("<Escape>", lambda event: dialog.destroy())
        return dialog

    def label(self, parent, text, dim=False):
        return tk.Label(parent, text=tr(text), bg=self.core.BG, fg=self.core.DIM if dim else self.core.FG,
                        justify="left", anchor="w", font=("Segoe UI", 10), wraplength=495)

    def entry(self, parent, variable):
        return tk.Entry(parent, textvariable=variable, bg="#111111", fg=self.core.FG,
                        insertbackground=self.core.FG, relief="solid", bd=1, font=("Segoe UI", 10))

    def button(self, parent, text, command):
        return tk.Button(parent, text=tr(text), command=command, bg="#181818", fg=self.core.FG,
                         activebackground="#303030", activeforeground=self.core.FG,
                         relief="flat", padx=10, pady=4, font=("Segoe UI", 9))

    def check(self, parent, text, variable, command=None):
        return tk.Checkbutton(parent, text=tr(text), variable=variable, command=command,
                              bg=self.core.BG, fg=self.core.FG, selectcolor="#252525",
                              activebackground=self.core.BG, activeforeground=self.core.FG,
                              anchor="w", font=("Segoe UI", 10))

    def show_settings(self, section="Accounts"):
        if self.window and self.window.winfo_exists():
            self.window.lift()
            self.select_settings(section)
            return
        self.settings_ui = SettingsWindow(self, section)

    def select_settings(self, section):
        self.settings_ui.show(section)

    def account_status(self, text=None):
        if self.account_label and self.account_label.winfo_exists():
            auth = self.app.twitch_manager.auth if self.app.twitch_manager else {}
            status = text or tr("Twitch: {twitch}\nYouTube: {youtube}",
                twitch=(auth.get("login") or str(tr("not signed in")))
                    + (str(tr(" · actions enabled")) if set(TWITCH_ACTION_SCOPES).issubset(set(auth.get("scope", []))) else ""),
                youtube=self.google.auth.get("name") or str(tr("not signed in")))
            self.account_label.configure(text=tr(status))

    def accounts_page(self, parent):
        self.account_label = self.label(parent, "")
        self.account_label.pack(fill="x", pady=(0, 8))
        self.account_status()
        self.button(parent, "Twitch sign-in / permissions", lambda: self.ensure_twitch(
            set(TWITCH_ACTION_SCOPES), lambda: self.account_status("Twitch sending / moderation enabled."))).pack(anchor="w", pady=4)
        self.label(parent, "Approve additional Twitch permissions once to send, moderate, or manage your own broadcast.", dim=True).pack(anchor="w", pady=5)
        self.button(parent, "Choose Google OAuth JSON…", self.choose_google_file).pack(anchor="w", pady=4)
        self.button(parent, "Sign in to YouTube", lambda: self.google.sign_in(
            lambda: self.account_status())).pack(anchor="w", pady=4)
        self.button(parent, "Cancel pending Google sign-in", self.google.cancel.set).pack(anchor="w", pady=4)
        self.label(parent, "Use a Desktop OAuth client. Sign-in opens your browser; tokens are encrypted for your Windows account. Google apps in Testing may require signing in again after seven days.", dim=True).pack(anchor="w", pady=10)

        self.label(parent, "YouTube reading: no key, no sign-in, 0 units. Paste a live link with +.", dim=True).pack(anchor="w", pady=8)
        self.button(parent, "YouTube quota / remaining actions…", lambda: self.select_settings("YouTube quota")).pack(anchor="w", pady=4)
        self.button(parent, "Connection tutorial…", lambda: self.select_settings("Tutorial")).pack(anchor="w", pady=4)

    def choose_google_file(self):
        path = filedialog.askopenfilename(parent=self.window, title=tr("Google Desktop OAuth client"),
                                          filetypes=[("JSON", "*.json")])
        if path:
            previous = self.settings.get("google_client_file")
            self.settings["google_client_file"] = path
            try:
                self.google.client()
            except Exception as error:
                if previous:
                    self.settings["google_client_file"] = previous
                else:
                    self.settings.pop("google_client_file", None)
                self.notify(str(error), True)
                return
            self.save()
            self.account_status("Google Desktop OAuth file selected.")

    def change_language(self, value):
        if value == language():
            return
        set_language(value)
        self.app.cfg["language"] = language()
        self.save()
        refresh_ui()
        # Re-render cached history so app-authored status lines change language.
        # Connections and unsent drafts stay intact.
        for pane in self.app.display_panes():
            pane.refresh_header()
            pane.load_recent()
        self.app.update_quota()
        if self.window and self.window.winfo_exists():
            self.select_settings(self.settings_section)

    def quota_page(self, parent):
        self.label(parent, "YouTube reading is free of API quota. This estimate is for sending, moderation and editing.", dim=True).pack(anchor="w", pady=(0, 10))
        for key in ("remaining", "used", "messages", "deletions", "bans"):
            label = self.label(parent, "")
            label.pack(anchor="w", pady=3)
            self.quota_widgets[key] = label
        self.label(parent, "Each count is an alternative use of the same balance. Deletions allow for an ID lookup; first actions may need 1 extra unit.", dim=True).pack(anchor="w", pady=8)
        estimate = self.app.quota_estimate()
        row = tk.Frame(parent, bg=self.core.BG)
        row.pack(fill="x", pady=4)
        fields = {}
        for index, (key, label, value) in enumerate((("limit", "Daily project limit", estimate["limit"]),
                                                    ("external", "Units used outside this app today", estimate["external"]))):
            self.label(row, label).grid(row=index, column=0, sticky="w", pady=5)
            variable = tk.StringVar(value=str(value))
            fields[key] = variable
            self.entry(row, variable).grid(row=index, column=1, sticky="w", padx=10, pady=5)
        def apply():
            try:
                limit, external = int(fields["limit"].get()), int(fields["external"].get())
                if not 1 <= limit <= 10000000 or not 0 <= external <= 10000000:
                    raise ValueError()
            except ValueError:
                self.notify("Use whole numbers: limit 1–10,000,000; external usage 0–10,000,000.")
                return
            self.app.cfg["youtube_daily_limit"] = limit
            self.app.cfg["youtube_external_usage"] = {"date": self.core._pacific_date(), "used": external}
            self.save()
            self.app.update_quota()
        self.button(parent, "Apply quota estimate", apply).pack(anchor="e", pady=8)
        self.label(parent, "Local estimate, not Google's remaining balance. Other apps and people can use the same project's quota. Quota resets at midnight Pacific time.", dim=True).pack(anchor="w", pady=6)
        self.button(parent, "Open Google project quotas", lambda: self.open_url(
            "https://console.cloud.google.com/apis/api/youtube.googleapis.com/quotas")).pack(anchor="w", pady=4)
        self.refresh_quota()

    def refresh_quota(self):
        if not self.quota_widgets:
            return
        estimate = self.app.quota_estimate()
        snapshot = (language(), tuple(estimate.items()))
        if snapshot == self.quota_display:
            return
        self.quota_display = snapshot
        values = {
            "remaining": tr("Approximately {left} of {limit} units left", left=number(estimate["left"]), limit=number(estimate["limit"])),
            "used": tr("Tracked here: {used} · Outside this app: {external}", used=number(estimate["used"]), external=number(estimate["external"])),
            "messages": tr("Messages: about {count} left · 20 units each", count=number(estimate["messages"])),
            "deletions": tr("Message deletions: about {count} left · up to 26 units each", count=number(estimate["deletions"])),
            "bans": tr("Timeouts / bans: about {count} left · 200 units each", count=number(estimate["bans"]))}
        for key, text in values.items():
            widget = self.quota_widgets[key]
            if widget.winfo_exists():
                widget.configure(text=tr(text))

    def tutorial_page(self, parent):
        topics = (
            ("Connect Twitch", "1. Open Settings → Accounts → Twitch sign-in / permissions.\n2. Approve the code in your browser using your own Twitch account.\n3. Press Ctrl+T and enter @channel_name or a Twitch link.\n4. Type below the chat and press Enter. Extra permissions are requested when needed.\n\nNo developer account, Client ID or token needs to be pasted. Sign-in is saved for your Windows user."),
            ("Connect YouTube", "To read: press Ctrl+T and paste the LIVE VIDEO link. No API key or Google sign-in is needed.\n\nTo send or moderate:\n1. In Google Cloud, create a project and enable YouTube Data API v3.\n2. Configure Google Auth Platform. If the app is in Testing, add your Google account as a test user.\n3. Create an OAuth client of type Desktop app and download its JSON.\n4. In Accounts, choose that JSON, then Sign in to YouTube. Select the YouTube channel you use to chat or moderate.\n\nEach friend uses their own project / JSON. Never share your tokens."),
            ("Moderate / edit channels", "1. Sign in to the account that owns or moderates the channel (Accounts).\n2. Right-click a user's message → Moderation → delete, timeout or ban. Approve extra Twitch permissions if requested.\n3. Right-click the pane header → Channel actions to edit title / category.\n\nPlatforms check your permissions. Being a moderator does not automatically allow title/category editing. Twitch cannot delete the broadcaster's own messages through this API.\n\nYouTube actions consume quota; click the YT balance above the chats for an estimate. Reading stays at 0 units."),
            ("Tabs, splits and saved channels", "Ctrl+T adds chats; choose separate tabs, side by side, stacked or a mixed feed. Middle-click empty tab-bar space to add a chat.\n\nDrag tabs to reorder; scroll over tabs to switch. Drag dividers to resize panes. Right-click a pane header for layout actions.\n\nSave a channel from Channel actions; open it later in Settings → Saved channels. YouTube checks for current broadcasts only when you click Open.\n\nYouTube and Twitch stay connected in background tabs. Pause YouTube manually from Channel actions when needed. Ctrl+F5 reconnects the current pane."),
            ("Emotes, highlights and alerts", "Click ☺ beside Send to browse emotes. Tab / Shift+Tab completes emotes or @names. BTTV and 7TV need no extra sign-in.\n\nSettings → Appearance changes language, Always on top, badges and emote sizes.\n\nRight-click a user to highlight them; Settings → Highlights adds roles and words. Notifications controls sounds and Twitch live alerts. YouTube has no live alerts.\n\nCtrl+F searches a pane; Ctrl+Shift+F searches all chats. More keys are listed in Keyboard shortcuts.")
        )
        selector = ttk.Combobox(parent, state="readonly", values=[tr(title) for title, _ in topics], width=43)
        selector.current(0)
        selector.pack(fill="x", pady=(0, 12))
        content = self.label(parent, "")
        content.pack(fill="x", anchor="w", pady=4)
        buttons = tk.Frame(parent, bg=self.core.BG)
        buttons.pack(fill="x", pady=12)
        self.button(buttons, "Open Accounts", lambda: self.select_settings("Accounts")).pack(side="left", padx=(0, 5))
        guide_button = self.button(buttons, "Official setup guide", lambda: self.open_url(
            "https://developers.google.com/youtube/v3/guides/auth/installed-apps" if selector.current() == 1
            else "https://help.twitch.tv/s/article/twitch-chat-commands"))
        guide_button.pack(side="left")
        def show(event=None):
            body = str(tr(topics[selector.current()][1]))
            body = body.replace("Settings → Highlights", "Settings → Appearance → Highlights")
            body = body.replace("Configurações → Destaques", "Configurações → Aparência → Destaques")
            content.configure(text=body)
            guide_button.configure(state="normal" if selector.current() in (0, 1, 2) else "disabled")
        selector.bind("<<ComboboxSelected>>", show)
        show()

    def appearance_page(self, parent):
        row = tk.Frame(parent, bg=self.core.BG)
        row.pack(fill="x", pady=(0, 8))
        self.label(row, "Language / Idioma").pack(side="left", padx=(0, 12))
        languages = ("English", "Português (Brasil)")
        selector = ttk.Combobox(row, state="readonly", values=languages, width=22)
        selector.current(1 if language() == "pt-BR" else 0)
        selector.pack(side="left")
        selector.bind("<<ComboboxSelected>>", lambda event: self.change_language("pt-BR" if selector.current() == 1 else "en"))
        self.check(parent, "Always on top", self.app.top_var, self.app.toggle_top).pack(anchor="w", pady=(0, 8))
        variables = {}
        for key, label in (("compact_spacing", "Compact message spacing"), ("show_timestamps", "Show timestamps"),
                           ("alternate_rows", "Alternate message backgrounds")):
            variable = tk.BooleanVar(value=bool(self.app.appearance.get(key)))
            variables[key] = variable
            self.check(parent, label, variable).pack(anchor="w", pady=5)
        row = tk.Frame(parent, bg=self.core.BG)
        row.pack(fill="x", pady=10)
        sizes = {}
        for index, (key, label, default, low, high) in enumerate((
                ("message_font_size", "Message font size (pt)", 12, 7, 48),
                ("username_font_size", "Username font size (pt)", 12, 7, 48),
                ("badge_size", "Badge height (px)", 18, 10, 64),
                ("emote_size", "Emote height (px)", 36, 16, 128))):
            self.label(row, label).grid(row=index, column=0, sticky="w", pady=5)
            value = tk.StringVar(value=str(self.app.appearance.get(key, default)))
            sizes[key] = (value, low, high)
            tk.Spinbox(row, from_=low, to=high, textvariable=value, width=6, bg="#111111", fg=self.core.FG,
                       insertbackground=self.core.FG, buttonbackground="#202020").grid(row=index, column=1, padx=10)
        self.label(parent, "Text size: Ctrl++ / Ctrl+- · Ctrl+0 resets. Images keep their aspect ratio.", dim=True).pack(anchor="w", pady=8)
        formats = tk.Frame(parent, bg=self.core.BG)
        formats.pack(fill="x", pady=8)
        date_format = tk.StringVar(value=self.app.appearance.get("date_format", "DD/MM/YYYY"))
        time_format = tk.StringVar(value=self.app.appearance.get("time_format", "HH:mm"))
        for i, (label, variable, choices) in enumerate((("Date format", date_format, tuple(DATE_FORMATS)),
                                                       ("Timestamp format", time_format, TIME_FORMATS))):
            self.label(formats, label).grid(row=i, column=0, sticky="w", pady=5)
            ttk.Combobox(formats, state="readonly", values=choices, textvariable=variable, width=20).grid(row=i, column=1, padx=10)
        self.label(parent, "HH = 24-hour; hh with am/pm = 12-hour. Add :ss to show seconds.", dim=True).pack(anchor="w")
        baseline = {key: variable.get() for key, (variable, _, _) in sizes.items()}
        def refresh_page():
            for key, (variable, _, _) in sizes.items():
                actual = str(self.app.appearance.get(key, baseline[key]))
                if variable.get() == baseline[key]:
                    variable.set(actual)
                baseline[key] = actual
        parent.refresh_settings = refresh_page
        def apply():
            parsed = {}
            try:
                for key, (variable, low, high) in sizes.items():
                    value = int(variable.get())
                    if not low <= value <= high:
                        raise ValueError()
                    parsed[key] = value
            except ValueError:
                self.notify("Font sizes must be 7–48 pt; badges 10–64 px; emotes 16–128 px.")
                return
            self.app.apply_font_sizes(parsed.pop('message_font_size'), parsed.pop('username_font_size'), persist=False)
            self.app.appearance.update({key: variable.get() for key, variable in variables.items()})
            self.app.appearance.update(parsed)
            old_time = self.app.appearance.get("time_format", "HH:mm")
            self.app.appearance.update(date_format=date_format.get(), time_format=time_format.get())
            set_datetime_formats(self.app.appearance)
            self.app.cfg["appearance"] = self.app.appearance
            self.save()
            for pane in self.app.display_panes():
                pane.set_appearance(self.app.appearance)
                if old_time != time_format.get():
                    pane.refresh_timestamps()
            for card in tuple(self.chat_tools.cards.values()):
                card.refresh_messages()
                card.load()
            self.refresh_highlights()
            refresh_page()
            self.app.updater.render()
        self.button(parent, "Apply", apply).pack(anchor="e", pady=12)

    def highlights_page(self, parent):
        settings = self.settings["highlight"]
        self.label(parent, "Users · one username per line, optionally followed by #RRGGBB").pack(anchor="w")
        users = tk.Text(parent, height=5, bg="#111111", fg=self.core.FG, insertbackground=self.core.FG, relief="flat")
        users.pack(fill="x", pady=(4, 10))
        users.insert("1.0", "\n".join(name + " " + color for name, color in settings.get("users", {}).items()))
        role_vars = {}
        role_colors = {}
        role_frame = tk.Frame(parent, bg=self.core.BG)
        role_frame.pack(fill="x")
        for i, (role, label) in enumerate(ROLE_LABELS.items()):
            variable = tk.BooleanVar(value=role in settings.get("roles", {}))
            color = tk.StringVar(value=settings.get("roles", {}).get(role, "#227400"))
            role_vars[role], role_colors[role] = variable, color
            self.check(role_frame, label, variable).grid(row=i // 2, column=(i % 2) * 2, sticky="w")
            def pick(v=color):
                choice = colorchooser.askcolor(v.get(), parent=self.window)[1]
                if choice:
                    v.set(choice)
            self.button(role_frame, "Color", pick).grid(row=i // 2, column=(i % 2) * 2 + 1, padx=4, pady=2)
        self.label(parent, "Words / phrases · separate with commas").pack(anchor="w", pady=(8, 2))
        words = tk.StringVar(value=", ".join(settings.get("words", [])))
        self.entry(parent, words).pack(fill="x")
        mention = tk.BooleanVar(value=settings.get("mention_me", True))
        self.check(parent, "Highlight mentions of my signed-in names", mention).pack(anchor="w", pady=5)
        def apply():
            parsed = {}
            for line in users.get("1.0", "end").splitlines():
                parts = line.strip().split()
                if not parts:
                    continue
                color = parts[-1] if re.fullmatch(r"#[0-9A-Fa-f]{6}", parts[-1]) else "#227400"
                name = " ".join(parts[:-1] if parts[-1] == color and len(parts) > 1 else parts).lstrip("@")
                if name:
                    parsed[name.casefold()] = color
            settings.update(users=parsed, roles={role: role_colors[role].get() for role, variable in role_vars.items() if variable.get()},
                            words=[word.strip() for word in words.get().split(",") if word.strip()], mention_me=mention.get())
            self.save()
            self.refresh_highlights()
        self.button(parent, "Apply highlights", apply).pack(anchor="e", pady=10)

    def notifications_page(self, parent):
        settings = self.settings["alerts"]
        enabled = tk.BooleanVar(value=settings.get("enabled", True))
        mentions = tk.BooleanVar(value=settings.get("mention_sound", True))
        automatic = tk.BooleanVar(value=settings.get("all_twitch", True))
        self.check(parent, "Enable notification sounds", enabled).pack(anchor="w")
        self.check(parent, "Sound for matching mentions / highlight words", mentions).pack(anchor="w")
        self.check(parent, "Notify all open Twitch channels by default", automatic).pack(anchor="w")
        self.label(parent, "Additional nicknames · separate with commas").pack(anchor="w", pady=(6, 2))
        names = tk.StringVar(value=", ".join(self.settings.get("nicknames", [])))
        self.entry(parent, names).pack(fill="x")
        row = tk.Frame(parent, bg=self.core.BG)
        row.pack(fill="x", pady=7)
        for key, label in (("sound_file", "Mention sound…"), ("live_sound_file", "Live sound…")):
            def choose(target=key):
                path = filedialog.askopenfilename(parent=self.window, title=tr("Notification sound"), filetypes=[("WAV audio", "*.wav")])
                if path:
                    settings[target] = path
                    self.save()
            self.button(row, label, choose).pack(side="left", padx=(0, 5))
        self.button(row, "Test", lambda: self.play_sound(force=True)).pack(side="left")
        self.button(row, "Default", lambda: (settings.pop("sound_file", None), settings.pop("live_sound_file", None), self.save())).pack(side="left", padx=4)
        self.label(parent, "Default sound: ChatternoPing.wav · Live alerts: Twitch only", dim=True).pack(anchor="w")
        watch_list = tk.Listbox(parent, height=4, bg="#111111", fg=self.core.FG, bd=0, exportselection=False)
        watch_list.pack(fill="x", pady=5)
        displayed = []
        def refresh():
            displayed[:] = self.effective_watches()
            watch_list.delete(0, "end")
            for item in displayed:
                watch_list.insert("end", item.get("name", item["id"]) + (" · automatic" if item.get("automatic") else ""))
        refresh()
        row2 = tk.Frame(parent, bg=self.core.BG)
        row2.pack(fill="x")
        sources = [source for source in self.app.sources.values() if source.platform == "twitch"]
        combo = ttk.Combobox(row2, state="readonly", values=[source.name for source in sources])
        combo.pack(side="left", fill="x", expand=True)
        if sources:
            combo.current(0)
        def add():
            if combo.current() >= 0:
                self.add_watch_source(sources[combo.current()])
                refresh()
        def remove():
            selected = watch_list.curselection()
            if selected:
                login = displayed[selected[0]]["id"]
                settings.setdefault("excluded", []).append(login.casefold())
                settings["watch"] = [item for item in settings["watch"] if item.get("id", "").casefold() != login.casefold()]
                self.watch_generation += 1
                self.save()
                refresh()
        self.button(row2, "Enable", add).pack(side="left", padx=4)
        self.button(row2, "Disable", remove).pack(side="left")
        self.label(parent, "Twitch checks run once a minute while the app is open. Alerts trigger on offline → live, with no sound on initial detection.", dim=True).pack(anchor="w", pady=8)
        def apply():
            settings.update(enabled=enabled.get(), mention_sound=mentions.get(), all_twitch=automatic.get())
            self.settings["nicknames"] = [name.strip().lstrip("@") for name in names.get().split(",") if name.strip()]
            self.watch_generation += 1
            self.save()
            self.refresh_highlights()
            refresh()
        self.button(parent, "Apply notifications", apply).pack(anchor="e", pady=4)
        def refresh_page():
            selected = sources[combo.current()].key if 0 <= combo.current() < len(sources) else None
            sources[:] = [source for source in self.app.sources.values() if source.platform == "twitch"]
            combo.configure(values=[source.name for source in sources])
            if sources:
                combo.current(next((i for i, source in enumerate(sources) if source.key == selected), 0))
            else:
                combo.set("")
            refresh()
        parent.refresh_settings = refresh_page

    def shortcuts_page(self, parent):
        self.label(parent, "Ctrl+T   Add chats\nCtrl+Shift+T   Add a split\nMiddle-click empty tab bar   Add chats\n"
                   "Ctrl+W   Close pane; Ctrl+Shift+W   Close tab\nCtrl+Tab / Ctrl+Shift+Tab   Next / previous tab\n"
                   "Ctrl+1–8   Select tab; Ctrl+9   Last tab\nCtrl+F / Ctrl+Shift+F   Search pane / all chats\n"
                   "Ctrl+F5   Reconnect pane\nCtrl+K   Quick switch\nCtrl++ / Ctrl+- / Ctrl+0   Text size\n"
                   "Ctrl+P   Settings\nAlt+Arrows   Focus another pane\nCtrl+Alt+M   Mix channels\n"
                   "Enter in message box   Send to selected channel\nTab / Shift+Tab   Complete / cycle emotes or @names\n"
                   "Drag a tab with the left mouse button   Reorder tabs\nDouble-click pane header   Focus / restore panes").pack(anchor="w")


    def effective_watches(self):
        alerts = self.settings["alerts"]
        excluded = {str(value).casefold() for value in alerts.get("excluded", [])}
        watched = {}
        for item in alerts.get("watch", []):
            if item.get("platform") == "twitch" and item.get("id", "").casefold() not in excluded:
                watched[item["id"].casefold()] = dict(item)
        if alerts.get("all_twitch", True):
            for source in self.app.sources.values():
                if source.platform == "twitch" and source.video_id.casefold() not in excluded:
                    watched[source.video_id.casefold()] = {"platform": "twitch", "id": source.video_id,
                                                          "name": source.name, "automatic": True}
        return list(watched.values())

    def source_header(self, source, value):
        if source.platform == "twitch" and "live" in value and any(
                item["id"].casefold() == source.video_id.casefold() for item in self.effective_watches()):
            self.live_changed("twitch:" + source.video_id.casefold(), value["live"])

    def default_sound_path(self):
        return Path(self.core.__file__).resolve().with_name("ChatternoPing.wav")

    def available_emotes(self, source):
        result = dict(source.emote_map)
        result.update(getattr(source, "youtube_emotes", {}))
        cached = self.emote_sets.get((source.key, self._emote_account_key()))
        if cached:
            for category in ("Global", "Channel", "Subs"):
                result.update(cached[1].get(category, {}))
        return result

    def _emote_account_key(self):
        auth = self.app.twitch_manager.auth if self.app.twitch_manager else {}
        return auth.get("login", ""), "user:read:emotes" in auth.get("scope", [])

    def load_picker_emotes(self, source):
        cache_key = (source.key, self._emote_account_key())
        cached = self.emote_sets.get(cache_key)
        if cached and time.monotonic() - cached[0] < 1800:
            cached[1]["Channel"].update(getattr(source, "youtube_emotes", {}))
            return cached[1]
        catalogs = {"Subs": {}, "Channel": {}, "Global": {}, "Emojis": emoji_catalog()}
        emotes = self.core.EMOTE_CATALOG.load(source.platform, source.emote_channel_id)
        for code, meta in emotes.items():
            catalogs["Global" if meta.get("scope") == "global" else "Channel"][code] = meta
        catalogs["Channel"].update(getattr(source, "youtube_emotes", {}))
        if source.platform == "twitch" and self.app.twitch_manager:
            try:
                global_set = self.helix("chat/emotes/global")
                catalogs["Global"].update(native_emotes(global_set.get("data", []), "global"))
                if self._emote_account_key()[1]:
                    identity = self.twitch_identity()
                    cursor, visited, records = None, set(), []
                    broadcaster = self.broadcaster(source)
                    while True:
                        params = {"user_id": identity["user_id"], "broadcaster_id": broadcaster}
                        if cursor:
                            params["after"] = cursor
                        response = self.helix("chat/emotes/user", params=params, scope="user:read:emotes")
                        records.extend(response.get("data", []))
                        cursor = response.get("pagination", {}).get("cursor")
                        if not cursor or cursor in visited:
                            break
                        visited.add(cursor)
                        if len(records) > 20000:
                            raise RuntimeError("The Twitch emote list is too large.")
                    for code, meta in native_emotes(records, "user").items():
                        destination = "Global" if meta.get("owner_id") in ("", "0") else (
                            "Channel" if meta.get("owner_id") == broadcaster and meta.get("emote_type") == "follower" else "Subs")
                        catalogs[destination][code] = meta
            except Exception as error:
                catalogs["error"] = str(error)
        self.emote_sets[cache_key] = (time.monotonic(), catalogs)
        while len(self.emote_sets) > 24:
            self.emote_sets.pop(next(iter(self.emote_sets)))
        return catalogs

    def show_emotes(self, pane):
        index = pane.target_combo.current()
        if index < 0 or index >= len(pane.sources):
            return
        source = pane.sources[index]
        previous = self.pickers.get(source.key)
        if previous and not previous.closed:
            previous.pane = pane
            previous.window.lift()
            return
        self.pickers[source.key] = EmotePicker(self, pane, source)

    def insert_emote(self, pane, source, code):
        if pane.closed or source not in pane.sources:
            self.notify("The destination chat was closed.")
            return
        pane.target_combo.current(pane.sources.index(source))
        self.update_send_label(pane)
        entry = pane.compose_entry
        if entry.selection_present():
            entry.delete("sel.first", "sel.last")
        cursor = entry.index("insert")
        text = pane.compose_text.get()
        before = " " if cursor and not text[cursor - 1].isspace() else ""
        after = "" if cursor < len(text) and text[cursor].isspace() else " "
        entry.insert(cursor, before + code + after)
        entry.icursor(cursor + len(before + code + after))
        pane.completion = None

    def bind_tab_drag(self, workspace, widgets):
        def press(event):
            self.tab_drag = {"tab": workspace, "x": event.x_root, "moved": False}
            self.app.select(workspace)
        def move(event):
            state = self.tab_drag
            if not state or state["tab"] is not workspace or workspace.closed:
                return
            if not state["moved"] and abs(event.x_root - state["x"]) < 6:
                return
            state["moved"] = True
            canvas = self.app.tabs_canvas
            relative = event.x_root - canvas.winfo_rootx()
            if relative < 15:
                canvas.xview_scroll(-1, "units")
            elif relative > canvas.winfo_width() - 15:
                canvas.xview_scroll(1, "units")
            others = [tab for tab in self.app.tabs if tab is not workspace]
            target = sum(event.x_root > tab.btn["frame"].winfo_rootx() + tab.btn["frame"].winfo_width() / 2 for tab in others)
            if self.app.tabs.index(workspace) != target:
                self.app.tabs.remove(workspace)
                self.app.tabs.insert(target, workspace)
                for tab in self.app.tabs:
                    tab.btn["frame"].pack_forget()
                    tab.btn["frame"].pack(side="left")
                self.app.root.update_idletasks()
        def release(event):
            if self.tab_drag and self.tab_drag.get("moved"):
                self.app.schedule_session_save()
            self.tab_drag = None
        for widget in widgets:
            widget.bind("<Button-1>", press)
            widget.bind("<B1-Motion>", move)
            widget.bind("<ButtonRelease-1>", release)

    def saved_channels_page(self, parent):
        self.label(parent, "Save channel identities; YouTube streams are resolved when you open one.", dim=True).pack(anchor="w", pady=(0, 8))
        listing = tk.Listbox(parent, height=8, bg="#111111", fg=self.core.FG,
                             selectbackground="#303030", bd=0, exportselection=False)
        listing.pack(fill="both", expand=True)
        saved = self.app.cfg.setdefault("saved_channels", [])
        status = self.label(parent, "")
        def refresh():
            listing.delete(0, "end")
            for item in saved:
                platform = item.get("platform", "youtube")
                listing.insert("end", ("TW · " if platform == "twitch" else "YT · ") + item.get("title", item.get("channel_id", "")))
        def current():
            chosen = listing.curselection()
            return saved[chosen[0]] if chosen and chosen[0] < len(saved) else None
        row = tk.Frame(parent, bg=self.core.BG)
        row.pack(fill="x", pady=7)
        self.button(row, "Open", lambda: self.open_saved(current(), status, parent)).pack(side="left")
        def remove():
            item = current()
            if item:
                saved.remove(item)
                self.save()
                refresh()
        self.button(row, "Remove", remove).pack(side="left", padx=5)
        listing.bind("<Double-Button-1>", lambda event: self.open_saved(current(), status, parent))
        open_sources = list(self.app.sources.values())
        combo = ttk.Combobox(parent, state="readonly", values=[source.platform.upper() + " · " + source.name for source in open_sources])
        combo.pack(fill="x", pady=(4, 3))
        if open_sources:
            combo.current(0)
        def save_open():
            if combo.current() >= 0:
                result = self.save_source(open_sources[combo.current()])
                status.config(text=tr(result))
                refresh()
        self.button(parent, "Save selected open channel", save_open).pack(anchor="w", pady=(2, 9))
        input_row = tk.Frame(parent, bg=self.core.BG)
        input_row.pack(fill="x")
        platform = ttk.Combobox(input_row, state="readonly", values=("YouTube", "Twitch"), width=10)
        platform.current(0)
        platform.pack(side="left", padx=(0, 5))
        reference = tk.StringVar()
        self.entry(input_row, reference).pack(side="left", fill="x", expand=True)
        def add():
            text = reference.get().strip()
            if platform.get() == "Twitch":
                login = self.core.twitch_login_from(text)
                if not login:
                    status.config(text=tr("Enter a Twitch channel URL or @username."))
                    return
                status.config(text=tr(self.save_channel({"platform": "twitch", "channel_id": login, "title": login})))
                refresh()
                return
            kind, value = self.app.parse_channel_reference(text)
            if not kind:
                status.config(text=tr("Enter a YouTube channel URL, @handle, or UC channel ID."))
                return
            def resolve():
                return YouTubeWebClient().resolve_channel(kind, value)
            def ready(item):
                if parent.winfo_exists():
                    status.config(text=tr(self.save_channel(item)))
                    refresh()
            self.async_action("save-channel", resolve, ready)
        self.button(input_row, "Add", add).pack(side="left", padx=(5, 0))
        status.pack(anchor="w", pady=6)
        self.label(parent, "YouTube checks happen only when you click Open. Multiple live broadcasts can be selected individually. Twitch opens even while offline.", dim=True).pack(anchor="w")
        def refresh_page():
            selected = combo.current()
            old_key = open_sources[selected].key if 0 <= selected < len(open_sources) else None
            open_sources[:] = list(self.app.sources.values())
            combo.configure(values=[source.platform.upper() + " · " + source.name for source in open_sources])
            if open_sources:
                combo.current(next((i for i, source in enumerate(open_sources) if source.key == old_key), 0))
            refresh()
        parent.refresh_settings = refresh_page
        refresh_page()

    def save_channel(self, channel):
        items = self.app.cfg.setdefault("saved_channels", [])
        if any(item.get("platform", "youtube") == channel["platform"] and item.get("channel_id") == channel["channel_id"] for item in items):
            return "That channel is already saved."
        items.append(channel)
        self.save()
        return "Saved " + channel["title"] + "."

    def save_source(self, source):
        channel_id = source.video_id if source.platform == "twitch" else source.header_info.get("channel_id")
        if not channel_id:
            return "Wait for the YouTube channel details to load."
        return self.save_channel({"platform": source.platform, "channel_id": channel_id, "title": source.name})

    def open_saved(self, item, status, parent):
        if not item:
            status.config(text=tr("Select a saved channel first."))
            return
        if item.get("platform", "youtube") == "twitch":
            self.app.open_twitch(item["channel_id"])
            status.config(text=tr("Opened " + item.get("title", item["channel_id"]) + "."))
            return
        status.config(text=tr("Looking for current live broadcasts…"))
        def lookup():
            return YouTubeWebClient().live_streams(item["channel_id"])
        def ready(entries):
            if not parent.winfo_exists():
                return
            streams = [entry for entry in entries if entry.get("id", {}).get("videoId")]
            if not streams:
                status.config(text=tr("This YouTube channel is not live right now."))
            elif len(streams) == 1:
                self.app.open_stream(streams[0]["id"]["videoId"])
                status.config(text=tr("Opened current broadcast."))
            else:
                dialog = self.dialog("Choose a live broadcast", 520, 300)
                choices = tk.Listbox(dialog, bg="#111111", fg=self.core.FG, bd=0, exportselection=False)
                choices.pack(fill="both", expand=True, padx=12, pady=12)
                for stream in streams:
                    choices.insert("end", stream.get("snippet", {}).get("title", "Live broadcast"))
                def choose(event=None):
                    selected = choices.curselection()
                    if selected:
                        self.app.open_stream(streams[selected[0]]["id"]["videoId"])
                        dialog.destroy()
                self.button(dialog, "Open", choose).pack(anchor="e", padx=12, pady=(0, 12))
                choices.bind("<Double-Button-1>", choose)
                choices.selection_set(0)
        self.async_action(("saved-live", item["channel_id"]), lookup, ready)


def fit_photo(photo, max_height, max_width):
    """Bound Tk's intermediate image while preserving aspect ratio and alpha."""
    from fractions import Fraction
    width, height = photo.width(), photo.height()
    scale = min(max_height / max(1, height), max_width / max(1, width))
    ratio = Fraction(scale).limit_denominator(1024)
    maximum = max(1, min(16, 2048 // max(width, height, 1)))
    if ratio.numerator > maximum:
        options = [(numerator, max(1, round(numerator / scale))) for numerator in range(1, maximum + 1)]
        numerator, denominator = min(options, key=lambda pair: abs(pair[0] / pair[1] - scale))
    else:
        numerator, denominator = ratio.numerator, ratio.denominator
    if numerator == denominator:
        return photo.copy()
    return photo.zoom(numerator, numerator).subsample(denominator, denominator)


def native_emotes(records, scope):
    result = {}
    for item in records:
        if not item.get("id") or not item.get("name"):
            continue
        eid = str(item["id"])
        fmt = "animated" if "animated" in item.get("format", []) else "static"
        result[item["name"]] = {"urls": (f"https://static-cdn.jtvnw.net/emoticons/v2/{eid}/{fmt}/dark/2.0",
                                                f"https://static-cdn.jtvnw.net/emoticons/v2/{eid}/static/dark/2.0"),
                                "source": "twitch", "scope": scope, "id": eid,
                                "owner_id": str(item.get("owner_id") or "0"), "emote_type": item.get("emote_type", "")}
    return result


def emoji_catalog():
    import unicodedata
    result = {}
    for start, end in ((0x1F300, 0x1FB00), (0x2600, 0x27C0)):
        for codepoint in range(start, end):
            char = chr(codepoint)
            if unicodedata.category(char) == "So":
                name = unicodedata.name(char, "").lower()
                if name:
                    result[char] = {"source": "emoji", "label": name, "urls": ()}
    return result


class EmotePicker:
    PAGE_SIZE = 72

    def __init__(self, controller, pane, source):
        self.owner, self.pane, self.source = controller, pane, source
        self.core = controller.core
        self.closed = False
        self.active = True
        self.image_profile = "picker"
        self.fonts = controller.app.fonts
        self.appearance = {"emote_size": 36, "badge_size": 18}
        self.used_assets = set()
        self.photos = {}
        self.tiles = {}
        self.pending = set()
        self.q = self.core.EventQueue()
        self.generation = 0
        self.search_timer = None
        self.tick_timer = None
        self.category = "Channel"
        self.page = 0
        self.catalogs = {"Subs": {}, "Channel": {}, "Global": {}, "Emojis": emoji_catalog()}
        self.window = controller.dialog("Emotes in #" + source.name, 570, 440)
        self.window.minsize(370, 300)
        self.window.protocol("WM_DELETE_WINDOW", self.close)
        self.window.bind("<Escape>", lambda event: self.close())
        self.window.bind("<Unmap>", lambda event: self.set_active(False) if event.widget is self.window else None)
        self.window.bind("<Map>", lambda event: self.set_active(True) if event.widget is self.window else None)
        self.query = tk.StringVar()
        entry = controller.entry(self.window, self.query)
        entry.pack(fill="x", padx=8, pady=(8, 4))
        self.query.trace_add("write", lambda *args: self.schedule_search())
        self.footer = tk.Frame(self.window, bg=controller.core.BG)
        self.footer.pack(side="bottom", fill="x", padx=7, pady=5)
        self.tab_buttons = {}
        for name in ("Subs", "Channel", "Global", "Emojis"):
            button = controller.button(self.footer, name, lambda value=name: self.choose_tab(value))
            button.pack(side="left", padx=1)
            self.tab_buttons[name] = button
        controller.button(self.footer, "›", lambda: self.change_page(1)).pack(side="right")
        controller.button(self.footer, "‹", lambda: self.change_page(-1)).pack(side="right")
        self.info = controller.label(self.window, "Loading emotes…", dim=True)
        self.info.pack(side="bottom", fill="x", padx=8, pady=3)
        self.permission = controller.button(self.window, "Enable Twitch subscriber emotes…", self.authorize)
        body = tk.Frame(self.window, bg=controller.core.BG)
        body.pack(fill="both", expand=True)
        self.canvas = tk.Canvas(body, bg=controller.core.BG, highlightthickness=0)
        scroll = ttk.Scrollbar(body, orient="vertical", command=self.canvas.yview, style=controller.app.sb_style)
        self.canvas.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.canvas.pack(side="left", fill="both", expand=True)
        self.grid = tk.Frame(self.canvas, bg=controller.core.BG)
        self.grid_window = self.canvas.create_window((0, 0), window=self.grid, anchor="nw")
        self.grid.bind("<Configure>", lambda event: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self.canvas.bind("<Configure>", self.resize)
        self.window.bind("<MouseWheel>", lambda event: self.canvas.yview_scroll(-int(event.delta / 120), "units"))
        self.columns = 7
        self.load()
        self.tick()
        entry.focus_set()

    def _resize_emote_photo(self, photo):
        return fit_photo(photo, 36, 54)

    def authorize(self):
        self.owner.ensure_twitch({"user:read:emotes"}, lambda: self.load(refresh=True))

    def load(self, refresh=False):
        if self.closed:
            return
        if refresh:
            self.owner.emote_sets.pop((self.source.key, self.owner._emote_account_key()), None)
        self.info.config(text=tr("Loading emotes…"))
        def ready(catalogs):
            if self.closed:
                return
            self.catalogs = catalogs
            self.render()
        def failed(error):
            if not self.closed:
                self.info.config(text=tr(error))
        self.owner.async_action(("emote-picker", id(self)), lambda: self.owner.load_picker_emotes(self.source), ready, failed)

    def schedule_search(self):
        if self.search_timer:
            self.window.after_cancel(self.search_timer)
        def run():
            self.search_timer = None
            self.page = 0
            self.render()
        self.search_timer = self.window.after(120, run)

    def choose_tab(self, name):
        self.category = name
        self.page = 0
        self.render()

    def resize(self, event):
        self.canvas.itemconfigure(self.grid_window, width=event.width)
        columns = max(3, event.width // 72)
        if columns != self.columns:
            self.columns = columns
            self.schedule_search()

    def change_page(self, direction):
        self.page = max(0, self.page + direction)
        self.render()
        self.canvas.yview_moveto(0)

    def render(self):
        if self.closed:
            return
        self.generation += 1
        for child in self.grid.winfo_children():
            child.destroy()
        self.tiles.clear()
        self.photos.clear()
        self.used_assets.clear()
        for name, button in self.tab_buttons.items():
            button.config(bg="#303030" if name == self.category else "#181818")
        needle = self.query.get().strip().casefold()
        if needle:
            catalog = {}
            for category in ("Subs", "Channel", "Global", "Emojis"):
                catalog.update(self.catalogs.get(category, {}))
        else:
            catalog = self.catalogs.get(self.category, {})
        records = [(code, meta) for code, meta in catalog.items()
                   if not needle or needle in code.casefold() or needle in meta.get("label", "").casefold()]
        records.sort(key=lambda entry: entry[1].get("label", entry[0]).casefold())
        pages = max(1, (len(records) + self.PAGE_SIZE - 1) // self.PAGE_SIZE)
        self.page = min(self.page, pages - 1)
        can_user = self.source.platform == "twitch" and self.owner._emote_account_key()[1]
        if self.category == "Subs" and self.source.platform == "twitch" and not can_user:
            self.permission.pack(side="bottom", fill="x", padx=8, pady=3)
        else:
            self.permission.pack_forget()
        self.info.config(text=tr(f"{len(records):,} emotes · page {self.page + 1}/{pages} · click to insert"))
        shown = records[self.page * self.PAGE_SIZE:(self.page + 1) * self.PAGE_SIZE]
        if not shown:
            message = "No matching emotes." if needle else ("Enable Twitch emote access to load your unlocked emotes." if self.category == "Subs" else "No emotes in this section.")
            self.owner.label(self.grid, message, dim=True).grid(row=0, column=0, padx=10, pady=20)
        generation = self.generation
        for index, (code, meta) in enumerate(shown):
            tile = tk.Frame(self.grid, bg=self.core.BG, width=70, height=69)
            tile.grid(row=index // self.columns, column=index % self.columns, padx=1, pady=2)
            tile.pack_propagate(False)
            button = tk.Button(tile, text=tr(code if meta.get("source") == "emoji" else "…"), bg=self.core.BG,
                               fg=self.core.FG, activebackground="#292929", activeforeground=self.core.FG,
                               bd=0, font=("Segoe UI Emoji", 22), command=lambda value=code: self.insert(value))
            button.pack(fill="both", expand=True)
            label = self.owner.label(tile, code if len(code) <= 11 else code[:10] + "…", dim=True)
            label.config(font=("Segoe UI", 8), wraplength=65)
            label.pack()
            label.bind("<Button-1>", lambda event, value=code: self.insert(value))
            for widget in (tile, button, label):
                widget.bind("<Enter>", lambda event, value=code, data=meta: self.info.config(text=tr(data.get("label", value) + " · " + data.get("source", ""))))
            urls = tuple(meta.get("urls") or ())
            if urls:
                self.tiles.setdefault(urls, []).append(button)
                self.used_assets.add(urls)
                self.core.EMOTE_IMAGES.request(urls, lambda payload, key=urls, gen=generation: self.q.put((gen, key, payload)))

    def insert(self, code):
        self.owner.insert_emote(self.pane, self.source, code)
        self.window.lift()

    def tick(self):
        if self.closed:
            return
        for _ in range(6):
            try:
                generation, urls, payload = self.q.get_nowait()
            except queue.Empty:
                break
            if generation != self.generation or not payload:
                continue
            try:
                if isinstance(payload, dict) and len(payload.get("frames", [])) > 48:
                    frames, delays = payload["frames"], payload.get("delays", [])
                    step = (len(frames) + 47) // 48
                    payload = dict(payload, frames=frames[::step], delays=[sum(delays[i:i + step]) for i in range(0, len(delays), step)])
                photo = self.owner.app.photo_bank.emote(urls, payload, self)
                self.photos[urls] = photo
                for button in self.tiles.get(urls, []):
                    if button.winfo_exists():
                        button.config(image=photo, text="")
            except (tk.TclError, ValueError):
                continue
        self.tick_timer = self.window.after(60, self.tick)

    def close(self):
        if self.closed:
            return
        self.closed = True
        self.active = False
        self.used_assets.clear()
        for timer in (self.search_timer, self.tick_timer):
            if timer:
                self.window.after_cancel(timer)
        self.window.destroy()

    def set_active(self, active):
        self.active = active and not self.closed
        if self.active:
            self.owner.app.photo_bank.wake()
