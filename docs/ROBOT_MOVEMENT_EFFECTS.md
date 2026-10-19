# Robot thrust and fast descent

Foot contacts, jump, fast descent, dodge and dash exhaust use the existing particle bank,
including its authored rate curves, colours, size and lifetime. The native
controller starts each group on a movement transition, follows the animated
mount, and lets particles already emitted finish after the movement ends.
Landing, stagger, death and scripted scenes cancel attached emission, including
emitters waiting for their first particle. Render views and live TVs share
these instances; opening another view does not advance or duplicate them.

## Controls and movement

- A / Space: press to jump and hold briefly for lift. Release and press again
  while airborne to latch downward thrust until landing.
- Left-stick click / Left Shift: dash on the ground, fast descent in the air.
- Lateral air steering stays available during descent. Air dodges cannot
  override a latched descent. Landing and stagger clear it.
- A held descent input cannot immediately jump again after landing; a fresh
  press is required. There is no instant position or velocity snap.

`world/training.json` exposes `movement.down_boost_acceleration` (58.8 world
units/s²), added to normal gravity (24.5), and `battle.buttons.down_boost`.
Existing saved host bindings apply to the jump and dash actions. The shortcut
is a native convenience. The jump re-press is the original interaction.
Both follow the original's spent-lift gate (0x0905af): descent latches
once the lift the jump has used outweighs its remaining rise, which is
always true while falling (docs/PART_RECORDS.md).

## Source evidence

Addresses refer to the European executable. Descriptive names here are
researcher-assigned; resource paths and output filenames are extractor-assigned.

- `0x0905af` latches robot +0x2559 on a jump re-press after release (+0x2558),
  sets effect bit 4, and removes remaining upward boost. The falling-state
  branch accepts the press directly. `0x088e53` clears descent on the ground.
- `0x04476d` reads world +0xef314; `0x046aa5` initializes it to 6.0. The
  acceleration in `0x0905af` is 6.0 × 9.8. The fixed simulation applies it
  with elapsed seconds; presentation rate does not multiply it.
- `0x0905af` plays bank 3 entry 0x14 for a piloted robot. The native sound is
  `sound/sfx/battle_player/20.wav`, once on descent, with normal battle falloff.
- `0x081863` loads the per-leg resource from table `0x259358` into the robot's
  handle at `0x241508 + index * 0x54`. All 21 resources are 392 bytes: fourteen
  records of position, kind and direction (28 bytes each).
- `0x0acd11` registers animated matrices from node flags `0x40000 << slot`.
  `0x084de1` mirrors local Z, transforms position and direction, and normalizes
  direction. The `0x20000` skeleton nodes are separate contact points, not jets.
- `0x07973f` dispatches jump slots 4–5 to `0x085d1a` (group 23), descent
  slots 6–7 to `0x085e83` (group 24, replacing jump emission), and dash slots
  8–12 to `0x085ad8` (groups 17 and 18). Body type 1 omits dash slot 8.
- `0x08600d` starts two group-24 dodge jets at the selected jump mount;
  `0x08814a` reflects the second direction's Y component.
- `0x08618c` starts dash-stop group 25. The apparent repeated group-19 call
  (`0x085c60`) receives a null output pointer in `0x07973f`, and returns without
  starting an effect; the native controller does not invent a continuous loop.

`movement_mounts.py` extracts `robots/movement_mounts.json`. Positions,
directions and group assignments are editable and load through normal asset
mod overlays. A missing resource is reported rather than replaced by generic
robot-centre exhaust. Native movement state selection remains an approximation
of the source state machine, not an exact timing-parity claim.

## Ground contacts

The pose evaluator now retains the contact nodes flagged `0x20000`, transforming
just their static local position through the animated parent (`0x0ad737`).
Their scale channels encode contact timing: `0x0adc1c` reports key-cursor
changes, not a scale magnitude. Native footstep effects use those key crossings,
with no emission each rendering frame and no arbitrary repeating timer.

`0x242bd8` supplies effect groups by movement state and material class.
The port queries the contacted stage triangle for the material, starts the
landing row on ground contact, and uses the ordinary/dodge rows for animated
steps. Takeoff starts group 26 at the contact points; `0x07973f` / `0x0862c2`
limit its continuous emission to 30 logic ticks (one second). Landing or
interruption cancels it sooner. These event-to-native-state choices are
approximations; the group identities and key timing come from the source data.
Runtime contact logs report robot/foot, event, group, material and allocation.

## Validation status

Source and extracted-value inspection completed. The Release game build passed
when rebuilding with the force-field contact fix. No automated tests were added
or run.
Runtime logs identify each player's jump/descent/dash/dodge transition, started
group count and vertical speed; landing logs confirm emission cancellation.
A missing mount/group warning is emitted once per robot ownership period.
In-game placement, apparent scale and feel still require visual review.
