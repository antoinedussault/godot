# SpatialCable — Godot 3.5 module

A procedurally meshed, wind-driven cable / rope / wire / power line strung
between two points, as a drop-in C++ module. Ships one node, `SpatialCable`.

No engine source edits — copy the folder into `modules/` and rebuild.

## What it does

- Runs **one or more parallel strands** between two endpoints.
- Each strand is a **Verlet particle chain**: pinned at both anchors, pulled by
  gravity, pushed by a gusting wind vector, given a gentle idle sway.
- Renders a **round tube** (configurable sides + segments) into an internal
  `ImmediateGeometry`, rebuilt every frame. Takes any `Material` and a
  cast-shadow toggle.
- **Breaks when shot.** With `collision_enabled` it carries an internal
  `StaticBody` of capsule segments so hitscan rays hit it; with `detect_shots`
  it watches a node group and severs on contact by itself. A cut splits the
  strand — each half keeps hanging from the anchor it still holds.
- Fully live in the editor (shows the static resting curve; dynamics only run
  at runtime).

## Install

1. Copy `spatial_cable/` into your fork's `modules/` directory:
   ```
   godot/modules/spatial_cable/
   ```
2. Rebuild the engine, e.g.:
   ```
   scons platform=windows target=release_debug -j8
   ```
3. `SpatialCable` now appears in the Create Node dialog and the class reference.

## Quick start

1. Add a **SpatialCable** to a 3D scene.
2. Set **end_position** (local space) — or point **attach_start** /
   **attach_end** at two `Spatial` nodes to have the ends follow them.
3. Assign a **material** (a thin dark `SpatialMaterial` reads as a wire).
4. Press Play — it sags, sways and reacts to wind.

```gdscript
var cable := SpatialCable.new()
cable.attach_start = poleA.get_path()
cable.attach_end = poleB.get_path()
cable.cable_count = 3
cable.cable_spacing = 0.25
cable.cable_slack = 0.35
cable.wind_direction = Vector3(1, 0, 0.2)
cable.wind_force = 3.0
cable.material = preload("res://art/wire.tres")
add_child(cable)
cable.connect("cable_severed", self, "_on_cable_cut")
```

## Shooting it

**Option A — you already do weapon raycasts.** Leave `collision_enabled` on.
Your ray hits the internal `StaticBody`; feed the impact point back:

```gdscript
var hit := space.intersect_ray(from, to)
if hit and hit.collider.get_parent() is SpatialCable:
    hit.collider.get_parent().hit(hit.position)
```

**Option B — physics-object bullets.** Put your projectiles in a node group
(default `"bullet"`) and leave `detect_shots` on. The cable sweeps each
projectile's motion against its strands every physics frame and severs on
contact — no weapon-side code. Tune `hit_radius` for how close counts.

Set `breakable = false` to make it shrug off hits (still emits `cable_hit`).

## Parameter groups

| Group | Keys |
|-------|------|
| Endpoints | `start_position`, `end_position`, `attach_start`, `attach_end` |
| Cable | `cable_count`, `cable_spacing`, `cable_radius`, `cable_sides`, `cable_segments`, `cable_slack` |
| Rendering | `material`, `cast_shadow` |
| Wind | `wind_enabled`, `wind_direction`, `wind_force`, `wind_turbulence`, `wind_affect` |
| Sway | `sway_enabled`, `sway_amount`, `sway_speed` |
| Simulation | `simulate`, `gravity`, `stiffness`, `damping`, `substeps`, `solver_iterations` |
| Breaking | `breakable`, `detect_shots`, `projectile_group`, `hit_radius`, `collision_enabled`, `collision_layer`, `collision_mask` |

## Signals

- `cable_hit(position: Vector3, cable_index: int)` — struck, break or not.
- `cable_severed(cable_index: int, segment_index: int, position: Vector3)` — a
  segment was cut.

## Notes & limits

- `damping` is frame-rate independent (fraction of velocity lost per 1/60 s),
  so changing `substeps` or FPS doesn't change the feel.
- Collision cost scales with `cable_count × cable_segments` capsules updated per
  physics frame; the body is skipped entirely past 512 capsules. Drop
  `cable_segments` or turn off `collision_enabled` for dense bundles and rely on
  `detect_shots` alone.
- The mesh rebuilds every frame while simulating. Fine for dozens of cables; if
  you have hundreds, hide distant ones or set `simulate = false` on them.
- No editor gizmo for the endpoints yet — drag `attach_*` targets, or type
  `start_position` / `end_position` in the Inspector.
- The strand is a chain of straight segments, not an analytic catenary; raise
  `cable_segments` for a smoother curve.
