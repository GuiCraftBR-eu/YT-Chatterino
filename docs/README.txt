YT-CHATTERINO 2.2.0 — WINDOWS 11 x64

1. Download YT-Chatterino-Windows11-2.2.0.zip from the GitHub release.
2. Right-click the ZIP and choose Extract All. Open the extracted YT-Chatterino folder.
3. Double-click YT-Chatterino.exe. No Python, compiler, installer or Command Prompt is needed.
4. Keep every DLL and plugin folder beside the EXE. Do not run it inside the ZIP.

ADD A CHAT
Press Ctrl+T (or click +). Paste a YouTube LIVE VIDEO link or Twitch @channel/link.
Normal YouTube web reading needs no API key, Google account or quota.
If Google returns HTTP 429, sign in and optionally enable the official API fallback per chat
in the channel menu. It costs 5 units per request, about 986 units per hour per chat
at the default 18.25-second interval; multiple chats use more.
Twitch: Settings > Accounts > Connect Twitch. Approve the browser code once.
Accounts shows your connected identity and enabled permissions. Refreshing subscriber emotes reuses your saved sign-in.

SEND / MODERATE
Type below a chat and press Enter. Right-click a Twitch message to prepare a reply. Ctrl+right-click opens actions/moderation.
Twitch uses your saved sign-in and asks for any extra permissions it needs.
YouTube sending/moderation needs your own Google Desktop OAuth client JSON and sign-in.
Enable YouTube Data API v3 in that Google Cloud project; configure the consent screen;
create an OAuth client of type Desktop app; download its JSON. In Accounts choose the
JSON, then Sign in to YouTube. The illustrated guide is in Settings > Accounts. If the OAuth app is in Testing, add the exact Google account used to sign in under Audience > Test users; this is required.
You must have the channel's actual moderation/owner permission. More steps are in Tutorial.
Do not share your JSON, account tokens or profile with friends. Share only this clean ZIP.

USEFUL CONTROLS
Ctrl+Shift+T: add a split. Ctrl+W: close focused pane. Ctrl+Shift+W: close tab.
Ctrl+Tab: next tab. Ctrl+1-8: select tab. Ctrl+9: last tab.
Drag tabs to reorder; drag dividers to resize. Scroll over tabs to switch.
Middle-click empty tab-bar space to add a chat. Right-click a header for layout/browser actions.
Double-click a header to zoom/restore a pane. Alt+Arrows: focus another pane.
Ctrl+F / Ctrl+Shift+F: search pane / all chats. Ctrl+K: quick switch.
Ctrl+F5: reconnect pane. Ctrl+P: settings. Ctrl++ / Ctrl+- / Ctrl+0: text size.
Tab / Shift+Tab: complete emotes or @names. Click the smile button for the emote picker.
Middle-click the chat and move the pointer for autoscroll. Click again or press Esc to stop.
Physical wheel speed defaults to 2×; change it in Appearance. Touchpads keep their pixel scrolling.
The picker separates Subs, Channel, Global and Emojis. Search filters the current tab.
Left-click a name for a user card; left-click a Twitch reply to open its retained thread.

SETTINGS
Appearance: language, message/name fonts, badge/emote sizes, wheel speed, dates, timestamps, always on top.
Messages: history and message limit (default 2000 per pane; 100-20000 allowed).
Highlights: users, roles and phrases. Notifications: mention sounds and Twitch live alerts.
Saved channels: save/open channels. YouTube looks for current streams only when opened.
YouTube and Twitch keep reading in background tabs. Pause YouTube manually if desired.
The window position, size, tabs and splits are saved. Sign-ins are encrypted for your OS user.
Settings search and Tutorial explain the rest.

UPDATES
The downward arrow appears when a newer release is available. Click it, then confirm.
The app verifies the download, backs up the folder, installs and restarts. A failed startup
rolls back. Keep the extracted folder writable and close other copies before updating.
Old Windows portable 1.x can update to this version. Source installs need one manual ZIP upgrade.
If native 2.0.0/2.0.1 rejects a .lnk shortcut during an update, move that shortcut outside the app folder,
retry the update, then move it back. Version 2.0.2 fixes shortcut preservation for subsequent updates.
The Source/*.py* files are unused compatibility markers for the old updater, not Python code
used by this app. All application code and its runtime are native C++/Qt.

LIMITS / HELP
YouTube units are approximate; web reading uses zero API units. The optional API fallback
uses quota. Google apps in Testing
may require signing in again after seven days. Recent Twitch history uses a public external
service and may be unavailable. Threads include only retained messages. Received Twitch
GIFs are supported; sending GIFs uses Twitch's browser. YouTube site changes can interrupt reading.
The build is unsigned: Windows may show an unknown publisher warning. Do not disable your
security tools. Download only from https://github.com/GuiCraftBR-eu/YT-Chatterino/releases
Library notices are in Licenses. Source/build instructions are in the GitHub repository.

GOOGLE: ACCESS BLOCKED (403 / access_denied)
If the browser says the app is in Testing, the owner of the project used by your
selected OAuth JSON must open Google Auth Platform > Audience > Test users, add
the exact Google account you sign in with, and save. Cancel the pending sign-in
in Accounts and retry. You can also configure your own project/Desktop OAuth JSON.
Reinstalling the app does not change test users. Public distribution of one OAuth
client must follow Google publishing/verification requirements.
Guide (PT-BR): https://github.com/GuiCraftBR-eu/YT-Chatterino/blob/main/docs/GOOGLE-LOGIN.md
