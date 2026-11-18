## YT-Chatterino 2.5.10

- Preload available channel and global emotes as chats open, with channel emotes prioritized and background downloads limited.
- Show a small still frame while a 7TV animation loads, and improve recovery when 7TV image connections stall.
- Share channel emotes within a mixed chat, while keeping each source's own emote definitions first.
- Load third-party emote catalogs for Kick and avoid repeated unsupported YouTube 7TV requests.

Animated 7TV emotes preload a still frame; the animation downloads when displayed. Large catalogs can exceed the in-memory cache, and 7TV availability still affects loading. The browser extension is unchanged; Firefox continues using the signed 2.5.8 extension.
