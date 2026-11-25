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
#include "cell_terrain_manager.h"
#include "scene/resources/material.h"
#include "servers/visual_server.h"
#include "editor/plugins/spatial_editor_plugin.h"
#include "scene/gui/base_button.h" // ButtonGroup
#include "scene/gui/box_container.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/gui/separator.h"
#include "scene/gui/button.h"
#include "core/image.h"
#include "scene/resources/texture.h"
#include "scene/resources/world.h"
#include "servers/physics_server.h"

FoliageEditorPlugin::FoliageEditorPlugin(EditorNode *p_node) {
	_tree_importer.instance();
	_build_icon_toolbar();
	_build_toolbar_settings();
}

// --- Tiny procedural tool-mode icons (16x16, flat white silhouette on
// transparent — see editor_ui_helpers.h for the drawing primitives). Same
// approach as the terrain tool: no art pipeline in this module, so the toolbar
// icons are rasterized in code.
static Ref<ImageTexture> _fol_icon_scatter() {
	Ref<Image> img = cs_icon_new(16);
	img->lock();
	// A spray of small clumps — several instances at once.
	static const float pts[][3] = {
		{ 4, 4, 1.5f }, { 9, 3, 1.2f }, { 13, 6, 1.3f },
		{ 3, 9, 1.2f }, { 8, 8, 1.7f }, { 12, 11, 1.4f },
		{ 5, 13, 1.3f }
	};
	for (int i = 0; i < 7; i++) {
		cs_icon_fill_circle(img, Vector2(pts[i][0], pts[i][1]), pts[i][2], Color(1, 1, 1));
	}
	img->unlock();
	return cs_icon_to_texture(img);
}
static Ref<ImageTexture> _fol_icon_single() {
	Ref<Image> img = cs_icon_new(16);
	img->lock();
	// One plant: a stem with a crown — places exactly one instance.
	cs_icon_line(img, Vector2(8, 14), Vector2(8, 7), 1.6f, Color(1, 1, 1));
	cs_icon_fill_circle(img, Vector2(8, 5), 3.2f, Color(1, 1, 1));
	img->unlock();
	return cs_icon_to_texture(img);
}
static Ref<ImageTexture> _fol_icon_erase() {
	Ref<Image> img = cs_icon_new(16);
	img->lock();
	// Clumps with a slash through them — removes instances in the radius.
	cs_icon_fill_circle(img, Vector2(5, 6), 1.7f, Color(1, 1, 1));
	cs_icon_fill_circle(img, Vector2(10, 5), 1.4f, Color(1, 1, 1));
	cs_icon_fill_circle(img, Vector2(7, 11), 1.5f, Color(1, 1, 1));
	cs_icon_line(img, Vector2(2, 14), Vector2(14, 2), 2.0f, Color(1, 1, 1));
	img->unlock();
	return cs_icon_to_texture(img);
}

void FoliageEditorPlugin::_build_icon_toolbar() {
	icon_toolbar = memnew(HBoxContainer);
	icon_toolbar->hide();

	mode_group.instance();
	static const char *mode_names[3] = { "Scatter", "Single", "Erase" };
	Ref<ImageTexture> icons[3] = { _fol_icon_scatter(), _fol_icon_single(), _fol_icon_erase() };
	for (int i = 0; i < 3; i++) {
		mode_buttons[i] = cs_add_icon_mode_button(icon_toolbar, mode_group, icons[i], mode_names[i], this, "_on_mode_button_pressed", i);
	}
	mode_buttons[0]->set_pressed(true);

	add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, icon_toolbar);
}

void FoliageEditorPlugin::_build_toolbar_settings() {
	// Appended to the SAME HBox as the mode icons so the whole tool shows and
	// hides as one unit.
	icon_toolbar->add_child(memnew(VSeparator));

	type_select = memnew(OptionButton);
	type_select->set_tooltip("Which foliage type to paint");
	type_select->set_custom_minimum_size(Size2(150, 0));
	type_select->connect("item_selected", this, "_on_type_selected");
	icon_toolbar->add_child(type_select);

	radius_spin = cs_add_toolbar_slider(icon_toolbar, "Radius", 0.5, 256, 0.5, radius, 120, this, "_on_radius_changed");
	strength_spin = cs_add_toolbar_slider(icon_toolbar, "Density", 0.01, 1.0, 0.01, strength, 120, this, "_on_strength_changed");
}

bool FoliageEditorPlugin::handles(Object *p_object) const {
	return Object::cast_to<FoliageManager>(p_object) != nullptr;
}

void FoliageEditorPlugin::edit(Object *p_object) {
	manager = Object::cast_to<FoliageManager>(p_object);
	_refresh_type_list();
	// Grab the editor camera immediately. Without this cached_camera stayed
	// null until the first viewport input event, so the PROCESS handler never
	// streamed terrain, no collision existed under the cursor, and early paint
	// clicks hit nothing - or worse, the y=0 fallback.
	if (manager) {
		SpatialEditor *se = SpatialEditor::get_singleton();
		if (se && se->get_editor_viewport(0)) {
			Camera *cam = se->get_editor_viewport(0)->get_camera();
			if (cam) {
				cached_camera = cam;
				manager->editor_stream_to(cam->get_global_transform().origin);
			}
		}
	}
}

void FoliageEditorPlugin::make_visible(bool p_visible) {
	// One call covers the whole UI: the settings are children of icon_toolbar.
	if (icon_toolbar) icon_toolbar->set_visible(p_visible);
	if (p_visible) {
		set_process(true);
	} else {
		set_process(false);
		cached_camera = nullptr;
		painting = false;
		brush_viz.destroy();
		_has_hit = false;
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
	if (type_index < n) {
		type_select->select(type_index);
	}
}

bool FoliageEditorPlugin::_raycast(Camera *cam, const Point2 &screen, Vector3 &out_hit, Vector3 &out_normal) {
	if (!manager || !cam) {
		return false;
	}
	CellTerrainManager *tm = Object::cast_to<CellTerrainManager>(
			manager->get_node_or_null(manager->get_terrain_manager_path()));
	if (!tm) return false;

	Vector3 from = cam->project_ray_origin(screen);
	Vector3 dir = cam->project_ray_normal(screen);
	// Same reason as the terrain tool: a 100,000-unit physics ray per
	// mouse-move froze the editor whenever it missed the ground.
	if (!tm->raycast_terrain(from, dir, out_hit)) {
		return false;
	}

	// Surface normal from the heightmap via central differences, so foliage can
	// align to slopes without needing collision present.
	const float e = 0.5f;
	float hl = tm->get_height_at_world(out_hit.x - e, out_hit.z);
	float hr = tm->get_height_at_world(out_hit.x + e, out_hit.z);
	float hd = tm->get_height_at_world(out_hit.x, out_hit.z - e);
	float hu = tm->get_height_at_world(out_hit.x, out_hit.z + e);
	out_normal = Vector3(hl - hr, 2.0f * e, hd - hu).normalized();
	if (out_normal.length_squared() < 0.5f) out_normal = Vector3(0, 1, 0);
	return true;
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
	// Once per stroke, not per mouse-move: each refresh triggers a navmesh
	// rebuild on the touched cells, which is the most expensive operation in
	// the module.
	manager->refresh_nav_obstacles();
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

void FoliageEditorPlugin::_clear_brush_viz() {
	// Hide, never destroy - see TerrainEditorPlugin::_clear_brush_viz().
	brush_viz.hide();
	_has_hit = false;
}

void FoliageEditorPlugin::_update_brush_viz(const Vector3 &world_hit) {
	if (!manager) {
		_clear_brush_viz();
		return;
	}
	_last_hit = world_hit;
	_has_hit = true;

	VisualServer *vs = VisualServer::get_singleton();
	const RID im = brush_viz.begin_rebuild(manager);
	if (!im.is_valid()) {
		return;
	}

	const Vector3 org = manager->get_global_transform().origin;
	const float bias = 0.06f;
	const int segs = 64;
	// Green to read as "planting", distinct from the terrain sculpt brush.
	const Color outer(0.45f, 1.0f, 0.5f, 1.0f);
	const Color inner(0.2f, 0.7f, 0.3f, 0.5f);

	// Outer ring at the paint radius. Single mode places one instance at the
	// cursor and ignores radius, so it gets a marker instead of a ring.
	if (mode != MODE_SINGLE) {
		vs->immediate_begin(im, VisualServer::PRIMITIVE_LINE_LOOP);
		vs->immediate_color(im, outer);
		for (int i = 0; i < segs; i++) {
			float a = Math_TAU * i / segs;
			float wx = world_hit.x + Math::cos(a) * radius;
			float wz = world_hit.z + Math::sin(a) * radius;
			bool ok = false;
			float wy = manager->get_terrain_height_at(wx, wz, &ok);
			if (!ok) wy = world_hit.y;
			vs->immediate_vertex(im, Vector3(wx - org.x, wy + bias - org.y, wz - org.z));
		}
		vs->immediate_end(im);

		vs->immediate_begin(im, VisualServer::PRIMITIVE_LINE_LOOP);
		vs->immediate_color(im, inner);
		float ir = radius * 0.5f;
		for (int i = 0; i < segs; i++) {
			float a = Math_TAU * i / segs;
			float wx = world_hit.x + Math::cos(a) * ir;
			float wz = world_hit.z + Math::sin(a) * ir;
			bool ok = false;
			float wy = manager->get_terrain_height_at(wx, wz, &ok);
			if (!ok) wy = world_hit.y;
			vs->immediate_vertex(im, Vector3(wx - org.x, wy + bias - org.y, wz - org.z));
		}
		vs->immediate_end(im);
	}

	// Centre cross, and a vertical stick so a single placement is visible even
	// against a busy ground texture.
	float arm = (mode == MODE_SINGLE) ? 0.75f : MAX(radius * 0.06f, 0.25f);
	float cx = world_hit.x - org.x;
	float cz = world_hit.z - org.z;
	float cy = world_hit.y + bias - org.y;
	vs->immediate_begin(im, VisualServer::PRIMITIVE_LINES);
	vs->immediate_color(im, outer);
	vs->immediate_vertex(im, Vector3(cx - arm, cy, cz));
	vs->immediate_vertex(im, Vector3(cx + arm, cy, cz));
	vs->immediate_vertex(im, Vector3(cx, cy, cz - arm));
	vs->immediate_vertex(im, Vector3(cx, cy, cz + arm));
	vs->immediate_vertex(im, Vector3(cx, cy, cz));
	vs->immediate_vertex(im, Vector3(cx, cy + arm * 2.0f, cz));
	vs->immediate_end(im);
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
	if (mm.is_valid()) {
		Vector3 hit, normal;
		if (_raycast(p_camera, mm->get_position(), hit, normal)) {
			// Keep the ring under the cursor whether or not a stroke is active,
			// so the brush is visible before you commit to a click.
			_update_brush_viz(hit);
			if (painting) {
				_snapshot_cells(hit);
				_apply_at(hit, normal);
			}
		} else {
			_clear_brush_viz();
		}
		if (painting) {
			return true;
		}
	}

	return false;
}

void FoliageEditorPlugin::_on_mode_button_pressed(int idx) {
	mode = idx;
	if (_has_hit) _update_brush_viz(_last_hit);
}
void FoliageEditorPlugin::_on_type_selected(int idx) { type_index = idx; }
void FoliageEditorPlugin::_on_radius_changed(float v) {
	radius = v;
	// Resize the ring live instead of waiting for the next mouse move.
	if (_has_hit) _update_brush_viz(_last_hit);
}
void FoliageEditorPlugin::_on_strength_changed(float v) { strength = v; }

void FoliageEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_mode_button_pressed", "idx"), &FoliageEditorPlugin::_on_mode_button_pressed);
	ClassDB::bind_method(D_METHOD("_on_type_selected", "idx"), &FoliageEditorPlugin::_on_type_selected);
	ClassDB::bind_method(D_METHOD("_on_radius_changed", "v"), &FoliageEditorPlugin::_on_radius_changed);
	ClassDB::bind_method(D_METHOD("_on_strength_changed", "v"), &FoliageEditorPlugin::_on_strength_changed);
}

#endif // TOOLS_ENABLED
