#include "cell_player_body.h"

#include "cell_terrain_manager.h"
#include "cell_world.h"
#include "core/engine.h"
#include "scene/3d/camera.h"
#include "scene/main/viewport.h"

CellPlayerBody::CellPlayerBody() {}

Camera *CellPlayerBody::_get_active_camera() const {
	if (!is_inside_tree()) return nullptr;
	Viewport *vp = const_cast<CellPlayerBody *>(this)->get_viewport();
	return vp ? vp->get_camera() : nullptr;
}

Vector3 CellPlayerBody::_get_stream_pos() const {
	Camera *cam = _get_active_camera();
	return cam ? cam->get_global_transform().origin : get_global_transform().origin;
}

void CellPlayerBody::set_cell_size(float s) { cell_size = s; }
float CellPlayerBody::get_cell_size() const { return cell_size; }

void CellPlayerBody::set_terrain_manager_path(const NodePath &p) { terrain_manager_path = p; }
NodePath CellPlayerBody::get_terrain_manager_path() const { return terrain_manager_path; }

void CellPlayerBody::set_cell_world_path(const NodePath &p) { cell_world_path = p; }
NodePath CellPlayerBody::get_cell_world_path() const { return cell_world_path; }

CellTerrainManager *CellPlayerBody::_get_terrain_manager() const {
	if (terrain_manager_path.is_empty()) return nullptr;
	Node *n = get_node_or_null(terrain_manager_path);
	return Object::cast_to<CellTerrainManager>(n);
}

CellWorld *CellPlayerBody::_get_cell_world() const {
	if (cell_world_path.is_empty()) return nullptr;
	Node *n = get_node_or_null(cell_world_path);
	return Object::cast_to<CellWorld>(n);
}

void CellPlayerBody::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!Engine::get_singleton()->is_editor_hint()) {
				// Synchronously load the spawn cell so the player lands on solid
				// ground immediately instead of falling through an unloaded chunk.
				CellTerrainManager *mgr = _get_terrain_manager();
				if (mgr) {
					Vector3 pos = _get_stream_pos();
					int cx = (int)Math::floor(pos.x / cell_size);
					int cz = (int)Math::floor(pos.z / cell_size);
					mgr->ensure_cell_sync(cx, cz);
				}
			}
			set_physics_process(true);
		} break;

		case NOTIFICATION_PHYSICS_PROCESS: {
			if (Engine::get_singleton()->is_editor_hint()) break;

			// Track our own velocity from the transform delta so pushable
			// CellItemBody props know how hard we're leaning into them.
			{
				float dt = get_physics_process_delta_time();
				Vector3 cur = get_global_transform().origin;
				if (_has_last_pos && dt > 0.0f) {
					_linear_velocity = (cur - _last_pos) / dt;
				}
				_last_pos = cur;
				_has_last_pos = true;
			}

			Vector3 pos = _get_stream_pos();
			int cx = (int)Math::floor(pos.x / cell_size);
			int cz = (int)Math::floor(pos.z / cell_size);

			bool changed = (!_has_cell || cx != _cell_x || cz != _cell_z);

			if (changed) {
				int old_x = _cell_x;
				int old_z = _cell_z;
				_cell_x = cx;
				_cell_z = cz;
				_has_cell = true;

				// Drive terrain streaming directly.
				CellTerrainManager *mgr = _get_terrain_manager();
				if (mgr) {
					mgr->stream_to(pos);
					// If collision for the new cell isn't ready yet, freeze this node
					// until it is. NOTIFICATION_PROCESS re-enables physics once loaded.
					if (!mgr->get_cell(_cell_x, _cell_z)) {
						_waiting_for_cell = true;
						set_physics_process(false);
						set_process(true);
					}
				}

				// Drive object streaming.
				CellWorld *cw = _get_cell_world();
				if (cw) {
					cw->stream_around(cx, cz);
				}

				emit_signal("cell_changed", cx, cz, old_x, old_z);
			}
		} break;

		case NOTIFICATION_PROCESS: {
			if (_waiting_for_cell) {
				CellTerrainManager *mgr = _get_terrain_manager();
				if (!mgr || mgr->get_cell(_cell_x, _cell_z)) {
					_waiting_for_cell = false;
					set_process(false);
					set_physics_process(true);
				}
			}
		} break;
	}
}

bool CellPlayerBody::is_terrain_loaded() const {
	if (!_has_cell) return false;
	CellTerrainManager *mgr = _get_terrain_manager();
	return mgr && mgr->get_cell(_cell_x, _cell_z) != nullptr;
}

void CellPlayerBody::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_cell_size", "s"), &CellPlayerBody::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &CellPlayerBody::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_terrain_manager_path", "p"), &CellPlayerBody::set_terrain_manager_path);
	ClassDB::bind_method(D_METHOD("get_terrain_manager_path"), &CellPlayerBody::get_terrain_manager_path);
	ClassDB::bind_method(D_METHOD("set_cell_world_path", "p"), &CellPlayerBody::set_cell_world_path);
	ClassDB::bind_method(D_METHOD("get_cell_world_path"), &CellPlayerBody::get_cell_world_path);
	ClassDB::bind_method(D_METHOD("get_cell_x"), &CellPlayerBody::get_cell_x);
	ClassDB::bind_method(D_METHOD("get_cell_z"), &CellPlayerBody::get_cell_z);
	ClassDB::bind_method(D_METHOD("is_terrain_loaded"), &CellPlayerBody::is_terrain_loaded);
	ClassDB::bind_method(D_METHOD("get_linear_velocity"), &CellPlayerBody::get_linear_velocity);

	ADD_PROPERTY(PropertyInfo(Variant::REAL, "cell_size"), "set_cell_size", "get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "terrain_manager_path"), "set_terrain_manager_path", "get_terrain_manager_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "cell_world_path"), "set_cell_world_path", "get_cell_world_path");

	ADD_SIGNAL(MethodInfo("cell_changed",
		PropertyInfo(Variant::INT, "new_x"),
		PropertyInfo(Variant::INT, "new_z"),
		PropertyInfo(Variant::INT, "old_x"),
		PropertyInfo(Variant::INT, "old_z")));
}
