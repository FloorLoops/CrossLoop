# CrossLoop

Experimental e-reader firmware for the **Xteink X4 Pro** and a dedicated **Xteink X3** build, based on [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).

**v0.9 testing.** This is an independent downstream project, not an official CrossPoint release. X4 Pro and X3 require their own application images; do not interchange them. Physical validation is ongoing.

- [CrossLoop features and status](personal_features/FEATURES.md)
- [rc4 setup and device testing](personal_features/RC4-TESTING.md)
- [Wi-Fi updates, publishing and recovery](personal_features/OTA.md)
- [Build setup and upstream lineage](FORK.md)
- [Original CrossPoint documentation](UPSTREAM-README.md)

The first configured release is `0.9.0-rc.2`. Once it is manually installed, newer signed releases are offered through **Settings → System → Check for Updates**. Test versions follow the CrossLoop testing feed; stable versions follow stable releases. See the OTA guide before installing, including the initial dedicated-X3 transition limitation.

The rc4 candidate adds native text browsing, TickTick, RSS/Atom, illustrated weather, a read-only calendar agenda, editable library details and genre/series sorting. It also includes daily sleep cards, image import, reading statistics, Crossed Orbits holiday artwork and optional timed light restoration on X4 Pro. Connections are user-configurable. The text browser does not run JavaScript; calendar uses an HTTPS ICS subscription. Read the rc4 guide for limits and physical tests before treating this candidate as stable.

Optional picture packs contain Forest Dawn and Quiet Orbit in each panel's dimensions. These are SD-card files; firmware OTA does not install them automatically.

The source retains the upstream MIT license and notices, and the pinned FreeInk SDK submodule. Initialize submodules before building. Source and releases contain no reader books, account credentials or private signing key.
