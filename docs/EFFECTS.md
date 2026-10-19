# The particle effects

The game's particle effects live in the XBE's data, not on the disc. The
converter (`effect_bank.py`, run by `convert-robots`) writes them to
`tables/effects.json`. It also writes the atlas `textures/binlink/1589.png`
and the pickup glow `textures/binlink/1574.png`. The port reads the bank in
`effect_bank.cpp` and runs it in `effect_system.cpp` and
`effect_particles.cpp`. The drawing is described in
[TRAINING_GROUND.md](TRAINING_GROUND.md#sprites-and-particle-effects).

Names in this file are researcher-assigned from what the code does with
each field, unless an address is quoted.

## The bank

| Key | Source | Meaning |
| --- | --- | --- |
| `frame_rate` | 30 | Effects step whole frames at the battle's logic rate. |
| `atlas` | constructor 0x11d060 | Texture, cell size (0.125 × 0.25) and the 22 cells' corners in the 8 × 4 grid. |
| `groups` | 0x205c10 | 79 groups. Each is a list of scripts and a list of flashes. |
| `scripts` | 0x215928 | 62 scripts. Each is a list of emitters: flags and a definition index. |
| `definitions` | 0x20d720–0x215cf8 | 71 definitions of 0x150 bytes each. |

Game code starts an effect by its group number (0x057760). Every script in
the group starts one emitter for each of its entries, and each of the
group's flashes starts one flash (0x0659e5, below).

### Emitter flags

The script entry's flags (emitter +0x10) choose how that emitter's
particles behave. `spawn` (0x2), `replaceable` (0x4), `inherit_velocity`
(0x8), `exact_spread` (0x10), `radial_push` (0x20), `random_direction`
(0x40), `spin` (0x80), `streak` (0x100), `streak_long` (0x200),
`streak_short` (0x400), `random_offset` (0x800), `fragments` (0x1000),
`detached` (0x2000), `random_speed` (0x4000), `swirl` (0x8000),
`die_at_target` (0x10000), `debris` (0x20000), `debris_kind` (0x40000),
`kept` (0x80000) and `streak_line` (0x100000). The list ends with 0x200000.
An unnamed bit is written `bit_N`.

### Definitions

Each definition keeps its XBE `address`. Scalars:
- **Force.** `force_scale`, `force_jitter`, `drag`, `drag_jitter` and
  `mass`. The drag each frame is
  clamp(((rand × drag_jitter + drag) / mass) / 100) × 0.5.
- **Gravity.** `no_gravity` and `gravity`. `gravity_angles` are raw
  radians turning (0, −1, 0).
- **Wind.** `wind_off`, `wind` and `wind_angles` (degrees).
- **Pull.** `pull_off`, `pull`, `pull_angles` (degrees),
  `pull_to_target` and `pull_range`. A negative pull draws a particle in.
- **Life.** `life` and `life_jitter` in seconds.
- **Size.** `size`, `size_jitter` and `scale`.
- **Colour.** `colour`, `colour_jitter_hsv`, `alpha` and `alpha_extra`, as
  bytes.
- **Emission.** `rate` (particles a second) and `rate_jitter`; `spread`
  and `spread_jitter` (degrees of cone); `speed` and `speed_jitter`;
  `push`.
- **Shape.** `shape_type` and `shape_variant` select a placement from table
  0x215b18: point, sphere (filled for type 3) or cluster.
- **Cell.** `cell` is the first atlas cell (+0x114). `frames` (+0x116) is
  how many cells it animates through, at 15 a second.
- **Blend.** `additive` (+0x18) selects additive blending. Glowing cells
  set it; smoke and shards do not.

`curves` holds keyed curves of [time, value] pairs, interpolated linearly
and held at the ends: `red`, `green`, `blue`, `alpha`, `scale`, `pull`,
`wind`, `rate`, `mass`, `speed`, `min_speed` and `max_speed`.
- The `*_time` fields say what a curve's time counts: none, the
  particle's age, the emitter's age, or the percentage of life lived.
- A `rate` curve that ends at zero or less stops its emitter.

Four fields are never read by the game: `force_time` (+0x04), `drag_time`
(+0x10), `mass_jitter` (+0x84) and `speed_time` (+0x10c). None of the
spawn (0x060b7c), the emitter's start and emission (0x060107, 0x06077e),
the motion (0x061d35), the acceleration (0x063c16) or the draw (0x0625e4)
loads them. The spawn reads the emission block from +0xc4 and skips
+0x10c. The acceleration reads force at +0x00 and +0x08 and drag at
+0x0c and +0x14, and times every curve by `mass_time`. The names follow
the layout's runs of value, curve time and jitter (`size`, `scale_time`,
`size_jitter`), so they are researcher inferences. `force_time`,
`drag_time` and `speed_time` are 0 in all 71 shipped definitions;
`mass_jitter` holds 0 to 3. They are extracted so each record is
complete, and the port ignores them.

## Running

| Limit | Value | Source |
| --- | --- | --- |
| Emitters | 0x90 | |
| Particles per emitter | 0x70 | |
| Spawning stops | above 0x6e live | |

- **Rate.** Each frame an emitter owes rate / frame_rate particles
  (0x0609d8), or the `rate` curve's value over its frames.
- **Stop.** Stopping (0x06516c) takes hold only after the emitter has
  thrown particles.
- **End.** An emitter ends when it is not emitting, has thrown particles
  and has none left alive.
- **Order.** Particles spawn at 0x060b7c and update at 0x061b8f, with
  motion at 0x061d35 and acceleration at 0x063c16. They are drawn by
  0x0625e4.
- **Drawing.** A sprite is 0.5 × size × scale across. A cluster's centre is
  3.5 × larger. A streak runs along the particle's motion in a frame, a
  fifth of that for `streak_short`.

The width conversion has two half factors: 0x0625e4 submits size × scale ×
0.5 to 0x0bf4cf, then `particle.xvu` expands the quad's corners at ±0.5
(the vertex records at 0x280d90, uploaded by 0x0bf328). The native
`WorldSprite::size` is a half-extent, so it must be size × scale × 0.25,
with the cluster multiplier applied once. This applies to explosion and
smoke particles, including destructible, robot and dropship effects, and
the pickup collection streaks (groups 49–51). It does not change the
separate model-based flashes or pickup glow sprites.

### Destructible explosions

Groups 13, 14 and 15 all start script 0: a smoke cluster (definition 0,
0.85-second children) and an additive orange cluster (definition 1,
0.4-second children). These are the initial explosion, not a separate
flip-book flash. The groups then add their own smoke, fire and debris.

The cluster centre's birth lifetime is a sentinel at 0x060b7c. Its first
update at 0x061b8f converts that sentinel to an indefinite lifetime; later
updates remove it when its children are gone. Killing an isolated centre
on its birth update prevents every child from spawning. The port preserves
this birth grace period and tests both multi-particle and isolated centres.

Children store relative positions (0x063aad), multiplied by submitted size
at 0x0bf4cf. The previously implemented camera-depth multiplier,
clamp(centre depth, 10, 30) × 0.1, enlarged both child quads and their spread
up to threefold when changing views. At the user's request, the port now
keeps both in world units: `effect_sprite.hpp` rotates the cluster offsets
to face each camera without scaling them by depth. Replay retains those
same relative coordinates. Ordinary smoke and flashes also keep their
world-space sizes. Perspective projection still changes apparent screen
size with distance. The centre retains its one-unit depth bias.

The `radial_push` flag on destructible fire and debris is a spawn-position
offset. At 0x060b7c the normalized sideways velocity is multiplied by
`push` and added to particle position (+0xb8), not velocity (+0xc4). The
port previously treated it as additional per-frame velocity, spreading
definition 44's fire at roughly 45 extra world units per second instead
of starting it 1.5 units off-centre. Applying it to position keeps the
fire around the wreck without changing its recovered size or lifetime.

Long smoke is intentional in the extracted definitions: 40 and 43 emit
until about frame 150 (five seconds), and individual particles live four
seconds. Their complete tail can therefore approach nine seconds. Their
scale curves end at 50 and 75 respectively, corresponding to sprite widths
of 25 and 37.5 world units before transparency. The existing half-extent
conversion and these data values are unchanged. Regression checks cover
burst survival, camera-dependent placement, smoke size bounds and expiry
at 30, 60 and 144 presentation updates per second. This does not establish
visual parity for the entire effect renderer.

Validation with the extracted bank measured groups 13 and 14 ending at
about 8.9 seconds and group 15 at 1.6 seconds. An isolated native runtime
test fired at a bus using group 14: captures showed the initial orange
cluster and debris, fire confined around the wreck, and the wreck alone
after the smoke expired. The temporary runtime and captures were removed
after inspection.

Game code may override an emitter's rate, target or position after it
starts. A supply pickup, for example, sets its burst's rate to 2400 × level
and retargets it every frame.

## Flashes

`flashes` (0x215e48, 4 records of 0x20 bytes, written by `effect_models.py`)
are flip-books of textures from pack 1566. `effect_flashes.cpp` runs them and
`desktop_shots.cpp` draws them. The draw callback 0x065ade puts one texture a
frame on `grid_mdl` (model slot 0x4db, a unit quad). The quad faces the
camera at the group's position and is scaled by the scale the group was
started with (0x057760's, record +0x70).

| Field | Source | Meaning |
| --- | --- | --- |
| `frames` | +0 first texture slot, +4 count | The texture shown at each age. |
| `step` | +8 | Frames the age moves on each frame. The flash ends past its last. |
| `billboard` | +0xc | Faces the camera (all four do). |
| `additive` | +0x10 is 0 | Drawn by 0x0b0124 (flags \| 0x220, SRCALPHA, ONE). Otherwise by 0x0affeb (alpha). |
| `update` | table 0x215ec8 | What changes each frame (below). |
| `draw_flags` | +0x14 | 0x8020, passed to the draw. Kept for reference. |

| Flash | Textures | Update |
| --- | --- | --- |
| 0 fireball | 1566/5–20 | `fade` (0x0666be): opaque, then 17 less alpha a frame over the last 15. |
| 1 smoke | 1566/25–39 | `rise` (0x066770): climbs 0.12 and drifts up to ±0.06 in its own space, which grows by age / (2 × frames) + 1 each frame. Alpha 0x70 − age / frames × 112. |
| 2 glow | 1566/24 | `pulse` (0x0668ab): scale sin(phase) × 0.5 + 1 and a flickering alpha top bit. The phase is shared by every pulse, moves 0.2 a frame and wraps at π. |
| 3 spark | 1566/21–23 | `still` (0x066936). |

Groups 5–12 carry flashes. The pool holds 0x80.

## Shots and impacts

`projectiles` (0x2534a8, 18 looks of 0x2c bytes, by weapon record +0x22)
say how 0x09831e draws a shot. Each look names a mesh of the weapon effects
model (resource 194). The mesh is scaled by `scale` and lies along the
flight, or faces the camera for a billboard. Look 17 is a sprite instead
(1567/1). A look with `trail` also draws the `exhaust` mesh (194 mesh 94)
`exhaust.offset` behind, scaled by `exhaust.scale` and rolled each frame.
The six missile meshes (83–85 and 91–93) have their noses along local -Z
in the runtime's game coordinates, so -Z follows velocity. The exhaust
mesh extends along +Z and its positive local-Z offset places it behind
the missile. This accounts for the loader mirroring glTF Z back into game
coordinates; interpreting the glTF axes directly reversed both visuals.
Its persistent smoke trail uses the original slot 0x16d texture (1583/0).
`DesktopShotResidue` samples projectile positions at 15 Hz, keeps up to 33
points for each of 50 trails, and fades a terminated trail for one second.
These defaults follow 0x0702d8, 0x0704d1, 0x071a04 and 0x072119. Tapered
ribbon segments remain after impact; camera/monitor rendering never advances
their clocks. `missile_trail` in the extracted effects table controls texture,
capacity, cadence, point count, fade and widths.

The smoke texture's transparent cross-section runs along V (its top and
bottom edges), while U carries the repeating detail. Missile ribbon sprites
transpose the usual particle UVs so V crosses the width. Mapping V along
each segment instead exposed opaque side edges and transparent gaps between
samples. This mapping is specific to smoke ribbons; atlas particle streaks
use cells 2, 12 and 20, whose outer edges already fade on both axes. The
standalone flash frames, projectile sprite, pickup glow and decal textures
also retain transparent outer edges and keep their existing UV mapping.

`impacts` (0x253408, 13 records of 12 bytes, by weapon record +0x23) name
the effect group a landing starts and the scale of that group's flashes
(0x065f28). A landing starts it where 0x098eda's case for that landing does:
the weapon's `robot_hit` on a robot, its `surface_hit` elsewhere. Cases 1,
2, 4 and 5 start the impact; 0 and 3 do not. `decal` marks the impacts that
leave a bullet hole (`decals`, 0x0663d2). Surface-aligned marks now use the
original texture pair and bounded 100-mark ring. Bullet holes use the
recovered 0.25 width and increment their rotation by one radian per mark.
Force-field and robot contacts do not receive these static marks. The second
texture supplies scorch marks: assigning it to explosive impacts, probing
beneath airbursts and sizing it from blast radius are native choices, not a
recovered original trigger. `decals.depth_offset` and `scorch_scale` expose
those placement/size choices. Marks and trail sprites are stored in replay.

`wreck_groups` are the groups 0x07675e starts where a robot's armour runs
out: 0x34, 0x35 and 0x36 (kept, debris and smoke) and 0xf (the blast). The
supply carrier starts group 0x3f both when shot and when its wreck crashes
(0x06781a and 0x06828e, `crash_effect` in the
supply look).

Shot or expired supply pickups start group 0x40 at their position, at unit
scale (0x069d52, state 3), then disappear. `supply.look.burst_effect` selects
this group. `SupplyDrop` queues each completed burst until `Battle` forwards
it to the normal effect events, so multiple pickups and ticks cannot lose
the effect when the pickup becomes gone or the carrier reloads. Collected
pickups and cargo lost inside a destroyed carrier do not use this effect.

The port's earlier glow discs for shots and blasts are gone. The Armory's
explosions now only time the blasts' damage.

## Arena fence impacts

Shots keep the closest collision triangle's surface and normal through
their landing event. Surface 8 (including its `% 1000` variants) selects
the arena boundary above the visible fence. When the weapon starts its
surface impact, `0x065f28` also starts `0x07027b`'s fence effect. A closer
robot, prop, or door does not inherit that boundary contact.

`fence` in `tables/effects.json` supplies the dedicated mesh animation:
model resource 197, slots 1021–1050 (meshes 0–29). `0x071869` selects one
mesh per 30 Hz frame, scales X/Z from 0.1 by `age / 30 * 0.05`, scales Y
by 0.2, and places it in the surface frame offset -0.5 along the normal.
`0x07027b` allows twenty simultaneous effects, each lasting thirty frames.
The native animation advances with elapsed game time, independently of
presentation rate and split-screen draws.

Robot contact ripples are a requested port addition, not recovered original
behaviour. A robot touching a surface-8 triangle reports the collision point
and the triangle's normal every `battle.fence_contact_seconds` in
`data/world/training.json` (0.016 seconds, every battle tick; the code's
default is 0.25; <= 0 disables it). `data/world/force_field.json` shapes the
touch so it reads as water rather than a ripple jumping along: the robot's
newest ripple glides to the contact, and once the robot has slid `spacing`
units (or `refresh_seconds` pass while it holds still) that ripple is left
behind and a new one follows. Left-behind ripples keep spreading (`growth`,
smoothly rather than a step a frame), are stretched along the slide by its
speed (`stretch`, `stretch_limit`) and fade their refraction out (`fade`),
so sliding down the field leaves a smeared wake of up to `trail` ripples.
The whole field keeps at most `limit` touch ripples (the oldest goes first),
so a crowd sliding along it stays cheap, and fence ripples skip the port's
dynamic lights (`WorldItem::local_light`): a grown, smeared ripple near the
camera covers much of the screen, and lighting each overlapping ripple's
pixels with every light's shadow map is what made spamming the field lag.
Touch ripples do not use the shots' pool of twenty. The two body spheres share one pulse
timer per robot. Leaving the field lets the cooldown elapse instead of resetting
it: intermittent contact at triangle seams cannot bypass the interval. Ordinary
walls and scripted entrance/exit walks do not trigger contact effects.
Tangency counts as contact, so falling or sliding along the field continues
to pulse without requiring input into the wall; each new pulse follows the
current contact position. Bullet impacts remain independent within the
existing pool limit. A two-second `Force field:`
log reports contact requests, new instances and live contact/total ripple counts.

Force-field collision pushes along the barrier normal rather than lifting the
foot sphere around a triangle edge. Force-field faces never count as walkable
ground, preventing false landings and their associated movement effects.
The Release build with these fixes passed; an in-game FPS comparison is still
pending. No automated tests were added or run.

The original uses its `effopt` refraction path with the shader sphere at
the contact point and radius zero (`0x071869`, `0x0c05c2`). The port draws
the extracted meshes through its existing scene-copy refraction pass;
the shape, timing, scale and refraction settings remain editable in the
extracted table. No replacement mesh or ripple texture is generated.

## Secondary particles and depth placement

Fragment emitters now make eight fixed-position copies, sharing the seed's
curves and lifetime (0x060b7c). The seed occupies step 1 and copies occupy
steps 2 through 9. Each copy has independent transverse jitter scaled by
`0.1 * push`; it does not accumulate the preceding copy's position error.
Its streak vector uses the preceding jitter plus the step vector. These
formulas come from 0x061755–0x061b78. An emitter given a segment
(`EffectSystem::set_segment`, the second row of the frame 0x065058 sets)
shares that segment out among the seed and its copies, so the chain spans
it; others step along their orientation. The electric arcs use it (see
Knocked-off mounts below).
Swirl uses the birth-time sinusoidal wind weight recovered from 0x063c16.

Debris settles while lifetime/colour curves continue. As in the original
0x062115 it stops on flat ground at y=0.05 and clears spin, without a
bounce. `debris_stage_contacts` settles it on the stage's own surfaces
instead, where restitution/friction can give it the previous bounce.
Restitution, friction, clearance, rest speed,
fragment count, spacing and jitter are editable under `particle_motion` in
the extracted effects table. A system without stage collision uses y=0.

The large values added to position.w in 0x0bf4cf are kind/sort tags removed
by 0x0bfae4, not world-space shifts. The renderer applies the actual physical
view-depth biases: kind 1 moves one unit toward the eye and kind 9 two units.
Existing cluster view-space expansion remains separate.

### Particle placement corrections

- `inherit_velocity` is a historical flag name. At 0x061475–0x0614ea it
  subtracts a random fraction of emitter displacement from particle
  **position**, not velocity. Births now fill the moving emitter's path
  without acquiring an unintended persistent speed or stretched streak.
- Cluster children rotate (0,0,-reach) about Y by acos(rand(0.1,1)), then
  about Z by rand(-pi,pi) (0x063aad). The former X/Y rotations biased the
  cloud instead of distributing it around the view-facing hemisphere.
  Matrix routines were identified by their entries: 0x0dcabd=X,
  0x0dcb37=Y, 0x0dcbb2=Z. These names are researcher-assigned.
- `random_direction` and `random_offset` compose Z then Y, matching
  0x06119e–0x0611cb and 0x0612b5–0x0612e2. The latter also uses a shared
  per-emitter anchor at 20–100% of `push` (0x06039a), then per-particle
  scatter of 30% of `push`. That anchor is separate from the native
  world-space attraction target.

Runtime diagnostics report live particle/cluster/fragment/resting counts,
maximum sprite radius and speed per effect frame at two-second intervals.
Rendering, including TV cameras, does not advance these clocks. Startup
logs also report decal/smoke texture availability and trail sampling cadence.

Validation (2026-09-30): the standard Release build succeeded. A bounded
1,800-frame rental-match run with movement and firing exited normally,
exercised robot/scenery destruction, and reported the 30 Hz effect clock.
Fourteen diagnostic samples included up to 170 live cluster particles and
226 motion-sampled particles. No fragment or resting-debris particles were
observed in those samples; those paths remain source/build checked only.
No tests were added or run, and no captures or temporary overrides were
created for this pass. Visual approval remains with the user.

## Knocked-off mounts

A critical hit that knocks off a weapon (0x08d686) puts its mount (robot
+0x8c + slot * 4) in state 1 for 15 ticks; 0x07973f then plays it a tick at
a time, and `DesktopMountEffects` follows it:

1. Sparks (group 38) at the arm's forearm node (robot +0x2fdc / +0x2fe0,
   bone 1 of the arm skeleton) in three lanes, and an arc (39) from its
   shoulder joint (+0x2fe4 / +0x2fe8, bone 0) 0.7 of the way to the
   forearm, starting within 0.1 of the joint.
2. The lanes stop; a burst (42) starts where the weapon was, with 43 and 44
   following it there (0x086e91). A shoulder weapon's burst is at its mount.
3. Until 43 and 44 have played out.
4. An arm's stump keeps going for the rest of the battle: sparks halfway
   between the joint and forearm, and at the joint, the arc 39, side arcs (41)
   from the joint along its x axis, scattered 0.8 and 0.3–0.7 long, re-chosen
   every tick (the left arm's point the other way), and a trail (40) from the joint
   while the robot is slower than 350 km/h (0x19b588). A lane that has
   played out waits a random number of ticks (0x2326e0, plus 5) and starts
   again. A wreck stops them all.

The arcs' segment is the frame's second row (0x087063, 0x065058).
In state 4, the original places the two persistent forearm spark lanes at
`(joint + forearm) * 0.5`, recomputed from the current pose each battle tick.
Focused Ghidra inspection and a radare2 disassembly cross-check of the European
XBE resolve the call chain at `0x07a304`: `0x052b40` adds the two world-space
positions and `0x065640` scales their sum by 0.5. These are researcher-assigned
meanings, not original source symbols. This is midpoint placement, not a
velocity or drift multiplier. The port now follows that placement for both
arms, including when the spark lanes restart. The initial state-1 sparks stay
at the forearm; the shoulder-joint sparks, arcs and trail retain their own
anchors.

## Emitter creation

Emitter creation follows the original: 0x060107 sets the position and the
previous position to the definition's offset (+0xd0), and 0x05fda8 then
sets only the position (0x064c4b), so the first frame's emitter motion
runs from the offset and births that inherit it spread along that path.
`particle_motion.start_at_offset` set to false starts emitters still at
the spawn point instead.

## Robot movement

Jump, downward thrust, dodge, dash and foot contact effects are connected to
animated robot attachments. See [ROBOT_MOVEMENT_EFFECTS.md](ROBOT_MOVEMENT_EFFECTS.md)
for controls, extracted mount data, original handlers and validation limits.
