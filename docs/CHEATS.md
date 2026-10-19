# Cheats

**Tools > Cheats** in the window menu is a port addition for testing and
play. The original game has no cheats; everything here is off or untouched
by default.

## Switches

Ticked items are saved in the settings file (`"cheats"`) and come back on
the next launch.

| Item | Effect | Where |
| --- | --- | --- |
| Infinite Money | The quest's money stays at the cap (99,999,999 NY). A shop already open keeps showing what it had until it is left. | `ScreenRunner::apply_cheat_switches` |
| Health Never Reaches 0 | A piloted robot's armour stops at 1, so it is never shot down. | `Battle::apply_hits` (`BattleCheats::never_die`) |
| Ammo Never Reaches 0 | A piloted robot's magazines are refilled every tick, beams included. | `Battle::fire_weapons` (`BattleCheats::infinite_ammo`) |
| Unlock All Contests | RUMBLING lets you into any contest held that day: Area and First Ranker contests open without their leaders or rankers won, any maker, legs or O.C modules pass the class rules, and rentals are offered everywhere. A ridden, complete Scoobee and the fee are still needed. Takes effect the next time RUMBLING opens. | `RegulationCheck::open_contests` |

"Piloted" means a robot a player controls, so in VS and co-op battles every
player's robot is covered.

## One-off changes

These change the quest itself, so they are saved with the game. On a
place's menu (Club Wired, the garage, ...) or the town map they apply at
once and the screen opens again; anywhere else (a shop, a battle) they
apply as the next screen opens, so that screen's own copy of the quest
cannot write over them. They are not available in co-op.

- **Fill Hangars (One of Each Robot Type)** fits one robot of each of the
  seven body models into the empty hangars, free of charge: the makers'
  showroom SVs Plain Holy, PH-8.10, AE-M5A2, TR-T3-4 Tolomeo, TR-R-45
  Tolomeo R, CG-F5.6 Charga and DRONES. It skips a robot whose body model a
  hangar already holds, so your current robot counts. The bosses' robots
  (SPUTNIK, SNOWHITE, MUTOS, GOU) are not in the list: their bodies are
  enemy copies of the Holy, Charga, Tolomeo and Tolomeo R models.
  With the S.L.A.I. asset mod installed, the list continues with all ten
  imported body types: PROTON-K7P2, HARTMAN-IP1, CARRO-H8/6, KHT-O.279,
  ZWERG-EV.3, ELINT-Re-K9E2, MOLNIYA-O.476, ARDITO-H8M, DUSTER and SWEEPER.
  Their starter loadouts use matching source-family legs and weapons, plus
  a Phantom Crash CHIP. Phantom Crash comes first, followed by S.L.A.I.;
  existing robots count toward their body type and are kept. The garage
  has twelve hangars, enough for the seven Phantom Crash types and the five
  main S.L.A.I. manufacturers. Each use stops when the empty hangars are filled.
  Older saves keep their existing ten hangars and gain two unowned slots that
  can be purchased normally or filled by the cheat.
- **Add All Items, Chips and Music** puts one of each distinct part in the
  parts stock: each body, legs and weapon model, and each CHIP and module
  name (7 bodies, 21 legs, 34 arm and 30 shoulder weapons, 32 CHIPs and 106
  modules). Where a shop sells the part, its record is the one given; parts
  only enemies or bosses carry come as their own record. Left out are the
  copies: the enemy-only records of shop parts, the "Spare" and Roy's
  weapons, and the five records of each animal CHIP, which differ only in
  their battle chatter. Weapons come two at a time (one for each side),
  everything else once. A copy of the same part already loose counts, so
  picking it again tops up rather than doubling. It raises the stock from
  the original's 255 entries to 1,024 (saved with the garage), and owns
  every paint, emblem and music track. The one shoulder "weapon" without a
  model is left out: it is the damage source of stage-object blasts, not
  something a robot can carry.
- **Go to Day** sleeps the nights until the calendar shows the chosen day,
  as the garage's SLEEP does: the nights slept count, Sonic Amp's chart
  changes, the town map's spin payments reset, and the day's RUMBLING
  rosters are drawn again. A day earlier than today goes round the end of
  the 60-day calendar. Entries are marked where a First or Area Ranker
  contest is held; the First Ranker fights on days 1, 16, 31 and 46.

## Editing

`data/debug/cheats.json` sets the hangar robots (robot record indices in
`tables/garage.json`; add 7, 6, 9 and 8 for the bosses' robots), the
copies kept of each part and the raised stock size. A mod folder can override it like any other data file.
S.L.A.I. registration appends its starter records to `mods/tables/garage.json`
and writes the extended list to `mods/data/debug/cheats.json`. Re-registering
updates these records and keeps the Phantom Crash entries first.
