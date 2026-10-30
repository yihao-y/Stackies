# Third-party software notices

Stackies application source code is licensed under the [MIT License](LICENSE).
That license does **not** replace the terms applicable to third-party software.

## Qt 6

The current Linux portable release uses dynamically linked Qt **6.8.3**:
Qt Base (Core, DBus, Gui, Network, Widgets, XcbQpa, and desktop/input
plugins) and Qt SVG (Svg and image plugins). On Windows, the exact deployed
DLL and plug-in inventory must be reviewed separately.

The open-source Qt components used by Stackies are generally available under
the **GNU Lesser General Public License v3 (LGPLv3)**; different licenses
may apply to particular bundled third-party components. The Linux bundle
includes the full LGPLv3/GPLv3 texts, build-specific
`licenses/Qt-SBOM/qtbase-6.8.3.spdx.json` and
`licenses/Qt-SBOM/qtsvg-6.8.3.spdx.json`, and
`Qt-LGPL-RELINKING.md` with instructions for replacing Qt libraries.

- Qt licensing: https://doc.qt.io/qt-6/licensing.html
- Qt LGPLv3 terms: https://doc.qt.io/qt-6/lgpl.html
- Qt open-source obligations: https://www.qt.io/development/open-source-lgpl-obligations
- Qt source code: https://code.qt.io/cgit/qt/

If distributing builds containing Qt libraries, distributors must review the
licenses of the *exact versions and binaries shipped*, include the applicable
license and copyright texts, fulfill applicable Qt source-code availability
requirements, and preserve the ability to replace/relink the LGPL libraries.
A link to upstream Qt source code alone does not necessarily meet those
distribution obligations.

## Linux system and ICU libraries

Linux bundles contain additional dynamic libraries, including the XCB support
libraries and components such as FreeType, libpng, PCRE2, DBus, GLib, SQLite
and ICU 73. The actual bundled library names and their source paths are
recorded in `licenses/BUNDLED_LIBRARIES.json`. Copyright/license
declarations for Ubuntu-provided packages are in `licenses/Ubuntu/`.
The license file(s) for the separate ICU runtime are provided in
`licenses/ICU-Conda/` where applicable.

Finding a copyright file for each binary is only a provenance check:
distributors must separately review any license-specific source,
attribution, or relinking requirements. LGPL/GPL license texts alone do not
automatically discharge all such requirements.

## Tabler Icons

The toolbar icons in `assets/tabler/` come from
[Tabler Icons](https://tabler.io/icons) and are licensed under **MIT**.
The included copyright and license notice is preserved at
[assets/tabler/LICENSE](assets/tabler/LICENSE).

## SQLite

Stackies uses [SQLite](https://www.sqlite.org/), whose core source code is
in the [public domain](https://www.sqlite.org/copyright.html).

## Binary package checklist

Before publishing a Windows ZIP or Linux AppImage, inspect the *actual*
bundled libraries and plugins for their corresponding license notices.
Also include Stackies' `LICENSE`, this notices file, and any required
third-party full license texts and source-code/offer materials. This document
is an inventory starting point, **not** a certification that a particular
binary package satisfies every third-party license.
