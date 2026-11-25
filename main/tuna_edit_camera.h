/**************************************************************************/
/*  tuna_edit_camera.h                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             TUNA ENGINE                                */
/**************************************************************************/

#ifndef TUNA_EDIT_CAMERA_H
#define TUNA_EDIT_CAMERA_H

#include "core/os/input_event.h"
#include "scene/3d/camera.h"

// Free-fly camera for --tuna-edit.
//
// The editor loads a scene with scripting disabled, so the scene's own camera rig is inert and
// there is nothing to fly the view around with. This supplies that: hold the right mouse button
// to look, WASD to move, Q/E for down/up, Shift to move faster.
//
// Input is polled in NOTIFICATION_PROCESS rather than handled through _input, so the node needs
// no script and no ClassDB instancing support -- _notification is an ordinary C++ virtual.
class TunaEditCamera : public Camera {
	GDCLASS(TunaEditCamera, Camera);

	float yaw;
	float pitch;
	bool looking;

	float move_speed;
	float look_sensitivity;
	float speed_min;
	float speed_max;

	// Single place that flips look mode, so the pointer is never captured without a matching
	// release path (button release, focus loss or Escape).
	void _set_looking(bool p_looking);

protected:
	void _notification(int p_what);
	static void _bind_methods();

	// Bound in _bind_methods and reached through Object::call_multilevel -- SceneTree dispatches
	// input by method name, not through a C++ virtual, so this only arrives if the class is
	// registered with ClassDB.
	void _input(const Ref<InputEvent> &p_event);

public:
	// Places the camera and derives yaw/pitch from the transform, so starting from an existing
	// camera in the scene does not snap the view on the first frame.
	void set_view(const Transform &p_transform);

	// Bounds the fly speed the mouse wheel can reach. Call before frame().
	void set_speed_limits(float p_min, float p_max);

	// Points the camera at the scene's bounds and sizes the clip planes and fly speed to it.
	// Godot's Camera defaults to far = 100, which clips a terrain world almost immediately.
	void frame(const AABB &p_bounds);

	TunaEditCamera();
};

#endif // TUNA_EDIT_CAMERA_H
