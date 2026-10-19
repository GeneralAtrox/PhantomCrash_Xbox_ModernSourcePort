"""Bind recovered weapon fire sequences to the existing native sound player."""

import json
import wave
from pathlib import Path

from .archive import SlaiDisc
from .assets import save
from .program import ProgramData
from .sound_cues import recover_cues
from .sound_envelope import RATE
from .sound_render import render_sequence, sequence_notes
from .source_sound import source_sound


def register_sound(image: Path, data: Path, mods: Path):
    root = mods / "slai/assets/sound"
    if not (root / "banks.json").exists():
        raise ValueError("Convert S.L.A.I. sound banks before native registration")
    banks = json.loads((root / "banks.json").read_text(encoding="utf-8"))["banks"]
    parts = json.loads((mods / "slai/native_parts.json").read_text(encoding="utf-8"))["parts"]
    with SlaiDisc(image) as disc:
        source = recover_cues(ProgramData(disc), banks)
    wanted = sorted({source["weapons"][p["category"]][p["source_record"]]["fire_cue"]
                     for p in parts if p["category"] in ("arm", "shoulder")} - {-1})
    impact_ids = {cue for p in parts if p["category"] in ("arm", "shoulder")
                  for target in ("robot_cues", "surface_cues")
                  for cue in source["weapons"][p["category"]][p["source_record"]]["impact"][target]}
    cycles = {row["fire_cue"]: row["fire_cycle"]
              for rows in source["weapons"].values() for row in rows if row["fire_cycle"]}
    paired_ids = {cue for base, cycle in cycles.items()
                  for cue in (base, cycle["secondary_cue"], cycle["ending_cue"])}
    wanted = sorted(set(wanted) | impact_ids | paired_ids)
    rendered, pending = {}, []
    for cue_id in wanted:
        cue = source["cues"][cue_id]
        bank = banks[cue["bank"]]
        sequence = bank["sequences"][cue["sequence"]]
        try:
            pcm = render_sequence(bank, sequence, root)
        except ValueError as e:
            pending.append({"cue": cue_id, "bank": cue["bank"], "sequence": cue["sequence"], "reason": str(e)})
            continue
        relative = f"slai/assets/sound/cue_{cue_id:03d}.wav"
        with wave.open(str(mods / relative), "wb") as writer:
            writer.setparams((2, 2, RATE, 0, "NONE", "not compressed"))
            writer.writeframes(pcm.tobytes())
        loop = any(n.get("source_loop") for n in sequence_notes(
            (root / sequence["file"]).read_bytes(), sony_loops=True))
        rendered[cue_id] = {"file": relative, "frames": len(pcm),
                            "source_sequence": sequence["file"], "loop": loop}
        if loop or cue_id in paired_ids:
            native_file = f"slai/assets/sound/cue_{cue_id:03d}.json"
            save(mods / native_file, source_sound(bank, sequence, root))
            rendered[cue_id].update({"file": native_file, "preview_file": relative,
                                    "playback": "authored sample loops and live ADSR release"})
    table = json.loads((data / "audio/battle_sounds.json").read_text(encoding="utf-8"))
    rules, impact_rules, bound, silent = [], [], [], []
    for part in parts:
        if part["category"] not in ("arm", "shoulder"):
            continue
        mapping = source["weapons"][part["category"]][part["source_record"]]
        cue_id = mapping["fire_cue"]
        sustained = mapping["fire_sustain"] is not None
        impact = mapping["impact"]
        for on_robot, key in ((True, "robot_cues"), (False, "surface_cues")):
            if all(cue in rendered for cue in impact[key]):
                impact_rules.append({"mount": part["category"], "models": [part["type"]],
                    "on_robot": on_robot,
                    "sound": rendered[impact[key][0]]["file"],
                    "loop": all(rendered[cue]["loop"] for cue in impact[key]),
                    "trigger_policy": ("sustained" if sustained else "held")
                        if all(rendered[cue]["loop"] for cue in impact[key]) else "shot",
                    "variants": [{"sound": rendered[cue]["file"], "weight": weight}
                        for cue, weight in zip(impact[key], [1] if on_robot else impact["surface_weights"])]})
        if cue_id < 0 or cue_id in rendered:
            rule = {"mount": part["category"], "models": [part["type"]],
                    "sound": rendered[cue_id]["file"] if cue_id >= 0 else "",
                    "loop": cue_id >= 0 and rendered[cue_id]["loop"]}
            rule["trigger_policy"] = "held" if rule["loop"] else "shot"
            if sustained:
                rule["trigger_policy"] = "sustained"
            if cue_id == 177:
                rule['requires_free_arm_action'] = True
            cycle = mapping["fire_cycle"]
            if cycle:
                needed = (cycle["secondary_cue"], cycle["ending_cue"])
                if not all(c in rendered for c in needed):
                    raise ValueError(f'Paired fire cue {cue_id} has an unconverted member')
                rule["trigger_policy"] = "paired"
                rule["fire_cycle"] = {"group": cycle["group"],
                    "start_together": cycle["start_together"],
                    "secondary": {"sound": rendered[needed[0]]["file"],
                                  "loop": rendered[needed[0]]["loop"]},
                    "ending": {"sound": rendered[needed[1]]["file"],
                               "loop": rendered[needed[1]]["loop"]}}
            rules.append(rule)
            (silent if cue_id < 0 else bound).append({"model": part["model"], "cue": cue_id})
    table["fire"] = rules + table["fire"]
    table["impact"] = impact_rules + table["impact"]
    save(mods / "data/audio/battle_sounds.json", table)
    source.update({"rendered": rendered, "bound_parts": bound, "silent_parts": silent,
                   "pending": pending,
                   "adapter": "Original sample sequences; native float interpolation/panning; hardware reverb and LFO pending",
                   "fire_variants": "Five source robot-shared pairs; accepted native shot events start/advance, held matching mounts with positive ammo retain; source mount order and ammo boundaries",
                   "sustained_fire": "209/228/247 start on accepted native shots when their mount cue is absent; live beam fade retains fire and impact voices through release; native acceptance and beam producer remain adapters",
                   "impact_cues": "Record +0x1d and original 0x50f9f0 switch; generic surface probabilities retained",
                   "bound_impact_rules": len(impact_rules)})
    save(mods / "slai/sound_cues.json", source)
    return {"registered_sound_cues": len(rendered), "parts_with_source_fire_sound": len(bound),
            "registered_impact_rules": len(impact_rules),
            "parts_with_source_silence": len(silent), "pending_fire_cues": len(pending)}
