/**************************************************************************/
/*  tuna_edit_selection.cpp                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             TUNA ENGINE                                */
/**************************************************************************/

#include "tuna_edit_selection.h"

#include "core/object.h"
#include "core/os/input.h"
#include "scene/3d/camera.h"
#include "scene/3d/visual_instance.h"
#include "scene/main/viewport.h"
#include "servers/visual_server.h"

void TunaEditSelection::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_input", "event"), &TunaEditSelection::_input);
}

void TunaEditSelection::_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> button = p_event;
	if (button.is_null() || !button->is_pressed() || button->get_button_index() != BUTTON_LEFT) {
		return;
	}

	// While the right button is held the pointer is captured for look, so the click position
	// is meaningless and the click is part of flying, not selecting.
	if (Input::get_singleton() && Input::get_singleton()->is_mouse_button_pressed(BUTTON_RIGHT)) {
		return;
	}

	Spatial *hit = _pick(button->get_position());
	if (hit) {
		select(hit);
	} else {
		clear_selection();
	}
}

Spatial *TunaEditSelection::_pick(const Point2 &p_screen) const {
	Viewport *viewport = get_viewport();
	if (!viewport) {
		return nullptr;
	}
	Camera *camera = viewport->get_camera();
	if (!camera) {
		return nullptr;
	}

	Ref<World> world = get_world();
	if (world.is_null()) {
		return nullptr;
	}

	const Vector3 from = camera->project_ray_origin(p_screen);
	const Vector3 to = from + camera->project_ray_normal(p_screen) * camera->get_zfar();

	Vector<ObjectID> hits = VisualServer::get_singleton()->instances_cull_ray(from, to, world->get_scenario());

	// Culling returns everything whose bounds the ray crosses, in no order, so pick the nearest
	// entry point along the ray rather than the first returned.
	Spatial *nearest = nullptr;
	float nearest_distance = 0.0f;

	for (int i = 0; i < hits.size(); i++) {
		Object *object = ObjectDB::get_instance(hits[i]);
		VisualInstance *visual = Object::cast_to<VisualInstance>(object);
		if (!visual || !visual->is_visible_in_tree()) {
			continue;
		}

		Vector3 point;
		Vector3 normal;
		if (!visual->get_transformed_aabb().intersects_segment(from, to, &point, &normal)) {
			continue;
		}

		const float distance = from.distance_to(point);
		if (!nearest || distance < nearest_distance) {
			nearest = visual;
			nearest_distance = distance;
		}
	}

	return nearest ? _resolve_item(nearest) : nullptr;
}

Spatial *TunaEditSelection::_resolve_item(Spatial *p_hit) {
	// Walk up to the node parented directly to a Cell: that is the thing placed in the world.
	Spatial *node = p_hit;
	while (node) {
		Node *parent = node->get_parent();
		if (parent && parent->is_class("Cell")) {
			return node;
		}

		Spatial *spatial_parent = Object::cast_to<Spatial>(parent);
		if (!spatial_parent) {
			break;
		}
		node = spatial_parent;
	}

	// Not inside a Cell -- loose geometry, or a scene that is not cell-authored. Select what
	// was actually hit rather than guessing at a root.
	return p_hit;
}

AABB TunaEditSelection::_item_bounds(Node *p_node, bool &r_has) {
	AABB bounds;

	VisualInstance *visual = Object::cast_to<VisualInstance>(p_node);
	if (visual && visual->is_visible_in_tree()) {
		AABB world = visual->get_transformed_aabb();
		if (world.size != Vector3()) {
			bounds = world;
			r_has = true;
		}
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		bool child_has = false;
		AABB child = _item_bounds(p_node->get_child(i), child_has);
		if (!child_has) {
			continue;
		}
		if (r_has) {
			bounds.merge_with(child);
		} else {
			bounds = child;
			r_has = true;
		}
	}

	return bounds;
}

void TunaEditSelection::select(Spatial *p_node) {
	if (!p_node) {
		clear_selection();
		return;
	}

	selected = p_node->get_instance_id();

	bool has_bounds = false;
	AABB bounds = _item_bounds(p_node, has_bounds);
	if (has_bounds) {
		_draw_outline(bounds);
	} else if (overlay_instance.is_valid()) {
		VisualServer::get_singleton()->instance_set_visible(overlay_instance, false);
	}

	// One-way report to the external editor, which reads the engine's stdout. Prefixed so it is
	// picked out of Godot's own logging.
	String cell;
	Node *parent = p_node->get_parent();
	if (parent && parent->is_class("Cell")) {
		cell = parent->get_name();
	}
	print_line("TUNA_SEL:" + String(p_node->get_name()) + "|" + p_node->get_class() + "|" + cell);
}

void TunaEditSelection::clear_selection() {
	selected = 0;
	if (overlay_instance.is_valid()) {
		VisualServer::get_singleton()->instance_set_visible(overlay_instance, false);
	}
	print_line("TUNA_SEL:|||");
}

Spatial *TunaEditSelection::get_selected() const {
	return Object::cast_to<Spatial>(ObjectDB::get_instance(selected));
}

void TunaEditSelection::_ensure_overlay() {
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
	overlay_material->set_flag(SpatialMaterial::FLAG_DISABLE_DEPTH_TEST, true);
	overlay_material->set_feature(SpatialMaterial::FEATURE_TRANSPARENT, true);
	overlay_material->set_render_priority(1);

	overlay_immediate = vs->immediate_create();
	vs->immediate_set_material(overlay_immediate, overlay_material->get_rid());
	overlay_instance = vs->instance_create2(overlay_immediate, world->get_scenario());
	vs->instance_geometry_set_cast_shadows_setting(overlay_instance, VisualServer::SHADOW_CASTING_SETTING_OFF);
	// Geometry is emitted in world space, so the instance itself never moves.
	vs->instance_set_transform(overlay_instance, Transform());
}

void TunaEditSelection::_draw_outline(const AABB &p_bounds) {
	_ensure_overlay();
	if (!overlay_immediate.is_valid()) {
		return;
	}

	VisualServer *vs = VisualServer::get_singleton();
	vs->immediate_clear(overlay_immediate);
	vs->instance_set_visible(overlay_instance, true);

	const Vector3 position = p_bounds.position;
	const Vector3 size = p_bounds.size;

	Vector3 corner[8];
	for (int i = 0; i < 8; i++) {
		corner[i] = position + Vector3(
									(i & 1) ? size.x : 0.0f,
									(i & 2) ? size.y : 0.0f,
									(i & 4) ? size.z : 0.0f);
	}

	// The 12 edges of the box, as corner index pairs.
	static const int edges[24] = {
		0, 1, 2, 3, 4, 5, 6, 7,
		0, 2, 1, 3, 4, 6, 5, 7,
		0, 4, 1, 5, 2, 6, 3, 7
	};

	vs->immediate_begin(overlay_immediate, VisualServer::PRIMITIVE_LINES, RID());
	vs->immediate_color(overlay_immediate, Color(1.0, 0.65, 0.1));
	for (int i = 0; i < 24; i++) {
		vs->immediate_vertex(overlay_immediate, corner[edges[i]]);
	}
	vs->immediate_end(overlay_immediate);
}

void TunaEditSelection::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process_input(true);

			if (!ClassDB::get_method(get_class_name(), "_input")) {
				ERR_PRINT("TunaEditSelection: _input is not registered with ClassDB; picking will not work.");
			}
		} break;

		case NOTIFICATION_EXIT_TREE: {
			VisualServer *vs = VisualServer::get_singleton();
			if (vs && overlay_instance.is_valid()) {
				vs->instance_set_visible(overlay_instance, false);
			}
		} break;
	}
}

TunaEditSelection::TunaEditSelection() {
	selected = 0;
}

TunaEditSelection::~TunaEditSelection() {
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
