"""Extract a user-supplied disc image into the ignored artifacts tree."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import BinaryIO, Callable

from . import xbe
from .xdvdfs import XdvdfsImage

_HASH_CHUNK = 8 * 1024 * 1024


class _HashingWriter:
    def __init__(self, target: BinaryIO):
        self.target = target
        self.digest = hashlib.sha256()

    def write(self, data: bytes) -> None:
        self.digest.update(data)
        self.target.write(data)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        while chunk := source.read(_HASH_CHUNK):
            digest.update(chunk)
    return digest.hexdigest()


def _safe_destination(root: Path, disc_path: str) -> Path:
    destination = (root / disc_path.lstrip("/")).resolve()
    if root.resolve() not in destination.parents:
        raise ValueError(f"Disc path escapes the output directory: {disc_path}")
    return destination


def _write_json(path: Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def extract(
    disc: Path, output: Path, progress: Callable[[str], None] | None = None
) -> dict:
    """Extract every file plus ``metadata/disc.json`` and ``metadata/xbe.json``.

    Refuses a non-empty output directory so runs never mix.
    """
    if output.exists() and any(output.iterdir()):
        raise FileExistsError(f"Output directory is not empty: {output}")
    report = progress or (lambda _message: None)
    files_root = output / "files"
    records = []
    with XdvdfsImage(disc) as image:
        for entry in image.files():
            destination = _safe_destination(files_root, entry.path)
            destination.parent.mkdir(parents=True, exist_ok=True)
            with open(destination, "wb") as target:
                writer = _HashingWriter(target)
                image.copy_to(entry, writer)
            records.append(
                {
                    "path": entry.path,
                    "sector": entry.sector,
                    "size": entry.size,
                    "sha256": writer.digest.hexdigest(),
                }
            )
            report(f"{entry.size:>12,}  {entry.path}")
        partition_offset = image.partition_offset

    report("Hashing disc image...")
    disc_record = {
        "image": str(disc),
        "image_size": disc.stat().st_size,
        "image_sha256": sha256_file(disc),
        "partition_offset": partition_offset,
        "file_count": len(records),
        "total_bytes": sum(record["size"] for record in records),
        "files": records,
    }
    _write_json(output / "metadata" / "disc.json", disc_record)

    executable = files_root / "default.xbe"
    header = xbe.parse(executable.read_bytes()).to_dict()
    header["sha256"] = next(r["sha256"] for r in records if r["path"].lower() == "/default.xbe")
    _write_json(output / "metadata" / "xbe.json", header)
    return {"disc": disc_record, "xbe": header}
