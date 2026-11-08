# Port menu

The port menu is an overlay drawn above the game with Dear ImGui (`port/overlay/`). It holds everything that is not
part of the original game, so the original menus stay untouched. It opens with Esc, the Back/View/- controller
button, a three finger tap or the Android Back button, and pauses the game while open.

## Sections

Every tab uses the same layout: titled sections with the option on the left and its control on the right, without
help texts.

| Section | Contents | Configuration keys |
| --- | --- | --- |
| Game | Game data status and folder, import another `.deb`, updates, level 2 records (view and clear), language | `general.language`, `general.check_updates` |
| Controls | Camera sensitivity and vertical inversion; controller dead zone, vibration and connected controllers; on touch devices, the experimental accelerometer and its options | `controls.*` |
| Graphics | Fullscreen, vertical sync, current resolution; render scale, FPS limit, FPS counter | `graphics.*` |
| Audio | General, music and sound effects, each with mute and volume | `audio.*` |
| Improvements | Show the whole level | `improvements.visible_sections` |
| About | Title, version, credits, community links with icons (Discord, GitHub, YouTube, Ko-fi), original game, libraries, license | |

On first run, without game data, the menu shows the import screen instead of the tabs: the title, one line, the
button to choose the `.deb` and the language.

The title image is `assets/title/title.png` (shipped next to the executable, or inside the APK). Images wider than
1100 px are halved when loaded, so the texture stays within the 2048 px limit of older GPUs; without the file the
name is shown as text.

## Keys on desktop

| Key | Action |
| --- | --- |
| F11 | Fullscreen on or off (saved as `graphics.fullscreen`) |
| F2 | Screenshot of the whole window, saved as `redbound-YYYYMMDD-HHMMSS.png` in the pictures folder: `Pictures/Redbound` (Linux, following `xdg-user-dir PICTURES`) or `%USERPROFILE%\Pictures\Redbound` (Windows) |

## Notifications

Short notices appear at the top right for a few seconds, stacked up to four, with an icon and a fade: a saved
screenshot, a controller connected or disconnected, a change of input
device (keyboard, keyboard and mouse, controller, touch screen, accelerometer; at most one every 1.5 s), cleared
records, accelerometer calibration and an available update. They are drawn after the F2 screenshot is taken, so they
never appear in screenshots, and are also written to the log.

## Updates

With `general.check_updates` on, the game checks once per launch, in the background, whether a newer release exists.
It requests `https://github.com/alox-dev/redbound/releases/latest` (headers only) and reads the version from the
redirect to `/releases/tag/vX.Y.Z`, which always points to the latest regular release (drafts and pre-releases are
skipped) and is not subject to the GitHub API rate limit. Versions are compared numerically (`1.10.0` is newer than
`1.9.0`). No data about the player or the device is sent beyond a normal web request. When a newer version exists, a
notice appears and the Updates section offers a Download button that opens the release page. Windows uses WinHTTP,
Linux `curl` (or `wget`), and Android the platform's HTTP client, which needs the `INTERNET` permission.

## Icons

The community buttons use icons from Font Awesome Free 6.7.2 (icons CC BY 4.0, font SIL OFL 1.1), subset to the four
glyphs in use: `assets/fonts/icons-brands.ttf` (Discord, GitHub, YouTube) and `assets/fonts/icons-solid.ttf` (the mug
used for Ko-fi, which has no Font Awesome logo, and the notification icons). Their code points are in
`port/overlay/Icons.h`. They are merged into the menu font at startup; the license is in
`assets/fonts/LICENSE-FontAwesome.txt`.

## Scrolling

The content scrolls by dragging anywhere on it, as in touch apps, also when the drag starts on a button or a row: once
the finger (or mouse) moves more than about 10 points vertically, the gesture becomes a scroll and the control under it
is released without being activated. Horizontal drags still move sliders. Releasing with speed keeps the content
moving with inertia until it slows down or reaches an end.

## Layout

Sizes are computed in physical millimeters from the display density (text about 3.4 mm high). On touch screens or
displays smaller than 9 inches the menu becomes compact: full screen, no title bar, touch targets of about 9 mm,
wide footer buttons, and content that scrolls by dragging. `REDBOUND_MENU_COMPACT=1` forces the compact layout. The
font is taken from the system (fontconfig on Linux, Roboto or Noto on Android) so that accents render;
`REDBOUND_FONT=file.ttf` overrides it.

## Configuration file

`config.ini` holds `key=value` lines:

| Key | Values |
| --- | --- |
| `general.language` | `auto` (system language) or a language code |
| `general.check_updates` | `1` / `0`: look for a newer release at startup (default on) |
| `graphics.vsync`, `graphics.fullscreen`, `graphics.show_fps` | `1` / `0` |
| `graphics.render_scale` | `0.5` to `1`: the scene is drawn at this fraction of the window size and stretched; the HUD and menus stay sharp. Shaders that need the render size (the water reflection's `TEXEL_OFFSET`) receive the size of the scaled image |
| `graphics.fps_limit` | `0` (unlimited), `30`, `60`, `120` |

The FPS counter draws the number with the game font and the "FPS" label with the menu font, because the original
font only has the digits, `.`, `/`, `:`, `m` and `s` used by the HUD.
| `audio.mute`, `audio.music_mute`, `audio.sounds_mute` | `1` / `0` (everything, music, sound effects) |
| `audio.volume`, `audio.music_volume`, `audio.sounds_volume` | `0` to `1` |
| `controls.camera_sensitivity` | `0.3` to `3` |
| `controls.invert_y` | `1` / `0` |
| `controls.deadzone` | `0.05` to `0.5` |
| `controls.touch` | `modern` (default), `classic`, `accelerometer` |
| `controls.tilt_sensitivity` | `0.4` to `2.5` |
| `controls.tilt_invert_x`, `controls.tilt_invert_y` | `1` / `0` |
| `controls.tilt_camera` | `follow`, `fixed` |
| `controls.vibration` | `1` / `0` |
| `improvements.visible_sections` | `1` / `0` |

## Languages

Included languages: English, Spanish, Portuguese, French and Italian. Texts live in `assets/lang/<code>.lang` as `key = text` lines (UTF-8; `\n` for line breaks; lines starting with `#`
are ignored). `assets/lang/languages` lists the available codes. The system language is detected with SDL; missing
keys fall back to English.

To add a language:

1. Copy `en.lang` to `<code>.lang` (for example `pt.lang`) and translate the texts, keeping the keys and any `%s` or
   `%d` placeholders.
2. Set `language.name` to the language's own name.
3. Add the code to `assets/lang/languages`.

Languages can also be tested without rebuilding by placing the file in `~/.config/redbound/lang/` together with a
`languages` file. The current font covers Latin scripts.
