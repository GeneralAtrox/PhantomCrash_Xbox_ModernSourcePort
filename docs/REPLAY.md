# Match replay

REPLAY on the RESULT receipt watches the current RUMBLING/Quick Rental match.
Stopping or reaching the end returns to the same receipt. It neither settles
money again nor reruns combat, AI, damage, dropships, or destructible logic.
The live world is paused while RESULT or its replay is open.

## Views and controls

Fixed, Cockpit, Rear, Right Side, Left Side, and Front View use the existing
battle camera implementation and its extracted fixed camera positions. Free
view is a movable camera. Player View restores the recorded local player's
camera, cockpit, HUD, sights, messages, cinematic bars, effects and monitor
feed. It retains the recorded aspect ratio if the window is resized. Player
View belongs to the human who played the match; choosing another robot from
it switches to Rear. Manual rotation from Player View also switches to Rear.
Other views can watch robots inside the play area, including AI pilots, with
names and models as they were at that time. Staged entrances and out-of-bounds
robots are excluded. Eligibility is recorded per frame and rechecked during
playback; a robot that becomes ineligible is replaced by an eligible target.
If none are eligible, the camera holds its position. In-bounds death animations
remain selectable until they finish.

| Action | Controller | Mouse / keyboard |
| --- | --- | --- |
| Change view | D-pad left/right | Click the view label to cycle |
| Change robot | LB/RB (White/Black) | Click the robot name to cycle |
| Pause/resume | A/START | Click Play/Pause |
| Stop, return to RESULT | B/Back | Click Stop |
| Hide/show replay controls | X | Mapped X key |
| Move in Free view | Left stick | WASD / movement bindings |
| Rotate the camera | Right stick | Hold left mouse and drag outside the controls |
| Reset camera | Right-stick click (R3) | Double left-click outside the controls |
| Lower/raise Free view | LT/RT | Mapped trigger keys |

Switching robots in Free view places the camera by the newly selected robot.
Rotation orbits the selected robot's torso in external views, including Rear;
the chase camera's forward aiming point is not the orbit pivot. The robot
remains the pivot even when the drag returns to zero rotation. Cockpit and
Free view turn the look direction. Reset removes the rotation override; in Free view it
returns to the selected robot's chase-camera position. Mouse presses on the
control bar operate its buttons without starting a camera drag.

`Replay orbit:` logs the selected view, robot and initial torso pivot when
manual rotation starts. Recorded world items retain their ground-shadow and
two-sided material overrides, so those rendering fixes survive playback.
Controls fade after three seconds without button/pointer activity; X can hide
them completely for an unobstructed Player View. Result buttons support both
controller navigation and mouse clicks. View labels, help, camera speeds and
mouse drag sensitivity live in `data/ui/match_result.json`.

## Recording and lifetime

The source is the native battle's submitted presentation state, not an emulator
capture or an input resimulation. Each distinct presented game-clock value
records the complete `WorldFrame`: camera/projection, lighting/post-process,
ordered model instances and posed nodes, paint, camouflage, shadows, moving
clouds, world sprites, cockpit overlay, and the separate monitor frame. The
renderer and texture cache retain the referenced models, materials and texture
handles for the lifetime of RESULT and playback. Robot ownership tags allow
alternate cockpit views to hide their subject without removing scenery.

Player View's 2D record is made at the actual sprite/glyph submission sites,
after ordering, animation, atlas insets, tint and alpha have been resolved.
It stores every quad's texture, source/destination rectangles, pivot, flip,
rotation, blend, colour and clip in submission order; cinematic rectangles
use the same record. No HUD is reconstructed from later battle state. Alternate
views replace the camera and cockpit and omit the local player's 2D HUD.

Playback uses recorded timestamps, independent of the frame cap. Repeated
presentations of an unchanged game clock do not advance replay time. Music
changes and sound cues retain their recorded timing and sound mix. Pausing or
stopping shuts down replay sound loops.

Frames are losslessly compressed into a private temporary spool with only the
timestamp/offset index retained in RAM. The file has delete-on-close ownership,
including process termination. It is released when RESULT closes; saved replay
files and replays from an earlier session are not part of this feature.

## Checks

Round-trip tests exercise world, monitor, cockpit, HUD, sound and resource state;
timeline tests cover 30/60/144 Hz lookup and repeated frames; ownership tests
verify spool deletion. Runtime checks exercise all eight views, controller and
mouse subject switching, Free movement, pause/stop, repeated playback, the
recorded combat HUD, and both post-match routes without duplicate settlement.
