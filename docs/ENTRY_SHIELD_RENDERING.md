# Entry shield rendering

Validated against the European disc's XBE and shipped shader binaries on
2026-09-30, with the timing and lighting details added on 2026-10-03. This
is a static validation of the original rendering path; it does not claim an
emulator capture comparison.

## Shader selection

`0x06f592` draws object slot `0x41b` (resource 197, mesh 30) with object flag
`0x100000`. The flagged branch in `0x0b3240` selects dispatch entry `0x19`.
The two dispatch tables resolve that entry to:

| Responsibility | Function | Evidence |
| --- | --- | --- |
| Bind programs | `0x0c5179` | Vertex handle `0xa112dc`, pixel handle `0xa11338` |
| Set constants and texture | `0x0c43ae` | Constants c0–c9 and texture stage 0 |

The shader loader `0x0beee0` maps those handles to vertex table entry 21
(`0x280a50`) and pixel table entry 18 (`0x280c90`):
`Media/Shaders/rainbow.xvu` and `Media/Shaders/rainbow.xpu`.
The previous attribution to `barrier.xvu` was incorrect.

## Colour and waves

The sphere contains 580 vertices, with object-space coordinates approximately
−0.5 to +0.5 on each axis. The rainbow vertex program has 48 instructions.
It calculates RGB at vertices from object-space height and three phase offsets,
then the rasterizer interpolates the colours across the sphere.

`0x0c43ae` supplies these constants:

| Constant | Value |
| --- | --- |
| c4.xyz | Negated lighting direction, normalized |
| c5.xyz, c6.xyz | Lighting colours from the object's lighting record |
| c7 | `(1, 0, scroll, 0.5)` |
| c8 | `(phase − 0.6666, phase, phase + 0.6666, 2)` |
| c9 | `(glow, 0, 0, 0)` |

The reciprocal-square-root/reciprocal pairs implement absolute values. For
each channel, the colour term is equivalent to:

```text
d = abs(vertex.y + channel_phase)
rainbow = abs((d >= 1 ? 2 : 0) - d)
light = c5.rgb * max(dot(c4.xyz, vertex.normal), 0) + c6.rgb
vertex.rgb = saturate(0.75 * light + 0.5 * rainbow)
vertex.alpha = glow
uv = (vertex.x + 0.5, vertex.y + 0.5 + scroll)
```

`vertex.normal` is the mesh's own normal: `0x0c43ae` normalizes the light
direction but does not take it into object space, so the lit side turns with
the spinning shell rather than staying towards the light. The hardware clamps
the output colour (oD0) to [0, 1].

The phase at `0xa69c44` decreases by `0.03` per invocation of `0x0c43ae`,
resetting to `1` when below `−1`. The scroll at `0x281828` starts at `1`,
decreases by `0.01` per invocation, and adds `1` when below zero. These are
shared draw-path updates, not evidence of a separate per-robot simulation
timer. A native implementation must account for repeated rendering, including
monitor views, without accidentally advancing the effect for each extra pass.

`Media/systemdata/muteki01.tga` is a 64×64 texture. Every RGB texel is white;
its alpha ranges from 5 to 255. The pixel program copies the interpolated RGB
and multiplies texture alpha by interpolated vertex alpha. The texture supplies
waves, not the rainbow colours. The draw colour `0x64ffffff` is passed by the
caller, but the rainbow vertex program supplies alpha from c9.x instead of
consuming that diffuse alpha. `0x0c4373` clamps the supplied glow to [0, 1].

## Timing

A battle draws once per 30 Hz tick: `0x0b3220` calls `0x0c53b1`, which
waits refresh / rate vertical blanks (`0xa69c94`, `0xac73b8`) before each
swap. Every per-draw step is therefore 30 times a second:

| Value | Where | Step per draw | Per second |
| --- | --- | --- | --- |
| Glow rise (to 0.3) | robot +0xac, `0x082e60` | +0.015 | 0.45 |
| Glow fall | robot +0xac, `0x082e60` | −0.03 | 0.9 |
| Shell angle | `0x988570`, `0x06f592` | +0.02, wraps at 2π | 0.6 rad |
| Rainbow phase | `0xa69c44`, `0x0c43ae` | −0.03 | 0.9 |
| UV scroll | `0x281828`, `0x0c43ae` | −0.01 | 0.3 |

The glow falls once 10 or fewer timer frames are left (the timer at +0x27d4
counts 1/60 s frames, two a tick) or when the armour is below 1; the shield
is drawn while the glow is above zero. The angle, phase and scroll are
globals stepped by each shield drawn, so with two shielded robots on screen
they turn and cycle twice as fast.

## Native implementation

The world vertex shader now evaluates the recovered height-dependent RGB
bands, lighting contribution and scrolling planar UVs. `WorldItem::rainbow`
carries phase and scroll into every view and the replay archive. The shell's
texture still supplies wave alpha; it is not recoloured in extraction. The
extra 100/255 opacity multiplier has been removed.

`BattleView::advance_shield` advances the shared angle, phase and scroll by
elapsed time at `update_hz` (30) draws a second, times the number of robots
whose glow is above zero (`per_shield`, on by default; off steps them once
whatever the number). It advances continuously rather than in 30 Hz steps,
so the effect is smooth at high frame rates, and rendering additional monitor
or split views does not advance it. The rainbow branch of the world vertex
shader lights with the untransformed mesh normal and clamps the colour. All
rates are editable in the entry-shield settings in `data/world/training.json`.
The exported glTF UVs remain a fallback for external viewers; the native
rainbow shader replaces them.

The port's `ambient` and `sun_colour` stand in for c6 and c5: the shield is
lit by the stage's lighting, like the robot inside it, rather than by the
lighting record `0x0c43ae` indexes (from `0xa7836c`, 0x68 bytes apart). This
is a deliberate choice; which record the original selects was not traced.
Appearance remains for user review. It is not a claim of exact shader or
draw-state parity.
