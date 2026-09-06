#include "cell_grid_visualizer.h"

#include "cell_terrain_manager.h"
#include "cell_world.h"
#include "cell_player_body.h"

CellGridVisualizer::CellGridVisualizer() {}

// --- tree search (same pattern as CellPlayerBody) ---
Node *CellGridVisualizer::_find_by_class(Node *root, const String &cls) const {
	if (!root) return nullptr;
	if (root->get_class() == cls) return root;
	for (int i = 0; i < root->get_child_count(); i++) {
		Node *found = _find_by_class(root->get_child(i), cls);
		if (found) return found;
	}
	return nullptr;
}

CellTerrainManager *CellGridVisualizer::_find_terrain_manager() const {
	Node *n = const_cast<CellGridVisualizer *>(this)->get_owner();
	if (!n) n = const_cast<CellGridVisualizer *>(this)->get_parent();
	while (n && n->get_parent() && n->get_owner()) n = n->get_parent();
	return Object::cast_to<CellTerrainManager>(_find_by_class(n, "CellTerrainManager"));
}

CellWorld *CellGridVisualizer::_find_cell_world() const {
	Node *n = const_cast<CellGridVisualizer *>(this)->get_owner();
	if (!n) n = const_cast<CellGridVisualizer *>(this)->get_parent();
	while (n && n->get_parent() && n->get_owner()) n = n->get_parent();
	return Object::cast_to<CellWorld>(_find_by_class(n, "CellWorld"));
}

CellPlayerBody *CellGridVisualizer::_find_player() const {
	Node *n = const_cast<CellGridVisualizer *>(this)->get_owner();
	if (!n) n = const_cast<CellGridVisualizer *>(this)->get_parent();
	while (n && n->get_parent() && n->get_owner()) n = n->get_parent();
	return Object::cast_to<CellPlayerBody>(_find_by_class(n, "CellPlayerBody"));
}

Ref<SpatialMaterial> CellGridVisualizer::_make_line_material(const Color &c) {
	Ref<SpatialMaterial> m;
	m.instance();
	m->set_flag(SpatialMaterial::FLAG_UNSHADED, true);
	m->set_flag(SpatialMaterial::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	m->set_albedo(c);
	if (c.a < 1.0f) {
		m->set_feature(SpatialMaterial::FEATURE_TRANSPARENT, true);
	}
	return m;
}

void CellGridVisualizer::_rebuild_grid(int center_cx, int center_cz) {
	if (!ig) return;
	ig->clear();

	CellTerrainManager *mgr = _find_terrain_manager();
	CellWorld *cw = _find_cell_world();
	CellPlayerBody *player = _find_player();

	int pcx = center_cx, pcz = center_cz;
	if (player) {
		pcx = player->get_cell_x();
		pcz = player->get_cell_z();
	}

	const int r = draw_radius;
	const float cs = cell_size;

	// Draw cell outlines. Each cell is a rectangle of 4 lines on the XZ plane.
	ig->set_material_override(mat_grid);
	ig->begin(Mesh::PRIMITIVE_LINES);

	for (int cz = center_cz - r; cz <= center_cz + r; cz++) {
		for (int cx = center_cx - r; cx <= center_cx + r; cx++) {

			// Determine cell state for coloring.
			bool is_player_cell = (cx == pcx && cz == pcz);
			bool is_active = false;

			if (mgr && mgr->get_cell(cx, cz)) is_active = true;
			if (cw && cw->get_cell(cx, cz)) is_active = true;

			Color c = grid_color;
			if (is_active) c = active_color;
			if (is_player_cell) c = player_color;

			ig->set_color(c);

			float x0 = cx * cs;
			float z0 = cz * cs;
			float x1 = x0 + cs;
			float z1 = z0 + cs;
			float y = line_y;

			// Bottom edge
			ig->add_vertex(Vector3(x0, y, z0));
			ig->add_vertex(Vector3(x1, y, z0));
			// Right edge
			ig->add_vertex(Vector3(x1, y, z0));
			ig->add_vertex(Vector3(x1, y, z1));
			// Top edge
			ig->add_vertex(Vector3(x1, y, z1));
			ig->add_vertex(Vector3(x0, y, z1));
			// Left edge
			ig->add_vertex(Vector3(x0, y, z1));
			ig->add_vertex(Vector3(x0, y, z0));

			// Cross marker on the player's cell.
			if (is_player_cell) {
				float mx = x0 + cs * 0.5f;
				float mz = z0 + cs * 0.5f;
				float s = cs * 0.1f;
				ig->add_vertex(Vector3(mx - s, y, mz));
				ig->add_vertex(Vector3(mx + s, y, mz));
				ig->add_vertex(Vector3(mx, y, mz - s));
				ig->add_vertex(Vector3(mx, y, mz + s));
			}
		}
	}

	ig->end();
}

void CellGridVisualizer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			ig = memnew(ImmediateGeometry);
			add_child(ig);
			// Use a single unshaded material; per-vertex color handles the rest.
			mat_grid = _make_line_material(grid_color);
			ig->set_material_override(mat_grid);
			set_process(true);
		} break;

		case NOTIFICATION_PROCESS: {
			if (!is_visible_in_tree()) return;

			// Figure out what cell to center the grid on.
			int cx = 0, cz = 0;
			CellPlayerBody *player = _find_player();
			if (player) {
				cx = player->get_cell_x();
				cz = player->get_cell_z();
			} else {
				// In editor with no player, center on camera.
				Vector3 pos = get_global_transform().origin;
				cx = (int)Math::floor(pos.x / cell_size);
				cz = (int)Math::floor(pos.z / cell_size);
			}

			// Only rebuild when the center cell changes.
			if (cx != _last_cx || cz != _last_cz) {
				_last_cx = cx;
				_last_cz = cz;
				_rebuild_grid(cx, cz);
			}
		} break;

		case NOTIFICATION_EXIT_TREE: {
			if (ig) {
				ig->queue_delete();
				ig = nullptr;
			}
		} break;
	}
}

// --- properties ---
void CellGridVisualizer::set_cell_size(float s) { cell_size = s; _last_cx = -9999; }
float CellGridVisualizer::get_cell_size() const { return cell_size; }
void CellGridVisualizer::set_draw_radius(int r) { draw_radius = MAX(1, r); _last_cx = -9999; }
int CellGridVisualizer::get_draw_radius() const { return draw_radius; }
void CellGridVisualizer::set_grid_color(const Color &c) { grid_color = c; _last_cx = -9999; }
Color CellGridVisualizer::get_grid_color() const { return grid_color; }
void CellGridVisualizer::set_active_color(const Color &c) { active_color = c; _last_cx = -9999; }
Color CellGridVisualizer::get_active_color() const { return active_color; }
void CellGridVisualizer::set_player_color(const Color &c) { player_color = c; _last_cx = -9999; }
Color CellGridVisualizer::get_player_color() const { return player_color; }
void CellGridVisualizer::set_show_coordinates(bool s) { show_coordinates = s; _last_cx = -9999; }
bool CellGridVisualizer::get_show_coordinates() const { return show_coordinates; }

void CellGridVisualizer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_cell_size", "s"), &CellGridVisualizer::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &CellGridVisualizer::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_draw_radius", "r"), &CellGridVisualizer::set_draw_radius);
	ClassDB::bind_method(D_METHOD("get_draw_radius"), &CellGridVisualizer::get_draw_radius);
	ClassDB::bind_method(D_METHOD("set_grid_color", "c"), &CellGridVisualizer::set_grid_color);
	ClassDB::bind_method(D_METHOD("get_grid_color"), &CellGridVisualizer::get_grid_color);
	ClassDB::bind_method(D_METHOD("set_active_color", "c"), &CellGridVisualizer::set_active_color);
	ClassDB::bind_method(D_METHOD("get_active_color"), &CellGridVisualizer::get_active_color);
	ClassDB::bind_method(D_METHOD("set_player_color", "c"), &CellGridVisualizer::set_player_color);
	ClassDB::bind_method(D_METHOD("get_player_color"), &CellGridVisualizer::get_player_color);
	ClassDB::bind_method(D_METHOD("set_show_coordinates", "s"), &CellGridVisualizer::set_show_coordinates);
	ClassDB::bind_method(D_METHOD("get_show_coordinates"), &CellGridVisualizer::get_show_coordinates);

	ADD_PROPERTY(PropertyInfo(Variant::REAL, "cell_size"), "set_cell_size", "get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "draw_radius"), "set_draw_radius", "get_draw_radius");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "grid_color"), "set_grid_color", "get_grid_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "active_color"), "set_active_color", "get_active_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "player_color"), "set_player_color", "get_player_color");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "show_coordinates"), "set_show_coordinates", "get_show_coordinates");
}
