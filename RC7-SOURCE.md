# CrossLoop 0.9.0-rc.7 source export

This is the source retained for the experimental X4 Pro Dream OS application.
Reader base: aa7f9f4cf07cdff44fbd3aa66ca9755416e95d16. SDK gitlink:
5e94fe7480f019c89fbf691aef87523eb1b46704. The source asset includes the exact
1032 copied SDK files used for this build, plus the 54 reviewed reader overlays.

The native browser comprises the included NetSurf 3.11 publisher archive,
12 files under native-browser/netsurf-overrides applied over its extracted
netsurf-all-3.11 tree, 18 frontend files, and eight native build files plus
the API probe and build launcher. The zlib 1.4 source archive is included;
unpack it under native-browser/third_party before configuring. Preserve all upstream COPYING, LICENSE and
NOTICE files. NetSurf has its own GPL version 2 terms; the reader base's MIT
notice alone does not describe every component in the linked application.

The original build used Windows, ESP32-S3, PlatformIO x4pro-native-probe,
the checked-in platformio.local.ini, native archive LTO and project-only LTO.
Build files retain their original local path assumptions; relocate those paths
to the source directories before a new build. This export was hash-checked,
not rebuilt from a clean portable environment. It is corresponding source for
inspection and maintenance, not a claim of reproducible binary output.

The public application is an app-only image, not a merged flash image.
No X3 rc.7 image is included or implied. The existing X3 update feed stays at
rc.4. Reader data, credentials, signing private keys, device backups, compiler
caches, build logs and the debugging ELF are not part of this source asset.
