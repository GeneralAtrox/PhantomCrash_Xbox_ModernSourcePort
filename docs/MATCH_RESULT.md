# Match result

RUMBLING and Quick Rental now end when the player's robot dies, when the
player leaves, or when the player defeats a ranker. Defeating another
contest's champion does not end the battle (its defeat scene gives the
battle back, `0x089d1b`): the player fights on, and leaving afterwards is a
Win, as the champion grades up to 40 shootdowns imply. RESULT owns
a snapshot of the final battle view, so the live world cannot move its backdrop
or restart combat under the receipt. Training and test drives return to their
own caller on death, without charging a rumbling repair bill.

Destruction starts motion 12 and makes the robot untargetable immediately,
but keeps its animated model visible until the clip finishes. RESULT waits
for the player's death clip; victory also waits for visible enemy deaths.
The playable timer stops on death, and a cockpit view returns behind the
robot for the sequence. Replay records the death poses. Robots without a
loaded pose use `death_fallback_seconds` from `world/training.json`.
`Death animation:`, `Death sound:` and `Result:` logs expose the sequence.

The receipt includes starting money, prize, the rental deduction, right/left
arm and backpack repairs, body/leg repair, total repair and final NY. It shows
the verdict graphic (Retire, or the grade), shootdowns, and playable battle time (entrance/exit
cinematics excluded). The shared menu frame draws day, money, music credit and
the audio-driven visualizer; the hint scrolls through the header. The original
RESULT atlas supplies the frame, receipt, labels, Retire and buttons.

## Recovered rules

European `default.xbe`, researcher-assigned meanings:

- `0x091a61`: capture starting wallet, kills, time, rental status and repairs.
  Rental prize is shifted right once. A weapon incurs a repair charge if lost
  or if the player died; an empty slot costs nothing. Body and legs are charged
  only on death, using their combined prices.
- Repair = truncate(base-level price * 0.20 + current-level price * 0.01).
  The floats are at `0x19b584` and `0x199f78`. Both prices, the rounding and the
  rental division operate in stored tens of NY; display multiplies by ten.
- `0x096c45`: cap net losses at the player's wallet. Native settlement also
  respects the existing wallet ceiling.
- `0x094fee`, static table `0x249a60`: RESULT/receipt layout; `0x249bb0`:
  rental annotations 2271 RENTAL and 2272 50% OFF. `0x095e74` selects them
  using the match clock divided by 30, modulo two. The earlier claim that
  they switch at tally completion was incorrect. `0x09547d`: LOSER music on retirement and WINNER
  on a surviving result.

`data/ui/match_result.json` owns repair/rental tuning, source sprite rectangles,
row positions, presentation timing, hint and music. The receipt reveals its
rows, then counts prize, each nonzero repair, total repairs and the wallet in
sequence (`0x095908`, `0x09699b`). Zero/unchanged values skip counting. Native
timings are editable: a 0.5-second opening delay, 1/6-second row reveal and
0.5-second count per active row. These are native wall-clock settings, not a
claim of identical original frame timing. Counting is skippable with
A/START, B/Back or a button click. A subsequent
press activates the selected REPLAY or EXIT button. Settlement runs once
on entry, independent of the tally or button presses. The live world's final
kill is collected even if its next 30 Hz observation has not occurred yet.

REPLAY watches the recorded match and returns to this same receipt when stopped
or finished; see [REPLAY.md](REPLAY.md). EXIT follows scene 30 to scene 24:
an eligible post-match conversation plays first, then the existing next-day
sound/transition advances the day and opens the Garage (scene 13). Story events
with conditions the existing story engine cannot yet evaluate remain gated.
Behind the receipt, after a RUMBLING contest (alone or co-op), the battle screen just left
runs a fresh battle of the contest's robots on its stage, as the original
does: `RumblingWorld::stand_up` stands up to six of the area's roster at
full armour on the enemy path, robot 0 (the player's place) out, each on
a team of its own, and the screen (`TrainingScreen::start_backdrop`) keeps
its stage, looks and effects and runs it without the HUD and unheard. The
area's own battle is left as it was. A co-op contest stands them up from
its match's world and save, and the places the players took go back to
the contest robots' looks. `ResultBackdrop` watches one robot
through the stage's fixed cameras with the eye brought within 12.5 of it
(`0x07e538`), picking another standing robot every 20
seconds or when that one is down, by the original's rule below; with
nobody standing the camera stays where it was. RESULT follows only
contests; one played without the live RUMBLING world (its tables failed
to load) loops its own replay with the same camera rule instead, or
shows the battle's last view without one. A contest won for the first time shows its trophy first
(`ResultTrophy`, `match_result.json` "trophy"), as below. What the
original does (researcher readings):

- **Moving background.** RESULT (`0x094fee`) starts a fresh battle task
  (`0x04a6a0` spawns `0x046aa5`/`0x047aab`, the battle globals' 0xef370
  bytes) with quest +0x618c set, and stands up to six robots from the
  contest roster (`0x045a68`, `0x0456b7`; an empty slot is marked 0xfffe).
  `0x04a78c` watches one robot through `0x07e538`: the battle camera's
  fixed view (`0x07f641`, the placement table's view 5: every 30 ticks the
  first of the three stage points nearest the robot that sees it, else the
  nearest), its eye then brought within 12.5 of the look point (the torso
  one leg radius up) along the line between them. With no point seeing the
  robot the eye can stand behind or inside scenery, as in the original.
  Every 600 frames, or when that robot is gone or its +0x4c passes 60, it
  picks another standing robot at random ((current + 1 + rand % 5) % 6);
  with none it falls back to `0x07e657`.
- **Award.** On a champion win (+0x1a) of a contest the player has not won
  before (`0x04a184`), the contest's award (pairs at `0x249e10`: contest,
  award) loads its trophy model from `0x249dd0` (resources 218-225:
  `trophy_lastboss`, `trophy_shinjuku`, `trophy_shibuya`,
  `trophy_aqualine`, `trophy_groupboss`, `trophy_regulation_0..2`) as
  0x390002/0x390003, unloaded by `0x094f43`. RESULT also sets a camera at
  (0, 0, -10) looking at the origin with a 52-degree field of view and a
  light from (0, 0, 3). `0x096466` draws it: a countdown (RESULT +0xa4)
  starts at 180 frames and the tally waits (`0x095c55`) until it passes
  0. Meanwhile the award's name plate (`0x249cb8`: sprite 2262-2265)
  slides in from the right at 30 units a frame, still from 150 frames
  on, and a black sprite (3708) at alpha 160 covers the backdrop. Below
  0 the trophy moves from its start to its end pose over 30 frames
  (tables at `0x98cbb8`, `0x98cb58`, `0x98cc18`, `0x98cc78`, set up by
  `0x11e6b0`), turned about z, y and x and spun 0.02 rad a frame about
  its own y; the plate slides out to the left and the dim fades by a
  hundredth a frame.

## Attract demo

The title's idle timeout starts the same kind of battle on its own
(original scene 1: factory `0xa2c0e`, init `0x0135a0`, task `0x0134cd`;
researcher readings). The port's screen is `attract_demo`
(`desktop_screens_attract.cpp`, `desktop_attract_demo.cpp`), tuned by
`data/ui/attract_demo.json`:

- **Battle.** `0x0134cd` draws a contest from `{3, 7, 20, 24, 37, 41}`
  (`0x1d6f54`, rand % 6), sets its roster up (`0x045270`), stands up to
  six of it (`0x045a68`, `0x0456b7`) and starts the battle task
  `0x04a6a0` with quest +0x618c = 1, which the draw (`0x047aab`) reads as
  "no HUD" and the setup (`0x046aa5`) as battle state 3. The stage is the
  save's stage index (+0x6104: area = index / 2, night when odd; the
  finale's 6 and 7 play 0). The port draws the roster with
  `draw_roster`, stands it up with `RumblingWorld::stand_up_contest`, builds a
  RUMBLING battle screen on that stage and turns it into a backdrop at
  once (`TrainingScreen::start_backdrop` with the robots' looks), which
  `ResultBackdrop` follows by the camera rule above.
- **Stages.** The cleanup (`0x013383`) moves the index on by `0x1d6f34`
  (`[2, 3, 4, 5, 1, 0]`), so one session's demos go round stages 0, 2, 4, 1,
  3, 5. The port keeps the index in the runner for the session, from 0.
- **Timing.** Once the battle exists (`0x0468b2`), `0x013488` plays track
  5 (12SENTOU, the port's `tutorial_battle`) and fades in over 0.5 s;
  `0x013473` waits for the fade. `0x013404` counts 60 s (ac73b8 × 60 scene
  ticks) and draws PRESS START BUTTON (`0x0135cd`: sprite `0xe91` at (225,
  336), blinking). A fresh press on any pad of mask 0xfff0 (`0x01365b`:
  every button but the D-pad) cuts to the title without a fade (result 1,
  scene 40). Running out fades out over 0.5 s (`0x0133e8`) to the intro
  loop (result 0, scene 158), as the continuation `0xa2c54` maps them.
- The battle runs unheard in the port, as the receipt's does; whether the
  original's demo plays its battle sounds was not checked.

## Result artwork and controls

`0x094fee` selects the grade using survival, champion defeat and shootdowns.
The sprite lookup is `0x249db4`: retirement uses 2312; surviving without
defeating the champion uses no grade at zero kills, 2308 (Good Job!) at 1,
2311 (Nice Fight!) at 5 and 2309 (Great!) at 20. Champion wins use 2306
(Excellent), 2307 (Fantastic) at 20 and 2310 (Marvelous) at 40. The champion
flag at quest +0x6196 is set through `0x082c40` when loadout +0x6c is 2,
matching the native champion roster classification. Thresholds and sprites
live in `verdicts`. Win and Leave show only the grade sprite; retirement keeps the original Retire
graphic. No native outcome label is drawn over the receipt.

Shootdowns use the RESULT atlas's large italic digits 2282..2291 with advance
28 (number font 4). Play time uses 2292..2301 with advance 13 and colon 2303
with advance 6 (font 2). These are the packed entries at `0x27dc18`, consumed
by `0x0962cf` and `0x0963a9`; generic glyph text is no longer enlarged to
imitate these counters. Monetary text and the shared day/music frame remain.

RENTAL/50% OFF alternate throughout the receipt. Button bounds follow their
configured sprite geometry. Replay is dimmed and excluded from selection
when no recording exists. Menu cursor/confirm/cancel cues use the Interface
audio category. A confirm arriving during tally completion cannot also
activate a button. Returning from Replay preserves the completed tally and
the original settled balance. Presentation never mutates the quest save.

## Validation

2026-09-30 presentation pass: the standard Release build passed. Bounded
live-match runs reached a zero-prize/zero-repair Leave receipt, completed its
reveal, opened Replay and returned to the same result. Each run logged one
settlement. A confirm during reveal logged tally completion without leaving
the receipt. The final digit-font build also passed that skip path. Nonzero
repair counting and each graded outcome were inspected in code/data, but
were not exercised in these runs. An in-memory native-window capture returned
black, so it is not visual validation of the layout. No tests or temporary
files were added for this pass.

2026-09-30: bounded live-match runs of the release executable confirmed enemy
death starts/completions and destruction sound cues. The player run logged one
sound cue, motion 12 starting at 0 seconds, completion at 2.083 seconds, then
exactly one RESULT transition. No automated tests were added for this change.

Earlier core tests covered the 200,000 + 90 - 8,820 = 191,270 NY rental example, survival
versus retirement repairs, empty slots, level prices, rounding, wallet limits,
and final-tick bounty collection without duplicate payment. A temporary
one-hit-player runtime exercised death through the actual battle and screen
runner; production armour and shield tuning are unchanged.

Runtime checks cover voluntary exit, retirement, repeated playback, and leaving
RESULT with the same final balance and exactly one settlement. Post-match routes
were checked both with an eligible conversation and directly through next day
to the Garage.
