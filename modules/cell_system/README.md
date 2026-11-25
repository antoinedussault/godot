# Game World (read this first)

`GameWorld` is the root of a playable scene and the **one authority** over
everything laid out on the cell grid. It replaces the old trio of independent
streamers (`CellWorld` + `CellTerrainManager` + `FoliageManager`, each with its
own cell size and its own camera polling).

```
MyLevel (GameWorld)            <- scene root
├── Terrain (CellTerrainManager)   created automatically when has_terrain is on
├── Foliage (FoliageManager)       created automatically when has_terrain is on
├── Village (Spatial)              a folder: looked inside, never touched
│   ├── House1                     <- streamed as one object
│   └── House2
└── Crate (RigidBody)              <- dynamic object
```

## What it owns

| | |
|---|---|
| **Grid** | One origin (the node), one `cell_size`. Terrain runs on it; foliage on `cell_size × foliage_cell_multiple`. No other node stores a cell size, so indices can no longer be read against the wrong grid. |
| **Focus** | The camera currently rendering the viewport. Terrain, foliage and objects are all streamed from that one position, once per frame. Override with `focus_path` or `set_focus_position()`. |
| **Terrain + foliage** | `has_terrain` on: both managers are created as children and kept in step with the grid. Off (an interior): no terrain, and therefore no foliage. Turning it off hides and stops existing managers but keeps their settings. |
| **Objects** | Assigned to a cell by position, **without reparenting**. Out of range, an object is removed from the scene tree — it leaves physics, rendering and processing — and comes back when the camera approaches. |
| **Dynamic objects** | Rigid/kinematic bodies follow the cell they are actually in and are parked while the terrain under them has no collision. |
| **State** | What was removed, moved or changed survives unloading and scene travel. |
| **Layers** | Editor layers, plus a per-layer *Game* flag and `set_layer_enabled()` at runtime. |

## Properties

- `cell_size` (64) — base grid. `world_dir` — where terrain data lives.
- `has_terrain` (on) — exterior vs interior. `foliage_cell_multiple` (4).
  `foliage_dir` — foliage data folder; empty means the same as `world_dir`.
- `stream_objects` (on), `load_radius` (2 cells), `unload_margin` (1 cell of
  hysteresis), `integrate_budget_ms` (2 ms of main thread per frame).
- `focus_path` — optional node to follow instead of the camera.

Set the grid here. The managers' own `cell_size` / `world_dir` are overwritten
from it.

## What counts as an object

Only things that are actually *placed* in the world stream. Walking down from
the `GameWorld`, each child is, in this order:

| The node is… | It is treated as |
|---|---|
| in the `always_loaded` group (or meta), or a system class (the managers, `Camera`, `DirectionalLight`, `SceneSpawnPoint`, `WorldLocationArea`, `TerrainPath`, `CellPlayerBody`) | left alone, subtree included |
| a `RigidBody` / `KinematicBody`, or in the `movable` group | a **dynamic** object |
| an instanced scene | an object if anything in it renders or collides; otherwise a marker, left alone |
| a visual node (mesh, light, particles, CSG) or a static body | an object |
| a node with a script | **logic**: left alone, subtree included |
| an `Area` | left alone |
| any other script-less helper (`Spatial`, `Position3D`, `Node`, an old `Cell`) | a **folder**: looked inside |

An object streams whole, subtree included. It is in range when **any cell its
bounding box touches** is in range, so a 300 m cliff does not vanish because
its pivot is three cells away.

The "script = logic" rule is what keeps a weather controller, a sky rig or a
camera-following particle system loaded no matter where its origin is. If you
do want a scripted, non-visual node to stream, make it an instanced scene with
something visible in it. A static object that is moved by a script should be in
the `movable` group, otherwise it stays filed under the cell it started in.

## Baking (streaming from disk)

Unbaked, the whole scene is loaded and out-of-range objects are merely kept out
of the tree. That fixes physics, draw and process cost but not memory or load
time. **World dock → Bake World** writes, next to the scene:

```
my_level.world/world.scn             the scene without its static objects
my_level.world/cells/cell_X_Z.scn    one file per cell
my_level.world/bake.cfg              timestamp of the scene it was baked from
```

`SceneManager.travel_to()` then loads `world.scn` instead, and each cell file is
loaded when the camera gets near and freed when it leaves. The authoring scene
is never modified. If the scene is newer than its bake, the game warns and uses
the scene itself — re-bake after editing. Dynamic objects, and objects that
span several cells, stay in `world.scn`.
Running the scene directly from the editor always uses the unbaked path.

## Persistent state

```gdscript
var save = SceneManager.get_world_states()   # every visited scene, plain data
SceneManager.set_world_states(save)          # on load
```

Tracked automatically: objects that were freed (stay gone), dynamic objects
that moved, objects added through `GameWorld.add_object(node)` (they must be
instanced scenes to survive a save), and runtime layer toggles. A node adds its
own data by implementing:

```gdscript
func _save_cell_state(): return {"open": is_open}
func _load_cell_state(state): is_open = state.open
```

## Signals and queries

`focus_cell_changed(cx, cz)`, `cell_loaded(cx, cz)`, `cell_unloaded(cx, cz)`;
`world_to_cell(pos)`, `get_cell_origin(cx, cz)`, `get_cell_state(cx, cz)`,
`is_ground_ready(pos)`, `prime(pos)` (load everything around a point now —
`SceneManager` calls it on arrival, behind the loading screen).

## Coming from the old system

- `CellWorld` and `Cell` still load but do nothing; `GameWorld` treats them as
  folders, so an old scene streams correctly as it is. **World dock → Migrate
  Legacy Scene** removes the wrappers for good.
- "Sort Into Cells" is gone — there is nothing to sort.
- `CellPlayerBody` no longer has `cell_size` / `cell_world_path`; it finds the
  `GameWorld` by itself.
- On first load of an old scene, `GameWorld` adopts the grid and the data
  folders from its existing managers, the load radius from its old `CellWorld`
  child, and treats a scene that had no terrain manager as an interior.

## Testing it

`tests/gameworld/` is a tiny project that drives every feature above from a
script and prints `ok` / `FAIL` per check:

```bash
bin\godot.windows.opt.tools.64.exe --no-window --path modules\cell_system\tests\gameworld
```

It must end with `RESULT: 0 failure(s)`. The same folder holds three scenes for
an editor smoke test (`-e --no-window --path … res://ext.tscn`, also
`legacy_interior.tscn` and `legacy_exterior.tscn`). Set `TUNA_CELL_TRACE=1` to
get one line per `GameWorld` saying what it decided to stream.

## Honest notes

- **Loading a cell file is main-thread.** It is polled within the frame budget,
  but the final `instance()` of one cell is a single call, so a very dense cell
  can still cost a visible frame. Smaller `cell_size` spreads it.
- **Bake copies objects with `Node.duplicate()`.** Changes made to the children
  of an instanced scene through *Editable Children* are not carried into the
  cell file.
- **A streamed-out object has no node path.** Scripts that hold a path to an
  object that can unload need it in `always_loaded`.
- **Rotation of the `GameWorld` node is not supported**; translation is.

---

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

A `GameWorld` with `has_terrain` on creates the `FoliageManager` for you, next
to the terrain manager, and sets its `terrain_manager_path`, `world_dir` and
`cell_size` from the world grid - there is nothing to keep in sync by hand.
Assign `FoliageType` resources to its `types`.

The focus position (the active camera) is pushed in by the `GameWorld` each
frame; the manager uses it for streaming, LOD and collision.

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
world_location_area.h / .cpp      WorldLocationArea — a named, discoverable region (Area)
scene_manager_editor_plugin.*     "Scenes" bottom-panel dock (TOOLS_ENABLED)
game_world.h / .cpp               EXTENDS your existing GameWorld with a location registry
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

Make each scene's root a `GameWorld`. An exterior leaves `has_terrain` on and
gets its terrain and foliage managers automatically; an interior turns it off.
On arrival the manager calls `GameWorld.prime()` at the spawn point, so the
loading screen lifts on a world that is already there.

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

> **Exterior gotcha:** `GameWorld` streams to whatever camera is `current` in
> the root viewport, not to a "player". After travelling into a scene make sure
> the player's camera is made `current`, or nothing streams.

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

## World Locations

`WorldLocationArea` (an `Area`) marks a region of a scene as a named location —
a settlement, dungeon, landmark — and fires `location_entered` /
`location_exited` as a `body_filter` body crosses its bounds, plus
`location_discovered` the first time it's entered. It's independent of
`SceneManager`/`SceneDoor`: it lives inside a scene and never loads anything,
it only announces itself.

When a scene's root is a `GameWorld`, every `WorldLocationArea` under it is
reachable without holding a node reference:

```gdscript
var world = SceneManager.get_world_game()          # current scene's GameWorld root, or null
var loc = SceneManager.get_location_by_id("sanctuary")
var all = SceneManager.get_locations()             # every WorldLocationArea under it
```

(equivalently `world.get_location_by_id(...)` / `world.get_locations()` if you
already hold the `GameWorld`). The registry is a live group lookup
(`"world_locations"`), not a stored list on `GameWorld` — nothing needs to
(un)register on add/remove/reparent, and nesting one `WorldLocationArea` inside
another (a house inside a settlement) just works, each firing independently.

A scene that isn't `GameWorld`-rooted can still use `WorldLocationArea` — its
signals fire regardless — it just isn't reachable through the registry
shortcuts above.

## The loading-screen scene

Any `.tscn`. If its root script has `func set_progress(ratio: float):` the
manager feeds it `0..1` while loading (drive a `ProgressBar`). With no loading
screen set, `SceneTransition` falls back to a `fade_color` fill so travel still
works. `SceneTransition.min_display_time` keeps very fast loads from flashing.

## Known limitations / honest notes

- **Not compiled against your tree yet.** Built against the 3.5 API and the
  `cell_system` headers; verify on your checkout (`vsproj=yes` flags any drift).
- **Persistent state lives on `GameWorld`**, not here: see *Persistent state*
  at the top. `SceneManager.get_world_states()` / `set_world_states()` are the
  save-game hook. `SceneCatalogEntry.keep_loaded` is still a placeholder.
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
from the item's `weight`. `GameWorld` treats any `RigidBody` as a dynamic
object, so it streams for free; hand a dropped item to
`GameWorld.add_object()`.

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
