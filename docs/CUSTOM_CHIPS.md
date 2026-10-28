# Custom CHIPs

How the port's added CHIPs are made, and how to recreate them or add another
one exactly the same way: three cats (Cat V, Cat VI, Cat VII) and seven
specialists (Fox I, Bear I, Tortoise I, Boar I, Hawk I, Monkey I, Meerkat I),
each best at one thing and clearly weak at another. They are part of the base game data, not
a mod: they appear in BUY CHIP, saves and battles like the original's 96. The
same file also retunes original CHIPs (the Rabbits, section 5).

## Where everything lives

| What | File |
| --- | --- |
| Records, stats, shop days | `data/tables/added_chips.json` |
| Face pictures (64x64) | `data/assets/textures/chips/<name>.png` |
| Source photographs | `source-art/chips/<name>.webp` |
| Face recipes | `scripts/chip_faces.json` |
| Face generator | `scripts/make_chip_face.py` (+ `chip_face_look.py`, `chip_face_matte.py`) |
| BUY CHIP and UPLIFT help text | `chip_help` in `data/ui/chip_shop.json` and `data/ui/uplift.json` (the same list), at the CHIP's index |
| Code that joins them to the tables | `port/src/added_chips.cpp` |

`data/assets/` is the last asset root (`DataRoots::standard` and `shipped`), so
a face named `textures/chips/cat_v.png` resolves there unless a mod or the
converted assets hold the same path. The release ZIP ships everything under
`data/`, so nothing extra is needed to package them.

## How the records join the game

The converted `tables/parts.json` (from the player's disc) holds the original
96 CHIPs, indices 0-95. `read_converted_table` reads it and appends one record
per family personality from `added_chips.json`, numbered from `first_index`
(96); `tables/garage.json` gets the `shop` offers added to its rotating
`chip_lineups`. Every reader of those two tables goes through it (desktop
screens, training robots, the Rumbling AI world, online checks), so the new
records are ordinary CHIPs everywhere. Nothing is written to the extraction, so
no re-convert or `converted_assets_version` bump is needed.

Rules:

- **Only ever append.** Saves and shop days name CHIPs by number. Add new
  families (or personalities) at the end of `families`; never reorder.
- The additions are skipped when the parts table does not end exactly at
  `first_index` (another game's table) and in S.L.A.I. mode.
- The original's shop table has 9 slots a day and fills 8; the port's BUY
  CHIP list takes any number, so each day sells one added cat and one
  specialist (10 CHIPs).

Current numbering (chatter personalities 0-4 in order): Cat V 96-100, Cat VI
101-105, Cat VII 106-110, Fox I 111-115, Bear I 116-120, Tortoise I 121-125,
Boar I 126-130, Hawk I 131-135, Monkey I 136-140, Meerkat I 141-145. Then
the bosses' CHIPs, one personality each (section 4): Retriever 146, Macaque
147, Penguin 148, Borzoi 149, Tanuki 150, Turtle 151, Chimp 152, Siamese 153,
Leopard 154, Black Cat 155, Vixen 156, Sparrow 157, Owl 158, Ferret 159,
Labrador 160.

## 1. The face picture

The original faces are 64x64 cells of
`textures/scroll/fight_chara00_tex/0.png`: an animal on a flat green
"monitor" backdrop, tinted along a green ramp, with dark scanlines and a
two-line frame. `chip_face_look.py` measures all of that from the player's own
extraction each run (nothing is copied from the original pictures):

- **Backdrop**: the median flat green between lines, about (0,142,57).
- **Ramp**: subject brightness to (R,G,B), with the scanlines divided out.
- **Scanlines**: the faces use one of two row sets; the generator uses set B
  (rows 2, 7, 11, 15, 20, 25, 30, 35, 40, 45, 51, 56, 60), the set 13 of the
  17 animals use. Each row's darkening is measured per channel (blue darkens
  less, giving the teal cast); dark rows are about x0.7-0.8, their neighbours
  about x0.92. Using one set only matters: averaging both doubles the lines.
- **Frame**: a bright outer line (about (0,227,8)) and a dark inner line.

Making a face:

1. Copy the photograph to `source-art/chips/<name>.webp`.
2. Add a recipe to `scripts/chip_faces.json`:
   - `crop` `[left, top, right, bottom]` in photo pixels: a square around the
     head and ruff, ears included, roughly as tight as the original animals.
   - `matte`:
     - `"lawn"` for a dark animal on grass, sky or cardboard (Cat V). It grows
       the light/green background in from the edges and keys it out.
     - `"outline"` for anything else (Cat VI, Cat VII): a polygon in `outline`,
       in units of the crop scaled to 512x512, (0,0) top left. Trace about
       10 units outside the fur; the matte refines the edge to the fur within
       12 units either side (colour clusters of the sure-inside and
       sure-outside pixels nearby) and keeps the part joined to the centre.
       Run the polygon along the bottom edge for the chest.
   - `despill_green: true` when the photo was taken on grass, so no green
     reflects off the fur before the tint.
   - `gamma`: the fur's tone curve after levels (1st to 99.7th percentile).
     Under 1 lifts dark fur. Used: black Cat V 0.62, orange Cat VI 0.78, grey
     Cat VII 0.8. Aim for a mean face brightness near the originals' (about
     130-150 on the green channel).
   - `output`: `data/assets/textures/chips/<name>.png`.
3. Run, with previews (cutout on magenta and a 6x face):

```bash
py -3.14 scripts/make_chip_face.py --only "Cat VI" --preview C:/temp/chip_preview
```

Without `--only` it rebuilds every face. The output is deterministic: the
three committed faces regenerate byte-identical. Python 3.14 with Pillow and
numpy is required; do not run it with `-I` (the scripts import each other).

Check the cutout for leftover background (bezels, gutters, a pale fringe) and
move the outline points in or out where it shows; check the 6x face against
the original faces for brightness.

## 2. The stats

Each family's `stats` are the original record fields, exactly as the
converted `parts.json` writes them:

| Field | Meaning |
| --- | --- |
| `inner_shape`, `inner_value` | aiming sight shape (0 square, 1 wide, 2 tall) and size curve |
| `outer_shape`, `outer_value` | lock-on sight shape and size curve |
| `track` | how fast the reticle follows a target (**Aim Speed**), on the port's 0..10 scale (below) |
| `lock_frames` | a lock takes this many 60 Hz frames (**Lock On Time**, in seconds) |
| `critical_percent` | chance of a critical hit, shown as **Mortal Shot** |
| `dodge_percent` | chance of the **Repulsion Shield** (the help texts' name for it) |
| `knockdown_resistance` | how well the robot keeps its weapons when a critical hit would knock one off (**Knockdown Resist**) |
| `weight` | hundredths of a tonne (22.0 shows as 0.22 t in the shop) |
| `price` | the shop price curve (shown x10 in NY) |

BUY CHIP, SELL CHIP and UPLIFT list the bold names with their values in the
CHIP dialog; the rows, their order and their layout are the dialog's `rows`
in `data/ui/chip_shop.json` and `data/ui/uplift.json`.

A curve is `{base, mid, top, knee, low_scale}` (`LevelCurve::value`): `base`
at level 0, `base + mid` at level `knee`, `base + top` at level 99, linear in
between. `low_scale` applies to negative levels; keep -1 (and 1 for price) as
the original does.

What the fields do in battle:

- **Sights.** Arm weapons lock inside the aiming sight (inner box), shoulder
  and melee weapons inside the lock-on sight (outer box); the player's boxes
  sit on the tracking reticle. A size runs from the shape's smallest box at 0
  to its largest at 999 and stops there, so values past 999 add nothing. At
  999 (half width x height, 640x480 units): aiming square 120x120, wide
  180x60, tall 60x180; lock-on square 160x160, wide 210x70, tall 70x210.
  Square covers the most area, wide reaches side to side, tall up and down.
- **Aim Speed.** The reticle moves `track_base + track_scale * min(track /
  track_full, 1)` NDC a frame toward the nearest enemy in the aiming sight
  (`data/ui/battle_hud.json` `control`). The original (0x4f6a0) reads the
  record's plain short (0x092c6c, no level growth) as 0.02 + 0.08 x track /
  999, so its CHIPs' tracks of 1 to 5 differ by under 2%. The port uses
  `track_full` 10 and `track_base` 0.012: track 1 keeps the original's 0.02 and
  each point adds 0.008, up to 0.092 at 10 (4.6x). Setting 0.02 and 999 there
  gives the original's speeds back.
- **Weight** counts toward the legs' capacity; past it, speed and jump fall
  by (capacity / weight)^10.

The original cats (Cat I-IV) share the sights (wide lock-on, square aiming
sight, same size curves), knockdown 3 -> 15, weight 20.1 and price; they
differ only in track, lock time, Mortal Shot and shield (table below). The added
cats keep the cat sights, price curve shape and knees, and move the other
stats from the owner's description of the animal:

| Trait | Stat |
| --- | --- |
| Hunts flies well / is fast | higher `track`, fewer `lock_frames` |
| Very fast, agile | higher `dodge_percent` (Repulsion Shield) |
| Aggressive / gentle, soft touch | higher / lower `critical_percent` (Mortal Shot) |
| Big, sturdy / small, indoor | higher / lower `knockdown_resistance`, `weight` |
| Demanding, rare | higher `price` |
| Lazy | lower `track`, more `lock_frames` |
| Talkative | no stat; mention it in the help text |

Resulting values (level 0 -> level 99):

| | Track | Lock | Mortal Shot % | Shield % | Knockdown | Weight | Price (base) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Cat I, Cat II | 1 | 12 | 2.0 -> 7.5 | 0.5 -> 7.5 | 3.0 -> 15.0 | 20.1 | 6500 |
| Cat III | 1 | 14 | 1.0 -> 6.5 | 0.5 -> 8.0 | 3.0 -> 15.0 | 20.1 | 6500 |
| Cat IV | 2 | 16 | 0.5 -> 6.0 | 0.5 -> 8.5 | 3.0 -> 15.0 | 20.1 | 6500 |
| Cat V Norwegian Forest Cat | 3 | 8 | 0.5 -> 4.0 | 1.0 -> 10.5 | 2.0 -> 11.0 | 22.0 | 6500 |
| Cat VI Maine Coon | 3 | 10 | 2.0 -> 8.0 | 1.0 -> 7.0 | 1.5 -> 9.5 | 20.1 | 7475 (x1.15) |
| Cat VII Ragdoll | 1 | 22 | 0.5 -> 3.5 | 0.5 -> 10.5 (late) | 3.5 -> 18.5 | 24.0 | 6500 |

Cat VI first had lock 8, Mortal Shot 10% and Repulsion Shield 10%, which
out-locked Bird II and matched the Wolf's rates; it was toned down to the row
above. Cat VII was first priced x1.3 of the other cats (the dearest CHIP at
level 99) for a low Mortal Shot and a slow lock, and now costs what they do.

Keep knees as the original's (50 for sights, critical and price; 60 for
shield and knockdown), and scale a price by multiplying all three of `base`,
`mid` and `top`. Each family's `traits` note in the JSON records why.

The specialists each beat the Wolf (the best original CHIP at level 99) at one
thing and pay for it elsewhere. Sights are shape and size, level 0 -> 99:

| | Aiming sight | Lock-on sight | Track | Lock | Mortal Shot % | Shield % | Knockdown | Weight | Price (base) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Fox I Red Fox | square 300 -> 850 | square 150 -> 700 | 4 | 14 | **3 -> 18** | 0 | 1 -> 5 | 15 | 7500 |
| Bear I Brown Bear | square **600 -> 999** (by 40) | square 300 -> 999 | **10** | 30 | 2 -> 10 | 0.5 -> 4 | 4 -> 24 | 38 | 8000 |
| Tortoise I Giant Tortoise | square 150 -> 600 | square 100 -> 500 | 1 | 30 | 0 | **3 -> 20** | 6 -> 22 | 40 | 7000 |
| Boar I Wild Boar | square 200 -> 600 | wide 70 -> 999 | 2 | 20 | 1 -> 9 | 0 | **5 -> 32** | 30 | 3500 |
| Hawk I Jackal Buzzard | wide 100 -> 500 | square **500 -> 999** (by 40) | 1 | **4** | 1 -> 6 | 0.5 -> 4 | 2 -> 9 | 12 | 7000 |
| Monkey I Mandrill | tall 700 -> 900 | square 600 -> 850 | 3 | 12 | 4 -> 5.5 | 4 -> 5.5 | 8 -> 11 | 22 | 20000 |
| Meerkat I Meerkat | square 300 -> 800 | **tall 400 -> 999** (by 40) | 4 | 6 | 1 -> 6 | 1 -> 6 | 1.5 -> 7 | 14 | 4250 |

- **Fox:** glass cannon, the highest Mortal Shot with no shield.
- **Bear:** brawler for arm weapons and close range: the fastest aim and the
  largest aiming sight early; slow to lock missiles, low shield, heavy.
- **Tortoise:** wall: the highest Repulsion Shield, no Mortal Shot, small slow
  sights, the heaviest.
- **Boar:** charger: the best Knockdown Resist (which keeps a critical hit from
  knocking off a weapon) and cheap; a wide lock-on sight for what is in front
  of it; no shield.
- **Hawk:** missile specialist: the fastest lock and a full-size square
  lock-on sight early; a small wide aiming sight keeps it poor with arm
  weapons; fragile.
- **Monkey:** front-loaded for players who do not uplift: the best CHIP at
  level 0, but every curve stops at level 15 (knee 15) and later CHIPs pass
  it; dear to buy (20000), cheap to uplift (80000 at level 99). Its first
  curves stopped at 6% and knockdown 11, which beat the Wolf until level 52;
  now the Wolf passes it on Mortal Shot by level 40 and overall by level 46.
- **Meerkat:** anti-air: the only square aiming sight paired with a tall
  lock-on sight, which reaches full size by level 40 and locks in 6 frames,
  so shoulder missiles catch robots jumping or flying overhead while arm
  weapons aim normally. The Rabbit's sights are both tall (a jump fighter);
  the Hawk's lock-on sight is square and faster but its aim is poor. Small
  and fragile: low shield and Knockdown Resist. It costs what the Rabbit
  does, whose shield is twice its own.

## 3. The chatter

A CHIP's `chatter` picks the line table its robot speaks from in battle
(`tables/chatter.json`, converted from the disc). There are 67 tables, each
listing a script for each of 35 events (first kill, shot down, hit from
behind, Repulsion Shield, low armour...); every animal CHIP family uses tables
0-4, one per personality, and the five records of a family differ only in
that. The added cats do the same: `"chatter": [0, 1, 2, 3, 4]` gives five
records per cat with the original cat chatter. Which events speak and how
often comes from `gates` and `chances` in the same table, not from the CHIP.

The face shown in the chatter window and on name entry is the CHIP's `face`,
so the added faces appear there with no extra work.

New chatter lines are not supported yet. They would need a new table appended
to `tables` (index 67 onward) whose 35 entries name new scripts appended to
`scripts`, joined at load the same way the CHIPs are; a family would then list
that table in `chatter`. Lines are full-width text, about 26 characters by 2
lines, with `{code:fff8}` for the robot's name.

## 4. Shop

`shop` lists `{day, chip, chatter, level}` offers: day 0-23 of the rotating
`chip_lineups`, the family `name` and personality, and the level it is sold
at (0, as all original offers). The current rotation sells two added CHIPs a
day beside the original eight: the cat `d % 3` (V, VI, VII) with personality
`(d / 3) % 5`, and the specialist `d % 7` (Fox, Bear, Tortoise, Boar, Hawk,
Monkey, Meerkat) with personality `d % 5`, so every specialist and personality
comes round.

`chip_help` in `data/ui/chip_shop.json` holds the help line for each index,
and `data/ui/uplift.json` holds the same list; add one string per record to
both (the five of a family alike, as the originals are), at most about 235
characters, in the original's style, ending "Modeled after <breed>.". The
original CHIPs' lines are references to the game's own descriptions
(`"Guard — {xbe:0x188988}"`, see [Data and mods](PORT_GUIDE.md#data-and-mods)); the shops show
those with their sight brackets evenly spaced.

### Bosses' CHIPs

A contest's champion whose animal has no CHIP on sale (Xeena's retriever,
Zee's turtle, Crackin's chimpanzee...) has a CHIP of its own: a family with
one personality whose `face` names the boss's portrait cell in the original's
atlas, `{"image": "textures/scroll/fight_chara00_tex/0.png", "rect": [...]}`
(the pilot's `robot_face` in `tables/parts.json`), so no picture is made.
`boss` records whose it is. Bosses whose animal is already sold (the dogs,
cats, wolves, birds, rabbits and rats of the original) have none.

`boss_shop` lists `{chip, chatter, contests, level}`. Once the player has won
any of `contests`, BUY CHIP adds the CHIP after the day's lineup on every
calendar day one of them is held (`add_boss_chips`, `chip_shop_bosses.cpp`),
once a day however many are held (Atsurou leads contests 3 and 7; the
original calendar never holds both on one day, but an edited one may). The calendar's own contests count, not the cleared game's
swap, since the First Ranker's contest (49, Kudryavka) is won by clearing it.
They are sold at level 0, as every shop CHIP.

The Labrador, Kudryavka's and the reward for beating the final boss, is the
best of the best: near the top at everything by level 99 (sights 400 and 450
growing to 999, Mortal Shot 3 -> 16%, Repulsion Shield 3 -> 17%, track 7,
lock 6 frames) and the only CHIP that never loses its weapons fully uplifted
(Knockdown Resist 5 -> 30 at level 50 -> 50, the strongest weapon's
knockdown). Each specialist still beats it at its own one thing. It is not
the boss's own CHIP (LV5: full sights, lock 4, Knockdown Resist 99 at every
level), which no player CHIP should match.

| CHIP | Boss (pilot) | Contests |
| --- | --- | --- |
| Retriever | Xeena (Atsurou) | 3, 7 |
| Macaque | Aide (Kanb@TK23) | 4 |
| Penguin | Daly (Lunch) | 10 |
| Borzoi | Blue (Shink) | 20 |
| Tanuki | Tsuyoshi (Maiko) | 21 |
| Turtle | Zee (Edmond) | 23 |
| Chimp | Crackin (Joker) | 25 |
| Siamese | Salsa (Pepper) | 26 |
| Leopard | Hale (Mion) | 37 |
| Black Cat | TIME (Screw) | 40 |
| Vixen | Charlotte (Milch) | 42 |
| Sparrow | Nichola (Roy), Area Ranker | 14 |
| Owl | Dromenon (Siba), Area Ranker | 31 |
| Ferret | Nayuta (Tienxia), Area Ranker | 48 |
| Labrador | Kudryavka (Yuki), First Ranker | 49 |

## 5. Retuning an original CHIP

`retune` in `added_chips.json` changes original records without touching the
extraction: each entry's `stats` replace those fields of every record whose
name is in `names`, before the added CHIPs are appended (so only on the
original's own table). `why` records the reason, and deleting an entry gives
the original's values back. Change the record's `chip_help` line too.

The Rabbits (61-70) are retuned into a jump fighter. Their tall sights were
the niche, but the lock-on sight started at the smallest box and the Rabbit
was the weakest CHIP. Now both sights reach full size by level 35 (aiming 700,
lock-on 500 at level 0), so they catch robots that jump above or drop below
other CHIPs' sights; lock 10 frames (was 18), Repulsion Shield 2 -> 12% (was
0.5 -> 5.75), Mortal Shot 0.5 -> 5% (was 0 -> 3). Knockdown Resist, weight
and price stay the original's.

The enemy-only CHIPs (91 LV5 to 95 LV1, the bosses') all had Aim Speed
(`track`) 1. Only the player's sights read it, so the original never tuned
it; it now runs by tier, LV1 2 to LV5 6. Nothing the bosses do changes.

## Adding another CHIP, step by step

1. Photograph -> `source-art/chips/`, recipe -> `scripts/chip_faces.json`, run
   `make_chip_face.py` (section 1).
2. Append a family to `families` in `data/tables/added_chips.json` with
   `name`, `breed`, `face`, `chatter`, `traits` and `stats` (section 2). Its
   records number on from the last (the next is 161).
3. Append its `shop` offers.
4. Append one `chip_help` string per record to `data/ui/chip_shop.json` and
   `data/ui/uplift.json`.
5. Build and run `phantomcrash_core_tests` (`added_chips_tests.cpp` checks the
   numbering, the rotation and some values; update it for the new family).
