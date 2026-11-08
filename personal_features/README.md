# CrossLoop reader features

CrossLoop is the custom OS name for the X4 Pro and X3 versions. The current testing milestone is v0.9; the installed candidate still displays Personal `1.0.0-rc.1`. This documentation adopts the name now. Firmware branding will change in a subsequent build. Feature acceptance and the requested timed light restore are tracked in [FEATURES.md](FEATURES.md).

These downstream features use CrossPoint 2.0.0's SD plugin interfaces and existing reader and sleep renderers. Keep the firmware source on `personal/x4-pro`. The Python tools run on BRUTUS with Python 3.10+ and the Pillow version in `requirements.txt`.

## Text Web

Fetch a public HTTPS page on BRUTUS. The companion verifies TLS through Python's default trust store, bounds downloads to 256 KiB, extracts readable text and retains up to 16 HTTPS links. The native catalog lists the cached pages. Downloading an entry saves a TXT document in `/WebPages` for CrossPoint's normal e-ink text reader. Page turns use the existing reader controls. To follow a numbered link, run the host command with the printed page ID and link number, then restart the catalog server to publish the new snapshot.

```powershell
python -B personal_features/browser.py --cache ../feature-preview/pages --url https://example.com/
python -B personal_features/browser.py --cache ../feature-preview/pages --follow 0f115db062b7c0dd 1
```

The second command is an example of the interface. Use a page and link number that actually exist in your cache. On-reader URL entry and live link activation remain future work. Pages that require scripts, logins, interactive forms, full CSS layout, or video need a companion browser that renders or extracts them. CrossPoint's ESP32-S3 X4 Pro target has 8 MB PSRAM, and the hardware still has only a few hundred KiB of internal RAM. [X4 Pro target](https://github.com/crosspoint-reader/crosspoint-reader/blob/2.0.0/platformio.ini) · [ESP32-S3 memory guidance](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/external-ram.html) · [Upstream scope](https://github.com/crosspoint-reader/crosspoint-reader/blob/2.0.0/SCOPE.md)

### Prepare a local offline SD package

Run from the fork directory and choose a new staging folder on the host.

```powershell
python -B personal_features/companion.py stage-offline --cache ../feature-preview/pages --output ../feature-preview/offline-sd
```

The command exports up to 24 cached Text Web pages as named TXT files in `WebPages/` and generates the sample TickTick image at `.crosspoint/personal-sleep/ticktick.bmp`. It reads local cache and fixture files and refuses to replace an existing output directory. For a later authorized SD trial, the TXT files open in CrossPoint's normal reader through **Browse Files → WebPages**. The X4 Pro fork's **Home → TickTick** and sleep selector share the staged image. Both can be tested with Wi-Fi off once the files are in place.

## Personal v1 candidate

Build either model from the fork with `Build-X4Pro.ps1 -Device x4pro` or `Build-X4Pro.ps1 -Device x3`, using the pinned setup in `FORK.md`. About displays the personal version alongside the CrossPoint base version. The dedicated X3 target uses 528 × 792 portrait assets. The X4 Pro uses 480 × 800.

On Windows, the X3 build's ESP-IDF framework requires resolved paths without spaces. Use a short local folder for the firmware checkout, tool cache and build output. The X4 Pro build can use the original workspace path. Verify the copied source and SDK against the candidate receipt before building the X3. The outer PlatformIO and the platform's inner environment both need the 6.1.19 pin to build the tuned ESP32-C3 framework. [CrossPoint 2.0.0 build configuration](https://github.com/crosspoint-reader/crosspoint-reader/blob/2.0.0/.github/workflows/ci.yml)

```powershell
python -B personal_features/companion.py stage-v1 --device x3 --cache ../personal-feature-demo/pages --rss personal_features/fixtures/feed.xml --today 2026-11-07 --output ../personal-feature-demo/v1-x3-test
```

Use `--device x4pro` and a new output folder for that model. `--rss` imports local RSS or Atom files. `--image` adds a folder image. `--today-data` accepts a local JSON object with optional `weather` and `calendar` text. **Home → Read Later** opens the offline queue. The sleep selector includes **Today**, and **Home → TickTick** uses the local snapshot. In a dictionary lookup, hold Select to save to **Browse Files → Word Bank**. Pass that local folder to `--word-bank` to use its entries in a later Word of the Day pack.

The profile correction has passed independent source review, 37 host checks, and X4 Pro/X3 firmware builds. Elias accepted the described behavior, and the corrected X4 Pro build is installed with a verified boot. Physical profile tests remain pending; the X3 build is not installed. In the corrected reader menu, **Save text settings for this book** saves font family, point size, line spacing and publisher styling for the current book path. It can be selected before editing or after choosing the desired font and size. Saving again updates the profile and keeps it enabled. Once enabled, changes to those fields are saved automatically. Other text controls remain global.

**Use global text settings** is a separate action shown while a profile is active. It removes only that book's profile and returns it to the defaults that were in place when the book opened. Global defaults stay unchanged after saving a book profile, including when the first save follows font/size edits. A moved or renamed book can need a new profile. Existing version 1 profiles contain no font family: migration retains their size, spacing and publisher styling and uses the current global family; the missing original family cannot be recovered. If a saved font is unavailable or fails to load, rendering can fall back to the built-in font while retaining the saved selection for a later retry. Choosing a different font explicitly still updates the book. Verify distinct fonts and sizes in two books, repeated Save, reopening, sleep/wake, reset and font-load recovery before accepting the correction on either device.

**Settings → System → Check for Updates** uses signed CrossLoop releases from `FloorLoops/CrossLoop`. The initial bootstrap is `0.9.0-rc.2`: an initial manual installation replaces the old unconfigured `Personal 1.0.0-rc.1` identity. RC builds follow the `crossloop-testing` release's model-specific manifest and accept strictly newer RCs or a stable release. Stable builds follow GitHub's latest stable release and reject RCs. The first stable install therefore leaves the testing channel. Every version has a separate release tag and model-specific application. See [OTA publishing and recovery](OTA.md) for the exact contract. The private signing key remains outside the source and reader. Host tests, publication, bootstrap installation and an actual Wi-Fi upgrade are separate evidence; this source change alone proves none of the hardware steps.

## TickTick sleep image

```powershell
python -B personal_features/ticktick.py --fixture personal_features/fixtures/ticktick.json --today 2026-11-06 --days 7 --rows 8 --output ../feature-preview/sleep.bmp
```

The renderer shows up to eight open tasks, sorted by due date and priority. The default includes overdue tasks and today's tasks. Use `--days` for upcoming tasks or `--include-undated` to add undated tasks. Timed dates use the host's local timezone. The image records its snapshot date. An unsuccessful refresh leaves the previous output file available, and the reader can display a previously copied `sleep.bmp` offline.

For a later live account configuration, obtain an OAuth token with `tasks:read` on BRUTUS, supply it in the host process environment as `TICKTICK_ACCESS_TOKEN`, and use `--project <id>` for each selected project in place of `--fixture`. The adapter uses only the official HTTPS project-data GET endpoint. Token acquisition and refresh are manual integration points. The renderer exports task titles and due labels as pixels. Keep real task images local or transfer them by USB. [TickTick Open API](https://developer.ticktick.com/docs/openapi.md)

## Prepare sleep-screen images

Run these examples on a computer from the fork directory. Use `--device x3` for a 528 × 792 monochrome BMP or `--device x4pro` for 480 × 800. TickTick uses bundled sample tasks. Folder images support BMP, JPEG and PNG and can be fit inside the screen or cropped to fill it.

```powershell
python -B personal_features/sleep_preview.py --output ../feature-preview/affirmation.bmp affirmation
python -B personal_features/sleep_preview.py --output ../feature-preview/word.bmp word
python -B personal_features/sleep_preview.py --output ../feature-preview/ticktick.bmp ticktick
python -B personal_features/sleep_preview.py --output ../feature-preview/folder.bmp folder './Sleep Screens' photo.jpg --framing fill
python -B personal_features/sleep_preview.py --device x3 --output ../feature-preview/online.bmp online https://raw.githubusercontent.com/crosspoint-reader/crosspoint-reader/2.0.0/docs/images/cover.jpg --framing fill
```

Online import accepts direct HTTPS links to JPEG and PNG. It verifies TLS and limits input to 8 MiB and 12 megapixels. The tool saves a BMP only after conversion succeeds. Copy it to the SD card and select it from the reader's image picker.

## Sleep-screen setting on the personal builds

The personal X3 and X4 Pro builds add five choices to CrossPoint's existing **Sleep Screen** setting. For a later authorized device trial, stage the BMPs in `/.crosspoint/personal-sleep/` on the SD card as `affirmation.bmp`, `word.bmp`, `folder.bmp`, `ticktick.bmp`, and `today.bmp`. Each choice loads its own file. CrossPoint's Quick Resume on Timeout setting takes precedence on timeout sleep. Weather can use its own `/sleep.bmp` through the original Custom mode.

**Settings → Display → Choose sleep image** browses SD directories and lists BMP files. Selecting a valid file saves its path and activates **Image from a folder**. It leaves the original file in place. If the selected file is unavailable, the reader tries the staged `folder.bmp` and then the default sleep screen. The other sources fall back to the default screen if their BMP is missing or rejected.

Open **TickTick** from Home to see the local snapshot. Cover-grid Home uses a bookmark icon for this entry. **Use for sleep** saves the same choice as selecting TickTick in Display settings. The shortcut requires a readable local snapshot and performs no account or network operation. The scaffold uses prepared snapshots for daily content and tasks. Native JPEG/PNG conversion, online image import and live daily or task updates remain separate work. Switch to a standard Sleep Screen choice before returning to upstream firmware.

## Staged catalog and pre-sleep hook

```powershell
python -B personal_features/companion.py stage --output ../feature-preview/plugins --origin http://127.0.0.1:8787
python -B personal_features/companion.py serve --cache ../feature-preview/pages
```

Loopback is the default for host tests. For a later device trial, stage with BRUTUS's private IPv4 address and start the companion bound to that address. The HTTP server exposes the public pages selected for its snapshot and one synthetic TickTick image. It has no route to export private TickTick task images or initiate webpage fetches.

The demo plugin uses `sleep.enter` to fetch a sample BMP before the normal sleep renderer draws it. A reader trial requires **Custom** sleep screen mode and Quick Resume off for timeout sleep. Select a single `/sleep.bmp` writer for the trial because Weather and this demo target the same file. Offline mode can use a copied image without the network plugin. The demo inherits CrossPoint's pre-sleep Wi-Fi and event budgets, including its 20% minimum for bringing up a disconnected radio. It uses the existing sleep refresh path. Measure display clarity, retained image, sleep latency, and battery impact on the X4 Pro before using a live refresh. [CrossPoint plugin events](https://github.com/crosspoint-reader/crosspoint-reader/blob/2.0.0/docs/plugin-events.md)
