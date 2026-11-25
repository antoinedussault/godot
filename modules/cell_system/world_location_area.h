#ifndef WORLD_LOCATION_AREA_H
#define WORLD_LOCATION_AREA_H

#include "scene/3d/area.h"

class Texture;

// Marks a spatial region as a named location — a settlement, dungeon, or
// landmark, Creation-Engine "Location" style — and fires signals as a body
// crosses its bounds. Independent of SceneManager/SceneDoor: this node lives
// *inside* a scene and never loads anything, it just announces "the player is
// now in Sanctuary Hills" for a HUD banner, map reveal, or fast-travel list to
// react to.
//
// Nest a smaller WorldLocationArea inside a bigger one freely (e.g. a house
// inside a settlement) — each fires its own enter/exit independently; there is
// no built-in "innermost wins" resolution here, a HUD script listening to both
// can layer that itself if it wants it.
class WorldLocationArea : public Area {
	GDCLASS(WorldLocationArea, Area);

public:
	enum LocationType {
		TYPE_WILDERNESS = 0,
		TYPE_SETTLEMENT = 1,
		TYPE_DUNGEON = 2,
		TYPE_LANDMARK = 3,
	};

private:
	String location_id;
	String display_name;
	LocationType location_type = TYPE_WILDERNESS;
	String body_filter = "player"; // group an entering body must be in ("" = any)

	bool discovered = false;
	bool discover_on_enter = true; // first matching entry marks it discovered
	bool notify_on_enter = true;   // fire location_entered/location_exited at all

	Ref<Texture> map_icon;

	// How many currently-overlapping bodies match body_filter — location_entered
	// fires on 0->1, location_exited on 1->0, so a second matching body (e.g.
	// local co-op) doesn't retrigger or early-exit the notification.
	int _inside_count = 0;

	void _on_body_entered(Node *p_body);
	void _on_body_exited(Node *p_body);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_location_id(const String &p_id) { location_id = p_id; }
	String get_location_id() const { return location_id; }
	void set_display_name(const String &p_name) { display_name = p_name; }
	String get_display_name() const { return display_name; }
	void set_location_type(int p_type) { location_type = (LocationType)p_type; }
	int get_location_type() const { return location_type; }
	void set_body_filter(const String &p_group) { body_filter = p_group; }
	String get_body_filter() const { return body_filter; }

	void set_discovered(bool p_discovered);
	bool is_discovered() const { return discovered; }
	void set_discover_on_enter(bool p_discover) { discover_on_enter = p_discover; }
	bool get_discover_on_enter() const { return discover_on_enter; }
	void set_notify_on_enter(bool p_notify) { notify_on_enter = p_notify; }
	bool get_notify_on_enter() const { return notify_on_enter; }

	void set_map_icon(const Ref<Texture> &p_icon) { map_icon = p_icon; }
	Ref<Texture> get_map_icon() const { return map_icon; }

	bool is_body_inside() const { return _inside_count > 0; }

	WorldLocationArea();
};

VARIANT_ENUM_CAST(WorldLocationArea::LocationType);

#endif // WORLD_LOCATION_AREA_H
