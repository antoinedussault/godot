#ifndef FOLIAGE_EDITOR_PLUGIN_H
#define FOLIAGE_EDITOR_PLUGIN_H
#ifdef TOOLS_ENABLED

#include "editor/editor_plugin.h"
#include "core/map.h"
#include "core/pool_vector.h"
#include "tree_importer.h"

class FoliageManager;
class Camera;
class Control;
class VBoxContainer;
class GridContainer;
class OptionButton;
class EditorSpinSlider;
class ButtonGroup;
class Button;

// Activates when a FoliageManager is selected. Left dock panel for painting
// foliage: a mode grid (Scatter / Single / Erase), a type picker, and
// radius/strength sliders. Mirrors TerrainEditorPlugin's dock structure and raycast.
class FoliageEditorPlugin : public EditorPlugin {
	GDCLASS(FoliageEditorPlugin, EditorPlugin);

	enum Mode { MODE_SCATTER, MODE_SINGLE, MODE_ERASE };

	FoliageManager *manager = nullptr;

	Control *dock_root = nullptr;
	VBoxContainer *dock_content = nullptr;

	GridContainer *mode_grid = nullptr;
	Ref<ButtonGroup> mode_group;
	Button *mode_buttons[3] = {};
	OptionButton *type_select = nullptr;
	EditorSpinSlider *radius_spin = nullptr;
	EditorSpinSlider *strength_spin = nullptr;

	int mode = MODE_SCATTER;
	int type_index = 0;
	float radius = 8.0f;
	float strength = 0.5f;
	bool painting = false;

	Camera *cached_camera = nullptr;

	Map<int64_t, PoolRealArray> _stroke_before;
	Ref<TreeImporter> _tree_importer;

	void _build_dock();
	bool _raycast(Camera *cam, const Point2 &screen, Vector3 &out_hit, Vector3 &out_normal);
	void _refresh_type_list();
	void _apply_at(const Vector3 &hit, const Vector3 &normal);
	void _snapshot_cells(const Vector3 &hit);
	void _end_stroke();
	void _on_mode_button_pressed(int idx);
	void _on_type_selected(int idx);
	void _on_radius_changed(float v);
	void _on_strength_changed(float v);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	virtual bool forward_spatial_gui_input(Camera *p_camera, const Ref<InputEvent> &p_event);
	virtual String get_name() const { return "Foliage"; }
	virtual bool has_main_screen() const { return false; }
	virtual void edit(Object *p_object);
	virtual bool handles(Object *p_object) const;
	virtual void make_visible(bool p_visible);

	FoliageEditorPlugin(EditorNode *p_node);
};

#endif // TOOLS_ENABLED
#endif // FOLIAGE_EDITOR_PLUGIN_H
