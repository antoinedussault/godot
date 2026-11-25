#include "cell_character_body.h"

#include "core/engine.h"
#include "scene/3d/spatial.h"

static const Vector3 UP(0, 1, 0);

CellCharacterBody::CellCharacterBody() {}

// ---------------------------------------------------------------------------
// Node lookup
// ---------------------------------------------------------------------------

// Resolved on every use rather than cached: it is two lookups a physics frame,
// and it keeps working when the view rig is swapped or re-parented at runtime.
Spatial *CellCharacterBody::_get_yaw_node() {
	if (!yaw_node_path.is_empty() && is_inside_tree()) {
		Spatial *n = Object::cast_to<Spatial>(get_node_or_null(yaw_node_path));
		if (n) {
			return n;
		}
	}
	return this;
}

Spatial *CellCharacterBody::_get_pitch_node() {
	if (pitch_node_path.is_empty() || !is_inside_tree()) {
		return nullptr;
	}
	return Object::cast_to<Spatial>(get_node_or_null(pitch_node_path));
}

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

Vector3 CellCharacterBody::get_move_direction() {
	if (_move_input == Vector2() || !is_inside_tree()) {
		return Vector3();
	}
	// Relative to the yaw node, not the pitch node: looking at the floor must
	// not slow the walk down.
	Vector3 dir = _get_yaw_node()->get_global_transform().basis.xform(Vector3(_move_input.x, 0.0f, _move_input.y));
	dir.y = 0.0f;
	const float len = dir.length();
	return len > CMP_EPSILON ? dir / len : Vector3();
}

void CellCharacterBody::jump() {
	_jump_requested = true;
}

void CellCharacterBody::set_velocity(const Vector3 &p_velocity) {
	_velocity = p_velocity;
	// Whoever launches the body upward by hand does not want the steep-slope
	// correction eating the launch on the first wall it brushes.
	_rising_from_jump = p_velocity.y > 0.0f;
}

void CellCharacterBody::set_movement_enabled(bool p_enabled) {
	if (movement_enabled == p_enabled) {
		return;
	}
	movement_enabled = p_enabled;
	if (!p_enabled) {
		// Do not come back from a locked state still carrying the speed the
		// body had when it was locked.
		_velocity = Vector3();
		_jump_requested = false;
		_rising_from_jump = false;
	}
}

void CellCharacterBody::process_movement(float p_delta) {
	if (p_delta <= 0.0f || !is_inside_tree()) {
		return;
	}

	const bool grounded = is_on_floor();

	// --- Acceleration ---
	// The horizontal velocity eases toward dir * speed. The rates are the `t`
	// of lerp(current, target, rate * delta), so a number tuned in a script
	// that did exactly that keeps its feel.
	const Vector3 dir = get_move_direction();
	const bool has_input = dir != Vector3();
	const float input_strength = MIN(1.0f, _move_input.length());

	float rate = has_input ? acceleration : deceleration;
	if (!grounded) {
		rate *= air_control;
	}
	const float t = MIN(1.0f, rate * p_delta);
	const Vector3 target = dir * (max_speed * input_strength);
	_velocity.x = Math::lerp(_velocity.x, target.x, t);
	_velocity.z = Math::lerp(_velocity.z, target.z, t);

	// An exponential ease never reaches zero; below this the body is stopped,
	// not creeping forever (and not sliding off whatever it stands on).
	if (!has_input && (_velocity.x * _velocity.x + _velocity.z * _velocity.z) < stop_speed * stop_speed) {
		_velocity.x = 0.0f;
		_velocity.z = 0.0f;
	}

	// --- Gravity ---
	bool jumped = false;
	if (!grounded) {
		_velocity.y -= gravity * p_delta;
		if (max_fall_speed > 0.0f && _velocity.y < -max_fall_speed) {
			_velocity.y = -max_fall_speed;
		}
	} else if (_jump_requested) {
		_velocity.y = jump_speed;
		jumped = true;
	}
	// A jump asked for in mid-air is dropped, not kept for the landing.
	_jump_requested = false;

	if (jumped) {
		_rising_from_jump = true;
	} else if (_velocity.y <= 0.0f) {
		_rising_from_jump = false;
	}

	// Snapping holds the body to the floor down slopes and stairs. It has to
	// let go the moment the body is meant to leave the ground.
	const bool snap = snap_length > 0.0f && !jumped && _velocity.y <= 0.0f;
	const Vector3 snap_vec = snap ? Vector3(0.0f, -snap_length, 0.0f) : Vector3();

	_velocity = move_and_slide_with_snap(_velocity, snap_vec, UP, stop_on_slope, max_slides, Math::deg2rad(floor_max_angle), infinite_inertia);

	// --- Steep slopes ---
	// floor_max_angle only decides what is_on_floor() calls a floor;
	// move_and_slide will still happily carry the body UP a slope steeper than
	// that. So strip the velocity that climbs one. Only slopes: a wall or a
	// ceiling (normal not facing up) is left to the slide itself, and so is a
	// body still rising from a jump - otherwise jumping next to a steep bank
	// would be cancelled the frame it starts.
	if (!_rising_from_jump) {
		const float floor_cos = Math::cos(Math::deg2rad(floor_max_angle));
		const int count = get_slide_count();
		for (int i = 0; i < count; i++) {
			const Vector3 n = get_slide_collision(i).normal;
			const float up_dot = n.dot(UP);
			if (up_dot > 0.01f && up_dot < floor_cos - 0.001f) {
				_velocity = _velocity.slide(n);
				if (_velocity.y > 0.0f) {
					_velocity.y = 0.0f;
				}
			}
		}
	}

	if (jumped) {
		emit_signal("jumped");
	}
}

// ---------------------------------------------------------------------------
// View
// ---------------------------------------------------------------------------

void CellCharacterBody::set_view_enabled(bool p_enabled) {
	if (view_enabled == p_enabled) {
		return;
	}
	view_enabled = p_enabled;
	if (!p_enabled) {
		// Motion that piled up while the view was locked must not be played
		// back the moment it is unlocked.
		_view_input = Vector2();
		_view_velocity = Vector2();
	}
}

void CellCharacterBody::process_view(float p_delta) {
	const Vector2 target = _view_input * view_sensitivity;
	_view_input = Vector2();

	if (view_smoothing > 0.0f && p_delta > 0.0f) {
		_view_velocity = _view_velocity.linear_interpolate(target, MIN(1.0f, view_smoothing * p_delta));
	} else {
		_view_velocity = target;
	}
	if (_view_velocity != Vector2()) {
		rotate_view(_view_velocity);
	}
}

// The angles live on the nodes, not in this class: every call reads what is
// there, adds to it and writes it back. A spawn point, a cutscene or a
// teleport that turns the rig directly is therefore never fought over.
void CellCharacterBody::rotate_view(const Vector2 &p_degrees) {
	if (!is_inside_tree()) {
		return;
	}
	if (p_degrees.x != 0.0f) {
		_get_yaw_node()->rotate_y(Math::deg2rad(p_degrees.x));
	}
	Spatial *pitch = _get_pitch_node();
	if (pitch && p_degrees.y != 0.0f) {
		set_view_pitch(get_view_pitch() + p_degrees.y);
	}
}

void CellCharacterBody::set_view_yaw(float p_degrees) {
	if (!is_inside_tree()) {
		return;
	}
	Spatial *yaw = _get_yaw_node();
	Vector3 r = yaw->get_rotation();
	r.y = Math::deg2rad(p_degrees);
	yaw->set_rotation(r);
}

float CellCharacterBody::get_view_yaw() {
	if (!is_inside_tree()) {
		return 0.0f;
	}
	return Math::rad2deg(_get_yaw_node()->get_rotation().y);
}

void CellCharacterBody::set_view_pitch(float p_degrees) {
	Spatial *pitch = _get_pitch_node();
	if (!pitch) {
		return;
	}
	const float lo = MIN(view_pitch_min, view_pitch_max);
	const float hi = MAX(view_pitch_min, view_pitch_max);
	Vector3 r = pitch->get_rotation();
	r.x = Math::deg2rad(CLAMP(p_degrees, lo, hi));
	pitch->set_rotation(r);
}

float CellCharacterBody::get_view_pitch() {
	Spatial *pitch = _get_pitch_node();
	return pitch ? Math::rad2deg(pitch->get_rotation().x) : 0.0f;
}

// ---------------------------------------------------------------------------

void CellCharacterBody::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PHYSICS_PROCESS: {
			// CellPlayerBody's own handler has already run (base first), and it
			// switches physics processing off while the ground under the body
			// is still streaming in - so this simply does not tick then.
			if (Engine::get_singleton()->is_editor_hint()) {
				break;
			}
			const float dt = get_physics_process_delta_time();
			// View first: the move direction follows where the view now faces.
			if (view_enabled) {
				process_view(dt);
			}
			if (movement_enabled) {
				process_movement(dt);
			}
		} break;
	}
}

void CellCharacterBody::_bind_methods() {
	ClassDB::bind_method(D_METHOD("process_movement", "delta"), &CellCharacterBody::process_movement);
	ClassDB::bind_method(D_METHOD("process_view", "delta"), &CellCharacterBody::process_view);

	ClassDB::bind_method(D_METHOD("set_move_input", "input"), &CellCharacterBody::set_move_input);
	ClassDB::bind_method(D_METHOD("get_move_input"), &CellCharacterBody::get_move_input);
	ClassDB::bind_method(D_METHOD("set_view_input", "input"), &CellCharacterBody::set_view_input);
	ClassDB::bind_method(D_METHOD("get_view_input"), &CellCharacterBody::get_view_input);
	ClassDB::bind_method(D_METHOD("add_view_input", "input"), &CellCharacterBody::add_view_input);
	ClassDB::bind_method(D_METHOD("jump"), &CellCharacterBody::jump);
	ClassDB::bind_method(D_METHOD("get_move_direction"), &CellCharacterBody::get_move_direction);

	ClassDB::bind_method(D_METHOD("set_velocity", "velocity"), &CellCharacterBody::set_velocity);
	ClassDB::bind_method(D_METHOD("get_velocity"), &CellCharacterBody::get_velocity);
	ClassDB::bind_method(D_METHOD("get_horizontal_velocity"), &CellCharacterBody::get_horizontal_velocity);

	ClassDB::bind_method(D_METHOD("rotate_view", "degrees"), &CellCharacterBody::rotate_view);
	ClassDB::bind_method(D_METHOD("set_view_yaw", "degrees"), &CellCharacterBody::set_view_yaw);
	ClassDB::bind_method(D_METHOD("get_view_yaw"), &CellCharacterBody::get_view_yaw);
	ClassDB::bind_method(D_METHOD("set_view_pitch", "degrees"), &CellCharacterBody::set_view_pitch);
	ClassDB::bind_method(D_METHOD("get_view_pitch"), &CellCharacterBody::get_view_pitch);

	ClassDB::bind_method(D_METHOD("set_movement_enabled", "enabled"), &CellCharacterBody::set_movement_enabled);
	ClassDB::bind_method(D_METHOD("is_movement_enabled"), &CellCharacterBody::is_movement_enabled);
	ClassDB::bind_method(D_METHOD("set_max_speed", "speed"), &CellCharacterBody::set_max_speed);
	ClassDB::bind_method(D_METHOD("get_max_speed"), &CellCharacterBody::get_max_speed);
	ClassDB::bind_method(D_METHOD("set_acceleration", "rate"), &CellCharacterBody::set_acceleration);
	ClassDB::bind_method(D_METHOD("get_acceleration"), &CellCharacterBody::get_acceleration);
	ClassDB::bind_method(D_METHOD("set_deceleration", "rate"), &CellCharacterBody::set_deceleration);
	ClassDB::bind_method(D_METHOD("get_deceleration"), &CellCharacterBody::get_deceleration);
	ClassDB::bind_method(D_METHOD("set_air_control", "amount"), &CellCharacterBody::set_air_control);
	ClassDB::bind_method(D_METHOD("get_air_control"), &CellCharacterBody::get_air_control);
	ClassDB::bind_method(D_METHOD("set_stop_speed", "speed"), &CellCharacterBody::set_stop_speed);
	ClassDB::bind_method(D_METHOD("get_stop_speed"), &CellCharacterBody::get_stop_speed);

	ClassDB::bind_method(D_METHOD("set_gravity", "gravity"), &CellCharacterBody::set_gravity);
	ClassDB::bind_method(D_METHOD("get_gravity"), &CellCharacterBody::get_gravity);
	ClassDB::bind_method(D_METHOD("set_max_fall_speed", "speed"), &CellCharacterBody::set_max_fall_speed);
	ClassDB::bind_method(D_METHOD("get_max_fall_speed"), &CellCharacterBody::get_max_fall_speed);
	ClassDB::bind_method(D_METHOD("set_jump_speed", "speed"), &CellCharacterBody::set_jump_speed);
	ClassDB::bind_method(D_METHOD("get_jump_speed"), &CellCharacterBody::get_jump_speed);
	ClassDB::bind_method(D_METHOD("set_floor_max_angle", "degrees"), &CellCharacterBody::set_floor_max_angle);
	ClassDB::bind_method(D_METHOD("get_floor_max_angle"), &CellCharacterBody::get_floor_max_angle);
	ClassDB::bind_method(D_METHOD("set_snap_length", "length"), &CellCharacterBody::set_snap_length);
	ClassDB::bind_method(D_METHOD("get_snap_length"), &CellCharacterBody::get_snap_length);
	ClassDB::bind_method(D_METHOD("set_stop_on_slope", "enabled"), &CellCharacterBody::set_stop_on_slope);
	ClassDB::bind_method(D_METHOD("is_stop_on_slope_enabled"), &CellCharacterBody::is_stop_on_slope_enabled);
	ClassDB::bind_method(D_METHOD("set_max_slides", "slides"), &CellCharacterBody::set_max_slides);
	ClassDB::bind_method(D_METHOD("get_max_slides"), &CellCharacterBody::get_max_slides);
	ClassDB::bind_method(D_METHOD("set_infinite_inertia", "enabled"), &CellCharacterBody::set_infinite_inertia);
	ClassDB::bind_method(D_METHOD("is_infinite_inertia_enabled"), &CellCharacterBody::is_infinite_inertia_enabled);

	ClassDB::bind_method(D_METHOD("set_view_enabled", "enabled"), &CellCharacterBody::set_view_enabled);
	ClassDB::bind_method(D_METHOD("is_view_enabled"), &CellCharacterBody::is_view_enabled);
	ClassDB::bind_method(D_METHOD("set_yaw_node_path", "path"), &CellCharacterBody::set_yaw_node_path);
	ClassDB::bind_method(D_METHOD("get_yaw_node_path"), &CellCharacterBody::get_yaw_node_path);
	ClassDB::bind_method(D_METHOD("set_pitch_node_path", "path"), &CellCharacterBody::set_pitch_node_path);
	ClassDB::bind_method(D_METHOD("get_pitch_node_path"), &CellCharacterBody::get_pitch_node_path);
	ClassDB::bind_method(D_METHOD("set_view_sensitivity", "sensitivity"), &CellCharacterBody::set_view_sensitivity);
	ClassDB::bind_method(D_METHOD("get_view_sensitivity"), &CellCharacterBody::get_view_sensitivity);
	ClassDB::bind_method(D_METHOD("set_view_smoothing", "rate"), &CellCharacterBody::set_view_smoothing);
	ClassDB::bind_method(D_METHOD("get_view_smoothing"), &CellCharacterBody::get_view_smoothing);
	ClassDB::bind_method(D_METHOD("set_view_pitch_min", "degrees"), &CellCharacterBody::set_view_pitch_min);
	ClassDB::bind_method(D_METHOD("get_view_pitch_min"), &CellCharacterBody::get_view_pitch_min);
	ClassDB::bind_method(D_METHOD("set_view_pitch_max", "degrees"), &CellCharacterBody::set_view_pitch_max);
	ClassDB::bind_method(D_METHOD("get_view_pitch_max"), &CellCharacterBody::get_view_pitch_max);

	ADD_GROUP("Movement", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "movement_enabled"), "set_movement_enabled", "is_movement_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "max_speed", PROPERTY_HINT_RANGE, "0,50,0.01,or_greater"), "set_max_speed", "get_max_speed");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "acceleration", PROPERTY_HINT_RANGE, "0,60,0.01,or_greater"), "set_acceleration", "get_acceleration");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "deceleration", PROPERTY_HINT_RANGE, "0,60,0.01,or_greater"), "set_deceleration", "get_deceleration");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "air_control", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_air_control", "get_air_control");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "stop_speed", PROPERTY_HINT_RANGE, "0,1,0.001,or_greater"), "set_stop_speed", "get_stop_speed");

	ADD_GROUP("Gravity", "");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "gravity", PROPERTY_HINT_RANGE, "0,60,0.01,or_greater"), "set_gravity", "get_gravity");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "max_fall_speed", PROPERTY_HINT_RANGE, "0,200,0.1,or_greater"), "set_max_fall_speed", "get_max_fall_speed");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "jump_speed", PROPERTY_HINT_RANGE, "0,30,0.01,or_greater"), "set_jump_speed", "get_jump_speed");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "floor_max_angle", PROPERTY_HINT_RANGE, "0,89.9,0.1"), "set_floor_max_angle", "get_floor_max_angle");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "snap_length", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_snap_length", "get_snap_length");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "stop_on_slope"), "set_stop_on_slope", "is_stop_on_slope_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_slides", PROPERTY_HINT_RANGE, "1,16,1"), "set_max_slides", "get_max_slides");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "infinite_inertia"), "set_infinite_inertia", "is_infinite_inertia_enabled");

	ADD_GROUP("View", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "view_enabled"), "set_view_enabled", "is_view_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "yaw_node", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Spatial"), "set_yaw_node_path", "get_yaw_node_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "pitch_node", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Spatial"), "set_pitch_node_path", "get_pitch_node_path");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "view_sensitivity", PROPERTY_HINT_RANGE, "0,2,0.001,or_greater"), "set_view_sensitivity", "get_view_sensitivity");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "view_smoothing", PROPERTY_HINT_RANGE, "0,60,0.1,or_greater"), "set_view_smoothing", "get_view_smoothing");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "view_pitch_min", PROPERTY_HINT_RANGE, "-90,0,0.1"), "set_view_pitch_min", "get_view_pitch_min");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "view_pitch_max", PROPERTY_HINT_RANGE, "0,90,0.1"), "set_view_pitch_max", "get_view_pitch_max");

	// Runtime-only state: reachable as properties from script (so
	// `player.move_input = ...` keeps working), never saved, never in the
	// inspector.
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "move_input", PROPERTY_HINT_NONE, "", 0), "set_move_input", "get_move_input");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "view_input", PROPERTY_HINT_NONE, "", 0), "set_view_input", "get_view_input");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "velocity", PROPERTY_HINT_NONE, "", 0), "set_velocity", "get_velocity");

	ADD_SIGNAL(MethodInfo("jumped"));
}
