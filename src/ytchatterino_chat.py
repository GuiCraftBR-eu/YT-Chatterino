"""Replies, public user cards and bounded recent history (v1.2)."""
import base64
from collections import OrderedDict, deque
from datetime import datetime
import io
import json
import re
import tkinter as tk
from tkinter import simpledialog, ttk
import urllib.parse
import urllib.request

from ytchatterino_i18n import tr, display_time, display_date, number

DEFAULT_MESSAGE_LIMIT = 2000
MIN_MESSAGE_LIMIT, MAX_MESSAGE_LIMIT = 100, 20000
HISTORY_URL = "https://recent-messages.robotty.de/api/v2/recent-messages/"


def message_limit(cfg, key=None):
    value = cfg.get("chat_message_limits", {}).get(key, cfg.get("message_limit", DEFAULT_MESSAGE_LIMIT))
    try:
        return min(MAX_MESSAGE_LIMIT, max(MIN_MESSAGE_LIMIT, int(value)))
    except (TypeError, ValueError):
        return DEFAULT_MESSAGE_LIMIT


def parse_privmsg(line, decode_tags, parse_badges, parse_emotes, parse_gifs):
    """One parser for live IRC and the public recent-message service."""
    tags = {}
    if line.startswith("@"):
        raw, _, line = line.partition(" ")
        tags = decode_tags(raw[1:])
    if not line.startswith(":"):
        return None
    prefix, _, line = line[1:].partition(" ")
    command, _, params = line.partition(" ")
    target, separator, body = params.partition(" :")
    if command != "PRIVMSG" or not separator:
        return None
    channel = target.lstrip("#").lower()
    login = prefix.split("!", 1)[0]
    if body.startswith("\x01ACTION "):
        body = body[8:-1] if body.endswith("\x01") else body[8:]
    badges = parse_badges(tags.get("badges", ""))
    labels = {"broadcaster": "[HOST]", "moderator": "[MOD]", "vip": "[VIP]",
              "founder": "[FOUNDER]", "subscriber": "[SUB]"}
    try:
        stamp = int(tags.get("tmi-sent-ts") or tags.get("rm-received-ts") or 0) / 1000
    except (TypeError, ValueError):
        stamp = 0
    date = datetime.fromtimestamp(stamp) if stamp else datetime.now()
    reply = None
    if tags.get("reply-parent-msg-id"):
        reply = {"id": tags["reply-parent-msg-id"], "user_id": tags.get("reply-parent-user-id"),
                 "login": tags.get("reply-parent-user-login", ""),
                 "name": tags.get("reply-parent-display-name") or tags.get("reply-parent-user-login", ""),
                 "text": tags.get("reply-parent-msg-body", "")}
    return channel, {"id": tags.get("id"), "name": tags.get("display-name") or login,
        "login": login, "user_id": tags.get("user-id"), "room_id": tags.get("room-id"),
        "time": date.strftime("%H:%M:%S"), "sent_at": stamp or date.timestamp(),
        "color": tags.get("color", ""), "text": body, "special": None,
        "badge_ids": badges, "badge_info": tags.get("badge-info", ""),
        "badges": [labels[b["set_id"]] for b in badges if b["set_id"] in labels],
        "native_emotes": parse_emotes(tags.get("emotes"), body),
        "gifs": parse_gifs(tags.get("gifs"), body), "reply": reply}


def recent_twitch(channel, limit, parser):
    """No account credentials are sent to this independent public service."""
    if not re.fullmatch(r"[a-z0-9_]{1,25}", channel):
        raise ValueError("Invalid Twitch channel.")
    params = urllib.parse.urlencode({"limit": min(1000, max(1, limit)),
        "hide_moderated_messages": "true", "hide_moderation_messages": "true"})
    request = urllib.request.Request(HISTORY_URL + channel + "?" + params,
                                     headers={"User-Agent": "YT-Chatterino/1.2.0"})
    with urllib.request.urlopen(request, timeout=8) as response:
        raw = response.read(4 * 1024 * 1024 + 1)
    if len(raw) > 4 * 1024 * 1024:
        raise ValueError("History response is too large.")
    data = json.loads(raw)
    result, seen = [], set()
    for line in data.get("messages", [])[-limit:]:
        if not isinstance(line, str):
            continue
        parsed = parser(line)
        if not parsed or parsed[0] != channel:
            continue
        message = parsed[1]
        if not message.get("id") or message["id"] in seen:
            continue
        seen.add(message["id"])
        message["_history"] = True
        result.append(message)
    return result


def same_user(message, target):
    if message.get("user_id") and target.get("user_id"):
        return message["user_id"] == target["user_id"]
    return (message.get("login") or message.get("name", "")).casefold() == (
        target.get("login") or target.get("name", "")).casefold()


class ChatTools:
    def __init__(self, owner):
        self.owner, self.app, self.core = owner, owner.app, owner.core
        self.cards = OrderedDict()
        self.profiles = OrderedDict()

    def attach_reply_bar(self, pane):
        pane.reply_target = None
        pane.reply_bar = tk.Frame(pane, bg="#202020", padx=6, pady=3)
        pane.reply_label = tk.Label(pane.reply_bar, bg="#202020", fg=self.core.DIM,
                                    anchor="w", font=("Segoe UI", 9))
        pane.reply_label.pack(side="left", fill="x", expand=True)
        tk.Button(pane.reply_bar, text="×", bg="#202020", fg=self.core.FG, bd=0,
                  command=lambda: self.cancel_reply(pane)).pack(side="right")
        pane.compose_entry.bind("<Escape>", lambda event: self.cancel_reply(pane))

    def start_reply(self, pane, source, message):
        if source.platform != "twitch" or not message.get("id") or message.get("deleted"):
            return
        pane.target_combo.current(pane.sources.index(source))
        pane.reply_target = (source.key, dict(message))
        pane.reply_label.configure(text=tr("Replying to @{name}: {body}",
            name=message.get("login") or message["name"], body=message.get("text", "")[:100]))
        pane.reply_bar.pack(side="bottom", fill="x", after=pane.composer)
        self.owner.update_send_label(pane)
        pane.compose_entry.focus_set()

    def cancel_reply(self, pane):
        pane.reply_target = None
        pane.reply_bar.pack_forget()
        return "break"

    def changed_target(self, pane):
        reply = getattr(pane, "reply_target", None)
        i = pane.target_combo.current()
        if reply and (i < 0 or i >= len(pane.sources) or pane.sources[i].key != reply[0]):
            self.cancel_reply(pane)

    def record_at(self, pane, index):
        for tag in pane.text.tag_names(index):
            if tag.startswith("message_"):
                return pane.message_records.get(int(tag[8:]))
        return None

    def click_user(self, pane, event):
        index = pane.text.index(f"@{event.x},{event.y}")
        if "name" in pane.text.tag_names(index):
            record = self.record_at(pane, index)
            if record:
                self.show_user(pane, *record)
                return "break"

    def source_history(self, source, parser):
        if source.platform != "twitch" or source.history_requested:
            return
        source.history_requested = True
        if not self.app.cfg.get("load_twitch_history", True):
            return
        source.history_loading = True
        def complete(value=None, error=None):
            if source.closed:
                return
            source.history_loading = False
            buffered = list(source.history_live)
            source.history_live.clear()
            for item in value or []:
                source.publish("msg", item)
            for item in buffered:
                source.publish("msg", item)
            if error:
                source.publish("status", "Recent Twitch history unavailable; live chat is connected independently.")
            elif not value:
                source.publish("status", "No recent Twitch messages available for this channel.")
        self.owner.async_action(("history", source.key),
            lambda: recent_twitch(source.channel_login, min(200, message_limit(self.app.cfg)), parser),
            lambda result: complete(result), lambda error: complete(error=error))

    def apply_limits(self):
        for pane in self.app.all_panes():
            cap = message_limit(self.app.cfg, pane.setting_key)
            pane.pending_events = deque(pane.pending_events, maxlen=cap)
            pane.text.configure(state="normal")
            pane.trim_messages()
            pane.text.configure(state="disabled")
        for source in self.app.sources.values():
            cap = max([message_limit(self.app.cfg)] + [message_limit(self.app.cfg, p.setting_key) for p in source.views])
            source.history = deque(source.history, maxlen=cap)
            source.history_live = deque(source.history_live, maxlen=cap)
            while len(source.seen) > cap * 2:
                source.seen.popitem(last=False)
        for card in tuple(self.cards.values()):
            card.refresh_messages()

    def pane_limit(self, pane):
        value = simpledialog.askinteger(tr("Message limit"), tr("Retained messages in this pane (100–20000):"),
            initialvalue=message_limit(self.app.cfg, pane.setting_key), minvalue=100, maxvalue=20000,
            parent=self.app.root)
        if value is not None:
            self.app.cfg.setdefault("chat_message_limits", {})[pane.setting_key] = value
            self.owner.save()
            self.apply_limits()

    def settings(self, parent):
        row = tk.Frame(parent, bg=self.core.BG)
        row.pack(fill="x", pady=7)
        self.owner.label(row, "Default message limit per pane").pack(side="left")
        limit = tk.StringVar(value=str(message_limit(self.app.cfg)))
        tk.Spinbox(row, from_=100, to=20000, increment=100, textvariable=limit, width=7,
            bg="#111111", fg=self.core.FG, insertbackground=self.core.FG,
            buttonbackground="#202020").pack(side="left", padx=10)
        history = tk.BooleanVar(value=self.app.cfg.get("load_twitch_history", True))
        self.owner.check(parent, "Load recent Twitch messages when opening a channel", history).pack(anchor="w")
        self.owner.label(parent, "Twitch history uses recent-messages.robotty.de without account tokens. Availability varies. YouTube loads the recent messages its page provides.", dim=True).pack(anchor="w", pady=4)
        def apply():
            try:
                value = int(limit.get())
                if not 100 <= value <= 20000:
                    raise ValueError()
            except ValueError:
                self.owner.notify("Use a message limit from 100 to 20000.")
                return
            self.app.cfg.update(message_limit=value, load_twitch_history=history.get())
            self.owner.save()
            self.apply_limits()
        self.owner.button(parent, "Apply message limits / history", apply).pack(anchor="e", pady=5)

    def on_message(self, source, message):
        for card in tuple(self.cards.values()):
            if card.source is source and same_user(message, card.message):
                card.schedule_refresh()

    def show_user(self, pane, source, message):
        key = (source.key, message.get("user_id") or message.get("login") or message.get("name"))
        if key in self.cards:
            self.cards[key].window.lift()
            return
        while len(self.cards) >= 8:
            next(iter(self.cards.values())).close()
        card = UserCard(self, pane, source, message, key)
        self.cards[key] = card
        card.load()

    def public_profile(self, source, message):
        cache_key = (source.platform, message.get("user_id") or message.get("login") or message.get("name"))
        if cache_key in self.profiles:
            self.profiles.move_to_end(cache_key)
            return self.profiles[cache_key]
        result = {}
        if source.platform == "twitch":
            params = {"id": message["user_id"]} if message.get("user_id") else {"login": message.get("login")}
            data = self.owner.helix("users", params=params).get("data", [])
            if data:
                user = data[0]
                result = {"name": user.get("display_name"), "login": user.get("login"),
                          "created": user.get("created_at", "")[:10], "description": user.get("description", ""),
                          "avatar": user.get("profile_image_url"), "id": user.get("id")}
                try:
                    result["followers"] = self.owner.helix("channels/followers", params={"broadcaster_id": user["id"]}).get("total")
                except Exception:
                    pass
        elif self.owner.google.auth.get("channel_id") and message.get("user_id"):
            data = self.owner.youtube("channels", params={"part": "snippet,statistics", "id": message["user_id"]}).get("items", [])
            if data:
                snippet, stats = data[0].get("snippet", {}), data[0].get("statistics", {})
                images = snippet.get("thumbnails", {})
                result = {"name": snippet.get("title"), "id": data[0]["id"],
                    "created": snippet.get("publishedAt", "")[:10], "description": snippet.get("description", ""),
                    "avatar": images.get("medium", images.get("default", {})).get("url"),
                    "subscribers": None if stats.get("hiddenSubscriberCount") else stats.get("subscriberCount")}
        self.profiles[cache_key] = result
        while len(self.profiles) > 128:
            self.profiles.popitem(last=False)
        return result


class UserCard:
    """A bounded view of one user's retained messages; never claims full logs."""
    def __init__(self, tools, pane, source, message, key):
        self.tools, self.owner, self.core = tools, tools.owner, tools.core
        self.pane, self.source, self.message, self.key = pane, source, dict(message), key
        self.closed, self.timer = False, None
        self.photos, self.photo_pending = {}, set()
        self.app, self.fonts = tools.app, tools.app.fonts
        self.appearance = dict(tools.app.appearance, emote_size=28)
        self.active, self.used_assets, self.image_profile = True, set(), "usercard"
        self.window = self.owner.dialog(message.get("name", "User"), 530, 650)
        self.window.minsize(400, 380)
        self.window.protocol("WM_DELETE_WINDOW", self.close)
        self.window.bind("<Escape>", lambda event: self.close())
        self.window.bind("<Destroy>", lambda event: self._destroyed() if event.widget is self.window else None)
        header = tk.Frame(self.window, bg=self.core.BG, padx=10, pady=10)
        header.pack(fill="x")
        self.avatar = tk.Label(header, text="●", bg=self.core.BG, fg=self.core.DIM, font=("Segoe UI", 40))
        self.avatar.pack(side="left", padx=(0, 12))
        details = tk.Frame(header, bg=self.core.BG)
        details.pack(side="left", fill="x", expand=True)
        self.owner.label(details, message.get("name", "")).pack(anchor="w")
        self.meta = self.owner.label(details, "Loading public profile…")
        self.meta.pack(anchor="w")
        self.owner.label(self.window, tr("ID: {id}", id=message.get("user_id") or "—"), dim=True).pack(anchor="w", padx=10)
        badges = " · ".join(message.get("badges", []))
        months = re.search(r"(?:^|,)subscriber/(\d+)", message.get("badge_info", ""))
        if months:
            badges += " · " + str(tr("Subscribed for {months} months", months=months[1]))
        self.owner.label(self.window, badges or tr("No badges reported in this chat"), dim=True).pack(anchor="w", padx=10, pady=4)
        self.description = self.owner.label(self.window, "")
        self.description.pack(fill="x", padx=10)
        actions = tk.Frame(self.window, bg="#111111", padx=6, pady=4)
        actions.pack(fill="x", pady=7)
        name = message.get("login") or message.get("name", "")
        self.owner.button(actions, "Open profile", self.open_profile).pack(side="left", padx=2)
        self.owner.button(actions, "Highlight", lambda: self.owner.highlight_user(name)).pack(side="left", padx=2)
        self.owner.button(actions, "Mention", lambda: self.owner.mention(pane, source, name) if not pane.closed else None).pack(side="left", padx=2)
        mod = tk.Frame(self.window, bg=self.core.BG)
        mod.pack(fill="x", padx=8)
        protected = "[HOST]" in message.get("badges", [])
        for label, action, duration in (("Unban", "unban", 0), ("1m", "timeout", 60), ("10m", "timeout", 600),
                                         ("1h", "timeout", 3600), ("Ban", "ban", 0)):
            if source.platform == "youtube" and action == "unban":
                continue
            button = self.owner.button(mod, label, lambda a=action, d=duration: self.owner.moderate(source, self.message, a, d))
            button.pack(side="left", padx=2)
            if protected:
                button.configure(state="disabled")
        self.count = self.owner.label(self.window, "Recent messages retained in this channel", dim=True)
        self.count.pack(anchor="w", padx=10, pady=(8, 4))
        frame = tk.Frame(self.window, bg=self.core.BG)
        frame.pack(fill="both", expand=True, padx=8, pady=(0, 8))
        self.text = tk.Text(frame, bg=self.core.BG, fg=self.core.FG, wrap="word", relief="flat", state="disabled",
            font=tools.app.fonts["normal"], selectbackground="#365f85", exportselection=False)
        scroll = ttk.Scrollbar(frame, command=self.text.yview, style=tools.app.sb_style)
        scroll.pack(side="right", fill="y")
        self.text.configure(yscrollcommand=scroll.set)
        self.text.pack(side="left", fill="both", expand=True)
        self.text.tag_configure("time", foreground=self.core.DIM)
        color = message.get("color") or "#df8060"
        self.text.tag_configure("name", foreground=color if re.fullmatch(r"#[0-9a-fA-F]{6}", color) else "#df8060")
        self.text.tag_configure("alt", background=self.core.ALT)
        self.refresh_messages()

    def load(self):
        def ready(data):
            if self.closed:
                return
            fields = []
            if data.get("created"):
                fields.append(str(tr("Created: {date}", date=display_date(data["created"]))))
            if data.get("followers") is not None:
                fields.append(str(tr("Followers: {count}", count=number(int(data["followers"])))))
            if data.get("subscribers") is not None:
                fields.append(str(tr("Subscribers: {count}", count=number(int(data["subscribers"])))))
            if self.source.platform == "youtube" and not data:
                fields.append(str(tr("Connect YouTube to load public profile details (1 API unit).")))
            self.meta.configure(text="\n".join(fields) or tr("Public details unavailable"))
            self.description.configure(text=data.get("description", "")[:240])
            avatar = data.get("avatar") or self.message.get("avatar")
            if avatar:
                self.owner.async_action(("avatar", self.key), lambda: self.download_avatar(avatar), self.avatar_ready, lambda e: None)
        self.owner.async_action(("profile", self.key), lambda: self.tools.public_profile(self.source, self.message),
                                ready, lambda error: ready({}))

    @staticmethod
    def download_avatar(url):
        parsed = urllib.parse.urlparse(url)
        host = (parsed.hostname or "").lower()
        if parsed.scheme != "https" or not (host.endswith(".jtvnw.net") or host.endswith(".googleusercontent.com") or host.endswith(".ggpht.com")):
            raise ValueError("Unsupported profile image host.")
        with urllib.request.urlopen(url, timeout=8) as response:
            data = response.read(2 * 1024 * 1024 + 1)
        if len(data) > 2 * 1024 * 1024:
            raise ValueError("Profile image is too large.")
        from PIL import Image
        with Image.open(io.BytesIO(data)) as image:
            if image.width > 2048 or image.height > 2048:
                raise ValueError("Profile image dimensions are too large.")
            image.thumbnail((96, 96))
            out = io.BytesIO()
            image.convert("RGBA").save(out, format="PNG")
            return out.getvalue()

    def avatar_ready(self, data):
        if not self.closed:
            self.avatar_photo = tk.PhotoImage(master=self.window, data=base64.b64encode(data))
            self.avatar.configure(image=self.avatar_photo, text="")

    def open_profile(self):
        uid = self.message.get("user_id")
        login = self.message.get("login")
        if self.source.platform == "twitch" and login:
            self.owner.open_url("https://www.twitch.tv/" + urllib.parse.quote(login, safe=""))
        elif uid:
            self.owner.open_url("https://www.youtube.com/channel/" + urllib.parse.quote(uid, safe=""))

    def schedule_refresh(self):
        if not self.closed and self.timer is None:
            self.timer = self.window.after(200, self.refresh_messages)

    def refresh_messages(self):
        self.timer = None
        if self.closed:
            return
        messages = [m for _, kind, m, _ in self.source.history if kind == "msg" and same_user(m, self.message)]
        messages = messages[-min(500, message_limit(self.tools.app.cfg, self.pane.setting_key)):]
        follow = self.text.yview()[1] >= 0.99
        view = self.text.yview()[0]
        self.text.configure(state="normal")
        self.text.delete("1.0", "end")
        self.used_assets.clear()
        for row, message in enumerate(messages):
            tags = ("alt",) if row % 2 else ()
            self.text.insert("end", display_time(message["time"], message.get("sent_at")) + " ", ("time",) + tags)
            self.text.insert("end", message["name"] + ": ", ("name",) + tags)
            body = str(tr("[message deleted]")) if message.get("deleted") else message.get("text", "")
            spans = list(message.get("native_emotes", [])) + list(message.get("gifs", []))
            if not message.get("deleted") and self.source.emote_pattern:
                spans += [dict(self.source.emote_map[m.group()], start=m.start(), end=m.end())
                          for m in self.source.emote_pattern.finditer(body)]
            cursor = 0
            for emote in sorted(spans, key=lambda e: (e.get("start", -1), -e.get("end", -1))):
                start, end = emote.get("start", -1), emote.get("end", -1)
                if not cursor <= start < end <= len(body) or message.get("deleted"):
                    continue
                self.text.insert("end", body[cursor:start], tags)
                urls = tuple(emote.get("urls", ()))
                photo = self.photos.get(urls)
                if photo:
                    self.text.image_create("end", image=photo, align="center")
                    self.used_assets.add(urls)
                else:
                    self.text.insert("end", body[start:end], tags)
                    if urls and urls not in self.photo_pending:
                        self.photo_pending.add(urls)
                        self.core.EMOTE_IMAGES.request(urls, lambda data, key=urls:
                            self.app.call_ui(lambda: self.emote_ready(key, data)))
                cursor = end
            self.text.insert("end", body[cursor:] + "\n", tags)
        for key in list(self.photos):
            if key not in self.used_assets:
                self.photos.pop(key, None)
        self.text.configure(state="disabled")
        self.text.see("end") if follow else self.text.yview_moveto(view)
        self.count.configure(text=tr("{count} retained messages · up to 500 shown", count=len(messages)))

    def _resize_emote_photo(self, photo):
        return self.core.fit_photo(photo, 28, 84)

    def _resize_gif_photo(self, photo):
        return self.core.fit_photo(photo, 96, 128)

    def emote_ready(self, urls, payload):
        if self.closed or not payload:
            return
        self.photos[urls] = self.app.photo_bank.emote(urls, payload, self)
        self.schedule_refresh()

    def _destroyed(self):
        self.closed = True
        self.active = False
        self.tools.cards.pop(self.key, None)

    def close(self):
        if not self.closed:
            self.closed = True
            self.tools.cards.pop(self.key, None)
            if self.timer:
                self.window.after_cancel(self.timer)
            self.window.destroy()
