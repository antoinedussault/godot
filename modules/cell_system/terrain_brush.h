#ifndef TERRAIN_BRUSH_H
#define TERRAIN_BRUSH_H

#include "core/map.h"
#include "core/reference.h"

class CellTerrainManager;
class UndoRedo;

// Editor-side sculpt brush. It edits the heightmaps owned by the manager's
// active tiles. A vertex on a cell border is written into EVERY cell that
// shares it with the same value, so shared edges stay byte-for-byte identical
// (height continuity); the manager then re-derives matching normals.
//
// Usage from an EditorPlugin, per drag:
//   begin_stroke();
//   apply_brush(hit_world_pos, radius, strength, mode);   // each motion event
//   end_stroke(get_undo_redo());                          // on mouse release
class TerrainBrush : public Reference {
	GDCLASS(TerrainBrush, Reference);

public:
	enum Mode { MODE_RAISE, MODE_LOWER, MODE_FLATTEN };

private:
	CellTerrainManager *manager = nullptr;

	Map<int64_t, PoolRealArray> _stroke_before; // pre-stroke snapshot per touched cell

	static int _floordiv(int a, int b);
	// Enumerate up to 2 (cell, local) pairs sharing global vertex index g.
	static int _axis_cells(int g, int seg, int *cell_out, int *local_out);

protected:
	static void _bind_methods();

public:
	void set_manager(CellTerrainManager *m) { manager = m; }

	void begin_stroke();
	void apply_brush(const Vector3 &world_pos, float radius, float strength, int mode);
	void end_stroke(UndoRedo *undo_redo);

	TerrainBrush() {}
};

VARIANT_ENUM_CAST(TerrainBrush::Mode);

#endif // TERRAIN_BRUSH_H
