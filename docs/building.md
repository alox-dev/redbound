# Building

Redbound is built with CMake. The game itself (`app/`) and its libraries are the same on every platform; each
platform adds its own project or packaging under `platforms/`.

## Dependencies

| Dependency | How it is obtained |
| --- | --- |
| KA3D engine | `scripts/setup-ka3d.sh` clones https://github.com/pent0/ka3d into `third_party/ka3d` and applies `third_party/ka3d-patches/0001-linux-gcc-64bit.patch` |
| SDL2 | System package on Linux; `scripts/setup-sdl2.sh` downloads SDL2 2.30.9 into `third_party/SDL2` for Android |
| OpenGL ES 2, zlib | System libraries (Mesa on Linux, the NDK on Android) |
| Dear ImGui, dr_libs, stb, PVRTC decompressor, SDL_GameControllerDB | Bundled in `third_party/` |

## Linux

Requirements: CMake 3.16 or newer, GCC or Clang with C++17, Ninja (optional), git, SDL2, OpenGL ES 2 and zlib
development packages (plus ImageMagick for icons and desktop-file-utils for the AppImage). On Debian and Ubuntu:

```bash
sudo apt install build-essential cmake ninja-build git libsdl2-dev libgles2-mesa-dev zlib1g-dev imagemagick desktop-file-utils
```

Build, test and run:

```bash
./scripts/setup-ka3d.sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/app/redbound
```

`platforms/linux/redbound.desktop` is the desktop entry for packaging.

### CMake options

| Option | Default | Effect |
| --- | --- | --- |
| `REDBOUND_VIEWER` | `ON` | Build the game (requires SDL2 and OpenGL ES 2) |
| `REDBOUND_TOOLS` | `ON` (`OFF` on Android) | Build `hgr-dump`, `pvr2png` and the unit tests |
| `REDBOUND_ASAN` | `OFF` | Build with AddressSanitizer |

## Windows

Windows builds are cross compiled from Linux with MinGW-w64. Graphics use OpenGL ES 2 through ANGLE, which
translates it to Direct3D 11, so the game runs on any GPU with Windows 10 or 11 drivers (NVIDIA, AMD or Intel,
including integrated graphics) instead of depending on each vendor's OpenGL ES support.

Requirements on Debian and Ubuntu:

```bash
sudo apt install mingw-w64 libz-mingw-w64-dev zip imagemagick
```

`scripts/setup-windows.sh` downloads the Windows dependencies into `third_party/windows/` (ignored by git): SDL2
2.30.9 for MinGW, the ANGLE DLLs (`libEGL.dll`, `libGLESv2.dll`, `d3dcompiler_47.dll`, taken from the Electron
33.2.1 release, which ships them prebuilt) and the Khronos OpenGL ES 2 headers. `cmake/mingw-w64.cmake` is the
toolchain file. The C and C++ runtimes and zlib are linked statically; KA3D uses its portable GCC code path on
MinGW.

## Android

The Gradle project in `platforms/android/` (Gradle 8.7, Android Gradle plugin 8.5.2; Android Studio is not needed)
compiles the root `CMakeLists.txt` into `libmain.so` together with SDL2. Minimum Android 11 (API 30), `arm64-v8a`,
application id `io.github.alox_dev.redbound`.

Requirements: JDK 17 or 21 (newer JDKs are not supported by the Android Gradle plugin yet), Android SDK with
platform-tools, `platforms;android-34`, `build-tools;34.0.0`, `ndk;26.1.10909125` and `cmake;3.22.1`. With the
command line tools:

```bash
sdkmanager "platform-tools" "platforms;android-34" "build-tools;34.0.0" "ndk;26.1.10909125" "cmake;3.22.1"
```

Build and install:

```bash
./scripts/setup-ka3d.sh
./scripts/setup-sdl2.sh
cd platforms/android
echo "sdk.dir=$ANDROID_HOME" > local.properties
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb logcat -s Redbound SDL SDL/APP
```

The APK bundles `assets/lang` and SDL_GameControllerDB. The Java side is SDL's `SDLActivity` plus
`RedboundActivity`, which adds the document picker used to import the `.deb`.

## Release builds

Versions follow semantic versioning, `MAJOR.MINOR.PATCH` (current: `1.0.0`). The number comes from
`project(... VERSION ...)` in `CMakeLists.txt` and `versionName` in `platforms/android/app/build.gradle`; both must be
updated together, and `versionCode` increased for every Android release. Artifacts are named
`redbound-<version>-<platform>-<arch>`.

### Linux (AppImage)

```bash
sudo apt install desktop-file-utils
./scripts/package-linux.sh
```

Builds in Release mode into `build-release/` and creates `dist/redbound-<version>-linux-x86_64.AppImage`, a single
executable file that runs on most distributions. It bundles the game, SDL2, `assets/lang`, `gamecontrollerdb.txt`,
the desktop entry and the icon (the local one when present); OpenGL ES 2 / EGL and zlib come from the system, as GPU
drivers must. The script downloads appimagetool 1.9.0 into `dist/tools/` and verifies its SHA-256 before using it.

Layout inside the AppImage: `AppRun` sets `LD_LIBRARY_PATH` to `usr/lib` and starts `usr/bin/redbound`, which finds
its assets next to itself (`usr/bin/assets/lang`, `usr/bin/gamecontrollerdb.txt`).

### Android

Create a signing key once and keep it outside the repository (losing it means future APKs cannot update installed
ones):

```bash
mkdir -p ~/keys
keytool -genkeypair -v -keystore ~/keys/redbound-release.jks -alias redbound -keyalg RSA -keysize 4096 -validity 10000
```

Copy `platforms/android/keystore.properties.example` to `platforms/android/keystore.properties` (ignored by git) and
fill in the path and passwords. Then:

```bash
cd platforms/android
./gradlew assembleRelease
```

The signed APK is `app/build/outputs/apk/release/redbound-<version>-android-arm64.apk`. Without
`keystore.properties` the release build falls back to the debug key.

### Windows

```bash
./scripts/package-windows.sh
```

Creates `dist/redbound-<version>-windows-x86_64.zip` with `redbound.exe` (a GUI application with the icon embedded,
the local one when present), `SDL2.dll`, the ANGLE DLLs, `assets/lang`, `gamecontrollerdb.txt`, the license, the
ANGLE and Chromium licenses and a short README. Settings and imported data live in `%APPDATA%\redbound`. If ANGLE
cannot start on a machine, setting the environment variable `SDL_OPENGL_ES_DRIVER=0` makes SDL use the GPU driver's
native OpenGL ES support instead.

## App icons

The default icons live in `platforms/android/app/src/main/res-icon/` and `platforms/linux/redbound.png`.
`scripts/make-icons.sh [image.png]` (requires ImageMagick; default source `assets/icons/icon.png`) generates local
icons, ignored by git:
Android launcher icons (`platforms/android/app/src/main/res-icon-local/`), a 256 px PNG for Linux
(`platforms/linux/icon-local/`), a multi-size `.ico` for Windows (`platforms/windows/icon-local/`) and a 256x256
JPEG for Switch (`platforms/switch/icon-local/`). Builds and packaging scripts use the local icons when present.

## Data locations

| | Linux | Android |
| --- | --- | --- |
| Imported game data | `$XDG_DATA_HOME/redbound` (`~/.local/share/redbound`); Windows: `%APPDATA%\redbound` | app internal storage |
| Settings (`config.ini`) | `$XDG_CONFIG_HOME/redbound` (`~/.config/redbound`); Windows: `%APPDATA%\redbound` | app internal storage |
| Level 2 records | next to the game data (`level2.scores`) | app internal storage |
| Log (`redbound.log`, previous run in `redbound.previous.log`) | next to the game data; Windows: `%APPDATA%\redbound` | `Android/data/io.github.alox_dev.redbound/files` (reachable over USB) |
| Extra languages, controller mappings | `~/.config/redbound/lang/`, `~/.config/redbound/gamecontrollerdb.txt` | bundled |

## Supported game packages

The importer computes the SHA-256 of the chosen `.deb` and warns when it is not a known version (the import still
proceeds). Known versions are listed in `supportedPackageHashes()` in `port/launcher/DebImport.cpp`:

| Package | SHA-256 |
| --- | --- |
| `Bounce_Evolution.deb` (Nokia N900) | `09e0b3abc8c422ffaa3a2fca7e796749d6ad6797137b840d84e28938c6401ab4` |
