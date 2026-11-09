# CrossLoop rc4 testing guide

This describes the rc4 testing candidate. It is not an installation receipt.
X4 Pro uses its 480 x 800 touch layout; X3 uses its own 528 x 792 button layout.
X3 has no frontlight settings. Keep rc3 and the recovery files until both readers
have passed their trials. A successful build does not establish device acceptance.

## New native apps

- **Text Web**, on Home, opens HTTP/HTTPS text pages directly using the reader's
  Wi-Fi. Open an address, select **Read saved page**, and use **Links** to follow
  links. Previous/next buttons turn text pages. Saved content works offline.
  The browser has one link-return step, 32 saved pages, up to 32 links per page,
  a 512 KiB download limit, 256 KiB extracted text, and 1024 reading pages.
  It does not run JavaScript or browser sign-in pages. RSS/Atom feeds are inside
  Text Web: configure up to four addresses, refresh, then open saved headlines.
- **TickTick**, on Home or Settings > Connectivity, accepts a personal API token.
  Create it using TickTick's web Settings > Account > API Token, then enter it
  in the reader's masked field. Select up to eight lists and refresh. Open a
  task to view details or confirm completion. A failed/uncertain completion
  response is reported; check TickTick before trying again. **Use for sleep**
  selects the same cached tasks used by the app. Maximum snapshot: 256 records.
- **Weather**, in Settings > Connectivity, accepts latitude, longitude, a location
  label, and Fahrenheit/Celsius. Refresh loads illustrated current conditions
  and three forecast days from Open-Meteo. Location is never built into firmware.
- **Calendar**, in Settings > Connectivity, accepts a private HTTPS iCalendar
  subscription. It shows a saved 30-day agenda in the reader's timezone.
  Set the reader's date and timezone first. Calendar is read-only; it does not
  edit events or provide a Google/Microsoft OAuth login. Ordinary daily, weekly,
  monthly and yearly recurrence is supported; unsupported recurrence/timezone
  rules fail the import and retain the last agenda. Limits: 2 MiB input,
  4096 source events, 255 saved events, and 64 relevant recurrence overrides.
  Today sleep uses a small preview of up to 12 upcoming events from the last
  refresh, alongside the weather. Refresh after that preview is exhausted.

All connections start unconfigured. Credentials are entered on the reader;
there is no phone/desktop paste form for these new apps in this candidate.
Tokens and the private calendar address stay in internal settings, while saved
tasks, calendar events and articles are on the SD card. TickTick and Calendar
offer Disconnect to remove their local connection and cached data; it does not
revoke a provider token. The browser, RSS and Weather have no Disconnect action.
Saved content shows its sync time and can be out of date. Live account tests
remain required; host tests use fixtures only.

## Library and reading

In Library, hold a book row and choose **Edit book details**. Edit title, author,
genre, series and series number, then Save. Blank fields inherit the book's
embedded metadata. **Use embedded book details** clears the draft; Save applies
the reset. These are local library labels, also used on Home. The EPUB and
reading position are unchanged. Metadata sidecars move with the book.

Library adds Genre and Series tabs and includes those fields in search. Series
numbers sort numerically. The updated index rebuilds once while preserving
arrival identity from the previous format. A failed rebuild keeps saved edits
and the prior index; use Refresh to retry.

Settings > Reader > **Reading statistics** shows local reading totals and recent
days. It counts completed forward page turns, with intervals from 2 seconds to
30 minutes; each interval is capped at 30 minutes. It is an estimate, not a
continuous attention timer. Reset requires confirmation.

The dictionary has **Save to Word Bank** on X4 Pro. On X3, hold logical Confirm
for at least 600 ms in a definition. Saved TXT entries are in `/Word Bank`.
Saving the same word again keeps the existing entry, including any later notes.

## Sleep, light and pictures

**Holiday emblem and boot screen** toggles date-based Crossed Orbits artwork.
Ordinary dates, unknown dates and the disabled setting use the regular emblem.
Personal sleep screens remain selected independently.

Affirmation and Word of the Day now render on the reader from 32 built-in
affirmations and 32 words with short definitions. The selection changes with
the reader's local date and repeats after 32 days. No computer or network is
needed. An unknown date uses the first entry and shows a date-setting reminder.
The older prepared affirmation/word BMP files remain on SD but are no longer
used by these two choices. Word Bank entries remain separate saved notes.

**Refresh online content before sleep** is off by default. Sleep uses saved
content, and reading-progress/plugin events remain queued for later delivery.
Enabling it can add network waiting. Native apps refresh when requested inside
each app; this switch controls the existing plugin drain, not an automatic
refresh of every new app.

Choose a sleep image in Settings > Display. Preparing it during selection moves
the first conversion out of sleep entry. The online image importer accepts a
direct JPEG/PNG address (up to 4 MiB and the converters' 2048 x 3072 limits),
then previews it before applying. A webpage containing an image is not a direct
image address. Fit/crop changes may need preparation again. Failed replacements
retain a recoverable previous file; physical SD power-loss recovery is untested.

The separate [picture packs](wallpapers/README.md) add Forest Dawn and Quiet Orbit.
Their native one-bit BMPs need no first-use conversion. They are SD files and
are not installed by the firmware OTA update.

On X4 Pro, **Restore Light on Wake** offers **Always off**, **Restore previous
state**, and **Restore within time window**. The window is 1–60 minutes, default
5. A timed wake restores the prior brightness/warmth only if the light was on
before sleep and the elapsed time is valid and within that window. Cold boots,
invalid elapsed time and a previously off light leave it off.

## Device trial

1. Record the starting book/page and settings. Verify rc4 in About after the
   candidate is published and installed. Keep the X4 Pro's already-passed rc3
   OTA/profile results as baseline; X3 needs its separate initial-install trial.
2. Test two books with different text settings, edited labels and series values.
   Move a test book, restart, and confirm its labels and reading position.
3. Try each app online, then reopen its saved content offline. Cancel a download,
   use an invalid address, and check the last successful content is retained.
   Complete only a disposable TickTick test task. Verify disconnect/reconnect.
4. Check new screens using touch on X4 Pro and buttons on X3. Check portrait,
   inverted portrait and both landscape orientations where the screen supports
   them; the standalone text reader and sleep screen intentionally use portrait.
5. Compare first and repeated sleeps after each screen selection, offline and
   online. Record seconds to the final sleep picture and seconds to a responsive
   reader after waking. Check the exact same book/page returns.
6. On X4 Pro test the light before/at/after the chosen window, with the light
   already off, and after a cold boot. Verify X3 has no light control.
7. Repeated Wi-Fi/image/book transitions need measured internal free memory,
   largest allocation block and worker stack headroom on both devices, especially
   X3. The intended C3 reading baseline is more than 50 KiB free without leaks;
   total free memory alone does not prove TLS or image allocations fit.

Stable v1 follows successful acceptance on both readers, not source or host-test
completion. Unverified timing, account behavior and recovery stay open.

The app caches share one fixed record format and version to keep storage and
recovery code small. A future change to that shared format must account for all
six datasets together. This is a maintenance constraint; it does not combine
their content or credentials.
