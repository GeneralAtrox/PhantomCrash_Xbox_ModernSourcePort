# Robot firing poses and weapon origins

Implementation: 2026-09-30, Europe `default.xbe`. Independent arm firing
poses and animated weapon origins are implemented. This is not a claim of
complete animation parity. Function descriptions below are researcher-assigned
meanings of executable addresses.

## Implemented path

- Shot-down robots play non-looping motion 12 at the motion bank's normal
  cadence. Static inspection of `0x077076`–`0x077087` shows `0x07675e`
  selecting 12 through `0x08ddb2`; it also starts the existing wreck VFX.
  Extracted leg timings end between frames 74 and 324, so completion comes
  from the animator rather than a fixed result delay. Dead robots continue
  falling/colliding, but cannot move, aim, fire or be targeted. Respawn and
  roster replacement cannot cut off an active death clip.

- `RobotCombat` layers each arm's extracted firing pose over locomotion,
  raises and lowers each arm independently, and targets the upper arm and
  forearm. Ordinary arm firing waits until the arm is raised. Lost arms
  cannot fire or leave a visible weapon behind.
- The simulation evaluates movement, combat intent, pose and muzzle before
  emitting shots. `BattleView` consumes the same pose without advancing its
  animator. Split-screen and arena monitors reuse those draw transforms;
  replay archives the resulting draw items.
- Projectiles, firing effects and positioned firing sounds use the selected
  animated muzzle. Multi-barrel weapons cycle through the extracted order.
  A stage or gate intersecting the robot-to-barrel segment blocks firing.
- `weapon_mounts.py` extracts attachment resources to editable
  `robots/weapon_mounts.json`. The current extraction has 127 model entries
  and 243 muzzle points. All 102 arm points map to existing skeleton bones.
  Shoulder points use their model hierarchy under the shared torso/pitch
  mount, also supplied to rendering.
- Mesh binding now counts only nodes that submit geometry. Transform-only
  ancestors do not consume bone slots, and multiple materials on a node
  share a bone. This also corrects the body's offset into leg bones.
- `data/world/robot_animation.json` controls raise/release time, firing pose
  indices, upper-joint pitch limit, locomotion fade and free-aim distance.
  The latter is a port convergence setting, not a recovered original value.

## Original combat pose path

| Address | Observed behaviour |
| --- | --- |
| `0x08ddb2` | Selects corresponding body/leg and two arm locomotion clips. This base-layer behaviour is already represented in the port. |
| `0x0764de` | Updates separate arm values at robot `+0x2fa8/+0x2fb4`, clamped to 0..5. Trigger state raises them; release lowers them. Firing-state values change when the arm reaches 5. |
| `0x0aa499` | Traverses the skeleton and selects each arm bank. Invokes the combat overlay when that arm's value is positive and its enable flag is set. |
| `0x0acda4` | Samples frame zero of arm-bank entries 21/22 (`+0x54/+0x58`). Uses arm value / 5 as the blend weight. Blends translation and quaternion rotation with the locomotion pose. |
| `0x0ad1a0` | Applies targeting to joints flagged `0xf0`. Distinguishes forearm flags `0x10/0x20` from the other joints; one branch clamps pitch to +/-0.8 radians. Forearm targeting preserves the joint translation. |
| `0x0ad51c` | Supplies additional node motion from body banks for flags including `0x400/0x800`, with separate frame sources. The evaluator now samples its dash, firing and jump channels, using the separate clocks described below. |
| `0x0acb36` | Saves evaluated bone matrices tagged `0x40/0x80` at robot `+0x2fe4/+0x2fe8`, which the arm muzzle calculation consumes. |

The firing entries are poses, not simply another looping animation. For the
first leg type, all four arm banks (1723..1726) have zero-frame entries 21/22
with two tracks. All 77 distinct arm banks referenced by the extracted
catalogue contain entries 21/22. This confirms availability of those entries,
not completeness of the whole combat animation system.

The extracted skeletons also retain `0x40/0x80` mount roles and body-motion
flags. Existing extraction is therefore useful; replacing the skeleton or
inventing a generic arm-raise animation is unnecessary.

The 0..5 ramp is confirmed in instructions and decompilation. Its caller
`0x075773` is the robot battle update; the verified scene clock table at
`0x255ab0` runs the battle scenes at 30 Hz (see `TRAINING_GROUND.md`).
The port therefore uses 5/30 seconds, independently of presentation rate.
A focused radare2 disassembly independently confirmed the ramp bound and
the 21/22 clip loads.

## Original muzzle path

1. `0x09d709` initializes weapon `+0xc0` from the active robot's resource
   handles (`0x2414f8..0x241504`, record stride `0x54`). Loader `0x081863`
   supplies arm attachment tables `0x25ae10/0x25ae98` and shoulder attachment
   tables `0x25b050/0x25b0e8`. These are indexed by part type and side.
   Unreferenced shoulder type placeholders are not equipped weapon models.
2. `0x098bb7` obtains the muzzle count, optionally advances weapon `+0x84`
   modulo that count, obtains a mount transform, and asks for that muzzle.
3. `0x09ade8` selects arm models from `0x25ad88/0x25ad00` and shoulder models
   from `0x25af20/0x25afb8`. Arms use the saved animated `+0x2fe4/+0x2fe8`
   matrices. Shoulders use weapon `+0x8c`.
4. `0x09a9d4` reads a muzzle entry comprising three floats and a node index
   (16 bytes). It walks the model node's parent chain, composing the node
   translation, rotation and scale. `0x09e8a3` and `0x09db7e` locate the
   relevant offset-based block and its muzzle array.
5. `0x098bb7` applies a pi rotation to the result. Certain shoulder aim modes
   reorient it while preserving the origin. It also tests the path to the
   muzzle against the stage before allowing ordinary firing.
6. `0x099c02` passes that matrix to the selected weapon fire handler and the
   firing effect path. Position alone is insufficient: direction matters.

Do not substitute skeleton effect points flagged `0x20000` for the muzzle
array. They are a separate path; the firing code above identifies the actual
mount and muzzle source. Extraction mirrors muzzle Z, matching `0x09a9d4`. Arms compose the socket
with the drawing node's own local transform and animated bone, matching the
geometry merge at `0x0a8f5a`/`0x0aa206`; shoulders retain the parent chain.

## Verification and limits

Automated coverage checks independent raising/lowering at 30/60/144 updates
per second, rigid quaternion interpolation, barrel cycling, lost arms,
same-tick projectile/effect origins, simultaneous firing while moving and
aiming, obstruction and drawing-node bindings. Extractor tests cover coordinate
conversion, barrel order and invalid attachment resources. Rear and side
captures of the running port confirm the arm switches from its hanging pose
to the raised weapon pose; this is visual review, not original-frame parity.

The earlier automated checks above predate the user's instruction not to add
tests. Subsequent animation work uses source inspection, builds and runtime
logs; those older checks are not evidence for the changes below.

`0x0ad51c` maps body flags 0x400/0x800 to bank entries 0/1 and flags
0x2000/0x4000/0x8000/0x10000 to entries 3..6. Each samples track zero using
its own clock, with the existing translation/rotation mirroring. Drivers are
0x091814 (dash, always active for leg type 20), 0x0916e9 (arm triggers) and
0x091509 (jump, with different player/AI release behavior). Defaults use a
60-frame timebase, loop/hold frame 44, return threshold 14, last frame 59,
loop span 30 and firing rate multiplier 0.5. These controls are editable in
`robot_animation.json` under `auxiliary`. Pose evaluation advances once per
simulation update; all views consume those transforms.

Shoulder `aim_mode` from weapon record +0x1e now reaches the combat pose.
Modes 0/1 orient the rendered mount and barrel toward the weapon target,
preserving the barrel position when correcting its basis (0x098bb7).
Other modes keep the mounted direction. The original keeps two sight
points for the player (0x04f6a0): +0x2688 under the crosshair, and +0x261c
under the crosshair moved by the tracked offset at +0x134. Mode 0 aims at
the first and mode 1 at the second. The port finds the tracked point under
the HUD's tracked reticle (`Battle::tracked_sight`, a few Newton steps at
the crosshair's distance from the eye). A lock on a target overrides both,
as before. AI robots have one sight point, used for both modes. Existing
missile/grenade launch rules remain in the weapon system.

Headless battles or older extractions without weapon mounts retain the old
fixed muzzle fallback. The library reports missing combat data. The standard
robot extraction now produces the required table; the local extraction has
been updated. No extracted assets belong in Git.

## Animation detail corrections (2026-09-30)

- Locomotion transition rotations now use quaternion interpolation, as
  `0x0ac757` does through `0x0dcfb3` and `0x0dd1c0`. Previously the port
  interpolated Euler components, which can turn a joint the long way around
  an angle wrap. Translation and the root-motion/foot-target treatment are
  retained; IK chains and ordinary bones both consume the corrected rotation.
  This does not reproduce the original's complete cached-world-matrix blend:
  the native transition still uses a frozen previous clip sample.
- Movement now identifies fresh animation actions independently of clip ID.
  A repeated hit reaction, a new jump using the same clip, and resetting an
  already-idle robot can restart at frame zero. Repeated updates of the same
  ongoing action keep advancing. Releasing and re-enabling a loop also reach
  the animator in both directions. `0x08ddb2` selects and initializes a motion
  without the native animator's former same-ID early return; action revisions
  are the native mechanism for distinguishing requests from updates.
- Destruction gives the death clip priority over native combat layers: arm
  weights and body auxiliary clocks become zero, and shoulder/muzzle aiming
  stops. Surviving weapons therefore follow their mounted death pose instead
  of continuing to track an opponent. This is explicit native state ownership,
  not a claim that every original destruction flag has been reproduced.
- Transition logs report robot, motion, action revision, frame, playback
  speed, loop status and both arm weights. Death start/completion logs include
  frame and arm weights. Drawing a monitor or replay does not tick the rig.

These gaps have since been closed:
- An interrupted transition keeps its fully blended pose. The animator
  holds up to four earlier motions, each with its frame and the blend weight
  it had when cut, and folds them oldest first before blending into the new
  clip (`MotionSource::blend_from`, both the track and the matrix paths).
  Each frozen motion also keeps how its root motion was split. A dodge
  (13/14) ends 2 units to the side with that offset on the feet. Standing
  keeps its sway on the body, so blending the raw tracks had briefly put
  the dodge's offset back on the body: the robot snapped back, then on to
  its new place.
- Shoulder modes 0/1 aim at separate sight points (above).
- Dash-turn clips 3/4/6/7 (`0x08f5d8`) play on eligible legs while dashing
  and turning (`movement.dash_turn_*` in `training.json`, see
  [TRAINING_GROUND.md](TRAINING_GROUND.md)). Original-code inspection confirms
  eased positive steering selects clip 3, negative steering clip 4; reverse
  dash adds three. Positive heading turns toward rider-right (model -X).
  The port's existing direction mapping matches these branches.

No replacement animation clips were generated.

Validation: the standard Release build passed on 2026-09-30. Two bounded
runtime runs exited successfully without reported errors. Transition logs
showed six separate jumps starting at frame 1, including jumps while firing;
arm weights reached 1 while firing and returned to 0 after release. Death
logs in the first run reached clip endpoints 74 and 89 after 1.233 and 1.483
seconds, respectively, with both arm weights zero. A fresh idle action also
restarted while retaining the same clip ID. Repeated hit reactions and the
visual quality of rotation blending remain unverified by these logs. No new
tests, captures, or temporary data overrides were created for this pass.

Wheels: skeleton nodes flagged 0x200 roll about x by the robot's wheel angle
(robot +0x2590) instead of the aim pitch or forearm turn (0x0aa499). 0x0889bf
advances it each 30 Hz tick: on the ground by the planar speed over pi
(negative in the walk-back and dash-back states), plus a fifth of the steering
stick while turning or nearly still; in the air by the forward stick; clamped
to 3.1677728 rad a tick. Leg types 3, 8, 9, 14, 15 and 20 have wheels.

Strafing (0x08dede, Standard controls) has no leg-type gate: every leg type,
wheels included, strafes at the stick times the robot's walk rate (+0x68),
except in the jump (4), air (5) and death (10) states and states 6, 7
and 9 (robot +0x128).
