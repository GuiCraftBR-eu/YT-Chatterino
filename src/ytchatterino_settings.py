"""Searchable, grouped settings. Pages are built lazily and preserve drafts."""
import tkinter as tk
import unicodedata
from tkinter import ttk
from ytchatterino_i18n import tr, language, search_translation

SECTIONS = ('Accounts', 'Appearance', 'Notifications', 'Saved channels',
            'YouTube quota', 'Updates', 'Keyboard shortcuts', 'Tutorial')
APPEARANCE_PAGES = ('Display', 'Messages', 'Highlights')

# Page, visible control label, additional English and Portuguese search words.
SEARCH_ITEMS = (
    ('Accounts', 'Twitch sign-in / permissions', 'login account connect oauth mod moderation twitch conectar conta permissoes moderacao'),
    ('Accounts', 'Choose Google OAuth JSON…', 'youtube google client desktop oauth json key conta conectar chave cliente'),
    ('Accounts', 'Sign in to YouTube', 'youtube login google send moderation entrar conectar enviar moderar'),
    ('Display', 'Language / Idioma', 'language english portuguese brazil pt br portugues idioma traducao'),
    ('Display', 'Always on top', 'pin window topmost fixar janela topo sempre'),
    ('Display', 'Compact message spacing', 'compact space rows compacto espacamento mensagens linhas'),
    ('Display', 'Show timestamps', 'time date clock timestamp hora horario data relogio'),
    ('Display', 'Date format', 'date format day month year dd mm yyyy data formato dia mes ano'),
    ('Display', 'Timestamp format', 'time seconds hour clock timestamp hh mm ss 12 24 am pm horario segundos hora formato relogio'),
    ('Updates', 'Check for updates automatically', 'automatic updates download version upgrade atualizar atualizacao automatica baixar versao'),
    ('Display', 'Alternate message backgrounds', 'background alternate rows color fundo linhas alternadas cor'),
    ('Display', 'Badge height (px)', 'badge size height moderator vip sub emblemas tamanho altura moderador'),
    ('Display', 'Emote height (px)', 'emote emoji size height gif animation tamanho altura animacao'),
    ('Display', 'Message font size (pt)', 'message body text font size zoom tamanho fonte mensagem texto letras'),
    ('Display', 'Username font size (pt)', 'username nickname name font size tamanho fonte nome usuario apelido'),
    ('Display', 'Text size: Ctrl++ / Ctrl+- · Ctrl+0 resets. Images keep their aspect ratio.', 'font text zoom size tamanho fonte texto letras'),
    ('Messages', 'Default message limit per pane', 'limit cap history memory performance cache count 2000 limite mensagens historico memoria desempenho quantidade'),
    ('Messages', 'Load recent Twitch messages when opening a channel', 'history recent old previous past historico recentes antigas anteriores twitch robotty'),
    ('Highlights', 'Users · one username per line, optionally followed by #RRGGBB', 'highlight user nickname color name destaque usuario apelido nome cor'),
    ('Highlights', 'Owners', 'owner host broadcaster highlight dono proprietario streamer destaque'),
    ('Highlights', 'Moderators', 'moderator mod highlight moderador destaque'),
    ('Highlights', 'VIPs', 'vip role badge highlight cargo emblema destaque'),
    ('Highlights', 'Subscribers / members', 'subscriber member sub highlight inscrito membro assinatura destaque'),
    ('Highlights', 'Founders', 'founder badge highlight fundador emblema destaque'),
    ('Highlights', 'Words / phrases · separate with commas', 'highlight words phrases mention mention_me destaque palavras frases mencao'),
    ('Highlights', 'Highlight mentions of my signed-in names', 'own username mention highlight destaque mencao nome apelido'),
    ('Notifications', 'Enable notification sounds', 'sound audio ping notifications alerts som notificacao alerta'),
    ('Notifications', 'Sound for matching mentions / highlight words', 'mention nickname ping notification mencao apelido nome som'),
    ('Notifications', 'Notify all open Twitch channels by default', 'live twitch stream online broadcast alert notification transmissao direto inicio alerta'),
    ('Notifications', 'Mention sound…', 'wav file ping sound audio som arquivo mencao'),
    ('Notifications', 'Live sound…', 'wav file live notification sound som arquivo transmissao'),
    ('Notifications', 'Additional nicknames · separate with commas', 'own name nickname mention username apelido nome nomes mencao'),
    ('Saved channels', 'Saved channels', 'save favourite favorite twitch youtube channel salvar salvos favorito canal'),
    ('YouTube quota', 'Daily project limit', 'youtube google api quota units 10000 budget cota unidades limite diario projeto'),
    ('YouTube quota', 'Units used outside this app today', 'external youtube quota units cota unidades externo outro aplicativo'),
    ('Keyboard shortcuts', 'Keyboard shortcuts', 'keys hotkey keyboard shortcut tab escape control shift ctrl atalhos teclado teclas'),
    ('Tutorial', 'Tutorial', 'help how connect setup send moderation split emote tutorial ajuda conectar configurar enviar moderar dividir'),
)


def normalized(value):
    return ''.join(c for c in unicodedata.normalize('NFKD', str(value).casefold()) if not unicodedata.combining(c))


def settings_matches(query):
    words = normalized(query).split()
    if not words:
        return []
    result = []
    for page, label, keywords in SEARCH_ITEMS:
        section = 'Appearance' if page in APPEARANCE_PAGES else page
        searchable = normalized(' '.join((page, section, label, search_translation(page), search_translation(section), search_translation(label), keywords)))
        if all(word in searchable for word in words):
            result.append((page, label))
    return result


class ScrollPage(tk.Frame):
    def __init__(self, parent, owner):
        super().__init__(parent, bg=owner.core.BG)
        self.canvas = tk.Canvas(self, bg=owner.core.BG, highlightthickness=0, bd=0)
        self.scrollbar = ttk.Scrollbar(self, orient='vertical', command=self.canvas.yview, style=owner.app.sb_style)
        self.scrollbar.pack(side='right', fill='y')
        self.canvas.configure(yscrollcommand=self.scrollbar.set)
        self.canvas.pack(side='left', fill='both', expand=True)
        self.inner = tk.Frame(self.canvas, bg=owner.core.BG, padx=14, pady=10)
        self.item = self.canvas.create_window(0, 0, window=self.inner, anchor='nw')
        self.timer = None
        self.width = None
        self.flashes = {}
        self.canvas.bind('<Configure>', self.resize)
        self.inner.bind('<Configure>', self.changed)
        self.bind('<Destroy>', self.destroyed, add='+')

    def resize(self, event):
        if event.width != self.width:
            self.width = event.width
            self.canvas.itemconfigure(self.item, width=event.width)

    def changed(self, event=None):
        if self.timer is None:
            self.timer = self.after_idle(self.measure)

    def measure(self):
        self.timer = None
        self.canvas.configure(scrollregion=self.canvas.bbox('all'))

    def destroyed(self, event):
        if event.widget is self:
            if self.timer:
                self.after_cancel(self.timer)
                self.timer = None
            for timer, _ in self.flashes.values():
                self.after_cancel(timer)
            self.flashes.clear()

    def install_wheel(self):
        tag = 'settings_scroll_' + str(id(self))
        def scroll(event):
            if isinstance(event.widget, (tk.Text, tk.Listbox, ttk.Combobox)):
                return None
            if self.canvas.yview() == (0.0, 1.0):
                return None
            delta = -1 if getattr(event, 'num', 0) == 4 else 1 if getattr(event, 'num', 0) == 5 else (-1 if event.delta > 0 else 1)
            self.canvas.yview_scroll(delta * 3, 'units')
            return 'break'
        def walk(widget):
            widget.bindtags((tag,) + widget.bindtags())
            for child in widget.winfo_children():
                walk(child)
        walk(self.inner)
        self.bind_class(tag, '<MouseWheel>', scroll)
        self.bind_class(tag, '<Button-4>', scroll)
        self.bind_class(tag, '<Button-5>', scroll)

    def reveal(self, label):
        needle = str(tr(label))
        def find(widget):
            try:
                if str(widget.cget('text')) == needle:
                    return widget
            except tk.TclError:
                pass
            for child in widget.winfo_children():
                match = find(child)
                if match is not None:
                    return match
        target = find(self.inner)
        if target is None:
            return
        self.update_idletasks()
        position = target.winfo_rooty() - self.inner.winfo_rooty()
        self.canvas.yview_moveto(max(0, position - 40) / max(1, self.inner.winfo_height()))
        try:
            previous = self.flashes.pop(target, None)
            if previous:
                self.after_cancel(previous[0])
            original = previous[1] if previous else target.cget('background')
            target.configure(background='#274020')
            def reset():
                self.flashes.pop(target, None)
                if target.winfo_exists():
                    target.configure(background=original)
            self.flashes[target] = (self.after(1800, reset), original)
        except tk.TclError:
            pass


class SettingsWindow:
    def __init__(self, owner, section='Accounts'):
        self.owner = owner
        self.window = owner.dialog('Settings · YT-Chatterino', 820, 720)
        owner.window = self.window
        self.window.minsize(660, 500)
        self.pages, self.shells, self.buttons, self.appearance_buttons = {}, {}, {}, {}
        self.current = section
        self.last_page = section
        self.lang = language()
        self.search_timer = None
        self.search_results = []
        self.query = tk.StringVar()
        side = tk.Frame(self.window, bg='#111111', width=160)
        side.pack(side='left', fill='y');side.pack_propagate(False)
        for name in SECTIONS:
            button = owner.button(side, name, lambda page=name: self.navigate(page))
            button.pack(fill='x', padx=5, pady=3)
            self.buttons[name] = button
        owner.settings_buttons = self.buttons
        footer = owner.label(side, 'Changes save locally.', dim=True)
        footer.configure(wraplength=140);footer.pack(side='bottom', padx=8, pady=12)
        right = tk.Frame(self.window, bg=owner.core.BG)
        right.pack(side='left', fill='both', expand=True)
        search = tk.Frame(right, bg='#111111', padx=10, pady=8)
        search.pack(fill='x')
        owner.label(search, 'Search settings').pack(side='left', padx=(0, 8))
        self.search_entry = owner.entry(search, self.query)
        self.search_entry.pack(side='left', fill='x', expand=True)
        owner.button(search, '×', lambda: self.query.set('')).pack(side='left', padx=(5, 0))
        self.search_entry.bind('<Return>', self.choose_first)
        self.search_entry.bind('<Escape>', lambda e: self.clear_search())
        self.search_entry.bind('<Down>', self.focus_results)
        self.window.bind('<Control-f>', self.focus_search)
        self.body = tk.Frame(right, bg=owner.core.BG)
        self.body.pack(fill='both', expand=True)
        self.result_frame = tk.Frame(self.body, bg=owner.core.BG, padx=14, pady=12)
        self.result_count = owner.label(self.result_frame, '')
        self.result_count.pack(anchor='w', pady=(0, 8))
        self.results = tk.Listbox(self.result_frame, bg='#111111', fg=owner.core.FG, bd=0,
            selectbackground='#274020', selectforeground='#ffffff', exportselection=False,
            activestyle='none', font=('Segoe UI', 10), height=14)
        self.results.pack(fill='both', expand=True)
        self.results.bind('<Double-Button-1>', self.choose_result)
        self.results.bind('<Return>', self.choose_result)
        self.results.bind('<Escape>', lambda e: self.clear_search())
        owner.button(self.result_frame, 'Open setting', self.choose_result).pack(anchor='e', pady=8)
        self.query.trace_add('write', self.schedule_search)
        self.window.bind('<Destroy>', self.destroyed, add='+')
        self.show(section)

    def destroyed(self, event):
        if event.widget is self.window and self.search_timer:
            self.window.after_cancel(self.search_timer)
            self.search_timer = None

    def focus_search(self, event=None):
        self.search_entry.focus_set();self.search_entry.selection_range(0, 'end')
        return 'break'

    def focus_results(self, event=None):
        if self.search_results:
            self.results.focus_set();self.results.selection_clear(0, 'end');self.results.selection_set(0)
        return 'break'

    def clear_search(self):
        self.query.set('');self.show(self.last_page)
        self.search_entry.focus_set()
        return 'break'

    def navigate(self, section):
        self.query.set('')
        self.show(section)

    def show(self, page, target=None):
        if page == 'Appearance':
            page = self.last_page if self.last_page in APPEARANCE_PAGES else 'Display'
        if self.lang != language():
            for shell in self.shells.values():
                shell.destroy()
            self.pages.clear();self.shells.clear();self.appearance_buttons.clear()
            self.owner.quota_widgets = {};self.owner.quota_display = None;self.owner.account_label = None
            self.lang = language()
        self.current = self.last_page = page
        self.owner.settings_section = page
        self.result_frame.pack_forget()
        for shell in self.shells.values():
            shell.pack_forget()
        section = 'Appearance' if page in APPEARANCE_PAGES else page
        for name, button in self.buttons.items():
            button.configure(bg='#303030' if name == section else '#181818')
        if section not in self.shells:
            shell = tk.Frame(self.body, bg=self.owner.core.BG)
            self.shells[section] = shell
            title = self.owner.label(shell, section)
            title.configure(font=('Segoe UI', 14, 'bold'));title.pack(anchor='w', padx=14, pady=(12, 8))
            if section == 'Appearance':
                tabs = tk.Frame(shell, bg='#111111')
                tabs.pack(fill='x', padx=14, pady=(0, 6))
                for name in APPEARANCE_PAGES:
                    button = self.owner.button(tabs, name, lambda selected=name: self.navigate(selected))
                    button.pack(side='left', padx=(0, 4));self.appearance_buttons[name] = button
        shell = self.shells[section]
        shell.pack(fill='both', expand=True)
        for frame in self.pages.values():
            frame.pack_forget()
        if page not in self.pages:
            frame = ScrollPage(shell, self.owner)
            self.pages[page] = frame
            builders = {'Accounts':self.owner.accounts_page, 'Display':self.owner.appearance_page,
                'Messages':self.owner.chat_tools.settings, 'Highlights':self.owner.highlights_page,
                'Notifications':self.owner.notifications_page, 'Saved channels':self.owner.saved_channels_page,
                'YouTube quota':self.owner.quota_page, 'Updates':self.owner.app.updater.settings_page, 'Keyboard shortcuts':self.owner.shortcuts_page,
                'Tutorial':self.owner.tutorial_page}
            builders[page](frame.inner)
            frame.install_wheel()
        frame = self.pages[page]
        frame.pack(fill='both', expand=True)
        self.owner.settings_content = frame.inner
        for name, button in self.appearance_buttons.items():
            button.configure(bg='#303030' if name == page else '#181818')
        if page == 'Accounts':
            self.owner.account_status()
        if page == 'YouTube quota':
            self.owner.refresh_quota()
        if hasattr(frame.inner, 'refresh_settings'):
            frame.inner.refresh_settings()
        if target:
            frame.canvas.yview_moveto(0)
            self.window.after_idle(lambda: frame.reveal(target) if frame.winfo_exists() else None)

    def schedule_search(self, *args):
        if self.search_timer:
            self.window.after_cancel(self.search_timer)
        self.search_timer = self.window.after(100, self.search)

    def search(self):
        if self.search_timer:
            self.window.after_cancel(self.search_timer)
            self.search_timer = None
        query = self.query.get().strip()
        if not query:
            self.search_results = []
            self.show(self.last_page)
            return
        for shell in self.shells.values():
            shell.pack_forget()
        self.result_frame.pack(fill='both', expand=True)
        self.search_results = settings_matches(query)
        self.results.delete(0, 'end')
        for page, label in self.search_results:
            path = str(tr('Appearance')) + ' / ' + str(tr(page)) if page in APPEARANCE_PAGES else str(tr(page))
            self.results.insert('end', path + ' — ' + str(tr(label)))
        count = len(self.search_results)
        result_text = tr('1 setting found') if count == 1 else tr('{count} settings found', count=count)
        self.result_count.configure(text=result_text if count else tr('No settings found. Try another word.'))
        if self.search_results:
            self.results.selection_set(0)

    def choose_first(self, event=None):
        self.search()
        return self.choose_result()

    def choose_result(self, event=None):
        selected = self.results.curselection()
        if selected and selected[0] < len(self.search_results):
            page, label = self.search_results[selected[0]]
            self.query.set('')
            if self.search_timer:
                self.window.after_cancel(self.search_timer);self.search_timer = None
            self.show(page, label)
        return 'break'
