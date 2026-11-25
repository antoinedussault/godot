# light_shafts module

> `DirectionalLight` now has god rays built in (`godrays_*`). This node is kept so
> existing scenes load; it forwards its older properties to them.

Adds **`DirectionalLightShaft`**, a `DirectionalLight` that also renders
Unreal-style **light shafts / god rays** as a real post-process stage in the
GLES3 renderer.

## What it does

| Unreal                | Property             | Effect                                                            |
|-----------------------|---------------------|------------------------------------------------------------------|
| Light Shaft Bloom     | `bloom_enabled`      | Bright streaks scattered out from the sun's on-screen position.   |
| Light Shaft Occlusion | `occlusion_enabled`  | Scene darkened toward the sun where **geometry** (depth) blocks it. |

Pipeline (in `RasterizerSceneGLES3::_light_shafts`, run each frame right before
glow):

1. Downsample the frame to half-res; build a bloom mask (soft-knee above
   `bloom_threshold`) and an occlusion mask that treats a pixel as "lit" if it's
   at the far plane **or** bright enough to be sky/sun (so a mesh sky works too).
2. One accumulating radial-blur gather toward the sun's projected screen
   position, with an interleaved-gradient dither, then a small tent blur to
   erase the dither grain. Samples that fall off screen contribute nothing.
3. Optional temporal filter against the previous frame (reprojected by the sun's
   screen motion, and dropped the moment the camera moves). Off by default.
4. Composite: `scene *= occ_factor` (localized to the sun's region, floored) then
   `scene += bloomTint * blur * bloom_intensity`. The whole contribution fades to
   zero as the sun nears/leaves the screen edge.

## Requirements

- **GLES3 only.** On GLES2 the node is a plain `DirectionalLight` (the VS call
  is a no-op).
- Needs the HDR / 3D-buffers render path (same one glow and DOF require). If the
  viewport has no depth buffer available the occlusion half is skipped.

## Engine changes this module depends on

This module ships with matching core-engine changes (already applied in this
fork):

- `VisualServer::light_directional_set_light_shafts(...)` — new API, plumbed
  through `rasterizer.h`, `visual_server{,_raster,_wrap_mt}`, dummy + GLES2 stubs.
- `RasterizerStorageGLES3::Light::LightShaft` — per-light params.
- `RenderTarget::Effects::LightShaft` — half-res ping-pong + temporal history
  buffers, allocated/freed with the other effect buffers.
- `drivers/gles3/shaders/light_shaft.glsl` — the 4-mode post-process shader.
- `RasterizerSceneGLES3::_light_shafts` + `_post_process` signature.

## Usage

1. Rebuild the engine (`scons platform=windows target=release_debug tools=yes
   bits=64`).
2. Add a **DirectionalLightShaft** node as your sun (or change an existing
   `DirectionalLight`'s type).
3. Tune it under **Light Shafts**, **Light Shaft Bloom**, **Light Shaft
   Occlusion** in the inspector. The effect is visible both in-game and in the
   editor viewport.

## Key properties

- `shaft_enabled` – master switch.
- `shaft_samples` – taps in the radial-blur gather (`ShaftQuality`: 16 / 32 / 64 /
  96; default 64).
- `shaft_density` (0.75) / `shaft_decay` (0.93) – reach and falloff of the rays.
- `shaft_weight` (1.0) – gain while accumulating; raise for punchier rays.
- `bloom_intensity` (1.0) / `bloom_threshold` (1.0) / `bloom_tint` – final
  strength, the linear brightness that counts as "the sun", and streak color.
- `occlusion_darkness` (0.35) – extra dimming toward a blocked sun (0 disables).
- `temporal_blend` (0.0) – shimmer reduction; turn to ~0.8 for a static camera,
  leave at 0 otherwise (it auto-drops on camera motion but a hard whip can still
  ghost a frame).

## Aiming

The shafts emanate from where the **`DirectionalLightShaft`'s own aim** puts the
sun on screen — *not* from whatever bright spot your sky material paints. Point
the node at your sky's sun or the rays will start from the wrong place. When the
sun is off screen the effect fades out and then switches off entirely.
