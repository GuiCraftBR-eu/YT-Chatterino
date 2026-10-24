# YT-Chatterino 2.0.0

A native **C++20 / Qt 6** desktop chat client for YouTube and Twitch, with a compact black interface inspired by Chatterino. The application no longer runs Python, Tk or a web view.

[Download the latest release](https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/latest) · [Português do Brasil](docs/README-PTBR.txt) · [Build instructions](docs/BUILDING.md)

## Run

**Windows 11 x64:** download `YT-Chatterino-Windows11-2.0.0.zip`, extract the entire folder, then double-click `YT-Chatterino.exe`. Keep the DLLs and plugin folders beside it. No Python, compiler or terminal is required.

**Arch Linux x86_64:** download `yt-chatterino-2.0.0-1-x86_64.pkg.tar.zst`, then run `sudo pacman -U ./yt-chatterino-2.0.0-1-x86_64.pkg.tar.zst`. Launch YT-Chatterino from your application menu. It runs natively with Qt; Wine is not used. See [Arch instructions](docs/ARCH-LINUX.md).

## Connect

Press **Ctrl+T** and paste a YouTube live-video URL or a Twitch `@channel`/URL. YouTube public reading needs no API key or Google account. For Twitch, open Settings → Accounts and approve the sign-in in your browser.

To send or moderate on YouTube, choose your own Google **Desktop OAuth client JSON** in Accounts and sign in. Enable YouTube Data API v3 in that client’s Google Cloud project. Accounts → Connection tutorial explains the steps. Never send your private JSON or saved tokens to friends.

## Features

- Tabs you can drag and switch with the mouse wheel; horizontal/vertical splits and mixed channel feeds.
- Shared asynchronous Twitch IRC connection and independent public YouTube readers. Background tabs keep reading; YouTube can be paused manually.
- Native Twitch/YouTube emotes and badges, BTTV/7TV, animated emotes and received Twitch subscriber GIFs. Size controls, Tab completion and a searchable paginated emote picker.
- Sending, Twitch replies, retained reply-thread windows, user cards, permission-aware moderation, title edits and Twitch category edits.
- Recent chat history when available; default 2,000 messages per pane, with configurable limits from 100 to 20,000.
- Copy/select messages, pane/all-chat search, filters, username/role/phrase highlights and configurable sounds.
- Twitch live alerts, saved channels, English/PT-BR, settings search, separate message/name fonts and customizable dates/timestamps.
- Restored layout/window position, always on top, encrypted account storage, YouTube action-quota estimate and confirmed automatic updates.

Dates default to DD/MM/YYYY; 12-hour timestamps use lowercase am/pm. Default highlight color is `#227400`; the default sound is `ChatternoPing.wav`.

## Upgrade from 1.x

Your settings, saved layouts, channels and encrypted sign-ins are reused under the same OS user. Keep a backup of your old app folder and profile before a major upgrade.

- **Windows portable:** the 1.x updater can install this native release. The ZIP contains unused `Source/*.py*` version markers solely for the old updater’s validation. They are not executed; no Python runtime is included. Future updates use the C++ updater.
- **Old Windows source installs:** extract the native Windows ZIP and launch its EXE once. The old Python source updater cannot install a native runtime.
- **Arch 1.x:** install the new x86_64 package once with pacman. The old updater only recognizes architecture-independent Python packages. Future native updates use pacman after system authorization.

## Limits

YouTube public-page reading can break when YouTube changes its site. Moderation/sending still use the official authenticated APIs. Quota is an estimate for this app and any manually entered external usage, not your project’s authoritative balance.

Recent Twitch history depends on `recent-messages.robotty.de`; no account tokens are sent there. Threads show messages retained by this app, not an unlimited server archive. YouTube does not expose Twitch-style reply-thread metadata. Subscriber GIFs can be received; sending them remains an official Twitch browser action. API permissions and channel roles determine what moderation and editing actions are allowed.

This project is independent of Chatterino, Twitch, YouTube, BTTV and 7TV. Third-party library notices and source information are in [Licenses](Licenses/NOTICE.txt). The project owner has not selected a general redistribution license for the application source.
