# Destructible rewards

Investigated 2026-10-01 from the European `default.xbe`. These are static
code/data findings, not a live xemu measurement. No gameplay changes were
made during this investigation. Function meanings and vehicle category names
below are researcher-assigned; addresses, values and model slots come from
the shipped executable and extracted assets.

## Vehicle payouts

| Object | Kind | Direct-hit reward, before rental deduction |
| --- | --- | --- |
| Car / van | 1 | 5,000 NY |
| Bus / truck | 0 | 10,000 NY |
| Car / van burnt wreck, destroyed subsequently | 1 | Another 5,000 NY |

Stage 0 kind 1 uses slots 1068, 1070 and 1072 (resource 198 meshes 16,
18 and 20). Their vehicle atlas is texture resource 1575 image 4; mesh 20
is the taller van. Kind 0 uses slots 1062, 1064 and 1066 (meshes 10, 12
and 14), with the bus/truck atlases at images 2 and 3. Slots and kinds are
available in `artifacts/europe/assets/tables/stage_objects.json`.

## Reward calculation and units

The nine signed integer entries at virtual address `0x203734` are:

| Kind | Table entry | Credited internal units | Displayed NY |
| --- | --- | --- | --- |
| 0 | 100 | 1,000 | 10,000 |
| 1 | 50 | 500 | 5,000 |
| 2 | 30 | 300 | 3,000 |
| 3 | 70 | 700 | 7,000 |
| 4 | 40 | 400 | 4,000 |
| 5 | 30 | 300 | 3,000 |
| 6 | 0 | 0 | 0 |
| 7 | 100 | 1,000 | 10,000 |
| 8 | 0 | 0 | 0 |

The scenery case in projectile-hit function `0x047ed9` loads the object's
kind from object offset `0x40`, indexes this table, and adds ten times the
entry to the shooter's field at offset `0x18`. The instructions at
`0x0486af` through `0x0486da` explicitly load the table value, multiply by
`0x0a`, and update that field. This is an award, not a lookup of the object's
health or repair price.

`0x091a61` copies shooter field `0x18` to result prize field `0x61a4` and
right-shifts it once for a rental. `0x096c45` subtracts repairs from that
prize to compute the wallet change. Result setup `0x094fee` copies the
receipt; `0x096a42` retains its prize and applies its net change to the
wallet. The result number draw in `0x095ccc` passes multiplier 10 to
`0x0bc6d2` / `0x0bc769`; the latter multiplies the stored integer by that
argument. Thus internal currency units are tens of displayed NY. Both
multiplications are necessary: the table entry itself is not the final NY
amount. A rental therefore retains 2,500 NY of a car/van award or 5,000 NY
of a bus/truck award, before repairs.

## Eligibility and repeat destruction

The direct-hit award is inside the check that object update handler `0x44`
equals idle handler `0x072719`. It runs when that hit starts destruction,
not for every shot while an object burns.

Kind 1 handler `0x072902` swaps to the wreck and sets flag `0x400` on its
first destruction. Update `0x072afa` returns that burnt wreck to the idle
handler after its six-second timer. A subsequent direct hit enters the
same reward path; the second destruction clears `0x400` and sets removal
flags `0x20` and `0x10`. The same 5,000 NY is therefore awarded again for
destroying the car/van wreck. This finding does not establish a robot-wreck
reward or imply every scenery kind has the same two-stage lifecycle.

The explosion-overlap loop later in `0x047ed9` dispatches destruction for
nearby scenery without the table-credit step. The amounts above describe
the direct-hit path; they must not be assumed to apply to every secondary
object caught in splash damage.

## Port

`Battle::hit_object` (`port/src/battle_objects.cpp`) pays the shooter when
its hit starts an object's destruction: an `object_reward` event whose
amount is `battle.object_reward_ny` in `data/world/training.json` for the
object's kind (the table above, in NY), in stored units. A car or van's
burnt wreck pays again when it is destroyed (`stage_objects.cpp` already
returns it to idle). Shots and strikes count, and so does a blast on the
object its own round struck. Another blast that catches the object pays
nothing. The money shows on the HUD as it is won, and goes into RESULT's
prize (halved for a rental) and co-op bounties like the other battle
bounties. Robot-wreck lifecycle work remains pending while its
reward is selected separately.

## Robot wrecks (port behavior)

The user selected **5,000 NY** for destroying a robot corpse; this is a port rule, not a recovered robot-corpse payout. The ordinary shootdown remains separate. The bonus enters the live money display and the result prize total, with the existing rental deduction applied at settlement.

After the death motion has finished and the robot has landed, its final pose is retained independently of the live roster. The death fire runs for 6.45 seconds from downing (the current 160-tick, 30 Hz fire emission plus the last particles). Once it expires, the posed wreck becomes solid to movement, shots and sight rays. It has no expiry; the first damaging hit removes it, plays the vehicle-wreck burst and sound, and pays the shooter once without another shootdown. A respawning NPC does not remove older corpses. Wreck items are included in recorded replay frames.

Editable defaults: `battle.robot_wrecks` in `data/world/training.json`: `burn_seconds`, `reward_ny`, `break_effect`, `break_sound`. Static collision is baked from the final rendered part geometry; missing robot assets do not create invisible obstacles.
