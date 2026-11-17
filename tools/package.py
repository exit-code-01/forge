#!/usr/bin/env python3
"""Package a built Forge tree into dist/forge-vault-<sha>-win64.zip (stdlib only).

Usage:  python tools/package.py [--build-dir build/release] [--out dist]

Zip layout (the game must be run with its own folder as the working directory):
  forge_sandbox.exe (+ any DLLs beside it)
  assets/                 shipped game assets (no dev junk)
  LICENSES/               third-party notices + docs/ASSET_SOURCES.md
  README-PLAY.txt
Never included: settings.ini, *.bak, logs, tools/, the dev harness.
"""
import argparse
import fnmatch
import re
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = "forge_sandbox.exe"
SKIP = ["settings.ini", "*.bak", "*.log", "*.tmp", "*.pdb", "*.ilk", "*.blend1",
        "*.py", "*.psd", "*.xcf", "Thumbs.db", ".DS_Store", "_*"]

# name, licence, url, glob(s) of licence files under build/<x>/_deps/<dep>-src
LIBS = [
    ("GLFW", "zlib/libpng", "https://github.com/glfw/glfw", "glfw-src", ["LICENSE.md"]),
    ("GLM", "MIT (Happy Bunny)", "https://github.com/g-truc/glm", "glm-src", ["copying.txt", "LICENSE*"]),
    ("volk", "MIT", "https://github.com/zeux/volk", "volk-src", ["LICENSE.md"]),
    ("Vulkan-Headers", "Apache-2.0 / MIT", "https://github.com/KhronosGroup/Vulkan-Headers", "vulkan_headers-src", ["LICENSE.md", "LICENSE.txt"]),
    ("Jolt Physics", "MIT", "https://github.com/jrouwe/JoltPhysics", "joltphysics-src", ["LICENSE"]),
    ("stb", "MIT / public domain", "https://github.com/nothings/stb", "stb-src", ["LICENSE"]),
    ("cgltf", "MIT", "https://github.com/jkuhlmann/cgltf", "cgltf-src", ["LICENSE"]),
    ("Assimp", "BSD-3-Clause", "https://github.com/assimp/assimp", "assimp-src", ["LICENSE"]),
    ("Lua", "MIT", "https://github.com/lua/lua", "lua-src", ["lua.h"]),
    ("sol2", "MIT", "https://github.com/ThePhD/sol2", "sol2-src", ["LICENSE.txt"]),
    ("Dear ImGui", "MIT", "https://github.com/ocornut/imgui", "imgui-src", ["LICENSE.txt"]),
    ("ImGuizmo", "MIT", "https://github.com/CedricGuillemet/ImGuizmo", "imguizmo-src", ["LICENSE"]),
    ("miniaudio", "MIT-0 / public domain", "https://github.com/mackron/miniaudio", "miniaudio-src", ["LICENSE"]),
]


def skipped(name: str) -> bool:
    return any(fnmatch.fnmatch(name, p) for p in SKIP)


def git_sha() -> str:
    try:
        return subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, text=True).strip()
    except Exception:
        return "nogit"


def lua_notice(path: Path) -> str:
    # lua.h ends with the MIT notice inside a comment block.
    text = path.read_text(errors="replace")
    m = re.search(r"/\*+\s*\n(Copyright.*?)\*+/", text[-4000:], re.S) or re.search(r"(Copyright.*?)\*/", text, re.S)
    return m.group(1).strip() if m else "See https://www.lua.org/license.html"


def licence_text(build: Path) -> str:
    out = ["THIRD-PARTY SOFTWARE NOTICES\n", "Forge links the following libraries.\n", "=" * 72 + "\n"]
    for name, lic, url, depdir, files in LIBS:
        out.append(f"\n{name}\nLicence: {lic}\nSource:  {url}\n")
        src = build / "_deps" / depdir
        text = None
        for pat in files:
            for f in sorted(src.glob(pat)) if src.is_dir() else []:
                text = lua_notice(f) if f.name == "lua.h" else f.read_text(errors="replace").strip()
                break
            if text:
                break
        out.append("\n" + (text if text else "(licence text not found in the build tree; see the URL above)") + "\n")
        out.append("\n" + "-" * 72 + "\n")
    return "".join(out)


def controls() -> str:
    src = (ROOT / "sandbox/src/main.cpp").read_text(errors="replace")
    m = re.search(r'"(WASD move[^"]*)"((?:\s*"[^"]*")*)', src)
    if not m:
        return "WASD move, mouse look. ESC pause."
    return re.sub(r"\s+", " ", (m.group(1) + "".join(re.findall(r'"([^"]*)"', m.group(2)))).strip())


def play_readme(sha: str) -> str:
    return f"""FORGE - VAULT (build {sha}, Windows 64-bit)

Run forge_sandbox.exe from inside this folder (it loads ./assets and writes
settings.ini next to itself). Needs a Vulkan 1.3 capable GPU and an up-to-date
graphics driver. No other install is required.

Controls
  {controls()}

Third-party licences and asset credits are in the LICENSES folder.
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", default="build/release")
    ap.add_argument("--out", default="dist")
    a = ap.parse_args()
    build = (ROOT / a.build_dir).resolve()
    exe = build / "bin" / EXE
    if not exe.is_file():
        print(f"error: {exe} not found; build the release preset first", file=sys.stderr)
        return 1
    sha = git_sha()
    out_dir = ROOT / a.out
    out_dir.mkdir(exist_ok=True)
    zpath = out_dir / f"forge-vault-{sha}-win64.zip"
    zpath.unlink(missing_ok=True)
    n = 0
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in sorted(exe.parent.iterdir()):
            if f.is_file() and f.suffix.lower() in (".exe", ".dll"):
                z.write(f, f.name)
                n += 1
        for f in sorted((ROOT / "assets").rglob("*")):
            if f.is_file() and not any(skipped(p) for p in f.relative_to(ROOT / "assets").parts):
                z.write(f, f.relative_to(ROOT).as_posix())
                n += 1
        z.writestr("LICENSES/THIRD_PARTY.txt", licence_text(build))
        asrc = ROOT / "docs/ASSET_SOURCES.md"
        if asrc.is_file():
            z.write(asrc, "LICENSES/ASSET_SOURCES.md")
        z.writestr("README-PLAY.txt", play_readme(sha))
    print(f"wrote {zpath} ({zpath.stat().st_size / 1e6:.1f} MB, {n} files + notices)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
