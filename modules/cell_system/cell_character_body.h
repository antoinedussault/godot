#ifndef CELL_CHARACTER_BODY_H
#define CELL_CHARACTER_BODY_H

#include "cell_player_body.h"

class Spatial;

// A CellPlayerBody (so, a KinematicBody) that moves itself.
//
// It owns the three things every first-person body ends up re-implementing in
// script: acceleration toward an input direction, gravity (with jump and floor
// snapping), and the view (yaw + clamped pitch, optionally smoothed).
//
// It does NOT read the keyboard or the mouse. Game code decides what the input
// is and hands it over - `move_input` every frame, `view_input` (or
// add_view_input) on mouse motion, jump() on a press - and tunes `max_speed`
// for walk / run / sprint. Everything that is game feel rather than motion
// (head bob, FOV, footsteps, crouch, step-up) stays in the script on top.
//
// The step runs on NOTIFICATION_PHYSICS_PROCESS, which arrives AFTER the
// script's own _physics_process, so whatever the script sets this tick is what
// the body moves with this tick. A script that needs a different order turns
// `movement_enabled` / `view_enabled` off and calls process_movement() /
// process_view() itself.
class CellCharacterBody : public CellPlayerBody {
	GDCLASS(CellCharacterBody, CellPlayerBody);

private:
	// --- movement ---
	bool movement_enabled = true;
	float max_speed = 4.26f;
	float acceleration = 9.0f;
	float deceleration = 12.0f;
	float air_control = 1.0f;
	float stop_speed = 0.02f;

	// --- gravity / floor ---
	float gravity = 9.8f;
	float max_fall_speed = 0.0f; // 0 = no limit
	float jump_speed = 2.1f;
	float floor_max_angle = 55.0f; // degrees
	float snap_length = 0.8f;
	bool stop_on_slope = true;
	int max_slides = 4;
	bool infinite_inertia = false;

	// --- view ---
	bool view_enabled = true;
	NodePath yaw_node_path; // empty = the body itself turns
	NodePath pitch_node_path; // empty = no pitch
	float view_sensitivity = 0.05f; // degrees per unit of view_input
	float view_smoothing = 22.0f; // 0 = raw
	float view_pitch_min = -82.0f; // degrees
	float view_pitch_max = 89.0f;

	// --- runtime state ---
	Vector2 _move_input;
	Vector2 _view_input;
	Vector2 _view_velocity;
	Vector3 _velocity;
	bool _jump_requested = false;
	bool _rising_from_jump = false;

	Spatial *_get_yaw_node();
	Spatial *_get_pitch_node();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	// One step of each half. Called automatically every physics frame while the
	// matching *_enabled flag is on; callable by hand when it is off.
	void process_movement(float p_delta);
	void process_view(float p_delta);

	// Input. x = strafe (right positive), y = forward/back (forward NEGATIVE,
	// like a -Z forward). Longer than 1 is treated as 1; shorter scales the
	// speed, so an analog stick walks.
	void set_move_input(const Vector2 &p_input) { _move_input = p_input; }
	Vector2 get_move_input() const { return _move_input; }
	// Pending view motion, consumed by the next view step. Setting it REPLACES
	// what is pending; add_view_input accumulates, which is what several mouse
	// events inside one physics frame need.
	void set_view_input(const Vector2 &p_input) { _view_input = p_input; }
	Vector2 get_view_input() const { return _view_input; }
	void add_view_input(const Vector2 &p_input) { _view_input += p_input; }

	// Jumps on the next movement step if the body is on the floor by then.
	void jump();

	// World-space direction the current move_input points at (unit length or
	// zero), on the horizontal plane, relative to where the view is facing.
	Vector3 get_move_direction();

	void set_velocity(const Vector3 &p_velocity);
	Vector3 get_velocity() const { return _velocity; }
	Vector2 get_horizontal_velocity() const { return Vector2(_velocity.x, _velocity.z); }

	// Turn the view by this many degrees (x = yaw, y = pitch), pitch clamped.
	void rotate_view(const Vector2 &p_degrees);
	void set_view_yaw(float p_degrees);
	float get_view_yaw();
	void set_view_pitch(float p_degrees);
	float get_view_pitch();

	void set_movement_enabled(bool p_enabled);
	bool is_movement_enabled() const { return movement_enabled; }
	void set_max_speed(float p_speed) { max_speed = MAX(0.0f, p_speed); }
	float get_max_speed() const { return max_speed; }
	void set_acceleration(float p_rate) { acceleration = MAX(0.0f, p_rate); }
	float get_acceleration() const { return acceleration; }
	void set_deceleration(float p_rate) { deceleration = MAX(0.0f, p_rate); }
	float get_deceleration() const { return deceleration; }
	void set_air_control(float p_amount) { air_control = CLAMP(p_amount, 0.0f, 1.0f); }
	float get_air_control() const { return air_control; }
	void set_stop_speed(float p_speed) { stop_speed = MAX(0.0f, p_speed); }
	float get_stop_speed() const { return stop_speed; }

	void set_gravity(float p_gravity) { gravity = p_gravity; }
	float get_gravity() const { return gravity; }
	void set_max_fall_speed(float p_speed) { max_fall_speed = MAX(0.0f, p_speed); }
	float get_max_fall_speed() const { return max_fall_speed; }
	void set_jump_speed(float p_speed) { jump_speed = MAX(0.0f, p_speed); }
	float get_jump_speed() const { return jump_speed; }
	void set_floor_max_angle(float p_degrees) { floor_max_angle = CLAMP(p_degrees, 0.0f, 89.9f); }
	float get_floor_max_angle() const { return floor_max_angle; }
	void set_snap_length(float p_length) { snap_length = MAX(0.0f, p_length); }
	float get_snap_length() const { return snap_length; }
	void set_stop_on_slope(bool p_enabled) { stop_on_slope = p_enabled; }
	bool is_stop_on_slope_enabled() const { return stop_on_slope; }
	void set_max_slides(int p_slides) { max_slides = MAX(1, p_slides); }
	int get_max_slides() const { return max_slides; }
	void set_infinite_inertia(bool p_enabled) { infinite_inertia = p_enabled; }
	bool is_infinite_inertia_enabled() const { return infinite_inertia; }

	void set_view_enabled(bool p_enabled);
	bool is_view_enabled() const { return view_enabled; }
	void set_yaw_node_path(const NodePath &p_path) { yaw_node_path = p_path; }
	NodePath get_yaw_node_path() const { return yaw_node_path; }
	void set_pitch_node_path(const NodePath &p_path) { pitch_node_path = p_path; }
	NodePath get_pitch_node_path() const { return pitch_node_path; }
	void set_view_sensitivity(float p_sensitivity) { view_sensitivity = p_sensitivity; }
	float get_view_sensitivity() const { return view_sensitivity; }
	void set_view_smoothing(float p_rate) { view_smoothing = MAX(0.0f, p_rate); }
	float get_view_smoothing() const { return view_smoothing; }
	void set_view_pitch_min(float p_degrees) { view_pitch_min = p_degrees; }
	float get_view_pitch_min() const { return view_pitch_min; }
	void set_view_pitch_max(float p_degrees) { view_pitch_max = p_degrees; }
	float get_view_pitch_max() const { return view_pitch_max; }

	CellCharacterBody();
};

#endif // CELL_CHARACTER_BODY_H
