import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

if sys.platform == 'win32':
    sys.modules['fcntl'] = types.SimpleNamespace()
spec = importlib.util.spec_from_file_location('flatpak_updater',
    Path(__file__).resolve().parents[1] / 'packaging/flatpak/updater.py')
updater = importlib.util.module_from_spec(spec)
spec.loader.exec_module(updater)


def release(version='2.5.6', content=b'bundle'):
    assets = []
    for name in (f'YT-Chatterino-Linux-{version}.flatpak', f'YT-Chatterino-OBS-Linux-{version}.flatpak'):
        assets.append({'name': name, 'size': len(content),
            'browser_download_url': f'https://github.com/{updater.REPO}/releases/download/v{version}/{name}',
            'digest': 'sha256:' + hashlib.sha256(content).hexdigest()})
    return {'tag_name': 'v' + version, 'assets': assets}


class UpdaterTests(unittest.TestCase):
    def test_requires_exact_release_urls_and_digests(self):
        version, assets = updater.release_assets(release())
        self.assertEqual(version, '2.5.6')
        self.assertEqual(set(assets), {'app', 'obs'})
        bad = release()
        bad['assets'][0]['browser_download_url'] = 'https://example.com/bundle.flatpak'
        with self.assertRaises(ValueError):
            updater.release_assets(bad)
        bad = release()
        bad['assets'][0]['digest'] = 'sha256:incorrect'
        with self.assertRaises(ValueError):
            updater.release_assets(bad)

    def test_download_rejects_changed_bytes_and_hosts(self):
        _, assets = updater.release_assets(release())
        class Response:
            def __init__(self, body, url): self.body, self.url = body, url
            def __enter__(self): return self
            def __exit__(self, *_): pass
            def geturl(self): return self.url
            def read(self, _): data, self.body = self.body, b''; return data
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'bundle.flatpak'
            with patch.object(updater.urllib.request, 'urlopen', return_value=Response(b'bundle', 'https://release-assets.githubusercontent.com/a')):
                updater.download(assets['app'], path)
            self.assertEqual(path.read_bytes(), b'bundle')
            for body, url in [(b'bundle', 'https://evil.example/a'), (b'tamper', 'https://github.com/a')]:
                with patch.object(updater.urllib.request, 'urlopen', return_value=Response(body, url)):
                    with self.assertRaises(ValueError): updater.download(assets['app'], path)

    def test_install_skips_current_and_refuses_downgrade(self):
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(updater, 'STATE_FILE', Path(directory) / 'state.json'), \
                patch.object(updater, 'installed', return_value=True), \
                patch.object(updater.subprocess, 'run') as run, \
                patch.object(updater, 'download', side_effect=lambda asset, path: path.write_bytes(b'bundle')) as download:
            run.return_value = types.SimpleNamespace(returncode=0)
            state = {'components': ['app'], 'versions': {'app': '2.5.5'}}
            updater.update(state, release())
            self.assertEqual(json.loads(updater.STATE_FILE.read_text())['versions']['app'], '2.5.6')
            self.assertTrue(any('--reinstall' in call.args[0] for call in run.call_args_list))
            updater.update(state, release())
            self.assertEqual(download.call_count, 1)
            state['versions']['app'] = '3.0.0'
            with self.assertRaises(ValueError): updater.update(state, release())

    def test_migration_copies_only_missing_profiles(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(updater, 'HOME_DIR', Path(directory)):
            old = Path(directory) / '.ytchatterino.json'
            old.write_text('{"theme":"dark"}', encoding='utf8')
            updater.migrate_legacy('app')
            new = Path(directory) / '.var/app' / updater.APP_ID / 'config/YT-Chatterino/.ytchatterino.json'
            self.assertEqual(new.read_text(encoding='utf8'), old.read_text(encoding='utf8'))
            new.write_text('{"new":true}', encoding='utf8')
            updater.migrate_legacy('app')
            self.assertEqual(new.read_text(encoding='utf8'), '{"new":true}')
            self.assertTrue(old.exists())


if __name__ == '__main__':
    unittest.main()
