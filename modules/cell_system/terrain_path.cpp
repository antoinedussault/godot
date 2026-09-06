#include "terrain_path.h"

#include "cell_terrain.h"
#include "cell_terrain_manager.h"
#include "terrain_path_point.h"

const float TerrainPath::REAPPLY_DEBOUNCE_SEC = 0.2f;

static void _closest_point_on_segment_xz(float px, float pz, const Vector3 &a, const Vector3 &b, float &out_dist_xz, float &out_y) {
	float abx = b.x - a.x, abz = b.z - a.z;
	float apx = px - a.x, apz = pz - a.z;
	float ab_len2 = abx * abx + abz * abz;
	float t = ab_len2 > CMP_EPSILON ? CLAMP((apx * abx + apz * abz) / ab_len2, 0.0f, 1.0f) : 0.0f;
	float cx = a.x + abx * t;
	float cz = a.z + abz * t;
	float dx = px - cx, dz = pz - cz;
	out_dist_xz = Math::sqrt(dx * dx + dz * dz);
	out_y = Math::lerp(a.y, b.y, t);
}

CellTerrainManager *TerrainPath::_get_manager() const {
	if (terrain_manager_path.is_empty() || !has_node(terrain_manager_path)) return nullptr;
	return Object::cast_to<CellTerrainManager>(get_node(terrain_manager_path));
}

void TerrainPath::_restore_cells(const Vector<int64_t> &p_keys, Set<int64_t> &r_touched) {
	CellTerrainManager *manager = _get_manager();
	for (int i = 0; i < p_keys.size(); i++) {
		int64_t key = p_keys[i];
		Map<int64_t, PoolRealArray>::Element *E = _baseline.find(key);
		if (!E) continue;
		if (manager) {
			int cx = (int32_t)(key >> 32);
			int cz = (int32_t)(key & 0xffffffff);
			CellTerrain *cell = manager->get_cell(cx, cz);
			if (cell) {
				cell->set_heights(E->get());
				r_touched.insert(key);
			}
		}
		_baseline.erase(key);
	}
}

void TerrainPath::reapply() {
	CellTerrainManager *manager = _get_manager();
	if (!manager) return;

	Vector<Vector3> world_pts;
	for (int i = 0; i < get_child_count(); i++) {
		TerrainPathPoint *p = Object::cast_to<TerrainPathPoint>(get_child(i));
		if (p) world_pts.push_back(p->get_global_transform().origin);
	}
	int n = world_pts.size();

	if (n < 2) {
		Vector<int64_t> keys;
		for (Map<int64_t, PoolRealArray>::Element *E = _baseline.front(); E; E = E->next()) keys.push_back(E->key());
		Set<int64_t> touched;
		_restore_cells(keys, touched);
		if (!touched.empty()) manager->rebuild_region(touched);
		return;
	}

	const int vps = manager->get_verts_per_side();
	const int seg = vps - 1;
	const float cell_size = manager->get_cell_size();
	const float spacing = cell_size / (float)seg;
	if (seg <= 0 || spacing <= 0.0f) return;

	const float half_w = MAX(0.0f, width) * 0.5f;
	const float fall = MAX(0.0f, falloff_width);
	const float reach = half_w + fall;
	if (reach <= 0.0f) return;

	float minx = world_pts[0].x, maxx = world_pts[0].x;
	float minz = world_pts[0].z, maxz = world_pts[0].z;
	for (int i = 1; i < n; i++) {
		minx = MIN(minx, world_pts[i].x); maxx = MAX(maxx, world_pts[i].x);
		minz = MIN(minz, world_pts[i].z); maxz = MAX(maxz, world_pts[i].z);
	}
	minx -= reach; maxx += reach; minz -= reach; maxz += reach;

	int gx0 = (int)Math::floor(minx / spacing);
	int gx1 = (int)Math::ceil(maxx / spacing);
	int gz0 = (int)Math::floor(minz / spacing);
	int gz1 = (int)Math::ceil(maxz / spacing);

	Set<int64_t> touched;
	const float clamped_strength = CLAMP(strength, 0.0f, 1.0f);

	for (int gz = gz0; gz <= gz1; gz++) {
		for (int gx = gx0; gx <= gx1; gx++) {
			float vx = gx * spacing;
			float vz = gz * spacing;

			float best_dist = 1e30f;
			float best_y = 0.0f;
			for (int i = 0; i < n - 1; i++) {
				float d, y;
				_closest_point_on_segment_xz(vx, vz, world_pts[i], world_pts[i + 1], d, y);
				if (d < best_dist) { best_dist = d; best_y = y; }
			}
			if (best_dist > reach) continue;

			float fo;
			if (best_dist <= half_w) {
				fo = 1.0f;
			} else {
				float t01 = fall > 0.0f ? 1.0f - (best_dist - half_w) / fall : 0.0f;
				fo = terrain_falloff(t01, falloff_type);
			}
			if (fo <= 0.0f) continue;

			int cxs[2], lxs[2], czs[2], lzs[2];
			int nx = terrain_axis_cells(gx, seg, cxs, lxs);
			int nz = terrain_axis_cells(gz, seg, czs, lzs);
			float target_h = best_y + height_offset;

			for (int ix = 0; ix < nx; ix++) {
				for (int iz = 0; iz < nz; iz++) {
					int cx = cxs[ix], lx = lxs[ix];
					int cz = czs[iz], lz = lzs[iz];
					CellTerrain *cell = manager->get_cell(cx, cz);
					if (!cell) continue;
					int64_t key = ((int64_t)(uint32_t)cx << 32) | (uint32_t)cz;

					Map<int64_t, PoolRealArray>::Element *base = _baseline.find(key);
					if (!base) base = _baseline.insert(key, cell->get_heights());

					int idx = CLAMP(lz, 0, vps - 1) * vps + CLAMP(lx, 0, vps - 1);
					float base_h;
					if (idx >= 0 && idx < base->get().size()) {
						PoolRealArray::Read br = base->get().read();
						base_h = br[idx];
					} else {
						base_h = cell->get_height_at(lx, lz);
					}
					float nh = Math::lerp(base_h, target_h, clamped_strength * fo);
					cell->set_height_at(lx, lz, nh);
					touched.insert(key);
				}
			}
		}
	}

	// Cells this path used to cover but no longer does (path shrank/moved):
	// restore to baseline and drop, so it stops shaping ground it no longer touches.
	Vector<int64_t> to_restore;
	for (Map<int64_t, PoolRealArray>::Element *E = _baseline.front(); E; E = E->next()) {
		if (!touched.has(E->key())) to_restore.push_back(E->key());
	}
	_restore_cells(to_restore, touched);

	if (touched.empty()) return;
	manager->rebuild_region(touched);

	// Debounced HLOD re-bake, same pattern as the sculpt brush — fires once
	// editing has settled instead of on every single reapply.
	_hlod_rebake_timer = 1.0f;
	set_process(true);
}

void TerrainPath::refresh_baseline() {
	CellTerrainManager *manager = _get_manager();
	Vector<int64_t> keys;
	for (Map<int64_t, PoolRealArray>::Element *E = _baseline.front(); E; E = E->next()) keys.push_back(E->key());
	Set<int64_t> touched;
	_restore_cells(keys, touched);
	if (manager && !touched.empty()) {
		manager->rebuild_region(touched);
	}
	reapply();
}

void TerrainPath::remove_effect() {
	CellTerrainManager *manager = _get_manager();
	Vector<int64_t> keys;
	for (Map<int64_t, PoolRealArray>::Element *E = _baseline.front(); E; E = E->next()) keys.push_back(E->key());
	Set<int64_t> touched;
	_restore_cells(keys, touched);
	if (manager && !touched.empty()) {
		manager->rebuild_region(touched);
		manager->rebake_dirty_hlod();
	}
	// Cancel any pending debounced reapply so it doesn't undo this restore.
	_reapply_dirty = false;
	_hlod_rebake_timer = 0.0f;
}

void TerrainPath::notify_point_changed() { _request_reapply(); }

void TerrainPath::_request_reapply() {
	_reapply_dirty = true;
	_reapply_timer = REAPPLY_DEBOUNCE_SEC;
	set_process(true);
}

void TerrainPath::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY:
			_request_reapply();
			break;
		case NOTIFICATION_PROCESS: {
			bool active = false;
			if (_reapply_dirty) {
				active = true;
				_reapply_timer -= get_process_delta_time();
				if (_reapply_timer <= 0.0f) {
					_reapply_dirty = false;
					reapply();
				}
			}
			if (_hlod_rebake_timer > 0.0f) {
				active = true;
				_hlod_rebake_timer -= get_process_delta_time();
				if (_hlod_rebake_timer <= 0.0f) {
					_hlod_rebake_timer = 0.0f;
					CellTerrainManager *manager = _get_manager();
					if (manager && manager->has_dirty_hlod()) manager->rebake_dirty_hlod();
				}
			}
			if (!active) set_process(false);
			break;
		}
	}
}

void TerrainPath::set_terrain_manager_path(const NodePath &p) { terrain_manager_path = p; _request_reapply(); }
NodePath TerrainPath::get_terrain_manager_path() const { return terrain_manager_path; }

void TerrainPath::set_width(float w) { width = MAX(0.0f, w); _request_reapply(); }
float TerrainPath::get_width() const { return width; }
void TerrainPath::set_falloff_width(float w) { falloff_width = MAX(0.0f, w); _request_reapply(); }
float TerrainPath::get_falloff_width() const { return falloff_width; }
void TerrainPath::set_falloff_type(int t) { falloff_type = t; _request_reapply(); }
int TerrainPath::get_falloff_type() const { return falloff_type; }
void TerrainPath::set_height_offset(float h) { height_offset = h; _request_reapply(); }
float TerrainPath::get_height_offset() const { return height_offset; }
void TerrainPath::set_strength(float s) { strength = CLAMP(s, 0.0f, 1.0f); _request_reapply(); }
float TerrainPath::get_strength() const { return strength; }

void TerrainPath::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_terrain_manager_path", "p"), &TerrainPath::set_terrain_manager_path);
	ClassDB::bind_method(D_METHOD("get_terrain_manager_path"), &TerrainPath::get_terrain_manager_path);
	ClassDB::bind_method(D_METHOD("set_width", "w"), &TerrainPath::set_width);
	ClassDB::bind_method(D_METHOD("get_width"), &TerrainPath::get_width);
	ClassDB::bind_method(D_METHOD("set_falloff_width", "w"), &TerrainPath::set_falloff_width);
	ClassDB::bind_method(D_METHOD("get_falloff_width"), &TerrainPath::get_falloff_width);
	ClassDB::bind_method(D_METHOD("set_falloff_type", "t"), &TerrainPath::set_falloff_type);
	ClassDB::bind_method(D_METHOD("get_falloff_type"), &TerrainPath::get_falloff_type);
	ClassDB::bind_method(D_METHOD("set_height_offset", "h"), &TerrainPath::set_height_offset);
	ClassDB::bind_method(D_METHOD("get_height_offset"), &TerrainPath::get_height_offset);
	ClassDB::bind_method(D_METHOD("set_strength", "s"), &TerrainPath::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &TerrainPath::get_strength);
	ClassDB::bind_method(D_METHOD("notify_point_changed"), &TerrainPath::notify_point_changed);
	ClassDB::bind_method(D_METHOD("reapply"), &TerrainPath::reapply);
	ClassDB::bind_method(D_METHOD("refresh_baseline"), &TerrainPath::refresh_baseline);
	ClassDB::bind_method(D_METHOD("remove_effect"), &TerrainPath::remove_effect);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "terrain_manager_path"), "set_terrain_manager_path", "get_terrain_manager_path");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "width", PROPERTY_HINT_RANGE, "0,256,0.1"), "set_width", "get_width");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "falloff_width", PROPERTY_HINT_RANGE, "0,256,0.1"), "set_falloff_width", "get_falloff_width");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "falloff_type", PROPERTY_HINT_ENUM, "Smooth,Linear,Spherical,Tip"), "set_falloff_type", "get_falloff_type");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "height_offset", PROPERTY_HINT_RANGE, "-1000,1000,0.01"), "set_height_offset", "get_height_offset");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "strength", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_strength", "get_strength");
}
