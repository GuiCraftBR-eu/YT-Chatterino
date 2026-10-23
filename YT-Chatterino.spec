# -*- mode: python ; coding: utf-8 -*-
from pathlib import Path
from PyInstaller.utils.hooks import collect_data_files

source = Path(SPECPATH) / 'src'
assets = ['ytchatterino.ico', 'ChatternoPing.wav', 'ytchatterino-logo.png', 'ytchatterino-logo.svg']
a = Analysis(
    [str(source / 'ytchatterino.pyw')],
    pathex=[str(source)], binaries=[],
    datas=[(str(source / name), '.') for name in assets] + collect_data_files('tzdata'),
    hiddenimports=['ytchatterino_features', 'ytchatterino_settings', 'ytchatterino_chat', 'ytchatterino_youtube', 'ytchatterino_i18n', 'ytchatterino_translations', 'tzdata', 'PIL.Image', 'PIL.PngImagePlugin', 'PIL.JpegImagePlugin'],
    hookspath=[], hooksconfig={}, runtime_hooks=[], excludes=[],
    noarchive=False, optimize=0,
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz, a.scripts, [], exclude_binaries=True, name='YT-Chatterino',
    debug=False, bootloader_ignore_signals=False, strip=False, upx=False,
    console=False, disable_windowed_traceback=False, argv_emulation=False,
    target_arch=None, codesign_identity=None, entitlements_file=None,
    icon=[str(source / 'ytchatterino.ico')],
)
coll = COLLECT(exe, a.binaries, a.datas, strip=False, upx=False, name='YT-Chatterino')
