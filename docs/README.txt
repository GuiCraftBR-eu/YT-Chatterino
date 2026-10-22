YT-CHATTERINO - WINDOWS 11 PORTABLE EDITION
Updated: 22 October 2026

QUICK START
===========
1. Right-click YT-Chatterino-Windows11-Updated.zip > Extract All.
2. Open the extracted YT-Chatterino folder.
3. Double-click YT-Chatterino.exe.

Python is included. No Python installation, pip, administrator launch or
installer is required. Keep _internal beside the EXE. Do not run inside the
ZIP or copy only the EXE. You can create a desktop shortcut to the EXE.
This build targets Windows 11 x64 (Intel/AMD). Windows on ARM can use x64
emulation, but ARM was not tested. S mode and managed PCs can restrict
portable apps. The EXE is unsigned; do not disable security software to run it.

LANGUAGE, SETTINGS AND HELP
===========================
Click the gear or press Ctrl+P. Appearance > Language / Idioma switches
immediately between English and Português (Brasil). The choice is saved.
Menus, buttons, help and connection messages follow the selected language;
chat messages, usernames, stream titles and emote codes stay unchanged.
Appearance includes Always on top, timestamps, alternating backgrounds,
compact spacing and separate badge/emote heights.
Settings > Tutorial has short guides for connections, moderation and other
features. Start there if needed.

TWITCH: CONNECT, SEND AND MODERATE
=================================
1. Settings > Accounts > Twitch sign-in / permissions.
2. Approve the code/page in your browser with YOUR OWN Twitch account.
3. Press Ctrl+T or click + and enter @channel_name or a Twitch URL.
4. Type below the chat and press Enter or Send.

The public Client ID is included. No developer project or separate token
is needed. Sign-in is saved securely for your Windows user. Additional
permissions can be requested for sending, moderation or subscriber emotes.
BTTV and 7TV require no separate login.
Right-click a message > Moderation for delete, timeout or ban. Your account
must own or moderate the channel. Twitch's API cannot delete the owner's
own messages; remove those on Twitch. Right-click the pane header > Channel
actions to edit title/category. Twitch requires the broadcaster's account
for editing; moderator/editor status alone does not grant that API permission.

YOUTUBE: READING NEEDS ONLY A LIVE LINK
=====================================
Ctrl+T > paste the LIVE VIDEO link. Public chat reading, stream details and
opening saved channels require no API key or Google sign-in and use zero
Google API units. The selectable official API reader has been removed.
YouTube website changes, timing and access restrictions still apply.

YOUTUBE: SEND, MODERATE AND EDIT
===============================
These actions require Google OAuth and use your project's API quota:
1. In Google Cloud, create a project and enable YouTube Data API v3.
2. Configure Google Auth Platform. If your app is in Testing, add your
   Google account as a test user.
3. Create an OAuth client of type Desktop app and download its JSON.
4. Settings > Accounts > Choose Google OAuth JSON > Sign in to YouTube.
   Select the YouTube channel you use to chat or moderate.
5. Type to send; right-click a message > Moderation for actions; right-click
   a pane header > Channel actions to edit the stream title.

Google checks your channel permissions. Signing in does not make you a
moderator. Testing projects may require sign-in again after seven days.
Each friend uses their OWN project, client JSON and accounts. No sender
account or credentials are included in this ZIP.
Official setup: https://developers.google.com/youtube/v3/guides/auth/installed-apps

YOUTUBE QUOTA ESTIMATE
======================
Click the YT balance above the chats or Settings > YouTube quota. It estimates
remaining units and messages, deletions or bans. These are ALTERNATIVE uses
of the SAME balance; do not add the action counts together.
- Sending: 20 units each, plus an initial chat-ID lookup if needed.
- Deleting: 20 units; website messages can need 5-6 extra lookup units.
- Timeout / ban / undo ban: 200 units, plus an initial lookup if needed.
- Title editing: about 51 units including the existing-title lookup.
The default limit is 10,000 units/day. Change it if Google granted a different
limit. Enter usage by other apps sharing your project to adjust the estimate.
Usage is saved after every action. Quota resets at midnight Pacific time;
the external-usage adjustment also resets with the day.
This is a LOCAL estimate. The app cannot see Google's actual remaining
balance. Other apps/people, failed requests and lookups can affect it.
Costs: https://developers.google.com/youtube/v3/determine_quota_cost

EVERYDAY FEATURES AND CONTROLS
==============================
- Ctrl+T adds chats; choose tabs, side by side, stacked or mixed feed.
  Middle-click empty tab-bar space to add chats. Ctrl+Shift+T adds a split.
- Drag tabs to reorder; scroll over tabs to switch. Drag dividers to resize.
  Double-click a header to expand/restore a pane. Right-click for more actions.
- Right-click a tab to open the stream, player or chat in your browser.
  YouTube player pages use a local server while this app remains open.
- In mixed panes, choose the sending destination beside the message box.
  The typing counter shows the limit: YouTube 200, Twitch 500 characters.
- Click the smiley for Subs / Channel / Global / Emojis. Tab and Shift+Tab
  complete/cycle emotes or @names. Native emotes, badges, BTTV/7TV, animated
  emotes and incoming Twitch subscriber GIFs render where supported.
  Send Twitch subscriber GIFs from the official Twitch browser chat.
- Right-click a user to highlight them. Highlights adds roles and words;
  Notifications controls mention sounds and Twitch live alerts.
  Default color: #227400. Default sound: ChatternoPing.wav.
- Open Twitch channels get live alerts by default. First detection is silent.
  YouTube never performs background live discovery or sends live alerts.
- Save channels in Channel actions; open them in Settings > Saved channels.
  YouTube checks current broadcasts only when you click Open and allows
  choosing among simultaneous broadcasts. Twitch opens while offline too.
- Hidden YouTube chats pause automatically. Twitch stays connected. Pause
  individual YouTube chats in the header menu, or all in the gear's right-click
  menu. Per-chat reading intervals respect YouTube's recommended timing.
- Ctrl+F searches a pane; Ctrl+Shift+F searches all chats. Ctrl+F5 reconnects.
  Ctrl+Tab / Ctrl+Shift+Tab switch tabs. Alt+Arrows moves pane focus.
  Ctrl++ / Ctrl+- / Ctrl+0 changes/resets text size. Ctrl+K switches chats.
  Settings > Keyboard shortcuts lists all controls.
- Layouts, preferences, accounts and saved channels persist across restarts.

SHARING, PERSONAL DATA AND TROUBLESHOOTING
=========================================
Share this ZIP, not your personal settings folder. It includes the EXE,
_internal runtime, Source, Licenses, README.txt and README-PTBR.txt.
It excludes tokens, accounts, Google client JSON, API keys, personal settings,
layouts, logs and chat history. Each friend signs into their own accounts.
Credentials use Windows Credential Manager or Windows-encrypted local files.
Settings/diagnostics use .ytchatterino* files in %USERPROFILE%. Deleting the
extracted program does not erase saved settings or Windows credentials.
- App will not open: extract the whole ZIP again; keep _internal intact.
- Twitch fails: check connection, reconnect or sign in again in Accounts.
- YouTube reading fails: check the live link and whether chat is enabled.
  Website restrictions/changes can interrupt reading; use Open chat in browser.
- YouTube actions fail: check API activation, OAuth/test-user setup, selected
  account/channel, moderator rights and project quota.
- Offline Twitch chats receive new messages only, without downloading history.
- No sound: check Notifications and Windows volume.
Independent app; not an official Twitch, YouTube or Chatterino release.
