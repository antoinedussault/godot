/**************************************************************************/
/*  tuna_edit_gizmo.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             TUNA ENGINE                                */
/**************************************************************************/

#include "tuna_edit_gizmo.h"

#include "core/math/math_funcs.h"
#include "core/os/input.h"
#include "core/os/keyboard.h"
#include "core/print_string.h"
#include "scene/3d/camera.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "servers/visual_server.h"

// Handle length and grab tolerance, both in gizmo-scale units.
static const float AXIS_LENGTH = 1.0f;
static const float ROTATE_RADIUS = 0.85f;
static const float GRAB_TOLERANCE = 0.12f;

static const Color AXIS_COLOR[3] = {
	Color(0.94f, 0.28f, 0.33f), // X
	Color(0.47f, 0.84f, 0.30f), // Y
	Color(0.30f, 0.56f, 0.94f), // Z
};
static const Color ACTIVE_COLOR = Color(1.0f, 0.85f, 0.25f);

void TunaEditGizmo::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_input", "event"), &TunaEditGizmo::_input);
}

Spatial *TunaEditGizmo::_target() const {
	return selection ? selection->get_selected() : nullptr;
}

Camera *TunaEditGizmo::_camera() const {
	Viewport *viewport = get_viewport();
	return viewport ? viewport->get_camera() : nullptr;
}

float TunaEditGizmo::_scale_for(const Vector3 &p_origin) const {
	Camera *camera = _camera();
	if (!camera) {
		return 1.0f;
	}

	// Distance along the view direction, not straight-line distance: it keeps the gizmo a
	// steady size as it moves across the screen instead of swelling toward the edges.
	const Transform view = camera->get_global_transform();
	const float depth = MAX((p_origin - view.origin).dot(-view.basis.get_axis(2)), 0.01f);
	return depth * Math::tan(Math::deg2rad(camera->get_fov() * 0.5f)) * 0.18f;
}

Vector3 TunaEditGizmo::_axis_vector(Axis p_axis) const {
	switch (p_axis) {
		case AXIS_X: return Vector3(1, 0, 0);
		case AXIS_Y: return Vector3(0, 1, 0);
		case AXIS_Z: return Vector3(0, 0, 1);
		default: return Vector3();
	}
}

// ---------------------------------------------------------------------------------------
// Picking
// ---------------------------------------------------------------------------------------

// Shortest distance between a ray and a segment, used to decide which handle is under the
// cursor. Both handles and tolerance are in gizmo scale, so this stays correct at any distance.
static float ray_segment_distance(const Vector3 &p_ro, const Vector3 &p_rd,
		const Vector3 &p_a, const Vector3 &p_b) {
	const Vector3 u = p_rd;
	const Vector3 v = p_b - p_a;
	const Vector3 w = p_ro - p_a;

	const float a = u.dot(u);
	const float b = u.dot(v);
	const float c = v.dot(v);
	const float d = u.dot(w);
	const float e = v.dot(w);
	const float denom = a * c - b * b;

	float sc, tc;
	if (denom < CMP_EPSILON) {
		sc = 0.0f;
		tc = (b > c ? d / b : e / c);
	} else {
		sc = (b * e - c * d) / denom;
		tc = (a * e - b * d) / denom;
	}

	sc = MAX(sc, 0.0f);          // in front of the camera only
	tc = CLAMP(tc, 0.0f, 1.0f);  // clamped to the segment

	const Vector3 closest_ray = p_ro + u * sc;
	const Vector3 closest_seg = p_a + v * tc;
	return closest_ray.distance_to(closest_seg);
}

TunaEditGizmo::Axis TunaEditGizmo::_pick_axis(const Point2 &p_screen, float p_scale,
		const Transform &p_transform) const {
	Camera *camera = _camera();
	if (!camera) {
		return AXIS_NONE;
	}

	const Vector3 from = camera->project_ray_origin(p_screen);
	const Vector3 dir = camera->project_ray_normal(p_screen);
	const Vector3 origin = p_transform.origin;
	const float tolerance = GRAB_TOLERANCE * p_scale;

	Axis best = AXIS_NONE;
	float best_distance = tolerance;

	for (int i = 0; i < 3; i++) {
		const Vector3 axis = _axis_vector((Axis)i);

		if (mode == MODE_ROTATE) {
			// Ring: intersect the plane the ring lies in, then compare against its radius.
			const Plane plane(origin, axis);
			Vector3 hit;
			if (!plane.intersects_ray(from, dir, &hit)) {
				continue;
			}
			const float radial = Math::abs(hit.distance_to(origin) - ROTATE_RADIUS * p_scale);
			if (radial < best_distance) {
				best_distance = radial;
				best = (Axis)i;
			}
		} else {
			const Vector3 tip = origin + axis * (AXIS_LENGTH * p_scale);
			const float distance = ray_segment_distance(from, dir, origin, tip);
			if (distance < best_distance) {
				best_distance = distance;
				best = (Axis)i;
			}
		}
	}

	return best;
}

// ---------------------------------------------------------------------------------------
// Dragging
// ---------------------------------------------------------------------------------------

// The scalar this drag is measured in: distance along the axis for move and scale, angle
// around it for rotate.
bool TunaEditGizmo::_axis_value(const Point2 &p_screen, Axis p_axis, const Transform &p_transform,
		float p_scale, float &r_value) const {
	Camera *camera = _camera();
	if (!camera || p_axis == AXIS_NONE) {
		return false;
	}

	const Vector3 from = camera->project_ray_origin(p_screen);
	const Vector3 dir = camera->project_ray_normal(p_screen);
	const Vector3 origin = p_transform.origin;
	const Vector3 axis = _axis_vector(p_axis);

	if (mode == MODE_ROTATE) {
		const Plane plane(origin, axis);
		Vector3 hit;
		if (!plane.intersects_ray(from, dir, &hit)) {
			return false;
		}

		// Two vectors spanning the ring's plane, so the hit can be turned into an angle.
		Vector3 reference = (p_axis == AXIS_Y) ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
		reference = (reference - axis * reference.dot(axis)).normalized();
		const Vector3 bitangent = axis.cross(reference);

		const Vector3 radial = hit - origin;
		r_value = Math::atan2(radial.dot(bitangent), radial.dot(reference));
		return true;
	}

	// Move and scale: the point on the axis line closest to the cursor ray.
	const Vector3 w = from - origin;
	const float a = dir.dot(dir);
	const float b = dir.dot(axis);
	const float c = axis.dot(axis);
	const float d = dir.dot(w);
	const float e = axis.dot(w);
	const float denom = a * c - b * b;
	if (Math::abs(denom) < CMP_EPSILON) {
		return false; // looking straight down the axis; nothing to measure
	}

	r_value = (a * e - b * d) / denom;
	return true;
}

void TunaEditGizmo::_begin_drag(Axis p_axis, const Point2 &p_screen) {
	Spatial *target = _target();
	if (!target) {
		return;
	}

	drag_start_transform = target->get_global_transform();
	gizmo_scale = _scale_for(drag_start_transform.origin);

	if (!_axis_value(p_screen, p_axis, drag_start_transform, gizmo_scale, drag_start_value)) {
		return;
	}

	drag_axis = p_axis;
	dragging = true;
}

void TunaEditGizmo::_update_drag(const Point2 &p_screen) {
	Spatial *target = _target();
	if (!target || drag_axis == AXIS_NONE) {
		return;
	}

	float value;
	if (!_axis_value(p_screen, drag_axis, drag_start_transform, gizmo_scale, value)) {
		return;
	}

	const Vector3 axis = _axis_vector(drag_axis);
	Transform result = drag_start_transform;

	switch (mode) {
		case MODE_MOVE: {
			result.origin = drag_start_transform.origin + axis * (value - drag_start_value);
		} break;

		case MODE_ROTATE: {
			float delta = value - drag_start_value;
			// Keep the shortest way round, or crossing +/-PI snaps the object half a turn.
			while (delta > Math_PI) {
				delta -= Math_TAU;
			}
			while (delta < -Math_PI) {
				delta += Math_TAU;
			}
			result.basis = Basis(axis, -delta) * drag_start_transform.basis;
		} break;

		case MODE_SCALE: {
			const float reference = MAX(AXIS_LENGTH * gizmo_scale, CMP_EPSILON);
			float factor = 1.0f + (value - drag_start_value) / reference;
			factor = MAX(factor, 0.01f); // never mirror or collapse through zero

			Vector3 scale(1, 1, 1);
			scale[(int)drag_axis] = factor;

			Basis local = drag_start_transform.basis;
			local.scale_local(scale);
			result.basis = local;
		} break;
	}

	target->set_global_transform(result);
}

void TunaEditGizmo::_end_drag() {
	if (!dragging) {
		return;
	}

	dragging = false;
	drag_axis = AXIS_NONE;

	Spatial *target = _target();
	if (target) {
		const Transform t = target->get_global_transform();
		const Vector3 euler = t.basis.get_euler();
		const Vector3 scale = t.basis.get_scale();
		print_line("TUNA_XFORM:" + String(target->get_name())
				+ "|" + rtos(t.origin.x) + "," + rtos(t.origin.y) + "," + rtos(t.origin.z)
				+ "|" + rtos(Math::rad2deg(euler.x)) + "," + rtos(Math::rad2deg(euler.y)) + "," + rtos(Math::rad2deg(euler.z))
				+ "|" + rtos(scale.x) + "," + rtos(scale.y) + "," + rtos(scale.z));
	}
}

// ---------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------

void TunaEditGizmo::_input(const Ref<InputEvent> &p_event) {
	// Mode keys. These are free because the camera only moves while the right button is held.
	Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
		switch (key->get_scancode()) {
			case KEY_W: set_mode(MODE_MOVE); return;
			case KEY_E: set_mode(MODE_ROTATE); return;
			case KEY_R: set_mode(MODE_SCALE); return;
			default: break;
		}
	}

	Spatial *target = _target();
	if (!target) {
		return;
	}

	Ref<InputEventMouseButton> button = p_event;
	if (button.is_valid() && button->get_button_index() == BUTTON_LEFT) {
		if (button->is_pressed()) {
			const Transform transform = target->get_global_transform();
			const float scale = _scale_for(transform.origin);
			const Axis axis = _pick_axis(button->get_position(), scale, transform);

			if (axis != AXIS_NONE) {
				_begin_drag(axis, button->get_position());
				// Claim the click so the selection node does not re-pick behind the gizmo.
				if (get_tree()) {
					get_tree()->set_input_as_handled();
				}
			}
		} else if (dragging) {
			_end_drag();
			if (get_tree()) {
				get_tree()->set_input_as_handled();
			}
		}
		return;
	}

	Ref<InputEventMouseMotion> motion = p_event;
	if (motion.is_valid()) {
		if (dragging) {
			_update_drag(motion->get_position());
			if (get_tree()) {
				get_tree()->set_input_as_handled();
			}
		} else {
			const Transform transform = target->get_global_transform();
			hover_axis = _pick_axis(motion->get_position(), _scale_for(transform.origin), transform);
		}
	}
}

void TunaEditGizmo::set_mode(Mode p_mode) {
	if (mode == p_mode) {
		return;
	}
	mode = p_mode;
	hover_axis = AXIS_NONE;

	const char *names[3] = { "move", "rotate", "scale" };
	print_line(String("TUNA_ACK:gizmo ") + names[(int)mode]);
}

// ---------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------

void TunaEditGizmo::_ensure_overlay() {
	if (overlay_immediate.is_valid()) {
		return;
	}

	Ref<World> world = get_world();
	if (world.is_null()) {
		return;
	}

	VisualServer *vs = VisualServer::get_singleton();

	overlay_material.instance();
	overlay_material->set_flag(SpatialMaterial::FLAG_UNSHADED, true);
	overlay_material->set_flag(SpatialMaterial::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	// Handles have to be reachable even when the object encloses them.
	overlay_material->set_flag(SpatialMaterial::FLAG_DISABLE_DEPTH_TEST, true);
	overlay_material->set_feature(SpatialMaterial::FEATURE_TRANSPARENT, true);
	overlay_material->set_render_priority(2);

	overlay_immediate = vs->immediate_create();
	vs->immediate_set_material(overlay_immediate, overlay_material->get_rid());
	overlay_instance = vs->instance_create2(overlay_immediate, world->get_scenario());
	vs->instance_geometry_set_cast_shadows_setting(overlay_instance, VisualServer::SHADOW_CASTING_SETTING_OFF);
	vs->instance_set_transform(overlay_instance, Transform());
}

void TunaEditGizmo::_hide() {
	if (overlay_instance.is_valid()) {
		VisualServer::get_singleton()->instance_set_visible(overlay_instance, false);
	}
}

void TunaEditGizmo::_redraw() {
	Spatial *target = _target();
	if (!target) {
		_hide();
		return;
	}

	_ensure_overlay();
	if (!overlay_immediate.is_valid()) {
		return;
	}

	const Transform transform = target->get_global_transform();
	const Vector3 origin = transform.origin;
	const float scale = _scale_for(origin);

	VisualServer *vs = VisualServer::get_singleton();
	vs->immediate_clear(overlay_immediate);
	vs->instance_set_visible(overlay_instance, true);
	vs->immediate_begin(overlay_immediate, VisualServer::PRIMITIVE_LINES, RID());

	for (int i = 0; i < 3; i++) {
		const Axis axis_id = (Axis)i;
		const Vector3 axis = _axis_vector(axis_id);
		const bool lit = (dragging ? drag_axis : hover_axis) == axis_id;
		const Color color = lit ? ACTIVE_COLOR : AXIS_COLOR[i];

		vs->immediate_color(overlay_immediate, color);

		if (mode == MODE_ROTATE) {
			// A ring in the plane the axis is normal to.
			Vector3 reference = (axis_id == AXIS_Y) ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
			reference = (reference - axis * reference.dot(axis)).normalized();
			const Vector3 bitangent = axis.cross(reference);
			const float radius = ROTATE_RADIUS * scale;

			const int segments = 48;
			for (int s = 0; s < segments; s++) {
				const float a0 = Math_TAU * float(s) / float(segments);
				const float a1 = Math_TAU * float(s + 1) / float(segments);
				vs->immediate_vertex(overlay_immediate,
						origin + (reference * Math::cos(a0) + bitangent * Math::sin(a0)) * radius);
				vs->immediate_vertex(overlay_immediate,
						origin + (reference * Math::cos(a1) + bitangent * Math::sin(a1)) * radius);
			}
			continue;
		}

		const Vector3 tip = origin + axis * (AXIS_LENGTH * scale);
		vs->immediate_vertex(overlay_immediate, origin);
		vs->immediate_vertex(overlay_immediate, tip);

		// Two vectors across the axis, to sketch the head at the tip.
		Vector3 side = (axis_id == AXIS_Y) ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
		side = (side - axis * side.dot(axis)).normalized();
		const Vector3 other = axis.cross(side);

		if (mode == MODE_MOVE) {
			// Arrowhead: four lines back from the tip.
			const float head = 0.16f * scale;
			const Vector3 base = tip - axis * head;
			vs->immediate_vertex(overlay_immediate, tip);
			vs->immediate_vertex(overlay_immediate, base + side * head * 0.5f);
			vs->immediate_vertex(overlay_immediate, tip);
			vs->immediate_vertex(overlay_immediate, base - side * head * 0.5f);
			vs->immediate_vertex(overlay_immediate, tip);
			vs->immediate_vertex(overlay_immediate, base + other * head * 0.5f);
			vs->immediate_vertex(overlay_immediate, tip);
			vs->immediate_vertex(overlay_immediate, base - other * head * 0.5f);
		} else {
			// Scale: a small box outline at the tip.
			const float box = 0.07f * scale;
			const Vector3 corners[4] = {
				tip + side * box + other * box,
				tip + side * box - other * box,
				tip - side * box - other * box,
				tip - side * box + other * box,
			};
			for (int c = 0; c < 4; c++) {
				vs->immediate_vertex(overlay_immediate, corners[c]);
				vs->immediate_vertex(overlay_immediate, corners[(c + 1) % 4]);
			}
		}
	}

	vs->immediate_end(overlay_immediate);
}

void TunaEditGizmo::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process(true);
			set_process_input(true);

			if (!ClassDB::get_method(get_class_name(), "_input")) {
				ERR_PRINT("TunaEditGizmo: _input is not registered with ClassDB; handles will not respond.");
			}
		} break;

		case NOTIFICATION_EXIT_TREE: {
			_hide();
		} break;

		case NOTIFICATION_PROCESS: {
			_redraw();
		} break;
	}
}

TunaEditGizmo::TunaEditGizmo() {
	selection = nullptr;
	mode = MODE_MOVE;
	hover_axis = AXIS_NONE;
	drag_axis = AXIS_NONE;
	dragging = false;
	drag_start_value = 0.0f;
	gizmo_scale = 1.0f;
}

TunaEditGizmo::~TunaEditGizmo() {
	VisualServer *vs = VisualServer::get_singleton();
	if (!vs) {
		return;
	}
	if (overlay_instance.is_valid()) {
		vs->free(overlay_instance);
		overlay_instance = RID();
	}
	if (overlay_immediate.is_valid()) {
		vs->free(overlay_immediate);
		overlay_immediate = RID();
	}
}
