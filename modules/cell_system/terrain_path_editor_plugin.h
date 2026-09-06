#ifndef TERRAIN_PATH_EDITOR_PLUGIN_H
#define TERRAIN_PATH_EDITOR_PLUGIN_H
#ifdef TOOLS_ENABLED

#include "editor/editor_plugin.h"

class TerrainPath;
class TerrainPathPoint;
class CellTerrainManager;
class Control;
class HBoxContainer;
class VBoxContainer;
class OptionButton;
class EditorSpinSlider;
class Button;
class ButtonGroup;
class Camera;

// Activates when a TerrainPath (or one of its TerrainPathPoint children) is
// selected. Existing points are always draggable via Godot's own normal
// node-selection + Move/Rotate/Scale gizmo (TerrainPathPoint is a real
// Spatial — see terrain_path_point_gizmo.h) — this plugin's own toolbar only
// gates Add/Remove-point clicks. The bottom dock exposes the deform
// parameters plus Refresh Baseline / Remove Effect — no Apply button since
// TerrainPath is always live.
class TerrainPathEditorPlugin : public EditorPlugin {
	GDCLASS(TerrainPathEditorPlugin, EditorPlugin);

	TerrainPath *path = nullptr;

	enum ToolMode {
		MODE_SELECT,
		MODE_ADD,
		MODE_REMOVE,
	};

	HBoxContainer *icon_toolbar = nullptr;
	Ref<ButtonGroup> mode_group;
	Button *mode_icon_buttons[3] = {};
	int tool_mode = MODE_SELECT;

	Control *dock_root = nullptr;
	VBoxContainer *dock_content = nullptr;

	EditorSpinSlider *width_spin = nullptr;
	EditorSpinSlider *falloff_width_spin = nullptr;
	OptionButton *falloff_select = nullptr;
	EditorSpinSlider *height_offset_spin = nullptr;
	EditorSpinSlider *strength_spin = nullptr;
	Button *refresh_baseline_button = nullptr;
	Button *remove_effect_button = nullptr;

	void _build_icon_toolbar();
	void _build_dock();
	bool _raycast(Camera *p_camera, const Point2 &p_screen, Vector3 &r_hit);
	void _add_point_at(const Vector3 &p_world_pos, int p_insert_index);
	void _on_tool_mode_pressed(int idx);
	void _on_width_changed(float v);
	void _on_falloff_width_changed(float v);
	void _on_falloff_selected(int idx);
	void _on_height_offset_changed(float v);
	void _on_strength_changed(float v);
	void _on_refresh_baseline_pressed();
	void _on_remove_effect_pressed();

protected:
	static void _bind_methods();

public:
	TerrainPath *get_edited_path() { return path; }

	virtual bool forward_spatial_gui_input(Camera *p_camera, const Ref<InputEvent> &p_event);
	virtual String get_name() const { return "TerrainPath"; }
	virtual bool has_main_screen() const { return false; }
	virtual bool handles(Object *p_object) const;
	virtual void edit(Object *p_object);
	virtual void make_visible(bool p_visible);

	TerrainPathEditorPlugin(EditorNode *p_node);
};

#endif
#endif
