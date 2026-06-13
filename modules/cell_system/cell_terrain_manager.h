#ifndef CELL_TERRAIN_MANAGER_H
#define CELL_TERRAIN_MANAGER_H

// Supersedes the previous cell_terrain_manager.h. Adds dirty-tracking + save.
// Include paths / container method names target a stock Godot 3.5.x checkout.
#include "core/map.h"
#include "core/set.h"
#include "scene/3d/spatial.h"

class CellTerrain;

// Keeps a (2*load_radius+1)^2 block of CellTerrain tiles loaded around a target
// node. Loads/saves per-cell heightmaps on disk, feeds neighbour edge rows so
// seam normals match, and tracks which tiles have unsaved edits.
class CellTerrainManager : public Spatial {
	GDCLASS(CellTerrainManager, Spatial);

private:
	NodePath target_path;
	String world_dir = "user://world";
	int verts_per_side = 65;
	float cell_size = 64.0f;
	float height_scale = 1.0f;
	int load_radius = 2;

	bool _has_last = false;
	int _last_cx = 0;
	int _last_cz = 0;

	Map<int64_t, CellTerrain *> _active;
	Set<int64_t> _dirty; // tiles edited since last save

	static int64_t _key(int x, int z);
	static void _unkey(int64_t k, int &x, int &z);

	Spatial *_get_target() const;
	CellTerrain *_spawn_cell(int cx, int cz);
	PoolRealArray _load_heights(int cx, int cz) const;
	bool _save_cell(int cx, int cz);             // write one active tile to disk
	void _update_neighbor_edges(int cx, int cz);
	void _evaluate(int cx, int cz);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	// Used by the brush.
	CellTerrain *get_cell(int cx, int cz) const;
	void rebuild_at(int cx, int cz); // marks (cx,cz) dirty, refreshes edges, rebuilds it + neighbours
	void mark_dirty(int cx, int cz);

	// Persistence.
	void save_dirty(); // write every dirty active tile, then clear the set

	// Editor authoring (driven by the EditorPlugin; harmless at runtime).
	void editor_focus(const Vector3 &world_pos); // load the block around a point
	void editor_clear();                         // save dirty + drop all preview tiles

	void set_target_path(const NodePath &p); NodePath get_target_path() const;
	void set_world_dir(const String &p);     String get_world_dir() const;
	void set_verts_per_side(int v);          int get_verts_per_side() const;
	void set_cell_size(float s);             float get_cell_size() const;
	void set_height_scale(float s);          float get_height_scale() const;
	void set_load_radius(int r);             int get_load_radius() const;

	void refresh();

	CellTerrainManager();
};

#endif // CELL_TERRAIN_MANAGER_H
