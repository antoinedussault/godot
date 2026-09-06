#ifndef TERRAIN_PATH_POINT_GIZMO_H
#define TERRAIN_PATH_POINT_GIZMO_H
#ifdef TOOLS_ENABLED

#include "editor/spatial_editor_gizmos.h"

class TerrainPathPoint;

// The actual clickable marker for a TerrainPath control point — a small
// solid-colored dot billboard (Unreal Landscape Spline style), same
// mechanism stock Godot uses for Light/AudioStreamPlayer3D/Listener icons
// (create_icon_material + add_unscaled_billboard). Because TerrainPathPoint
// is a real Spatial node, clicking this marker selects it exactly like any
// other node — Godot's own Move/Rotate/Scale toolbar gizmo then shows up at
// its position automatically, no custom drag-handle code required.
class TerrainPathPointGizmo : public EditorSpatialGizmo {
	GDCLASS(TerrainPathPointGizmo, EditorSpatialGizmo);

	TerrainPathPoint *point;

public:
	virtual void redraw();
	TerrainPathPointGizmo(TerrainPathPoint *p_point = nullptr);
};

class TerrainPathPointGizmoPlugin : public EditorSpatialGizmoPlugin {
	GDCLASS(TerrainPathPointGizmoPlugin, EditorSpatialGizmoPlugin);

protected:
	Ref<EditorSpatialGizmo> create_gizmo(Spatial *p_spatial);

public:
	String get_name() const;
	int get_priority() const;
	TerrainPathPointGizmoPlugin();
};

#endif
#endif
