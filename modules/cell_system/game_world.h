#ifndef GAME_WORLD_H
#define GAME_WORLD_H

#include "core/array.h"
#include "core/dictionary.h"
#include "core/io/resource_loader.h"
#include "core/map.h"
#include "core/set.h"
#include "scene/3d/spatial.h"

class CellTerrainManager;
class FoliageManager;

// The scene root of a playable world, and the ONE authority over everything
// that is laid out on the cell grid.
//
// What it owns
// ------------
//   1. The grid. One origin (this node), one base cell_size. Terrain runs on the
//      base grid, foliage on an integer multiple of it. Nothing else in the
//      module stores a cell size of its own any more, so an index can no longer
//      be handed to a node that reads it against a different grid.
//
//   2. The streaming focus. The camera currently rendering this viewport (or
//      focus_path / set_focus_position when you want to override it). Terrain,
//      foliage and objects are all streamed from this one position, in this
//      node's _process - none of them polls a camera of its own.
//
//   3. The terrain and the foliage. With has_terrain on, a CellTerrainManager
//      and a FoliageManager are created as children automatically and kept in
//      step with the grid. With it off (an interior) there is no terrain and
//      therefore no foliage either.
//
//   4. Object streaming. Every object under this node is assigned to a cell by
//      its position - WITHOUT being reparented. Out-of-range objects are taken
//      out of the scene tree (so they leave the physics server, the renderer
//      and the process lists, which hiding them never did) and come back when
//      the focus approaches. After bake_world() they are not even in memory:
//      each cell lives in its own file, loaded and freed on demand.
//
//   5. Dynamic objects. Rigid/kinematic bodies follow the cell they are
//      actually in, and are parked whenever the ground under them has no
//      collision, so nothing simulates over terrain that is not there.
//
//   6. Persistent state. What was removed, moved or changed survives an unload
//      and a trip to another scene, and get_world_state() / set_world_state()
//      hand the whole thing to a save game.
//
//   7. Scene layers, which now also work at runtime (set_layer_enabled), and
//      the WorldLocationArea registry.
//
// What counts as "an object"
// --------------------------
// Only things that are actually PLACED in the world stream. Walking down from
// this node, each child is, in this order:
//
//   - tagged always_loaded (group or meta), or one of the system classes
//     (managers, Camera, DirectionalLight, SceneSpawnPoint, WorldLocationArea,
//     TerrainPath, CellPlayerBody)          -> left alone, subtree included.
//   - a RigidBody / KinematicBody / "movable" -> a DYNAMIC unit.
//   - an instanced scene                     -> a unit, if anything in it
//     renders or collides; otherwise it is a marker and is left alone.
//   - a visual node or a static body         -> a unit.
//   - a node with a script                   -> LOGIC: left alone, subtree
//     included. A weather controller whose particles follow the camera must
//     not be unloaded because its origin is far away.
//   - an Area                                -> left alone (triggers are cheap
//     and a large one has to keep working from anywhere).
//   - any other script-less helper (Spatial, Position3D, Node, an old Cell)
//                                            -> a FOLDER: looked inside.
//
// A unit streams whole, subtree included. So "Village/Houses/House3" streams
// House3; the two folders above it stay put and are never touched.
//
// A unit is in range when any cell its bounding box touches is in range, not
// just the cell holding its origin - a cliff 300 m long must not vanish
// because its pivot is three cells away.
//
// layer_data shape: { "next_id": int, "layers": Array }
// each entry in "layers" (and recursively each entry's "children"):
//   { "id": int, "name": String, "visible": bool, "runtime": bool, "children": Array }
// "visible" is the EDITOR toggle only and never reaches the game. "runtime" is
// whether the layer is loaded when the game starts.
class GameWorld : public Spatial {
	GDCLASS(GameWorld, Spatial);

public:
	enum CellState {
		CELL_UNLOADED, // out of range, nothing of it in the tree
		CELL_LOADING,  // in range, still reading its file or attaching objects
		CELL_ACTIVE,   // in range and fully in the tree
		CELL_UNLOADING // out of range, still being taken out
	};

private:
	// --- Grid ---
	float cell_size = 64.0f;
	int foliage_cell_multiple = 4;
	String world_dir = "user://world";
	// Empty = same as world_dir. Old scenes kept foliage in a folder of its own.
	String foliage_dir;

	// --- Subsystems ---
	bool has_terrain = true;
	CellTerrainManager *_terrain = nullptr;
	FoliageManager *_foliage = nullptr;

	// --- Object streaming ---
	bool stream_objects = true;
	int load_radius = 2;
	// Extra rings a loaded cell is kept for before it is dropped. Without it a
	// focus pacing along a cell border would load and unload the same row of
	// cells on every step.
	int unload_margin = 1;
	// Main-thread time one frame may spend putting objects in / taking them out.
	float integrate_budget_ms = 2.0f;

	// --- Focus ---
	NodePath focus_path;
	bool _focus_forced = false;
	Vector3 _forced_focus;
	bool _has_focus_cell = false;
	int _focus_cx = 0, _focus_cz = 0;

	// --- Layers ---
	Dictionary layer_data;
	Map<int, bool> _layer_override; // runtime set_layer_enabled() calls
	Map<int, bool> _layer_enabled;  // resolved: own flag AND every ancestor's
	Vector<ObjectID> _layer_nodes;  // always-loaded Spatials carrying layer ids
	Set<ObjectID> _layer_hidden;    // the ones a disabled layer hid (and may un-hide)
	void _resolve_layers();
	void _apply_layers();
	bool _layers_allow(const Vector<int> &p_ids) const;

	// --- Bake (set only inside a baked runtime scene; see bake_world) ---
	String baked_dir;
	PoolIntArray baked_cells; // cx, cz pairs
	String source_scene;

	// 0 = scene from before the overhaul; adopted on first load.
	int format_version = 0;
	void _adopt_legacy_settings();

	// One streamable object.
	struct Unit {
		ObjectID id = 0;
		ObjectID parent_id = 0;
		String uid;          // path from this node when first seen: its save-game identity
		int64_t cell = 0;    // key of the cell it currently belongs to
		bool dynamic = false;
		bool from_file = false; // freed on unload instead of parked
		bool attached = true;
		bool spawned = false;   // added at runtime through add_object()
		Vector3 last_pos;       // global origin, valid while parked
		// Spans more than one cell: streamed by this cell rectangle rather than
		// with its origin cell.
		bool large = false;
		int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
		Vector<int> layers;
	};

	struct CellRec {
		int cx = 0, cz = 0;
		bool wanted = false;
		bool announced = false; // cell_loaded emitted and not yet cell_unloaded
		bool has_file = false;
		int file_state = 0; // 0 not loaded, 1 loading, 2 instanced
		Ref<ResourceInteractiveLoader> loader;
		Vector<Unit *> units;
	};

	Map<int64_t, CellRec> _cells;
	Vector<Unit *> _dynamic;
	Vector<Unit *> _large;
	bool _large_dirty = false;
	void _evaluate_large();
	// Cell rectangle covered by everything that renders under p_node.
	bool _cell_rect(Node *p_node, int &r_x0, int &r_z0, int &r_x1, int &r_z1) const;
	static bool _has_presence(Node *p_node);
	Set<int64_t> _work; // cells with loading/attaching/detaching still owed
	bool _partitioned = false;
	bool _streamed_once = false;
	float _dynamic_accum = 0.0f;
	int _spawn_counter = 0;

	// Persistent state (see get_world_state).
	Set<String> _st_removed;
	Map<String, Variant> _st_custom;
	// set_world_state() called before the objects were sorted into units.
	Dictionary _pending_state;
	bool _has_pending_state = false;

	static int64_t _key(int x, int z);
	static void _unkey(int64_t k, int &x, int &z);

	bool _is_runtime() const;
	bool _is_folder(Node *p_node) const;
	bool _is_always_loaded(Node *p_node) const;
	static bool _is_dynamic(Node *p_node);
	// Result of walking the tree: what streams, and with which layers.
	struct Collected {
		Vector<Node *> statics;
		Vector<Vector<int> > static_layers;
		Vector<Node *> dynamics;
		Vector<Vector<int> > dynamic_layers;
		Vector<Node *> layer_nodes; // always loaded, but on a layer
	};
	void _collect(Node *p_node, const Vector<int> &p_inherited_layers, Collected &r_out);
	static void _append_layer_ids(Node *p_node, Vector<int> &r_ids);

	void _partition();
	Unit *_make_unit(Node *p_node, bool p_dynamic, const Vector<int> &p_layers);
	CellRec &_cell_rec(int64_t p_key);
	void _move_unit_to_cell(Unit *p_unit, int64_t p_key);
	void _drop_unit(Unit *p_unit);
	void _free_all_units();

	Node *_unit_node(const Unit *p_unit) const;
	bool _unit_wanted(const Unit *p_unit) const;
	void _attach(Unit *p_unit);
	// p_allow_free: a unit that came from a cell file is freed rather than
	// parked, since the file can rebuild it.
	void _detach(Unit *p_unit, bool p_allow_free);

	bool _cell_in_range(int cx, int cz, bool p_currently_wanted) const;
	void _evaluate_cells();
	// Returns true while the cell still has work left.
	bool _work_cell(CellRec &r_cell, uint64_t p_deadline_usec, bool p_sync);
	void _instance_cell_file(CellRec &r_cell);
	void _run_work(bool p_sync);
	void _tick_dynamic();

	void _ensure_subsystems();
	void _find_subsystems();
	void _push_grid_to_subsystems();
	void _apply_has_terrain();
	bool _terrain_active() const;

	void _stream(const Vector3 &p_focus, bool p_force_sync);
	String _state_key() const;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	// --- Grid ---
	void set_cell_size(float p_size);
	float get_cell_size() const { return cell_size; }
	void set_foliage_cell_multiple(int p_multiple);
	int get_foliage_cell_multiple() const { return foliage_cell_multiple; }
	void set_world_dir(const String &p_dir);
	String get_world_dir() const { return world_dir; }
	void set_foliage_dir(const String &p_dir);
	String get_foliage_dir() const { return foliage_dir; }

	// World position -> (cx, cz) on the base grid, as a Vector2.
	Vector2 world_to_cell(const Vector3 &p_world) const;
	void world_to_cell_i(const Vector3 &p_world, int &r_cx, int &r_cz) const;
	// World-space corner (minimum X/Z) of a base-grid cell.
	Vector3 get_cell_origin(int cx, int cz) const;

	// --- Subsystems ---
	void set_has_terrain(bool p_enabled);
	bool get_has_terrain() const { return has_terrain; }
	CellTerrainManager *get_terrain() const { return _terrain; }
	FoliageManager *get_foliage() const { return _foliage; }
	Node *get_terrain_node() const;
	Node *get_foliage_node() const;

	// --- Focus ---
	void set_focus_path(const NodePath &p_path) { focus_path = p_path; }
	NodePath get_focus_path() const { return focus_path; }
	// Pins the focus to a fixed point (a cutscene, a loading screen). Cleared by
	// clear_focus_position(), after which the camera is followed again.
	void set_focus_position(const Vector3 &p_world);
	void clear_focus_position();
	bool has_focus() const;
	Vector3 get_focus_position() const;
	Vector2 get_focus_cell() const { return Vector2(_focus_cx, _focus_cz); }

	// Brings everything around p_world in RIGHT NOW, blocking: terrain collision
	// and every object in range. Call it behind a loading screen so the player
	// never lands in an empty world. SceneManager does it on every arrival.
	void prime(const Vector3 &p_world);
	// True once the ground under p_world is streamed in (always true without a
	// terrain). CellPlayerBody waits on this instead of falling through.
	bool is_ground_ready(const Vector3 &p_world) const;
	// Builds the terrain collision under p_world right now, blocking. Unlike
	// prime() it touches no objects, so it is safe to call from a _ready.
	void ensure_ground(const Vector3 &p_world);

	// --- Object streaming ---
	void set_stream_objects(bool p_enabled);
	bool get_stream_objects() const { return stream_objects; }
	void set_load_radius(int p_radius);
	int get_load_radius() const { return load_radius; }
	void set_unload_margin(int p_margin) { unload_margin = MAX(0, p_margin); }
	int get_unload_margin() const { return unload_margin; }
	void set_integrate_budget_ms(float p_ms) { integrate_budget_ms = MAX(0.0f, p_ms); }
	float get_integrate_budget_ms() const { return integrate_budget_ms; }

	int get_cell_state(int cx, int cz) const;
	int get_pending_cell_count() const { return _work.size(); }
	int get_object_count() const;
	int get_attached_object_count() const;

	// Hands a node created at runtime (a dropped item, a spawned NPC) to the
	// streamer. It is parented under this node if it has no parent yet. It only
	// survives a save game if it is an instanced scene, since that is the only
	// thing it can be rebuilt from.
	void add_object(Node *p_node);

	// --- Persistent state ---
	// { "removed": [uid], "moved": { uid: Transform }, "custom": { uid: Variant },
	//   "spawned": [ { "scene", "xf", "custom" } ], "layers": { id: bool } }
	// Plain data, safe to store_var() into a save file. A node contributes its
	// own state by implementing _save_cell_state() -> Variant and
	// _load_cell_state(state).
	Dictionary get_world_state();
	void set_world_state(const Dictionary &p_state);

	// --- Baking ---
	// Writes "<scene>.world/": one .scn per cell holding that cell's static
	// objects, plus world.scn, a copy of this scene with those objects taken
	// out. SceneManager loads world.scn in place of the authoring scene, so a
	// baked world costs the memory of what is near the player, not of the whole
	// map. The authoring scene itself is never modified.
	Error bake_world();
	void clear_bake();
	bool has_bake() const;
	String get_bake_dir() const;
	// The scene SceneManager should actually load for p_scene_path: its baked
	// world.scn when there is one and it is not older than the scene.
	static String resolve_baked_scene(const String &p_scene_path);

	void set_baked_dir(const String &p_dir) { baked_dir = p_dir; }
	String get_baked_dir() const { return baked_dir; }
	void set_baked_cells(const PoolIntArray &p_cells) { baked_cells = p_cells; }
	PoolIntArray get_baked_cells() const { return baked_cells; }
	void set_source_scene(const String &p_path) { source_scene = p_path; }
	String get_source_scene() const { return source_scene; }
	void set_format_version(int p_version) { format_version = p_version; }
	int get_format_version() const { return format_version; }

	// Converts a scene from before the overhaul: flattens Cell / CellWorld /
	// DynamicObjects wrappers back into the hierarchy and adopts the old
	// managers' grid settings. p_owner is the edited scene root.
	void migrate_legacy(Node *p_owner = nullptr);

	// --- Layers ---
	void set_layer_data(const Dictionary &p_data);
	Dictionary get_layer_data() const { return layer_data; }
	int get_layer_id(const String &p_name) const;
	void set_layer_enabled(int p_id, bool p_enabled);
	bool is_layer_enabled(int p_id) const;

	// --- Location registry ---
	// Every WorldLocationArea under this GameWorld, found live through the
	// "world_locations" group, so nothing has to (un)register.
	Node *get_location_by_id(const String &p_id) const;
	Array get_locations() const;

	virtual String get_configuration_warning() const;

	GameWorld();
	~GameWorld();
};

VARIANT_ENUM_CAST(GameWorld::CellState);

#endif // GAME_WORLD_H
