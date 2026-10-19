# S.L.A.I. content (optional)

From v0.5.2, enable **Settings > S.L.A.I. content (restart required)** to install
the content from a European S.L.A.I. PS2 ISO (`SLES-52940`). The ISO is required
only for installation. Converted content stays in `artifacts/slai/assets`, and
the toggle is off by default. Restart after changing it. Each mode uses separate
campaign saves so disabling the pack preserves imported parts and progress.

The commands below document the earlier research overlay workflow. The game
does not automatically load a legacy S.L.A.I. `mods/` overlay; keep it for research
and use the Settings installation for gameplay.

S.L.A.I. (PS2, the Phantom Crash sequel) uses the same robot makers and part
slots, so its part models can become new Phantom Crash parts. Nothing changes
unless the mod is built from the user's own S.L.A.I. disc. All format names
below are researcher-assigned.

## Adding an arm weapon

```
python -m phantomcrash_research add-slai-arm-weapon <S.L.A.I. .iso>
```

reads the definition `src/phantomcrash_research/slai/twin_missile.json` (the
part's name, S.L.A.I. model and paint, the Phantom Crash part it starts from
and every stat that differs from it) and writes under `mods/` (never
committed):

- `models/binlink/<resource>.gltf` for the right and left arms, and the
  texture under `textures/slai/`;
- full copies of `robots/catalogue.json`, `robots/skeletons.json`,
  `robots/weapon_mounts.json` and `tables/parts.json` with one new arm type
  and part appended (the base files are replaced wholesale by these);
- `data/ui/robot_preview.json` with the new type's place when shown alone.

Re-running it starts again from the base tables, so it adds one part. Change
the stats in the definition and re-run. Remove the files above to undo.

The default definition adds part 76, "SLAI Twin Missile": the Holy missile
pod (`S3A02H`), two homing missiles per pull, one from each tube.

## Formats

Code is in `src/phantomcrash_research/slai/`.

- `archive.py`: `PC2.STM` holds packages, each a sector-aligned table of
  32-byte entries (name[24], u32 offset from the package, u32 size).
  Files start `WpNO` (stored), `Wp08` or `Wp16` (LZ in 1- or 2-byte units:
  a u32 flag word read from the low bit, 1 = literal; a copy is a u16 with
  length in the low 5 bits plus 3 (Wp08) or 2 (Wp16) and the distance back
  in units in the high 11, 0 meaning 2048; bytes before the start are 0).
- `pod.py`: `.PO2` models. Nodes (0x50 bytes) hold parent, child and
  sibling links and a row-major 4x4 matrix; materials point to batches of
  triangle strips with positions, normals, colours and UVs. Space is y down
  and -z forward; arms are stored as the left one and mirrored for the right.
  Units are about 10 times Phantom Crash's.
- `pig.py`: `.PIG` images, 8- or 4-bit palettes in the GS's swizzled orders.
- Part models are named `S<maker><A|S|B|L><nn>H` (arm, shoulder, body,
  legs); paints are `<model><nn>.PIG`.
