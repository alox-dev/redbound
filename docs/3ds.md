# Nintendo 3DS

The 3DS port is an experiment on the `feature/3ds` branch. What exists today is a probe:
it loads a level with KA3D and draws it with a citro3d renderer, to measure whether the
hardware can run the game before the rest of the port is written. There is no audio, no
menus, no physics and no `.deb` importer on the console yet.

Target: New 3DS and New 2DS XL. The Old 3DS is kept reachable (every expensive feature is
optional and the model is detected at runtime), but it is not the goal.

## Why the renderer is different

The PICA200 has programmable vertex shaders only, written in its own assembly language;
it cannot run GLSL. Its fragment stage is fixed function: six configurable TEV combiner
stages. So `port/render/gles2`, which translates Rovio's `.fx` effects into GLSL ES
programs, cannot be reused as is.

`port/render/citro3d` implements the same `gr::Context` interface with:

| Piece                                                               | Files              |
| ------------------------------------------------------------------- | ------------------ |
| Context, shader, primitive and texture on citro3d                   | `Citro3D.cpp`      |
| Classifying each `.fx` pass into a combiner recipe and render states | `PassRecipe.cpp`   |
| Power of two resizing, mipmaps and PICA tiling for textures         | `TextureTiling.cpp`|
| Vertex shader: transform, per vertex lighting, two texture sets     | `scene.v.pica`     |

The `.fx` files are still parsed by the shared `EffectParser`. Instead of compiling the
GLSL, the pixel shader source is read to find which samplers a pass uses, and the pass is
mapped to one of four recipes: vertex color, base texture, base texture plus lightmap, or
water. The lightmap recipe reproduces Rovio's `color += (SIMAP - 0.5) * 2` with the
combiner's signed add and a 2x scale.

`PassRecipe` and `TextureTiling` have no console dependency and are covered by
`test_citro3d_core`, which runs in the normal desktop test suite.

## Known gaps

- Water reflections, particles, render targets and the sky are not drawn yet.
- Vertices are uploaded as floats (56 bytes each). Packing them into shorts is the first
  optimisation to try if geometry turns out to be the bottleneck.
- `setRenderTarget` is a stub, so effects that need an offscreen pass fall back to flat
  rendering.
- Texture upload keeps a full RGBA copy in memory while tiling; large levels may need
  streaming.

## Building

Requires devkitPro with the `3ds-dev` group installed (devkitARM, libctru, citro3d,
picasso, 3ds-tools) and `3ds-zlib`.

```
./scripts/setup-ka3d.sh
cmake -S . -B build-3ds -G Ninja -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake
cmake --build build-3ds
```

The result is `build-3ds/platforms/3ds/redbound_3ds.3dsx`.

## Running

Import your `.deb` on a computer first, then copy the resulting `data` folder to the SD
card at `sdmc:/3ds/redbound/data`, so that `sdmc:/3ds/redbound/data/bundles/original/level01.hgr`
exists. Copy the `.3dsx` to `sdmc:/3ds/` and start it from the Homebrew Launcher.

| Control      | Action                                            |
| ------------ | ------------------------------------------------- |
| Circle Pad   | Move the free camera                              |
| C-Stick      | Look around                                       |
| X / B        | Move up and down                                  |
| L / R        | Previous and next camera from the level           |
| Y            | Toggle the free camera                            |
| A            | Invert culling                                    |
| SELECT       | Flip textures vertically                          |
| START        | Exit                                              |

A and SELECT exist because two conventions can only be confirmed on hardware: whether
KA3D's winding order ends up clockwise or counter clockwise after the screen rotation, and
whether the PICA's texture origin matches the one the `.pvr` files assume. The bottom
screen reports frames per second, CPU and GPU time per frame, and free linear and VRAM
memory.

## Screen orientation

The top screen is physically rotated, so the projection matrix is rotated a quarter turn
in `setPerspectiveProjection`, as `Mtx_PerspTilt` does. The PICA also expects a depth
range of -1 to 0 with an inverted depth test, so KA3D's Direct3D style projection has its
`w` row subtracted and every comparison is flipped in the renderer.
