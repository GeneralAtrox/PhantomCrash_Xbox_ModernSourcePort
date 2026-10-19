# Original robot AI: recovered behaviour

Research and native baseline implementation: 2026-10-01.

## Scope and evidence

The recovery sections describe the European original executable. The final
section records how those findings are implemented in the native port. It is a first recovered description, not a complete
specification of every opponent or game mode. No original gameplay capture was
made during this investigation.

Primary evidence: `artifacts/europe/files/default.xbe`, SHA-256
`bdbe2c3eb2361395babfe6095d1a2da72f6581dccb34812b8d45e084c6c903c6`.
Addresses below are Xbox virtual addresses in that executable. Functions were
inspected with the existing Ghidra project and focused read-only decompilation.
Radare2 independently confirmed the NPC tick call at `0x0755d0`, the parameter
copies in `0x057820`, the indirect behaviour dispatch at `0x057ce2`, and the
table addresses and indexing in `0x05c680`. Capstone was used to locate callers.

All descriptive function, field and state names in this document are
researcher-assigned. They are not recovered original source symbols or SDK
symbol matches. Ghidra's `FUN_...` labels are generated names. x87 float-to-int
helper `0x1138ec` is not a random-number generator; some decompiled expressions
lose the floating-point calculation preceding it. Such expressions are not
treated as complete formulas here.

## What the original system does

The original has a stateful, probability-driven NPC controller, with authored
path following and additional reactive routines. It produces controls that are
consumed by the same robot movement machinery as player controls. It is more
elaborate than selecting the nearest enemy, walking to a fixed distance and
holding all weapon triggers.

### Controller pipeline

`0x0754eb` branches on robot `+0x08`: `-1` takes the NPC control path, while
player-controlled robots read input/replay controls. A living, enabled NPC calls
`0x057c71`, then proceeds through the shared robot update `0x075773`.

Each NPC has a `0x1dc`-byte controller record at
`0x471790 + robot_slot * 0x1dc`. `0x057863` initializes it. The main tick:

1. Refreshes target selection and target memory (`0x05b9c1`).
2. Updates health bands, weapon availability, target distances and counters
   (`0x058486`).
3. Invokes the current behaviour callback through controller `+0x14`.
4. Updates optical camouflage (`0x05ca2c`).
5. Runs additional reactions/overrides (`0x05949b`, except in a special route
   callback).
6. Evaluates incoming threats and requests evasive movement (`0x05d530`).
7. Scales and clamps four analogue control values to `[-1, 1]` and writes them
   to robot `+0x2f5c..+0x2f68`.
8. Processes four weapon channels (`0x05b360`), remembers current durability
   and advances counters.

### Per-NPC tuning and behaviour selection

During spawning, `0x080778` passes loadout byte `+0x58` and short `+0x5a` to
`0x057820`. They become controller `+0x3c` and `+0x40` respectively:

- **Ability parameter** (`+0x3c`, interpreted as a signed byte): influences
  target memory, control scaling, weapon decisions, detection and evasive
  reactions. `0x05f445` supplies it, with a forced value of 99 in scene mode 1.
  The identity of that scene mode is not established here.
- **Behaviour profile selector** (`+0x40`): chooses a 25-byte table at
  `0x206038 + profile * 25`. It is not simply a different movement speed.

`0x058486` computes five durability bands with
`clamp(current_durability * 5 / maximum_durability, 0, 4)`.
`0x05c680` uses a situation and the durability band to select a row of decision
thresholds. In the inspected function, situations include timer expiration,
having a target, a durability change, and the no-weapon-availability flag.
Later checks override earlier ones. The ordinary threshold table is `0x205e88`;
scene mode 1 substitutes `0x205f60`.

The five choices are evaluated sequentially through `0x05be18`, with state IDs
`[0, 1, 5, 3, 6]` from `0x2062a0`. These are **sequential chance checks**, not
five normalized weights to add together. There is also a chance to turn choice
0 into choice 1 and a fallback to the existing state.

`0x058846` changes state, installs a callback from `0x2061e0`, loads an
ability/state-dependent duration from `0x2062e8`, and establishes a four-second
transition guard using the game's timing-rate variable. This gives behaviours
persistence rather than making a fresh unrestricted choice every frame.

The recovered primary callback families are:

| State | Entry / running callback | Demonstrated actions; labels are descriptive |
|---|---|---|
| 0 | `0x058938` / `0x0589e7` | Random movement subchoice, movement-button requests, target turning and weapon requests; timed reevaluation. |
| 1 | `0x058ae3` / `0x058b66` | Authored route following, with target aiming and weapon requests alongside it. Initial state after controller setup. |
| 2 | `0x058cb0` / `0x058d0e` | Clears analogue movement and draws one random number (`0x044705`, stored at `+0x1c` but never read back); requests eligible weapons and reevaluates. Unreachable (see below). |
| 3 | `0x058d69` / `0x058e44` | Target-engagement family: multiple subchoices, aiming, movement/jump requests, firing, and a route-following subchoice. |
| 4 | `0x0593e3` / `0x059440` | The same as state 2 except at entry: it sets substate `+0xc` to 0 instead of drawing a random number. The running callbacks are instruction-for-instruction equivalent. Unreachable (see below). |
| 5 | `0x059d62` / `0x059d8d` | Withdrawal-like movement: positive ability requests reverse movement while aiming/firing at the target; nonpositive ability takes a different forward/turn branch. |
| 6 | `0x059ea9` / `0x059ed8` | Special route-following behaviour that can still aim/fire; bypasses one common reaction routine. Do not call it an exit/retreat state without further evidence. |

Callbacks can change without changing the primary state number. This is not a
complete seven-state diagram: there are additional substates and overrides.

**States 2 and 4 are never entered.** `0x05c680` picks from `state_order`
(`0x2062a0` = 0, 1, 5, 3, 6; read only at `0x05c7ed`) and otherwise keeps the
current state; every direct `0x058846` call passes state 1 or 3; and the
callback table `0x2061e0` is read only at `0x0578db` (setup) and `0x058872`
(`0x058846` itself). Nothing selects state 4, so its one difference from state
2 (substate 0 instead of a random draw at entry) is never observable. The port
does not implement either state (`AiMode` keeps their numbers as
`stationary` / `stationary_alt` for the table layout only).

### Seeing and remembering opponents

`0x05b9c1` starts with candidates from a per-robot awareness structure, filters
by distance below 100 original world units, and has separate selection branches
for player-controlled and NPC-controlled opponents. It is not just the port's
nearest-hostile scan.

`0x05d11e` checks horizontal and vertical angular limits and a target eligibility
byte. Optical camouflage is explicitly considered through `0x074e2a`: detection
can be permitted by controller flags, proximity below 5 world units, or weapon
activity, using ability-dependent chance checks. Cloaked robots are therefore
not always categorically invisible to the original AI.

It also retains a target slot and a countdown instead of discarding every lost
candidate immediately. The inspected memory assignment is
`(ability + 127)^3 / 100000`, using integer arithmetic, followed by countdown
and checks that the remembered robot still exists and is alive.

The awareness structure is `*(globals + 0xee674 + robot * 0x30)` (accessor
`0x046844`). Its producer `0x04f152` writes, for every other robot `j`, a
0x20-byte record at `+0x48 + j * 0x20`: distance, bearing off the heading plus
twist, absolute elevation (`atan2(dy, horizontal)`), an in-frustum byte,
projected coordinates and a line-of-sight-blocked byte. The block test is a ray
(`0x0539ce`) from the robot's centre (`+0x454c`) plus the stage objects
(`0x06fd32`); the result for `j < self` is copied from the pair already
measured. Enemies (different team) are listed at `+0x114` (count `+0x12c`) and
bubble-sorted by distance.

`0x04f6a0` then picks the robot's lock (`+0x130`): the nearest listed enemy
that is visible, not cloaked, not blocked, in the frustum, closer than 75
(`0x19b55c`) and inside the sight's inner box (`0x0525b0`). It also counts, at
`+0x89c[j]`, the robots locking each robot.

Selection in `0x05b9c1` walks the enemies closer than 100 (`0x17ccd4`),
nearest first, in two passes:

1. A computer-controlled candidate (robot `+8 == -1`) is taken only when it is
   this robot's lock (`+0x130`). A player-controlled one is taken when
   `0x05d11e` sees it. The first match wins.
2. With no match, any candidate `0x05d11e` sees. Holding no target it takes
   the first (nearest). While a target is held, every seen candidate
   overwrites the choice, so the last (farthest) seen wins.

`0x05d11e` tests `|bearing| < ctrl[0x23]`, `|elevation| < ctrl[0x24]` and the
blocked byte, then camouflage through `0x074e2a`.

`0x05d2bd` (retaliation) runs after selection, only when the robot holds no
target (`+0x48 == 0`) and is not in the special route (`0x05f47f`: callback
`0x059ed8`). In order:

- **Attacker.** `0x05d4b7` walks the six robot slots of the robot's pending
  damage (`robot + 0x27a0 + j*4`, through `0x04678c`). That array collects the
  hits since the robot's last update; `0x07675e` applies and zeroes it every
  tick. It returns the first slot with damage whose class is below 2. A class-2
  slot (a ranker or champion) is returned only on a 10/255 chance; otherwise
  the walk carries on.
- **Chance.** With the attacker uncloaked (`0x074e2a`), (ability + 127)/255.
  With it cloaked, max(ability, 10)/255. Always when the robot's own class
  (`0x05ec7d`) is above 0.
- **Class.** `0x05ec7d` returns 2 for entrants 0, 1, 10 and 19 (the rankers).
  Otherwise it returns loadout `+0x6c`, which `0x0456b7` copies from the
  roster: member 0, leader 1, champion 2. `0x07675e` reads the same field
  (`== 2`) for the "hit by a rival" line. A reserve's is 0: `0x045270` seats
  the members (`0x045987`, class 0), the leader (1), the champion (2,
  `0x0459ed`) and then the reserve team `0x24b3a4` through `0x0462cd` with
  class 0; `0x045a68` copies that class unchanged and marks the reserve with
  `+0x70` = 3.
- **No side.** Neither `0x05d4b7` nor `0x05d2bd` asks which side the attacker
  is on; the original has no teams in a battle.
- **Supply chase.** While fetching a supply box (flag 4, record `+0x15c`) of
  kind 4 to 6, it carries on and keeps no target.
- **Result.** Otherwise it takes the attacker. Its memory becomes game rate × 2
  + ability × rate / 10 ticks (2 s + ability × 0.1 s at 30 Hz; not the
  sighting's (ability + 127)³ / 100000). It clears flags 0x1e (supply chase,
  jump), sets `+0x132` to rate × 8 and enters state 3. `+0x132` is the
  rankers' choice timer (below): a provoked ranker fights in the common states
  for 8 s before its own callback chooses again. Other robots never read it.

### Navigation and getting unstuck

`0x05efc1` / `0x05f0b3` attach a controller to authored paths using position,
facing direction, allowed segment types and grounded/airborne context.
`0x05a2a9` advances through curve segments and chooses among linked successors,
including a random branch and type exclusions. This matches the authored path
graph approach, not a recovered navmesh or global shortest-path planner.

`0x05a5dc` steers toward the path, handles airborne movement, and has timed
responses to obstruction/contact conditions: reverse movement, pauses,
alternative path selection, lateral evasive inputs and jump inputs. The jump
bit `0x04` is consumed by `0x08d7fa`; lateral bits `0x10/0x20` by `0x08d98c`.
Path segment types also affect speed and waiting behaviour.

`0x05eb80` periodically reacquires a route after a four-second counter unless
progress resets it. This is evidence of recovery machinery, not evidence that
it reliably escapes every corner, prop arrangement or ramp.

### Aiming and firing

`0x05c1fb` generates horizontal turning and vertical aiming controls from target
angles and the robot's turning capability. It does not simply teleport the
robot's heading to its target.

`0x05b139` requests weapons. Every running callback calls it while a target
is held (`+0x48`; states 2, 4 and 6 call it unconditionally): when fewer
triggers are held (`+0xec`, counted at the end of the last `0x05b360`) than
`(ability + 128)^2 / 10000 + 1` at the *live* ability, each weapon whose band
passes `0x05d28c` (`minimum_range <= target_distance < maximum_range`) has its
request byte set. A request is sticky: only a pull clears it.

`0x05b360` runs once per controller tick (`0x057f4f`) over four weapon records
at `+0x94`, `0x14` bytes each, built by `0x0582b0` / `0x05bf22`:

| Offset | Meaning (researcher-assigned) |
|---|---|
| `+0x00` | Pointer to the trigger byte at `+0xe4 + slot`; `0x0762e8` fires each tick while it is set. |
| `+0x04` | Request byte (`0x05b139`). |
| `+0x06` | Hold, in ticks. Counts down while the gate below passes. |
| `+0x08` | Delay, in ticks: a one-time wait before the first pull. |
| `+0x0c` | Weapon record: subtype `+0x1c` (0 cannon, 1 missile, 2 machine gun, 5 beam, 6 melee) and aim mode `+0x1e`. |
| `+0x10` | Range band for `0x05d28c`. |

`0x05b1ed` caches the timing in ticks at the game rate: fire probability
`+0xee` = `(ability + 45) x 256 / 125`, interval `+0xf0` =
`((1/30 - 0.5) x ability + 0.5 x 99 - 45/30) / 54` s, `+0xf2` =
`(ability + 99) / 198` s (not read by the firing code), first delay `+0xf4` =
`(70 - ability) / 70` s, and jitter `+0xf8` = 0.2. `0x05bf22` starts each
record with hold = interval x (1 + 0.2U) and delay = first delay x (1 + 0.2U),
where U = `0x044705` / 32767; each value is truncated to whole ticks.

Each tick, the beam sustain `+0x166` counts down. The gate passes for a held
target (memory, not sight; at ability 0 no target is needed here) that
`0x05b992` finds unblocked (the awareness record's line-of-sight byte), and
above ability 0 only inside the robot's view (the record's frustum byte). The
sight angles of `0x05d11e` do not apply to firing. While the gate passes, for
each weapon:

1. A weapon is *forced* when its subtype's bit is set in `+0x158` (beam 1,
   missile 2, cannon 4, machine gun 8). The hold counts down by one; unless
   forced, nothing more happens while it is 1 or more. **A pulled trigger
   therefore stays held for the whole hold**: a machine gun streams, a cannon
   refires at its own rate.
2. With no request, the trigger is released and the hold restarts.
3. The delay counts down, and nothing more happens until it was already
   negative. It counts only on these ticks (hold expired, requested) and is
   never reset. It is a signed short, so after about 32 768 such ticks it would
   wrap and block that weapon again; the port does not reproduce the wrap.
4. `0x05d0be` scales the probability: x0.5 (`0x17accc`) in state 5, x1.5
   (`0x199fb4`) when `+0x7c` is 3. `0x05cfcc` sets `+0x7c` from the robot's
   heading (`+0xec`) minus the target's: 3 below `cos(pi/4)` (`0x052a40` of
   `0x3f490fdb`), otherwise 0. Its classes 1 and 2 need the difference between
   0.7071 and `cos(pi/3)` = 0.5, an empty range, so they never occur; the
   x1.25 branch (`0x19f8fc`) repeats the test for 3 and is dead. A forced
   weapon uses 256 (always).
5. A beam copies its sustain `+0x168` into `+0x166`, before the roll.
6. `0x05be18(p, 256)` rolls. A failure releases the trigger and restarts the
   hold.
7. On success the aim mode decides. Mode 1: a melee weapon (subtype 6) pulls
   only when byte `+0x19` of the `0x046844` record is set (not identified);
   other mode 1 weapons pull. Modes 2 and 3 pull only when there is no target
   or the robot's lock table (`+0x8ae + target index` in the same record) has
   the target; otherwise nothing changes and the roll is made again next tick.
   Every other mode pulls. A pull sets the trigger, clears the request and
   restarts the hold.

Then, gate or not, a beam weapon whose sustain is still running is pulled
again (the hold restarts). Every other trigger the gate did not serve is
released. Finally `+0xec` counts the held triggers and `0x0762e8` fires each.
Entering any state releases every trigger (`0x05bdce`); requests, holds and
delays carry on.

`0x05ed21` sets the per-robot extras at setup. In the attract demo and RESULT
backdrop (quest `+0x618c` 1) the cannon and machine gun bits (4 | 8) are
forced. The rankers (entrants 0, 1, 10 and 19) get a beam sustain of 5 s
(`0x19b544` x the game rate; `+0x16a` = 10, or 50 for entrant 1, which also
gets `+0x16f` = 1 and the 4 | 8 bits; those two fields are not read by the
firing code). Any other robot whose class (`0x05ec7d`) is above 1, a champion,
gets 0.5 s (`0x17accc`) and the 4 | 8 bits. `0x05b061`'s flag is not read.
Availability is refreshed in `0x058486` / `0x05c04c`, and unavailability
participates in state selection.

### Camouflage and threat reactions

`0x05ca2c` explicitly requests the same camouflage input bit (`0x08`) consumed
by the robot camouflage update. It uses on/off counters, probability gates,
target/threat flags and controller-specific conditions.

`0x05d530` scans several threat lists. It uses projectile positions, velocities,
distance, facing/dot products, ability-dependent thresholds and chance gates.
On a positive reaction it randomly requests one of the two lateral evasive
directions. One threat category is checked only every fifteenth controller
update. This is reactive evasion; no predictive trajectory optimizer was
recovered.

### Jumping at a target off the sight

`0x05949b` holds a second reaction besides the supply chase. At 15/255 a tick
it starts a jump (`0x059799`) when all of these hold:

- the robot is not in the special route (`0x057c71` skips `0x05949b` when
  `0x05f47f` holds);
- the quest's stage index (`+0x6104`) is not 2 or 3, which is RUMBLING area 1
  (area = index / 2);
- the robot is not in `0x06e331`'s zone 1;
- it carries no melee weapon (kind 6, `0x05ec15`);
- it is not airborne (motions 4 and 5);
- controller flag 2 is clear;
- it has a target 5 to 20 units away (`0x19b544`, `0x199f7c`), feet to feet;
- the target is more than 15 degrees (immediate `0x3e860a92`) off the sight
  direction (`+0x2688`).

Zone 1 is a fixed circle (centre 30.522, 59.594; radius 39.295;
`0x1b5a5c..0x1b5a64`). It covers ground above −2.5 outside the circle, or at
−0.5 and above inside it; zones 0 and 2 to 4 are lower floors. The circle
describes area 1: `0x059fb0` and `0x07973f`, the function's other callers,
consult it only when the stage index is 2 or 3. `0x05949b` skips area 1 outright
and then applies area 1's circle in every other area, where it means nothing in
particular. RUMBLING area 0's streets stand near −3, below it, so the jump
happens there.

The states, one controller tick each:

- **Press (`0x059799`).** Lets go of every stick (controller `+0x60..+0x6c`).
  It presses robot input bits 2 (held) and 4 (fresh), the bits the jump
  button (`+0x2f88`) sets in `0x0774ad`. `0x05b061` sets bit 1 of each
  weapon channel's flag word (controller `+0x94 + slot × 0x14 + 0x0a`) at
  chance (ability + 128)/255. Nothing in the XBE reads it: the only accesses
  are this write and the clears in `0x05bf22` and `0x05e1ec`; no code uses the
  channel's absolute addresses or `+0x9e/+0xb2/+0xc6/+0xda`, and no other
  `+0x94` record walk reads `+0x0a`.
- **In the air (`0x05981c`).** Without a target it returns to state 1.
  Otherwise it requests weapons (`0x05b139`), pushes forward at full stick
  (`+0x60 = 1.0`) and aims (`0x05c1fb`, flags 0xc) at the target's awareness
  record: where the target is, seen or not. It holds bits 2 and 4 in
  motion 4 (rising) and bit 2 in motion 5 (air). The controller tick zeroes
  the input bits first (`0x057c71`), so the fresh bit never reaches motion 5.
  In `0x0905af` a held jump keeps its full lift, and no descent latch fires.
- **Landed.** Any other motion lets go, sets the aim timer to 30 × 3.0
  (`0x17cce0`) ticks and moves to `0x059921`.
- **Aiming (`0x059921`).** Aims while it still holds a target, and counts
  down whether or not it does; then state 1. It never resets the forward
  stick, and the controller's sticks persist, so the robot keeps walking
  forward at full stick through the three seconds.

Quest `+0x618c == 1` (the attract demo and RESULT's backdrop battle) makes
`0x05f445` report ability 99 for every opponent. With it, `0x05949b` chases a
supply box of any kind, money included.

`0x05ddb5` / `0x05de0f` also install special behaviour callbacks for loadout
`+0x64` values 0, 1, 10 and 19. These use durability bands and weapon-policy
flags. `0x0456b7` copies the roster entrant ID to loadout `+0x64`, so these are
entrant IDs. The native implementation reads those same IDs from the roster.

`0x05949b` calls `0x05ddb5` first, so the ranker's choice runs every tick after
the state callback, except in the special route (`0x05f47f`). `0x05de0f`
counts `+0x132` down (held to ±20000). When it reaches 0 with no flag 0x1e
set, it sets the band's weapon policy and chooses: the attack callback
`0x05e660` when it holds a target and the band is below full (full: 40/255),
else the route callback `0x05e494`, or `0x05e660` when the count `+0x12c`
is odd (not reproduced). Past rate × 180 on that count with a target it also
turns defensive. It
writes rate × 4 for the attack and rate × 8 for the route, but both callbacks
write rate × 8 as they start the next tick, so either choice holds 8 s.
`0x05e292` and `0x05ea6f` fall back to `0x05e494` (and 8 s) when the target
is lost. Its cases 1 and 2 (`0x05e157`, 4 s; `0x05e363`, 5 s) are unreachable:
the choice is only ever 0 or 3.

## Important limits and corrections

- Loadout `+0x5c` was previously labelled an AI index. The spawn/input path
  disproves that interpretation: negative values select NPC/inactive cases;
  nonnegative values are used for player/controller ownership. `0x080778`,
  `0x0825df` and `0x046f81` support the correction. Behaviour tuning comes from
  `+0x58/+0x5a`.
- Robot `+0x34ac` and callback table `0x249848` concern movement sound families,
  not tactical AI. They must not be used as evidence of behaviour profiles.
- **Initialization order (resolved).** The spawn path `0x080778` calls
  `0x057863` at `0x0809da` and the parameter setter `0x057820` at `0x080a1c`,
  in straight-line code. `0x057863` clears the controller and runs the derived
  setters `0x05b1ed` (firing), `0x05c905` (sight), `0x05cce9` (movement
  chances) and `0x05ed21` (forced weapons, beam sustain); each has that single
  caller. They read the ability through `0x05f445`, which returns the cleared
  `+0x3c`, 0, except in the attract demo and RESULT backdrop, where it returns
  99. Nothing refreshes them afterwards. So for every named opponent the cached
  settings are ability 0's: sight pi/6 in yaw and pitch, fire probability
  92/256, interval 0.889 s, first delay 1 s, camouflage at ability 0, and no
  random movement chances (`0x05cce9` sets none below ability 1). Everything
  that calls `0x05f445` per tick uses the assigned ability: state durations,
  memory, control scale, the request limit, the firing gate, cloak detection,
  retaliation, reactions and the supply chase.
- Some original branches use fixed six-slot loops. Their assumptions cannot be
  copied blindly into the port's larger robot populations.
- Follow-up inspection of `0x0678f8` identifies `0x9668a0`, the existing supply
  pickup array (three 0x28-byte records). `0x05949b` / `0x059a1d` divert toward
  the nearest falling/landed pickup within 25 units, without an enemy target.
  Ability >=31 rejects kinds 0..3 (money); the chase expires after five seconds.
- No squad coordination, cover scoring or long-term tactical planning was
  established in the inspected routines. This is a recovery limit, not proof
  that no related behaviour exists elsewhere.

## What this tells us about the port

The current `port/src/battle_ai.cpp` finds the nearest eligible opponent, turns
and moves forward/backward around one desired distance, then fires valid locks
after range and obstruction checks. The recovered original adds substantial
behaviour on top: persistent probabilistic states, ability/profile data, target
memory, angle-limited detection, conditional camouflage detection, vertical aim,
weapon-specific decisions, jump/evasion requests and route recovery.

The original still plausibly feels poor because it relies heavily on authored
routes, local steering, timers and random choices. That is an inference about
the recovered design, not a measured rating of its gameplay. A modern tactical
planner would be a change in design, not a description of what has been recovered.

The visible implementation is small fixed controller records, table lookups,
bounded opponent/weapon loops and geometry checks; it does not reveal an
expensive global planning system. However, **original AI CPU cost has not been
profiled**, and route/threat processing prevents concluding that every tick is
constant-cost. No performance number is claimed.

## Native baseline implementation

The live RUMBLING now uses `robot_ai` and the `battle_ai` adapters instead of
nearest-enemy / fixed-distance steering. Training's frozen and scripted drones
retain their lesson controls. `port/tests/battle_ai_tests.cpp` covers the lock
rule, the second pass, retaliation and the provoked ranker's hold, the jump
reaction, the showcase ability and the port options.

`data/world/robot_ai.json` contains the twelve original 25-byte profiles,
42 five-byte decision rows, state order, search weights, and 100 ability rows.
Regenerate it with:

```powershell
$env:PYTHONPATH = 'src'
python -m phantomcrash_research.robot_ai_tables --xbe artifacts/europe/files/default.xbe --output data/world/robot_ai.json
```

The extraction addresses are recorded in that module. The names remain
research labels. `mods/data/world/robot_ai.json` can override the tuning.
Entrants now expose `ai_profile`; existing extractions using `word_4` also load.
`0x0456b7` copies the entrant level into ability separately from the part-level
adjustments, so New Game Plus part increases do not automatically raise ability.

Implemented policies:

- State decisions run at 30 Hz in both visible and offscreen battles. Health,
  target, elapsed-state and ammunition situations select the recovered rows.
- Per-entrant ability controls sight angles, target memory, probability and
  firing delays. Targets can be lost, searched for, or reacquired. Current
  collision rays gate visibility and firing.
- Retaliation follows `0x05d2bd` / `0x05d4b7`. It acts only with no target
  held, blaming the first robot that hurt the drone since its last AI tick
  (the change in `damage_from`). Class comes from `ranker_entrants` and the
  roster source (`marker_colour`: member 0, leader 1, champion 2). The chances,
  the supply exception and the 2 s + ability × 0.1 s memory are `ranker_class`
  / `retaliation_*` parameters.
- Firing follows `0x05b360`'s gate: a remembered target with a clear line of
  sight, in the robot's view above ability 0. The weapon lock and
  `target_visible` (cloak detection) still apply.
- The attract demo and RESULT's backdrop set `DroneAi::showcase`: every
  opponent plays at `showcase_ability` (99) and chases any supply box.
- Selection follows `0x05b9c1`'s two passes: a computer rival must be the
  robot's lock (`Battle::ai_lock_target`, after `0x04f6a0`, with `lock_range`
  and the sight's inner box). A player's robot must be seen. The second pass
  keeps the last seen while a target is held, as the original does. The
  elevation limit is absolute, as in `0x04f152`.
- The jump at a target off the sight follows `0x05949b` / `0x059799` /
  `0x05981c` / `0x059921`, including the stick release on the press and the
  forward push through the aim. Its range, angle, chance, aim time, skipped
  area and `0x06e331`'s circle are `jump_reaction_*` / `high_ground_*`
  parameters.
- The route, engagement, stationary and withdrawal families produce native
  robot controls. Existing ENPT polylines, slower route kinds, pauses and
  obstacle recovery drive movement; there is no new global path planner.
- Weapons follow `0x05b139` / `0x05b360`: sticky requests, a one-time delay,
  probability rolls and triggers held for the whole hold, which also covers
  the native arm raise. Only lock-on weapons (aim modes 2 and 3) wait for a
  lock.
- Camouflage uses the recovered ability curves and the port's gauge, drain and
  recharge rules. Cloaked opponents are detectable by the ability checks.
- Incoming projectiles can provoke dodging and camouflage. The ranker entrant
  cases use their health-dependent route/approach, close dodge and weapon policies.
- With no enemy target, NPCs can approach supply pickups within 25 units and
  jump for an overhead box. Ability >=31 skips money, and a chase expires after
  five seconds or when the box is collected/destroyed.
- Death, removal and respawn reset runtime AI state; roster ability/profile stay
  attached to the opponent. Scripted door entrances keep control of movement.

### Native integration limits

This is a recovered-behaviour baseline, **not a complete executable-equivalent
translation**. In particular:

- The cached settings (sight, firing, camouflage, movement chances) use
  `setup_ability`, 0 or the showcase's 99, as the original's setup order gives
  them; per-tick reads use the assigned ability. `assigned_ability_settings`
  derives the cached ones from the assigned ability instead.
- Visibility, projectile threats, usable weapon ranges and movement outputs use
  the existing native collision, weapons and locomotion systems. Perception is
  computed on demand from the same quantities `0x04f152` records (distance,
  bearing, elevation, frustum, line of sight); no per-robot record table is
  kept. Triggers follow `0x05b360` natively (holds, delays, sticky requests,
  forced weapons, beam sustain, the lock rule for aim modes 2 and 3) in whole
  30 Hz AI ticks; a held trigger fires at the weapon's own rate through the
  native weapon code, including its raise pose. The melee flag (`+0x19` of the
  `0x046844` record) is not identified and is taken as met; the lock is the
  port's per-mount lock rather than the record's per-robot lock table. A lost
  weapon has no record.
- A reserve's class is 0, as in the original. The player's is taken as 0.
- Retaliation asks no side, as in the original. No weapon hurts a teammate in
  the port, so `damage_from` never names one; a teammate target would also be
  dropped by sensing on the next tick.
- A provoked ranker (`retaliation_ranker_hold_seconds`, `+0x132`) runs the
  common states until its choice timer runs out. The timer only runs while
  `ai_move` does (not during a supply chase or the jump); the original's runs
  in every tick but the special route's.
- `0x05b061`'s per-weapon flag is not reproduced: nothing in the XBE reads it.
- Route recovery and search timing include native bridge parameters alongside
  recovered constants. Special movement callbacks are expressed through native
  movement controls; they do not overwrite the player's camera or teleport NPCs.
- The supply candidate is now resolved and implemented: idle opponents can chase
  nearby supply boxes, with the original money/ability restriction and timeout.
  Pickup collection uses the existing collision handler. The original candidate
  marker write appears to use the finished scan index; no out-of-bounds write is
  reproduced in native code. Replay does not rerun this controller.

### Port options

Five switches in `live.ai` (`world/rumbling.json`, overridable from a mod)
change original quirks. All are off by default, so the defaults stay the
original's:

| Option | Effect |
| --- | --- |
| `nearest_fallback_target` | `0x05b9c1`'s second pass keeps the nearest seen rival instead of the last seen while a target is held. |
| `jump_keeps_target` | Target memory does not run down during the jump and the aim after it. Weak pilots (0.68 s of memory at ability 0) otherwise lose the target mid-air and go back to their route. |
| `rivals_by_sight` | A computer rival is taken on sight like a player's robot, not only once locked. Opponents then fight each other as readily as they go for the player. |
| `jump_any_floor` | Area 1's floors (`0x06e331`) no longer block the jump in other areas, where they describe no real ground. |
| `assigned_ability_settings` | The cached sight, firing, camouflage and movement-chance settings are derived from the robot's assigned ability, not ability 0 (see the initialization order above). Opponents then see in a wider cone, fire and cloak by their ability, and make random jumps, dashes and dodges from ability 20. |

### Port navigation (`live.ai.navigation`)

The original steers in a straight line to the next ENPT point and escapes
obstructions with timed taps and reversals (see Navigation and getting
unstuck). On the real stages that leaves robots pressed against walls and
props for long stretches. `live.ai.navigation` replaces only the steering
between route points. Route choice, the ENPT graph and the fight logic stay
the original's. This is a port addition. It is on in the shipped data because
it was requested; `"enabled": false` gives back the original steering. The
code default is off.

- **Grid** (`nav_graph_build.cpp`). The grid is built from the stage
  collision, one cell every `cell` units, with a node per standable level.
  Nodes need `headroom` above them and must not be enclosed by walls.
  - Walk links join neighbours within `step` of each other's height and pass
    knee and chest tests against walls.
  - Ledge nodes also get held-jump links, up to `jump_rise` and across gaps
    up to `jump_cells`, and drop links down.
  - The grid builds once per stage on a worker thread and is shared by all
    battles on that stage. It takes 0.4–0.9 s per stage; until it is ready,
    robots use the original steering.
- **Following** (`battle_ai_steering.cpp`). A robot runs A* to its route
  point, string-pulls the walk legs, and steers waypoint to waypoint.
  - On a jump link it stops and faces the ledge, then presses jump and walks
    on in the same tick. It holds the jump until its feet are
    `jump_hold_over` above the landing, so it has time to move over the
    edge. It lets go once, because a second press would dive. It presses
    only when the mover takes the press (`RobotMover::jump_ready`: on the
    ground, jump released, not in the 0.25 s landing). Before, it walked
    into the wall under the ledge, was lifted off the ground for a tick, and
    each landing ignored the press: at Shinjuku (128.5, −1.4, −69) about
    two thirds of its presses did nothing.
  - If it makes no progress for `stuck_seconds`, it hops.
  - After `give_up_seconds` it replans without shortcuts. If it is still
    stuck after that, it avoids the waypoint for `avoid_seconds`.
  - When the route point cannot be reached on the grid, the original's
    rejoin picks a reachable ENPT point instead.
  - **Cost**. A plan (A* and string-pulling) on Umihotaru's 23,600-node
    grid takes about 1 ms on average and up to 5 ms. A robot replans when
    its goal moves `goal_moved` units, but no sooner than
    `replan_min_seconds` after its last plan. At most `plans_per_tick`
    searches run per battle tick for all robots together (0: no limit). A
    robot over the limit keeps its route and plans in a later tick. Without
    these limits, 9 robots chasing moving targets stacked several plans into
    one tick, and frames missed a 144 Hz refresh.
    The same budget covers the two other graph searches. A high-ground
    search waits for a tick with a search to spare. A route rejoin's reach
    test waits too, and is a flood without costs (`reachable_from`, about
    1 ms on Umihotaru; the costed search took up to 8 ms). The rejoin then
    sight-tests route points nearest first and stops at the first clear
    one, picking the same point as before. Before, it tested every closer
    candidate, up to 200 sight tests and 17 ms in one tick. With none in
    sight it still tested all of them (119 sight tests, about 10 ms), so
    only the nearest `rejoin_sight_tests` reachable points are tested; with
    none of those in sight it takes the nearest reachable one, which a
    planned route reaches anyway. Without the grid, every point is tested
    as the original does.
    Robots also think in turn: the AI steps at 30 Hz in a 60 Hz battle, and
    each robot starts half a step apart from the one before it, so half of
    them step on each tick. Otherwise all of them step together on every
    other tick.
    Measured on Umihotaru night, 10-robot Rumbling at 2263x1272, three 30 s
    runs each: the 99th-percentile frame went from 8.4–11.2 ms to 7.3–7.9 ms,
    and frames under 10.4 ms from 98.2–99.9% to 99.9–100%.
- **High ground**. In a fight, every `vantage_seconds` a robot with a ranged
  weapon may, with `vantage_chance`, pick a spot it can reach.
  - The spot must be at least `vantage_rise` above its target, within the
    weapon's range times `vantage_range_share`, and have a clear line of
    sight to the target.
  - The robot climbs there, then fights from the spot without closing in.
  - It also uses the grid to chase a target it has lost sight of, or one on
    a different level.
- **Room for the body** (`nav_graph_build.cpp`). A jump link needs the
  robot's whole body (a sphere of `body_radius`) to rise clear above the
  take-off, not just its middle. Before, take-offs beside an overhang (for
  example Shinjuku near (112, −84) and Umihotaru's edge near (−144, −20))
  hit the overhang's underside, rose 1.6–2.6 units and fell back, again and
  again. A robot also walks to within `takeoff_reach` of a take-off before
  jumping, and a route that starts with a jump first walks to the take-off.
- **Authored climbs** (`battle_ai_steering.cpp`). ENPT climb segments
  (kind 7) with a point no grid node lies within `point_rise` of are left
  out of route following and rejoins while the grid is in use. Shinjuku's
  segment 116 climbs to a roof at y 25.5 that no jump reaches. Robots
  rejoined it, could not climb, and went back to it, near (9.4, 10, −5.9).
  A rejoin also skips route points the robot already stands at, such as
  single-point dead ends.
- **Escalation**. A robot that is no more than `escalate_distance` from
  where it started trying, after `escalate_seconds` or
  `escalate_recoveries` give-ups, drops its goal. A high-ground spot is
  avoided and another is picked. A route point is skipped for the next one.
  If it is still stuck after the next escalation, it uses the original
  steering for `fallback_seconds`.
- **Crowds**. A robot stalled against another robot within `crowd_gap` ahead
  of it sidesteps instead of hopping into it.
- **Each robot's own jump** (`battle_ai_reach.cpp`). Legs and load set how
  high a robot jumps (`movement.jump_height`), but the grid's jump links go
  up to `jump_rise` for every robot. A robot plans, rejoins and picks high
  ground only over jumps its own held jump (`held_jump_rise`) clears by
  `jump_margin`. A robot that jumps and lands back below the ledge avoids
  that landing for `failed_jump_seconds` and plans another way.
- **Unreachable route points** (`battle_ai_navigation.cpp`). A route point
  with no route to it (another level, or a ledge out of reach) is skipped
  instead of walked at in a straight line. After `route_skips` in a row the
  robot rejoins the route elsewhere. Before, robots on Shibuya's lower level
  walked into the wall under route points on the street above.
- **Placement**. A robot placed or respawned more than a robot's height
  under the floor is lifted onto it. Some ENPT points lie under the street
  (Shinjuku segment 68 at y −4, 5 units under the street), and robots
  scattered or respawned there fell below the world.

Two collision fixes in the same work apply to every robot, the player's too:

- **Head bumps** (`battle.cpp`, `head_bump_drop`). The head sphere ended a
  jump on any downward push. Rounding against a wall pushed it down by about
  2e-6 units, so jumps beside a wall stopped dead at the bottom. A push under
  0.01 units no longer counts.
- **Return point** (`battle_damage.cpp`, `stage_floor`). A robot that steps
  where the stage has no ground goes back to the last place it stood. That
  place was recorded whenever it was grounded, also while propped on a
  wall's edge above the floor. Put back there, it fell past the edge again
  every few frames, never landed and so could never jump out: a robot hung
  at Shinjuku (120.1, 0.9, −36.8) for 200 s. Only a place where it stands
  on the floor is recorded now.
- **Knockback through walls** (`battle_damage.cpp`, `recover`). Knockback
  carries a robot up to 5 units an original tick, farther than its radius.
  In one step it went past a thin wall's middle and came out the far side:
  a hit put a robot outside the arena at Shinjuku (186, −3, 5) for 130 s.
  It now moves in steps of half its radius, pushed out of walls after each.
  Distance and fade are unchanged.
- **Robots parting through walls** (`battle.cpp`, `separate`). Two robots
  that overlap are pushed apart, each by up to its radius in one tick. A
  robot landing on another at Shibuya's pit shoved it 1.5 units through the
  pit wall into the solid under the ledge at (40.4, −68.4, 88.2), where it
  stayed 250 s. The parting push now moves the same way as knockback
  (`Battle::carried`).

Drop links need the whole body to walk off the lip (`nav_graph_build.cpp`
`drop_clear`): a line at head height (`headroom`), and a sweep of a
`body_radius` sphere toward the landing that no wall may turn back. Shibuya's
pit rim at (−69.6, −48, −32.5) hangs 2.5–4 units over the edge, and a wall
beside the drop at (−35, 0.1, −15) left robots 0.6 short of it. The sweep
adds 0.3–0.5 s to each stage's grid build.

Neither trap left after the jump fixes was a hole in the stage. The robot
in the Umihotaru hollow under the steps at (−152.7, −0.5, −13) had been
knocked into the step slabs, and the knockback steps stopped it. The Shibuya
pit pocket was the parting push above. After both fixes, no robot in the 7
batches is blocked for 10 s or more at once; the longest is 8.5 s.

**Level fixes.** A stage entry in `world/stages.json` can name
`"collision_fixes"`, a data document in the collision files' layout
(`{"floor": [...], "walls": [...]}`, each triangle
`{"points": [[x, y, z] x3], "surface": n}`). Its triangles are added to the
converted collision when the stage loads, in battle, rumbling and training
alike (`stage_fixes.cpp`). The extraction is never edited, and a mod
replaces or empties the file path for path. No stage needs one yet.

**Trap scan.** `phantomcrash_ai_sim --areas N --trap-scan RISE` looks over a
stage's whole grid at jump rise RISE (0: any rise the grid links). It lists
the places with no route back to the main grid. A **dead end** is one the
main grid's links lead into. An **island** is one no link leads into. Each
one is tested by putting a robot down there, armoured against hits, and
leaving it to the AI for 30 s. If it is still held, the scan lists which of
16 directions are open at waist height. At any rise, and at 10.7, the
lowest jump any robot has, no stage has a dead end. 41 islands, all
tested:

- Shinjuku 25, Shibuya 5, Umihotaru 11 (including Umihotaru's two
  out-of-bounds ends).
- 34 let the robot walk out, most within 2 s.
- 7 held it 30 s: Shinjuku (−2.9, 0, −94.9), (106.5, 0.3, −157),
  (114.5, 0.3, −158.1), (−25.6, −3.3, 68.2), (191.6, −2.7, −15.9) and
  (−20, −3.3, 68.2), and Shibuya (55.4, 0.2, 18.3).
- Of those 7, three are walled in on every side. The rest open only onto
  each other or onto floor outside the arena. A robot cannot walk into any
  of them, and none did in about 600 simulated battles. They need no fix.

`--floor-map X,Y,Z,R` prints the highest floor at or under Y + 3 on a 1-unit
grid around a spot, for writing fixes.

`phantomcrash_ai_sim` runs headless battles at high speed (see
`scripts/analyze_ai_sim.py`). `--watch X,Y,Z,R` writes every robot's
navigation state near a spot each tick to `watch.csv`. `--nav-dump X,Y,Z,R`
prints the grid nodes near a spot with their headroom and jump and drop
links. Over the same 7 batches (126 runs of 300 s across Shinjuku, Shibuya
and Umihotaru), the changes above measured:

| | Before | After |
| --- | --- | --- |
| Stuck share | 6.0% | 3.1% |
| Long stuck episodes per robot-minute | 0.32 | 0.20 |
| Longest stuck episode (95th percentile / max) | 9 s / 130 s | 6 s / 35.5 s |
| Stuck episodes of 30 s or more | 73 | 4 |
| Shots per minute | 24.9 | 27.4 |

Most of the stuck time left is in episodes under 6 s, in crowded fights.

The sim also counts **blocked** time: the robot pushes its stick and does not
move. Standing still to aim, shoot, turn or line up a jump is not blocked.
The sim builds the grid before a battle starts. The game builds it on a
worker thread and has it within a second; at 100–250× a battle could end
first, so its robots never used the grid, and runs were not repeatable. The
jump, reach, route-point and collision fixes above, over the same 7 batches:

| | Before | After |
| --- | --- | --- |
| Blocked share | 2.1% | 1.0% |
| Blocked episodes over 15 s per robot-minute | 0.132 | 0.014 |
| Longest blocked episode, 95th percentile | 5.5 s | 4 s |
| Robot lives blocked 10 s or more at once (of about 6,400) | 56 | 2 |
| Moving share | 83.8% | 85.0% |
| Shots / kills per minute | 27.4 / 1.26 | 25.8 / 1.27 |

The "before" column was measured with the grid on a worker thread.

Headless 9-robot runs (300 s, two seeds, handgun) measured the time robots
spent trying to move without getting anywhere:

| Stage | Original steering: stuck | Navigation: stuck | Original: parked at a route point | Navigation: parked |
| --- | --- | --- | --- | --- |
| Shinjuku and Shibuya | 20–47% | 1.1–6.5% | 11–30% | 1.5–7% |

Most of the stuck time left is fight states such as hit stagger, turning and
withdrawal.

### Behaviour scorecard (`phantomcrash_ai_sim` lives.jsonl and runs.jsonl)

The sim also scores how robots fight, per life (`ai_sim_probe_behaviour.cpp`):

- **Mounts:** shots by mount, seconds each mount could fire, and seconds
  the target was in its reach.
- **Camouflage:** seconds camouflaged, and how many times it was turned on,
  in total and with a shot incoming.
- **Dodges:** dodges taken, split into those with a shot incoming and the
  rest. Also how often a heavy hit (20% of the armour within 1 s) or a
  rival's fresh lock was followed by a dodge within 1.5 s.
- **Being shot:** hits from a rival other than its target, with the robot
  holding no target or another one, and whether it turned on the attacker
  within 5 s.
- **After a kill:** whether it held a live rival again within 30 s, and how
  soon.
- **No target:** how much of its time it moved while it had none.

Each run also records, for the player and for computer robots, how many
robots hold each one as their target over time, and how long three or more
held it at once.

Over the 7 batches (about 4,200 robot-minutes), with the robot changes
above:

| Measure | Free-for-all | With a player stand-in |
| --- | --- | --- |
| Time holding a target / in engage | 52% / 77% | 57% / 78% |
| First shot (median) / reaction to a fresh sighting | 3.0 s / 1.0 s | 3.1 s / 1.0 s |
| Rival in reach and clear sight but not targeted | 30% | 29% |
| New target within 30 s of a kill / mean delay | 95% / 2.5 s | 95% / 2.4 s |
| Moving while holding no target | 75% | 75% |
| Shot holding no target: turns on attacker | 76% in 1.0 s | 74% |
| Shot holding another target: switches | 54% | 51% |
| Mounts fired, of lives with 10 s in reach | 92–97% | 94–98% |
| Camouflage: uses per minute / share of time | 9.4 / 34% | 9.1 / 34% |
| Dodges per minute / with a shot incoming | 4.3 / 93% | 4.2 / 93% |
| Heavy hit / fresh lock followed by a dodge | 21% / 14% | 20% / 14% |
| Any dodge in a random 1.5 s | 10% | 10% |
| Robots on a target, when anyone is | 1.29 | player 1.25, others 1.29 |
| Three or more on one target | 1.7% of the time | player 1.2%, others 1.6% |

The computer robots spend 13.6% of their target time on the player, against
a fair share of 12.6%; they do not gang up. Even so, 46% of their damage
goes to the stand-in. It never dodges or camouflages, so they fire at it 2–3
times as often as their target time suggests and do twice the damage per
shot. A real player dodges and hides, so this is the sim's stand-in, not
targeting.

Dodging follows the original rule: a dodge against a visible incoming shot
at a chance scaled by ability (13% of heavy hits dodged at ability under 20,
29% at 40–59), plus random dodges. Nothing answers a lock or the damage
itself, which is why the heavy-hit and lock rates sit near the 10% chance
rate. The default profile (4, 96% of robots) engages whenever it is hurt,
so withdrawal almost never happens (54 of about 250,000 robot-seconds).

#### Recovered firing and setup order (2026-10-15)

`phantomcrash_ai_sim --areas 0,1,2 --runs 6 --seconds 300 --seed 2718`
(18 runs, about 490 robot-minutes each), summarised by
`scripts/analyze_ai_sim.py`. *Before* is the previous build; *firing only* is
the new firing with `assigned_ability_settings` on, isolating the `0x05b360`
changes; *default* is the new default (ability-0 cached settings). Area 2
starts a single robot in every run, before and after.

| Measure | Before | Firing only | Default | Stand-in: before / default |
| --- | --- | --- | --- | --- |
| Holding a target / target visible | 48% / 32% | 46% / 30% | 39% / 23% | 57% / 40% → 45% / 28% |
| Trigger held | 5.7% | 11.7% | 9.1% | 8.7% → 12.6% |
| First sight p50 / p90 | 0.02 / 2.5 s | 0.02 / 2.1 s | 0.25 / 7.1 s | 0.02 / 2.8 → 0.20 / 7.2 s |
| First shot p50 / p90 | 3.5 / 14.8 s | 4.4 / 17.3 s | 6.9 / 25.0 s | 3.2 / 14.5 → 6.8 / 21.2 s |
| Reaction to a fresh sighting p50 | 1.05 s | 0.45 s | 0.52 s | 1.03 → 0.52 s |
| Lives that never fired | 6.3% | 5.7% | 10.4% | 4.4% → 8.1% |
| Shots / hits / kills per minute | 23.0 / 20.5 / 1.29 | 46.1 / 31.8 / 1.54 | 28.4 / 22.0 / 1.17 | 55.5 / 38.7 / 1.24 → 49.5 / 33.7 / 1.11 |
| Moving / stuck | 86.3% / 2.4% | 86.9% / 1.5% | 88.4% / 1.7% | 85.8% / 2.0% → 87.3% / 1.8% |
| Dodges per minute | 17.5 | 25.9 | 13.8 | 16.6 → 13.3 |

The firing alone improves every engagement measure: guns no longer wait for
a lock, so robots answer a sighting twice as fast and fire twice as often,
and movement is unchanged or better. The default's slower acquisition, the
higher never-fired share and the lighter pressure on the stand-in (targeted
29% of the time instead of 43%; three of 18 runs never targeted it) come from
the original's setup order: the pi/6 sight cone of ability 0, and no random
jumps, dashes or dodges. They are the original's behaviour as recovered, not
a navigation or firing regression; `assigned_ability_settings` restores the
wider cone and ability-based settings.

Set `live.ai.trace` in `world/rumbling.json` to true for per-opponent identity and
periodic state/target/health/request/actual-attack/dodge/recovery diagnostics.
It is disabled by default. Release build and bounded native battle runs were
used for validation; no original xemu behaviour comparison was performed.


Validation on 2026-10-01: the Release game target built successfully.
A 3,600-frame native rental battle (60 simulated seconds, three live areas)
exited normally, initialized 27 opponent lifecycles, and logged actual attacks,
durability loss, state/target changes, dodges and a route recovery. Weapon audio
received 193 submissions. No pickup chase happened in that sampled run, so its
selection/collection flow is implemented but not yet observed end-to-end there.
These are runtime observations, not an original-game parity or CPU benchmark.
