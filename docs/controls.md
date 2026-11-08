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

The touch scheme is chosen in the port menu (Controls > Touch screen), with the modern scheme by default:

- **Modern**: a finger landing on the left half is a virtual stick for movement (it starts where the finger lands:
  radius 8 % of the screen height, 12 % dead zone); a finger on the right half drags the view directly, like the
  camera of mobile open world games (a swipe across the whole screen width turns the camera half a turn, scaled by
  the camera sensitivity, smoothed over about 50 ms). While moving without touching the view for one second, the
  camera turns gradually behind the direction of movement (up to 1.2 rad/s), never swinging around when Bounce moves
  toward the camera.
- **Classic** (in the style of Bounce Touch): one finger anywhere swipes to move relative to the camera (a quick
  flick released within 220 ms keeps Bounce sliding for 0.35 s); holding it still for 200 ms rotates the camera
  toward that side of the screen, easing in and out (up to 1.8 rad/s, scaled by the camera sensitivity).
- In both, a **double tap** (two short taps within 350 ms) jumps, and the top left corner pauses.
- Touches are checked against the fingers SDL reports on the screen every frame, so a finger lifted while a menu was
  open never stays active, and switching to the accelerometer clears every touch state.

## Accelerometer

Chosen as the touch scheme in the port menu. While it is active the screen orientation is locked, so Android does not
flip the screen (and the tilt axes) in the middle of a level. Tilting moves Bounce like on the N900 (full input at about 20 degrees,
adjustable, with dead zones of 0.14 sideways and 0.08 forward; each axis can be inverted) and a sharp shake (more than
12 m/s² away from the smoothed gravity, at most every 0.7 s) jumps; tilting, even quickly, does not. The tilt is measured as the real angle of the phone around each screen axis (from the
normalized gravity, with `atan2`), relative to the neutral position, so it responds the same forward and backward
however upright the phone is held; measuring the gravity component instead (the sine of the angle) flattens out near
vertical and made reversing almost impossible. The smoothing starts from the first real reading. The neutral position is taken when the scheme is chosen,
when Bounce becomes controllable and when the game resumes, and can be taken by hand.

As in the original, the camera is not controlled by the player in this scheme: where the level defines a camera guide
(`setguidingobject`, see `gameplay.md`), the camera follows it; elsewhere it follows the path (default) or stays
fixed. Tilting is always relative to where the camera looks.

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
