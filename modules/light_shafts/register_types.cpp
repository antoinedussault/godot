#include "register_types.h"

#include "core/class_db.h"

#include "directional_light_shaft.h"

void register_light_shafts_types() {
	ClassDB::register_class<DirectionalLightShaft>();
}

void unregister_light_shafts_types() {
	// ClassDB handles teardown.
}
