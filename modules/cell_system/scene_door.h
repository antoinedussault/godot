#ifndef SCENE_DOOR_H
#define SCENE_DOOR_H

#include "scene/3d/area.h"

// A linked load door, Creation-Kit style. Place it as an Area with a shape in
// the doorway. When the player walks in (or an interaction system calls
// activate()) it asks the SceneManager singleton to travel to target_scene and
// place the player at target_spawn in that scene.
class SceneDoor : public Area {
	GDCLASS(SceneDoor, Area);

public:
	enum TriggerMode {
		ON_BODY_ENTER = 0, // fires when a body in `body_filter` enters the Area
		ON_INTERACT = 1,   // inert until activate() is called
	};

private:
	String target_scene;              // SceneCatalog entry id
	String target_spawn;              // spawn_id in the destination scene
	TriggerMode trigger_mode = ON_BODY_ENTER;
	String body_filter = "player";    // group the entering body must be in ("" = any)
	bool one_shot = false;
	float cooldown = 1.0f;            // seconds between triggers

	bool _spent = false;
	uint64_t _last_trigger_msec = 0;

	bool _can_trigger() const;
	void _do_travel();
	void _on_body_entered(Node *p_body);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_target_scene(const String &p_id) { target_scene = p_id; }
	String get_target_scene() const { return target_scene; }
	void set_target_spawn(const String &p_id) { target_spawn = p_id; }
	String get_target_spawn() const { return target_spawn; }
	void set_trigger_mode(int p_mode) { trigger_mode = (TriggerMode)p_mode; }
	int get_trigger_mode() const { return trigger_mode; }
	void set_body_filter(const String &p_group) { body_filter = p_group; }
	String get_body_filter() const { return body_filter; }
	void set_one_shot(bool p_one_shot) { one_shot = p_one_shot; }
	bool get_one_shot() const { return one_shot; }
	void set_cooldown(float p_cooldown) { cooldown = MAX(0.0f, p_cooldown); }
	float get_cooldown() const { return cooldown; }

	// Trigger the door regardless of trigger_mode / body_filter (for an
	// interaction / "press E" system).
	void activate();

	SceneDoor();
};

VARIANT_ENUM_CAST(SceneDoor::TriggerMode);

#endif // SCENE_DOOR_H
