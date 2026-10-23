# Resize measurement for v1.2.1

A local Windows 11 benchmark used Python 3.14, Tk 9.0 and a visible chat pane
with 2,000 synthetic messages. Every message contained text and an inline
28 px image. The benchmark changed the window width through 189 steps between
420 and 1,040 px and processed the Tk event loop after each change.

| Version | Total resize time | Median step | 95th percentile |
| --- | ---: | ---: | ---: |
| 1.2.0 | 23.98 s | 128.37 ms | 149.83 ms |
| 1.2.1 | 17.62 s, including final layout | 93.18 ms | 113.25 ms |

This is about 27% less elapsed time in this particular test. It is not a
general speedup estimate or a frame rate guarantee. Results depend on hardware,
message lengths, images, splits, display scaling and the window system.
The scripted steps are not identical to a person's continuous mouse drag.

To repeat it with Python/Tk, run `python tools/resize_benchmark.py . --output-dir
../resize-results` from the source folder. It opens a synthetic window and
writes JSON and profiling reports. It uses no accounts or network connections.
Run the same script against a v1.2.0 checkout to compare versions.

## What changed

- Size events are grouped and only the latest dimensions are applied.
- Large chat panes render a small copy of the visible range during a resize.
  The original text, marks, selection and image references stay intact.
- Full layout returns after a short pause in resizing or before interacting
  with the chat. Live readers stay connected; queued display updates and
  animated frames resume afterward.
- Header wrapping and the Jump to latest button avoid redundant updates.
- Settings pages are built on demand and reused while that window is open.

The default retained message limit remains 2,000 per pane, configurable under
Settings → Appearance → Messages or per pane from its header menu.

## Coverage

The regression suite checks search keywords and translations, grouped settings,
draft preservation, search navigation, latest-size coalescing, incoming messages
during resize, and preservation of text, selection, tags and image references.
Native Arch Linux CI runs the same Tk tests under Xvfb. Live Windows checks
also cover the settings window and public Twitch/YouTube rendering.

## v1.2.2 comparison

A fresh comparison of the same scripted workload on 23/10/2026 measured 12.01 s
for v1.2.1 and 11.75 s for v1.2.2, including the final layout. That is about 2%
less elapsed time in this run; there is no large general resize-speed claim.
These measurements also show why results from separate sessions should not be
compared as if the machine and window system were constant.

The temporary visible range during dragging is smaller. Emote catalog patterns
are compiled in a background worker and shared in a bounded cache. Appearance
changes avoid recalculating all panes' highlights once per pane. English startup
does not compile the Portuguese catalog or import archive readers until needed.
Startup timings vary with caches; live checks confirm a responsive first window.

Updater checks run after the first window appears. They never block startup on
GitHub or download automatically without confirmation. Regression transports
for sending/moderation/editing are mocked; live UI checks do not post to chat.
