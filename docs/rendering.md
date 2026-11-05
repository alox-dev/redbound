# Rendering

`port/render/gles2/` implements KA3D's `gr::Context` on OpenGL ES 2.0; the same code runs on Linux (Mesa through
SDL2) and Android.

| Piece | Files |
| --- | --- |
| Context, primitives (one VBO per attribute, 16 bit indices), render targets | `GLES2.cpp` |
| `.fx` effects: preprocessor, techniques, passes, states, uniforms by name | `EffectParser.cpp` |
| Textures: legacy PVR (PVRTC 2/4 bpp and uncompressed) decoded to RGBA; PNG/TGA/JPG with stb_image | `ImageLoader.cpp` |
| Water reflection (`WaterPipe`): mirrored camera, `Reflected` technique, oblique near plane, `water01..30.pvr` normals | `WaterReflection.cpp` |
| Particles: `ParticleSystem::renderGLES2`, adapted from `renderDX` | KA3D patch |

## Conventions inherited from KA3D (Direct3D origins)

- Column vectors stored `m[row][column]`, uploaded to GLSL column major.
- Projection depth 0..1 converted to -1..1; clockwise front faces; viewport origin at the top left.
- Scenes store file names (sometimes Windows paths): the base name is looked up in `data/shaders/` and
  `data/resources/textures/`, trying `.pvr` when the original extension does not exist.
- Materials with `sort = BACKTOFRONT` / `FRONTTOBACK` reorder their triangles toward the camera.

## Lightmaps

Rovio's shaders add the lightmap with sign: `color += (SIMAP - 0.5) * 2`. `TEX1` is used as stored (measured with
`hgr-dump --lmcheck`: 1.5 % of the lightmapped vertices fall on the black atlas background as is, 38 to 50 % with any
flip). When a material needs `TEX1` but a mesh lacks it, `TEX0` is used.

## Geometry that is not drawn

Nodes whose `group` has bit `0x100` (and whose name does not start with `T_`) are collision only, as in
`CollisionObject::parseCollisionProperties`; meshes without material are hidden too.

## Defaults for missing inputs

Samplers without texture: `REFLMAP`/`REFRACMAP` black, `NORMALMAP` flat, `SIMAP` 0.5 gray, others white. Materials
without depth test (Bounce's eyes) are depth tested without writing depth, with a polygon offset toward the camera.

## KA3D patch

Base: https://github.com/pent0/ka3d (GPL v2), patched by `third_party/ka3d-patches/0001-linux-gcc-64bit.patch`:

| Change | Reason |
| --- | --- |
| `PLATFORM_LINUX` (also used for Android and Switch), static global storage, GCC/Clang `config.h` | KA3D only knew Win32 and phone platforms of its time |
| `BUFFER_HEADER_SIZE` uses `sizeof(uint8_t*)` | it assumed 4 byte pointers |
| Unknown platform ids return "unknown" | Rovio's scenes use ids newer than KA3D 2007 |
| Unsigned `String` hash | signed overflow is undefined behavior |
| Particle frame grid with an integer square root | `waterfall.prs` uses a 3x3 grid |
| `ParticleSystem::renderGLES2` | particles on OpenGL ES |
| Sorting by bounding box center and a conservative culling radius | transparency order (eyes, leaves) and objects vanishing up close |
| Debug-only assert and include case fixes | build on Linux |

## Runtime textures

`REFLMAP` (and `REFRACMAP`) are not stored in the level: the water reflection is rendered every frame by
`WaterReflection` and bound to those samplers when drawing. On frames without a reflection (the first frame, or the
camera under the water level, where the original draws none either) they fall back to a neutral texture, so they are
not reported as missing.

## Render targets

`GLES2_Context::setRenderTarget` caches one framebuffer per texture. OpenGL reuses the name of a deleted texture,
so the cache entry is released when its texture is destroyed and rebuilt whenever it belongs to a different texture
or size; otherwise a recreated target (the water reflection after a render scale change) would keep drawing into the
deleted texture. Textures register with their context, which detaches them when it is destroyed first.
