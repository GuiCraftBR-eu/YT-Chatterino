"""Ensure GitHub received the exact tested assets before publishing a draft."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import time

repo, tag = os.environ['GH_REPO'], os.environ['RELEASE_TAG']
files = {path.name: path for path in Path('.').iterdir() if path.is_file()}
if not files:
    raise SystemExit('No release assets found.')
expected = {}
for name, path in files.items():
    with path.open('rb') as stream:
        expected[name] = 'sha256:' + hashlib.file_digest(stream, 'sha256').hexdigest()

for attempt in range(12):
    payload = subprocess.run(['gh', 'api', f'repos/{repo}/releases?per_page=100'],
                             check=True, capture_output=True, text=True)
    matching = [release for release in json.loads(payload.stdout) if release['tag_name'] == tag]
    if len(matching) != 1 or not matching[0]['draft']:
        raise SystemExit('Expected exactly one unpublished release draft.')
    assets = {asset['name']: asset for asset in matching[0]['assets']}
    if set(assets) == set(expected) and all(assets[name]['state'] == 'uploaded' and
                                            assets[name].get('digest') == expected[name]
                                            for name in expected):
        print(f'Validated {len(expected)} uploaded release assets by SHA-256.')
        break
    if attempt == 11:
        raise SystemExit('Uploaded asset names, states, or SHA-256 digests do not match.')
    time.sleep(5)
