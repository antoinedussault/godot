# Box3D physics backend

A third `PhysicsServer` implementation alongside `GodotPhysics` and `Bullet`,
built on [Box3D](https://github.com/erincatto/box3d) — Erin Catto's 3D engine
(MIT, C17, soft-step solver, CCD, SIMD, cross-platform determinism).

**Status: playable.** Rigid bodies fall, collide and stack; `KinematicBody`
characters stand on ground, slide, and report `is_on_floor()`; height fields give
terrain collision; and ray/point/shape queries return real hits.

Verified end to end: a sphere dropped from y=10 rests at exactly y=2.0 on a floor
whose top face is y=1, and a capsule character lands at the geometrically correct
height and walks at exactly the requested speed.

Not yet wired: **Areas** (no `body_entered` signals, no gravity overrides — this
is the biggest remaining gap for real game content), joints, contact reporting in
`_integrate_forces`, trimesh shapes, and `RayShape`.

## Enabling it

The module registers "Box3D" as an *option* and never calls
`set_default_server()`, so building it cannot change how any existing project
behaves. Opt in per project:

```
physics/3d/physics_engine = "Box3D"
```

Build with the module off entirely:

```bash
scons platform=windows target=release_debug tools=yes bits=64 -j16 module_box3d_enabled=no
```

## Layout

| File | Holds |
|---|---|
| `box3d_objects.h` | Godot-side bookkeeping for shapes, areas, bodies, joints |
| `box3d_space.h/.cpp` | One world + all scene queries (`Box3DPhysicsDirectSpaceState`) |
| `box3d_direct_body_state.h/.cpp` | The object passed to `_integrate_forces()` |
| `box3d_physics_server.h/.cpp` | The `PhysicsServer` surface itself |
| `SCsub` | Build; carries the notes for vendoring Box3D |

The bookkeeping is deliberately separate from the solver. That is what makes the
port incremental — the engine can create, configure and free bodies through this
layer before a single `b3World` exists, so each subsystem below flips from
"stored" to "live" without the `PhysicsServer` surface changing again.

## Order to fill this in

Roughly dependency order; each line is independently testable.

- [x] **Vendor Box3D** into `thirdparty/box3d/` and compile it from `SCsub`.
      Done: upstream `47d7f7cc` (2026-08-29), v0.1.0, MIT. Only `src/` and
      `include/` are vendored — `extern/` is sokol for the samples and nothing
      in `src/` references it. 50 `.c` files, ~67k lines, 2.3 MB.
- [x] **Spaces**: `b3WorldId` per `Box3DSpace`, `b3World_Step()` in
      `Box3DSpace::step()`, transforms written back to `Box3DBody`. Gravity
      follows the default area. Single-threaded (`workerCount = 1`) for now.
- [x] **Shapes, primitives**: sphere, box, capsule, cylinder and convex are
      built and attached. Two axis conventions to keep in mind, both already
      handled: Godot's `CapsuleShape` is **Z**-aligned with `height` covering
      only the cylindrical section, while `CylinderShape` is **Y**-aligned with
      `height` as the full height.
      - `SHAPE_CYLINDER` is a 16-sided generated hull, not a true cylinder — it
        settles on a facet instead of rolling forever. A real behaviour
        difference from Bullet; document it.
- [x] **Height fields**: built, owned and invalidated correctly. Box3D anchors
      them at the body origin extending positive while Godot centers them, and
      `b3CreateHeightFieldShape` takes no transform — so the offset is applied to
      the static body via `Box3DBody::get_solver_transform()`.
      Box3D holds a *reference* to the height blob rather than cloning it, and
      the blob is shared across attachments, so `shape_set_data()` destroys every
      referencing b3 shape before freeing it. This is the streamed-terrain hot
      path: `CellTerrain` reassigns heightmap data on every cell load.
      **Watch for seams**: Box3D quantizes heights into `uint16` between a global
      min/max. Cells that compute their own bounds will not line up at shared
      edges — they must agree on the range.
- [ ] **Shapes, the rest**:
      - `SHAPE_CONCAVE_POLYGON` — `b3CreateMeshShape` has the same non-cloning
        contract as the height field. Static bodies only.
      - `SHAPE_PLANE` — no equivalent (use a large box for a floor).
      - `SHAPE_RAY` — no equivalent. Bullet needed both a custom shape and a
        custom collision algorithm for this one (`modules/bullet/btRayShape.cpp`,
        `godot_ray_world_algorithm.cpp`).
- [x] **Bodies**: static / kinematic / rigid / character, axis locks, layer &
      mask, sleeping, transform and velocity sync, central and positional
      impulses. Mass matches Godot's value exactly: shapes are attached at
      density 1, then mass and inertia are rescaled by the ratio to the
      requested mass (inertia is linear in mass for a fixed shape).
- [ ] **Bodies, remaining**: `add_force`/`add_torque` accumulation, torque
      impulses, per-body damping driven by areas, CCD flag.
- [x] **Queries**: `intersect_ray`, `intersect_point`, `intersect_shape` and
      `cast_motion` are live. `rest_info` and `collide_shape` still stubbed —
      they need contact depth, which `b3World_OverlapShape` does not return.
- [x] **`body_test_motion`**: done, and *not* via a shape cast. A cast is the
      wrong tool for a character already standing on the ground — it reports
      impact fraction 0 and the body never moves, which is exactly what the
      first attempt did.
      The working version uses Box3D's mover API: `b3World_CollideMover`
      gathers every contact plane, and `b3SolvePlanes` resolves depenetration
      and sliding together in one pass. That also handles inside corners, which
      a one-normal-at-a-time slide gets wrong.
      Capsule and sphere bodies take that path; anything else falls back to the
      shape cast and gets a correct time of impact but no depenetration.
      `body_test_ray_separation` shares the same plane-solver recovery.
      Verified: a capsule falls onto a floor, rests at the geometrically correct
      height, reports `is_on_floor()`, and walks at exactly the requested speed.
- [ ] **Areas**: Box3D sensors give overlap. Godot also wants gravity
      point/vector override, space-override modes, damp overrides and ordered
      enter/exit callbacks — all backend-side logic.
- [ ] **Joints**: the mapping is much better than Box3D's own feature list
      suggests. It ships parallel, distance, motor, filter, prismatic, revolute,
      spherical, weld and wheel joints:
      - hinge → `b3RevoluteJoint`, slider → `b3PrismaticJoint` (direct)
      - pin → `b3SphericalJoint` (direct — a real ball-socket joint)
      - cone twist → `b3SphericalJoint` too: its def carries `enableConeLimit` /
        `coneAngle` plus `enableTwistLimit` / `lowerTwistAngle` /
        `upperTwistAngle`, which is exactly Godot's cone-twist parameter set
      - generic 6DOF → still no equivalent; needs building or a documented
        unsupported status. It is the only real gap.
- [ ] **Contacts**: fill `Box3DPhysicsDirectBodyState::contacts` so
      `CellItemBody::_direct_state_changed` sees a `CellPlayerBody` shoving it.
- [ ] **Threading**: hand Box3D's task callbacks a thread pool.
- [ ] **Debug**: feed `Box3DSpace::debug_contacts` for "Visible Collision Shapes".

## Before investing in the rest of it

The reason to prefer Box3D here is solver quality and cost under a streaming open
world, and that is measurable before most of the list above is done. `CellTerrain`
rebuilds and reassigns a whole `HeightMapShape` per streamed cell — its own
comments call that the most expensive single step in cell loading
(`modules/cell_system/cell_terrain.h:264`). Box3D's height-field create/destroy
cost under that churn is unknown.

Benchmark it right after the vendoring and shape steps, against Bullet, on the
same cell-streaming path. If the height-field churn is not better there, the rest
of the port is not worth the API churn of tracking a 0.1.0 library.

## Unsupported, permanently

**Soft bodies.** Box3D has none and none are planned. The `soft_body_*` API is
stubbed, the same way `GodotPhysics` stubs it — a `SoftBody` node does nothing
rather than breaking the build.
