import pytest

from phantomcrash_research import xbe
from fixtures import build_xbe


def test_parses_header_certificate_and_tables():
    parsed = xbe.parse(build_xbe())
    assert parsed.base_address == 0x10000
    assert parsed.build_kind == "retail"
    assert parsed.entry_point == 0x11234
    assert parsed.kernel_thunk_address == 0x80010000
    assert parsed.title_code == "PL-001"
    assert parsed.title_name == "phantomcrash"
    assert parsed.xdk_build == 4721
    assert parsed.debug_path == r"K:\x.exe"
    assert parsed.section(".text").virtual_address == 0x11000
    assert parsed.timestamp_utc.startswith("2002-10-16")


def test_json_form_uses_hex_addresses():
    data = xbe.parse(build_xbe()).to_dict()
    assert data["entry_point"] == "0x00011234"
    assert data["sections"][0]["virtual_address"] == "0x11000"


def test_rejects_bad_magic():
    with pytest.raises(xbe.XbeError, match="magic"):
        xbe.parse(b"MZ" + build_xbe()[2:])


def test_rejects_unknown_entry_encoding():
    data = bytearray(build_xbe())
    data[0x128:0x12C] = b"\x00\x00\x00\x00"
    with pytest.raises(xbe.XbeError, match="retail nor debug"):
        xbe.parse(bytes(data))
