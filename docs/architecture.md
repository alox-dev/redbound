# Architecture

## Layers

```
app/        the game: one LevelSession per level, input, menus, HUD
  |
port/       platform layer on top of KA3D
  game/       SceneWorld (actions that touch the scene), BallController (player, camera, rails, shadows)
  render/     GLES2 backend (KA3D gr::Context implementation), effect parser, PVR loader, water reflection
  audio/      SDL2 mixer with dr_wav / dr_mp3
  ui/         2D sprites and bitmap fonts from the original .dat sheets
  overlay/    port menu (Dear ImGui), config, languages, Android bridge
  launcher/   .deb importer
  |
game/       platform independent: Trigger interpreter, Actions, ball Physics, RailTrack (unit tested)
  |
engine/     KA3D (GPL v2) with third_party/ka3d-patches applied: scene graph, .hgr loader, math, particles

platforms/  per platform projects and packaging (android/, linux/)
```

CMake targets: `redbound_game` (core), `redbound_world` (port/game), `redbound_gles2`, `redbound_audio`,
`redbound_ui`, `redbound_overlay`, `redbound_launcher`, `ka3d_core`, and the game itself (`redbound` on desktop,
`main` as `libmain.so` on Android). C++ code lives in the `redbound` namespace.

## Program flow

`app/main.cpp` sets up the platform (on Android: logcat redirection and app storage), loads the config, parses the
command line, creates the SDL window and GLES2 context, initializes controllers and sensors (`Devices`) and the
port menu (`Overlay`). Without imported data it shows the import screen. Then it runs one `LevelSession` per level;
a session ends by quitting or by requesting another level (`--play level02.hgr`, `--menu level01.hgr`), which is
loaded in the same process with the same window and GL context.

`LevelSession::run`:

1. `loadScene`: GLES2 context, resource manager, loading screen, the `.hgr` scene, library objects
   (`load=` markers), fog, hidden helper meshes, cameras, background color, audio and ambient sounds.
2. `setupWorld`: `SceneWorld` with every trigger of the level, collectables, particles, sounds, vibration.
3. `setupPlayer` (levels, not the attraction demo): the ball model `bounce.hgr`, shadows, sphere triggers, ball
   parameters (`D_forminfo`), eye pose, rails and sky (level 2), boost particles, `T_restartLevel`.
4. Main loop: events (devices, port menu, touch gestures, pause, level menu, dev keys), update (particles, ambient
   sounds, results, triggers, player or free camera, boost particles), render (water reflection, sky, scene, fade,
   blurred capture behind menus, HUD, logo, level menu, port menu).

## Input

`PlayerInput` merges keyboard, numeric keypads (also phone keypads that send digits), game controllers and generic
joysticks (`Devices`, with SDL_GameControllerDB), touch gestures and the accelerometer into a `PlayerCommand`. Two
schemes: classic (tank: turn and move forward, camera behind Bounce) and modern (movement relative to the camera,
which the player rotates). The automatic mode follows the last used device: d-pad and keyboard select classic;
sticks, mouse camera drag, touch and tilt select modern. On level 2 only the sideways component is used.

Pause (the original level menu) opens with P, Start, the top left corner (touch or click), or any tap with the
accelerometer scheme; in the attraction demo any click opens it. Clicking outside the buttons resumes. The port
menu opens with Esc, Back/View/- or a three finger tap, and pauses the game while open.

## Audio

`port/audio/Audio.cpp` mixes WAV (dr_wav) and MP3 (dr_mp3) with SDL2. Level music comes from `music=` properties;
`S_*` nodes are ambient emitters with `inradius`/`outradius`/`volume`, attenuated by distance to the ball. Sounds
from actions (`playsound`, `stopsound`) and from the ball (jump, impacts) go through the same mixer.

## 2D interface

`port/ui/Ui.cpp` reads the original sprite sheets (`resources/images/*.dat` + `.png`) and bitmap fonts
(`resources/localization/FONT*.dat`), laid out on the original 800x480 coordinates and scaled to the window. The
level menu (`app/LevelMenu`), HUD and results (`app/Hud`) use the original sprite names (`MENU_LEVEL_1`,
`LEVEL_2_HUD_TIME`...). Level 2 records are stored in `level2.scores` next to the game data.

## Command line

```
redbound [--menu | --play] [level.hgr] [--import-deb file.deb] [--size WxH] [--dev]
         [--screenshot file.png] [--frames N] [--data DIR] [--camera NAME] [--var NAME=VALUE]
```

Without arguments the game starts from the imported data. `--dev` enables development keys: Tab cycles scene
cameras, F3 shows collision meshes, F4 toggles a free camera, F5 toggles water reflections, R respawns the ball and
F12 saves a screenshot.

## Environment variables

| Variable | Effect |
| --- | --- |
| `REDBOUND_TRACE=1` | Traces of triggers, collisions, rails and scene changes |
| `REDBOUND_NO_AUDIO=1` | Starts without audio |
| `REDBOUND_MENU_COMPACT=1` | Forces the compact (phone) layout of the port menu |
| `REDBOUND_FONT=file.ttf` | Font for the port menu |
| `REDBOUND_ORIGINAL_SECTIONS=1` | Set by the "level sections always visible" option when disabled |

Configuration keys are described in `port-menu.md`.
