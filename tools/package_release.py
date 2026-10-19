"""Package a built Windows release without including user game content."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

import vc_runtime


ROOT = Path(__file__).resolve().parents[1]
VCPKG = ROOT / ".tools/vcpkg"
INSTALLED = ROOT / "build/port/vcpkg_installed/x64-windows"
BIN = ROOT / "build/port/port/Release"
LIBRARIES = {
    "cgltf": "jkuhlmann-cgltf-v1.15.tar.gz",
    "ffmpeg": "ffmpeg-ffmpeg-n9.0.tar.gz",
    "sdl3": "libsdl-org-SDL-release-3.4.14.tar.gz",
    "sdl3-image": "libsdl-org-SDL_image-release-3.4.4.tar.gz",
    "libdeflate": "ebiggers-libdeflate-v1.25.tar.gz",
    "libpng": "pnggroup-libpng-v1.6.58.tar.gz",
    "zlib": "madler-zlib-v1.3.2.tar.gz",
}
DLLS = (
    "SDL3.dll", "SDL3_image.dll", "avcodec-63.dll", "avformat-63.dll",
    "avutil-61.dll", "swresample-7.dll", "swscale-10.dll", "libpng16.dll", "z.dll",
    "deflate.dll",
)
PROGRAMS = ("phantomcrash_port.exe", "phantomcrash_extract.exe",
            "phantomcrash_slai_install.exe", "phantomcrash_slai_audio.exe")
RUNTIME_NOTICE = """\
{names}
are Microsoft Visual C++ {version} runtime files from the Visual Studio
Redist folder, shipped beside the programs so that no separate runtime
install is needed. They are Microsoft's Distributable Code under the Visual
Studio license terms and are not covered by this project's license.
A system-wide copy is available from
https://aka.ms/vs/17/release/vc_redist.x64.exe
"""
# The extractor installs the game data from the player's disc image; the game
# runs it too when an update needs the assets converted again.
INSTALL_SCRIPT = "\r\n".join((
    "@echo off",
    "rem Installs the game data from your Phantom Crash (Europe) disc image.",
    "rem Drop the .iso onto this file, or run it and drag the .iso into the window.",
    "rem Without a disc image it brings existing game data up to date.",
    "setlocal",
    'set "disc=%~1"',
    'if not defined disc set /p "disc=Disc image (.iso), or Enter to update existing data: "',
    'if defined disc set "disc=%disc:"=%"',
    "if not defined disc (",
    '  "%~dp0phantomcrash_extract.exe" install --root "%~dp0."',
    ") else (",
    '  "%~dp0phantomcrash_extract.exe" install --root "%~dp0." --disc "%disc%"',
    ")",
    'set "install_exit=%ERRORLEVEL%"',
    "pause",
    "exit /b %install_exit%",
    "",
))


def git(*args: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(ROOT), *args], text=True, timeout=30
    ).strip()


def add(archive: zipfile.ZipFile, source: Path, name: str) -> None:
    if not source.is_file() or source.is_symlink():
        raise ValueError(f"Missing or linked package input: {source}")
    archive.write(source, name)


def visual_cpp_runtime(binaries: list[Path]) -> tuple[list[Path], str]:
    """The runtime DLLs to ship and their license notice; refuses a Redist
    older than the compiler that built the port, or any import a clean
    Windows 10 would not resolve."""
    crt = vc_runtime.find_crt_folder()
    redist = vc_runtime.crt_version(crt)
    toolset = vc_runtime.compiler_toolset(ROOT / "build/port")
    if redist[:2] < toolset:
        raise ValueError(f"Visual C++ Redist {redist} is older than the build's toolset {toolset}")
    dlls = vc_runtime.runtime_dlls(binaries, crt)
    shipped = {path.name for path in binaries + dlls}
    if missing := vc_runtime.unresolved_imports(binaries + dlls, shipped, crt):
        raise ValueError("Imports a clean Windows would not find: " + ", ".join(missing))
    notice = RUNTIME_NOTICE.format(names=", ".join(path.name for path in dlls),
                                   version=".".join(map(str, redist)))
    return dlls, notice


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/releases",
                        help="folder for the archives (default: build/releases)")
    output = parser.parse_args().output
    manifest = json.loads((ROOT / "vcpkg.json").read_text())
    version = manifest["version-string"]
    baseline = manifest["builtin-baseline"]
    if git("-C", str(VCPKG), "rev-parse", "HEAD") != baseline:
        raise ValueError("vcpkg checkout does not match the manifest baseline")
    stem = f"PhantomCrash-{version}-windows-x64"
    runtime, runtime_notice = visual_cpp_runtime([BIN / name for name in PROGRAMS + DLLS])
    output.mkdir(parents=True, exist_ok=True)
    binary = output / f"{stem}.zip"
    sources = output / f"PhantomCrash-{version}-third-party-sources.zip"
    if binary.exists() or sources.exists():
        raise FileExistsError("Release archives already exist; choose a new version/output")

    files = sorted(set(git("ls-files", "--cached", "--others", "--exclude-standard",
                           "data").splitlines()))
    metadata = {
        "version": version,
        "commit": git("rev-parse", "HEAD"),
        "working_tree_changes": bool(git("status", "--porcelain")),
        "vcpkg_baseline": baseline,
        "configuration": "Release",
        "architecture": "x64",
    }
    with zipfile.ZipFile(binary, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for program in PROGRAMS:
            add(z, BIN / program, f"{stem}/{program}")
        for dll in DLLS:
            add(z, BIN / dll, f"{stem}/{dll}")
        for dll in runtime:
            add(z, dll, f"{stem}/{dll.name}")
        z.writestr(f"{stem}/licenses/microsoft-visual-cpp-runtime.txt", runtime_notice)
        for file in files + ["LICENSE", "NOTICE.md", "docs/REPLAY.md", "docs/MATCH_RESULT.md",
                             "docs/ENTRY_SHIELD_RENDERING.md", "docs/CHEATS.md",
                             "docs/ORIGINAL_ROBOT_AI.md",
                             "docs/ONLINE_MULTIPLAYER_DETERMINISM.md", "docs/SLAI.md",
                             "docs/SLAI_INTEGRATION.md"]:
            add(z, ROOT / file, f"{stem}/{file}")
        add(z, ROOT / "docs/WINDOWS_RELEASE.md", f"{stem}/README.md")
        for library in LIBRARIES:
            add(z, INSTALLED / "share" / library / "copyright",
                f"{stem}/licenses/{library}.txt")
        importer_licenses = ROOT / "build/port/slai-installer/licenses"
        if not (importer_licenses / "python.txt").is_file():
            raise ValueError("Build the S.L.A.I. installer and its runtime notices first")
        for path in sorted(importer_licenses.rglob("*")):
            if path.is_file():
                add(z, path, f"{stem}/licenses/slai-installer/" +
                    path.relative_to(importer_licenses).as_posix())
        z.writestr(f"{stem}/build-info.json", json.dumps(metadata, indent=2) + "\n")
        z.writestr(f"{stem}/Install Game Data.bat", INSTALL_SCRIPT)
        z.writestr(f"{stem}/Start Game.bat", (
            '@echo off\r\npushd "%~dp0"\r\n'
            'phantomcrash_port.exe --root "%~dp0." %*\r\n'
            'set "game_exit=%ERRORLEVEL%"\r\n'
            'if not "%game_exit%"=="0" pause\r\npopd\r\nexit /b %game_exit%\r\n'
        ))

    with zipfile.ZipFile(sources, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for library, filename in LIBRARIES.items():
            add(z, VCPKG / "downloads" / filename, f"upstream/{filename}")
            for path in sorted((VCPKG / "ports" / library).rglob("*")):
                if path.is_file():
                    add(z, path, "vcpkg/" + path.relative_to(VCPKG).as_posix())
        # FFmpeg's helper is part of the pinned build recipe too.
        for path in sorted((VCPKG / "ports/ffmpeg-bin2c").rglob("*")):
            if path.is_file():
                add(z, path, "vcpkg/" + path.relative_to(VCPKG).as_posix())
        for file in ("config.h", "config_components.h", "ffbuild/config.mak"):
            add(z, VCPKG / "buildtrees/ffmpeg/x64-windows-rel" / file,
                f"ffmpeg-build/{file}")
        for file in ("vcpkg.json", "scripts/build-port.ps1", "docs/WINDOWS_RELEASE.md",
                     "tools/slai-installer-requirements.txt", "tools/build_slai_installer.py"):
            add(z, ROOT / file, file)
        z.writestr("README.txt", (
            f"Library sources for Phantom Crash {version} Windows x64.\n"
            "Upstream archives are unmodified. Apply the patches in each port's\n"
            "portfile.cmake in order, or use the port build script in the source tag.\n"
            f"vcpkg baseline: {baseline}\n"
            "https://github.com/microsoft/vcpkg/tree/" + baseline + "\n"
            "FFmpeg configuration is included under ffmpeg-build.\n"
            "FFmpeg is built with the MSVC dynamic CRT, without GPL/nonfree features.\n"
            "The Microsoft Visual C++ runtime DLLs in the binary ZIP are Microsoft's\n"
            "redistributable files; their source is not available.\n"
        ))

    checksums = []
    for path in (binary, sources):
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        checksums.append(f"{digest}  {path.name}\n")
        print(f"{path.name}: {path.stat().st_size:,} bytes")
    (output / "SHA256SUMS.txt").write_text("".join(checksums), encoding="utf-8")


if __name__ == "__main__":
    main()
