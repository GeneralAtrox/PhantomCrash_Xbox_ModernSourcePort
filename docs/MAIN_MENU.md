# Main menu buttons

The main menu and Quest submenu use code-drawn rounded buttons and Century
Gothic Bold Italic text. Selection pulses a yellow fill with dark text over the
black button, using the same animation as the options menu, with a pale
yellow/white outline and halo; unavailable entries have dimmed text and borders.

Labels live in `data/ui/main_menu.json`; the experimental Coop label lives in
`data/ui/coop.json`. Dimensions, typography, spacing, colours and offsets live
in `data/ui/main_menu_buttons.json`, in the usual 640x480 UI coordinates. These
documents support the normal mod overlay. Removing an item's label retains its
original sprite rendering for existing mods.
The selected fill and text use `selected_background` and `selected_text`;
each item's selected sprite animation also controls its code-drawn pulse.

The Windows renderer obtains the font from the installed system fonts; the
proprietary font is not bundled. If Century Gothic Bold Italic is unavailable,
it logs a warning and uses Arial Bold Italic. GDI+ generates rounded paths and
glyph outlines into cached GPU textures at twice the current output scale.
Each label's three states are cached together, rebuilt only when the output
scale changes, and released with the menu. No text shaping or texture upload
runs during ordinary cursor movement. Menu actions and the co-op setting gate
are unchanged.

## Mouse

A port addition: a left click anywhere on the title opens the menu, as START
does. In the menu, moving the pointer onto a button moves the cursor there
(with the cursor sound) and a click on it confirms it; a click off every
button does nothing. Right-click is B (back), as in the other menus. A
button's area is its drawn rectangle (`offset` and `size` in
`main_menu_buttons.json`); an item that keeps its sprite artwork uses the
sprite's own bounds. Pointer movement and clicks also reset the idle timer
when `resets_on_input` is on.
