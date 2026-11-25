#include "register_types.h"

#include "box3d_physics_server.h"
#include "core/class_db.h"
#include "core/project_settings.h"

#ifndef _3D_DISABLED
static PhysicsServer *_create_box3d_physics_callback() {
	return memnew(Box3DPhysicsServer);
}
#endif

void register_box3d_types() {
#ifndef _3D_DISABLED
	// Registered as an option only. No set_default_server() call here on
	// purpose: Bullet stays the default, so building this module cannot change
	// the behaviour of any existing project. Opt in per project with
	// physics/3d/physics_engine = "Box3D".
	// Threads Box3D may use for one world. Every active world gets its own.
	GLOBAL_DEF("physics/3d/box3d/worker_count", 1);
	ProjectSettings::get_singleton()->set_custom_property_info("physics/3d/box3d/worker_count",
			PropertyInfo(Variant::INT, "physics/3d/box3d/worker_count", PROPERTY_HINT_RANGE, "1,16,1"));

	PhysicsServerManager::register_server("Box3D", &_create_box3d_physics_callback);
#endif
}

void unregister_box3d_types() {
}
