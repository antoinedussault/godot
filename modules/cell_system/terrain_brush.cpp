#include "terrain_brush.h"

#include "cell_terrain.h"
#include "cell_terrain_manager.h"
#include "core/undo_redo.h"

int TerrainBrush::_floordiv(int a, int b) {
	int d = a / b;
	int m = a % b;
	if (m != 0 && ((m < 0) != (b < 0))) d--;
	return d;
}

// For a global vertex index g on an axis with `seg` (= verts_per_side - 1)
// vertices per cell, return which cell(s) and local index reference it.
// Interior vertices map to one cell; vertices on a cell boundary are shared
// by two adjacent cells (east edge of one == west edge of the next).
int TerrainBrush::_axis_cells(int g, int seg, int *cell_out, int *local_out) {
	int q = _floordiv(g, seg);
	int rem = g - q * seg;
	if (rem == 0) {
		cell_out[0] = q - 1; local_out[0] = seg; // previous cell, far edge
		cell_out[1] = q;     local_out[1] = 0;   // this cell, near edge
		return 2;
	}
	cell_out[0] = q; local_out[0] = rem;
	return 1;
}

void TerrainBrush::begin_stroke() {
	_stroke_before.clear();
}

void TerrainBrush::apply_brush(const Vector3 &world_pos, float radius, float strength, int mode) {
	if (!manager || radius <= 0.0f) return;

	const int vps = manager->get_verts_per_side();
	const int seg = vps - 1;
	const float cell_size = manager->get_cell_size();
	const float spacing = cell_size / (float)seg;

	// Global vertex index window covered by the brush footprint.
	int gx0 = (int)Math::floor((world_pos.x - radius) / spacing);
	int gx1 = (int)Math::ceil((world_pos.x + radius) / spacing);
	int gz0 = (int)Math::floor((world_pos.z - radius) / spacing);
	int gz1 = (int)Math::ceil((world_pos.z + radius) / spacing);

	// Flatten target: height directly under the brush centre.
	float flat_target = 0.0f;
	if (mode == MODE_FLATTEN) {
		int gx = (int)Math::round(world_pos.x / spacing);
		int gz = (int)Math::round(world_pos.z / spacing);
		int cxs[2], lxs[2], czs[2], lzs[2];
		_axis_cells(gx, seg, cxs, lxs);
		_axis_cells(gz, seg, czs, lzs);
		CellTerrain *t = manager->get_cell(cxs[0], czs[0]);
		if (t) flat_target = t->get_height_at(lxs[0], lzs[0]);
	}

	Set<int64_t> touched;

	for (int gz = gz0; gz <= gz1; gz++) {
		for (int gx = gx0; gx <= gx1; gx++) {
			float vx = gx * spacing;
			float vz = gz * spacing;
			float dx = vx - world_pos.x;
			float dz = vz - world_pos.z;
			float dist = Math::sqrt(dx * dx + dz * dz);
			if (dist > radius) continue;

			// Smoothstep falloff, 1 at centre -> 0 at the rim.
			float t01 = 1.0f - (dist / radius);
			float fo = t01 * t01 * (3.0f - 2.0f * t01);

			int cxs[2], lxs[2], czs[2], lzs[2];
			int nx = _axis_cells(gx, seg, cxs, lxs);
			int nz = _axis_cells(gz, seg, czs, lzs);

			// Write the SAME new height to every cell sharing this vertex.
			for (int ix = 0; ix < nx; ix++) {
				for (int iz = 0; iz < nz; iz++) {
					int cx = cxs[ix], lx = lxs[ix];
					int cz = czs[iz], lz = lzs[iz];
					CellTerrain *cell = manager->get_cell(cx, cz);
					if (!cell) continue;

					// Snapshot this cell once, before its first edit this stroke.
					int64_t key = ((int64_t)(uint32_t)cx << 32) | (uint32_t)cz;
					if (!_stroke_before.has(key)) {
						_stroke_before[key] = cell->get_heights(); // COW, cheap
					}

					float cur = cell->get_height_at(lx, lz);
					float nh = cur;
					switch (mode) {
						case MODE_RAISE:   nh = cur + strength * fo; break;
						case MODE_LOWER:   nh = cur - strength * fo; break;
						case MODE_FLATTEN: nh = Math::lerp(cur, flat_target, strength * fo); break;
					}
					cell->set_height_at(lx, lz, nh);
					touched.insert(key);
				}
			}
		}
	}

	// Live preview: re-derive mesh + seam edges for everything we touched.
	for (Set<int64_t>::Element *E = touched.front(); E; E = E->next()) {
		int cx = (int32_t)(E->get() >> 32);
		int cz = (int32_t)(E->get() & 0xffffffff);
		manager->rebuild_at(cx, cz);
	}
}

void TerrainBrush::end_stroke(UndoRedo *undo_redo) {
	if (!manager || !undo_redo || _stroke_before.empty()) {
		_stroke_before.clear();
		return;
	}

	undo_redo->create_action("Sculpt Terrain");
	for (Map<int64_t, PoolRealArray>::Element *E = _stroke_before.front(); E; E = E->next()) {
		int cx = (int32_t)(E->key() >> 32);
		int cz = (int32_t)(E->key() & 0xffffffff);
		CellTerrain *cell = manager->get_cell(cx, cz);
		if (!cell) continue;

		PoolRealArray after = cell->get_heights();
		const PoolRealArray &before = E->get();

		undo_redo->add_do_method(cell, "set_heights", after);
		undo_redo->add_do_method(manager, "rebuild_at", cx, cz);
		undo_redo->add_undo_method(cell, "set_heights", before);
		undo_redo->add_undo_method(manager, "rebuild_at", cx, cz);
	}
	undo_redo->commit_action();
	_stroke_before.clear();
}

void TerrainBrush::_bind_methods() {
	ClassDB::bind_method(D_METHOD("begin_stroke"), &TerrainBrush::begin_stroke);
	ClassDB::bind_method(D_METHOD("apply_brush", "world_pos", "radius", "strength", "mode"), &TerrainBrush::apply_brush);

	BIND_ENUM_CONSTANT(MODE_RAISE);
	BIND_ENUM_CONSTANT(MODE_LOWER);
	BIND_ENUM_CONSTANT(MODE_FLATTEN);
}
