# Battle sounds

Almost every battle sound in the original is a DirectSound 3D voice, placed
where it happens and heard from the camera. The port does the same. The
table is `data/audio/battle_sounds.json`; the code is
`port/include/phantomcrash/battle_sounds.hpp` (what a battle sounds like),
`world_sound.hpp` (placing a sound) and `effect_mixer.hpp` (the voices).

The port AI rechecks its locks before holding a trigger: protected or scripted
entrants and targets behind stage geometry, gate doors or solid props do not
receive fire. This prevents repeated gunshot requests against cover and releases
held beam sounds when a target becomes obstructed. Audible combat between other
robots can still continue while the player enters; this change does not mute it.

## Banks

The extractor writes each bank's entries as
`sound/sfx/<bank>/NN.wav`, with NN the entry number in decimal.

| Bank | File | Used by |
|---|---|---|
| 0 | `sys` | the menus, READY/GO |
| 1 | `battle` | the computer's robots, some shared sounds |
| 2 | `battle_old` | the computer's plain shots and missiles |
| 3 | `battle_player` | the players' robots, doors, pickups |

A robot whose +8 is -1 is the computer's. The original plays a player's
sound through 0x09df75 and a computer robot's through 0x0493c0; both place
it at robot +0xb0. In the split screen, 0x09df75 plays sounds unplaced, so
the port's split screen hears every sound whole.

## Distance

0x0ddc50 and 0x0de390 place a voice and the listener at half the world's
distances (0x17accc). Every voice has a minimum distance of 1 (0x0ddd4f),
and the listener has a rolloff factor of 0.1 and no Doppler (0x0de616).
Under DirectSound's inverse distance law, a sound is at full volume within
2 world units and at half volume 22 units away, and there is no maximum
distance. The port pans a sound by the side of the camera it is on.

The September 2026 audit rechecked these constants in the XBE. They did
not need retuning. The listener now updates every active positional voice,
including one-shots, every presentation; previously a gunshot retained the
distance and pan from its start even as the camera moved. Split-screen and
menu sounds remain unplaced. The listener state and source positions are
also retained in replay sound cues.

## Voice limits and stopping

The SE BIN entry's byte +2 gives its buffer count. 0x0de8d0 cycles those
buffers; 0x0de660 restarts a busy slot at sample zero. The common NPC shot
(`battle_old/00.wav`) and player shot (`battle_player/44.wav`) each have
one voice, while the NPC beam and missile have two. Previously the port
stacked up to 24 unrestricted copies of shots, amplifying repeated AI fire.
The extractor now writes `tables/sound_effects.json`, an editable per-file
`voices` table consumed by the mixer. Aliases share the same sample and
voice budget. Custom clips without a table entry use the general voice pool.

One-shots finish at their last sample. Positional sounds and keyed loops
are stopped when leaving their screen or stopping replay. Boost loops still
follow the robot and stop when the dash ends or the robot dies. Beam sounds
follow their weapon while its trigger is accepted and stop on release,
weapon loss, empty charge or death. Their long clip is refreshed every
`beam_restart_seconds` (9 seconds, 0x09b9fe); 0x09de60 stops it when firing
ends. Previously it was retriggered as an unkeyed one-shot and its roughly
11-second recording could continue after the beam stopped.

The audit also checked the original 256-step BGM/SE millibel volume table,
its gain conversion, the SDL stream gains, pause/resume and weapon-bank
selection. Those paths did not require changes. Ordinary repeated AI
trigger pulls still produce repeated shots, subject to weapon cooldowns;
the audio fixes do not alter AI firing decisions.

## Robots

0x093637 runs every tick for each robot:

| Sound | Entry | Where |
|---|---|---|
| step | 2 | 0x093cf0 (tables 0x249818, 0x249830) |
| landing | 3 | 0x093df2 |
| jump | 6 | 0x08ea5b, 0x08d7fa |
| dash | 4, then 5 looping with the robot until the dash ends | 0x0945c1 |
| dodge | 7 (motions 13, 14) | 0x094a8d |
| air dodge | 0x13, players only (motions 15, 16) | 0x094a8d |
| shot down | bank 1's 0x10, for every robot | 0x07675e |
| low armour | 0x27 every 0x20 ticks below a quarter of the armour (0x199fd4), players only | 0x093637 |

Shot-down playback is queued once from `BattleEventKind::destroyed`, rather
than any alive-to-inactive transition. Withdrawing or replacing a robot does
not trigger this sound. The extracted `sound/sfx/battle/16.wav` is 1.332 seconds;
both player and computer mappings select it. The desktop logs the file queued
for each destruction as `Death sound:`.

Steps follow the feet. `0x0ad737` walks the skeleton and, for a grounded
robot whose motion time moved (motion 10 excepted), counts in robot
+0x210c every contact node whose scale-channel key cursor changed
(`0x0adc1c`). `0x093637` then calls the robot's step callback (+0x44b8, set
by `0x093470` from the table at `0x249848` by robot +0x34ac) once if the
count is nonzero; `0x093cf0` plays the step in motions 0-3. The port plays
the step when a walking robot's contact node crosses a key
(`contact_stepped`, the same crossings that start the step dust). A robot
without contact nodes counts steps by time instead (`footstep_seconds`, a
port value), faster as the walk speeds up.
The original's strafe (`0x08dede`) slides the robot sideways without
changing its state or motion, so a strafe alone takes no steps; the port
animates the walk while strafing but plays no steps for it, so the
rollers (HL4Roller and the other wheeled legs) no longer clatter sideways.

## Weapons

A weapon's muzzle sound (0x098bb7 → 0x099c02) comes from the handler its
record's +0x21 selects in table 0x2537c0. The extractor writes that byte as
`fire_sound` in `tables/parts.json`.

| +0x21 | Handler | Sound |
|---|---|---|
| 0 | 0x099d26 | by mount and model (+8): shoulder models 1/11/21/31 → 0x19, 6/16/26/34 → 0x1b; arm models 8/29 → 0x1c, 32 → 0x1d; otherwise the plain shot 0x2c (bank 2's 0 for the computer) |
| 1 | 0x09a0da | the plain shot |
| 2 | 0x09a15e | a grenade (HUD kind 3) → 0x18, a missile → 0x30 (bank 2's 2) |
| 3 | 0x09a3ab | 0x18 |
| 4 | 0x09a409 | 0x17 |
| 5 | 0x09b9fe (the beam's update) | 0x2f (bank 2's 1) |
| 6 | 0x09c337 (the melee update) | the swing, by model |

A landing shot or strike sounds too. A melee strike plays 0x1e (bank 1's
0x26 for the computer) (0x098eda, impact kind 4). Other shots play bank 1's
entries whoever fired them (0x047ed9): a missile 0x0f, the straight shots of
models 6/16/26/34 0x0e, and the rest 0x2e. Beams and lobbed grenades make
no landing sound.

The rules in `fire` and `impact` are tried in order, and the first match
wins. A rule can match on `style` (+0x21), `mount`, `models`, `hud_kind` and
`flight`. A weapon without a record plays `fire_fallback`.

## Other sounds

The gate doors (3/0x31 as a door moves, 3/0x32 as it opens) play at their
door. A supply pickup (3/0x26, 0x066e50) plays at the robot that takes it.
READY/GO and the chatter's opening sound play unplaced, as in the original.

The pickup sample is Xbox ADPCM, 44,100 Hz mono, with 64 frames per
36-byte block declared in its RIFF format. The converter retains the
header predictor and the next 63 decoded samples, matching xemu's
`hw/xbox/mcpx/apu/vp/vp.c` playback (`ADPCM_SAMPLES_PER_BLOCK = 64`).
The former 65-frame export inserted an extra sample at every block boundary
and lengthened playback by 1/64. Pickup entry 38 now has 59,776 frames
(1.355465 seconds). Existing extractions need their sound banks regenerated
to receive this correction; the executable reads the converted WAV files.

## Landing surfaces

0x094d86 picks a landing's sound by the robot's contact flags (+0x27f8):
3/0x11 on surfaces 5 and 7 (flags 0x1400), 3/0x12 on surface 6 (0x800),
for any robot, and the robot's own landing sound otherwise. The port reads
`robot.landing_surfaces` in `battle_sounds.json`, matching the robot's
stage contact and the surface it stands over (`Combatant::floor_surface`).
The training ground's floor is all surface 9, so the robot's own landing
sound plays there.

## A player's robot and the carrier

`port/src/battle_sounds_pilot.cpp` plays the rest of 0x093637, which runs
only for robots with a player (robot +8 not -1):

- **Servo** (0x0949a0): 3/0xe starts once the twist stick (robot +0x2f68)
  pushes past 0.1 (0x17ac38) while the twist (+0xf8) is inside 45 degrees
  (0x17d0e0); 3/0xf plays when either stops holding.
- **Optic Camouflage** (0x093c5d, the state at +0x3494 run by 0x074e80):
  3/0xc as it turns on (state 1), 3/0x10 as it turns off (state 5), and
  3/0xd twice when damage breaks it (state 7, set by 0x075444 from
  0x07675e). The port now breaks a running camouflage on damage too, and
  its fades follow the original: 60 frames on and off, 20 when broken.
- **Lost arm** : 3/0x25 every 5 s (`DAT_ac73b8` x 5) while an arm mount
  (+0x8c, +0x90) is in state 4, its weapon knocked off (0x07973f).
- **Locked on**: 3/0x2a loops while any of the six flags at +0x8a2 is set.
  0x051045 sets flag i when robot i holds this robot in its lock list.
- **Prize**: 1/0x13, non-positional (0x03ea7a), on any tick the robot's
  prize (+0x18) differs from the last tick's (+0x1c, which 0x0754eb then
  updates), only in battle mode 0 (quest +0x618c) and while the robot has
  armour left. The prize grows by one stored unit (10 NY) for each hit that
  damages another robot (0x09d2ce, except in scenes 0x2a and 0x2f, the
  tutorial and TEST DRIVE), so every such hit is heard, as are kills,
  scenery and money pickups. The port plays it in the training screen
  (`prize_sound`, `hit_reward_ny`; both set for RUMBLING in
  data/world/rumbling.json).

The supply carrier loops bank 1 entry 0x2f from the battle's start
(0x066940 starts it at the carrier); the falling wreck carries it and the
crash stops it (0x06828e, then effect 0x3f). The supply code plays nothing
else: there is no separate destruction sample, and a shot at the carrier
sounds as any impact. The only other supply sound is a player's pickup,
3/0x26 (0x066e50).

## Native category mixing and coverage (2026-09-30)

Settings > Audio exposes Master and eight independent categories in 10%
steps, including mute. All default to 100%, preserving the previous mix.
These are additional native mix controls: SOUND CONFIG's existing BGM and
SE levels still apply. Music uses Master × Music × BGM; effect voices use
Master × category × SE, before their existing sample and spatial gains.
Movies use Master × Movies; their combined soundtrack cannot be separated
into dialogue, music and effects without replacement source stems.

| Category | Connected producers |
| --- | --- |
| Music | Menu/stage tracks, radio and jukebox |
| Weapons | Weapon fire, held beam voices, projectile/melee impacts |
| Robots | Footsteps, landing, jump/down boost, dash loop, dodges, low-armour alarm, robot death |
| Environment / destruction | Gate motion/opening and extracted prop break sounds |
| Pickups | Supply collection |
| Interface | Menu navigation/confirmation, sound test, READY/GO and other screen notifications |
| Dialogue / chatter | Story text/page sounds, tutorial messages and battle chatter notifications |
| Movies | Intro, opening story movie and credits movie audio |

Dialogue currently means the sounds the text/chatter system actually emits;
this pass does not add spoken dialogue recordings. Destructible break sounds
were already connected through `StageObjects::take_sounds`; the old gap entry
was stale. The current extraction contains 174 nonempty break-sound mappings
among 222 object records. All 89 distinct explicit WAV/OGG references found
in the checked-in data resolve across the runtime's asset roots. These
counts establish configured-file coverage, not completeness against every
original sound event. The original events listed above remain unmapped.
Footsteps follow the feet's contact keys (above).

Each playing effect retains its category. Gain is applied when mixing, so
changes affect existing loops/one-shots without restarting them; muted voices
continue advancing and stop normally. Replay records the category alongside
the cue. Categories are assigned by the producing event, allowing the same
sample to serve robot death and prop destruction independently. Movies also
update both the current and preloaded player's gain.

Preferences are stored under `audio` in the existing settings JSON. Selection
saves immediately without checkpointing an active match; missing fields in
older settings default to 100%. Logs report mix changes and successful cue
submissions per category on exit (including muted submissions). The Movies
stream is separate and is not included in the effect submission counts.

Validation: the standard Release build passed. A bounded run exercised the
native menu commands for Master and every category, read back the displayed
levels and confirmed they were written while the application was still open.
A fresh battle process reloaded those exact values, including Weapons at 0%.
Its logs counted 536 weapon, 144 robot, 5 environment and 2 interface cues,
with no audio-load failures. This run did not collect a pickup, trigger
dialogue, or play a movie; those paths were checked in source, not verified
by listening. No new tests were added. The user's settings were not modified.
Cleanup of `build/port/audio-options-review.json` was rejected by automatic
approval review ("blocked by policy"); the isolated review file remains.

## Pause-menu and homing acquisition cues

The native pause overlay queues the existing `window` UI effect on opening.
Gameplay pause freezes music and non-UI effect cursors while keeping the UI
channel live; focus loss still pauses the entire device. The runner drains the
menu cue before returning from its paused tick.

Each local viewport plays `battle_player/11.wav` once when a homing weapon
acquires a new target, including reacquisition after losing the lock. Simultaneous
mount acquisitions are coalesced into one cue. This is distinct from the existing
`battle_player/42.wav` warning for being locked onto. The acquisition sample is
called as bank 3, entry 0x0b at European XBE 0x05137d and 0x052153.
