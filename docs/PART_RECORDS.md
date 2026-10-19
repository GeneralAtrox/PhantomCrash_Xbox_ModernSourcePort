# Part records, aim height, name tags and the new game

Research notes on the Europe XBE. Addresses are virtual addresses. Field names
are researcher-assigned unless marked as coming from shipped data. Part and
pilot names are the full-width Shift-JIS strings shipped in the XBE.

## The weapon stats block

0x092a07(S, record, level) builds a weapon's stats block S (weapon + 0x20)
from its 0xc8-byte arm (0x1bae48) or shoulder (0x1be9a8) record. "Curve" means
a level curve (0x092160 for 16-bit, 0x0924fd for float; see
`port/include/phantomcrash/robot_parts.hpp`). Frame counts and speeds are in
1/60 s: a battle tick advances by refresh / rate = 2 frames (scene 42 runs at
30 Hz, 0x255ab0).

| S | Record | Meaning | Examples (level 0) |
| --- | --- | --- | --- |
| +0x00 | float curve +0x0c | unknown | Pistol 40, Gatling 160, Javelin 145, Drill 90, Aspide 80 |
| +0x04 | short +0x24, degrees | an angle, probably the lob's launch angle | Slingshot 30, others 0 |
| +0x08 | ushort +0x26 | frames between shots (cooldown +0x80) | Pistol 18, Gatling 2, Drill 40, Aspide 45 |
| +0x0c | ushort +0x28 | projectile lifetime in frames | Pistol 30, Slingshot 75, Aspide 120 |
| +0x10 | float +0x2c | start speed, units per frame | |
| +0x14 | float +0x30 | top speed | |
| +0x18 | float +0x34 | acceleration per frame (missiles, 0x098177) | |
| +0x1c | byte +0x38, degrees | spread of the extra rounds | Gatling 5 |
| +0x20 | float curve +0x40, degrees, floored at +0x3c | homing turn per frame; 0 does not home | |
| +0x24 | int curve +0x52, min +0x50 | rounds per trigger pull | |
| +0x28 | int curve +0x5c, min +0x5a | damage per hit | Pistol 450, Gatling 260, Drill 1850, Aspide 950 |
| +0x2c | curve +0x64 | blast radius at its largest | Slingshot 20 |
| +0x30 | short +0x6c | blast radius at the start | 1 |
| +0x34 | curve +0x6e | blast growth, frames | 15 |
| +0x38 | curve +0x76 | blast damage | Slingshot 120 |
| +0x3c | int curve +0x80, min +0x7e | ammunition (a beam's full charge) | |
| +0x40 | curve +0x88 | a beam's recharge, frames from empty to full (0x097785 adds max / this each frame) | Javelin 150 |
| +0x44 | curve +0x90 | stun: sets the target's stun timer to at least this | Drill 1 |
| +0x48 | float curve +0x98 | knockback strength | |
| +0x4c | float curve +0xa8 | knockdown power | |
| +0x50 | int curve +0xb8 (0x092311) | shop price (0x0932c7, category 2 and 3) | Pistol 1500, Gatling 15600 |

The record's other bytes: +0x1c the HUD kind, +0x1d the flight (0 straight
0x09789e, 1 lobbed 0x097ac9, 2 missile 0x097d42; table 0x2533f4), +0x1e the aim
mode, +0x1f and +0x20 what a shot does where it lands on a surface or on a
robot (`surface_hit`, `robot_hit`: the cases of 0x098eda, 0 gone, 1 its impact
and gone, 2 a blast with its impact, 3 a blast without, 4 its impact and a hit
sound, 5 its impact and it flies on), +0x22 the shot's look (`effect`, table
0x2534a8, drawn by 0x09831e) and +0x23 its impact (table 0x253408). The looks
and impacts are in [EFFECTS.md](EFFECTS.md#shots-and-impacts).

- **Firing (0x09b4ed, kinds 0-4).** The cooldown resets to S+0x08. A pull
  fires S+0x24 rounds, the extras turned by random spread up to S+0x1c. A
  homing missile fires its rounds one every 10 frames. Each round spends one
  ammunition, but a machine gun (kind 2) spends one per pull.
- **Flight.** A projectile lives S+0x0c frames. Missiles speed up from S+0x10
  to S+0x14 by S+0x18 and turn toward the lock by S+0x20 (0x097f1a). They
  lose the lock when the target is camouflaged. Lobbed rounds fall by
  9.8 / (refresh / 2)² × 2.5 a tick (0x097c30). A blast grows from S+0x30 to
  S+0x2c over S+0x34 frames (0x098298).
- **Blast.** An exploding projectile (state 3) hurts every enemy whose body
  is inside the blast's current radius by ftol(S+0x38) each battle tick for
  as long as the blast grows, the body it hit directly too. The radius is
  the record's own (Slingshot 1 growing to 20 over 15 frames, so 8 ticks
  of 120), measured to the body's surface.
- **Damage (0x0988e1).** No damage while the target's invulnerability timer
  runs. A directional hit (a shot or a strike, not a blast) travelling
  within 45 degrees of the way the target faces struck it from behind and
  does triple damage. A beam's damage is cut by the target's beam
  resistance (from option parts). The target dodges when rand(0..1) is
  below its robot +0x4548 (30 frames of invulnerability). A critical hit
  doubles the damage when rand(0..100) is below the attacker's robot
  +0x4538. On a critical hit, a knockdown happens when S+0x4c − target
  +0x4540 > rand(0..100): it knocks off the arm or the shoulder weapon, at
  even odds, on the side the hit struck, and the HUD shows WEAPON LOST. The
  knockback is S+0x48 × (100 − target +0x44e0) / 100 units a tick.
- **Stun and knockback (0x07675e).** A hit with stun (S+0x44) above 0 makes
  the target play a hit-reaction motion (0x08dcd1): the push (+0x27c4) in
  the robot's own frame (matrix +0x374, normalised) picks it, along the
  robot when that part is larger, else across: pushed back (z ≤ 0) motion
  0x11, forward 0x12, to its left (x ≤ 0) 0x13, to its right 0x14; in the
  air (+0x252c, set on landing by 0x07c7c0) 12 later, 0x1d-0x20. Each is
  29 frames, blended in over 7 (0x08ddb2). The knockback adds to the
  robot's push, which moves it at most 5 units a tick and fades to 0.8 of
  itself every tick (0x07c34e).

The robot's fight stats come from its chip and legs (0x092c6c copies the
chip's block to robot +0x4504, and 0x075d4a copies these on):

| Robot | Record | Meaning | Example |
| --- | --- | --- | --- |
| +0x4538 | chip float curve +0x3c | critical hit percent | Dog I 1 |
| +0x4548 | chip float curve +0x4c | dodge percent, clamped 0..100, as a share | |
| +0x4540 | chip float curve +0x5c | knockdown resistance | Dog I 3 |
| +0x44e0 | legs float curve +0x7c (legs block +0x1c, 0x09281d) | knockback resistance, percent | legs 0: 70 |

Weights: 0x075f93 sums the chip's (+8), body's (+0x18), legs' (+0x4c),
weapons' (+0x0c, the curve that was unknown: Pistol 40, Gatling 160) and
option parts' weights, each at its level. 0x075a36 turns the weight W into
the robot's movement:

- The legs' level L (loadout +8) trims up to a tenth: t = 1 − L / 99 × 0.1.
- The speed factor (0x07c148) is t × √(F / W), F the robot's own body's
  and legs' weights at level 0 (0x07c0df); the jump factor (0x07c1c4)
  takes body 0 and legs 0 as F instead. Past the legs' capacity C (+0x5c,
  legs 0: 1060) both are multiplied by (C / W)^10.
- 0x07c34e scales the legs block (0x09281d copies the curves at the legs'
  level to robot +0x44c4): the dash value is +0x1c (legs 0: 118) × the
  speed factor, 20..2000, and the walk half of it, 10..1000. Each is
  divided by 216 (0x1b5ab4) and, where the legs model's motions move the
  robot, by the model's speed in the table at 0x232558 (walk, dash; model
  0: 1/3, 0.533; 0 for motions without root motion), which makes it the
  walk and dash motions' playback rate (robot +0x68, +0x70; flags +0x64,
  +0x6c). The jump height h is +0x2c × the jump factor, 2.5..100 (+0x74,
  legs 0: 19.14). With g = 2.5 × 9.8 / 30² a tick, take-off (+0x2560,
  0x08ea5b) adds 0.4 × √(2 g h) + g to the rise, and the lift budget
  (+0x2564) is g × 0.84 h over that speed. While the jump stays held
  (0x0905af) the rise eases a tenth of the way to the take-off speed a
  tick and the budget, spent at g a tick, cancels gravity (0x07d117), so
  the robot climbs 0.84 h and its throw the rest; letting go (+0x2558)
  ends the lift. A re-press latches descent once the lift spent is more
  than the rise left. +0x3c × the speed factor (0.1..90, legs 0: 2.35)
  goes to +0x78 and +0x7c, the turn in degrees a 60 Hz frame: 0x08e257
  turns by +0x7c on the ground, in a dash and in a dodge, 0x0905af by
  +0x78 in the air, each times DAT_a69c94 / DAT_ac73b8 (60 / 30 in a
  battle), so two frames' worth a tick. +0x9c and +0xac × the speed factor (0.1..30, legs 0: 0.55 and
  0.3) go to +0x80 and +0x84, the push a dodge (0x08d98c) gives on the
  ground and in the air, in units a tick.

The training player (Dog I, body 0, legs 0 and its four weapons) weighs
950.1 against a frame of 675: a speed factor of 0.843, so its walk plays
at 0.69 and its dash at 0.86.

Robot gravity is right at 24.5 units/s²: 0x07d117 steps 9.8 × 2.5 at the
battle's 30 Hz. Only the lobbed rounds' formula (0x097c30) has the extra
factor that makes their 49.

Other shop prices from 0x0932c7: body record +0x30, legs +0x8c, chip +0x6c
(all read by 0x092311); categories 5-7 read a plain field.

## Aim height

The HUD's aim starts at robot +0xb0..+0xb8 (the robot's position at its feet)
plus robot +0x14 upward (0x098bb7, 0x04e6b8). Robot +0x14 is set once, by
0x080e41: the legs record's model index (0x05f788: legs record +8) looks up the
per-leg float at 0x22f424 + model × 8, clamped to 0..2 (a value above 2 becomes
2). That is the value the collision already uses as the leg radius
(`robots/catalogue.json` `radius`). So the original aims from one leg radius
above the feet, the middle of the robot's lower collision sphere. Robot
+0x279c is 0.7 times it.

| Legs model | 0 HL2Leg | 2 HL2Roller | 4 HLHover | 5 PHLeg | 6 PH4Leg | 17 TL4Leg | 20 Drones 3Roller |
| --- | --- | --- | --- | --- | --- | --- | --- |
| +0x14 | 1.731 | 1.395 | 1.531 | 1.999 | 2.0 (2.219 clamped) | 1.75 | 1.418 |

## Markers and name tags (0x04de50)

The marker is drawn at the robot's projected point P, in the team colour
chosen by loadout +0x6c:
- 0: yellow 0xffffff00, or green 0xff00e100 when loadout +0x70 is 3
- 1: 0xff8080ff
- 2: 0xffff8080

In RUMBLING, these fields describe roster status, not the port's combat
allegiance. 0x045270 puts member/leader/champion (0/1/2) in the roster
triple's third word; 0x0456b7 copies it to loadout +0x6c. Its +0x70 comes
from the draw pool (0x045a68): 1 unbeaten, 2 beaten, 3 reserve. Thus regular
entrants are yellow, leaders blue, champions red and reserve entrants green.
The port's `Combatant::marker_colour` selects that editable HUD palette
independently of `team`, so the live free-for-all's unique combat IDs cannot
turn names white or assign them another rank's colour. The same colour tints
the name, armour frame and bar; replacement NPCs receive their own status.

Each part is placed relative to P:
- **Frame.** Sprite 0x560 at P + (−72, −18).
- **Armour bar.** Sprite 0x56a at P + (−66, −13), with its x scale set to
  armour / maximum (+0x44 / +0x48).
- **Name tag.** Loadout +0x3c is the pilot's name id. When it is not −1,
  0x0c7a09(id) returns a 16-byte entry at 0x1c5af8 + id × 16 (0x3de
  entries). The entry's second string (+4) is the tag, for example
  "Ｙｕｋｉ", "Ｒｏｙ" or "Ｋａｎｂ＠ＴＫ２３". The first string (+0) is the
  full name, and name id + 1 is the robot's name, for example "Ｋｕｄｒｙａｖｋａ".
  A tag shorter than 30 bytes is drawn by 0x0bdc9b:
  - in font 1, with 12 × 12 glyphs, in the team colour
  - at x = P.x − 72 + 37 − n × 12, y = P.y − 18 − 12
  - n = ((bytes / 2) + 1) / 2, about half the glyph count, so the tag is
    centred over x = P.x − 35
- **Leader icon.** When loadout +0x70 is 2, sprite 0x567 (game_tex
  232,184-256,200) is drawn left of the tag, at
  x = P.x − 72 + 42 − n × 12 − 32, y = P.y − 30.
- **Number.** A robot with no pilot (+0x3c = −1) gets no tag. In versus
  mode (save +0x11074 set), a robot whose player number (+8) is not −1
  instead gets sprite 0x5b8 + number at P + (−54, −34): the 1P-4P labels in
  game_vs_tex (32 × 16 each).

So the training drones have no pilot and no label.

## The new game's robot and money

- **Team loadouts.** At boot, and again for a new save (0x06cd57, after
  0x0b94e0 resets the save), 0x05fabc clears the four team blocks (save +
  0x2820, 0xdf0 each). It then fills each team's six loadouts
  (+0xa5c + slot × 0x84) from entries 0-5 of the table at 0x1b1008 (8 bytes
  each):
  - short +0 is the pilot record (0x0c7a51: 0x1c9920 + id × 0x1c, 7 dwords
    copied to loadout +0x3c)
  - short +2 is the robot preset (0x05fa83: 0x19f920 + id × 0x3c, 15 dwords
    copied to loadout +0)
  - +4 and +6 go to loadout +0x5a and +0x58
- **Entry 0.** Pilot 1 is "Ｙｕｋｉ" with robot name "Ｋｕｄｒｙａｖｋａ". Preset 6
  is "ＳＮＯＷＷＨＩＴＥ" with an enemy body and legs (Chaga: body 9, legs 20),
  enemy weapons (arm 34 and 33, shoulder 30 and 31) and the enemy-only chip 91
  at level 50. It is a team placeholder, not a robot the player gets.
- **Loadout layout.** Each part is a pair of a short index and a level:
  - +2 body (0x092661, 0x1b5b88, 16 records)
  - +6 legs (0x09268e, 0x1b5f88, 43 records)
  - +0xa, +0xe arm weapons (slots 0 and 1, 0x039d5e)
  - +0x12, +0x16 shoulder weapons
  - +0x1a chip, with its level byte at +0x1c
  - +0x58 signed AI ability parameter; +0x5a behaviour-profile selector
    (0x080778 passes these to 0x057820; see `ORIGINAL_ROBOT_AI.md`)
  - +0x5c player/controller ownership selector, not an AI index: −1 is
    NPC-controlled, −2 is an inactive slot, and nonnegative values select
    player ownership (0x080778, 0x0825df, 0x046f81)
- **The player's robot.** A new save gives the player no robot. 0x0b9774
  clears all ten garage records (save + 0x25f0, 0x36 each) to −1.
  0x0b9668 sets the ridden slot (save + 0x9c8, low 5 bits) to −1 and
  clears the part inventory (save + 0xe08, 255 records of 0x18 bytes:
  byte level, short category, short index). It owns only item 0 of
  category 6 (bit set at save + 0xc3c) and item 205 of category 7
  (save + 0xc75). The player buys parts at the shop (0x022bab, prices from
  0x0932c7) and builds a robot in the garage (0x019a3e, 0x01dccb copies
  the ten records back).
- **The money.** The money word is cfg + 0x9d0 (top bit: the game was
  cleared). Money is added with 0x0b1ebb, which clamps the total to
  0..99,999,999. No code writes 200,000: the value does not appear as a
  literal or as a float, and it is not in the disc files. The only direct
  set is 0x02bec6. It sets the money to 20,000 (keeping the top bit) and
  clears the ridden slot. It is reached from menu choice 3 of the task
  0x02c1ce / 0x02c2d9, which is not identified yet. Whether a new game
  starts with 200,000 or 20,000 still needs a runtime check in xemu.

## Battle chatter (0x049470, 0x049552)

In battle, robots comment on what happens to them in a window mid-screen,
where Pepper's lesson messages show: a panel, a face, a name and a line.
The converter writes `tables/chatter.json` (`chatter.py`).

- **Events.** Game code raises one of 35 events for a robot (0x049470).
  It passes its gate (0x203848: 1 only for a robot a player controls, robot
  +8 not −1; 2 only for a computer robot on team 2, loadout +0x6c; 0 any)
  and its chance, rand(0..1) below the event's float in the table for the
  robot's kind (0x203890 player, 0x203920 team 2, 0x2039b0 other). The
  robot keeps its lowest pending event (+0x4594, 0x23 for none).
- **Lines.** Every tick 0x049552 takes the lowest event pending over all
  robots and forgets the rest. It starts only when no line is showing
  (0xa69cbc bit 0), except events 1 and 4, which close the line showing.
  The speaker's table (0x1b3520, 67 tables of 35 script pointers) gives
  the script: a player's robot speaks from its chip's (chip +0x18, copied
  to robot +0x4508 by 0x092c6c), a computer robot from loadout +0x54 (none
  below 0). The line opens with battle sound 45. While robot 0 is a
  player's and in states 1-6, only its events 0x1c, 9 and 0x12 play.
- **Scripts.** Talk-scene commands run by 0x0c57e0 with four values: text
  code 0xfff8 prints the robot's name and 0xfff9 the pilot's; sprite −1
  shows the robot's face and −2 the pilot's (name entry +8). A player's
  robot's name is its chip's (0x06d663), its face the chip's name entry's
  (chip +0x1a), its pilot's name the player's, and it has no pilot face.
- **Raised by.** The damage rules (0x0988e1), to the victim: 7 hit from
  behind, 0x11 critical hit, 10 dodge. A knockdown (0x08d686): 0xc below a
  quarter of its armour, otherwise on an arm 0xf while an arm is left or 6
  for the last. The damage applier (0x07675e), to the victim: 0x18 on
  armour falling below half, below a quarter 3 when team 2 did damage or
  else 0xe; shot down 2 by team 2 or else 5. To a player's robot that
  helped shoot it down: 1 while a flag (0x05fc37) is clear and the victim
  is on team 2, then 0x15, or 8 below a quarter of its own armour; the
  flag is set either way (the port clears it once a battle). The
  entrance (0x088ffa), only in battle modes 0 and 2: 0x12 for a player's
  robot or an ally, and for a team-2 robot 0, or 4 when its loadout +0x70
  is 1 (a boss). The
  exit (0x08224d), as a robot walks back into a gate: 0x1c, or 9 below a
  quarter of its armour. The rest:
  - Supply pickups (0x066e50), to the robot taking one: repair 0x1d, or
    0xb if it is left below a quarter of its armour; ammunition 0x1e when
    short of full, or 0x13 below half its rounds.
  - Rounds (0x075773): once, as the robot's share of rounds falls through
    half, 0x14, or 0xd below a quarter of its armour. The share (0x08248b)
    is the mean of ammunition / maximum over its weapons that are not beams
    or melee weapons, 1 with none.
  - Optic Camouflage: 0x22 as it switches on (0x074fa4), 0x10 as its gauge
    falls below a quarter (0x0751be, 0x199fd4).
  - Hits (0x047ed9): 0x16 to a robot that shoots the supply carrier down
    (case 2), 0x19 or 0x1a to a robot whose melee (kind 6) or beam (kind 5)
    shot hits a robot (case 5), and 0x1f to a robot whose shot starts a
    stage object's destruction (case 4), unless the object is of type 5, 6
    or 8.
  - 0x17 to a player's robot as a missile lock on it completes (0x051045).
  - 0x1b to a robot that lives through a blast that hurt it (shot state 3,
    0x09c9c2).
  - 0x20 on firing a weapon of kind 1 or 3, missile or grenade (0x09b5db).
  - 0x21 after 900 ticks standing with the pad untouched (0x08e480,
    0x1b59f0, the count at robot +0x251c).
- While the player's robot is in robot states 1-6 (its entrance and exit,
  [TRAINING_GROUND.md](TRAINING_GROUND.md)), 0x049552 takes only robot 0's
  event, and only 0x1c, 9 or 0x12; 0x1c and 9 close a line showing.

Table 0, the report voice of the early chips, says "【Report】Target
destroyed. Keep battle mode." on event 1 and "【Caution】Shock detected.
Continue battle?" on 0x11. The event names in `battle_chatter.hpp` are
researcher-assigned.

The port: `BattleChatter` (`port/src/battle_chatter.cpp`) applies the gates,
chances and the lowest event; `chatter_script` fills in the names and
faces; `Battle` raises the events above as `BattleEventKind::chatter`
(`rival` stands in for team 2, and the training drones are rivals); the
training ground's `ChatterWindow` plays the line with the event player at
the battle's 30 frames a second (`chatter_frame_rate`, a port reading),
ignores the buttons (they fire weapons) and gives way to lesson messages.
`BattleChatter::set_scene` applies the state 1-6 rule while the scene runs.

## What the port uses

- **Weapons.** `apply_weapon_stats` (`port/src/robot_parts.cpp`) converts a
  record at its level into the battle's `WeaponTuning`, from frames to
  seconds and from units per frame to units per second (×60, ×3600 for
  acceleration). It covers the HUD kind, damage, fire interval, rounds and
  spread, the start and top speed and acceleration, the lifetime, the
  homing (floored at +0x3c), a lobbed round's launch angle, the blast
  (start radius, full radius, growth, damage), the ammunition and a beam's
  recharge, the knockback (×30 to units a second), the knockdown chance and
  the stun (a weapon without stun does not make its target flinch; one
  with it flinches for its stun, at least the hit motion). `weapon_reach`
  (`port/src/weapon_reach.cpp`) sets its reach and locks as below.
- **Reach and locks.** 0x09d42f works out the reach the computer fires a
  weapon within (weapon +0x74, the nearest at +0x78):
  - a straight or homing round (0x09dd85): the distance it flies in its
    life, from its start speed, speeding up by its acceleration to its top
    speed; without acceleration it keeps its start speed (start × life);
  - a lobbed round on a shoulder: 2v²·sinθ·cosθ / g with v its start speed
    a frame and g the world's gravity a battle tick (9.8 / 30² × 2.5), about
    half the true range, plus its blast radius and 5 (0x19b544); the
    nearest is the same less them, at least 0. A round fired downward
    reaches 10;
  - a beam (kind 5): its top speed, the beam's length;
  - a melee weapon (kind 6): 15.
  An arm weapon locks a robot within 75 units (0x19b55c) inside the inner
  reticle (0x4f6a0); a shoulder weapon within 125 (0x19f8c8) inside the
  outer one (0x5262d) and holds it while the robot stays within 125 and 90
  degrees of the aim (0x19f870, 0x51045); a melee weapon within 25, or 20
  on a computer's robot, and holds it within 90 degrees (0x51e37). A box
  holds a robot whose screen point lies within its half sizes of the
  tracked reticle (robot +0x108, +0x10c; 0x0525b0); the nearest locks
  first. The port tests the player's boxes as drawn; a robot without a
  drawn reticle takes its chip's boxes around its sight, the point 64
  units ahead of its aim (0x04e6b8); the AI fires within the reach
  once locked, and a round without a life (a beam) ends at its reach.
  A beam's tip runs out at its acceleration a frame (0x09b9fe adds it to
  the shot's length +0x84 every frame up to the top speed); the port holds
  it as one ray on the muzzle (docs/WEAPON_AUDIT.md, Beam).
- **Damage rules.** `Battle::hit_damage` (`port/src/battle_damage.cpp`)
  applies the rules above: invulnerability, dodges, hits from behind,
  beam resistance, criticals and knockdowns (`Combatant::lost`, drawn as
  WEAPON LOST). Knockback builds `Combatant::push`, capped and fading as
  above. `combat_stats` reads the chip's and legs' stats; the training
  ground gives them to the player (its chip and legs) and to the drones
  (legs 23, no chip). Blasts hurt every tick as above. A homing missile's
  rounds leave one every 10 frames. A stunned robot plays the hit reaction
  for the push's direction (`RobotMover::stagger`, `robot_motion::hit_back`
  and the rest), for the weapon's `stagger_seconds` but at least the
  motions' 29 frames (`hit_seconds`).
- **Load.** `load_stats` and `loaded_movement` (`port/src/robot_load.cpp`)
  weigh the robot and scale its movement as above. With the leg motions'
  strides (`stride_tuning`) the walk and dash rates set both the motions'
  playback (`walk_motion_rate`, `dash_motion_rate`) and the speeds. The
  original moves a flagged model by its motion's root at that rate and
  any other by the value / 216 a frame (`0x08e6b6` walk, `0x08f2da` dash),
  so a flagged model whose motion has no stride the port can measure (the
  RUMBLING opponents' motions are never measured) takes the table's own
  stride, and the speed comes to the value / 216 either way. The
  `movement` walk and dash speeds are left only for robots without legs
  records. The turns
  (`turn_degrees_per_second`, `dash_turn_degrees_per_second` and
  `air_turn_degrees_per_second`) are the legs' turn under the load, 30
  ticks a second, and `jump_height` is the legs' jump under the load;
  `RobotMover` takes off and lifts as above (`jump_takeoff`). Legs without
  those curves scale the training file's values by the factors. The
  dodge's push is extracted (`dodge`, `air_dodge`) but the port's dodge
  still uses `movement.dodge_speed` and `dodge_seconds`, and the aim's
  pitch rate is a port value.
- **Stopping momentum.** The leg record's float at `+0xbc` is `stop_keep`.
  `0x09281d` copies it to robot `+0x44f0`; `0x090d21` multiplies the
  world-space residual displacement at `+0x2584` by it each 30 Hz tick.
  This is a fixed part value, not a level curve or a load factor: leg 0
  retains 0.875, leg 4 retains 0.96, and drone legs 23/24 retain 0.5.
  Below 0.2 units/tick (6 units/s) the residual is cleared. Both extractors
  emit the field, and loaded movement uses it for stopping after root-driven
  travel. Forward walk/dash motion now samples root displacement each tick
  from the simulation animation phase, rather than only its average stride.
- **Chatter.** See below.
- **Armour.** `robot_armour` sums the body's and legs' armour curves, each
  raised to the player's or the computer's floor. The training ground gives
  the player 15000 + 3500 = 18500 (body 0, legs 0) and a drone
  10000 + 600 = 10600 (body 12, and legs 23 raised from 0 to the computer
  floor).
- **Aim.** `Combatant::aim_origin` is the robot's feet plus its leg radius.
  The sight and the lock search start there.
- **Name tags.** The markers draw `Combatant::tag` (a pilot record's tag)
  as described above (`markers.tag` in `data/ui/battle_hud.json`), the
  leader icon beside it (`markers.leader`, `Combatant::leader`) and, on
  untagged robots, the versus number (`markers.player_numbers`,
  `Combatant::player_number`). The training ground sets neither.
- **Lobbed rounds** fall at 49 units/s² (`training.json`), the per-tick
  step above converted to seconds.
