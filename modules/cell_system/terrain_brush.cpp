#include "terrain_brush.h"

#include "cell_terrain.h"
#include "cell_terrain_manager.h"
#include "core/undo_redo.h"

// Cheap hashed value-noise (no dependency on the opensimplex module, keeps
// cell_system buildable standalone). Not fractal/multi-octave on purpose —
// the brush is dragged repeatedly to build up detail, same as Unreal's noise brush.
float TerrainBrush::_hash_noise(float x, float z, int seed) {
	int ix = (int)Math::floor(x);
	int iz = (int)Math::floor(z);
	float fx = x - ix;
	float fz = z - iz;

	auto hash = [seed](int xi, int zi) -> float {
		uint32_t h = (uint32_t)xi * 374761393u + (uint32_t)zi * 668265263u + (uint32_t)seed * 2654435761u;
		h = (h ^ (h >> 13)) * 1274126177u;
		h ^= (h >> 16);
		return (float)(h & 0xffffff) / (float)0xffffff * 2.0f - 1.0f;
	};

	float v00 = hash(ix, iz), v10 = hash(ix + 1, iz);
	float v01 = hash(ix, iz + 1), v11 = hash(ix + 1, iz + 1);
	float sx = fx * fx * (3.0f - 2.0f * fx);
	float sz = fz * fz * (3.0f - 2.0f * fz);
	return Math::lerp(Math::lerp(v00, v10, sx), Math::lerp(v01, v11, sx), sz);
}

void TerrainBrush::begin_stroke() { _stroke_before.clear(); }

void TerrainBrush::apply_brush(const Vector3 &world_pos, float radius, float strength, int mode) {
	if (!manager || radius <= 0.0f) return;

	const int vps = manager->get_verts_per_side();
	const int seg = vps - 1;
	const float cell_size = manager->get_cell_size();
	const float spacing = cell_size / (float)seg;

	int gx0 = (int)Math::floor((world_pos.x - radius) / spacing);
	int gx1 = (int)Math::ceil((world_pos.x + radius) / spacing);
	int gz0 = (int)Math::floor((world_pos.z - radius) / spacing);
	int gz1 = (int)Math::ceil((world_pos.z + radius) / spacing);

	float flat_target = 0.0f;
	if (mode == MODE_FLATTEN) {
		if (force_flatten_height) {
			flat_target = flatten_height;
		} else {
			int gx = (int)Math::round(world_pos.x / spacing);
			int gz = (int)Math::round(world_pos.z / spacing);
			int cxs[2], lxs[2], czs[2], lzs[2];
			terrain_axis_cells(gx, seg, cxs, lxs);
			terrain_axis_cells(gz, seg, czs, lzs);
			CellTerrain *t = manager->get_cell(cxs[0], czs[0]);
			if (t) flat_target = t->get_height_at(lxs[0], lzs[0]);
		}
	}

	// Resolved once per stroke, not per vertex.
	const bool slope_filter_active = (paint_min_angle > 0.001f) || (paint_max_angle < 89.999f);

	Set<int64_t> touched;

	// Lock the brush image once for the whole stroke, not per vertex.
	const bool use_brush_img = brush_image.is_valid() && brush_image->get_width() > 0;
	if (use_brush_img) brush_image->lock();

	for (int gz = gz0; gz <= gz1; gz++) {
		for (int gx = gx0; gx <= gx1; gx++) {
			float vx = gx * spacing;
			float vz = gz * spacing;
			float dx = vx - world_pos.x;
			float dz = vz - world_pos.z;
			float dist = Math::sqrt(dx * dx + dz * dz);
			if (dist > radius) continue;

			// Radial falloff curve always applies; an optional alpha-stamp brush
			// texture multiplies on top of it (matches how Unreal combines a
			// brush's falloff shape with its alpha pattern).
			float t01 = 1.0f - (dist / radius);
			float fo = terrain_falloff(t01, falloff_type);
			if (use_brush_img) {
				float u = (dx / radius) * 0.5f + 0.5f;
				float v = (dz / radius) * 0.5f + 0.5f;
				int px = CLAMP((int)(u * brush_image->get_width()), 0, brush_image->get_width() - 1);
				int py = CLAMP((int)(v * brush_image->get_height()), 0, brush_image->get_height() - 1);
			// Slope filter (MODE_PAINT only). Evaluated per grid vertex and
			// skipped entirely when the range is wide open, so the common case
			// costs nothing. Sampled through the manager rather than the cell so
			// vertices on a cell border read their neighbour correctly instead
			// of clamping at the edge.
			if (mode == MODE_PAINT && slope_filter_active) {
				float hl = manager->get_height_at_world(vx - spacing, vz);
				float hr = manager->get_height_at_world(vx + spacing, vz);
				float hd = manager->get_height_at_world(vx, vz - spacing);
				float hu = manager->get_height_at_world(vx, vz + spacing);
				Vector3 nrm(hl - hr, 2.0f * spacing, hd - hu);
				nrm.normalize();
				float angle = Math::rad2deg(Math::acos(CLAMP(nrm.y, -1.0f, 1.0f)));
				if (angle < paint_min_angle || angle > paint_max_angle) continue;
			}

				fo *= brush_image->get_pixel(px, py).r; // red channel as intensity
			}

			int cxs[2], lxs[2], czs[2], lzs[2];
			int nx = terrain_axis_cells(gx, seg, cxs, lxs);
			int nz = terrain_axis_cells(gz, seg, czs, lzs);

			for (int ix = 0; ix < nx; ix++) {
				for (int iz = 0; iz < nz; iz++) {
					int cx = cxs[ix], lx = lxs[ix];
					int cz = czs[iz], lz = lzs[iz];
					CellTerrain *cell = manager->get_cell(cx, cz);
					if (!cell) continue;

					int64_t key = ((int64_t)(uint32_t)cx << 32) | (uint32_t)cz;

					if (mode == MODE_PAINT) {
						// Texture painting: write to splatmap.
						cell->paint_layer(lx, lz, paint_layer, strength * fo);
					} else if (mode == MODE_COLOR) {
						// Colour painting: blend the cell's tint map toward the
						// picked colour. Same "textures only" class of edit as
						// MODE_PAINT - no geometry changes, so no rebuild.
						cell->paint_color_at(lx, lz, paint_color, strength * fo);
					} else if (mode == MODE_HOLE) {
						// Hole painting: hard 0/1, not a blend. A quad is either
						// there or it is not (see CellTerrain::_quad_is_hole_s),
						// so an intermediate value would only ever read as one
						// of the two anyway - and lerping would make a soft
						// brush edge need many passes to actually cut through.
						if (strength * fo > 0.01f) {
							cell->set_hole_at(lx, lz, hole_erase ? 1.0f : 0.0f);
						}
					} else if (mode == MODE_NAV_MASK) {
						// Navmesh mask painting: blend toward 0 (excluded) or 1
						// (navigable) — leaves a hole in the baked navmesh, same
						// as an over-steep slope (see CellTerrain::_mask_at_s).
						float target = nav_mask_erase ? 1.0f : 0.0f;
						float cur = cell->get_nav_mask_at(lx, lz);
						cell->set_nav_mask_at(lx, lz, Math::lerp(cur, target, CLAMP(strength * fo, 0.0f, 1.0f)));
					} else {
						// Height sculpting: snapshot for undo.
						if (!_stroke_before.has(key))
							_stroke_before[key] = cell->get_heights();

						float cur = cell->get_height_at(lx, lz);
						float nh = cur;
						switch (mode) {
							case MODE_RAISE:   nh = cur + strength * fo; break;
							case MODE_LOWER:   nh = cur - strength * fo; break;
							case MODE_FLATTEN: nh = Math::lerp(cur, flat_target, strength * fo); break;
							case MODE_SMOOTH: {
								// Average heights from a ±2-vertex kernel around this point.
								const int kernel = 2;
								float sum = 0.0f;
								int cnt = 0;
								for (int dkz = -kernel; dkz <= kernel; dkz++) {
									for (int dkx = -kernel; dkx <= kernel; dkx++) {
										int s_cxs[2], s_lxs[2], s_czs[2], s_lzs[2];
										int snx = terrain_axis_cells(gx + dkx, seg, s_cxs, s_lxs);
										int snz = terrain_axis_cells(gz + dkz, seg, s_czs, s_lzs);
										for (int six = 0; six < snx; six++) {
											for (int siz = 0; siz < snz; siz++) {
												CellTerrain *nc = manager->get_cell(s_cxs[six], s_czs[siz]);
												if (nc) { sum += nc->get_height_at(s_lxs[six], s_lzs[siz]); cnt++; }
											}
										}
									}
								}
								if (cnt > 0) nh = Math::lerp(cur, sum / (float)cnt, strength * fo);
							} break;
							case MODE_NOISE: {
								float n = _hash_noise(vx / noise_scale, vz / noise_scale, noise_seed);
								nh = cur + n * strength * fo;
							} break;
							case MODE_ERODE: {
								// Simplified single-pass thermal erosion: slump material
								// toward the lowest of the 4 axis neighbors whenever the
								// slope exceeds erosion_talus. Not a hydraulic simulation —
								// dragging the brush repeatedly builds up the effect, same
								// as the other stroke-based modes.
								static const int dxs[4] = { 1, -1, 0, 0 };
								static const int dzs[4] = { 0, 0, 1, -1 };
								float lowest = cur;
								for (int ni = 0; ni < 4; ni++) {
									int s_cxs[2], s_lxs[2], s_czs[2], s_lzs[2];
									terrain_axis_cells(gx + dxs[ni], seg, s_cxs, s_lxs);
									terrain_axis_cells(gz + dzs[ni], seg, s_czs, s_lzs);
									CellTerrain *nc = manager->get_cell(s_cxs[0], s_czs[0]);
									if (nc) {
										float h = nc->get_height_at(s_lxs[0], s_lzs[0]);
										if (h < lowest) lowest = h;
									}
								}
								float slope = cur - lowest;
								if (slope > erosion_talus) {
									float excess = slope - erosion_talus;
									nh = cur - excess * CLAMP(strength * fo, 0.0f, 1.0f) * 0.5f;
								}
							} break;
						}
						cell->set_height_at(lx, lz, nh);
					}
					touched.insert(key);
				}
			}
		}
	}

	if (use_brush_img) brush_image->unlock();

	if (mode == MODE_PAINT || mode == MODE_COLOR) {
		// Painting only changes textures; update those in place. No mesh or
		// collision rebuild is needed (the material already binds them).
		for (Set<int64_t>::Element *E = touched.front(); E; E = E->next()) {
			int cx = (int32_t)(E->get() >> 32);
			int cz = (int32_t)(E->get() & 0xffffffff);
			CellTerrain *cell = manager->get_cell(cx, cz);
			if (cell) {
				if (mode == MODE_PAINT) cell->update_splatmap_textures();
				else cell->update_color_texture();
			}
			manager->mark_dirty(cx, cz);
		}
	} else {
		// Rebuild every touched cell (and its neighbors) exactly once.
		manager->rebuild_region(touched);
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
		undo_redo->add_do_method(manager, "mark_dirty", cx, cz);
		undo_redo->add_undo_method(cell, "set_heights", before);
		undo_redo->add_undo_method(manager, "rebuild_at", cx, cz);
		undo_redo->add_undo_method(manager, "mark_dirty", cx, cz);
	}
	undo_redo->commit_action();
	_stroke_before.clear();
}

void TerrainBrush::_bind_methods() {
	ClassDB::bind_method(D_METHOD("begin_stroke"), &TerrainBrush::begin_stroke);
	ClassDB::bind_method(D_METHOD("apply_brush", "world_pos", "radius", "strength", "mode"), &TerrainBrush::apply_brush);
	ClassDB::bind_method(D_METHOD("set_paint_min_angle", "v"), &TerrainBrush::set_paint_min_angle);
	ClassDB::bind_method(D_METHOD("get_paint_min_angle"), &TerrainBrush::get_paint_min_angle);
	ClassDB::bind_method(D_METHOD("set_paint_max_angle", "v"), &TerrainBrush::set_paint_max_angle);
	ClassDB::bind_method(D_METHOD("get_paint_max_angle"), &TerrainBrush::get_paint_max_angle);
	ClassDB::bind_method(D_METHOD("set_paint_layer", "l"), &TerrainBrush::set_paint_layer);
	ClassDB::bind_method(D_METHOD("get_paint_layer"), &TerrainBrush::get_paint_layer);
	ClassDB::bind_method(D_METHOD("set_falloff_type", "t"), &TerrainBrush::set_falloff_type);
	ClassDB::bind_method(D_METHOD("get_falloff_type"), &TerrainBrush::get_falloff_type);
	ClassDB::bind_method(D_METHOD("set_noise_scale", "s"), &TerrainBrush::set_noise_scale);
	ClassDB::bind_method(D_METHOD("get_noise_scale"), &TerrainBrush::get_noise_scale);
	ClassDB::bind_method(D_METHOD("set_noise_seed", "s"), &TerrainBrush::set_noise_seed);
	ClassDB::bind_method(D_METHOD("get_noise_seed"), &TerrainBrush::get_noise_seed);
	ClassDB::bind_method(D_METHOD("set_erosion_talus", "t"), &TerrainBrush::set_erosion_talus);
	ClassDB::bind_method(D_METHOD("get_erosion_talus"), &TerrainBrush::get_erosion_talus);
	ClassDB::bind_method(D_METHOD("set_nav_mask_erase", "e"), &TerrainBrush::set_nav_mask_erase);
	ClassDB::bind_method(D_METHOD("get_nav_mask_erase"), &TerrainBrush::get_nav_mask_erase);
	BIND_ENUM_CONSTANT(MODE_RAISE);
	BIND_ENUM_CONSTANT(MODE_LOWER);
	BIND_ENUM_CONSTANT(MODE_FLATTEN);
	BIND_ENUM_CONSTANT(MODE_PAINT);
	BIND_ENUM_CONSTANT(MODE_SMOOTH);
	BIND_ENUM_CONSTANT(MODE_NOISE);
	BIND_ENUM_CONSTANT(MODE_ERODE);
	BIND_ENUM_CONSTANT(MODE_NAV_MASK);
	BIND_ENUM_CONSTANT(MODE_HOLE);
	BIND_ENUM_CONSTANT(MODE_COLOR);
	BIND_ENUM_CONSTANT(FALLOFF_SMOOTH);
	BIND_ENUM_CONSTANT(FALLOFF_LINEAR);
	BIND_ENUM_CONSTANT(FALLOFF_SPHERICAL);
	BIND_ENUM_CONSTANT(FALLOFF_TIP);
}
