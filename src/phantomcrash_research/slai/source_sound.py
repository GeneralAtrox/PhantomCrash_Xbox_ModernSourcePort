"""Export authored source notes, sample loops and live ADSR."""

from .sound_render import sequence_notes, sequence_regions, voice_parameters
from .sound_envelope import RATE


def source_sound(bank, sequence, root):
    loop = {}
    notes = sequence_notes((root / sequence["file"]).read_bytes(), sony_loops=True, loop_info=loop)
    if not notes:
        raise ValueError("Expected an audible source sequence")
    if not loop:
        loop = {"start": 0, "end": max(n["end"] for n in notes)}
    if set(loop) != {"start", "end"} or not 0 <= loop["start"] < loop["end"]:
        raise ValueError("Expected one valid source sequence region")
    voices = []
    for note, program, split, sample in sequence_regions(bank, notes):
        wave = next(w for w in bank["waves"] if w["index"] == sample["wave"])
        # render_sequence validates tracking/modulation before this export.
        voices.append({"file": "slai/assets/sound/" + wave["file"],
                       "start_seconds": note["start"], "end_seconds": note["end"],
                       "adsr1": sample["adsr1"], "adsr2": sample["adsr2"],
                       "loop_start": wave["loop"].get("start_sample", 0),
                       "loop_end": wave["loop"].get("end_sample", wave["samples"]),
                       "repeat_sample": wave["loop"].get("repeat", False),
                       **voice_parameters(note, program, split, sample)})
    return {"format": "source_sample_sequence", "envelope_rate": RATE,
            "loop_start_seconds": loop["start"], "loop_end_seconds": loop["end"],
            "source_sequence": sequence["file"], "voices": voices}
