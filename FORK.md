# CrossLoop firmware fork

CrossLoop is a custom OS for the X4 Pro and X3, based on CrossPoint. Current physical testing is v0.9. New builds show `CrossLoop 0.9.0-rc.N` in About; the original trial used the internal label `Personal 1.0.0-rc.1`. The initial OTA setup uses a manual application update to make that version transition. Subsequent releases must increase their version. Existing storage paths and board identifiers remain compatible. Upstream source, license and notices are retained.

This local fork starts at [CrossPoint 2.0.0](https://github.com/crosspoint-reader/crosspoint-reader/releases/tag/2.0.0), commit `42d58daaabcf469a2e5e1ba871714fb9dcf51d29`. Work goes on `personal/x4-pro`. The firmware, SDK, license, and upstream notices remain in the source tree.

The build uses the upstream `x4pro-gh_release` target for the ESP32-S3 X4 Pro. Build tools and cached packages live beside this repository under `x4-build-tools` and `x4-pio-core`.

## Build on Windows

Run from this repository in PowerShell:

```powershell
.\Build-X4Pro.ps1
```

The script compiles with two jobs by default. The firmware image is written to `.pio/build/x4pro-gh_release/firmware.bin`. This entry point only compiles.

For a fresh checkout, initialize the pinned submodules and create the local Python environment first:

```powershell
git submodule update --init --recursive
python -m venv ..\x4-build-tools
& ..\x4-build-tools\Scripts\python.exe -m pip install -r .\build-requirements-windows.txt
```

BRUTUS's default Git could not launch its submodule command. The Git bundled with Codex could. To use that copy for the build process, run:

```powershell
$buildGit = (Get-Command git -All | Where-Object Source -Like '*codex-primary-runtime*' | Select-Object -First 1).Source
.\Build-X4Pro.ps1 -GitDirectory (Split-Path -Parent $buildGit)
```

The script enables long Git paths for dependency checkouts and UTF-8 for the asset generator's multilingual output. The outer PlatformIO Core and the board platform's internal Python environment both use pioarduino 6.1.19, matching the upstream build. [Upstream CI](https://github.com/crosspoint-reader/crosspoint-reader/blob/2.0.0/.github/workflows/ci.yml)

## Boundaries for later work

The local build produces an upstream firmware baseline for comparing later changes. The downstream feature status and signed update workflow are documented in `personal_features/FEATURES.md` and `personal_features/OTA.md`. Before a hardware trial, keep a tested way back to the official release and check reading positions, USB transfer, sync, plugins, and the sleep screen on the X4 Pro.
