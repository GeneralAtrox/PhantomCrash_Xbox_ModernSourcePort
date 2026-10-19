"""Keep original rigid hierarchies and PMT channels on native part mounts."""

import copy
from pathlib import PurePosixPath

from .native_winding import port_winding
from .motion_resources import native_clip_names, body_attachment_nodes




def articulated_model(source, texture, stem, *, side=None, offset=None, profile=None):
    model = copy.deepcopy(source)
    first = model["nodes"][0]
    origin = first.get("translation", first.get("matrix", [0.] * 16)[12:15])
    # A separate mount node cancels the part's authored assembly position.
    # Animation keys stay in their original hierarchy and coordinate system.
    anchor = {"name": "native_part_mount", "translation": [-v for v in origin],
              "children": model["scenes"][model.get("scene", 0)]["nodes"]}
    index = len(model["nodes"])
    model["nodes"].append(anchor)
    if side is not None or offset is not None:
        mount = {"name": "native_side_mount", "children": [index],
                 "scale": [-1 if side == 0 else 1, 1, 1],
                 "translation": list(offset) if offset is not None else [0, 0, 0]}
        index = len(model["nodes"])
        model["nodes"].append(mount)
    model["scenes"] = [{"nodes": [index]}]
    model["scene"] = 0
    port_winding(model, side == 0)
    metadata = model["asset"].setdefault("extras", {})
    metadata.update({"rigid_animation": True, "animation_prefix": stem,
                     "motion_clips": [f"{stem}{i+1:02d}" for i in range(37)],
                     "weapon_clip": f"{stem}01", "source_frame_rate": 60})
    if profile is not None:
        metadata["animation_prefix"] = profile["motion_prefix"]
        metadata["motion_clips"] = native_clip_names(profile)
        metadata["source_motion_descriptor"] = profile["address"]
        metadata["source_action_clips"] = profile["source_action_clips"]
    if offset is not None:
        metadata["body_mount_offset"] = list(offset)
    if stem[2:3].upper() == "B" and profile is not None:
        metadata['preserve_disabled_clips'] = True
        metadata['independent_motions'] = list(range(37, 45))
        metadata['source_transition_frames'] = profile.get('native_transition_frames', [])
        metadata.update(body_attachment_nodes(profile))
        metadata["source_attachment_nodes"] = profile["attachment_nodes"]
    elif stem[2:3].upper() == "B" and len(source["nodes"]) > 5:
        # Body controls 4/5 and 2/3 end at the two hands. The source weapon
        # roots are hand-mounted; native arm bone zero is an upper-arm joint.
        if 3 in source["nodes"][2].get("children", []) and 5 in source["nodes"][4].get("children", []):
            metadata["arm_mount_nodes"] = [5, 3]
        elif stem.upper() == "S9B01H":
            # This asymmetric body has direct attachment controls rather than
            # the ordinary makers' upper-arm/forearm chains.
            metadata["arm_mount_nodes"] = [2, 3]
    for image in model.get("images", []):
        image["uri"] = texture or "../../slai/assets/textures/" + PurePosixPath(image["uri"]).name
    for material in model.get("materials", []):
        material.setdefault("extras", {})["part_paint"] = bool(texture)
    return model
