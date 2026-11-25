/**************************************************************************/
/*  tuna_edit_camera.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             TUNA ENGINE                                */
/**************************************************************************/

#include "tuna_edit_camera.h"

#include "core/os/input.h"
#include "core/os/input_event.h"
#include "core/os/keyboard.h"

void TunaEditCamera::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_input", "event"), &TunaEditCamera::_input);
}

void TunaEditCamera::_set_looking(bool p_looking) {
	if (looking == p_looking) {
		return;
	}
	looking = p_looking;

	Input *input = Input::get_singleton();
	if (input) {
		input->set_mouse_mode(looking ? Input::MOUSE_MODE_CAPTURED : Input::MOUSE_MODE_VISIBLE);
	}
}

void TunaEditCamera::_input(const Ref<InputEvent> &p_event) {
	// Escape always gives the pointer back. Embedded in another application's window, a stuck
	// capture means the whole editor is unusable, so there has to be a way out that does not
	// depend on the engine seeing a button release.
	Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() && key->get_scancode() == KEY_ESCAPE) {
		_set_looking(false);
		return;
	}

	// Wheel sets the fly speed. Multiplicative so a notch means the same thing whether you are
	// inching around a prop or crossing a terrain, and handled before the look check so it
	// works whether or not the right button is down.
	Ref<InputEventMouseButton> button = p_event;
	if (button.is_valid() && button->get_button_index() == BUTTON_RIGHT) {
		// Driven by the press and release events themselves. Polling the button state once a
		// frame misses a release that lands while the window is not focused, which left the
		// pointer captured with no way to get it back.
		_set_looking(button->is_pressed());
		return;
	}

	if (button.is_valid() && button->is_pressed()) {
		const int index = button->get_button_index();
		if (index == BUTTON_WHEEL_UP || index == BUTTON_WHEEL_DOWN) {
			const float step = (index == BUTTON_WHEEL_UP) ? 1.2f : (1.0f / 1.2f);
			move_speed = CLAMP(move_speed * step, speed_min, speed_max);
			return;
		}
	}

	if (!looking) {
		return;
	}

	Ref<InputEventMouseMotion> motion = p_event;
	if (motion.is_null()) {
		return;
	}

	// get_relative() is the exact movement this event carries. Input::get_last_mouse_speed(),
	// which this used to poll, is a smoothed velocity in pixels per second: it is far larger
	// than a per-frame delta, and it decays instead of reaching zero, so the view kept turning
	// after the mouse stopped.
	const Vector2 relative = motion->get_relative();
	yaw -= relative.x * look_sensitivity;
	pitch -= relative.y * look_sensitivity;

	const float limit = Math_PI * 0.5f - 0.01f;
	pitch = CLAMP(pitch, -limit, limit);
}

void TunaEditCamera::set_view(const Transform &p_transform) {
	set_transform(p_transform);

	// Recover the Euler angles the fly controller works in. Roll is dropped on purpose: the
	// camera is rebuilt from yaw and pitch every frame, so keeping it would be a lie.
	Vector3 euler = p_transform.basis.get_euler();
	yaw = euler.y;
	pitch = euler.x;
}

void TunaEditCamera::set_speed_limits(float p_min, float p_max) {
	speed_min = MAX(p_min, 0.001f);
	speed_max = MAX(p_max, speed_min);
	move_speed = CLAMP(move_speed, speed_min, speed_max);
}

void TunaEditCamera::frame(const AABB &p_bounds) {
	const Vector3 center = p_bounds.position + p_bounds.size * 0.5f;
	const float radius = MAX(p_bounds.size.length() * 0.5f, 1.0f);

	// Stand off far enough to take the scene in, but do not retreat to orbit for a world that
	// streams for kilometres -- past a point the useful view is "somewhere inside it".
	const float standoff = MIN(radius * 1.4f, 400.0f);

	const Vector3 offset = Vector3(0.55f, 0.45f, 0.55f).normalized() * standoff;
	Transform view;
	view.origin = center + offset;
	view.basis = Transform().looking_at(-offset, Vector3(0, 1, 0)).basis;
	set_view(view);

	set_znear(MAX(standoff * 0.0005f, 0.05f));
	set_zfar(MAX(radius * 8.0f, 4096.0f));

	// Flying across a large world at 8 m/s is unusable; scale to what is being looked at.
	move_speed = CLAMP(standoff * 0.35f, MAX(speed_min, 4.0f), MIN(speed_max, 400.0f));
}

void TunaEditCamera::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process(true);
			set_process_input(true);
			set_current(true);

			// SceneTree finds _input through ClassDB by name. If the class was not registered,
			// mouse look silently does nothing -- say so rather than leaving it a mystery.
			if (!ClassDB::get_method(get_class_name(), "_input")) {
				ERR_PRINT("TunaEditCamera: _input is not registered with ClassDB; mouse look will not work.");
			}
		} break;

		case NOTIFICATION_EXIT_TREE:
		case NOTIFICATION_WM_FOCUS_OUT: {
			// Never hold the pointer once this window is not the one being used.
			_set_looking(false);
		} break;

		case NOTIFICATION_PROCESS: {
			Input *input = Input::get_singleton();
			if (!input) {
				return;
			}

			const float delta = get_process_delta_time();

			// Movement is only live while looking, the way every 3D editor does it. That is
			// not just convention here: it leaves W / E / R free for the gizmo's move,
			// rotate and scale modes instead of driving the camera.
			Vector3 direction;
			if (!looking) {
				Basis idle = Basis(Vector3(0, 1, 0), yaw) * Basis(Vector3(1, 0, 0), pitch);
				Transform held = get_transform();
				held.basis = idle;
				set_transform(held);
				return;
			}

			if (input->is_key_pressed(KEY_W)) {
				direction.z -= 1.0f;
			}
			if (input->is_key_pressed(KEY_S)) {
				direction.z += 1.0f;
			}
			if (input->is_key_pressed(KEY_A)) {
				direction.x -= 1.0f;
			}
			if (input->is_key_pressed(KEY_D)) {
				direction.x += 1.0f;
			}
			if (input->is_key_pressed(KEY_E)) {
				direction.y += 1.0f;
			}
			if (input->is_key_pressed(KEY_Q)) {
				direction.y -= 1.0f;
			}

			Basis basis = Basis(Vector3(0, 1, 0), yaw) * Basis(Vector3(1, 0, 0), pitch);

			Transform transform = get_transform();
			transform.basis = basis;

			if (direction != Vector3()) {
				float speed = move_speed;
				if (input->is_key_pressed(KEY_SHIFT)) {
					speed *= 4.0f;
				}
				if (input->is_key_pressed(KEY_ALT)) {
					speed *= 0.25f;
				}
				transform.origin += basis.xform(direction.normalized()) * speed * delta;
			}

			set_transform(transform);
		} break;
	}
}

TunaEditCamera::TunaEditCamera() {
	yaw = 0.0f;
	pitch = 0.0f;
	looking = false;
	move_speed = 8.0f;
	speed_min = 0.25f;
	speed_max = 4000.0f;
	// Radians per pixel of mouse movement, about 0.14 degrees per pixel.
	look_sensitivity = 0.0025f;
}
