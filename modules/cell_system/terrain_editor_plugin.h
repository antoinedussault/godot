#ifndef TERRAIN_EDITOR_PLUGIN_H
#define TERRAIN_EDITOR_PLUGIN_H
#ifdef TOOLS_ENABLED

#include "editor/editor_plugin.h"
#include "scene/gui/check_box.h"
#include "terrain_brush.h"

class CellTerrainManager;
class Camera;
class Control;
class HBoxContainer;
class VBoxContainer;
class ImmediateGeometry;
class OptionButton;
class EditorSpinSlider;
class ButtonGroup;
class Button;
class TexturePackerDialog;

class TerrainEditorPlugin : public EditorPlugin {
	GDCLASS(TerrainEditorPlugin, EditorPlugin);

	CellTerrainManager *manager = nullptr;
	Ref<TerrainBrush> brush;

	// Icon-only tool picker in the viewport's top toolbar (native Godot style —
	// same row as the Select/Move/Rotate tools), independent of the bottom dock.
	HBoxContainer *icon_toolbar = nullptr;
	Ref<ButtonGroup> mode_group;
	Button *mode_icon_buttons[8] = {};

	// Bottom dock panel (grouped settings), added once in the constructor and
	// shown/hidden per selection in make_visible() rather than rebuilt each time.
	Control *dock_root = nullptr;
	VBoxContainer *dock_content = nullptr;

	EditorSpinSlider *radius_spin = nullptr;
	EditorSpinSlider *strength_spin = nullptr;
	OptionButton *falloff_select = nullptr;
	EditorSpinSlider *layer_spin = nullptr;
	class CheckBox *flatten_force_check = nullptr;
	EditorSpinSlider *flatten_height_spin = nullptr;
	Button *flatten_pick_button = nullptr;
	EditorSpinSlider *noise_scale_spin = nullptr;
	EditorSpinSlider *noise_seed_spin = nullptr;
	EditorSpinSlider *erosion_talus_spin = nullptr;
	CheckBox *nav_mask_erase_check = nullptr;
	Button *bake_hlod_button = nullptr;
	Button *clear_hlod_button = nullptr;
	Button *bake_navmesh_button = nullptr;
	Button *clear_navmesh_button = nullptr;
	CheckBox *normals_check = nullptr;
	EditorSpinSlider *normal_length_spin = nullptr;
	CheckBox *navmesh_viz_check = nullptr;
	CheckBox *show_all_check = nullptr;
	Button *pack_textures_button = nullptr;
	TexturePackerDialog *pack_dialog = nullptr;

	int brush_mode = 0;
	float brush_radius = 8.0f;
	float brush_strength = 2.0f;
	bool painting = false;

	Camera *cached_camera = nullptr;
	ImmediateGeometry *brush_viz = nullptr;
	Vector3 _last_stream_pos = Vector3(1e9f, 1e9f, 1e9f);
	Vector3 _last_terrain_hit;
	bool _has_terrain_hit = false;
	bool _flatten_stroke_locked = false;
	bool _flatten_height_picked = false; // Pick was clicked; activate Force on next left-click

	// Debounce timer for auto re-baking HLOD chunks after editing settles.
	// Set on stroke end; counts down in _notification; fires rebake_dirty_hlod().
	float _hlod_rebake_timer = 0.0f;

	void _build_icon_toolbar();
	void _build_dock();
	bool _raycast(Camera *cam, const Point2 &screen, Vector3 &out_hit);
	void _update_brush_viz(const Vector3 &world_hit);
	void _clear_brush_viz();
	void _on_mode_button_pressed(int idx);
	void _on_radius_changed(float v);
	void _on_strength_changed(float v);
	void _on_falloff_selected(int idx);
	void _on_layer_changed(float v);
	void _on_flatten_force_toggled(bool pressed);
	void _on_flatten_height_changed(float v);
	void _on_flatten_pick_pressed();
	void _on_noise_scale_changed(float v);
	void _on_noise_seed_changed(float v);
	void _on_erosion_talus_changed(float v);
	void _on_nav_mask_erase_toggled(bool pressed);
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
