#include "cell_player_body.h"

#include "cell_terrain_manager.h"
#include "core/engine.h"
#include "core/list.h"
#include "game_world.h"
#include "scene/main/scene_tree.h"

CellPlayerBody::CellPlayerBody() {}

void CellPlayerBody::set_terrain_manager_path(const NodePath &p) { terrain_manager_path = p; }
NodePath CellPlayerBody::get_terrain_manager_path() const { return terrain_manager_path; }

GameWorld *CellPlayerBody::_get_world() {
	GameWorld *w = Object::cast_to<GameWorld>(ObjectDB::get_instance(_world_id));
	if (w && w->is_inside_tree()) {
		return w;
	}
	_world_id = 0;
	if (!is_inside_tree()) {
		return nullptr;
	}
	// The player usually lives in an autoload, OUTSIDE the world scene, and the
	// world is swapped under it on every scene change - so this is looked up
	// through a group rather than a path, and looked up again whenever the one
	// we knew has gone.
	List<Node *> worlds;
	get_tree()->get_nodes_in_group("game_worlds", &worlds);
	GameWorld *first = nullptr;
	for (List<Node *>::Element *E = worlds.front(); E; E = E->next()) {
		GameWorld *g = Object::cast_to<GameWorld>(E->get());
		if (!g) {
			continue;
		}
		if (g->is_a_parent_of(this)) {
			first = g; // the one we are actually inside wins
			break;
		}
		if (!first) {
			first = g;
		}
	}
	if (first) {
		_world_id = first->get_instance_id();
	}
	return first;
}

Node *CellPlayerBody::get_game_world() {
	return _get_world();
}

CellTerrainManager *CellPlayerBody::_get_fallback_terrain() const {
	if (terrain_manager_path.is_empty()) {
		return nullptr;
	}
	return Object::cast_to<CellTerrainManager>(get_node_or_null(terrain_manager_path));
}

bool CellPlayerBody::_ground_ready(const Vector3 &p_pos) {
	GameWorld *w = _get_world();
	if (w) {
		return w->is_ground_ready(p_pos);
	}
	CellTerrainManager *tm = _get_fallback_terrain();
	return !tm || tm->get_cell_at_world(p_pos) != nullptr;
}

void CellPlayerBody::_ensure_ground(const Vector3 &p_pos) {
	GameWorld *w = _get_world();
	if (w) {
		// Terrain only. prime() would also start taking objects out of the
		// tree, and this runs from _ready - while the scene is still handing
		// out _ready calls and its nodes may not be removed.
		w->ensure_ground(p_pos);
		return;
	}
	CellTerrainManager *tm = _get_fallback_terrain();
	if (tm) {
		tm->ensure_collision_at_world(p_pos);
	}
}

bool CellPlayerBody::is_terrain_loaded() {
	return is_inside_tree() && _ground_ready(get_global_transform().origin);
}

void CellPlayerBody::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!Engine::get_singleton()->is_editor_hint()) {
				// Build what is under us before the first physics step.
				_ensure_ground(get_global_transform().origin);
			}
			set_physics_process(true);
		} break;

		case NOTIFICATION_PHYSICS_PROCESS: {
			if (Engine::get_singleton()->is_editor_hint()) {
				break;
			}
			const Vector3 pos = get_global_transform().origin;

			// Velocity from the transform delta, for pushable props.
			const float dt = get_physics_process_delta_time();
			if (_has_last_pos && dt > 0.0f) {
				_linear_velocity = (pos - _last_pos) / dt;
			}
			_last_pos = pos;
			_has_last_pos = true;

			// Cell tracking on the world's grid - the same grid everything else
			// uses, so these indices mean the same thing to every node.
			GameWorld *w = _get_world();
			int cx = _cell_x, cz = _cell_z;
			if (w) {
				w->world_to_cell_i(pos, cx, cz);
			} else {
				CellTerrainManager *tm = _get_fallback_terrain();
				const float cs = tm ? MAX(1.0f, tm->get_cell_size()) : 64.0f;
				cx = (int)Math::floor(pos.x / cs);
				cz = (int)Math::floor(pos.z / cs);
			}
			if (!_has_cell || cx != _cell_x || cz != _cell_z) {
				const int old_x = _cell_x, old_z = _cell_z;
				_cell_x = cx;
				_cell_z = cz;
				_has_cell = true;
				emit_signal("cell_changed", cx, cz, old_x, old_z);

				// Outran the streamer (teleport, very fast vehicle): stand still
				// until the ground is there. Asked by WORLD position, about where
				// the BODY is - the place it would fall from.
				if (!_ground_ready(pos)) {
					_waiting_for_ground = true;
					set_physics_process(false);
					set_process(true);
				}
			}
		} break;

		case NOTIFICATION_PROCESS: {
			if (_waiting_for_ground && _ground_ready(get_global_transform().origin)) {
				_waiting_for_ground = false;
				set_process(false);
				set_physics_process(true);
			}
		} break;
	}
}

void CellPlayerBody::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_terrain_manager_path", "p"), &CellPlayerBody::set_terrain_manager_path);
	ClassDB::bind_method(D_METHOD("get_terrain_manager_path"), &CellPlayerBody::get_terrain_manager_path);
	ClassDB::bind_method(D_METHOD("get_cell_x"), &CellPlayerBody::get_cell_x);
	ClassDB::bind_method(D_METHOD("get_cell_z"), &CellPlayerBody::get_cell_z);
	ClassDB::bind_method(D_METHOD("is_terrain_loaded"), &CellPlayerBody::is_terrain_loaded);
	ClassDB::bind_method(D_METHOD("get_game_world"), &CellPlayerBody::get_game_world);
	ClassDB::bind_method(D_METHOD("get_linear_velocity"), &CellPlayerBody::get_linear_velocity);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "terrain_manager_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "CellTerrainManager"), "set_terrain_manager_path", "get_terrain_manager_path");

	ADD_SIGNAL(MethodInfo("cell_changed",
			PropertyInfo(Variant::INT, "new_x"),
			PropertyInfo(Variant::INT, "new_z"),
			PropertyInfo(Variant::INT, "old_x"),
			PropertyInfo(Variant::INT, "old_z")));
}
