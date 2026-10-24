# 2.0.1 release verification

The native QtTest suite has 69 passing cases/data rows. It runs on Windows and Arch Linux. Tests use isolated profiles and fake API transports; no real messages, bans or title changes are submitted by the automated suite.

| Area | Verification |
| --- | --- |
| Startup, Windows icon, no-console launcher | Packaged EXE opened on Windows; embedded version/icon; native runtime deployment checked. |
| Account migration | Real Windows Credential Manager sign-ins loaded; legacy Google name migrated; revoked Twitch access detected; user reauthorized and the saved access was reused. Refresh requests are serialized. |
| Twitch reading and metadata | Real IRC connection, recent history, offline title/category, live viewer counts and native badge images checked on screen. |
| YouTube reading | Real public live chat loaded with no key or account; continued while another tab was selected. Continuation/reconnect/manual-pause fixtures pass. |
| Emotes, animation and GIF receiving | Native/Twitch/BTTV/7TV parsing and Unicode offsets; QMovie two-frame/disposal fixture, offscreen pause/resume, image fallback/retry and host checks. Real emotes/picker images displayed. Public YouTube PNG downloads/decoding checked separately. |
| Picker and completion | Real subscriber-emote picker loaded; selection inserted unsent text. Pagination, search, Tab/Shift+Tab and escaped Twitch codes have regression tests. |
| Replies and user cards | A real retained Twitch parent/reply thread and a user card opened on screen. Nested threads, deletion protection, missing/cyclic parents and root retention at the cap are tested. |
| Sending, moderation and editing | Mocked Twitch send/reply/drop, YouTube send, ID mapping, delete, timeout/ban, permission gates and title/category payloads. No real moderation or channel edits were needed. |
| Tabs, splits, mixing and persistence | Live split creation/maximize, tab shortcuts and focused quota visibility; fixtures cover mixed/legacy sessions, limits, selection, zoom and geometry restoration. |
| Filters, search and highlights | Nickname/role/phrase/filter fixtures and on-screen role highlights; settings search checked in English/PT-BR. |
| Appearance and language | Separate message/name fonts, timestamps, lowercase am/pm, dates, badge/emote sizing, full-width row backgrounds and settings/tutorial language switching checked. |
| Sounds and live alerts | Alert routing, initial silence, offline-to-live transitions, closed watched channels and batched requests tested with a sound observer. Default WAV is bundled. |
| Saved channels and quota | Legacy JSON retained; channel saving/discovery and action-cost logic ported. Local quota/reset/external-usage tests pass. Reading never consumes official API units. |
| Updates | SHA/version/path validation, real filesystem success/rollback and custom-file/empty-folder preservation. The unchanged 1.3 updater installed the 2.0 native ZIP, restarted it and completed acknowledgement/cleanup in an isolated folder. |
| Native Arch | CI compiles C++ against Arch Qt, runs the same suite, builds/installs the x86_64 pacman package and checks the installed binary version. |

The 2,000-message resize fixture completed 80 resizes in approximately 220–230 ms on the Windows test machine, with seven laid-out message documents cached. This is one synthetic workload, not a claim that every interaction is faster than 1.x.

External services can change independently: YouTube page structure, Twitch/API role permissions, recent-message history and provider emote catalogs remain network-dependent. A unit/fixture pass is not evidence that a destructive API action was performed live.
