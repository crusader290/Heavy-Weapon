#!/usr/bin/env python3
"""Packages a release zip for Windows x64 or Linux x64.

  python tools/release/package.py windows --version 1.0.0 --build build-release-windows \
      --vcredist "<VS>/VC/Redist/MSVC/<ver>/x64/Microsoft.VC143.CRT"
  python tools/release/package.py linux --version 1.0.0 --build build-release-linux \
      --deps build-release-windows/_deps --openmpt-src <libopenmpt-x.y.z+release>

The zip holds one folder, HeavyWeapon-<version>/, with the game, README.txt,
LICENSES/ (third-party licenses) and docs/MODDING.md. No game data.

Build the binaries first (see docs/RELEASING.md): Windows from the windows-x64 preset
in Release without HW_DATA_DIR; Linux on an old glibc (Debian 12) with
HW_BUNDLE_SDL=ON, HW_BUNDLE_OPENMPT=ON and -static-libstdc++ -static-libgcc.
"""
import argparse
import os
import re
import shutil
import stat
import sys
import tempfile
import zipfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def readme(version, platform):
    exe = "heavyweapon.exe" if platform == "windows" else "./heavyweapon"
    lines = [
        f"Heavy Weapon {version} ({'Windows x64' if platform == 'windows' else 'Linux x64'})",
        "=" * 60,
        "",
        "A native source port of PopCap's Heavy Weapon Deluxe.",
        "Source, issues and documentation: https://github.com/crusader290/Heavy-Weapon",
        "",
        "GAME DATA (not included)",
        "  You need your own copy of Heavy Weapon Deluxe (for example from Steam).",
        "  Copy its data, Fonts, Images, Music and Sounds folders next to this program,",
        "  or into a folder named \"Heavy Weapon Deluxe\" next to it, or point the game",
        "  at your install:",
        f"    {exe} --data \"/path/to/Heavy Weapon Deluxe\"",
        "",
        "RUNNING",
        f"  {exe}" + ("   (or double-click it)" if platform == "windows" else ""),
    ]
    if platform == "linux":
        lines += [
            "  Needs a 64-bit Linux with glibc 2.36 or newer (Debian 12, Ubuntu 23.04,",
            "  Fedora 37 or later), OpenGL, and X11 or Wayland. Sound uses PipeWire,",
            "  PulseAudio or ALSA. If the file is not executable: chmod +x heavyweapon",
        ]
    else:
        lines += [
            "  Windows 10 or 11, 64-bit. Everything it needs is in this folder (including",
            "  the Microsoft Visual C++ runtime DLLs).",
        ]
    lines += [
        "  settings.ini (music, best survival time, button names) is saved in the folder",
        "  the game is started from.",
        "",
        "CONTROLS",
        "  Drive: A / D or arrows        Aim: mouse        Fire: left click, Ctrl, J",
        "  Nuke: right click, X, N       Pause: P / Esc    Music: Tab",
        "  Fullscreen: F11. Enter, Esc or a click skips the intro and story movies.",
        "  Gamepads: left stick drives, right stick aims (push far to fire),",
        "  A / RB fire, Y / LB nuke, Start pause, Back music.",
        "  Button names in prompts: buttons=xbox|playstation|auto in settings.ini.",
        "",
        "MODS",
        "  Put mod folders in a \"mods\" folder next to your game data. Each needs a",
        "  mod.lua. Guide: docs/MODDING.md. Errors are written to mods.log.",
        "  --no-mods turns mods off.",
        "",
        "COMMAND LINE",
        "  --data <dir>   --scale <n>   --size <W>x<H>   --stretch",
        "  --buttons <xbox|playstation|auto>",
        "  --no-mods   --help",
        "",
        "LEGAL",
        "  Heavy Weapon Deluxe is (c) 2005 PopCap Games, Inc. / Electronic Arts. This is",
        "  an unofficial fan port, not affiliated with or endorsed by them. It contains",
        "  no game assets. Third-party licenses are in LICENSES/.",
        "",
    ]
    return "\r\n".join(lines) if platform == "windows" else "\n".join(lines)


def tinyxml2_license():
    head = open(os.path.join(REPO, "3rdparty", "tinyxml2", "tinyxml2.h"), encoding="utf-8").read(4000)
    m = re.search(r"/\*\s*(Original code by.*?distribution\.)\s*\*/", head, re.S)
    return (m.group(1) if m else head.split("*/")[0].lstrip("/* \n")).strip() + "\n"


def collect_licenses(dst, platform, deps, openmpt_src):
    os.makedirs(dst, exist_ok=True)

    def copy(src, name):
        if not os.path.exists(src):
            sys.exit(f"missing license file: {src}")
        shutil.copyfile(src, os.path.join(dst, name))

    def find(prefix):
        for d in os.listdir(deps):
            if d.startswith(prefix):
                return os.path.join(deps, d)
        sys.exit(f"no {prefix}* in {deps}")

    sdl, img, mix, mpt = find("SDL3-"), find("SDL3_image-"), find("SDL3_mixer-"), find("libopenmpt-")
    copy(os.path.join(sdl, "LICENSE.txt"), "SDL3.txt")
    copy(os.path.join(img, "LICENSE.txt"), "SDL3_image.txt")
    copy(os.path.join(mix, "LICENSE.txt"), "SDL3_mixer.txt")
    copy(os.path.join(mpt, "LICENSE.txt"), "libopenmpt.txt")
    copy(os.path.join(REPO, "3rdparty", "lua", "LICENSE"), "Lua.txt")
    # The Flash runtime that plays the intro and story movies, and what it bundles.
    fr = os.path.join(REPO, "3rdparty", "flashrt")
    copy(os.path.join(fr, "LICENSE"), "flashrt.txt")
    copy(os.path.join(fr, "THIRD-PARTY-LICENSES.txt"), "flashrt-notes.txt")
    copy(os.path.join(fr, "plutovg", "LICENSE"), "PlutoVG.txt")
    for header, name, marker in (("drmp3.h", "dr_mp3.txt", "This software is available as a choice"),
                                 ("stb_image.h", "stb_image.txt", "This software is available under 2 licenses")):
        src = open(os.path.join(fr, "third_party", header), encoding="utf-8", errors="replace").read()
        if marker not in src:
            sys.exit(f"no license text in {header}")
        with open(os.path.join(dst, name), "w", encoding="utf-8") as f:
            f.write(src[src.rfind(marker):].replace("*/", "").strip() + "\n")
    with open(os.path.join(dst, "tinyxml2.txt"), "w", encoding="utf-8") as f:
        f.write(tinyxml2_license())

    if platform == "windows":
        # Codec DLLs shipped next to the exe, and libopenmpt's DLL dependencies.
        for lib in (img, mix):
            opt = os.path.join(lib, "lib", "x64", "optional")
            for name in sorted(os.listdir(opt)):
                if name.lower().startswith("license"):
                    copy(os.path.join(opt, name), name.replace("LICENSE.", ""))
        for name in sorted(os.listdir(os.path.join(mpt, "Licenses"))):
            copy(os.path.join(mpt, "Licenses", name), "libopenmpt-" + name.replace("License.", ""))
        with open(os.path.join(dst, "Microsoft-VC-runtime.txt"), "w", encoding="utf-8") as f:
            f.write("vcruntime140.dll, vcruntime140_1.dll and msvcp140.dll are the Microsoft Visual C++\n"
                    "runtime, redistributed under the Microsoft Visual Studio license terms\n"
                    "(https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files).\n")
    else:
        # libopenmpt built from source with its bundled decoders.
        for name in ("License.mpt.BSD-3-Clause.txt", "License.mpt.BSL-1.0.txt"):
            copy(os.path.join(mpt, "Licenses", name), "libopenmpt-" + name.replace("License.", ""))
        copy(os.path.join(openmpt_src, "include", "minimp3", "LICENSE"), "minimp3.txt")
        copy(os.path.join(openmpt_src, "include", "miniz", "LICENSE"), "miniz.txt")
        src = open(os.path.join(openmpt_src, "include", "stb_vorbis", "stb_vorbis.c"), encoding="utf-8", errors="replace").read()
        tail = src[src.rfind("This software is available under 2 licenses"):]
        with open(os.path.join(dst, "stb_vorbis.txt"), "w", encoding="utf-8") as f:
            f.write("stb_vorbis (used by libopenmpt and SDL3_mixer)\n\n" + tail.replace("*/", "").strip() + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("platform", choices=["windows", "linux"])
    ap.add_argument("--version", required=True)
    ap.add_argument("--build", required=True, help="folder with the built binary (and DLLs on Windows)")
    ap.add_argument("--deps", help="a Windows build's _deps folder (license files); default <build>/_deps")
    ap.add_argument("--vcredist", help="Windows: Microsoft.VC143.CRT folder")
    ap.add_argument("--openmpt-src", help="Linux: libopenmpt release source folder (decoder licenses)")
    ap.add_argument("--out", default=os.path.join(REPO, "dist"))
    a = ap.parse_args()

    deps = a.deps or os.path.join(a.build, "_deps")
    top = f"HeavyWeapon-{a.version}"
    name = f"HeavyWeapon-{a.version}-{a.platform}-x64.zip"
    stage = tempfile.mkdtemp(prefix="hw-release-")
    root = os.path.join(stage, top)
    os.makedirs(root)

    if a.platform == "windows":
        if not a.vcredist:
            sys.exit("--vcredist is required for Windows")
        shutil.copy2(os.path.join(a.build, "heavyweapon.exe"), root)
        for f in sorted(os.listdir(a.build)):
            if f.lower().endswith(".dll"):
                shutil.copy2(os.path.join(a.build, f), root)
        for f in ("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll"):
            shutil.copy2(os.path.join(a.vcredist, f), root)
    else:
        if not a.openmpt_src:
            sys.exit("--openmpt-src is required for Linux")
        shutil.copy2(os.path.join(a.build, "heavyweapon"), root)

    with open(os.path.join(root, "README.txt"), "w", encoding="utf-8", newline="") as f:
        f.write(readme(a.version, a.platform))
    collect_licenses(os.path.join(root, "LICENSES"), a.platform, deps, a.openmpt_src)
    os.makedirs(os.path.join(root, "docs"))
    shutil.copy2(os.path.join(REPO, "docs", "MODDING.md"), os.path.join(root, "docs"))

    os.makedirs(a.out, exist_ok=True)
    zpath = os.path.join(a.out, name)
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for dirpath, _, files in os.walk(stage):
            for fn in sorted(files):
                full = os.path.join(dirpath, fn)
                arc = os.path.relpath(full, stage).replace(os.sep, "/")
                info = zipfile.ZipInfo.from_file(full, arc)
                info.compress_type = zipfile.ZIP_DEFLATED
                if a.platform == "linux":
                    info.create_system = 3   # Unix, so the mode bits below are honoured
                    mode = 0o755 if fn == "heavyweapon" else 0o644
                    info.external_attr = (stat.S_IFREG | mode) << 16
                with open(full, "rb") as fh:
                    z.writestr(info, fh.read())
    shutil.rmtree(stage)
    print(zpath, os.path.getsize(zpath), "bytes")


if __name__ == "__main__":
    main()
