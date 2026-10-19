"""The Microsoft Visual C++ runtime DLLs a release ships beside its programs.

Players then need no separate "VC++ Redistributable" install: Windows loads a
DLL from the program's folder before the system copy (app-local deployment,
which the Visual Studio license allows for the files in its Redist folder).
Only the runtime DLLs the shipped binaries actually import are bundled, read
from their PE import tables, so the list follows the code.
"""

from __future__ import annotations

import os
from pathlib import Path
import re
import struct
import subprocess

VSWHERE = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
    "Microsoft Visual Studio/Installer/vswhere.exe"
# Windows' own API sets, resolved by the loader rather than found as files.
API_SETS = ("api-ms-", "ext-ms-")
SYSTEM32 = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"

_IMPORT_DIRECTORY = 1
_DELAY_IMPORT_DIRECTORY = 13


def _read_string(data: bytes, offset: int) -> str:
    return data[offset:data.index(b"\0", offset)].decode("ascii")


def pe_imports(path: Path) -> list[str]:
    """The DLL names a 64-bit PE file imports, including delay-loaded ones."""
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError(f"{path} is not a PE file")
    sections, optional_size = struct.unpack_from("<H12xH", data, pe + 6)
    optional = pe + 24
    if struct.unpack_from("<H", data, optional)[0] != 0x20B:
        raise ValueError(f"{path} is not a 64-bit (PE32+) file")
    table = optional + optional_size
    spans = [struct.unpack_from("<8xIIII", data, table + 40 * i) for i in range(sections)]

    def offset(rva: int) -> int:
        for size, address, raw_size, raw in spans:
            if address <= rva < address + max(size, raw_size):
                return rva - address + raw
        raise ValueError(f"{path}: RVA {rva:#x} is outside every section")

    def directory(index: int) -> int:
        return struct.unpack_from("<I", data, optional + 112 + 8 * index)[0]

    names = []
    if rva := directory(_IMPORT_DIRECTORY):
        at = offset(rva)
        while any(struct.unpack_from("<5I", data, at)):
            names.append(_read_string(data, offset(struct.unpack_from("<I", data, at + 12)[0])))
            at += 20
    if rva := directory(_DELAY_IMPORT_DIRECTORY):
        at = offset(rva)
        while any(struct.unpack_from("<8I", data, at)):
            names.append(_read_string(data, offset(struct.unpack_from("<I", data, at + 4)[0])))
            at += 32
    return names


def _version(text: str) -> tuple[int, ...]:
    return tuple(int(part) for part in text.split("."))


def find_crt_folder() -> Path:
    """The newest x64 CRT folder of the installed Visual Studio's Redist.

    VCToolsRedistDir (set by a Developer Command Prompt) takes precedence.
    """
    roots = []
    if redist := os.environ.get("VCToolsRedistDir"):
        roots.append(Path(redist))
    else:
        installation = subprocess.check_output(
            [str(VSWHERE), "-latest", "-products", "*", "-requires",
             "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
            text=True, timeout=60).strip()
        if not installation:
            raise FileNotFoundError("No Visual Studio with the C++ x64 tools was found")
        roots = sorted((Path(installation) / "VC/Redist/MSVC").glob("14.*"),
                       key=lambda folder: _version(folder.name), reverse=True)
    for root in roots:
        crt = sorted((root / "x64").glob("Microsoft.VC*.CRT"))
        if crt:
            return crt[-1]
    raise FileNotFoundError("No x64 Microsoft.VC*.CRT folder in the Visual Studio Redist")


def crt_version(crt: Path) -> tuple[int, ...]:
    """The Redist version of a CRT folder (VC/Redist/MSVC/<version>/x64/...)."""
    return _version(crt.parents[1].name)


def compiler_toolset(build: Path) -> tuple[int, int]:
    """The MSVC toolset a CMake build used, as (14, minor): cl 19.38 is 14.38."""
    for cache in build.glob("CMakeFiles/*/CMakeCXXCompiler.cmake"):
        found = re.search(r'CMAKE_CXX_COMPILER_VERSION "19\.(\d+)', cache.read_text())
        if found:
            return 14, int(found.group(1))
    raise FileNotFoundError(f"No MSVC compiler version recorded under {build}")


def runtime_dlls(binaries: list[Path], crt: Path) -> list[Path]:
    """The CRT DLLs the binaries import, directly or through one another."""
    available = {path.name.lower(): path for path in crt.glob("*.dll")}
    needed: dict[str, Path] = {}
    pending = list(binaries)
    while pending:
        for name in pe_imports(pending.pop()):
            key = name.lower()
            if key in available and key not in needed:
                needed[key] = available[key]
                pending.append(available[key])
    return [needed[key] for key in sorted(needed)]


def unresolved_imports(binaries: list[Path], shipped: set[str], crt: Path) -> list[str]:
    """Imports a clean Windows would not find: neither shipped, an API set,
    nor a system DLL. The CRT's DLLs count as missing unless shipped, since
    the packaging machine's System32 copies come from its own runtime install."""
    redistributable = {path.name.lower() for path in crt.glob("*.dll")}
    shipped = {name.lower() for name in shipped}
    missing = set()
    for binary in binaries:
        for name in pe_imports(binary):
            key = name.lower()
            if key in shipped or key.startswith(API_SETS):
                continue
            if key in redistributable or not (SYSTEM32 / name).is_file():
                missing.add(f"{binary.name} -> {name}")
    return sorted(missing)
