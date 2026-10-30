# Qt LGPLv3 notice and library replacement (Linux)

Stackies is licensed under the MIT License. The portable Linux distributions
also contain dynamically linked Qt 6.8.3 libraries and plugins. Those Qt
components are third-party software available under their respective
open-source terms, including LGPL-3.0-only. Qt's LGPL terms and rights are
independent of the MIT License covering Stackies itself.

* Qt modules in this build: Qt Base (Core, DBus, Gui, Network, Widgets,
  XcbQpa and platform/input plugins) and Qt SVG.
* The full LGPLv3 and GPLv3 license texts are in `licenses/`.
* SPDX metadata from the Qt 6.8.3 Linux build is in `licenses/Qt-SBOM/`.
* See `THIRD_PARTY_NOTICES.md` and `licenses/Ubuntu/` for other components
  distributed in the package. The complete corresponding Qt source archives
  for this released build should be provided with the GitHub Release:
  `qtbase-everywhere-src-6.8.3.tar.xz` and
  `qtsvg-everywhere-src-6.8.3.tar.xz`.
  See https://download.qt.io/archive/qt/6.8/6.8.3/submodules/ for upstream
  originals. Any additional modifications must be made available too.

## Replacing Qt libraries

The Qt libraries are dynamically linked, not statically combined into the
Stackies executable. You may replace Qt shared libraries and Qt plugins with
modified, ABI-compatible builds at your own discretion. No restriction on
reverse-engineering for debugging modifications to the LGPL libraries is
imposed by Stackies' MIT license.

**Portable Linux ZIP:** unpack the ZIP, and replace the relevant `libQt6*.so.6`
files under `lib/` (and Qt plugins under `plugins/`) with ABI-compatible
versions. Then launch through `./run.sh`. The program's `run.sh` points to
the bundled `lib/` and `plugins/` directories.

**Linux AppImage:** it is a SquashFS-based application archive. Extract it
using `./Stackies-*.AppImage --appimage-extract`, replace the Qt shared
libraries/plugins in `squashfs-root/lib/` and `squashfs-root/plugins/`, and
run `./squashfs-root/AppRun`. You may also repack the modified AppDir into
an AppImage using compatible AppImage tools. The application does not enforce
code signing or integrity checks that would prevent using a modified library.

See Qt's published LGPL obligations for further information:
https://www.qt.io/development/open-source-lgpl-obligations

This document describes how the Linux binary package is assembled and does
not replace any third-party license.
