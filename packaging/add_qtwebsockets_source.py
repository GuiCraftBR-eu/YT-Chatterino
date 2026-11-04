"""Add the matching upstream Qt WebSockets sources to the Qt release archive."""
from pathlib import Path
import hashlib
import sys
import zipfile

archive, source = map(Path, sys.argv[1:3])
expected = 'fc6310c1b12d6e22a31062727aa6574ed69239d036c8a4247b3b089902709b11'
data = source.read_bytes()
if hashlib.sha256(data).hexdigest() != expected:
    raise SystemExit('Qt WebSockets source hash mismatch.')
name = 'Qt-6.8.3-Library-Sources/qtwebsockets-everywhere-src-6.8.3.tar.xz'
with zipfile.ZipFile(archive, 'a') as bundle:
    if name in bundle.namelist():
        raise SystemExit('Qt WebSockets sources are already present.')
    entry = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
    entry.compress_type = zipfile.ZIP_STORED
    entry.external_attr = 0o644 << 16
    bundle.writestr(entry, data)
with zipfile.ZipFile(archive) as bundle:
    if bundle.testzip() is not None or bundle.read(name) != data:
        raise SystemExit('Qt source archive failed validation.')
print('Added and verified the matching Qt WebSockets sources.')
