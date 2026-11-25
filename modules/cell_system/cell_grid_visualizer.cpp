#include "cell_grid_visualizer.h"

#include "cell_player_body.h"
#include "cell_terrain_manager.h"
#include "scene/3d/camera.h"
#include "scene/main/viewport.h"

CellGridVisualizer::CellGridVisualizer() {}

// --- tree search (same pattern as CellPlayerBody) ---
Node *CellGridVisualizer::_find_by_class(Node *root, const String &cls) const {
	if (!root) return nullptr;
	// is_class, not an exact name match: a CellCharacterBody is a CellPlayerBody.
	if (root->is_class(cls)) return root;
	for (int i = 0; i < root->get_child_count(); i++) {
		Node *found = _find_by_class(root->get_child(i), cls);
		if (found) return found;
	}
	return nullptr;
}

// Walk up to the top of the edited/running scene and search down from there,
// so a sibling CellTerrainManager is found wherever this node sits.
// get_owner() is null for the scene root itself and for nodes added from code,
// hence the parent fallbacks.
Node *CellGridVisualizer::_search_root() const {
	Node *self = const_cast<CellGridVisualizer *>(this);
	if (!self->is_inside_tree()) return nullptr;
	Node *n = self->get_owner();
	if (!n) n = self->get_parent();
	if (!n) n = self;
	Node *tree_root = self->get_tree() ? self->get_tree()->get_root() : nullptr;
	while (n->get_parent() && n->get_parent() != tree_root) {
		n = n->get_parent();
	}
	return n;
}

CellTerrainManager *CellGridVisualizer::_find_terrain_manager() const {
	return Object::cast_to<CellTerrainManager>(_find_by_class(_search_root(), "CellTerrainManager"));
}

CellPlayerBody *CellGridVisualizer::_find_player() const {
	return Object::cast_to<CellPlayerBody>(_find_by_class(_search_root(), "CellPlayerBody"));
}

Vector3 CellGridVisualizer::_stream_center() const {
	// Mirrors CellTerrainManager's own preference order, so the scan is centred
	// where the streamer is actually centred. In the editor get_camera()
	// returns the editor viewport's camera, which is what drives preview
	// streaming there too.
	Viewport *vp = const_cast<CellGridVisualizer *>(this)->get_viewport();
	if (vp) {
		Camera *cam = vp->get_camera();
		if (cam) return cam->get_global_transform().origin;
	}
	CellPlayerBody *player = _find_player();
	if (player) return player->get_global_transform().origin;
	return get_global_transform().origin;
}

Ref<SpatialMaterial> CellGridVisualizer::_make_box_material() {
	Ref<SpatialMaterial> m;
	m.instance();
	m->set_flag(SpatialMaterial::FLAG_UNSHADED, true);
	m->set_flag(SpatialMaterial::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	// White base: albedo is MULTIPLIED by the vertex color, so tinting it here
	// as well would darken every cube by its own color twice. (The previous
	// version did exactly that.)
	m->set_albedo(Color(1, 1, 1, 1));
	m->set_feature(SpatialMaterial::FEATURE_TRANSPARENT, true);
	// Visible from inside — you are standing in one of these cubes, and with
	// back-face culling your own cell would be the one you could never see.
	m->set_cull_mode(SpatialMaterial::CULL_DISABLED);
	// Depth TEST stays on so the world still occludes cubes, but depth WRITE is
	// off so overlapping translucent cubes blend instead of fighting.
	m->set_depth_draw_mode(SpatialMaterial::DEPTH_DRAW_DISABLED);
	return m;
}

void CellGridVisualizer::_add_box_faces(const Transform &to_local, const Vector3 &a, const Vector3 &b, const Color &c) {
	const Vector3 v[8] = {
		to_local.xform(Vector3(a.x, a.y, a.z)),
		to_local.xform(Vector3(b.x, a.y, a.z)),
		to_local.xform(Vector3(b.x, a.y, b.z)),
		to_local.xform(Vector3(a.x, a.y, b.z)),
		to_local.xform(Vector3(a.x, b.y, a.z)),
		to_local.xform(Vector3(b.x, b.y, a.z)),
		to_local.xform(Vector3(b.x, b.y, b.z)),
		to_local.xform(Vector3(a.x, b.y, b.z)),
	};
	static const int faces[6][4] = {
		{ 0, 1, 2, 3 }, // bottom
		{ 4, 5, 6, 7 }, // top
		{ 0, 1, 5, 4 }, // -z
		{ 3, 2, 6, 7 }, // +z
		{ 0, 3, 7, 4 }, // -x
		{ 1, 2, 6, 5 }, // +x
	};
	ig->set_color(c);
	for (int f = 0; f < 6; f++) {
		const int *q = faces[f];
		// Two triangles per quad; winding is irrelevant with culling disabled.
		ig->add_vertex(v[q[0]]); ig->add_vertex(v[q[1]]); ig->add_vertex(v[q[2]]);
		ig->add_vertex(v[q[0]]); ig->add_vertex(v[q[2]]); ig->add_vertex(v[q[3]]);
	}
}

void CellGridVisualizer::_add_box_edges(const Transform &to_local, const Vector3 &a, const Vector3 &b, const Color &c) {
	const Vector3 v[8] = {
		to_local.xform(Vector3(a.x, a.y, a.z)),
		to_local.xform(Vector3(b.x, a.y, a.z)),
		to_local.xform(Vector3(b.x, a.y, b.z)),
		to_local.xform(Vector3(a.x, a.y, b.z)),
		to_local.xform(Vector3(a.x, b.y, a.z)),
		to_local.xform(Vector3(b.x, b.y, a.z)),
		to_local.xform(Vector3(b.x, b.y, b.z)),
		to_local.xform(Vector3(a.x, b.y, b.z)),
	};
	static const int edges[12][2] = {
		{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, // bottom ring
		{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }, // top ring
		{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }, // uprights
	};
	ig->set_color(c);
	for (int e = 0; e < 12; e++) {
		ig->add_vertex(v[edges[e][0]]);
		ig->add_vertex(v[edges[e][1]]);
	}
}

void CellGridVisualizer::_rebuild_grid() {
	if (!ig || !is_inside_tree()) return;
	ig->clear();

	CellTerrainManager *mgr = _find_terrain_manager();
	if (!mgr) return; // nothing streaming, nothing to show

	const float cs = mgr->get_cell_size();
	if (cs <= 0.0f) return;

	// Cells are indexed relative to the manager's own origin (see
	// CellTerrainManager::get_height_at_world), so the cubes have to be too.
	const Vector3 grid_origin = mgr->get_global_transform().origin;

	// Auto radius covers everything the streamer could be holding; +1 so a cell
	// that is still on its way out at the edge is not silently missed.
	int r = draw_radius;
	if (r <= 0) r = mgr->get_lod_radius() + 1;

	const Vector3 center = _stream_center() - grid_origin;
	const int center_cx = (int)Math::floor(center.x / cs);
	const int center_cz = (int)Math::floor(center.z / cs);

	const Transform to_local = ig->get_global_transform().affine_inverse();
	const float y0 = base_y;
	const float y1 = y0 + MAX(0.01f, box_height);

	ig->set_material_override(mat_box);

	// Faces and edges are separate primitive types, so they need separate
	// begin/end sections and therefore two passes over the same cells.
	ig->begin(Mesh::PRIMITIVE_TRIANGLES);
	for (int cz = center_cz - r; cz <= center_cz + r; cz++) {
		for (int cx = center_cx - r; cx <= center_cx + r; cx++) {
			CellTerrain *t = mgr->get_cell(cx, cz);
			if (!t) continue; // not loaded — deliberately draws nothing
			// The collision tier IS the full-detail tier: CellTerrainManager
			// sets enable_collision for dist <= load_radius and leaves it off
			// for the LOD ring beyond it.
			const Color c = t->get_enable_collision() ? loaded_color : lod_color;

			const float x0 = grid_origin.x + cx * cs;
			const float z0 = grid_origin.z + cz * cs;
			_add_box_faces(to_local,
					Vector3(x0, grid_origin.y + y0, z0),
					Vector3(x0 + cs, grid_origin.y + y1, z0 + cs), c);
		}
	}
	ig->end();

	if (!draw_edges) return;

	ig->begin(Mesh::PRIMITIVE_LINES);
	for (int cz = center_cz - r; cz <= center_cz + r; cz++) {
		for (int cx = center_cx - r; cx <= center_cx + r; cx++) {
			CellTerrain *t = mgr->get_cell(cx, cz);
			if (!t) continue;
			Color c = t->get_enable_collision() ? loaded_color : lod_color;
			// Edges outline the cube, so give them more presence than the
			// faces they bound.
			c.a = MIN(1.0f, c.a * 5.0f);

			const float x0 = grid_origin.x + cx * cs;
			const float z0 = grid_origin.z + cz * cs;
			_add_box_edges(to_local,
					Vector3(x0, grid_origin.y + y0, z0),
					Vector3(x0 + cs, grid_origin.y + y1, z0 + cs), c);
		}
	}
	ig->end();
}

void CellGridVisualizer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!ig) {
				ig = memnew(ImmediateGeometry);
				add_child(ig);
			}
			mat_box = _make_box_material();
			ig->set_material_override(mat_box);
			set_process(true);
			_refresh_accum = refresh_interval; // draw on the first tick
		} break;

		case NOTIFICATION_PROCESS: {
			if (!is_visible_in_tree()) return;
			_refresh_accum += get_process_delta_time();
			if (_refresh_accum >= refresh_interval) {
				_refresh_accum = 0.0f;
				_rebuild_grid();
			}
		} break;
	}
	// NOTE: deliberately no NOTIFICATION_EXIT_TREE handling. The previous
	// version queue_delete()d the ImmediateGeometry and nulled the pointer
	// there, but NOTIFICATION_READY does not fire again on re-entering the
	// tree — so the node came back permanently blank. `ig` is an ordinary
	// child and is freed along with this node.
}

// --- properties ---
void CellGridVisualizer::set_draw_radius(int r) { draw_radius = MAX(0, r); _refresh_accum = refresh_interval; }
int CellGridVisualizer::get_draw_radius() const { return draw_radius; }
void CellGridVisualizer::set_box_height(float h) { box_height = h; _refresh_accum = refresh_interval; }
float CellGridVisualizer::get_box_height() const { return box_height; }
void CellGridVisualizer::set_base_y(float y) { base_y = y; _refresh_accum = refresh_interval; }
float CellGridVisualizer::get_base_y() const { return base_y; }
void CellGridVisualizer::set_draw_edges(bool e) { draw_edges = e; _refresh_accum = refresh_interval; }
bool CellGridVisualizer::get_draw_edges() const { return draw_edges; }
void CellGridVisualizer::set_loaded_color(const Color &c) { loaded_color = c; _refresh_accum = refresh_interval; }
Color CellGridVisualizer::get_loaded_color() const { return loaded_color; }
void CellGridVisualizer::set_lod_color(const Color &c) { lod_color = c; _refresh_accum = refresh_interval; }
Color CellGridVisualizer::get_lod_color() const { return lod_color; }

void CellGridVisualizer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_draw_radius", "r"), &CellGridVisualizer::set_draw_radius);
	ClassDB::bind_method(D_METHOD("get_draw_radius"), &CellGridVisualizer::get_draw_radius);
	ClassDB::bind_method(D_METHOD("set_box_height", "h"), &CellGridVisualizer::set_box_height);
	ClassDB::bind_method(D_METHOD("get_box_height"), &CellGridVisualizer::get_box_height);
	ClassDB::bind_method(D_METHOD("set_base_y", "y"), &CellGridVisualizer::set_base_y);
	ClassDB::bind_method(D_METHOD("get_base_y"), &CellGridVisualizer::get_base_y);
	ClassDB::bind_method(D_METHOD("set_draw_edges", "e"), &CellGridVisualizer::set_draw_edges);
	ClassDB::bind_method(D_METHOD("get_draw_edges"), &CellGridVisualizer::get_draw_edges);
	ClassDB::bind_method(D_METHOD("set_loaded_color", "c"), &CellGridVisualizer::set_loaded_color);
	ClassDB::bind_method(D_METHOD("get_loaded_color"), &CellGridVisualizer::get_loaded_color);
	ClassDB::bind_method(D_METHOD("set_lod_color", "c"), &CellGridVisualizer::set_lod_color);
	ClassDB::bind_method(D_METHOD("get_lod_color"), &CellGridVisualizer::get_lod_color);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "draw_radius", PROPERTY_HINT_RANGE, "0,32,1"), "set_draw_radius", "get_draw_radius");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "box_height", PROPERTY_HINT_RANGE, "0.01,512,0.1"), "set_box_height", "get_box_height");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "base_y"), "set_base_y", "get_base_y");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "draw_edges"), "set_draw_edges", "get_draw_edges");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "loaded_color"), "set_loaded_color", "get_loaded_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "lod_color"), "set_lod_color", "get_lod_color");
}
