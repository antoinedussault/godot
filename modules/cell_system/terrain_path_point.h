#ifndef TERRAIN_PATH_POINT_H
#define TERRAIN_PATH_POINT_H

#include "scene/3d/spatial.h"

// A single TerrainPath control point — a real scene-tree node (child of a
// TerrainPath), not a Curve3D point or a custom gizmo handle. This is what
// makes it a fully normal, selectable Spatial: clicking its marker in the
// viewport (see TerrainPathPointGizmoPlugin) selects it exactly like any
// other node, so Godot's own Move/Rotate/Scale toolbar gizmo appears at its
// position automatically — no bespoke drag/handle code needed for that part.
//
// Only the node's position is currently consumed by TerrainPath's
// deformation math (rotation/scale are free to manipulate but not yet used
// for anything like cross-section banking).
class TerrainPathPoint : public Spatial {
	GDCLASS(TerrainPathPoint, Spatial);

	void _notify_parent();

protected:
	void _notification(int p_what);

public:
	TerrainPathPoint() {}
};

#endif
