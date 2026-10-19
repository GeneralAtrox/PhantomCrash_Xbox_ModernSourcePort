# Supply boxes

The supply carrier drops pickups ("boxes") whose kinds are data. The
original game has ten kinds; the port keeps them as they are and appends
three top money tiers, and gives any kind its own picture, colours, glow
and pickup sound. These additions are part of the base game data, not a mod.

## Where everything lives

| What | File |
| --- | --- |
| Kinds, chances, looks | `data/world/training.json` `battle.supply.kinds` |
| Crate pictures | `data/assets/textures/supply/money_<NY>.png` |
| Crate generator | `scripts/make_supply_crates.py` |
| Parse | `port/src/supply_settings.cpp` |
| Roll and take | `port/src/supply_drop.cpp`, `port/src/battle_supply.cpp` |
| Drawing | `port/src/desktop_supply.cpp`, `port/src/desktop_supply_kinds.cpp` |
| Two-colour tint, sheen | `port/src/desktop_world_shaders.cpp` (`label_colour`, `reflect_modes.y`) |
| The red carrier | `battle.supply.special`; `port/src/supply_special.cpp` |
| The rescue box | `battle.supply.rescue`; `port/src/supply_rescue.cpp` |

## The shipped boxes

Money `amount` is in stored units, tens of NY (`money.hpp`): 10000 shows as
100,000 NY. A rental robot's prize is halved at RESULT as usual.

| Kind | Box | Chance | Body | Lettering | Glow | Extra |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 1,000 NY | 6.25% | original blue | original | blue | |
| 1 | 5,000 NY | 12% | original blue | original | blue | |
| 2 | 10,000 NY | 8% | original blue | original | blue | |
| 3 | 100,000 NY | 2% | `#8A4FFF` violet | white | violet | |
| 4-6 | repair 10/30/50% | 15/10/10% | original | original | grey | |
| 7-9 | ammo 30/50/100% | 6/15/14% | original | original | green | |
| 10 | 250,000 NY | 1% | `#FF8A1E` orange | white | orange | |
| 11 | 500,000 NY | 0.5% | `#FFC83A` gold | `#4A2A08` brown | gold | sheen 0.6 |
| 12 | 1,000,000 NY | 0.25% | `#FFD54A` gold | `#B01818` red | every hue | sheen 1, glow x1.5, `spin_pay` |

The original's chances sum to 1; the 1,000 NY box gave up the 1.75% the
three new tiers take (8% to 6.25%). With three boxes a trip, a jackpot
comes on about 1 trip in 133.

## A kind's fields

```json
{ "effect": "money", "amount": 100000, "chance": 0.0025, "crate": 3,
  "image": "textures/supply/money_1000000.png",
  "tint": "#FFD54A", "label_tint": "#B01818", "sheen": 1.0,
  "glow_scale": 1.5, "glow_cycle": 0.75,
  "take_sounds": ["supply_take", "spin_pay"] }
```

| Field | Meaning | Default |
| --- | --- | --- |
| `effect` | `money`, `repair` or `ammo` | `money` |
| `amount` | stored money units, or the share of armour or magazine | 0 |
| `chance` | odds of each pickup carried; the rolls run in list order | the original's for kinds 0-9, else 0 |
| `image` | a crate picture replacing the crate's own | none |
| `crate` | which original crate (0-9) its mesh is | its own number; 0 for an added kind |
| `tint` | body colour, `#rrggbb` or `[r, g, b]`, multiplying the picture | none (the picture as it is) |
| `label_tint` | lettering colour, where the picture's alpha is below 1 | none |
| `glow` | the glow's colour | the tint, else the effect's |
| `glow_scale` | the glow's size, times the look's `glow_size` | 1 |
| `glow_cycle` | hue turns a second (0: a fixed colour) | 0 |
| `sheen` | strength of a metal rim and sun glint in the tint | 0 |
| `take_sounds` | named sounds (`data/audio/sfx.json`) played together on a take | the look's `take_sound` |
| `special_chance` | odds on the red carrier | its `chance` |

A pickup's kind is its place in the list (the battle event's `slot`, the AI,
replays and online state all use it), so **only ever append**; to retire a
kind set its chance to 0. Chances should sum to at most 1; when they sum to
less, the remainder falls to the first kind with a chance (kind 0 in the
original's roll).

The computer robots leave money to the player at ability 31 and above
(`robot_ai.json` `pickup_money_ability`); the port tests the kind's effect,
where the original tested the kind's number, so added money kinds count.

An ammo box refills one weapon among those short of rounds (fitted, not
knocked off, with a limited magazine) by its share of that weapon's
magazine, rounded up. The original picks the weapon at random, so a box
often went to a weapon that was nearly full. The port's data
(`battle.supply.ammo_to`: `"emptiest"`) gives it to the weapon with the
smallest share of its magazine left, the first of equals. A second box then
goes to whichever weapon is emptiest after the first. `"random"` restores the
original's pick, which is also the default when `ammo_to` is absent.

On a split screen the original tints a box by its effect's colour, blinking,
in place of the glow; a kind with a tint blinks in its tint instead.

The S.L.A.I. stages (`supply_pickups` in `stages.json`) draw kinds 0-9 with
their own crate and pictures, untinted; added kinds are drawn on the
original's crate there.

## Crate pictures

`make_supply_crates.py` draws each crate from scratch in vector shapes at
128x128 on the original crate's layout (rails, clamps, panel), so the crate
mesh's UVs fit. Nothing is copied from the extracted art. The body is
greyscale for `tint`; the lettering is white with alpha 0 for `label_tint`.

```bash
python scripts/make_supply_crates.py --amounts 100000 250000 500000 1000000
```

It needs Pillow and the Windows fonts Times New Roman Bold ("NY") and Arial
Narrow Bold (the amount). A picture of your own works the same way: any
size, greyscale body, lettering marked by alpha below 1; leave alpha at 1
throughout and `label_tint` does nothing.

## Adding a box

1. Append a kind to `battle.supply.kinds` with its `effect`, `amount` and
   `chance`, taking that chance from another kind.
2. For a new money tier, run the generator with its NY amount and point
   `image` at the result; pick `crate` 3 (the 100,000 crate) for a money box.
3. Choose `tint`, `label_tint` and, for something special, `sheen`,
   `glow_scale`, `glow_cycle` and `take_sounds`.

A new effect (a shield, a speed boost) needs code: a new `SupplyEffect`,
its case in `Battle::take_supply`, its parse name in `supply_settings.cpp`,
and a `glow_colours`/`take_effects` entry or a kind `glow`.

## The red carrier

Shoot three carriers down in a row before any of them drops a box, and the
next one comes red, 20% bigger, with the `warning` alarm. It hovers over
the training ground's helipad as usual and drops nine boxes, 12 ticks apart,
each drifting as it falls so that it lands on its own spot of the H: four
down each bar and one in the middle. Its boxes roll `special_chance`.

| Kind | Box | Red carrier's chance |
| --- | --- | --- |
| 0, 1 | 1,000 / 5,000 NY | 0 |
| 2 | 10,000 NY | 12.5% |
| 3 | 100,000 NY | 20% |
| 4-6 | repair 10/30/50% | 0 / 10% / 15% |
| 7-9 | ammo 30/50/100% | 0 / 10% / 15% |
| 10 | 250,000 NY | 10% |
| 11 | 500,000 NY | 5% |
| 12 | 1,000,000 NY | 2.5% |

With nine rolls, about one red carrier in five brings the jackpot.

The streak counts carriers brought down while every box was still in the
hold, by anyone in the battle. A carrier that lets a box fall, or flies its
route untouched, sets it back to 0, and so does the red carrier setting
out. The red carrier can be shot down too: its hold is lost and it pays its
own `shot_bounty`, and it does not count toward the next streak. It only
comes on the training ground; stages with routes of their own (the S.L.A.I.
stages) never send it.

```json
"special": {
  "streak": 3, "tint": "#E01E1E", "scale": 1.2, "drop_ticks": 12,
  "shot_bounty": 5000, "arrive_sound": "warning",
  "spots": [[-2.3, 3.6], [-2.3, 1.2], [-2.3, -1.2], [-2.3, -3.6], [0, 0],
            [2.3, -3.6], [2.3, -1.2], [2.3, 1.2], [2.3, 3.6]]
}
```

| Field | Meaning | Default |
| --- | --- | --- |
| `streak` | carriers in a row shot down before dropping anything; 0 turns it off | 0 |
| `tint` | the carrier's colour, `#rrggbb` or `[r, g, b]` (the night shell keeps its flashing lights) | `[0.9, 0.1, 0.1]` |
| `scale` | its size; its hit radius and hold grow with it | 1 |
| `drop_ticks` | 30 Hz ticks between one box falling and the next | 12 |
| `spots` | `[x, z]` where each box lands, one box each, in drop order (at most 64) | none |
| `shot_bounty` | stored money units paid for bringing it down (5000: 50,000 NY) | 1000 |
| `arrive_sound` | named sound played as it sets out | none |

The helipad's emblem is a 12 x 12 quad at the stage's centre (choco mesh 3,
texture 6): its bars run along z at x = +-2.3, from z = -4.5 to 4.5, with
the crossbar at z = 0. To see the red carrier quickly, set `streak` to 1.

## The rescue box

Supply boxes are the only way to get armour back in a battle, so a player
low on armour would otherwise just hope for the right box. When a player's
robot is below a quarter of its armour as a carrier sets out, that carrier's
first box is the 50% repair box. As it falls it glides toward the robot, up
to 1.5 across a tick (three times as fast as it falls), and follows the
robot if it moves. It lands at the robot's feet, or as near as it can reach.

- Each player calling for one gets their own box, up to the carrier's load.
  The other boxes roll as usual.
- The call is checked as a carrier sets out. A carrier already on its way
  carries what it rolled, and the next one brings the box.
- The box is an ordinary pickup. Any robot can take it or shoot it, and
  shooting the carrier down loses it with the hold. If the player is still
  low when the next carrier sets out, that one brings another.
- It also comes in versus battles, where both robots are players, and on
  the red carrier (whose first box then leaves its spot on the H).

```json
"rescue": { "armour_share": 0.25, "min_repair": 0.5, "glide": 1.5 }
```

| Field | Meaning | Default |
| --- | --- | --- |
| `armour_share` | below this share of its armour a player's robot calls for a box (the low-armour alarm's 0.25); 0 turns it off | 0 |
| `min_repair` | the box is the weakest repair kind giving at least this share; if none does, the strongest | 0.5 |
| `glide` | how far across the box may drift toward its robot a tick (`fall_speed` is 0.5) | 1.5 |
