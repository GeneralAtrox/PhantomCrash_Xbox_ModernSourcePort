"""Extract Sony HD/BD waveform banks and SQ MIDI sequences from SNDSEQ.BIN.

The waveform metadata layout is independently documented by vgmstream's
src/meta/hd_bd.c. Original instrument/envelope records are retained alongside
the waves; a waveform is not represented as a complete rendered sound cue.
"""

from __future__ import annotations

from array import array
import json
from pathlib import Path
import struct
import sys
import wave

from .sound_bank import read_instruments
from .sony_midi import convert_events

PREDICTORS = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))


def decode_adpcm(data: bytes) -> tuple[bytes, dict]:
    if len(data) % 16:
        raise ValueError("ADPCM wave is not block aligned")
    samples = array("h")
    previous, older = 0, 0
    loop = {}
    for offset in range(0, len(data), 16):
        block = data[offset:offset + 16]
        predictor, shift = block[0] >> 4, block[0] & 15
        flags = block[1]
        if predictor >= len(PREDICTORS) or shift > 12 or flags > 7:
            raise ValueError(f"Invalid ADPCM block at {offset:#x}")
        if flags == 6 and "start_sample" not in loop:
            loop["start_sample"] = len(samples)
        a, b = PREDICTORS[predictor]
        for i in range(28):
            nibble = (block[2 + i // 2] >> (4 * (i % 2))) & 15
            signed = nibble - 16 if nibble >= 8 else nibble
            value = ((signed * 4096) >> shift) + ((previous * a + older * b + 32) >> 6)
            if flags == 7:
                value = 0
            value = max(-32768, min(32767, value))
            older, previous = previous, value
            samples.append(value)
        if flags in (1, 3) and "end_sample" not in loop:
            loop["end_sample"] = len(samples)
            loop["repeat"] = bool(flags & 2)
    if sys.byteorder != "little":
        samples.byteswap()
    return samples.tobytes(), loop


def _table(data: bytes, offset: int, tag: bytes) -> list[int]:
    if data[offset:offset + 8] != tag:
        raise ValueError(f"Expected {tag!r} at {offset:#x}")
    last = struct.unpack_from("<I", data, offset + 12)[0]
    return list(struct.unpack_from(f"<{last + 1}I", data, offset + 16))


def midi_events(data: bytes) -> bytes:
    """Validate MIDI event boundaries and trim Sony's alignment padding."""
    cursor, running = 0, None

    def vlq():
        nonlocal cursor
        value = 0
        for _ in range(4):
            if cursor >= len(data):
                raise ValueError("Truncated MIDI variable length value")
            byte = data[cursor]
            cursor += 1
            value = (value << 7) | (byte & 127)
            if byte < 128:
                return value
        raise ValueError("MIDI variable length value exceeds four bytes")

    while cursor < len(data):
        vlq()  # delta ticks
        if cursor >= len(data):
            break
        status = data[cursor]
        if status >= 128:
            cursor += 1
        elif running is not None:
            status = running
        else:
            raise ValueError("MIDI running status has no prior channel event")
        if status == 255:
            if cursor >= len(data):
                raise ValueError("Truncated MIDI meta event")
            kind = data[cursor]
            cursor += 1
            length = vlq()
            cursor += length
            if kind == 47:
                if length or cursor > len(data):
                    raise ValueError("Invalid MIDI end-of-track")
                return data[:cursor]
        elif status in (240, 247):
            length = vlq()
            cursor += length
            running = None
        elif 128 <= status < 240:
            running = status
            length = 1 if status >> 4 in (12, 13) else 2
            if any(b >= 128 for b in data[cursor:cursor + length]):
                raise ValueError("Invalid MIDI channel data")
            cursor += length
        else:
            raise ValueError(f"Unsupported MIDI status {status:#x}")
        if cursor > len(data):
            raise ValueError("MIDI event exceeds sequence")
    raise ValueError("MIDI sequence is missing end-of-track")


def export_sound(disc, output: Path) -> dict:
    start, size = disc.disc_files()["SNDSEQ.BIN"]
    data = disc._read(start, size)
    if data[:4] != b"Wpbb":
        raise ValueError("Unsupported sound container")
    count = struct.unpack_from("<I", data, 4)[0]
    if count % 3:
        raise ValueError("Sound banks are not SQ/HD/BD triples")
    entries = [struct.unpack_from("<II", data, 8 + i * 8) for i in range(count)]
    banks, wave_count, sequence_count = [], 0, 0
    output.mkdir(parents=True, exist_ok=True)
    for bank in range(count // 3):
        chunks = []
        for offset, length in entries[bank * 3:bank * 3 + 3]:
            if offset + length > len(data):
                raise ValueError("Sound bank exceeds container")
            chunks.append(data[offset:offset + length])
        sq, hd, bd = chunks
        if hd[:8] != b"IECSsreV" or sq[:8] != b"IECSsreV":
            raise ValueError("Missing Sony sound header")
        hd_size, bd_size, prog, sets, smpl, vagi = struct.unpack_from("<6I", hd, 28)
        if hd_size != len(hd) or bd_size != len(bd):
            raise ValueError("Sound bank sizes disagree")
        offsets = _table(hd, vagi, b"IECSigaV")
        headers = [struct.unpack_from("<IHBB", hd, vagi + at) for at in offsets]
        waves = []
        for i, (at, rate, flags, marker) in enumerate(headers):
            if at == len(bd):
                continue
            end = next((h[0] for h in headers[i + 1:] if h[0] > at), len(bd))
            if not 8000 <= rate <= 192000 or end > len(bd) or at > end:
                raise ValueError("Invalid waveform metadata")
            pcm, loop = decode_adpcm(bd[at:end])
            filename = f"bank_{bank:02d}_wave_{i:03d}.wav"
            with wave.open(str(output / filename), "wb") as writer:
                writer.setparams((1, 2, rate, 0, "NONE", "not compressed"))
                writer.writeframes(pcm)
            waves.append({"index": i, "file": filename, "sample_rate": rate,
                          "samples": len(pcm) // 2, "flags": flags, "loop": loop,
                          "source_offset": at, "source_size": end - at})
            wave_count += 1
        midi = struct.unpack_from("<I", sq, 36)[0]
        midi_offsets = _table(sq, midi, b"IECSidiM")
        midi_size = struct.unpack_from("<I", sq, midi + 8)[0]
        sequences = []
        for i, at in enumerate(midi_offsets):
            end = midi_offsets[i + 1] if i + 1 < len(midi_offsets) else midi_size
            raw = sq[midi + at:midi + end]
            if len(raw) < 6:
                raise ValueError("Truncated MIDI sequence")
            event_offset, ticks = struct.unpack_from("<IH", raw)
            if event_offset not in (6, 8) or event_offset > len(raw):
                raise ValueError("Unsupported Sony MIDI data header")
            events = midi_events(convert_events(raw[event_offset:]))
            filename = f"bank_{bank:02d}_sequence_{i:03d}.mid"
            header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, ticks)
            (output / filename).write_bytes(header + b"MTrk" + struct.pack(">I", len(events)) + events)
            sequences.append({"index": i, "file": filename, "source_event_offset": event_offset,
                              "ticks_per_quarter": ticks})
            sequence_count += 1
        banks.append({"index": bank, "waves": waves, "sequences": sequences,
                      "instruments": read_instruments(hd, smpl, sets, prog)})
    manifest = {"source": "SNDSEQ.BIN", "banks": banks,
                "waveforms": wave_count, "sequences": sequence_count,
                "native_cue_bindings": "pending"}
    (output / "banks.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return {"banks": len(banks), "waves": wave_count, "sequences": sequence_count}
