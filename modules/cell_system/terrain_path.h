#ifndef TERRAIN_PATH_H
#define TERRAIN_PATH_H

#include "core/map.h"
#include "core/set.h"
#include "scene/3d/spatial.h"
#include "terrain_math_utils.h"

class CellTerrain;
class CellTerrainManager;
class TerrainPathPoint;

// Non-destructive road/ramp terrain deformation, Unreal-Landscape-Spline
// style. Control points are real child TerrainPathPoint nodes (see that
// class) rather than Curve3D points or custom gizmo drag-handles — clicking
// a point's marker in the viewport selects it as a normal Spatial, so
// Godot's own Move/Rotate/Scale toolbar gizmo shows up at its position for
// free. TerrainPath itself just walks its TerrainPathPoint children (in
// child order) to get the polyline, and keeps the terrain live-synced to
// wherever they currently are — no manual Apply step.
//
// Non-destructive works via a per-cell "baseline" height snapshot, captured
// lazily the first time this path touches a cell. Every reapply blends from
// that baseline (never from the terrain's current live height), so repeated
// edits — dragging a point back and forth, tweaking width — never drift.
// A cell the path stops covering is restored to its baseline and dropped.
//
// Known limitation (accepted): this is a single-path baseline, not a full
// multi-layer compositing system. If the sculpt brush edits ground a path
// has already shaped, that path's baseline goes stale for the overlapping
// cells — refresh_baseline() is the manual escape hatch to re-anchor.
// remove_effect() reverts this path's contribution entirely (e.g. right
// before deleting the node) without reapplying afterward.
class TerrainPath : public Spatial {
	GDCLASS(TerrainPath, Spatial);

	NodePath terrain_manager_path;

	float width = 6.0f;
	float falloff_width = 4.0f;
	int falloff_type = FALLOFF_SMOOTH;
	float height_offset = 0.0f;
	float strength = 1.0f;

	Map<int64_t, PoolRealArray> _baseline;

	// Debounced live reapply — armed by any TerrainPathPoint child moving
	// (fires continuously while a Move-gizmo drag is live, see
	// TerrainPathPoint::_notification) and by every setting change, so a
	// drag doesn't recompute the whole path every single frame.
	bool _reapply_dirty = false;
	float _reapply_timer = 0.0f;
	static const float REAPPLY_DEBOUNCE_SEC;

	// Separate, longer debounce for HLOD re-baking — mirrors TerrainEditorPlugin's
	// brush-stroke debounce so distant HLOD stays in sync once editing settles,
	// without re-baking on every single reapply while dragging.
	float _hlod_rebake_timer = 0.0f;

	void _request_reapply();
	CellTerrainManager *_get_manager() const;

	// Restores whichever of p_keys are present in _baseline (erasing them from
	// it as it goes) and inserts each successfully-restored key into r_touched.
	// Pure data restore — caller is responsible for calling rebuild_region().
	void _restore_cells(const Vector<int64_t> &p_keys, Set<int64_t> &r_touched);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_terrain_manager_path(const NodePath &p);
	NodePath get_terrain_manager_path() const;
	// Public so the editor plugin can raycast against the same terrain the
	// path deforms (e.g. to place a new point right on the ground).
	CellTerrainManager *get_terrain_manager() const { return _get_manager(); }

	void set_width(float w);         float get_width() const;
	void set_falloff_width(float w); float get_falloff_width() const;
	void set_falloff_type(int t);    int get_falloff_type() const;
	void set_height_offset(float h); float get_height_offset() const;
	void set_strength(float s);      float get_strength() const;

	// Called by a TerrainPathPoint child whenever it moves (including live
	// during a gizmo drag, and on undo/redo of one), and by the editor
	// plugin right after adding/removing a point. Arms the debounce timer —
	// safe and cheap to call every frame during a drag.
	void notify_point_changed();

	// The core non-destructive step: walks the TerrainPathPoint children in
	// order, blends each affected vertex from its cell's baseline height,
	// restores cells no longer covered, and rebuilds the touched region once.
	void reapply();
	// Restores every baseline cell (undoing this path's current contribution),
	// clears the baseline, then reapplies — re-anchoring to whatever the
	// terrain looks like right now (e.g. after brush edits drifted it).
	void refresh_baseline();
	// Restores every baseline cell and clears the baseline, without
	// reapplying — for "revert this path's effect before deleting it".
	void remove_effect();

	TerrainPath() {}
	~TerrainPath() {}
};

#endif
