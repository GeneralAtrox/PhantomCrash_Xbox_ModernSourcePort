# S.L.A.I. research and native integration

## Current asset integration (2026-10-07)

The port now registers the converted parts, cosmetics and arenas. S.L.A.I.
content is enabled through **Settings > S.L.A.I. content (restart required)**,
off by default. The installer converts the user's ISO to `artifacts/slai/assets`;
the ISO is required only for installation. Base and S.L.A.I. modes keep separate
campaign saves. Legacy S.L.A.I. `mods/` overlays stay on disk for research but
are no longer automatically loaded by the game. Its story, menus, scripts, movies and
progression systems are outside this import. Source is the user's PAL disc,
`SLES_529.40`. Source filenames and message names are shipped data; semantic
field labels and native adapter parameters are researcher-assigned.

Implemented in the native port:

- 140 source part models: 47 arms, 48 shoulders, 35 legs and 10 bodies.
  This includes four Sweeper parts missed by the earlier filename inventory.
  These have normal shop records, compatibility, assembly previews and battle
  resources. The earlier Twin Missile prototype remains a separate entry.
  Shop/stock/assembly/tuning list names are bounded to their available column
  using the font's actual widths; overflowing names end in an ellipsis. Full
  names remain in the content tables, and a list can set `name.width` explicitly.
- 58 purchasable paints, 230 purchasable stickers and source body decal
  carriers where available. Paints apply to each imported part independently,
  including mixed Phantom Crash/S.L.A.I. assemblies. No extra wearable
  accessory category has been confirmed.
- Original hierarchies and PMT clips for all 35 leg models adapted to the
  native motion bank. The 14 wheel/roller sets retain native driving.
  Wheel meshes remain local to their original controls instead of having
  their complete parent transforms baked into independent rolling meshes.
  The compiled node callbacks now identify all 71 single-axis rolling mesh
  controls across 19 leg sets, including hybrid and boss legs. Geometry-based
  selection previously spun nearby non-wheel meshes and missed some rollers.
  Authored leaf scale is retained on Photon wheel nodes 27/34 (`S1L04H`)
  and Aeron walking node 18 (`S2L06H`), with inverse-scale normals. Aeron's
  spherical callback replaces its bind basis; playback cancels that baked
  scale after the first callback while retaining the rest geometry before it.
  Bodies and weapons now retain their complete exported hierarchies and play
  their original glTF position/quaternion/scale channels. Body motions follow
  the native locomotion slot and weapons play their source clip on firing.
  All 35 compiled body motion banks are exported separately and selected by
  the legs authored with that package. Sixteen use motion prefixes different
  from their body mesh name, including Photon banks using Holy animations.
  The original 59-action filename tables drive walk, reverse walk, dash,
  reverse dash and idle selection in both leg and body playback. Other native
  movement states retain the numbered-filename adapter pending investigation.
  Imported body arm weapons use compiled hand controls (native right/left
  nodes 5/3 for makers 1–8). The drone body has one enabled control, node 2;
  its other arm attachment and both Sweeper arm attachments are disabled in
  the source table. Disabled entries retain native mounting for mixed parts.
  Imported shoulder weapons now follow their compiled animated body controls.
  The static shoulder-to-body root offset is cancelled when those controls
  supply the mount and retained for a Phantom Crash body. The leg torso
  mount is a child of source control 1, so both the body and adjacent leg
  geometry follow that control's clip-specific position and rotation. The
  former separate rest-pose mount could leave the body behind during leg
  animation. Mixed-part mounting, gameplay muzzle
  sockets and aiming still use Phantom Crash adapters.
  Native body/weapon and arena adapters reverse the canonical export's triangle winding
  for the port's front-face convention, accounting for the reflected right
  mount. Original normals and animation keys are unchanged. Both sides are
  checked against their normals through the native model loader; two-sided
  materials alone do not establish correct exterior lighting.
- 15 selectable arenas in Test Drive and VS: seven day cities, six night
  cities, Practice and the Animation Room. Models, dedicated collision,
  source skies, fog, ambient/sun colors and 309 prop placements are loaded.
  Source sun rotations and all 273 local light volumes are applied. Objects
  receive the nearest two in-range source lights, using the source quadratic
  falloff and nearest points on point, line or rectangle volumes.
  Human-readable labels and scrolling selectors support the longer list.
  Starts are derived from standing collision surfaces, rather than recovered
  original spawn positions. DRUMCAN has no standing collision and is excluded.
- Source texture ordering recovered from compiled battle descriptors for
  102 models. All registered arena material images resolve. Prop resources
  are decoded from the original model/texture selector.
- 73 source sequences rendered to stereo WAV: 46 base fire cues cover 85 imported
  weapon models, with ten source-silent models. This includes held sequences
  209, 228 and 247, started/moved/stopped with the trigger. Robot and surface
  impacts have 190 rules across all 95 weapon models, including the source
  generic surface-hit probabilities. Cue and impact selectors are recovered
  from executable code; cues are not assigned by filename adjacency.
  Six looped cues (209/210, 228/229 and 247/248) use live sample sequences
  instead of repeating their preview WAVs. Their eight authored sample layers
  retain sample-loop points, delayed notes, note-offs and the sequence repeat
  region. ADSR runs at the native 48 kHz register-adapter clock, independently
  of host output rate. Trigger release preserves each sample's current phase
  and envelope; detached tails can overlap the next trigger. Ten paired-impact
  rules start once, follow their contact and stop with the originating mount.
  Five additional source groups (166, 192, 197, 202 and 235) bind their original
  start, repeating layer and ending cues across 13 imported weapons. They share
  one pair across matching mounts on each robot, query actual keyed mixer
  playback, and play the ending when the last matching mount stops firing.
  Group 197 starts both layers together; the other groups start the repeating
  layer while the first is playing. All 15 group members use live samples and
  ADSR, bringing the total to 21 live sequences. Three repeating members retain
  all 420 authored notes each; playback schedules notes by their source times.
- Fifteen registered ballistic weapons now draw their source grenade/cannon
  projectile models instead of their inherited Phantom Crash projectile looks.
  The executable's flight 1/5 branches select those models; their basis scale
  multipliers are 0.03/0.018. Native copies reverse canonical glTF winding for
  outward native fronts, retaining the canonical files and their original
  hierarchy, materials and normals. Flight direction and simulation timing
  still come from native battle. Other flight branches retain existing adapters.
- Native PTK particles use the original shared 60-resource pool
  and 95 source weapon impact mappings. Birth limits, emission delays/stops,
  speed/lifetime spreads, gravity/drag, size/color/alpha curves and spin are
  interpreted in source ticks. Generator activation now suppresses the 12
  inactive entries; 164 generators are enabled. Of those, the source defines
  155 billboards and nine world quads. The native birth sampler now follows
  the source shape/direction/reference rules, angle order and offset conversion.
  World quads use those birth bases on the source X/Z plane; billboards continue
  to use the camera basis. The particle engine now transports local positions
  and world-quad bases through a live or frozen parent frame and implements
  source inertia updates. Moving impacts and fire-gated ASB effects use the
  frame path. Fire-gated effects follow animated weapon joints and source
  attachment masks, with displacement and grounded state supplied by native battle.
  Action and impact effects share the source-sized 64-entry table. Stopped
  tails retain entries, and overflow follows the recovered replacement policy.
  Authored rotation ranges are wrapped in radians before symmetric birth
  sampling, and spin variation is sampled on every update. Source, alpha,
  additive and subtractive blending
  are supported, with zero-alpha rejection and generator depth-test flags.
  Horizontal texture strips now select cells using the source timed, random
  or lifetime-curve controls. The shared pool has 21 multi-frame generators,
  including 20 timed strips. Authored alpha is halved before GS submission;
  the native scale now follows that read instead of doubling half-opacity values.
  Held impacts stop emitting on release while particles finish.
- The three remaining texture bindings are resolved: `ETC_SHLD_00_0C` uses
  `SHIELD_01`, `S_LIGHT_01` uses `S_LIGHT01`, and the generic prop table binds
  `JET_CONT` to `JET01`. Dropped supply crates now use the ten source image
  banks selected by pickup kind, with the source draw scale 2.5. Each imported
  arena has an editable `supply_pickups` asset setting. Rewards, movement and
  collection remain the port's existing supply data.
- 90 named songs converted to Ogg Vorbis, sold by Sonic Amp for its existing
  editable 200 NY price, and available through the garage radio/BATTLE MUSIC.
  FFmpeg streaming retains WAV, looping and spectrum support. Credit IDs
  preserve the original 78 purchasable songs, exclude special credits 78–80,
  and append imported songs at 81–170.

## Preserved assets and remaining native work

`mods/slai/assets/` contains glTF, PNG, WAV, MIDI and editable JSON. The export
has 341 primary models plus 52 distinct package-qualified model variants,
supplemented by 18 package-owned body animation-bank glTFs,
9,383 primary textures plus 167 variants, 3,018 distinct motion payloads from
3,083 references, 7,914 part-paint references and 16 source BG descriptions.
The exporter now also preserves all 128 compiled ASB action banks (1,243
640-byte records), including editable trigger fields and all 2,233 active
effect descriptors. Unresolved words are preserved. Their package-qualified JSON lives
under `slai/assets/actions/`; registration exports these for existing local
asset libraries as well as a fresh conversion.
Identically named day/night assets retain their package ownership. Original
rigs, bind matrices, animation keys, attachment directives, lighting records,
effect actions and unresolved words remain available in open formats.

Native integration is incomplete in these specific areas:

- Body playback selects the bank associated with the assembled legs. Complete
  original action selection and original clock cadence are not reproduced.
  Mixed-part mounts, walking
  motion timing and weapon clip selection use explicit native adapters.
  Original wheel steering producers are not reproduced. Source
  control hierarchies and PMT poses now play; rolling node selection uses
  compiled callbacks. Single-axis roll angles now use the compiled per-model
  coefficients, separate accumulator groups, travel and signed turning terms;
  movement inputs and untraced motion selection remain native adapters. Source
  actions outside the seven mapped actor states are preserved but their
  original gameplay producers are not connected. No selected rolling mesh also has PMT keys in
  the source leg banks, so roll does not duplicate an authored spin channel.
  Aeron's spherical callback now accumulates a matrix in X/Y/Z order,
  replaces the control basis and preserves its world position. Native travel,
  forward control and heading changes still adapt the original input producers.
  The boss suspension callbacks now play their recovered eight-step world-Y cycle
  on `S7L01H` controls 29/30. Their phases start at 4/0; offsets use
  the source-to-native world-Y conversion, independently of mesh units. The cycle
  advances on the native 60 Hz clock, independently of clip speed and action
  changes; original callback cadence remains unverified.
  Fire-gated ASB records now dispatch natively and bind to original animated
  weapon nodes. Primary leg-motion and special death effects now dispatch too;
  part-state effects consume a native loss transition. Original secondary/condition
  gates and the source part-loss/clock producers remain incomplete.
- The reported arena scale error was traced to the part resource loader.
  Its six-way switch at `0x41eca0` sends slot zero (legs) to `0x2526c0`;
  the other five entries bypass root scaling. Instructions at
  `0x2526dc..252704` construct float `0x3dcccccd` (0.1) for all three axes
  and call `0x1ca8d0`. Attached body and weapon slots inherit this scale.
  Battle construction at `0x4dc7bc..4dc7d4` restores these loaded part records
  and the subsequent assembly uses their compiled attachment descriptors.
  Native robot exports already bake 0.1 into mesh vertices and PMT positions,
  so source world lengths convert by export scale / original part-root scale,
  approximately 1.0. Applying the mesh export factor to worlds made arenas
  ten times too small relative to robots. Native stage roots now restore that
  factor for geometry and props; collision, fog and light volumes use the same
  world units. All 16 `MODEL_SCALE` and 101 `PARTS_SCALE` directives remain 1.0.
  Germany's standing collision now spans about 406.8 by 424.3 native units.
  Source supply and ballistic models also restore world units before their
  original runtime scales (2.5 and 0.03/0.018 respectively).
  Native starts are sampled from standing collision, avoiding building roofs,
  walls and blocked camera approaches while recognizing broad arena ceilings.
  These starts are adapters, not recovered original spawn tables. PTK lengths,
  wheel displacement and suspension world offsets now also consume this world
  conversion. Full-frame particle attachments reconstruct the original model
  basis scale; masked Euler/translation attachments retain their source mask
  behavior. Mesh and PMT coordinates continue to use their separate export factor.
- Model images resolve, but source culling/blending details and sky
  composition still need visual review. Source supply flight routes, the carrier,
  rotating night searchlight and kind-indexed pulsing crates are now integrated;
  see the supply section below. Original banking, cargo-release/wreck producers,
  additional path-header inputs and visibility timing remain incomplete.
- All 61 PTK assets are preserved losslessly. The native clock is an editable
  60 Hz adapter; source timestep 1 is verified at `0x19a518`. Target-translation
  impact groups now follow the struck robot, including stopped emitters with
  live particles. Live/frozen parent-frame transport and inertia math are
  implemented in the particle engine. Fire-gated ASB effects use source
  attachment-mask composition, actual displacement and grounded state;
  original collision-mask suppression and other action gates remain pending. Billboard
  and fixed-plane rotation now follow their separate source branches, including
  camera-projected velocity orientation. No positive-capacity/rate generator in
  the shared pool requests velocity alignment; controlled renderer tests cover
  that branch. Impact resources 45/53/55 now use the target leg control-zero frame
  and mask `0x380c`, with an explicit native world-hit coordinate adapter. Full GS
  material/sampler state, integer color quantization and submission ordering
  are not yet reproduced. Original muzzle/trail action selection and projectile
  flight branches other than the grenade/cannon model bindings remain pending.
- Sony NRPN markers in cues 209/228/247 are loop controls, not modulation.
  The native effect mixer now plays their authored samples and envelopes live,
  including paired impacts, robot-shared start/repeat/ending groups and
  overlapping release tails. Paired cues start/advance on accepted native
  shots, while held matching mounts retain them between shots. The source
  sustained fire/contact groups remain keyed until the live beam completes
  its release fade; their starts also require accepted native shots. The source
  tests weapon-runtime ammunition float `+0xd8` at `0x511148` (nonnegative
  for the current accepted shot) and `0x5114bc` (positive for a supporting
  held mount). This field is ammunition, not charge. Cue 177's
  robot-byte `+0x19c` gate is also pending. Finite/nested sequence repeats are
  rejected rather than silently treated as infinite loops.
  Rendering uses source samples/splits/envelopes with native resampling and
  panning; hardware reverb and LFO are not reproduced. The source sequence-stop
  fade argument (cue byte +7, value 10 for these cues) still needs its IOP units
  and interaction with key-off recovered.
- No additional accessories were confirmed. The earlier supply-crate icon
  candidate is not treated as a wearable part.

Sony extraction contains 51 SQ/HD/BD banks, 186 mono WAV samples and 275
standard MIDI sequences, plus program/instrument/sample-set JSON. Original
Sony note-off/compressed-delta conventions are normalized to standard MIDI.
The global cue table has 256 entries at `0x3085f0`; weapon and base-fire
selectors are at `0x507cb0` and `0x50abf0`. Source cue shorts remain in JSON.

PTK field reads were traced through loader `0x19ddb0`, birth `0x198d80`,
update `0x199750`, emission `0x19c4f0` and GS blend setup `0x19cbf8`.
Blend code 3 writes GS ALPHA `0x42` (destination minus source times source
alpha); it does not retain the previous blend state. Generator flag `0x10`
selects depth testing at `0x19cba0`; both paths test alpha greater than zero.
Sun rotations are loaded at `0x51affc`/`0x51b024`, and local volumes at
`0x51b0a8`. Nearest-light selection and quadratic falloff are recovered from
`0x1b4b80`, `0x1cabb0`, `0x1cad00` and `0x1caed8`.
Texture dimensions are copied into the emitter at `0x19b45c`; birth `0x198de0`
uses width/height for the horizontal strip count. Atlas timing is at
`0x199d58` through `0x199e74`, with twice the source timestep subtracted from
the frame timer. Period and lifetime curve come from parameter offsets
`+0x8c` and `+0xcc`. UV submission at `0x19d2e8` divides U by the cell count.
Textured RGB is halved at `0x19d230/268/2a0`; authored alpha is halved at
`0x199cd8` and submitted unchanged at `0x19d2d8`.
Generator flags are copied at `0x19b420`; the update at `0x19c1dc` tests raw
flag 1 and returns before emission when it is unset. Of 176 positive-capacity/
rate shared generators, 164 are enabled and 12 are inactive. Billboard flag
`0x10000` selects the camera frame at `0x19cf80`; the other branch at
`0x19d170` through `0x19d1c4` uses a local X rotation by pi and the particle's
world frame. Component writes in `0x10ec70` identify an X rotation; `0x10ed18`
is a Y rotation, so the non-billboard spin is about Y, not Z. The birth routine
at `0x198f10` builds particle matrix `+0x70` using direction/reference vectors:
`0x1994c8` through `0x1994f0` constructs the perpendicular axes, while
`0x199500` through `0x199520` copies the emitter matrix when the direction/
velocity alignment is within +/-0.00001. `0x10e898` computes a dot product,
not a vector length. `0x10ea20` adds spawn position and direction before the
basis is normalized; it is not the subtraction helper at `0x10ea38`.
The render frame is therefore not simply the PTK generator's angle vector.
The native sampler and world-quad path now implement these standalone birth
rules; parent/attachment frame composition remains pending. The source
range helper `0x19e570` evaluates `fmod(range + pi, 2*pi) - pi`; birth at
`0x198e60` samples both signs. Spin base/spread are converted from degrees at
`0x19e5c0`, variation is sampled at `0x199e94` through `0x199eac` on every
update, and the live angle is wrapped at `0x199ec8` through `0x199f0c`.
The birth parameter cache applies the angle at `+0x44` with the X helper
at `0x199388`, then the angle at `+0x40` with the Y helper at `0x199398` to
spawn position, direction and reference vectors. These operations are
distinct from the parent-effect Euler frame at `0x19c298` through `0x19c2b8`
(Z, then X, then Y). These paths remain distinct in the native sampler;
raw PTK angle vectors alone do not identify the final render frame. The
third cached birth angle is unused by this branch and zero in the shared pool.
The cache flips offset Y/Z at `0x19df4c` through `0x19df74`, and the offset is
added after rotation at `0x199448` through `0x199478`. Ellipse/ellipsoid
positions use cosine for X and sine for Z; the identified cosine helper is
`0x12c948`, and sine is `0x12cb98`. Their near-zero kernels return 1 and the
argument, respectively. Filled radii use an absolute 0.1 bound, not 10 percent
of each authored dimension. Shape codes 3 and 4 reverse their launch vectors.
These helper names are semantic identifications, not original source symbols.

Submitted corner vectors at `0x2b9b10` through `0x2b9b40` are
`(-.5,0,-.5)`, `(.5,0,-.5)`, `(-.5,0,.5)` and `(.5,0,.5)`; their UVs at
`0x2b9b60` through `0x2b9b90` put V=0 on negative Z. The source non-billboard
X rotation by pi therefore makes its top axis the birth frame's positive Z.
The native sprite vertex builder consumes the recovered right/top axes and
spin directly, including degenerate fixed bases, and replay retains them.

Particle inertia at `0x19a010` reads an external movement-vector pointer at
particle `+0x4`, multiplies it by the weight at `+0x50`, can flatten Y through
the byte pointer at `+0x8`, and suppresses it when the flags pointed to by
`+0xc` contain `0x10`. It then scales by 0.8 and decays the weight by `+0x54`.
The caller at `0x19c3b0` uses the birth frame when parent-effect flags `+0x18`
contain `0x300`; otherwise it passes the current emitter frame and parent byte
`+0x7c` controls local-to-world composition. Generator PTK flags must not be
substituted for parent state. The ASB effect constructor's inertia producers
are identified and connected for fire-gated weapon records. Native displacement
and grounded state supply movement and vertical suppression; collision-mask
bit `0x10` remains unbound.
The native `SourceParticleParent` interface takes an effective attachment frame
in native coordinates and displacement per source tick. Particles retain local
positions and quad bases, so live parent orientation transforms both together.
Flags `0x300` choose each particle's frozen birth frame; flag `0x200` captures
its inertia weight and decay. Updates flatten movement Y only for gate value 1,
suppress movement for state bit `0x10`, multiply by 0.8, and decay the weight
without clamping even during suppression. Frozen-frame transport accumulates
independently for each particle. This implements the recovered update math;
the fire-gated action adapter also composes recovered model attachment masks.
Other robot-state producers remain incomplete.
Impact constructors `0x508910` and `0x508bb0` select parent flags `0x400` for
resources 18/19/20/28/30/38/40/42/44/54 when supplied a struck model. The branch
at `0x19c0b8..198` retains the world offset and follows target translation with
an identity parent frame. Collision kind 4 supplies that owner through
`0x501e20`, `0x2242c0` and object `+0x1a4`; surface branches have no robot owner.
The native landing/event path now carries that target through projectiles,
strikes and beams. These rules cover 87 of the 95 registered weapon mappings.
Unique one-shot keys preserve independent impacts, and held groups retain
their owner until their particles expire. Resources 45/53/55 use the other
parent frame. With a target, constructor `0x508910` writes
mask `0x380c` at `0x508a88..94`; the impact dispatcher supplies parent node zero
at `0x50fef8`. `0x19bf54..64` obtains that node's matrix through `0x1b3c80`,
which copies node `+0x50`. The struck actor's model pointer comes from actor
`+0x1a4` at `0x501e28`, the first part record (legs). Native playback now preserves
the imported leg control zero separately from the torso bone, including when
that control has no geometry. It normalizes the source mask's Euler basis,
rebases the native world hit once into that frame and subsequently composes
the retained local hit with the current target frame. The original collision
producer's coordinates remain unverified; the inverse conversion is explicitly
a native input adapter. Phantom Crash targets use their actor root as the mixed-game
adapter. Stopped emitters retain attachment updates while their particles live.
Constructor resource 27 has the translation
rule but is not selected by the imported impact table.

The compiled ASB files were a missing export in the earlier inventory. All
128 have a two-byte count followed by exactly count times 640 bytes. Runtime
`0x50ebe0..ec38` passes record `+0x80` to selector `0x508210`; constructor
`0x508250` consumes that effect descriptor. The readable labels below are
researcher-assigned; remaining record words are retained without interpretation.

| First descriptor's ASB record offset | Recovered effect field | Source read |
|---|---|---|
| `+0x80` | Signed part-selector byte | Source weapon slot fixup at `0x50cf80..cfc0`; slots 0/1/2/3 correspond to selectors 3/2/5/4 |
| `+0x81`, `+0x82` | Normal/alternate signed particle IDs; -1 suppresses | `0x508210..244`; alternate selected for runtime word 0x80 or 0x100 |
| `+0x84` | Signed parent node | `0x508750..75c` |
| `+0x88` | Effect scale | `0x508738..740` |
| `+0x8c` | Parent flags | `0x5087dc..7ec` |
| `+0x9c`, `+0xa0` | Inertia weight/decay | `0x50881c..830` |
| `+0xa4` | Front-show/camera bias | `0x508838..840` |
| `+0xa8` | Position offset, three floats | `0x508778..7a4`; special resource paths can override it |
| `+0xb8`, `+0xb9` | Signed recycling priority and secondary priority | Constructor `0x5088b0` stores a pointer to these bytes; replacement reads them at `0x509358..3c8` |

At `0x5087f8..818`, the constructor supplies robot movement `+0xe0`, byte
`+0xb61` and flags `+0xdf0` to effect fields `+0xbc/+0xc0/+0xc4`. Birth copies
these pointers and authored weight/decay at `0x19c6f4..730` when parent flags
contain `0x200`. Flags `0x300` select a frozen birth frame at `0x19c658`; 789
of the 2,233 active ASB effect descriptors request that frame and 738 contain
the inertia flag. Inertia's
vertical suppression requires the pointed-to byte to equal 1. Weight decays
without a zero clamp, even while movement is suppressed. Its translation
adjusts the stored parent frame before local particle/world-frame composition.
The parent mask at `0x19bac0` normalizes the supplied model basis, recovers
Euler angles and selectively applies Z/X/Y through flags `0x2000/0x800/0x1000`.
Byte `+0xb61` is grounded state: ascent clears it at `0x4e1d40..74`, and
standing contact sets it at `0x4e3ff4..405c`. Word `+0xdf0` is a collision
result mask; the meaning of its inertia-suppression bit `0x10` remains unresolved.

ASB round trips preserve every original byte and SHA-256, including unused
fields and disabled particle IDs. Editing decoded effect values changes only
their corresponding fields. The Photon handgun's smoke scale, weight and
decay independently agree with its shipped `SHOT_END_SMOKE.ACT`. Native
playback now connects fire-gated records to the parent/inertia engine and
animated source weapon nodes. Native fire events and held-trigger/blade state
supply request and stop gates; original motion/state gates are not connected.
The dispatcher at `0x50e800..50f1e8` walks 640-byte records. Each record has
up to eight 60-byte effect descriptors at `+0x80`, with the active count at
`+0x260`; `0x50ef68..50f1c0` walks all of them. The earlier decoder exposed
only the first descriptor while preserving the others as raw words. All
2,233 active descriptors are now decoded and writable, including the 990
additional descriptors, without changing any original bank hash.
Trigger fields also expose flags, signed motion-ID lists, frame thresholds,
weapon masks/delays, speed thresholds and raw condition/state selectors.
Its weapon gates
reset per-record counters at `+0x26c` from signed delays at `+0x2c`, decrement
them in the same update and activate on reaching zero. Weapon runtime float
`+0xd8` also participates in those gates. At `0x50cf58..50d03c`, the loader
replaces every active weapon effect's part selector with 3/2/5/4 for slots
0/1/2/3, replaces only placeholder `0xff` weapon masks with
`0x40/0x80/0x100/0x200`, and sets the weapon-state selector at `+0x3f` to that
slot. `bind_weapon_action_bank` implements those fixups on a copy of the bank.
At `0x509aec..509b14`, the loader calls weapon selector `0x507cb0` and indexes
the 95-name table at `0x59ce3c`. All 95 imported weapon models now have a
`weapon_actions` route using that exact shipped bank name, without filename
similarity matching. The table preserves the name-entry and loader addresses.
Across all exported banks there are 89 weapon-gated action records. The 95
registered weapon routes select 86 records with flags `0x40/0x60`, containing
175 active effect descriptors. These now play with the original signed delays,
same-update countdown and last-handle suppression. All their alternate IDs
equal their normal IDs and their front-show values are zero. Effects resolve
their parent node through the animated glTF hierarchy, apply normalized Z/X/Y
attachment masks or the full-matrix override, and retain source scale/offsets.
The blade resource also applies its constructor's quarter-turn. Movement uses
actual position displacement per source tick; grounded state supplies Y suppression.
Native firing/blade state stops held emitters while their particles finish.
Original charge/alternate gates, collision-mask suppression, special boss stop
rules, motion-gated selection and
original clock cadence remain pending. Existing muzzle/trail adapters still run.
The singleton returned by `0x506e30` owns 64 effect entries, each 36 bytes.
Both action constructor `0x508250` and impact constructor `0x508910` first use
`0x509130` to find the first free entry, including entry zero. At capacity,
`0x509290` scans entries 1 through 63 for a replacement. Resources 26/27,
31/32/33 and 58/59 are protected by `0x509240`; eligible prioritized actions
are compared by signed primary priority, secondary priority, then age. All
2,233 shipped ASB descriptors have zero priority bytes, so they cannot be
chosen as prioritized replacements. An arriving protected resource can instead
replace the oldest eligible impact without an ASB priority pointer. Ordinary
arrivals are rejected when no prioritized replacement was selected.
The native particle system now shares those 64 slots across imported action
and impact effects, counting effect groups rather than their generators.
Stopped tails retain their slots until every generator retires. Replacement
removes the complete selected group, following the immediate release at
`0x509740`. Ages advance once per source update except resource 58, as at
`0x5080b8..dc`. Native group handles remain an adapter; original pointer aliasing
after recycling is not reproduced.
Serialized editor addresses are preserved data, never executed. The shared
resource table is at `0x59be30`, loaded by `0x507160`. Weapon record byte
`+0x1d` is read at `0x501c80` and passed to impact selector `0x50f9f0`.
Its branches select both the cue and PTK. The generic surface branch uses
55/15/30 percent weights for cues 57/58/59; robot impacts use cue 72.
Curve branches were traced at `0x199550` through table `0x34bf60`.
Texture pairs come from loaders `0x50743c`/`0x507484`; the generic prop
table's final model/image rows pair `JET_CONT`/`JET01`.
The supply "counter" is the crate's reward image bank. Its loader reads
`jet_cont.po2` at `0x5b2650` and ten image paths, 64 bytes apart, at `0x5b2690`.
At `0x522e54..64`, pickup kind directly selects image bank `kind*4` from the
model bank's following pointer slots. This is not a numeric countdown or a
filename-sorted atlas. The bank order is `cy_100`, `cy_1000`, `cy_10000`,
`cy_100000`, `ch_10`, `ch_30`, `ch_50`, `cm_030`, `cm_050`, `cm_100`.
These are shipped asset names, not verified reward amounts inferred from them.
The source sets all three model scales to 2.5 at `0x525394..a0`.
`ProgramData.supply_pickup_visuals` exports the model, ordered image paths,
scale and loader/binding provenance into each imported stage's `supply_pickups`.
The native stage parser passes that asset setting to the supply view. Falling
and landed pickups select their per-kind texture on the source model, keeping
its UVs and samplers. Ordinary stages retain their existing pickup art. No
source reward, random-selection or flight-path system is imported by this change.

Weapon sound state at `0x511050` checks whether 209/228/247 already play
before starting them on an accepted shot. These cues now use accepted-shot
events and the live native beam's release fade rather than raw held input;
the source counter and stop wrapper are documented below. Original beam and
acceptance producers remain adapters. The branch
at `0x511270` checks paired cues for 166/192/197/202/235; cue 197 starts both
members initially, while the other groups start the second when the first
is playing at a subsequent accepted shot. When no matching mount remains held
with positive remaining ammunition and the
same base cue, `0x511580` through `0x5115c4` stops both members and plays
base+2. Cue 177 also depends on robot byte `+0x19c`, set by free arm actions
in `0x4e9710`; the native gate now follows accepted free arm attacks (see
the melee integration update below). The five paired branches distinguish accepted native shot
events from held native input and consume post-shot ammunition. Original
weapon acceptance and input producers remain native adapters. `0x2243d0` obtains
the mount's weapon through `0x224360` and `0x1ee470`; the latter returns the
object pointer at weapon-wrapper `+0x10`. The held-pair stop path at `0x50a8b0` reaches
`0x2201b0` through `0x50a670`; `0x220230` through `0x220260` resolves the
default stop argument from cue byte +7. `0x216b40` sends that argument to the
IOP as command `0x31`. Its units and relationship to envelope key-off remain
unverified.

P2MT layout was checked at `0x19fcb0`, `0x19fd00`, `0x19fec0`, `0x19ff40`,
`0x1a05f0` and `0x1a06b0`. Source frame times remain in JSON; glTF previews
use a nominal 60 FPS. No requested texture file or motion conversion failed
in the final output audit.

The 90 named songs total 318.1 minutes: 3.14 GiB as PCM16 or 537.6 MiB as
Ogg in this conversion. Streams 90–118 have no entries in the source song
catalogue; they are excluded, and that alone does not identify them as voice.
Non-ASCII font indices remain in metadata rather than being misread as
Unicode. Titles/artists display their recovered ASCII portions.

## Original shop calendar

`tables/town.json` contains 60 calendar rows. Each selects seven lineup IDs
from `tables/garage.json`: bodies, legs, arms, shoulders, modules, paints
and emblems. Several days share lineups, so editing an original lineup
could affect multiple dates. The converter appends separate copies for each
changed day/category and preserves every original item.

Every day contains 60 original stock variants. A variant is
`(category,id,level)`; there are 553 first appearances across the cycle.
Constant stock size does not mean constant novelty. The calendar table below
distinguishes first-ever appearances from additions relative to yesterday.
Day 1's turnover compares against day 60; its first appearances start from
an empty set. Active venues means contest class is nonzero, not a measurement
of contest difficulty.

Days 29 and 49 have the least turnover (46), followed by day 54 (47), then
13, 17, 19, 53 and 59 (49). Day 25 is the only day with one active venue.
Many later dates have zero first appearances despite rotating stock.

| Day | Stock | First appearances | Added vs yesterday | Active venues |
|---|---:|---:|---:|---:|
| 1 | 60 | 60 | 53 | 3 |
| 2 | 60 | 50 | 50 | 2 |
| 3 | 60 | 43 | 50 | 3 |
| 4 | 60 | 35 | 50 | 3 |
| 5 | 60 | 36 | 50 | 2 |
| 6 | 60 | 35 | 53 | 3 |
| 7 | 60 | 28 | 52 | 3 |
| 8 | 60 | 20 | 56 | 2 |
| 9 | 60 | 14 | 53 | 3 |
| 10 | 60 | 18 | 53 | 2 |
| 11 | 60 | 20 | 56 | 3 |
| 12 | 60 | 19 | 53 | 2 |
| 13 | 60 | 9 | 49 | 3 |
| 14 | 60 | 9 | 50 | 3 |
| 15 | 60 | 17 | 50 | 2 |
| 16 | 60 | 11 | 53 | 3 |
| 17 | 60 | 5 | 49 | 2 |
| 18 | 60 | 10 | 50 | 3 |
| 19 | 60 | 17 | 49 | 2 |
| 20 | 60 | 23 | 53 | 2 |
| 21 | 60 | 17 | 56 | 2 |
| 22 | 60 | 15 | 56 | 3 |
| 23 | 60 | 14 | 52 | 2 |
| 24 | 60 | 5 | 50 | 3 |
| 25 | 60 | 3 | 53 | 1 |
| 26 | 60 | 1 | 53 | 3 |
| 27 | 60 | 0 | 53 | 2 |
| 28 | 60 | 0 | 53 | 3 |
| 29 | 60 | 0 | 46 | 3 |
| 30 | 60 | 1 | 50 | 2 |
| 31 | 60 | 1 | 53 | 3 |
| 32 | 60 | 1 | 56 | 2 |
| 33 | 60 | 0 | 56 | 3 |
| 34 | 60 | 0 | 53 | 3 |
| 35 | 60 | 2 | 53 | 2 |
| 36 | 60 | 0 | 56 | 3 |
| 37 | 60 | 0 | 52 | 3 |
| 38 | 60 | 0 | 53 | 2 |
| 39 | 60 | 0 | 50 | 3 |
| 40 | 60 | 3 | 50 | 2 |
| 41 | 60 | 0 | 53 | 3 |
| 42 | 60 | 0 | 50 | 2 |
| 43 | 60 | 0 | 52 | 3 |
| 44 | 60 | 0 | 53 | 3 |
| 45 | 60 | 1 | 53 | 2 |
| 46 | 60 | 0 | 56 | 3 |
| 47 | 60 | 0 | 52 | 2 |
| 48 | 60 | 0 | 53 | 3 |
| 49 | 60 | 0 | 46 | 3 |
| 50 | 60 | 4 | 50 | 2 |
| 51 | 60 | 0 | 53 | 3 |
| 52 | 60 | 0 | 50 | 3 |
| 53 | 60 | 0 | 49 | 2 |
| 54 | 60 | 0 | 47 | 3 |
| 55 | 60 | 5 | 53 | 2 |
| 56 | 60 | 0 | 56 | 3 |
| 57 | 60 | 0 | 56 | 2 |
| 58 | 60 | 0 | 56 | 3 |
| 59 | 60 | 0 | 49 | 3 |
| 60 | 60 | 1 | 53 | 2 |

## Delivery policy

The overlay has 141 part delivery entries (140 source models and the earlier
Twin Missile) and 288 cosmetic entries across the original 60-day calendar.
It preserves original stock. Introductions are staggered, using lighter days
by bookings, first appearances, active venues and turnover; repeat visits
provide later purchase opportunities. Parts are purchased normally rather
than automatically granted to the garage.

`mods/slai/native_deliveries.json` and `mods/slai/cosmetic_deliveries.json`
store editable availability days. Registration reuses those schedules and
rebuilds overlays from base data, avoiding duplicates. The earlier 15-entry
wheel-only schedule in `deliveries.json` is superseded by the full schedule.

## Original stats and balance

The PAL ELF's selectors identify these tables. Program headers map virtual
addresses to file offsets; instruction checks reject mismatching revisions.
Counts include enemy, special and unused records, rather than only shop parts.

| Category | Table VA | Selector VA | Count | Record bytes |
|---|---|---|---:|---:|
| Bodies | 0x3ca320 | 0x24d210 | 34 | 60 |
| Legs | 0x3cab60 | 0x24d290 | 89 | 208 |
| Arms | 0x3cf480 | 0x24d320 | 101 | 312 |
| Shoulders | 0x3d70d0 | 0x24d3b0 | 106 | 312 |

Weapon direct damage uses the short curve at +0x54 (`0x24dea8`), with the
floor at +0x52 applied by `0x24dec4`. Fire interval is the u16 at +0x20
(`0x24dd54`). The raw export preserves each record address, name index,
maker/model IDs, curves, floors and candidate weapon parameters. Player and
enemy variants stay distinct: the first arm's player damage is 795, while
an enemy version with the same name is 318.

These are level-zero examples by role, not assertions of identical weapons:

| Role | S.L.A.I. | Damage / interval | Phantom Crash | Damage / interval |
|---|---|---:|---|---:|
| Holy arm cannon | LC15 "Tifone" | 2960 / 26 | 65/14M25Barrister | 2400 / 55 |
| Photon shoulder missile | T57 ASV Missile | 2342 / 36 | Single-fire ASVM | 855 / 40 |
| Photon shoulder rocket | T66 Anti-SV Rocket | 3670 / 40 | Single-fire ASVM (missile comparison) | 855 / 40 |
| Photon shoulder grenade | T50 Grenade | 480 / 28 | No.27 Bazooka (lobbed comparison) | 360 / 40 |

The Holy cannon's damage is 23.3% higher, but Holy body armour is 34,000
versus 15,000. Damage/body armour is 8.7% versus 16.0%, excluding legs,
splash and other modifiers. Photon body armour is 27,000 versus 13,000;
its single shoulder missile's normalized direct damage is 8.7% versus 6.6%.
Rocket damage is in the same order of magnitude as Phantom Crash's heavier
weapons, but that does not establish whole-game balance equivalence.

Candidate splash values differ substantially: T66's radius base is 25 and
splash-damage base 2250; the Phantom Crash Photon arm cannon has radius 20
and splash base 360. Interval-to-seconds conversion, splash application,
homing and multi-muzzle shot counting have not been verified across games.
A DPS ratio cannot be inferred from interval alone. The raw export records
`native_units_verified: false`.

The existing Twin Missile retains its earlier Phantom Crash-derived tuning:
900 direct damage, two rounds, interval 50. It is not original S.L.A.I.
tuning. Imported parts use recovered armour, weight, capacity, price
and movement curves under Phantom Crash's load formula. Curve knees 0–100
are scaled to native 0–99. This is an adapter, not recovered S.L.A.I.
simulation or a finished balance pass. Acceleration, grip, braking and
steering multipliers are editable native values in
`src/phantomcrash_research/slai/wheels.json` and generated per-part movement data.

## Commands and verification

Build the standard port with `pwsh -File scripts/build-port.ps1`. Generate
assets first, then register them against the Phantom Crash extraction:

```powershell
$env:PYTHONPATH = 'src'
python -m phantomcrash_research convert-slai-music C:\Projects\SLAI\SLAI.iso --assets artifacts/europe/assets --data data --output mods
python -m phantomcrash_research convert-slai-assets C:\Projects\SLAI\SLAI.iso --output mods
python -m phantomcrash_research register-slai-assets C:\Projects\SLAI\SLAI.iso --assets artifacts/europe/assets --data data --output mods
```

Registration retains wheel resource IDs and native driving settings while
rebuilding their models and motion banks from the original hierarchies.
To replay registration alone, retain the converted sound banks and manifest.
Disc-derived outputs remain local working content and must not be committed.

Verification for this continuation:

- Release port build and focused native test target build succeeded.
- 67 focused Python tests passed for asset formats, progression, audio and
  native adapters. All 61 PTK documents reconstruct their source bytes.
  All 128 ASB banks also reconstruct their original bytes and recorded hashes.
  Action-bank checks cover signed disabled IDs, writable recovered fields,
  preservation of unrelated words, and malformed/truncated extents.
  They also cover all eight descriptors, signed trigger lists, owner fixups
  on both sides of every imported weapon, and complete compiled-bank routing.
  Signed recycling-priority edits preserve every unrelated byte.
  Supply-image checks preserve compiled bank order and reject unsupported source scale code.
  The wheel-converter regression checks local geometry, authored mesh scale,
  normals and attachment hierarchy under a rotated parent control.
- 93 native tests passed, including assembly/resource/finite-pose checks for
  all 140 parts and original walking/wheeled motion banks, paint variants, arena
  selection and standing collision, recovered fire/impact rules, wheels and
  songs. Added checks cover hierarchy-only parents, clip resets, tick-based
  emission/retirement, lifetime curves and held audio start/move/stop. All 35
  body banks select their authored package, all 15 arenas have the native
  winding convention, and every source local light is consumed.
  Body-bank checks sample hierarchy-only hand and shoulder mounts for every
  original clip in all 35 banks and compare the indices with the compiled
  attachment table. A disabled side retains the fallback frame instead of
  borrowing another side's attachment.
  Added checks cover font-width truncation, moving impact anchors, stopped
  particle tails and projectile/strike/beam target ownership versus scenery.
  Parent-frame checks cover live orientation, frozen transport, source inertia
  suppression/decay, negative weights and independent sibling birth frames.
  Added sound checks cover all six looped cues at 44.1/48 kHz, source ADSR,
  release/retrigger overlap, scheduled note boundaries, and paired impacts on
  both mounts. Atlas checks cover all 21 strips, source timer/curve cadence,
  UV cells and authored alpha. Existing mixer, battle audio and armory cases pass.
  Paired-fire checks cover shared mounts, simultaneous startup, actual mixer
  retirement/restart, ending selection, independent robots, lost mounts, all
  13 imported bindings and all 15 source member sequences at 44.1/48 kHz.
  Particle motion checks cover inactive suppression, the 164 enabled
  generators and nine source world-quad flags, wrapped source ranges,
  symmetric birth angles, per-update spin variation and both wrap directions.
  Birth checks cover point direction, pitch/yaw order, offset conversion,
  alignment fallback, shared ellipse azimuth, reversed sphere directions,
  absolute fill bounds and finite bases for all 164 enabled generators.
  Action checks cover countdown/retrigger behavior, permitted gates, last-handle
  suppression, disabled final descriptors, normalized/full parent matrices and
  finite animated parent-node frames for all 95 routes on both mounts.
  Shared-effect-table checks cover 64-entry saturation, first-vacancy reuse,
  ordinary rejection, protected-resource selection, oldest-impact replacement,
  signed priorities, complete multi-generator eviction and stopped-tail retirement.
  Supply checks cover all 15 imported stages, all ten distinct image paths,
  the source crate's texture-zero override, complete kind tables and preservation
  of native rewards and drop timing when applying an asset override.
- Twelve input/replay/vertex tests passed, including round trips of source
  light volumes, subtractive/depth-test sprite state and fixed quad axes.
  The vertex-buffer builder checks source X/Z corners and UVs, Y spin,
  camera-independent world planes, camera-facing billboards and preservation
  of a degenerate fixed plane without adding camera-facing area.
- All 73 rendered cue WAVs have valid stereo PCM16/48 kHz headers and frames;
  all 341 primary model material image bindings resolve.
- Repeated registration produced identical critical overlay tables. All 420
  day/category lineups match original stock plus the saved import schedules.
- Agent-created temporary runtimes and captures were removed and their absence
  verified. The standard build, extraction and imported mods are retained.
- Earlier smoke scenes loaded the particle resources, impact mappings, lights
  and audio, but their loadouts incorrectly supplied robot type IDs where
  shop-part indices were required. Their screenshots therefore do not verify
  imported Photon/Holy assembly, animation or source effect attachments.
  The shop test with intentionally overlong labels remains valid: the ellipsis
  stayed inside the selected row, using changes only in the temporary runtime.
- Corrected 450-frame Germany Day scenes used source Photon body part 16,
  walking legs 57 and handgun 77 or blade 79. Both exited successfully without
  load errors. The handgun started two source action effects and the blade
  one, with zero missing attachment frames in both runs. Widened front-view
  captures were inspected for source parts, external faces and stage geometry.
  The HUD and dark lighting limit inspection of the lower joins; these runs
  establish native attachment/playback activity, not that every mixed assembly,
  particle orientation or the reported arena scale is correct.
- After shared-slot integration, a further corrected 450-frame Photon handgun
  run in Germany Day completed with two source action starts and zero missing
  frames. The capture was inspected; no load errors were reported. Saturation
  and replacement behavior are covered by native tests rather than this scene.
- Ten 180-frame Germany Day fixture runs forced pickup kinds 0 through 9.
  Each completed and submitted its expected source crate image with no load
  errors. Captures were produced, and the kind-zero scene was inspected.
  The fixture paths, camera and drop timing are temporary test settings; they
  do not establish original supply placement, animation or colour timing.
- A torso attachment regression samples the beginning, midpoint and end of
  every available native clip across all 35 leg sets, including a grafted
  Phantom Crash body. Its torso matrix must equal the animated leg attachment
  control matrix. Six complete same-maker assemblies (Photon walking legs and
  wheeled sets from each of the five main makers) rendered successfully and
  were inspected for connected joins and exterior faces. These views do not
  establish correct original motion selection or resolve arena scale.
  Two further renders checked the sets containing all three scaled leaf
  meshes: Photon `S1L04H` wheels and Aeron `S2L06H` walking legs.
- Two 540-frame Photon runs exercised forward, reverse, dash and final idle
  using walking `S1L02H` and wheeled `S1L04H` legs. Both completed without
  load errors or missing playback frames. Captures were inspected for torso
  and arm joins; the HUD limits lower-leg inspection. These runs do not verify
  original clock cadence, wheel steering/suspension or arena scale.
- Two further 420-frame runs used Holy `S3L04H` and Kubel `S5L05H` legs with
  the corrected callback-based rolling selection. Both completed without load
  errors or missing playback frames. Their captures were inspected; the Holy
  view still shows apparent gaps around the arm weapons, so this is not proof
  that every assembly joins correctly. Original body/arm procedural controls
  and aim producers remain relevant to that reported assembly problem.
- After compiled body attachment integration, four 540-frame runs covered
  Holy `S3L04H`, Brineman `S4L04H`, Drone `S9L01H` and Sweeper `SAL01H`.
  Each completed without load errors or missing playback frames. Captures
  were inspected; the Holy idle view shows connected torso/arm/shoulder joins.
  A further 240-frame Holy run held forward input and its inspected view also
  shows connected upper joins. Source hand/shoulder node indices are covered
  across every clip by native tests; these five views do not prove every
  assembly, procedural body/arm aim control or original clock cadence correct.

Compiled motion selection evidence:

- PAL loader `0x250d08..250f98` indexes 372-byte descriptors from the eight
  pointers at `0x333220`. Each descriptor carries a folder, model name, motion
  prefix and 59 signed filename numbers. Disabled entries and aliases are
  preserved in `slai/assets/motion_resources.json`.
- Bodies and legs are paired by shipped folder name. Boss descriptor row
  ordering differs between those two tables, so row ordinal is insufficient.
  Matching folders accounts for all 35 body banks and their required clips.
- Locomotion producers pass the same original action to legs and body via
  `0x4e94a0`. Native walk, reverse walk, dash, reverse dash and idle map to
  original actions 0, 2, 1, 4 and 47, traced at `0x4ea99c`, `0x4ea670`,
  `0x4ea754`, `0x4eac38` and `0x4ea1c4` respectively. Action 47 usually names
  PMT 50; treating native idle slot 28 as filename 29 selected the wrong pose.
  Both registered leg banks and body model metadata consume this lookup.
- Native airborne slot 11 now selects source action 22, passed at
  `0x4ef2cc` to the body/leg dispatcher. Death slot 12 selects source action
  46, passed at `0x4e0b18` after the actor's health check. Import guards
  verify both action-selector instructions. The lookup preserves disabled
  descriptors instead of substituting a filename; both leg and body banks
  consume it across all 35 sets. All 70 newly selected leg clips exist and
  have no Euler-key or non-unit scale-key channels requiring extra playback.
  The rebuilt release port and focused native target passed 99 native tests
  and 72 Python import checks. A 145-frame Holy `S3L02H` jump/re-press test
  rendered without load errors; its capture was inspected. This verifies
  loading and playback, without proving original action transitions or fidelity.
  Temporary runtime, junctions, logs and capture were removed afterward.
- Original jumping has separate anticipation, lift and falling actions:
  20 at `0x4eb6ec`, 21 at `0x4ef1ac`, and 22 at `0x4ef2cc`.
  The lift transition also checks actor counters and animation flags;
  their full producers are not established. Native jump slot 10 currently
  loops through flight, while slot 11 serves fast descent and recovery.
  Mapping slot 10 directly to anticipation would therefore repeat the wrong
  phase. That slot and the complete original jump controller remain native
  adapters. Source actions 43/44/45 are retained; their native braking and
  turning relationships are not yet established.
- Descriptor `+0x88` contains 51 per-node callback pointers. Registration at
  `0x2524c8..252608` passes the node index and its callback to `0x253520`;
  the decoded assignment is exported in `motion_resources.json` and each
  native leg bank. Callbacks `0x4e5ad0`, `0x4e5b60`, `0x4e5bf0` and
  `0x4e5c80` select separate actor accumulators `+0xc10/+0xc14/+0xc18/+0xc1c`
  and the common roll update at `0x4e54b0`. Their 71 assigned nodes are all
  leaf meshes and have no PMT keys in any compiled referenced leg clip.
  Native wheel flags now follow those assignments across all 35 leg sets;
  the former geometric detector and its default manual override are removed.
- The original roll update derives travel from actor positions `+0xc0/+0xd0`
  in the `+0xab0` frame, adds a signed turning contribution, uses leg-kind
  coefficients at `0x59a7f0`, and rotates about the node's existing basis.
  The four accumulators now play with the compiled coefficient pair for each
  of the 35 source leg-model indices. Ground travel comes from native position
  differences in the current heading frame; turning comes from native heading
  changes and airborne roll from the native forward control. These are explicit
  input adapters; full source actor state and callback cadence remain pending.
  Callback `0x4e5d10` handles spherical rolling through its own persistent
  matrix rather than a single-axis flag. Callbacks `0x4e5e70/0x4e5eb0`
  update the boss suspension through `0x4e5da0`.
- Wheel coefficient selection at `0x254890` returns the source leg record's
  signed model index at `+6`; wrappers `0x4e5ad0/5b60/5bf0/5c80` index
  `0x59a7f0` with that value times 8. This is a 35-pair table, not an
  eight-kind table. The final five leg descriptor indices match those model
  indices (`S9L01H`, `SAL01H`, `S6L01H`, `S7L01H`, `S8L01H`), independently
  of the body descriptor order. The first pair member drives accumulators
  `+0xc10/+0xc14`; the second drives `+0xc18/+0xc1c`.
  Each callback increments its shared accumulator before rotating its mesh;
  meshes sharing a group therefore retain separate sampled angles. Export
  writes those groups, coefficients, clamps and source-unit conversion into
  editable skeleton fields for all 71 single-axis rolling meshes. Native
  playback replays descriptor registration order; the original global
  callback registry slot order and cadence are still not reproduced.
  It preserves each mesh's first-row rotation axis and translation, uses
  actual native displacement on the ground, and the forward control with
  native battle tick duration in the air. Original actor frame/turn/control
  producers remain adapters. `0x10e8c0` normalizes its input vector, and
  `0x10e830` multiplies the existing node frame by the axis-angle result;
  VU instruction names were cross-checked using the
  [PCSX2 R5900 opcode tables](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/R5900OpcodeTables.cpp).
  The release port and focused native target rebuilt. All 99 native tests
  and 71 Python import checks passed, including source group ordering,
  separate coefficients, heading conversion, mesh-axis/position preservation,
  clamp/wrap, airborne control and destroyed-actor gating. Holy `S3L06H`
  and Photon `S1L04H` assemblies each completed a 540-frame forward/turn/reverse
  live check with no load errors. Both captures were inspected; this verifies
  those paths without proving original visual fidelity or arena scale.
  Temporary runtimes, junctions, logs and captures were removed afterward.
- Boss suspension initialization at `0x4dcd5c..74` sets actor `+0xb38` to
  4.0 and `+0xb3c` to zero. `0x4e5da0` increments the selected counter,
  wraps at 8, then adds `-0.05 + 0.15 * phase / 4` to world Y through
  phase 4, or `0.10 - 0.15 * (phase - 4) / 4` thereafter. Export converts
  those offsets by the negative world scale for the native Y axis: approximately
  +0.05/-0.15/-0.10 for the low/span/high values. The only registered users
  are `S7L01H` leaf meshes 29/30, with no authored PMT keys in their source
  package. Native playback reads their editable cycle fields after posing,
  preserving world axes and the independent side phases. Pose queries never
  advance the clock; robot reset restores its initial state. The original
  callback cadence remains unverified, so timing explicitly uses native 60 Hz.
  The release port and focused native target rebuilt successfully; all 95
   native tests and 69 Python import checks passed. A 240-frame `S7L01H`
  movement render exited successfully with no load errors. This establishes
  loading and playback, not original visual fidelity. Its temporary runtime,
  junctions, logs and capture were removed after inspection.
- Animation stepping at `0x19eec4..19eee8` multiplies animator speed `+0xe4`
  by context delta `+0x20` before advancing keys through `0x19f520`. This
  identifies the original clock input but does not yet establish its full
  producer cadence or map every native motion speed to the original state.
- Particle drawing at `0x19d120..144` applies the negative stored angle in
  the camera-plane branch, while the fixed world-plane branch at
  `0x19d194..1b0` uses a different basis. Native billboard rotation now uses
  the converted negative angle; authored world-quad rotation retains its
  existing sign. This corrects random orientation on 141 enabled billboard
  generators, including 70 with ongoing spin. Phantom Crash sprites retain
  their existing rotation convention.
  With PTK flag `0x100`, `0x19d054..114` transforms stored particle velocity
  `+0xf0` into camera space, clears Z, normalizes XY and derives a signed angle
  against `(0,-1,0)` at `0x2b9b50`. The sign comes from projected X, and the
  comparison at `0x19d08c..a0` skips the angle only when the projected length
  equals float `0x3727c5ac` (0.00001). Native drawing supplies each view's
  camera axes, preserves source distance units for that guard and uses native
  normalization/trigonometry. It uses the stored local velocity, rather than
  substituting parent displacement. Velocity orientation replaces authored
  billboard spin; on fixed world quads that flag simply suppresses spin.
  The 60-resource shared library contains no enabled emitting generator with
  this flag. Controlled renderer checks cover directions, a rotated/pitched
  camera, depth projection, spin override, the equal-length guard and finite
  zero projection. These checks establish native branch behavior, not PS2
  floating-point parity.
  The release port and rendering/input target rebuilt successfully. All 16
  rendering/input tests and 72 Python import checks passed. A 300-frame Photon
  firing run in Germany Day rendered impact effects without load errors and
  its capture was inspected. No ASB action starts occurred in that run. The
  later weapon test found that changing the rendered robot alone left the
  training script's Phantom Crash weapon records active. That earlier run
  therefore does not verify source weapon-action dispatch or source weapon
  impact routing. Temporary
  runtime, junctions, logs and capture were removed afterward.
- Body descriptors carry 16 signed node indices at `+0x154`. Assembly calls
  at `0x268364`, `0x2684a4`, `0x2685e4` and `0x268724` read the first four
  indices for arm/shoulder attachments. `0x1b3590` connects the child model
  to that parent node, or the parent model frame when the index is disabled.
  Native right/left order reverses each source pair for the port's facing
  convention. This replaces the body-hierarchy guess, preserves the drone's
  disabled arm and establishes Sweeper's disabled arm entries directly.
  Both the ordinary body resource and each package-owned bank retain these
  indices. The native renderer uses the sampled shoulder frames for drawing
  and source weapon effects, cancelling the old static offset once.
- The firing routine `0x505c90` allocates a shot actor through `0x224530`,
  then calls weapon runtime `+0x230`. `0x505204..214` chooses that callback
  from the seven flight entries at `0x59b620` using original weapon record
  byte `+0x19`. Flight 1/5 share `0x502e10`; its branch at `0x502f44`
  allocates grenade handle `+0x53c` through `0x2272e0`, or cannon handle
  `+0x53e` through `0x227320`. The loader at `0x226a90` binds their PO2
  resources, and `0x504690` writes scaled actor bases and positions to their
  instance batches. Basis scale instructions at `0x504884`/`0x504944`
  assemble floats 0.03/0.018. Registration validates the callback table,
  scale instructions and unique model resources before exporting the 15 routes.
  Native rendering suppresses the inherited projectile mesh, exhaust and
  projectile sprite for those routes. Muzzle flashes and other flight branches
  remain separate pending work; the source actor basis/timing is still adapted
  from native flight velocity and position.
  The release port built successfully; 100 native checks and 75 Python import
  checks passed, including outward-facing triangles in both native projectile
  models. Corrected live setups bound the training weapon records as well as
  the rendered robot. Photon grenade and Kubel cannon runs submitted their
  expected source model at scales 0.03/0.018, started nine/four source actions
  and reported zero missing attachment frames or load errors. A 310-frame
  Photon handgun run, firing through frame 180, started its delayed source
  action once. Inspected captures establish loading/playback activity, not
  original visual fidelity or correct arena scale. All temporary runtimes,
  junctions, logs and captures were removed after inspection.

Arena-scale validation:

- The release port and focused native target built successfully. All 100
  native checks and 80 Python import checks passed. World-unit checks cover
  the guarded original root-scale instructions, all 15 registered geometry
  roots, every floor/wall coordinate, source fog distances and light volumes.
  Standing-start checks cover a sloped floor, local roof, wall and broad ceiling.
- Two corrected 180-frame runs used complete Photon parts in Germany Day and
  Shinjuku Day. Both exited successfully without load errors and started nine
  source weapon actions with zero missing attachment frames. Their inspected
  captures show clear starting positions, connected torso/arm/shoulder joins
  and exterior robot faces. They establish the tested loadout's rendering and
  playback, not every mixed-part combination or original animation cadence.
- Temporary runtimes, junctions, logs and captures were removed after inspection;
  the standard release build and imported content remain available.

Playback-unit follow-through:

- Wheel travel at `0x4e5500..08` subtracts actor positions `+0xc0/+0xd0`;
  it is a world displacement, not a mesh coordinate. All 71 registered roll
  controls now use reciprocal world scale instead of the old factor ten.
- Suspension loads and stores at `0x4e5e0c/28/4c/64` address node world Y
  at `+0x84`. Both registered boss controls now use world lengths instead of
  mesh-export lengths, retaining the original constants and independent phases.
- Particle scale setter `0x19a668..6a8` forwards original values to generators.
  Full-frame parent override `0x19c040..50` copies the model matrix, while masked
  parents reconstruct selected Euler/translation components. Native PTK motion,
  gravity, sizes and action offsets now use world units. Full-frame attachments
  restore the original part-root basis scale without scaling world translation;
  normalized masked attachments do not inherit that model-scale factor.
- Release build succeeded; 101 native checks and 84 Python checks passed.
  Coverage includes the entire registered roll/suspension inventory, common
  stage/particle world-unit metadata, the eight suspension phases and particle
  positions/quad axes under both full-frame and masked parents.
- Two 420-frame Shinjuku runs moved forward/reverse and fired with Photon
  `S1L04H` wheels and the complete `S7L01H` boss rig. Both completed without
  loading errors or missing attachment frames and started three source actions.
  Captures were inspected, but HUD/camera occlusion limits lower-leg review;
  these runs do not establish original steering, suspension callback cadence,
  particle ordering or complete visual fidelity. Temporary runtimes, junctions,
  logs and captures were removed; standard build and imports remain available.

Spherical rolling playback:

- Descriptor callback `0x4e5d10` belongs only to `S2L06H` control 18.
  It calls `0x4e54b0` with both actor accumulators `+0xc10/+0xc14`;
  the non-null second pointer bypasses the single-wheel turn contribution.
  Initialization at `0x4dcd0c..4c` sets actor matrix `+0xc20` to identity.
- `0x4e5a00..2c` rotates that persistent matrix around X, Y, then Z using
  forward, lateral and steering increments. `0x4e5a30..4c` replaces the
  node's local basis and composes its model root. `0x4e5a88..90` restores
  the saved world position. Registration guards those instructions and the
  callback's accumulator pointers. All 72 rolling controls now register;
  71 retain single-axis behavior and one consumes this spherical matrix.
- Native playback preserves this matrix across pose reads and animation
  changes. It cancels the Aeron mesh's baked bind scale (1.025) once the
  callback takes over, preserving both source-rest geometry and the replaced
  basis. Forward/lateral travel uses native actor displacement; native heading
  changes and airborne control remain explicit steering/input adapters.
  Original producer and callback cadence are still pending.
- Release build succeeded; 103 native checks and 85 Python checks passed.
  New checks cover all three rotations in order, preserved world position,
  bind-scale cancellation, repeated pose reads, destruction freeze, airborne
  lateral suppression and reset. Registered callback coverage includes the
  spherical control across the complete leg inventory.
- Complete Aeron body/arms/shoulders with `S2L06H` legs rendered in Practice
  for 60-frame idle and 480-frame forward/reverse/turn runs. Both completed
  without load errors. Captures were inspected for assembled exterior parts;
  lower-mesh HUD occlusion limits visual review of rolling. These scenes did
  not start source weapon actions and do not verify effect attachments.
  Temporary runtimes, junctions, logs and captures were removed; the standard
  build and imported data remain available.

Reported shop, scale and assembly issues (2026-10-06):

- Long names were already ellipsized at the panel boundary, but that still
  allowed names under the fitted-hangar and side badges. The shared goods-row
  layout now reserves those sprites, including centered badges and the badge
  base. Explicit name widths cannot extend beyond the resulting column.
  A live Assemble arm list confirmed the ellipsis stays clear of the row icons.
- The existing world-unit correction passed the focused import checks again:
  all 15 arena geometry roots, collision coordinates, fog and light distances
  use the original robot/world relationship. No additional arena scaling was
  introduced during this investigation. The Germany side-camera capture was
  occluded by nearby geometry and does not establish visual scale correctness.
- The current saved loadout contains `S4B01H` body, `S8L01H` boss legs and
  a right `S3A07H` arm. Reproduction in Assemble and Practice shows the weapon
  attached at the body's hand, and the torso moving with the leg control.
  The mixed torso/leg join still looks awkward; its physical fit is unresolved.
  Separate matching `S4B01H`/`S4L01H` and `S8B01H`/`S8L01H` captures showed
  connected joins. Those observations do not establish every mixed assembly.
- Release port and focused native target built successfully. All 104 native
  checks and 30 focused Python checks passed. Temporary runtimes, junctions,
  logs and captures were removed; the standard build and imported content remain.

Model-frame impact playback:

- Registration guards the original struck-actor getter and first-part pointer,
  control-zero dispatch, constructor masks and node-matrix getter. All 95
  routes export their mask and control: 87 translation-only and eight model
  routes using PTK resources 45, 53 and 55.
- Every imported leg rig now retains its animated control-zero frame in the
  native pose. The torso, arm aiming and drawing-bone slots remain separate.
  The renderer supplies that frame to impact creation and attachment updates.
- All 106 native checks and 86 Python checks passed. New native coverage verifies
  world-hit preservation with a scaled, rotated parent at a nonzero world
  position; live rotation/translation of stopped tails and their quad axes;
  non-drawing control capture and per-pose reset. The existing 35-leg inventory
  check now observes control zero across every available imported clip.
- Three 600-frame Practice runs fired the original part bindings for `S1S07H`,
  `S2S04H` and `S3S04H` at an imported `S7B01H`/`S7L01H` target. Resources
  45/53/55 recorded four/one/three model-impact starts and zero missing frames.
  All runs completed successfully; captures were inspected. These establish
  native route and attachment activity, without proving original producer
  coordinates, cadence, particle ordering or complete visual fidelity.
- Temporary runtimes, junctions, logs and captures were removed; the standard
  release build and imported content remain available. Original action/charge
  gates, collision suppression, muzzle/trail selection and GS rendering remain
  part of the unfinished playback goal.

Format cross-checks use primary implementations:
[vgmstream ILD](https://github.com/vgmstream/vgmstream/blob/master/src/meta/ild.c),
[HD/BD](https://github.com/vgmstream/vgmstream/blob/master/src/meta/hd_bd.c),
[VGMTrans Sony PS2](https://github.com/vgmtrans/vgmtrans/tree/master/src/main/formats/SonyPS2)
and the [SPU envelope register description](https://psx-spx.consoledev.net/ps1/spu/soundprocessingunitspu/).
The native PS2 envelope adapter runs at 48 kHz; it is not a hardware parity
claim. Music uses the repository's existing FFmpeg dependency.

Take-off selection and torso attachment investigation (2026-10-06):

- The original jump handler checks actor input bit 4 at `0x4eb568..570`,
  selects source action 20 at `0x4eb59c`, and dispatches through `0x4e94a0`
  at `0x4eb5ac`. Native take-off slot 10 now resolves through that action
  table for legs and body banks, rather than guessing filename suffix 11.
  Disabled source entries remain disabled. Native movement and timing remain
  adapters; this does not recover the whole original airborne state machine.
- Assembly at `0x2689a0..9b4` attaches the body model to the leg descriptor's
  first attachment entry through `0x1b3590`. The converter now consumes that
  entry rather than assuming node 1. All 35 shipped leg descriptors specify
  node 1, so this confirms the existing join location and does not repair
  the awkward `S4B01H` / `S8L01H` mixed geometry.
- Callback `0x4e4320` reads actor float `+0x15c`, applies source Z rotation
  through `0x10ed18`, and restores the control's world position. Its input
  producer remains unresolved; treating it as yaw steering would be wrong.
  Callback `0x4e43a0` generates random positional shake, rather than steering.
- Wheel callback `0x4e54b0` uses actor `+0xe18`, turn-speed `+0x8c` and
  integer clock ticks for the single-axis turning contribution. The current
  native heading-delta adapter remains; source inputs and cadence have not
  been substituted with inferred values.
- Regenerated all 140 registrations and 35 body banks. All 88 Python import
  checks and 106 native checks passed, including compiled take-off lookup in
  every body bank and sampled leg poses in every available clip. This run
  did not produce a new visual capture or establish mixed-part physical fit.

Direct rolling controls and compiled turn selection (2026-10-06):

- The single-axis branch at `0x4e55e0..569c` reads actor steering `+0xe18`,
  turn speed `+0x8c`, and clock ticks. After coefficient scaling/division the
  steering term is independent of the rolling coefficient. The spherical
  branch skips that term and uses the separate negative steering increment
  divided by clock ticks at `0x4e56f4..56fc`. Native playback now uses direct
  steering input for these operations; heading changes alone no longer spin
  the controls. Reverse and boost physics scaling do not alter that input.
- Registration guards those inputs, the source radians-per-degree word
  `0x3c8efa35`, and the player input store at `0x4de7dc`. The new editable
  rolling fields retain that angular conversion and explicitly label a
  native 60-frame clock adapter. Native controls, loaded turn-speed values,
  displacement and callback cadence remain adapters. Original input release
  filtering, load-factor production and global callback timing remain pending.
- `0x4e8fa0..8ff4` derives signed direction from steering and forward input.
  The turn selector reads that direction at `0x4ebbd8`; negative selects
  action 45 at `0x4ebbf0`, otherwise action 44 at `0x4ebc14`, both through
  `0x4e94a0`. Native turn slots 9 and 8 now use those compiled entries for
  legs and body banks. Ten native slots now resolve through the source action
  tables. Original state gates, turn playback-rate production (`0x4ebc28..cd4`),
  remaining motion selection and transition timing are still incomplete.
- The release port and native target built. All 90 Python import checks and
  107 native checks passed. Added checks cover direct steering while a chassis
  stays still, heading-only repositioning, wheeled lateral bindings, source
  callback order, elapsed-clock single-axis steering and spherical cancellation.
  All 35 leg sets and body banks verify the new compiled turn bindings.
- Two initial 600-frame Practice runs covered Holy `S3L06H` wheeled legs and
  Aeron `S2L06H` spherical walker legs, with their matching bodies. Under the
  selected Standard controls, Holy reached turn states while Aeron strafed;
  the initial Aeron camera was obscured near the wall. Two final 360-frame
  runs used Preferred controls, a wider side camera, stationary steering and
  brief forward/reverse travel. Both reached turn slot 8, exited successfully
  and reported no loading errors. Their inspected captures showed connected
  matching assemblies. No weapon actions were fired in these runs. These
  observations do not prove original producer timing, all mixed assemblies
  or the previously reported mixed boss-leg physical fit.

Rendered locomotion travel correction (2026-10-06):

- The original model constructor identifies the root control and clears its
  POD bind matrix at `0x1b2a9c..1b2aa8`. During PMT playback,
  `0x1b56e8..1b56f4` passes whether the control is a child to `0x19ee50`.
  `0x19f478..19f490` adds sampled translation only for children; the root
  stores a travel delta separately at `0x19f4c4..19f4f4`. The checked
  instructions are now guarded by the motion-descriptor reader.
- The native leg adapter had applied that root-key travel to the entire
  visible hierarchy while native gameplay also moved the actor. Walking
  clips could therefore displace the robot and its effect attachments from
  the collision position and snap them back on a clip loop. Rendering now
  keeps the root's static attachment translation; the imported root bind is
  cleared as in the constructor. Motion travel remains in the editable bank
  for movement consumers, and child translations and root rotations remain.
- All 35 leg sets were regenerated. The source scan found 1,517 leg motion
  payloads with no nonzero alternate root-bind translations; one POD root
  (`S1L03H`) had a small authored translation that the constructor clears.
  Native tests sample every available imported clip at its start, midpoint
  and end, verifying the rendered model root stays at the actor position
  while the torso continues to follow the animated leg attachment.
- Both Release targets build; 91 Python and 107 native checks pass. Seven
  bounded Practice runs covered the saved `S4B01H`/`S8L01H` mixed build and
  matching `S4B01H`/`S4L01H` build. The final short moving capture reached
  walking without a fence reaction and showed the connected assembly. The
  longer moving runs reached nearby collision and are not visual proof of
  the complete gait. Zero runtime load errors were reported.
- This fixes rendered locomotion displacement, not the unresolved physical
  proportions of a smaller torso on boss legs. Shop column bounds and the
  existing 15-arena world-unit correction passed the focused checks again;
  no new arena scaling was introduced. Temporary runtimes, junctions,
  captures, logs and disassembly listings were removed before handoff.

Accepted-shot and held-input sound gates (2026-10-06):

- The sound context's four bytes at `+0x14` are accepted-shot markers.
  Setter `0x5060a0` is called at `0x505c4c`, `0x506148` and `0x5064dc`
  after weapon acceptance. Sound observation at `0x50e584` precedes effect
  processing at `0x50e590`, whose loop `0x50f700..50f72c` clears the markers.
  A continuously held native firing flag was therefore the wrong producer
  for starting/advancing the five shared pairs.
- The pair's inactive branch separately reads held input from actor `+0xe34`
  at `0x51144c`, checks each supporting weapon's remaining ammunition is
  positive at `0x5114bc..4d4`, and compares its base cue. This retains an
  existing pair through cooldown; it cannot start or advance a layer.
  The accepted mount's check permits zero remaining ammunition. The native
  observer now processes mounts in source order, including a last-round
  start followed by a later inactive mount's ending in the same update.
- Weapon-runtime float `+0xd8` is ammunition: initialization at
  `0x505218..505224` converts integer `+0x64` to float and stores it there;
  `0x505eb4..505ec8` subtracts one after a projectile dispatch. It is not a
  charge field. The converter guards these reads, setters, marker reset,
  observer order and ammunition comparisons, and exports their provenance
  in `slai/sound_cues.json`.
- Accepted native firing events and a separate native held-control snapshot
  feed the recovered gates. Queued rounds after release cannot count as held
  input. Native unlimited magazines explicitly retain a held pair; original
  input filtering, acceptance timing and the separate energy/capacitor
  producer remain adapters. The updates below integrate cue 177 and the
  sustained 209/228/247 lifetime. IOP stop fade, reverb and LFO remain open.
- Release port and focused native target build without warnings. All 92
  Python and 110 native checks pass. Native cases exercise cooldown waits,
  last-round mount order, queued rounds after release, multiple matching
  mounts, separate robots, death/loss and actual mixer retirement. Two
  bounded Practice runs used `S3A07H` (sequential pair) and `S4A07H`
  (simultaneous pair); each submitted three weapon sounds, loaded all 21
  source sample sequences and reported zero loading errors. This verifies
  the native gates and live delivery, not original hardware audio fidelity.
- Temporary runtimes, ten junctions, captures, logs and disassembly listings
  were removed and their absence verified. The standard build and refreshed
  sound assets remain available.

Follow-up on the reported shop, scale and assembly issues (2026-10-06):

- Rebuilt the standard Release port and focused native target with zero
  warnings or errors. All 110 native checks and 35 focused Python checks
  pass, including shop label bounds, arena units and imported leg controls.
- Four isolated Practice captures exercised the saved `S4B01H` body,
  `S8L01H` legs and `S3A07H` arm, with a lower side camera and short movement
  input, and compared the matching `S8B01H` body on the same legs. The visible
  torso and hand-mounted weapon remain connected; there were no runtime
  load failures. These observations do not establish fit for every assembly
  or reproduce the user's unspecified failing combination.
- A scan of the exported body, arm and shoulder roots found no nonidentity
  bind bases or animated root translations beyond their fixed mount origins.
  No additional root-offset correction was warranted by that scan. The prior
  locomotion root-travel correction remains in place.
- The specific stage and direction of the reported scale error have not been
  identified. The existing source-unit correction passes its checks; Practice
  captures do not close the report for the other arenas. No speculative
  rescaling or joint offsets were added during this follow-up.
- Temporary runtimes, junctions, captures and test logs were removed and
  their absence verified. The standard Release build remains available.

Sustained weapon sound lifetime (2026-10-06):

- `0x5111a0..511234` identifies cues 247/209/228, queries the current
  robot/mount through `0x50aac0`, and starts a missing cue only on an
  accepted-shot marker with nonnegative post-shot ammunition. These cues
  do not use the paired group's inactive-mount stop branch.
- Their source kind-5 beam update is `0x5060b0` (callback-table entry
  `0x59b684`). At `0x5061ec..506220`, accepted updates grow weapon float
  `+0xac` while it is below eight clock frames. Inactive updates decrement
  it at `0x506300..506330`. At zero, `0x506338..5063a4` releases the
  persistent projectile, clears its pointer, and calls `0x50a8b0` with the
  robot and mount. The five registered weapons all have nonzero record
  halfword `+0x134` (value four), selecting the persistent-projectile path.
  The alternate zero-field path may retain a projectile beyond counter zero.
- `0x50a8b0` stops fire cues 209/247/228 and their contact cues 210/248/229
  for that mount. Additional callers retire these sounds on owner death,
  explicit projectile disposal, and completed projectile fade. Cue byte +7
  still supplies an IOP stop-fade argument whose units remain unresolved.
- Native registration now assigns `sustained` policy to the five source
  weapons and their ten looping robot/surface contact rules. Accepted native
  shot events start absent voices; actual mixer key queries detect retirement.
  A live matching native beam, including its existing eight-frame release
  counter, retains both fire and contact voices. Counter expiry, loss, death
  or weapon replacement stops their keys and allows the existing ADSR tails
  to retire independently. Held input before a shot cannot start the fire
  cue, and held cooldown cannot restart a retired cue.
- Cue 177's byte `+0x19c` has two nonzero producers, in arm-action dispatch
  `0x4e9710`, at `0x4e9818` and `0x4e98f0`. Its caller at `0x5066dc`
  sets byte `+0xc80` from a targeting-state byte at `+0xdba + mount` before
  choosing the arm action. The update below recovers the completed-lock
  producer and consumes the free-arm meaning of this flag. Native acceptance,
  lock timing and animation-completion timing remain adapters.
- Both Release targets build with zero warnings/errors. All 117 native and
  93 Python checks pass. New native checks cover accepted starts, independent
  mounts, empty ammunition, fire/contact retirement after beam fade, actual
  mixer retirement, replacement, loss and owner death. Three bounded Practice
  hold/release runs covered `S1A06H` (209), `S7A01H` (228) and `S1S06H`
  (247); each delivered weapon audio without load errors, and end captures
  were inspected. This verifies native integration, not original hardware
  audio or the original acceptance, targeting, energy and clock producers.
- Temporary runtime, junctions, captures, logs and disassembly listings were
  removed and their absence verified. Standard builds and regenerated sound
  assets remain available.


## Free and locked arm actions (2026-10-06)

- The original runtime weapon parameter pointer is loaded at
  `0x504bd8..504be0` through `0x24d760`. Arm-action dispatch reads its raw
  kind at `+0x18`, independently of the HUD-kind adapter. Source kind six
  selects actions 39/40 for free right/left attacks and 41/42 for locked
  lunges; kind seven selects 35/36 and 37/38. The branches and their caller
  are instruction-guarded in the extractor.
- Target-control byte `+0xdba + mount` becomes one when the countdown in
  `0x22ce30..22ce90` completes; `0x22d030` clears it after invalidating the
  target. `0x506640..5066e0` copies that choice into actor `+0xc80` before
  `0x4e9710` selects the action. Free branches set actor `+0x19c` at
  `0x4e9818`/`0x4e98f0`; locked branches do not set it. Completion tests
  animation flags masked by `0x500` and clears the arm flag at `0x4e9be4`.
  Cue 177 tests this flag at `0x511384..511388`.
- All 35 native leg/body banks now carry eight additional motion slots,
  37..44, mapped to source actions 35..42. Original disabled clips remain
  empty. Disabled leg actions leave the leg animator intact; disabled body
  actions keep advancing the previous body clip instead of showing a rest
  pose. Enabled body attacks start their own clock, independently of a
  disabled leg clip, and restart on the next accepted same-side attack.
  Hand and shoulder frame queries use this same cached body playback.
- Four source-kind-seven arms were previously assigned a gun template and
  gun HUD kind by registration: S2A03H, S3A03H, S5A03H and S5A08H. Their
  shop records now use native melee kind six and select a same-maker melee
  template where available. Both source families remain distinct for their
  animation choices. The normal test-drive/RUMBLING weapon factory also
  carries the source family into battle. Checks construct all six imported
  melee arms through that factory and verify their weapon kind and family.
- Imported arm records preserve raw `source_melee_kind`. Native accepted
  melee attacks select by that family, mount side and completed native lock.
  The cue-177 rule requires the free-arm flag; rejected, cooling and locked
  attacks cannot manufacture it. Native strike termination clears the flag.
  The native single-strike/input/lock and PMT timing paths remain adapters;
  original animation flag completion, interrupt/reset branches and full
  actor producer cadence are not yet reproduced.
- Both Release targets build with zero warnings/errors. All 122 native and
  95 Python checks pass. Checks cover both families/sides, completed and
  pending locks, rejected starts, repeated attacks, disabled-action retention,
  and all 35 body-bank mappings. Four bounded Practice captures were
  inspected: both sides of S4A03H on S1 body/legs, S4 body with S8 boss legs,
  and S5A03H on S4 body/legs. Weapon attachments follow the animated hands;
  three cue-177 runs each submitted one weapon cue, while the source-silent
  S5A03H submitted none. These captures do not verify every mixed assembly
  or identify the user's unspecified failing combination.
- Temporary melee-test runtime, junctions, captures, disassembly listings
  and logs were removed, and absence was verified. Standard builds and
  regenerated imports are retained.


## Primary leg-motion effects (2026-10-06)

- Assembly loader `0x2513f0..251408` gets the leg record through
  `0x250610` and calls `0x509a20` with selector -1. The original
  `0x507b60` switch at `0x5baaf0` maps that record to one of 35 banks,
  named through `0x59cdb0`. Registration uses the source record rather
  than model numbering or filename order. All 35 imported leg sets now
  receive their original action bank, including boss, drone and sweeper sets.
- `0x50e840..86c` reads the leg-model animation clock. Primary action IDs
  come from model `+0xde`; original secondary playback takes precedence
  when active. Native playback uses the actual drawn primary leg action
  and frame, with verified source action mappings and native motion speed.
  Untraced native actions map to -1, preventing guessed source dispatch.
- `0x50eb1c..98` tests the authored motion list and resets saved frames
  on a miss. At `0x50ec3c`, bit 1 has priority: fire once on the first
  matching update. Otherwise bit 4 at `0x50ec58` tests
  `previous < authored_frame <= current`. The wrap branch
  `0x50eba0..dc` adds duration only when previous exceeds current and
  previous plus speed reaches duration. Bit 0x20 at `0x50e958..968`
  skips all record work while the last submitted effect still exists.
  The native player retains this last-handle behavior, including a failed
  final descriptor replacing an earlier handle with zero.
- Descriptor part selectors at `0x50ef8c..efa8` choose model records at
  actor `+0x1a4 + selector * 0x114`: selector zero is legs, one is body.
  All leg controls now retain pose frames, including hierarchy-only nodes
  with no mesh. Body frames use the same cached rigid playback as drawing.
  Effects share the existing 64-slot source particle pool and recovered
  attachment masks/inertia, with native movement and grounded-state adapters.
  Owner respawn, leg replacement or loss of the imported bank retires emitters.
- 560 simple leg-bank records have flags 6, 7, 38 or 39. The 35 whose first
  descriptor selects resource 27 remain inactive: `0x50ebf4..ec34`
  checks owned effect counts and death state, bypassing ordinary crossings.
  The remaining 525 records contain 902 descriptors. Two descriptors in
  the Photon/Brineman four-leg take-off entries use parent node -1;
  `0x19bf54..64` calls the direct `0x1b3c80` getter, which has no negative
  guard. Their allocation/producer meaning is not established, so those
  two descriptors remain inactive. The native supported total is 900.
- Original secondary-action clocks, state/charge/probability gates,
  resource-27 death behavior, bank byte `+0x41`, source actor cadence and
  procedural animation producers remain incomplete. The alternate resource
  selector is recovered and tested for exact words 0x80/0x100; the native
  normal variant remains zero because the original `+0xdf4` producer is
  pending. This is partial native playback, not complete source behavior.
- Both Release targets build with zero warnings/errors. All 128 native
  tests and 98 Python tests pass. Checks cover all 35 bank routes and leg
  hierarchies, exact supported record/descriptor counts, cache clearing,
  entry precedence, frame crossings, loop/rewind distinction, retained
  handles and alternate selectors. Six bounded Practice runs cover S1
  walking/jumping, S4 body on S1 four-leg take-off, S4 body on S3 walking
  legs, and S1 roller movement. Walking and take-off submit four to six
  effects, with zero missing attachment frames; roller movement submits
  zero through this primary-motion subset. Captures were inspected for
  robot attachment and stage placement. Effects had mostly expired by the
  final captures; logs verify starts and live attachments, not every
  descriptor's visible appearance or the user's unspecified assembly.
- Temporary motion-effect runtime, all ten junctions, captures, logs and
  disassembly listings were removed, and absence verified. Standard builds
  and regenerated imports remain available.


## Supply flight assets and crate presentation (2026-10-06)

- DRUMCAN.BG belongs to resource folder `Z/BGOBJ/SUPPLY_SHIP`, not
  `Z/BG`. Its BGP references `jet.po2`, while that object's loaded resource
  is JET01.PO2. It has no authored standing arena. Registration now reports
  it as a supply-ship object description; adding a fabricated floor would
  turn the inventory error into an invented level. The 15 arena choices
  remain available.
- The source path loader `0x522520..522690` uses 18 stage-indexed prefix
  pointers at `0x5b3b60`, counts at `0x5b38d0` and format `%s%02d.pth`.
  Export selects the current JET_PATH resource package, excluding OLD.
  Uncompressed files have a 20-byte header followed by count times 64 bytes:
  four homogeneous points per cubic. Entry/drop/exit markers at header
  +2/+4/+8 are consumed at `0x524438`, `0x5245c8` and `0x524600`;
  +6 and the float at +12 are retained as unresolved source inputs.
  There are 18 nonempty routes with 489 cubics; empty placeholder paths
  contribute no route. All nine source prefix families are exported,
  including Trial, although no Trial BG arena is currently registered.
- Arena registration supplies each arena's routes in native world units,
  with the existing source axis conversion. Shinjuku and London filename
  aliases map to the compiled sinjyuku/london resource families. Day/night
  arenas use the same path banks. Native loading preserves an explicitly
  empty imported route list, rather than falling back to Phantom Crash's
  scene table. Supply timing, native collision/drop state and rewards remain
  the existing port adapter; source banking, cargo-release/wreck producers
  and the unresolved header inputs are not claimed complete.
- The source JET01 body now follows the carrier's native flight position and
  heading. Night arenas load JET_LIGHT, regardless of the native scene-stage
  number's parity. `0x5235c0..523678` increments the source angle by .03,
  composes a Y rotation and replaces light control zero's matrix. Export
  clears that control's bind transform before world-unit restoration;
  drawing uses the converted rotating frame. The native 60 Hz presentation
  adapter advances this angle by elapsed simulation time (1.8 radians/sec),
  independent of render rate. The original per-update producer cadence remains
  part of the wider animation timing investigation. Native carrier-owned
  lamps do not attach to these source models. Wreck playback still uses
  native falling state with the intact source carrier; source destruction
  geometry and state effects remain incomplete.
- The crate update `0x525318..525498` uses remaining lifetime: above 60
  frames, rate is 900/lifetime; otherwise 15. Phase advances by .05*rate,
  wraps at pi, and RGB is .5 + integer(96*abs(sin(phase)))/64, alpha 1.
  Exported pulse values are editable data. Native remaining lifetime is
  converted to the adopted 60 Hz frame units without changing pickup
  duration or reward amounts. Falling/landed source crates use this tint
  on both single and split views. Their authored image bank still follows
  pickup kind; the Phantom Crash overlay glow/tint is suppressed for source
  presentation. Cargo hiding, release, collection and expiry use native
  state transitions, so original visibility timing remains incomplete.
- Implementation was followed by clean Release builds, 129 native and 100
  Python checks. Checks cover all 15 registered route/image/carrier settings,
  the current source package, axis conversion, prefix aliases, empty paths,
  and matching night-light rotation at 30 versus 144 simulation updates/sec.
  Two bounded 960-frame live runs completed without loading errors. Practice
  selected one imported route and released kind 6/9 crates with CH_50/CM_100;
  night Germany selected two routes and loaded its source searchlight.
  Captures were inspected for stage and robot stability; the carrier/crates
  were outside those final camera views, so these runs do not establish their
  complete visual appearance or the original flight state machine.
- Temporary supply runtime, all ten junctions, captures, disassembly listings
  and logs were removed, with absence verified. Standard builds and refreshed
  imported assets remain available.

## Death and weapon-loss particle gates (2026-10-06)

- At `0x50ebe0..50ec90`, the source resolves the first effect descriptor
  before applying the resource-27 exception. With bank byte `+0x41` zero,
  `0x5094c0` counts active resource-27 entries for the actor, including
  negative-owner wildcard entries. A positive count permits emission before
  the authored key; otherwise the adjusted primary frame must be at or beyond
  the key. This repeats on matching updates, rather than using ordinary frame
  crossings. Motion-list and retained-handle gates still run first.
  The native shared pool now tracks effect owners and counts complete managed
  effects, retaining occupied slots until all their stopped generators' tails
  retire. Weapon actions pass their owner into the pool too.
- All 35 leg banks author resource 27 on motion 46 with key 30. Their
  constructors at `0x508760..5087b8` replace the descriptor's attachment mask
  with `0x400` and rebase its offset into the model object's translation.
  `0x19c190` discards the joint matrix in the composed result. Native death
  particles therefore follow the leg object's translation without inheriting
  joint rotation/scale. This also resolves the negative node on Brineman's
  roller death descriptor. The supported primary subset now includes 560
  records and 935 descriptors; the two resource-5 negative-node leg descriptors
  remain deferred. Actor position, death-state duration and update cadence
  still come from the native battle adapter. The source's accompanying
  `0x51ef00` explosion/physics call is not reproduced by this particle gate.
- Bank mode 1 suppresses non-27 descriptors at `0x50f018..50f030`, after
  trigger bookkeeping. It also restores ordinary entry/frame-crossing rules
  for resource 27. The player accepts this mode, but the original byte's
  producer remains unconnected; native model banks currently supply zero.
- The 95 routed weapon banks contain 95 flag-`0x100` part-state records in
  addition to 86 fire-counter records. `0x50eac8..50eaf4` reads actor
  `+0x9c +4*slot` and requires value 2. The owner loader replaces the selector
  at row `+0x3f`, independently of the authored value. This belongs to the
  weapon-loss lifecycle, not a firing/charging counter: `0x4e1130` advances
  state 1's countdown to 2, then advances 2 to 3 with timer 15; the later
  state-3 branch replaces model callbacks with `0x4e42e0`/`0x4e4300`, which
  disable drawing, before entering state 4. Earlier loss/countdown producers
  and original callback ordering still need investigation.
- Native loss transitions latch one state-2 particle update independently of
  accepted shots, including a loss on the same tick as actor death. The bank
  player's gate remains level-triggered if a caller supplies state 2 for
  several updates. Loss effects continue their authored generators/tails
  instead of being immediately stopped by the missing-part flag. Respawn
  retires that owner's old action clocks and effects. Original delayed model
  disappearance is not implemented by this adapter.
- There are 81 enabled weapon-loss descriptors with valid nonnegative nodes,
  two authored disabled descriptors and twelve enabled negative-node
  descriptors. The supported weapon subset therefore has 181 records and
  258 descriptors, including the existing 175 firing descriptors. Raw ASB
  records remain lossless. The twelve deferred routes are Holy G-chip,
  Kubel stun/acid/G-chip/shields, four boss shoulder weapons, and three extra
  Kubel-shield routes. `0x19bf5c..64` passes node -1 to `0x1b3c80`, whose
  getter directly indexes the control array; the loader copies these nodes
  without rewriting them. Control allocation at `0x1b2898..1b28dc` provides
  count times `0x110` plus its array header, without an identified preceding
  dummy control. A negative node is consequently not evidence for choosing
  control zero or a body joint. These twelve attachments and the two earlier
  leg attachments remain explicitly deferred until their frame semantics are
  established.
- Release port and focused native target build successfully; 133 native and
  102 Python checks pass. Checks cover owner/wildcard counts, retained tails,
  repeated/early/late death gates, bank-mode suppression, all four bound
  weapon-state selectors, independence from firing counters, and attachment
  availability across all 35 leg and 95 weapon routes. These checks establish
  the supported subset, not complete original actor state or visual parity.
- A bounded 240-frame Practice run loaded the 35 model and 95 weapon banks
  and completed successfully. It reached neither destruction trigger (both
  counters stayed zero), so it does not verify the new effects' in-game
  appearance. No visual verification is claimed from that capture.
- Removed the temporary runtime, all nine junctions and 48 owned files,
  including captures, fixture data, disassembly listings and logs. Verified
  that both this scratch directory and the earlier death-effect scratch
  directory are absent. Standard builds and refreshed imported assets remain.

## Body secondary-channel transitions (2026-10-06)

- The source setter `0x1b43c0..1b4400` records a secondary action at model
  `+0xe0`, the transition duration at `+0xe8` and a zero counter at `+0xec`.
  `0x19ecc0..19ee18` starts its independent per-control key cursor.
  `0x1b4818..1b48b0` advances the counter once per model update, independent
  of the PMT playback speed. After the counter reaches the duration, the
  following update promotes the secondary action/cursor to primary
  (`0x1b4848..1b48a4`, `0x19ee20..19ee40`).
- The evaluated pose is a feedback input, not a fixed outgoing-clip sample:
  `0x19f0e8..19f140` replaces the primary rotation/scale/translation with
  cached control `+0xb0/+0xc0/+0xd0` when the secondary payload exists.
  `0x19f2f8..19f3f4` blends toward the secondary pose using counter/duration;
  `0x19f3f8..19f424` stores the result for the next update. Rotation uses
  the short quaternion arc (`0x1a0990..1a0b94`); scale/translation use
  vector interpolation (`0x1a0960..1a0984`). Interrupted transitions retain
  that last evaluated pose and restart the transition counter.
- Native `RigidPlayback` now evaluates and caches source body controls on
  simulation updates. Rendering, hand mounts, shoulder mounts and effect
  attachment queries all consume that same cache without advancing it.
  Disabled source body entries still retain their accepted clip and clock.
  The implementation lives separately in `rigid_playback.cpp`; glTF channel
  sampling and control-to-world composition remain in `rigid_animation.cpp`.
- Guarded PAL actor call sites export fixed transition lengths for native
  walk/back/dash/back/take-off/air/death/idle: 8/2/8/2/3/5/10/12 updates.
  The editable `source_transition_frames` metadata is loaded by every one
  of the 45 registered base/package-owned native body models. Source
  `motion_resources.json` and body-bank profiles preserve the producer
  addresses and values. Fresh registrations emit the same metadata.
- Turns use `0x4ebb90..4ebbd4`. The getter `0x2548c8` reads leg-record
  **+6 (alternate model)**, rather than assuming a descriptor ordinal or
  part name. Alternate models 22/28 use ten updates; other legs use one
  update from actor state zero or eight from another state. All 35 registered
  legs carry editable idle/moving timing pairs through `RobotAssembly`, so
  mixed bodies use the selected legs' rule. The special registered models
  are Aeron `S2L05H` and Kubel `S4L06H`. Native actor idle/moving state adapts
  the source state input; accepted arm actions retain that state.
- This does not finish the original leg transition evaluator, queued
  non-blending secondary actions, unmapped motion/state producers, original
  body/arm procedural callbacks, wheel input filtering or complete secondary
  particle dispatch. Body channel time still follows the native locomotion
  clock or accepted independent arm-action clock. Physical fit of the reported
  mixed boss-leg assembly and original global callback cadence remain open.
- Removed the 23 owned investigation listings with explicit file edits, then
  removed their empty scratch directory and verified its absence. The earlier
  cleanup rejection is resolved. No temporary runtime or capture was created
  during this pass; standard builds and updated mod assets are retained.
- Release port and focused native target build with no warnings or errors;
  139 native and 105 Python checks pass. New checks cover iterative pose
  feedback, interrupted/disabled transitions, independent counter timing,
  next-update promotion, quaternion sign equivalence, shared draw/socket
  poses, all 35 leg timing selectors and all 35 package-owned body banks.
  Guard tests reject altered source counter, getter and timing instructions.
  No new in-game visual review is claimed for this change.

## Leg secondary-channel transitions (2026-10-06)

- The counter/feedback evaluator is now connected to all 35 imported leg
  sets through `source_transition_frames` in editable leg definitions and
  `RobotAssembly`. The ten mapped movement states consume the same guarded
  fixed/conditional source durations documented above. Fresh registration
  and the current mod catalogue carry the data.
- `SourceLegPlayback` caches each original tracked control's local channel
  pose once per simulation update. Mapped transitions consume the last
  evaluated pose, advance counter/duration independently of clip speed,
  retain that pose on interruption and leave transition mode on the following update.
  Root-key translation remains excluded. Wheel/roller rotation, spherical
  controls and suspension callbacks apply after the channel pose; draw,
  torso-mount and particle-control queries do not advance playback.
- Disabled mapped leg actions retain the accepted clip and clock, matching
  the source model setter's disabled-entry rejection. This does not change
  the actor's requested gameplay state. Existing primary-effect dispatch
  already reads the accepted leg animator action rather than the request.
- Unmapped movement and zero-duration queued secondary actions keep their
  existing native transition adapter. Its sampled translation and quaternion
  transition are cached without applying a second rotation blend. Original
  Phantom Crash legs and native arm skeletons retain their own pose paths.
- This completes the mapped leg transition mixer, not complete motion
  playback. At this checkpoint native leg banks still converted PMT quaternion
  keys into Euler channels; the following sampling update supersedes that
  limitation for mapped leg movements. Queued-action
  completion, unmapped source selectors, original callback cadence and full
  secondary effect dispatch remain unfinished. Body/arm procedural controls,
  filtered wheel inputs, physical mixed-part fit, stage visuals, sound and
  supply behavior remain in the active scope.
- Original primary/secondary model action identities for effect dispatch
  remain adapted: the effect clock still reads the accepted native animator
  action/frame. Source model `+0xde/+0xe0` identity changes and the preferred
  secondary per-control clock must be connected independently of the mixer.
- Release port and focused native target build without warnings or errors;
  143 native and 105 Python checks pass. New checks cover counter/feedback
  timing, interrupted transitions, root-key travel exclusion, wheel ordering,
  repeatable attachment queries and preservation of the unmapped-state adapter.
  The robot-player integration check exercises `S4B01H` with all 35 imported
  leg sets, including the reported `S8L01H` combination, across ten mapped
  states at two clip speeds. Torso/control matrices stay connected and finite
  and repeated pose reads are identical. This does not establish physical
  mesh fit or original visual fidelity.
- No temporary runtime, captures or logs were written. The earlier owned
  body-control, state-effect and death-effect scratch directories remain
  absent. Standard builds and updated mod data are retained.

## Original leg quaternion sampling and control clocks (2026-10-06)

- The scalar quaternion matrix routine at `0x1a05f0..1a06a8` writes the
  transpose of the row-vector quaternion matrix previously used by the leg
  exporter. Applying the leg coordinate basis gives native XYZW
  `[-x,y,z,w]` from source WXYZ. This matches the existing body glTF/native
  conversion. Both the retained Euler adapter channels and the new direct
  quaternion payload use the corrected direction.
- Native motion banks now retain editable `source_pmt` keys, durations,
  bind matrices, quaternion rotations and positions. The new sampler uses
  normalized short quaternion arcs and linear translation, following
  `0x19fd30..19fe98` and `0x1a0990..1a0b94`. Leg controls use this sampler
  directly; Euler channels remain available for the generic motion APIs and
  root-stride calculation. Authored leaf mesh scale remains baked only once.
- Primary `0x19f520` and secondary `0x19f900` key cursors traverse segments
  preceding the final key; the final key's duration is never traversed.
  Native loop/timing periods now use the endpoint frame. A disc scan found
  nonzero final durations on every keyed leg track, so the previous exporter
  lengthened these loops. Static tracks do not contribute a period.
- Mapped leg movements now maintain a key index and elapsed value per
  control. Controls evaluate their current cursor before it advances, matching
  `0x19eeb4`, `0x19eee4` and `0x19ef14`. Positive speed advances the cursor;
  transition counters still advance independently of speed. Source nonloop
  endings restore pre-update elapsed and retain the current segment instead
  of analytically clamping to the endpoint. Root travel remains actor-owned;
  wheel/suspension callbacks and all attachment queries consume cached poses.
- All 35 live leg banks and catalogue timing tables were refreshed: 1,280
  registered clip entries, 42,091 control entries and 15,295 keyed control
  entries (counts include repeated clip registrations). Fresh registration
  emits the same payload. Sampler instruction guards are exported with the
  motion resource metadata and checked against the PAL executable.
- Unmapped states and zero-duration queued secondary actions retain the
  native transition/clock adapter while sampling corrected quaternion keys.
  Original freeze, completion and promotion producers, body/weapon
  per-control clocks, primary/secondary effect identity selection, procedural
  body/arm adjustments and callback cadence remain unfinished. The physical
  mixed-part fit, stage visuals, remaining sound/particle behavior and supply
  state machine remain in the active scope.
- Release port and focused native target build without warnings or errors.
  147 native and 108 Python checks pass. Added checks cover source matrix
  direction, normalized short quaternion arcs/sign changes, terminal-key loop
  exclusion, segment crossings, nonloop elapsed restoration, speed-zero holds,
  evaluate-before-advance timing and root-travel suppression. The existing
  all-35-leg mixed-body attachment check runs against the refreshed banks.
  No new visual capture was made; these checks do not prove physical mesh fit.
- No temporary runtime, captures or logs were created. The earlier owned
  body-control, state-effect and death-effect scratch directories were checked
  and remain absent. Standard `build/port` and updated mod data are retained.
