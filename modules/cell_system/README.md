# Foliage System for `cell_system` (Godot 3.5.3)

An Unreal-style foliage system that plugs into your existing
`modules/cell_system/`. Foliage instances are cell-owned data that stream with
the terrain, render through per-LOD `MultiMesh` (one instanced draw call per
type × LOD), LOD + dither-dissolve per instance by camera distance, and get
trunk-style collision only in cells near the camera.

## Files — drop into `modules/cell_system/`

```
foliage_type.h / .cpp          FoliageType resource (per-species config)
cell_foliage.h / .cpp          Per-cell instance store + MultiMesh LOD render
foliage_manager.h / .cpp       Top-level node; streams foliage with terrain
foliage_editor_plugin.h / .cpp Paint tool (Scatter / Single / Erase)
foliage_dither.shader          Per-instance IGN dissolve via INSTANCE_CUSTOM
register_types.cpp             REPLACES your current one (adds foliage classes)
```

`SCsub` needs no change — it globs `*.cpp`. `config.py` is unchanged.

> Back up your existing `register_types.cpp` first and diff it against the one
> here; I reconstructed the existing class list (CellTerrain, CellTerrainManager,
> Cell, CellWorld, CellPlayerBody, CellGridVisualizer, TerrainBrush, and the
> three editor plugins). If yours registers anything else, merge rather than
> overwrite.

Rebuild:

```bash
scons platform=windows target=release_debug -j8
```

## How it fits your architecture

- **Ownership:** a `CellFoliage` node is parented under each active `CellTerrain`
  (the cell you already stream). When the terrain cell frees, its foliage goes
  with it. No parallel spatial structure — the cell grid *is* the partition.
- **Streaming:** `FoliageManager` watches the terrain manager's active cells (via
  `get_cell`) and attaches/detaches `CellFoliage` to match. It does not
  re-implement streaming.
- **Persistence:** per-cell `cell_<cx>_<cz>.foliage` files in `world_dir`, using
  the same `FileAccess` float-stream style as `.heights`. Dirty cells flush on
  eviction and on `save_dirty()`.
- **Ground placement:** instance Y and (optional) slope come from the terrain
  cell via `get_height_at`, using the same world→grid conversion as the sculpt
  brush.

## Scene setup

```
WorldRoot
├── CellTerrainManager        (your existing node)
├── FoliageManager            (NEW — add as a sibling)
│     terrain_manager_path -> ../CellTerrainManager
│     world_dir            -> same as the terrain manager
│     cell_size            -> same as the terrain manager
│     type0..type15        -> FoliageType resources
└── CellPlayerBody / Camera   (drives streaming as before)
```

`FoliageManager` reads the **active camera** each frame for LOD/collision, and
mirrors terrain cells for streaming. Keep `cell_size` and `world_dir` identical
to the terrain manager or cells won't line up.

## Creating a FoliageType

Create a `FoliageType` resource and assign it to a `type` slot on the manager.
Per type:

- **LOD Meshes:** `lod_mesh_0`..`lod_mesh_7`, highest detail first. Use as many
  as you have; the last non-null is the lowest LOD (often a billboard). For a
  tree that's `lod_mesh_0..3` + a billboard in `lod_mesh_4`.
- **lod_distances:** ascending camera distances for the boundaries. With N
  meshes give N-1 distances, e.g. `[20, 50, 110, 220]` for 5 meshes.
- **cull_distance:** instances beyond this aren't drawn (0 = no cull).
- **fade_band:** world-unit band around each boundary over which the dither
  dissolve runs.
- **Scatter:** `density` (instances/m² at full strength), `min/max_scale`,
  `random_yaw`, `align_to_normal` + `align_amount`, `slope_limit_deg`.
- **Collision:** `collision_enabled`, `collision_radius`, `collision_height`
  (a capsule at the trunk base). Only instantiated in near cells.
- **Material:** assign a `ShaderMaterial` using `foliage_dither.shader` here so
  the dissolve works (see below).

## The dither dissolve

The fade is per-instance, delivered through the MultiMesh custom-data buffer as
`INSTANCE_CUSTOM` — NOT a per-material uniform — which is what lets thousands of
instances fade independently in one draw call.

On each FoliageType, set `material_override` to a `ShaderMaterial` using
`foliage_dither.shader`. The shader reads `INSTANCE_CUSTOM.r` as the coverage
the manager writes each frame. Without this shader, LOD still switches but as a
hard pop (the custom data is set but nothing reads it).

`dither_scale` (coarseness) and `dither_animated` (temporal shimmer) are uniforms
on the shader.

## Painting

Select the `FoliageManager` in the scene tree — a toolbar appears in the 3D
viewport (same place as the terrain brush):

- **Scatter:** drag to scatter instances by the type's density within the brush
  radius. Respects slope limit and align-to-normal.
- **Single:** click to drop one instance at the cursor.
- **Erase:** drag to remove instances of the selected type within the radius.
- **Type / Radius / Strength** controls sit alongside.

Painting only affects cells that are currently streamed in (visible). Edits mark
cells dirty and save on mouse-up.

## Tuning for scale (1k–10k+)

- `lod_update_interval` (manager): 0 = update every frame. Set to e.g. 0.1 to
  update LOD/collision 10×/sec instead, cutting per-frame cost on dense scenes.
- `tree_collision_radius` (manager): cells around the camera that get colliders.
  1 = the 3×3 around the player. Keep small; colliders are the expensive part.
- Per type `cull_distance` aggressively for grass/small foliage.
- Grass: 1–2 LOD meshes, `collision_enabled = false`, high `density`, short
  `cull_distance`. Trees: 4 LOD + billboard, collision on.

## Known limitations / honest notes

- **Not compiled against your tree.** I built against the 3.5 API and your
  uploaded headers, but include paths and a few signatures should be verified on
  your checkout (the `vsproj=yes` solution will flag any drift instantly). Most
  likely touch-ups: editor GUI includes and `Math_TAU`/`Math_PI` from
  `core/math/math_defs.h`.
- **`min_spacing` is not yet enforced.** The field exists on FoliageType but the
  scatter loop does not reject overlapping placements yet; density alone governs
  spacing. Add a per-cell proximity query in `CellFoliage` to enforce it.
- **All instances are uploaded to every LOD's MultiMesh**, hidden via coverage.
  This is simple and keeps transform buffers static (only custom data changes
  per frame), but uses more VRAM than moving instances between LOD buffers. Fine
  for typical counts; revisit if VRAM-bound.
- **`INSTANCE_CUSTOM` requires GLES3.** On GLES2 the per-instance fade won't
  work; LOD would need the hard-swap path.
- **Editor preview of the dissolve** depends on the editor camera; LOD updates
  use `get_viewport()->get_camera()`, which in-editor is the editor viewport
  camera, so it should preview. If it doesn't update, nudge the camera.
- The cross-fade is distance-band based (via `fade_band`), so it's inherently
  framerate-independent — no per-frame accumulator to tune.
```

---

# Scene Manager (Creation-Engine-style scene travel)

Every location — an interior, an exterior area, a hub — is its own `.tscn`, and
the game travels between them at runtime the way Bethesda's Creation Kit links
cells with load doors: async-load the target behind a loading screen, swap it in
as the current scene, drop the player at a named marker.

## Classes — drop into `modules/cell_system/`

```
scene_catalog.h / .cpp            SceneCatalog + SceneCatalogEntry resources (the "cell list")
scene_manager.h / .cpp            SceneManager — Engine singleton, runs the travel
scene_transition.h / .cpp         SceneTransition — the loading-screen overlay
scene_spawn_point.h / .cpp        SceneSpawnPoint — a named arrival marker
scene_door.h / .cpp               SceneDoor — a linked load door (Area)
scene_manager_editor_plugin.*     "Scenes" bottom-panel dock (TOOLS_ENABLED)
register_types.cpp                REPLACES your current one (adds the classes + the SceneManager singleton)
config.py                         REPLACES your current one (adds get_doc_classes)
doc_classes/Scene*.xml            class reference
```

`SCsub` needs no change. Rebuild:

```bash
scons platform=windows target=release_debug vsproj=yes -j8
```

## How a "scene" works here

One `PackedScene` is the SceneTree's current scene at a time. `SceneManager`
never uses `SceneTree.change_scene` (it frees the old scene the same frame with
no completion hook) — it manages the swap itself, adding the new scene as a
direct child of the root viewport and calling `set_current_scene`, then
`queue_free`-ing the old one.

An exterior scene that needs streamed terrain simply contains its own
`CellTerrainManager` (+ `CellWorld` / `FoliageManager`) inside its `.tscn` —
nothing special, it just loads with the rest of the scene.

> Loading is **poll-based on the main thread** (this 3.5 fork has no threaded
> `ResourceLoader`). The loading screen hides the cost. `max_polls_per_frame`
> (default 4) trades load speed for frame smoothness. Big scenes on a worker
> `Thread` would be a later optimisation.

## The player is yours

`SceneManager` does **not** own or spawn the player. Your existing master
autoload keeps the player; after each load the manager reports where to put it.
Wire it once, in the autoload's `_ready()`:

```gdscript
func _ready():
    SceneManager.set_catalog_path("res://data/scene_catalog.tres")
    SceneManager.set_loading_screen("res://ui/loading_screen.tscn")
    SceneManager.connect("player_spawn_requested", self, "_on_player_spawn")

func _on_player_spawn(xform, spawn_id):
    $Player.global_transform = xform
    $Player/Camera.current = true   # exterior terrain streams off the *current* camera
```

Or, instead of the signal, name a target method:
`SceneManager.set_player_target("/root/GameMaster", "place_player")` — the
manager then calls `place_player(xform, spawn_id)` directly. Default target is
`/root/GameMaster.place_player`.

> **Exterior gotcha:** `CellTerrainManager` streams to whatever camera is
> `current` in the root viewport, not to a "player". After travelling into an
> exterior scene make sure the player's camera is made `current`, or terrain
> won't stream.

## Authoring the catalog

Open the **Scenes** dock (bottom panel). **New** creates a `SceneCatalog`
`.tres`; **Add Current Scene** registers the scene you're editing (id defaults
to the file basename). Per entry: `id`, `scene_path`, `display_name`, `kind`
(Interior/Exterior), `default_spawn`. Keep the `.tres` in version control and
point the runtime at it with `SceneManager.set_catalog_path(...)`.

The dock's **Open** / **Play** buttons are editor conveniences (they call
`EditorInterface.open_scene_from_path` / `play_custom_scene`) — unrelated to the
runtime travel path.

## Wiring doors

In each scene:

- A `SceneSpawnPoint` per arrival point, with a `spawn_id` (e.g. `"entry"`,
  `"from_house"`). Its position + yaw is where the player lands.
- A `SceneDoor` (an `Area` — give it a `CollisionShape`) per exit:
  `target_scene` (a catalog id), `target_spawn` (a `spawn_id` in that scene),
  `trigger_mode` (`On Body Enter` / `On Interact`), `body_filter` (default
  group `player`), `one_shot`, `cooldown`.

`On Interact` doors do nothing until gameplay code calls `door.activate()` — hook
that to your "press E" system.

From script you can also travel directly:
`SceneManager.travel_to("interior_house", "entry")`, and
`SceneManager.travel_back()` / `SceneManager.reload_current()`.

## The loading-screen scene

Any `.tscn`. If its root script has `func set_progress(ratio: float):` the
manager feeds it `0..1` while loading (drive a `ProgressBar`). With no loading
screen set, `SceneTransition` falls back to a `fade_color` fill so travel still
works. `SceneTransition.min_display_time` keeps very fast loads from flashing.

## Known limitations / honest notes

- **Not compiled against your tree yet.** Built against the 3.5 API and the
  `cell_system` headers; verify on your checkout (`vsproj=yes` flags any drift).
- **No persistent object state across unload.** A scene is rebuilt from its
  `.tscn` every time you enter — no Creation-Engine "persistent references",
  no save-game hook yet. `SceneCatalogEntry.keep_loaded` is a placeholder.
- **Main-thread loading.** See the note above.
- **`SceneManager` auto-creates its `SceneTransition`** under the root viewport
  on the first `travel_to` (or `bootstrap()`), via a SceneTree idle callback.
  It is not an autoload and needs no scene setup.

# Items, Recipes & Categories

A main-screen editor tab (top bar, next to *AssetLib*) plus one runtime node.

## Classes — drop into `modules/cell_system/`

| File | Class | Role |
|---|---|---|
| `items_editor_plugin.{h,cpp}` | `ItemsEditor` / `ItemsEditorPlugin` | The **Items & Recipes** main-screen tab (editor only). |
| `item_category_catalog.{h,cpp}` | `ItemCategory`, `ItemCategoryCatalog` | Main + sub categories and the item‑id → category mapping. |
| `cell_item_body.{h,cpp}` | `CellItemBody` | A `RigidBody` that is one dropped item in the world. |

The item/recipe resources themselves live in the **game project** as GDScript
(`ItemResource`, `ItemRecipeResource` and any subclasses — `CunsumableResource`,
`Alphamax`, …). The tab discovers them by script inheritance, so nothing here
needs to know their fields.

## Set the folder first

Project Settings → `cell_system/items/`:

- `directory` (default `res://items`) — root folder scanned for item / recipe
  `.tres`. **Must exist** or the tab shows only a setup prompt.
- `recipes_subdirectory` (default `recipes`) — where *New* writes recipes.
- `category_catalog` (default `res://items/categories.tres`) — the
  `ItemCategoryCatalog` resource, created on first save.

## The tab

Three views via the top switch:

- **Items** — tree of every `.tres` whose script extends `ItemResource`.
  *New* → pick the script class → a dialog asks for **Name** (required),
  **Main / Sub category** (from the catalog) and a **Scene** (`.tscn` picker).
  The tool derives the `id` from the name (slug, made unique), writes
  `<id>.tres`, fills `id` / `name` / `scene_path`, and files the item in the
  catalog. Edit further in the embedded inspector; *Duplicate / Rename / Delete*
  act on the file. The **Category** row re-assigns the selected item later.
- **Recipes** — same, for `ItemRecipeResource`. *Validate Recipes* flags null
  array entries and any string field (`*id*`, `*item*`, `*ingredient*`,
  `*result*`) that doesn't resolve to a known item `id`.
- **Categories** — edit the catalog: *New* main category, *+ Sub*, *Rename*,
  *Delete*. Renames/removes cascade into the assignment map.

## `CellItemBody`

`RigidBody` + `item` (an `ItemResource`). On entering the tree it instances the
item's `scene_path` as `ItemVisual` and — while `auto_build` is on — adds one box
`CollisionShape` sized to that scene's combined visual AABB, with `mass` taken
from the item's `weight`. `CellWorld` already sorts any `RigidBody` into
`DynamicObjects`, so it streams for free.

Pushing: `CellPlayerBody` now tracks its own velocity (transform delta per
physics frame, `get_linear_velocity()`); `CellItemBody::_direct_state_changed`
reads it for every player contact and applies a central impulse scaled by
`push_strength`. `push(Vector3)` is the manual equivalent; `pickup()` emits
`picked_up(item)` and frees the node.

## Known limitations / honest notes

- **Editor tab hosting** mirrors `AssetLibraryEditorPlugin` (control parented to
  the editor viewport, `PRESET_WIDE`, shown on select).
- **Discovery loads each `.tres`** under the folder on refresh to read its
  script — fine for hundreds, not tens of thousands.
- **Recipe validation is heuristic** — `ItemRecipeResource` has no ingredient
  schema yet, so it scans by field-name convention. Tighten once the schema exists.
- **Category assignment needs an `id`** on the item; items with an empty `id`
  can't be filed and the tab says so.
