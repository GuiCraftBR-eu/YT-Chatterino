# Arch Linux — native C++ / Qt

Download `yt-chatterino-2.2.0-1-x86_64.pkg.tar.zst` from the release and install it with:

```sh
sudo pacman -U ./yt-chatterino-2.2.0-1-x86_64.pkg.tar.zst
```

Launch YT-Chatterino from the desktop application menu, or run `yt-chatterino`. Qt runs directly on Linux; Wine and Python are not used. Dependencies are installed through pacman. Both X11 and Wayland can use the Qt platform plugins supplied by Arch.

Install/unlock a Secret Service compatible keyring (`gnome-keyring` or a compatible KDE setup) to retain sign-ins. `secret-tool` comes from libsecret. Without a working keyring, sign-ins last for the current session and the app reports the storage error; it does not save plaintext tokens.

Notification sounds use `pw-play`, `paplay` or `aplay` when available. Install `noto-fonts-emoji` for color emoji. In-app package updates use `pkexec pacman -U` and require polkit authorization; manual pacman installation also works.

The 1.x package contained Python files and used architecture `any`. Install v2 once with pacman because the old updater cannot recognize native x86_64 packages. Your home profile and compatible keyring credentials are retained. No manual profile deletion is needed.

To build from source, see BUILDING.md or run `makepkg -si` in packaging/arch as a regular user.
