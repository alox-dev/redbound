# Gameplay

## Ball

The ball model is `resources/models/bounce.hgr`. Its parameters come from `D_forminfo` (read like
`PlayerBall::load`):

| Field | Value | Meaning | Variable |
| --- | --- | --- | --- |
| 6 | 50 | radius | |
| 7 | 1 | mass | `ballmass` |
| 8 | 1750 | movement force | `ballmovementforce` |
| 9 | 800 | jump force | `balljumpforce` |
| 10 | 350 | friction | `ballfriction` |
| 11 | 0.4 | sliding friction | `ballfrictionslide` |
| 12 | 800 | max speed (level 1 sets 600) | `ballmaxspeed` |
| 13 | 0.1 | rebound | `ballrebound` |

Gravity is 2000 units/s² along `(0,-1,0)` (`Game::initializeLevel`), re-read every frame from `gravity`. Forces
follow `ActiveGameObject::updateForces` (gravity times mass plus movement force, a = F / m), with no air control.
`ballmaxspeed` limits horizontal speed only (falls keep accelerating). A jump is allowed up to 0.5 s after the last
contact; it cancels the vertical speed and adds `balljumpforce / mass` upwards with a 0.85 multiplier (the original
multiplier was not recovered; calibrated against video of the original).

Collision (`game/src/Physics.cpp`): triangles in a uniform grid, sphere against triangle on both faces, with sub
steps so the ball never moves more than 0.4 radius per step. Meshes with the sensor bit are passable but report
contact for `executetrigger`.

## Presentation

- The body (`ballform_bounce_body_`) rolls; the eyes group (`G_bounce_lookat_eyes`) rotates around the ball center
  to look where Bounce moves (towards the camera during cutscenes), with the brows kept up. Its rest pose is the
  first frame of its animation, lowered by 12 % of the eye distance to match the original.
- The ball shadow is `shadow_bounce` projected on the ground below the ball (along the track's down direction on
  rails), tilted with the slope. Apple shadows are projected the same way.
- The camera chases Bounce from above, like the screenshots of the original; in the modern scheme the player
  rotates it (yaw and pitch, limited) and it follows more tightly (position smoothing rate 9/s instead of 3/s), so it
  settles about 250 ms after the input stops.
- The butterfly is animated during gameplay too, and the eyes are always depth tested (fixes over the original).
- "Level sections always visible" (default on): section unlinks from `T_link_*`, `T_unlink_*` and `T_checkpoint*`
  are ignored; the N900 hid far sections to save memory.

## Level 1

Collect 14 apples (`item=collectable` library objects). Cutscenes switch cameras (`setcamera`) and disable control.
The log puzzle, apple tree drop and checkpoints are pure level logic. The results card shows the apple count.

## Level 2 (arcade)

The track is ten rails `L_hillsrail01..10` (a closed circuit of about 3555 m). Each rail is a `Lines` node with a
`track` polyline and a `tracknormal` polyline (the track's up vector); `D_hillsrailNN` holds `next`, `deathplane`
and `tracknormalgravity=1`. Only rails with `tracknormalgravity=1` use rail mode, so level 1's guide line keeps
working as before.

- `RailTrack` chains the segments by connectivity (reversing reversed ones), keeps disconnected pieces apart, and
  finds the ball's position with a local search around its last position (loops pass close to themselves).
- Gravity follows the track normal; the ball advances on its own toward the level speed (3000, 4500 after a boost;
  scaled by 1.15 in the port), with no rolling friction; the player only steers sideways.
- Boost arrows (`T_boost`): speed multiplied up to the boost max speed, then decaying (`T_boostvariable` lowers
  `maxspeed_variable` by `_dt`); blue particles follow the ball. Slow zones apply `speedmul,0.95` while touched
  (`speed01` is the ball speed).
- Side barriers: a strong sideways impact leaves 15 % of the forward speed.
- While airborne, velocity away from the track is damped strongly, so the ball follows crests.
- Falling: more than 0.7 s airborne and more than 1200 from the track (or more than 6000 at any time) activates the
  rail's `T_deathPlaneNN`, which fades and respawns at the start of the rail. The level 1 kill height does not
  apply on rails (the track goes below it).
- Time arcs (`T_timercheckpoint01..04`) add time to `timer01`; `T_timer` ends the run. Distance stops counting at
  the end, and the results show the distance and the top three records.
- The rail camera sits behind and above Bounce (height 158, distance 465) and rolls with the track.
- The sky (`skybox=1`) is drawn separately before the scene, unlit, at infinity and without writing depth.

## Camera guides

A level can define camera guides: `L_` nodes with three line children, `track`, `cameraposition` and
`cameralookat` (level 1 has `L_bmmbh`). The action `setguidingobject,<node>` makes one the active guide; naming a node
without camera lines (level 1 uses `O_guidingobject`) turns it off. While a guide is active and the touch scheme is the
accelerometer, the camera finds Bounce's closest point on `track` (searching near the previous one, so loops and
crossings do not jump), takes the same fraction of the length along `cameraposition` and `cameralookat`, and uses the
direction from that camera point to that look point: its heading and its downward angle (clamped between 0.15 and
0.75 rad), both eased at 3/s. The camera is then placed behind Bounce at the usual chase distance along that
direction, so it keeps the original framing angles without sitting as far and high as the guide's own points.
The other schemes keep the player controlled camera.

## Camera collisions

Every chase camera (player controlled or guided) checks the line from Bounce to its desired position with three
parallel rays through the solid collision meshes (center, and 1.2 ball radii to each side). Only an obstacle that
blocks at least two of them counts, so thin things such as tree trunks do not move the camera. Only steep surfaces
(walls, the sides of rocks and buildings) count: a surface facing up or down (ground, slopes, a ledge Bounce stands
on) never pulls the camera in, so when the camera is below Bounce it keeps its distance, as in modern third person
games. When an obstacle does count, the camera moves in front of it (keeping at least 3 ball radii) with a quick but
smooth transition.

The ground is not treated as an obstacle: terrain meshes (`ground*`) are left out of those rays, and instead the camera
stays at least 1.5 ball radii above the ground under it, rising only as much as needed. This keeps it outside the
island when Bounce is at the shore, and makes it stop smoothly over the ground when pulled down. Also ignored are the
invisible boundary walls (`wall*`) and the meshes that only exist to fire a trigger (with `executetrigger`, such as
`watercollision`). Water is not a collision mesh, so the water surfaces found by the reflection (their height and
extent) are checked too: the camera never goes below a water surface, also after Bounce falls in.

## Eye animations

The ball model (`resources/models/bounce.hgr`) has its own triggers for the eyes: `T_bounce_lookat_eyes_anim01`
waits a random time (`random,bounceBlinkDelay,0.05,2`) and plays the eyeball and pupil animations frame range by frame
range, which is the blink, and `T_bounce_lookat_eyes_anim02` loops the eyebrows. They are added to the level's trigger
world when the ball loads and started right away; the eye look-at itself is done by the port. Nodes added to the scene
after the world was created, such as the ball's, are looked up in the scene when they are not in the world's index.
Animations that come from the ball model blend from the node's current pose over 0.12 s, so the randomly chosen
pupil animations never snap; level animations, such as cutscene cameras, still start exactly on their first frame.
