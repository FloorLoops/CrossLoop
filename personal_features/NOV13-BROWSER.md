# CrossLoop browser and stability candidate, November 13

The unpublished **0.9.0-rc.5** candidate gains Google/English Wikipedia query entry, indexed in-page link markers, 32-entry
session back/forward history and cached-first link navigation. Successful downloads
open the reading view. Links/Confirm return to page actions. Back/Forward history
opens saved pages without Wi-Fi. GPIO readers use the Links list. Failed/cancelled
requests return to the prior page if present. Deleting a page removes its history;
missing entries are skipped. History expires on exit and adds no SD navigation writes.

This remains an HTML-to-text browser. Scripts, CSS layout, forms, images, cookies and
modern login flows are not implemented. Search providers can demand JavaScript,
CAPTCHA or consent; search shortcuts do not guarantee compatibility. A full visual
browser remains an unmet requirement. Existing limits: 32 links, 32 saved pages,
512-KiB downloads, 256-KiB text, 1024 reader page offsets.

CLWEB02 adds 128 bytes per cache for actual marker offsets. Up to 20 visible inline
markers and 3 footer buttons fit the shared input table; all 32 links stay available
through Links. Legacy caches remain readable; Reload adds inline targets. Older
firmware cannot read new v2 caches, except an intact v1 backup. See file-formats.md.
History owns 1056 bytes of keys plus counters. The parent adds four 128-byte labels
and four list rows; the reader adds 128 bytes of offsets and small metadata. No new
background task or PSRAM dependency. One open file serves each link-index read.
Line fitting uses logarithmic width probes, preserves split UTF-8 characters, and
replaces incomplete trailing bytes at end of file so navigation always advances.
These are source/test results, not measured device speed or heap claims.

The historical report retrieved November 13 records HalFile::close with a null impl.
Stack addresses map through ImageImportActivity::discard/onExit in the retained rc4
ELF. Empty preview-file closes are guarded; web reader/cache cleanup also guard
unopened files. The host substitute now asserts on default-handle close. This does
not establish that the reported idle Text Web restart is fully fixed.

## Hardware acceptance

Keep the previous firmware and back up card progress/settings before flashing. X4 Pro
and X3 require their separate images. This source/build pass has not flashed or
restarted either device. The human owns physical acceptance results.

- Open Image Import and leave immediately; cancel a fetch; preview/save/leave. No restart.
- Leave Text Web idle past the previous failure window. Exit/reopen repeatedly. Capture
  a fresh crash report if it resets; measure internal/PSRAM free and largest blocks.
- Search for a public topic with both shortcuts. Check results and provider-error handling.
- Follow A to B to C; go Back twice/Forward twice. Repeat offline, branch, reload,
  cancel and delete a page. Check missing-history entries do not block traversal.
- Tap underlined [n] in newly saved HTML; literal [n] stays inert. Try Links/physical
  buttons with legacy caches, unsupported links and more than 20 visible markers.
- Check portrait, inverted, landscape CW/CCW, touch mapping and remapped buttons.
  Check long UTF-8 lines/chunk boundaries and long-article previous/next navigation.
- Read/export v1, reload to v2, restart/read. Books, progress, settings, sleep art and
  unrelated caches must be unchanged. Never bulk-clear book or web caches.
- Repeat on X3: internal free heap above 50 KiB and no declining trend; measure largest
  blocks separately. Record heap across network/reader transitions, not only at idle.

Provider parity: both peers can inspect the same downstream C++ candidate. This change
requires no provider configuration, private-context copying, peer wake or instruction
synchronization. The prior writer and unrelated held tasks remain untouched.
