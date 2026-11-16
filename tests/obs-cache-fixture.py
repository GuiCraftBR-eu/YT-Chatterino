"""Exercise an OBS restart loading a staged core, without touching the user's profile."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

archive, fixture, frontend, bootstrap = (Path(item).resolve() for item in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='yt-obs-cache-test-') as temporary:
    temp = Path(temporary)
    with zipfile.ZipFile(archive) as package:
        assert package.testzip() is None
        package.extractall(temp / 'package')
    root = temp / 'updates'
    directory = '9.8.7-12345678-1234-4234-8234-123456789abc'
    staged = root / directory
    shutil.copytree(temp / 'package/yt-chatterino-obs', staged)
    manifest = staged / 'data/sidecar/update-manifest.json'
    data = json.loads(manifest.read_text(encoding='utf8'))
    data['version'] = '9.8.7'
    manifest.write_text(json.dumps(data), encoding='utf8')
    digest = hashlib.sha256((staged / 'yt-chatterino-obs-core.dll').read_bytes()).hexdigest()
    (root / 'current.json').write_text(json.dumps({'directory': directory, 'version': '9.8.7', 'core_sha256': digest}), encoding='utf8')
    isolated = temp / 'loader'
    isolated.mkdir()
    loader = isolated / bootstrap.name
    shutil.copyfile(bootstrap, loader)
    env = dict(os.environ, YTCHATTERINO_OBS_TEST_UPDATE_ROOT=str(root),
               YTCHATTERINO_OBS_PROFILE=str(temp / 'profile'), YTCHATTERINO_OBS_DISABLE_UPDATES='1')
    # No bundled core is present beside the loader, so success proves the staged core was used.
    subprocess.run([str(fixture), str(frontend), str(loader)], env=env, check=True, timeout=45)
print('PASS: OBS restarted using the staged plugin and sidecar.')
