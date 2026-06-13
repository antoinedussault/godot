#include "cell_terrain_manager.h"

#include "cell_terrain.h"
#include "core/os/dir_access.h"
#include "core/os/file_access.h"

CellTerrainManager::CellTerrainManager() {}

int64_t CellTerrainManager::_key(int x, int z) {
	return ((int64_t)(uint32_t)x << 32) | (uint32_t)z;
}
void CellTerrainManager::_unkey(int64_t k, int &x, int &z) {
	x = (int32_t)(k >> 32);
	z = (int32_t)(k & 0xffffffff);
}

Spatial *CellTerrainManager::_get_target() const {
	if (target_path.is_empty()) return nullptr;
	Node *n = get_node_or_null(target_path);
	return Object::cast_to<Spatial>(n);
}

PoolRealArray CellTerrainManager::_load_heights(int cx, int cz) const {
	const int n = verts_per_side * verts_per_side;
	PoolRealArray h;
	h.resize(n);
	PoolRealArray::Write w = h.write();

	String path = world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".heights");
	FileAccess *f = FileAccess::open(path, FileAccess::READ);
	if (f) {
		for (int i = 0; i < n; i++) {
			w[i] = f->eof_reached() ? 0.0f : f->get_float();
		}
		f->close();
		memdelete(f);
	} else {
		for (int i = 0; i < n; i++) w[i] = 0.0f; // no file yet -> flat tile
	}
	return h;
}

bool CellTerrainManager::_save_cell(int cx, int cz) {
	CellTerrain *t = get_cell(cx, cz);
	if (!t) return false;

	DirAccess *da = DirAccess::create_for_path(world_dir);
	if (da) {
		da->make_dir_recursive(world_dir);
		memdelete(da);
	}

	String path = world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".heights");
	FileAccess *f = FileAccess::open(path, FileAccess::WRITE);
	if (!f) return false;

	PoolRealArray h = t->get_heights();
	PoolRealArray::Read r = h.read();
	const int n = h.size();
	for (int i = 0; i < n; i++) f->store_float(r[i]);
	f->close();
	memdelete(f);
	return true;
}

CellTerrain *CellTerrainManager::_spawn_cell(int cx, int cz) {
	CellTerrain *t = memnew(CellTerrain);
	t->set_verts_per_side(verts_per_side);
	t->set_cell_size(cell_size);
	t->set_height_scale(height_scale);
	t->set_heights(_load_heights(cx, cz));
	add_child(t);
	Transform xf;
	xf.origin = Vector3(cx * cell_size, 0.0f, cz * cell_size);
	t->set_transform(xf);
	return t;
}

void CellTerrainManager::_update_neighbor_edges(int cx, int cz) {
	Map<int64_t, CellTerrain *>::Element *self = _active.find(_key(cx, cz));
	if (!self) return;
	const int vps = verts_per_side;

	PoolRealArray n_e, s_e, e_e, w_e; // empty => no neighbour (CellTerrain clamps)
	Map<int64_t, CellTerrain *>::Element *E;

	if ((E = _active.find(_key(cx + 1, cz)))) { // East: its column 1 is our x=vps
		PoolRealArray nh = E->value()->get_heights();
		e_e.resize(vps); PoolRealArray::Write w = e_e.write();
		for (int z = 0; z < vps; z++) w[z] = nh[z * vps + 1];
	}
	if ((E = _active.find(_key(cx - 1, cz)))) { // West: column vps-2 is our x=-1
		PoolRealArray nh = E->value()->get_heights();
		w_e.resize(vps); PoolRealArray::Write w = w_e.write();
		for (int z = 0; z < vps; z++) w[z] = nh[z * vps + (vps - 2)];
	}
	if ((E = _active.find(_key(cx, cz + 1)))) { // North (+Z): row 1 is our z=vps
		PoolRealArray nh = E->value()->get_heights();
		n_e.resize(vps); PoolRealArray::Write w = n_e.write();
		for (int x = 0; x < vps; x++) w[x] = nh[1 * vps + x];
	}
	if ((E = _active.find(_key(cx, cz - 1)))) { // South (-Z): row vps-2 is our z=-1
		PoolRealArray nh = E->value()->get_heights();
		s_e.resize(vps); PoolRealArray::Write w = s_e.write();
		for (int x = 0; x < vps; x++) w[x] = nh[(vps - 2) * vps + x];
	}

	self->value()->set_neighbor_edges(n_e, s_e, e_e, w_e);
}

void CellTerrainManager::_evaluate(int cx, int cz) {
	const int r = load_radius;
	Set<int64_t> dirty;

	// 1. Free tiles outside the block. Save first if they hold unsaved edits,
	//    otherwise sculpts near the streaming edge would be lost on unload.
	List<int64_t> remove;
	for (Map<int64_t, CellTerrain *>::Element *E = _active.front(); E; E = E->next()) {
		int x, z; _unkey(E->key(), x, z);
		if (ABS(x - cx) > r || ABS(z - cz) > r) remove.push_back(E->key());
	}
	for (List<int64_t>::Element *E = remove.front(); E; E = E->next()) {
		int x, z; _unkey(E->get(), x, z);
		if (_dirty.has(E->get())) {
			_save_cell(x, z);
			_dirty.erase(E->get());
		}
		CellTerrain *t = _active[E->get()];
		_active.erase(E->get());
		t->queue_delete();
		dirty.insert(_key(x + 1, z)); dirty.insert(_key(x - 1, z));
		dirty.insert(_key(x, z + 1)); dirty.insert(_key(x, z - 1));
	}

	// 2. Load new tiles inside the block.
	for (int z = cz - r; z <= cz + r; z++) {
		for (int x = cx - r; x <= cx + r; x++) {
			int64_t k = _key(x, z);
			if (_active.has(k)) continue;
			_active[k] = _spawn_cell(x, z);
			dirty.insert(k);
			dirty.insert(_key(x + 1, z)); dirty.insert(_key(x - 1, z));
			dirty.insert(_key(x, z + 1)); dirty.insert(_key(x, z - 1));
		}
	}

	// 3. Recompute edges + rebuild every dirty tile that is currently active.
	for (Set<int64_t>::Element *E = dirty.front(); E; E = E->next()) {
		int x, z; _unkey(E->get(), x, z);
		if (!_active.has(E->get())) continue;
		_update_neighbor_edges(x, z);
		_active[E->get()]->rebuild();
	}

	_last_cx = cx; _last_cz = cz; _has_last = true;
}

void CellTerrainManager::rebuild_at(int cx, int cz) {
	mark_dirty(cx, cz); // the centre tile's heights are what changed
	const int dx[5] = { 0, 1, -1, 0, 0 };
	const int dz[5] = { 0, 0, 0, 1, -1 };
	for (int i = 0; i < 5; i++) {
		int x = cx + dx[i], z = cz + dz[i];
		if (!_active.has(_key(x, z))) continue;
		_update_neighbor_edges(x, z);
		_active[_key(x, z)]->rebuild();
	}
}

void CellTerrainManager::mark_dirty(int cx, int cz) {
	_dirty.insert(_key(cx, cz));
}

void CellTerrainManager::save_dirty() {
	List<int64_t> keys;
	for (Set<int64_t>::Element *E = _dirty.front(); E; E = E->next()) keys.push_back(E->get());
	for (List<int64_t>::Element *E = keys.front(); E; E = E->next()) {
		int x, z; _unkey(E->get(), x, z);
		_save_cell(x, z);
	}
	_dirty.clear();
}

void CellTerrainManager::editor_focus(const Vector3 &world_pos) {
	int cx = (int)Math::floor(world_pos.x / cell_size);
	int cz = (int)Math::floor(world_pos.z / cell_size);
	if (_has_last && cx == _last_cx && cz == _last_cz) return;
	_evaluate(cx, cz);
}

void CellTerrainManager::editor_clear() {
	save_dirty();
	List<int64_t> keys;
	for (Map<int64_t, CellTerrain *>::Element *E = _active.front(); E; E = E->next()) {
		keys.push_back(E->key());
	}
	for (List<int64_t>::Element *E = keys.front(); E; E = E->next()) {
		CellTerrain *t = _active[E->get()];
		_active.erase(E->get());
		t->queue_delete();
	}
	_has_last = false;
}

CellTerrain *CellTerrainManager::get_cell(int cx, int cz) const {
	const Map<int64_t, CellTerrain *>::Element *E = _active.find(_key(cx, cz));
	return E ? E->value() : nullptr;
}

void CellTerrainManager::refresh() { _has_last = false; }

void CellTerrainManager::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_process(true);
		} break;
		case NOTIFICATION_PROCESS: {
			Spatial *tgt = _get_target();
			if (!tgt) return;
			Vector3 p = tgt->get_global_transform().origin;
			int cx = (int)Math::floor(p.x / cell_size);
			int cz = (int)Math::floor(p.z / cell_size);
			if (_has_last && cx == _last_cx && cz == _last_cz) return;
			_evaluate(cx, cz);
		} break;
	}
}

void CellTerrainManager::set_target_path(const NodePath &p) { target_path = p; }
NodePath CellTerrainManager::get_target_path() const { return target_path; }
void CellTerrainManager::set_world_dir(const String &p) { world_dir = p; }
String CellTerrainManager::get_world_dir() const { return world_dir; }
void CellTerrainManager::set_verts_per_side(int v) { verts_per_side = MAX(2, v); }
int CellTerrainManager::get_verts_per_side() const { return verts_per_side; }
void CellTerrainManager::set_cell_size(float s) { cell_size = s; }
float CellTerrainManager::get_cell_size() const { return cell_size; }
void CellTerrainManager::set_height_scale(float s) { height_scale = s; }
float CellTerrainManager::get_height_scale() const { return height_scale; }
void CellTerrainManager::set_load_radius(int r) { load_radius = MAX(0, r); }
int CellTerrainManager::get_load_radius() const { return load_radius; }

void CellTerrainManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("rebuild_at", "cx", "cz"), &CellTerrainManager::rebuild_at);
	ClassDB::bind_method(D_METHOD("mark_dirty", "cx", "cz"), &CellTerrainManager::mark_dirty);
	ClassDB::bind_method(D_METHOD("save_dirty"), &CellTerrainManager::save_dirty);
	ClassDB::bind_method(D_METHOD("editor_focus", "world_pos"), &CellTerrainManager::editor_focus);
	ClassDB::bind_method(D_METHOD("editor_clear"), &CellTerrainManager::editor_clear);
	ClassDB::bind_method(D_METHOD("refresh"), &CellTerrainManager::refresh);
	ClassDB::bind_method(D_METHOD("set_target_path", "p"), &CellTerrainManager::set_target_path);
	ClassDB::bind_method(D_METHOD("get_target_path"), &CellTerrainManager::get_target_path);
	ClassDB::bind_method(D_METHOD("set_world_dir", "p"), &CellTerrainManager::set_world_dir);
	ClassDB::bind_method(D_METHOD("get_world_dir"), &CellTerrainManager::get_world_dir);
	ClassDB::bind_method(D_METHOD("set_verts_per_side", "v"), &CellTerrainManager::set_verts_per_side);
	ClassDB::bind_method(D_METHOD("get_verts_per_side"), &CellTerrainManager::get_verts_per_side);
	ClassDB::bind_method(D_METHOD("set_cell_size", "s"), &CellTerrainManager::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &CellTerrainManager::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_height_scale", "s"), &CellTerrainManager::set_height_scale);
	ClassDB::bind_method(D_METHOD("get_height_scale"), &CellTerrainManager::get_height_scale);
	ClassDB::bind_method(D_METHOD("set_load_radius", "r"), &CellTerrainManager::set_load_radius);
	ClassDB::bind_method(D_METHOD("get_load_radius"), &CellTerrainManager::get_load_radius);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "target_path"), "set_target_path", "get_target_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "world_dir"), "set_world_dir", "get_world_dir");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "verts_per_side"), "set_verts_per_side", "get_verts_per_side");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "cell_size"), "set_cell_size", "get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "height_scale"), "set_height_scale", "get_height_scale");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "load_radius"), "set_load_radius", "get_load_radius");
}
