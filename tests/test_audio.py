import struct

import pytest

from phantomcrash_research import audio


def adpcm_wav(blocks: bytes, channels: int = 1, rate: int = 44100) -> bytes:
    fmt = struct.pack("<HHIIHHHH", audio.XBOX_ADPCM, channels, rate, 0, 36 * channels, 4, 2, 64)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
    body += b"data" + struct.pack("<I", len(blocks)) + blocks
    return b"RIFF" + struct.pack("<I", len(body)) + body


def test_adpcm_silent_block_repeats_header_sample():
    block = struct.pack("<hBB", 1000, 0, 0) + b"\0" * 32
    pcm = audio.decode_xbox_adpcm(block, 1)
    samples = struct.unpack("<64h", pcm)
    assert samples[0] == 1000
    # Code 0 adds step >> 3 (7 >> 3 = 0) at the smallest step, so the level holds.
    assert set(samples) == {1000}


def test_adpcm_step_arithmetic():
    # First code 7 at index 0: diff = 0 + 7 + 3 + 1 = 11, index -> 8.
    block = struct.pack("<hBB", 0, 0, 0) + struct.pack("<I", 0x7) + b"\0" * 28
    samples = struct.unpack("<64h", audio.decode_xbox_adpcm(block, 1))
    assert samples[1] == 11
    # Next code 0 at index 8 (step 16): +16 >> 3 = +2.
    assert samples[2] == 13


def test_adpcm_stereo_interleaves_channels():
    block = struct.pack("<hBBhBB", 100, 0, 0, -100, 0, 0) + b"\0" * 64
    samples = struct.unpack("<128h", audio.decode_xbox_adpcm(block, 2))
    assert samples[0:4] == (100, -100, 100, -100)


def test_bank_directory_and_wav_conversion(tmp_path):
    block = struct.pack("<hBB", 5, 0, 0) + b"\0" * 32
    wav = adpcm_wav(block * 2)
    header = audio.BANK_MAGIC + struct.pack("<I", 1)
    directory = struct.pack("<4I", 0x10001, 0x30C, 0x24, len(header) + 16)
    bank = header + directory + wav
    (entry,) = audio.parse_bank(bank)
    assert (entry.flags, entry.wave.channels, entry.wave.sample_rate) == (0x10001, 1, 44100)
    assert entry.voices == 1
    target = tmp_path / "e.wav"
    audio.write_pcm16_wav(target, entry.wave)
    converted = audio.parse_wav(target.read_bytes())
    assert converted.format_tag == audio.PCM and len(converted.data) == 128 * 2


@pytest.mark.parametrize("channels", [1, 2])
def test_adpcm_block_boundary_discards_final_code(channels):
    # A nonzero final high nibble would add a spurious sample just before the
    # next header. Playback must advance directly from frame 63 to that header.
    first = b"".join(
        struct.pack("<hBB", 100 * (ch + 1), 0, 0) for ch in range(channels)
    )
    first += b"\0" * (28 * channels)
    first += b"\0\0\0\x70" * channels
    second = b"".join(
        struct.pack("<hBB", -100 * (ch + 1), 0, 0) for ch in range(channels)
    ) + b"\0" * (32 * channels)
    pcm = audio.decode_xbox_adpcm(first + second, channels)
    samples = struct.unpack(f"<{128 * channels}h", pcm)
    assert samples[63 * channels:64 * channels] == tuple(
        100 * (ch + 1) for ch in range(channels)
    )
    assert samples[64 * channels:65 * channels] == tuple(
        -100 * (ch + 1) for ch in range(channels)
    )


def test_rejects_non_bank():
    with pytest.raises(audio.AudioError):
        audio.parse_bank(b"RIFF" + b"\0" * 32)
