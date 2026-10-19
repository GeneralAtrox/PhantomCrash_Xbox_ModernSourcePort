# Option screen (scene 26) and its sub-screens

How the original's OPTION screen and its SOUND CONFIG, BUTTON CONFIG and
SCREEN ADJUST screens work, from the Europe XBE. The port's versions are
Phases E and F of [OBJECTIVE_01_PLAN.md](OBJECTIVE_01_PLAN.md).

Scene 26 (factory `0xa1fe6`, task `0x2ec8c`) loads `option_tex.bin` (`0x67c`)
and `scr_common_tex.bin`, starts 5GARAGE and fades in over 0.5 s (`0x2eb8f`).

- Six items at x 225, y 121 + 43n (`0x1fa398`), normal sprite on layer 3 and
  selected on layer 4 (`0x1fa368`): VIBRATION, SOUND CONFIG, BUTTON CONFIG,
  SCREEN ADJUST, DEFAULT, EXIT over the background sprite `0x83a`. The
  selected item pulses only while the list takes input (`0x2e710` is called
  with AL = 1 from `0x2e9eb` and 0 from every other state).
- The list (`0x2e9eb`) moves the cursor with the helper (flags `0x16`, SFX 3),
  then confirm plays SFX 5: SOUND CONFIG → scene 35, BUTTON CONFIG → 16,
  SCREEN ADJUST → 0, EXIT → back to 21 (or 39 when entered from the garage),
  each after a 0.5 s fade to black. B moves the cursor to EXIT with SFX 3 and
  does not leave. Returning from 0, 16 or 35 puts the cursor on the item that
  led there; from the main menu it starts on VIBRATION.
- Each item has a text box (`0xbe287`) whose help line (`0x1fa4ac`, padded
  with 30 ideographic spaces before and 4 after) scrolls left at y 72 across
  the 4:3 area, one unit per frame (`0xbe4b0`: the refresh rate divided by
  the logic rate), repeating. Moving onto an item restarts its line; the B
  jump to EXIT does not.
- A second ticker (`0x2a85a`, redrawn by `0x2a443`) scrolls the current
  track's credit in the CURRENT MUSIC band: x 0–396, y 419, as
  《 title : artist 》 from the 81 records at `0x1efd38` (5GARAGE is
  "Sanctuary" by Dunkirk). The band's seven meter bars (at 434, 395) move
  with the music as on the town screens, and the credit follows the music
  when the radio changes songs; SOUND CONFIG and BUTTON CONFIG share the
  band.
- VIBRATION (`0x2e87f`) opens a YES/NO message box on the current setting
  (bit 30 of the config word at `+0x9cc` means off; the default is on). The
  choice moves with left/right (flags `0x17`), B moves it to NO, A stores it.
  DEFAULT (`0x2e923`) asks with NO chosen at first and remembers the last
  answer; YES resets the options (`0x2e6ea`) and shows "Set the options to
  default." with an OK button until A (`0x2eb23`).
- The message box (`0xbec0f`): window sprite `0xe8d` centred at (320, 240),
  layer `0x107`, growing from 0.1 scale by 0.15 (x) and 0.3 (y) per frame and
  playing sys SFX 12 on its first frame. Once full size it shows up to six
  lines at (95, 193 + 18n) and the buttons: YES `0xe85`/`0xe86` at (223, 297)
  and NO `0xe83`/`0xe84` at (322, 297), or OK `0xc1b`/`0xc1c` at (273, 297),
  the chosen one pulsing on layer `0x109`.
- Text uses the system font `ascii_font_sys_tex` (512×2048, 18-texel cells,
  28 per row). Glyph N is the Nth Shift-JIS character of the string at
  `*0x27f114` (`0xbde54`); the first `0x184` glyphs have a left offset and
  width in `0x1c3fb0`, the rest are 18 wide. The game's English text is
  written in full-width characters.
- Entered from the garage (scene 39), the screen also shows the day and money
  (`0xb1b56`, `0xb1cdd`).

## Shared behaviour of the sub-screens

- Each fades in and out over 0.5 s and takes no input while fading. The
  selected item pulses in every state, fades included (unlike the option
  list, which pulses only while it takes input).
- The cursor helper `0xdf410` takes flags 0x1 horizontal, 0x2 repeat, 0x4
  wrap on a fresh press, 0x10 left stick too, over [min, min + count - 1].
  `0xdf390` is A or START with the confirm sound, `0xdf310` the same without
  it, `0xdf170(pad, 3, 1, mask)` a fresh press of any button in the mask.
- Leaving returns to scene 26 with the cursor on the item that led there.

## SOUND CONFIG (scene 35)

Init `0x3b6b8`, list `0x3b52d`, draw `0x3b426`, exit `0x3b213`. Plays
AWAKETO1 ("title") with its credit in the CURRENT MUSIC band (y 419).

- Items BGM, SE, SE TEST and EXIT at x 62, y 132 + 43n (sprites
  `0x1fde30`, selected `0x1fde10`), the help strings at `0x1fdf98`.
- Up and down wrap on a fresh press (flags 0x16) and restart the help line.
  Left and right change the value with flags 0x17: the volumes over 0-255,
  the SE test over 150 effects, both wrapping at the ends on a fresh press.
  The volumes are heard at once. B moves the cursor to EXIT; EXIT (confirm)
  keeps the settings and fades out.
- Values are three zero-padded digits from `common_2d_tex` 1, 11 apart,
  ending at (283, 143 + 43n) (`0xbc6d2`, flags 0x18). A volume also has a
  bar: an 8-texel strip scaled by level / 8 at (312, 148 + 43n), layer
  0x108, inside frames at (308, 143) and (308, 186).
- The SE test numbers run through four banks in order (`0x253800`):
  battle_player 51, battle_old 3, battle 48, sys 48. Confirm plays the
  effect and nothing else.
- A volume setting becomes DirectSound millibels through the int16[256]
  table at `0x410400` (converted to `tables/music.json` `volume_millibels`);
  level 0 is -9600 (silence). The port's gain is 10^(mB / 2000), with a
  2285 * log10(v / 255) curve standing in when the table is missing.

## BUTTON CONFIG (scene 16)

Init `0x28329`, list `0x28189`, draw `0x27d74`, exit `0x280ed`. Plays
5GARAGE with its credit.

- Thirteen rows: control type (Standard / Preferred), view change (NORMAL /
  REVERSE), beginner mode (OFF / ON), eight actions, DEFAULT and EXIT. Bars
  at x 46, y 98 upwards (`0x1dd658`); the selected row's bar replaces its
  dimmed one. Labels `0x1dd7e0`, help strings `0x1dda24`.
- Up and down wrap on a fresh press. On a choice row left and right flip the
  value, and its arrows (`0x1dd968`, placed from `0x1dd9f4`) show while it
  is selected. On an action's row the first assignable button pressed (A,
  B, X, Y, L, R, white, black) is assigned to it, and the action that had
  that button takes the row's old one, silently. The row shows the
  button's icon (`scr_common_tex` 0) and, while selected, its marker on
  the controller picture (`0x1dd558`).
- DEFAULT (confirm) restores the default assignments only. EXIT keeps
  everything. There is no cancel, because B is assignable.
- BEGINNER MODE's help line ends with EXIT's text in the original; the port
  keeps it.

## SCREEN ADJUST (scene 0)

Init `0x132d8`, adjust `0x131d0`, question `0x130ee`, draw `0x13004`
(sprites `0x1d6e00`, `0x1d6e38`). Plays 5GARAGE.

- The offset lives in the config word at `+0x9cc`: bits 8-13 x and 14-19 y,
  x in [-32, 30] and y in [-16, 14]. The render loop (`0xb7cc0`) shifts every
  pass's viewport by it, so the whole game moves.
- The D-pad moves the picture one unit a step without wrapping: x plays sys
  effect 4, y effect 3. X centres it silently.
- The screen's labels (the D-pad and button hints and the title) are drawn at
  their place minus the offset, so they stay still while the picture moves;
  the fades cover the whole screen.
- Confirm asks whether to apply the adjustment, with NO chosen.
  Left and right move the choice; B moves it to NO. A plays the confirm
  sound, and NO puts back the offset the screen started with. Either way
  the screen fades out with the box still shown (`0x130b4`).

## Mouse

A port addition: moving the pointer onto an item moves the cursor there (with
the cursor sound) and a click on it confirms it, as A does. Once a question
box has grown, a click on YES or NO answers with it; a click on a notice's OK
closes it. Right-click is B (cancel), as in the other menus. Hit areas are the
items' and buttons' own sprites. Sound Config, Button Config and Screen Adjust
are still controller-only.
