Version 0.5.21 draws dashes in game-font text and cleans up the menu
titles. 0.5.20 made online co-op rollback cheaper, added a one-tick online
input delay, turned the co-op sharing options on by default and logs the
details of an online divergence on both players' machines.

This is a work-in-progress Windows x64 port. The ZIP includes
`phantomcrash_port.exe`, the game data installer `phantomcrash_extract.exe`,
their libraries and editable `data`. Extract the entire ZIP before running it.
No game assets, executable, disc image or Xbox firmware are included, apart
from the executable icon, which contains a render of game artwork (see
NOTICE.md); the game is installed from your own European Xbox disc image. Python is not
needed.

## Setup

1. Use Windows 10 or newer with a Direct3D 11 capable GPU. Nothing else
   needs installing: the Microsoft Visual C++ runtime DLLs ship beside the
   EXE.
2. Run `Start Game.bat` or `phantomcrash_port.exe`. The first time, the game
   asks for your Phantom Crash (Europe) disc image and installs the game data
   from it in a console window (about ten seconds and 3.5 GB). Instead, you
   can drop the `.iso` onto `Install Game Data.bat` beforehand. Both trimmed
   XISO and full Redump-style images are supported.

The game data goes to `artifacts\europe` beside the EXE: `files` holds the
disc's files and `metadata` their sizes and SHA-256 hashes; `assets` holds
the textures, models, sounds and tables converted from them. The disc image
is only read during installation and can be moved or deleted afterwards.

## Updating to a new version

Extract the new ZIP and copy your `artifacts` and `mods` folders into it (or
extract the new ZIP over the old folder). The disc is not needed again:

- When a new version changes the converted assets, the game converts them
  again from `artifacts\europe\files` on its first start (a few seconds in
  a console window). `assets\manifest.json` records the converter version.
- The disc is asked for again only when files in `artifacts\europe\files`
  are missing or have changed size. `phantomcrash_extract.exe install
  --verify` also compares every file's hash; `--convert` converts again
  even when the assets are current; `phantomcrash_extract.exe status`
  reports what is installed.
- Installation writes to a staging folder that replaces the old one only
  when it is complete, so an interrupted run leaves the previous data in
  place and is redone on the next start.
- `mods`, `data`, settings and saves are never changed by installation.
  Keep personal asset changes in `mods`, which overlays `artifacts`, rather
  than editing the converted assets.

Settings and saves use the same app-data location as a source build. The
`--settings <file.json>` option selects a separate settings/save file.
Controllers, keyboard and mouse are supported; bindings are available under
Settings. F11 switches fullscreen. `Start Game.bat --start rumbling_d_rental`
opens the Class D rental shortcut (`rumbling_d_night_rental` for the night
one); it uses and saves the current quest.

## Included changes

0.5.21 changes:

- Dashes in game-font text, such as "Guard — A standard square" in the
  CHIP list, are drawn with the font's own dash instead of a missing-glyph
  mark. En dashes and minus signs use the font's hyphen.
- The Hangar, Club Wired, Plustech and Wild Machines menu titles erase
  their whole title plate, so none of the original lettering shows round
  the redrawn title.
- Online play is unchanged from 0.5.20; both players should still use the
  same version.

0.5.20 changes:

- Online co-op corrections cost less: a correction replays fewer ticks,
  each battle copy and state check is cheaper, and a state check is put off
  from a frame that already replayed a correction.
- Online controls take effect one tick (1/60 s) after they are captured,
  so the partner's controls usually arrive in time and fewer corrections
  are needed. `online_input_delay_ticks` in `settings.json` sets 0 to 4.
- Experimental co-op, shared shootdown credit, shared cash and shared boss
  and contest clears are on by default, with enemy level offset 0 and
  NY 100.
- When online gameplay diverges, each player's `phantomcrash.log` lists the
  diverged tick, both state digests, a digest of every player's controls for
  the 60 ticks before it and a named digest of each part of the match
  (players, world areas, battle, armory, each robot's fields, movement and
  pose) with readable positions, headings, health and kills. Comparing the
  two players' logs shows which part went apart.
- Online, both players should use 0.5.20.

0.5.19 changes:

- `phantomcrash.log` can be read by other programs while the game writes
  it. Before, the game held the file exclusively, so Phangrade's co-op log
  sharing read nothing from it until the game closed.

0.5.18 changes:

- The copy icon and F9: when Phangrade started the game, the clipboard copy
  ends with Phangrade's context for this run, which says where its full logs,
  crash dumps and co-op partners' logs are. The dump file stays plain JSON.
- Battle copies share the stage's collision and each wreck's collision
  instead of duplicating them. Online play copies the battle many times a
  tick, so each kill used to slow every tick and grow memory; a two-player
  RUMBLING match could fall from the high 90s to 15 fps.
- `--online-stress-check [seconds]` runs two peers on this computer over
  loopback with added delay and reports each peer's frame, simulation and
  drawing time against a 144 fps budget.
- Online, both players should use 0.5.18.

0.5.17 changes:

- RUMBLING areas have 10 opponents by default, up from 9 (11 places with
  your own). `rumble_robots` in settings, `--rumble-robots N` or
  `live_size` in `data/world/rumbling.json` still change it. Settings saved
  by an older version keep their count; set `rumble_robots` to 11 for the
  new default.
- Co-op players join in addition to an area's opponents instead of taking
  over one of them. A place a player leaves goes to the next to join, and
  once everyone has left the area returns to its own opponents.
- SYSTEM has QUIT, a port addition: it asks to confirm and goes back to the
  main menu. Play not saved is lost.
- ASSEMBLE: the right stick hides or shows the equipment the Scoobee cannot
  fit, kept with the save. LB and RB turn the list's page, and LT and RT
  go to its top and bottom.
- BUY CHIP: Y in the dialog turns the comparison with the fitted CHIP off
  and on.
- RESULT shows only the grade for a win or a leave, with no extra label, and
  the battle music keeps playing through the exit walk.
- Contest hits pay `hit_reward_ny`, as RUMBLING's do.
- The radial menus' titles are drawn in capitals, in a fixed box.
- Co-op menus have new art and keyboard controls: Enter joins a free bay,
  Esc goes back, and the address box types with the keyboard.
- `PHANTOMCRASH_BEAM_TRACE`, set to a file path, logs the player's beam
  steps and hits for diagnosing lasers.
- Both players need 0.5.17 online.

0.5.16 changes:

- Sun shadows come from one shadow map seen from the stage's sun, in place
  of the original's stencil shadow volumes and the robots' flat ground
  shadows. Buildings, fences and robots cast them onto the ground, each
  other and themselves, with soft edges that widen the higher the caster
  stands. Each stage keeps its original shadow darkness.
- `sun_shadows` in `data/world/dynamic_lighting.json` tunes the sun's size,
  the softest edge and the map; `"enabled": false` brings back the stencil
  shadows, which are also used if the shadow map cannot be created.
- Training replays keep one frame per game tick at high frame rates, and
  drop the sounds of the frames they skip.
- Both players need 0.5.16 online.

0.5.15 changes:

- Online, each player hears the battle's explosions, shots and impacts.
  The second player's sounds were muted while the first player was still
  in the match, leaving only menu blips and the prize sound.
- A player who quits online no longer leaves the others on "Connection
  interrupted. Waiting to resume...". Their game tells the others it is
  leaving, and a computer silent for 30 seconds is dropped. If they quit
  mid-match, their robot leaves the arena and the others play on. If the
  host quits, the other players see that the host left.
- Both players need 0.5.15.

0.5.14 changes:

- Online battles keep their frame rate: each destroyed robot's wreck was
  rebuilt every time the game corrected its prediction of the other
  player, thousands of times a second late in a match. Wrecks are now
  kept, so kills no longer slow the game down.
- Online, weapons stay on their robots instead of trailing behind them.
- Online, the other players' rider icons show on the town map and in the
  menus you share, as in split screen, and their status lines include
  their day.
- Both players need 0.5.14: the online presence message changed.

0.5.13 changes:

- Online co-op between two computers: installs converted into different
  folders now connect instead of being rejected as a different game build.
  Both players need 0.5.13; the disc image must still be the same
  European release.
- Online, the local player uses the keyboard, mouse and every controller,
  and rumble reaches every controller. A controller stopped responding
  once COOP opened.
- Online play fills the window; the other players' status lines draw over
  the top-left corner instead of a bar that pushed the game down.
- Hosts with fullwidth names (such as Ｓａｍ) show their names in the COOP
  list instead of question marks.
- Online inputs survive heavy packet loss, a stalled frame's ticks are
  caught up rather than dropped, resizing the window keeps the session
  running, and COOP can join a host by typing its address.
- RUMBLING: every damaging hit adds 10 NY to the attacker's prize, and the
  prize coin sound plays as it grows (`hit_reward_ny` and `prize_sound` in
  `data/world/rumbling.json`).
- BUY CHIP also sells the CHIPs of bosses beaten that day; the BUY and SELL
  dialog steps between neighbouring CHIPs and compares each with the one
  fitted.
- Opponents fire as the original: held triggers, sticky fire requests, a
  one-time first delay, and only lock-on weapons waiting for a lock.
- Robots stand on floor triangles however far they sank below them, so
  Shibuya's station stairs no longer swallow a robot.
- The World Map: left goes to the next place and right goes back, turned
  so left first heads to the neighbour further left on screen.
- Part and option tab captions draw at one size; the entrance jingle starts
  6 s before the entrance ends; turning the Hangar wheel plays its sound;
  shoulder weapons keep the body's aim pitch limit.
- The game writes its log to `phantomcrash.log` beside the settings.

0.5.12 changes:

- RUMBLING battles no longer drop to a few frames a second when many robots
  fight close together, above all on AQUA LINE: a shot now skips every
  vehicle, prop and wreck it does not pass near instead of testing their
  collision shapes.
- The areas other than the player's fight only while the RUMBLING area
  choice is open, so its map still updates; elsewhere, the player's battle
  included, they wait (`fighting_screens` in `data/world/rumbling.json`).
- Seven specialist CHIP families (Fox I, Bear I, Tortoise I, Boar I,
  Hawk I, Monkey I, Meerkat I), each best at one thing and weak at another.
  BUY CHIP sells one added cat and one specialist a day. The Rabbits become
  jump fighters; Cat VI is toned down; Monkey I, Meerkat I and Cat VII are
  rebalanced. Aim Speed now sets how fast the reticle follows a target (the
  original's speed at 1, 4.6 times faster at 10).
- The red carrier: shoot three supply carriers down before they drop
  anything and the next comes red, bigger and with the warning alarm,
  dropping nine boxes onto the helipad's H with better odds.
- The rescue box: a player's robot below a quarter of its armour when a
  carrier sets out is brought the 50% repair box first, landing at its
  feet. Ammo boxes refill the weapon with the least of its magazine left
  (`"ammo_to": "random"` keeps the original's pick).
- A regular battle's music waits for the entrance: silence while the robot
  walks out, the play-in jingle, then 3 s before the program or the
  battle's own track, with 3 s between the program's songs
  (`music_gap_seconds` in `data/world/training.json`).
- The town map turns as the original: left always steps clockwise and
  right anticlockwise, including after Sonic Amp.
- RESULT no longer shows the top bar's SAVE button.
- The leg tables in CUSTOMIZE and the module shops draw a curved arrow
  from WEIGHT down to CAPACITY in place of a second WEIGHT label, so
  upgraded values are easy to read.

0.5.11 changes:

- The supply carrier can drop three new money boxes: 250,000 NY (1%),
  500,000 NY (0.5%) and a 1,000,000 NY jackpot (0.25%) whose glow cycles
  through every colour and whose pickup plays the payout sting. The
  100,000 NY box turns violet, 250,000 orange, 500,000 and 1,000,000 gold
  with a metal sheen. The 1,000 NY box gives up the 1.75% they take.
- Box kinds are a list in `data/world/training.json` (`battle.supply.kinds`):
  each sets its chance, effect and amount, and may set its own picture,
  body and lettering colours (`#rrggbb`), glow, sheen and pickup sounds.
  New crate pictures are in `data/assets/textures/supply/`; see
  `docs/SUPPLY_BOXES.md`. Computer robots treat every money box alike.
- Left and right turn a part's details to a second page in ASSEMBLE and the
  module shops: a weapon's type and hidden values (fire rate, range, shot
  speed, spread, homing, blast, stun, knockback, knockdown), a module's or
  CHIP's mortal shot and resistances, a leg's knockback resistance
  (`data/ui/weapon_types.json`, `part_stats.json`).
- BUY CHIP and UPLIFT show a CHIP's fire control as rows in a taller panel:
  aiming and lock-on range tinted as in the preview, mortal shot, repulsion
  shield, lock-on time, aim speed and knockdown resistance.
- SV SPEC pages between the vehicle's and the weapons' specs.
- NAME ENTRY takes the keyboard: typing a character jumps to its key and
  types it; Backspace deletes and Enter ends the entry.
- The mouse works in ASSEMBLE and in LOAD and SAVE (slots, EXIT, OK, YES
  and NO). A SAVE button on the top bar opens SAVE. SKIP TO DAY is offered
  from every place. The town map's left and right follow the screen.
- Moving the mouse back reverses a turn at once. Laser reduction takes its
  share off a beam truncated, as the original does; a shoulder weapon
  without lock sights (a grenade launcher) fires along its mount; legs
  without root motion move at their table speed.

0.5.10 changes:

- Three new CHIPs join the original's 96: Cat V (Norwegian Forest Cat),
  Cat VI (Maine Coon) and Cat VII (Ragdoll), each with the five cat
  personalities' chatter, a green monitor-look face like the original
  animals and stats of its own. BUY CHIP offers one of them every day
  beside the usual stock.
- They are base game data (`data/tables/added_chips.json`, faces under
  `data/assets/textures/chips/`, help text in `data/ui/chip_shop.json`),
  joined to the converted tables as they load; no re-install of the game
  data is needed. S.L.A.I. mode is unchanged.

0.5.9 changes:

- The lettering still baked into menu art is drawn as sharp text: the
  garage, Club Wired, Plus Tech and Wild Machines titles, SYSTEM, KEEP SV
  (pills, pop-ups, damage and empty tags), BATTLE MUSIC's programs and
  prompts, ASSEMBLE and CUSTOMIZE (parameter tables, SV-NAME / CHIP /
  COLOR / STICKER, Equipment Imperfection, Underkill, Overweight), the
  dealers' greetings and the chip, module and used-SV shops (including
  Square / Oblong / V-Oblong), RUMBLING, SCREEN ADJUST, the hangar's
  slanted numbers and VS MODE's entry, Scoobee and area screens.
- VS battles draw YOU WIN / YOU LOSE / DRAW and the RETRY / SELECT AREA /
  SELECT SV / EXIT menu sharp, and each rider's 1P-4P tag shows over its
  robot.
- Ruled lines in tables are drawn as vector lines
  (`data/ui/sprite_lines.json`). Sharp-UI entries share settings through
  named classes, so a look is changed in one place.
- The schedule opened from the garage offers SKIP TO DAY beside EXIT: it
  sleeps through to the chosen day after a YES/NO question
  (`data/ui/schedule.json` `skip`).
- BUTTON CONFIG rows can be picked with the mouse.

0.5.8 changes:

- Menu lettering baked into the art (labels, headings, warnings) is drawn as
  sharp text at the window's resolution, and the records and BATTLE MUSIC
  frames and cursors as vector outlines (`data/ui/sprite_text.json`,
  `data/ui/sprite_shapes.json`). Both follow the sharp text setting.
- BUY, SELL and STOCK MODULE show MORTAL SHOT, KNOCKDOWN RESIST and the legs'
  KNOCKBACK RESIST. A leg's taller window fits above the CURRENT MUSIC plate,
  and MODULE PARAMETER no longer overlaps the LEVEL row.
- BATTLE MUSIC opens with the original's tray, disc fan, pulsing cursor and
  STOCK heading; empty program slots are drawn.
- RECORDS shows the venue logos and the PERSONAL BEST watermark. Hangars 11
  and 12 have their own plates.
- A boss takes the camera for its entrance the first time in a battle, and a
  boss shot down holds the battle under letterbox bars while the camera
  watches it. Area Rankers speak their defeat lines and their contests play
  their own themes.
- Rapid-fire guns fire at the original's 30 Hz battle cadence; beams recharge
  as the shop states. Only a lock sight's countdown completes a lock, and a
  strafe alone plays no footsteps.
- The night video ship's searchlights follow their beams, the Aqualine night
  gate lights read as a glow, reflective stages show their reflection, and
  grated stairs and fences no longer hide what is behind their gaps.

Converted assets: chatter gains the Area Rankers' defeat lines (version 4).
The game converts the assets again on first start; the disc is not needed.

0.5.7 brings root-motion movement with momentum on stopping, faster NPC
entrances, wreck and prize rules, the RUMBLING roster in the pause menu and
the HM10 barrel fix.

0.5.6 adds per-game volume and window persistence for Phangrade launches.

0.5.5 fixes:

- Enabled the original RUMBLING entrance music cue previously suppressed by
  inherited tutorial settings.
- Restored the MISSILE ALERT border, durability/O.C. frame outlines and the
  multiplication sign in the SHOOTDOWN counter.
- Added ordinary damage notifications to the scrolling HUD ticker and made
  online HUD notifications follow confirmed battle events.
- Added skull markers for credited defeated robot identities when they return
  during the battle.
- Made durability warning text heavier and centered within its striped panel.

0.5.4 fixes:

- Corrected left/right stick rotation of the town map rider while preserving
  the corrected up/down controls.

0.5.3 fixes:

- Optical camouflage uses the equipped modules' active and recharge timings.
  The O.C. gauge flashes during recovery rather than while discharging.
- Dodge animations lean towards the dodge direction.
- Occupied save slots show the saved robot's equipped CHIP portrait.
- Pepper's training dialogue extends the blue upper-left edge correctly.
- The town map keeps its upper-right border black and corrects vertical rider
  rotation on the board.


0.5.2 adds:

- **Automatic LAN discovery in COOP.** Choose Host network game on one PC, then
  select its automatically listed name on another. Discovery searches each
  active IPv4 network, including alongside VPN adapters. Local split-screen
  remains available, with separate network and local saves.
- **S.L.A.I. content toggle.** Settings > S.L.A.I. content is off by default.
  First enable requires your European S.L.A.I. PS2 ISO (SLES-52940). A bundled
  installer converts the assets into `artifacts/slai/assets`; no Python install
  is required. The ISO is only needed for installation. Later toggles reuse the
  installed assets and take effect after restarting the game.
- **Weapon fixes.** Homing locks drop when a target leaves the chip's lock box;
  non-homing shoulder weapons follow the reticle instead of an off-screen lock.
  Firing displays muzzle flashes from the existing converted models.
- **Audio cues.** The pause menu plays its opening cue while gameplay audio is
  paused, and acquiring a new homing target plays the acquisition cue.
- **Garage and shop updates.** Twelve hangar slots, S.L.A.I. starter robots in
  the Fill Hangars cheat when enabled, and label/layout fixes.

S.L.A.I. and the base game have separate campaign/profile files. A new S.L.A.I.
campaign starts fresh; switching off returns to the base-game campaign. Original
settings and saves are retained. If an older, manually assembled S.L.A.I. mod is
found in `mods`, that legacy overlay is retained on disk but no longer loaded
automatically; new base and S.L.A.I. profiles are kept separately from its saves.
Both peers must enable the same content when playing online.

0.5.1 fixes:

- Selected main-menu and Quest-submenu buttons pulse yellow with dark text,
  using the options menu's selection timing.
- New Game plays the missing second cinematic, `opening02.sfd`, before the
  opening conversation. Each cinematic can be skipped separately.
- The CURRENT MUSIC meter animates during dialogue scenes.

0.5.0 adds:

- **Experimental online co-op.** Up to three players share deterministic combat
  with rollback prediction. Menus send player presence, and active matches support
  late joining and recovery from a network outage while the games remain open.
  Rollback does not replay confirmed sounds/effects or award results twice.
- **Matching-content checks.** Peers must use the same executable, gameplay data,
  extracted resources and mods, including external model buffers.
- **S.L.A.I. import support.** Native support for the optional parts, stages,
  animation, audio and effects overlay built from your own S.L.A.I. disc. Imported
  content is not included in this ZIP; see `docs/SLAI_INTEGRATION.md`.
- AI/navigation, robot motion and rendering updates from the current source,
  bounded shop labels, and an executable icon.

Online play is a prerelease feature. Local desktop gameplay passed 35–65 ms
delay each way, 3% packet loss and a thirteen-second blackout; different-machine
and Internet testing remains outstanding. Joining pauses the match while its
history is reconstructed, and confirmed sounds/effects can be delayed by latency.
Reconnection does not restore a closed game or migrate the host.

### Starting online play

Install the same release and matching content on each machine. In version 0.5.2,
open COOP, choose **Host network game** on one PC, then select its automatically
listed name on the other PC. Allow the game through the private-network firewall:
UDP 29642 for discovery and UDP 29641 for the hosted game. Both PCs must be on the
same broadcast network. Games running in solo mode are not advertised.

For a manually addressed connection, from the extracted release directory the host runs:

```powershell
.\phantomcrash_port.exe --root . --online-host 29641
```

Other players replace `HOST_IPV4` with the host's reachable address:

```powershell
.\phantomcrash_port.exe --root . --online-join HOST_IPV4:29641
```

Allow UDP 29641 to the host; Internet play needs a reachable route or port
forwarding. Each player enters the same arena through the normal menus. Keep
the games open when testing a brief network outage. More implementation and
test details are in `docs/ONLINE_MULTIPLAYER_DETERMINISM.md`.

0.4.1 adds:

- **Smoother frames.** Removed the stutters seen in busy matches: replay
  recording compresses and saves frames on a separate thread; the robots
  that may enter a Rumbling match are loaded when it starts instead of when
  they arrive; sharp text no longer stalls the frame its first text appears
  in; and a computer robot rejoining its route no longer runs over a hundred
  line-of-sight tests in one tick. In a ten-robot match the worst frames
  fell from 27�76 ms to about 8�10 ms.

Existing users: copy your `artifacts` and `mods` folders into the new
version. The converted assets are unchanged.

0.4.0 added:

- **Dynamic lighting at night.** Ship and carrier searchlights sweep the
  arena and cast shadows; entrance lamps replace the painted light pools;
  fires, blasts, muzzle flashes, projectiles and beams light their
  surroundings, and the strongest fires cast shadows.
- **Light beams in the air.** On night stages the searchlights and lamps
  show as beams through the haze, and fires glow in the air around them
  (Settings > Visuals, "Light beams in the air"; needs Dynamic lighting).
- **Smarter opponents.** The AI plans routes over the arena with jumps and
  drops, frees itself when stuck and seeks high ground; ten-robot matches no
  longer drop frames.
- **Pause menu** with RESUME and EXIT (EXIT asks to confirm).
- **Night rental match** in Level Select.
- **Sharp text** option, drawing the menus' text with the MS Gothic font
  installed with Windows (the font is not included).
- Robots no longer stick in a jump on stepped ledges, kerbs and wreck rims.
- The entry shield rises and falls at the original's speed; talk scenes no
  longer show the HUD; menu cursor sounds match the original.
- Optic camouflage hides the robot's shadow; the arm-loss crackle plays
  once; shot marks fit the surfaces they hit and no longer cover fires.

Existing users: copy your `artifacts` and `mods` folders into the new
version. The converted assets are unchanged.

0.3.0 added:

- **No prerequisites.** The Python extraction tools are replaced by
  `phantomcrash_extract.exe`, and the Visual C++ runtime DLLs ship beside the
  EXE. On first start the game asks for the disc image and installs the game
  data in about ten seconds; `Install Game Data.bat` does the same from a
  dropped `.iso`.
- **Updates keep your data.** A new version converts the assets again from
  the files already extracted when its converter changed, without the disc;
  installs are staged so an interrupted one leaves the old data in place.
  See "Updating to a new version" above.
- **Faster installation.** Extraction takes about 5 s instead of 27 s and
  conversion about 3 s, with parallel conversion, hardware SHA-256 and
  libdeflate PNG compression.
- The window title shows the release's version.

Existing users: copy your `artifacts` and `mods` folders into the new
version; the game converts the assets again on its first start. Python and
the separate Visual C++ runtime install are no longer needed.

0.2.0 added:

- **Opponent AI closer to the original.** Computer opponents take a
  computer rival only once they have locked it, turn on whoever just hurt
  them (leaders, champions and rankers always do), jump at a target that
  slips beside them and keep aiming after landing, and the attract demo's
  and RESULT's opponents fight at full skill. A provoked ranker fights in
  the common way for 8 s before choosing its own tactic again. Changes from
  the original are opt-in under `ai` in `data/world/rumbling.json`; see
  `docs/ORIGINAL_ROBOT_AI.md`.
- **Attract demo.** Left idle for 30 s, the title plays a RUMBLING battle
  for a minute, then the intro again; any button returns to the title.
- **Battle and RESULT cameras.** RESULT's backdrop watches a robot through
  the stage's fixed cameras, as the original does; the behind view follows
  the original's chase camera.
- **Cheats** (Tools > Cheats, all off by default): infinite money, armour
  and ammo, open contests, fill the empty hangars with one robot of each
  body model, add one of each part, CHIP and module, and go to any day.
  See `docs/CHEATS.md`.
- **Movement and animation.** Leg-record walk/dash speeds and turn rates,
  wheels that roll, strafing for every leg type, dash-turn leans and
  layered motion blends, and footsteps that follow the feet.
- **Fixes.** Robots no longer start under the Shinjuku street, light
  volume shadows have the original's length, and mixed-maker parts hang
  from the body's mounts when the `same_maker` rule is modded off.

## Licenses and library sources

The port and the game data installer are MIT licensed (`LICENSE`). Bundled
third-party notices are in `licenses`. FFmpeg is dynamically linked under
LGPL 2.1 or later; its DLLs can be replaced with compatible rebuilt versions.
The release's companion `third-party-sources.zip` supplies the upstream
source archives, vcpkg patches/build recipes and FFmpeg configuration used
for these libraries. It is not needed to play.
The Microsoft Visual C++ runtime DLLs (`msvcp140*.dll`, `vcruntime140*.dll`)
are Microsoft's redistributable files, shipped under the Visual Studio
license terms (`licenses/microsoft-visual-cpp-runtime.txt`).

## Reproducing a package

From the matching source tag, run `scripts/build-port.ps1`, then
build `phantomcrash_slai_audio` and the bundled importer:

```powershell
python -m venv .tools/slai-builder
.tools/slai-builder/Scripts/python.exe -m pip install -r tools/slai-installer-requirements.txt
.tools/slai-builder/Scripts/python.exe tools/build_slai_installer.py
```

Run `python tools/package_release.py` (`--output DIR` writes elsewhere). Packaging
uses `build/port`, the Visual Studio C++ Redist for the runtime DLLs the
binaries import, and the pinned `.tools/vcpkg` sources, and writes the two
ZIPs plus SHA-256 checksums to `build/releases`. Only the port, data, tooling
and library files on its allowlist are included; extracted game content is
excluded.
