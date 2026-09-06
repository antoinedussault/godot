#ifndef TERRAIN_PATH_GIZMO_H
#define TERRAIN_PATH_GIZMO_H
#ifdef TOOLS_ENABLED

#include "editor/spatial_editor_gizmos.h"

class TerrainPath;

// Purely visual: draws the polyline connecting a TerrainPath's
// TerrainPathPoint children in order. No handles here — dragging happens on
// the point nodes themselves (see TerrainPathPointGizmo), which get Godot's
// normal Move/Rotate/Scale toolbar gizmo for free once selected.
class TerrainPathGizmo : public EditorSpatialGizmo {
	GDCLASS(TerrainPathGizmo, EditorSpatialGizmo);

	TerrainPath *path;

public:
	virtual void redraw();
	TerrainPathGizmo(TerrainPath *p_path = nullptr);
};

class TerrainPathGizmoPlugin : public EditorSpatialGizmoPlugin {
	GDCLASS(TerrainPathGizmoPlugin, EditorSpatialGizmoPlugin);

protected:
	Ref<EditorSpatialGizmo> create_gizmo(Spatial *p_spatial);

public:
	String get_name() const;
	int get_priority() const;
	TerrainPathGizmoPlugin();
};

#endif
#endif
