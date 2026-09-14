# Box3D physics backend

A third `PhysicsServer` implementation alongside `GodotPhysics` and `Bullet`,
built on [Box3D](https://github.com/erincatto/box3d) — Erin Catto's 3D engine
(MIT, C17, soft-step solver, CCD, SIMD, cross-platform determinism).

**Status: rigid bodies simulate.** A `b3World` is created per space, bodies enter
and leave it with the scene tree, primitive and convex shapes are built, mass
matches Godot's, and solver transforms are written back each step — so dynamic
bodies fall, collide and stack.

Not yet wired: scene queries (every `intersect_*` returns no hit),
`body_test_motion` (so `KinematicBody` does not collide), areas, joints, contact
reporting, and trimesh/heightmap shapes.

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
- [ ] **Shapes, the rest**:
      - `SHAPE_CONCAVE_POLYGON` / `SHAPE_HEIGHTMAP` — note that
        `b3CreateMeshShape` and `b3CreateHeightFieldShape` do **not** clone their
        input. The buffers must be owned by `Box3DShape` and outlive every shape
        built from them. Both are static-body only.
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
- [ ] **Queries**: `intersect_ray` first — it is what `RayCast` and most game
      code hits — then `intersect_shape`, `cast_motion`, `rest_info`.
- [ ] **`body_test_motion`**: the big one. This single method defines the feel of
      `KinematicBody.move_and_slide()` — recovery, margin, safe/unsafe fractions.
      Box3D's character mover is a different model and cannot be dropped in
      behind this signature; it needs a shape cast plus a recovery loop written
      to Godot's semantics.
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
