#include "game_world.h"

#include "core/engine.h"
#include "core/io/resource_saver.h"
#include "core/list.h"
#include "core/os/dir_access.h"
#include "core/os/file_access.h"
#include "core/os/os.h"
#include "scene/3d/camera.h"
#include "scene/3d/area.h"
#include "scene/3d/physics_body.h"
#include "scene/3d/visual_instance.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/resources/packed_scene.h"

#include "cell_terrain_manager.h"
#include "cell_world.h"
#include "foliage_manager.h"
#include "scene_manager.h"
#include "world_location_area.h"

static const int GAME_WORLD_FORMAT = 2;
// How often dynamic objects are re-sorted into the cell they are really in.
static const float DYNAMIC_TICK_SECONDS = 0.25f;

int64_t GameWorld::_key(int x, int z) {
	return ((int64_t)(uint32_t)x << 32) | (uint32_t)z;
}

void GameWorld::_unkey(int64_t k, int &x, int &z) {
	x = (int32_t)(k >> 32);
	z = (int32_t)(k & 0xffffffff);
}

GameWorld::GameWorld() {}

GameWorld::~GameWorld() {
	_free_all_units();
}

bool GameWorld::_is_runtime() const {
	return is_inside_tree() && !Engine::get_singleton()->is_editor_hint();
}

// ---------------------------------------------------------------------------
// Grid
// ---------------------------------------------------------------------------

void GameWorld::set_cell_size(float p_size) {
	cell_size = MAX(1.0f, p_size);
	_push_grid_to_subsystems();
}

void GameWorld::set_foliage_cell_multiple(int p_multiple) {
	foliage_cell_multiple = MAX(1, p_multiple);
	_push_grid_to_subsystems();
}

void GameWorld::set_world_dir(const String &p_dir) {
	world_dir = p_dir;
	_push_grid_to_subsystems();
}

void GameWorld::set_foliage_dir(const String &p_dir) {
	foliage_dir = p_dir;
	_push_grid_to_subsystems();
}

void GameWorld::world_to_cell_i(const Vector3 &p_world, int &r_cx, int &r_cz) const {
	// The grid hangs off this node's origin. Subtracting it here, in the one
	// place every conversion goes through, is what lets a GameWorld sit
	// anywhere instead of only at (0, 0, 0).
	Vector3 origin = is_inside_tree() ? get_global_transform().origin : get_transform().origin;
	r_cx = (int)Math::floor((p_world.x - origin.x) / cell_size);
	r_cz = (int)Math::floor((p_world.z - origin.z) / cell_size);
}

Vector2 GameWorld::world_to_cell(const Vector3 &p_world) const {
	int cx, cz;
	world_to_cell_i(p_world, cx, cz);
	return Vector2(cx, cz);
}

Vector3 GameWorld::get_cell_origin(int cx, int cz) const {
	Vector3 origin = is_inside_tree() ? get_global_transform().origin : get_transform().origin;
	return origin + Vector3(cx * cell_size, 0.0f, cz * cell_size);
}

// ---------------------------------------------------------------------------
// Subsystems (terrain + foliage)
// ---------------------------------------------------------------------------

static Node *_gw_find_by_class(Node *p_node, const String &p_class) {
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *c = p_node->get_child(i);
		if (c->is_class(p_class)) {
			return c;
		}
		// A nested GameWorld owns its own managers.
		if (c->is_class("GameWorld")) {
			continue;
		}
		Node *found = _gw_find_by_class(c, p_class);
		if (found) {
			return found;
		}
	}
	return nullptr;
}

void GameWorld::_find_subsystems() {
	_terrain = Object::cast_to<CellTerrainManager>(_gw_find_by_class(this, "CellTerrainManager"));
	_foliage = Object::cast_to<FoliageManager>(_gw_find_by_class(this, "FoliageManager"));
}

bool GameWorld::_terrain_active() const {
	return has_terrain && _terrain != nullptr;
}

Node *GameWorld::get_terrain_node() const {
	return _terrain;
}

Node *GameWorld::get_foliage_node() const {
	return _foliage;
}

void GameWorld::_push_grid_to_subsystems() {
	if (_terrain) {
		_terrain->set_cell_size(cell_size);
		_terrain->set_world_dir(world_dir);
	}
	if (_foliage) {
		_foliage->set_cell_size(cell_size * foliage_cell_multiple);
		_foliage->set_world_dir(foliage_dir.empty() ? world_dir : foliage_dir);
		if (_terrain) {
			_foliage->set_terrain_manager_path(_foliage->get_path_to(_terrain));
		}
	}
}

void GameWorld::_adopt_legacy_settings() {
	// A scene saved before the overhaul kept the grid on the managers, and this
	// node's own values are just defaults. Take the managers' word for it once,
	// otherwise the first thing a legacy scene would do is have its terrain
	// re-read against the wrong cell size.
	if (format_version >= GAME_WORLD_FORMAT) {
		return;
	}
	// An old GameWorld scene with content but no terrain manager was an
	// interior. has_terrain defaults to on, and without this such a scene would
	// sprout a terrain and a foliage manager the first time it is opened.
	// Only for a scene that came from a file: a world assembled from code has
	// no version stamp either, and there has_terrain means what it says.
	const Node *root = get_owner() ? get_owner() : this;
	if (!root->get_filename().empty() && !_terrain && !_foliage && get_child_count() > 0) {
		has_terrain = false;
	}
	if (_terrain) {
		cell_size = MAX(1.0f, _terrain->get_cell_size());
		world_dir = _terrain->get_world_dir();
	}
	if (_foliage) {
		const float fcs = _foliage->get_cell_size();
		foliage_cell_multiple = MAX(1, (int)Math::round(fcs / cell_size));
		if (!_terrain) {
			world_dir = _foliage->get_world_dir();
		} else if (_foliage->get_world_dir() != world_dir) {
			// Foliage data was kept apart from the terrain's; keep reading it
			// from where it actually is.
			foliage_dir = _foliage->get_world_dir();
		}
	}
	// The old CellWorld child was the object streamer, and its radius is the
	// one the scene was tuned with.
	CellWorld *old_cw = Object::cast_to<CellWorld>(_gw_find_by_class(this, "CellWorld"));
	if (old_cw) {
		load_radius = old_cw->get_load_radius();
	}
	// Only stamped in the editor, where it gets saved with the scene. At runtime
	// the adoption is simply repeated on each load - it is a few assignments.
	if (Engine::get_singleton()->is_editor_hint()) {
		format_version = GAME_WORLD_FORMAT;
	}
}

void GameWorld::_ensure_subsystems() {
	if (!is_inside_tree()) {
		return;
	}
	_find_subsystems();
	_adopt_legacy_settings();

	if (has_terrain) {
		Node *owner = nullptr;
		bool can_create = true;
		if (Engine::get_singleton()->is_editor_hint()) {
			// Only inside the scene actually being edited. A GameWorld that shows
			// up as part of an instanced sub-scene must not grow children there:
			// they would be saved into the wrong scene.
#ifdef TOOLS_ENABLED
			Node *edited = get_tree()->get_edited_scene_root();
			can_create = edited && (edited == this || edited->is_a_parent_of(this));
			owner = (edited == this) ? (Node *)this : get_owner();
			if (can_create && !owner) {
				can_create = false;
			}
#else
			can_create = false;
#endif
		}
		if (can_create && !_terrain) {
			_terrain = memnew(CellTerrainManager);
			_terrain->set_name("Terrain");
			add_child(_terrain);
			if (owner) {
				_terrain->set_owner(owner);
			}
		}
		if (can_create && !_foliage) {
			_foliage = memnew(FoliageManager);
			_foliage->set_name("Foliage");
			add_child(_foliage);
			if (owner) {
				_foliage->set_owner(owner);
			}
		}
	}

	_push_grid_to_subsystems();
	if (_terrain) {
		_terrain->set_external_focus(true);
		_terrain->set_streaming_enabled(has_terrain);
	}
	if (_foliage) {
		_foliage->set_external_focus(true);
		_foliage->set_streaming_enabled(has_terrain);
	}
	update_configuration_warning();
}

void GameWorld::set_has_terrain(bool p_enabled) {
	if (has_terrain == p_enabled) {
		return;
	}
	has_terrain = p_enabled;
	_apply_has_terrain();
}

void GameWorld::_apply_has_terrain() {
	if (!is_inside_tree()) {
		return;
	}
	// Turning the terrain off never deletes the managers: their layers,
	// textures and foliage types are hours of setup, and a checkbox must not be
	// able to throw that away. They are switched off and hidden instead, and a
	// GameWorld that was created without a terrain never gets them at all.
	_ensure_subsystems();
	if (_partitioned) {
		// Dynamic objects are parked on missing ground; with no terrain there is
		// no ground rule any more, so let them all be reconsidered.
		_dynamic_accum = DYNAMIC_TICK_SECONDS;
	}
}

// ---------------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------------

void GameWorld::set_focus_position(const Vector3 &p_world) {
	_focus_forced = true;
	_forced_focus = p_world;
}

void GameWorld::clear_focus_position() {
	_focus_forced = false;
}

bool GameWorld::has_focus() const {
	if (_focus_forced) {
		return true;
	}
	if (!is_inside_tree()) {
		return false;
	}
	if (!focus_path.is_empty() && Object::cast_to<Spatial>(get_node_or_null(focus_path))) {
		return true;
	}
	Viewport *vp = get_viewport();
	return vp && vp->get_camera();
}

Vector3 GameWorld::get_focus_position() const {
	if (_focus_forced) {
		return _forced_focus;
	}
	if (!is_inside_tree()) {
		return Vector3();
	}
	if (!focus_path.is_empty()) {
		Spatial *s = Object::cast_to<Spatial>(get_node_or_null(focus_path));
		if (s) {
			return s->get_global_transform().origin;
		}
	}
	// The camera that is actually rendering, not "the player": what has to be
	// loaded is what is on screen, and that stays true for a third-person rig,
	// a vehicle camera or a cutscene.
	Viewport *vp = get_viewport();
	Camera *cam = vp ? vp->get_camera() : nullptr;
	return cam ? cam->get_global_transform().origin : Vector3();
}

void GameWorld::ensure_ground(const Vector3 &p_world) {
	if (_terrain_active()) {
		_terrain->ensure_collision_at_world(p_world);
	}
}

bool GameWorld::is_ground_ready(const Vector3 &p_world) const {
	if (!_terrain_active()) {
		return true;
	}
	return _terrain->get_cell_at_world(p_world) != nullptr;
}

// ---------------------------------------------------------------------------
// Sorting the tree into units
// ---------------------------------------------------------------------------

bool GameWorld::_is_folder(Node *p_node) const {
	// A script-less helper that is not an instanced scene: something to look
	// inside, never something to stream.
	return p_node->get_script().is_null() && p_node->get_filename().empty();
}

bool GameWorld::_is_always_loaded(Node *p_node) const {
	if (p_node->is_in_group("always_loaded") || p_node->is_in_group("player")) {
		return true;
	}
	if (p_node->has_meta("always_loaded") && (bool)p_node->get_meta("always_loaded")) {
		return true;
	}
	static const char *classes[] = {
		"CellTerrainManager", "FoliageManager", "CellGridVisualizer", "Camera",
		"DirectionalLight", "SceneSpawnPoint", "WorldLocationArea", "TerrainPath",
		"CellPlayerBody", "GameWorld", nullptr
	};
	for (int i = 0; classes[i]; i++) {
		if (p_node->is_class(classes[i])) {
			return true;
		}
	}
	return false;
}

// True when something under p_node renders or collides - i.e. it occupies a
// place in the world, as opposed to a marker or a piece of logic.
bool GameWorld::_has_presence(Node *p_node) {
	if (Object::cast_to<VisualInstance>(p_node) || Object::cast_to<PhysicsBody>(p_node)) {
		return true;
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		if (_has_presence(p_node->get_child(i))) {
			return true;
		}
	}
	return false;
}

static void _gw_merge_aabb(Node *p_node, AABB &r_aabb, bool &r_any) {
	VisualInstance *vi = Object::cast_to<VisualInstance>(p_node);
	if (vi && vi->is_inside_tree()) {
		const AABB a = vi->get_transformed_aabb();
		if (r_any) {
			r_aabb.merge_with(a);
		} else {
			r_aabb = a;
			r_any = true;
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_gw_merge_aabb(p_node->get_child(i), r_aabb, r_any);
	}
}

bool GameWorld::_cell_rect(Node *p_node, int &r_x0, int &r_z0, int &r_x1, int &r_z1) const {
	AABB aabb;
	bool any = false;
	_gw_merge_aabb(p_node, aabb, any);
	if (!any) {
		return false;
	}
	world_to_cell_i(aabb.position, r_x0, r_z0);
	world_to_cell_i(aabb.position + aabb.size, r_x1, r_z1);
	return true;
}

bool GameWorld::_is_dynamic(Node *p_node) {
	if (Object::cast_to<RigidBody>(p_node) || Object::cast_to<KinematicBody>(p_node)) {
		return true; // VehicleBody is a RigidBody
	}
	if (p_node->is_in_group("movable")) {
		return true;
	}
	return p_node->has_meta("movable") && (bool)p_node->get_meta("movable");
}

void GameWorld::_append_layer_ids(Node *p_node, Vector<int> &r_ids) {
	if (!p_node->has_meta("_scene_layer_ids")) {
		return;
	}
	Array ids = p_node->get_meta("_scene_layer_ids");
	for (int i = 0; i < ids.size(); i++) {
		int id = ids[i];
		if (r_ids.find(id) < 0) {
			r_ids.push_back(id);
		}
	}
}

void GameWorld::_collect(Node *p_node, const Vector<int> &p_inherited_layers, Collected &r_out) {
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *c = p_node->get_child(i);
		// A layer put on a folder covers everything inside it.
		Vector<int> layers = p_inherited_layers;
		_append_layer_ids(c, layers);
		Spatial *spatial = Object::cast_to<Spatial>(c);

		if (_is_always_loaded(c)) {
			if (layers.size() && spatial) {
				r_out.layer_nodes.push_back(c);
			}
			continue;
		}
		if (_is_dynamic(c)) {
			r_out.dynamics.push_back(c);
			r_out.dynamic_layers.push_back(layers);
			continue;
		}

		const bool instanced = !c->get_filename().empty();
		const bool placed = spatial && (instanced ? _has_presence(c) : (Object::cast_to<VisualInstance>(c) || Object::cast_to<PhysicsBody>(c) || c->is_class("MeshPacker")));
		if (placed) {
			r_out.statics.push_back(c);
			r_out.static_layers.push_back(layers);
			continue;
		}
		// Not placed content. A folder is looked inside; anything else - a
		// scripted controller, a trigger Area, a marker scene, a non-spatial
		// node - is logic and stays loaded with everything under it.
		const bool logic = instanced || Object::cast_to<Area>(c) || (!spatial && c->get_class() != "Node");
		if (!logic && _is_folder(c)) {
			_collect(c, layers, r_out);
			continue;
		}
		if (layers.size() && spatial) {
			r_out.layer_nodes.push_back(c);
		}
	}
}

// ---------------------------------------------------------------------------
// Unit bookkeeping
// ---------------------------------------------------------------------------

GameWorld::CellRec &GameWorld::_cell_rec(int64_t p_key) {
	Map<int64_t, CellRec>::Element *E = _cells.find(p_key);
	if (E) {
		return E->get();
	}
	CellRec rec;
	_unkey(p_key, rec.cx, rec.cz);
	// A cell first heard of mid-game (a body rolled into it) takes whatever the
	// range test says right now, so it does not wait for the next focus change.
	rec.wanted = _streamed_once ? _cell_in_range(rec.cx, rec.cz, false) : false;
	rec.announced = rec.wanted;
	return _cells.insert(p_key, rec)->get();
}

GameWorld::Unit *GameWorld::_make_unit(Node *p_node, bool p_dynamic, const Vector<int> &p_layers) {
	Unit *u = memnew(Unit);
	u->id = p_node->get_instance_id();
	Node *parent = p_node->get_parent();
	u->parent_id = parent ? parent->get_instance_id() : 0;
	u->uid = String(get_path_to(p_node));
	u->dynamic = p_dynamic;
	u->layers = p_layers;
	Spatial *s = Object::cast_to<Spatial>(p_node);
	if (s && s->is_inside_tree()) {
		u->last_pos = s->get_global_transform().origin;
	}
	int cx, cz;
	world_to_cell_i(u->last_pos, cx, cz);
	u->cell = _key(cx, cz);
	_cell_rec(u->cell).units.push_back(u);
	if (p_dynamic) {
		_dynamic.push_back(u);
	} else if (_cell_rect(p_node, u->x0, u->z0, u->x1, u->z1)) {
		// Make sure the origin cell is part of it, then see whether it spills.
		u->x0 = MIN(u->x0, cx);
		u->x1 = MAX(u->x1, cx);
		u->z0 = MIN(u->z0, cz);
		u->z1 = MAX(u->z1, cz);
		if (u->x0 != u->x1 || u->z0 != u->z1) {
			u->large = true;
			_large.push_back(u);
			_large_dirty = true;
		}
	}
	return u;
}

void GameWorld::_move_unit_to_cell(Unit *p_unit, int64_t p_key) {
	if (p_unit->cell == p_key) {
		return;
	}
	Map<int64_t, CellRec>::Element *E = _cells.find(p_unit->cell);
	if (E) {
		E->get().units.erase(p_unit);
	}
	p_unit->cell = p_key;
	_cell_rec(p_key).units.push_back(p_unit);
}

void GameWorld::_drop_unit(Unit *p_unit) {
	Map<int64_t, CellRec>::Element *E = _cells.find(p_unit->cell);
	if (E) {
		E->get().units.erase(p_unit);
	}
	_dynamic.erase(p_unit);
	_large.erase(p_unit);
	memdelete(p_unit);
}

void GameWorld::_free_all_units() {
	for (Map<int64_t, CellRec>::Element *E = _cells.front(); E; E = E->next()) {
		Vector<Unit *> &units = E->get().units;
		for (int i = 0; i < units.size(); i++) {
			Unit *u = units[i];
			// A parked object is in nobody's tree, so nothing else will ever free
			// it. Attached ones belong to their parent and go with it.
			if (!u->attached) {
				Node *n = Object::cast_to<Node>(ObjectDB::get_instance(u->id));
				if (n && !n->is_inside_tree() && !n->get_parent()) {
					memdelete(n);
				}
			}
			memdelete(u);
		}
	}
	_cells.clear();
	_dynamic.clear();
	_large.clear();
	_work.clear();
	_partitioned = false;
	_streamed_once = false;
}

Node *GameWorld::_unit_node(const Unit *p_unit) const {
	return Object::cast_to<Node>(ObjectDB::get_instance(p_unit->id));
}

bool GameWorld::_layers_allow(const Vector<int> &p_ids) const {
	for (int i = 0; i < p_ids.size(); i++) {
		const Map<int, bool>::Element *E = _layer_enabled.find(p_ids[i]);
		if (E && !E->get()) {
			return false;
		}
	}
	return true;
}

bool GameWorld::_unit_wanted(const Unit *p_unit) const {
	if (!stream_objects) {
		return true;
	}
	if (!_layers_allow(p_unit->layers)) {
		return false;
	}
	const Map<int64_t, CellRec>::Element *E = _cells.find(p_unit->cell);
	if (!E || !E->get().wanted) {
		return false;
	}
	if (p_unit->dynamic && _terrain_active()) {
		// A body that can fall must have something to land on. Object cells reach
		// further out than the terrain's collision ring, so being in range is not
		// enough: without this, everything loose rains through the world one
		// ring past the player.
		Vector3 pos = p_unit->last_pos;
		if (p_unit->attached) {
			Spatial *s = Object::cast_to<Spatial>(_unit_node(p_unit));
			if (s && s->is_inside_tree()) {
				pos = s->get_global_transform().origin;
			}
		}
		if (!_terrain->has_collision_at_world(pos)) {
			return false;
		}
	}
	return true;
}

void GameWorld::_attach(Unit *p_unit) {
	Node *n = _unit_node(p_unit);
	if (!n) {
		// Freed while it was parked: the game destroyed it. Remember that.
		if (!p_unit->spawned) {
			_st_removed.insert(p_unit->uid);
		}
		_drop_unit(p_unit);
		return;
	}
	if (n->get_parent()) {
		// Somebody already put it somewhere (picked up, re-parented). It is no
		// longer ours to place.
		p_unit->attached = true;
		return;
	}
	Node *parent = Object::cast_to<Node>(ObjectDB::get_instance(p_unit->parent_id));
	if (!parent || !parent->is_inside_tree()) {
		parent = this;
	}
	parent->add_child(n);
	p_unit->attached = true;
	// It has been out of the tree for an arbitrary time; do not let physics
	// interpolation blend it in from wherever it was when it left.
	n->reset_physics_interpolation();

	if (p_unit->from_file) {
		Map<String, Variant>::Element *S = _st_custom.find(p_unit->uid);
		if (S && n->has_method("_load_cell_state")) {
			n->call("_load_cell_state", S->get());
		}
	}
}

void GameWorld::_detach(Unit *p_unit, bool p_allow_free) {
	Node *n = _unit_node(p_unit);
	if (!n) {
		if (!p_unit->spawned) {
			_st_removed.insert(p_unit->uid);
		}
		_drop_unit(p_unit);
		return;
	}
	Node *parent = n->get_parent();
	if (parent && parent->get_instance_id() != p_unit->parent_id && parent != this) {
		// Re-parented by the game since we placed it (carried, mounted on
		// something). Taking it out of the tree now would yank it out of the
		// player's hands, so stop tracking it instead.
		if (p_unit->from_file) {
			_st_removed.insert(p_unit->uid);
		}
		_drop_unit(p_unit);
		return;
	}
	// Never unload whatever carries the camera we are streaming from: the focus
	// would vanish with it and nothing would ever load again.
	if (n->is_inside_tree()) {
		Viewport *vp = get_viewport();
		Camera *cam = vp ? vp->get_camera() : nullptr;
		if (cam && (cam == n || n->is_a_parent_of(cam))) {
			return;
		}
	}

	Spatial *s = Object::cast_to<Spatial>(n);
	if (s && s->is_inside_tree()) {
		p_unit->last_pos = s->get_global_transform().origin;
	}

	if (p_allow_free && p_unit->from_file && !p_unit->dynamic) {
		// Came from a cell file and can be rebuilt from it: really free it.
		if (n->has_method("_save_cell_state")) {
			_st_custom[p_unit->uid] = n->call("_save_cell_state");
		}
		if (parent) {
			parent->remove_child(n);
		}
		memdelete(n);
		_drop_unit(p_unit);
		return;
	}

	if (parent) {
		parent->remove_child(n);
	}
	p_unit->attached = false;
}

// ---------------------------------------------------------------------------
// Partition (runtime, once)
// ---------------------------------------------------------------------------

void GameWorld::_partition() {
	if (_partitioned) {
		return;
	}
	_partitioned = true;
	_resolve_layers();

	Collected c;
	_collect(this, Vector<int>(), c);

	for (int i = 0; i < c.layer_nodes.size(); i++) {
		_layer_nodes.push_back(c.layer_nodes[i]->get_instance_id());
	}

	if (stream_objects) {
		for (int i = 0; i < c.statics.size(); i++) {
			_make_unit(c.statics[i], false, c.static_layers[i]);
		}
		for (int i = 0; i < c.dynamics.size(); i++) {
			_make_unit(c.dynamics[i], true, c.dynamic_layers[i]);
		}
		// Baked cells: their objects are not in this scene at all, only on disk.
		if (!baked_dir.empty()) {
			PoolIntArray::Read r = baked_cells.read();
			for (int i = 0; i + 1 < baked_cells.size(); i += 2) {
				_cell_rec(_key(r[i], r[i + 1])).has_file = true;
			}
		}
	}

	// State left behind by a previous visit to this scene.
	SceneManager *sm = SceneManager::get_singleton();
	if (sm) {
		const String key = _state_key();
		if (sm->has_world_state(key)) {
			set_world_state(sm->get_world_state(key));
		}
	}
	if (_has_pending_state) {
		_has_pending_state = false;
		Dictionary st = _pending_state;
		_pending_state = Dictionary();
		set_world_state(st);
	}
	_apply_layers();
}

// ---------------------------------------------------------------------------
// Streaming
// ---------------------------------------------------------------------------

bool GameWorld::_cell_in_range(int cx, int cz, bool p_currently_wanted) const {
	if (!stream_objects || !_has_focus_cell) {
		return true;
	}
	const int d = MAX(ABS(cx - _focus_cx), ABS(cz - _focus_cz));
	if (d <= load_radius) {
		return true;
	}
	if (d > load_radius + unload_margin) {
		return false;
	}
	return p_currently_wanted; // inside the hysteresis band: leave it as it is
}

void GameWorld::_evaluate_cells() {
	for (Map<int64_t, CellRec>::Element *E = _cells.front(); E; E = E->next()) {
		CellRec &rec = E->get();
		const bool want = _cell_in_range(rec.cx, rec.cz, rec.wanted);
		if (want != rec.wanted || !_streamed_once) {
			rec.wanted = want;
			_work.insert(E->key());
		}
	}
}

void GameWorld::_instance_cell_file(CellRec &r_cell) {
	Ref<PackedScene> ps;
	if (r_cell.loader.is_valid()) {
		ps = r_cell.loader->get_resource();
	}
	r_cell.loader.unref();
	r_cell.file_state = 2;
	if (ps.is_null()) {
		return;
	}
	Node *root = ps->instance();
	if (!root) {
		return;
	}
	Vector<Node *> kids;
	for (int i = 0; i < root->get_child_count(); i++) {
		kids.push_back(root->get_child(i));
	}
	const int64_t key = _key(r_cell.cx, r_cell.cz);
	for (int i = 0; i < kids.size(); i++) {
		Node *n = kids[i];
		const String uid = n->has_meta("_gw_uid") ? String(n->get_meta("_gw_uid")) : String(n->get_name());
		if (_st_removed.has(uid)) {
			continue; // destroyed / picked up earlier; stays gone
		}
		root->remove_child(n);
		if (n->has_meta("_gw_name")) {
			n->set_name(String(n->get_meta("_gw_name")));
		}

		Unit *u = memnew(Unit);
		u->id = n->get_instance_id();
		Node *parent = this;
		if (n->has_meta("_gw_parent")) {
			Node *p = get_node_or_null(NodePath(String(n->get_meta("_gw_parent"))));
			if (p) {
				parent = p;
			}
		}
		u->parent_id = parent->get_instance_id();
		u->uid = uid;
		u->cell = key;
		u->from_file = true;
		u->attached = false;
		u->last_pos = get_cell_origin(r_cell.cx, r_cell.cz);
		if (n->has_meta("_gw_layers")) {
			Array ids = n->get_meta("_gw_layers");
			for (int j = 0; j < ids.size(); j++) {
				u->layers.push_back((int)ids[j]);
			}
		}
		r_cell.units.push_back(u);
	}
	memdelete(root); // whatever was skipped goes with it
}

bool GameWorld::_work_cell(CellRec &r_cell, uint64_t p_deadline_usec, bool p_sync) {
	OS *os = OS::get_singleton();

	if (r_cell.wanted) {
		if (r_cell.has_file && r_cell.file_state == 0) {
			const String path = baked_dir.plus_file("cells").plus_file("cell_" + itos(r_cell.cx) + "_" + itos(r_cell.cz) + ".scn");
			Error err = OK;
			r_cell.loader = ResourceLoader::load_interactive(path, "", false, &err);
			if (r_cell.loader.is_null()) {
				ERR_PRINT("GameWorld: cannot open baked cell '" + path + "'");
				r_cell.has_file = false;
			} else {
				r_cell.file_state = 1;
			}
		}
		if (r_cell.has_file && r_cell.file_state == 1) {
			while (true) {
				Error err = r_cell.loader->poll();
				if (err == ERR_FILE_EOF) {
					_instance_cell_file(r_cell);
					break;
				}
				if (err != OK) {
					ERR_PRINT("GameWorld: error " + itos(err) + " loading a baked cell");
					r_cell.loader.unref();
					r_cell.has_file = false;
					break;
				}
				if (!p_sync && os->get_ticks_usec() > p_deadline_usec) {
					return true;
				}
			}
		}

		// _attach / _detach can drop a unit, which edits r_cell.units; walk a copy.
		Vector<Unit *> units = r_cell.units;
		for (int i = 0; i < units.size(); i++) {
			Unit *u = units[i];
			if (u->dynamic || u->large) {
				continue; // driven by _tick_dynamic / _evaluate_large
			}
			const bool want = _layers_allow(u->layers);
			if (want && !u->attached) {
				_attach(u);
			} else if (!want && u->attached) {
				_detach(u, false);
			} else {
				continue;
			}
			if (!p_sync && os->get_ticks_usec() > p_deadline_usec) {
				return true;
			}
		}
		if (!r_cell.announced) {
			r_cell.announced = true;
			emit_signal("cell_loaded", r_cell.cx, r_cell.cz);
		}
		return false;
	}

	Vector<Unit *> units = r_cell.units;
	for (int i = 0; i < units.size(); i++) {
		Unit *u = units[i];
		if (u->dynamic || u->large) {
			continue;
		}
		if (u->attached) {
			_detach(u, true);
		} else if (u->from_file) {
			// Instanced from the file but never attached (its layer was off).
			Node *n = _unit_node(u);
			if (n && !n->get_parent()) {
				memdelete(n);
			}
			_drop_unit(u);
		} else {
			continue;
		}
		if (!p_sync && os->get_ticks_usec() > p_deadline_usec) {
			return true;
		}
	}
	if (r_cell.has_file) {
		r_cell.file_state = 0;
		r_cell.loader.unref();
	}
	if (r_cell.announced) {
		r_cell.announced = false;
		emit_signal("cell_unloaded", r_cell.cx, r_cell.cz);
	}
	return false;
}

void GameWorld::_run_work(bool p_sync) {
	if (_work.empty()) {
		return;
	}
	// Nearest first, so what the player is about to walk into arrives before
	// what is merely on the horizon.
	Vector<int64_t> order;
	Vector<int> dist;
	for (Set<int64_t>::Element *E = _work.front(); E; E = E->next()) {
		int x, z;
		_unkey(E->get(), x, z);
		const int d = MAX(ABS(x - _focus_cx), ABS(z - _focus_cz));
		int at = order.size();
		while (at > 0 && dist[at - 1] > d) {
			at--;
		}
		order.insert(at, E->get());
		dist.insert(at, d);
	}

	const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + (uint64_t)(integrate_budget_ms * 1000.0f);
	for (int i = 0; i < order.size(); i++) {
		Map<int64_t, CellRec>::Element *E = _cells.find(order[i]);
		if (!E) {
			_work.erase(order[i]);
			continue;
		}
		const bool more = _work_cell(E->get(), deadline, p_sync);
		if (!more) {
			_work.erase(order[i]);
		}
		if (!p_sync && OS::get_singleton()->get_ticks_usec() > deadline) {
			break;
		}
	}
}

void GameWorld::_evaluate_large() {
	_large_dirty = false;
	Vector<Unit *> list = _large;
	for (int i = 0; i < list.size(); i++) {
		Unit *u = list[i];
		bool want = true;
		if (stream_objects && _has_focus_cell) {
			// Distance from the focus cell to the nearest cell of the rectangle.
			const int dx = MAX(MAX(u->x0 - _focus_cx, _focus_cx - u->x1), 0);
			const int dz = MAX(MAX(u->z0 - _focus_cz, _focus_cz - u->z1), 0);
			const int d = MAX(dx, dz);
			want = d <= load_radius ? true : (d > load_radius + unload_margin ? false : u->attached);
		}
		want = want && _layers_allow(u->layers);
		if (want && !u->attached) {
			_attach(u);
		} else if (!want && u->attached) {
			_detach(u, false);
		}
	}
}

void GameWorld::_tick_dynamic() {
	Vector<Unit *> list = _dynamic;
	for (int i = 0; i < list.size(); i++) {
		Unit *u = list[i];
		Node *n = _unit_node(u);
		if (!n) {
			if (!u->spawned) {
				_st_removed.insert(u->uid);
			}
			_drop_unit(u);
			continue;
		}
		if (u->attached) {
			Spatial *s = Object::cast_to<Spatial>(n);
			if (!s || !s->is_inside_tree()) {
				continue;
			}
			u->last_pos = s->get_global_transform().origin;
			int cx, cz;
			world_to_cell_i(u->last_pos, cx, cz);
			_move_unit_to_cell(u, _key(cx, cz));
			if (!_unit_wanted(u)) {
				_detach(u, false);
			}
		} else if (_unit_wanted(u)) {
			_attach(u);
		}
	}
}

void GameWorld::_stream(const Vector3 &p_focus, bool p_force_sync) {
	// Terrain first: it builds the collision ring, and the dynamic-object pass
	// below asks it whether there is ground.
	if (_terrain_active()) {
		_terrain->stream_to(p_focus);
		if (p_force_sync) {
			_terrain->ensure_collision_at_world(p_focus);
		}
		if (_foliage) {
			_foliage->stream_focus(p_focus);
		}
	}

	int cx, cz;
	world_to_cell_i(p_focus, cx, cz);
	const bool moved = !_has_focus_cell || cx != _focus_cx || cz != _focus_cz;
	if (moved) {
		_focus_cx = cx;
		_focus_cz = cz;
		_has_focus_cell = true;
		emit_signal("focus_cell_changed", cx, cz);
		// Which cells are in range just changed; do not make loose objects
		// wait out the rest of their tick interval to hear about it.
		_dynamic_accum = DYNAMIC_TICK_SECONDS;
	}
	if (!_partitioned) {
		return;
	}
	if (moved || !_streamed_once) {
		_evaluate_cells();
		_large_dirty = true;
	}
	if (_large_dirty) {
		_evaluate_large();
	}

	// The very first pass is blocking: a level has to start with its
	// surroundings in place, not have them trickle in around the player.
	const bool sync = p_force_sync || !_streamed_once;
	const bool first = !_streamed_once;
	_run_work(sync);
	_streamed_once = true;
	if (first && cs_trace_enabled()) {
		// TUNA_CELL_TRACE=1: one line saying what this world decided to stream.
		int n_dynamic = _dynamic.size(), n_large = _large.size(), n_files = 0;
		for (Map<int64_t, CellRec>::Element *E = _cells.front(); E; E = E->next()) {
			if (E->get().has_file) {
				n_files++;
			}
		}
		print_line("[cell-trace] GameWorld '" + String(get_name()) + "': " + itos(get_object_count()) + " objects (" +
				itos(n_dynamic) + " dynamic, " + itos(n_large) + " multi-cell), " + itos(get_attached_object_count()) + " in tree, " +
				itos(n_files) + " baked cells | has_terrain=" + (has_terrain ? "yes" : "no") + " cell_size=" + rtos(cell_size) +
				" foliage_x" + itos(foliage_cell_multiple) + " load_radius=" + itos(load_radius) +
				" world_dir=" + world_dir + (foliage_dir.empty() ? String() : " foliage_dir=" + foliage_dir));
	}

	_dynamic_accum += sync ? DYNAMIC_TICK_SECONDS : get_process_delta_time();
	if (_dynamic_accum >= DYNAMIC_TICK_SECONDS) {
		_dynamic_accum = 0.0f;
		_tick_dynamic();
	}
}

void GameWorld::prime(const Vector3 &p_world) {
	if (!_is_runtime()) {
		return;
	}
	_partition();
	_stream(p_world, true);
}

void GameWorld::set_stream_objects(bool p_enabled) {
	if (stream_objects == p_enabled) {
		return;
	}
	stream_objects = p_enabled;
	if (_partitioned) {
		// Re-test every cell; with streaming off they all come back in range.
		for (Map<int64_t, CellRec>::Element *E = _cells.front(); E; E = E->next()) {
			E->get().wanted = _cell_in_range(E->get().cx, E->get().cz, E->get().wanted);
			_work.insert(E->key());
		}
		_dynamic_accum = DYNAMIC_TICK_SECONDS;
		_large_dirty = true;
	}
}

void GameWorld::set_load_radius(int p_radius) {
	load_radius = MAX(0, p_radius);
	if (_partitioned && _has_focus_cell) {
		_evaluate_cells();
		_large_dirty = true;
	}
}

int GameWorld::get_cell_state(int cx, int cz) const {
	const int64_t k = _key(cx, cz);
	const Map<int64_t, CellRec>::Element *E = _cells.find(k);
	const bool working = _work.has(k);
	if (!E) {
		return _cell_in_range(cx, cz, false) ? CELL_ACTIVE : CELL_UNLOADED;
	}
	if (E->get().wanted) {
		return working ? CELL_LOADING : CELL_ACTIVE;
	}
	return working ? CELL_UNLOADING : CELL_UNLOADED;
}

int GameWorld::get_object_count() const {
	int n = 0;
	for (const Map<int64_t, CellRec>::Element *E = _cells.front(); E; E = E->next()) {
		n += E->get().units.size();
	}
	return n;
}

int GameWorld::get_attached_object_count() const {
	int n = 0;
	for (const Map<int64_t, CellRec>::Element *E = _cells.front(); E; E = E->next()) {
		const Vector<Unit *> &units = E->get().units;
		for (int i = 0; i < units.size(); i++) {
			if (units[i]->attached) {
				n++;
			}
		}
	}
	return n;
}

void GameWorld::add_object(Node *p_node) {
	ERR_FAIL_NULL(p_node);
	if (!p_node->get_parent()) {
		add_child(p_node);
	}
	if (!_partitioned || !stream_objects) {
		return; // editor, or streaming off: it is simply a child like any other
	}
	Vector<int> layers;
	for (Node *a = p_node; a && a != this; a = a->get_parent()) {
		_append_layer_ids(a, layers);
	}
	Unit *u = _make_unit(p_node, _is_dynamic(p_node), layers);
	u->spawned = true;
	u->uid = "@spawn/" + itos(_spawn_counter++);
}

// ---------------------------------------------------------------------------
// Persistent state
// ---------------------------------------------------------------------------

String GameWorld::_state_key() const {
	if (!source_scene.empty()) {
		return source_scene; // baked: same key as the authoring scene
	}
	const Node *root = get_owner() ? get_owner() : this;
	return root->get_filename();
}

Dictionary GameWorld::get_world_state() {
	Dictionary moved;
	Dictionary custom;
	Array spawned;

	for (Map<String, Variant>::Element *E = _st_custom.front(); E; E = E->next()) {
		custom[E->key()] = E->get();
	}

	for (Map<int64_t, CellRec>::Element *E = _cells.front(); E; E = E->next()) {
		Vector<Unit *> units = E->get().units;
		for (int i = 0; i < units.size(); i++) {
			Unit *u = units[i];
			Node *n = _unit_node(u);
			if (!n) {
				if (!u->spawned) {
					_st_removed.insert(u->uid);
				}
				_drop_unit(u);
				continue;
			}
			Variant cst;
			const bool has_custom = n->has_method("_save_cell_state");
			if (has_custom) {
				cst = n->call("_save_cell_state");
			}
			Spatial *s = Object::cast_to<Spatial>(n);
			if (u->spawned) {
				// Only an instanced scene can be rebuilt from a save.
				if (n->get_filename().empty()) {
					continue;
				}
				Dictionary d;
				d["scene"] = n->get_filename();
				// Local transform: valid in and out of the tree, and the folders
				// above it never move.
				d["xf"] = s ? s->get_transform() : Transform();
				Node *parent = Object::cast_to<Node>(ObjectDB::get_instance(u->parent_id));
				d["parent"] = (parent && (parent == this || is_a_parent_of(parent))) ? String(get_path_to(parent)) : String(".");
				if (has_custom) {
					d["custom"] = cst;
				}
				spawned.push_back(d);
				continue;
			}
			if (has_custom) {
				custom[u->uid] = cst;
			}
			if (u->dynamic && s) {
				moved[u->uid] = s->get_transform();
			}
		}
	}

	Array removed;
	for (Set<String>::Element *E = _st_removed.front(); E; E = E->next()) {
		removed.push_back(E->get());
	}
	Dictionary layers;
	for (Map<int, bool>::Element *E = _layer_override.front(); E; E = E->next()) {
		layers[E->key()] = E->get();
	}

	Dictionary out;
	out["removed"] = removed;
	out["moved"] = moved;
	out["custom"] = custom;
	out["spawned"] = spawned;
	out["layers"] = layers;
	return out;
}

void GameWorld::set_world_state(const Dictionary &p_state) {
	if (!_partitioned) {
		// Objects are not sorted yet; apply it as soon as they are.
		_pending_state = p_state;
		_has_pending_state = true;
		return;
	}

	Map<String, Unit *> by_uid;
	for (Map<int64_t, CellRec>::Element *E = _cells.front(); E; E = E->next()) {
		const Vector<Unit *> &units = E->get().units;
		for (int i = 0; i < units.size(); i++) {
			by_uid[units[i]->uid] = units[i];
		}
	}

	Array removed = p_state.has("removed") ? (Array)p_state["removed"] : Array();
	for (int i = 0; i < removed.size(); i++) {
		const String uid = removed[i];
		_st_removed.insert(uid);
		Map<String, Unit *>::Element *U = by_uid.find(uid);
		if (!U) {
			continue; // in a baked cell that is not loaded; skipped when it is
		}
		Node *n = _unit_node(U->get());
		if (n) {
			if (n->get_parent()) {
				n->get_parent()->remove_child(n);
			}
			memdelete(n);
		}
		_drop_unit(U->get());
		by_uid.erase(U);
	}

	Dictionary custom = p_state.has("custom") ? (Dictionary)p_state["custom"] : Dictionary();
	List<Variant> ckeys;
	custom.get_key_list(&ckeys);
	for (List<Variant>::Element *E = ckeys.front(); E; E = E->next()) {
		const String uid = E->get();
		_st_custom[uid] = custom[E->get()];
		Map<String, Unit *>::Element *U = by_uid.find(uid);
		Node *n = U ? _unit_node(U->get()) : nullptr;
		if (n && n->has_method("_load_cell_state")) {
			n->call("_load_cell_state", custom[E->get()]);
		}
	}

	Dictionary moved = p_state.has("moved") ? (Dictionary)p_state["moved"] : Dictionary();
	List<Variant> mkeys;
	moved.get_key_list(&mkeys);
	for (List<Variant>::Element *E = mkeys.front(); E; E = E->next()) {
		Map<String, Unit *>::Element *U = by_uid.find(String(E->get()));
		if (!U) {
			continue;
		}
		Unit *u = U->get();
		Spatial *s = Object::cast_to<Spatial>(_unit_node(u));
		if (!s) {
			continue;
		}
		const Transform xf = moved[E->get()];
		s->set_transform(xf);
		// Work out where that puts it in the world, so it is filed under the
		// right cell even while parked.
		Spatial *parent = Object::cast_to<Spatial>(ObjectDB::get_instance(u->parent_id));
		Transform pxf = (parent && parent->is_inside_tree()) ? parent->get_global_transform() : get_global_transform();
		u->last_pos = pxf.xform(xf.origin);
		int cx, cz;
		world_to_cell_i(u->last_pos, cx, cz);
		_move_unit_to_cell(u, _key(cx, cz));
	}

	Array spawned = p_state.has("spawned") ? (Array)p_state["spawned"] : Array();
	for (int i = 0; i < spawned.size(); i++) {
		Dictionary d = spawned[i];
		const String scene = d.has("scene") ? (String)d["scene"] : String();
		Ref<PackedScene> ps = ResourceLoader::load(scene);
		if (ps.is_null()) {
			continue;
		}
		Node *n = ps->instance();
		if (!n) {
			continue;
		}
		Spatial *s = Object::cast_to<Spatial>(n);
		if (s && d.has("xf")) {
			s->set_transform(d["xf"]);
		}
		Node *parent = d.has("parent") ? get_node_or_null(NodePath((String)d["parent"])) : nullptr;
		(parent ? parent : (Node *)this)->add_child(n);
		add_object(n);
		if (d.has("custom") && n->has_method("_load_cell_state")) {
			n->call("_load_cell_state", d["custom"]);
		}
	}

	if (p_state.has("layers")) {
		Dictionary layers = p_state["layers"];
		List<Variant> lkeys;
		layers.get_key_list(&lkeys);
		_layer_override.clear();
		for (List<Variant>::Element *E = lkeys.front(); E; E = E->next()) {
			_layer_override[(int)E->get()] = (bool)layers[E->get()];
		}
		_resolve_layers();
		_apply_layers();
	}

	for (Map<int64_t, CellRec>::Element *E = _cells.front(); E; E = E->next()) {
		_work.insert(E->key());
	}
	_dynamic_accum = DYNAMIC_TICK_SECONDS;
	_large_dirty = true;
}

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

static void _gw_resolve_layers(const Array &p_layers, bool p_parent_on, const Map<int, bool> &p_override, Map<int, bool> &r_out) {
	for (int i = 0; i < p_layers.size(); i++) {
		Dictionary d = p_layers[i];
		const int id = d.has("id") ? (int)d["id"] : -1;
		bool own = d.has("runtime") ? (bool)d["runtime"] : true;
		const Map<int, bool>::Element *O = p_override.find(id);
		if (O) {
			own = O->get();
		}
		const bool on = p_parent_on && own;
		r_out[id] = on;
		if (d.has("children")) {
			_gw_resolve_layers(d["children"], on, p_override, r_out);
		}
	}
}

static int _gw_find_layer(const Array &p_layers, const String &p_name) {
	for (int i = 0; i < p_layers.size(); i++) {
		Dictionary d = p_layers[i];
		if (d.has("name") && (String)d["name"] == p_name) {
			return d.has("id") ? (int)d["id"] : -1;
		}
		if (d.has("children")) {
			const int found = _gw_find_layer(d["children"], p_name);
			if (found >= 0) {
				return found;
			}
		}
	}
	return -1;
}

void GameWorld::_resolve_layers() {
	_layer_enabled.clear();
	if (layer_data.has("layers")) {
		_gw_resolve_layers(layer_data["layers"], true, _layer_override, _layer_enabled);
	}
}

void GameWorld::_apply_layers() {
	if (!_is_runtime()) {
		return; // in the editor, layer visibility belongs to the Scene Layer dock
	}
	for (int i = 0; i < _layer_nodes.size(); i++) {
		Spatial *s = Object::cast_to<Spatial>(ObjectDB::get_instance(_layer_nodes[i]));
		if (!s) {
			continue;
		}
		Vector<int> ids;
		for (Node *a = s; a && a != this; a = a->get_parent()) {
			_append_layer_ids(a, ids);
		}
		const bool allow = _layers_allow(ids);
		if (!allow && s->is_visible()) {
			s->set_visible(false);
			_layer_hidden.insert(_layer_nodes[i]);
		} else if (allow && _layer_hidden.has(_layer_nodes[i])) {
			// Only un-hide what a layer hid. Something the author left hidden on
			// purpose is not ours to reveal.
			s->set_visible(true);
			_layer_hidden.erase(_layer_nodes[i]);
		}
	}
	if (_partitioned) {
		for (Map<int64_t, CellRec>::Element *E = _cells.front(); E; E = E->next()) {
			_work.insert(E->key());
		}
		_dynamic_accum = DYNAMIC_TICK_SECONDS;
		_large_dirty = true;
	}
}

void GameWorld::set_layer_data(const Dictionary &p_data) {
	layer_data = p_data;
	if (_partitioned) {
		_resolve_layers();
		_apply_layers();
	}
}

int GameWorld::get_layer_id(const String &p_name) const {
	if (!layer_data.has("layers")) {
		return -1;
	}
	return _gw_find_layer(layer_data["layers"], p_name);
}

void GameWorld::set_layer_enabled(int p_id, bool p_enabled) {
	_layer_override[p_id] = p_enabled;
	_resolve_layers();
	_apply_layers();
}

bool GameWorld::is_layer_enabled(int p_id) const {
	const Map<int, bool>::Element *E = _layer_enabled.find(p_id);
	if (E) {
		return E->get();
	}
	// Not resolved yet (editor, or before _ready): answer from the data.
	Map<int, bool> resolved;
	if (layer_data.has("layers")) {
		_gw_resolve_layers(layer_data["layers"], true, _layer_override, resolved);
	}
	const Map<int, bool>::Element *R = resolved.find(p_id);
	return R ? R->get() : true;
}

// ---------------------------------------------------------------------------
// Baking
// ---------------------------------------------------------------------------

String GameWorld::get_bake_dir() const {
	const Node *root = get_owner() ? get_owner() : this;
	const String src = source_scene.empty() ? root->get_filename() : source_scene;
	return src.empty() ? String() : src.get_basename() + ".world";
}

bool GameWorld::has_bake() const {
	const String dir = get_bake_dir();
	return !dir.empty() && FileAccess::exists(dir.plus_file("world.scn"));
}

String GameWorld::resolve_baked_scene(const String &p_scene_path) {
	const String dir = p_scene_path.get_basename() + ".world";
	const String world = dir.plus_file("world.scn");
	if (!FileAccess::exists(world)) {
		return p_scene_path;
	}
	// A bake older than its scene describes a world that no longer exists.
	// Running it would silently show stale content, so fall back to the
	// authoring scene (which streams from memory) and say so. Both checks are
	// skipped where the information is not available - an exported build has no
	// modification times and no bake.cfg, and there the bake is all there is.
	const String cfg = dir.plus_file("bake.cfg");
	if (FileAccess::exists(cfg)) {
		const uint64_t now = FileAccess::get_modified_time(p_scene_path);
		FileAccess *f = FileAccess::open(cfg, FileAccess::READ);
		if (f) {
			const uint64_t baked = (uint64_t)f->get_line().to_int64();
			f->close();
			memdelete(f);
			if (now != 0 && baked != 0 && now != baked) {
				WARN_PRINT("GameWorld: the bake for '" + p_scene_path + "' is older than the scene; using the scene itself. Re-bake to stream from disk.");
				return p_scene_path;
			}
		}
	}
	return world;
}

void GameWorld::clear_bake() {
	const String dir = get_bake_dir();
	if (dir.empty()) {
		return;
	}
	const String cells = dir.plus_file("cells");
	DirAccess *da = DirAccess::open(cells);
	if (da) {
		da->list_dir_begin();
		String f = da->get_next();
		while (f != "") {
			if (!da->current_is_dir() && f.begins_with("cell_")) {
				da->remove(cells.plus_file(f));
			}
			f = da->get_next();
		}
		da->list_dir_end();
		memdelete(da);
	}
	da = DirAccess::open(dir);
	if (da) {
		da->remove(dir.plus_file("world.scn"));
		da->remove(dir.plus_file("bake.cfg"));
		memdelete(da);
	}
}

Error GameWorld::bake_world() {
	ERR_FAIL_COND_V(!is_inside_tree(), ERR_UNCONFIGURED);
	Node *root = get_owner() ? get_owner() : this;
	const String src = root->get_filename();
	ERR_FAIL_COND_V_MSG(src.empty(), ERR_FILE_BAD_PATH, "GameWorld: save the scene before baking it.");
	ERR_FAIL_COND_V_MSG(!baked_dir.empty(), ERR_ALREADY_EXISTS, "GameWorld: this is already a baked scene; bake the authoring scene instead.");

	const String dir = src.get_basename() + ".world";
	const String cells_dir = dir.plus_file("cells");
	clear_bake();
	{
		DirAccess *da = DirAccess::create_for_path(cells_dir);
		ERR_FAIL_COND_V(!da, ERR_CANT_CREATE);
		Error err = da->make_dir_recursive(cells_dir);
		memdelete(da);
		ERR_FAIL_COND_V(err != OK, err);
	}

	Collected c;
	_collect(this, Vector<int>(), c);

	// --- One file per cell, holding copies of that cell's static objects ---
	// Objects that span several cells are left in world.scn: they are few, and
	// which single cell file would own one has no good answer.
	Map<int64_t, Vector<int> > by_cell;
	Vector<Node *> cell_statics;
	for (int i = 0; i < c.statics.size(); i++) {
		Spatial *s = Object::cast_to<Spatial>(c.statics[i]);
		int cx, cz;
		world_to_cell_i(s->get_global_transform().origin, cx, cz);
		int x0, z0, x1, z1;
		if (_cell_rect(s, x0, z0, x1, z1) && (MIN(x0, cx) != MAX(x1, cx) || MIN(z0, cz) != MAX(z1, cz))) {
			continue;
		}
		by_cell[_key(cx, cz)].push_back(i);
		cell_statics.push_back(c.statics[i]);
	}

	PoolIntArray cell_list;
	for (Map<int64_t, Vector<int> >::Element *E = by_cell.front(); E; E = E->next()) {
		int cx, cz;
		_unkey(E->key(), cx, cz);

		Spatial *croot = memnew(Spatial);
		croot->set_name("Cell_" + itos(cx) + "_" + itos(cz));
		const Vector<int> &idx = E->get();
		for (int i = 0; i < idx.size(); i++) {
			Node *unit = c.statics[idx[i]];
			Node *dup = unit->duplicate(Node::DUPLICATE_SIGNALS | Node::DUPLICATE_GROUPS | Node::DUPLICATE_SCRIPTS | Node::DUPLICATE_USE_INSTANCING);
			if (!dup) {
				continue;
			}
			croot->add_child(dup, true);
			dup->set_owner(croot);
			// A packed scene only keeps nodes its root owns. Everything the
			// authoring scene owned inside this object has to be handed over;
			// what belongs to a nested instanced scene is left to that instance.
			List<Node *> owned;
			unit->get_owned_by(unit->get_owner(), &owned);
			for (List<Node *>::Element *O = owned.front(); O; O = O->next()) {
				if (O->get() == unit) {
					continue;
				}
				Node *d = dup->get_node_or_null(unit->get_path_to(O->get()));
				if (d) {
					d->set_owner(croot);
				}
			}
			dup->set_meta("_gw_uid", String(get_path_to(unit)));
			// Two same-named objects from different folders get renamed apart
			// inside the cell file; this is how each gets its real name back.
			dup->set_meta("_gw_name", String(unit->get_name()));
			dup->set_meta("_gw_parent", String(get_path_to(unit->get_parent())));
			if (c.static_layers[idx[i]].size()) {
				Array ids;
				for (int j = 0; j < c.static_layers[idx[i]].size(); j++) {
					ids.push_back(c.static_layers[idx[i]][j]);
				}
				dup->set_meta("_gw_layers", ids);
			}
		}

		Ref<PackedScene> ps;
		ps.instance();
		Error err = ps->pack(croot);
		if (err == OK) {
			err = ResourceSaver::save(cells_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".scn"), ps);
		}
		memdelete(croot);
		ERR_FAIL_COND_V_MSG(err != OK, err, "GameWorld: could not write cell " + itos(cx) + "," + itos(cz));
		cell_list.push_back(cx);
		cell_list.push_back(cz);
	}

	// --- world.scn: this scene with those objects taken out ---
	// The objects are lifted out of the live tree for the duration of the pack
	// and put straight back. remove_child() drops ownership all the way down
	// (an owner must be an ancestor), so that is recorded first and restored
	// after, or the authoring scene would quietly lose them on its next save.
	struct Lifted {
		Node *node;
		Node *parent;
		int index;
		Vector<Node *> owned;
		Vector<Node *> owners;
	};
	Vector<Lifted> lifted;
	for (int i = 0; i < cell_statics.size(); i++) {
		Lifted l;
		l.node = cell_statics[i];
		l.parent = l.node->get_parent();
		l.index = l.node->get_index();
		Vector<Node *> stack;
		stack.push_back(l.node);
		while (stack.size()) {
			Node *n = stack[stack.size() - 1];
			stack.resize(stack.size() - 1);
			if (n->get_owner()) {
				l.owned.push_back(n);
				l.owners.push_back(n->get_owner());
			}
			for (int j = 0; j < n->get_child_count(); j++) {
				stack.push_back(n->get_child(j));
			}
		}
		lifted.push_back(l);
	}
	for (int i = 0; i < lifted.size(); i++) {
		lifted[i].parent->remove_child(lifted[i].node);
	}

	baked_dir = dir;
	baked_cells = cell_list;
	source_scene = src;

	Ref<PackedScene> world;
	world.instance();
	Error err = world->pack(root);
	if (err == OK) {
		err = ResourceSaver::save(dir.plus_file("world.scn"), world);
	}

	baked_dir = String();
	baked_cells = PoolIntArray();
	source_scene = String();

	// In original order, so each recorded index is valid again as it is used.
	for (int i = 0; i < lifted.size(); i++) {
		const Lifted &l = lifted[i];
		l.parent->add_child(l.node);
		l.parent->move_child(l.node, l.index);
		for (int j = 0; j < l.owned.size(); j++) {
			l.owned[j]->set_owner(l.owners[j]);
		}
	}
	ERR_FAIL_COND_V_MSG(err != OK, err, "GameWorld: could not write world.scn");

	FileAccess *f = FileAccess::open(dir.plus_file("bake.cfg"), FileAccess::WRITE);
	if (f) {
		f->store_line(String::num_uint64(FileAccess::get_modified_time(src)));
		f->close();
		memdelete(f);
	}
	print_line("GameWorld: baked " + itos(cell_statics.size()) + " objects into " + itos(by_cell.size()) + " cells at " + dir);
	return OK;
}

// ---------------------------------------------------------------------------
// Legacy migration
// ---------------------------------------------------------------------------

static void _gw_collect_wrappers(Node *p_node, Vector<Node *> &r_out) {
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *c = p_node->get_child(i);
		if (c->is_class("GameWorld")) {
			continue;
		}
		_gw_collect_wrappers(c, r_out); // children first, so nesting unwinds inside-out
		const String cls = c->get_class();
		const bool dyn_box = cls == "Spatial" && c->get_name() == "DynamicObjects" && c->get_script().is_null() && c->get_filename().empty();
		if (cls == "Cell" || cls == "CellWorld" || dyn_box) {
			r_out.push_back(c);
		}
	}
}

void GameWorld::migrate_legacy(Node *p_owner) {
	ERR_FAIL_COND(!is_inside_tree());
	// Adopt first: the CellWorld about to be flattened still carries the load
	// radius the scene was tuned with.
	_find_subsystems();
	_adopt_legacy_settings();

	Vector<Node *> wrappers;
	_gw_collect_wrappers(this, wrappers);

	for (int i = 0; i < wrappers.size(); i++) {
		Node *w = wrappers[i];
		Node *dest = w->get_parent();
		if (!dest) {
			continue;
		}
		Vector<Node *> kids;
		for (int j = 0; j < w->get_child_count(); j++) {
			kids.push_back(w->get_child(j));
		}
		for (int j = 0; j < kids.size(); j++) {
			Node *k = kids[j];
			Spatial *s = Object::cast_to<Spatial>(k);
			Transform gxf;
			if (s) {
				gxf = s->get_global_transform();
			}
			// Same ownership caveat as in bake_world(): reparenting drops it.
			Vector<Node *> owned;
			Vector<Node *> stack;
			stack.push_back(k);
			while (stack.size()) {
				Node *n = stack[stack.size() - 1];
				stack.resize(stack.size() - 1);
				if (n->get_owner() && n->get_owner() == p_owner) {
					owned.push_back(n);
				}
				for (int c = 0; c < n->get_child_count(); c++) {
					stack.push_back(n->get_child(c));
				}
			}
			w->remove_child(k);
			dest->add_child(k, true);
			if (s) {
				s->set_global_transform(gxf);
			}
			for (int c = 0; c < owned.size(); c++) {
				owned[c]->set_owner(p_owner);
			}
		}
		dest->remove_child(w);
		w->queue_delete();
	}

	_find_subsystems();
	_adopt_legacy_settings();
	format_version = GAME_WORLD_FORMAT;
	_ensure_subsystems();
	print_line("GameWorld: migrated legacy scene (" + itos(wrappers.size()) + " wrapper nodes flattened).");
}

// ---------------------------------------------------------------------------
// Location registry
// ---------------------------------------------------------------------------

Array GameWorld::get_locations() const {
	Array out;
	if (!is_inside_tree()) {
		return out;
	}
	List<Node *> nodes;
	get_tree()->get_nodes_in_group("world_locations", &nodes);
	for (List<Node *>::Element *E = nodes.front(); E; E = E->next()) {
		WorldLocationArea *loc = Object::cast_to<WorldLocationArea>(E->get());
		if (loc && is_a_parent_of(loc)) {
			out.push_back(loc);
		}
	}
	return out;
}

Node *GameWorld::get_location_by_id(const String &p_id) const {
	if (!is_inside_tree()) {
		return nullptr;
	}
	List<Node *> nodes;
	get_tree()->get_nodes_in_group("world_locations", &nodes);
	for (List<Node *>::Element *E = nodes.front(); E; E = E->next()) {
		WorldLocationArea *loc = Object::cast_to<WorldLocationArea>(E->get());
		if (loc && loc->get_location_id() == p_id && is_a_parent_of(loc)) {
			return loc;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------

String GameWorld::get_configuration_warning() const {
	if (has_terrain && is_inside_tree() && (!_terrain || !_foliage)) {
		return "has_terrain is on but this GameWorld has no CellTerrainManager / FoliageManager child yet. They are created automatically when the GameWorld is part of the edited scene.";
	}
	return String();
}

void GameWorld::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			add_to_group("game_worlds");
			if (Engine::get_singleton()->is_editor_hint()) {
				// Deferred in the editor: when this node is being made the scene
				// root, the editor has not recorded it as the edited scene yet.
				_find_subsystems();
				call_deferred("_ensure_subsystems");
			} else {
				// Before any child's _ready, so the managers start already
				// knowing their grid and that they are being driven from here.
				_ensure_subsystems();
			}
		} break;

		case NOTIFICATION_READY: {
			if (Engine::get_singleton()->is_editor_hint()) {
				set_process(false);
				break;
			}
			_partition();
			set_process(true);
		} break;

		case NOTIFICATION_PROCESS: {
			if (Engine::get_singleton()->is_editor_hint()) {
				break;
			}
			if (!has_focus()) {
				break; // nothing is looking at the world yet; leave it as it is
			}
			_stream(get_focus_position(), false);
		} break;

		case NOTIFICATION_EXIT_TREE: {
			if (!Engine::get_singleton()->is_editor_hint() && _partitioned) {
				// Leaving for another scene: hand what changed here to the
				// SceneManager so it is still true when the player comes back.
				SceneManager *sm = SceneManager::get_singleton();
				const String key = _state_key();
				if (sm && !key.empty()) {
					sm->store_world_state(key, get_world_state());
				}
			}
			_terrain = nullptr;
			_foliage = nullptr;
		} break;
	}
}

void GameWorld::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_cell_size", "size"), &GameWorld::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &GameWorld::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_foliage_cell_multiple", "multiple"), &GameWorld::set_foliage_cell_multiple);
	ClassDB::bind_method(D_METHOD("get_foliage_cell_multiple"), &GameWorld::get_foliage_cell_multiple);
	ClassDB::bind_method(D_METHOD("set_world_dir", "dir"), &GameWorld::set_world_dir);
	ClassDB::bind_method(D_METHOD("get_world_dir"), &GameWorld::get_world_dir);
	ClassDB::bind_method(D_METHOD("set_foliage_dir", "dir"), &GameWorld::set_foliage_dir);
	ClassDB::bind_method(D_METHOD("get_foliage_dir"), &GameWorld::get_foliage_dir);
	ClassDB::bind_method(D_METHOD("world_to_cell", "world_pos"), &GameWorld::world_to_cell);
	ClassDB::bind_method(D_METHOD("get_cell_origin", "cx", "cz"), &GameWorld::get_cell_origin);

	ClassDB::bind_method(D_METHOD("set_has_terrain", "enabled"), &GameWorld::set_has_terrain);
	ClassDB::bind_method(D_METHOD("get_has_terrain"), &GameWorld::get_has_terrain);
	ClassDB::bind_method(D_METHOD("get_terrain"), &GameWorld::get_terrain_node);
	ClassDB::bind_method(D_METHOD("get_foliage"), &GameWorld::get_foliage_node);
	ClassDB::bind_method(D_METHOD("_ensure_subsystems"), &GameWorld::_ensure_subsystems);

	ClassDB::bind_method(D_METHOD("set_focus_path", "path"), &GameWorld::set_focus_path);
	ClassDB::bind_method(D_METHOD("get_focus_path"), &GameWorld::get_focus_path);
	ClassDB::bind_method(D_METHOD("set_focus_position", "world_pos"), &GameWorld::set_focus_position);
	ClassDB::bind_method(D_METHOD("clear_focus_position"), &GameWorld::clear_focus_position);
	ClassDB::bind_method(D_METHOD("has_focus"), &GameWorld::has_focus);
	ClassDB::bind_method(D_METHOD("get_focus_position"), &GameWorld::get_focus_position);
	ClassDB::bind_method(D_METHOD("get_focus_cell"), &GameWorld::get_focus_cell);
	ClassDB::bind_method(D_METHOD("prime", "world_pos"), &GameWorld::prime);
	ClassDB::bind_method(D_METHOD("is_ground_ready", "world_pos"), &GameWorld::is_ground_ready);
	ClassDB::bind_method(D_METHOD("ensure_ground", "world_pos"), &GameWorld::ensure_ground);

	ClassDB::bind_method(D_METHOD("set_stream_objects", "enabled"), &GameWorld::set_stream_objects);
	ClassDB::bind_method(D_METHOD("get_stream_objects"), &GameWorld::get_stream_objects);
	ClassDB::bind_method(D_METHOD("set_load_radius", "radius"), &GameWorld::set_load_radius);
	ClassDB::bind_method(D_METHOD("get_load_radius"), &GameWorld::get_load_radius);
	ClassDB::bind_method(D_METHOD("set_unload_margin", "margin"), &GameWorld::set_unload_margin);
	ClassDB::bind_method(D_METHOD("get_unload_margin"), &GameWorld::get_unload_margin);
	ClassDB::bind_method(D_METHOD("set_integrate_budget_ms", "ms"), &GameWorld::set_integrate_budget_ms);
	ClassDB::bind_method(D_METHOD("get_integrate_budget_ms"), &GameWorld::get_integrate_budget_ms);
	ClassDB::bind_method(D_METHOD("get_cell_state", "cx", "cz"), &GameWorld::get_cell_state);
	ClassDB::bind_method(D_METHOD("get_pending_cell_count"), &GameWorld::get_pending_cell_count);
	ClassDB::bind_method(D_METHOD("get_object_count"), &GameWorld::get_object_count);
	ClassDB::bind_method(D_METHOD("get_attached_object_count"), &GameWorld::get_attached_object_count);
	ClassDB::bind_method(D_METHOD("add_object", "node"), &GameWorld::add_object);

	ClassDB::bind_method(D_METHOD("get_world_state"), &GameWorld::get_world_state);
	ClassDB::bind_method(D_METHOD("set_world_state", "state"), &GameWorld::set_world_state);

	ClassDB::bind_method(D_METHOD("bake_world"), &GameWorld::bake_world);
	ClassDB::bind_method(D_METHOD("clear_bake"), &GameWorld::clear_bake);
	ClassDB::bind_method(D_METHOD("has_bake"), &GameWorld::has_bake);
	ClassDB::bind_method(D_METHOD("get_bake_dir"), &GameWorld::get_bake_dir);
	ClassDB::bind_method(D_METHOD("migrate_legacy", "owner"), &GameWorld::migrate_legacy, DEFVAL(Variant()));

	ClassDB::bind_method(D_METHOD("set_baked_dir", "dir"), &GameWorld::set_baked_dir);
	ClassDB::bind_method(D_METHOD("get_baked_dir"), &GameWorld::get_baked_dir);
	ClassDB::bind_method(D_METHOD("set_baked_cells", "cells"), &GameWorld::set_baked_cells);
	ClassDB::bind_method(D_METHOD("get_baked_cells"), &GameWorld::get_baked_cells);
	ClassDB::bind_method(D_METHOD("set_source_scene", "path"), &GameWorld::set_source_scene);
	ClassDB::bind_method(D_METHOD("get_source_scene"), &GameWorld::get_source_scene);
	ClassDB::bind_method(D_METHOD("set_format_version", "version"), &GameWorld::set_format_version);
	ClassDB::bind_method(D_METHOD("get_format_version"), &GameWorld::get_format_version);

	ClassDB::bind_method(D_METHOD("set_layer_data", "data"), &GameWorld::set_layer_data);
	ClassDB::bind_method(D_METHOD("get_layer_data"), &GameWorld::get_layer_data);
	ClassDB::bind_method(D_METHOD("get_layer_id", "name"), &GameWorld::get_layer_id);
	ClassDB::bind_method(D_METHOD("set_layer_enabled", "id", "enabled"), &GameWorld::set_layer_enabled);
	ClassDB::bind_method(D_METHOD("is_layer_enabled", "id"), &GameWorld::is_layer_enabled);

	ClassDB::bind_method(D_METHOD("get_location_by_id", "id"), &GameWorld::get_location_by_id);
	ClassDB::bind_method(D_METHOD("get_locations"), &GameWorld::get_locations);

	ADD_GROUP("Grid", "");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "cell_size", PROPERTY_HINT_RANGE, "1,1024,1,or_greater"), "set_cell_size", "get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "world_dir", PROPERTY_HINT_DIR), "set_world_dir", "get_world_dir");
	ADD_GROUP("Terrain", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "has_terrain"), "set_has_terrain", "get_has_terrain");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "foliage_cell_multiple", PROPERTY_HINT_RANGE, "1,16,1"), "set_foliage_cell_multiple", "get_foliage_cell_multiple");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "foliage_dir", PROPERTY_HINT_DIR), "set_foliage_dir", "get_foliage_dir");
	ADD_GROUP("Object Streaming", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "stream_objects"), "set_stream_objects", "get_stream_objects");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "load_radius", PROPERTY_HINT_RANGE, "0,32,1"), "set_load_radius", "get_load_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "unload_margin", PROPERTY_HINT_RANGE, "0,8,1"), "set_unload_margin", "get_unload_margin");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "integrate_budget_ms", PROPERTY_HINT_RANGE, "0.1,16,0.1"), "set_integrate_budget_ms", "get_integrate_budget_ms");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "focus_path"), "set_focus_path", "get_focus_path");

	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "layer_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR), "set_layer_data", "get_layer_data");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "format_version", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR), "set_format_version", "get_format_version");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "baked_dir", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR), "set_baked_dir", "get_baked_dir");
	ADD_PROPERTY(PropertyInfo(Variant::POOL_INT_ARRAY, "baked_cells", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR), "set_baked_cells", "get_baked_cells");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "source_scene", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR), "set_source_scene", "get_source_scene");

	ADD_SIGNAL(MethodInfo("focus_cell_changed", PropertyInfo(Variant::INT, "cx"), PropertyInfo(Variant::INT, "cz")));
	ADD_SIGNAL(MethodInfo("cell_loaded", PropertyInfo(Variant::INT, "cx"), PropertyInfo(Variant::INT, "cz")));
	ADD_SIGNAL(MethodInfo("cell_unloaded", PropertyInfo(Variant::INT, "cx"), PropertyInfo(Variant::INT, "cz")));

	BIND_ENUM_CONSTANT(CELL_UNLOADED);
	BIND_ENUM_CONSTANT(CELL_LOADING);
	BIND_ENUM_CONSTANT(CELL_ACTIVE);
	BIND_ENUM_CONSTANT(CELL_UNLOADING);
}
