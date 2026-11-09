# CrossLoop Wi-Fi updates

CrossLoop uses signed public releases at `FloorLoops/CrossLoop`. No GitHub login,
personal API key or running companion computer is needed on the reader.
The reader downloads updates only when **Settings → System → Check for Updates**
is opened and installs only after **Update** is confirmed. This does not add
background checks, remote wake or forced updates.

## Version and channel contract

- Testing versions end in `-rc.N`, with a positive increasing number. They read
  `releases/download/crossloop-testing/personal-MODEL.manifest`.
- Stable versions use `major.minor.patch`, read
  `releases/latest/download/personal-MODEL.manifest`, and reject RC versions.
- A testing build accepts a newer RC or stable version. Installing stable
  firmware switches to the stable channel. Returning to testing then requires
  a deliberate manual install.
- `MODEL` is `x4pro` or `x3`. Every version is a separate GitHub tag matching
  the version exactly. Its application is `personal-VERSION-MODEL.bin`.
- RC comparison is numeric: rc.10 follows rc.9. Same and older versions are
  rejected. The manifest and embedded application identity must agree.

The initial bootstrap is `0.9.0-rc.2`. It replaces the old unconfigured trial
whose internal label was `Personal 1.0.0-rc.1`, using a manual application
installation. That deliberate one-time version reset cannot use the old OTA
channel. Later updates must increase from the new version. The next candidate
`0.9.0-rc.3` can verify the actual OTA path without changing reader features.

## Publishing

The firmware contains the public P-256 key in `src/network/PersonalRelease.h`.
Keep its private counterpart outside the checkout, firmware, SD card, releases
and build logs. Retain a secure private backup. Losing it requires a manual
reader update to establish another key; changing a repository file alone does
not change keys already installed on readers.

1. Increase `CROSSLOOP_RELEASE_VERSION` in `PersonalRelease.h`. Never reuse a
   published version for different bytes. Complete the repository's review and
   build checks for both supported targets.
2. Retain exact source, pinned SDK revision, image validation, hashes and test
   evidence. Firmware binaries must be application images, not merged full
   flash images. Keep the MIT license and upstream notices with source.
3. Stage each model with Python and the existing `cryptography` dependency:

   ```text
   python personal_features/release_manifest.py --model x4pro --version VERSION --application PATH_TO_APPLICATION --private-key PATH_OUTSIDE_CHECKOUT --output NEW_STAGING_DIRECTORY
   ```

   Repeat with `--model x3` and the dedicated X3 application. The helper rejects
   wrong-chip, wrong-board and wrong-version images. It signs model, version,
   byte count and SHA-256. Complete ESP image validation remains a build check.
4. Publish the exact source/tag and both staged application/manifest pairs to
   the versioned release. Mark RC releases as prereleases. Download the public
   assets without authentication and verify their signatures and image hashes.
5. Only after that succeeds, replace each model's manifest on the mutable
   `crossloop-testing` prerelease. Each manifest points to the immutable version
   tag, not a mutable binary. A brief missing manifest during replacement can
   make a check fail; retry after publication completes. Do not move the feed
   before both model assets are downloadable. Do not mutate released version
   tags or binaries. For stable releases, publish both manifests on the latest
   stable release and also promote them to the test feed so testers can graduate.

The GitHub `latest` shortcut excludes prereleases, which is why testing uses an
explicit feed. See [GitHub release documentation](https://docs.github.com/en/rest/releases/releases).
Publication is performed by the maintainer using their existing GitHub access;
no private signing key is stored in GitHub Actions by this setup.

## Installation and verification

An already configured CrossLoop reader needs only Wi-Fi and sufficient battery.
Open **Settings → System → Check for Updates**, select a network if asked,
verify the offered version, and choose **Update**. Wait for the restart, then
check **Settings → System → About** and reopen the previously used book.

For the initial bootstrap, a manually installed application is required. It
can be copied using USB or Wi-Fi File Transfer and selected through the existing
SD firmware updater when the installed firmware accepts that model tag. The
dedicated X3 application must not be assumed compatible with an official
combined X3/X4 updater; establish the X3 transition separately.

Before a live trial, retain the reader's SD settings/progress and original full
flash recovery image locally. OTA writes the inactive application slot and
selects it only after size, SHA-256, embedded version, model and ESP image checks
pass. It does not repartition flash or intentionally modify book/settings files.
The old application occupies the other slot after a successful update. Normal
future OTA cycles overwrite that slot, including any original official firmware
kept there. A host recovery backup is therefore still necessary. This setup
does not promise automatic rollback after every possible boot/runtime failure.

Acceptance requires a real download, restart and version readback on each
model, plus preservation of settings/progress and a verified recovery route.
Host parsing/signature tests and successful builds do not establish device
network compatibility, power-loss recovery, runtime heap or physical behavior.

## Current evidence

The bootstrap `0.9.0-rc.2` has passed 318 production C++ host checks across both
models/channels, three host signing tests, four independent source reviews,
and final X4 Pro/X3 builds. Both ESP image checksums and SHA-256 trailers are
valid. Each staged manifest verifies against the public key embedded in its
application. Source build inputs were matched across the publication checkout
and both build locations.

Both releases and the testing feed were downloaded without authentication and
verified against the embedded public key, model, version, size and SHA-256.
The X4 Pro user completed the setup, reported the rc.3 wireless update in
progress, then confirmed About displays `CrossLoop 0.9.0-rc.3` after the update.
The user also confirmed different per-book text settings now save correctly.
This passes the core X4 Pro OTA flow by user report. It does not establish
instrumented runtime memory, power-loss recovery or every preservation case.
The X3 initial installation and actual OTA test remain pending. CrossLoop
remains v0.9 testing until the wider device checks pass.

The `0.9.0-rc.3` upgrade candidate changes only the release-version constant.
Both final target builds and image/signature/model checks passed for that
version as well. It provides an actual newer version for the first on-reader
OTA trial after rc.2. The X4 Pro trial is user-confirmed; X3 acceptance remains pending.


## rc4 candidate verification

`0.9.0-rc.4` adds the native apps and reader improvements described in
[RC4-TESTING.md](RC4-TESTING.md). Both final target builds passed without compiler
warnings. Their application images have valid ESP checksums and hash trailers,
the correct chip/model/version identities, and the existing public verification
key. Both signed manifests verified against that key. The 806 firmware/build
inputs matched across the canonical, X3 build and publication checkouts, with
the pinned SDK revision retained.

The updater regression suite passed 318 C++ checks across both models/channels
and three host signing tests. Feature-specific host suites and the four
independent review axes are recorded in the feature guide. These results do
not establish live account behavior, sleep/wake timing, runtime heap, recovery
or installation on either reader. X4 Pro rc3 OTA/profile acceptance remains the
baseline. X3 still needs its separate initial-install/OTA acceptance.
