# Port menu

The port menu is an overlay drawn above the game with Dear ImGui (`port/overlay/`). It holds everything that is not
part of the original game, so the original menus stay untouched. It opens with Esc, the Back/View/- controller
button, a three finger tap or the Android Back button, and pauses the game while open.

## Sections

Every tab uses the same layout: titled sections with the option on the left and its control on the right, without
help texts.

| Section | Contents | Configuration keys |
| --- | --- | --- |
| Game | Game data status and folder, import another `.deb`, language | `general.language` |
| Controls | Camera sensitivity and vertical inversion; controller dead zone, vibration and connected controllers; on touch devices, the experimental accelerometer and its options | `controls.*` |
| Graphics | Fullscreen, vertical sync, current resolution | `graphics.vsync`, `graphics.fullscreen` |
| Audio | General, music and sound effects, each with mute and volume | `audio.*` |
| Improvements | Show the whole level | `improvements.visible_sections` |
| About | Title, version, credits, community links (Discord, GitHub, YouTube, Ko-fi), original game, libraries, license | |

On first run, without game data, the menu shows the import screen instead of the tabs: the title, one line, the
button to choose the `.deb` and the language.

The title image is `assets/title/title.png` (shipped next to the executable, or inside the APK). Images wider than
1100 px are halved when loaded, so the texture stays within the 2048 px limit of older GPUs; without the file the
name is shown as text.

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
| `graphics.vsync`, `graphics.fullscreen` | `1` / `0` |
| `audio.mute`, `audio.music_mute`, `audio.sounds_mute` | `1` / `0` (everything, music, sound effects) |
| `audio.volume`, `audio.music_volume`, `audio.sounds_volume` | `0` to `1` |
| `controls.camera_sensitivity` | `0.3` to `3` |
| `controls.invert_y` | `1` / `0` |
| `controls.deadzone` | `0.05` to `0.5` |
| `controls.accelerometer` | `1` / `0` (experimental, off by default) |
| `controls.tilt_sensitivity` | `0.4` to `2.5` |
| `controls.tilt_invert_x`, `controls.tilt_invert_y` | `1` / `0` |
| `controls.tilt_camera` | `follow`, `fixed` |
| `controls.vibration` | `1` / `0` |
| `improvements.visible_sections` | `1` / `0` |

## Languages

Texts live in `assets/lang/<code>.lang` as `key = text` lines (UTF-8; `\n` for line breaks; lines starting with `#`
are ignored). `assets/lang/languages` lists the available codes. The system language is detected with SDL; missing
keys fall back to English.

To add a language:

1. Copy `en.lang` to `<code>.lang` (for example `pt.lang`) and translate the texts, keeping the keys and any `%s` or
   `%d` placeholders.
2. Set `language.name` to the language's own name.
3. Add the code to `assets/lang/languages`.

Languages can also be tested without rebuilding by placing the file in `~/.config/redbound/lang/` together with a
`languages` file. The current font covers Latin scripts.
