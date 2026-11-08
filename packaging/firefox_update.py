"""Prepare a Firefox update manifest from a Mozilla-signed self-distributed XPI.

Upload the exact XPI to the matching GitHub release before committing the generated
manifest. Never publish unsigned files or credentials using this script.
"""
import argparse
import hashlib
import json
import re
import zipfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('signed_xpi', type=Path)
parser.add_argument('--output', type=Path, default=Path('browser-extension/updates.json'))
args = parser.parse_args()
raw = args.signed_xpi.read_bytes()
with zipfile.ZipFile(args.signed_xpi) as archive:
    if archive.testzip() is not None:
        raise SystemExit('Corrupt XPI')
    names = {name.lower() for name in archive.namelist()}
    if not any(name.startswith('meta-inf/') and name.endswith(('.rsa', '.ec')) for name in names):
        raise SystemExit('The XPI does not contain a Mozilla signature')
    manifest = json.loads(archive.read('manifest.json'))
    gecko = manifest.get('browser_specific_settings', {}).get('gecko', {})
    version = manifest.get('version', '')
    if gecko.get('id') != 'vod-sync@yt-chatterino.local' or not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise SystemExit('Unexpected extension ID or version')
    if gecko.get('update_url') != 'https://raw.githubusercontent.com/GuiCraftBR-eu/YT-Chatterino/main/browser-extension/updates.json':
        raise SystemExit('The XPI does not use the permanent update URL')
url = f'https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v{version}/YT-Chatterino-Firefox-{version}.xpi'
entry = {'version': version, 'update_link': url, 'update_hash': 'sha256:' + hashlib.sha256(raw).hexdigest()}
args.output.write_text(json.dumps({'addons': {gecko['id']: {'updates': [entry]}}}, indent=2) + '\n', encoding='utf-8')
print(f'Upload this exact file as YT-Chatterino-Firefox-{version}.xpi, then publish {args.output}.')
