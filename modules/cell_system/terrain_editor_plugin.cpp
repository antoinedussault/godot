#include "terrain_editor_plugin.h"
#ifdef TOOLS_ENABLED

#include "cell_terrain.h"
#include "cell_terrain_manager.h"
#include "core/image.h"
#include "editor/editor_node.h"
#include "editor/editor_spin_slider.h"
#include "editor/plugins/spatial_editor_plugin.h"
#include "editor_ui_helpers.h"
#include "texture_packer_dialog.h"
#include "scene/3d/camera.h"
#include "scene/3d/immediate_geometry.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"
#include "scene/gui/base_button.h" // ButtonGroup
#include "scene/gui/box_container.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/resources/world.h"
#include "servers/physics_server.h"

// --- Tiny procedural tool-mode icons (16x16, flat white silhouette on
// transparent — see editor_ui_helpers.h for the drawing primitives). No art
// pipeline exists in this module, so these are generated in code rather than
// shipped as image files.
static Ref<ImageTexture> _icon_raise() {
	Ref<Image> img = cs_icon_new(16);
	img->lock();
	cs_icon_fill_triangle(img, Vector2(8, 2), Vector2(2, 13), Vector2(14, 13), Color(1, 1, 1));
	img->unlock();
	return cs_icon_to_texture(img);
}
static Ref<ImageTexture> _icon_lower() {
	Ref<Image> img = cs_icon_new(16);
	img->lock();
	cs_icon_fill_triangle(img, Vector2(8, 13), Vector2(2, 2), Vector2(14, 2), Color(1, 1, 1));
	img->unlock();
	return cs_icon_to_texture(img);
}
static Ref<ImageTexture> _icon_flatten() {
	Ref<Image> img = cs_icon_new(16);
	img->lock();
	cs_icon_fill_rect(img, 2, 7, 12, 3, Color(1, 1, 1));
	cs_icon_fill_rect(img, 2, 4, 2, 9, Color(1, 1, 1));
	cs_icon_fill_rect(img, 12, 4, 2, 9, Color(1, 1, 1));
	img->unlock();
	return cs_icon_to_texture(img);
}
static Ref<ImageTexture> _icon_paint() {
	Ref<Image> img = cs_icon_new(16);
	img->lock();
	cs_icon_fill_triangle(img, Vector2(8, 2), Vector2(5, 9), Vector2(11, 9), Color(1, 1, 1));
	cs_icon_fill_circle(img, Vector2(8, 10), 4.0f, Color(1, 1, 1));
	img->unlock();
	return cs_icon_to_texture(img);
}
static Ref<ImageTexture> _icon_smooth() {
	Ref<Image> img = cs_icon_new(16);
	img->lock();
	Vector2 prev(1, 8);
	for (int i = 1; i <= 14; i++) {
		float x = 1.0f + i;
		float y = 8.0f + Math::sin((x - 1.0f) / 14.0f * Math_TAU) * 4.5f;
		Vector2 cur(x, y);
		cs_icon_line(img, prev, cur, 1.6f, Color(1, 1, 1));
		prev = cur;
	}
	img->unlock();
	return cs_icon_to_texture(img);
}
static Ref<ImageTexture> _icon_noise() {
	Ref<Image> img = cs_icon_new(16);
	img->lock();
	static const float pts[][2] = {
		{ 3, 3 }, { 8, 2 }, { 12, 4 }, { 3, 8 }, { 8, 7 },
		{ 13, 8 }, { 2, 12 }, { 6, 13 }, { 10, 12 }, { 13, 13 }
	};
	for (int i = 0; i < 10; i++) cs_icon_fill_circle(img, Vector2(pts[i][0], pts[i][1]), 1.2f, Color(1, 1, 1));
	img->unlock();
	return cs_icon_to_texture(img);
}
static Ref<ImageTexture> _icon_erode() {
	Ref<Image> img = cs_icon_new(16);
	img->lock();
	Vector2 pts[] = { Vector2(2, 4), Vector2(5, 9), Vector2(8, 5), Vector2(11, 12), Vector2(14, 7) };
	for (int i = 0; i < 4; i++) cs_icon_line(img, pts[i], pts[i + 1], 1.6f, Color(1, 1, 1));
	img->unlock();
	return cs_icon_to_texture(img);
}
static Ref<ImageTexture> _icon_nav_mask() {
	Ref<Image> img = cs_icon_new(16);
	img->lock();
	// A fenced-off square with a diagonal bar — "mark areas excluded/allowed
	// for navmesh generation", distinct from the sculpt/paint icons above.
	cs_icon_line(img, Vector2(3, 3), Vector2(13, 3), 1.6f, Color(1, 1, 1));
	cs_icon_line(img, Vector2(3, 3), Vector2(3, 13), 1.6f, Color(1, 1, 1));
	cs_icon_line(img, Vector2(13, 3), Vector2(13, 13), 1.6f, Color(1, 1, 1));
	cs_icon_line(img, Vector2(3, 13), Vector2(13, 13), 1.6f, Color(1, 1, 1));
	cs_icon_line(img, Vector2(3, 3), Vector2(13, 13), 1.6f, Color(1, 1, 1));
	img->unlock();
	return cs_icon_to_texture(img);
}

TerrainEditorPlugin::TerrainEditorPlugin(EditorNode *p_node) {
	brush.instance();
	_build_icon_toolbar();
	_build_dock();
}

void TerrainEditorPlugin::_build_icon_toolbar() {
	icon_toolbar = memnew(HBoxContainer);
	icon_toolbar->hide();

	mode_group.instance();
	static const char *mode_names[8] = { "Raise", "Lower", "Flatten", "Paint", "Smooth", "Noise", "Erode", "Nav Mask" };
	Ref<ImageTexture> icons[8] = {
		_icon_raise(), _icon_lower(), _icon_flatten(), _icon_paint(),
		_icon_smooth(), _icon_noise(), _icon_erode(), _icon_nav_mask()
	};
	for (int i = 0; i < 8; i++) {
		mode_icon_buttons[i] = cs_add_icon_mode_button(icon_toolbar, mode_group, icons[i], mode_names[i], this, "_on_mode_button_pressed", i);
	}
	mode_icon_buttons[0]->set_pressed(true);

	add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, icon_toolbar);
}

void TerrainEditorPlugin::_build_dock() {
	dock_root = cs_make_dock_root(&dock_content);
	dock_root->set_name("TerrainDock");

	// --- Brush settings ---
	cs_add_section_header(dock_content, "Brush Settings");
	radius_spin = cs_add_slider_row(dock_content, "Radius", 0.1, 512, 0.1, brush_radius, this, "_on_radius_changed");
	strength_spin = cs_add_slider_row(dock_content, "Strength", 0.01, 1000, 0.01, brush_strength, this, "_on_strength_changed");

	HBoxContainer *falloff_row = memnew(HBoxContainer);
	Label *falloff_label = memnew(Label);
	falloff_label->set_text("Falloff  ");
	falloff_row->add_child(falloff_label);
	falloff_select = memnew(OptionButton);
	falloff_select->add_item("Smooth");    // FALLOFF_SMOOTH
	falloff_select->add_item("Linear");    // FALLOFF_LINEAR
	falloff_select->add_item("Spherical"); // FALLOFF_SPHERICAL
	falloff_select->add_item("Tip");       // FALLOFF_TIP
	falloff_select->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	falloff_select->connect("item_selected", this, "_on_falloff_selected");
	falloff_row->add_child(falloff_select);
	dock_content->add_child(falloff_row);

	// --- Textures ---
	cs_add_section_header(dock_content, "Textures");
	pack_textures_button = memnew(Button);
	pack_textures_button->set_text("Pack Textures...");
	pack_textures_button->set_tooltip("Combine separate Albedo/Height/Normal/Roughness images into the packed textures this terrain's shader expects");
	pack_textures_button->connect("pressed", this, "_on_pack_textures_pressed");
	dock_content->add_child(pack_textures_button);

	pack_dialog = memnew(TexturePackerDialog);
	EditorNode::get_singleton()->get_gui_base()->add_child(pack_dialog);

	// --- Paint (mode-specific) ---
	layer_spin = cs_add_slider_row(dock_content, "Layer", 0, 15, 1, 0, this, "_on_layer_changed");

	// --- Flatten (mode-specific) ---
	flatten_force_check = memnew(CheckBox);
	flatten_force_check->set_text("Force Height");
	flatten_force_check->set_pressed(false);
	flatten_force_check->connect("toggled", this, "_on_flatten_force_toggled");
	dock_content->add_child(flatten_force_check);
	flatten_height_spin = cs_add_slider_row(dock_content, "Height", -1000, 1000, 0.5, 0, this, "_on_flatten_height_changed");
	flatten_pick_button = memnew(Button);
	flatten_pick_button->set_text("Pick Height From Terrain");
	flatten_pick_button->set_tooltip("Sample terrain height at cursor and use it as flatten target");
	flatten_pick_button->connect("pressed", this, "_on_flatten_pick_pressed");
	dock_content->add_child(flatten_pick_button);

	// --- Noise (mode-specific) ---
	noise_scale_spin = cs_add_slider_row(dock_content, "Scale", 0.1, 256, 0.1, 8.0, this, "_on_noise_scale_changed");
	noise_seed_spin = cs_add_slider_row(dock_content, "Seed", 0, 9999, 1, 0, this, "_on_noise_seed_changed");

	// --- Erode (mode-specific) ---
	erosion_talus_spin = cs_add_slider_row(dock_content, "Talus", 0.0, 50.0, 0.05, 0.5, this, "_on_erosion_talus_changed");

	// --- Nav Mask (mode-specific) ---
	nav_mask_erase_check = memnew(CheckBox);
	nav_mask_erase_check->set_text("Erase (restore navigable)");
	nav_mask_erase_check->set_tooltip("Off: paint excludes this area from the navmesh (leaves a hole).\nOn: paint restores it to navigable.");
	nav_mask_erase_check->set_pressed(false);
	nav_mask_erase_check->connect("toggled", this, "_on_nav_mask_erase_toggled");
	dock_content->add_child(nav_mask_erase_check);

	// --- Navigation ---
	cs_add_section_header(dock_content, "Navigation");
	bake_navmesh_button = memnew(Button);
	bake_navmesh_button->set_text("Bake Navmesh");
	bake_navmesh_button->set_tooltip("Build persistent navmesh regions for every saved cell outside the live streaming radius, so AI can path across the whole world, not just near the player. Skips cells that are already active (they keep their own live navmesh).");
	bake_navmesh_button->connect("pressed", this, "_on_bake_navmesh_pressed");
	dock_content->add_child(bake_navmesh_button);

	clear_navmesh_button = memnew(Button);
	clear_navmesh_button->set_text("Clear Baked Navmesh");
	clear_navmesh_button->connect("pressed", this, "_on_clear_navmesh_pressed");
	dock_content->add_child(clear_navmesh_button);

	// --- HLOD ---
	cs_add_section_header(dock_content, "HLOD");
	bake_hlod_button = memnew(Button);
	bake_hlod_button->set_text("Bake HLOD");
	bake_hlod_button->connect("pressed", this, "_on_bake_hlod_pressed");
	dock_content->add_child(bake_hlod_button);

	clear_hlod_button = memnew(Button);
	clear_hlod_button->set_text("Clear HLOD");
	clear_hlod_button->set_tooltip("Delete all baked HLOD chunks (mesh + splatmaps) from disk and unload them");
	clear_hlod_button->connect("pressed", this, "_on_clear_hlod_pressed");
	dock_content->add_child(clear_hlod_button);

	// --- Debug ---
	cs_add_section_header(dock_content, "Debug");
	normals_check = memnew(CheckBox);
	normals_check->set_text("Show Normals");
	normals_check->set_pressed(false);
	normals_check->connect("toggled", this, "_on_normals_toggled");
	dock_content->add_child(normals_check);
	normal_length_spin = cs_add_slider_row(dock_content, "Normal Length", 0.1, 32.0, 0.1, 1.0, this, "_on_normal_length_changed");

	navmesh_viz_check = memnew(CheckBox);
	navmesh_viz_check->set_text("Show Navmesh");
	navmesh_viz_check->set_tooltip("Draw each streamed cell's navigation mesh in the viewport. Independent of Godot's built-in \"Visible Navigation\" debug option (which only draws NavigationRegion nodes, not this module's raw NavigationServer regions).");
	navmesh_viz_check->set_pressed(false);
	navmesh_viz_check->connect("toggled", this, "_on_navmesh_viz_toggled");
	dock_content->add_child(navmesh_viz_check);

	show_all_check = memnew(CheckBox);
	show_all_check->set_text("Show All Edited Terrain");
	show_all_check->set_tooltip("Keep every saved cell loaded regardless of streaming radius, so the whole sculpted world stays visible while editing. Editor only — has no effect at runtime.");
	show_all_check->set_pressed(false);
	show_all_check->connect("toggled", this, "_on_show_all_toggled");
	dock_content->add_child(show_all_check);

	add_control_to_bottom_panel(dock_root, "Terrain");

	_update_toolbar_visibility();
}

bool TerrainEditorPlugin::handles(Object *p_object) const {
	return Object::cast_to<CellTerrainManager>(p_object) != nullptr;
}

void TerrainEditorPlugin::edit(Object *p_object) {
	manager = Object::cast_to<CellTerrainManager>(p_object);
	brush->set_manager(manager);
	if (manager) {
		Ref<Texture> bt = manager->get_brush_texture();
		if (bt.is_valid()) {
			brush->set_brush_image(bt->get_data());
		} else {
			brush->set_brush_image(Ref<Image>());
		}
		// Grab the editor camera and stream immediately so terrain loads
		// without waiting for the first mouse event.
		SpatialEditor *se = SpatialEditor::get_singleton();
		if (se) {
			Camera *cam = se->get_editor_viewport(0)->get_camera();
			if (cam) {
				cached_camera = cam;
				_last_stream_pos = Vector3(1e9f, 1e9f, 1e9f);
				manager->stream_to(cam->get_global_transform().origin);
			}
		}
		if (show_all_check) show_all_check->set_pressed(manager->get_show_all_in_editor());
	}
}

void TerrainEditorPlugin::make_visible(bool p_visible) {
	if (icon_toolbar) icon_toolbar->set_visible(p_visible);
	if (dock_root && p_visible) {
		// Auto-expand the bottom panel when a CellTerrainManager is selected;
		// deselecting deliberately leaves it as-is (matches how Output/Debugger
		// behave — only an explicit switch closes a bottom panel).
		make_bottom_panel_item_visible(dock_root);
	}
	if (p_visible) {
		set_process(true);
	} else {
		set_process(false);
		cached_camera = nullptr;
		_last_stream_pos = Vector3(1e9f, 1e9f, 1e9f);
		_clear_brush_viz();
		if (manager) {
			manager->set_debug_normals(false, 1.0f);
			manager->set_debug_navmesh(false);
			// Do NOT call editor_clear() — keep terrain cells alive so
			// re-selecting the manager doesn't force a full reload.
			manager = nullptr;
			brush->set_manager(nullptr);
			painting = false;
		}
		if (normals_check) normals_check->set_pressed(false);
		if (navmesh_viz_check) navmesh_viz_check->set_pressed(false);
	}
}

void TerrainEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_PROCESS) {
		if (manager && cached_camera) {
			Vector3 cam_pos = cached_camera->get_global_transform().origin;
			if (cam_pos.distance_squared_to(_last_stream_pos) > 0.01f) {
				_last_stream_pos = cam_pos;
				manager->stream_to(cam_pos);
			}
		}

		// Debounced HLOD re-bake: once editing has settled, refresh only the
		// chunks whose cells changed so distant terrain stays in sync.
		if (_hlod_rebake_timer > 0.0f && !painting) {
			_hlod_rebake_timer -= get_process_delta_time();
			if (_hlod_rebake_timer <= 0.0f) {
				_hlod_rebake_timer = 0.0f;
				if (manager && manager->has_dirty_hlod())
					manager->rebake_dirty_hlod();
			}
		}
	}
}

bool TerrainEditorPlugin::_raycast(Camera *cam, const Point2 &screen, Vector3 &out_hit) {
	if (!manager) return false;
	Vector3 from = cam->project_ray_origin(screen);
	Vector3 dir = cam->project_ray_normal(screen);

	bool have_ground = false;
	Vector3 ground;
	if (Math::abs(dir.y) > CMP_EPSILON) {
		float t = -from.y / dir.y;
		if (t > 0.0f) { ground = from + dir * t; have_ground = true; }
	}

	Ref<World> world = manager->get_world();
	if (world.is_valid()) {
		PhysicsDirectSpaceState *ss = world->get_direct_space_state();
		if (ss) {
			PhysicsDirectSpaceState::RayResult res;
			Vector3 to = from + dir * 100000.0f;
			if (ss->intersect_ray(from, to, res)) { out_hit = res.position; return true; }
		}
	}
	if (have_ground) { out_hit = ground; return true; }
	return false;
}

void TerrainEditorPlugin::_clear_brush_viz() {
	if (brush_viz) {
		brush_viz->queue_delete();
		brush_viz = nullptr;
	}
}

void TerrainEditorPlugin::_update_brush_viz(const Vector3 &world_hit) {
	if (!manager) { _clear_brush_viz(); return; }

	_last_terrain_hit = world_hit;
	_has_terrain_hit = true;

	if (_flatten_height_picked) {
		float h = manager->get_height_at_world(world_hit.x, world_hit.z);
		brush->set_flatten_height(h);
		if (flatten_height_spin) flatten_height_spin->set_value(h);
	}


	if (!brush_viz) {
		brush_viz = memnew(ImmediateGeometry);
		Ref<SpatialMaterial> mat;
		mat.instance();
		mat->set_flag(SpatialMaterial::FLAG_UNSHADED, true);
		mat->set_flag(SpatialMaterial::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		mat->set_feature(SpatialMaterial::FEATURE_TRANSPARENT, true);
		mat->set_render_priority(1);
		brush_viz->set_material_override(mat);
		manager->add_child(brush_viz);
	}

	const Vector3 org = manager->get_global_transform().origin;
	const float bias = 0.06f;
	const int segs = 64;

	// Outer ring — white with light blue tint, UE4-style
	const Color outer(0.75f, 0.92f, 1.0f, 1.0f);
	// Inner falloff ring — dimmer blue
	const Color inner(0.25f, 0.60f, 1.0f, 0.55f);

	brush_viz->clear();

	// --- Outer ring at full brush radius ---
	brush_viz->begin(Mesh::PRIMITIVE_LINE_LOOP);
	brush_viz->set_color(outer);
	for (int i = 0; i < segs; i++) {
		float a = Math_TAU * i / segs;
		float wx = world_hit.x + Math::cos(a) * brush_radius;
		float wz = world_hit.z + Math::sin(a) * brush_radius;
		float wy = manager->get_height_at_world(wx, wz) + bias;
		brush_viz->add_vertex(Vector3(wx - org.x, wy, wz - org.z));
	}
	brush_viz->end();

	// --- Inner ring at ~50% radius ---
	float inner_r = brush_radius * 0.5f;
	brush_viz->begin(Mesh::PRIMITIVE_LINE_LOOP);
	brush_viz->set_color(inner);
	for (int i = 0; i < segs; i++) {
		float a = Math_TAU * i / segs;
		float wx = world_hit.x + Math::cos(a) * inner_r;
		float wz = world_hit.z + Math::sin(a) * inner_r;
		float wy = manager->get_height_at_world(wx, wz) + bias;
		brush_viz->add_vertex(Vector3(wx - org.x, wy, wz - org.z));
	}
	brush_viz->end();

	// --- Center cross ---
	float arm = MAX(brush_radius * 0.06f, 0.25f);
	float cx = world_hit.x - org.x;
	float cz_l = world_hit.z - org.z;
	float cy = manager->get_height_at_world(world_hit.x, world_hit.z) + bias;
	brush_viz->begin(Mesh::PRIMITIVE_LINES);
	brush_viz->set_color(outer);
	brush_viz->add_vertex(Vector3(cx - arm, cy, cz_l));
	brush_viz->add_vertex(Vector3(cx + arm, cy, cz_l));
	brush_viz->add_vertex(Vector3(cx, cy, cz_l - arm));
	brush_viz->add_vertex(Vector3(cx, cy, cz_l + arm));
	brush_viz->end();
}

bool TerrainEditorPlugin::forward_spatial_gui_input(Camera *p_camera, const Ref<InputEvent> &p_event) {
	if (!manager) return false;
	cached_camera = p_camera;

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->get_button_index() == BUTTON_LEFT) {
		if (mb->is_pressed()) {
			Vector3 hit;
			if (_raycast(p_camera, mb->get_position(), hit)) {
				_last_terrain_hit = hit;
				_has_terrain_hit = true;
				if (manager) {
					Ref<Texture> bt = manager->get_brush_texture();
					brush->set_brush_image(bt.is_valid() ? bt->get_data() : Ref<Image>());
				}
				// First click in flatten mode: lock the height for this stroke.
				// Use the pre-picked height if Pick was clicked, otherwise sample here.
				if (brush_mode == TerrainBrush::MODE_FLATTEN && !brush->get_force_flatten_height()) {
					if (!_flatten_height_picked) {
						float h = manager->get_height_at_world(hit.x, hit.z);
						brush->set_flatten_height(h);
						if (flatten_height_spin) flatten_height_spin->set_value(h);
					}
					brush->set_force_flatten_height(true);
					_flatten_stroke_locked = true;
					_flatten_height_picked = false;
				}
				_update_brush_viz(hit);
				brush->begin_stroke();
				brush->apply_brush(hit, brush_radius, brush_strength, brush_mode);
				painting = true;
				return true;
			}
		} else if (painting) {
			// Release: remove auto-lock so the next stroke re-samples at the new click.
			if (_flatten_stroke_locked) {
				brush->set_force_flatten_height(false);
				_flatten_stroke_locked = false;
			}
			if (brush_mode != TerrainBrush::MODE_PAINT) {
				brush->end_stroke(&get_undo_redo());
			}
			painting = false;
			// Arm the debounced HLOD re-bake; resets on each subsequent stroke so it
			// only fires ~1s after editing actually stops.
			_hlod_rebake_timer = 1.0f;
			return true;
		}
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid()) {
		Vector3 hit;
		if (_raycast(p_camera, mm->get_position(), hit)) {
			_last_terrain_hit = hit;
			_has_terrain_hit = true;
			_update_brush_viz(hit);
			if (painting) {
				brush->apply_brush(hit, brush_radius, brush_strength, brush_mode);
			}
		} else {
			_clear_brush_viz();
		}
		return painting;
	}

	return false;
}

void TerrainEditorPlugin::_on_mode_button_pressed(int idx) {
	brush_mode = idx;
	_update_toolbar_visibility();
}
void TerrainEditorPlugin::_on_radius_changed(float v) { brush_radius = v; }
void TerrainEditorPlugin::_on_strength_changed(float v) { brush_strength = v; }
void TerrainEditorPlugin::_on_falloff_selected(int idx) { brush->set_falloff_type(idx); }
void TerrainEditorPlugin::_on_layer_changed(float v) { brush->set_paint_layer((int)v); }
void TerrainEditorPlugin::_on_flatten_force_toggled(bool pressed) {
	brush->set_force_flatten_height(pressed);
}
void TerrainEditorPlugin::_on_flatten_height_changed(float v) { brush->set_flatten_height(v); }

void TerrainEditorPlugin::_on_flatten_pick_pressed() {
	_flatten_height_picked = true; // enter sampling mode; height updates on hover until first click
}

void TerrainEditorPlugin::_on_noise_scale_changed(float v) { brush->set_noise_scale(v); }
void TerrainEditorPlugin::_on_noise_seed_changed(float v) { brush->set_noise_seed((int)v); }
void TerrainEditorPlugin::_on_erosion_talus_changed(float v) { brush->set_erosion_talus(v); }
void TerrainEditorPlugin::_on_nav_mask_erase_toggled(bool pressed) { brush->set_nav_mask_erase(pressed); }

void TerrainEditorPlugin::_on_bake_hlod_pressed() {
	if (manager) {
		manager->save_dirty();
		manager->bake_hlod();
	}
}

void TerrainEditorPlugin::_on_clear_hlod_pressed() {
	if (manager) {
		manager->clear_hlod();
	}
}

void TerrainEditorPlugin::_on_bake_navmesh_pressed() {
	if (manager) {
		manager->save_dirty();
		manager->bake_navmesh();
	}
}

void TerrainEditorPlugin::_on_clear_navmesh_pressed() {
	if (manager) {
		manager->clear_baked_navmesh();
	}
}

void TerrainEditorPlugin::_on_normals_toggled(bool pressed) {
	if (manager)
		manager->set_debug_normals(pressed, normal_length_spin->get_value());
}

void TerrainEditorPlugin::_on_normal_length_changed(float v) {
	if (manager && normals_check->is_pressed())
		manager->set_debug_normals(true, v);
}

void TerrainEditorPlugin::_on_navmesh_viz_toggled(bool pressed) {
	if (manager) manager->set_debug_navmesh(pressed);
}

void TerrainEditorPlugin::_on_show_all_toggled(bool pressed) {
	if (manager) manager->set_show_all_in_editor(pressed);
}

void TerrainEditorPlugin::_on_pack_textures_pressed() {
	if (pack_dialog) pack_dialog->popup_centered(Size2(500, 0));
}

void TerrainEditorPlugin::_update_toolbar_visibility() {
	bool is_paint = (brush_mode == TerrainBrush::MODE_PAINT);
	bool is_flatten = (brush_mode == TerrainBrush::MODE_FLATTEN);
	bool is_noise = (brush_mode == TerrainBrush::MODE_NOISE);
	bool is_erode = (brush_mode == TerrainBrush::MODE_ERODE);
	bool is_nav_mask = (brush_mode == TerrainBrush::MODE_NAV_MASK);
	if (layer_spin) layer_spin->set_visible(is_paint);
	if (flatten_force_check) flatten_force_check->set_visible(is_flatten);
	if (flatten_height_spin) flatten_height_spin->set_visible(is_flatten);
	if (flatten_pick_button) flatten_pick_button->set_visible(is_flatten);
	if (noise_scale_spin) noise_scale_spin->set_visible(is_noise);
	if (noise_seed_spin) noise_seed_spin->set_visible(is_noise);
	if (erosion_talus_spin) erosion_talus_spin->set_visible(is_erode);
	if (nav_mask_erase_check) nav_mask_erase_check->set_visible(is_nav_mask);
	// Falloff curve shapes the radial strength for every mode, including Paint.
}

void TerrainEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_mode_button_pressed", "idx"), &TerrainEditorPlugin::_on_mode_button_pressed);
	ClassDB::bind_method(D_METHOD("_on_radius_changed", "v"), &TerrainEditorPlugin::_on_radius_changed);
	ClassDB::bind_method(D_METHOD("_on_strength_changed", "v"), &TerrainEditorPlugin::_on_strength_changed);
	ClassDB::bind_method(D_METHOD("_on_falloff_selected", "idx"), &TerrainEditorPlugin::_on_falloff_selected);
	ClassDB::bind_method(D_METHOD("_on_layer_changed", "v"), &TerrainEditorPlugin::_on_layer_changed);
	ClassDB::bind_method(D_METHOD("_on_flatten_force_toggled", "pressed"), &TerrainEditorPlugin::_on_flatten_force_toggled);
	ClassDB::bind_method(D_METHOD("_on_flatten_height_changed", "v"), &TerrainEditorPlugin::_on_flatten_height_changed);
	ClassDB::bind_method(D_METHOD("_on_flatten_pick_pressed"), &TerrainEditorPlugin::_on_flatten_pick_pressed);
	ClassDB::bind_method(D_METHOD("_on_noise_scale_changed", "v"), &TerrainEditorPlugin::_on_noise_scale_changed);
	ClassDB::bind_method(D_METHOD("_on_noise_seed_changed", "v"), &TerrainEditorPlugin::_on_noise_seed_changed);
	ClassDB::bind_method(D_METHOD("_on_erosion_talus_changed", "v"), &TerrainEditorPlugin::_on_erosion_talus_changed);
	ClassDB::bind_method(D_METHOD("_on_nav_mask_erase_toggled", "pressed"), &TerrainEditorPlugin::_on_nav_mask_erase_toggled);
	ClassDB::bind_method(D_METHOD("_on_bake_hlod_pressed"), &TerrainEditorPlugin::_on_bake_hlod_pressed);
	ClassDB::bind_method(D_METHOD("_on_clear_hlod_pressed"), &TerrainEditorPlugin::_on_clear_hlod_pressed);
	ClassDB::bind_method(D_METHOD("_on_bake_navmesh_pressed"), &TerrainEditorPlugin::_on_bake_navmesh_pressed);
	ClassDB::bind_method(D_METHOD("_on_clear_navmesh_pressed"), &TerrainEditorPlugin::_on_clear_navmesh_pressed);
	ClassDB::bind_method(D_METHOD("_on_normals_toggled", "pressed"), &TerrainEditorPlugin::_on_normals_toggled);
	ClassDB::bind_method(D_METHOD("_on_normal_length_changed", "v"), &TerrainEditorPlugin::_on_normal_length_changed);
	ClassDB::bind_method(D_METHOD("_on_navmesh_viz_toggled", "pressed"), &TerrainEditorPlugin::_on_navmesh_viz_toggled);
	ClassDB::bind_method(D_METHOD("_on_show_all_toggled", "pressed"), &TerrainEditorPlugin::_on_show_all_toggled);
	ClassDB::bind_method(D_METHOD("_on_pack_textures_pressed"), &TerrainEditorPlugin::_on_pack_textures_pressed);
}

#endif
