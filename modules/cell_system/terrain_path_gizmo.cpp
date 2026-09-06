#include "terrain_path_gizmo.h"
#ifdef TOOLS_ENABLED

#include "terrain_path.h"
#include "terrain_path_point.h"

void TerrainPathGizmo::redraw() {
	clear();

	Ref<SpatialMaterial> path_material = gizmo_plugin->get_material("terrain_path_material", this);

	Vector<Vector3> lines;
	Transform gi = path->get_global_transform().affine_inverse();
	Vector3 prev;
	bool has_prev = false;
	for (int i = 0; i < path->get_child_count(); i++) {
		TerrainPathPoint *p = Object::cast_to<TerrainPathPoint>(path->get_child(i));
		if (!p) continue;
		// Gizmo lines are drawn in the gizmo's spatial node's local space.
		Vector3 local = gi.xform(p->get_global_transform().origin);
		if (has_prev) {
			lines.push_back(prev);
			lines.push_back(local);
		}
		prev = local;
		has_prev = true;
	}

	if (lines.size() > 1) {
		add_lines(lines, path_material);
		add_collision_segments(lines);
	}
}

TerrainPathGizmo::TerrainPathGizmo(TerrainPath *p_path) {
	path = p_path;
	set_spatial_node(p_path);
}

Ref<EditorSpatialGizmo> TerrainPathGizmoPlugin::create_gizmo(Spatial *p_spatial) {
	Ref<TerrainPathGizmo> ref;
	TerrainPath *path = Object::cast_to<TerrainPath>(p_spatial);
	if (path) ref = Ref<TerrainPathGizmo>(memnew(TerrainPathGizmo(path)));
	return ref;
}

String TerrainPathGizmoPlugin::get_name() const {
	return "TerrainPath";
}

int TerrainPathGizmoPlugin::get_priority() const {
	return -1;
}

TerrainPathGizmoPlugin::TerrainPathGizmoPlugin() {
	Color path_color = EDITOR_DEF("editors/3d_gizmos/gizmo_colors/path", Color(0.5, 0.5, 1.0, 0.8));
	create_material("terrain_path_material", path_color);
}

#endif
