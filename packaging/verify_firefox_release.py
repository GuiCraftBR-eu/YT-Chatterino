"""Check that Mozilla's signed XPI matches the reviewed browser extension and update feed."""
import hashlib
import json
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parents[1]
release_version = '2.5.7'
extension_version = '2.5.7.1'
xpi = root / 'firefox-release' / f'YT-Chatterino-Firefox-{release_version}.xpi'
extension = root / 'browser-extension'
data = xpi.read_bytes()
feed = json.loads((extension / 'updates.json').read_text(encoding='utf8'))
entry = feed['addons']['vod-sync@yt-chatterino.local']['updates'][0]
assert entry == {'version': extension_version,
                 'update_link': f'https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v{release_version}/YT-Chatterino-Firefox-{release_version}.xpi',
                 'update_hash': 'sha256:' + hashlib.sha256(data).hexdigest()}
with zipfile.ZipFile(xpi) as archive:
    assert archive.testzip() is None
    names = set(archive.namelist())
    assert 'META-INF/mozilla.rsa' in names and 'META-INF/mozilla.sf' in names
    for path in extension.iterdir():
        if not path.is_file() or path.name == 'updates.json':
            continue  # The update feed lives outside the signed package.
        assert path.name in names, path.name
        if path.name == 'manifest.json':
            manifest = json.loads(archive.read(path.name))
            assert manifest == json.loads(path.read_text(encoding='utf8'))
            assert manifest['version'] == extension_version
        else:
            assert archive.read(path.name) == path.read_bytes(), path.name
    assert {name for name in names if not name.startswith('META-INF/')} == {
        path.name for path in extension.iterdir() if path.is_file()}
print(f'Firefox extension {extension_version} for release {release_version}: signed package and update feed verified.')
