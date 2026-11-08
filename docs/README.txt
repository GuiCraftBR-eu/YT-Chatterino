YT-CHATTERINO 2.4.8 — WINDOWS 11 x64

1. Download YT-Chatterino-Windows11-2.4.8.zip from the GitHub release.
2. Right-click the ZIP and choose Extract All. Open the extracted YT-Chatterino folder.
3. Double-click YT-Chatterino.exe. No Python, compiler, installer or Command Prompt is needed.
4. Keep every DLL and plugin folder beside the EXE. Do not run it inside the ZIP.

ADD A CHAT
Press Ctrl+T (or click +). Enter a channel or link. For YouTube, use the handle from its URL: for example, LofiGirl for youtube.com/@LofiGirl. The @ is optional when opening and saving channels.
Normal YouTube web reading needs no API key, Google account or quota.
If Google returns HTTP 429, sign in and optionally enable the official API fallback per chat
in the channel menu. It costs 5 units per request, about 986 units per hour per chat
at the default 18.25-second interval; multiple chats use more.
Twitch: Settings > Accounts > Connect Twitch. Approve the browser code once.
Accounts shows your connected identity and enabled permissions. Refreshing subscriber emotes reuses your saved sign-in.

KICK: CONNECT, SEND AND MODERATE
Read by opening a channel link or selecting Kick and entering the channel name.
For sending and moderation, follow the illustrated Kick guide in Settings > Tutorial > Accounts.
Create a developer app at https://kick.com/settings/developer with the exact redirect
http://localhost:37365/callback. Select Read user information and Write to Chat feed.
For moderation and editing also select Read channel information, Update channel
information, Execute moderation actions for moderators, and Execute moderation actions
on chat messages. Sign in under Settings > Accounts > Kick and approve in your browser.
Reconnect after changing permissions; uncheck management if you only need to send.
The OS credential store protects the secret; never share it. Moderation requires a channel
role, and editing the title/category requires the broadcaster account. Kick live reading
uses an unofficial public connection and recent-history recovery; changes or prolonged
disconnections can still cause missed messages.

SEND / MODERATE
Type below a chat and press Enter. Right-click a Twitch message to prepare a reply. Ctrl+right-click opens actions/moderation.
Twitch uses your saved sign-in and asks for any extra permissions it needs.
YouTube sending/moderation needs your own Google Desktop OAuth client JSON and sign-in.
Enable YouTube Data API v3 in that Google Cloud project; configure the consent screen;
create an OAuth client of type Desktop app; download its JSON. In Accounts choose the
JSON, then Sign in to YouTube. The illustrated guide is in Settings > Accounts. If the OAuth app is in Testing, add the exact Google account used to sign in under Audience > Test users; this is required.
You must have the channel's actual moderation/owner permission. More steps are in Tutorial.
Do not share your JSON, account tokens or profile with friends. Share only this clean ZIP.

VOD CHAT REPLAY (BETA)
1. In the app, click + > Open VOD chat replay (Beta). Open the extension folder.
2. In Chrome/Edge, open Extensions, enable Developer mode and Load unpacked, selecting
   the browser-extension folder. In Firefox open about:debugging, select This Firefox >
   Load Temporary Add-on and choose manifest.json. Firefox removes temporary add-ons when
   the browser closes, so repeat the installation after restarting it.
3. Open a supported VOD. Click the extension icon, paste the code from the app and connect
   this tab. Leave the replay window open. Pause or seek the video normally; chat follows it.
   A new replay window uses a new pairing code.
Replay is read-only and depends on the platform retaining and exposing the chat. It uses
unofficial public interfaces and may fail, be rate-limited or omit messages/emotes.
The extension sends only the selected tab's URL and video position to the local app.

OPEN ON PHONE
Right-click the tab, select the pane and Open on phone. Choose the local network,
start access, and scan the QR code on your phone or enter the displayed address and code.
Use the same network on phone and computer and keep the app open. The QR window can be
closed; click Phone in the pane to reopen it and Stop to end access. If Windows asks for network access, allow private networks
only; the firewall can block the connection until configured. The connection uses local
HTTP, so use a trusted network. The phone view supports
reading, sending, replies, emotes and permitted moderation, but not every desktop feature.

USEFUL CONTROLS
Ctrl+Shift+T: add a split. Ctrl+W: close focused pane. Ctrl+Shift+W: close tab.
Ctrl+Tab: next tab. Ctrl+1-8: select tab. Ctrl+9: last tab.
Drag the stream title to adjust its height. Drag tabs to reorder; drag dividers to resize. Scroll over tabs to switch.
Middle-click empty tab-bar space to add a chat. Right-click a header for layout/browser actions.
Double-click a header to zoom/restore a pane. Alt+Arrows: focus another pane.
Ctrl+F / Ctrl+Shift+F: search pane / all chats. Ctrl+K: quick switch.
Ctrl+F5: reconnect pane. Ctrl+P: settings. Ctrl++ / Ctrl+- / Ctrl+0: text size.
Tab / Shift+Tab: complete emotes or @names. Click the smile button for the emote picker.
In the message field, use Up and Down to browse messages sent during this session.
Click an @mention to open the user's card and available message history in that channel.
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
