"""Register the source PTK library and recovered weapon impact routing."""

from pathlib import Path
from .archive import SlaiDisc
from .assets import save
from .effect_routes import particle_pool, impact_route, impact_attachment_contract
from .program import ProgramData
from .sound_cues import recover_cues
from .action_bank import export_action_banks, weapon_action_route, weapon_state_contract
from .projectile_routes import projectile_routes, export_projectile_models
from .world_units import particle_world_units
import json
from PIL import Image


def register_particles(image: Path, mods: Path):
    root = mods / "slai/assets"
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    parts = json.loads((mods / "slai/native_parts.json").read_text(encoding="utf-8"))["parts"]
    with SlaiDisc(image) as disc:
        program = ProgramData(disc)
        impact_attachments = impact_attachment_contract(program)
        projectiles = projectile_routes(program, parts, manifest["models"])
        units = particle_world_units(program)
        export_projectile_models(mods, projectiles, units)
        action_banks = export_action_banks(disc, root)
        manifest["action_banks"] = action_banks
        save(root / "manifest.json", manifest)
        summary_path = root / "summary.json"
        if summary_path.exists():
            summary = json.loads(summary_path.read_text(encoding="utf-8"))
            summary.update({"action_banks": len(action_banks),
                            "action_records": sum(e["records"] for e in action_banks),
                            "action_effects": sum(e["effects"] for e in action_banks)})
            save(summary_path, summary)
        pool = particle_pool(program)
        banks = json.loads((root / "sound/banks.json").read_text(encoding="utf-8"))["banks"]
        cues = recover_cues(program, banks)
        from .motion_effects import motion_effect_contract, leg_action_route
        motion_contract = motion_effect_contract(program)
        state_contract = weapon_state_contract(program)
        leg_actions = [{"model": part['type'], "source_record": part['source_record'],
                        **leg_action_route(program, action_banks, part['source_record'])}
                       for part in parts if part['category'] == 'leg']
        rules, weapon_actions = [], []
        for part in parts:
            if part["category"] not in ("arm", "shoulder"):
                continue
            kind = cues["weapons"][part["category"]][part["source_record"]]["weapon_kind"]
            route = impact_route(program, part["category"], part["source_record"], kind)
            rules.append({"mount": part["category"], "model": part["type"], **route})
            weapon_actions.append({"mount": part["category"], "model": part["type"],
                                   "source_record": part["source_record"],
                                   **weapon_action_route(program, action_banks, kind)})
    for entry in pool:
        if not (mods / entry["file"]).exists():
            raise ValueError(f"Missing source particle {entry['source_path']}")
        entry["atlas_frames"] = {}
        for g in json.loads((mods / entry["file"]).read_text(encoding="utf-8"))["generators"]:
            fields = g["decoded_fields"]
            if fields["flags"] & 0x20000:
                texture = fields["texture_path"]
                name = Path(texture.replace("\\", "/")).stem
                with Image.open(root / "textures" / (name + ".png")) as image:
                    width, height = image.size
                entry["atlas_frames"][texture] = width // height
    table = {"update_hz": 60, "world_scale": units['world_scale'], "pool": pool, "impacts": rules,
             "action_banks": [{**entry, "file": "slai/assets/" + entry["file"]} for entry in action_banks],
             "weapon_actions": weapon_actions, "leg_actions": leg_actions,
             "motion_effect_contract": motion_contract, "projectiles": projectiles,
             "weapon_state_contract": state_contract,
             "world_units": units,
             "impact_attachments": impact_attachments,
             "adapter": "Native 60 Hz particle clock; source tick increment is 1; native world hit points are rebased once into the masked target frame",
             "provenance": {"resource_pool": "0x59be30", "loader": "0x507160",
                            "impact": "0x50f9f0"},
             "unresolved": ["Original ASB secondary/condition gates, part-loss countdown and visibility producers, bank byte +0x41 producer and clock cadence",
                            "Two leg and twelve weapon-loss descriptors with negative parent nodes; raw ASB descriptors remain preserved",
                            "Original actor +0xdf4 alternate-particle producer; native model actions currently use the normal variant",
                            "Collision-mask inertia suppression, special stop rules and recycled effect pointer aliasing",
                            "Muzzle/trail and non-ballistic projectile playback; original collision-point producer coordinates",
                            "Full GS state and particle submission ordering"]}
    save(mods / "slai/native_particles.json", table)
    return {"native_particle_resources": len(pool), "particle_impact_rules": len(rules),
            "source_action_banks": len(action_banks), "leg_action_routes": len(leg_actions),
            "source_action_records": sum(e["records"] for e in action_banks),
            "source_action_effects": sum(e["effects"] for e in action_banks),
            "weapon_action_routes": len(weapon_actions), "projectile_model_routes": len(projectiles)}
