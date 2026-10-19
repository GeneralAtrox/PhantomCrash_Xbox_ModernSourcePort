# Experimental local co-op

Local co-op is disabled by default. Enable **Settings > Experimental Co-op**,
then choose **Coop** on the title menu or directly below **New Game** in Quest
Mode. This is local split-screen, not network multiplayer.

## Joining and saves

- Press Start on each controller to join, up to four players. On the keyboard,
  Enter joins the first free bay with the keyboard.
- Choose Continue, New Game, or import a solo save with Left/Right and A.
  Keyboard: Up/Down or W/S choose, Enter or Space load, Backspace leaves the
  bay, Esc returns to the main menu. New Game's name is typed on the keyboard.
- New Game uses the existing name-entry screen, skips the opening/tutorial,
  and starts in the Garage. Later campaign progression remains active.
- Player 1 presses Start once every joined player is ready. B changes a ready
  player's save selection; B again leaves an unready slot.
- Each numbered player has an independent co-op profile in the normal settings
  file's `coop_profiles` array. Importing a solo save makes a separate copy;
  it does not write back to the original solo slot. The in-game Save action
  writes that player's profile to disk immediately, preserving the other
  profiles and solo saves. The Save screen reports failure if writing fails.
  Normal application exit also persists the current co-op profiles.
- Importing a solo save or completing a new player's name saves a starting
  checkpoint before marking that player ready. Load can return to it during
  the same session, including after leaving a match and advancing the day.
  Playing does not replace that checkpoint; the in-game Save action does.
  If the initial write fails, the lobby offers Retry or Change Save.
- Save/Load shows all four numbered profiles. Your slot is highlighted; other
  players' named slots are dimmed and labelled LOCKED. Only your own slot can
  be selected, loaded or overwritten.

Two players use side-by-side panes; three or four use a grid. Each pane owns
its menu flow, camera, inventory, money, options and campaign. The small status
strip identifies peers, their current screen and day. Town Map markers show
their current destination, greyed while they are inside it. Shop markers and
the status text identify peers' selections when sharing a shop screen.
Radial-menu markers resolve the peer's item on the viewing player's wheel,
including its rotation, instead of copying the peer's selected-label position.
Entering a submenu retains a dimmed marker on the parent option until returning
or leaving that location. Battle Music reports its menu, program, track and play
mode selections. Rumbling reports the selected area, menu item and entrance;
Area Select keeps a dimmed marker on an area while a peer is inside it. Markers
from different internal panels are not overlaid on unrelated controls.
The Rumbling live arena map reads the shared battle when its day, area and
contest match. It shows the actual NPCs and human robots, with P1/P2/P3/P4
labels on the human markers; positions and headings update while a peer plays.
Inactive, destroyed and off-stage entrance robots are excluded. Unoccupied
contests continue to show the player's normal background simulation.

## Shared battles and progression

Players entering the same day, area and contest share one live battle. Joining
later does not restart the match. Human players come in addition to the
area's opponents: the first takes the player's own place, and each further
player gets a robot of their own, so no NPC is ever taken over. A place a
player leaves is reused by the next to join, and once everyone has left the
area goes back to its roster's robots alone. Each player uses their own
robot, weapons, stats, entrance sequence, HUD and camera. Co-op teammates
cannot damage each other.

Settings > Co-op opens the host's persistent co-op preferences. Experimental
Co-op remains disabled by default. Each new shared match snapshots its rules;
changes made during a match apply to the next newly created match, and late
joiners use the existing match's rules.

- Enemy and boss level offset: -99 through +99 (default 0), applied to NPC
  chip, body, legs and weapon levels through the existing stat curves. Final
  levels clamp to -99 through +99. Reinforcements use the same offset. Player
  loadouts and saved levels are unchanged.
- Robot kill NY: 0 through 1000 percent (default 100 = 1x). The multiplier
  applies once to robot bounties, rounding down to whole NY. Pickups, carriers
  and wreck destruction retain their own values.
- Share shootdown credit (default off): every current participant receives one
  credit; contributors already credited by combat are not counted twice.
- Share cash (default off): the full earned amount goes to every current
  participant, including money pickups and destruction awards. For example,
  a 5,000 NY boss bounty at 100% pays each participant 5,000 NY. This does not
  transfer existing balances or reward players shopping or in another match.
- Share boss defeats and contest clears (default on): teammates receive the
  existing boss-beaten and champion victory progression. These feed the normal
  contest unlock and game-clear routes; no separate trophy system is added.

Checkboxes save immediately; use **Apply level and NY values** for the numeric
fields. Repairs and rental deductions remain personal. Awards settle once into
that player's live quest at the result screen and persist through their normal
Save flow; changing these host preferences does not checkpoint player saves.
The values are stored in the settings file's `coop` object.


Start opens a player's pause/Exit controls and suspends that player's input;
the shared battle continues. Leaving settles that player's result only. They
can advance to the next day and keep shopping while teammates continue the old
match. Different days, areas or contests run as separate matches. Players must
reach the same day and choose the same contest to meet again.

One pane supplies audible music, preferring an active battle. Shared battle
sound effects are audible from one participant to avoid doubled sound. Menu
interaction and dialogue still belong to their respective panes.

## Implementation and verification

`CoopScreen` owns independent `ScreenRunner` instances and render targets.
`CoopMatches` owns battles keyed by day/area/contest and advances each shared
simulation once per logic tick. Per-player controls, target views, rewards and
result settlement are separate. Robot animation poses are shared by the views
of the same battle. The default solo path remains selected when the flag is off.

The Release build and bounded runtime runs using the existing launch/input
options verified:

- Co-op menu visibility with the flag on and off.
- Four controller ports joining, new-save name entry, and Garage startup.
- Four participants joining one arena containing ten robots total, then moving
  and firing with separate cameras and HUDs.
- Independent Town Map/Garage navigation and peer markers.
- Garage peers on different radial selections, a retained dimmed Music marker
  after entry, and separate live cursors inside Battle Music.
- Rumbling peers on different area buttons, with a retained dimmed area marker
  while the other player enters that area's submenu.
- Player 1 watching the live Rumbling map while Player 2 joins and moves in
  the matching battle; captures show the labelled robot moving across the map.
- A later participant joining a running match.
- Player 1 leaving and reaching Garage on Day 3 while Player 2 continues Day 2.
- Separate profile persistence, with the source solo save unchanged.
- Explicit Save writes observed on disk for all four players while the game
  remained open; each write preserved the other three profiles and solo save.
- Other players' slots remain dimmed and unselectable after Left/Right input.
- A deliberately locked temporary save file caused Save to fail without
  changing the existing save.

Friendly-fire exclusions were checked in the targeting, projectile and damage
paths. Physical controller disconnect/reconnect, every shop transaction, and
long campaign sessions have not been exhaustively exercised. No new automated
test suite was added; verification used builds, runtime logs and captures.
