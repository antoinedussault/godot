# ExpHeightFog — Godot 3.5.3 module

Unreal-style exponential height fog as a drop-in C++ module. No engine source
edits required — it ships an `ExpHeightFog` node that runs a screen-space depth
pass and applies the same exponential-height integral UE uses.

## Why a screen-space pass (read this)

A Godot 3.5 module **cannot** inject fog math into the built-in `scene.glsl`
fragment shader — that file is baked into the GLES3 rasterizer at engine build
time and has no module hook. So this module does the next best thing: a
fullscreen pass that reconstructs world position from `DEPTH_TEXTURE` and fogs
per pixel. Practically identical to in-shader fog for opaque scenes.

**Known limitation:** because it reads scene depth, it fogs opaque geometry
correctly but does **not** fog the interior of transparent surfaces (glass,
particles) the way true in-shader fog would. If you need that, the in-shader
patch (editing `scene.glsl` directly) is the only route — but that's not a
module. For almost all scenes this pass looks the same.

## Install

1. Copy the `exp_height_fog/` folder into your fork's `modules/` directory:
   ```
   godot/modules/exp_height_fog/
   ```
2. Rebuild the engine as usual, e.g.:
   ```
   scons platform=linuxbsd target=editor -j8
   ```
   (use your platform's normal build command)
3. The `ExpHeightFog` node now appears in the Create Node dialog and the class
   reference.

## Use

1. Open a 3D scene with a `Camera` and at least one lit object.
2. Add an **ExpHeightFog** node anywhere in the scene (it's global; position
   doesn't matter).
3. Tweak in the Inspector:
   - **Density** — thickness at the reference height.
   - **Falloff** — smaller = fog climbs higher; larger = hugs the ground.
   - **Ref Height** — the world Y treated as fog "sea level."
   - **Ref Height Relative** — when on, **Ref Height** is an offset from the
     camera's Y instead of an absolute world Y, so the fog "sea level" follows
     the camera (density at eye level stays constant as you climb).
   - **Start Distance** — clear space near the camera.
   - **Max Opacity** — opacity ceiling.
   - **Inscatter** — fog color.
   - **Directional Inscattering** — optional sun glow; set
     **Directional Light Direction** to your sun's forward (-Z) vector.

### GDScript

```gdscript
var fog := ExpHeightFog.new()
fog.density = 0.05
fog.falloff = 0.12
fog.ref_height = 0.0
fog.start_distance = 4.0
fog.max_opacity = 0.9
fog.inscatter = Color(0.55, 0.62, 0.72)

# Optional sun glow, synced to a DirectionalLight named "Sun":
fog.directional_enabled = true
fog.directional_color = Color(1.0, 0.85, 0.6)
fog.directional_light_direction = -$Sun.global_transform.basis.z
add_child(fog)
```

## Parameter ↔ Unreal mapping

| ExpHeightFog              | Unreal Engine              |
|---------------------------|----------------------------|
| density                   | Fog Density                |
| falloff                   | Fog Height Falloff         |
| ref_height                | Fog actor height           |
| start_distance            | Start Distance             |
| max_opacity               | Fog Max Opacity            |
| inscatter                 | Fog Inscattering Color     |
| directional_*             | Directional Inscattering   |

## Tuning notes

- The integral is the analytic line integral of `density * exp(-falloff*(h-ref))`
  along the ray, with a series-limit fallback for near-horizontal rays
  (`falloff*dz ≈ 0`) so the horizon doesn't band.
- Colors are converted to linear before upload, matching Godot's linear
  lighting pipeline.
- If fog ever vanishes when the camera looks a certain way, it's frustum
  culling — the quad already uses a huge custom AABB to prevent that, but if you
  swap the mesh, keep the oversized AABB.

## If you decide you want true in-shader fog later

That requires editing `drivers/gles3/shaders/scene.glsl` and the rasterizer —
not possible from a module. The earlier patch set covers that route.
