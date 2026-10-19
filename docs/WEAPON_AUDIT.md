# Weapon audit

This audit compares each weapon class in `tables/parts.json` (76 arm and 69
shoulder records in the Europe baseline) with the port's handling of it.
Reach, lock and stun follow the original's rules (PART_RECORDS.md, "Reach
and locks"). A class is a record's kind (+0x1c) and flight (+0x1d).

## By class

| Class (kind/flight) | Records | Reach (units) | Port status |
| --- | --- | --- | --- |
| Cannon, pistol (0/0) | 23 arm, 17 shoulder | 53–370 | Straight round. It leaves at its start speed and speeds up to its top speed; it expires after its life. Arm rounds lock at 75 units, shoulder rounds at 125. Most arm cannons have robot hit 0: a hit on a robot shows no impact, as the record says. |
| Machine gun, shotgun (2/0) | 17 arm | 85–460 | As a cannon. Its rounds scatter within its spread, and one pull spends one round of ammunition (0x09b4ed). |
| Rocket, bazooka (0/2, 2/2) | 11 arm | 43–62 | Flight 2, so the port flies it as a missile without homing: it speeds up from a near standstill to its top speed. It explodes against surfaces and robots (case 2). |
| Missile (1/2) | 35 shoulder | 86–124 | Homes on its lock from its first tick (0x097d42 picks 0x097f1a when it has a lock and turns; it turns by its record's turn a frame, 2 frames a tick). A volley's rounds leave 10 frames apart. Aim mode 2 does not turn the shoulder toward the target. The missile alert and the locked-on chatter are ported. |
| Grenade (3/1) | 9 shoulder | 20–48 (nearest 0) | Lobbed 30 degrees above the aim, falling at 49 units/s² (0x097c30). The computer fires it within the original's estimate plus or minus its blast and 5. Record S36 is the blast that breakable objects use and is never fired. |
| Beam (5/0) | 7 arm, 8 shoulder | 45–60 | Holds one ray on the muzzle while the trigger is held and drains 2 charge a tick (0x09b9fe). Its tip runs out at its acceleration a frame and ends at its length (the top speed); a robot it reaches is hit and the ray starts again from the muzzle, and it flies on through walls. Beam resistance cuts the damage; the charge recharges (0x097785); the sound restarts every 9 s. |
| Melee (6/0) | 18 arm | 15 | Starts a strike (see Melee below). The armed blade hits the first robot of another team it comes within 1.2 radii of. It locks at 25 units, or 20 on a computer's robot, and holds within 90 degrees. The robot cries out when it strikes. |

Every record's fire sound (+0x21), look (+0x22), impact (+0x23) and hit
cases (+0x1f, +0x20) reach the port. Aim modes 0 and 1 aim the mount;
2 (missiles) and 4 (grenades) keep it fixed.

## Beam

`Armory::hold_beam` keeps one ray on the muzzle while the trigger is held
(0x09b9fe). Its length (+0x84) grows by the weapon's acceleration a frame
up to its top speed. The ray is a shot whose tip moves from where it was to
where it is. A robot the tip reaches takes the weapon's damage (robot hit 1
in 0x098eda: the impact, then the shot is cleared by 0x0982db). The next
tick 0x09b9fe starts the cleared shot again from the muzzle (0x098bb7), so
the tip has to run out to the robot again. A robot near the muzzle is hit
more often than a far one, while the charge lasts. A wall or a shut door
leaf takes the impact and the ray flies on (surface hit 5).
Held, a counter (+0x80) counts frames up to 8 (0x17beec) and the ray's
colour rises 0x20202020 a frame to white; let go, it counts down and the
ray is released at zero. It is drawn as model slot 0x3a5 (model 194,
mesh 16, a unit quad) stretched from the muzzle to the tip, 0.25 wide;
the port turns it about its length to face the camera.
`tables/effects.json` `beam` names the mesh, the width and the fade.

## Melee

A pull starts a strike (0x09c337) unless one is under way: the robot enters
mover state 9 and plays the arm's strike motion (0x09dd24: 0x17 left and
0x18 right, or 0x19 and 0x1a for a weapon record whose model, +8, is 7; ten
later in the air). The leg type's timing table gives the motion's frames
(0x22f4c8, copied to robot +0x2114..+0x2120 by 0x08ddb2): the blade is
armed (robot +0x25a5, phases 1 and 2 of 0x08fc1e) until the loop end, and
the strike ends at the last frame, standing or falling. While armed the
robot steps forward 0.25 units a frame (0x199fd4).

When the robot's lock has completed (0x0520f0 sets the lock's +0x19 once
the fire control's lock time runs out, and robot +0x25a8 to the target)
the strike lunges instead (0x091245): the robot turns toward the target by
up to 0.196 rad a tick (0.137 for a computer robot), moves the whole
distance in a tick but no faster than robot +0x25bc (the dash motion's
rate times 2, or 1.4 for a computer robot, units a frame; 0x075a36), not at
all while it faces more than 90 degrees away, and climbs a tenth of the
height to the target's starting height a frame within the jump's take-off
speed, gravity cancelled. The values are `movement.lunge_*` in
`training.json`. Each tick the
shot pinned to the weapon's node (0x098bb7) tests every robot of another
team (0x09c9c2): one whose hit centre lies within its radius times 1.2
(0x19cbe8) takes the weapon's damage (0x0988e1) and the blade disarms, so a
strike connects once. A hit on the striker breaks the strike off.
The port keeps the blade on the weapon's muzzle node, which the strike
motion now swings; the arm's aim drops for the strike so the motion shows.

## Homing lock bounds

Homing weapons now check the chip's box when retaining a candidate as well
as acquiring one. The angular hold cannot keep or finish a lock outside
that box. Leaving it clears the candidate and its acquisition timer;
returning requires the chip's lock delay again. Already launched missiles
retain their assigned target, as before.

## Reticle-aimed shoulder weapons

Shoulder aim modes 0 and 1 use the crosshair or tracked reticle direction
already evaluated by the combat pose (`0x098bb7`). Projectile launch now
preserves that direction for non-homing weapons, including the Catapult,
instead of redirecting the round toward a retained lock outside the sight.
Homing weapons keep their lock-directed launch.

## Muzzle flashes

The firing event now draws the original muzzle model. Handler `0x099d26`
uses model slot `0x39e`, and `0x09a0da` uses `0x39f`, both at scale 3.
These resolve to meshes 9 and 10 of the already converted binlink model
194. `data/world/muzzle_flashes.json` maps the weapon's `fire_sound`
selector to these models and exposes scale and display duration.

The native renderer holds the immediate draw for one 30 Hz battle tick,
with additive, unlit materials. The firing event captures the barrel's
position and direction before a multi-barrel weapon advances. S.L.A.I.
weapons with their own projectile routes retain their source effects.
The smoke path at `0x086bfa` is separate from these muzzle-model draws.

## What remains

- **Sights without a reticle.** Every robot locks in its chip's boxes
  (arm weapons the inner one, shoulder and melee weapons the outer one;
  0x05262d, 0x051e37, 0x0525b0). The original's lock pass (0x04e6b8) builds
  each robot's sight through that robot's own camera, and its reticle
  tracks off the sight (0x04f6a0). The port has cameras only for the
  screens it draws. A fixed view centres the boxes on the sight in that
  view. A computer or split-screen robot sees from its own eyes at
  `sight_fov_degrees`, and its boxes stay centred on the sight without
  tracking.
- **Range-limited lock.** A weapon whose reach is shorter than its lock range
  (beams 45–60 against 75 or 125, grenades) still locks at the full lock
  range, as in the original. The computer fires only within the reach.
