#include "terrain_path_point_gizmo.h"
#ifdef TOOLS_ENABLED

#include "editor_ui_helpers.h"
#include "terrain_path_point.h"

// Small two-tone dot (dark soft outline + bright core) rasterized in code —
// this module has no art pipeline. The core is tinted per-instance via
// add_unscaled_billboard's modulate color, so it stays plain white here.
static Ref<ImageTexture> _make_point_icon() {
	Ref<Image> img = cs_icon_new(32);
	img->lock();
	cs_icon_fill_circle(img, Vector2(16, 16), 14.0f, Color(0, 0, 0, 0.55f));
	cs_icon_fill_circle(img, Vector2(16, 16), 10.0f, Color(1, 1, 1, 1.0f));
	img->unlock();
	return cs_icon_to_texture(img);
}

void TerrainPathPointGizmo::redraw() {
	clear();
	Ref<Material> icon = gizmo_plugin->get_material("terrain_path_point_icon", this);
	Color accent = is_selected() ? Color(1.0f, 0.75f, 0.15f) : Color(0.35f, 1.0f, 0.5f);
	add_unscaled_billboard(icon, 0.07f, accent);
}

TerrainPathPointGizmo::TerrainPathPointGizmo(TerrainPathPoint *p_point) {
	point = p_point;
	set_spatial_node(p_point);
}

Ref<EditorSpatialGizmo> TerrainPathPointGizmoPlugin::create_gizmo(Spatial *p_spatial) {
	Ref<TerrainPathPointGizmo> ref;
	TerrainPathPoint *point = Object::cast_to<TerrainPathPoint>(p_spatial);
	if (point) ref = Ref<TerrainPathPointGizmo>(memnew(TerrainPathPointGizmo(point)));
	return ref;
}

String TerrainPathPointGizmoPlugin::get_name() const {
	return "TerrainPathPoint";
}

int TerrainPathPointGizmoPlugin::get_priority() const {
	return -1;
}

TerrainPathPointGizmoPlugin::TerrainPathPointGizmoPlugin() {
	create_icon_material("terrain_path_point_icon", _make_point_icon());
}

#endif
