#include "foliage_manager.h"

#include "cell_terrain_manager.h"
#include "cell_terrain.h"
#include "core/engine.h"
#include "core/os/file_access.h"
#include "core/os/dir_access.h"
#include "core/math/random_number_generator.h"
#include "scene/3d/camera.h"
#include "scene/main/viewport.h"

int64_t FoliageManager::_key(int x, int z) { return ((int64_t)(uint32_t)x << 32) | (uint32_t)z; }
void    FoliageManager::_unkey(int64_t k, int &x, int &z) { x = (int32_t)(k >> 32); z = (int32_t)(k & 0xffffffff); }

FoliageManager::FoliageManager() {}

CellTerrainManager *FoliageManager::_get_terrain_manager() const {
	if (terrain_manager_path.is_empty()) return NULL;
	Node *n = const_cast<FoliageManager *>(this)->get_node_or_null(terrain_manager_path);
	return Object::cast_to<CellTerrainManager>(n);
}

Camera *FoliageManager::_get_active_camera() const {
	if (!is_inside_tree()) return NULL;
	Viewport *vp = const_cast<FoliageManager *>(this)->get_viewport();
	return vp ? vp->get_camera() : NULL;
}

Vector<Ref<FoliageType>> FoliageManager::_gather_types() const {
	// types is already a flat Vector — return it directly.
	return types;
}

int FoliageManager::get_type_count() const {
	return types.size();
}

Ref<FoliageType> FoliageManager::get_type(int p_index) const {
	if (p_index < 0 || p_index >= types.size()) return Ref<FoliageType>();
	return types[p_index];
}

void FoliageManager::set_type(int idx, const Ref<FoliageType> &t) {
	if (idx < 0) return;
	if (idx >= types.size()) types.resize(idx + 1);
	types.write[idx] = t;
}

Ref<FoliageType> FoliageManager::get_type_slot(int idx) const {
	if (idx < 0 || idx >= types.size()) return Ref<FoliageType>();
	return types[idx];
}

Array FoliageManager::get_types_array() const {
	Array a;
	for (int i = 0; i < types.size(); i++)
		a.push_back(types[i]);
	return a;
}

void FoliageManager::set_types_array(const Array &arr) {
	types.clear();
	for (int i = 0; i < arr.size(); i++) {
		Ref<FoliageType> ft = arr[i];
		types.push_back(ft);
	}
}

void FoliageManager::_get_property_list(List<PropertyInfo> *p_list) const {
	for (int i = 0; i <= types.size(); i++) {
		p_list->push_back(PropertyInfo(Variant::OBJECT,
				"types/" + itos(i),
				PROPERTY_HINT_RESOURCE_TYPE, "FoliageType",
				PROPERTY_USAGE_DEFAULT));
	}
}

bool FoliageManager::_get(const StringName &p_name, Variant &r_ret) const {
	String name = p_name;
	if (!name.begins_with("types/")) return false;
	int idx = name.substr(6).to_int();
	r_ret = (idx >= 0 && idx < types.size()) ? Variant(types[idx]) : Variant(Ref<FoliageType>());
	return true;
}

bool FoliageManager::_set(const StringName &p_name, const Variant &p_value) {
	String name = p_name;
	if (!name.begins_with("types/")) return false;
	int idx = name.substr(6).to_int();
	if (idx < 0) return false;
	Ref<FoliageType> ft = p_value;
	if (ft.is_null()) {
		if (idx < types.size()) types.remove(idx);
	} else {
		if (idx >= types.size()) types.resize(idx + 1);
		types.write[idx] = ft;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Terrain sync
// ---------------------------------------------------------------------------

void FoliageManager::world_to_cell(const Vector3 &p_world, int &r_cx, int &r_cz) const {
	r_cx = (int)Math::floor(p_world.x / cell_size);
	r_cz = (int)Math::floor(p_world.z / cell_size);
}

Vector3 FoliageManager::cell_world_origin(int cx, int cz) const {
	return Vector3(cx * cell_size, 0.0f, cz * cell_size);
}

String FoliageManager::_foliage_path(int cx, int cz) const {
	return world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".foliage");
}

PoolRealArray FoliageManager::_load_cell(int cx, int cz) const {
	PoolRealArray data;
	String path = _foliage_path(cx, cz);
	if (!FileAccess::exists(path)) return data;
	FileAccess *f = FileAccess::open(path, FileAccess::READ);
	if (!f) return data;
	int count = (int)f->get_32();
	data.resize(count);
	{
		PoolRealArray::Write w = data.write();
		for (int i = 0; i < count; i++)
			w[i] = f->eof_reached() ? 0.0f : f->get_float();
	}
	f->close();
	memdelete(f);
	return data;
}

void FoliageManager::_save_cell(int cx, int cz, CellFoliage *p_cf) {
	if (!p_cf) return;
	DirAccess *da = DirAccess::create_for_path(world_dir);
	if (da) { da->make_dir_recursive(world_dir); memdelete(da); }
	PoolRealArray data = p_cf->serialize();
	String path = _foliage_path(cx, cz);
	FileAccess *f = FileAccess::open(path, FileAccess::WRITE);
	if (!f) return;
	f->store_32(data.size());
	{
		PoolRealArray::Read r = data.read();
		for (int i = 0; i < data.size(); i++)
			f->store_float(r[i]);
	}
	f->close();
	memdelete(f);
}

CellFoliage *FoliageManager::_spawn_for_cell(int cx, int cz, CellTerrain *p_terrain) {
	CellFoliage *cf = memnew(CellFoliage);
	cf->set_cell_size(cell_size);
	cf->configure_types(_gather_types());
	add_child(cf);
	Transform xf;
	xf.origin = cell_world_origin(cx, cz);
	cf->set_transform(xf);
	PoolRealArray data = _load_cell(cx, cz);
	if (data.size() > 0) cf->deserialize(data);
	cf->rebuild();
	return cf;
}

void FoliageManager::_load_all_persistent() {
	// Skip the scan entirely if no type is persistent (no trees, no keep_loaded).
	bool any_persistent = false;
	for (int i = 0; i < types.size(); i++) {
		if (types[i].is_valid() && types[i]->is_persistent()) { any_persistent = true; break; }
	}
	if (!any_persistent) return;

	DirAccess *da = DirAccess::open(world_dir);
	if (!da) return;

	Vector<int64_t> cells;
	da->list_dir_begin();
	String fname = da->get_next();
	while (fname != "") {
		if (!da->current_is_dir() && fname.begins_with("cell_") && fname.ends_with(".foliage")) {
			// "cell_<cx>_<cz>.foliage" -> strip 5-char prefix and 8-char suffix.
			String mid = fname.substr(5, fname.length() - 13);
			Vector<String> parts = mid.split("_");
			if (parts.size() == 2)
				cells.push_back(_key(parts[0].to_int(), parts[1].to_int()));
		}
		fname = da->get_next();
	}
	da->list_dir_end();
	memdelete(da);

	for (int i = 0; i < cells.size(); i++) {
		int64_t k = cells[i];
		if (_active.has(k)) continue;
		int cx, cz;
		_unkey(k, cx, cz);

		CellFoliage *cf = memnew(CellFoliage);
		cf->set_cell_size(cell_size);
		cf->configure_types(_gather_types());
		add_child(cf);
		Transform xf;
		xf.origin = cell_world_origin(cx, cz);
		cf->set_transform(xf);
		PoolRealArray data = _load_cell(cx, cz);
		if (data.size() > 0) cf->deserialize(data);

		// Only keep cells that actually contain a keep_loaded type; others are
		// left for normal terrain streaming.
		if (!cf->has_persistent_instances()) {
			cf->queue_delete();
			continue;
		}
		cf->set_persistent_only(true);
		cf->rebuild();
		_active[k] = cf;
	}
}

void FoliageManager::_sync_with_terrain() {
	CellTerrainManager *tm = _get_terrain_manager();
	if (!tm) return;

	List<int64_t> remove;
	for (Map<int64_t, CellFoliage *>::Element *E = _active.front(); E; E = E->next()) {
		int x, z;
		_unkey(E->key(), x, z);
		if (!tm->get_cell(x, z))
			remove.push_back(E->key());
	}
	for (List<int64_t>::Element *E = remove.front(); E; E = E->next()) {
		int x, z;
		_unkey(E->get(), x, z);
		CellFoliage *cf = _active[E->get()];
		if (!cf) { _active.erase(E->get()); continue; }
		if (_dirty.has(E->get())) {
			_save_cell(x, z, cf);
			_dirty.erase(E->get());
		}
		// Cells with persistent (tree) content stay loaded; their full/imposter
		// mode is managed by distance in _update_lod_modes(). Everything else
		// (grass-only cells) unloads with its terrain cell.
		if (cf->has_persistent_instances()) continue;
		cf->queue_delete();
		_active.erase(E->get());
	}

	if (!_has_cam_cell) return;
	const int probe = 32;
	for (int z = _cam_cz - probe; z <= _cam_cz + probe; z++) {
		for (int x = _cam_cx - probe; x <= _cam_cx + probe; x++) {
			CellTerrain *ct = tm->get_cell(x, z);
			if (!ct) continue;
			int64_t k = _key(x, z);
			// Already loaded: full/imposter mode is owned by _update_lod_modes().
			if (_active.has(k)) continue;
			_active[k] = _spawn_for_cell(x, z, ct);
		}
	}
}

void FoliageManager::_update_lod_modes() {
	if (!_has_cam_cell) return;
	for (Map<int64_t, CellFoliage *>::Element *E = _active.front(); E; E = E->next()) {
		CellFoliage *cf = E->value();
		if (!cf) continue;
		int x, z;
		_unkey(E->key(), x, z);
		int d = MAX(ABS(x - _cam_cx), ABS(z - _cam_cz));
		bool want_full = (d <= tree_full_lod_radius);
		if (want_full) {
			// Near: build full LOD meshes so close trees render as meshes, not
			// edge-on imposters.
			if (cf->get_persistent_only()) {
				cf->set_persistent_only(false);
				cf->rebuild();
			}
		} else if (!cf->get_persistent_only() && cf->has_persistent_instances()) {
			// Far: drop the LOD meshes down to imposter-only to bound cost.
			cf->set_persistent_only(true);
			cf->rebuild();
		}
	}
}

void FoliageManager::_update_all_lod(const Vector3 &cam_world) {
	for (Map<int64_t, CellFoliage *>::Element *E = _active.front(); E; E = E->next()) {
		int x, z;
		_unkey(E->key(), x, z);
		E->value()->update_lod(cam_world, cell_world_origin(x, z));
	}
}

void FoliageManager::_update_collision(const Vector3 &cam_world) {
	int ccx, ccz;
	world_to_cell(cam_world, ccx, ccz);
	for (Map<int64_t, CellFoliage *>::Element *E = _active.front(); E; E = E->next()) {
		int x, z;
		_unkey(E->key(), x, z);
		bool near = (ABS(x - ccx) <= tree_collision_radius && ABS(z - ccz) <= tree_collision_radius);
		E->value()->set_collision_active(near, cell_world_origin(x, z));
	}
}

void FoliageManager::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_process(true);
			if (!Engine::get_singleton()->is_editor_hint())
				_load_all_persistent();
		} break;

		case NOTIFICATION_PROCESS: {
			if (Engine::get_singleton()->is_editor_hint()) break;
			Camera *cam = _get_active_camera();
			if (!cam) return;
			Vector3 cam_world = cam->get_global_transform().origin;
			world_to_cell(cam_world, _cam_cx, _cam_cz);
			_has_cam_cell = true;

			CellTerrainManager *tm = _get_terrain_manager();
			if (tm) tm->stream_to(cam_world);

			_sync_with_terrain();
			_update_lod_modes();

			float dt = get_process_delta_time();
			_accum += dt;
			if (lod_update_interval <= 0.0f || _accum >= lod_update_interval) {
				_accum = 0.0f;
				_update_all_lod(cam_world);
				_update_collision(cam_world);
			}
		} break;

		case NOTIFICATION_EXIT_TREE: {
			save_dirty();
			_active.clear();
			_dirty.clear();
			_has_cam_cell = false;
		} break;
	}
}

// ---------------------------------------------------------------------------
// Paint API
// ---------------------------------------------------------------------------

CellFoliage *FoliageManager::get_cell_foliage(int cx, int cz) const {
	const Map<int64_t, CellFoliage *>::Element *E = _active.find(_key(cx, cz));
	return E ? E->value() : NULL;
}

void FoliageManager::mark_dirty(int cx, int cz) { _dirty.insert(_key(cx, cz)); }

void FoliageManager::editor_stream_to(const Vector3 &p_world_pos) {
	world_to_cell(p_world_pos, _cam_cx, _cam_cz);
	_has_cam_cell = true;
	CellTerrainManager *tm = _get_terrain_manager();
	if (tm) tm->stream_to(p_world_pos);
	_sync_with_terrain();
	_update_all_lod(p_world_pos);
	_update_collision(p_world_pos);
}

void FoliageManager::save_dirty() {
	List<int64_t> keys;
	for (Set<int64_t>::Element *E = _dirty.front(); E; E = E->next())
		keys.push_back(E->get());
	for (List<int64_t>::Element *E = keys.front(); E; E = E->next()) {
		int x, z;
		_unkey(E->get(), x, z);
		CellFoliage *cf = get_cell_foliage(x, z);
		if (cf) _save_cell(x, z, cf);
	}
	_dirty.clear();
}

void FoliageManager::rebuild_cell(int cx, int cz) {
	CellFoliage *cf = get_cell_foliage(cx, cz);
	if (cf) cf->rebuild();
}

static bool _sample_ground(CellTerrainManager *tm, float cell_size,
		const Vector3 &world_xz, float &out_y, Vector3 &out_normal) {
	if (!tm) return false;
	int cx = (int)Math::floor(world_xz.x / cell_size);
	int cz = (int)Math::floor(world_xz.z / cell_size);
	CellTerrain *ct = tm->get_cell(cx, cz);
	if (!ct) return false;
	int   vps     = ct->get_verts_per_side();
	float spacing = cell_size / (float)(vps - 1);
	float hscale  = ct->get_height_scale();
	float local_x = world_xz.x - cx * cell_size;
	float local_z = world_xz.z - cz * cell_size;
	int   lx      = CLAMP((int)Math::round(local_x / spacing), 0, vps - 1);
	int   lz      = CLAMP((int)Math::round(local_z / spacing), 0, vps - 1);
	out_y = ct->get_height_at(lx, lz) * hscale;
	float hl = ct->get_height_at(CLAMP(lx - 1, 0, vps - 1), lz) * hscale;
	float hr = ct->get_height_at(CLAMP(lx + 1, 0, vps - 1), lz) * hscale;
	float hd = ct->get_height_at(lx, CLAMP(lz - 1, 0, vps - 1)) * hscale;
	float hu = ct->get_height_at(lx, CLAMP(lz + 1, 0, vps - 1)) * hscale;
	out_normal = Vector3(hl - hr, 2.0f * spacing, hd - hu).normalized();
	return true;
}

int FoliageManager::paint_scatter(int p_type_index, const Vector3 &p_world_center,
		float p_radius, float p_strength) {
	CellTerrainManager *tm = _get_terrain_manager();
	Ref<FoliageType> ft = get_type(p_type_index);
	if (ft.is_null() || p_radius <= 0.0f) return 0;

	float area     = Math_PI * p_radius * p_radius;
	int   attempts = (int)(ft->get_density() * area * CLAMP(p_strength, 0.0f, 1.0f));
	if (attempts <= 0) attempts = 1;

	Ref<RandomNumberGenerator> rng;
	rng.instance();
	rng->randomize();

	float min_s    = ft->get_min_scale();
	float max_s    = ft->get_max_scale();
	bool  ryaw     = ft->get_random_yaw();
	bool  align    = ft->get_align_to_normal();
	float align_amt = ft->get_align_amount();
	float slope_cos = Math::cos(Math::deg2rad(ft->get_slope_limit_deg()));
	float y_off_min = ft->get_y_offset_min();
	float y_off_max = ft->get_y_offset_max();

	int added = 0;
	for (int a = 0; a < attempts; a++) {
		float ang = rng->randf_range(0.0f, Math_TAU);
		float rr  = p_radius * Math::sqrt(rng->randf());
		float wx  = p_world_center.x + Math::cos(ang) * rr;
		float wz  = p_world_center.z + Math::sin(ang) * rr;

		float   gy;
		Vector3 gnormal;
		if (!_sample_ground(tm, cell_size, Vector3(wx, 0, wz), gy, gnormal)) continue;
		if (gnormal.y < slope_cos) continue;

		int cx, cz;
		world_to_cell(Vector3(wx, 0, wz), cx, cz);
		CellFoliage *cf = get_cell_foliage(cx, cz);
		if (!cf) continue;

		float y_off = (y_off_min != y_off_max)
				? rng->randf_range(y_off_min, y_off_max) : y_off_min;
		Vector3 local_origin(wx - cx * cell_size, gy + y_off, wz - cz * cell_size);
		Basis basis;
		if (align && align_amt > 0.0f) {
			Vector3 up    = Vector3(0, 1, 0).linear_interpolate(gnormal, align_amt).normalized();
			Vector3 right = Vector3(1, 0, 0);
			Vector3 fwd   = up.cross(right).normalized();
			right = fwd.cross(up).normalized();
			basis.elements[0] = right;
			basis.elements[1] = up;
			basis.elements[2] = fwd;
		}
		if (ryaw) {
			basis.rotate(basis.elements[1].normalized(), rng->randf_range(0.0f, Math_TAU));
		}
		float s = rng->randf_range(min_s, max_s);
		basis.scale(Vector3(s, s, s));

		Transform xf;
		xf.basis  = basis;
		xf.origin = local_origin;

		cf->add_instance(p_type_index, xf);
		mark_dirty(cx, cz);
		added++;
	}

	int min_cx, min_cz, max_cx, max_cz;
	world_to_cell(p_world_center - Vector3(p_radius, 0, p_radius), min_cx, min_cz);
	world_to_cell(p_world_center + Vector3(p_radius, 0, p_radius), max_cx, max_cz);
	for (int z = min_cz; z <= max_cz; z++)
		for (int x = min_cx; x <= max_cx; x++)
			rebuild_cell(x, z);
	return added;
}

bool FoliageManager::place_single(int p_type_index, const Vector3 &p_world_pos,
		const Vector3 &p_world_normal) {
	Ref<FoliageType> ft = get_type(p_type_index);
	if (ft.is_null()) return false;
	int cx, cz;
	world_to_cell(p_world_pos, cx, cz);
	CellFoliage *cf = get_cell_foliage(cx, cz);
	if (!cf) return false;

	Ref<RandomNumberGenerator> rng;
	rng.instance();
	rng->randomize();

	Basis basis;
	if (ft->get_random_yaw()) {
		basis.rotate(Vector3(0, 1, 0), rng->randf_range(0.0f, Math_TAU));
	}
	if (ft->get_align_to_normal() && ft->get_align_amount() > 0.0f
			&& p_world_normal.length() > 0.001f) {
		Vector3 up    = Vector3(0, 1, 0).linear_interpolate(p_world_normal, ft->get_align_amount()).normalized();
		Vector3 right = Vector3(1, 0, 0);
		Vector3 fwd   = up.cross(right).normalized();
		right = fwd.cross(up).normalized();
		Basis ab;
		ab.elements[0] = right;
		ab.elements[1] = up;
		ab.elements[2] = fwd;
		basis = ab * basis;
	}
	float s = rng->randf_range(ft->get_min_scale(), ft->get_max_scale());
	basis.scale(Vector3(s, s, s));

	Transform xf;
	xf.basis  = basis;
	xf.origin = Vector3(p_world_pos.x - cx * cell_size, p_world_pos.y,
			p_world_pos.z - cz * cell_size);
	cf->add_instance(p_type_index, xf);
	mark_dirty(cx, cz);
	rebuild_cell(cx, cz);
	return true;
}

int FoliageManager::erase(int p_type_index, const Vector3 &p_world_center, float p_radius) {
	int total = 0;
	int min_cx, min_cz, max_cx, max_cz;
	world_to_cell(p_world_center - Vector3(p_radius, 0, p_radius), min_cx, min_cz);
	world_to_cell(p_world_center + Vector3(p_radius, 0, p_radius), max_cx, max_cz);
	for (int z = min_cz; z <= max_cz; z++) {
		for (int x = min_cx; x <= max_cx; x++) {
			CellFoliage *cf = get_cell_foliage(x, z);
			if (!cf) continue;
			int removed = cf->erase_in_radius(p_type_index, p_world_center,
					cell_world_origin(x, z), p_radius);
			if (removed > 0) {
				mark_dirty(x, z);
				cf->rebuild();
				total += removed;
			}
		}
	}
	return total;
}

// --- Undo/redo ---

PoolRealArray FoliageManager::get_cell_data(int cx, int cz) const {
	CellFoliage *cf = get_cell_foliage(cx, cz);
	return cf ? cf->serialize() : PoolRealArray();
}

void FoliageManager::apply_cell_data(int cx, int cz, const PoolRealArray &data) {
	CellFoliage *cf = get_cell_foliage(cx, cz);
	if (!cf) return;
	cf->deserialize(data);
	cf->rebuild();
	mark_dirty(cx, cz);
}

// --- Property accessors ---

void     FoliageManager::set_terrain_manager_path(const NodePath &p) { terrain_manager_path = p; }
NodePath FoliageManager::get_terrain_manager_path() const             { return terrain_manager_path; }
void     FoliageManager::set_world_dir(const String &p)               { world_dir = p; }
String   FoliageManager::get_world_dir() const                        { return world_dir; }
void     FoliageManager::set_cell_size(float s)                       { cell_size = s; }
float    FoliageManager::get_cell_size() const                        { return cell_size; }
void     FoliageManager::set_tree_collision_radius(int r)             { tree_collision_radius = MAX(0, r); }
int      FoliageManager::get_tree_collision_radius() const            { return tree_collision_radius; }
void     FoliageManager::set_tree_full_lod_radius(int r)              { tree_full_lod_radius = MAX(0, r); }
int      FoliageManager::get_tree_full_lod_radius() const             { return tree_full_lod_radius; }
void     FoliageManager::set_lod_update_interval(float s)             { lod_update_interval = MAX(0.0f, s); }
float    FoliageManager::get_lod_update_interval() const              { return lod_update_interval; }

// --- _bind_methods ---

void FoliageManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_terrain_manager_path", "p"), &FoliageManager::set_terrain_manager_path);
	ClassDB::bind_method(D_METHOD("get_terrain_manager_path"),      &FoliageManager::get_terrain_manager_path);
	ClassDB::bind_method(D_METHOD("set_world_dir", "p"),            &FoliageManager::set_world_dir);
	ClassDB::bind_method(D_METHOD("get_world_dir"),                 &FoliageManager::get_world_dir);
	ClassDB::bind_method(D_METHOD("set_cell_size", "s"),            &FoliageManager::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"),                 &FoliageManager::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_tree_collision_radius", "r"), &FoliageManager::set_tree_collision_radius);
	ClassDB::bind_method(D_METHOD("get_tree_collision_radius"),      &FoliageManager::get_tree_collision_radius);
	ClassDB::bind_method(D_METHOD("set_tree_full_lod_radius", "r"),  &FoliageManager::set_tree_full_lod_radius);
	ClassDB::bind_method(D_METHOD("get_tree_full_lod_radius"),       &FoliageManager::get_tree_full_lod_radius);
	ClassDB::bind_method(D_METHOD("set_lod_update_interval", "s"),  &FoliageManager::set_lod_update_interval);
	ClassDB::bind_method(D_METHOD("get_lod_update_interval"),       &FoliageManager::get_lod_update_interval);

	ClassDB::bind_method(D_METHOD("set_types_array", "arr"),        &FoliageManager::set_types_array);
	ClassDB::bind_method(D_METHOD("get_types_array"),               &FoliageManager::get_types_array);

	ClassDB::bind_method(D_METHOD("save_dirty"),                    &FoliageManager::save_dirty);
	ClassDB::bind_method(D_METHOD("editor_stream_to", "world_pos"), &FoliageManager::editor_stream_to);
	ClassDB::bind_method(D_METHOD("mark_dirty", "cx", "cz"),        &FoliageManager::mark_dirty);
	ClassDB::bind_method(D_METHOD("rebuild_cell", "cx", "cz"),      &FoliageManager::rebuild_cell);
	ClassDB::bind_method(D_METHOD("paint_scatter", "type_index", "world_center", "radius", "strength"), &FoliageManager::paint_scatter);
	ClassDB::bind_method(D_METHOD("place_single",  "type_index", "world_pos", "world_normal"),          &FoliageManager::place_single);
	ClassDB::bind_method(D_METHOD("erase",         "type_index", "world_center", "radius"),             &FoliageManager::erase);
	ClassDB::bind_method(D_METHOD("get_cell_data", "cx", "cz"),     &FoliageManager::get_cell_data);
	ClassDB::bind_method(D_METHOD("apply_cell_data", "cx", "cz", "data"), &FoliageManager::apply_cell_data);
	ClassDB::bind_method(D_METHOD("get_type_count"),                &FoliageManager::get_type_count);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "terrain_manager_path",
			PROPERTY_HINT_NODE_PATH_VALID_TYPES, "CellTerrainManager"),
			"set_terrain_manager_path", "get_terrain_manager_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "world_dir"),
			"set_world_dir", "get_world_dir");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "cell_size"),
			"set_cell_size", "get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tree_collision_radius",
			PROPERTY_HINT_RANGE, "0,8,1"),
			"set_tree_collision_radius", "get_tree_collision_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tree_full_lod_radius",
			PROPERTY_HINT_RANGE, "0,32,1"),
			"set_tree_full_lod_radius", "get_tree_full_lod_radius");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "lod_update_interval",
			PROPERTY_HINT_RANGE, "0,1,0.01"),
			"set_lod_update_interval", "get_lod_update_interval");

}
