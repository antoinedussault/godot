# Box3D physics backend

A third `PhysicsServer` implementation alongside `GodotPhysics` and `Bullet`,
built on [Box3D](https://github.com/erincatto/box3d) — Erin Catto's 3D engine
(MIT, C17, soft-step solver, CCD, SIMD, cross-platform determinism).

**Status: complete except for soft bodies.** Rigid, kinematic, static and
character bodies, every shape type, areas, all five joint types, contact
reporting, and every scene query are implemented.

## Enabling it

The module registers "Box3D" as an *option* and never calls
`set_default_server()`, so building it cannot change how any existing project
behaves. Opt in per project:

```
physics/3d/physics_engine = "Box3D"
```

`physics/3d/box3d/worker_count` sets how many threads each world uses (default 1).
A pile of 1000 boxes did not get measurably faster with 2, 4 or 8, so raise it
only after measuring.

Build with the module off entirely:

```bash
scons platform=windows target=release_debug tools=yes bits=64 -j16 module_box3d_enabled=no
```

## Layout

| File | Holds |
|---|---|
| `box3d_objects.h` | Godot-side bookkeeping for shapes, areas, bodies, joints |
| `box3d_shapes.cpp` | Godot shape → Box3D geometry |
| `box3d_space.h/.cpp` | One world, its step, area events, and all scene queries |
| `box3d_collide.h/.cpp` | Contacts with depth for shapes that are not in the world |
| `box3d_joints.cpp` | The joint half of the `PhysicsServer` surface |
| `box3d_direct_body_state.h/.cpp` | The object passed to `_integrate_forces()` |
| `box3d_physics_server.h/.cpp` | The rest of the `PhysicsServer` surface |
| `SCsub` | Build; carries the notes for vendoring Box3D |

The bookkeeping is authoritative. A body can be fully configured before it has a
space; b3 objects are created when it joins one and destroyed when it leaves.

## How the less obvious parts work

- **Collision filtering.** Godot collides a pair when *either* mask matches, Box3D
  when *both* do. Shapes are given an accept-all Box3D mask, and
  `b3_custom_filter` applies Godot's rule, collision exceptions, and an Area's
  `monitorable`. The layer stays in the category bits because scene queries match
  on it. A consequence: a body with `collision_mask = 0` is visible to raycasts,
  as in the other backends.
- **Areas** are static bodies made of sensors. Box3D reports changes by shape id,
  and the shape may be dead by the time its end is reported, so each overlap
  records at the start everything its end has to say (`Box3DSpace::overlaps`).
  Events are collected right after the step and delivered in `call_queries()`.
- **Gravity and damping overrides** are recomputed per body each step, because
  Box3D gravity is per world.
- **Kinematic bodies** are teleported by Godot, but contacts need velocity. The
  teleport happens at once, so queries see it; the next step rewinds the body and
  replays the motion as velocity. More than 4 m in one step counts as a teleport.
- **`body_test_motion`** depenetrates, then sweeps to first contact and stops. Bodies
  with a capsule or sphere use Box3D's mover API; others use `b3_collide_query`
  and shape casts. Neither is ever depenetrated by a RigidBody.
- **Scale** is baked into the shapes, since Box3D bodies are rigid.
- **Height fields** are anchored at a corner by Box3D and take no transform, so the
  static body is shifted instead (`Box3DBody::get_solver_transform`). Only on a
  static body, and it must be the only shape. Box3D quantizes heights between a
  global min and max: neighbouring cells must agree on that range or seams open.
- **`RayShape`** is cast each step and applied to the velocity of dynamic bodies;
  kinematic ones get it from `body_test_ray_separation`.
- **Forces and impulses** take their position as an offset from the body's origin in
  *global* axes, which is Godot 3's convention.

## Differences from the other backends

- `CylinderShape` is a 16-sided hull; a rolling one settles on a facet.
- A convex hull is limited to about 44 vertices and is simplified beyond that.
- `PlaneShape` is an 8 km slab, not infinite.
- `StaticBody.constant_angular_velocity` does not turn what stands on it; the linear
  one works.
- Box3D only generates trimesh contacts for static bodies.
- An Area cannot use a `HeightMapShape`, and a trimesh Area does not notice trimesh
  or height field bodies. A convex Area does.
- Joint softness, bias, relaxation and restitution, `PinJoint` damping and impulse
  clamp, and the solver priority are stored and returned but do nothing: Box3D's
  solver has no counterpart. Neither does `SPACE_PARAM_*`, apart from the linear
  sleep threshold.
- A `ConeTwistJoint` swing span over 90 degrees is treated as no swing limit.

**Soft bodies are unsupported, permanently.** Box3D has none and none are planned.
The `soft_body_*` API is stubbed, the same way `GodotPhysics` stubs it — a
`SoftBody` node does nothing.

## Local changes to thirdparty/box3d

Vendored from upstream `47d7f7cc` (2026-08-29), v0.1.0; only `src/` and `include/`.
These edits have to be carried over when updating. All are marked `Tuna`.

- `src/generic_joint.c` (new), plus its hooks in `src/joint.c`, `src/joint.h`,
  `include/box3d/types.h` and `include/box3d/box3d.h`: a six degree of freedom
  joint. Upstream has none, and `Generic6DOFJoint` and `SliderJoint` need it. Not
  supported by Box3D recordings.
- `src/sensor.c`: a convex sensor may detect a mesh or height field. Upstream only
  accepts convex visitors.
- `src/body.c`: `b3Body_SetType` returned with the world still locked for a body
  carrying a height field.
