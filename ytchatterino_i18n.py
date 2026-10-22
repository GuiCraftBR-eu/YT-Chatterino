"""Small, explicit English / Brazilian Portuguese UI catalog.

Only strings marked with tr() are localized. User names, messages, stream titles,
emote codes, configuration keys and API values remain untouched. The Tk adapter
remembers marked options so changing language never restarts a connection.
"""
import functools
import re
import string
import tkinter as tk
import weakref
from tkinter import messagebox, simpledialog

from ytchatterino_translations import PT_BR

_language = 'en'
_widgets = weakref.WeakKeyDictionary()
_installed = False
_refreshing = False
_patterns = []


def language():
    return _language


def set_language(value):
    global _language
    _language = 'pt-BR' if value in ('pt-BR', 'pt_BR', 'pt-br') else 'en'


def number(value):
    text = f'{value:,}'
    return text.replace(',', '.') if _language == 'pt-BR' else text


def display_time(value):
    """Render cached timestamps in the chosen language without changing history."""
    match = re.fullmatch(r'(\d{1,2}):(\d{2})(?:\s*([ap]m))?', str(value), re.I)
    if not match:
        return value
    hour, minute = int(match[1]), match[2]
    if match[3]:
        hour = hour % 12 + (12 if match[3].lower() == 'pm' else 0)
    if _language == 'pt-BR':
        return f'{hour:02}:{minute}'
    return f'{hour % 12 or 12}:{minute} ' + ('pm' if hour >= 12 else 'am')


def _compile_patterns():
    # Prefer complete phrases over generic suffixes ("5 of 34 matches" must
    # match before "34 matches").
    ordered = sorted(PT_BR.items(), key=lambda item: len(re.sub(r'\{[^}]*\}', '', item[0])), reverse=True)
    for source, target in ordered:
        if '{' not in source:
            continue
        pieces, names = [], []
        try:
            for literal, name, spec, conversion in string.Formatter().parse(source):
                pieces.append(re.escape(literal))
                if name is not None:
                    names.append(name)
                    pieces.append('(.+?)')
            _patterns.append((re.compile('^' + ''.join(pieces) + '$', re.S), names, target))
        except ValueError:
            continue


@functools.lru_cache(maxsize=1024)
def _translate(text, lang):
    if lang != 'pt-BR' or not text:
        return text
    if text in PT_BR:
        return PT_BR[text]
    for pattern, names, target in _patterns:
        match = pattern.fullmatch(text)
        if match:
            values = dict(zip(names, match.groups()))
            # Translate app-authored nested errors and action labels, while
            # preserving arbitrary names / IDs in other fields.
            for name in ('error', 'action', 'status', 'detail'):
                if name in values:
                    values[name] = _translate(values[name], lang)
            return re.sub(r'\{(\w+)\}', lambda field: values.get(field[1], field[0]), target)
    if text.startswith('[') and '] ' in text:
        prefix, remainder = text.split('] ', 1)
        return prefix + '] ' + _translate(remainder, lang)
    return text


class Localized(str):
    def __new__(cls, source, values=None):
        values = values or {}
        target = PT_BR.get(source, source) if _language == 'pt-BR' else source
        rendered = target.format_map(values) if values else _translate(source, _language)
        result = super().__new__(cls, rendered)
        result.source, result.values = source, values
        return result

    def current(self):
        return Localized(self.source, self.values)


def tr(text, **values):
    if isinstance(text, Localized) and not values:
        return text.current()
    return Localized(str(text), values)


def _remember(widget, options):
    if _refreshing:
        return
    marked = _widgets.setdefault(widget, {})
    for key, value in options.items():
        if key in ('text', 'title'):
            if isinstance(value, Localized):
                marked[key] = value
            else:
                marked.pop(key, None)
        elif key == 'values':
            if isinstance(value, (list, tuple)) and any(isinstance(v, Localized) for v in value):
                marked[key] = tuple(value)
            else:
                marked.pop(key, None)


def refresh_ui():
    global _refreshing
    _refreshing = True
    try:
        for widget, options in list(_widgets.items()):
            try:
                if not widget.winfo_exists():
                    continue
                for key, value in list(options.items()):
                    if key == 'title':
                        widget.title(value.current())
                    elif key == 'values':
                        index = widget.current()
                        widget.configure(values=[v.current() if isinstance(v, Localized) else v for v in value])
                        if index >= 0:
                            widget.current(index)
                    elif isinstance(key, tuple):
                        widget.entryconfigure(key[1], label=value.current())
                    else:
                        widget.configure(**{key: value.current()})
            except tk.TclError:
                pass  # closed dialogs/menus can still have live Python references
    finally:
        _refreshing = False


def install_ui(root):
    """Track explicit tr() options at Tk's public widget/menu boundaries."""
    global _installed
    if _installed:
        return
    _installed = True
    original_init = tk.BaseWidget.__init__
    original_configure = tk.Misc._configure
    original_title = tk.Wm.wm_title
    original_add = tk.Menu.add

    def widget_init(self, master, widgetName, cnf={}, kw={}, extra=()):
        original_init(self, master, widgetName, cnf, kw, extra)
        _remember(self, dict(tk._cnfmerge((cnf, kw)) or {}))

    def configure(self, cmd, cnf, kw):
        result = original_configure(self, cmd, cnf, kw)
        if cmd == 'configure' and (isinstance(cnf, dict) or kw):
            _remember(self, dict(tk._cnfmerge((cnf if isinstance(cnf, dict) else {}, kw or {})) or {}))
        return result

    def title(self, text=None):
        result = original_title(self, text)
        if text is not None:
            _remember(self, {'title': text})
        return result

    def menu_add(self, itemType, cnf={}, **kw):
        result = original_add(self, itemType, cnf, **kw)
        label = kw.get('label', cnf.get('label') if isinstance(cnf, dict) else None)
        if isinstance(label, Localized):
            _widgets.setdefault(self, {})[('menu', self.index('end'))] = label
        return result

    tk.BaseWidget.__init__ = widget_init
    tk.Misc._configure = configure
    tk.Wm.wm_title = tk.Wm.title = title
    tk.Menu.add = menu_add

    # Native Windows message boxes take their button language from Windows.
    # Use an equivalent compact dark dialog in PT-BR so the entire app follows
    # its language setting. Existing English behavior remains standard Tk.
    original_show = messagebox._show

    def show(title=None, message=None, _icon=None, _type=None, **options):
        if language() != 'pt-BR':
            return original_show(title, message, _icon, _type, **options)
        parent = options.get('parent') or root
        dialog = tk.Toplevel(parent)
        dialog.configure(bg='#000000')
        dialog.title(tr(title or 'YT-Chatterino'))
        dialog.transient(parent.winfo_toplevel())
        dialog.resizable(False, False)
        tk.Label(dialog, text=tr(message or ''), bg='#000000', fg='#efeff1',
                 wraplength=480, justify='left', padx=18, pady=18).pack(fill='x')
        row = tk.Frame(dialog, bg='#000000')
        row.pack(fill='x', padx=12, pady=(0, 12))
        choices = {'yesno': ('yes', 'no'), 'yesnocancel': ('yes', 'no', 'cancel'),
                   'okcancel': ('ok', 'cancel'), 'retrycancel': ('retry', 'cancel'),
                   'abortretryignore': ('abort', 'retry', 'ignore')}.get(_type, ('ok',))
        names = {'yes': 'Yes', 'no': 'No', 'ok': 'OK', 'cancel': 'Cancel',
                 'retry': 'Retry', 'abort': 'Abort', 'ignore': 'Ignore'}
        answer = [choices[-1]]
        def finish(value):
            answer[0] = value
            dialog.destroy()
        for value in reversed(choices):
            button = tk.Button(row, text=tr(names[value]), bg='#181818', fg='#efeff1', bd=0,
                               padx=16, pady=5, command=lambda v=value: finish(v))
            button.pack(side='right', padx=3)
        dialog.protocol('WM_DELETE_WINDOW', lambda: finish(choices[-1]))
        dialog.bind('<Escape>', lambda event: finish(choices[-1]))
        dialog.update_idletasks()
        dialog.geometry(f'+{max(0, parent.winfo_rootx()+30)}+{max(0, parent.winfo_rooty()+80)}')
        previous = dialog.grab_current()
        dialog.grab_set()
        dialog.wait_window()
        if previous and previous.winfo_exists():
            previous.grab_set()
        return answer[0]

    messagebox._show = show
    original_buttonbox = simpledialog.Dialog.buttonbox

    def buttonbox(self):
        if language() != 'pt-BR':
            return original_buttonbox(self)
        box = tk.Frame(self)
        tk.Button(box, text=tr('OK'), width=10, command=self.ok, default='active').pack(side='left', padx=5, pady=5)
        tk.Button(box, text=tr('Cancel'), width=10, command=self.cancel).pack(side='left', padx=5, pady=5)
        self.bind('<Return>', self.ok)
        self.bind('<Escape>', self.cancel)
        box.pack()

    simpledialog.Dialog.buttonbox = buttonbox


_compile_patterns()
