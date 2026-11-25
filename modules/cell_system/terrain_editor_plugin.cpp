#include "terrain_editor_plugin.h"
#ifdef TOOLS_ENABLED

#include "cell_terrain.h"
#include "cell_terrain_layer.h"
#include "terrain_math_utils.h"
#include "cell_terrain_manager.h"
#include "core/image.h"
#include "core/os/os.h"
#include "editor/editor_node.h"
#include "editor/editor_spin_slider.h"
#include "editor/plugins/spatial_editor_plugin.h"
#include "editor_ui_helpers.h"
#include "texture_packer_dialog.h"
#include "scene/3d/camera.h"
#include "scene/resources/material.h"
#include "servers/visual_server.h"
#include "scene/resources/texture.h"
#include "scene/gui/base_button.h" // ButtonGroup
#include "scene/gui/box_container.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/color_picker.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/separator.h"
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

static Ref<ImageTexture> _icon_hole() {
	Ref<Image> img = cs_icon_new(16);
	img->lock();
	// A solid ground bar with a bite punched out of the middle - "remove the
	// terrain here", as opposed to Nav Mask's fenced square which only marks it.
	cs_icon_fill_rect(img, 2, 7, 4, 3, Color(1, 1, 1));
	cs_icon_fill_rect(img, 10, 7, 4, 3, Color(1, 1, 1));
	cs_icon_fill_circle(img, Vector2(8, 8.5f), 3.0f, Color(1, 1, 1, 0.35f));
	cs_icon_line(img, Vector2(6, 11.5f), Vector2(10, 11.5f), 1.2f, Color(1, 1, 1, 0.5f));
	img->unlock();
	return cs_icon_to_texture(img);
}

static Ref<ImageTexture> _icon_color() {
	Ref<Image> img = cs_icon_new(16);
	img->lock();
	// A paint blob - distinct from Paint's layer-stack icon because this tints
	// whatever is already there rather than choosing a layer.
	cs_icon_fill_circle(img, Vector2(7, 7), 4.5f, Color(1, 1, 1));
	cs_icon_fill_circle(img, Vector2(11.5f, 11.0f), 2.2f, Color(1, 1, 1, 0.55f));
	img->unlock();
	return cs_icon_to_texture(img);
}

TerrainEditorPlugin::TerrainEditorPlugin(EditorNode *p_node) {
	brush.instance();
	_build_icon_toolbar();
	_build_toolbar_settings();
}

void TerrainEditorPlugin::_build_icon_toolbar() {
	icon_toolbar = memnew(HBoxContainer);
	icon_toolbar->hide();

	mode_group.instance();
	static const char *mode_names[10] = { "Raise", "Lower", "Flatten", "Paint", "Smooth", "Noise", "Erode", "Nav Mask", "Hole", "Color" };
	Ref<ImageTexture> icons[10] = {
		_icon_raise(), _icon_lower(), _icon_flatten(), _icon_paint(),
		_icon_smooth(), _icon_noise(), _icon_erode(), _icon_nav_mask(),
		_icon_hole(), _icon_color()
	};
	for (int i = 0; i < 10; i++) {
		mode_icon_buttons[i] = cs_add_icon_mode_button(icon_toolbar, mode_group, icons[i], mode_names[i], this, "_on_mode_button_pressed", i);
	}
	mode_icon_buttons[0]->set_pressed(true);

	add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, icon_toolbar);
}

void TerrainEditorPlugin::_build_toolbar_settings() {
	// Everything below is appended to the SAME HBox as the mode icons, so it
	// all lives in the viewport's top bar and is shown/hidden as one unit by
	// make_visible().
	icon_toolbar->add_child(memnew(VSeparator));

	// --- Brush settings (always visible) ---
	radius_spin = cs_add_toolbar_slider(icon_toolbar, "Radius", 0.1, 512, 0.1, brush_radius, 120, this, "_on_radius_changed");
	strength_spin = cs_add_toolbar_slider(icon_toolbar, "Strength", 0.01, 1000, 0.01, brush_strength, 130, this, "_on_strength_changed");

	falloff_select = memnew(OptionButton);
	falloff_select->add_item("Smooth");    // FALLOFF_SMOOTH
	falloff_select->add_item("Linear");    // FALLOFF_LINEAR
	falloff_select->add_item("Spherical"); // FALLOFF_SPHERICAL
	falloff_select->add_item("Tip");       // FALLOFF_TIP
	falloff_select->set_tooltip("Falloff curve — shapes radial strength for every mode");
	falloff_select->connect("item_selected", this, "_on_falloff_selected");
	icon_toolbar->add_child(falloff_select);

	icon_toolbar->add_child(memnew(VSeparator));

	// --- Mode-specific (only one group is ever visible, see _update_toolbar_visibility) ---
	layer_select = memnew(OptionButton);
	layer_select->set_tooltip("Which texture layer to paint");
	layer_select->set_custom_minimum_size(Size2(140, 0));
	layer_select->connect("item_selected", this, "_on_layer_selected");
	icon_toolbar->add_child(layer_select);
	paint_min_angle_spin = cs_add_toolbar_slider(icon_toolbar, "Min Ang", 0.0, 90.0, 0.5, 0.0, 110, this, "_on_paint_min_angle_changed");
	paint_min_angle_spin->set_tooltip("Only paint where the terrain is at least this steep (degrees)");
	paint_max_angle_spin = cs_add_toolbar_slider(icon_toolbar, "Max Ang", 0.0, 90.0, 0.5, 90.0, 110, this, "_on_paint_max_angle_changed");
	paint_max_angle_spin->set_tooltip("Only paint where the terrain is at most this steep (degrees)");

	flatten_force_check = memnew(CheckBox);
	flatten_force_check->set_text("Force Height");
	flatten_force_check->set_pressed(false);
	flatten_force_check->connect("toggled", this, "_on_flatten_force_toggled");
	icon_toolbar->add_child(flatten_force_check);
	flatten_height_spin = cs_add_toolbar_slider(icon_toolbar, "Height", -1000, 1000, 0.5, 0, 120, this, "_on_flatten_height_changed");
	flatten_pick_button = memnew(Button);
	flatten_pick_button->set_text("Pick Height");
	flatten_pick_button->set_tooltip("Sample terrain height at cursor and use it as flatten target");
	flatten_pick_button->connect("pressed", this, "_on_flatten_pick_pressed");
	icon_toolbar->add_child(flatten_pick_button);

	noise_scale_spin = cs_add_toolbar_slider(icon_toolbar, "Scale", 0.1, 256, 0.1, 8.0, 110, this, "_on_noise_scale_changed");
	noise_seed_spin = cs_add_toolbar_slider(icon_toolbar, "Seed", 0, 9999, 1, 0, 110, this, "_on_noise_seed_changed");

	erosion_talus_spin = cs_add_toolbar_slider(icon_toolbar, "Talus", 0.0, 50.0, 0.05, 0.5, 110, this, "_on_erosion_talus_changed");

	nav_mask_erase_check = memnew(CheckBox);
	nav_mask_erase_check->set_text("Erase");
	nav_mask_erase_check->set_tooltip("Off: paint excludes this area from the navmesh (leaves a hole).\nOn: paint restores it to navigable.");
	nav_mask_erase_check->set_pressed(false);
	nav_mask_erase_check->connect("toggled", this, "_on_nav_mask_erase_toggled");
	icon_toolbar->add_child(nav_mask_erase_check);

	hole_erase_check = memnew(CheckBox);
	hole_erase_check->set_text("Fill");
	hole_erase_check->set_tooltip("Off: cut the terrain away (removes mesh, collision and navmesh).\nOn: fill the hole back in.");
	hole_erase_check->set_pressed(false);
	hole_erase_check->connect("toggled", this, "_on_hole_erase_toggled");
	icon_toolbar->add_child(hole_erase_check);

	paint_color_button = memnew(ColorPickerButton);
	paint_color_button->set_custom_minimum_size(Size2(60, 0));
	paint_color_button->set_pick_color(Color(1, 1, 1, 1));
	paint_color_button->set_tooltip("Tint multiplied over the blended terrain textures.\nWhite is neutral - paint white to erase a tint.");
	paint_color_button->connect("color_changed", this, "_on_paint_color_changed");
	icon_toolbar->add_child(paint_color_button);

	// Only meaningful while Show Normals is on, so it is gated on that rather
	// than on the brush mode like the controls above.
	normal_length_spin = cs_add_toolbar_slider(icon_toolbar, "Normal Len", 0.1, 32.0, 0.1, 1.0, 120, this, "_on_normal_length_changed");

	icon_toolbar->add_child(memnew(VSeparator));

	// --- Action groups, collapsed into popups ---
	textures_menu = memnew(MenuButton);
	textures_menu->set_text("Textures");
	textures_menu->get_popup()->add_item("Pack Textures...", 0);
	textures_menu->get_popup()->set_item_tooltip(0, "Combine separate Albedo/Height/Normal/Roughness images into the packed textures this terrain's shader expects");
	textures_menu->get_popup()->connect("id_pressed", this, "_on_textures_menu");
	icon_toolbar->add_child(textures_menu);

	nav_menu = memnew(MenuButton);
	nav_menu->set_text("Navigation");
	nav_menu->get_popup()->add_item("Bake Navmesh", 0);
	nav_menu->get_popup()->set_item_tooltip(0, "Build persistent navmesh regions for every saved cell outside the live streaming radius, so AI can path across the whole world, not just near the player. Skips cells that are already active (they keep their own live navmesh).");
	nav_menu->get_popup()->add_item("Clear Baked Navmesh", 1);
	nav_menu->get_popup()->connect("id_pressed", this, "_on_nav_menu");
	icon_toolbar->add_child(nav_menu);

	hlod_menu = memnew(MenuButton);
	hlod_menu->set_text("HLOD");
	hlod_menu->get_popup()->add_item("Bake HLOD", 0);
	hlod_menu->get_popup()->add_item("Clear HLOD", 1);
	hlod_menu->get_popup()->set_item_tooltip(1, "Delete all baked HLOD chunks (mesh + splatmaps) from disk and unload them");
	hlod_menu->get_popup()->connect("id_pressed", this, "_on_hlod_menu");
	icon_toolbar->add_child(hlod_menu);

	debug_menu = memnew(MenuButton);
	debug_menu->set_text("Debug");
	PopupMenu *dbg = debug_menu->get_popup();
	dbg->add_check_item("Show Normals", 0);
	dbg->add_check_item("Show Navmesh", 1);
	dbg->set_item_tooltip(1, "Draw each streamed cell's navigation mesh in the viewport. Independent of Godot's built-in \"Visible Navigation\" debug option (which only draws NavigationRegion nodes, not this module's raw NavigationServer regions).");
	dbg->add_check_item("Show All Edited Terrain", 2);
	dbg->add_separator();
	dbg->add_check_item("Trace Slow Operations", 3);
	dbg->set_item_tooltip(dbg->get_item_index(3),
			"Log any cell_system call slower than 30ms to the Output panel.\n"
			"Use this to find what is stalling the editor - it reports\n"
			"brush input, streaming, navmesh and HLOD work separately.");
	dbg->set_item_tooltip(2, "Keep every saved cell loaded regardless of streaming radius, so the whole sculpted world stays visible while editing. Editor only — has no effect at runtime.");
	dbg->connect("id_pressed", this, "_on_debug_menu");
	icon_toolbar->add_child(debug_menu);

	icon_toolbar->add_child(memnew(VSeparator));
	status_label = memnew(Label);
	status_label->set_text("");
	// Fixed width + clipping: a Label that grows with its text re-lays-out the
	// whole toolbar every time the number changes.
	status_label->set_custom_minimum_size(Size2(130, 0));
	status_label->set_clip_text(true);
	icon_toolbar->add_child(status_label);

	pack_dialog = memnew(TexturePackerDialog);
	EditorNode::get_singleton()->get_gui_base()->add_child(pack_dialog);

	_update_toolbar_visibility();
}

// Layer names come from the CellTerrainLayer resources; a layer with no name
// set still needs to be pickable, hence the index fallback.
void TerrainEditorPlugin::_populate_layer_select() {
	if (!layer_select) return;
	int previous = layer_select->get_selected();
	layer_select->clear();

	if (manager) {
		Array layers = manager->get_layers();
		for (int i = 0; i < layers.size(); i++) {
			Ref<CellTerrainLayer> l = layers[i];
			String name;
			if (l.is_valid()) name = l->get_layer_name();
			if (name.empty()) name = "Layer " + itos(i);
			layer_select->add_item(name, i);
		}
	}
	if (layer_select->get_item_count() == 0) {
		// No manager or no layers configured yet — keep the control usable
		// rather than showing an empty popup.
		layer_select->add_item("(no layers)", 0);
		layer_select->set_disabled(true);
	} else {
		layer_select->set_disabled(false);
	}

	if (previous >= 0 && previous < layer_select->get_item_count()) {
		layer_select->select(previous);
	} else {
		layer_select->select(0);
	}
	brush->set_paint_layer(layer_select->get_selected());
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
		if (debug_menu) {
			PopupMenu *dbg = debug_menu->get_popup();
			int idx = dbg->get_item_index(2);
			if (idx >= 0) dbg->set_item_checked(idx, manager->get_show_all_in_editor());
		}
	}
	// Layer names belong to the manager being edited, so the picker can only be
	// filled in once we have one.
	_populate_layer_select();
	_update_toolbar_visibility();
}

void TerrainEditorPlugin::make_visible(bool p_visible) {
	// One call covers the whole bar: the settings are children of icon_toolbar.
	if (icon_toolbar) icon_toolbar->set_visible(p_visible);
	if (p_visible) {
		set_process(true);
	} else {
		set_process(false);
		cached_camera = nullptr;
		_last_stream_pos = Vector3(1e9f, 1e9f, 1e9f);
		// Full teardown here, not just hide(): the overlay instance lives in
		// the edited scene's scenario, and that scenario dies with the scene.
		brush_viz.destroy();
		_has_terrain_hit = false;
		if (manager) {
			manager->set_debug_normals(false, 1.0f);
			manager->set_debug_navmesh(false);
			// Do NOT call editor_clear() — keep terrain cells alive so
			// re-selecting the manager doesn't force a full reload.
			manager = nullptr;
			brush->set_manager(nullptr);
			painting = false;
		}
		// The debug overlays were switched off on the manager just above, so
		// clear their checkmarks to match.
		if (debug_menu) {
			PopupMenu *dbg = debug_menu->get_popup();
			int n_idx = dbg->get_item_index(0);
			if (n_idx >= 0) dbg->set_item_checked(n_idx, false);
			int v_idx = dbg->get_item_index(1);
			if (v_idx >= 0) dbg->set_item_checked(v_idx, false);
			_update_toolbar_visibility();
		}
	}
}

void TerrainEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_PROCESS) {
		uint64_t _tr = cs_trace_enabled() ? OS::get_singleton()->get_ticks_usec() : 0;
		if (manager && cached_camera) {
			Vector3 cam_pos = cached_camera->get_global_transform().origin;
			if (cam_pos.distance_squared_to(_last_stream_pos) > 0.01f) {
				_last_stream_pos = cam_pos;
				manager->stream_to(cam_pos);
			}
		}
		if (_tr) cs_trace_report("plugin_process(stream_to)", _tr);

		// Streaming is async, so the editor stays responsive and this label can
		// actually paint. Sampled at 4Hz rather than per frame: get_pending_count()
		// takes the streaming mutex (contending with the worker) and changing the
		// text re-lays-out the toolbar, so doing it every frame made streaming
		// measurably slower - the readout cost more than it reported.
		_status_timer -= get_process_delta_time();
		if (status_label && _status_timer <= 0.0f) {
			_status_timer = 0.25f;
			int pending = manager ? manager->get_pending_count() : 0;
			if (pending != _status_shown) {
				_status_shown = pending;
				// Never hidden and never resized - visibility and width changes
				// are what dirty the parent HBox. Blank text instead.
				status_label->set_text(pending > 0 ? vformat("Streaming %d...", pending) : String());
			}
		}

		// Debounced HLOD re-bake: once editing has settled, refresh only the
		// chunks whose cells changed so distant terrain stays in sync.
		if (_hlod_rebake_timer > 0.0f && !painting) {
			_hlod_rebake_timer -= get_process_delta_time();
			if (_hlod_rebake_timer <= 0.0f) {
				_hlod_rebake_timer = 0.0f;
				if (manager && manager->has_dirty_hlod()) {
					uint64_t _trh = cs_trace_enabled() ? OS::get_singleton()->get_ticks_usec() : 0;
					manager->rebake_dirty_hlod();
					if (_trh) cs_trace_report("rebake_dirty_hlod", _trh);
				}
			}
		}
	}
}

bool TerrainEditorPlugin::_raycast(Camera *cam, const Point2 &screen, Vector3 &out_hit) {
	if (!manager) return false;
	Vector3 from = cam->project_ray_origin(screen);
	Vector3 dir = cam->project_ray_normal(screen);
	// Heightmap march, not a physics ray. The old path fired intersect_ray()
	// out to 100,000 units on EVERY mouse-move; rays that missed the terrain
	// walked heightfield grid cells the whole way and froze the editor.
	return manager->raycast_terrain(from, dir, out_hit);
}

void TerrainEditorPlugin::_clear_brush_viz() {
	// Hide, never destroy. This runs on every mouse-move that misses the
	// terrain - which is every move towards the toolbar, since the ray goes
	// to the sky - and the old version deleted a node out of the edited
	// scene each time, rebuilding the whole Scene dock with it.
	brush_viz.hide();
	_has_terrain_hit = false;
}

// Distance from the brush centre, as a fraction of the radius, at which the
// selected falloff curve has dropped to half strength. Solved numerically from
// terrain_falloff() itself rather than hardcoded per curve, so the ring stays
// truthful if those curves are ever retuned. terrain_falloff takes t01 = 1 at
// the centre and 0 at the edge, and is monotonic, so a plain bisection works.
static float cs_falloff_half_radius(int p_type) {
	float lo = 0.0f, hi = 1.0f;
	for (int i = 0; i < 24; i++) {
		float mid = (lo + hi) * 0.5f;
		if (terrain_falloff(1.0f - mid, p_type) > 0.5f) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	return (lo + hi) * 0.5f;
}

void TerrainEditorPlugin::_update_brush_viz(const Vector3 &world_hit) {
	uint64_t _tr_v = cs_trace_enabled() ? OS::get_singleton()->get_ticks_usec() : 0;
	struct _TrV {
		uint64_t t;
		~_TrV() { if (t) cs_trace_report("_update_brush_viz", t); }
	} _tr_v_guard = { _tr_v };
	(void)_tr_v_guard;
	if (!manager) { _clear_brush_viz(); return; }

	_last_terrain_hit = world_hit;
	_has_terrain_hit = true;

	if (_flatten_height_picked) {
		float h = manager->get_height_at_world(world_hit.x, world_hit.z);
		brush->set_flatten_height(h);
		if (flatten_height_spin) flatten_height_spin->set_value(h);
	}


	VisualServer *vs = VisualServer::get_singleton();
	const RID im = brush_viz.begin_rebuild(manager);
	if (!im.is_valid()) {
		return;
	}

	const Vector3 org = manager->get_global_transform().origin;
	const float bias = 0.06f;
	const int segs = 64;

	// Outer ring — white with light blue tint, UE4-style
	const Color outer(0.75f, 0.92f, 1.0f, 1.0f);
	// Inner falloff ring — dimmer blue
	const Color inner(0.25f, 0.60f, 1.0f, 0.55f);

	// --- Outer ring at full brush radius ---
	vs->immediate_begin(im, VisualServer::PRIMITIVE_LINE_LOOP);
	vs->immediate_color(im, outer);
	for (int i = 0; i < segs; i++) {
		float a = Math_TAU * i / segs;
		float wx = world_hit.x + Math::cos(a) * brush_radius;
		float wz = world_hit.z + Math::sin(a) * brush_radius;
		float wy = manager->get_height_at_world(wx, wz) + bias;
		vs->immediate_vertex(im, Vector3(wx - org.x, wy, wz - org.z));
	}
	vs->immediate_end(im);

	// --- Inner ring where the falloff reaches half strength ---
	// Depends on the curve: Linear and Smooth land at 0.5 of the radius, but
	// Spherical is much wider (~0.87) and Tip much tighter (~0.29), so a fixed
	// 50%% ring misrepresented three of the four curves.
	float inner_r = brush_radius * cs_falloff_half_radius(brush->get_falloff_type());
	vs->immediate_begin(im, VisualServer::PRIMITIVE_LINE_LOOP);
	vs->immediate_color(im, inner);
	for (int i = 0; i < segs; i++) {
		float a = Math_TAU * i / segs;
		float wx = world_hit.x + Math::cos(a) * inner_r;
		float wz = world_hit.z + Math::sin(a) * inner_r;
		float wy = manager->get_height_at_world(wx, wz) + bias;
		vs->immediate_vertex(im, Vector3(wx - org.x, wy, wz - org.z));
	}
	vs->immediate_end(im);

	// --- Center cross ---
	float arm = MAX(brush_radius * 0.06f, 0.25f);
	float cx = world_hit.x - org.x;
	float cz_l = world_hit.z - org.z;
	float cy = manager->get_height_at_world(world_hit.x, world_hit.z) + bias;
	vs->immediate_begin(im, VisualServer::PRIMITIVE_LINES);
	vs->immediate_color(im, outer);
	vs->immediate_vertex(im, Vector3(cx - arm, cy, cz_l));
	vs->immediate_vertex(im, Vector3(cx + arm, cy, cz_l));
	vs->immediate_vertex(im, Vector3(cx, cy, cz_l - arm));
	vs->immediate_vertex(im, Vector3(cx, cy, cz_l + arm));
	vs->immediate_end(im);
}

bool TerrainEditorPlugin::forward_spatial_gui_input(Camera *p_camera, const Ref<InputEvent> &p_event) {
	uint64_t _tr_in = cs_trace_enabled() ? OS::get_singleton()->get_ticks_usec() : 0;
	struct _TrIn {
		uint64_t t;
		~_TrIn() { if (t) cs_trace_report("forward_spatial_gui_input", t); }
	} _tr_in_guard = { _tr_in };
	(void)_tr_in_guard;
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
void TerrainEditorPlugin::_on_radius_changed(float v) {
	brush_radius = v;
	// Redraw the on-terrain ring at the new size straight away. The viz is
	// otherwise only rebuilt on mouse-move over the terrain, so dragging the
	// slider showed no change until you moved the cursor — which is exactly
	// when you want to see the size you are picking.
	if (_has_terrain_hit) _update_brush_viz(_last_terrain_hit);
}
void TerrainEditorPlugin::_on_strength_changed(float v) { brush_strength = v; }
void TerrainEditorPlugin::_on_falloff_selected(int idx) {
	brush->set_falloff_type(idx);
	// The inner ring encodes the curve now, so it has to be rebuilt here too.
	if (_has_terrain_hit) _update_brush_viz(_last_terrain_hit);
}
void TerrainEditorPlugin::_on_layer_selected(int idx) { brush->set_paint_layer(idx); }
void TerrainEditorPlugin::_on_paint_min_angle_changed(float v) {
	brush->set_paint_min_angle(v);
	// Keep the pair ordered so an inverted range cannot silently paint nothing.
	if (paint_max_angle_spin && v > paint_max_angle_spin->get_value()) {
		paint_max_angle_spin->set_value(v);
	}
}
void TerrainEditorPlugin::_on_paint_max_angle_changed(float v) {
	brush->set_paint_max_angle(v);
	if (paint_min_angle_spin && v < paint_min_angle_spin->get_value()) {
		paint_min_angle_spin->set_value(v);
	}
}

void TerrainEditorPlugin::_on_textures_menu(int id) {
	if (id == 0) _on_pack_textures_pressed();
}
void TerrainEditorPlugin::_on_nav_menu(int id) {
	if (id == 0) _on_bake_navmesh_pressed();
	else if (id == 1) _on_clear_navmesh_pressed();
}
void TerrainEditorPlugin::_on_hlod_menu(int id) {
	if (id == 0) _on_bake_hlod_pressed();
	else if (id == 1) _on_clear_hlod_pressed();
}
void TerrainEditorPlugin::_on_debug_menu(int id) {
	// Check items do not toggle themselves — flip the state, then reuse the
	// same handlers the old checkboxes called.
	PopupMenu *dbg = debug_menu->get_popup();
	int idx = dbg->get_item_index(id);
	if (idx < 0) return;
	bool on = !dbg->is_item_checked(idx);
	dbg->set_item_checked(idx, on);
	switch (id) {
		case 0: _on_normals_toggled(on); break;
		case 1: _on_navmesh_viz_toggled(on); break;
		case 2: _on_show_all_toggled(on); break;
		case 3: {
			cs_trace_set_enabled(on);
			print_line(on
					? "[cell-trace] enabled - slow cell_system calls (>30ms) will be logged here."
					: "[cell-trace] disabled.");
		} break;
	}
	_update_toolbar_visibility();
}
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
void TerrainEditorPlugin::_on_hole_erase_toggled(bool pressed) { brush->set_hole_erase(pressed); }
void TerrainEditorPlugin::_on_paint_color_changed(const Color &color) { brush->set_paint_color(color); }

// Shows the editor progress dialog and forces one refresh BEFORE the caller
// starts blocking, so the dialog is actually on screen during the freeze
// rather than appearing after it. progress_task_step(force_refresh) pumps the
// UI, which a plain Label cannot do from a blocked main thread.
void TerrainEditorPlugin::_run_with_progress(const String &p_task, const String &p_label) {
	EditorNode::progress_add_task(p_task, p_label, 0);
	EditorNode::progress_task_step(p_task, p_label, 0, true);
}

void TerrainEditorPlugin::_on_bake_hlod_pressed() {
	if (manager) {
		_run_with_progress("cs_bake_hlod", "Baking HLOD chunks...");
		manager->save_dirty();
		manager->bake_hlod();
		EditorNode::progress_end_task("cs_bake_hlod");
	}
}

void TerrainEditorPlugin::_on_clear_hlod_pressed() {
	if (manager) {
		_run_with_progress("cs_clear_hlod", "Clearing baked HLOD...");
		manager->clear_hlod();
		EditorNode::progress_end_task("cs_clear_hlod");
	}
}

void TerrainEditorPlugin::_on_bake_navmesh_pressed() {
	if (manager) {
		_run_with_progress("cs_bake_nav", "Baking navmesh...");
		manager->save_dirty();
		manager->bake_navmesh();
		EditorNode::progress_end_task("cs_bake_nav");
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
	if (!manager || !debug_menu) return;
	PopupMenu *dbg = debug_menu->get_popup();
	int idx = dbg->get_item_index(0); // Show Normals
	if (idx >= 0 && dbg->is_item_checked(idx))
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
	bool is_hole = (brush_mode == TerrainBrush::MODE_HOLE);
	bool is_color = (brush_mode == TerrainBrush::MODE_COLOR);
	if (layer_select) layer_select->set_visible(is_paint);
	if (paint_min_angle_spin) paint_min_angle_spin->set_visible(is_paint);
	if (paint_max_angle_spin) paint_max_angle_spin->set_visible(is_paint);
	if (flatten_force_check) flatten_force_check->set_visible(is_flatten);
	if (flatten_height_spin) flatten_height_spin->set_visible(is_flatten);
	if (flatten_pick_button) flatten_pick_button->set_visible(is_flatten);
	if (noise_scale_spin) noise_scale_spin->set_visible(is_noise);
	if (noise_seed_spin) noise_seed_spin->set_visible(is_noise);
	if (erosion_talus_spin) erosion_talus_spin->set_visible(is_erode);
	if (nav_mask_erase_check) nav_mask_erase_check->set_visible(is_nav_mask);
	if (hole_erase_check) hole_erase_check->set_visible(is_hole);
	if (paint_color_button) paint_color_button->set_visible(is_color);
	// Normal Length is tied to the Show Normals debug toggle, not to a mode —
	// keeping it in the bar unconditionally would waste space most of the time.
	if (normal_length_spin && debug_menu) {
		PopupMenu *dbg = debug_menu->get_popup();
		int idx = dbg->get_item_index(0);
		normal_length_spin->set_visible(idx >= 0 && dbg->is_item_checked(idx));
	}
	// Falloff curve shapes the radial strength for every mode, including Paint.
}

void TerrainEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_mode_button_pressed", "idx"), &TerrainEditorPlugin::_on_mode_button_pressed);
	ClassDB::bind_method(D_METHOD("_on_radius_changed", "v"), &TerrainEditorPlugin::_on_radius_changed);
	ClassDB::bind_method(D_METHOD("_on_strength_changed", "v"), &TerrainEditorPlugin::_on_strength_changed);
	ClassDB::bind_method(D_METHOD("_on_falloff_selected", "idx"), &TerrainEditorPlugin::_on_falloff_selected);
	ClassDB::bind_method(D_METHOD("_on_layer_selected", "idx"), &TerrainEditorPlugin::_on_layer_selected);
	ClassDB::bind_method(D_METHOD("_on_paint_min_angle_changed", "v"), &TerrainEditorPlugin::_on_paint_min_angle_changed);
	ClassDB::bind_method(D_METHOD("_on_paint_max_angle_changed", "v"), &TerrainEditorPlugin::_on_paint_max_angle_changed);
	ClassDB::bind_method(D_METHOD("_on_textures_menu", "id"), &TerrainEditorPlugin::_on_textures_menu);
	ClassDB::bind_method(D_METHOD("_on_nav_menu", "id"), &TerrainEditorPlugin::_on_nav_menu);
	ClassDB::bind_method(D_METHOD("_on_hlod_menu", "id"), &TerrainEditorPlugin::_on_hlod_menu);
	ClassDB::bind_method(D_METHOD("_on_debug_menu", "id"), &TerrainEditorPlugin::_on_debug_menu);
	ClassDB::bind_method(D_METHOD("_on_flatten_force_toggled", "pressed"), &TerrainEditorPlugin::_on_flatten_force_toggled);
	ClassDB::bind_method(D_METHOD("_on_flatten_height_changed", "v"), &TerrainEditorPlugin::_on_flatten_height_changed);
	ClassDB::bind_method(D_METHOD("_on_flatten_pick_pressed"), &TerrainEditorPlugin::_on_flatten_pick_pressed);
	ClassDB::bind_method(D_METHOD("_on_noise_scale_changed", "v"), &TerrainEditorPlugin::_on_noise_scale_changed);
	ClassDB::bind_method(D_METHOD("_on_noise_seed_changed", "v"), &TerrainEditorPlugin::_on_noise_seed_changed);
	ClassDB::bind_method(D_METHOD("_on_erosion_talus_changed", "v"), &TerrainEditorPlugin::_on_erosion_talus_changed);
	ClassDB::bind_method(D_METHOD("_on_nav_mask_erase_toggled", "pressed"), &TerrainEditorPlugin::_on_nav_mask_erase_toggled);
	ClassDB::bind_method(D_METHOD("_on_hole_erase_toggled", "pressed"), &TerrainEditorPlugin::_on_hole_erase_toggled);
	ClassDB::bind_method(D_METHOD("_on_paint_color_changed", "color"), &TerrainEditorPlugin::_on_paint_color_changed);
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
