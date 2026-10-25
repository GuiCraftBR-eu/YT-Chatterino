"""Harness de comparação: usa o código 1.3.0 intacto e um perfil temporário sem contas/rede."""
from pathlib import Path
import argparse
import importlib.machinery
import importlib.util
import json
import os
import sys
import time
import urllib.request

started = time.perf_counter()
parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
parser.add_argument('--profile', required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
profile, output, source_dir = Path(args.profile).resolve(), Path(args.output), Path(args.source).resolve()
assert profile != Path.home().resolve(), 'Use um perfil isolado.'
profile.mkdir(parents=True, exist_ok=True)
os.environ['YTCHATTERINO_DATA_DIR'] = str(profile)
config = {'automatic_updates': False, 'message_limit': 2000, 'language': 'en', 'topmost': False,
          'appearance': {'font_size': 12, 'message_font_size': 12, 'username_font_size': 12,
                         'show_timestamps': True, 'alternate_rows': True, 'time_format': 'HH:mm'},
          'session': {'geometry': '520x760+550+80', 'tabs': []},
          'features': {'alerts': {'enabled': False}}}
(profile / '.ytchatterino.json').write_text(json.dumps(config), encoding='utf-8')
network_requests = 0

def block_network(*args, **kwargs):
    global network_requests
    network_requests += 1
    raise RuntimeError('Rede desativada no benchmark')

urllib.request.urlopen = block_network
if sys.platform == 'win32':
    import ctypes
    ctypes.windll.user32.SetProcessDPIAware()
sys.path.insert(0, str(source_dir))
loader = importlib.machinery.SourceFileLoader('ytchatterino_130_benchmark', str(source_dir / 'ytchatterino.pyw'))
spec = importlib.util.spec_from_loader(loader.name, loader)
core = importlib.util.module_from_spec(spec)
sys.modules[loader.name] = core
loader.exec_module(core)
assert core.APP_VERSION == '1.3.0'
core.TwitchTokenStore.read = lambda self: None
core.ChatSource.ensure_worker = lambda self: None
core.ChatSource.load_emotes = lambda *args, **kwargs: None
core.FeatureController.start = lambda self: None
root = core.tk.Tk(className='YtchatterinoBenchmark')
root.tk.call('tk', 'scaling', 96 / 72)
if Path(core.APP_ICON_PATH).exists():
    root.iconbitmap(core.APP_ICON_PATH)
app = core.App(root)
source = app.source_for({'platform': 'twitch', 'id': 'benchmark'})
source.header_info = {'channel': 'benchmark', 'title': 'Benchmark', 'category': 'Just Chatting', 'live': False}
workspace = app.new_workspace([source], 'Benchmark')
pane = workspace.focused
root.geometry('520x760+550+80')
root.update()
core.style_titlebar(root)
root.update_idletasks()
startup_ms = (time.perf_counter() - started) * 1000
Path(str(output) + '.ready').write_text('ready', encoding='utf-8')

def settle():
    deadline = time.perf_counter() + 4
    quiet = time.perf_counter()
    while time.perf_counter() < deadline:
        root.update()
        if pane.resizing or pane.viewport_after_id is not None or pane.resize_restore_id is not None or pane.pending_events:
            quiet = time.perf_counter()
        if time.perf_counter() - quiet >= .03:
            return
        time.sleep(.001)
    raise RuntimeError('O layout 1.3.0 não concluiu dentro do limite.')

settle()
for index in range(2000):
    message = {'id': str(index), '_seq': index + 1, 'name': f'User{index % 20:02}',
               'text': f'Message {index}: Retained chat benchmark with a link https://example.com/{index} and a longer sentence that wraps in a narrow window.',
               'time': '12:34', 'sent_at': '2026-10-24T12:34:00+00:00', 'color': '#4a9cdb'}
    source.history.append((index + 1, 'msg', message, source))
loading = time.perf_counter()
pane.load_recent()
settle()
load_ms = (time.perf_counter() - loading) * 1000
resizing = time.perf_counter()
sizes = [(360, 680), (900, 760), (520, 760), (680, 640)]
for index in range(80):
    width, height = sizes[index % len(sizes)]
    root.geometry(f'{width}x{height}+550+80')
    root.update()
settle()
result = {'version': core.APP_VERSION, 'startup_internal_ms': startup_ms,
          'load_2000_ms': load_ms, 'resize_80_ms': (time.perf_counter() - resizing) * 1000,
          'messages': len(pane.message_records), 'network_requests': network_requests,
          'tk_scaling': float(root.tk.call('tk', 'scaling')), 'chat_font_pixels': app.fonts['normal'].metrics('linespace'),
          'python': sys.version.split()[0], 'tk': root.tk.call('info', 'patchlevel')}
output.write_text(json.dumps(result, indent=2), encoding='utf-8')
app.quit()
assert result['messages'] == 2000 and not network_requests, result
