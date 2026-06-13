#include "terrain_editor_plugin.h"
#ifdef TOOLS_ENABLED

#include "cell_terrain.h"
#include "cell_terrain_manager.h"
#include "editor/editor_node.h"
#include "scene/3d/camera.h"
#include "scene/gui/box_container.h"
#include "scene/gui/option_button.h"
#include "scene/gui/slider.h"
#include "scene/resources/world.h"
#include "servers/physics_server.h"

TerrainEditorPlugin::TerrainEditorPlugin(EditorNode *p_node) {
	brush.instance();

	toolbar = memnew(HBoxContainer);
	toolbar->hide();

	mode_select = memnew(OptionButton);
	mode_select->add_item("Raise");   // 0 -> MODE_RAISE
	mode_select->add_item("Lower");   // 1 -> MODE_LOWER
	mode_select->add_item("Flatten"); // 2 -> MODE_FLATTEN
	mode_select->connect("item_selected", this, "_on_mode_selected");
	toolbar->add_child(mode_select);

	radius_slider = memnew(HSlider);
	radius_slider->set_min(1);
	radius_slider->set_max(64);
	radius_slider->set_value(brush_radius);
	radius_slider->set_custom_minimum_size(Size2(80, 0));
	radius_slider->connect("value_changed", this, "_on_radius_changed");
	toolbar->add_child(radius_slider);

	strength_slider = memnew(HSlider);
	strength_slider->set_min(0.1);
	strength_slider->set_max(20);
	strength_slider->set_step(0.1);
	strength_slider->set_value(brush_strength);
	strength_slider->set_custom_minimum_size(Size2(80, 0));
	strength_slider->connect("value_changed", this, "_on_strength_changed");
	toolbar->add_child(strength_slider);

	add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, toolbar);
}

bool TerrainEditorPlugin::handles(Object *p_object) const {
	return Object::cast_to<CellTerrainManager>(p_object) != nullptr;
}

void TerrainEditorPlugin::edit(Object *p_object) {
	manager = Object::cast_to<CellTerrainManager>(p_object);
	brush->set_manager(manager);
}

void TerrainEditorPlugin::make_visible(bool p_visible) {
	if (toolbar) toolbar->set_visible(p_visible);
	if (!p_visible && manager) {
		manager->editor_clear(); // save edits + drop preview tiles from the scene
		manager = nullptr;
		brush->set_manager(nullptr);
		painting = false;
	}
}

bool TerrainEditorPlugin::_raycast(Camera *cam, const Point2 &screen, Vector3 &out_hit) {
	if (!manager) return false;

	Vector3 from = cam->project_ray_origin(screen);
	Vector3 dir = cam->project_ray_normal(screen);

	// 1. Where the ray meets the ground plane (y=0) decides which tiles to load.
	bool have_ground = false;
	Vector3 ground;
	if (Math::abs(dir.y) > CMP_EPSILON) {
		float t = -from.y / dir.y;
		if (t > 0.0f) {
			ground = from + dir * t;
			have_ground = true;
			manager->editor_focus(ground); // ensure tiles exist under the cursor
		}
	}

	// 2. Precise hit against the (now loaded) terrain colliders.
	Ref<World> world = manager->get_world();
	if (world.is_valid()) {
		PhysicsDirectSpaceState *ss = world->get_direct_space_state();
		if (ss) {
			PhysicsDirectSpaceState::RayResult res;
			Vector3 to = from + dir * 100000.0f;
			if (ss->intersect_ray(from, to, res)) {
				out_hit = res.position;
				return true;
			}
		}
	}

	// 3. Fallback: the ground-plane point. Correct for flat/new tiles, and
	//    covers the frame after a tile loads before its collider is queryable.
	if (have_ground) {
		out_hit = ground;
		return true;
	}
	return false;
}

bool TerrainEditorPlugin::forward_spatial_gui_input(Camera *p_camera, const Ref<InputEvent> &p_event) {
	if (!manager) return false;

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->get_button_index() == BUTTON_LEFT) {
		if (mb->is_pressed()) {
			Vector3 hit;
			if (_raycast(p_camera, mb->get_position(), hit)) {
				brush->begin_stroke();
				brush->apply_brush(hit, brush_radius, brush_strength, brush_mode);
				painting = true;
				return true; // consume so the editor doesn't box-select
			}
		} else if (painting) {
			brush->end_stroke(&get_undo_redo()); // get_undo_redo() returns a reference in 3.5
			painting = false;
			return true;
		}
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && painting) {
		Vector3 hit;
		if (_raycast(p_camera, mm->get_position(), hit)) {
			brush->apply_brush(hit, brush_radius, brush_strength, brush_mode);
		}
		return true;
	}

	return false;
}

void TerrainEditorPlugin::_on_mode_selected(int idx) { brush_mode = idx; }
void TerrainEditorPlugin::_on_radius_changed(float v) { brush_radius = v; }
void TerrainEditorPlugin::_on_strength_changed(float v) { brush_strength = v; }

void TerrainEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_mode_selected", "idx"), &TerrainEditorPlugin::_on_mode_selected);
	ClassDB::bind_method(D_METHOD("_on_radius_changed", "v"), &TerrainEditorPlugin::_on_radius_changed);
	ClassDB::bind_method(D_METHOD("_on_strength_changed", "v"), &TerrainEditorPlugin::_on_strength_changed);
}

#endif // TOOLS_ENABLED
