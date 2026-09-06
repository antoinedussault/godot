#include "terrain_path_editor_plugin.h"
#ifdef TOOLS_ENABLED

#include "cell_terrain_manager.h"
#include "core/math/geometry.h"
#include "core/undo_redo.h"
#include "editor/editor_node.h"
#include "editor/editor_spin_slider.h"
#include "editor/plugins/spatial_editor_plugin.h"
#include "editor_ui_helpers.h"
#include "scene/3d/camera.h"
#include "scene/gui/base_button.h" // ButtonGroup
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/resources/world.h"
#include "servers/physics_server.h"
#include "terrain_path.h"
#include "terrain_path_gizmo.h"
#include "terrain_path_point.h"
#include "terrain_path_point_gizmo.h"

// --- Tiny procedural tool-mode icons (see terrain_editor_plugin.cpp for the
// same idea — no art pipeline in this module, so these are drawn in code). ---
static Ref<ImageTexture> _icon_select() {
	Ref<Image> img = cs_icon_new(16);
	img->lock();
	cs_icon_line(img, Vector2(2, 2), Vector2(5, 2), 1.6f, Color(1, 1, 1));
	cs_icon_line(img, Vector2(2, 2), Vector2(2, 5), 1.6f, Color(1, 1, 1));
	cs_icon_line(img, Vector2(14, 2), Vector2(11, 2), 1.6f, Color(1, 1, 1));
	cs_icon_line(img, Vector2(14, 2), Vector2(14, 5), 1.6f, Color(1, 1, 1));
	cs_icon_line(img, Vector2(2, 14), Vector2(5, 14), 1.6f, Color(1, 1, 1));
	cs_icon_line(img, Vector2(2, 14), Vector2(2, 11), 1.6f, Color(1, 1, 1));
	cs_icon_line(img, Vector2(14, 14), Vector2(11, 14), 1.6f, Color(1, 1, 1));
	cs_icon_line(img, Vector2(14, 14), Vector2(14, 11), 1.6f, Color(1, 1, 1));
	cs_icon_fill_circle(img, Vector2(8, 8), 1.6f, Color(1, 1, 1));
	img->unlock();
	return cs_icon_to_texture(img);
}
static Ref<ImageTexture> _icon_add_point() {
	Ref<Image> img = cs_icon_new(16);
	img->lock();
	cs_icon_fill_circle(img, Vector2(6, 6), 3.0f, Color(1, 1, 1));
	cs_icon_line(img, Vector2(11, 8), Vector2(11, 14), 1.8f, Color(1, 1, 1));
	cs_icon_line(img, Vector2(8, 11), Vector2(14, 11), 1.8f, Color(1, 1, 1));
	img->unlock();
	return cs_icon_to_texture(img);
}
static Ref<ImageTexture> _icon_remove_point() {
	Ref<Image> img = cs_icon_new(16);
	img->lock();
	cs_icon_fill_circle(img, Vector2(6, 6), 3.0f, Color(1, 1, 1));
	cs_icon_line(img, Vector2(8, 8), Vector2(14, 14), 1.8f, Color(1, 1, 1));
	cs_icon_line(img, Vector2(14, 8), Vector2(8, 14), 1.8f, Color(1, 1, 1));
	img->unlock();
	return cs_icon_to_texture(img);
}

TerrainPathEditorPlugin::TerrainPathEditorPlugin(EditorNode *p_node) {
	Ref<TerrainPathGizmoPlugin> line_gizmo_plugin;
	line_gizmo_plugin.instance();
	SpatialEditor::get_singleton()->add_gizmo_plugin(line_gizmo_plugin);

	Ref<TerrainPathPointGizmoPlugin> point_gizmo_plugin;
	point_gizmo_plugin.instance();
	SpatialEditor::get_singleton()->add_gizmo_plugin(point_gizmo_plugin);

	_build_icon_toolbar();
	_build_dock();
}

void TerrainPathEditorPlugin::_build_icon_toolbar() {
	icon_toolbar = memnew(HBoxContainer);
	icon_toolbar->hide();

	mode_group.instance();
	static const char *mode_names[3] = { "Select / Move Points (standard Move/Rotate gizmo)", "Add Point", "Remove Point" };
	Ref<ImageTexture> icons[3] = { _icon_select(), _icon_add_point(), _icon_remove_point() };
	for (int i = 0; i < 3; i++) {
		mode_icon_buttons[i] = cs_add_icon_mode_button(icon_toolbar, mode_group, icons[i], mode_names[i], this, "_on_tool_mode_pressed", i);
	}
	mode_icon_buttons[0]->set_pressed(true);

	add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, icon_toolbar);
}

void TerrainPathEditorPlugin::_build_dock() {
	dock_root = cs_make_dock_root(&dock_content);
	dock_root->set_name("TerrainPathDock");

	cs_add_section_header(dock_content, "Path Settings");
	width_spin = cs_add_slider_row(dock_content, "Width", 0.0, 256.0, 0.1, 6.0, this, "_on_width_changed");
	falloff_width_spin = cs_add_slider_row(dock_content, "Falloff Width", 0.0, 256.0, 0.1, 4.0, this, "_on_falloff_width_changed");

	HBoxContainer *falloff_row = memnew(HBoxContainer);
	Label *falloff_label = memnew(Label);
	falloff_label->set_text("Falloff  ");
	falloff_row->add_child(falloff_label);
	falloff_select = memnew(OptionButton);
	falloff_select->add_item("Smooth");
	falloff_select->add_item("Linear");
	falloff_select->add_item("Spherical");
	falloff_select->add_item("Tip");
	falloff_select->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	falloff_select->connect("item_selected", this, "_on_falloff_selected");
	falloff_row->add_child(falloff_select);
	dock_content->add_child(falloff_row);

	height_offset_spin = cs_add_slider_row(dock_content, "Height Offset", -1000.0, 1000.0, 0.01, 0.0, this, "_on_height_offset_changed");
	strength_spin = cs_add_slider_row(dock_content, "Strength", 0.0, 1.0, 0.01, 1.0, this, "_on_strength_changed");

	cs_add_section_header(dock_content, "Non-Destructive");
	refresh_baseline_button = memnew(Button);
	refresh_baseline_button->set_text("Refresh Baseline");
	refresh_baseline_button->set_tooltip("Re-anchor this path to the terrain's current shape (use after sculpting over ground this path already shaped, so it stops reverting toward the old baseline)");
	refresh_baseline_button->connect("pressed", this, "_on_refresh_baseline_pressed");
	dock_content->add_child(refresh_baseline_button);

	remove_effect_button = memnew(Button);
	remove_effect_button->set_text("Remove Effect");
	remove_effect_button->set_tooltip("Revert the terrain to how it looked before this path (does not delete the node)");
	remove_effect_button->connect("pressed", this, "_on_remove_effect_pressed");
	dock_content->add_child(remove_effect_button);

	add_control_to_bottom_panel(dock_root, "Path");
}

bool TerrainPathEditorPlugin::handles(Object *p_object) const {
	if (Object::cast_to<TerrainPath>(p_object)) return true;
	TerrainPathPoint *pt = Object::cast_to<TerrainPathPoint>(p_object);
	return pt && Object::cast_to<TerrainPath>(pt->get_parent()) != nullptr;
}

void TerrainPathEditorPlugin::edit(Object *p_object) {
	TerrainPath *new_path = Object::cast_to<TerrainPath>(p_object);
	if (!new_path) {
		TerrainPathPoint *pt = Object::cast_to<TerrainPathPoint>(p_object);
		if (pt) new_path = Object::cast_to<TerrainPath>(pt->get_parent());
	}
	path = new_path;
	if (path) {
		width_spin->set_value(path->get_width());
		falloff_width_spin->set_value(path->get_falloff_width());
		falloff_select->select(path->get_falloff_type());
		height_offset_spin->set_value(path->get_height_offset());
		strength_spin->set_value(path->get_strength());
	}
}

void TerrainPathEditorPlugin::make_visible(bool p_visible) {
	if (icon_toolbar) icon_toolbar->set_visible(p_visible);
	if (dock_root && p_visible) {
		make_bottom_panel_item_visible(dock_root);
	}
	if (!p_visible) {
		path = nullptr;
		tool_mode = MODE_SELECT;
		if (mode_icon_buttons[0]) mode_icon_buttons[0]->set_pressed(true);
	}
}

bool TerrainPathEditorPlugin::_raycast(Camera *p_camera, const Point2 &p_screen, Vector3 &r_hit) {
	Vector3 from = p_camera->project_ray_origin(p_screen);
	Vector3 dir = p_camera->project_ray_normal(p_screen);

	CellTerrainManager *manager = path ? path->get_terrain_manager() : nullptr;
	if (manager) {
		Ref<World> world = manager->get_world();
		if (world.is_valid()) {
			PhysicsDirectSpaceState *ss = world->get_direct_space_state();
			if (ss) {
				PhysicsDirectSpaceState::RayResult res;
				Vector3 to = from + dir * 100000.0f;
				if (ss->intersect_ray(from, to, res)) { r_hit = res.position; return true; }
			}
		}
	}
	// Fallback: intersect the y=0 ground plane.
	if (Math::abs(dir.y) > CMP_EPSILON) {
		float t = -from.y / dir.y;
		if (t > 0.0f) { r_hit = from + dir * t; return true; }
	}
	return false;
}

void TerrainPathEditorPlugin::_add_point_at(const Vector3 &p_world_pos, int p_insert_index) {
	if (!path) return;

	TerrainPathPoint *point = memnew(TerrainPathPoint);
	point->set_name("Point");
	Transform t;
	t.origin = path->get_global_transform().affine_inverse().xform(p_world_pos);
	point->set_transform(t);

	Node *owner = get_tree()->get_edited_scene_root();
	UndoRedo *ur = &get_undo_redo();
	ur->create_action(TTR("Add Terrain Path Point"));
	ur->add_do_method(path, "add_child", point);
	if (p_insert_index >= 0) ur->add_do_method(path, "move_child", point, p_insert_index);
	ur->add_do_method(point, "set_owner", owner);
	ur->add_do_method(path, "notify_point_changed");
	ur->add_do_reference(point);
	ur->add_undo_method(path, "remove_child", point);
	ur->add_undo_method(path, "notify_point_changed");
	ur->commit_action();
}

bool TerrainPathEditorPlugin::forward_spatial_gui_input(Camera *p_camera, const Ref<InputEvent> &p_event) {
	if (!path || tool_mode == MODE_SELECT) return false;

	Ref<InputEventMouseButton> mb = p_event;
	if (!mb.is_valid() || !mb->is_pressed() || mb->get_button_index() != BUTTON_LEFT) return false;
	Point2 mbpos(mb->get_position().x, mb->get_position().y);
	static const int click_dist = 10;

	Vector<TerrainPathPoint *> pts;
	for (int i = 0; i < path->get_child_count(); i++) {
		TerrainPathPoint *p = Object::cast_to<TerrainPathPoint>(path->get_child(i));
		if (p) pts.push_back(p);
	}
	int pc = pts.size();

	if (tool_mode == MODE_ADD) {
		// Don't add on top of an existing point — let the click select it instead.
		for (int i = 0; i < pc; i++) {
			if (p_camera->unproject_position(pts[i]->get_global_transform().origin).distance_to(mbpos) < click_dist) return false;
		}

		int closest_seg = -1;
		Vector3 closest_point;
		float closest_d = 1e20f;
		for (int i = 0; i < pc - 1; i++) {
			Vector3 a = pts[i]->get_global_transform().origin;
			Vector3 b = pts[i + 1]->get_global_transform().origin;
			Vector2 s[2] = { p_camera->unproject_position(a), p_camera->unproject_position(b) };
			Vector2 inters = Geometry::get_closest_point_to_segment_2d(mbpos, s);
			float d = inters.distance_to(mbpos);
			if (d < click_dist && d < closest_d) {
				closest_d = d;
				closest_seg = i;
				Vector3 ray_from = p_camera->project_ray_origin(mbpos);
				Vector3 ray_dir = p_camera->project_ray_normal(mbpos);
				Vector3 ra, rb;
				Geometry::get_closest_points_between_segments(ray_from, ray_from + ray_dir * 4096, a, b, ra, rb);
				closest_point = rb;
			}
		}

		if (closest_seg != -1) {
			_add_point_at(closest_point, closest_seg + 1);
			return true;
		}

		// Otherwise append a new point at the end, placed right on the terrain
		// under the cursor (falls back to the ground plane if no terrain hit).
		Vector3 hit;
		if (_raycast(p_camera, mbpos, hit)) {
			_add_point_at(hit, -1);
			return true;
		}
	} else if (tool_mode == MODE_REMOVE) {
		for (int i = 0; i < pc; i++) {
			if (p_camera->unproject_position(pts[i]->get_global_transform().origin).distance_to(mbpos) < click_dist) {
				TerrainPathPoint *p = pts[i];
				Node *owner = get_tree()->get_edited_scene_root();
				int idx = p->get_index();
				UndoRedo *ur = &get_undo_redo();
				ur->create_action(TTR("Remove Terrain Path Point"));
				ur->add_do_method(path, "remove_child", p);
				ur->add_do_method(path, "notify_point_changed");
				ur->add_undo_method(path, "add_child", p);
				ur->add_undo_method(path, "move_child", p, idx);
				ur->add_undo_method(p, "set_owner", owner);
				ur->add_undo_method(path, "notify_point_changed");
				ur->add_undo_reference(p);
				ur->commit_action();
				return true;
			}
		}
	}

	return false;
}

void TerrainPathEditorPlugin::_on_tool_mode_pressed(int idx) { tool_mode = idx; }
void TerrainPathEditorPlugin::_on_width_changed(float v) { if (path) path->set_width(v); }
void TerrainPathEditorPlugin::_on_falloff_width_changed(float v) { if (path) path->set_falloff_width(v); }
void TerrainPathEditorPlugin::_on_falloff_selected(int idx) { if (path) path->set_falloff_type(idx); }
void TerrainPathEditorPlugin::_on_height_offset_changed(float v) { if (path) path->set_height_offset(v); }
void TerrainPathEditorPlugin::_on_strength_changed(float v) { if (path) path->set_strength(v); }

void TerrainPathEditorPlugin::_on_refresh_baseline_pressed() {
	if (path) path->refresh_baseline();
}

void TerrainPathEditorPlugin::_on_remove_effect_pressed() {
	if (path) path->remove_effect();
}

void TerrainPathEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_tool_mode_pressed", "idx"), &TerrainPathEditorPlugin::_on_tool_mode_pressed);
	ClassDB::bind_method(D_METHOD("_on_width_changed", "v"), &TerrainPathEditorPlugin::_on_width_changed);
	ClassDB::bind_method(D_METHOD("_on_falloff_width_changed", "v"), &TerrainPathEditorPlugin::_on_falloff_width_changed);
	ClassDB::bind_method(D_METHOD("_on_falloff_selected", "idx"), &TerrainPathEditorPlugin::_on_falloff_selected);
	ClassDB::bind_method(D_METHOD("_on_height_offset_changed", "v"), &TerrainPathEditorPlugin::_on_height_offset_changed);
	ClassDB::bind_method(D_METHOD("_on_strength_changed", "v"), &TerrainPathEditorPlugin::_on_strength_changed);
	ClassDB::bind_method(D_METHOD("_on_refresh_baseline_pressed"), &TerrainPathEditorPlugin::_on_refresh_baseline_pressed);
	ClassDB::bind_method(D_METHOD("_on_remove_effect_pressed"), &TerrainPathEditorPlugin::_on_remove_effect_pressed);
}

#endif
