/* register_types.cpp */
#include "register_types.h"

#include "core/class_db.h"
#include "exp_height_fog.h"

void register_exp_height_fog_types() {
	ClassDB::register_class<ExpHeightFog>();
}

void unregister_exp_height_fog_types() {
	// Nothing to clean up.
}
