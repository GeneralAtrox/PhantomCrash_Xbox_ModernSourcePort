# Objective 1: boot movies, logos and main menu

Goal, in the user's words:

1. All intro videos and logos, with transitions similar to the original.
2. Each one can be skipped individually.
3. The main menu, with complete audio.

This document records how the original does it (static analysis of the
European `default.xbe` in Ghidra, 2026-09-26) and the plan for the port.
Addresses are virtual addresses in that executable.

## How the original works

### Scene system

- The game runs one *scene* at a time. `0x256738` is a table of 159 scene
  factories; the current scene ID is at `0x25672c` and starts as `0x9e` (158).
- Each factory starts a child task (`0x0a1b1e(parent, 0, update, cleanup,
  data_size)`). A task exits with a result code (`0x0a1bd8(task, result)`),
  and the scene's continuation maps that code to the next scene
  (`0x0a1bf7(task, next_scene, 1)`).
- `0x0a1b7a(scene)` starts that scene's music from the per-scene table at
  `0x256230` (index into the WAV name table at `0x254480`; -1 stops music).
- The scene's own screen draws only after the previous scene finishes, and a
  scene may skip its fade-in when it knows the previous one shares its
  background (title and main menu do this).

### Scenes 5, 7 and 23

Researcher readings from a sweep of all 234 calls to `0x0a1bf7` (the
operands pushed before each), the one transition with a computed target
(`0x0a3963`, Pepper's guide: story events, 42, 8 or 41), the story events'
target scenes (3, 6, 8, 15, 19, 20, 22, 24, 27, 30, 41, 159) and the
soft-reset table `0x255fb0` (always 40):

- **Scene 5 is REPLAY**, reached from the match result (scene 30,
  continuation `0x0a2dc5`: result 0 to 5, result 1 to 24) and returning to
  30. The port has it ([REPLAY.md](REPLAY.md)).
- **Scene 7** (init `0x04a540`, task `0x04a59e` then `0x04a5ef`) starts the
  demo battle task `0x04a6a0` on its own, as the attract demo does, and
  exits to 40. Nothing transitions to it: a debug scene, not ported.
- **Scene 23** (init `0x074500`, steps `0x074561`, `0x0745bf`) prints "ID
  %08X" (`0x22f2ec`), sets `0x98ba3c` and exits to 40. Nothing transitions
  to it either: an ID viewer for debugging, not ported.

### Boot flow

```
158 phantagram.sfd ─► 18 logo screen ─┬─(first boot)──► 45 dolby.sfd ─► 2 attract movie
                                      └─(later loops)─────────────────► 2 attract movie
2 attract movie (phantomcrash.sfd, then opening.sfd on the next visit) ─► 40 title
40 title ─ START ─► 21 main menu        40 title ─ idle 30 s ─► 1 attract battle demo
1 attract demo ─► 158 (loop the intro) or 40
21 main menu ─ B or idle ─► 40 title (no fade, music continues)
```

| Scene | Factory / task | Content | Enters | Skip | Leaves |
|---|---|---|---|---|---|
| 158 | `0xa8e68` / `0x2ed62` | `phantagram.sfd` (movie 8) | Cut in | After a 1 s grace, pad 1 A or START (mask `0x110`) | Instant black (`fade(0, 0)`) |
| 18 | `0xa1e90` / `0x291fa` | `logo_tex.bin` sprite `0x5f2`: Genki logo, 192×192, centred | Fade in 0.5 s, hold 2 s | After the fade-in, A or START | Fade out 0.5 s (skip: instant) |
| 18 (first boot only) | `0x29046`, `0x28fc5` | sprite `0x5f3`: MPEG Sofdec + ADX licence screen, 640×480 | Fade in 0.5 s, hold 2 s | A or START, only in the last second | Fade out 0.5 s; then result 0 → Dolby |
| 45 | `0xa67bd` / `0x1bf82` | `dolby.sfd` (movie 7) | Cut in | 1 s grace, A or START | Hard cut |
| 2 | `0xa2b7b` / `0x1372a` | `phantomcrash.sfd` / `opening.sfd`, alternating via `0x1d6f6c` (starts at 1; table `0x259524`: 0 opening, 1 phantomcrash) | Cut in | 1 s grace, **any** button on any pad | Holds 16 frames, cut to black |
| 40 | `0xa1f3b` / `0x3e8f2` | Title: `title_tex.bin` + `scr_common_tex.bin` | Fade in 0.5 s | START (mask `0x10`) plays SFX 14 | To menu: no fade. Idle: fade out 0.5 s |
| 21 | `0xa2222` / `0x2c460` | Main menu, same textures as title | No fade from title | — | Items fade out 0.5 s |

- Every movie runs at 640×480, 59.94 fps (`0xae961`). Movies are MPEG-1
  program streams with the CRI "(c)CRI" marker, video `0xE0` and ADX audio
  `0xC0`, which FFmpeg demuxes and decodes natively, as in Lodoss.
- The logo screen is also where the original copies the `binlink` packs to the
  hard-disk cache (`0x9edbc`, controlled by `0x98d18c`). The port does not need
  that.
- `0x415e7e` records that the licence screen was shown once. Later intro loops
  (reached from the attract demo) show the Genki logo but skip the licence
  screen and Dolby.
- The movie scenes that play later in the game are out of scope here: kudryavka
  + cloud (12), ending (11), epilogue (37), opening02 (in 25), staff and
  the_end.

### Fades

`0x9f6e0(duration, colour)` starts a fade to a colour and `0x9f734(duration)`
fades back in; `0x9f7e1` advances them once per frame. Durations are in frames
at the game rate stored in `0xac73b8`, so `0xac73b8 * 0.5` is half a second.
The states are: 0 = clear, 1 = fading out (alpha = 1 − t/d), 2 = opaque,
3 = fading in (alpha = t/d). A duration of 0 goes straight to opaque. Scenes
change while the screen is opaque, and each movie start clears the overlay
(`0x9f77f`). All boot fades use black, except for one white fade in
`opening02`.

### Title and main menu

- Title (`0x3e8f2` → `0x3e8d4` → `0x3e82e`) loads resource `0x690`
  (`scr_common_tex.bin`) and `0x689` (`title_tex.bin`, one 1024×1024 DXT5).
  `0x3e6cd` draws (the list below is in call order; the draw order comes
  from the layer words, see *Draw parameters*):
  - the sprite list at `0x1fe9c8`: background `0xa53`, logo `0xa57` at
    (96, 93), copyright `0xa56` at (240, 402);
  - sprite `0xa55` (8×40 texels) stretched to 640×54 bands at y 0 and y 427;
  - sprite `0xa54` (640×24) as 20 centred scanline strips at x 320, scrolling
    0.2 px per frame and reset at 640 (`0x3e641`);
  - "PRESS START BUTTON" (`0xe91`, `common_2d_tex` texture 1) at (225, 335),
    layer `0x108`, blinking (flag `0x4000`, `0x135cd`).
- The title waits 30 s. START (mask `0x10`, any pad, only after the fade-in)
  records the controller that pressed it as player 1, plays SFX 14 and opens
  the main menu. A timeout fades out 0.5 s and starts the attract demo. Coming
  back from the menu (scene 21), the title neither fades nor restarts the
  music (`0x3e8f2`).
- The main menu (`0x2c1ce`, `0x2befc`) draws QUEST MODE, VS MODE and OPTION at
  x 225, y 292/325/358. Each item has a dark sprite, plus a yellow sprite for
  the selected one; the pairs are at `0x1f944c`. VS MODE turns grey
  (`0xff404040`) and plays SFX 8 when fewer than two controllers are
  connected. The normal sprites use layer 6 and the selected one layer 7 with
  the pulse flag `0x2000`.
  - Each tick `0x2c1ce` moves the cursor, then checks confirm (A or START,
    mask `0x110`), then cancel (B or BACK, `0x220`, which plays SFX 8) and the
    idle timer; both return to the title (result 4) without a fade.
  - The cursor helper `0xdf670(pad, &cursor, first, count, 0x16, 0, 3)` →
    `0xdf410` moves on the D-pad (auto-repeat) and the left stick (flag
    `0x10`), wraps on a fresh press but stops at the ends while repeating
    (flag `4`), and plays bank 0 effect **3** on a move. The earlier reading
    of `0x16` as SFX 22 was wrong: `0x16` is the flags.
  - Confirm plays SFX 5; QUEST MODE opens its submenu and resets the idle
    timer; VS MODE and OPTION fade to black over 0.5 s (`0x9f6e0`) and exit
    with result 2 or 3.
  - The initial cursor is 0, or 1 when returning from scene 6 or 44 (VS) and
    2 from scene 26 (OPTION), so it lands on the item that led away.
  - QUEST MODE opens NEW GAME / LOAD GAME at the same positions (`0x2c2d9`,
    `0x2c05a`, layer `0x101`, selected sprite pulsing). `0x6ded3(284, 375,
    0x101, 0xc3d, 20)` draws the CANCEL hint: button sprite `0xc32` at
    (284, 375) and text `0xc3d` at (304, 377). Confirm plays SFX 5 and fades
    out; B plays SFX 8 and returns to the top level, keeping its cursor. The
    submenu decrements the idle timer but does not act on it. NEW GAME →
    scene 28 and LOAD GAME → scene 17. The submenu starts on LOAD GAME when
    the save holds a game (`0x2c460`).
  - VS MODE → scene 44. OPTION → scene 26.
  - The menu returns to the title 75 s after it opens (`0xac73b8 * 75.0`);
    only opening the submenu resets the timer, not cursor moves.
- The menu reads `phantomcrash.sav` at start-up to know whether a save exists
  (`0x6b040`, `0x6b3ed`).

### Option screen

Scene 26 and its sub-screens (35, 16, 0) are described in
[OPTION_SCREEN.md](OPTION_SCREEN.md).

### Draw parameters

`0xbc10d` fills the 52-byte block: sprite, x, y, scale x/y, three unused
words, the layer word (default `0x101`), colour (`0xffffffff`), alpha (1.0),
flags (default 4) and one more word. The renderer (`0xd391d`, `0xd28e8`):

- Flag 8 centres the sprite on its position.
- Flags `0x6020` animate the alpha from the frame counter (`0xe05f0`) with an
  80-frame period: t = (frame mod 81) / 80 and w = |2t − 1| (CRT `fabs`).
  Flag `0x20` uses w, `0x2000` uses 0.3 + 0.7w (a pulse, on the selected menu
  item) and `0x4000` shows the sprite while w ≥ 0.5 (a blink, on PRESS START:
  frames 60–80 and 0–20 on, 21–59 off, so 0.7 s on and 0.65 s off). Flags `0x100` / `0x200` force
  alpha 0 / 1.
- Layer words below 9 go to one queue and `0x101`–`0x108` to another, drawn
  after it; each queue draws by layer. The title and menu therefore draw:
  background (1), scanlines (4), menu items (6, selected 7), logo, submenu
  items and CANCEL hint (`0x101`), bands (`0x103`), copyright (`0x104`),
  PRESS START (`0x108`).

### Resources and sprites

- Resource IDs index a table of 1,927 24-byte records at `0x285e40`: path
  pointer, size, type, a pointer to {first texture slot, slot count} for
  texture packs (type 3), then binlink pack index and offset for resources
  stored inside `Media\binlink` packs. `0x675` = `logo_tex.bin`, `0x689` =
  `title_tex.bin`, `0x690` = `scr_common_tex.bin`.
- Sprite IDs index `0x276680`: 3,763 pairs of {definition pointer, texture
  slot}. A definition is six floats: left, top, right, bottom in texels, then
  the texture's width and height. A slot maps back to a pack file and a
  texture index through the resource table. The portrait slots 534 and 535 are
  shared by 28 character packs, which are swapped at run time.
- Draw calls fill a 52-byte parameter block (`0xbc10d`): x, y, scale x/y,
  colour, a layer word (`0x101`, `0x103`, `0x104`) and flags. Flag 8 centres
  the sprite on its position.

### Audio

- Music: title and main menu both use table entry 28, `AWAKETO1.WAV`
  (44.1 kHz, 16-bit, stereo PCM, 41 MB, no loop points, so it loops whole).
  The menu does not restart it when coming from the title. Scenes 17, 26 and
  44 use `5GARAGE.WAV`; scene 28 (new game) stops the music.
- Effects: `sound\sys.bin` is an "SE BIN" bank: `"SE BIN  "`, a count of 48,
  then 16-byte records {flags, runtime slot, parameters, offset}. Each offset
  holds a complete RIFF WAV in Xbox ADPCM (IMA ADPCM with 36-byte blocks),
  mostly mono 44.1 kHz. `battle.bin`, `battle_player.bin` and `battle_old.bin`
  have the same layout. Menu IDs: 3 = cursor move, 5 = confirm, 8 = cancel
  and unavailable item, 14 = title START.
- `0x9f7e1` also mutes controller rumble during fades. There is no audio fade
  on scene change beyond the music stop in `0x0a1b7a`.

### Texture packs

The `Media\scroll` and `Media\texture` packs start with `0x80000101` or
`0x80000102`, a count, the offsets of 20-byte XDK texture descriptors and
usually one palette offset per texture. Each descriptor holds a common word,
an absolute data offset, a lock word and a format word:

- bits 8–15: format — `0x06` swizzled A8R8G8B8, `0x0B` swizzled P8 (fonts),
  `0x0F` DXT5
- bits 16–19: mip levels
- bits 20–23 and 24–27: log2 width and height

`src/phantomcrash_research/texture_pack.py` reads them, and all 190 textures
in 88 packs decode correctly.

## Port design

### Data (all editable; mods override by path)

```
data/boot/intro.json      steps: movie | image, files, fades, hold, skip buttons,
                          first_boot_only, rotating attract movies, then "title"
data/ui/title.json        layers (image, rect, position, size, repeat/scroll),
                          start button, idle timeout
data/ui/main_menu.json    items and the quest submenu, sprites, targets, sounds
data/ui/option.json       option items, actions, help lines, tickers
data/ui/{sound_config,button_config,screen_adjust}.json  the option sub-screens
data/ui/message_box.json  the SYSTEM INFORMATION window and its buttons
data/audio/music.json     named tracks -> WAV files
data/audio/sfx.json       named effects -> converted WAV files
```

Every path resolves against these roots, first match wins:

1. `mods/`
2. `artifacts/europe/assets/` — output of `phantomcrash convert`:
   `textures/<pack>/<n>.png`, `sound/sfx/<bank>/<nn>.wav` and `tables/*.json`
3. `artifacts/europe/files/Media/` — movies and music are already open formats
   (MPEG-1/ADX `.sfd`, PCM `.wav`) and stay there

FFmpeg also accepts replacement `.mp4`/`.webm` movies. Changing the intro or
the menu background is a data edit or a file dropped into `mods/`.

### Code (`port/src`, files under 400 lines)

| Module | Source |
|---|---|
| SDL3 window + D3D11 device, resolution-independent 640×480 2D space with widescreen pillarboxing for 4:3 content | Lodoss `desktop_host` |
| Frame pacer 30–144 Hz + unlimited, fixed 60 Hz logic tick | Lodoss `desktop_frame_rate`, `desktop_frame_rate_dialog` |
| Movie playback: FFmpeg demux/decode, SDL audio stream, YUV upload | Lodoss `movie_ffmpeg`, `movie_player*`, `movie_audio_output`, minus exact-replay/contract code |
| Gap-free handoff between movies (open the next file before the current one ends, keep the last frame until the first new one is ready) | Lodoss `movie_handoff` / `boot_sequence` ideas |
| JSON loading | Lodoss `strict_json` |
| Input: SDL gamepad + keyboard, edge-triggered presses, Xbox button names | new, after Lodoss `boot_input` |
| Scene runner + fade overlay (states and timing as above) | new |
| Intro sequence driven by `intro.json` | new |
| Sprite renderer (textured quads, atlas from `sprites.json`) | new |
| Audio mixer: streamed looping music + one-shot SFX on one SDL stream | new |
| Title and main menu screens | new |

### Deliberate differences from the original

- Movies and logos skip on a fresh press of any button, with no 1 s grace. A
  press counts only when it started during that step, so one tap skips one
  item. The grace period and the skip buttons are per-step settings.
- Natural endings keep the original's transitions (cuts, black, 0.5 s fades),
  but without the loading gaps. Where the original holds or goes black while
  loading, the port preloads.
- No 50/60 Hz or language prompt. No hard-disk cache copy.
- The attract battle demo (scene 1) is ported: after the title's idle
  timeout a contest's robots fight on one of the RUMBLING stages for 60 s
  under a blinking PRESS START BUTTON, then the intro loops; any button
  returns to the title. See `data/ui/attract_demo.json` and *Attract demo*
  in [MATCH_RESULT.md](MATCH_RESULT.md).
- Main-menu items lead to placeholder screens that return to the menu.

## Work plan

### Phase A: extraction and formats — done

`python tools/phantomcrash.py convert` writes 190 PNG textures, 150 PCM effect
WAVs and the resource, sprite, music and movie tables in about a minute. It
uses the standard library only. pytest covers:

- the texture, ADPCM, effect-bank and PNG code, on synthetic inputs;
- the real XBE tables;
- that every data file parses and every file and rectangle it names exists in
  the converted assets.

A composite built from the data files alone reproduces the title, main menu
and Genki screens.

### Phase B: host (C++) — done, pending the user's interactive check

The port opens a window at full pixel resolution over SDL3's D3D11 renderer.
It runs 60 Hz logic ticks and interpolates between them for presentation,
which is paced by VSync and/or the Lodoss frame-rate cap. While the pointer
is on the title or menu bar, or a menu is open, it presents with VSync even
when VSync is off: tearing flip-model presents leave the GDI menu bar and the
copy icon's tooltip flickering unless something forces composition, such as a
screen recording. The Lodoss Settings
menu and dialog are reused, and settings are saved. The 640×480 canvas fills
the window height, and the extra width is exposed as canvas x < 0 and > 640.
Input maps the keyboard and gamepads to Xbox buttons, latching taps between
ticks. Data documents merge `mods/data/` overlays.

CTest covers the JSON, the overlay, asset resolution, the shipped data files,
the pacer, the fixed step, input edges, canvas fitting and settings, plus a
hidden smoke run of the executable.

### Phase C: intro sequence (done, pending the user's interactive check)

1. Port the movie player core and verify that all 11 `.sfd` files decode with
   audio.
2. Scene runner, fade overlay and the `intro.json` step machine: movie steps,
   image steps (fade in, hold, fade out), first-boot-only steps and loop
   target.
3. Preload and handoff so natural endings flow with no black gaps or hitches;
   per-step skip.

Status: the port plays `intro.json` from boot. `MovieDecoder` (FFmpeg, no SDL)
decodes the Sofdec files; `MoviePlayer` shows frames as YUV textures, timed
by the audio clock (Lodoss helpers in `movie_clock.hpp`). `IntroSequence` is
the step machine; `IntroScreen` draws it and opens the next movie while the
current step runs. A skipped movie's sound stops at once and its last frame
stays for the fade. Before Phase D the intro restarted without the first-boot
steps, alternating the two attract movies.

Verified: the media test decodes all 11 `.sfd` files at 640×480 with audio
(the intro's two short movies to the end); unit tests cover step order and
timing, first-boot branching, rotation, fades, one step per tap, parse errors
and the movie clock; smoke captures showed Phantagram, Genki, the
Sofdec/ADX screen, Dolby and the opening movie in order. Sound, skip feel and
transitions are for the user's check.

### Phase D: title and main menu — done

1. Sprite renderer; title screen with background, bands and scrolling strips;
   30 s idle.
2. Main menu: three items, the new/load submenu, VS greyed out with fewer
   than two pads, back and idle return to the title without a fade.
3. Audio: `AWAKETO1.WAV` looping from title into menu without a restart;
   SFX 14/3/5/8 on START/cursor/confirm/cancel; music stops or switches on
   leaving.
4. Remaining reads: the `0x40` draw flag on PRESS START, the CANCEL hint
   placement, and music loop points (does the WAV loop whole?). Resolved: see
   *Draw parameters* and the menu notes above.

Status: after the intro the port shows the title and main menu from
`title.json` and `main_menu.json`. `FrontEnd` (core) runs the title, menu and
quest submenu as one screen, so the scanlines keep scrolling and the music
carries on between them. `ScreenRunner` switches between the intro, the front
end and placeholder screens (`placeholders.json`) for the items that lead to
unported scenes, and plays each screen's music and effects through
`AudioMixer`. The title's idle timeout goes back to the intro, now without the
first-boot steps.

Deliberate differences: any controller drives the menu (the original hands it
to the pad that pressed START); the cursor's auto-repeat timing is the port's
own (0.4 s, then every 0.1 s); the scanlines scroll smoothly and keep their
position from title to menu (the original resets them); stretched sprites
sample from texel centres, so the bands have no bleed at their edges.

Verified: unit tests cover START and its fade-in lock, the 30 s and 75 s
timeouts, cursor wrap and repeat, VS with one or two controllers, the submenu
and B, the return cursor, the draw-flag waves and the scanline placement. A
menu smoke run drives title, menu, submenu, placeholder and back with
injected presses. Captures showed the title with PRESS START blinking, the
menu, VS greyed out, the submenu with its CANCEL hint, a placeholder and the
return, and boot followed by five taps reaching the title.

### Phase E: option screen (done, pending the user's interactive check)

1. Research scene 26: items, cursor, help lines, the vibration and DEFAULT
   boxes, the music credit, the system font. See
   [OPTION_SCREEN.md](OPTION_SCREEN.md).
2. Converter: the font's character order and widths (`tables/font.json`) and
   the music credits (`tables/music.json`).
3. Port: text rendering and tickers, the message box, the option screen and
   its placeholders; the vibration setting saved with the settings.

Status: OPTION on the main menu fades to the option screen from
`option.json`. `OptionMenu` (core) runs the list, the per-item help lines and
the boxes styled by `message_box.json`; `GlyphFont` lays out text in the
game's font and places the scrolling lines. The vibration choice is kept in
the settings file (`game.vibration`) and silences the battle's damage rumble
(`data/world/rumble.json`, `rumble.hpp`). EXIT returns to
the main menu with the cursor on OPTION.

Deliberate differences: a message box grows (with its sound) every time it
opens; the original grows the vibration box only the first time in the
scene. The day and money shown when entering from the garage wait for the
garage.

Verified: unit tests cover the font mapping and layout, ticker scrolling and
wrap, the fade-in lock, the vibration box (growth, choice, B to NO, storing
the answer), DEFAULT with its notice and remembered answer, B to EXIT and the
fade back to the main menu, the per-item help restarts, the return cursor,
the credit line and parse errors. An option smoke run answers the vibration
box and exits. Captures showed the list with its help and credit lines, the
growing and grown vibration box, the DEFAULT notice, and the round trip from
the main menu and back with the cursor on OPTION.

### Phase F: option sub-screens (done, pending the user's interactive check)

SOUND CONFIG, BUTTON CONFIG and SCREEN ADJUST replace their placeholders,
each from its own `data/ui/*.json` (research in
[OPTION_SCREEN.md](OPTION_SCREEN.md)). Core: `SoundConfigMenu`,
`ButtonConfigMenu` and `ScreenAdjustMenu` on shared `MenuBasics`,
`ScreenFade` and `HelpClocks`; desktop: `DrawList` and the three screens.
Volumes apply at once through the original's millibel table, and the screen
offset shifts every screen's viewport. Held left/right repeat timing
(`value_repeat`) is the port's; the original's is not known.

Verified: unit tests cover volume gain, the SE test files, value wrap, the
button swap, choices, DEFAULT, EXIT, the offset range, YES/NO and parse
errors. A smoke run visits all three and returns; captures showed the
volumes and bars, a reassigned button, and the moved picture with its
labels held still. Smoke `--press` frames are now logic ticks.

## Open items to resolve during the phases

- The SE BIN `flags` word (the low half probably marks looping effects).
- Whether a save changes the menu (the `0x2c460` "3 or 4" count).
- Whether `0xac73b8` becomes 50 in 50 Hz mode. The port uses 60.
- Texture slots 474–478 (301 sprites) have no pack in the resource table;
  none of them is used by the intro, title or menu.
