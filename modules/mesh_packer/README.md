# mesh_packer

Packs a pile of `MeshInstance` nodes into as few draw calls as possible.

Add a **MeshPacker** node, put (or move) the meshes under it, select it and press
**Pack** in the 3D viewport toolbar. The result lands in a `PackedOutput` child;
the originals are hidden, not deleted, and **Unpack** puts everything back.

## What it does

It walks its own subtree and sorts every `MeshInstance` it finds into batches,
then emits one node per batch:

| strategy | when | output |
| --- | --- | --- |
| **merge** | many small *unique* meshes (kitbash, modular walls) | one `MeshInstance` with an `ArrayMesh`, one surface per material |
| **instance** | many *repeated* meshes (barrels, fence posts, rocks) | one `MultiMeshInstance` |

`pack_mode = Auto` (the default) picks per group: a mesh that appears at least
`multimesh_threshold` times becomes a MultiMesh, everything else is merged.
Merging removes the per-object draw call but duplicates vertex data;
instancing shares it. Auto avoids paying the wrong cost for either case.

Two sources only share a batch when everything that affects how the renderer
treats them matches: material, material overlay, shadow casting setting, layer
mask, and the *Use Baked Light* flag. Grouping ignores none of these, so packing
never silently changes how something is drawn.

## Settings

- **pack_mode** — `Merge`, `MultiMesh` or `Auto`.
- **multimesh_threshold** — Auto only: copies needed before a mesh is instanced.
- **cell_size** — when above 0, sources are also grouped by a grid cell of this
  size. One giant merged mesh is always drawn in full; splitting it per cell
  keeps frustum culling useful. A sensible value is roughly the size of a room
  or a terrain chunk. `0` means one batch per material for the whole subtree.
- **vertex_budget** — a merged surface splits once it would pass this many
  vertices. The default of 65536 keeps surfaces on 16-bit indices.
- **compress_result** — store merged geometry with Godot's default vertex
  compression. Turn it off if the packed mesh shows precision artifacts.
- **source_handling** — `Hide` (default, reversible), `Remove` (frees the
  sources, runtime only, and only for nodes with no children), or `Keep`.
- **pack_on_ready** — pack in `_ready()` when the game runs, for subtrees built
  at runtime. Packing in the editor is better when the scene is authored: the
  output is saved into the `.tscn` and the game pays nothing on load.

## What it skips

Sources that cannot be baked safely are left alone and counted in
`skipped_surfaces` / left out of `sources`:

- already hidden nodes (hiding something is a deliberate choice),
- no mesh, or a mesh with no surfaces,
- skinned meshes (a `Skeleton` reachable from the skeleton path) and meshes with
  blend shapes — their vertices move at runtime,
- surfaces that are not `PRIMITIVE_TRIANGLES`.

Only visuals are touched. `StaticBody`/`CollisionShape` children of the sources
keep working, which is why `Remove` refuses to free a source that has children.

## Details worth knowing

- Geometry is baked in the packer's local space and each batch is re-centred on
  its own bounding box, so the output keeps float precision far from the origin
  and gets a tight AABB to cull against.
- Sources with different vertex formats can share a surface: missing channels
  are filled with neutral defaults (white colour, `(0,1,0)` normal, zero UVs)
  rather than rejecting the mesh.
- Mirrored (negative-scale) sources have their triangle winding and tangent
  handedness flipped back, so they do not end up inside out.
- Non-indexed surfaces get their implicit indices generated on the way in.
- Per-surface material overrides on an instanced source are baked into a copy of
  its mesh, since a `MultiMesh` has no per-surface material slots.

## Relation to `MeshInstance.merge_meshes()`

The engine already has a low-level merge primitive, but it requires the meshes
to have identical surface counts, identical vertex formats and identical
materials, merges into a single destination you pick yourself, and does no node
bookkeeping. `MeshPacker` is the layer above it: grouping, spatial splitting,
MultiMesh conversion, source handling and a reversible bake. It does not call
`merge_meshes()`; it has its own accumulator so it can accept mixed formats.

## Scripting

```gdscript
$MeshPacker.pack_mode = MeshPacker.PACK_MODE_AUTO
$MeshPacker.cell_size = 32.0
$MeshPacker.pack()
print($MeshPacker.get_pack_stats())
# { "sources": 812, "merged_meshes": 14, "multimeshes": 6,
#   "surfaces_before": 1104, "surfaces_after": 31, "vertices": 402118, ... }
$MeshPacker.unpack()
```
