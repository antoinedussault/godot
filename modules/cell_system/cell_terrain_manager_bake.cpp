// CellTerrainManager - offline bakes: ground blend pages and the whole-world
// navmesh. Split out of cell_terrain_manager.cpp; same class, no behaviour change.

#include "cell_terrain_manager.h"

#include "cell_terrain.h"
#include "cell_terrain_layer.h"
#include "terrain_shader.h"
#include "core/engine.h"
#include "core/os/dir_access.h"
#include "core/os/file_access.h"
#include "core/os/os.h"
#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#endif
#include "core/io/resource_saver.h"
#include "core/io/resource_loader.h"
#include "scene/3d/camera.h"
#include "scene/3d/mesh_instance.h"
#include "scene/main/viewport.h"
#include "scene/resources/navigation_mesh.h"
#include "scene/resources/world.h"
#include "servers/navigation_server.h"

void CellTerrainManager::_release_baked_navmesh(int64_t key) {
	Map<int64_t, RID>::Element *E = _baked_nav_regions.find(key);
	if (!E) return;
	NavigationServer::get_singleton()->free(E->get());
	_baked_nav_regions.erase(key);
}

static const uint32_t GROUND_PAGES_MAGIC = 0x43475031;

// --- Ground blend pages (see the header for what these are and why) ---

// One representative colour per layer. The page is low-frequency by design, so
// what matters is the layer average colour, not its tiling detail - sampling
// the full texture per texel would just alias at this resolution. Strided so a
// 4K source costs the same as a 512.
Color CellTerrainManager::_layer_average_color(int idx) const {
	if (idx < 0 || idx >= layers.size() || layers[idx].is_null()) return Color(0.5, 0.5, 0.5, 1);
	Ref<CellTerrainLayer> layer = layers[idx];
	Ref<Texture> tex = layer->get_albedo_texture();
	Color tint = layer->get_color_mod();
	if (tex.is_null()) return Color(0.5, 0.5, 0.5, 1) * tint;

	Ref<Image> img = tex->get_data();
	if (img.is_null()) return Color(0.5, 0.5, 0.5, 1) * tint;
	img = img->duplicate();
	if (img->is_compressed()) {
		// A VRAM-compressed source cannot be read with get_pixel().
		if (img->decompress() != OK) return Color(0.5, 0.5, 0.5, 1) * tint;
	}
	img->convert(Image::FORMAT_RGBA8);

	const int w = img->get_width(), h = img->get_height();
	if (w <= 0 || h <= 0) return Color(0.5, 0.5, 0.5, 1) * tint;
	const int step_x = MAX(1, w / 64), step_z = MAX(1, h / 64);

	double r = 0, g = 0, b = 0;
	int n = 0;
	img->lock();
	for (int y = 0; y < h; y += step_z) {
		for (int x = 0; x < w; x += step_x) {
			Color c = img->get_pixel(x, y);
			r += c.r; g += c.g; b += c.b;
			n++;
		}
	}
	img->unlock();
	if (n == 0) return Color(0.5, 0.5, 0.5, 1) * tint;
	return Color(r / n, g / n, b / n, 1) * tint;
}

void CellTerrainManager::bake_ground_texture() {
	Set<int64_t> keys;
	_scan_all_cell_keys(keys);
	if (keys.empty()) {
		print_line("Ground bake: no saved cells in " + world_dir + " - nothing to bake.");
		return;
	}

	int min_cx = 0, min_cz = 0, max_cx = 0, max_cz = 0;
	bool first = true;
	for (Set<int64_t>::Element *E = keys.front(); E; E = E->next()) {
		int cx, cz; _unkey(E->get(), cx, cz);
		if (first) { min_cx = max_cx = cx; min_cz = max_cz = cz; first = false; continue; }
		min_cx = MIN(min_cx, cx); max_cx = MAX(max_cx, cx);
		min_cz = MIN(min_cz, cz); max_cz = MAX(max_cz, cz);
	}

	const int cells_x = max_cx - min_cx + 1;
	const int cells_z = max_cz - min_cz + 1;
	const float world_w = cells_x * cell_size;
	const float world_d = cells_z * cell_size;

	// Keep the page inside ground_max_size by dropping resolution rather than
	// refusing to bake - a huge world still gets a (coarser) usable page.
	float tpu = MAX(0.0001f, ground_texels_per_unit);
	int page_w = (int)Math::ceil(world_w * tpu);
	int page_h = (int)Math::ceil(world_d * tpu);
	if (page_w > ground_max_size || page_h > ground_max_size) {
		float scale = (float)ground_max_size / (float)MAX(page_w, page_h);
		tpu *= scale;
		page_w = (int)Math::ceil(world_w * tpu);
		page_h = (int)Math::ceil(world_d * tpu);
		print_line("Ground bake: clamped to " + itos(ground_max_size) + "px (texels/unit now " + rtos(tpu) + ").");
	}
	page_w = MAX(1, page_w);
	page_h = MAX(1, page_h);

	Color avg[16];
	for (int i = 0; i < 16; i++) avg[i] = _layer_average_color(i);

	Ref<Image> albedo_img;
	albedo_img.instance();
	albedo_img->create(page_w, page_h, false, Image::FORMAT_RGBA8);
	Ref<Image> height_img;
	height_img.instance();
	height_img->create(page_w, page_h, false, Image::FORMAT_RF);
	albedo_img->lock();
	height_img->lock();

	const float origin_x = min_cx * cell_size;
	const float origin_z = min_cz * cell_size;
	int baked = 0;

	// Iterate cell-by-cell so each cell data is loaded from disk exactly once,
	// then fill only that cell block of texels.
	for (Set<int64_t>::Element *E = keys.front(); E; E = E->next()) {
		int cx, cz; _unkey(E->get(), cx, cz);
		PoolRealArray heights = _load_heights(cx, cz);
		if (heights.size() != verts_per_side * verts_per_side) continue;
		Ref<Image> splats[4];
		for (int sidx = 0; sidx < 4; sidx++) {
			splats[sidx] = _load_splatmap(cx, cz, sidx);
			if (splats[sidx].is_valid()) {
				if (splats[sidx]->is_compressed()) splats[sidx]->decompress();
				splats[sidx]->lock();
			}
		}
		PoolRealArray::Read hr = heights.read();

		const int x0 = (int)Math::floor((cx - min_cx) * cell_size * tpu);
		const int z0 = (int)Math::floor((cz - min_cz) * cell_size * tpu);
		const int x1 = MIN(page_w, (int)Math::ceil((cx - min_cx + 1) * cell_size * tpu));
		const int z1 = MIN(page_h, (int)Math::ceil((cz - min_cz + 1) * cell_size * tpu));

		for (int pz = z0; pz < z1; pz++) {
			for (int px = x0; px < x1; px++) {
				// Page texel -> position inside this cell, in 0..1.
				float u = ((px + 0.5f) / tpu - (cx - min_cx) * cell_size) / cell_size;
				float v = ((pz + 0.5f) / tpu - (cz - min_cz) * cell_size) / cell_size;
				u = CLAMP(u, 0.0f, 1.0f);
				v = CLAMP(v, 0.0f, 1.0f);

				// Bilinear height, matching get_height_at_world sampling so the
				// page agrees with what collision and queries report.
				float fx = u * (verts_per_side - 1);
				float fz = v * (verts_per_side - 1);
				int vx = CLAMP((int)fx, 0, verts_per_side - 2);
				int vz = CLAMP((int)fz, 0, verts_per_side - 2);
				float tx = CLAMP(fx - vx, 0.0f, 1.0f);
				float tz = CLAMP(fz - vz, 0.0f, 1.0f);
				float h00 = hr[vz * verts_per_side + vx] * height_scale;
				float h10 = hr[vz * verts_per_side + vx + 1] * height_scale;
				float h01 = hr[(vz + 1) * verts_per_side + vx] * height_scale;
				float h11 = hr[(vz + 1) * verts_per_side + vx + 1] * height_scale;
				float hh = Math::lerp(Math::lerp(h00, h10, tx), Math::lerp(h01, h11, tx), tz);
				height_img->set_pixel(px, pz, Color(hh, 0, 0, 1));

				// Splat weights at the nearest vertex, blended into a tint.
				int sx = CLAMP((int)Math::round(fx), 0, verts_per_side - 1);
				int sz = CLAMP((int)Math::round(fz), 0, verts_per_side - 1);
				float weights[16] = {};
				float total = 0.0f;
				for (int sidx = 0; sidx < 4; sidx++) {
					if (splats[sidx].is_null()) continue;
					if (sx >= splats[sidx]->get_width() || sz >= splats[sidx]->get_height()) continue;
					Color c = splats[sidx]->get_pixel(sx, sz);
					weights[sidx * 4 + 0] = c.r; weights[sidx * 4 + 1] = c.g;
					weights[sidx * 4 + 2] = c.b; weights[sidx * 4 + 3] = c.a;
				}
				for (int i = 0; i < num_layers && i < 16; i++) total += weights[i];
				Color out(0, 0, 0, 1);
				if (total > 0.001f) {
					for (int i = 0; i < num_layers && i < 16; i++) {
						float wgt = weights[i] / total;
						if (wgt <= 0.001f) continue;
						out.r += avg[i].r * wgt;
						out.g += avg[i].g * wgt;
						out.b += avg[i].b * wgt;
					}
				} else {
					out = avg[0];
				}
				albedo_img->set_pixel(px, pz, out);
			}
		}

		for (int sidx = 0; sidx < 4; sidx++) {
			if (splats[sidx].is_valid()) splats[sidx]->unlock();
		}
		baked++;
	}

	albedo_img->unlock();
	height_img->unlock();

	DirAccess *da = DirAccess::create_for_path(world_dir);
	if (da) { da->make_dir_recursive(world_dir); memdelete(da); }
	String path = world_dir.plus_file("ground.pages");
	FileAccess *f = FileAccess::open(path, FileAccess::WRITE);
	if (!f) {
		ERR_PRINT("Ground bake: cannot write " + path);
		return;
	}
	f->store_32(GROUND_PAGES_MAGIC);
	f->store_float(origin_x);
	f->store_float(origin_z);
	f->store_float(world_w);
	f->store_float(world_d);
	f->store_32(page_w);
	f->store_32(page_h);
	PoolByteArray ad = albedo_img->get_data();
	{
		PoolByteArray::Read r = ad.read();
		f->store_buffer(r.ptr(), ad.size());
	}
	PoolByteArray hd = height_img->get_data();
	{
		PoolByteArray::Read r = hd.read();
		f->store_buffer(r.ptr(), hd.size());
	}
	f->close();
	memdelete(f);

	ground_bounds = Rect2(origin_x, origin_z, world_w, world_d);
	ground_albedo_tex.instance();
	ground_albedo_tex->create_from_image(albedo_img, Texture::FLAG_FILTER);
	ground_height_tex.instance();
	ground_height_tex->create_from_image(height_img, Texture::FLAG_FILTER);
	_ground_load_attempted = true;

	print_line("Ground bake: " + itos(baked) + " cells -> " + itos(page_w) + "x" + itos(page_h) +
			" page covering " + rtos(world_w) + "x" + rtos(world_d) + " units.");
}

void CellTerrainManager::_load_ground_pages() {
	if (_ground_load_attempted) return;
	_ground_load_attempted = true;

	String path = world_dir.plus_file("ground.pages");
	if (!FileAccess::exists(path)) return;
	FileAccess *f = FileAccess::open(path, FileAccess::READ);
	if (!f) return;
	if (f->get_32() != GROUND_PAGES_MAGIC) { f->close(); memdelete(f); return; }
	float ox = f->get_float();
	float oz = f->get_float();
	float sw = f->get_float();
	float sd = f->get_float();
	int w = f->get_32();
	int h = f->get_32();
	if (w <= 0 || h <= 0) { f->close(); memdelete(f); return; }

	PoolByteArray ad; ad.resize(w * h * 4);
	{ PoolByteArray::Write wr = ad.write(); f->get_buffer(wr.ptr(), ad.size()); }
	PoolByteArray hd; hd.resize(w * h * 4); // FORMAT_RF is one float per texel
	{ PoolByteArray::Write wr = hd.write(); f->get_buffer(wr.ptr(), hd.size()); }
	f->close();
	memdelete(f);

	Ref<Image> ai; ai.instance(); ai->create(w, h, false, Image::FORMAT_RGBA8, ad);
	Ref<Image> hi; hi.instance(); hi->create(w, h, false, Image::FORMAT_RF, hd);
	ground_albedo_tex.instance();
	ground_albedo_tex->create_from_image(ai, Texture::FLAG_FILTER);
	ground_height_tex.instance();
	ground_height_tex->create_from_image(hi, Texture::FLAG_FILTER);
	ground_bounds = Rect2(ox, oz, sw, sd);
}

void CellTerrainManager::clear_ground_texture() {
	ground_albedo_tex = Ref<ImageTexture>();
	ground_height_tex = Ref<ImageTexture>();
	ground_bounds = Rect2();
	_ground_load_attempted = true; // do not silently reload what was just cleared
	String path = world_dir.plus_file("ground.pages");
	if (FileAccess::exists(path)) {
		DirAccess *da = DirAccess::create_for_path(world_dir);
		if (da) { da->remove(path); memdelete(da); }
	}
}

bool CellTerrainManager::has_ground_texture() {
	_load_ground_pages();
	return ground_albedo_tex.is_valid();
}

Ref<Texture> CellTerrainManager::get_ground_albedo_texture() {
	_load_ground_pages();
	return ground_albedo_tex;
}

Ref<Texture> CellTerrainManager::get_ground_height_texture() {
	_load_ground_pages();
	return ground_height_tex;
}

Rect2 CellTerrainManager::get_ground_bounds() {
	_load_ground_pages();
	// Baked in manager-local space; apply the current origin so a moved manager
	// still maps world XZ to the right texel.
	Vector3 o = get_global_transform().origin;
	return Rect2(ground_bounds.position.x + o.x, ground_bounds.position.y + o.z,
			ground_bounds.size.x, ground_bounds.size.y);
}

void CellTerrainManager::set_ground_texels_per_unit(float v) { ground_texels_per_unit = MAX(0.01f, v); }
float CellTerrainManager::get_ground_texels_per_unit() const { return ground_texels_per_unit; }

void CellTerrainManager::bake_navmesh() {
	clear_baked_navmesh();
	if (!enable_navmesh) {
		print_line("Navmesh bake: enable_navmesh is off, nothing to bake.");
		return;
	}

	Set<int64_t> all;
	_scan_all_cell_keys(all);

	Ref<World> w = get_world();
	const NavigationServer *ns = NavigationServer::get_singleton();
	Transform base_xf = get_global_transform();
	int baked = 0, skipped_active = 0;

	for (Set<int64_t>::Element *E = all.front(); E; E = E->next()) {
		int64_t key = E->get();
		// Already covered by its own live navmesh region — baking here too
		// would just double-register the same walkable area.
		if (_active.has(key)) { skipped_active++; continue; }

		int cx, cz; _unkey(key, cx, cz);

		CellTerrain::ComputeInput in;
		in.heights = _load_heights(cx, cz);
		if (in.heights.size() != verts_per_side * verts_per_side) continue;
		in.nav_mask = _load_nav_mask(cx, cz);
		in.hole_mask = _load_hole_mask(cx, cz);
		_load_neighbor_edges_from_disk(cx, cz, in.edge_n, in.edge_s, in.edge_e, in.edge_w);
		in.verts_per_side = verts_per_side;
		in.cell_size = cell_size;
		in.height_scale = height_scale;
		in.lod_level = bake_navmesh_lod;
		in.nav_max_slope_deg = nav_max_slope_deg;
		in.nav_downsample = nav_downsample;

		CellTerrain::ComputedData data = CellTerrain::compute(in);
		if (data.nav_vertices.size() == 0 || data.nav_polygons.empty()) continue;

		Ref<NavigationMesh> navmesh;
		navmesh.instance();
		navmesh->set_vertices(data.nav_vertices);
		for (int i = 0; i < data.nav_polygons.size(); i++) navmesh->add_polygon(data.nav_polygons[i]);

		RID region = ns->region_create();
		ns->region_set_navmesh(region, navmesh);
		Transform cell_xf;
		cell_xf.origin = Vector3(cx * cell_size, 0.0f, cz * cell_size);
		ns->region_set_transform(region, base_xf * cell_xf);
		if (w.is_valid()) ns->region_set_map(region, w->get_navigation_map());

		_baked_nav_regions[key] = region;
		baked++;
	}

	print_line("Navmesh bake: " + itos(baked) + " region(s) baked, " + itos(skipped_active) + " active cell(s) skipped (already live).");
}

void CellTerrainManager::clear_baked_navmesh() {
	const NavigationServer *ns = NavigationServer::get_singleton();
	for (Map<int64_t, RID>::Element *E = _baked_nav_regions.front(); E; E = E->next()) {
		ns->free(E->value());
	}
	_baked_nav_regions.clear();
}

void CellTerrainManager::set_bake_navmesh_lod(int l) { bake_navmesh_lod = CLAMP(l, 0, 3); }
