#!/usr/bin/env python3
"""Stage Stackies' Linux portable directory from an existing Qt 6.8.3 build.

This prepares one directory for both the portable ZIP and an AppImage AppDir.
It does not install desktop entries or claim to complete a legal license audit.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
from collections import deque

# Tested with official Qt 6.8.3 Linux gcc_64. Prefer explicit names to copying
# every platform/embedded-device plugin bundled in that Qt installation.
QT_PLUGINS = {
    "platforms": ("libqxcb.so", "libqoffscreen.so"),
    "platforminputcontexts": (
        "libcomposeplatforminputcontextplugin.so",
        "libibusplatforminputcontextplugin.so",
    ),
    "platformthemes": ("libqxdgdesktopportal.so",),
    "imageformats": ("libqjpeg.so", "libqgif.so", "libqsvg.so"),
    "xcbglintegrations": (
        "libqxcb-egl-integration.so", "libqxcb-glx-integration.so",
    ),
}

# Host libraries: do not ship a Colab copy of glibc, X11 core ABIs, GL
# drivers, or libstdc++. Deliberately NOT excluding xcb-cursor, icccm,
# image, keysyms, render-util, xkbcommon-x11, etc. that Qt xcb may need.
# Based on https://github.com/AppImageCommunity/pkg2appimage/blob/master/excludelist
# with an intentional exception: vendor-neutral libOpenGL.so.0 is absent
# from some otherwise functional GNOME systems (Ubuntu's libopengl0 package).
# It is a hard runtime requirement of the distributed Qt binary, so bundle
# this ABI-stable dispatch-facing library while keeping hardware-dependent
# libGL/libEGL/libGLX and libGLdispatch on the host.
HOST_LIBS = {
    "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0",
    "librt.so.1", "libresolv.so.2", "libutil.so.1", "libanl.so.1",
    "libmvec.so.1", "libstdc++.so.6",
    "libGL.so.1", "libEGL.so.1", "libGLdispatch.so.0",
    "libGLX.so.0", "libdrm.so.2",
    "libglapi.so.0", "libgbm.so.1",
    "libxcb.so.1", "libX11.so.6", "libX11-xcb.so.1",
    "libwayland-client.so.0",
}


def linked_libraries(binary, env):
    result = subprocess.run(
        ["ldd", str(binary)], env=env,
        capture_output=True, text=True, check=True,
    )
    found = []
    for line in result.stdout.splitlines():
        if "not found" in line:
            raise RuntimeError(f"{binary}: unresolved dependency: {line}")
        if "=>" not in line:
            continue
        soname, destination = line.split("=>", 1)
        soname = soname.strip()
        location = destination.strip().split()[0]
        if location.startswith("/"):
            found.append((soname, Path(location)))
    return found


def package_owner(path):
    # Ubuntu multiarch package ownership must be read from actual resolved
    # binaries. Preserve the package's complete Debian copyright declaration.
    for candidate in (str(path), str(path.resolve())):
        result = subprocess.run(
            ["dpkg-query", "-S", candidate],
            capture_output=True, text=True,
        )
        if result.returncode == 0:
            for line in result.stdout.splitlines():
                if ": " in line:
                    return line.split(": ", 1)[0].split(",")[0].split(":")[0]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", required=True, type=Path)
    ap.add_argument("--build", required=True, type=Path)
    ap.add_argument("--qt-root", required=True, type=Path)
    ap.add_argument("--output", required=True, type=Path)
    args = ap.parse_args()

    project, build, qt, out = (p.resolve() for p in (
        args.project, args.build, args.qt_root, args.output,
    ))
    exe = build / "Stackies"
    if not exe.is_file():
        raise FileNotFoundError(f"Build the executable first: {exe}")

    if out.exists():
        shutil.rmtree(out)
    (out / "lib").mkdir(parents=True)
    (out / "plugins").mkdir()

    shutil.copy2(exe, out / "Stackies")
    (out / "Stackies").chmod(0o755)
    # These release assets are source files, not compiled outputs.
    # Reading them directly from the checkout allows packaging an existing
    # executable immediately after git pull, without rerunning CMake.
    release_assets = {
        "stackies.svg": "assets/app-icon.svg",
        "stackies.desktop": "packaging/linux/stackies.desktop",
        "LICENSE": "LICENSE",
        "THIRD_PARTY_NOTICES.md": "THIRD_PARTY_NOTICES.md",
        "licenses/Tabler-Icons-MIT.txt": "assets/tabler/LICENSE",
        "Qt-LGPL-RELINKING.md": "packaging/linux/Qt-LGPL-RELINKING.md",
    }
    for destination_name, source_name in release_assets.items():
        source = project / source_name
        if not source.is_file():
            raise FileNotFoundError(f"Missing release asset in source tree: {source}")
        destination = out / destination_name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    # Preserve the SBOMs from this exact Qt installation. They record
    # build-specific Qt modules, plug-ins and internal third-party components.
    # Only qtbase and qtsvg ship runtime binaries in this portable package.
    qt_sbom_dest = out / "licenses" / "Qt-SBOM"
    qt_sbom_dest.mkdir(parents=True, exist_ok=True)
    for module in ("qtbase", "qtsvg"):
        sbom_file = qt / "sbom" / f"{module}-6.8.3.spdx.json"
        if not sbom_file.is_file():
            raise FileNotFoundError(
                f"Qt 6.8.3 SBOM missing: {sbom_file}; "
                "do not publish an unverifiable Qt binary distribution"
            )
        sbom = json.loads(sbom_file.read_text(encoding="utf-8"))
        if sbom.get("spdxVersion") != "SPDX-2.3" or not sbom.get("packages"):
            raise RuntimeError(f"Invalid Qt SBOM: {sbom_file}")
        shutil.copy2(sbom_file, qt_sbom_dest / sbom_file.name)

    plugins = []
    for category, names in QT_PLUGINS.items():
        for name in names:
            source = qt / "plugins" / category / name
            if not source.is_file():
                raise FileNotFoundError(f"Required Qt plugin missing: {source}")
            dest = out / "plugins" / category / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, dest)
            plugins.append(source)

    env = os.environ.copy()
    # Include the official Qt build, plus any extra runtime library roots
    # installed for this Colab session (if needed).
    inherited = env.get("LD_LIBRARY_PATH", "")
    env["LD_LIBRARY_PATH"] = str(qt / "lib") + (":" + inherited if inherited else "")

    origins, excluded = {}, {}
    checked = set()
    pending = deque([exe, *plugins])
    while pending:
        binary = pending.popleft()
        if str(binary.resolve()) in checked:
            continue
        checked.add(str(binary.resolve()))
        for name, source in linked_libraries(binary, env):
            if name.startswith("ld-linux") or name in HOST_LIBS:
                excluded[name] = str(source)
                continue
            if name.startswith("libQt6") and not source.resolve().is_relative_to(qt):
                raise RuntimeError(f"Non-Qt-6.8.3 library selected: {name} -> {source}")
            previous = origins.get(name)
            if previous is not None and previous.resolve() != source.resolve():
                raise RuntimeError(f"Conflicting libraries for {name}: {previous} / {source}")
            if previous is None:
                origins[name] = source
                shutil.copy2(source, out / "lib" / name)
                pending.append(source)

    # Unlike GPU driver libraries, libOpenGL is a small vendor-neutral
    # runtime ABI and not guaranteed on a minimal desktop. We intentionally
    # bundle it instead of asking ZIP/AppImage users to install libopengl0.
    if "libOpenGL.so.0" not in origins:
        raise RuntimeError(
            "Qt runtime did not resolve libOpenGL.so.0 during staging; "
            "inspect Qt library selection before publishing"
        )

    licenses = out / "licenses"
    for filename, origin in (
        ("LGPL-3.0.txt", "/usr/share/common-licenses/LGPL-3"),
        ("GPL-3.0.txt", "/usr/share/common-licenses/GPL-3"),
        ("LGPL-2.1.txt", "/usr/share/common-licenses/LGPL-2.1"),
    ):
        if not Path(origin).is_file():
            raise FileNotFoundError(f"Required GNU license text missing: {origin}")
        shutil.copy2(origin, licenses / filename)

    packages, unowned = {}, []
    for name, source in sorted(origins.items()):
        if source.resolve().is_relative_to(qt):
            continue
        owner = package_owner(source)
        if owner:
            copyright_file = Path("/usr/share/doc") / owner / "copyright"
            if not copyright_file.is_file():
                unowned.append(f"{name}: {source} (missing {copyright_file})")
                continue
            packages[owner] = str(copyright_file)
            continue
        # The Colab ICU 73 runtime may come from the unpacked conda-forge
        # archive; keep its own copyright/license files if that dependency
        # really appears in the final runtime.
        source_parts = source.resolve().parts
        if "icu73" in source_parts:
            icu_root = Path(*source_parts[:source_parts.index("icu73") + 1])
            icu_licenses = icu_root / "info" / "licenses"
            if icu_licenses.is_dir() and list(icu_licenses.rglob("*")):
                shutil.copytree(icu_licenses, licenses / "ICU-Conda",
                                dirs_exist_ok=True)
                continue
        unowned.append(f"{name}: {source} (unidentified license origin)")

    for owner, copyright_file in packages.items():
        dest = licenses / "Ubuntu" / owner / "copyright"
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(copyright_file, dest)

    manifest = {
        "qt_version": "6.8.3",
        "plugins": sorted(str(p.relative_to(qt)) for p in plugins),
        "bundled_libraries": {name: str(path) for name, path in sorted(origins.items())},
        "host_libraries": dict(sorted(excluded.items())),
        "ubuntu_copyright_files": packages,
        "unresolved_license_origins": unowned,
        "qt_spdx_files": [
            "licenses/Qt-SBOM/qtbase-6.8.3.spdx.json",
            "licenses/Qt-SBOM/qtsvg-6.8.3.spdx.json",
        ],
        "review_required_before_public_release": [
            "Provide Qt 6.8.3 qtbase/qtsvg corresponding sources alongside the release",
            "Check Qt-internal third-party attribution and license texts against SBOM",
            "Review source/relinking obligations of bundled LGPL system libraries",
            "Audit the Windows windeployqt output separately before release",
        ],
    }
    (licenses / "BUNDLED_LIBRARIES.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    if unowned:
        raise RuntimeError(
            "Missing license evidence for bundled libraries:\n" +
            "\n".join(unowned) +
            "\nNo ZIP/AppImage should be released until these are reviewed."
        )

    launcher = out / "run.sh"
    launcher.write_text(
        '#!/usr/bin/env bash\n'
        'set -e\n'
        'APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"\n'
        'export LD_LIBRARY_PATH="$APP_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
        'export QT_PLUGIN_PATH="$APP_DIR/plugins"\n'
        'export QT_QPA_PLATFORM_PLUGIN_PATH="$APP_DIR/plugins/platforms"\n'
        'export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"\n'
        'exec "$APP_DIR/Stackies" "$@"\n',
        encoding="utf-8",
    )
    launcher.chmod(0o755)

    # Run without relying on the system Qt libraries installed by aqt.
    test_env = os.environ.copy()
    test_env["LD_LIBRARY_PATH"] = str(out / "lib")
    test_env["QT_PLUGIN_PATH"] = str(out / "plugins")
    test_env["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(out / "plugins" / "platforms")
    test_env["QT_QPA_PLATFORM"] = "offscreen"
    subprocess.run(
        [str(out / "Stackies"), "--verify-translations"],
        env=test_env, check=True, timeout=60,
    )
    print(f"Portable directory ready: {out}")
    print(f"Qt plugins: {len(plugins)}; bundled libraries: {len(origins)}")
    print(f"Ubuntu copyright packages: {len(packages)}")
    print("Next: package this directory into ZIP and AppImage; finalize Qt source notices before release.")


if __name__ == "__main__":
    main()
