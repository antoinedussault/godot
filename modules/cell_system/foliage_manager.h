#ifndef FOLIAGE_MANAGER_H
#define FOLIAGE_MANAGER_H

#include "core/map.h"
#include "core/set.h"
#include "scene/3d/spatial.h"
#include "foliage_type.h"
#include "cell_foliage.h"

class CellTerrainManager;
class CellTerrain;
class Camera;

// Top-level foliage system that cooperates with an existing CellTerrainManager.
// Watches which cells the terrain manager has active, attaches / detaches a
// CellFoliage to each, loads and saves per-cell instance data, drives per-
// instance LOD + dither every frame, and toggles trunk collision on cells
// within tree_collision_radius of the camera.
//
// Setup: add as a sibling-or-child near the CellTerrainManager. Point
// terrain_manager_path at it. Assign FoliageType resources via the types
// Array property. Use the editor plugin to paint.
class FoliageManager : public Spatial {
	GDCLASS(FoliageManager, Spatial);

private:
	NodePath terrain_manager_path;
	String   world_dir             = "user://world";
	float    cell_size             = 64.0f;
	int      tree_collision_radius = 1;
	// Cells within this Chebyshev radius of the camera build full tree LODs;
	// farther cells render the imposter only. Decided by distance, not terrain,
	// so near trees always show meshes even if terrain streaming differs.
	int      tree_full_lod_radius  = 4;
	float    lod_update_interval   = 0.0f;

	// Dynamic list of FoliageType resources. No fixed upper bound.
	Vector<Ref<FoliageType>> types;

	// CellFoliage nodes keyed by cell, parented under this manager.
	Map<int64_t, CellFoliage *> _active;
	Set<int64_t>                _dirty;

	float _accum         = 0.0f;
	bool  _has_cam_cell  = false;
	int   _cam_cx        = 0, _cam_cz = 0;

	static int64_t _key(int x, int z);
	static void    _unkey(int64_t k, int &x, int &z);

	CellTerrainManager       *_get_terrain_manager() const;
	Camera                   *_get_active_camera()   const;
	Vector<Ref<FoliageType>>  _gather_types()         const;

	CellFoliage *_spawn_for_cell(int cx, int cz, CellTerrain *p_terrain);
	// Switches each active cell between full-LOD and imposter-only based on its
	// distance to the camera (independent of terrain streaming).
	void _update_lod_modes();
	// Loads every saved cell that contains a keep_loaded type, in persistent-only
	// mode, so all trees are present from game start regardless of camera position.
	void _load_all_persistent();
	void _sync_with_terrain();
	void _update_all_lod(const Vector3 &cam_world);
	void _update_collision(const Vector3 &cam_world);

	String        _foliage_path(int cx, int cz) const;
	PoolRealArray _load_cell(int cx, int cz)    const;
	void          _save_cell(int cx, int cz, CellFoliage *p_cf);

protected:
	static void _bind_methods();
	void _notification(int p_what);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	bool _set(const StringName &p_name, const Variant &p_value);
	void _get_property_list(List<PropertyInfo> *p_list) const;

public:
	// Editor / paint API.
	CellFoliage    *get_cell_foliage(int cx, int cz) const;
	int             get_type_count()                  const;
	Ref<FoliageType> get_type(int p_index)            const;
	void            mark_dirty(int cx, int cz);
	void            save_dirty();
	void            rebuild_cell(int cx, int cz);

	void editor_stream_to(const Vector3 &p_world_pos);

	void    world_to_cell(const Vector3 &p_world, int &r_cx, int &r_cz) const;
	Vector3 cell_world_origin(int cx, int cz) const;

	int  paint_scatter(int p_type_index, const Vector3 &p_world_center, float p_radius, float p_strength);
	bool place_single(int p_type_index, const Vector3 &p_world_pos, const Vector3 &p_world_normal);
	int  erase(int p_type_index, const Vector3 &p_world_center, float p_radius);

	PoolRealArray get_cell_data(int cx, int cz) const;
	void          apply_cell_data(int cx, int cz, const PoolRealArray &data);

	// Scalar property accessors.
	void     set_terrain_manager_path(const NodePath &p); NodePath get_terrain_manager_path() const;
	void     set_world_dir(const String &p);              String   get_world_dir()             const;
	void     set_cell_size(float s);                      float    get_cell_size()              const;
	void     set_tree_collision_radius(int r);            int      get_tree_collision_radius()  const;
	void     set_tree_full_lod_radius(int r);             int      get_tree_full_lod_radius()   const;
	void     set_lod_update_interval(float s);            float    get_lod_update_interval()    const;

	// Individual type slot (used by the paint system).
	void             set_type(int idx, const Ref<FoliageType> &t);
	Ref<FoliageType> get_type_slot(int idx) const;

	// Array property accessor (for the Inspector).
	Array get_types_array() const;
	void  set_types_array(const Array &arr);

	FoliageManager();
};

#endif // FOLIAGE_MANAGER_H
