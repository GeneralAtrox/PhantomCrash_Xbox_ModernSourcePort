# MechAssault weapon effects (experimental mod)

An opt-in overlay that plays MechAssault's weapon effects from Phantom Crash's
weapons in the battle screens. The base data ships disabled
(`data/world/mechassault_vfx.json`, `"enabled": false`); nothing changes
unless the mod is converted from the user's own MechAssault disc.

## Converting

```
python -m phantomcrash_research convert-mechassault-vfx <MechAssault .iso/.xiso>
```

writes under `mods/` (never committed): `textures/mechassault/*.png`,
`models/mechassault/*.gltf` and `data/world/mechassault_vfx.json` with
`"enabled": true`. Reader code is in `src/phantomcrash_research/mechassault/`
(`mgf.py` archives and property streams, `texture.py` MGIc textures and
geometry, `effects.py` node trees and modifiers, `export.py` the writer).

## What the port does

`port/src/mech_vfx_data.*` parses the JSON; `port/src/desktop_mech_vfx.*`
starts a muzzle effect on each `fired` event, rides a shot effect on each
projectile and starts an impact effect on each `impact` event, keyed by the
weapon's HUD kind (`weapons` in the JSON; edit it to remap). Only the node
trees are drawn, with the modifiers for scale growth, spin, kill time and
alpha ramps. `unit_scale` converts MechAssault's units to the port's.

## Arm swap (visual only)

```
python -m phantomcrash_research swap-mechassault-arm <MechAssault .iso/.xiso>
```

writes `mods/models/binlink/105.gltf` and `92.gltf` (the 25mm minigun's
right and left arms) and their textures under
`mods/textures/mechassault/arms/`. Each keeps the original model's nodes and
upper arm (bone 0); the forearm (bone 1) becomes the Mad Cat's gun arm
(`\Models\Mechs\Heavy\MadCat\Model_Low_Eny\mad_rgimparm` and
`mad_lgimparm` in `Mission06a.mgf`), scaled to the forearm's length with
the front barrel's tip on the original muzzle, so `weapon_mounts.json` is
unchanged. The weapon keeps the minigun's stats. `--right`, `--left`,
`--right-arm` and `--left-arm` pick other models; `src/.../arm_swap.py`.
Delete the two glTF files to undo.

## Not done

- The `.cfx` particle files (sparks, smoke, hit bursts) are not decoded, so
  impact effects that are mostly particles show little or nothing.
- Beams (the laser's shot effect has no `.NODE`) and decal stamps are skipped.
- Orientation (+Z forward) and `unit_scale` were judged from captures only;
  the shot meshes' stretch (up to 27 times along the shot) wants tuning.
