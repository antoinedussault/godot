#include "register_types.h"

#include "core/class_db.h"

#include "shader_sky.h"

void register_sky_shader_types() {
	ClassDB::register_class<ShaderSky>();
}

void unregister_sky_shader_types() {
}
