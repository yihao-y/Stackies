# Stackies

[简体中文](README.zh-CN.md)

Stackies is a lightweight desktop sticky-note app built with **C++20, Qt 6, and SQLite**. Keep notes in separate windows, combine them into stacks with colorful side tabs, and use **Overview** to see and rearrange a whole stack at once.

Stackies is designed for **Windows and Linux**. Notes are stored locally without requiring an account.

## Features

- **Flexible stacks:** Merge notes by dragging, reorder tabs, move notes between stacks, or detach any note into its own window.
- **Overview:** Browse all notes in a stack on a glass-style canvas; rearrange or drag them out.
- **Rich-text editing:** Formatting, text colors, bullet and numbered lists, and checklists.
- **Personalization:** Paper-color presets and custom swatches, stack opacity, and always-on-top controls.
- **Safe organization:** Hide notes without deleting them, and restore deleted notes from Recently Deleted.
- **Multilingual interface:** English, 简体中文, 繁體中文, 日本語, 한국어, and Español.

## Install and run

- **Windows (x64):** Extract the portable ZIP and run `Stackies.exe`. If Windows reports that the Microsoft Visual C++ runtime is missing, run the included `vc_redist.x64.exe`.
- **Linux (x86_64):** Make the AppImage executable (`chmod +x Stackies-*.AppImage`) and launch it. X11/`xcb` is recommended for full window-management behavior.

To build from source instead, see [Platforms and building from source](#platforms-and-building-from-source).

## Getting started

1. Use the system tray menu → **New standalone note** to create a note.
2. Drag one note's title bar onto another and hold for about **800 ms**. Release when the merge indicator appears to create a stack.
3. Use the colored side tabs to switch between notes. Drag tabs to reorder, move to another stack, or detach.
4. Select the grid icon in a stack's header to open **Overview**.
5. Use **×** in a note's header to hide its window; use the tray menu → **Show all notes** to show hidden notes again.

### Controls

| Action | How |
| --- | --- |
| Add a note to a stack | Header **+** |
| Edit note text | Pencil icon or double-click the body; **Ctrl+Enter** to finish |
| Merge notes | Drag one title bar onto another; hold until the merge indicator appears, then release |
| Reorder / move / detach | Drag a colored side tab or an Overview card |
| Change paper color or stack opacity | Right-click the header or side tab |
| Change text color and formatting | Use the editor toolbar |
| Pin / unpin a stack | Header pin icon |
| Hide a stack | Header **×** (does not delete the note) |
| Delete a note | Right-click its header or tab → **Delete note** |
| Recover a deleted note | Tray menu → **Recently Deleted…** |
| Change UI language | Tray menu → **Language**; restart to apply |
| Change database location | Tray menu → **Change Data Location…** |

Deleted notes stay in **Recently Deleted** until they are permanently removed. Stackies also remembers window geometry, visibility, tab order, and other saved workspace settings between sessions.

## Data and privacy

All notes and stacks are stored in a local SQLite database named `notes.sqlite`. Stackies does not require an account or an external service to store notes.

| Platform | Default data directory |
| --- | --- |
| Windows | `%APPDATA%\Stackies` |
| Linux | `~/.local/share/Stackies` (or the system's XDG data location) |

Use **Change Data Location…** to select another directory, including a parent or child of the current one. Stackies saves and verifies a database snapshot before switching, then exits so you can restart it. If the destination already has a Stackies database, choose **Merge** (keep both sets of notes), **Switch** (use the destination and retain the original database), or **Overwrite** (replace the destination with your current notes). Merging preserves distinct note versions, stack groups, and deleted notes. Overwriting saves a dated backup of the replaced destination database before making changes. **Restore Default** returns to the OS data directory.

The temporary SQLite `-wal` and `-shm` files may exist while the app is running. For manual backups or transfers, **quit Stackies first** and keep any remaining SQLite files together. If using a folder synchronized by a cloud-drive app, run Stackies on only one device at a time and allow synchronization to finish before opening it elsewhere. Folder synchronization is not a live, multi-user SQLite service.

While Stackies is open, it checks the database revision every **30 seconds**. If another device's changes have arrived, idle windows reload without reopening the app. Active editors and unsaved changes defer reload; each save checks the revision before writing. This is **not** simultaneous-edit synchronization: only edit on one device at a time, and wait for the cloud client to finish syncing before switching devices. If a revision conflict occurs, Stackies preserves the incoming database and adds your locally modified notes as **separate conflict-copy notes**. After a successful save you can keep working or quit normally; you do not need to open a recovery database. Only one Stackies instance can run for the same data directory. Launching it again activates the existing window (or restores a hidden note).

An explicit UI location takes priority over `STACKIES_DATA_DIR`, which remains an optional fallback for development and testing.

### Diagnostics

Diagnostic files stay on the **local computer**, independent of the database directory:

- Windows: `%LOCALAPPDATA%\Stackies\logs\app.log`; crash dumps in `%LOCALAPPDATA%\Stackies\crashes`.
- Linux: `~/.local/state/Stackies/logs/app.log` (or `XDG_STATE_HOME`).

Normal operation logs warnings and errors; pass `--debug` when detailed messages are needed. Logs are not moved with the database and have no tray-menu entry.

## Platforms and building from source

**Requirements:** CMake 3.21+, C++20, SQLite3, and Qt 6.4+ (Core, Gui, Widgets, Svg, Network, LinguistTools).

### Windows

Install Visual Studio 2022 C++ tools, Qt 6 for MSVC x64, Python 3, and SQLite3 (for example through vcpkg).

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  "-DVCPKG_TARGET_TRIPLET=x64-windows-static-md" `
  "-DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64"
cmake --build build --config Release
```

### Linux

For Debian/Ubuntu-based systems, install the build dependencies and compile:

```bash
sudo apt install build-essential cmake qt6-base-dev qt6-svg-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools libsqlite3-dev libx11-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/Stackies
```

For optional desktop integration, the Linux build includes `stackies.svg` and `stackies.desktop`. The portable packaging script also bundles the runtime dependencies and license materials.

Desktop integration is optional. For a portable directory, copy `stackies.desktop` to `~/.local/share/applications/` and edit its `Exec=` to the absolute path of `run.sh` (or `Stackies` for a system-library build), and its `Icon=` to the absolute path of `stackies.svg`. An AppImage contains its own desktop metadata; simply launching it does not register it in the application menu.

**Linux caveats:** X11/`xcb` offers better support for window positioning and pinning. On native Wayland, compositor restrictions limit some window-management and drag-to-merge behavior. Linux desktop behavior varies by environment.

### Tests

Run the core and UI regression tests from an ordinary Qt build:

```bash
ctest --test-dir build --output-on-failure
```

For core-only tests without Qt:

```bash
cmake -S . -B build-test -DSTICKY_BUILD_UI=OFF -DBUILD_TESTING=ON
cmake --build build-test
ctest --test-dir build-test --output-on-failure
```

## License

Stackies is licensed under the [MIT License](LICENSE). Third-party components, including Qt libraries and Tabler icons, remain subject to their respective licenses.

## Acknowledgments

- Built with [Qt](https://www.qt.io/) and [SQLite](https://www.sqlite.org/).
- Toolbar icons are by [Tabler Icons](https://tabler.io/icons), licensed under MIT; see [their license](assets/tabler/LICENSE).
- See [Third-party notices](THIRD_PARTY_NOTICES.md) for dependency licensing information.
