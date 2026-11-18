"""Exercise update-feed generation without publishing or using real signatures."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile


SCRIPT = Path(__file__).resolve().parents[1] / 'packaging' / 'firefox_update.py'
ADDON_ID = 'vod-sync@yt-chatterino.local'
UPDATE_URL = 'https://raw.githubusercontent.com/GuiCraftBR-eu/YT-Chatterino/main/browser-extension/updates.json'


class FirefoxUpdateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.xpi = self.root / 'test.xpi'
        self.output = self.root / 'updates.json'

    def make_xpi(self, version):
        manifest = {'version': version, 'browser_specific_settings': {'gecko': {
            'id': ADDON_ID, 'update_url': UPDATE_URL}}}
        with zipfile.ZipFile(self.xpi, 'w') as archive:
            archive.writestr('manifest.json', json.dumps(manifest))
            # Only the presence check is under test, not cryptographic verification.
            archive.writestr('META-INF/mozilla.rsa', b'test fixture')

    def run_script(self, *args):
        return subprocess.run([sys.executable, str(SCRIPT), str(self.xpi),
                               '--output', str(self.output), *args],
                              capture_output=True, text=True)

    def test_four_component_extension_uses_application_release_url(self):
        self.make_xpi('2.5.7.1')
        result = self.run_script('--release-version', '2.5.7')
        self.assertEqual(result.returncode, 0, result.stderr)
        entry = json.loads(self.output.read_text())['addons'][ADDON_ID]['updates'][0]
        self.assertEqual(entry, {
            'version': '2.5.7.1',
            'update_link': 'https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/download/v2.5.7/YT-Chatterino-Firefox-2.5.7.xpi',
            'update_hash': 'sha256:' + hashlib.sha256(self.xpi.read_bytes()).hexdigest(),
        })

    def test_four_component_version_requires_matching_release(self):
        self.make_xpi('2.5.7.1')
        for args in ((), ('--release-version', '2.5.8')):
            with self.subTest(args=args):
                self.assertNotEqual(self.run_script(*args).returncode, 0)
                self.assertFalse(self.output.exists())

    def test_three_component_version_keeps_existing_behavior(self):
        self.make_xpi('2.5.7')
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        entry = json.loads(self.output.read_text())['addons'][ADDON_ID]['updates'][0]
        self.assertEqual(entry['version'], '2.5.7')
        self.assertTrue(entry['update_link'].endswith('/v2.5.7/YT-Chatterino-Firefox-2.5.7.xpi'))


if __name__ == '__main__':
    unittest.main()
