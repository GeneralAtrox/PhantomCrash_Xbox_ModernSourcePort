"""Shipped resource descriptor identities and their archive package ownership."""

import re
import struct


def resource_packages(disc) -> dict[str, int]:
    start, size = disc.disc_files()["SLES_529.40"]
    elf = disc._read(start, size)
    folders = {m.start() + 0xFFF00: m.group().rstrip(b"\0").decode("ascii")
               for m in re.finditer(rb"Z/[A-Z0-9_/]{3,80}\x00", elf)}
    result = {}
    for at in range(256, len(elf) - 16, 4):
        pointer, metadata, sector, sectors = struct.unpack_from("<4I", elf, at)
        if pointer not in folders:
            continue
        package = disc.packages().get(sector * 2048)
        if package and metadata >> 16 == len(package) and sectors > 0:
            result[folders[pointer]] = sector * 2048
    return result
