#ifndef CELL_GRID_VISUALIZER_H
#define CELL_GRID_VISUALIZER_H

#include "scene/3d/spatial.h"
#include "scene/3d/immediate_geometry.h"
#include "scene/resources/material.h"

class CellTerrainManager;
class CellWorld;
class CellPlayerBody;

// Draws a grid of cell boundaries in the 3D viewport. Add it to your scene
// alongside the CellTerrainManager / CellWorld and it auto-detects them.
//
// Grid lines show cell edges. The cell the player occupies is highlighted.
// Loaded/active cells get a different color from unloaded ones.
// Toggle visible/invisible to turn it on and off; works in editor and at runtime.
class CellGridVisualizer : public Spatial {
	GDCLASS(CellGridVisualizer, Spatial);

private:
	float cell_size = 64.0f;
	int draw_radius = 5;        // how many cells outward to draw
	float line_y = 0.1f;        // slight offset above ground to avoid z-fight
	Color grid_color = Color(0.5f, 0.5f, 0.5f, 0.6f);
	Color active_color = Color(0.2f, 0.8f, 0.3f, 0.8f);
	Color player_color = Color(1.0f, 0.9f, 0.2f, 0.9f);
	bool show_coordinates = true;

	ImmediateGeometry *ig = nullptr;
	Ref<SpatialMaterial> mat_grid;
	Ref<SpatialMaterial> mat_active;
	Ref<SpatialMaterial> mat_player;

	int _last_cx = -9999;
	int _last_cz = -9999;

	CellTerrainManager *_find_terrain_manager() const;
	CellWorld *_find_cell_world() const;
	CellPlayerBody *_find_player() const;
	Node *_find_by_class(Node *root, const String &cls) const;

	Ref<SpatialMaterial> _make_line_material(const Color &c);
	void _rebuild_grid(int center_cx, int center_cz);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_cell_size(float s);
	float get_cell_size() const;
	void set_draw_radius(int r);
	int get_draw_radius() const;
	void set_grid_color(const Color &c);
	Color get_grid_color() const;
	void set_active_color(const Color &c);
	Color get_active_color() const;
	void set_player_color(const Color &c);
	Color get_player_color() const;
	void set_show_coordinates(bool s);
	bool get_show_coordinates() const;

	CellGridVisualizer();
};

#endif // CELL_GRID_VISUALIZER_H
