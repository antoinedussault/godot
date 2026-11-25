# Making a tree for the foliage system

How to author a tree so `FoliageManager` can paint it. Written from the code
that actually parses it — `cell_foliage.cpp` (`_lod_index_from_name`,
`_is_imposter_node`, `_extract_tree_meshes`, `_extract_tree_collision`) and
`tree_importer.cpp` — so if you change those, update this.

---

## The short version

1. In Blender (or whatever), name each LOD mesh object `something-lod0`,
   `something-lod1`, … the optional billboard `something-imposter`, and the
   collision mesh `something-colonly`.
2. Export the whole tree as one `.glb`.
3. In the editor, select the `.glb` → **Import** dock → set importer to
   **Tuna Tree** → **Reimport**. The `.glb` is now a `FoliageType`.
4. Drag the `.glb` into a slot of the `Foliage` node's `types` array.
   **Not into a "Tree Scene" slot** - it is a `FoliageType` now, not a scene.
5. Select the `Foliage` node, pick the type in the viewport toolbar, paint.

There are two ways to make a tree type, and they do not mix:

| | **Tuna Tree importer** | **By hand** |
|---|---|---|
| The `.glb` is imported as | Tuna Tree | Scene (the default) |
| The `FoliageType` is | the `.glb` itself | a `.tres` you create (*New Resource → FoliageType*, type = Tree) |
| `Tree Scene` slot | filled in for you | *Load* the `.glb` or a `.tscn` built from it |
| Settings are edited in | the **Import** dock, then Reimport | the Inspector |
| Materials | by name, from files next to the `.glb` | on each `MeshInstance` of your `.tscn` |

Everything below is the detail behind those steps.

---

## Naming rules (this is the part that is easy to forget)

The tree is **one scene** containing every LOD as a separate `MeshInstance`.
Which mesh is which LOD is decided **purely by node name**:

| Node name ends with | Meaning |
|---|---|
| `-lod0` | LOD 0 — the closest, highest detail |
| `-lod1` … `-lod4` | Progressively cheaper LODs |
| `-imposter` or `_imposter` | Optional billboard for the far distance |

Specifics that bite:

- The match is `rfind("-lod")` followed by an integer, so `trunk-lod0`,
  `oak_bark-lod2`, `leaves-lod1` all work. **`-LOD0` (uppercase) does not**, and
  neither does `-lod 0` or `lod0` without the hyphen.
- Only the imposter accepts an underscore (`_imposter`). LODs need the hyphen.
- **A `MeshInstance` whose name matches nothing is silently ignored.** This is
  the usual reason a tree paints as "nothing visible" — an unnamed or
  mis-suffixed mesh is skipped without any error.
- LOD index must be `< lod_count` on the `FoliageType`. A `-lod3` mesh with
  `lod_count = 2` is dropped.
- **Multiple meshes can share one LOD.** `bark-lod0` and `leaves-lod0` both
  become part of LOD 0. That is how you keep separate bark/leaf materials.
- Nesting is fine — the tree is walked recursively, and each part's transform
  is accumulated up to the tree root, so child meshes under empties keep their
  placement.
- Only the **first** imposter found is used; extras are ignored.

## Materials

**With the Tuna Tree importer:** name the material in Blender, then put a
material file with the same name next to the `.glb`. A surface whose material
is called `m_oak_bark` uses `m_oak_bark.tres` (or `.material`) from the `.glb`'s
folder when that file exists; a surface with no matching file keeps the
material that came from Blender. Reimport after adding a new material file.
Editing an existing one needs no reimport - it is referenced, not copied.
The `material_override` import option replaces everything with one material.

**By hand:** open the `.glb`, make a new inherited scene (or save it as a
`.tscn`), and assign a material on each `MeshInstance`. Point `Tree Scene` at
that scene.

In both cases, each part's material is then read from the **MeshInstance**, in
this order:

1. `material_override` on the instance
2. the first instance-level surface material

If the instance has neither, the mesh's own built-in surface material is used.
This matters because everything is rendered through `MultiMesh` — so assign
materials in a way that survives that, and expect one draw call per part per
LOD, not per tree.

`FoliageType.material_override` overrides all of it, if you want one material
across the whole type.

## Collision

In Blender, add a simple mesh for the trunk and name it `something-colonly`
(triangle mesh) or `something-convcolonly` (convex hull, cheaper). It is not
drawn; it becomes a `CollisionShape`. Both importers understand the suffix.

More generally, collision lives **inside the tree scene**: any `CollisionShape`
node anywhere in the hierarchy is collected, with its transform baked relative
to the tree root. You do not need a `StaticBody` wrapper — `StaticBody` and
other non-mesh nodes are ignored by the mesh walk, so they are harmless. When
you build the scene by hand you can simply add a `CollisionShape` node.

Toggle with `FoliageType.collision_enabled` (TREE only). No shapes in the
scene means no collision, regardless of that flag.

---

## Import options (the "Tuna Tree" importer)

Select the `.glb` → Import dock → importer **Tuna Tree**:

| Option | Default | Notes |
|---|---|---|
| `lod_count` | 1 | How many LOD levels are active, 1–5 |
| `lod_distance_0` | 20 | Distance where LOD0 → LOD1 |
| `lod_distance_1` | 50 | LOD1 → LOD2 |
| `lod_distance_2` | 100 | LOD2 → LOD3 |
| `lod_distance_3` | 200 | LOD3 → LOD4 |
| `max_render_distance` | 100 | Beyond this the type stops rendering |
| `kind` | Tree | Tree or Rock |
| `material_override` | empty | One material for the whole type |
| `scatter/*` | | Every scatter rule listed below |
| `collision/*` | | `enabled`, `nav_obstacle_radius` |

`lod_count` left at 1 means "as many as the file has"; a higher value is a cap.

The importer reads the glTF itself and stores the model **inside** the
`FoliageType`, so the result is one self-contained resource. An imported
resource cannot be edited in the Inspector, which is why every setting is an
import option. The Output panel prints one line per import, e.g.
`Tuna Tree: oak.glb -> 3 LOD(s), 1 collision shape(s), 2 external material(s).`
— check it: `0 LOD(s)` means the mesh names are wrong.

You can also skip the importer entirely (the "by hand" column above): create a
`FoliageType` resource, set `type = TREE`, and point `tree_scene` at any
`PackedScene` following the same naming rules.

---

## FoliageType settings worth knowing

**Scatter rules** (used by the paint brush):

- `density` — instances per unit area when scattering
- `min_scale` / `max_scale` — random uniform scale range
- `random_yaw` — random rotation about the instance up axis
- `align_to_normal` + `align_amount` — tilt toward the terrain normal.
  `align_amount` is a 0–1 blend between world up and the surface normal, so
  `0.3` gives a slight lean and `1.0` fully follows the ground
- `slope_limit_deg` — refuse to place on steeper ground
- `min_spacing` — minimum distance between instances
- `y_offset_min` / `y_offset_max` — random vertical offset, e.g. to sink trunks
  slightly into the ground

**Rendering:**

- `cast_shadow` and `shadow_lod_count` — only the N closest LODs cast shadows;
  the imposter never does. Lowering `shadow_lod_count` is the cheapest win if
  the shadow pass is heavy.
- `fade_band` — crossfade width at LOD transitions
- `keep_loaded` — keep the cell's foliage rendering even after its terrain cell
  streams out (for distant trees with imposters)

---

## Painting

Select the `FoliageManager`. The tool lives in the **viewport top toolbar**:

- Three icon modes: **Scatter** (spray within radius), **Single** (place one at
  the cursor), **Erase** (remove within radius)
- **Type** dropdown — the entries come from the manager's `types` array, so if
  it is empty you have not assigned any `FoliageType` yet
- **Radius** and **Density**

The green ring on the ground shows the brush; Single mode shows a marker
instead, since radius does not apply there. Strokes are undoable per-stroke.

---

## When a tree paints but nothing appears

In rough order of likelihood:

1. **Node naming** — a mesh with no `-lod{n}` suffix is skipped silently.
   Check the exact spelling and the hyphen.
2. **`lod_count` too low** — `-lod2` meshes are dropped when `lod_count` is 1.
3. **Nothing in `types`** — the Type dropdown shows `(no layers)`-style empty.
4. **`max_render_distance`** — you may be standing too far away.
5. **`slope_limit_deg`** — scatter refuses ground steeper than this, so nothing
   lands on a hillside.
6. **No terrain under the cursor** — foliage is placed against the terrain
   surface, so the cell has to be streamed in. If the paint lands nowhere, the
   raycast found no ground.
