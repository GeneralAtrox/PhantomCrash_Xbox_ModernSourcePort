# Online multiplayer determinism investigation

Investigation started 2026-10-06; implementation evidence updated 2026-10-07.
This records the current working tree, including existing uncommitted gameplay
work. Normal online arena entry and gameplay are connected and have passed a
two-process loopback check. Coordinated exits and committed results also pass.
Confirmed presentation, fresh-peer catch-up and connection recovery are now
implemented. Adverse-network desktop verification is recorded below.

## Implementation progress

The active objective is to complete deterministic online gameplay, networking
and prediction, verify two local game builds communicating, then ask the user
to test between actual networked machines. Local implementation and verification
are complete; the two-machine network check remains for the user.

Implemented and tested so far:

- `online_input.hpp` / `online/input.cpp`: lossless tick/player control and
  TargetView encoding, 36/112/136 bytes depending on optional view data.
- `online_socket.hpp` / `online/socket.cpp`: owned nonblocking IPv4 UDP sockets;
  datagrams limited to 1,200 bytes. `wait_readable` lets a thread sleep until a
  datagram arrives.
- `online_peer.hpp` / `online/peer.cpp`: bounded reliable ordered messages,
  acknowledgements, duplicate rejection, retries and timeout handling. Resends
  follow a smoothed round-trip estimate (RFC 6298, Karn's rule: resent messages
  are not measured), clamped to 20 ms–1 s and starting at 50 ms. Unsequenced
  datagrams (kind 3) carry gameplay inputs outside the ordered window.
- `online::Lobby::start_transport_worker`: a networking thread waits on the
  socket and services the room every 4 ms, so acknowledgements, heartbeats and
  resends continue while the frame loop renders, loads or stalls. The room
  serialises access with a mutex; message handling stays on the game thread.
- `online_room.hpp` / `online/room.cpp`: host/join handshake, assigned slots,
  shared seed, caller-supplied compatibility digest, heartbeats and room membership.
  The desktop supplies a SHA-256 digest of the executable, data, converted
  assets, raw Media fallback, mods and decoded external glTF/GLB buffers.
  Directory links are followed with cycle detection. Installation paths and
  independent profiles are excluded; different bytes reject the handshake.
  The converter's `artifacts/<content>/assets/manifest.json` is excluded too:
  it records the absolute folder converted on that machine, and every file
  it lists is hashed individually.
- `online::Lobby` and the desktop co-op screen now support command-line room
  hosting/joining, assigned local slots and remote name/screen/location presence.
  Menus and cursors remain local. Each process displays one full-size local pane.
  `--online-host <port>` and `--online-join <IPv4:port>` select this development path;
  COOP's "Join by address..." row types the same IPv4[:port] in game (kept for
  the session, port 29641 by default). Normal rental-battle menu entry
  now starts network combat through the shared admission protocol.
- Online profiles use a dedicated settings field, independent of solo and local
  co-op profiles and of the room's assigned player number. The online room stays
  alive across local menu changes. An online window continues running when
  unfocused; the existing offline pause-on-unfocus option is unchanged.
- `online_prediction.hpp` / `online/prediction.cpp`: bounded speculative ticks,
  immutable confirmed inputs, late-input rollback/replay and no lost tick numbers
  when prediction stalls. Missing remote inputs repeat held controls, never mouse
  displacement or button edges.
- `Battle::checkpoint` / `restore_checkpoint`: local in-memory snapshots that
  copy state, own weapon definitions and repair weapon/supply-route references.
  RobotCombat copies bind their animator to the copied assembly. These are not
  wire-serializable snapshots.
- `RumblingWorld::checkpoint` and `CoopMatch::checkpoint`: restore all arenas,
  fractional background clocks, replacement RNG, roster/loadout records, match
  rewards, participants and entrance scenes. Restoring retains the shared Battle
  and rider-scene identities used by views. Static asset tables remain session-owned.
- `online::MatchSimulation`: prediction adapter for the co-op match/world tick,
  including per-player TargetView and scripted entrances in player-slot order.
  Rider scenes retain the existing local co-op phase (observe before the shared
  match tick). Normal online menu entry now creates the agreed timeline.
- `online::MatchSession` connects an agreed match to prediction and its Room input
  channel. The desktop room loop dispatches match input, reconciles corrections,
  and advances an attached session after local controls have been captured.
  The production TrainingScreen now captures mapped controls and its locally
  maintained TargetView once per network tick when a session is attached.
  Unsent mouse deltas and button edges survive a prediction stall; already-sent
  controls cannot be replaced. Camera/HUD drawing does not write online targeting.
- BattleScenePresenter now retains a shared scene object that the online match
  can own and restore without replacing the presenter's reference. The online
  simulation advances it; the offline screen keeps its existing update path.
  TrainingContext can supply an agreed entrance seed; offline still uses its
  existing clock seed. OnlineGame now supplies participant/start agreement.
- `online::InputChannel`: match-timeline input routing through the room host,
  sender/assigned-slot validation, host relay and immutable locally submitted
  inputs during stalls. Unrelated timeline messages remain with the dispatcher.
  Inputs travel as unsequenced datagrams, as in Lodoss: each packet repeats every
  input the recipient has not acknowledged, oldest per player first, then each
  player's newest, then the rest, within one datagram. Every packet acknowledges
  the next tick needed from each player, so a lost packet is covered by the next
  one instead of waiting for a resend. Packets repeat while inputs are
  unacknowledged (half the resend timeout, between one tick and 100 ms), and a
  bare acknowledgement goes out a tick after new inputs arrive. The host relays
  only inputs it had not seen before. Setup records, agreement and state checks
  stay on the reliable ordered channel.
- `FixedStep::set_carry_backlog`: online play keeps the time a stalled frame
  owes (up to one second) and catches up at most four ticks a frame, so a
  hitch does not put this machine's tick numbers behind the others. Offline play
  still drops the backlog after an eight-tick burst, as before.
- `LiveResizeTicks`: dragging, resizing or opening the window's system menu runs
  a Windows modal loop inside SDL_PollEvent. While online, SDL's live-resize
  expose events keep running ticks with neutral controls there, so the other
  players do not wait at the prediction limit. Failures are rethrown after the
  event poll returns.
- `online::RecordChannel`: bounded chunked setup transfers over the room, with
  per-peer budgets that leave room for input and heartbeats. Tests cover large
  records, queue saturation, malformed fragments and unrelated-message dispatch.
- `online::TimelineAgreement`: host proposals, per-peer readiness, state-digest
  comparison and commit/rejection/timeout handling. The normal desktop admission
  path uses the match-state inspector to agree the actual initialized state.
- `online::BattleSetup`: versioned initial-match data carrying a shared seed,
  calendar/arena/contest, fixed player slots, separate profiles and entry loadouts.
  Both host and clients decode the serialized form, excluding process-local epochs.
  This is a saved-roster genesis, not a live checkpoint for joining an active match.
- `build_online_battle`: session-owned tables/assets build the world, configure all
  participants in slot order, create their entrance scenes, prepare navigation and
  bind simulation-owned poses before any local TrainingScreen exists. Prepared
  screens reuse those scenes and cannot reseat/rearm players or restart entrances.
  The factory is resource-tested and wired into normal online menu entry.
  Online views also ignore local never-die/infinite-ammo overrides; offline cheats
  keep their existing behavior. This restriction was reported to the user.
  Creating the first shared match reconstructs its saved roster and restarts
  transient AI/projectile state absent from saves. That possible gameplay change
  was reported before implementation. It must not be used to rebuild active
  matches when players join them.
- `Battle::prepare_deterministic`: explicit online-only navigation preparation.
  Normal online startup uses it; offline gameplay does not.
  Once opted in, RumblingWorld also prepares newly built arenas before they run.
- `DesktopMatchAssets`: simulation-owned robot poses and collision radii, refreshed
  when a participant/roster look changes. Local views cannot install missing
  simulation-owned poses. Newly replaced actors are bound after the world update
  before presentation, as well as before simulation. Binding bookkeeping rolls back with
  the match. Named model assets are loaded before simulation needs them.
- Online wreck collision now has a simulation-owned builder. Wrecks prepare their
  captured posed geometry before the next tick's burn-clock update; views do not
  install collision when that builder is present. The builder reuses the existing
  posed-model triangle extraction and asset cache without submitting a draw.
  Offline collision setup keeps its existing view-owned path.
- Battle checkpoint restoration increments a presentation-only generation counter.
  Wreck draw caches rebuild after rewind, including same-count replacements and
  restoration of a previously destroyed wreck. This counter is not gameplay state.
- `online::state_digest`: SHA-256 inspection of match/world/battle/scene state,
  including exact integer and floating-point bits, random generators, pending
  controls, targeting, AI/navigation state, mover/animation clocks, evaluated poses,
  weapon queues/projectiles, collision, supply drops, rosters and background clocks.
  Public aggregate records are decomposed into every field; non-aggregate owners
  have explicit visitors. `python scripts/check-state-coverage.py` accounts for
  283 fields across 19 explicit owners. The decomposition table is generated by
  `scripts/generate-state-aggregate.py`; unsupported owner types fail compilation.
  Pointer addresses are never hashed. Immutable references use content digests;
  mutable references also include traversal ordinals to detect accidental sharing.
  Inspection requires prepared navigation and the room's matching executable/data
  identity. Fixed source tables referenced by RumblingWorld remain covered by that
  content identity; its per-match rules/HUD tuning and runtime records are inspected.
  Exclusions: the draw-cache restore generation; the co-op local player selector
  overwritten by every shared tick; resolved navigation futures; graphics/resource
  owners and transport/prediction history; scene/door/scenery sound/music outboxes.
  The wreck collision callback is represented by its enabled state plus its produced
  collision, with code/assets covered by room compatibility. These exclusions do
  not remove state that controls a future shared gameplay step.
- `Prediction::inspect_confirmed` and `MatchSession::confirmed_digest` inspect a
  retained confirmed tick after applying pending corrections, then restore the
  predicted present. Inspection exceptions also restore it; unavailable, unconfirmed
  and pruned states cannot be reported as verified. Two UDP-driven session replicas
  now compare digests at both the current confirmed tick and an older retained tick.
  `StateChannel` now exchanges full confirmed-state digests every 60 ticks from
  the timeline's starting tick. Host comparisons include menu spectators; each
  client compares with the host. Reports are retained separately from rollback
  history, with a bounded backlog. A mismatch stops the local online path and
  reports the timeline, tick and peer. Tests cover delayed reports, mismatches,
  wrong senders/timelines/ticks, missing history and bounded backlog.
- `OnlineGame` now owns host-ordered arena admission. All connected peers simulate
  every active arena while their menus remain local. Before admission, sessions
  freeze, report their horizons (including captured but unstepped input), align
  and confirm the highest horizon, apply the entrant, compare full state and
  commit new timeline IDs. Alignment repeats held controls without repeating
  mouse displacement or button edges. Adding a participant preserves existing
  actors, AI, projectiles and elapsed time; it does not rebuild the live arena.
  The brief admission pause was reported as a possible gameplay-visible change.
  Inputs arriving immediately behind the commit are queued until the receiving
  timeline exists; otherwise reliable transport could acknowledge an input which
  the old timeline ignored, permanently stalling confirmation.
- Online completion and play-time counters now belong to each shared participant
  and are restored by rollback. Death waits for the death animation to finish;
  gate exits wait for their shared scene; victory retains the configured hold.
  The host observes confirmed outcomes only. Manual exit requests and automatic
  completion use the freeze/alignment/state-agreement protocol to release the
  actor and commit an immutable settlement on every replica. Local receipt
  consumption does not remove an actor or run shared simulation, and returns a
  bounty only once. The last departure returns the roster and retires the arena.
  Waiting for confirmation can briefly delay an online exit; this was reported
  before implementation. Offline exits and reward formulas retain their paths.
- Result views now retain the correct renderer/resource owner. Constructing the
  result backdrop originally advanced the shared world's random stream; the
  online path checkpoints/restores that world around the local backdrop build.
  Result/backdrop creation and local settlement leave shared gameplay unchanged.
- `online::Presentation` retains confirmed post-tick states outside rollback.
  The local view consumes each once for audio, dialogue, scene/music cues, rumble,
  shot/impact particles, supply effects and lights, then restores the predicted
  present. Its cursor survives membership changes; exited views stop collecting.
  Simulation clears previous presentation outboxes at the start of each tick.
  Tests cover a speculative shot removed by correction, once-only confirmed
  emission, timeline changes and restoration after a callback exception.
  Movement/aiming remain predicted; event presentation waits for confirmation.
- Fresh room peers catch up from the host's confirmed per-arena input journal,
  including genesis and ordered admissions/exits. Existing peers align and pause;
  the newcomer replays acknowledged chunks without creating a historical view,
  checks the complete final state digest and joins the normal timeline agreement.
  Existing arenas are never rebuilt. Completed arenas release their journals.
  Catch-up time and retained journal memory grow with the age of a running arena;
  long matches can therefore produce a noticeable join pause.
- Established rooms retain their slots, reliable sequence numbers and queued data
  through network outages. A repeated session nonce can rebind a changed endpoint.
  Simulation stops on a missing peer or transport congestion and resumes when
  delivery returns. Windows UDP ICMP reset reporting is disabled so errors from
  an expired endpoint cannot starve its returning handshake. This recovers a
  connection while the applications remain open; it does not restore a terminated
  client/host process or provide host migration.
- Departures: a closing room sends a repeated leave notice, and a peer silent
  for `peer_drop_seconds` (30) is dropped, so a quitting player no longer leaves
  the others waiting to resume. The host frees the slot and announces the
  smaller membership; a client whose host leaves, or that was dropped, ends with
  an error. A departed player still in an arena gets idle controls from the
  host until the normal departure transition settles them out (outcome
  `leave`), so the remaining computers keep confirming ticks. State checks and
  records skip departed peers. A departure during an in-progress barrier still
  ends the session.

The `phantomcrash_online_tests` target currently passes 49 tests, including its
port identity check. Coverage includes real loopback sockets, lost packets and
ACKs, duplicate/reordered input, three-player room membership, incompatible
fingerprints, bounded prediction, and real Battle rollback with three delayed
player streams. State inspection tests additionally detect hidden AI/queue/RNG
changes, preserve exact numeric bits, distinguish mutable aliases, and verify
restoration and confirmed-history inspection. A minimal pose
copy test also checks that the source pose can be destroyed safely. Replacing a
cooperative actor now clears its mover's borrowed motion-timing reference before
releasing the old pose. The regression test demonstrated different state hashes
before the fix and checks checkpoint restoration after the old poses are freed.
This can remove stale-animation behaviour during robot replacement; combat
damage, firing rates and balance values are unchanged.
The three additional wreck tests cover preparation before burn expiry, collision
and shot blocking, repeated rollback through creation/destruction, and empty geometry.

Build verification: the Release desktop executable and online/co-op tests build
successfully. Co-op tests now pass 22/22: new coverage includes repeated rollback
through replacements and background clocks, rewards/scenes, arena rebuilds, and
three real UDP rooms driving co-op match replicas with delayed inputs. The network
test converges to the same exact selected observations as a fully confirmed
reference and rejects another player's input and another timeline's messages.
The desktop session test also compares complete inspected match-state digests
after delayed-input reconciliation and verifies historical inspection leaves the
current state unchanged.
Result tests cover rollback of completion/play-time counters, the victory hold,
death-animation completion, gate-exit completion and withholding a predicted
finish until delayed inputs are confirmed and reconciled.
These unit/component tests run in one process; separate desktop evidence follows.
Additional tests cover late-peer UI presence, application-message dispatch,
content/executable/mod fingerprint changes, installation-path independence,
and separate online-profile persistence.
The session test fills its eight-tick prediction window, rejects a second capture
for the same tick, receives delayed peer controls, and converges after rollback.
The pending-input test verifies that mouse motion/edges survive a stall exactly
once while analog/button levels use the latest sample.

Two separate hidden desktop processes using the same Release executable were
also tested on loopback with separate temporary HOST and CLIENT profiles. Each
received the other's assigned slot, name and garage presence, rendered its own
full-size garage with the remote player's location, and exited successfully.
A second run kept the host in the garage while the client entered the hangar;
the host received the client's `hangar / garage` presence and stayed in its own
menu. Both temporary settings files remained byte-identical after the smoke run.
Those initial runs exercised the desktop room/profile/menu path only.
The subsequent combat run used two separate hidden desktop processes with the
same Release executable, separate temporary profiles, 960x540 windows and a
60 Hz cap. Scripted controls navigated garage, town map, Club Wired and the
day-zero Shinjuku class-D rental contest. The host entered first; the client
joined its running arena at tick 173. Both processes verified matching full
confirmed-state digests at ticks 60 and 120 before that admission, then every
60 ticks from 233 through 1253 afterward. Each process used its own local view
and targeting, and the scripted inputs included weapon fire. Both captures
showed active gameplay at 21 seconds, both processes exited successfully, and
both temporary profile files remained byte-identical. No divergence or online
error was logged. This establishes that the actual desktop processes exchanged
gameplay and matched the inspected confirmed state on this machine/build. It
does not establish adverse-network behavior or portability to another machine.
A subsequent two-process run covered coordinated exit and results: the client
left at agreed tick 935 while the host remained in combat. Both processes kept
matching full confirmed-state digests through tick 1475 while the client showed
its independent result backdrop. The host then left at about 25.4 simulation
seconds and received its own receipt. Both processes logged 23 successful state
comparisons, produced result-screen captures, and exited with status zero and no
online error. The host's one shootdown earned a gross bounty of 173 internal
money units and a rental-adjusted prize of 86; the client received zero. Separate
temporary settings files remained byte-identical. This was the normal pause-menu
Exit/confirmation path, not a direct test-only departure call.
That earlier compatibility scan read about 2.58 GB across 17,859 files. The scan
now additionally covers 308 raw Media files (2,717,604,170 bytes) and decoded
model buffers, including buffers outside the scanned roots. It conservatively
includes cosmetic assets and can impose substantial startup delay. The test
fixtures verify same-size changes, different installation roots, mods, raw
fallback data, percent-encoded external buffer paths and missing dependencies.
The scan assumes installed content is not edited during a running session.

The separate `phantomcrash_online_asset_check <game-root>` executable requires
converted assets. With this checkout's shipped training robot, three posed
players ran 360 ticks with 34 rollbacks and converged to identical checked combat,
bone and muzzle observations. It also restores poses after their source Battle
is destroyed. The simulation loop took about 0.06 seconds in Release on this host;
this is a small arena without stage collision and is not a full-match performance
measurement. This check does not prove complete hidden-state equality.

`phantomcrash_port --online-stress-check [seconds] --root <game-root> --settings
<scratch.json>` times a full match against the 144 fps frame cap. Two peers on
this computer join one RUMBLING quick-rental arena (10 opponents) over loopback,
with 60 ms of added one-way delay so predictions roll back as on the internet.
Each peer has the production battle view and draws every frame; the pilots
weave and fire with never-die and infinite-ammo set before the first tick, so
wrecks keep piling up. Every 15 game seconds it prints the wreck and rollback
counts and each peer's frame, simulation and drawing time (mean/p99). It passes
when each peer's last p99 frame fits 6.94 ms and its mean has not grown by half.

`phantomcrash_port --online-resource-check --root <game-root>` exercises the actual
hidden desktop host, Direct3D asset loader, match asset binding and wreck draw cache.
It returns before loading or saving user preferences/profiles. This checkout passes
with 10 posed robots and a 3,282-triangle wreck: simulation collision matches the
existing posed-model calculation; a view cannot install simulation-owned collision;
rollback restores destroyed/replaced wreck drawings and the correct robot pose.
It also advances the production death animation/world update twice from the same
checkpoint, without view ticks: the wreck appears on tick 541 and becomes solid on
tick 542 in both runs, with identical collision triangles. The test initializes the
dying state directly; it does not exercise the preceding damage application.
It does not connect two game instances or establish full-match hidden-state equality.
The resource check also constructs the production TrainingScreen, registers its
real entrance/exit scene, attaches a MatchSession, draws at alternating aspect
ratios, and advances six captured control frames. It verifies exact control
encoding, no simulation-target mutation from tick/draw, consumption of the
captured camera/lock boxes, and preservation of shared scene identity on restore.
This particular input-boundary check uses a single local participant; it is not
the requested two-build gameplay test. Normal startup separately uses OnlineGame
to agree the factory's initial state before attaching these components.
The resource check also builds two independent factories with separate resource
owners and three participants, creates local views for different slots and aspect
ratios, and compares exact selected combat observations, evaluated bones and
muzzles over 720 ticks. Both initial states and every observed tick match. A
360-tick rollback/replay matches the reference. Creating another local view in
mid-match and destroying views leave these observations unchanged. This exercises
real converted arena/robot assets, entrances and world updates, but does not prove
transport between desktop processes. The check now also compares the full inspected
match-state digest at initialization, after view construction, after 720 ticks,
after replay, and before/after opening or destroying local views. Those checks pass;
full digests are not currently computed at every intervening tick.
The same resource check now also connects three real UDP peers, commits one
arena while two players remain in menus, then admits those two players into the
running arena. It verifies preserved existing-player/in-flight-weapon state,
confirmed digests, first inputs coalesced with commit messages, and alignment
from unequal frozen horizons with an already-captured edge input. All three
replicas reach identical confirmed state at tick 182.
It then tests the host leaving first, duplicate exit requests, equal committed
settlements, once-only local bounty consumption, isolated receipt/backdrop
construction, a confirmed client death, continued gameplay for the remaining
client, and retirement after the last departure. The death state is initialized
directly for this membership test; the separate animation/wreck check exercises
the death progression itself.
It additionally starts fresh UDP peers after ticks 180 and 480. Catch-up preserves
the host state, reconstructs an earlier admission and completed exit/settlement,
and all three replicas agree through continued combat at tick 600. A separate
socket test drops all traffic for thirteen seconds, changes the client's upstream
endpoint and verifies that its original slot resumes with once-only delivery.
A three-peer presence test continues changing menus on the healthy client while
the other connection is down, then verifies that its latest location reaches
the recovered peer. Gameplay input relays retain their backlog while the
outgoing reliable window is congested, rather than overflowing it on resume.
The final two-process desktop run used 35–65 ms delay in each direction, 3%
random packet loss and a thirteen-second total blackout. The client connected
after the host entered combat and verified its catch-up. Both processes resumed
confirmed gameplay after the outage, with 58/47 matching-state checks and maximum
rollback counts of 725/720. The host left first; the client's subsequent death
completed the last departure, arena retirement and result without a mismatch.
Both processes exited successfully and their test settings remained unchanged.
The relay forwarded 33,461 packets (3,540,563 bytes), randomly dropped 1,012 and
dropped 13,357 during the blackout. This exercised actual desktop gameplay over
impaired local UDP; it does not establish behaviour across different machines.
An offline `training_free` desktop smoke run also completed 30 frames successfully
after the shared-scene refactor. This checks startup/update/rendering, not full
offline gameplay equivalence.

The earlier broader core suite
reports 552/556 passing; failures are `host_controls_tests.cpp:141` (diagonal
movement/mouse strafe), `tutorial_tests.cpp:96` and `:141` (course completion),
and `post_process_tests.cpp:43` (blur/tint/distortion). Those test paths do not
invoke the new online/checkpoint APIs. No baseline comparison was performed, so
these results are recorded without claiming the failures predate this work.
They have not been changed as part of online implementation.

All agent-created build/test logs for this implementation pass were temporary;
the standard `build/port` working build is retained. No game settings or saves
were changed by the new online tests.

The remaining validation is the two-machine network test below. Current source
builds have a COOP connection menu: Local split-screen, Host network game, and an
automatically refreshed list of LAN hosts. UDP discovery uses port 29642; hosts
created through the menu listen for gameplay on 29641. Hosts advertise only while
their online co-op session is open, including during gameplay. Solo games are not
advertised. Manually addressed hosts also advertise when the discovery port is
available. Room replies identify the actual gameplay port and current capacity;
stale rooms expire after four seconds and full rooms cannot be selected.

Discovery queries each active IPv4 interface's broadcast address, with global
broadcast and loopback fallbacks, so a preferred VPN adapter does not hide the
physical LAN. It uses a separate, bounded UDP query/reply protocol. A random query token
rejects unsolicited replies, host identities deduplicate multiple interfaces,
and discovered addresses still undergo the existing full compatibility handshake.
The first GUI connection hashes content on a worker while the menu remains
responsive. Network saves use the online profile; local profiles are restored
when leaving the network session. Discovery/room-handshake and save-isolation
regressions are included in the counts above. A two-process desktop check opened
COOP without network arguments, hosted from the menu, displayed the host in the
browser and joined as player 2. A separate broadcast-only query received the
host's reply. LAN menu discovery is included in v0.5.2.

### Two-machine network test

Use the same executable, data, extracted assets and mods on both Windows machines.
With v0.5.2 or later, open COOP on both computers, select Host network game
on one, then select its automatically listed name on the other. When the host is
not listed (another network, or broadcasts blocked), choose "Join by address..."
and type its IPv4 address, adding :port if it is not 29641. Allow the game
through the private-network firewall for UDP 29641 and 29642. Discovery requires
the same broadcast network; it does not discover Internet hosts. The explicit
commands below remain available for routed networks and the v0.5.0 release.
Both computers must use the same S.L.A.I. setting and installed content when
enabled. Disabled S.L.A.I. assets are excluded from the compatibility hash;
different enabled settings or changes to active content reject the connection.
From the repository directory, start the host with:

```powershell
.\build\port\port\Release\phantomcrash_port.exe --root . --online-host 29641
```

On the other machine, replace `HOST_IPV4` with the reachable host address:

```powershell
.\build\port\port\Release\phantomcrash_port.exe --root . --online-join HOST_IPV4:29641
```

Allow UDP 29641 to the host (an Internet connection also needs a reachable route
or port forwarding). Enter an arena on the host before starting the client to
exercise live catch-up. Join that arena on the client, move and shoot, briefly
interrupt the client's network connection while keeping both games open, then
restore it. Check that play resumes, each player can leave, and each result is
awarded once. A third machine can use the same join command. The join catch-up
pauses the match, and confirmed sound/effect playback can be delayed by latency.

## Agreed scope

- Investigate deterministic gameplay for three players. Initially target the
  same Windows x64 executable and matching gameplay data on each machine.
- Menus exchange presence: which UI a player occupies. They do not need shared
  simulation, cursor replication, or deterministic animation.
- Keep each player's camera, HUD and reticle local. Supply their gameplay-relevant
  values as simulation inputs; other machines need not draw that player's HUD.
- Alert the user before implementing a change that could affect gameplay.
- Determinism here means the same initial state and the same tick-numbered input
  stream produce identical gameplay state. It does not mean matching an emulator
  or making every cosmetic effect deterministic.

## Current input boundary

`RobotControl` already expresses controls after local bindings and control-mode
conversion (`robot_mover.hpp:55`, `training_battle.cpp:152`). Capture these values,
not raw keyboard/controller events. In particular, preserve mouse displacement
and the mover's turn-rate/backlog rules (`robot_twist.cpp:9`); replacing them with
an absolute facing direction could change movement.

`FireControl` supplies four held fire requests and a camouflage edge. Requests
are not confirmed shots: the simulation still applies ammo, cooldowns, animation
readiness, obstruction, lock time and spread.

The existing `TargetView` is a conservative initial aiming-input boundary:

| Field | Consumer / reason |
| --- | --- |
| View-projection matrix | View eligibility and lock-box tests in `battle_targeting.cpp:22` and `:33` |
| Camera eye position | Camera-to-target obstruction in `battle_targeting.cpp:65` |
| Optional reticle centre and inner/outer half sizes | Weapon-specific acquisition boxes, `battle_targeting.cpp:101` |
| Presence of view and boxes | Existing fallback sight behaviour, `battle_targeting.cpp:167` |
| The same view and box centre | Tracked shoulder-weapon aim, `battle_pose.cpp:63` |

Capture the values the current code would consume at the simulation boundary,
including absent optionals. Apply them to the explicit player/actor for that tick.
Remote presentation must not overwrite them. Preserve their current update phase;
sampling a newer camera/reticle value could move acquisition by one tick.

An aim direction or a client-selected target ID alone does not represent all
these rules. The first implementation should retain the existing combat checks
and preserve the full view input. Reducing this input is not currently necessary.

### Proposed uncompressed input size

This is a proposed explicit binary encoding, not `sizeof` or a measured packet:

| Fields per player per tick | Bytes |
| --- | ---: |
| Seven 32-bit control floats | 28 |
| View matrix (16), eye (3), lock-box values (6), all 32-bit floats | 100 |
| Flags for controls, firing, camouflage and optional presence | 2 |
| Signed dodge direction | 1 |
| Tick number | 4 |
| Player slot | 1 |
| Maximum total | 136 |

At 60 Hz this is 8,160 bytes/s of input payload per player. A host forwarding all
three players' inputs to both remote players sends 48,960 bytes/s, approximately
0.392 Mbps. This excludes packet headers, acknowledgements, input redundancy,
checksums, joins, initial state, recovery snapshots and any relay overhead. It is
not a forecast of the final network bandwidth. No precision reduction is assumed.

## Deterministic simulation boundary

Useful existing foundations: fixed 60 Hz active gameplay; indexed robot loops;
seedable random engines; one shared co-op battle.

The shared unit must encompass match progression, not just `Battle::tick`:
`CoopMatch::tick` prepares entrances, ticks combat, collects rewards, observes
entrances and updates the rumbling world (`desktop_coop_matches.cpp:61`). The
world also controls roster replacements. Background arenas currently run at the
configured 30 Hz while active combat runs at 60 Hz. Their update phase matters.

Required initial/checkpoint state includes gameplay-relevant fields owned by:

- Battle/Combatant: movement, queued controls, AI/path state, locks and timers,
  health, magazines, damage attribution, camouflage and respawn/death state.
- Armory: projectiles, beams, growing blasts, identity counters and spread RNG.
- Combat poses: animation cursors, readiness, muzzle/barrel selection and any
  state affecting movement, firing, melee or death completion.
- Stage systems: doors, destructibles, wrecks, supply carrier and pickups.
- Match/world: roster/arrival state, entrances, participants, rewards, timing
  accumulators and all gameplay random-generator states.

This is an owner inventory, not a completed field-by-field snapshot schema.
Transient events must either be consumed before a checkpoint or represented
where pending consumption affects gameplay. Static content needs matching
identities; pointers, renderer handles and object padding are not state encoding.

### Remaining sources of divergence

The investigation listed five; the first four are now handled on the online path.

1. Navigation activation followed background-worker completion
   (`battle_ai_steering.cpp:86`, `nav_graph_search.cpp:258`). Handled: online
   matches call `RumblingWorld::prepare_deterministic`, which builds navigation
   synchronously before the first tick, and arenas built later are prepared too.
2. Live worlds used local `random_device` seeds
   (`desktop_screens_rumbling.cpp:87`). Handled: `build_online_battle` builds a
   separate world from `BattleSetup`'s shared seed and saved roster, and later
   entrants are admitted into that running world, never seeded afresh.
3. Joins reuse the first eligible robot slot (`desktop_coop_matches.cpp:9`).
   Handled: online joins and departures are host-proposed timeline changes,
   applied at a confirmed-state barrier in the same order on every replica, so the
   eligible-slot search runs on identical state; the resulting state digests are
   compared before play resumes.
4. `FixedStep::add` dropped elapsed-time backlog after stalls. Handled for
   online: the backlog (up to one second) is carried and caught up four ticks a
   frame, and window drags keep ticking. Tick numbers never depended on the
   clock; this keeps a stalled machine from falling behind the others.
5. Floating-point math and standard-library distributions need verification on
   supported machines. Neither a math rewrite nor a RNG replacement has been
   established as necessary for a shared executable. This still needs the
   two-machine test.

The battle, armory, supply and entrance systems have separate random engines.
Preserve their states and call order rather than merging or reseeding streams.
Changing their organization changes random outcomes and is not a neutral fix.

## Initial repeatability experiment

Built `phantomcrash_ai_sim` from the current working tree in the standard
`build/port` Release build. Ran two separate processes with:

```text
--areas 0,1,2 --runs 1 --seconds 60 --seed 314159 --hz 60 --jobs 1
--robots 12 --player patrol --trace --watch 0,0,0,1000000 --max-wall 45
```

The simulator derives each area's seed using its run index. Poses, doors and
supply were enabled; navigation was built synchronously by the existing harness.
Each process completed 60 simulation seconds per area with no early-stop or
missing-pose warnings. Outcomes matched:

| Area | Lives observed | Kills | Shots |
| --- | ---: | ---: | ---: |
| Shinjuku | 21 | 14 | 221 |
| Shibuya | 23 | 14 | 380 |
| Umihotaru | 1 | 0 | 362 |

Byte comparisons matched for all 71,169 watch records, 1,205 trace records,
45 life records and 27 stuck records. Run summaries differed only in measured
wall-clock duration.

Limits: watch data is alive NPC state, excludes the piloted robot and many hidden
fields, and writes rounded decimal floats. Combat trace samples are less frequent
than ticks. This was one machine/build, without three human input streams,
late joins, GUI targeting, asynchronous navigation or network delay. Matching
logs are evidence of repeatability for recorded observations, not proof of
bitwise gameplay determinism. The existing replay records presentation rather
than input resimulation (`docs/REPLAY.md:54`).

## Gameplay-impact alerts

| Proposal | Potential effect / disposition |
| --- | --- |
| Capture existing controls and full TargetView | Intended to preserve behaviour; test unchanged value precision and consumption phase |
| Prebuild navigation before online simulation | Can change initial enemy fallback behaviour and add loading time; implemented only for online sessions, including normal startup |
| Admit another player into a running arena | Briefly pauses shared simulation to align confirmed ticks and commit the entrant; existing combat state is preserved |
| Move local reticle tracking to fixed ticks | Can alter tracking feel; no longer required by this input-boundary proposal |
| Quantize controls or replace aiming with a target ID | Can alter turn/aim precision or acquisition rules; not proposed |
| Change RNG algorithms, streams, timer arithmetic or floating-point math | Can alter combat outcomes; investigate only if a reproducible divergence requires it |
| Wait for network inputs or predict missing inputs | Bounded eight-tick prediction is implemented; excess delay stalls simulation and retains unsampled mouse motion/edges. Late corrections can be visible. Sounds/effects wait for confirmation and are emitted once. |
| Confirm an online departure before results | May briefly delay Exit or a completed death/gate/victory transition while peers align and commit the settlement; reward formulas remain unchanged |

## Next necessary verification

Implement a separate reproducibility test path around the actual co-op update
order. Feed a recorded three-player input stream, including TargetView, into
independent simulations. Hash canonical exact gameplay fields per tick and emit
the first field-level difference. Include hidden future-affecting state, not only
positions, and verify that state restoration resumes identically.

Exercise firing, locks, melee, collisions, cloak, deaths, pickups, replacements
and joins/leaves. First compare fresh processes on one build/machine, then
supported machines. Playback of a captured input stream must remain independent
of render scheduling. Live input capture may differ with a player's local view;
all replicas must consume the same captured values for that player.

The current AI harness can supply assets and arena setup, but its world-before-
battle tick order and non-firing patrol player are not the co-op integration path.
It cannot establish that boundary by itself. This section records the original
investigation requirements; the implementation and newer verification evidence
are recorded above, and do not yet prove complete online gameplay determinism.
