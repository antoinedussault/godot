#include "world_location_area.h"

#include "core/engine.h"

static const char *LOCATION_GROUP = "world_locations";

WorldLocationArea::WorldLocationArea() {
}

void WorldLocationArea::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			add_to_group(LOCATION_GROUP);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			remove_from_group(LOCATION_GROUP);
		} break;
		case NOTIFICATION_READY: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return;
			}
			connect("body_entered", this, "_on_body_entered");
			connect("body_exited", this, "_on_body_exited");
		} break;
	}
}

void WorldLocationArea::_on_body_entered(Node *p_body) {
	if (!p_body || (!body_filter.empty() && !p_body->is_in_group(body_filter))) {
		return;
	}
	_inside_count++;
	if (_inside_count != 1) {
		return; // already occupied by another matching body
	}

	if (discover_on_enter && !discovered) {
		set_discovered(true);
	}
	if (notify_on_enter) {
		emit_signal("location_entered", location_id, display_name);
	}
}

void WorldLocationArea::_on_body_exited(Node *p_body) {
	if (!p_body || (!body_filter.empty() && !p_body->is_in_group(body_filter))) {
		return;
	}
	if (_inside_count <= 0) {
		return;
	}
	_inside_count--;
	if (_inside_count != 0) {
		return; // still occupied by another matching body
	}

	if (notify_on_enter) {
		emit_signal("location_exited", location_id, display_name);
	}
}

void WorldLocationArea::set_discovered(bool p_discovered) {
	if (discovered == p_discovered) {
		return;
	}
	discovered = p_discovered;
	if (discovered) {
		emit_signal("location_discovered", location_id, display_name);
	}
}

void WorldLocationArea::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_location_id", "id"), &WorldLocationArea::set_location_id);
	ClassDB::bind_method(D_METHOD("get_location_id"), &WorldLocationArea::get_location_id);
	ClassDB::bind_method(D_METHOD("set_display_name", "name"), &WorldLocationArea::set_display_name);
	ClassDB::bind_method(D_METHOD("get_display_name"), &WorldLocationArea::get_display_name);
	ClassDB::bind_method(D_METHOD("set_location_type", "type"), &WorldLocationArea::set_location_type);
	ClassDB::bind_method(D_METHOD("get_location_type"), &WorldLocationArea::get_location_type);
	ClassDB::bind_method(D_METHOD("set_body_filter", "group"), &WorldLocationArea::set_body_filter);
	ClassDB::bind_method(D_METHOD("get_body_filter"), &WorldLocationArea::get_body_filter);

	ClassDB::bind_method(D_METHOD("set_discovered", "discovered"), &WorldLocationArea::set_discovered);
	ClassDB::bind_method(D_METHOD("is_discovered"), &WorldLocationArea::is_discovered);
	ClassDB::bind_method(D_METHOD("set_discover_on_enter", "discover"), &WorldLocationArea::set_discover_on_enter);
	ClassDB::bind_method(D_METHOD("get_discover_on_enter"), &WorldLocationArea::get_discover_on_enter);
	ClassDB::bind_method(D_METHOD("set_notify_on_enter", "notify"), &WorldLocationArea::set_notify_on_enter);
	ClassDB::bind_method(D_METHOD("get_notify_on_enter"), &WorldLocationArea::get_notify_on_enter);

	ClassDB::bind_method(D_METHOD("set_map_icon", "icon"), &WorldLocationArea::set_map_icon);
	ClassDB::bind_method(D_METHOD("get_map_icon"), &WorldLocationArea::get_map_icon);

	ClassDB::bind_method(D_METHOD("is_body_inside"), &WorldLocationArea::is_body_inside);

	ClassDB::bind_method(D_METHOD("_on_body_entered", "body"), &WorldLocationArea::_on_body_entered);
	ClassDB::bind_method(D_METHOD("_on_body_exited", "body"), &WorldLocationArea::_on_body_exited);

	ADD_GROUP("World Location", "");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "location_id"), "set_location_id", "get_location_id");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name"), "set_display_name", "get_display_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "location_type", PROPERTY_HINT_ENUM, "Wilderness,Settlement,Dungeon,Landmark"), "set_location_type", "get_location_type");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "body_filter"), "set_body_filter", "get_body_filter");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "discovered"), "set_discovered", "is_discovered");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "discover_on_enter"), "set_discover_on_enter", "get_discover_on_enter");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "notify_on_enter"), "set_notify_on_enter", "get_notify_on_enter");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "map_icon", PROPERTY_HINT_RESOURCE_TYPE, "Texture"), "set_map_icon", "get_map_icon");

	ADD_SIGNAL(MethodInfo("location_entered", PropertyInfo(Variant::STRING, "location_id"), PropertyInfo(Variant::STRING, "display_name")));
	ADD_SIGNAL(MethodInfo("location_exited", PropertyInfo(Variant::STRING, "location_id"), PropertyInfo(Variant::STRING, "display_name")));
	ADD_SIGNAL(MethodInfo("location_discovered", PropertyInfo(Variant::STRING, "location_id"), PropertyInfo(Variant::STRING, "display_name")));

	BIND_ENUM_CONSTANT(TYPE_WILDERNESS);
	BIND_ENUM_CONSTANT(TYPE_SETTLEMENT);
	BIND_ENUM_CONSTANT(TYPE_DUNGEON);
	BIND_ENUM_CONSTANT(TYPE_LANDMARK);
}
