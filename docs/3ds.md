# Nintendo 3DS

The 3DS port lives on the `feature/3ds` branch. Level 1 is playable on a New 3DS: the ball, its
physics and rails, apples with their shadows and pickup effects, triggers and animations, water
reflections, particles and fog. There is no audio, no HUD, no menus and no `.deb` importer on the
console yet. The executable is still a probe with a debug menu on the bottom screen.

Target: New 3DS and New 2DS XL. The Old 3DS is kept reachable (every expensive feature can be
switched off and the model is detected at runtime), but it is not the goal.

## Shared level logic

`app/LevelCore` holds everything about a level that does not depend on SDL2 or OpenGL: loading the
scene and library objects, fog, the trigger world, the ball, object shadows, rails, particles, the
game, free and debug cameras, and the level completion timer. Input arrives as plain data
(`LevelInput`, `PlayerCommand`) and sounds and rumble leave through callbacks.

The desktop `LevelSession` and the 3DS probe both drive `LevelCore`, so gameplay is the same code on
every platform. Water surface detection is shared too, in `port/game/WaterSurfaces`.

## Why the renderer is different

The PICA200 has programmable vertex shaders only, written in its own assembly language; it cannot
run GLSL. Its fragment stage is fixed function: six configurable TEV combiner stages. So
`port/render/gles2`, which compiles Rovio's `.fx` effects to GLSL ES, cannot be reused.

`port/render/citro3d` implements the same `gr::Context` interface:

| Piece                                                                   | Files               |
| ----------------------------------------------------------------------- | ------------------- |
| Context, shader, primitive, texture and render targets on citro3d        | `Citro3D.cpp`       |
| Render states of each `.fx` pass                                         | `PassRecipe.cpp`    |
| Power of two resizing, mipmaps and PICA tiling for textures              | `TextureTiling.cpp` |
| Vertex shader: transform, lighting, specular, fog, projected reflections | `scene.v.pica`      |

`PassRecipe` and `TextureTiling` have no console dependency and are covered by `test_citro3d_core`,
which runs in the normal desktop test suite.

### Matching the original effects

The `.fx` files are parsed by the shared `EffectParser`. Rovio's pixel shaders all include a common
file that declares every sampler, so the shader text says nothing about what a pass really does.
The renderer follows the effect `#define`s instead, as the GLSL preprocessor would:

| Define                        | Effect on 3DS                                                     |
| ----------------------------- | ----------------------------------------------------------------- |
| `ENABLE_LIGHTMAP`             | Lightmap applied as `color + (SIMAP - 0.5) * 2`, with two signed add stages |
| `DISABLE_LIGHTING`            | Unlit: the texture is not multiplied by `DIFFUSEC`                |
| `MULTIPLY_BY_DIFFUSEC`        | `DIFFUSEC` multiplies colour and alpha                            |
| `DISABLE_AMBIENT`, `_DIFFUSE`, `_SPECULAR` | Parts of the lit model switched off                  |
| `ENABLE_SHININESS_MAP`        | Highlight strength from the mean of the shininess map             |
| `ENABLE_ALPHA_TEST`           | Alpha test. No shipped effect defines it, so nothing is clipped   |
| `ENABLE_MOVEUV`               | `TEX0_OFFSET` scrolls the texture                                 |
| `DISABLE_TRANSFORM`           | Positions are already projected (particles)                       |
| `ENABLE_VERTEXCOLOR`          | Vertex colour is used; otherwise it is ignored                    |
| `DISABLE_FOG`                 | No fog (sky and sprites)                                          |

Lit effects (`diff-tex*`) compute `texture * (AMBIENTC + N.L * light colour)` per vertex, and the
vertex shader also writes a Blinn-Phong term `pow(N.H, GLOSSINESS)` to the primary alpha, which a
combiner stage adds as `SPECULARC * shininess`. Most materials carry `DIFFUSEC = 0.588`, the 3ds Max
default; the original ignores it outside `MULTIPLY_BY_DIFFUSEC`, and so does the port.

### Textures

The PICA samples textures with V reversed relative to OpenGL. Every texture is flipped at load, as
the desktop renderer ignores the PVR flip flag; lightmap atlases and path transition textures are
where a mistake shows. Textures are capped at 256 pixels on New 3DS and 128 on Old 3DS, tiled in
Morton order, stored as RGB565 or RGBA8, and cached on the SD card in `sdmc:/3ds/redbound/cache`.
Sky textures are clamped to the edge, which removes the seams between skybox faces.

### Water

Water materials have no base texture, only `REFLMAP`, which the game renders every frame from a
camera mirrored across the water plane. On 3DS the mirrored pass renders the `Reflected` technique
into a 128x256 texture, and the water samples it projectively through `texcoord0w`. The combiner
reproduces `REFLMAP^2 + DIFFUSEC`. The original offsets the lookup per pixel with 30 animated
normal maps, which the PICA cannot do; instead the red channel of the same frames modulates the
reflection brightness at 15 frames per second. The reflection is not clipped against the water
plane yet.

### Particles and dynamic data

KA3D projects particles on the CPU with `projectionTransform()`. To keep that matrix standard, the
quarter turn of the top screen is applied at the end of the vertex shader rather than in the
projection. Primitives uploaded more than once in a frame (particles, sorted transparent indices)
rotate through up to three buffers, so the reflection pass and both stereo eyes keep their own
data until the GPU has used it. Every particle system is loaded with the level to avoid stalls on
the SD card during play.

### Fog

The original fog is linear in view distance. The PICA fog table is indexed by depth, and with the
game camera range (1 to 100000) the whole fog would fall into its first entry, so the vertex shader
computes the factor instead and writes it to `texcoord2`. The last combiner stage blends towards the
level fog colour through a 256 texel ramp. The strength defaults to 35 percent, which matched the
Android build best on hardware.

### Display modes

| Mode         | Notes                                                                          |
| ------------ | ------------------------------------------------------------------------------ |
| 400px        | Default                                                                        |
| 800px wide   | Twice the horizontal resolution, no 3D. Not supported on the Old 2DS           |
| 3D           | Off axis stereo following the slider; one eye is drawn when the slider is down |
| 3D swapped   | Same with the eyes exchanged                                                   |

Eye separation is 14 world units with the zero parallax plane at 450, about where the ball sits.
Water samples the shared reflection with the opposite eye shift. `gfxSet3D` and `gfxSetWide`
write the same libctru variable, so only the call that matches the mode is made.

### Orientation and depth

The top screen is physically rotated, so its render target is 240x400 and the vertex shader turns
the image a quarter. The PICA expects a depth range of -1 to 0 with an inverted test, so KA3D's
Direct3D style projection has its `w` row subtracted and every comparison is flipped. Culling is
inverted relative to the desktop.

## Building

Requires devkitPro with the `3ds-dev` group (devkitARM, libctru, citro3d, picasso, 3ds-tools) and
`3ds-zlib`. In every new terminal:

```
source /etc/profile.d/devkit-env.sh
```

Then:

```
./scripts/setup-ka3d.sh
cmake -S . -B build-3ds -G Ninja -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake
cmake --build build-3ds
```

The result is `build-3ds/platforms/3ds/redbound_3ds.3dsx`. GCC prints a note about `make_pair` and
the C++17 ABI in `SceneWorld.cpp`; it is informational.

`setup-ka3d.sh` applies `third_party/ka3d-patches/*.patch`, but that folder is not in the
repository yet, so a fresh clone cannot prepare KA3D. The 3DS build also needs `defined(__3DS__)`
in the platform detection of `third_party/ka3d/include/lang/pp.h`.

## Running

Import your `.deb` on a computer first, then copy the resulting `data` folder to the SD card at
`sdmc:/3ds/redbound/data`, so that `sdmc:/3ds/redbound/data/bundles/original/level01.hgr` exists.
Copy the `.3dsx` to `sdmc:/3ds/` and start it from the Homebrew Launcher. Loading level 1 takes
about 25 seconds; the first run also builds the texture cache.

The probe writes `sdmc:/3ds/redbound/probe-log.txt` with every effect, texture and material it
loaded.

| Control        | Game                         | Free camera        |
| -------------- | ---------------------------- | ------------------ |
| Circle Pad     | Move the ball                | Move               |
| C-Stick        | Turn the camera              | Look               |
| A, B           | Jump                         | Hold B to go fast  |
| L, R           |                              | Down, up           |
| Y              | Switch game and free camera  |                    |
| X              | Respawn                      |                    |
| SELECT         | Spawn the selected effect in front of the camera |   |
| D-Pad          | Debug menu: up and down pick, left and right change |  |
| START          | Exit                         |                    |

The debug menu on the bottom screen:

| Entry       | Values                                                    |
| ----------- | --------------------------------------------------------- |
| Camera      | game, free                                                |
| Level cam   | none or one of the level cameras (seen in free mode)      |
| Spawn fx    | any of the particle systems, spawned with SELECT          |
| Display     | 400px, 800px wide, 3D, 3D swapped                         |
| View        | normal, untextured, no lighting, lightmap only            |
| Fog         | off, 25, 35, 50, 75, 100 percent                          |
| Reflection  | on, off                                                   |
| Water       | ripples, still                                            |
| Specular    | on, off                                                   |
| Meshes      | all, or draw only the first N and show the last material  |

Above the menu the bottom screen shows frames per second, CPU and GPU time, free memory, the camera
position, and a CPU profile per phase: logic, reflection pass, main pass, and inside the renderer
pass setup, vertex upload, triangle sorting and draw submission.

## Performance

On a New 3DS XL the game runs between 20 and 30 frames per second and is limited by the CPU, not
the GPU. In a slow area the main pass takes about 23 ms, of which about 10 ms is setting up 270
passes (uniforms are looked up by name in a map) and about 9 ms is sorting transparent triangles
again every frame. Frames snap to 20 or 30 because of the vertical sync. Stereo 3D draws the scene
twice and costs about as much again.

## Known gaps

- No audio, HUD, menus, results screen or fade to black yet.
- The water reflection is not clipped against the water plane.
- The CPU cost above, which matters most in 3D.
- The KA3D patches are missing from the repository (see Building).
- Vertices are uploaded as floats (56 bytes each); packing them into shorts would save memory and
  bandwidth.
- Texture upload keeps a full RGBA copy in memory while tiling; larger levels may need streaming.
