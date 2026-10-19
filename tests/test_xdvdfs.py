import io
import struct

import pytest

from phantomcrash_research import xdvdfs
from fixtures import SECTOR, build_xiso


@pytest.mark.parametrize("offset", [0, 0x2080000])
def test_lists_nested_files(tmp_path, offset):
    image_path = tmp_path / "game.iso"
    image_path.write_bytes(build_xiso(offset))
    with xdvdfs.XdvdfsImage(image_path) as image:
        assert image.partition_offset == offset
        files = {entry.path: entry for entry in image.files()}
        assert set(files) == {"/default.xbe", "/Media/a.bin"}
        assert image.read(files["/Media/a.bin"]) == b"hello world"
        assert image.find("media\\A.BIN") == files["/Media/a.bin"]


def test_copy_streams_file(tmp_path):
    image_path = tmp_path / "game.iso"
    image_path.write_bytes(build_xiso())
    with xdvdfs.XdvdfsImage(image_path) as image:
        target = io.BytesIO()
        image.copy_to(image.find("default.xbe"), target)
    assert target.getvalue() == b"XBE-DATA"


def test_rejects_non_xdvdfs(tmp_path):
    image_path = tmp_path / "not.iso"
    image_path.write_bytes(b"\0" * (40 * SECTOR))
    with pytest.raises(xdvdfs.XdvdfsError):
        xdvdfs.XdvdfsImage(image_path)


def test_rejects_directory_loop(tmp_path):
    image = bytearray(build_xiso())
    # The root's right child is the Media node; make that node its own right child.
    root_table = 33 * SECTOR
    media_node = struct.unpack_from("<H", image, root_table + 2)[0]
    struct.pack_into("<H", image, root_table + media_node * 4 + 2, media_node)
    image_path = tmp_path / "loop.iso"
    image_path.write_bytes(bytes(image))
    with xdvdfs.XdvdfsImage(image_path) as parsed:
        with pytest.raises(xdvdfs.XdvdfsError, match="loop"):
            parsed.entries()


def test_rejects_extent_outside_image(tmp_path):
    image = bytearray(build_xiso())
    struct.pack_into("<I", image, 32 * SECTOR + len(xdvdfs.MAGIC), 10_000)
    image_path = tmp_path / "bad.iso"
    image_path.write_bytes(bytes(image))
    with xdvdfs.XdvdfsImage(image_path) as parsed:
        with pytest.raises(xdvdfs.XdvdfsError, match="outside"):
            parsed.entries()
