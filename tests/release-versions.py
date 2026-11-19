"""Check that the app, OBS bootstrap, release workflow, and Flatpaks agree on the release version."""
from pathlib import Path
import os
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def project_version(path, project):
    content = (ROOT / path).read_text(encoding="utf-8")
    match = re.search(r"project\(" + re.escape(project) + r"\s+VERSION\s+(\d+\.\d+\.\d+)", content)
    if not match:
        raise AssertionError(f"Missing version in {path}")
    return match.group(1)


class ReleaseVersions(unittest.TestCase):
    def test_app_obs_workflow_and_flatpak_versions_match(self):
        version = project_version("CMakeLists.txt", "YTChatterino")
        self.assertEqual(project_version("obs-plugin/CMakeLists.txt", "YTChatterinoObs"), version)
        bootstrap = os.environ.get("YT_OBS_BOOTSTRAP_VERIFY")
        if bootstrap:
            self.assertIn(version.encode("utf-16le"), Path(bootstrap).read_bytes(), "OBS bootstrap binary version")
        workflow = (ROOT / ".github/workflows/native-checks.yml").read_text(encoding="utf-8")
        self.assertIn(f"RELEASE_TAG: v{version}", workflow)
        self.assertIn(f"startsWith(github.event.head_commit.message, 'Release {version}')", workflow)
        for name in ("io.github.GuiCraftBR_eu.YTChatterino.metainfo.xml",
                     "com.obsproject.Studio.Plugin.YTChatterino.metainfo.xml"):
            root = ET.parse(ROOT / "packaging/flatpak" / name).getroot()
            release = root.find("./releases/release")
            self.assertIsNotNone(release, name)
            self.assertEqual(release.get("version"), version, name)


if __name__ == "__main__":
    unittest.main()
