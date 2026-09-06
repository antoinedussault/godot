#include "foliage_editor_plugin.h"
#ifdef TOOLS_ENABLED

#include "foliage_manager.h"
#include "foliage_type.h"
#include "cell_foliage.h"
#include "editor/editor_node.h"
#include "editor/editor_spin_slider.h"
#include "editor_ui_helpers.h"
#include "core/undo_redo.h"
#include "scene/3d/camera.h"
#include "scene/gui/base_button.h" // ButtonGroup
#include "scene/gui/box_container.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/resources/world.h"
#include "servers/physics_server.h"

FoliageEditorPlugin::FoliageEditorPlugin(EditorNode *p_node) {
	_tree_importer.instance();
	_build_dock();
}

void FoliageEditorPlugin::_build_dock() {
	dock_root = cs_make_dock_root(&dock_content);
	dock_root->set_name("FoliageDock");

	cs_add_section_header(dock_content, "Tool");
	mode_grid = memnew(GridContainer);
	mode_grid->set_columns(3);
	dock_content->add_child(mode_grid);
	mode_group.instance();
	static const char *mode_names[3] = { "Scatter", "Single", "Erase" };
	for (int i = 0; i < 3; i++) {
		mode_buttons[i] = cs_add_mode_button(mode_grid, mode_group, mode_names[i], this, "_on_mode_button_pressed", i);
	}
	mode_buttons[0]->set_pressed(true);

	cs_add_section_header(dock_content, "Brush Settings");

	HBoxContainer *type_row = memnew(HBoxContainer);
	Label *type_label = memnew(Label);
	type_label->set_text("Type  ");
	type_row->add_child(type_label);
	type_select = memnew(OptionButton);
	type_select->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	type_select->connect("item_selected", this, "_on_type_selected");
	type_row->add_child(type_select);
	dock_content->add_child(type_row);

	radius_spin = cs_add_slider_row(dock_content, "Radius", 0.5, 256, 0.5, radius, this, "_on_radius_changed");
	strength_spin = cs_add_slider_row(dock_content, "Strength", 0.01, 1.0, 0.01, strength, this, "_on_strength_changed");

	add_control_to_bottom_panel(dock_root, "Foliage");
}

bool FoliageEditorPlugin::handles(Object *p_object) const {
	return Object::cast_to<FoliageManager>(p_object) != nullptr;
}

void FoliageEditorPlugin::edit(Object *p_object) {
	manager = Object::cast_to<FoliageManager>(p_object);
	_refresh_type_list();
}

void FoliageEditorPlugin::make_visible(bool p_visible) {
	if (dock_root && p_visible) {
		make_bottom_panel_item_visible(dock_root);
	}
	if (p_visible) {
		set_process(true);
	} else {
		set_process(false);
		cached_camera = nullptr;
		painting = false;
		_stroke_before.clear();
		if (manager) {
			manager->save_dirty();
		}
		manager = nullptr;
	}
}

void FoliageEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		add_import_plugin(_tree_importer);
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		remove_import_plugin(_tree_importer);
	} else if (p_what == NOTIFICATION_PROCESS) {
		// Drive terrain + foliage streaming to the editor camera while this
		// tool is active, mirroring TerrainEditorPlugin. Without this, selecting
		// the foliage tool stops terrain streaming and cells under the cursor
		// never load, so paint clicks would silently do nothing.
		if (manager && cached_camera) {
			manager->editor_stream_to(cached_camera->get_global_transform().origin);
		}
	}
}

void FoliageEditorPlugin::_refresh_type_list() {
	if (!type_select) {
		return;
	}
	type_select->clear();
	if (!manager) {
		return;
	}
	int n = manager->get_type_count();
	for (int i = 0; i < n; i++) {
		Ref<FoliageType> ft = manager->get_type(i);
		String label = ft.is_valid() && ft->get_type_name() != "" ? ft->get_type_name() : ("Type " + itos(i));
		type_select->add_item(label, i);
	}
	if (type_index >= n) {
		type_index = 0;
	}
}

bool FoliageEditorPlugin::_raycast(Camera *cam, const Point2 &screen, Vector3 &out_hit, Vector3 &out_normal) {
	if (!manager || !cam) {
		return false;
	}
	Vector3 from = cam->project_ray_origin(screen);
	Vector3 dir = cam->project_ray_normal(screen);

	Ref<World> world = manager->get_world();
	if (world.is_valid()) {
		PhysicsDirectSpaceState *ss = world->get_direct_space_state();
		if (ss) {
			PhysicsDirectSpaceState::RayResult res;
			Vector3 to = from + dir * 100000.0f;
			if (ss->intersect_ray(from, to, res)) {
				out_hit = res.position;
				out_normal = res.normal;
				return true;
			}
		}
	}
	// Fallback: intersect the y=0 plane.
	if (Math::abs(dir.y) > CMP_EPSILON) {
		float t = -from.y / dir.y;
		if (t > 0.0f) {
			out_hit = from + dir * t;
			out_normal = Vector3(0, 1, 0);
			return true;
		}
	}
	return false;
}

void FoliageEditorPlugin::_snapshot_cells(const Vector3 &hit) {
	if (!manager) return;
	float cs = manager->get_cell_size();
	int cx0 = (int)Math::floor((hit.x - radius) / cs);
	int cx1 = (int)Math::floor((hit.x + radius) / cs);
	int cz0 = (int)Math::floor((hit.z - radius) / cs);
	int cz1 = (int)Math::floor((hit.z + radius) / cs);
	for (int cz = cz0; cz <= cz1; cz++) {
		for (int cx = cx0; cx <= cx1; cx++) {
			int64_t key = ((int64_t)(uint32_t)cx << 32) | (uint32_t)cz;
			if (_stroke_before.has(key)) continue;
			CellFoliage *cf = manager->get_cell_foliage(cx, cz);
			if (!cf) continue;
			_stroke_before[key] = cf->serialize();
		}
	}
}

void FoliageEditorPlugin::_end_stroke() {
	if (!manager || _stroke_before.empty()) {
		_stroke_before.clear();
		return;
	}
	UndoRedo *ur = &get_undo_redo();
	ur->create_action("Paint Foliage");
	for (Map<int64_t, PoolRealArray>::Element *E = _stroke_before.front(); E; E = E->next()) {
		int cx = (int32_t)(E->key() >> 32);
		int cz = (int32_t)(E->key() & 0xffffffff);
		PoolRealArray after = manager->get_cell_data(cx, cz);
		ur->add_do_method(manager, "apply_cell_data", cx, cz, after);
		ur->add_undo_method(manager, "apply_cell_data", cx, cz, E->get());
	}
	ur->commit_action();
	_stroke_before.clear();
}

void FoliageEditorPlugin::_apply_at(const Vector3 &hit, const Vector3 &normal) {
	if (!manager) {
		return;
	}
	switch (mode) {
		case MODE_SCATTER:
			manager->paint_scatter(type_index, hit, radius, strength);
			break;
		case MODE_SINGLE:
			manager->place_single(type_index, hit, normal);
			break;
		case MODE_ERASE:
			manager->erase(type_index, hit, radius);
			break;
	}
}

bool FoliageEditorPlugin::forward_spatial_gui_input(Camera *p_camera, const Ref<InputEvent> &p_event) {
	if (!manager) {
		return false;
	}
	cached_camera = p_camera;

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->get_button_index() == BUTTON_LEFT) {
		if (mb->is_pressed()) {
			Vector3 hit, normal;
			if (_raycast(p_camera, mb->get_position(), hit, normal)) {
				_stroke_before.clear();
				_snapshot_cells(hit);
				_apply_at(hit, normal);
				if (mode == MODE_SINGLE) {
					_end_stroke();
				} else {
					painting = true;
				}
				return true;
			}
		} else {
			if (painting) {
				_end_stroke();
				painting = false;
			}
			if (manager) {
				manager->save_dirty();
			}
			return true;
		}
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && painting) {
		Vector3 hit, normal;
		if (_raycast(p_camera, mm->get_position(), hit, normal)) {
			_snapshot_cells(hit);
			_apply_at(hit, normal);
		}
		return true;
	}

	return false;
}

void FoliageEditorPlugin::_on_mode_button_pressed(int idx) { mode = idx; }
void FoliageEditorPlugin::_on_type_selected(int idx) { type_index = idx; }
void FoliageEditorPlugin::_on_radius_changed(float v) { radius = v; }
void FoliageEditorPlugin::_on_strength_changed(float v) { strength = v; }

void FoliageEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_mode_button_pressed", "idx"), &FoliageEditorPlugin::_on_mode_button_pressed);
	ClassDB::bind_method(D_METHOD("_on_type_selected", "idx"), &FoliageEditorPlugin::_on_type_selected);
	ClassDB::bind_method(D_METHOD("_on_radius_changed", "v"), &FoliageEditorPlugin::_on_radius_changed);
	ClassDB::bind_method(D_METHOD("_on_strength_changed", "v"), &FoliageEditorPlugin::_on_strength_changed);
}

#endif // TOOLS_ENABLED
