# Performance and release checks

The C++ renderer stores message data up to the configured cap and lays out only the visible rows. It keeps at most 128 QTextDocuments, shares decoded emote/badge resources and caps the image cache at 48 MiB. QMovie decodes animations and pauses entries that are no longer visible. Resize events are coalesced over 24 ms. API operations use asynchronous Qt networking; each YouTube source has its own continuation and timer. Twitch shares one TLS IRC connection and live-state checks are batched for up to 100 logins.

The native suite includes 2,000-message resize/cache checks, parsers, Unicode emote offsets, image/animation handling, completion and picker pagination, retention, selection, filters/highlights, threads/moderation, session geometry, encrypted storage namespaces, mocked send/moderation/title APIs, quota accounting and update success/rollback/file preservation. It performs no real sends or moderation.

Live checks use public chats and the operator's saved account. Network-dependent API availability and local rendering tests are reported separately. A faster compiler language alone does not guarantee every interaction is faster; measurements are workload-specific.
