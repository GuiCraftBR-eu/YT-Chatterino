## YT-Chatterino 2.5.11

- Keep downloaded emotes and other chat images in a disk cache across restarts. Emotes displayed in desktop chat or pinned messages remain cached until they have not been displayed for seven days; unused entries are cleaned on startup and periodically while the app is running.
- Avoid immediately downloading retired emotes again during background preloading. They can still load when they appear in chat, and the cache size limit may evict images sooner when space is needed.
- Choose simple presets or advanced limits for simultaneous image downloads, memory cache, and disk cache. The settings page can open or clear the disk cache. The desktop app and OBS dock use separate profile caches.
- Open YouTube links using their full destinations when YouTube supplies them, including links in pinned messages and the phone view.

The browser extension is unchanged; Firefox continues using the signed 2.5.8 extension. Channel emote catalogs still require a network connection.
