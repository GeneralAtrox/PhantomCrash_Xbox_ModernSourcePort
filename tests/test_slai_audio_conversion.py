"""Exercise the built converter and music player with a synthetic ILD."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile

import pytest


def test_built_ild_converter_and_music_decoder():
    root = Path(__file__).resolve().parents[1]
    binaries = root / "build/port/port/Release"
    converter = binaries / "phantomcrash_slai_audio.exe"
    decoder_tests = binaries / "phantomcrash_music_tests.exe"
    if not converter.exists() or not decoder_tests.exists():
        pytest.skip("Build the Release port and music tests first")
    with tempfile.TemporaryDirectory(prefix="phantomcrash-slai-audio-test-") as folder:
        source = Path(folder) / "test.ild"
        output = Path(folder) / "test.ogg"
        header = bytearray(2048)
        header[:4] = b"ILD\0"
        struct.pack_into("<3I", header, 4, 2, 2048, 2000 * 32)
        struct.pack_into("<I", header, 32, 16)
        struct.pack_into("<I", header, 40, 44100)
        struct.pack_into("<I", header, 72, 44100)
        # Independent filter-zero fixture: positive left, negative right.
        source.write_bytes(header + (b"\x00\x00" + b"\x11" * 14 +
                                      b"\x00\x00" + b"\xff" * 14) * 2000)
        result = subprocess.run([str(converter), str(source), str(output)],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stderr[-600:]
        assert "56000 frames" in result.stdout
        assert output.read_bytes().startswith(b"OggS")
        result = subprocess.run([str(decoder_tests)], capture_output=True, text=True,
                                timeout=30, env=os.environ | {"PHANTOMCRASH_TEST_OGG": str(output)})
        assert result.returncode == 0, result.stdout[-1500:] + result.stderr[-600:]
        assert "5 tests, 0 failed" in result.stdout
        # A malformed stereo payload must fail and leave no partial output.
        source.write_bytes(header + b"\x00" * 3)
        output.unlink()
        result = subprocess.run([str(converter), str(source), str(output)],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode != 0
        assert not output.exists()
