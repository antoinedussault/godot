#ifndef TERRAIN_EDITOR_PLUGIN_H
#define TERRAIN_EDITOR_PLUGIN_H
#ifdef TOOLS_ENABLED

#include "editor/editor_plugin.h"
#include "terrain_brush.h"

class CellTerrainManager;
class Camera;
class HBoxContainer;
class OptionButton;
class HSlider;

// Activates when a CellTerrainManager is selected in the 3D editor. Forwards
// left-mouse drags as sculpt strokes: raycasts the cursor onto the terrain and
// drives the TerrainBrush. Camera orbit (middle mouse) is left untouched.
class TerrainEditorPlugin : public EditorPlugin {
	GDCLASS(TerrainEditorPlugin, EditorPlugin);

	CellTerrainManager *manager = nullptr;
	Ref<TerrainBrush> brush;

	HBoxContainer *toolbar = nullptr;
	OptionButton *mode_select = nullptr;
	HSlider *radius_slider = nullptr;
	HSlider *strength_slider = nullptr;

	int brush_mode = 0;       // TerrainBrush::MODE_RAISE
	float brush_radius = 8.0f;
	float brush_strength = 2.0f;
	bool painting = false;

	bool _raycast(Camera *cam, const Point2 &screen, Vector3 &out_hit);

	void _on_mode_selected(int idx);
	void _on_radius_changed(float v);
	void _on_strength_changed(float v);

protected:
	static void _bind_methods();

public:
	virtual bool forward_spatial_gui_input(Camera *p_camera, const Ref<InputEvent> &p_event);
	virtual String get_name() const { return "CellTerrain"; }
	virtual bool has_main_screen() const { return false; }
	virtual void edit(Object *p_object);
	virtual bool handles(Object *p_object) const;
	virtual void make_visible(bool p_visible);

	TerrainEditorPlugin(EditorNode *p_node);
};

#endif // TOOLS_ENABLED
#endif // TERRAIN_EDITOR_PLUGIN_H
