#include "scene_manager.h"

#include "core/engine.h"
#include "core/list.h"
#include "scene/3d/spatial.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/resources/packed_scene.h"

#include "game_world.h"
#include "scene_spawn_point.h"
#include "scene_transition.h"

SceneManager *SceneManager::singleton = nullptr;

SceneManager::SceneManager() {
	singleton = this;
}

SceneManager::~SceneManager() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

void SceneManager::idle_callback() {
	if (singleton) {
		singleton->_idle();
	}
}

SceneTransition *SceneManager::_get_transition() const {
	if (_transition_id == 0) {
		return nullptr;
	}
	return Object::cast_to<SceneTransition>(ObjectDB::get_instance(_transition_id));
}

void SceneManager::_ensure_runtime() {
	if (_bootstrapped) {
		return;
	}
	SceneTree *st = SceneTree::get_singleton();
	if (!st || !st->get_root()) {
		return;
	}
	_bootstrapped = true;

	SceneTransition *t = memnew(SceneTransition);
	t->set_name("SceneTransition");
	_transition_id = t->get_instance_id();
	// Deferred: _ensure_runtime() may be reached from inside a node's _ready()
	// (root is "blocked" mid-add_child then) as well as from the safe idle
	// callback. The deferred add lands before the next frame either way.
	st->get_root()->call_deferred("add_child", t);
}

void SceneManager::_idle() {
	_ensure_runtime();
	if (travelling) {
		_pump();
	}
}

void SceneManager::_pump() {
	SceneTransition *t = _get_transition();

	if (!_load_done) {
		if (loader.is_null()) {
			_fail(target_id, "interactive loader was lost");
			return;
		}
		for (int i = 0; i < max_polls_per_frame; i++) {
			Error err = loader->poll();
			if (err == ERR_FILE_EOF) {
				_load_done = true;
				break;
			}
			if (err != OK) {
				_fail(target_id, "load error " + itos(err));
				return;
			}
		}
		if (!_load_done) {
			int count = loader->get_stage_count();
			progress = (count > 0) ? (float)loader->get_stage() / (float)count : 0.0f;
			emit_signal("load_progress", progress);
			if (t) {
				t->set_progress(progress);
			}
			return;
		}
		progress = 1.0f;
		emit_signal("load_progress", progress);
		if (t) {
			t->set_progress(1.0f);
		}
	}

	// Scene is ready — wait until the loading screen fully covers the viewport
	// before swapping, so the instance() hitch is never visible.
	if (t && !t->is_covered()) {
		return;
	}
	_finish_load();
}

void SceneManager::_finish_load() {
	SceneTree *st = SceneTree::get_singleton();

	Ref<PackedScene> ps;
	if (loader.is_valid()) {
		Ref<PackedScene> loaded(loader->get_resource());
		ps = loaded;
	}
	loader.unref();
	_load_done = false;

	if (!st || ps.is_null()) {
		_fail(target_id, "no packed scene produced");
		return;
	}
	Node *new_root = ps->instance();
	if (!new_root) {
		_fail(target_id, "scene instance() failed");
		return;
	}

	// Old scene out BEFORE the new one goes in. A GameWorld files its persistent
	// state as it leaves the tree and reads it back as it becomes ready; with
	// the order the other way round, reloading the current scene would read
	// the state before the outgoing copy had written it.
	Node *old_scene = st->get_current_scene();
	if (old_scene && old_scene != new_root) {
		st->get_root()->remove_child(old_scene);
		old_scene->queue_delete();
	}
	st->get_root()->add_child(new_root);
	st->set_current_scene(new_root);

	current_id = target_id;
	current_spawn = target_spawn;
	emit_signal("scene_instanced", current_id, new_root);

	Transform xf;
	_resolve_spawn(new_root, target_spawn, xf);
	emit_signal("player_spawn_requested", xf, target_spawn);

	if (!player_target_path.is_empty()) {
		Node *pt = st->get_root()->get_node_or_null(player_target_path);
		if (pt && pt->has_method(player_target_method)) {
			pt->call(player_target_method, xf, target_spawn);
		}
	}

	// Still behind the loading screen: bring in the ground and everything
	// around the arrival point now, so the screen lifts on a finished world
	// rather than one that is still filling in.
	GameWorld *gw = Object::cast_to<GameWorld>(new_root);
	if (gw) {
		gw->prime(xf.origin);
	}

	travelling = false;
	progress = 1.0f;

	SceneTransition *t = _get_transition();
	if (t) {
		t->hide_loading();
	}
	emit_signal("travel_finished", current_id);
}

void SceneManager::_fail(const String &p_id, const String &p_reason) {
	ERR_PRINT("SceneManager: travel to '" + p_id + "' failed: " + p_reason);
	travelling = false;
	_load_done = false;
	_suppress_history = false;
	loader.unref();
	SceneTransition *t = _get_transition();
	if (t) {
		t->hide_loading();
	}
	emit_signal("travel_failed", p_id, p_reason);
}

bool SceneManager::_resolve_spawn(Node *p_scene_root, const String &p_spawn_id, Transform &r_xform) const {
	Spatial *rs = Object::cast_to<Spatial>(p_scene_root);
	r_xform = rs ? rs->get_global_transform() : Transform();

	SceneTree *st = SceneTree::get_singleton();
	if (!st || !p_scene_root) {
		return false;
	}

	List<Node *> nodes;
	st->get_nodes_in_group("scene_spawn_points", &nodes);
	SceneSpawnPoint *fallback = nullptr;
	for (List<Node *>::Element *E = nodes.front(); E; E = E->next()) {
		SceneSpawnPoint *sp = Object::cast_to<SceneSpawnPoint>(E->get());
		if (!sp) {
			continue;
		}
		bool in_new_scene = (sp == p_scene_root) || p_scene_root->is_a_parent_of(sp);
		if (!in_new_scene) {
			continue;
		}
		if (!fallback) {
			fallback = sp;
		}
		if (!p_spawn_id.empty() && sp->get_spawn_id() == p_spawn_id) {
			r_xform = sp->get_spawn_transform();
			return true;
		}
	}

	if (fallback) {
		r_xform = fallback->get_spawn_transform();
		if (!p_spawn_id.empty()) {
			WARN_PRINT("SceneManager: spawn '" + p_spawn_id + "' not found in '" + target_id +
					"', using '" + fallback->get_spawn_id() + "'");
		}
		return p_spawn_id.empty();
	}

	if (!p_spawn_id.empty()) {
		WARN_PRINT("SceneManager: no SceneSpawnPoint in '" + target_id + "', placing at scene origin");
	}
	return false;
}

// --- Public API ---

void SceneManager::set_catalog(const Ref<SceneCatalog> &p_catalog) {
	catalog = p_catalog;
}

void SceneManager::set_catalog_path(const String &p_path) {
	Ref<SceneCatalog> c = ResourceLoader::load(p_path);
	if (c.is_null()) {
		ERR_PRINT("SceneManager: could not load catalog '" + p_path + "'");
		return;
	}
	catalog = c;
}

void SceneManager::set_loading_screen(const String &p_path) {
	loading_screen_path = p_path;
}

void SceneManager::set_player_target(const NodePath &p_path, const StringName &p_method) {
	player_target_path = p_path;
	player_target_method = p_method;
}

void SceneManager::set_max_polls_per_frame(int p_n) {
	max_polls_per_frame = MAX(1, p_n);
}

void SceneManager::bootstrap() {
	_ensure_runtime();
}

void SceneManager::travel_to(const String &p_scene_id, const String &p_spawn_id) {
	_ensure_runtime();

	if (travelling) {
		WARN_PRINT("SceneManager: already travelling, ignoring travel_to('" + p_scene_id + "')");
		return;
	}
	if (catalog.is_null()) {
		_fail(p_scene_id, "no catalog set (call set_catalog / set_catalog_path)");
		return;
	}
	Ref<SceneCatalogEntry> entry = catalog->get_entry(p_scene_id);
	if (entry.is_null()) {
		_fail(p_scene_id, "unknown scene id");
		return;
	}
	String path = entry->get_scene_path();
	if (path.empty()) {
		_fail(p_scene_id, "catalog entry has no scene_path");
		return;
	}
	if (!ResourceLoader::exists(path)) {
		_fail(p_scene_id, "scene file not found: " + path);
		return;
	}

	target_id = p_scene_id;
	target_spawn = p_spawn_id.empty() ? entry->get_default_spawn() : p_spawn_id;

	if (_suppress_history) {
		_suppress_history = false;
	} else if (!current_id.empty()) {
		history.push_back(current_id);
	}

	// A baked world keeps its objects in per-cell files; load the stripped
	// scene that goes with them instead of the full authoring scene.
	path = GameWorld::resolve_baked_scene(path);

	Error err = OK;
	loader = ResourceLoader::load_interactive(path, "", false, &err);
	if (loader.is_null()) {
		_fail(p_scene_id, "cannot open " + path);
		return;
	}

	travelling = true;
	_load_done = false;
	progress = 0.0f;

	emit_signal("travel_started", p_scene_id);
	SceneTransition *t = _get_transition();
	if (t) {
		t->show_loading(loading_screen_path);
	}
}

void SceneManager::reload_current() {
	if (current_id.empty()) {
		ERR_PRINT("SceneManager: no current scene to reload");
		return;
	}
	_suppress_history = true;
	travel_to(current_id, current_spawn);
}

void SceneManager::travel_back() {
	if (history.empty()) {
		ERR_PRINT("SceneManager: travel history is empty");
		return;
	}
	String prev = history[history.size() - 1];
	history.remove(history.size() - 1);
	_suppress_history = true;
	travel_to(prev, String());
}

void SceneManager::store_world_state(const String &p_key, const Dictionary &p_state) {
	world_states[p_key] = p_state;
}

bool SceneManager::has_world_state(const String &p_key) const {
	return world_states.has(p_key);
}

Dictionary SceneManager::get_world_state(const String &p_key) const {
	return world_states.has(p_key) ? (Dictionary)world_states[p_key] : Dictionary();
}

Dictionary SceneManager::get_world_states() const {
	// The current scene's world has not left the tree, so its state is not in
	// the store yet; fold it in so a save taken right now is complete.
	Dictionary out = world_states.duplicate();
	GameWorld *w = get_world_game();
	if (w) {
		const Node *root = w->get_owner() ? w->get_owner() : (const Node *)w;
		const String key = w->get_source_scene().empty() ? root->get_filename() : w->get_source_scene();
		if (!key.empty()) {
			out[key] = w->get_world_state();
		}
	}
	return out;
}

void SceneManager::set_world_states(const Dictionary &p_states) {
	world_states = p_states.duplicate();
	GameWorld *w = get_world_game();
	if (w) {
		const Node *root = w->get_owner() ? w->get_owner() : (const Node *)w;
		const String key = w->get_source_scene().empty() ? root->get_filename() : w->get_source_scene();
		if (world_states.has(key)) {
			w->set_world_state(world_states[key]);
		}
	}
}

void SceneManager::clear_world_states() {
	world_states.clear();
}

GameWorld *SceneManager::get_world_game() const {
	SceneTree *st = SceneTree::get_singleton();
	if (!st) {
		return nullptr;
	}
	return Object::cast_to<GameWorld>(st->get_current_scene());
}

Node *SceneManager::get_location_by_id(const String &p_id) const {
	GameWorld *w = get_world_game();
	return w ? w->get_location_by_id(p_id) : nullptr;
}

Array SceneManager::get_locations() const {
	GameWorld *w = get_world_game();
	return w ? w->get_locations() : Array();
}

void SceneManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_world_states"), &SceneManager::get_world_states);
	ClassDB::bind_method(D_METHOD("set_world_states", "states"), &SceneManager::set_world_states);
	ClassDB::bind_method(D_METHOD("clear_world_states"), &SceneManager::clear_world_states);

	ClassDB::bind_method(D_METHOD("set_catalog", "catalog"), &SceneManager::set_catalog);
	ClassDB::bind_method(D_METHOD("get_catalog"), &SceneManager::get_catalog);
	ClassDB::bind_method(D_METHOD("set_catalog_path", "path"), &SceneManager::set_catalog_path);

	ClassDB::bind_method(D_METHOD("set_loading_screen", "path"), &SceneManager::set_loading_screen);
	ClassDB::bind_method(D_METHOD("get_loading_screen"), &SceneManager::get_loading_screen);

	ClassDB::bind_method(D_METHOD("set_player_target", "path", "method"), &SceneManager::set_player_target);
	ClassDB::bind_method(D_METHOD("get_player_target_path"), &SceneManager::get_player_target_path);
	ClassDB::bind_method(D_METHOD("get_player_target_method"), &SceneManager::get_player_target_method);

	ClassDB::bind_method(D_METHOD("set_max_polls_per_frame", "count"), &SceneManager::set_max_polls_per_frame);
	ClassDB::bind_method(D_METHOD("get_max_polls_per_frame"), &SceneManager::get_max_polls_per_frame);

	ClassDB::bind_method(D_METHOD("bootstrap"), &SceneManager::bootstrap);
	ClassDB::bind_method(D_METHOD("travel_to", "scene_id", "spawn_id"), &SceneManager::travel_to, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("reload_current"), &SceneManager::reload_current);
	ClassDB::bind_method(D_METHOD("travel_back"), &SceneManager::travel_back);

	ClassDB::bind_method(D_METHOD("get_current_scene_id"), &SceneManager::get_current_scene_id);
	ClassDB::bind_method(D_METHOD("can_travel_back"), &SceneManager::can_travel_back);
	ClassDB::bind_method(D_METHOD("is_travelling"), &SceneManager::is_travelling);
	ClassDB::bind_method(D_METHOD("get_progress"), &SceneManager::get_progress);

	ClassDB::bind_method(D_METHOD("get_world_game"), &SceneManager::get_world_game);
	ClassDB::bind_method(D_METHOD("get_location_by_id", "id"), &SceneManager::get_location_by_id);
	ClassDB::bind_method(D_METHOD("get_locations"), &SceneManager::get_locations);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "catalog", PROPERTY_HINT_RESOURCE_TYPE, "SceneCatalog"), "set_catalog", "get_catalog");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "loading_screen", PROPERTY_HINT_FILE, "*.tscn,*.scn"), "set_loading_screen", "get_loading_screen");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_polls_per_frame", PROPERTY_HINT_RANGE, "1,64,1"), "set_max_polls_per_frame", "get_max_polls_per_frame");

	ADD_SIGNAL(MethodInfo("travel_started", PropertyInfo(Variant::STRING, "scene_id")));
	ADD_SIGNAL(MethodInfo("load_progress", PropertyInfo(Variant::REAL, "ratio")));
	ADD_SIGNAL(MethodInfo("scene_instanced", PropertyInfo(Variant::STRING, "scene_id"),
			PropertyInfo(Variant::OBJECT, "scene_root", PROPERTY_HINT_RESOURCE_TYPE, "Node")));
	ADD_SIGNAL(MethodInfo("player_spawn_requested", PropertyInfo(Variant::TRANSFORM, "transform"),
			PropertyInfo(Variant::STRING, "spawn_id")));
	ADD_SIGNAL(MethodInfo("travel_finished", PropertyInfo(Variant::STRING, "scene_id")));
	ADD_SIGNAL(MethodInfo("travel_failed", PropertyInfo(Variant::STRING, "scene_id"),
			PropertyInfo(Variant::STRING, "reason")));
}
