# Widescreen

## The original

The original has no widescreen support. Its device setup (0xc5427) reads the
dashboard's video flags (`XGetVideoFlags`, 0xd8678) and the AV pack
(0xd864f), but uses them only for the refresh rate (50 or 60 Hz on PAL,
kept at 0xa69c94) and the 480p flag. It never tests the widescreen bit and
always creates a 640x480 back buffer. A 16:9 TV simply stretched the 4:3
picture.

## The port

- **Menus, 2D screens and movies stay 4:3.** The 640x480 picture fills the
  window height, centred, with black side bars. Their art is fixed 640x480
  pictures, so nothing is gained by filling the side bars. Tickers stay cut
  at the 4:3 edge.
- **In-game 3D (battles, the attract demo) goes widescreen** once it is
  ported: the vertical field of view is kept and the view widens with the
  window's aspect (Hor+).
- **A setting chooses where the in-game HUD sits:**
  - *4:3 HUD*: every HUD element keeps its original position inside the
    centred 640x480 area, as the original frames it.
  - *Widescreen HUD*: elements anchored to the left or right edge of the
    original screen move out to the window's edges (`Viewport::visible_left`
    and `visible_right`); centred elements stay centred.

  Each HUD element's anchor (left, centre or right) belongs in its data file
  with its position, so the 4:3 positions stay the original numbers and the
  setting only moves where the anchor points are.

  The battle panels (`data/ui/battle_panels.json`) are the first to do this.
  Each element there has an anchor, and `"layout"` picks `"widescreen"` (the
  default) or `"4:3"`. That is a data value until the OPTION screen gains
  the setting. The world HUD (`battle_hud.json`) follows projected points
  and has no anchors.
