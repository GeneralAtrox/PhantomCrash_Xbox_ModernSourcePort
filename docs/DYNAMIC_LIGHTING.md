# Optional dynamic lighting

Settings > Visuals > Dynamic lighting and shadows enables the native spotlight
pass. It defaults off, including for older settings files. The modeless Visuals
panel also contains the existing post effects, SMAA and display/frame-rate
controls. Changes affect the next rendered frame and save immediately without
saving the current match. The panel remains open for comparison; the game
continues rendering and simulating while it has focus.

The preference is `post_process.dynamic_lighting` in the desktop settings file.
The group retains its existing name for compatibility with the other rendering
preferences. Alt+Enter and Alt+Numpad Enter both toggle fullscreen; F11 remains
available in the game window.

## Light sources

`data/world/dynamic_lighting.json` contains port-authored enhancements, not
recovered Xbox local-light values. `mods/data/world/dynamic_lighting.json` can
override them. Night-stage entrances have fixed overhead spotlights.

The night models of both ships carry searchlight beam meshes, and their lamps
are placed in those beams. Each beam's apex, axis and spread were measured
from the beam geometry.

- **Video ship** (`models/binlink/211.gltf`). Meshes 0–3 are four narrow
  beams. Each is 128 units long, widens to a disc 8.8 across, and leans about
  20° off vertical. Each beam turns about its own origin at a rotor pivot
  (`tables/arena_monitor.json` spinners). The original translates it to the
  pivot and then turns it (0x06a5ac). The port used to turn it *about* the
  pivot, which made the beams orbit; that is fixed. Each lamp starts 22.5
  units down its beam, below the hull: inside the hull, the hull would shadow
  its own lamp.
- **Supply carrier** (`208.gltf`). Its spinner, mesh 5, holds a lamp and one
  beam tilted 37° off vertical, whose glow fans about 33° each side. The
  light's cone is 14°, so its pool stays near the carrier instead of covering
  the street. The lamp sweeps the ground as the spinner turns, and goes out
  when the carrier is shot down.
- The converted model is mirrored in Z when it loads (`world_model.cpp`), so a
  beam measured from the raw glTF has its Z negated in the data. Without this,
  the carrier's lamp leaned backward while its beam leaned forward.
- **Day models** (210, 207) have no beams and get no lamps.

Data keys in `data/world/dynamic_lighting.json`:

- `stages` keys are scene-stage numbers, with world-space positions/directions.
- `ship` (video ship) and `supply` (carrier) keys are model parity (0 day,
  1 night). Positions and directions are in model space. With `mesh` they are
  in that mesh's frame and follow its hinge or spin.
- `near` is the shadow map's near plane (default 0.5). Lights thrown hundreds
  of units need a farther near plane, or the fixed depth bias lifts robots'
  shadows off the ground.
- Order: stage lights first, then the video ship's, then the carrier's. Lights
  past the eighth are dropped; on scene stage 7 that is the carrier's lamp.
- `colour` is linear RGB, `intensity` a multiplier, and `range` world units.
- `inner_degrees`/`outer_degrees` are cone half-angles, with a smooth edge.
- Range attenuation falls to zero at `range`.
- `bar` (optional) makes a spotlight a strip lamp: half its length, from
  `position` to one end. Surfaces and air take their light from the strip's
  nearest point, so the pool and beam are as wide as the strip; the shadow map
  is seen from behind the strip (the cone that holds its whole beam), with its
  near plane just past it. Stage 1's entrance lamps are the rows of three
  panels across each gate's top, placed 0.6 units in front of the panels and
  aimed at the pools the earlier point lamps lit.
- `light_pools` keys are scene-stage numbers, each listing the base texture of
  the original's painted entrance light pools (blended, unlit stage layers that
  draw a white radial sprite). While the option is on those materials are not
  drawn; the entrance spotlights, aimed at each pool's centre with a cone sized
  to it, light the ground and robots and cast shadows instead. Other materials
  that share the same texture atlas keep drawing.

The defaults add lights to night stages only. Enabling the option does not
replace the original ambient/directional lighting or brighten the entire sky.

## Point lights

Battle effects also light their surroundings while the option is on (a port
addition; `point_lights` in the same file, `port/src/desktop_point_lights.cpp`):

- **Glowing effects.** Every playing effect with additive particles or flashes
  glows (`port/src/effect_glow.cpp`). Its energy sums each live particle's
  alpha × brightest colour channel × half-size, plus each flash's alpha × scale
  × growth. The light sits above the glow's weighted centre, in the particles'
  average colour. Intensity and range grow with the square root of the energy.
  Measured energies: a burning wreck 17–47 over its 6.45 s, a blast about 11
  for half a second, a bullet impact about 1 (below `min_energy`, so unlit).
  `group_scales` reweights or silences an effect group.
- **Blast fireballs.** Each armory explosion flashes. Its range scales with the
  blast radius, and it fades (squared) over the blast's life.
- **Weapons**, keyed by HUD kind:
  - a short muzzle flash where each round leaves (`muzzle`);
  - a light riding missiles and energy shots in flight (`projectile`);
  - lights spaced along a held beam (`beam`).
- **Budget.** Up to 32 unshadowed point lights reach the shader. Each frame
  keeps the `max_lights` that light most near the camera
  (intensity × r² / (r² + d²)). `day_scale` dims them on day stages.
- **Shadows.** While the stage and ship spotlights leave shadow maps free, the
  `shadowed` strongest fires and blasts cast shadows instead, as a downward
  spot `shadow_lift` above them. Shinjuku night's lamps and ships fill all
  eight maps, so there its fires stay unshadowed.

Point lights use the same falloff as the spotlights, (1 − d/range)² × Lambert,
with no cone and no shadow lookup. Their cost is per pixel only; they add no
geometry pass.

## Rendering and replay

Up to eight spotlights use 1024x1024 D32 shadow-map slices, with alpha-tested
cut-outs and two-sided solid casters. Robots, wrecks, doors and buildings block
the light. Sky, monitor faces, particles, transparent glow layers, optical
camouflage and flat-shadow decals do not cast these shadows. Local lighting
uses surface normals/normal maps and distance/cone attenuation; shadow lookup
uses a filtered 3x3 comparison kernel and depth bias.

The main view and its live monitor feed share the maps, generated before either
view. World-space lights are recorded with replay frames, so replay cameras and
live toggling use the recorded moving-light positions. Cockpit/HUD and live TV
faces are not illuminated by the spotlight pass.

Each light's shadow map draws only the items whose bounding sphere can reach
its cone and range, so a narrow searchlight renders only what is under it.

Disabled means no shadow-map draws; maps allocate lazily on first enable.
Allocation/shader failures log and disable the pass until the option is changed.
The fixed budget bounds cost but this is an additional geometry pass per light,
so enabling it can reduce frame rate. Light seen in the air is the separate
pass below.

## Volumetric light

With dynamic lighting on, **Settings > Visuals > Light beams in the air**
(`post_process.volumetric_light`, on by default) shows the lights in the air:
the ships' searchlights and the entrance lamps as shafts, and a glow around
fires and blasts. It is a port addition, tuned by `volumetric` in
`data/world/dynamic_lighting.json` (`port/src/desktop_volumetric_lights.cpp`,
shader `desktop_volumetric_light_shader.hpp`). Only the stages listed under
`volumetric.stages` have air: the four night stages. Day stages stay clear.

- **March.** After the world and before the screen pass, each view ray at
  half resolution is clipped to every spotlight's range and cone, then
  sampled `steps` times inside the beam. Every sample tests that light's
  shadow map, so robots, wrecks and buildings cut the shaft. The
  `max_point_lights` point lights nearest the camera add `point_steps`
  unshadowed samples each. A per-pixel offset staggers the samples; the
  composite's depth-aware 4x4 taps smooth the result into the full frame
  without bleeding over the edges of what stands in front.
- **Look.** `density` sets how much light the air scatters per unit.
  Henyey-Greenstein scattering (`anisotropy`) makes a beam brightest when you
  look toward its lamp. The air thins above `height_base`, by e every
  `height_falloff` units, so the ships' beams strengthen toward the ground. A
  beam dims as 1/(1 + (d/fade)²) with distance from its lamp; `beam_fade` is
  the default fade, and a light's own `air_fade` overrides it (the ships'
  narrow searchlights carry 250 units, the supply carrier's 80, stage 1's
  and stage 5's (Aqualine night) gate strips 16, with `air` 0.7, so they read
  as a glow along the lamps rather than a cone filling the view when looking
  up at them).
- **Per light.** Each stage, ship or supply light's `air` scales how strongly
  it lights the air (default 1). `point_lights.air` sets it for every effect
  light, including fires and blasts promoted to shadowed spotlights.
- **Simplifications.** The air only adds light; it does not dim what lies
  behind it. Smoke does not catch or block the beams. The air is the same
  everywhere at a given height.
- **Cost.** One half-resolution pass plus a full-resolution composite; no
  extra shadow maps or geometry. At 2263x1272 on an RTX 5080 Laptop GPU it
  averaged 0.15–0.30 ms of GPU time on Umihotaru night (four ship
  searchlights, two entrance lamps, the supply carrier and fires). Clipping to
  each cone halved the cost against marching every light's whole range.

## Sun shadows

Unlike the rest of this page, sun shadows are always on. They replace the
original's stencil shadow volumes and the robots' flat ground shadows with one
4096x4096 D32 shadow map seen from the stage's sun. The map covers 110 world
units either side of a point 70 units ahead of the camera. Robots, buildings
and alpha-tested fences and lattices cast into it; translucent, additive,
optical-camouflage and sky items do not. The world shader removes the sun's
diffuse light and highlight where the map is blocked, darkened by the stage's
own shadow strength (`shadow_colour` alpha), so each stage keeps the original's
shadow depth.

Edges are soft (PCSS): a 16-sample blocker search, then a 32-sample filter whose
radius grows with the blocker's height above the receiver. A receiver-plane
depth slope keeps the wide filter from shadowing sloped ground.

`sun_shadows` in `data/world/dynamic_lighting.json` tunes it:
- `sun_degrees` is the sun's apparent size (default 3, a hazy sun six times the
  real one). 0 gives hard 3x3-filtered edges.
- `widest_blur` caps the soft edge, in world units.
- `resolution`, `half_width`, `ahead` and `sun_distance` place the map.
- `depth_bias` and `slope_depth_bias` keep surfaces from shadowing themselves.
- `debug` tints shadowed ground red.

`"enabled": false`, or any failure to create or draw the map, falls back to the
stencil shadows.

## Verification

Release build completed. Separate night-scene captures with the option off/on
confirmed local illumination on the robot and ground. A bounded native UI run
opened Visuals, toggled lighting on/off/on, saved the four existing effect
preferences, and switched fullscreen with Alt+keypad Enter and back with
Alt+Enter. Reload and replay playback/reset/return completed with lighting and
SMAA enabled and no shader/resource errors.

Volumetric light: captures of Umihotaru and Shinjuku night showed the ship
searchlights as shafts reaching down to their pools of light, cut by the
walls and fading toward the ship, and the entrance lamps as lit air around
the gate. A same-frame split comparison confirmed nothing changes away from
the lights except a faint haze. The cone clipping was checked against brute
force sampling of 20,000 random rays, including rays starting inside a cone,
with no lit sample left out. Turning the setting off removes the pass.

Point lights: a forced test fire on Shinjuku night lifted the ground around it
by about 47 levels of red with the option on, and nothing with it off. A
six-robot Rumbling run produced fireball, missile, muzzle and effect lights.
`effect_tests.cpp` covers the glow sums.
