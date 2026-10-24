"""Bounded Twitch reply threads using the main chat renderer and composer."""
from collections import OrderedDict, deque
import tkinter as tk

from ytchatterino_chat import message_limit
from ytchatterino_i18n import tr


def thread_root(message, messages):
    """Prefer Twitch's root tag; follow older parent-only history safely."""
    current = message
    seen = set()
    while current:
        mid = current.get('id')
        if mid in seen:
            return min(seen) if seen else mid
        if mid:
            seen.add(mid)
        reply = current.get('reply') or {}
        if reply.get('thread_id'):
            return reply['thread_id']
        parent = reply.get('id')
        if not parent:
            return mid
        if parent not in messages:
            return parent
        current = messages[parent]
    return None


def collect_thread(history, selected, root_id=None):
    messages = OrderedDict((m['id'], m) for _, kind, m, _ in history
                           if kind == 'msg' and m.get('id'))
    if selected.get('id'):
        messages.setdefault(selected['id'], selected)
    root_id = root_id or thread_root(selected, messages)
    return root_id, [m for m in messages.values()
                     if m.get('id') == root_id or thread_root(m, messages) == root_id]


class ThreadWindow:
    def __init__(self, tools, source, selected, root_id, messages):
        self.tools, self.owner, self.app = tools, tools.owner, tools.app
        self.source, self.root_id = source, root_id
        self.key = (source.key, root_id)
        self.closed = False
        self.messages = OrderedDict((m['id'], m) for m in messages)
        self.selected = dict(selected)
        self.root_message = dict(self.messages.get(root_id) or self.context(selected))
        self.window = self.owner.dialog('', width=500, height=420)
        self.window.protocol('WM_DELETE_WINDOW', self.close)
        self.host = tk.Frame(self.window, bg=tools.core.BG)
        self.host.app, self.host.closed, self.host.btn = self.app, False, None
        self.host.pack(fill='both', expand=True)
        self.pane = tools.core.ChatTab(self.host, [source], self.app.fonts, self.app.sb_style,
                                       self.app.appearance, thread_window=self)
        self.pane.pack(fill='both', expand=True)
        self.pane.header.pack_forget()
        self.pane.target_combo.pack_forget()
        self.pane.set_filters({})
        self.pane.active = True
        self.app.extra_panes.add(self.pane)
        self.notice = self.owner.label(self.window, 'Only messages retained in this app are available.', dim=True)
        self.notice.pack(side='bottom', fill='x', padx=6, pady=2)
        self.window.bind('<Map>', self.visibility, add='+')
        self.window.bind('<Unmap>', self.visibility, add='+')
        self.window.bind('<Escape>', lambda event: self.close())
        self.rebuild()
        self.pane.compose_entry.focus_set()

    def context(self, selected):
        reply = selected.get('reply') or {}
        direct = reply.get('id') == self.root_id
        name = (reply.get('name') or reply.get('login')) if direct else reply.get('thread_login')
        return {'id':self.root_id, '_seq':-1, 'name':name or '?', 'login':name or '',
                'user_id':reply.get('user_id') if direct else None,
                'text':reply.get('text', '') if direct else str(tr('[Earlier message unavailable]')),
                'time':'', 'sent_at':None, 'reply':None, '_thread_context':True}

    def visibility(self, event):
        if event.widget is self.window and not self.closed:
            self.pane.set_active(event.type == tk.EventType.Map)

    def reset_reply(self):
        target = self.root_message
        if target.get('deleted'):
            target = next((m for m in reversed(self.messages.values()) if not m.get('deleted')), target)
        self.pane.reply_target = (self.source.key, dict(target))
        self.pane.reply_bar.pack_forget()

    def trim(self):
        cap = message_limit(self.app.cfg, self.source.key)
        while len(self.messages) > cap:
            self.messages.popitem(last=False)

    def rebuild(self):
        if self.closed:
            return
        self.trim()
        self.pane.clear_messages(reset_sequence=True)
        self.pane.add(self.root_message, self.source)
        for message in self.messages.values():
            if message.get('id') == self.root_id:
                continue
            seq = message.get('_seq')
            if seq is not None:
                self.pane.pending_events.append((seq, 'msg', message, self.source))
        self.reset_reply()
        name = self.root_message.get('login') or self.root_message.get('name', '?')
        self.window.title(tr('Reply thread · @{name} in {channel}', name=name, channel=self.source.name))

    def on_message(self, source, message):
        if self.closed or source is not self.source or not message.get('id'):
            return
        mid = message['id']
        if mid != self.root_id and thread_root(message, self.messages) != self.root_id:
            return
        if mid in self.messages:
            return
        self.messages[mid] = message
        self.trim()
        if mid == self.root_id:
            self.root_message = message
            self.rebuild()
        else:
            self.pane.pending_events.append((message['_seq'], 'msg', message, source))

    def redact(self, source, target):
        if source is not self.source:
            return
        changed = False
        for message in [self.root_message, *self.messages.values()]:
            if (target.get('all') or target.get('message_id') == message.get('id')
                    or target.get('user_id') and target['user_id'] == message.get('user_id')):
                message.update(text=str(tr('[message deleted]')), native_emotes=[], gifs=[], deleted=True)
                changed = True
        if changed:
            self.rebuild()

    def close(self):
        if self.closed:
            return
        self.closed = self.host.closed = True
        self.tools.threads.pop(self.key, None)
        self.app.extra_panes.discard(self.pane)
        self.pane.close()
        self.window.destroy()
        if not self.owner.closed:
            self.app.prune_sources()
