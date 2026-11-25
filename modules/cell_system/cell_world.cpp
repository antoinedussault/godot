#include "cell_world.h"

void CellWorld::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_load_radius", "r"), &CellWorld::set_load_radius);
	ClassDB::bind_method(D_METHOD("get_load_radius"), &CellWorld::get_load_radius);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "load_radius", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR), "set_load_radius", "get_load_radius");
}

String CellWorld::get_configuration_warning() const {
	return "CellWorld is deprecated and no longer streams anything. Use a GameWorld as the scene root - it owns the grid, the terrain, the foliage and object streaming. Press \"Migrate Legacy Scene\" in the World dock to convert.";
}
