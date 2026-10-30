# Qt LGPLv3 notice and library replacement (Windows)

Stackies itself is licensed under MIT. Its Windows portable ZIP includes
dynamically linked Qt 6.8.3 libraries and plugins with their own licenses,
including LGPLv3. The MIT license does not supersede these rights.

The released Qt libraries are ordinary DLL files, not statically embedded in
Stackies.exe. Users may replace compatible Qt DLLs and plugin DLLs to run
modified versions of the LGPL components. No license condition imposed by
Stackies prohibits reverse-engineering the application to debug modified
LGPL libraries.

## Replacing Qt libraries in a portable ZIP

1. Extract the Stackies Windows ZIP to a writable directory.
2. Replace the relevant `Qt6*.dll` files alongside `Stackies.exe` and any Qt
   plugin DLLs in subdirectories (e.g. `platforms/qwindows.dll`) with
   appropriately built, ABI-compatible alternatives.
3. Start `Stackies.exe` from the extracted directory. There is no application
   signature or integrity-check mechanism that prevents loading an
   appropriately compatible replacement Qt DLL.

Qt Base 6.8.3 and Qt SVG 6.8.3 corresponding source archives for the
distributed build must be made available under the distributor's control,
alongside the actual release:
- `qtbase-everywhere-src-6.8.3.tar.xz`
- `qtsvg-everywhere-src-6.8.3.tar.xz`

The original sources are published upstream at
https://download.qt.io/archive/qt/6.8/6.8.3/submodules/ .
If any Qt libraries were modified, those modifications must also be made
available. See `licenses/Qt-SBOM/` for Qt build metadata and
`THIRD_PARTY_NOTICES.md` for component notices. Complete applicable
GNU license texts must accompany the public ZIP.

This document does not replace the license terms applicable to any
third-party library or to redistributed Microsoft runtime components.
