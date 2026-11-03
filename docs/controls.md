# Controls

All devices are active at the same time; `app/PlayerInput.cpp` merges them into one command per frame.

## Schemes

| Scheme | Movement | Camera |
| --- | --- | --- |
| Classic (tank) | forward/back moves, left/right turns Bounce | follows behind Bounce |
| Modern | relative to the camera (up moves away from the camera, left goes to the left of the screen) | rotated by the player (yaw and limited pitch) |

Vertical limits of the modern camera: it looks down on Bounce from up to about 69 degrees above; it never goes lower
than 0.12 rad of elevation (so it stays above the ground), and pushing further tilts the view upward instead, up
to about 33 degrees above the horizon, to look at the sky.

`controls.scheme = auto` (default) follows the last used device: the d-pad, keyboard alone and hat switches select
classic; sticks, dragging the mouse with the right button, touch gestures and tilting select modern. When the scheme
changes, the camera keeps its direction. On level 2 only the sideways component steers.

## Devices

| Device | Move | Jump | Camera |
| --- | --- | --- | --- |
| Keyboard | W/S or arrows forward/back, A/D or arrows turn | Space | |
| Numeric keypad, phone keypads | 8/2 forward/back, 4/6 turn | 5, 0 | |
| Mouse | | | drag with the right button (switches to modern) |
| Game controller | d-pad (classic), left stick (modern) | A or B | right stick |
| Generic joystick | axes 0 and 1, hat | buttons 0 and 1 | |
| Touch gestures | swipe | double tap | hold one side of the screen |
| Accelerometer | tilt | shake | follows the path (or fixed) |

Game controllers are recognized through SDL_GameControllerDB (bundled), so Xbox and XInput, PlayStation, Switch Pro,
Joy-Con, 8BitDo and many generic controllers use the same layout. Controllers that are not in the database are read
as generic joysticks. Controllers connected before or during a level are picked up immediately.

## Pause and menus

| | Keyboard and mouse | Controller | Touch |
| --- | --- | --- | --- |
| Open the original pause menu | P, or click the top left corner (in the demo, any click) | Start / + | top left corner (gestures) or anywhere (accelerometer) |
| Navigate the original menus | arrows, Enter | d-pad, A selects, B goes back | tap |
| Resume | P, or click outside the buttons | Start / B | tap outside the buttons |
| Close the results card | Enter | A | tap |
| Open the port menu | Esc | Back / View / - | three finger tap, Android Back button |

## Touch gestures

Each half of the screen has its own job, decided by where a finger lands; both thumbs can be used at once.

- **Move half** (left by default): a virtual stick starts where the finger lands (radius: 8 % of the screen height,
  12 % dead zone). Bounce slides relative to the camera. A quick flick (released within 220 ms) keeps him sliding
  for 0.35 s.
- **Look half** (right by default): a second virtual stick for the camera, like a controller's right stick
  (radius: 10 % of the screen height, 15 % dead zone). Holding the finger away from where it landed rotates the
  camera continuously in that direction. The response is quadratic, so small offsets give fine, slow adjustments and
  only the edge of the stick reaches full speed: 1.5 rad/s sideways and 1.0 rad/s vertically, scaled by the camera
  sensitivity setting and eased in about 80 ms. Releasing the finger stops the camera.
- **Double tap**: two short taps within 350 ms, on either half, jump.
- **Swap sides**: `controls.touch_swap_sides` moves Bounce with the right half and looks with the left half.
- **Top left corner**: pauses (12 % of the width, 15 % of the height).

## Accelerometer

- Read through SDL's sensor API; SDL is told not to expose the accelerometer as a joystick.
- The neutral position is taken automatically when Bounce becomes controllable (end of the intro or the countdown)
  and when the game resumes, and can be taken by hand in the port menu.
- Full input at about 20 degrees of tilt (adjustable), with dead zones of 0.14 sideways and 0.08 forward; each axis
  can be inverted. Landscape and flipped landscape are handled.
- Shaking (a deviation of more than 7 m/s² from gravity) jumps, at most every 0.5 s. Tapping anywhere pauses.
- Camera: **follows the path** (default) or **fixed**. Following only applies while moving mostly forward (within
  about 45 degrees), with reduced strength while tilting sideways and a turn rate limit of 0.8 rad/s. This prevents a
  feedback loop (sideways push, camera follows, push changes direction) and makes a sustained sideways tilt turn
  gently.

## Vibration

The `pressure,N` action, used where the original N900 vibrated, vibrates the phone and rumbles controllers for 45 ms
per unit (between 30 and 250 ms). It can be turned off in the port menu.
