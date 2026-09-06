/* register_types.cpp */
#include "register_types.h"

#include "core/class_db.h"

#include "spatial_cable.h"

void register_spatial_cable_types() {
	ClassDB::register_class<SpatialCable>();
}

void unregister_spatial_cable_types() {
	// ClassDB handles teardown.
}
