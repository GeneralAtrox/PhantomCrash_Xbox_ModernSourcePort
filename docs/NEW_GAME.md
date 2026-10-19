# NEW GAME (research)

What the original does after NEW GAME on the main menu, and what the port
needs to do the same. Static analysis of the Europe XBE in Ghidra; nothing
here has been watched running yet.

## Headline: the talking characters are 2D

The characters who talk to the player are not real-time 3D models. Each one
is a pre-rendered 3D sprite sheet, `scroll/mht25_yuki_tex` … `mht40_glik_tex`
(two pages each) and `sht01_naomi_tex` … `sht23_board_tex`:

- nine full-body poses;
- a column of cut-out faces: mouth closed, half open, open, and a blink.

The event player animates them in 2D (opcode 4 below). It changes poses,
blinks at random, flaps the mouth while the text types, and fades characters
in and out. The only human-shaped 3D data on the disc is the mechs (Holy,
Photon, Aeron, drones, bosses), which the garage and battles need, not NEW
GAME. So NEW GAME needs no 3D renderer; it reuses the port's sprite layers,
font, tickers, message box and movie player.

## Scene flow

The scene table is at `0x256738`. Each factory sets the scene id
(`0xe07b0`), creates the scene's task and picks the next scene from its
result (`0xa1bf7`).

| Scene | What it is | Next |
|---|---|---|
| 21 main menu | NEW GAME: result 0 (`0xa2268`) | 28 |
| 28 (`0xa2a53`, task `0xb1af1`) | A wrapper with a 0x113cc-byte state. It stops the music and, after the fade-in, runs the scene whose id is at `0x25b48c` as its child (`0xa1ae8`); that id is 9 and nothing changes it. | 40 on quit |
| 9 (`0x570c0`, `0x571a3`) | Save checks: an existing save (`0x6b040`), free space (`0x6b0cf`, `0x6b1ed`) and message boxes (strings at `0x21cc44`); a cancel goes back to the title. No music. | 0 → 46, 1 → 40 |
| 46 (`0x9f250`, `0x9f381`) | Creates the save (0x5fea4-byte state, `0x6b99b`), with a free-blocks check (`0x6c3d6` < 0x1000) and messages (`0x2002cc`, `0x254dd0`). LOAD GAME (17) also comes here. | 0 → 25, 1 → 39 (garage) |
| 25 (`0x2e692`, `0x2e663`) | Plays `opening.sfd` followed by `opening02.sfd` (also identified in `OBJECTIVE_01_PLAN.md`). Then points name entry at the player's name, save + 0x280c, mode 0 (`0x2c5b4`). | 0 → 22, 1 → 40 |
| 22 (`0x2c614`) | NAME ENTRY (see below). Its result depends on the scene that opened it. | 0 → 8, 1 → 19, … (`0xa2e70`) |
| 8 (`0x1bec1`, `0x1bd93`) | Club Wired's menu (`club_wird_tex`, 4TOWNMAP), with six help lines (`0x1db1c0`): Rumbling, "The shop guide, Pepper, will show you around", records, schedule, change alias (back to name entry), exit. | 3, 19, 29, 32, 22, 41 |
| 19 (`0x299fe`) | Pepper's guide: a question menu ("What is Rumbling?", …) from the table at `0x1de070` (35 records of {short group, short id, char* title, int}). It returns to the scene that opened it. | caller |
| 48–157 | Story events (below). Each loads EVENTSEG (`0xa3e32`), runs one event, unloads it and goes to scene 159, then on to the scene it interrupted. | 159 |

Story events are not edges of the scene table. On a scene change,
`0xd1861(from, to)` looks for an event record whose from and to scenes
match, that is enabled, has not been seen, and whose required events have
been; if one does, it plays first. So NEW GAME runs:

opening movies (25) → **event 48** → NAME ENTRY (22) → **event 49** → Pepper's guide
(19) → event 50 (19 → 41, once 49 has played) …

Event 48 (script `0xdb4a38`) is the first conversation, Salsa and Pepper:

> "Welcome to Shinjuku Wired, where apathy and freedom abound…" · "I am
> Salsa, the store guide, a Cat Chip." · … Pepper asks the player to enter
> their Alias "in that machine over there".

That opens NAME ENTRY, which, called from scene 48, returns result 1: scene
19. Event 49 (22 → 19, 4TOWNMAP) plays on that move.

## Name entry (scene 22)

- Textures `name_entry_tex` (the background, the keyboard frame, the tabs
  Alphabet / Symbols / End, and the labels CHIP NAME, YOUR NAME, PROGRAM
  NAME and SV NAME) and `scr_common_tex`.
- A 15 × 6 grid on each of three pages. The character under the cursor is
  `0x1f99e8[page * 90 + row * 15 + column]` (a string, mapped to a glyph by
  `0xbde14`). Names hold up to 8 glyphs, in the buffer at `0x1f94d4`.
- Controls (`0x2cd21`, `0x2d194`). The masks are the original's: A `0x100`,
  B `0x200`, X `0x400`, Y `0x800`, L `0x4000`, R `0x8000`, START `0x10`,
  BACK `0x20`.
  - The D-pad moves the cursor: columns wrap (flags `0x17`, sys 4), rows
    stop at the ends (`0x12`, sys 3), and row 6 is the tab row. On the tab
    row, left and right choose the page at once (sys 9, help restarted), and
    up goes back to the grid. End has no grid, so up from End returns to the
    page and row before it.
  - A or START (`0x110`) types the character under the cursor, or ends the
    entry on End; typing at the eighth place replaces it.
  - X deletes (sys 21), Y adds a space (sys 5), and L / R change page (the
    "LIST CHANGE" hint), wrapping only on a new press.
  - B or BACK (`0x220`) jumps to End (sys 9).
- End checks the name (`0x2d723`): a name of only spaces is no name. For the
  player (mode 0) that empties the name and shows the no-input notice
  (`no_name` in `data/ui/name_entry.json`) until A. The Chip and Scoobee
  modes have their own boxes (asking for a CHIP name, and whether to leave
  the Scoobee unnamed). The result then depends on the
  calling scene: 8 → 0, 48 → 1, 4 → 2, 34 → 3, 15 → 4, 14 → 5, 33 → 6, and
  anything else → 0.
- Mode 0-3 (`0x2c5d9`) picks which name is edited: player, Chip, Scoobee
  or program.
- The CURRENT MUSIC ticker is at y 419 (`0x2a85a(…, 0x1a3, 0x17)`).
- Draw (`0x2d9ca`) and animation (`0x2ca50`, `0x2cb9e`):
  - The grid frame and tabs fade in over 20 frames.
  - Halfway, the controls box with the button hints slides in from x 640 to
    289 (30 a frame), then its hints fade in.
  - The name box then grows over 10 frames (`0xbe87c`: wider than full,
    then taller), and YOUR NAME and the name appear.
  - Each page's characters fade in over 10 frames as the previous page's
    fade out. The cursors pulse at sin(phase) × 128 alpha.

Ported: `data/ui/name_entry.json`, `NameEntry` (core) and `NameEntryScreen`.
Where the entry goes depends on the screen before it (`exits`: after event
48, Pepper's guide; otherwise Club Wired). It saves the name in the
settings' quest save.

## Event scripts (EVENTSEG)

`EVENTSEG` (`0xd4c820`–`0xec66a8`, 1.5 MB) is loaded on demand. Each event
scene's creator makes a task (`0xd19ea(1, task, start, end)`); its runner
pushes the script address and calls `0xd0f07`. Every event runs exactly one
script (110 events, 110 scripts). The event table at `0x1d0e00` has 110
records of 36 bytes:

| Offset | Meaning |
|---|---|
| +0, +4, +8 | scene id, from scene, to scene |
| +0xc, +0xd | the range of events (table indices) that must have played |
| +0xe–+0x13 | flag lifetime, predicate, contest, area, contest class and day/night variant; see Story progression below |
| +0x14 | enabled |
| +0x16 | background, into `0x1d0dd0`: {back, front} sprites × 6; the back is the 640 × 480 picture, the front its lower strip at y 296, layer 0x101 |
| +0x17 | cast: up to 6 talk-character indices, -1 ending |
| +0x1e | music (-1 none, 0xCA keeps the current track) |
| +0x20 | music volume (float) |

The event fades in over 0.5 s showing only the background, runs the script,
then fades out the same way. Music starts when the event is created.

Talk characters are at `0x1c42e8 + 40n` (28: resource 1608 + n, `mht25_yuki`
… `sht23`, and nine pose ids; a missing pose shows pose 0, `0xc7a77`). Poses
are at `0x1c4748 + 28 × id`: body, mouth closed / half / open and blink
sprites, plus byte offsets for the face parts. Cast 12 is Pepper
(`mht37_pepper`), 7 Mion. Salsa is a portrait sprite (`fight_chara00`), not
a character.

### The interpreter (`0xc5a1a`)

Commands are 13 ints (52 bytes). Each frame it runs from a checkpoint and
stops at the first command that is not done, so a page's window, portraits
and text are redrawn every frame. A finished wait moves the checkpoint past
it and ends the frame. The script ends when it reaches op 13 with every
command done.

| Op | Handler | Meaning |
|---|---|---|
| 1 | `0xc5b9d` | text: glyph codes, frames per character, flags (bit 31 unskippable, 0x20 the ▼'s fade), x, y, colour, size, layer 0x101 + n. Records come in pairs (ruby, then text; no ruby is used). The text is always drawn 18 units high (the call hard-codes 0x12); the size sizes only the ruby, at half |
| 2 | `0xc604b` | wait N frames; clears the page's fades, blinks and mouths |
| 3 | `0xc620c` | a sprite: id, x, y, layer, colour, scale |
| 4 | `0xc62ff` | a character: character, pose, x, y, layer, colour, flags 1 fade in / 2 fade out / 4 no mouth |
| 10 | `0xc6bc4` | wait for confirm (from its second frame; sys 19) |
| 13 | `0xc6e1b` | end |

- **Typing:** a character every N frames with sys 16, except spaces;
  glyphs past 0x92 (letters) set the mouth flag (`0xa69cbc` |= 8). Confirm
  while typing shows all of it the next frame with sys 20. Holding a
  trigger or stick click (0xc0c0) hides the text and runs through the
  pages. The original does not reset its character count after a skip,
  which could cut the next text short; the port resets it.
- **Codes:** `0xFFFE` new line, `0xFFFB` the player's name (cyan), `0xFFF7`
  back to the text's colour, `0xFFEE`–`0xFFF6` colours (`0xFFF3` grey, used
  for asides). Colour codes take no typing step.
- **Characters:** a fade takes 30 frames and is shared by the page's
  characters. Fully shown, each cast slot blinks (open 60 + up to 60
  frames, closed 8 + up to 4) and, unless flag 4, steps its mouth closed →
  half → open → half, holding each shape 5 frames while the flag is set.

Ported: the converter writes `tables/events.json` and `tables/talk.json`;
`data/story/talk.json` holds the timings, buttons and sounds and
`data/story/scenes.json` the scene ids of the ported screens. `EventPlayer`
and `TalkScene` (core) play a script; `TalkScreen` draws it. NEW GAME now
runs `opening.sfd` → `opening02.sfd` → event 48 → NAME ENTRY → event 49 →
Pepper's guide, and the quest save records the events played. Both movies are
listed in `data/story/new_game.json`; a button press skips one movie at a time.

Event conditions (`0xd1861`) now use saved class/area-ranker wins, the
selected contest and arena variant, and owned music. The earlier interpretation
of these fields as chapter/garage checks was incorrect; see Story progression
below for the recovered fields, predicate IDs and transition handling.

## The town frame and the radio

Every town screen draws the day and money at fixed places (`0xb1b56`,
`0xb1cdd`): "DAY-" at (51, 49) and the day + 1 in two zero-padded digits
ending at x 108; the money (cfg+0x9d0) right-aligned at x 253 in up to nine
digits with commas, then "NY" (font 3: digits 11 wide, comma 5; too big a
number shows nines). Money is stored in tens of NY and drawn times ten
(`phantomcrash/money.hpp`). NEW GAME's 200,000NY (stored 20000, set by
`0x2bec6`, which keeps the money word's cleared-game bit) and day 0 are in
`data/story/new_game.json`; [PART_RECORDS.md](PART_RECORDS.md#the-new-games-robot-and-money)
shows that a new game gives the player no robot. The CURRENT MUSIC meter's seven bars (grey `0xe93`, orange `0xe94`)
move with the music; their motion is the port's: each bar is a band of
pitches, bass on the left, measured against its own last three seconds so
every bar gets used (`MusicSpectrum`, `data/ui/frame.json`). The credit line scrolls at
y 339 (`0x2a85a`, run with the radio by the task `0x9e652`/`0x9e780`).

Opening Club Wired shuffles the songs and keeps sixteen; on the town
screens a new one starts after five seconds of silence
(`data/audio/radio.json`). `data/ui/frame.json` holds the frame's sprites.

The RIDING SV plate (`0x6dd74` → `0x6db03`; Club Wired `0x1c844`, Pepper's
guide the same call) shows the Scoobee being ridden: the low five bits of
save+0x9c8, sign-extended, pick one of ten garage records (save+0x25f0 +
slot × 0x36) and the plate draws only for slots 0–9; a new save sets −1
(`0xb9668`), so a new game shows none. The plate (`0xc08`,
`scr_common_tex` [720, 0, 984, 32]) slides in from x −320 to 0 at y 399
over 8 frames, starting when the screen has faded in; the record's name
(+0x22, up to 9 glyphs, #f0f0f0) sits at (+106, +5). The port keeps the
name (`riding` in the quest save) beside the hangar ridden
(`garage.riding`); the picture is cut at x 0 while the plate slides, as on
the original's 4:3 screen.

## The garage

The quest's `"garage"` (`port/include/phantomcrash/garage.hpp`) is the
port's model of the save's hangars and parts stock: ten hangars (save +
0x25f0, 0x36 bytes each; the first three owned in a new game, from
`data/story/new_game.json`), each with its seven part slots (body, legs,
both arms, both shoulders, CHIP) and its modules as stock entries, its
name, paint and emblem; and a stock of up to 255 entries, each a part,
CHIP or module record with its level, its name (the CHIP's is set by NAME
ENTRY) and the hangar it is fitted in. The paints and emblems owned are
kept beside them. The layout is the port's own (researcher-assigned
names); the save offsets are only where the original keeps the same
things. A dealer's SV (`tables/garage.json` robots, 0x19f920) is fitted by
putting each of its parts and modules in the stock and in the hangar
(`install_robot`, after 0x1da04).

## The cleared flag

The top bit of the money word (cfg+0x9d0) marks a cleared game: scene 37
(the staff roll, `staff.sfd`) sets it (`0x6df71`), a new save clears it
(`0xb94e0` → `0x6df99`) and `0x6dfc1` reads it. The port keeps it as
`cleared` in the quest save. Pepper's guide uses it (above). The contest
schedule (`0x6ce91`, contest 49 → 52) also reads it and is implemented.
The port's staff roll (`data/story/staff_roll.json`, played by the intro step
machine) sets it as it starts. A first First Ranker victory leads to it
through the finale and the ending movie (`docs/FINAL_BATTLE.md`).

## Club Wired (scene 8)

`0x1ba45` / `0x1bec1`: a wheel of six items (Rumbling, Training, Record,
Schedule, Change alias, Exit), eight slots 45° apart turned about a pivot
over 8 frames, the short way round; no input while it turns. Left/right
move with sys 23 and wrap; confirm plays 5 (26 on Exit) and fades out to
the item's scene; cancel moves to Exit; arriving from the town map plays
25. The cursor starts on the item of the scene before. The help panel's
line scrolls per item. See `data/ui/club_wired.json`.

## Pepper's guide (scene 19)

`0x298a4` loads `scr_common_tex`, `talk_tex` and `club_beginner_tex`, puts
Pepper (cast 12) in the talk character slots (`0xd158a`) and shows her with
one talk character command (`0x40e778` via `0xc57e0`: pose 2 at (329, 37),
layer 10, fading in), so she blinks as in a talk scene. The questions are
the table at `0x1de070`: 35 records {short group, short target, char*
text, int}; group 35 is the top list, and a record whose target is 159
opens the group numbered by its own index. `0x29247` counts a group and
`0x29264` finds its n-th record.

Controls (`0x29587`): five rows; the selected record is (scroll + row) mod
count and the list scrolls round (`0xdf6d0`), a short list wrapping the
cursor. Each level's row and scroll, the level and the group live in
globals (`0x415e84`–`0x415e9c`), so an answer comes back to its question.
Confirm (sys 5) opens a topic, or sets the lesson (`0x2928c`) and fades out
to the target; End (record ≥ 0x22) fades out to scene 8, or 41 when the
guide was opened from event 49 (the previous scene, `0x415e80`, is not
updated when coming back from scenes 0x69–0x80 or 42). Cancel in a topic
plays 8 and goes up, resetting that level; at the top it moves to End (sys
3) unless there. Once the game is cleared (below), "What is a Ranker?" and
"Who is Mona Lisa?" lead to events 157 and 128 instead of 112 and 127
(`cleared_next`).

Draw (`0x292da`): the background and CURRENT MUSIC strip (list
`0x1ddef8`), the window `0x43b` at (54, 86) scaled 1.1 × 1 about its
centre, the top band `0x43d`, the arrows (`0x1ddfa4`, `0x1ddfdc`) when a
list is longer than five, the pulsing cursor bar `0x43a` at (71, row × 30 +
142), the questions (`0x293fa`, same place, 18-unit font, white) and, in a
topic, the B BACK hint (`0x6ded3`). The answers are events 105–128 and 157,
from and to scene 159; the port returns them to the screen that opened them
(`return_scene` in `data/story/scenes.json`). The lessons (targets 42) are
a tutorial placeholder. See `data/ui/pepper_guide.json`.

## Still to build

1. The tutorial battle (scene 42): the training ground's stage draws
   (docs/TRAINING_GROUND.md); the mechs, the battle and Pepper's lesson
   script are still to come.
2. The save checks (scenes 9 and 46) as message boxes; the PC version has
   no memory-unit limits, so they reduce to "create the save".
3. The rest of the event conditions (+0xe–+0x13) as the scenes that need
   them are ported.

The 3D renderer (Direct3D 11 on SDL's device, glTF models) started with the
training ground; the mech models, motions and effects follow, for the
garage (39), the hangar and battles.

## Story progression (2026-09-30)

`story/progression.json` maps the shipped predicate IDs to editable conditions.
`StoryConditionContext` supplies the saved clear flags, calendar, contest rules
and the current match. All predicates referenced by the 110 shipped events
are supported; 74 events require this context. Unknown modded predicates remain
blocked instead of silently passing. The existing `seen_events`, `won_contests`
and `cleared` save fields remain the permanent storage, including older saves.

Focused XBE inspection corrected the earlier interpretations of `0xd1861`:

| Record byte | Meaning |
|---|---|
| +0xe | -1 is transient; other shipped flags persist |
| +0xf | predicate index in `0x40e888`; 75 or negative skips it |
| +0x10 | selected contest ID, `0x6cf1c` (not a story chapter) |
| +0x11 | selected area, from stage index / 2 (`0x6daa3`) |
| +0x12 | contest class, table `0x24b388 + 0x6c * contest`, offset +4 |
| +0x13 | day/night variant, stage index modulo 2 |

`0x96e43` counts cleared class-leader contests from `0x253368`;
`0x96ea1` counts the area's ranker from `0x2533c8`. These tables are already
represented by `world/rumbling.json`'s area rankers. Predicates 51–74 check
one through eight class wins (area order 0, 2, 1). Predicates 19–28 count
class and area ranker wins together (`0xd143b`), with thresholds 1, then
3 through 27. Predicate 31 checks 78 owned music tracks (`0x2a178`).
Predicates 9–11 check today's unbeaten area-ranker contest; 12/13 distinguish
new ranker victory from a still-unbeaten loss; 29 checks First Ranker class;
30 detects a new class-leader clear. Repeating an already-cleared contest
does not produce a new-win flag.

The transition coordinator `0xa1c4a` marks an event seen when selected,
rechecks the interrupted transition after it, and clears transient flags
when the chain completes (`0xd1477`). The native runner now follows that
ordering. It retains the selected rental/gate/contest across pre-match talk,
and retains an already-settled receipt and replay across pre-result talk.
The original source transition is preserved across multiple conversations.
Normal RUMBLING and its battle are now mapped to scenes 3 and 6.

A first First Ranker victory skips RESULT, as the original does: the match
is settled as usual, then `clear_exit` (`final_battle`) opens the finale's
epilogue (scene 10, robot states 8-18 on Umihotaru by day), which leads to
the ending movie (scene 11) and the staff roll, which sets `cleared`. See
`docs/FINAL_BATTLE.md`. `clear_exit` is editable. Already-cleared games
continue their ordinary loop.

Verification: Release build and bounded runtime log checks, no new tests.
With no leader clears, Club Wired's event 152 stayed locked. With nine
clears it played and returned to the town map; loading the same state with
152 already seen suppressed it. Normal rental entry played event 51 and
resumed battle with the pending entry. Leaving that match settled one receipt,
then played event 90 on RESULT Exit before sleep. The temporary changes to
the existing agent-owned review preferences were restored byte-for-byte.
The final victory/credits path and every individual conversation have not
been exercised through a full gameplay run.
