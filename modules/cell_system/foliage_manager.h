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
	// Terrain cells currently carrying foliage-derived nav holes, so they can
	// be cleared when the foliage above them is deleted.
	Set<int64_t> _nav_stamped;

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
	// True when ANY terrain cell overlapping foliage cell (fx, fz) is streamed
	// in. Foliage and terrain are independent grids with independent cell
	// sizes, so this converts through WORLD space - passing a foliage index
	// straight to CellTerrainManager::get_cell() names a completely different
	// place as soon as the two sizes differ.
	bool _terrain_covers_cell(class CellTerrainManager *p_tm, int fx, int fz) const;
	// Cheap change detector for _sync_with_terrain: the expensive probe only
	// has to run when the camera crossed a foliage cell or the terrain's
	// streamed set changed.
	int _last_sync_cam_cx = 0;
	int _last_sync_cam_cz = 0;
	int _last_sync_terrain_count = -1;
	bool _has_synced = false;
	// Same gate for _update_lod_modes: which LOD mode a cell wants depends only
	// on its distance from the camera's cell, so it can only change when the
	// camera crosses a cell boundary or the active set changes. Persistent
	// (keep_loaded) cells stay in _active for the whole session - a big authored
	// world keeps thousands of them resident - so scanning them every frame to
	// re-derive an answer that cannot have changed is pure waste.
	int _last_lodmode_cam_cx = 0;
	int _last_lodmode_cam_cz = 0;
	int _last_lodmode_active = -1;
	bool _has_lodmode = false;
	// Rocks carve the navmesh through CellTerrainManager::set_cell_nav_obstacles,
	// which is far too heavy to run per streamed cell. Set when the foliage set
	// changes and consumed at most once every nav_obstacle_refresh_interval.
	bool _nav_obstacles_dirty = false;
	float _nav_obstacle_accum = 0.0f;
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

	// Set by a parent GameWorld: stop polling the camera, stream only when
	// stream_focus() is called. See CellTerrainManager::external_focus.
	bool external_focus = false;
	void set_external_focus(bool e) { external_focus = e; }
	bool get_external_focus() const { return external_focus; }
	// Off = no foliage at all (GameWorld.has_terrain unchecked). Settings kept.
	bool streaming_enabled = true;
	void set_streaming_enabled(bool e);
	bool get_streaming_enabled() const { return streaming_enabled; }
	void stream_focus(const Vector3 &p_focus);

	// Recomputes which terrain cells are blocked by rock/tree instances and
	// pushes the result to the terrain. Called once per completed brush stroke
	// rather than per mouse-move: each push triggers a navmesh rebuild, which
	// is expensive enough that it must not run continuously.
	void refresh_nav_obstacles();

	// Terrain height under a world XZ, or 0 with r_valid=false when there is no
	// terrain manager / no loaded cell there. The foliage brush needs this so a
	// missed physics raycast can still land on the ground instead of silently
	// falling back to the y=0 plane and burying everything.
	float get_terrain_height_at(float wx, float wz, bool *r_valid = nullptr) const;

	void    world_to_cell(const Vector3 &p_world, int &r_cx, int &r_cz) const;
	Vector3 cell_world_origin(int cx, int cz) const;
	Vector3 _grid_origin() const;
	Vector3 _cell_local_origin(int cx, int cz) const;

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
