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
	PhysicsServerManager::register_server("Box3D", &_create_box3d_physics_callback);
#endif
}

void unregister_box3d_types() {
}
