# Controls

Every input method is detected automatically; there is nothing to choose. The only manual option is the
accelerometer, which is experimental. `app/PlayerInput.cpp` merges all devices into one command per frame.

## Schemes

| Scheme | Movement | Camera |
| --- | --- | --- |
| Classic (tank) | forward/back moves, left/right turns Bounce | follows behind Bounce |
| Modern | relative to the camera (up moves away from the camera, left goes to the left of the screen) | rotated by the player (yaw and limited pitch) |

The scheme follows what is being used: the keyboard without the mouse, the d-pad and hat switches select classic;
the mouse, the sticks and touch select modern. When the scheme changes, the camera keeps its direction. On level 2
only the sideways component steers.

Vertical limits of the modern camera: it looks down on Bounce from up to about 69 degrees above; it never goes lower
than 0.12 rad of elevation (so it stays above the ground), and pushing further tilts the view upward instead, up
to about 33 degrees above the horizon, to look at the sky.

## Keyboard and mouse

- **Keyboard only**: W/S or the arrows move forward and back, A/D or the arrows turn (classic scheme). The numeric
  keypad and phone keypads use 8/2 and 4/6. Space, 5 or 0 jump.
- **Mouse**: moving the mouse during play captures the cursor (after about 24 pixels of travel, so a bump does not
  trigger it) and the mouse rotates the camera, without holding any button; W/A/S/D then move relative to the camera.
  The cursor is released whenever the game pauses, the port menu or the results card opens, or the window loses
  focus.

## Game controllers

The d-pad plays with the classic scheme; the left stick moves relative to the camera and the right stick rotates it.
A or B jump. In the original menus the d-pad and the left stick move between options, A selects and B goes back.
Controllers are recognized through SDL_GameControllerDB (bundled), so Xbox and XInput, PlayStation, Switch Pro,
Joy-Con, 8BitDo and many generic controllers share this layout; controllers missing from the database are read as
generic joysticks. Controllers connected before or during a level are picked up immediately.

## Touch

One adaptive scheme covers one handed and two handed play:

- **First finger**: swiping moves Bounce relative to the camera (a virtual stick that starts where the finger lands:
  radius 8 % of the screen height, 12 % dead zone; a quick flick released within 220 ms keeps him sliding for
  0.35 s). Holding the finger still for 200 ms rotates the camera toward that side of the screen, easing in and out
  (up to 1.8 rad/s, scaled by the camera sensitivity).
- **Second finger**, while the first is down: a virtual stick for the camera (radius 10 % of the screen height, 15 %
  dead zone, quadratic response up to 1.5 rad/s sideways and 1.0 rad/s vertically).
- **Double tap** with any finger (two short taps within 350 ms) jumps.

## Accelerometer (experimental)

Enabled by hand in the port menu. Tilting moves Bounce like on the N900 (full input at about 20 degrees, adjustable,
with dead zones of 0.14 sideways and 0.08 forward; each axis can be inverted) and shaking (more than 7 m/s² from
gravity) jumps. The neutral position is taken when Bounce becomes controllable and when the game resumes, and can be
taken by hand. The camera follows the path (default) or stays fixed. Its camera behavior is still being tuned.

## Pause and menus

| | Keyboard and mouse | Controller | Touch |
| --- | --- | --- | --- |
| Pause (original menu) | P, or click the top left corner | Start / + | Tap the top left corner (every scheme) |
| Navigate the original menus | arrows, Enter | d-pad or left stick, A selects, B goes back | tap |
| Resume | P, or click outside the buttons | Start / B | tap outside the buttons |
| Close the results card | Enter | A | tap |
| Port menu | Esc | Back / View / - | three finger tap, Android Back button |

## Vibration

The `pressure,N` action, used where the original N900 vibrated, vibrates the phone and rumbles controllers for 45 ms
per unit (between 30 and 250 ms). It can be turned off in the port menu.
