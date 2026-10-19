"""Render static Sony sample sequences for the native WAV effect player.

Supports authored notes, regions, volumes, tuning, pan, loops and ADSR. Rejects
dynamic modulation rather than silently flattening it. Float resampling is a
native adapter; this is not SPU2 hardware interpolation or reverb emulation.
"""

import struct
import wave
import numpy as np

from .sound_envelope import RATE, envelope


def sequence_notes(data, *, sony_loops=False, loop_info=None):
    if data[:4] != b"MThd" or struct.unpack_from(">HH", data, 8) != (0, 1):
        raise ValueError("Expected a single-track MIDI sequence")
    division = struct.unpack_from(">H", data, 12)[0]
    if not 0 < division < 0x8000 or data[14:18] != b"MTrk":
        raise ValueError("Unsupported MIDI clock")
    end = 22 + struct.unpack_from(">I", data, 18)[0]
    if end != len(data):
        raise ValueError("MIDI track size mismatch")
    cursor, running, time, tempo = 22, None, 0., 500000
    states = [{"program": 0, "volume": 127, "expression": 127, "pan": 64} for _ in range(16)]
    active, notes = {}, []

    def vlq():
        nonlocal cursor
        value = 0
        for _ in range(4):
            if cursor >= end:
                raise ValueError("Truncated MIDI delta")
            byte = data[cursor]
            cursor += 1
            value = value * 128 + (byte & 127)
            if byte < 128:
                return value
        raise ValueError("Invalid MIDI delta")

    while cursor < end:
        time += vlq() * tempo / (division * 1e6)
        if cursor >= end or time > (120 if sony_loops else 30):
            raise ValueError("Sequence exceeds bounded render duration")
        status = data[cursor]
        if status >= 128:
            cursor += 1
        elif running is not None:
            status = running
        else:
            raise ValueError("Invalid MIDI running status")
        if status == 255:
            kind = data[cursor]
            cursor += 1
            size = vlq()
            payload = data[cursor:cursor+size]
            cursor += size
            if kind == 81 and size == 3:
                tempo = int.from_bytes(payload, "big")
            elif kind == 47:
                if active:
                    raise ValueError("Sequence ends with active notes")
                return notes
            continue
        if not 128 <= status < 240:
            raise ValueError("Unsupported MIDI system event")
        running = status
        kind, channel = status >> 4, status & 15
        size = 1 if kind in (12, 13) else 2
        values = data[cursor:cursor+size]
        cursor += size
        if len(values) != size or any(v >= 128 for v in values):
            raise ValueError("Invalid MIDI channel data")
        key = (channel, values[0])
        state = states[channel]
        if kind == 9 and values[1]:
            if key in active:
                raise ValueError("Overlapping repeated MIDI note")
            active[key] = {**state, "note": values[0], "velocity": values[1], "start": time}
        elif kind == 8 or (kind == 9 and not values[1]):
            if key not in active:
                raise ValueError("MIDI note-off has no note-on")
            note = active.pop(key)
            notes.append({**note, "end": time})
        elif kind == 12:
            state["program"] = values[0]
        elif kind == 11:
            if sony_loops and values[0] in (6, 38, 99):
                # Sony uses NRPN MSB 0/1 for loop boundaries, Data Entry
                # for the loop number/count. They do not modulate the voice.
                if values[0] == 99:
                    if values[1] not in (0, 1):
                        raise ValueError("Unknown Sony sequence loop control")
                    state["source_loop"] = values[1] == 0
                    if loop_info is not None:
                        boundary = "start" if values[1] == 0 else "end"
                        if boundary in loop_info:
                            raise ValueError("Multiple Sony loop regions require sequence playback")
                        loop_info[boundary] = time
                elif values[0] == 6 and values[1] != 0:
                    raise ValueError("Nested Sony loop index requires sequence playback")
                elif values[0] == 38 and values[1] != 0:
                    raise ValueError("Finite Sony repeat requires sequence playback")
                continue
            fields = {7: "volume", 10: "pan", 11: "expression"}
            if values[0] not in fields:
                raise ValueError(f"Dynamic MIDI controller {values[0]} requires source semantics")
            field = fields[values[0]]
            if any(k[0] == channel for k in active) and state[field] != values[1]:
                raise ValueError("Controller changes during an active voice")
            state[field] = values[1]
        elif kind == 14:
            if list(values) != [0, 64]:
                raise ValueError("Dynamic MIDI pitch bend requires source semantics")
        else:
            raise ValueError(f"Unsupported MIDI event {kind}")
    raise ValueError("MIDI end marker is missing")


def _pan(value):
    value &= 255
    return value-64 if value <= 127 else 64-(value-127)


def voice_parameters(note, program, split, sample):
    cents = sum(v.get("detune", 0) for v in (program, split, sample))
    pitch = note["note"] - sample["base_note"] + program["transpose"] + split["transpose"] + cents/100
    gain = note["velocity"] * note["volume"] * note["expression"] / 127**3
    gain *= program["volume"] * split["volume"] * sample["volume"] / 127**3
    pan = np.clip(note["pan"] + sum(_pan(v["pan"]) for v in (program, split, sample)), 0, 127)
    return {"pitch_ratio": 2**(pitch/12), "gain_left": float(min(1, (127-pan)/63)*gain),
            "gain_right": float(min(1, pan/64)*gain)}


def _voice(bank, root, note, program, split, sample):
    unsupported = ("key_attack", "key_decay", "key_release", "key_sustain_rate", "key_sustain_level",
                   "velocity_pitch", "velocity_amplitude")
    if any(sample[k] for k in unsupported) or any(split[k] for k in ("key_pitch", "key_amplitude", "key_pan")):
        raise ValueError("Key/velocity tracking requires source semantics")
    wave_info = next(v for v in bank["waves"] if v["index"] == sample["wave"])
    with wave.open(str(root / wave_info["file"]), "rb") as reader:
        if reader.getnchannels() != 1 or reader.getsampwidth() != 2:
            raise ValueError("Expected source PCM16 mono wave")
        pcm = np.frombuffer(reader.readframes(reader.getnframes()), "<i2").astype(np.float32) / 32768
        rate = reader.getframerate()
    held = round((note["end"]-note["start"]) * RATE)
    if held < 0:
        raise ValueError("Negative MIDI note duration")
    env = envelope(sample["adsr1"], sample["adsr2"], held, held + RATE * 8)
    parameters = voice_parameters(note, program, split, sample)
    positions = np.arange(len(env)) * rate / RATE * parameters["pitch_ratio"]
    loop = wave_info["loop"]
    last = loop.get("end_sample", len(pcm))
    if loop.get("repeat"):
        first = loop.get("start_sample", 0)
        if not 0 <= first < last <= len(pcm):
            raise ValueError("Invalid sample loop")
        positions = np.where(positions >= last, first + (positions-first) % (last-first), positions)
    else:
        keep = positions < last
        positions, env = positions[keep], env[keep]
    out = np.interp(positions, np.arange(len(pcm)), pcm) * env
    # Native WAV stereo gains. Preserve center volume on both speakers.
    return out[:, None] * np.array([parameters["gain_left"], parameters["gain_right"]])[None, :]


def sequence_regions(bank, notes):
    instruments = bank["instruments"]
    for note in notes:
        if note["program"] >= len(instruments["programs"]):
            raise ValueError("MIDI selects a missing program")
        program = instruments["programs"][note["program"]]
        if program is None or program["attributes"] or program["key_pan"]:
            raise ValueError("Program modulation requires source semantics")
        matched = False
        for split in program["splits"]:
            high = split["note_max"] if split["note_max"] >= split["note_min"] else 127
            if not split["note_min"] <= note["note"] <= high:
                continue
            sample_set = instruments["sample_sets"][split["sample_set"]]
            if sample_set["velocity_curve"]:
                raise ValueError("Velocity curve requires source semantics")
            if not sample_set["velocity_min"] <= note["velocity"] <= sample_set["velocity_max"]:
                continue
            for index in sample_set["samples"]:
                sample = instruments["samples"][index]
                if sample["velocity_min"] <= note["velocity"] <= sample["velocity_max"]:
                    yield note, program, split, sample
                    matched = True
        if not matched:
            raise ValueError("No authored sample region for MIDI note")


def render_sequence(bank, sequence, root):
    notes = sequence_notes((root / sequence["file"]).read_bytes(), sony_loops=True)
    voices = [(round(note["start"]*RATE), _voice(bank, root, note, program, split, sample))
              for note, program, split, sample in sequence_regions(bank, notes)]
    if not voices:
        raise ValueError("Sequence has no audible voices")
    length = max(at+len(v) for at, v in voices)
    out = np.zeros((length, 2), np.float32)
    for at, voice in voices:
        out[at:at+len(voice)] += voice
    return (np.clip(out, -1, 1)*32767).round().astype("<i2")
