#include "scene_door.h"

#include "core/engine.h"
#include "core/os/os.h"

#include "scene_manager.h"

SceneDoor::SceneDoor() {
}

void SceneDoor::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		if (Engine::get_singleton()->is_editor_hint()) {
			return;
		}
		if (trigger_mode == ON_BODY_ENTER) {
			connect("body_entered", this, "_on_body_entered");
		}
	}
}

bool SceneDoor::_can_trigger() const {
	if (_spent) {
		return false;
	}
	SceneManager *sm = SceneManager::get_singleton();
	if (sm && sm->is_travelling()) {
		return false;
	}
	if (cooldown > 0.0f && _last_trigger_msec != 0) {
		uint64_t now = OS::get_singleton()->get_ticks_msec();
		if ((now - _last_trigger_msec) < (uint64_t)(cooldown * 1000.0f)) {
			return false;
		}
	}
	return true;
}

void SceneDoor::_do_travel() {
	SceneManager *sm = SceneManager::get_singleton();
	if (!sm) {
		ERR_PRINT("SceneDoor: SceneManager singleton unavailable");
		return;
	}
	if (target_scene.empty()) {
		ERR_PRINT("SceneDoor: target_scene is not set");
		return;
	}
	_last_trigger_msec = OS::get_singleton()->get_ticks_msec();
	if (one_shot) {
		_spent = true;
	}
	sm->travel_to(target_scene, target_spawn);
}

void SceneDoor::_on_body_entered(Node *p_body) {
	if (!_can_trigger()) {
		return;
	}
	if (!body_filter.empty() && (!p_body || !p_body->is_in_group(body_filter))) {
		return;
	}
	_do_travel();
}

void SceneDoor::activate() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if (!_can_trigger()) {
		return;
	}
	_do_travel();
}

void SceneDoor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_target_scene", "id"), &SceneDoor::set_target_scene);
	ClassDB::bind_method(D_METHOD("get_target_scene"), &SceneDoor::get_target_scene);
	ClassDB::bind_method(D_METHOD("set_target_spawn", "id"), &SceneDoor::set_target_spawn);
	ClassDB::bind_method(D_METHOD("get_target_spawn"), &SceneDoor::get_target_spawn);
	ClassDB::bind_method(D_METHOD("set_trigger_mode", "mode"), &SceneDoor::set_trigger_mode);
	ClassDB::bind_method(D_METHOD("get_trigger_mode"), &SceneDoor::get_trigger_mode);
	ClassDB::bind_method(D_METHOD("set_body_filter", "group"), &SceneDoor::set_body_filter);
	ClassDB::bind_method(D_METHOD("get_body_filter"), &SceneDoor::get_body_filter);
	ClassDB::bind_method(D_METHOD("set_one_shot", "one_shot"), &SceneDoor::set_one_shot);
	ClassDB::bind_method(D_METHOD("get_one_shot"), &SceneDoor::get_one_shot);
	ClassDB::bind_method(D_METHOD("set_cooldown", "seconds"), &SceneDoor::set_cooldown);
	ClassDB::bind_method(D_METHOD("get_cooldown"), &SceneDoor::get_cooldown);
	ClassDB::bind_method(D_METHOD("activate"), &SceneDoor::activate);
	ClassDB::bind_method(D_METHOD("_on_body_entered", "body"), &SceneDoor::_on_body_entered);

	ADD_GROUP("Scene Door", "");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "target_scene"), "set_target_scene", "get_target_scene");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "target_spawn"), "set_target_spawn", "get_target_spawn");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "trigger_mode", PROPERTY_HINT_ENUM, "On Body Enter,On Interact"), "set_trigger_mode", "get_trigger_mode");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "body_filter"), "set_body_filter", "get_body_filter");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "one_shot"), "set_one_shot", "get_one_shot");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "cooldown", PROPERTY_HINT_RANGE, "0,30,0.1"), "set_cooldown", "get_cooldown");

	BIND_ENUM_CONSTANT(ON_BODY_ENTER);
	BIND_ENUM_CONSTANT(ON_INTERACT);
}
