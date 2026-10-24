# Changelog

## 2.0.2

- Preserve Windows .lnk shortcut files during native updates while continuing to reject filesystem symbolic links and junctions.
- Restart the original app when a validated update is rejected before the folder swap.
- Keep the settings sidebar synchronized with shortcuts and search results; support keyboard navigation between settings pages.
- Add settings navigation and real Windows shortcut preservation regression coverage.

## 2.0.1

- Corrected an empty update-state mutation in the toolbar. An up-to-date or invalid version now keeps Install disabled.
- Added a regression check for the complete toolbar-to-update-dialog path.

## 2.0.0

- Replaced the Python/Tk application with a native C++20/Qt 6 implementation on Windows and Arch Linux.
- Ported account storage, public YouTube reading, shared Twitch IRC, sending/moderation, metadata edits, emotes/badges/GIFs, saved channels, layouts, reply threads, user cards, search/filters/highlights, sounds, appearance, translations and automatic updates.
- Virtualized message layout, bounded the document/image caches, paused offscreen animations and batched Twitch live checks.
- Added paginated emote browsing, preserved Chatterino shortcuts, native window/icon metadata, update file-preservation and rollback tests.
- Preserved compatible 1.x settings and encrypted accounts. Source installs and old Arch packages need one manual native upgrade; Windows portable updates remain supported.

Earlier Python releases remain available through the repository's 1.x tags.
