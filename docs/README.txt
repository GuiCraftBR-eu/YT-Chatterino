YT-CHATTERINO - WINDOWS 11 PORTABLE EDITION
Updated: 23 October 2026

QUICK START
===========
1. Right-click YT-Chatterino-Windows11-1.3.0.zip > Extract All.
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
Click the gear or press Ctrl+P. Appearance > Display > Language / Idioma switches
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
- Right-click a user to highlight them. Appearance > Highlights adds roles and words;
  Notifications controls mention sounds and Twitch live alerts.
  Default color: #227400. Default sound: ChatternoPing.wav.
- Open Twitch channels get live alerts by default. First detection is silent.
  YouTube never performs background live discovery or sends live alerts.
- Save channels in Channel actions; open them in Settings > Saved channels.
  YouTube checks current broadcasts only when you click Open and allows
  choosing among simultaneous broadcasts. Twitch opens while offline too.
- YouTube and Twitch stay connected in background tabs. Pause
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
- Offline Twitch channels may also load recent messages when the history service has them.
- No sound: check Notifications and Windows volume.
Independent app; not an official Twitch, YouTube or Chatterino release.

VERSION 1.2.0: REPLIES, USER CARDS AND MESSAGE LIMITS
==================================================
Twitch: right-click a message > Reply to message. The recipient appears above
the input. Type and send normally; Escape or x cancels the reply. Received
replies display their parent message above the message body.

Left-click a username in Twitch or YouTube to open their user card. The card
shows public details provided by the platform, reported badges and subscription
months, and up to 500 of that user's messages still retained in this channel.
Use its profile, highlight, mention and moderation buttons as needed. Platform
permissions still apply; private subscription details are not exposed publicly.
YouTube's extended public profile lookup uses your connected Google account and
costs 1 API unit per uncached profile. Without login, basic chat details remain.

Settings > Appearance > Messages: default retained message limit is 2000 per pane. Set a
value from 100 to 20000. Right-click the pane header > Message limit to override
that pane. A mixed feed shares its limit across its channels. Old text, images
and rendering references are discarded. This is not a permanent chat log.

Twitch loads up to 200 recent messages from the independent public service
recent-messages.robotty.de, also used by Chatterino. It receives the channel
name, never account tokens. Availability varies, especially for newly tracked
channels. Disable this in Settings > Appearance > Messages. YouTube loads only the recent
messages provided by its chat page. Historical messages do not trigger sounds.

NATIVE ARCH LINUX DOWNLOAD
=========================
A separate Arch Linux package is available on the GitHub release page.
Install yt-chatterino-1.3.0-1-any.pkg.tar.zst using pacman -U, or build the
PKGBUILD in the source tar.gz with makepkg -si. No Wine is used. See
docs/ARCH-LINUX.md in that download for dependencies, encrypted keyring and sound.

VERSION 1.2.1: SETTINGS AND RESIZING
===================================
Appearance now groups Display, Messages and Highlights. Use Search settings
at the top of the settings window, or Ctrl+F there. Search names or keywords
in English or Portuguese. Enter opens the first match; Down selects results;
double-click or Open setting jumps to the control. Escape clears the search.
Switching settings pages keeps unsaved drafts until the window closes.

Window and split resizing avoid repeatedly laying out the entire retained
history. During a resize, the visible messages are rendered separately; new
display updates and animated frames resume when the drag stops. The message
buffer, selection and scroll position are preserved. See docs/PERFORMANCE.md
in the source download for a reproducible benchmark and its limits.

VERSION 1.2.2: UPDATES, DATES AND TIMES
=====================================
The app checks for stable GitHub releases after startup and every six hours.
When a newer package is ready, a download arrow appears beside the YouTube
balance area. Click it, then Install update and restart. The app downloads,
checks the SHA-256 digest, closes, replaces its files and restarts. Accounts,
settings and extra files in the app folder are preserved. A previous-files
backup remains beside the app folder. A failed portable/source startup restores
the previous files. Close other copies of the app before updating.

Settings > Updates can disable background checks or check manually. Arch
pacman installations use pkexec/polkit to authorize pacman; install polkit and
an authentication agent for this flow. The normal app still runs as your user.
Source updates preserve your existing Python and dependencies. A writable app
folder is required for portable/source updates. Version 1.2.1 and earlier need
one manual update to 1.2.2 before these controls are available.

Appearance > Display now has Date format (default DD/MM/YYYY) and Timestamp
format: HH:mm, HH:mm:ss, hh:mm am/pm or hh:mm:ss am/pm. Apply changes to update
the messages already retained, including timestamps currently hidden. Dates in
user cards and update notices follow your selected format. Chat message text
and API/storage formats remain unchanged.

The YouTube balance is visible only when the focused pane includes YouTube.
The update arrow remains available when viewing Twitch. Startup defers unused
translation and archive work; emote pattern compilation runs off the UI thread.


VERSION 1.3.0: BACKGROUND CHATS, WINDOW AND REPLY THREADS

YouTube keeps reading when another tab is selected or the app is minimized.
Manual Pause/Resume and Pause all still work. Reading uses no Data API quota.
The main window remembers its normal size/position and maximized state.
Closing while minimized keeps the last normal/maximized placement.

Settings > Appearance > Display has separate message and username font sizes
(7–48 pt). Ctrl++ and Ctrl+- adjust both; Ctrl+0 resets both to 12 pt. Existing
messages, selections and images stay intact. Twelve-hour clocks use am/pm.

Left-click a Twitch reply's body or its reply-context line to open the thread.
The window shows available retained messages and new replies; its bottom input
sends a reply to that conversation. There is no thread-subscription feature.
Drag to select/copy as usual; clicking a username still opens the user card.
Pruned or unavailable history cannot be recovered. A missing parent uses the
context supplied by Twitch, or an unavailable-message label. YouTube does not
provide equivalent reply-thread metadata to this reader.
