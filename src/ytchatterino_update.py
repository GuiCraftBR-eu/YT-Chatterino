"""Release checks and transactional updates. No account credentials are used."""
from pathlib import Path, PurePosixPath
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import uuid

REPOSITORY = 'GuiCraftBR-eu/YT-Chatterino'
LATEST_URL = 'https://api.github.com/repos/' + REPOSITORY + '/releases/latest'
DOWNLOAD_PREFIX = 'https://github.com/' + REPOSITORY + '/releases/download/'
CHECK_INTERVAL_MS = 6 * 60 * 60 * 1000
MAX_DOWNLOAD = 256 * 1024 * 1024
MAX_EXPANDED = 512 * 1024 * 1024
SOURCE_SUFFIXES = {'.py', '.pyw', '.png', '.svg', '.ico', '.wav'}


def version_tuple(value):
    match = re.fullmatch(r'v?(\d+)\.(\d+)\.(\d+)', str(value))
    if not match:
        raise ValueError('Invalid release version.')
    return tuple(map(int, match.groups()))


def installation_kind(root=None):
    root = Path(root or Path(__file__).parent).resolve()
    if getattr(sys, 'frozen', False) and sys.platform == 'win32':
        return 'windows'
    if sys.platform.startswith('linux') and root == Path('/usr/share/yt-chatterino'):
        return 'pacman'
    return 'source'


def release_info(data, current, kind):
    """Accept only stable releases and exact assets from this repository."""
    if data.get('draft') or data.get('prerelease'):
        return None
    version = str(data.get('tag_name', '')).removeprefix('v')
    if version_tuple(version) <= version_tuple(current):
        return None
    if kind == 'windows':
        pattern = r'YT-Chatterino-Windows11-' + re.escape(version) + r'\.zip'
    elif kind == 'pacman':
        pattern = r'yt-chatterino-' + re.escape(version) + r'-\d+-any\.pkg\.tar\.zst'
    else:
        pattern = r'YT-Chatterino-ArchLinux-' + re.escape(version) + r'\.tar\.gz'
    for asset in data.get('assets', []):
        if not re.fullmatch(pattern, str(asset.get('name', ''))):
            continue
        url = asset.get('browser_download_url', '')
        expected = DOWNLOAD_PREFIX + 'v' + version + '/' + asset['name']
        digest = asset.get('digest', '')
        size = asset.get('size', 0)
        if url != expected or not re.fullmatch(r'sha256:[0-9a-f]{64}', str(digest)):
            raise ValueError('The release has no valid download checksum.')
        if not isinstance(size, int) or not 0 < size <= MAX_DOWNLOAD:
            raise ValueError('Invalid update download size.')
        return {'version':version, 'url':url, 'name':asset['name'], 'size':size,
                'sha256':digest.split(':')[1], 'published':data.get('published_at', ''), 'kind':kind}
    raise ValueError('The update package is not available yet. Check again later.')


def check_release(current, kind):
    request = urllib.request.Request(LATEST_URL, headers={
        'User-Agent':'YT-Chatterino/' + current, 'Accept':'application/vnd.github+json',
        'X-GitHub-Api-Version':'2022-11-28'})
    with urllib.request.urlopen(request, timeout=15) as response:
        raw = response.read(2 * 1024 * 1024 + 1)
    if len(raw) > 2 * 1024 * 1024:
        raise ValueError('Release information is too large.')
    return release_info(json.loads(raw), current, kind)


def download_asset(info, destination, progress=lambda done, total: None):
    request = urllib.request.Request(info['url'], headers={'User-Agent':'YT-Chatterino updater'})
    digest, size = hashlib.sha256(), 0
    with urllib.request.urlopen(request, timeout=30) as response, Path(destination).open('wb') as output:
        while True:
            chunk = response.read(128 * 1024)
            if not chunk:
                break
            size += len(chunk)
            if size > min(MAX_DOWNLOAD, info['size']):
                raise ValueError('The update download is larger than expected.')
            output.write(chunk)
            digest.update(chunk)
            progress(size, info['size'])
    if size != info['size'] or digest.hexdigest() != info['sha256']:
        raise ValueError('Update checksum verification failed. No files were changed.')


def safe_member(name):
    path = PurePosixPath(name)
    if not name or '\\' in name or ':' in name or '\x00' in name or path.is_absolute() or '..' in path.parts:
        raise ValueError('Unsafe update archive path.')
    return path


def extract_archive(archive, destination):
    """Manual extraction rejects links, traversal, duplicates and oversized data."""
    import tarfile
    import zipfile
    destination = Path(destination).resolve()
    total, seen = 0, set()
    def target(name, size):
        nonlocal total
        relative = safe_member(name)
        key = relative.as_posix().casefold()
        if key in seen or len(seen) >= 5000:
            raise ValueError('Duplicate or excessive update archive entries.')
        seen.add(key)
        total += size
        if size < 0 or total > MAX_EXPANDED:
            raise ValueError('Update archive is too large.')
        result = destination.joinpath(*relative.parts)
        if not result.resolve().is_relative_to(destination):
            raise ValueError('Unsafe update extraction destination.')
        result.parent.mkdir(parents=True, exist_ok=True)
        return result
    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as opened:
            for entry in opened.infolist():
                safe_member(entry.orig_filename)
                if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                    raise ValueError('Links are not allowed in updates.')
                if entry.is_dir():
                    safe_member(entry.filename)
                    continue
                out = target(entry.filename, entry.file_size)
                with opened.open(entry) as src, out.open('wb') as dst:
                    shutil.copyfileobj(src, dst)
    else:
        with tarfile.open(archive, 'r:gz') as opened:
            for entry in opened:
                if entry.isdir():
                    safe_member(entry.name)
                    continue
                if not entry.isfile():
                    raise ValueError('Links and special files are not allowed in updates.')
                out = target(entry.name, entry.size)
                with opened.extractfile(entry) as src, out.open('wb') as dst:
                    shutil.copyfileobj(src, dst)


def source_version(source):
    text = (Path(source)/'ytchatterino.pyw').read_text(encoding='utf-8-sig')
    match = re.search(r'^APP_VERSION = [\'"]([0-9.]+)[\'"]', text, re.M)
    if not match:
        raise ValueError('The update is missing its application version.')
    return match[1]


def validate_target(target, kind):
    target = Path(target).resolve()
    if target == target.parent or target.is_symlink():
        raise ValueError('Invalid application folder.')
    marker = target/'YT-Chatterino.exe' if kind == 'windows' else target/'ytchatterino.pyw'
    if not marker.is_file():
        raise ValueError('The application folder could not be identified.')
    if kind == 'pacman' and target != Path('/usr/share/yt-chatterino'):
        raise ValueError('Invalid native package location.')
    return target


def prepare_update(info, target, progress=lambda done, total: None, downloader=download_asset):
    import tempfile
    kind = info['kind']
    target = validate_target(target, kind)
    if kind == 'pacman' and (not shutil.which('pkexec') or not shutil.which('pacman')):
        raise RuntimeError('Install polkit to allow package updates from the app.')
    # Source/portable updates stage on the same volume for an atomic folder swap.
    parent = Path(tempfile.gettempdir()) if kind == 'pacman' else target.parent
    container = Path(tempfile.mkdtemp(prefix='.ytchatterino-update-', dir=parent)).resolve()
    try:
        archive = container/info['name']
        downloader(info, archive, progress)
        if kind == 'pacman':
            stage = archive
        else:
            extracted = container/'extracted'
            extracted.mkdir()
            extract_archive(archive, extracted)
            if kind == 'windows':
                stage = extracted/'YT-Chatterino'
                if not (stage/'YT-Chatterino.exe').is_file() or not (stage/'_internal').is_dir():
                    raise ValueError('The Windows update package is incomplete.')
                source = stage/'Source'
            else:
                source = extracted/('YT-Chatterino-' + info['version'])/'src'
                stage = source
            if source_version(source) != info['version']:
                raise ValueError('The update version does not match the release.')
            required = ('ytchatterino_features.py', 'ytchatterino_chat.py', 'ytchatterino_update.py',
                        'ytchatterino_i18n.py', 'ytchatterino_settings.py', 'ytchatterino_youtube.py',
                        'ytchatterino_translations.py', 'ytchatterino_chat_translations.py')
            if any(not (source/name).is_file() for name in required):
                raise ValueError('The update source is incomplete.')
        manifest = {'kind':kind, 'target':str(target), 'stage':str(stage), 'container':str(container),
            'version':info['version'], 'parent_pid':os.getpid(), 'python':sys.executable,
            'backup':str(target.parent/('.ytchatterino-backup-' + uuid.uuid4().hex))}
        path = container/'update.json'
        path.write_text(json.dumps(manifest), encoding='utf-8')
        return path
    except Exception:
        if container.parent == parent.resolve() and container.name.startswith('.ytchatterino-update-'):
            shutil.rmtree(container)
        raise


def helper_command(manifest):
    container = Path(manifest).parent
    if getattr(sys, 'frozen', False):
        helper = container/'helper'
        helper.mkdir()
        exe = helper/'YT-Chatterino.exe'
        shutil.copy2(sys.executable, exe)
        shutil.copytree(Path(sys.executable).parent/'_internal', helper/'_internal')
        return [str(exe), '--apply-update', str(manifest)]
    script = container/'update-helper.py'
    shutil.copy2(__file__, script)
    return [sys.executable, str(script), '--apply-update', str(manifest)]


def wait_for_process(pid, timeout=90):
    if pid <= 0 or pid == os.getpid():
        raise ValueError('Invalid updater parent process.')
    if sys.platform == 'win32':
        import ctypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        kernel.OpenProcess.restype = ctypes.c_void_p
        kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.OpenProcess(0x00100000, False, pid)
        if not handle:
            if ctypes.get_last_error() == 87:
                return
            raise OSError('Could not wait for the application to close.')
        try:
            if kernel.WaitForSingleObject(handle, int(timeout*1000)) != 0:
                raise TimeoutError('The application is still running. Close all app windows and try again.')
        finally:
            kernel.CloseHandle(handle)
    else:
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                return
            time.sleep(.2)
        raise TimeoutError('The application did not close in time.')


def preserve_user_files(target, stage):
    manifest = target/'update-manifest.json'
    if manifest.is_file():
        old_files = {str(safe_member(p)).casefold() for p in json.loads(manifest.read_text(encoding='utf-8'))['files']}
    else:
        old_files = {p.relative_to(stage).as_posix().casefold() for p in stage.rglob('*') if p.is_file()}
    for path in target.rglob('*'):
        if path.is_symlink():
            raise ValueError('Move symbolic links out of the application folder before updating.')
        if not path.is_file():
            continue
        relative = path.relative_to(target)
        if relative.as_posix().casefold() not in old_files:
            destination = stage/relative
            if not destination.exists():
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, destination)


def apply_files(manifest):
    """Return a rollback function. Paths are checked before any directory move."""
    kind = manifest['kind']
    target = validate_target(manifest['target'], kind)
    container, stage, backup = (Path(manifest[k]).resolve() for k in ('container', 'stage', 'backup'))
    if (not container.name.startswith('.ytchatterino-update-') or not stage.is_relative_to(container)
            or backup.parent != target.parent or not backup.name.startswith('.ytchatterino-backup-')
            or backup.exists() or (kind != 'pacman' and container.parent != target.parent)):
        raise ValueError('Invalid update installation paths.')
    if kind == 'pacman':
        if stage.suffixes[-3:] != ['.pkg', '.tar', '.zst']:
            raise ValueError('Invalid native package.')
        subprocess.run(['pkexec', 'pacman', '-U', '--noconfirm', str(stage)], check=True)
        return lambda: None
    if kind == 'windows':
        if source_version(stage/'Source') != manifest['version']:
            raise ValueError('Staged update version changed.')
        preserve_user_files(target, stage)
        os.replace(target, backup)
        try:
            os.replace(stage, target)
        except Exception:
            os.replace(backup, target)
            raise
        def rollback():
            failed = container/'failed-install'
            os.replace(target, failed)
            os.replace(backup, target)
        return rollback
    if source_version(stage) != manifest['version']:
        raise ValueError('Staged update version changed.')
    paths = [p for p in stage.iterdir() if p.is_file() and p.suffix in SOURCE_SUFFIXES]
    backup.mkdir()
    installed = []
    def rollback():
        for path, existed in reversed(installed):
            destination = target/path.name
            if existed:
                shutil.copy2(backup/path.name, destination)
            elif destination.exists():
                destination.unlink()
    try:
        for path in paths:
            destination = target/path.name
            existed = destination.is_file()
            if destination.is_symlink():
                raise ValueError('Symbolic links are not allowed in the source folder.')
            if existed:
                shutil.copy2(destination, backup/path.name)
            installed.append((path, existed))
            os.replace(path, destination)
    except Exception:
        rollback()
        raise
    return rollback


def restart_command(manifest, updated=True):
    target = Path(manifest['target'])
    command = [str(target/'YT-Chatterino.exe')] if manifest['kind'] == 'windows' else [manifest['python'], str(target/'ytchatterino.pyw')]
    if updated:
        command += ['--updated', str(Path(manifest['container'])/'ready.json')]
    return command


def run_helper(path):
    rollback, manifest, parent_exited = None, None, False
    try:
        path = Path(path).resolve()
        manifest = json.loads(path.read_text(encoding='utf-8'))
        if path != Path(manifest['container']).resolve()/'update.json':
            raise ValueError('Invalid update manifest.')
        wait_for_process(int(manifest['parent_pid']))
        parent_exited = True
        rollback = apply_files(manifest)
        environment = dict(os.environ, PYINSTALLER_RESET_ENVIRONMENT='1')
        process = subprocess.Popen(restart_command(manifest), cwd=manifest['target'], env=environment)
        ready = path.parent/'ready.json'
        until = time.monotonic() + 40
        while time.monotonic() < until:
            if ready.is_file():
                (path.parent/'done.json').write_text(json.dumps({'success':True, 'version':manifest['version']}))
                return 0
            if process.poll() is not None:
                break
            time.sleep(.2)
        # Never replace files while a partially started new process holds them.
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        raise RuntimeError('The updated app did not finish starting.')
    except Exception as error:
        if rollback:
            try:
                rollback()
            except Exception as restore_error:
                error = RuntimeError(str(error) + '\nRestore: ' + str(restore_error))
        if manifest:
            (Path(manifest['container'])/'error.txt').write_text(str(error), encoding='utf-8')
            if parent_exited:
                try:
                    subprocess.Popen(restart_command(manifest, False), cwd=manifest['target'], env=dict(os.environ, PYINSTALLER_RESET_ENVIRONMENT='1'))
                except OSError:
                    pass
        try:
            import tkinter as tk
            from tkinter import messagebox
            root = tk.Tk(); root.withdraw()
            messagebox.showerror('YT-Chatterino update', str(error), parent=root)
            root.destroy()
        except Exception:
            pass
        return 1


def acknowledge_update(path):
    """Called only after the new main window has initialized successfully."""
    ready = Path(path).resolve()
    container = ready.parent
    if ready.name != 'ready.json' or not container.name.startswith('.ytchatterino-update-') or not (container/'update.json').is_file():
        return
    ready.write_text(json.dumps({'pid':os.getpid()}), encoding='utf-8')
    def cleanup():
        for _ in range(100):
            time.sleep(.3)
            if (container/'done.json').is_file():
                try:
                    shutil.rmtree(container)
                    return
                except OSError:
                    pass
    threading.Thread(target=cleanup, daemon=True, name='update-cleanup').start()


class UpdateController:
    def __init__(self, app, core):
        self.app, self.core = app, core
        self.kind = installation_kind(Path(core.__file__).parent)
        self.target = Path(sys.executable).parent if self.kind == 'windows' else Path(core.__file__).resolve().parent
        self.available, self.busy, self.closed = None, False, False
        self.timer, self.window, self.status, self.install_button = None, None, None, None
        self.state = 'idle'
        self.error = ''

    def start(self):
        if self.app.cfg.get('automatic_updates', True):
            self.timer = self.app.root.after(2500, self.check)

    def settings_page(self, parent):
        from ytchatterino_i18n import tr
        body = self.app.features
        body.label(parent, tr('Installed version: {version}', version=self.core.APP_VERSION)).pack(anchor='w', pady=(0, 10))
        automatic = self.core.tk.BooleanVar(value=self.app.cfg.get('automatic_updates', True))
        def changed():
            self.app.cfg['automatic_updates'] = automatic.get()
            self.core.save_config(self.app.cfg)
            if automatic.get():
                self.check()
            elif self.timer:
                self.app.root.after_cancel(self.timer); self.timer = None
        body.check(parent, 'Check for updates automatically', automatic, changed).pack(anchor='w', pady=5)
        body.label(parent, 'Checks run in the background at startup and every six hours. The download arrow appears beside the YouTube balance when a new version is ready.', dim=True).pack(fill='x', pady=8)
        body.button(parent, 'Check now / install update…', self.show).pack(anchor='w', pady=8)
        body.label(parent, 'Updates require confirmation before downloading and restarting. Your previous app files are kept in a backup folder.', dim=True).pack(fill='x', pady=8)

    def check(self):
        if self.closed or self.busy:
            return
        if self.timer:
            self.app.root.after_cancel(self.timer); self.timer = None
        self.busy, self.state, self.error = True, 'checking', ''
        self.render()
        def work():
            try:
                result = check_release(self.core.APP_VERSION, self.kind)
                self.app.call_ui(lambda: self.checked(result))
            except Exception as error:
                text = str(error)
                self.app.call_ui(lambda: self.checked(None, text))
        threading.Thread(target=work, daemon=True, name='update-check').start()

    def checked(self, info, error=''):
        if self.closed:
            return
        self.busy, self.error = False, error
        if not error:
            self.available = info
        self.state = 'error' if error else 'available' if info else 'current'
        button = self.app.update_button
        if self.available:
            button.pack(side='right', after=self.app.gear)
        else:
            button.pack_forget()
        self.app.update_quota()
        self.render()
        if self.app.cfg.get('automatic_updates', True):
            delay = 15*60*1000 if error else CHECK_INTERVAL_MS
            self.timer = self.app.root.after(delay, self.check)

    def show(self):
        from ytchatterino_i18n import tr, display_date
        if self.window and self.window.winfo_exists():
            self.window.lift(); return
        self.window = self.app.features.dialog('App updates', 530, 320)
        window = self.window
        body = self.app.features
        body.label(window, tr('Installed version: {version}', version=self.core.APP_VERSION)).pack(anchor='w', padx=16, pady=(16, 8))
        self.status = body.label(window, '')
        self.status.pack(fill='x', padx=16, pady=8)
        body.label(window, 'Installing closes and restarts the app. Your accounts and settings are kept.', dim=True).pack(fill='x', padx=16, pady=8)
        if self.kind == 'pacman':
            body.label(window, 'Arch Linux updates use pacman and may ask for administrator authorization.', dim=True).pack(fill='x', padx=16)
        row = self.core.tk.Frame(window, bg=self.core.BG)
        row.pack(fill='x', padx=16, pady=12)
        self.install_button = body.button(row, 'Install update and restart', self.install)
        self.install_button.pack(side='right', padx=4)
        body.button(row, 'Check now', self.check).pack(side='left')
        body.button(row, 'Close', window.destroy).pack(side='left', padx=4)
        self.render()
        if self.state == 'idle':
            self.check()

    def render(self, progress=None):
        from ytchatterino_i18n import tr, display_date
        if not self.status or not self.status.winfo_exists():
            return
        if progress is not None:
            text = tr('Downloading update… {percent}%', percent=progress)
        elif self.state == 'checking':
            text = tr('Checking for updates…')
        elif self.state == 'installing':
            text = tr('Verifying and preparing update…')
        elif self.available:
            text = tr('Version {version} is available · {date}', version=self.available['version'], date=display_date(self.available.get('published', '')[:10]))
            if self.error:
                text = str(text) + '\n' + str(tr(self.error))
        elif self.error:
            text = tr('Update check failed: {error}', error=self.error)
        else:
            text = tr('Your app is up to date.')
        self.status.configure(text=text)
        self.install_button.configure(state='normal' if self.available and not self.busy else 'disabled')

    def install(self):
        if self.closed or self.busy or not self.available:
            return
        self.busy, self.state, self.error = True, 'installing', ''
        self.render()
        info = dict(self.available)
        def progress(done, total):
            value = int(done * 100 / max(1, total))
            self.app.call_ui(lambda: self.render(value))
        def work():
            try:
                path = prepare_update(info, self.target, progress)
                command = helper_command(path)
                self.app.call_ui(lambda: self.launch(command, path))
            except Exception as error:
                text = str(error)
                self.app.call_ui(lambda: self.install_failed(text))
        threading.Thread(target=work, daemon=True, name='update-download').start()

    def install_failed(self, error):
        if not self.closed:
            self.busy, self.state, self.error = False, 'error', error
            self.render()

    def launch(self, command, path):
        if self.closed:
            return
        try:
            options = {'cwd':str(Path(path).parent)}
            if sys.platform == 'win32':
                options['creationflags'] = subprocess.CREATE_NO_WINDOW
            options['env'] = dict(os.environ, PYINSTALLER_RESET_ENVIRONMENT='1')
            subprocess.Popen(command, **options)
            self.app.quit()
        except Exception as error:
            self.install_failed(str(error))

    def close(self):
        self.closed = True
        if self.timer:
            self.app.root.after_cancel(self.timer)
            self.timer = None


if __name__ == '__main__' and len(sys.argv) == 3 and sys.argv[1] == '--apply-update':
    raise SystemExit(run_helper(sys.argv[2]))
