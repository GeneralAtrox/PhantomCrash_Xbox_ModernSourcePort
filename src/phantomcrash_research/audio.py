"""Game audio formats: RIFF WAV, Xbox ADPCM and the "SE BIN" effect banks.

Xbox ADPCM (format tag 0x69) is IMA ADPCM with fixed 36-byte blocks per
channel: a 4-byte header (int16 first sample, uint8 step index, reserved) and
32 bytes of 4-bit codes, low nibble first, interleaved per channel in 4-byte
groups. Playback uses 64 samples per channel: the header sample and the
first 63 decoded codes. The final code is padding, not a 65th output sample.

An effect bank (``Media\\sound\\*.bin``) starts with ``"SE BIN  "``, ``u32
count`` and ``count`` 16-byte directory records ``{u32 flags, u32 slot,
u32 parameters, u32 offset}``; each offset points at a complete RIFF WAV.
The flags' byte +2 is the number of reusable voices (0x0de8d0 cycles that
many slots); byte +0 marks a 3D buffer. ``slot`` and ``parameters`` are
buffer/3D-state offsets fixed up by the loader. Raw values are retained.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct

XBOX_ADPCM = 0x69
PCM = 0x01
BANK_MAGIC = b"SE BIN  "

_STEPS = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
)  # fmt: skip
_INDEX_ADJUST = (-1, -1, -1, -1, 2, 4, 6, 8)


class AudioError(ValueError):
    """The data is not audio this module understands."""


@dataclass(frozen=True)
class Wave:
    format_tag: int
    channels: int
    sample_rate: int
    block_align: int
    bits: int
    data: bytes


def parse_wav(data: bytes) -> Wave:
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise AudioError("Not a RIFF WAVE file")
    offset = 12
    fmt = None
    end = min(len(data), 8 + struct.unpack_from("<I", data, 4)[0])
    while offset + 8 <= end:
        kind, size = struct.unpack_from("<4sI", data, offset)
        body = data[offset + 8 : offset + 8 + size]
        if kind == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", body)
        elif kind == b"data":
            if fmt is None:
                raise AudioError("data chunk before fmt chunk")
            tag, channels, rate, _average, align, bits = fmt
            return Wave(tag, channels, rate, align, bits, body)
        offset += 8 + size + (size & 1)
    raise AudioError("No data chunk")


def _decode_nibble(code: int, predictor: int, index: int) -> tuple[int, int]:
    step = _STEPS[index]
    difference = step >> 3
    if code & 4:
        difference += step
    if code & 2:
        difference += step >> 1
    if code & 1:
        difference += step >> 2
    predictor += -difference if code & 8 else difference
    predictor = max(-32768, min(32767, predictor))
    index = max(0, min(88, index + _INDEX_ADJUST[code & 7]))
    return predictor, index


def decode_xbox_adpcm(data: bytes, channels: int) -> bytes:
    """Decode Xbox ADPCM to interleaved signed 16-bit PCM."""
    block_size = 36 * channels
    out = []
    for block in range(0, len(data) - block_size + 1, block_size):
        predictors = []
        indices = []
        for channel in range(channels):
            sample, index = struct.unpack_from("<hB", data, block + channel * 4)
            predictors.append(sample)
            indices.append(min(index, 88))
        samples = [[predictors[channel]] for channel in range(channels)]
        position = block + channels * 4
        for _group in range(8):
            for channel in range(channels):
                word = struct.unpack_from("<I", data, position)[0]
                position += 4
                for shift in range(0, 32, 4):
                    predictors[channel], indices[channel] = _decode_nibble(
                        (word >> shift) & 0xF, predictors[channel], indices[channel]
                    )
                    samples[channel].append(predictors[channel])
        # Xbox consumes 64 frames, including the header predictor. Generic IMA
        # decoding also produces a final frame, which must not enter playback.
        for frame in range(64):
            out.extend(samples[channel][frame] for channel in range(channels))
    return struct.pack(f"<{len(out)}h", *out)


def to_pcm16(wave: Wave) -> bytes:
    if wave.format_tag == XBOX_ADPCM:
        return decode_xbox_adpcm(wave.data, wave.channels)
    if wave.format_tag == PCM and wave.bits == 16:
        return wave.data
    raise AudioError(f"Unsupported WAV format tag 0x{wave.format_tag:x}/{wave.bits} bits")


def encode_pcm16_wav(pcm: bytes, channels: int, sample_rate: int) -> bytes:
    align = channels * 2
    header = struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF",
        36 + len(pcm),
        b"WAVE",
        b"fmt ",
        16,
        PCM,
        channels,
        sample_rate,
        sample_rate * align,
        align,
        16,
        b"data",
        len(pcm),
    )
    return header + pcm


@dataclass(frozen=True)
class BankEntry:
    index: int
    flags: int
    slot: int
    parameters: int
    offset: int
    wave: Wave

    @property
    def voices(self) -> int:
        return (self.flags >> 16) & 0xff


def parse_bank(data: bytes) -> list[BankEntry]:
    if data[:8] != BANK_MAGIC:
        raise AudioError("Not an SE BIN effect bank")
    count = struct.unpack_from("<I", data, 8)[0]
    if 12 + count * 16 > len(data):
        raise AudioError(f"Directory of {count} entries lies outside the file")
    entries = []
    for index in range(count):
        flags, slot, parameters, offset = struct.unpack_from("<4I", data, 12 + index * 16)
        if offset + 12 > len(data):
            raise AudioError(f"Entry {index} lies outside the file")
        riff_size = struct.unpack_from("<I", data, offset + 4)[0]
        wave = parse_wav(data[offset : offset + 8 + riff_size])
        entries.append(BankEntry(index, flags, slot, parameters, offset, wave))
    return entries


def write_pcm16_wav(path: Path, wave: Wave) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encode_pcm16_wav(to_pcm16(wave), wave.channels, wave.sample_rate))
