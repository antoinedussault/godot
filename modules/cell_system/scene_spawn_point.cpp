#include "scene_spawn_point.h"

static const char *SPAWN_GROUP = "scene_spawn_points";

void SceneSpawnPoint::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			add_to_group(SPAWN_GROUP);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			remove_from_group(SPAWN_GROUP);
		} break;
	}
}

Transform SceneSpawnPoint::get_spawn_transform() const {
	Transform gxf = get_global_transform();
	if (use_flat_orientation) {
		float yaw = gxf.basis.get_euler().y;
		gxf.basis = Basis(Vector3(0.0f, yaw, 0.0f));
	}
	return gxf;
}

void SceneSpawnPoint::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_spawn_id", "id"), &SceneSpawnPoint::set_spawn_id);
	ClassDB::bind_method(D_METHOD("get_spawn_id"), &SceneSpawnPoint::get_spawn_id);
	ClassDB::bind_method(D_METHOD("set_use_flat_orientation", "flat"), &SceneSpawnPoint::set_use_flat_orientation);
	ClassDB::bind_method(D_METHOD("get_use_flat_orientation"), &SceneSpawnPoint::get_use_flat_orientation);
	ClassDB::bind_method(D_METHOD("get_spawn_transform"), &SceneSpawnPoint::get_spawn_transform);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "spawn_id"), "set_spawn_id", "get_spawn_id");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_flat_orientation"), "set_use_flat_orientation", "get_use_flat_orientation");
}
