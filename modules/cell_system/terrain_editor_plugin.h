#ifndef TERRAIN_EDITOR_PLUGIN_H
#define TERRAIN_EDITOR_PLUGIN_H
#ifdef TOOLS_ENABLED

#include "editor/editor_plugin.h"
#include "scene/gui/check_box.h"
#include "editor_ui_helpers.h"
#include "terrain_brush.h"

class CellTerrainManager;
class Camera;
class Control;
class HBoxContainer;
class VBoxContainer;
class OptionButton;
class EditorSpinSlider;
class ButtonGroup;
class Button;
class TexturePackerDialog;
class MenuButton;
class ColorPickerButton;

class TerrainEditorPlugin : public EditorPlugin {
	GDCLASS(TerrainEditorPlugin, EditorPlugin);

	CellTerrainManager *manager = nullptr;
	Ref<TerrainBrush> brush;

	// The plugin's entire UI, in the viewport's top toolbar (native Godot style —
	// same row as the Select/Move/Rotate tools): mode icons, then brush
	// settings, then the mode-specific controls, then the action menus. There
	// is no bottom panel any more, so make_visible() showing/hiding this one
	// HBox covers the whole UI.
	HBoxContainer *icon_toolbar = nullptr;
	Ref<ButtonGroup> mode_group;
	Button *mode_icon_buttons[10] = {};

	EditorSpinSlider *radius_spin = nullptr;
	EditorSpinSlider *strength_spin = nullptr;
	OptionButton *falloff_select = nullptr;
	// Paint target picked by NAME (from each CellTerrainLayer) rather than a
	// bare index — the index is still what gets painted, it is just not what
	// you have to recognise the layer by. Repopulated in edit().
	OptionButton *layer_select = nullptr;
	// Paint-mode slope filter (see TerrainBrush::paint_min_angle).
	EditorSpinSlider *paint_min_angle_spin = nullptr;
	EditorSpinSlider *paint_max_angle_spin = nullptr;
	class CheckBox *flatten_force_check = nullptr;
	EditorSpinSlider *flatten_height_spin = nullptr;
	Button *flatten_pick_button = nullptr;
	EditorSpinSlider *noise_scale_spin = nullptr;
	EditorSpinSlider *noise_seed_spin = nullptr;
	EditorSpinSlider *erosion_talus_spin = nullptr;
	CheckBox *nav_mask_erase_check = nullptr;
	CheckBox *hole_erase_check = nullptr;
	ColorPickerButton *paint_color_button = nullptr;
	EditorSpinSlider *normal_length_spin = nullptr;
	// Streaming readout in the toolbar. Only meaningful for ASYNC work: while
	// the main thread is blocked nothing repaints, so genuinely blocking
	// operations use the editor progress dialog instead (see _run_with_progress).
	class Label *status_label = nullptr;
	int _status_shown = -1;
	// Throttle. Updating the label every frame re-laid-out the whole spatial
	// editor toolbar while cells were streaming - the readout was costing more
	// than the thing it was reporting on.
	float _status_timer = 0.0f;
	// Action groups live in popups rather than as a row of buttons: the spatial
	// editor's top bar is shared with the transform/view menus, so anything
	// that is not a per-stroke setting gets collapsed to keep the bar usable.
	MenuButton *textures_menu = nullptr;
	MenuButton *nav_menu = nullptr;
	MenuButton *hlod_menu = nullptr;
	MenuButton *debug_menu = nullptr;
	TexturePackerDialog *pack_dialog = nullptr;

	int brush_mode = 0;
	float brush_radius = 8.0f;
	float brush_strength = 2.0f;
	bool painting = false;

	Camera *cached_camera = nullptr;
	// Drawn straight through the VisualServer, never parented into the
	// edited scene - see CSViewportOverlay for why that matters here.
	CSViewportOverlay brush_viz;
	Vector3 _last_stream_pos = Vector3(1e9f, 1e9f, 1e9f);
	Vector3 _last_terrain_hit;
	bool _has_terrain_hit = false;
	bool _flatten_stroke_locked = false;
	bool _flatten_height_picked = false; // Pick was clicked; activate Force on next left-click

	// Debounce timer for auto re-baking HLOD chunks after editing settles.
	// Set on stroke end; counts down in _notification; fires rebake_dirty_hlod().
	float _hlod_rebake_timer = 0.0f;

	void _build_icon_toolbar();
	// Everything that used to be in the bottom "Terrain" panel now lives in the
	// viewport's top bar next to the mode icons.
	void _build_toolbar_settings();
	void _populate_layer_select();
	bool _raycast(Camera *cam, const Point2 &screen, Vector3 &out_hit);
	void _update_brush_viz(const Vector3 &world_hit);
	void _clear_brush_viz();
	void _on_mode_button_pressed(int idx);
	void _on_radius_changed(float v);
	void _on_strength_changed(float v);
	void _on_falloff_selected(int idx);
	void _on_layer_selected(int idx);
	void _on_paint_min_angle_changed(float v);
	void _on_paint_max_angle_changed(float v);
	void _on_flatten_force_toggled(bool pressed);
	void _on_flatten_height_changed(float v);
	void _on_flatten_pick_pressed();
	void _on_noise_scale_changed(float v);
	void _on_noise_seed_changed(float v);
	void _on_erosion_talus_changed(float v);
	void _on_nav_mask_erase_toggled(bool pressed);
	void _on_hole_erase_toggled(bool pressed);
	void _on_paint_color_changed(const Color &color);
	void _update_toolbar_visibility();
	void _on_bake_hlod_pressed();
	void _on_clear_hlod_pressed();
	void _on_bake_navmesh_pressed();
	void _on_clear_navmesh_pressed();
	void _on_normals_toggled(bool pressed);
	void _on_normal_length_changed(float v);
	void _on_navmesh_viz_toggled(bool pressed);
	void _on_show_all_toggled(bool pressed);
	void _on_pack_textures_pressed();
	// Wraps a blocking call in the editor progress dialog so the user gets
	// feedback instead of an apparently hung editor.
	void _run_with_progress(const String &p_task, const String &p_label);
	// Popup dispatchers — these just fan out to the handlers above, so the
	// actual behaviour stays in one place whatever the UI looks like.
	void _on_textures_menu(int id);
	void _on_nav_menu(int id);
	void _on_hlod_menu(int id);
	void _on_debug_menu(int id);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	virtual bool forward_spatial_gui_input(Camera *p_camera, const Ref<InputEvent> &p_event);
	virtual String get_name() const { return "CellTerrain"; }
	virtual bool has_main_screen() const { return false; }
	virtual void edit(Object *p_object);
	virtual bool handles(Object *p_object) const;
	virtual void make_visible(bool p_visible);

	TerrainEditorPlugin(EditorNode *p_node);
};

#endif
#endif
