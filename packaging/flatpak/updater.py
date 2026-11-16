#!/usr/bin/env python3
"""Install and update the project's Flatpak bundles from verified GitHub releases."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request

REPO = 'GuiCraftBR-eu/YT-Chatterino'
API = f'https://api.github.com/repos/{REPO}/releases/latest'
APP_ID = 'io.github.GuiCraftBR_eu.YTChatterino'
OBS_ID = 'com.obsproject.Studio.Plugin.YTChatterino'
MAX_BYTES = 256 * 1024 * 1024
HOME_DIR = Path.home()
CONFIG = HOME_DIR / '.config/yt-chatterino-flatpak-updater'
INSTALL_DIR = HOME_DIR / '.local/share/yt-chatterino-flatpak-updater'
SYSTEMD = HOME_DIR / '.config/systemd/user'
STATE_FILE = CONFIG / 'state.json'


def release_assets(release):
    if release.get('draft') or release.get('prerelease'):
        raise ValueError('The latest release is not stable.')
    match = re.fullmatch(r'v(\d+\.\d+\.\d+)', str(release.get('tag_name', '')))
    if not match:
        raise ValueError('Invalid release version.')
    version = match.group(1)
    expected = {'app': f'YT-Chatterino-Linux-{version}.flatpak',
                'obs': f'YT-Chatterino-OBS-Linux-{version}.flatpak'}
    found = {}
    for asset in release.get('assets', []):
        for component, name in expected.items():
            if asset.get('name') != name:
                continue
            url = f'https://github.com/{REPO}/releases/download/v{version}/{name}'
            digest, size = asset.get('digest', ''), asset.get('size', 0)
            if (asset.get('browser_download_url') != url or not isinstance(size, int) or
                    size <= 0 or size > MAX_BYTES or not re.fullmatch(r'sha256:[a-f0-9]{64}', digest)):
                raise ValueError(f'Invalid {component} download metadata.')
            found[component] = {'name': name, 'url': url, 'sha256': digest[7:], 'size': size}
    return version, found


def request(url):
    return urllib.request.Request(url, headers={'User-Agent': 'YT-Chatterino-Flatpak-Updater',
                                               'X-GitHub-Api-Version': '2022-11-28'})


def download(asset, path):
    digest, count = hashlib.sha256(), 0
    with urllib.request.urlopen(request(asset['url']), timeout=60) as response, path.open('wb') as output:
        redirect = urllib.parse.urlparse(response.geturl())
        if redirect.scheme != 'https' or not (redirect.hostname == 'github.com' or
                                               (redirect.hostname or '').endswith('.githubusercontent.com')):
            raise ValueError('Unexpected download host.')
        while block := response.read(1024 * 1024):
            count += len(block)
            if count > asset['size'] or count > MAX_BYTES:
                raise ValueError('Download exceeds the expected size.')
            digest.update(block)
            output.write(block)
    if count != asset['size'] or digest.hexdigest() != asset['sha256']:
        raise ValueError('Download checksum or size does not match GitHub.')


def atomic_write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile('w', encoding='utf8', dir=path.parent, delete=False) as file:
        temporary = Path(file.name)
        file.write(content)
    os.replace(temporary, path)


def installed(app_id):
    return subprocess.run(['flatpak', 'info', '--user', app_id], stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL, check=False).returncode == 0


def version_tuple(value):
    if not isinstance(value, str) or not re.fullmatch(r'\d+\.\d+\.\d+', value):
        raise ValueError('Invalid installed version.')
    return tuple(int(part) for part in value.split('.'))


def migrate_legacy(component):
    """Copy settings once, without deleting or overwriting either profile."""
    if component == 'app':
        source = HOME_DIR
        destination = HOME_DIR / '.var/app' / APP_ID / 'config/YT-Chatterino'
    else:
        source = HOME_DIR / '.config/YT-Chatterino/OBS'
        destination = HOME_DIR / '.var/app/com.obsproject.Studio/config/YT-Chatterino/OBS'
    for name in ('.ytchatterino.json', '.ytchatterino_quota.json'):
        old, new = source / name, destination / name
        if not old.is_file() or new.exists():
            continue
        destination.mkdir(parents=True, exist_ok=True)
        try:
            with old.open('rb') as incoming, new.open('xb') as outgoing:
                os.chmod(new, 0o600)
                shutil.copyfileobj(incoming, outgoing)
        except FileExistsError:
            pass


def update(state, release):
    version, assets = release_assets(release)
    components = state.get('components', [])
    if not components or any(item not in ('app', 'obs') for item in components):
        raise ValueError('No valid components are configured.')
    missing = [item for item in components if item not in assets]
    if missing:
        raise ValueError('This release does not contain every requested Flatpak bundle.')
    if 'obs' in components and subprocess.run(['flatpak', 'info', 'com.obsproject.Studio'],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False).returncode:
        raise ValueError('Install the OBS Studio Flatpak from Flathub before its YT-Chatterino plugin.')
    subprocess.run(['flatpak', 'remote-add', '--user', '--if-not-exists', 'flathub',
                    'https://flathub.org/repo/flathub.flatpakrepo'], check=True)
    versions = state.setdefault('versions', {})
    for component in components:
        app_id = APP_ID if component == 'app' else OBS_ID
        if component in versions and version_tuple(versions[component]) > version_tuple(version):
            raise ValueError(f'Refusing to downgrade {component}.')
        if versions.get(component) == version and installed(app_id):
            continue
        with tempfile.TemporaryDirectory(prefix='yt-chatterino-flatpak-') as temporary:
            path = Path(temporary) / assets[component]['name']
            download(assets[component], path)
            subprocess.run(['flatpak', 'install', '--user', '--noninteractive', '--reinstall', str(path)], check=True)
        migrate_legacy(component)
        versions[component] = version
        atomic_write(STATE_FILE, json.dumps(state, indent=2) + '\n')
        print(f'Installed {component} {version}. Restart OBS to load an updated plugin.' if component == 'obs'
              else f'Installed {component} {version}.')


def setup_timer():
    INSTALL_DIR.mkdir(parents=True, exist_ok=True)
    target = INSTALL_DIR / 'updater.py'
    source = Path(__file__).resolve()
    if source != target.resolve():
        with tempfile.NamedTemporaryFile('wb', dir=INSTALL_DIR, delete=False) as output:
            temporary = Path(output.name)
            with source.open('rb') as input_file:
                shutil.copyfileobj(input_file, output)
        os.chmod(temporary, 0o700)
        os.replace(temporary, target)
    service = ('[Unit]\nDescription=Update YT-Chatterino Flatpaks\n'
               '[Service]\nType=oneshot\nExecStart=/usr/bin/env python3 '
               '"%h/.local/share/yt-chatterino-flatpak-updater/updater.py" --update\n')
    timer = ('[Unit]\nDescription=Check YT-Chatterino Flatpak updates\n'
             '[Timer]\nOnBootSec=5min\nOnUnitActiveSec=6h\nPersistent=true\n'
             '[Install]\nWantedBy=timers.target\n')
    atomic_write(SYSTEMD / 'yt-chatterino-flatpak-update.service', service)
    atomic_write(SYSTEMD / 'yt-chatterino-flatpak-update.timer', timer)
    subprocess.run(['systemctl', '--user', 'daemon-reload'], check=True)
    subprocess.run(['systemctl', '--user', 'enable', '--now', 'yt-chatterino-flatpak-update.timer'], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    choice = parser.add_mutually_exclusive_group(required=True)
    choice.add_argument('--app', action='store_true', help='Install the standalone application')
    choice.add_argument('--obs', action='store_true', help='Install the OBS Studio extension')
    choice.add_argument('--both', action='store_true', help='Install both components')
    choice.add_argument('--update', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if os.geteuid() == 0:
        raise ValueError('Run this as your normal user, without sudo.')
    CONFIG.mkdir(parents=True, exist_ok=True)
    with (CONFIG / 'update.lock').open('w') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print('Another update is already running.')
            return
        state = json.loads(STATE_FILE.read_text(encoding='utf8')) if STATE_FILE.exists() else {}
        if not args.update:
            selected = ['app', 'obs'] if args.both else ['app' if args.app else 'obs']
            state['components'] = sorted(set(state.get('components', [])) | set(selected))
        with urllib.request.urlopen(request(API), timeout=30) as response:
            raw = response.read(4 * 1024 * 1024 + 1)
            if len(raw) > 4 * 1024 * 1024:
                raise ValueError('Release metadata is too large.')
        update(state, json.loads(raw))
        if not args.update:
            setup_timer()
            print('Automatic update checks are enabled for this user.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'YT-Chatterino Flatpak updater: {error}', file=sys.stderr)
        sys.exit(1)
