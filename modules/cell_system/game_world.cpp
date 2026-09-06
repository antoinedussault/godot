#include "game_world.h"

void GameWorld::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_layer_data", "data"), &GameWorld::set_layer_data);
	ClassDB::bind_method(D_METHOD("get_layer_data"), &GameWorld::get_layer_data);
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "layer_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR | PROPERTY_USAGE_STORAGE),
			"set_layer_data", "get_layer_data");
}
