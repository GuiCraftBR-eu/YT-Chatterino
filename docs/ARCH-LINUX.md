# Arch Linux (native)

This edition uses Arch's native Python and Tk. It does not use Wine, a Windows
executable, an embedded Windows runtime, or an emulator.

## Install

For the ready-built pacman package, download `yt-chatterino-1.2.1-1-any.pkg.tar.zst`:

```sh
sudo pacman -U ./yt-chatterino-1.2.1-1-any.pkg.tar.zst
```

The source download below is an alternative if you prefer to build it locally.

Download and extract `YT-Chatterino-ArchLinux-1.2.1.tar.gz`. From the extracted folder:

```sh
sudo pacman -S --needed base-devel python tk python-pillow python-keyring python-secretstorage ttf-dejavu
cd packaging/arch
makepkg -si
```

Run **YT-Chatterino** from the application menu, or use `yt-chatterino`.
Run `makepkg` as your regular user. It asks for administrator access only when
installing the generated package through pacman. To uninstall: `sudo pacman -R yt-chatterino`.

For a portable source launch (after installing dependencies):

```sh
python src/ytchatterino.pyw
```

## Saved accounts and sounds

Sign in separately on Linux. Windows credentials cannot be transferred.
Unlock your desktop keyring (GNOME Keyring/Secret Service or KWallet) to keep
sign-ins saved. If no supported encrypted keyring is available, the app keeps
the account for the current run and reports the storage failure; it never saves
tokens in plaintext. Some desktop setups need `gnome-keyring` installed and started
by their session manager. Do not use a plaintext keyring backend.

Sounds use `pw-play`, `paplay`, or `aplay`, when available. The desktop bell is a fallback.
Tk uses X11 or XWayland under Wayland. Install/enable your compositor's XWayland support if needed.

## Português brasileiro

Baixe e extraia o arquivo `YT-Chatterino-ArchLinux-1.2.1.tar.gz`.
Instale as dependências com o comando `pacman` acima, entre em `packaging/arch`
e execute `makepkg -si` como seu usuário normal. Abra **YT-Chatterino** pelo menu
de aplicativos ou pelo comando `yt-chatterino`.

Não precisa de Wine. Conecte suas contas novamente no Linux e desbloqueie o
chaveiro do ambiente gráfico para salvar o login. Se o chaveiro seguro não
estiver disponível, o login dura somente até fechar o aplicativo.
