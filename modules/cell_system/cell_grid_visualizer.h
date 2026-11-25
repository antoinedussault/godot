#ifndef CELL_GRID_VISUALIZER_H
#define CELL_GRID_VISUALIZER_H

#include "scene/3d/spatial.h"
#include "scene/3d/immediate_geometry.h"
#include "scene/resources/material.h"

class CellTerrainManager;
class CellPlayerBody;

// Creation-Engine-style streaming debug view: draws one translucent cube per
// LOADED cell so you can see what the streamer is actually holding in memory
// as you move.
//
//   red  = full-detail cell (collision + navmesh tier, i.e. within load_radius)
//   blue = LOD cell        (render-only tier, out to lod_radius)
//
// Cells that are not loaded are simply not drawn — the point of the view is
// what IS resident. This deliberately shows streaming STATE, not terrain: the
// cubes are plain boxes at a fixed height and do not follow the ground.
//
// Add it anywhere in the scene; it finds the CellTerrainManager itself and
// takes cell_size / lod_radius from it, so the cubes always line up with real
// cells. Toggle the node's visibility to turn it on and off; works in the
// editor and at runtime.
class CellGridVisualizer : public Spatial {
	GDCLASS(CellGridVisualizer, Spatial);

private:
	// 0 = auto: derive from the manager's lod_radius so the scan always covers
	// everything the streamer could have loaded.
	int draw_radius = 0;
	float box_height = 32.0f;
	float base_y = 0.0f;
	bool draw_edges = true;

	Color loaded_color = Color(1.0f, 0.12f, 0.12f, 0.13f); // full detail + collision
	Color lod_color = Color(0.2f, 0.45f, 1.0f, 0.10f);     // render-only LOD

	ImmediateGeometry *ig = nullptr;
	Ref<SpatialMaterial> mat_box;

	// Rebuilt on a timer, not every frame: the view only changes when cells
	// stream in/out, and resolving the manager means a recursive tree search
	// that has no business running at frame rate.
	float _refresh_accum = 0.0f;
	float refresh_interval = 0.2f;

	CellTerrainManager *_find_terrain_manager() const;
	CellPlayerBody *_find_player() const;
	Node *_find_by_class(Node *root, const String &cls) const;
	Node *_search_root() const;
	// Where the streamer is centred, mirroring CellTerrainManager's own choice
	// (active camera first, then a player body, then this node).
	Vector3 _stream_center() const;

	Ref<SpatialMaterial> _make_box_material();
	void _add_box_faces(const Transform &to_local, const Vector3 &a, const Vector3 &b, const Color &c);
	void _add_box_edges(const Transform &to_local, const Vector3 &a, const Vector3 &b, const Color &c);
	void _rebuild_grid();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_draw_radius(int r);
	int get_draw_radius() const;
	void set_box_height(float h);
	float get_box_height() const;
	void set_base_y(float y);
	float get_base_y() const;
	void set_draw_edges(bool e);
	bool get_draw_edges() const;
	void set_loaded_color(const Color &c);
	Color get_loaded_color() const;
	void set_lod_color(const Color &c);
	Color get_lod_color() const;

	CellGridVisualizer();
};

#endif // CELL_GRID_VISUALIZER_H
