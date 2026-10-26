"""Build and validate the portable Windows release from a tested Qt build."""
from pathlib import Path
import argparse
import json
import shutil
import subprocess
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('--exe', type=Path, required=True)
parser.add_argument('--stage', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--version', required=True)
parser.add_argument('--skip-source-archive', action='store_true',
                    help='Only for local package checks before changes are committed.')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
stage, output = args.stage.resolve(), args.output.resolve()
if stage.exists():
    raise SystemExit('Use a new staging directory.')
stage.mkdir(parents=True)
output.mkdir(parents=True, exist_ok=True)
exe = stage / 'YT-Chatterino.exe'
shutil.copy2(args.exe, exe)
subprocess.run(['windeployqt', '--release', '--no-opengl-sw', '--no-system-d3d-compiler',
                '--dir', str(stage), str(exe)], check=True)
for name in ('README.txt', 'README-PTBR.txt'):
    shutil.copy2(root / 'docs' / name, stage / name)
shutil.copytree(root / 'Licenses', stage / 'Licenses')
(stage / 'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=.\n', encoding='utf-8')

# The 1.x Windows updater checks for these files before installing a native release.
source = stage / 'Source'
source.mkdir()
(source / 'ytchatterino.pyw').write_text(
    '# Compatibility metadata only; run ../YT-Chatterino.exe.\n'
    f'APP_VERSION = "{args.version}"\n', encoding='utf-8')
for name in ('features', 'chat', 'threads', 'window', 'update', 'i18n', 'settings',
             'youtube', 'translations', 'chat_translations'):
    (source / f'ytchatterino_{name}.py').write_text(
        '# Unused compatibility marker for the 1.x updater.\n', encoding='utf-8')
(source / 'README.txt').write_text(
    'Unused compatibility markers for the 1.x updater. The application is native C++/Qt.\n',
    encoding='utf-8')
(stage / '_internal').mkdir()
(stage / '_internal' / 'legacy-update-marker.txt').write_text(
    'Legacy updater validation marker; no Python runtime is included.\n', encoding='utf-8')

required = ['YT-Chatterino.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll',
            'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll',
            'platforms/qwindows.dll', 'tls/qschannelbackend.dll', 'imageformats/qwebp.dll',
            'README.txt', 'README-PTBR.txt', 'qt.conf']
for name in required:
    if not (stage / name).is_file():
        raise SystemExit(f'Missing required release file: {name}')
files = sorted(path.relative_to(stage).as_posix() for path in stage.rglob('*') if path.is_file())
for name in files:
    if any(secret in name.lower() for secret in
           ('google-oauth-client', 'client_secret', '.ytchatterino', 'credentials',
            'python.exe', 'pythonw.exe', 'python3.dll')):
        raise SystemExit(f'Private file or Python runtime in release: {name}')
files.append('update-manifest.json')
(stage / 'update-manifest.json').write_text(
    json.dumps({'version': args.version, 'runtime': 'native-cpp-qt6', 'files': files}, indent=2) + '\n',
    encoding='utf-8')
package = output / f'YT-Chatterino-Windows11-{args.version}.zip'
with zipfile.ZipFile(package, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for name in sorted(files):
        archive.write(stage / name, 'YT-Chatterino/' + name)
with zipfile.ZipFile(package) as archive:
    if archive.testzip() is not None:
        raise SystemExit('Windows ZIP failed its integrity check.')
    names = set(archive.namelist())
    if names != {'YT-Chatterino/' + name for name in files}:
        raise SystemExit('Windows ZIP and updater manifest differ.')
    manifest = json.loads(archive.read('YT-Chatterino/update-manifest.json'))
    if manifest['version'] != args.version or set(manifest['files']) != set(files):
        raise SystemExit('Invalid updater manifest.')
print(f'Validated {package} with {len(files)} managed files.')

if not args.skip_source_archive:
    # Generated build/test files can be untracked; git archive never includes them.
    # Tracked changes would make the compiled sources differ from the archive.
    status = subprocess.run(['git', '-C', str(root), 'status', '--porcelain', '--untracked-files=no'],
                            check=True, capture_output=True, text=True)
    if status.stdout.strip():
        raise SystemExit('Tracked files changed during the build; source archive would not match:\n'
                         + status.stdout)
    source_package = output / f'YT-Chatterino-Cpp-Source-{args.version}.zip'
    subprocess.run(['git', '-C', str(root), 'archive', '--format=zip',
                    f'--prefix=YT-Chatterino-{args.version}/', '-o', str(source_package), 'HEAD'], check=True)
    with zipfile.ZipFile(source_package) as archive:
        if archive.testzip() is not None:
            raise SystemExit('Source ZIP failed its integrity check.')
    print(f'Validated {source_package}.')
