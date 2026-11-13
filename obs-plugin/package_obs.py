"""Package an OBS 33+ Windows dock and the separately built native chat."""
import argparse
import json
from pathlib import Path, PurePosixPath
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--plugin', required=True, type=Path)
    parser.add_argument('--windows-zip', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--version', required=True)
    args = parser.parse_args()

    plugin = args.plugin.read_bytes()
    if plugin[:2] != b'MZ':
        raise SystemExit('OBS dock is not a Windows DLL.')
    args.output.mkdir(parents=True, exist_ok=True)
    destination = args.output / f'YT-Chatterino-OBS-Windows-{args.version}.zip'
    forbidden = ('client_secret', 'google-oauth-client', '.ytchatterino', 'credentials', '.dpapi')
    with zipfile.ZipFile(args.windows_zip) as source:
        names = source.namelist()
        if len(names) != len(set(names)) or source.testzip():
            raise SystemExit('Invalid Windows archive.')
        prefix = 'YT-Chatterino/'
        if not names or any(not name.startswith(prefix) or '\\' in name for name in names):
            raise SystemExit('Unexpected archive root.')
        relative = [name[len(prefix):] for name in names]
        if any(not name or name.startswith('/') or '..' in PurePosixPath(name).parts
               or ':' in name or any(item in name.lower() for item in forbidden) for name in relative):
            raise SystemExit('Unexpected or private file in Windows archive.')
        manifest = json.loads(source.read(prefix + 'update-manifest.json'))
        if manifest.get('version') != args.version or set(manifest.get('files', [])) != set(relative):
            raise SystemExit('Windows archive does not match its update manifest.')
        if 'YT-Chatterino.exe' not in relative or 'platforms/qwindows.dll' not in relative:
            raise SystemExit('The native chat runtime is incomplete.')

        info = f'''YT-Chatterino {args.version} para OBS Studio 33 ou mais recente (Windows 64 bits)

1. Feche o OBS Studio.
2. Extraia a pasta yt-chatterino-obs deste ZIP para:
   %PROGRAMDATA%\\obs-studio\\plugins\\
   No OBS portatil, use a pasta plugins dentro da instalacao portatil.
3. Abra o OBS e ative Paineis > YT-Chatterino.
4. Na engrenagem, abra Contas e conecte suas contas separadamente das do app.
   Seus canais entram no mesmo chat; o YouTube precisa estar ao vivo.

Para atualizar o plugin, feche o OBS e substitua a pasta pela nova versao.
O aplicativo independente possui seu proprio atualizador.
O plugin e para Windows; o aplicativo independente tambem possui versao para Arch Linux.
'''
        with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as out:
            root = 'yt-chatterino-obs/'
            out.writestr(root + 'yt-chatterino-obs.dll', plugin)
            out.writestr(root + 'LEIA-ME-OBS.txt', info.encode('utf-8'))
            for name, path in zip(names, relative):
                out.writestr(root + 'data/sidecar/' + path, source.read(name))
    with zipfile.ZipFile(destination) as check:
        if check.testzip():
            raise SystemExit('OBS archive failed its integrity check.')
    print(f'Validated {destination} with {len(relative)} sidecar files.')


if __name__ == '__main__':
    main()
