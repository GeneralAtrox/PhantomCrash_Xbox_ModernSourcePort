"""Sony HD instrument assets, checked against VGMTrans's SonyPS2 reader.

Source byte values are preserved. Envelope registers and LFO controls are
not replaced with guessed timing constants or rendered approximations.
"""

import struct

SAMPLE_FIELDS = [
    ("wave", "H"), ("velocity_min", "B"), ("velocity_crossfade", "B"),
    ("velocity_max", "B"), ("velocity_pitch", "b"), ("velocity_pitch_center", "B"),
    ("velocity_pitch_curve", "B"), ("velocity_amplitude", "b"),
    ("velocity_amplitude_center", "B"), ("velocity_amplitude_curve", "B"),
    ("base_note", "B"), ("detune", "b"), ("pan", "b"), ("group", "B"),
    ("priority", "B"), ("volume", "B"), ("reserved", "B"),
    ("adsr1", "H"), ("adsr2", "H"), ("key_attack", "b"), ("key_attack_center", "B"),
    ("key_decay", "b"), ("key_decay_center", "B"), ("key_sustain_rate", "b"),
    ("key_sustain_rate_center", "B"), ("key_release", "b"), ("key_release_center", "B"),
    ("key_sustain_level", "b"), ("key_sustain_level_center", "B"),
    ("pitch_lfo_delay", "H"), ("pitch_lfo_fade", "H"),
    ("amplitude_lfo_delay", "H"), ("amplitude_lfo_fade", "H"),
    ("lfo_attributes", "B"), ("spu_attributes", "B"),
]
PROGRAM_FIELDS = [
    ("split_offset", "I"), ("split_count", "B"), ("split_stride", "B"),
    ("volume", "B"), ("pan", "b"), ("transpose", "b"), ("detune", "b"),
    ("key_pan", "b"), ("key_pan_center", "B"), ("attributes", "B"), ("reserved", "B"),
    ("pitch_lfo_wave", "B"), ("amplitude_lfo_wave", "B"), ("pitch_lfo_phase", "B"),
    ("amplitude_lfo_phase", "B"), ("pitch_lfo_random_phase", "B"),
    ("amplitude_lfo_random_phase", "B"), ("pitch_lfo_period_ms", "H"),
    ("amplitude_lfo_period_ms", "H"), ("pitch_lfo_depth_positive", "h"),
    ("pitch_lfo_depth_negative", "h"), ("midi_pitch_depth_positive", "h"),
    ("midi_pitch_depth_negative", "h"), ("amplitude_lfo_depth_positive", "b"),
    ("amplitude_lfo_depth_negative", "b"), ("midi_amplitude_depth_positive", "b"),
    ("midi_amplitude_depth_negative", "b"),
]
SPLIT_FIELDS = [
    ("sample_set", "H"), ("note_min", "B"), ("crossfade", "B"), ("note_max", "B"),
    ("number", "B"), ("bend_min", "H"), ("bend_max", "H"), ("key_pitch", "b"),
    ("key_pitch_center", "B"), ("key_amplitude", "b"), ("key_amplitude_center", "B"),
    ("key_pan", "b"), ("key_pan_center", "B"), ("volume", "B"), ("pan", "b"),
    ("transpose", "b"), ("detune", "b"),
]


def _fields(data: bytes, at: int, fields: list) -> dict:
    code = "<" + "".join(t for _, t in fields)
    return dict(zip((n for n, _ in fields), struct.unpack_from(code, data, at)))


def _offsets(data: bytes, at: int, tag: bytes) -> list[int | None]:
    if data[at:at + 8] != tag:
        raise ValueError("Invalid Sony instrument chunk")
    size, last = struct.unpack_from("<II", data, at + 8)
    if size < 16 + 4 * (last + 1) or at + size > len(data):
        raise ValueError("Invalid Sony instrument table size")
    offsets = struct.unpack_from(f"<{last + 1}I", data, at + 16)
    return [None if off == 0xFFFFFFFF else at + off for off in offsets]


def read_instruments(data: bytes, samples_at: int, sets_at: int, programs_at: int) -> dict:
    samples = [None if at is None else _fields(data, at, SAMPLE_FIELDS)
               for at in _offsets(data, samples_at, b"IECSlpmS")]
    sets = []
    for at in _offsets(data, sets_at, b"IECStesS"):
        if at is None:
            sets.append(None)
            continue
        curve, low, high, count = struct.unpack_from("<4B", data, at)
        indices = list(struct.unpack_from(f"<{count}H", data, at + 4))
        if any(i >= len(samples) or samples[i] is None for i in indices):
            raise ValueError("Sample set references missing sample parameters")
        sets.append({"velocity_curve": curve, "velocity_min": low,
                     "velocity_max": high, "samples": indices})
    programs = []
    for at in _offsets(data, programs_at, b"IECSgorP"):
        if at is None:
            programs.append(None)
            continue
        program = _fields(data, at, PROGRAM_FIELDS)
        if program["split_stride"] != 20:
            raise ValueError("Unsupported Sony program split stride")
        splits = [_fields(data, at + program["split_offset"] + i * 20, SPLIT_FIELDS)
                  for i in range(program["split_count"])]
        if any(s["sample_set"] >= len(sets) or sets[s["sample_set"]] is None for s in splits):
            raise ValueError("Program split references missing sample set")
        program["splits"] = splits
        programs.append(program)
    return {"samples": samples, "sample_sets": sets, "programs": programs,
            "value_units": "original Sony HD values; ADSR registers retained"}
