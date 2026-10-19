# Phantom Crash — Original Xbox modern port

Research tooling and a clean-room modern C++ port of *Phantom Crash* for a
user-supplied Original Xbox disc image. This repository does **not** contain
the game, its executable, extracted assets, its text, or any Xbox firmware.

This is an unofficial fan project. It is not affiliated with, endorsed or
sponsored by Genki, Phantagram or any other rights holder of *Phantom Crash*,
nor by Microsoft. *Phantom Crash* and the names of its companies, characters
and robots belong to their owners; Xbox is a trademark of Microsoft.

The workflow follows the Record of Lodoss War project: extract and study the
user's disc, understand behaviour with Ghidra and the
[xemu_decomp](https://github.com/GeneralAtrox/xemu_decomp) emulator fork,
then reimplement it as a native SDL3 + Direct3D 11 program with modern
resolution, widescreen and frame-rate support. Menus and movies stay 4:3;
the in-game 3D goes widescreen, with a choice of 4:3 or widescreen HUD
positions (`docs\WIDESCREEN.md`).

## Workspace

```text
C:\Projects\Xemu\
├── Games\            # user-supplied disc images (outside every repository)
├── PhantomCrash\     # this repository
└── xemu_decomp\      # emulator fork used for runtime research
```

Inside this repository, `artifacts\`, `research-output\`, `build\` and
`.tools\` are generated and ignored by Git.

## Requirements

For a prebuilt Windows executable, download the Windows x64 ZIP from
[Releases](https://github.com/GeneralAtrox/PhantomCrash_Xbox_ModernSourcePort/releases).
See [Windows release setup](docs/WINDOWS_RELEASE.md) for runtime prerequisites
and extracting your own game disc. The requirements below are for source builds.

Version 0.5.2 discovers LAN games from **COOP**. One player selects
**Host network game**; other players open **COOP** and select that host from the
automatically refreshed list. **Local split-screen** remains separate. Both PCs
need matching builds/content and must allow the game through their private-network
firewall (UDP 29642 for discovery, UDP 29641 for menu-hosted games). A game merely
running in solo mode is not a joinable host. Older releases use the command-line
setup in the Windows release instructions.

**Settings > S.L.A.I. content** enables the optional content pack. It is off by
default and requires your European S.L.A.I. PS2 ISO (SLES-52940) for installation
only. The release includes a standalone converter; no Python installation is
needed. Restart after changing the toggle. Base-game and S.L.A.I. campaigns use
separate save files; legacy manually imported content and saves are retained.

- Python 3.11+ (no third-party runtime dependencies; `pytest` for tests)
- PowerShell 7.4+
- Visual Studio 2022 with the C++ workload, CMake 3.25+
- For static analysis: Ghidra 12.1.2 and Java 21 (see
  [the Ghidra tooling notes](docs/GHIDRA_TOOLING.md))

## Extract the disc

```powershell
.\scripts\extract-game.ps1      # phantomcrash_extract.exe install
# or, step by step (build\port\port\Release\phantomcrash_extract.exe)
phantomcrash_extract.exe extract
phantomcrash_extract.exe list
phantomcrash_extract.exe xbe
phantomcrash_extract.exe convert
phantomcrash_extract.exe status
```

The C++ extractor writes the same files as the Python research tools
(`python .\tools\phantomcrash.py`, which keeps the same commands); its PNGs
are compressed differently but decode to the same pixels. `install` extracts
and converts only what is missing or out of date (see
[Windows release setup](docs/WINDOWS_RELEASE.md#updating-to-a-new-version)),
and the port runs it on startup when the assets need converting again.

The default disc is `Phantom Crash (Europe).xiso.iso` in the repository root; pass `--disc`
for another image. Both trimmed `.xiso` images and full Redump-style images are
supported. Output goes to `artifacts\europe`:

- `files\`: every file on the disc;
- `metadata\disc.json`: image hash, partition offset, and per-file sector,
  size and SHA-256;
- `metadata\xbe.json`: the parsed `default.xbe` header, certificate, SDK
  library versions and section table.

Extraction refuses a non-empty output directory so runs never mix.

`convert` turns the extracted files into formats the port reads, in
`artifacts\europe\assets` (a few seconds; the script runs it after
extraction):

- `textures\<folder>\<pack>\<n>.png`: every Xbox texture pack, unswizzled;
- `sound\sfx\<bank>\<nn>.wav`: every sound-effect bank entry as 16-bit PCM;
- `tables\*.json`: the resource, sprite, scene-music and movie tables read
  from `default.xbe`, and `tables\text.json`, the game text the data files
  reference (below).
- `models\model\<name>.gltf`: the standalone models of `Media\model`
  (stages, the garage, the town) as glTF 2.0, textured from the converted
  PNGs ([the training ground notes](docs/TRAINING_GROUND.md)).

Movies (`.sfd`) and music (`.wav`) are already open formats and stay in
`files\Media`. `convert` also refuses a non-empty output; pass `--replace` to
redo a previous conversion.

## Data and mods

The port's behaviour lives in editable JSON under `data\` (intro sequence,
title, main menu, music and effect names). Every file path in it resolves
against `mods\`, then `artifacts\europe\assets`, then
`artifacts\europe\files\Media`, so a file placed in `mods\` under the same
relative path replaces the original.

The game's own words (help lines, part and CHIP descriptions, shop dialogue)
are not in the repository. Where the data shows one of them, it holds a
reference such as `{xbe:0x19b848}`: the Shift-JIS string at that address in
the player's `default.xbe`. `convert` reads every string that `data\` and
`mods\data\` reference into `tables\text.json`, and the port puts them back
as it loads each data file. A reference can sit inside other text
(`"Guard — {xbe:0x188988}"`), and plain text works anywhere a reference does.
After adding new references, run `phantomcrash_extract install --convert` so
the table holds them; a replacement `mods\tables\text.json` translates them.

## Static analysis

The Ghidra scripts are in the shared `C:\Projects\Xbox\xbox-re-tools`
repository:

```powershell
$tools = 'C:\Projects\Xbox\xbox-re-tools\scripts'
pwsh -NoProfile -File $tools\setup-ghidra-tools.ps1
pwsh -NoProfile -File $tools\build-ghidra-xbe.ps1 -Install
pwsh -NoProfile -File $tools\onboard-xbe.ps1 -Executable .\artifacts\europe\files\default.xbe `
  -ProjectDirectory .\research-output\ghidra -ProjectName phantomcrash-europe
```

See [the Ghidra tooling notes](docs/GHIDRA_TOOLING.md) and
[the European baseline](docs/EUROPE_BASELINE.md).

## Build the port

```powershell
pwsh -NoProfile -File .\scripts\build-port.ps1 -Test
.\launch.bat
```

`launch.bat` works like the Lodoss `Start Game.bat`. It rebuilds anything
out of date, puts the vcpkg DLL folders on PATH, and runs the port from the
project folder with `--root`, passing any extra arguments through. It pauses to
show the error if the build fails or the game exits with an error. Run
`build-port.ps1` once first to configure the build.
Both build paths resolve the installed PowerShell executable and exclude the
WindowsApps execution-alias directory from the build process PATH, so vcpkg
does not mistake the Store alias for a readable executable. Windows PATH
settings are left unchanged.

**Settings > Visuals...** opens a panel that stays open while you compare changes
in the running game. It contains fullscreen, VSync, frame-rate cap/counter,
**Dynamic lighting and shadows** (with **Light beams in the air**, the lights
seen as shafts on night stages), **SMAA anti-aliasing**, **Depth blur**,
**Shinjuku distortion**, **Stage colour tint** and **Original softness**, which
gives the 3D view the softness of the original's 640x480 picture while still
rendering at native resolution. The port's additions default off;
selections apply immediately, save without checkpointing the match, and reload
on the next launch. The game keeps rendering while the panel has focus.
Dynamic night lighting adds shadowed entrance lights and moving video-ship
spotlights. Its editable sources are `data/world/dynamic_lighting.json`.
See [dynamic lighting](docs/DYNAMIC_LIGHTING.md) and
[post-processing](docs/POST_PROCESSING.md) for details.

`Start Game Quick.bat` uses the same launcher and opens the garage with your
saved quest, skipping the intro and title. It uses the normal settings/save
file; append `--settings <file.json>` to use a different one.
For a rental match, choose **Tools > Level Select > Class D: Fire Balloons
(rental SV)**. This testing shortcut restarts Day 1's Shinjuku contest and its
opponents with a rental SV, keeping your pilot, money (less the normal entry
fee, zero for this contest), and garage. **Class D: Night Electric (rental
SV)** does the same with Day 4's Shinjuku night contest. The current quest
still saves normally on exit. The entries, their destinations and rentals are
editable in `battle.quick_rentals` in `data/world/rumbling.json`; `--start
rumbling_d_rental` and `--start rumbling_d_night_rental` also open them
directly.

**Tools > Cheats** holds infinite money, armour and ammo that never reach 0,
and contests without ranker or regulation gates (switches, saved with the
settings, all off by default), plus one-off changes to the quest: fill the
empty hangars with one robot of each body model, add one of each distinct
part, CHIP and module and every paint, emblem and music track, and go to any day (the
menu marks the First and Area Ranker days). See [cheats](docs/CHEATS.md).

The port finds `data\` by searching upward from the working directory and the
executable, or takes `--root <folder>`. Other options: `--fullscreen`,
`--windowed`, `--size <width>x<height>`, `--start <screen>` to begin on
`title`, `main_menu`, `option`, `sound_config`, `button_config`,
`screen_adjust`, `new_game`, `name_entry`, a talk scene (`event_48`) or a
placeholder instead of the intro, `--settings <file.json>` to read (and
save) that settings file instead of the one in your app data folder,
`--rumble-robots <0-16>` for this run's live RUMBLING robots an area (the
settings file's `rumble_robots`, 10 by default; 0 keeps the original's
roster sizes), and
`--smoke-test [frames] [--capture <file.bmp>] [--press <frame>[-<last>]:<button>]`
`[--mouse <frame>:<x>,<y>[:click]]`
for a hidden, bounded run that can press (or, with a range, hold) buttons,
move the mouse to a point in the 640x480 space (and click there) and save
its last frame. A smoke run steps one 60 Hz logic tick per frame, so
`--press` and `--mouse` frames are game ticks. `--benchmark [seconds]`
(20 by default) runs visibly without vsync or the frame cap, and prints
the frame times once done; its `--press` frames are game ticks too
(docs/TRAINING_GROUND.md, Performance).
`--render-robot <record> <file.png> [--robot-view <yaw>,<pitch>,<distance>,<height>[,<size>]]`
stands one garage robot record alone on a transparent background and saves
a square PNG, then exits; the camera orbits the robot (degrees, yaw 0 facing
it, pitch above 0 looking down) at `height` above the ground. The executable
icon (`port/res`) is the Plain Holy (record 0) from
`--robot-view -35,-10,8,1.9,2048`, laid out by `tools/make_icon.py`.

The port boots into the intro from `data\boot\intro.json`: Phantagram,
Genki, the Sofdec/ADX screen and Dolby (first boot only), then an attract
movie. Any button skips the current step; one tap skips one step. Movies are
read from the extracted `Media\movie` folder, or from `mods\` first.

After the intro comes the title screen (press START) and the main menu, with
the title music and menu sounds. QUEST MODE opens NEW GAME / LOAD GAME. Left
idle for 30 s, the title plays the attract battle demo
(`data\ui\attract_demo.json`): a RUMBLING contest's robots fight for a
minute, then the intro plays again; any button goes back to the title.

NEW GAME stops the music and plays the opening movie (any button skips it;
`data\story\new_game.json`). Then comes the first conversation, Salsa and
Pepper at Club Wired (story event 48): the text types with its tick and
Pepper blinks and talks. A or START shows the whole page, then turns it;
holding L or R (Q/E) fast-forwards. The scripts are the converted
`tables\events.json` and `tables\talk.json`, the timings
`data\story\talk.json`. Then NAME ENTRY opens:

- the D-pad moves over the Alphabet and Symbols grids and the tab row;
- A types, X deletes, Y adds a space, L/R change page and B jumps to End;
- or type on the keyboard: each key types its full-width character and the
  cursor jumps to it (`keyboard.aliases` in `data\ui\name_entry.json` maps
  keys the grids lack, such as `-` to the long bar), Backspace deletes and
  Enter ends the entry. The arrows and Esc still move and jump to End.

Ending with no name shows the original's "No input." notice. Otherwise the
name is saved with the settings, event 49 plays, and Pepper's guide opens:
a BEGINNERS CLUB list of questions (up/down scroll round, A opens a topic
or plays Pepper's answer and comes back, B closes a topic or jumps to End).
End leads on (event 50, then a town map placeholder, then Club Wired). Club
Wired's wheel turns with left/right; A opens the item (Training is
Pepper's guide again, Change alias NAME ENTRY), B jumps to Exit. These town
screens, the talk scenes and NAME ENTRY share the town frame: the day, the
money, the CURRENT MUSIC meter and the credit of the radio song, which
Club Wired deals from the game's songs; the meter's seven bars follow the
music heard, bass to treble, each against its own band's recent level so
all of them move (`data\ui\frame.json`,
`data\audio\radio.json`, `data\ui\club_wired.json`,
`data\ui\pepper_guide.json`). With a Scoobee ridden (`"riding"` in the
save's quest) the RIDING SV plate slides in on Club Wired and Pepper's
guide; once the game is cleared (`"cleared"`), two of Pepper's answers
change, as in the original. A new game has neither. The staff roll
(`data\story\staff_roll.json`, `--start staff_roll`) marks the game cleared
as it starts: the staff and epilogue movies, then THE END looping for 30
seconds (A or START ends it after two seconds and goes to sleep; left to
run out, the Kudryavka and cloud movies play first). A first win over the
First Ranker leads there through the finale: an epilogue on Umihotaru by
day, then the ending movie (`docs\FINAL_BATTLE.md`, `--start final_battle`,
`--start ending`). To start from a cleared game, copy
`port\tests\cleared_save.json` and pass the copy to `--settings` (the game
saves back into that file). Each event plays once per game;
the save remembers which have played. The
screen is `data\ui\name_entry.json`. LOAD GAME (`data\ui\load_game.json`)
shows the three save slots with the date and time each was saved and the
chosen one's name, money, Scoobee and day; with no save, only the "No Game
Records" notice. Choosing a slot loads its game, shows the original's
"Finished loading" notice and leads on to the garage. The garage's SYSTEM
menu has SAVE (`data\ui\save_game.json`, the same screen): an empty slot
is written at once, a used one after asking. The slots are kept with the
settings as `save_slots`. The port also keeps the game being played with
the settings when it closes; until something is saved, LOAD GAME shows
that game in the first slot.
The garage (`data\ui\garage.json`) is Club Wired's wheel menu with the
garage's picture and eight items: Hangar, battle music, modules, records,
schedule, sleep, system and out to the town map; X jumps to the hangar and
Y to sleep. Sleep, the schedule, the records, the system menu and battle
music are ported (`data\ui\sleep.json`, `schedule.json`, `records.json`,
`system.json`, `battle_music.json`; the schedule and records go back to
wherever they were opened from). The hangar (`data\ui\hangar.json`,
`--start hangar`) shows the ten hangars and EXIT as cards on a wheel:
each card says whether the hangar is for sale, empty or ridden, its
maker's emblem, its Scoobee's name and what keeps it from a battle
(Equipment Imperfection, Underkill, Overweight). A hangar for sale is
bought for 10,000 NY; an owned one's menu boards its Scoobee, test-drives
the ridden one, shows its specs or assembles it. ASSEMBLE
(`data\ui\assemble.json`, `--start assemble`) fits the stock into the
hangar: a grid of body, legs, arm and back weapons, CHIP, options, paint
and sticker, with NAME, DIS (take everything off), SPEC and EXIT. ARM and
BACK first ask for the right or left side. The list greys out what cannot
be fitted: another hangar holds it, it is of another maker than the robot
(`same_maker`), a module needs more socket holes than are free, or a
weapon sits on the other side. With `same_maker` off (a port mod), another
maker's legs take the body's own body bones, and another maker's arms and
back weapons hang from the body's maker's mounts (`robot_preview.json`'s
maker tables). Gold is fitted here. A line's details show
its own stats and the whole robot's before and after, with arrows, and
confirm fits it or takes it off. Left and right turn a part with extra
stats to a second page (a port addition): a weapon's type and hidden
stats, a module's or CHIP's mortal shot and resistances, a leg's knockback
resistance. A body with fewer socket holes takes the
modules off. NAME names the hangar's Scoobee in NAME ENTRY. SV SPEC
(`data\ui\sv_spec.json`, `--start sv_spec`) shows the SV chosen where it
was opened from (a hangar, ASSEMBLE, a dealer's or the used SVs' offer):
its name, its CHIP's name and the maker's badge, then on one page the
durability, run and max speed, jump height, O.C time, weight, capacity
and socket holes, and on the other its four weapons. Left and right turn
the page. The SV stands beside the window in 3D, or shows its parts one by
one when it has no body or legs (`data\ui\robot_preview.json`). TEST DRIVE
(`data\world\test_drive.json`) takes the ridden Scoobee onto the
training ground's FreePlay: its body, legs and CHIP at their levels, its
paint, and each fitted weapon with its record's stats. BACK returns to the
hangar. The contest's own opponents wait for the battle roster. MODULES
(`data\ui\stock_module.json`, `--start modules`) is STOCK MODULE: the
parts shop's category grid and lists over the player's own parts, CHIPs,
modules, paints and stickers, with the hangar each is fitted in and its
stats, trading nothing. The body, legs, arm or shoulder under the cursor is
shown in 3D in the picture window. Start the
garage with `--start garage`.
RUMBLING (`data\ui\rumbling.json`, `--start rumbling`) is how Club
Wired enters the day's contests: left and right choose Shinjuku, Shibuya,
Aqualine or EXIT, each with its venue's picture by day or night (CLOSED
when nothing is held), the contest, its class, the play fee and CLEAR once
won. A opens the area's menu over its map: INSIDE RUMBLING enters with the
ridden Scoobee, SV RENTAL with one of the rental SVs, SELECT SV opens the
hangar and comes back to this menu, INFORMATION shows the contest's
regulations and fee (the schedule's popup draws the same details), and
EXIT goes back to the areas. Entering checks the regulation (the fee, a
ridden Scoobee with a body, legs and CHIP, the class's maker, leg types and
its no-overclock or rentals-only rule), then asks for one of the stage's
gates and pays the fee. An Area Ranker's contest opens once its area's
class leaders are beaten and the First Ranker's after three Area Rankers,
and the ranker holds one gate. The rules are `data\world\rumbling.json`.
Each day draws every area's opponents from its contest's teams (members
until a team is beaten, then its leader; the extra team past the contest's
reserve; the champion once all are beaten), saved with the game.
WIRE HEADS DATA steps through them with left and right: pilot and chip
faces, alias, SV and chip names, team and prize money (the ranker alone in
a ranker's contest).
The RUMBLING is live, a port addition (`data\world\rumbling.json` "live"):
while the RUMBLING area choice is open (`fighting_screens`), each area's
opponents fight one another on its stage off screen, 30 times a second,
so its map updates; elsewhere they wait, though the downed are still
replaced (from the first screen of a game, not the intro, title or menus),
with the drones' simple AI (face the nearest other robot within range,
keep a distance, fire what is loaded). One shot down (or out of rounds)
leaves, and after `arrival_seconds` the next is drawn from those not out.
An area holds `rumble_robots` robots, the player's place among them (so
nine opponents by default). The area map shows each one where it stands by
its maker and legs type, turned the way it faces, and WIRE HEADS DATA
lights the shown one with a ring and gives its armour as it stands.
Entering an area joins its battle as it is, on the area's stage by day or
night; the player walks out of the gate among them. Those the player
shoots down are beaten and their prize money paid on leaving. Positions,
armour, rounds and shields are saved with the rosters, and a loaded game
takes them up where they stood.
KEEP SV (`data\ui\keep_sv.json`, `--start keep_sv`), from the system
menu, keeps copies of the hangars' Scoobees for the VS mode. Its cards show
each kept Scoobee's maker (or damaged, when its body is not in the
tables); left and right move along them, the row scrolling past the fifth,
and the chosen one's name and saving date show below. A kept Scoobee's
menu opens SV SPEC on it or deletes it after asking. SAVE opens the hangar
with its own EXIT card and a KEEP SV, SV SPEC and CANCEL menu
(`origins` in `hangar.json`); it takes only an armed, complete Scoobee,
and KEEP comes back and saves it, asking before it overwrites one of the
same name. The kept Scoobees are stored in the settings file as
`vs_scoobees`, as the original stored them on the memory unit.
The town map (`data\ui\town_map.json`) links the places round the garage, Club
Wired, Wild Machines, PlusTech, Sonic Amp and the three dealers: as the map
opens, left is set to first go to the neighbour further left on screen and
right to the other, and they keep counting that way until it closes (from
the garage left counts up; out of Sonic Amp it counts down, to PlusTech), the chosen place's sign pops up and
the rider rides to it, and confirm goes there. The map starts on the place it was opened
from. Sonic Amp (`data\ui\sonic_amp.json`) sells and previews tracks. The
three SV dealers (`data\ui\dealer.json`; `--start dealer_ventuno`,
`dealer_kojima` or `dealer_american_stars`) offer the day's three SVs from
the calendar: the company profile, each SV's name plate and price, and
BUY, which fits the SV into an empty hangar of the player's, asks whether
to board it and has NAME ENTRY name its CHIP. The hangars and the parts
stock they fill are saved with the quest (`"garage"`); the SVs, lineups,
modules, paints and emblems are in the converted `tables\garage.json`.
Wild Machines and PlusTech open their top menus (`data\ui\wild_machines.json`,
`plustech.json`). Wild Machines sells used SVs (`used_sv.json`), CHIPs
(`chip_shop.json`, BUY CHIP and SELL CHIP) and parts (`module_shop.json`,
BUY MODULE and SELL MODULE): a grid of categories (bodies, legs, arm and
backpack weapons, options, colouring patterns, stickers), the day's goods
or the player's in a list, and the chosen one's stats (the lines by kind
are in `part_stats.json`), bought into the stock or sold for half their
price. A weapon's panel adds its type (`weapon_types.json`, by record) and,
on a second page that left, right or a click turns to, the battle's
values the original never shows: fire rate, range, shot speed, spread,
homing, blast, stun, knockback and knockdown. The shops share the notice box, the stats panel and the windows
popping open. PlusTech's UPLIFT (`uplift.json`) shows the player's CHIPs on
SELL CHIP's cards and raises the chosen one's level for the difference in
price. CUSTOMIZE (`customize.json`) lists the player's bodies, legs and
weapons and tunes one lighter or heavier on the level meter, for the
difference in price; a body tuned below its modules' socket holes has them
taken off. SV SPEC is a placeholder that comes back; the first visit to each shop plays its story event. The mouse works there
too: moving it onto a place's number chooses the place, and clicking a
number, a name or the open sign goes there. The original spins the rider
with the right thumbstick. Here he turns to face the mouse instead, and
each full circle round him pays 10 NY, up to 100 NY per game. Start it
with `--start town_map`.
Pepper's four lessons (FreePlay, basic controls, attack methods, Optic
Camouflage) start the tutorial battle on the training ground, the first 3D
screen: the stage and the robots drawn with Direct3D 11 at the window's
full resolution, the player's Scoobee posed from its skeletons and motions
under a chase camera, and drones walking the stage's enemy path. Pepper's
messages lead through each lesson's steps (walking, turning, dashing,
jumping, dodging, twisting; kills with each weapon; Optic Camouflage) and
hand back to her at the end; FreePlay runs until BACK. The lessons, weapons,
buttons and tuning are in `data\world\training.json` and
`data\world\stages.json` ([the training ground notes](docs/TRAINING_GROUND.md)).
Start it with `--start training_free` (or `training_basic`,
`training_attack`, `training_optic`); it needs the models from a current
`convert` and the robots from `convert-robots`.
After updating the city stage converter, run
`python .\tools\phantomcrash.py convert-robots` to refresh existing city models.
This resolves object slots from each model resource and excludes static shadow
volumes from solid scenery and draws them as static stencil shadows (the Shinjuku and Umihotaru
night variants have none).
VS MODE needs two controllers (the keyboard counts as one). The
layouts, sounds and timings are in `data\ui\title.json`,
`data\ui\main_menu.json` and `data\audio\`.

VS MODE (`data\ui\vs_mode.json`, `--start vs_mode`) takes up to four
players on the controller ports: the keyboard is port 1 and the gamepads
the ports after it (with four gamepads, they take all four). Each port
enters with A, and once two have entered Are you ready? leads to the
Scoobee select. There each player chooses from their own strip, which
holds the three preset Scoobees and the ones kept by KEEP SV. Then any
player chooses the area and its time of day, or RANDOM; with three or more
players only the arena is offered. The panels, strips, pictures and timings
follow the original's scene 44. The battle runs on the chosen stage, one
split screen per player, each player's Scoobee driven from their own port
(`data\world\vs_battle.json`). A robot is out once its armour or its
weapons are gone. When one or none stands, each screen shows YOU WIN, YOU
LOSE or DRAW, and any player picks RETRY, SELECT AREA, SELECT SV or EXIT.
The choice adds the round to the players' wins and losses.
`--press frame:button@port` presses a button on port 1 to 4 in scripted
runs.

OPTION opens the option screen with its scrolling help line and the credit
for the current music. VIBRATION asks YES/NO and is saved with the settings
(nothing rumbles yet); DEFAULT restores the defaults; B moves to EXIT. The
screen is in `data\ui\option.json` and the message box in
`data\ui\message_box.json`. SOUND CONFIG sets the music and effect volumes
(heard at once, through the game's volume table in the converted
`tables\music.json`) and plays test effects; BUTTON CONFIG sets the control
type, view, beginner mode and the button for each action; SCREEN ADJUST
moves the whole picture with the D-pad (X centres it). They are
`data\ui\sound_config.json`, `button_config.json` and `screen_adjust.json`,
and their settings are saved.
Text uses the game's own font from the converted `tables\font.json`; after
updating the converter, run `python .\tools\phantomcrash.py convert
--replace` again to get it.

**Settings > Keyboard / Controller Controls...** edits keyboard keys, controller
buttons, stick directions and triggers. Select an action, choose an input and
click Assign; OK applies and saves the bindings with your settings on exit.
Restore defaults resets the bindings and mouse options; Cancel discards edits.
In battle, W/S move forward/backward and A/D strafe. The mouse or controller's
right stick turns and aims; the left stick moves. Left/right click fires the
left/right arm weapon. Hold Alt to show the cursor and click Settings or Copy Agent Prompt;
release Alt over the game to resume aiming. The cursor stays free over the window bar
or while a menu is open. F10 also releases the mouse for the menus; click the game to
capture it again. Mouse sensitivity, Y inversion and mouse capture can be changed
in the same dialog. The torso-twist training steps retain right-stick torso control.

Controls: keyboard Enter = START, Z/Space = A, X/Backspace = B, Esc = BACK,
arrows or WASD = D-pad; gamepads map by position (south = A). F11, Alt+Enter, or Alt+Numpad Enter toggles
fullscreen. Settings > Visuals contains VSync, a frame-rate cap from 30 to
144 FPS (VSync off with no cap is unlimited), and a frame counter. Pause When
Unfocused remains in Settings; choices are saved in `%APPDATA%\PhantomCrash\PhantomCrash Port`.

vcpkg is checked out under `.tools\vcpkg` at the same baseline as Lodoss, so
packages already built for Lodoss are restored from the local binary cache.

## Tests

```powershell
python -m pytest
python -m pytest --require-private-inputs   # fail instead of skip without the disc
```

## Legal boundary

Only process a disc you are entitled to use. Never commit or distribute the
disc image, `default.xbe`, extracted assets, the game's text,
BIOS/MCPX/EEPROM/HDD images, or Xbox SDK libraries. The ignore rules exclude
the default locations; game text in `data\` goes in as `{xbe:0x...}`
references (see [Data and mods](#data-and-mods)).
