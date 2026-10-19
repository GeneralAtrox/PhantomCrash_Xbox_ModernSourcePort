# Battle post-processing

## Implemented controls

**Settings → Visuals** contains four independent items: **Depth blur**,
**Shinjuku distortion**, **Stage colour tint**, and **SMAA anti-aliasing**.
The first three are the original's own screen pass and start checked, as
the original always runs them; SMAA is the port's and starts unchecked.
Each takes effect immediately and is saved when selected. Restarting restores
the checkmarks. A settings file without an entry takes that default.
The settings file stores:

```json
"post_process": {
  "depth_blur": false,
  "distortion": false,
  "colour_tint": false,
  "smaa": false
}
```

Fog, lights, shadows and gameplay effects retain their existing behavior.
Distortion only applies where the stage enables it (daytime Shinjuku), and
works with Depth blur either on or off. Colour tint is independently applied
after blur/distortion. The cockpit is drawn next; SMAA processes the resolved
3D image afterward, before SDL draws the HUD and menu text.

The corrected blur intermediates use reference heights 240 and 120, extending
their widths to the viewport aspect ratio. A 4:3 viewport uses 320×240 then
160×120; 16:9 uses 427×240 then 213×120. The main scene stays at full native
resolution. Both blur strengths and distortion matrix values follow the
original ten-day formula, established once when entering the battle.

The distortion reconstructs the original signed offset bytes. Its identical
columns are stored as a 1×64 signed texture; the second offset component is
zero. Its 0.2-radian phase advances on 30 Hz reference ticks of the fixed game
clock, with the alternate buffer's one-tick delay represented explicitly.
Repeated presentations, extra monitor renders and pausing do not accelerate
it. Startup uses a neutral offset map before the first tick. Timing is carried
in the recorded world frame for replay. The current desktop toggles apply
while viewing replay too.

Stage tuning is editable under `screen.depth_of_field` (`focus`,
`day_focus_range`) and `screen.distortion` (`enabled`, `amplitude`,
`phase_step`, `update_hz`) in `data/world/stages.json`. The unsupported Shibuya
focus override of 73 was removed; all shipped stage defaults use 80.

Validation: Release build; 454 core and 8 input/replay tests; actual window-menu
selection, immediate file save, restart and independent disable with quest data
preserved; individual and combined effect smoke runs at 4:3 and widescreen.
This is a behavioral port; Xbox sample alignment and presentation timing have
not been matched against a newly captured xemu frame sequence.

## SMAA anti-aliasing

SMAA 1x uses the upstream High preset, color-edge detection, area/search
lookup textures, blending-weight calculation and neighborhood blending.
The unmodified upstream files and MIT license are pinned under
`port/third_party/smaa` (revision in `UPSTREAM.txt`). Shader source is embedded
at CMake configure time and the lookup bytes are compiled into the executable;
releases need no extra shader downloads or external texture files.

The pass runs after the existing MSAA resolve, independently of the other
post effects. Edge and weight targets use R8G8 and RGBA8 UNORM respectively,
at the current render resolution, and are recreated on resize. Reads and
writes use the port's existing non-sRGB color convention; neighborhood blending
therefore happens in gamma space, an upstream-supported integration mode.
Coverage alpha is filtered along with RGB for transparent 3D previews.
This is spatial SMAA, without temporal history, jitter or motion vectors.

`SmaaPass` owns its shaders, samplers, lookup textures and intermediate targets.
It initializes only when enabled. Source/target resources are separate and
unbound between passes; the existing SDL device-context restoration still
surrounds the complete 3D draw. A setup failure is logged once and leaves the
unfiltered resolved image available; toggling off/on allows another attempt.
Disabling it skips all SMAA passes. The saved preference defaults off, including
when loading older settings.

Validation for SMAA: Release build and runtime logs, with no new tests. A
960x540 battle initialized all three passes with all existing post effects on.
An owned normal runtime verified the menu item/checkmark, immediate persistence,
live off/on toggling and resize from 800x600 to 1706x994. A fresh launch restored
the saved enabled value. The existing agent-owned review preferences were
restored byte-for-byte; no screenshots or extra verification directories were created.
Visual quality remains for gameplay review; these checks do not constitute an
image-quality comparison.

## Original investigation

Investigated 2026-09-30 against the user-supplied European XBE, shipped
pixel shaders, and the port at `0ea8bce`. This is static analysis, not a
new xemu frame capture or a claim of exact rendering parity. The comparison
below describes that baseline, before the implementation above.

## Findings

The original's soft, hazy battle appearance combines distance fog,
stage colour tint and a deliberately low-resolution depth blur. Daytime
Shinjuku additionally distorts the scene through an animated offset map.
There are three concrete gaps in the port's implementation.

| Component | Original evidence | Port behavior at investigated baseline |
|---|---|---|
| Blur chain | `0x0b26f0` allocates 640×480, 320×240 and **160×120** textures. `0x0b5fb0` draws the two successive blur passes into the latter two. | `ensure_blur_targets()` creates **both** targets at half the output size. At 1920×1080 both are 960×540. |
| Day-dependent strength | Battle setup `0x046aa5` calculates focus from the stored day modulo ten. | Focus is fixed in stage JSON, normally 80; Shibuya day has an unexplained 73 override. |
| Shinjuku day distortion | Stage flag table `0x203840` is `[1,0,0,0,0,0,0,0]`. Stage 0 selects `dofblur.xpu` and an animated bump offset map. | All stages use the same undistorted depth-blur shader. Stage 0 is now a playable port stage. |
| Fog and tint | The environment preset supplies fog distance/colour and screen multiplication colour. | Both are already active; this is not a missing blanket fog pass. |
| Gamma/levels | `0x044974` updates channel lookup tables. All eight first stage presets have minimum `[0,0,0]`, maximum `[255,255,255]`. | The screen pass applies a stage's `screen.levels` after the tint, clipping each channel's bytes below the minimum to black and above the maximum to white as `0x044974` does (it leaves the bytes between unchanged). The first presets' values are neutral, so stages leave them out. These default values are neutral and do not explain the absent haze. |

The first three differences directly affect the requested appearance and
were the implementation targets. The table establishes code differences,
not a measured attribution of every difference in a screenshot.

## The actual blur chain

`0x0b7900` selects the screen pass once, before view layer `0xb` (cockpit).
Battle setup enables depth blur via `0x0b2680(1)`. The normal path in
`0x0b5fb0` is:

1. Bind the scene in four texture stages. Use program 9 (`blur.xpu`) and
   quad `0x25b6a8` to sample four diagonal offsets of one scene texel into
   the 320×240 target (`0x9cb250`, surface `0x9cb258`).
2. Bind that result in the same stages. Quad `0x25b768` samples offsets of
   one 320×240 texel into the **160×120** target (`0x9c7144`, surface
   `0x9cb168`).
3. Program 8 (`dof.xpu`) composites the original scene and softened result
   using the top byte of the original 24-bit depth; the final colour is
   multiplied by the stage tint.

Texture allocation sizes were independently checked in Ghidra and radare2.
The quad source coordinates span 640×480 and 320×240 respectively. The
previous documentation's description of two same-size blur targets was wrong.

The port already keeps tap offsets in reference-resolution units, then
doubles them for pass two. That preserves the nominal tap separation, but
does not reproduce the low-resolution sample grids and reconstruction.
Simply increasing the tap radius is not equivalent to fixing the chain.
The main scene should continue rendering at native resolution; low-resolution
post-process intermediates do not require lowering the main scene or UI.
Widescreen dimensions and sample alignment remain implementation decisions
to check during that correction.

## The field is the day counter

The word at saved-player `+0x9cc` packs several fields. Bits 20–29 are the
stored zero-based day: `0x0b1b56` reads exactly that field for the DAY display.
It is not a camera property. `0x046aa5` reads it when setting up a battle:

```text
d = stored_day % 10
strength = abs(abs(d - 5) - 5) / 5
focus = stage_focus - 10 * strength
threshold = 0.95 + 0.0005 * focus
blur_mix = clamp((depth_top_byte / 255 - threshold) * 32, 0, 1)
```

All eight shipped stage-focus values at `0x203820` are 80. `0x02118c` is
the absolute-value helper. The day-derived setting is established at battle
setup, rather than continuously tracking the camera or a target.

| Displayed day | Focus | Maximum distant blur mix |
|---|---:|---:|
| 1 | 80 | 32% |
| 2 | 78 | 35.2% |
| 3 | 76 | 38.4% |
| 6 | 70 | 48% |
| 10 | 78 | 35.2% |
| 11 | 80 | 32% |

The maximum assumes the depth byte has reached 255; closer geometry mixes
less. The 73 override in `stages.json` is not a shipped table value and
cannot result from this integer-day formula.

## Shinjuku's distortion

Battle setup passes the stage's flag to `0x0b26a0`, selecting program
`0x1c` (`dofblur.xpu`) instead of program 8. `0x0b2770` updates a 64×64
offset texture with row-dependent sine values and a vertical falloff;
the phase advances by 0.2 each call and wraps at 2π. The texture buffers
alternate. This is a distortion of the scene sampling in the depth-blur
path, not an additional bright-pixel bloom pass.

The same day strength is passed to `0x0b26b0`, setting bump matrix diagonal
terms to `1 + strength` and off-diagonal terms to `3 * strength`. Therefore
restoring a fixed wave alone would still miss the original day variation.
Exact offset-byte interpretation, alternating-buffer latency, sampler
alignment and presentation cadence should be checked when implementing it;
the phase increment alone does not establish seconds-based animation speed.

## What was not established as a missing effect

- There is no bright-pass bloom or temporal scene-history motion-blur pass
  in the inspected battle screen chain. A blurry screenshot alone is not
  evidence to add either.
- `shimmer.xpu` has a separate path at `0x0b6660`, but the inspected static
  references to its gate `0x9c7140` contain no writer. The stage-0 distortion
  uses `dofblur.xpu`, despite both effects being describable as shimmer.
- Xbox SDK device initialization calls the identified flicker-filter setter
  with 5 and the soft-display-filter setter with 0 (`0x128c33–0x128c3c`).
  This does not prove either filter is visible in the user's xemu output.
  Video mode, encoder and emulator presentation behavior were not captured.
- Fog is already supplied from the original presets. Its distance is now
  checked (see "Fog distance" below); the port fogs by the same metric.

## Fog distance

`0x0a1590` sets the preset's fog table mode (render state 0x5d), colour,
density, start and end. The Xbox D3D runtime turns start and end into the
hardware's linear parameters unchanged (`0x126b20`: 1/(end − start) and
end/(end − start) + 1, method 0x9c0), so the distance is in the units the
draw supplies. Fog generation is planar unless range fog is on (method
0x2a0), and nothing in the game enables range fog. The vertex programs
write their own fog distance: `multi_matrix.xvu` (robots), `bump.xvu`,
`seabump.xvu`, `monitor.xvu` and `cockpitUV.xvu` all write the clip-space
z, which for the D3DX projection is about the view depth. `fog.xvu` scales
it down with height. `0x0b3240` turns fog on per batch only in view
layers 2 and 8 (off with object flag 0x02000000), so the sky is not
fogged. The port therefore fogs by the clip-space z, not by the distance
from the eye, which had fogged the screen's edges more than the original.

The preset's last word (+0x50: 80, or 73 in Shibuya's day preset) goes to
`0x416930` when its flag is set (`0x044c80`), but nothing reads it, so it
is not the haze either. With the fog, tint, levels and depth blur all
accounted for, the remaining softness of the original image is not
explained by the stage presets. The likeliest remaining contributor is the
640×480 output and its scaling in the emulator, which the port does not
copy (it renders at native resolution by design).

Nothing after the screen pass softens the image either. The game creates
its device (0x0c5491, through 0x0c5770 to `Direct3D_CreateDevice`) with a
640×480 X8R8G8B8 back buffer, D24S8 depth and multisample type 0 (+0x10
left zeroed), so `Swap` resolves no Gaussian multisample filter. The game
never calls `D3DDevice_SetFlickerFilter` or `D3DDevice_SetSoftDisplayFilter`
(0x1226c0, 0x122710, XbSymbolDatabase matches; both only send a TV encoder
option, which xemu's picture does not show).

### Original softness (opt-in)

Because the rest comes from the 640×480 picture being scaled up to the
window, the port offers **Visuals > Original softness (640x480 look)**,
off by default. The frame is still rendered at native resolution; the
screen pass then mixes in four filtered taps half of one of the original's
texels out on each axis (a 480th of the window's height, the same size
horizontally). This is about the spread of a 640×480 picture filtered up
to the window. `softness_strength` in the settings file's `post_process`
object (0 to 1, default 1) sets how much of the soft copy is mixed in. It
softens every 3D view, before the cockpit and HUD, which stay sharp. This
is a port addition with no address in the original.

## Evidence provenance and code locations

`blur.xpu`, `dof.xpu`, `dofblur.xpu` and `shimmer.xpu` are shipped filenames.
API names such as `D3DDevice_CreateTexture2` are XbSymbolDatabase SDK matches.
“Blur chain”, “day strength” and other descriptive labels are researcher
names, not recovered game-source symbols.

- Original setup: `0x046aa5`; day display: `0x0b1b56`.
- Target creation: `0x0b26f0`; drawing/composition: `0x0b5fb0`.
- Wave selection/strength/update: `0x0b26a0`, `0x0b26b0`, `0x0b2770`.
- Port: `port/src/desktop_world_screen.cpp`, `desktop_world_passes.cpp`,
  `port/include/phantomcrash/training_ground.hpp`, `data/world/stages.json`.
- Environment extraction: `src/phantomcrash_research/environment.py`.

Generated decompilations, command logs and temporary scripts were removed
after recording these findings. The existing analyzed Ghidra project and
user extraction were preserved.
