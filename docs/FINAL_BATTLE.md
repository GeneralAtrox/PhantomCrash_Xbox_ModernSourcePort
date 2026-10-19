# Final battle and ending

Winning the First Ranker's contest for the first time ends the story. The
original goes straight from that battle to a scripted epilogue (scene 10) on
Umihotaru by day: the player's robot alone, out of the player's hands, filmed
by eleven camera scenes with lines from Kudryavka and, last, DEF TV's call-in
show. The TV then switches off, and the ending movie (scene 11) leads to the
staff roll (scene 37), which marks the game cleared.

The port plays the epilogue on the training screen (`TrainingFinale`,
`port/src/desktop_training_finale.cpp`) from a sequence in the core
(`FinaleSequence`, `port/include/phantomcrash/finale.hpp`). The ending
movie is an intro script (`data/story/ending.json`). `--start final_battle`
and `--start ending` open them directly.

## Route

European `default.xbe`; addresses are the original's and the meanings are
researcher-assigned.

- `0x089861`, the battle's victory state: for the First Ranker (class 23) it
  sets robot state 0x17 with a 240-frame timer (class 22 sets 0x14 and the
  area instead). The port holds the won battle for
  `story/progression.json` `clear_hold_frames` (240) before moving on.
- `0x089d1b` leaves states 0x13-0x17. In mode 0 it adds the prize and sets
  mode 6, keeps the robot's armour and parts in the save (+0x10eac..), and
  sends battle exit code 6 (`0x048ef6`), which opens scene 10.
- Scene 10 (`0x01c18e`) sets stage 4 (save +0x6104; `0x06daa3` gives area 2
  by day), sets up contest 0 with `0x045270(0)` ("No games today", an empty
  team) so only the player's robot is there, and sets battle mode +0x618c to
  3. The battle task is the usual one.
- `0x080778` in mode 3 starts robot state 8 with +0xef320 = 1, and reloads
  the armour (+0x44) and nine dwords of parts from the save.
- Scene step `0x01c138`: once the battle reaches mode 6 it captures the
  last frame into texture slot 549 (`0x0b2110`) and plays TVOFF3 (track
  0xb2). `0x01c0e7` then runs `0x06c933`, which draws that frame scaled about
  the screen's centre through the keys at `0x21ce18` (frames since the start:
  a quarter-width line at 2-3, the full picture at 4, a 5% high line at 6-7,
  the full picture at 8, gone at 11), waits `DAT_ac73b8` x 2 = 120 frames,
  fades out, and the exit leads to scene 11.
- Scene 11 draws the ending movie's 13 subtitles (`0x01c4fa`) from the table
  at `0x1db4a0`: `{int16 start, int16 end, char *text}` in 60 Hz frames,
  Shift-JIS full-width Latin, centred on x 320 at y 416 in colour 0xf0f0f0.
- Scene 11 (`0x01c372`) plays `ending.sfd` with no skip (`0x0aed1a` tests only
  for the movie's end), then scene 37.

The port settles the match as usual (prize, repairs, contest win), skips the
RESULT screen, and opens `story/progression.json` `clear_exit`
(`final_battle`). The robot's armour share and lost weapons are carried into
the epilogue.

## Robot states 8-18

The state table at `0x242d38` holds three handlers per state (enter, tick,
leave; robot +0x276c is the state, +0x2770 the phase), which `0x088b69` runs.
States 8-18 share one set of three:

- Enter (`0x08ac52`): freezes the robots and counts the wait (+0x277c).
  Once the wait has passed, it fades in (`0x09f734`) and starts the scene.
  It also starts the carrier on its route (`0x06789f`, supply route record
  cam + 24 at speed `0x21c74c[cam]`; -1 sends it away with `0x0678da`) and
  sets the carrier's lights (save +0x10ef8..). Running each wait frame, it
  holds the carrier at the route's start until the scene begins. State 8 plays
  sys sound 47 at wait frames 15-16, and state 9's scene starts END1A (track
  0x39).
- Tick (`0x08b017`): one frame less per frame (+0x2750). At the phase's
  `script_frame` it shows its line (`0x091f29`) with battle sound 45, except
  in state 18. With `fade_out_frames` left it fades out (`0x09f6e0`). In
  state 18 the callers ring in at the frames listed at `0x1b5b14`, each with
  battle sound 45, then every other frame once the list runs out. Over the
  last 60 frames, sprites 3063 and 3074 show on odd robot frames (+0x40b0),
  both centred at (320, 240) on layers 0x1002 and 0x1003, each with
  battle-player sound 39. The state ends with 2 frames left.
- Leave (`0x08b397`): with `dissolve_frames`, the dissolve (`0x0b29e0`):
  the frame is captured into slot 549 and `0x09fa85` draws it over the next
  scene, full screen, its alpha falling over those frames. Then the next
  state, or mode 6 after state 18.

`0x068db0` switches the carrier's particle effect groups (`0x0878d0`):
0x41-0x44 at the carrier while `carrier_flag` is set, and 0x49-0x4c,
0x45-0x48 and 0x4d-0x4e at the three lights' points while each is on. They
look like showers of paper.

The fade (`0x09f6e0`, `0x09f734`) has four states: clear, fading out, black,
fading in. A fade-out starts only from clear and a fade-in only from black.
Scene 10 opens with a two-frame fade-out.

Each state's camera and walk are its entry in the scene table
(`tables/battle_scenes.json` `states` 8-18, now converted alongside 1-6), in
world coordinates with the z channels negated (`0x0ae01d`). Walks keep only
x and z (`0x090f8a`); the floor sets the height. Before state 12's walk, the
robot stands where that walk starts.

## Data

`tables/finale.json` (`src/phantomcrash_research/finale.py`, written by the
robot conversion):

- `phases`: the phase table at `0x242f78`, 0x34 bytes per state 8-18. Each
  row has `wait_frames`, `fade_in_frames`, `fade_out_frames`,
  `dissolve_frames`, `carrier_route`, three `lights` (`point`, `on`),
  `carrier_flag`, `script` (or null) and `script_frame`.
- `scripts`: the phases' lines as talk-script command arrays.
- `call_ins`: the frames at `0x1b5b14`, up to -1.
- `light_points`: three points pushed as immediates at `0x11e660`.
- `banners`: sprites 3063 and 3074.
- `carrier_routes`: supply route records 24-30 (bezier segments) with their
  speeds from `0x21c74c`, in segments per 60 Hz frame.

`tables/ending_subtitles.json` holds the 13 subtitles, decoded to text.
`data/story/ending.json` `subtitles` names that table and the position and
colour.

`data/story/finale.json` holds the stage, the next screen and the timing
constants above, named for what they do, with `end.tv_off` (the TV-off keys
as `frame`, `scale_x`, `scale_y`, `alpha`), `carrier.effects` and
`carrier.light_effects` (the effect groups). `data/audio/music.json` adds
`ending_theme` (END1A.WAV) and `tv_off` (TVOFF3.WAV). `data/audio/sfx.json`
adds `finale_start` (sys 47) and `warning` (battle_player 39). The lines and
call-ins use the battle chatter's `chatter` (battle 45).

The file names END1A.WAV, TVOFF3.WAV and ending.sfd come from the shipped
data. The table field names, state names and the `final_battle`, `ending`,
`ending_theme`, `tv_off`, `finale_start` and `warning` names are
researcher-assigned.

## Differences

- The carrier's frame turns about the vertical only (its heading); the
  original's matrix (`0x966980`) is built by `0x06984a`, whose tilt, if any,
  was not traced.

The paper showers follow the original. `0x0689bb` starts the carrier's groups
(kinds 0, 0, 1, 2, 3) at `0x966960`, (0, -0.5, -1.7) in the carrier's frame,
and the lights' groups at their points. Every group heads along `0x966c88`,
(0, -0.62, -1) turned with the carrier, and `0x068db0` keeps them there each
frame. The carrier is the supply carrier itself: `0x06717c` runs its state
function and draws it with `0x0699d5` in the finale as in a battle. So the
port's model and `spin_speed` spin are the original's.

## Verification

- `port/tests/finale_tests.cpp` covers phase timing and fades, lines and
  their sound, the call-ins, banners and the TV-off end, camera and walk
  coordinates, and the shipped data.
- `tests/test_finale.py` checks the converted tables.
- Bounded `--smoke-test` captures of `--start final_battle` showed the stage,
  the robot walking, Kudryavka's lines, the call-in show and END1A, then
  TVOFF3 and the ending movie; the dissolve between states 12 and 13, the
  paper showers in state 15 and the TV-off squeeze.
- Not checked on screen: the carrier itself, the 240-frame hold and the
  subtitles over the movie.
- The first-clear route from a won First Ranker contest was not run end to
  end.
