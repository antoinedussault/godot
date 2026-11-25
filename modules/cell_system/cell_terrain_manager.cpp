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

CellTerrainManager::CellTerrainManager() {}

Camera *CellTerrainManager::_get_active_camera() const {
	if (!is_inside_tree()) return nullptr;
	Viewport *vp = const_cast<CellTerrainManager *>(this)->get_viewport();
	return vp ? vp->get_camera() : nullptr;
}

int64_t CellTerrainManager::_key(int x, int z) { return ((int64_t)(uint32_t)x << 32) | (uint32_t)z; }
void CellTerrainManager::_unkey(int64_t k, int &x, int &z) { x = (int32_t)(k >> 32); z = (int32_t)(k & 0xffffffff); }

Spatial *CellTerrainManager::_get_target() const {
	if (target_path.is_empty()) return nullptr;
	return Object::cast_to<Spatial>(get_node_or_null(target_path));
}

// Build a TextureArray from a 16-slot source, packing data into RGBA8 with mipmaps.
static Ref<TextureArray> _build_array(Ref<Texture> *src, int &out_count, bool is_normal) {
	int count = 0;
	for (int i = 0; i < 16; i++) if (src[i].is_valid()) count = i + 1;
	out_count = count;
	int tw = 0, th = 0;
	for (int i = 0; i < count; i++) if (src[i].is_valid()) { tw = src[i]->get_width(); th = src[i]->get_height(); break; }
	// Always produce at least a 1-layer fallback so the shader never samples an
	// unbound sampler2DArray (which returns (0,0,0,0) and breaks normals/albedo).
	if (count == 0) count = 1;
	if (tw == 0) tw = th = 4;

	Ref<TextureArray> arr; arr.instance();
	arr->create(tw, th, count, Image::FORMAT_RGBA8, Texture::FLAG_FILTER | Texture::FLAG_REPEAT | Texture::FLAG_MIPMAPS);
	for (int i = 0; i < count; i++) {
		Ref<Image> img;
		if (src[i].is_valid()) {
			img = src[i]->get_data();
			if (img.is_valid()) {
				if (img->get_width() != tw || img->get_height() != th) img->resize(tw, th);
				if (img->get_format() != Image::FORMAT_RGBA8) img->convert(Image::FORMAT_RGBA8);
			}
		}
		if (img.is_null()) {
			img.instance(); img->create(tw, th, false, Image::FORMAT_RGBA8);
			// Default: albedo mid-grey + height 0.5 (A); normal flat (0.5,0.5,1) + roughness 1 (A).
			img->fill(is_normal ? Color(0.5f, 0.5f, 1.0f, 1.0f) : Color(0.5f, 0.5f, 0.5f, 0.5f));
		}
		img->generate_mipmaps();
		arr->set_layer_data(img, i);
	}
	return arr;
}

void CellTerrainManager::_rebuild_texture_array() {
	Ref<Texture> alb_src[16], nrm_src[16];
	for (int i = 0; i < layers.size() && i < 16; i++) {
		if (layers[i].is_null()) continue;
		alb_src[i] = layers[i]->get_albedo_texture();
		nrm_src[i] = layers[i]->get_normal_texture();
	}

	int ac = 0, nc = 0;
	albedo_texture_array = _build_array(alb_src, ac, false);
	normal_texture_array = _build_array(nrm_src, nc, true);
	_texarray_dirty = false;

	// Diagnostic: distinguishes "no textures assigned" from "textures assigned but
	// build failed" (e.g. a slot holds a texture that reports width 0).
	int avalid = 0, nvalid = 0;
	String dims;
	for (int i = 0; i < 16; i++) {
		if (alb_src[i].is_valid()) { avalid++; dims += " a" + itos(i) + "=" + itos(alb_src[i]->get_width()) + "x" + itos(alb_src[i]->get_height()); }
		if (nrm_src[i].is_valid()) { nvalid++; dims += " n" + itos(i) + "=" + itos(nrm_src[i]->get_width()) + "x" + itos(nrm_src[i]->get_height()); }
	}
	print_line("TEXARRAY: albedo_assigned=" + itos(avalid) + " normal_assigned=" + itos(nvalid) +
		" albedo_arr_valid=" + itos(albedo_texture_array.is_valid()) +
		" normal_arr_valid=" + itos(normal_texture_array.is_valid()) +
		" ac=" + itos(ac) + " nc=" + itos(nc) + dims);

	_rebuild_layer_params_textures();
}

// Packs each layer's normal_strength/parallax_scale/roughness_scale/uv_scale/
// color_mod into two tiny 16x1 lookup textures (see terrain_shader.h for why
// — Godot 3.x shader uniforms can't be arrays). Nearest-filtered, clamped,
// no mipmaps, and always produces a valid (if trivial) texture even with
// zero layers, since these are bound unconditionally as sampler2D uniforms.
void CellTerrainManager::_rebuild_layer_params_textures() {
	// Always exactly 16 wide, matching the shader's fixed (float(i)+0.5)/16.0
	// column lookup — unused columns (beyond layers.size()) get neutral
	// defaults and are simply never selected by any splatmap weight.
	const int count = 16;

	// Float format: normal_strength/roughness_scale legitimately go above 1.0
	// (up to 4.0 / 2.0), which an 8-bit-per-channel image would clamp/quantize.
	Ref<Image> params_img; params_img.instance();
	params_img->create(count, 1, false, Image::FORMAT_RGBAF);
	Ref<Image> color_img; color_img.instance();
	color_img->create(count, 1, false, Image::FORMAT_RGBA8);

	params_img->lock();
	color_img->lock();
	for (int i = 0; i < count; i++) {
		Ref<CellTerrainLayer> l = (i < layers.size()) ? layers[i] : Ref<CellTerrainLayer>();
		if (l.is_valid()) {
			params_img->set_pixel(i, 0, Color(l->get_normal_strength(), l->get_parallax_scale(), l->get_roughness_scale(), l->get_uv_scale()));
			color_img->set_pixel(i, 0, l->get_color_mod());
		} else {
			params_img->set_pixel(i, 0, Color(1.0f, 0.0f, 1.0f, 1.0f)); // strength 1, no parallax, roughness x1, uv_scale x1
			color_img->set_pixel(i, 0, Color(1, 1, 1, 1)); // no tint
		}
	}
	params_img->unlock();
	color_img->unlock();

	const uint32_t flags = 0; // no filter, no repeat, no mipmaps — exact per-texel lookup
	if (layer_params_tex.is_null()) layer_params_tex.instance();
	layer_params_tex->create_from_image(params_img, flags);
	if (layer_color_tex.is_null()) layer_color_tex.instance();
	layer_color_tex->create_from_image(color_img, flags);
}

void CellTerrainManager::_on_layer_changed() {
	_texarray_dirty = true;
	if (is_inside_tree()) call_deferred("refresh");
}

Ref<ImageTexture> CellTerrainManager::_get_neutral_white_texture() {
	if (_neutral_white_tex.is_null()) {
		Ref<Image> img; img.instance();
		img->create(4, 4, false, Image::FORMAT_RGBA8);
		img->fill(Color(1, 1, 1, 1)); // multiplies to no change
		_neutral_white_tex.instance();
		_neutral_white_tex->create_from_image(img, Texture::FLAG_FILTER);
	}
	return _neutral_white_tex;
}

Ref<ImageTexture> CellTerrainManager::_get_neutral_normal_texture() {
	if (_neutral_normal_tex.is_null()) {
		Ref<Image> img; img.instance();
		img->create(4, 4, false, Image::FORMAT_RGB8);
		img->fill(Color(0.5f, 0.5f, 1.0f)); // decodes to (0,0,1): no perturbation
		_neutral_normal_tex.instance();
		_neutral_normal_tex->create_from_image(img, Texture::FLAG_FILTER);
	}
	return _neutral_normal_tex;
}

void CellTerrainManager::_ensure_material() {
	if (terrain_shader.is_null()) { terrain_shader.instance(); terrain_shader->set_code(TERRAIN_SHADER_CODE); }
	if (terrain_material.is_null()) { terrain_material.instance(); terrain_material->set_shader(terrain_shader); }
	if (_texarray_dirty) _rebuild_texture_array();
	if (albedo_texture_array.is_valid()) terrain_material->set_shader_param("albedo_arr", albedo_texture_array);
	if (normal_texture_array.is_valid()) terrain_material->set_shader_param("normal_arr", normal_texture_array);
	terrain_material->set_shader_param("texture_scale", texture_scale);
	terrain_material->set_shader_param("num_layers", num_layers);
	terrain_material->set_shader_param("height_blend_sharpness", height_blend_sharpness);
	terrain_material->set_shader_param("layer_params", layer_params_tex);
	terrain_material->set_shader_param("layer_color", layer_color_tex);
	terrain_material->set_shader_param("macro_variation_color1", macro_variation_color1);
	terrain_material->set_shader_param("macro_variation_color2", macro_variation_color2);
	terrain_material->set_shader_param("macro_variation_scale", macro_variation_scale);
	terrain_material->set_shader_param("parallax_max_distance", parallax_max_distance);
	terrain_material->set_shader_param("debug_view", debug_view);
	// Per-cell instances override this with their own baked map; this is only
	// a safety default so the shared base material never leaves the sampler
	// unbound (see terrain_shader.h).
	terrain_material->set_shader_param("detail_normal_map", _get_neutral_normal_texture());
	terrain_material->set_shader_param("detail_normal_strength", detail_normal_strength);
	// Same reasoning: colour_map is a multiply, so it must never be left unbound.
	// Per-cell instances override this with their own painted map.
	terrain_material->set_shader_param("color_map", _get_neutral_white_texture());
}

PoolRealArray CellTerrainManager::_load_heights(int cx, int cz) const {
	const int n = verts_per_side * verts_per_side;
	PoolRealArray h; h.resize(n); PoolRealArray::Write w = h.write();
	String path = world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".heights");
	FileAccess *f = FileAccess::open(path, FileAccess::READ);
	if (f) { for (int i = 0; i < n; i++) w[i] = f->eof_reached() ? 0.0f : f->get_float(); f->close(); memdelete(f); }
	else { for (int i = 0; i < n; i++) w[i] = 0.0f; }
	return h;
}

// Same file-per-cell pattern as _load_heights, but missing-file/eof defaults
// to 1.0 (fully navigable) instead of 0.0 — see CellTerrain::_mask_at_s.
PoolRealArray CellTerrainManager::_load_nav_mask(int cx, int cz) const {
	const int n = verts_per_side * verts_per_side;
	PoolRealArray m; m.resize(n); PoolRealArray::Write w = m.write();
	String path = world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".navmask");
	FileAccess *f = FileAccess::open(path, FileAccess::READ);
	if (f) { for (int i = 0; i < n; i++) w[i] = f->eof_reached() ? 1.0f : f->get_float(); f->close(); memdelete(f); }
	else { for (int i = 0; i < n; i++) w[i] = 1.0f; }
	return m;
}

// Same file-per-cell pattern as _load_nav_mask, defaulting to 1.0 (solid)
// — see CellTerrain::_hole_at_s.
PoolRealArray CellTerrainManager::_load_hole_mask(int cx, int cz) const {
	const int n = verts_per_side * verts_per_side;
	String path = world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".holes");
	FileAccess *f = FileAccess::open(path, FileAccess::READ);
	// No file means nothing was ever cut out. Return EMPTY rather than an
	// all-ones array: CellTerrain treats an empty mask as "solid everywhere"
	// and has_holes() can then answer without scanning, which matters because
	// _apply_collision() calls it on every cell that streams in.
	if (!f) return PoolRealArray();
	PoolRealArray m; m.resize(n);
	{
		PoolRealArray::Write w = m.write();
		for (int i = 0; i < n; i++) w[i] = f->eof_reached() ? 1.0f : f->get_float();
	}
	f->close(); memdelete(f);
	return m;
}

// Null Ref when there is no file: an unpainted tint is white, and CellTerrain
// binds a 4x4 white texture for that case instead of carrying a full image.
Ref<Image> CellTerrainManager::_load_color_map(int cx, int cz) const {
	String path = world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".color.png");
	FileAccess *f = FileAccess::open(path, FileAccess::READ);
	if (!f) return Ref<Image>();
	PoolByteArray bytes; bytes.resize(f->get_len());
	{ PoolByteArray::Write w = bytes.write(); f->get_buffer(w.ptr(), bytes.size()); }
	memdelete(f);
	Ref<Image> img; img.instance();
	if (img->load_png_from_buffer(bytes) == OK && img->get_width() == verts_per_side && img->get_height() == verts_per_side) {
		if (img->get_format() != Image::FORMAT_RGBA8) img->convert(Image::FORMAT_RGBA8);
		return img;
	}
	return Ref<Image>();
}

Ref<Image> CellTerrainManager::_load_splatmap(int cx, int cz, int idx) const {
	String path = world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".splat" + itos(idx) + ".png");
	Ref<Image> img; img.instance();
	FileAccess *_sfa = FileAccess::open(path, FileAccess::READ);
	if (_sfa) {
		PoolByteArray _splat_bytes; _splat_bytes.resize(_sfa->get_len());
		{ PoolByteArray::Write w = _splat_bytes.write(); _sfa->get_buffer(w.ptr(), _splat_bytes.size()); }
		memdelete(_sfa);
		if (img->load_png_from_buffer(_splat_bytes) == OK && img->get_width() == verts_per_side && img->get_height() == verts_per_side) {
			if (img->get_format() != Image::FORMAT_RGBA8) img->convert(Image::FORMAT_RGBA8);
			return img;
		}
	}
	img->create(verts_per_side, verts_per_side, false, Image::FORMAT_RGBA8);
	img->fill(idx == 0 ? Color(1, 0, 0, 0) : Color(0, 0, 0, 0));
	return img;
}

void CellTerrainManager::_load_neighbor_edges_from_disk(int cx, int cz,
		PoolRealArray &r_n, PoolRealArray &r_s, PoolRealArray &r_e, PoolRealArray &r_w) const {
	const int vps = verts_per_side;
	if (FileAccess::exists(world_dir.plus_file("cell_" + itos(cx + 1) + "_" + itos(cz) + ".heights"))) {
		PoolRealArray nh = _load_heights(cx + 1, cz);
		r_e.resize(vps);
		PoolRealArray::Write w = r_e.write();
		for (int z = 0; z < vps; z++) w[z] = nh[z * vps + 1];
	}
	if (FileAccess::exists(world_dir.plus_file("cell_" + itos(cx - 1) + "_" + itos(cz) + ".heights"))) {
		PoolRealArray nh = _load_heights(cx - 1, cz);
		r_w.resize(vps);
		PoolRealArray::Write w = r_w.write();
		for (int z = 0; z < vps; z++) w[z] = nh[z * vps + (vps - 2)];
	}
	if (FileAccess::exists(world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz + 1) + ".heights"))) {
		PoolRealArray nh = _load_heights(cx, cz + 1);
		r_n.resize(vps);
		PoolRealArray::Write w = r_n.write();
		for (int x = 0; x < vps; x++) w[x] = nh[1 * vps + x];
	}
	if (FileAccess::exists(world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz - 1) + ".heights"))) {
		PoolRealArray nh = _load_heights(cx, cz - 1);
		r_s.resize(vps);
		PoolRealArray::Write w = r_s.write();
		for (int x = 0; x < vps; x++) w[x] = nh[(vps - 2) * vps + x];
	}
}

bool CellTerrainManager::_save_cell(int cx, int cz) {
	CellTerrain *t = get_cell(cx, cz); if (!t) return false;
	DirAccess *da = DirAccess::create_for_path(world_dir);
	if (da) { da->make_dir_recursive(world_dir); memdelete(da); }
	String hpath = world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".heights");
	FileAccess *f = FileAccess::open(hpath, FileAccess::WRITE); if (!f) return false;
	PoolRealArray h = t->get_heights(); PoolRealArray::Read r = h.read();
	for (int i = 0; i < h.size(); i++) f->store_float(r[i]);
	f->close(); memdelete(f);
	// Only written once the mask has actually been touched (see
	// CellTerrain::_ensure_nav_mask) — no file means "fully navigable",
	// matching _load_nav_mask's default.
	PoolRealArray mask = t->get_nav_mask();
	if (mask.size() == h.size()) {
		String mpath = world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".navmask");
		FileAccess *mf = FileAccess::open(mpath, FileAccess::WRITE);
		if (mf) {
			PoolRealArray::Read mr = mask.read();
			for (int i = 0; i < mask.size(); i++) mf->store_float(mr[i]);
			mf->close(); memdelete(mf);
		}
	}
	// Only written once something was actually cut - no file means "solid",
	// matching _load_hole_mask's default.
	PoolRealArray holes = t->get_hole_mask();
	if (holes.size() == h.size()) {
		String hp = world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".holes");
		FileAccess *hf = FileAccess::open(hp, FileAccess::WRITE);
		if (hf) {
			PoolRealArray::Read hr = holes.read();
			for (int i = 0; i < holes.size(); i++) hf->store_float(hr[i]);
			hf->close(); memdelete(hf);
		}
	}
	for (int s = 0; s < 4; s++) {
		Ref<Image> splat = t->get_splatmap(s);
		if (splat.is_valid()) { String sp = world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".splat" + itos(s) + ".png"); splat->save_png(sp); }
	}
	// Same rule: an unpainted tint has no image and needs no file.
	Ref<Image> tint = t->get_color_map();
	if (tint.is_valid()) {
		tint->save_png(world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".color.png"));
	}
	return true;
}

CellTerrain *CellTerrainManager::_spawn_cell(int cx, int cz, int lod, bool collision) {
	_ensure_material();
	CellTerrain *t = memnew(CellTerrain);
	t->set_verts_per_side(verts_per_side);
	t->set_cell_size(cell_size);
	t->set_height_scale(height_scale);
	t->set_lod_level(lod);
	t->set_enable_collision(collision);
	t->set_enable_navmesh(collision && enable_navmesh);
	t->set_nav_max_slope_deg(nav_max_slope_deg);
	t->set_nav_downsample(nav_downsample);
	t->set_shader_parallax_max_distance(parallax_max_distance);
	t->set_shader_debug_view(debug_view);
	t->set_heights(_load_heights(cx, cz));
	t->set_nav_mask(_load_nav_mask(cx, cz));
	t->set_hole_mask(_load_hole_mask(cx, cz));
	for (int s = 0; s < 4; s++) t->set_splatmap(s, _load_splatmap(cx, cz, s));
	t->set_color_map(_load_color_map(cx, cz));
	t->set_material(terrain_material);
	t->set_shader_resources(albedo_texture_array, normal_texture_array, num_layers, texture_scale, height_blend_sharpness, layer_params_tex, layer_color_tex, detail_normal_strength,
				macro_variation_color1, macro_variation_color2, macro_variation_scale);
	if (_debug_normals) t->set_debug_normals(true, _debug_normal_length);
	if (_debug_navmesh) t->set_debug_navmesh(true);
	add_child(t);
	Transform xf; xf.origin = Vector3(cx * cell_size, 0.0f, cz * cell_size);
	t->set_transform(xf);
	// Same reasoning as _integrate_ready()'s async path: a just-created cell
	// shouldn't let physics interpolation blend in from its pre-existence
	// transform history.
	t->reset_physics_interpolation();
	return t;
}

int CellTerrainManager::_lod_for_distance(int dist) const {
	if (dist <= load_radius) return 0;
	// One LOD step PER RING. The old mapping squeezed every ring into LOD 1-3,
	// so with load_radius=1 / lod_radius=6 the five rings came out 1,1,2,2,3 -
	// neighbouring rings shared a level and the detail fall-off was uneven.
	int lod = dist - load_radius;

	// Hard ceiling from the grid: each level halves the step, and a step larger
	// than the grid has nothing left to decimate. seg=64 tops out at level 6.
	int seg = MAX(1, verts_per_side - 1);
	int grid_max = 0;
	while ((1 << (grid_max + 1)) <= seg) grid_max++;

	return CLAMP(lod, 1, MIN(grid_max, max_lod_level));
}

void CellTerrainManager::_update_neighbor_edges(int cx, int cz) {
	Map<int64_t, CellTerrain *>::Element *self = _active.find(_key(cx, cz));
	if (!self) return;
	const int vps = verts_per_side;
	PoolRealArray n_e, s_e, e_e, w_e;
	Map<int64_t, CellTerrain *>::Element *E;
	if ((E = _active.find(_key(cx+1,cz)))) { PoolRealArray nh = E->value()->get_heights(); e_e.resize(vps); PoolRealArray::Write w = e_e.write(); for (int z=0;z<vps;z++) w[z]=nh[z*vps+1]; }
	if ((E = _active.find(_key(cx-1,cz)))) { PoolRealArray nh = E->value()->get_heights(); w_e.resize(vps); PoolRealArray::Write w = w_e.write(); for (int z=0;z<vps;z++) w[z]=nh[z*vps+(vps-2)]; }
	if ((E = _active.find(_key(cx,cz+1)))) { PoolRealArray nh = E->value()->get_heights(); n_e.resize(vps); PoolRealArray::Write w = n_e.write(); for (int x=0;x<vps;x++) w[x]=nh[1*vps+x]; }
	if ((E = _active.find(_key(cx,cz-1)))) { PoolRealArray nh = E->value()->get_heights(); s_e.resize(vps); PoolRealArray::Write w = s_e.write(); for (int x=0;x<vps;x++) w[x]=nh[(vps-2)*vps+x]; }
	self->value()->set_neighbor_edges(n_e, s_e, e_e, w_e);
}

void CellTerrainManager::_scan_all_cell_keys(Set<int64_t> &r_out) const {
	DirAccess *da = DirAccess::create_for_path(world_dir);
	if (!da || da->change_dir(world_dir) != OK) {
		if (da) memdelete(da);
		return;
	}
	da->list_dir_begin();
	String fname = da->get_next();
	while (fname != "") {
		if (fname.ends_with(".heights")) {
			String base = fname.get_basename(); // cell_X_Z
			Vector<String> parts = base.split("_");
			if (parts.size() >= 3 && parts[0] == "cell") {
				int cx = parts[1].to_int();
				int cz = parts[2].to_int();
				r_out.insert(_key(cx, cz));
			}
		}
		fname = da->get_next();
	}
	da->list_dir_end();
	memdelete(da);
}

void CellTerrainManager::_evaluate(int cx, int cz) {
	uint64_t _tr = cs_trace_enabled() ? OS::get_singleton()->get_ticks_usec() : 0;
	Set<int64_t> rebuild_set;

	// Editor-only: every saved cell stays resident regardless of lod_radius, so
	// the whole edited world stays visible while sculpting. No effect at runtime.
	bool show_all = show_all_in_editor && Engine::get_singleton()->is_editor_hint();
	Set<int64_t> all_edited;
	if (show_all) _scan_all_cell_keys(all_edited);

	// For deferring NEW render-only (no-collision) cells that are behind the
	// camera — see cull_lod_behind_camera's field comment. Never applied to
	// collision cells, and never used to unload an already-active cell.
	bool have_cam_dir = false;
	Vector3 cam_pos, cam_forward;
	if (cull_lod_behind_camera) {
		Camera *cam = _get_active_camera();
		if (cam) {
			Transform cam_xf = cam->get_global_transform();
			cam_pos = cam_xf.origin;
			cam_forward = -cam_xf.basis.get_axis(2);
			cam_forward.y = 0.0f;
			if (cam_forward.length_squared() > 0.0001f) {
				cam_forward.normalize();
				have_cam_dir = true;
			}
		}
	}

	// --- Remove out-of-range cells (unless kept resident by show_all) ---
	List<int64_t> remove;
	for (Map<int64_t, CellTerrain *>::Element *E = _active.front(); E; E = E->next()) {
		int x, z; _unkey(E->key(), x, z);
		bool out_of_range = (ABS(x - cx) > lod_radius || ABS(z - cz) > lod_radius);
		if (out_of_range && !(show_all && all_edited.has(E->key()))) remove.push_back(E->key());
	}
	for (List<int64_t>::Element *E = remove.front(); E; E = E->next()) {
		int x, z; _unkey(E->get(), x, z);
		if (_dirty.has(E->get())) { _save_cell(x, z); _dirty.erase(E->get()); }
		CellTerrain *t = _active[E->get()]; _active.erase(E->get()); t->queue_delete();
		_recompute_again.erase(E->get());
		rebuild_set.insert(_key(x+1,z)); rebuild_set.insert(_key(x-1,z));
		rebuild_set.insert(_key(x,z+1)); rebuild_set.insert(_key(x,z-1));
	}

	// --- Load / update cells in range ---
	for (int z = cz - lod_radius; z <= cz + lod_radius; z++) {
		for (int x = cx - lod_radius; x <= cx + lod_radius; x++) {
			int dist = MAX(ABS(x - cx), ABS(z - cz));
			int lod = _lod_for_distance(dist);
			bool coll = (dist <= load_radius);
			int64_t k = _key(x, z);

			if (_active.has(k)) {
				// Check if LOD changed.
				CellTerrain *t = _active[k];
				if (t->get_lod_level() != lod || t->get_enable_collision() != coll) {
					t->set_lod_level(lod);
					t->set_enable_collision(coll);
					t->set_enable_navmesh(coll && enable_navmesh);
					rebuild_set.insert(k);
				}
			} else {
				// Defer a brand-new render-only cell that's behind the camera —
				// it'll be picked up on a later _evaluate() once the camera
				// turns toward it (or the player gets close enough to need it
				// as a collision cell regardless of facing).
				if (!coll && have_cam_dir) {
					Vector3 cell_center = get_global_transform().origin + Vector3((x + 0.5f) * cell_size, 0.0f, (z + 0.5f) * cell_size);
					Vector3 to_cell = cell_center - cam_pos;
					to_cell.y = 0.0f;
					if (to_cell.length_squared() > 0.0001f && cam_forward.dot(to_cell.normalized()) < frustum_cull_dot) {
						continue;
					}
				}
				if (_thread_running && !_loading.has(k)) {
					// Submit async: I/O + PNG decode on worker thread, node creation
					// deferred to _integrate_ready() (main thread, max N cells/frame).
					_submit_load(x, z, lod, coll);
				} else if (!_thread_running) {
					// Synchronous fallback before NOTIFICATION_READY (editor tool calls).
					_active[k] = _spawn_cell(x, z, lod, coll);
					_release_baked_navmesh(k);
					rebuild_set.insert(k);
					rebuild_set.insert(_key(x+1,z)); rebuild_set.insert(_key(x-1,z));
					rebuild_set.insert(_key(x,z+1)); rebuild_set.insert(_key(x,z-1));
				}
				// If _loading.has(k): job already in flight, do nothing.
			}
		}
	}

	// --- Show-all: load any edited cell the radius loop above didn't reach ---
	if (show_all) {
		for (Set<int64_t>::Element *E = all_edited.front(); E; E = E->next()) {
			int64_t k = E->get();
			if (_active.has(k) || _loading.has(k)) continue;
			int x, z; _unkey(k, x, z);
			int dist = MAX(ABS(x - cx), ABS(z - cz));
			int lod = _lod_for_distance(dist);
			if (_thread_running) {
				_submit_load(x, z, lod, dist <= load_radius);
			} else {
				_active[k] = _spawn_cell(x, z, lod, dist <= load_radius);
				_release_baked_navmesh(k);
				rebuild_set.insert(k);
			}
		}
	}

	// --- Rebuild dirty ---
	for (Set<int64_t>::Element *E = rebuild_set.front(); E; E = E->next()) {
		int x, z; _unkey(E->get(), x, z);
		if (!_active.has(E->get())) continue;
		_update_neighbor_edges(x, z);
		if (_thread_running) {
			// Off the main thread (see _submit_recompute) — this fires on
			// every LOD/collision-tier transition as the player moves, so a
			// direct ->rebuild() here is a real streaming-freeze source.
			// update_collision stays true (the default) here specifically
			// because enable_collision may have just flipped, unlike the pure
			// neighbor-edge-restitch case in _integrate_ready().
			_submit_recompute(x, z);
		} else {
			// No worker thread to pick up an async job (e.g. before
			// NOTIFICATION_READY starts it) — fall back to synchronous.
			_active[E->get()]->rebuild();
		}
	}

	// --- HLOD chunks ---
	uint64_t _tr_hlod = cs_trace_enabled() ? OS::get_singleton()->get_ticks_usec() : 0;
	_load_hlod_chunks(cx, cz);
	if (_tr_hlod) cs_trace_report("_load_hlod_chunks", _tr_hlod, "chunks=" + itos(_hlod_active.size()));

	_last_cx = cx; _last_cz = cz; _has_last = true;
	if (_tr) {
		cs_trace_report("_evaluate", _tr, "rebuild_set=" + itos(rebuild_set.size()) +
				" active=" + itos(_active.size()) + " loading=" + itos(_loading.size()));
	}
}

// --- Opt-in editor/runtime tracing -------------------------------------------
// Set TUNA_CELL_TRACE=1 to log any cell_system call that exceeds the
// threshold (TUNA_CELL_TRACE_MS, default 30ms). Off by default and costs one
// already-cached bool when disabled, so it can live in the shipping build —
// the alternative, driving the editor GUI to reproduce, is off the table.
static int _cs_trace_state = -1; // -1 = not yet resolved from the environment

bool cs_trace_enabled() {
	if (_cs_trace_state < 0) {
		_cs_trace_state = OS::get_singleton()->get_environment("TUNA_CELL_TRACE") == "1" ? 1 : 0;
	}
	return _cs_trace_state == 1;
}

void cs_trace_set_enabled(bool p_enabled) {
	_cs_trace_state = p_enabled ? 1 : 0;
}
uint64_t cs_trace_threshold_usec() {
	static uint64_t cached = 0;
	if (cached == 0) {
		String v = OS::get_singleton()->get_environment("TUNA_CELL_TRACE_MS");
		int ms = v.empty() ? 30 : v.to_int();
		cached = (uint64_t)MAX(1, ms) * 1000;
	}
	return cached;
}
void cs_trace_report(const char *p_what, uint64_t p_start_usec, const String &p_extra) {
	uint64_t el = OS::get_singleton()->get_ticks_usec() - p_start_usec;
	if (el < cs_trace_threshold_usec()) return;
	print_line(String("[cell-trace] ") + p_what + " " + rtos(el / 1000.0) + "ms" +
			(p_extra.empty() ? String() : " " + p_extra));
}

void CellTerrainManager::set_streaming_enabled(bool e) {
	if (streaming_enabled == e) return;
	streaming_enabled = e;
	set_visible(e);
	if (!e && is_inside_tree()) editor_clear();
}

bool CellTerrainManager::has_collision_at_world(const Vector3 &p_world_pos) const {
	CellTerrain *c = get_cell_at_world(p_world_pos);
	return c && c->has_collision_ready();
}

void CellTerrainManager::stream_to(const Vector3 &p_world_pos) {
	if (!streaming_enabled) return;
	// Into this manager's own space first. Every lookup (get_cell_at_world,
	// get_height_at_world, ensure_collision_at_world) already subtracted the
	// origin; this was the one entry point that did not, so a manager that was
	// not sitting at (0, 0, 0) streamed cells around one place and answered
	// queries about another.
	const Vector3 world_pos = p_world_pos - get_global_transform().origin;
	int cx = (int)Math::floor(world_pos.x / cell_size);
	int cz = (int)Math::floor(world_pos.z / cell_size);

	if (_thread_running) {
		// Walking pace keeps ~preload_margin's worth of lead time; sprinting
		// or a vehicle gets proportionally more, so the async pipeline
		// (I/O + compute + budgeted integration) still finishes well before
		// arrival instead of the fixed margin being outrun. See the
		// streaming_lookahead_seconds field comment.
		float speed = _estimate_speed(world_pos);
		float margin = CLAMP(preload_margin + (speed * streaming_lookahead_seconds) / MAX(1.0f, cell_size), 0.0f, 0.49f);
		if (margin > 0.0f) {
			float fx = world_pos.x / cell_size - cx;
			float fz = world_pos.z / cell_size - cz;
			_preload_border(cx, cz, fx, fz, margin);
			if (preload_collision_upgrade) _preload_collision_upgrade(cx, cz, fx, fz, margin);
		}
	}

	// First stream at runtime: build the collision ring synchronously so the
	// very first physics step has ground under it. A one-off hitch at level
	// start is the right trade against every body falling through the world.
	if (!_has_last && preload_collision_on_start && !Engine::get_singleton()->is_editor_hint()) {
		int built = ensure_collision_at_world(p_world_pos);
		if (built > 0) {
			print_line("CellTerrain: pre-built " + itos(built) + " collision cells at spawn.");
		}
	}

	if (_has_last && cx == _last_cx && cz == _last_cz) return;
	_evaluate(cx, cz);
}
void CellTerrainManager::editor_focus(const Vector3 &p) { stream_to(p); }

void CellTerrainManager::rebuild_at(int cx, int cz) {
	mark_dirty(cx, cz);
	const int dx[5]={0,1,-1,0,0}; const int dz[5]={0,0,0,1,-1};
	for (int i=0;i<5;i++) { int x=cx+dx[i],z=cz+dz[i]; if (!_active.has(_key(x,z))) continue; _update_neighbor_edges(x,z); _active[_key(x,z)]->rebuild(); }
}

void CellTerrainManager::rebuild_region(const Set<int64_t> &p_cells) {
	const int dx[5]={0,1,-1,0,0}; const int dz[5]={0,0,0,1,-1};
	// Expand to the set of cells touched plus their neighbors (for edge stitching).
	Set<int64_t> all;
	for (Set<int64_t>::Element *E = p_cells.front(); E; E = E->next()) {
		int cx, cz; _unkey(E->get(), cx, cz);
		mark_dirty(cx, cz);
		for (int i = 0; i < 5; i++) {
			int64_t k = _key(cx + dx[i], cz + dz[i]);
			if (_active.has(k)) all.insert(k);
		}
	}
	// Update edges for the whole region first, then rebuild each cell exactly once.
	for (Set<int64_t>::Element *E = all.front(); E; E = E->next()) {
		int x, z; _unkey(E->get(), x, z);
		_update_neighbor_edges(x, z);
	}
	for (Set<int64_t>::Element *E = all.front(); E; E = E->next())
		_active[E->get()]->rebuild();
}
int64_t CellTerrainManager::_chunk_key_for_cell(int cx, int cz) const {
	int cs = MAX(1, hlod_chunk_size);
	int hx = (cx >= 0) ? cx / cs : (cx - cs + 1) / cs;
	int hz = (cz >= 0) ? cz / cs : (cz - cs + 1) / cs;
	return _key(hx, hz);
}

void CellTerrainManager::mark_dirty(int cx, int cz) {
	_dirty.insert(_key(cx, cz));
	_hlod_dirty.insert(_chunk_key_for_cell(cx, cz));
}

void CellTerrainManager::rebake_dirty_hlod() {
	if (_hlod_dirty.empty()) return;

	// The bake reads cell heights/splatmaps from disk, so flush pending edits first.
	save_dirty();

	for (Set<int64_t>::Element *E = _hlod_dirty.front(); E; E = E->next()) {
		int hx, hz; _unkey(E->get(), hx, hz);
		bake_hlod_chunk(hx, hz);

		// Reload the chunk instance so the refreshed mesh/splatmaps show.
		int64_t k = _key(hx, hz);
		if (_hlod_active.has(k)) { _hlod_active[k]->queue_delete(); _hlod_active.erase(k); }
		MeshInstance *mi = _load_hlod_chunk(hx, hz);
		if (mi) _hlod_active[k] = mi;
	}
	_hlod_dirty.clear();

	// Re-apply visibility so the reloaded chunks hide where detail cells overlap.
	if (_has_last) _load_hlod_chunks(_last_cx, _last_cz);
}
void CellTerrainManager::save_dirty() {
	List<int64_t> keys; for (Set<int64_t>::Element *E=_dirty.front();E;E=E->next()) keys.push_back(E->get());
	for (List<int64_t>::Element *E=keys.front();E;E=E->next()) { int x,z; _unkey(E->get(),x,z); _save_cell(x,z); }
	_dirty.clear();
}
void CellTerrainManager::ensure_cell_sync(int cx, int cz) {
	int64_t k = _key(cx, cz);
	if (_active.has(k)) return;

	// Remove from in-flight tracking so the async path won't skip it later.
	_loading.erase(k);

	// Drop any queued async job for this cell so the worker won't overwrite us.
	_load_mutex.lock();
	for (List<PendingLoad *>::Element *E = _load_queue.front(); E; E = E->next()) {
		if (E->get()->cx == cx && E->get()->cz == cz) {
			memdelete(E->get());
			_load_queue.erase(E);
			break;
		}
	}
	_load_mutex.unlock();

	// Load synchronously on the calling (main) thread.
	PoolRealArray heights = _load_heights(cx, cz);
	PoolRealArray nav_mask = _load_nav_mask(cx, cz);
	PoolRealArray hole_mask = _load_hole_mask(cx, cz);
	Ref<Image> splatmaps[4];
	int nsplat = (num_layers + 3) / 4;
	for (int i = 0; i < nsplat && i < 4; i++) {
		splatmaps[i] = _load_splatmap(cx, cz, i);
	}

	CellTerrain *cell = _create_cell_from_data(cx, cz, 0, true, heights, nav_mask, hole_mask,
			splatmaps, _load_color_map(cx, cz));
	if (cell) {
		// MUST be registered before anything else: get_cell() and rebuild_at()
		// both look the cell up in _active, so without this the cell existed as
		// a child but had no mesh, no collision, and was invisible to queries.
		_active[k] = cell;
		_release_baked_navmesh(k);
		cell->set_skip_ready_rebuild(false);
		_update_neighbor_edges(cx, cz);
		rebuild_at(cx, cz);
	}
}

void CellTerrainManager::editor_clear() {
	save_dirty();
	List<int64_t> keys; for (Map<int64_t,CellTerrain*>::Element *E=_active.front();E;E=E->next()) keys.push_back(E->key());
	for (List<int64_t>::Element *E=keys.front();E;E=E->next()) { _active[E->get()]->queue_delete(); _active.erase(E->get()); }
	// Also clear HLOD.
	for (Map<int64_t,MeshInstance*>::Element *E=_hlod_active.front();E;E=E->next()) E->value()->queue_delete();
	_hlod_active.clear();
	_hlod_loaded = false;
	_has_last = false;
	// Clear async state. Jobs still in _load_queue will complete on the worker and
	// arrive in _ready_queue; _integrate_ready discards them because _has_last is false.
	_loading.clear();
	if (_thread_running) {
		_load_mutex.lock();
		for (List<PendingLoad *>::Element *E = _ready_queue.front(); E; E = E->next()) memdelete(E->get());
		_ready_queue.clear();
		_load_mutex.unlock();
	}
}
CellTerrain *CellTerrainManager::get_cell(int cx, int cz) const {
	const Map<int64_t,CellTerrain*>::Element *E=_active.find(_key(cx,cz)); return E?E->value():nullptr;
}

CellTerrain *CellTerrainManager::get_cell_at_world(const Vector3 &p_world_pos) const {
	if (cell_size <= 0.0f) return nullptr;
	const Vector3 origin = get_global_transform().origin;
	const int cx = (int)Math::floor((p_world_pos.x - origin.x) / cell_size);
	const int cz = (int)Math::floor((p_world_pos.z - origin.z) / cell_size);
	return get_cell(cx, cz);
}
float CellTerrainManager::get_height_at_world(float wx, float wz) const {
	Vector3 origin = get_global_transform().origin;
	float lx = wx - origin.x;
	float lz = wz - origin.z;
	int cx = (int)Math::floor(lx / cell_size);
	int cz = (int)Math::floor(lz / cell_size);
	CellTerrain *cell = get_cell(cx, cz);
	if (!cell) return 0.0f;

	float spacing = cell_size / (float)(verts_per_side - 1);
	float cell_lx = lx - cx * cell_size;
	float cell_lz = lz - cz * cell_size;
	int vx = CLAMP((int)(cell_lx / spacing), 0, verts_per_side - 2);
	int vz = CLAMP((int)(cell_lz / spacing), 0, verts_per_side - 2);
	float tx = CLAMP((cell_lx - vx * spacing) / spacing, 0.0f, 1.0f);
	float tz = CLAMP((cell_lz - vz * spacing) / spacing, 0.0f, 1.0f);

	float h00 = cell->get_height_at(vx,     vz)     * height_scale;
	float h10 = cell->get_height_at(vx + 1, vz)     * height_scale;
	float h01 = cell->get_height_at(vx,     vz + 1) * height_scale;
	float h11 = cell->get_height_at(vx + 1, vz + 1) * height_scale;
	return Math::lerp(Math::lerp(h00, h10, tx), Math::lerp(h01, h11, tx), tz);
}

bool CellTerrainManager::raycast_terrain(const Vector3 &p_from, const Vector3 &p_dir, Vector3 &r_hit) const {
	// Analytic march against the heightmap instead of a physics raycast.
	//
	// The editor tools used to do intersect_ray() out to 100,000 units. A ray
	// that HITS terrain returns quickly, but one that misses (pointing at sky,
	// past the terrain edge, or sweeping toward the toolbar) traverses the full
	// length through every btHeightfieldTerrainShape the broadphase hands back,
	// walking heightfield grid cells the whole way — which froze the editor on
	// mouse-move. This has a hard bounded cost instead: a coarse march with a
	// growing step, then a bisection refine, all Map lookups.
	//
	// It also works on cells that carry NO collision (the LOD ring), which a
	// physics ray could never hit.
	Vector3 dir = p_dir;
	if (dir.length_squared() < CMP_EPSILON) return false;
	dir.normalize();

	const float max_dist = raycast_max_distance;
	float t = 0.0f;
	float prev_t = 0.0f;
	// Signed distance from the ray to the ground: >0 above, <0 below.
	float prev_d = p_from.y - get_height_at_world(p_from.x, p_from.z);

	// Already underground at the origin: treat the start as the hit rather than
	// marching away from it.
	if (prev_d <= 0.0f) {
		r_hit = p_from;
		return true;
	}

	// Step grows with distance so near the cursor is precise and far away stays
	// cheap. ~120 steps to cover the default range.
	while (t < max_dist) {
		float step = MAX(1.0f, t * 0.05f);
		t = MIN(t + step, max_dist);
		Vector3 p = p_from + dir * t;
		float h = get_height_at_world(p.x, p.z);
		float d = p.y - h;
		if (d <= 0.0f) {
			// Crossed the surface between prev_t and t — bisect to refine.
			float lo = prev_t, hi = t;
			for (int i = 0; i < 16; i++) {
				float mid = (lo + hi) * 0.5f;
				Vector3 mp = p_from + dir * mid;
				float md = mp.y - get_height_at_world(mp.x, mp.z);
				if (md > 0.0f) {
					lo = mid;
				} else {
					hi = mid;
				}
			}
			r_hit = p_from + dir * ((lo + hi) * 0.5f);
			return true;
		}
		prev_t = t;
		prev_d = d;
	}
	return false;
}

Variant CellTerrainManager::raycast_terrain_v(const Vector3 &p_from, const Vector3 &p_dir) const {
	Vector3 hit;
	if (!raycast_terrain(p_from, p_dir, hit)) return Variant();
	return hit;
}

int CellTerrainManager::get_pending_count() {
	_load_mutex.lock();
	int queued = _load_queue.size() + _ready_queue.size();
	_load_mutex.unlock();
	return queued + _loading.size() + _recomputing.size();
}

void CellTerrainManager::set_cell_nav_obstacles(int cx, int cz, const PoolRealArray &p_obstacles) {
	CellTerrain *c = get_cell(cx, cz);
	if (!c) return; // not streamed in; FoliageManager re-stamps on its next refresh
	if (c->get_nav_obstacles().size() == 0 && p_obstacles.size() == 0) return;
	c->set_nav_obstacles(p_obstacles);
	if (_thread_running) {
		_submit_recompute(cx, cz, false); // nav/mesh only, collision untouched
	} else {
		c->rebuild();
	}
}

int CellTerrainManager::ensure_collision_at_world(const Vector3 &p_world_pos) {
	Vector3 origin = get_global_transform().origin;
	int ccx = (int)Math::floor((p_world_pos.x - origin.x) / cell_size);
	int ccz = (int)Math::floor((p_world_pos.z - origin.z) / cell_size);
	int built = 0;
	// The collision tier is exactly the load_radius ring (see _evaluate).
	for (int cz = ccz - load_radius; cz <= ccz + load_radius; cz++) {
		for (int cx = ccx - load_radius; cx <= ccx + load_radius; cx++) {
			if (get_cell(cx, cz)) continue;
			ensure_cell_sync(cx, cz);
			built++;
		}
	}
	return built;
}

void CellTerrainManager::set_debug_normals(bool enabled, float length) {
	_debug_normals = enabled;
	_debug_normal_length = length;
	for (Map<int64_t, CellTerrain *>::Element *E = _active.front(); E; E = E->next())
		E->value()->set_debug_normals(enabled, length);
}

void CellTerrainManager::set_debug_navmesh(bool enabled) {
	_debug_navmesh = enabled;
	for (Map<int64_t, CellTerrain *>::Element *E = _active.front(); E; E = E->next())
		E->value()->set_debug_navmesh(enabled);
}

void CellTerrainManager::refresh() {
	// Rebuild the texture arrays from the current layer set and re-push them to
	// every active cell. Without this, cells keep whatever arrays they were given
	// at spawn time (often null), so assigning textures later left them black.
	_texarray_dirty = true;
	_ensure_material();
	for (Map<int64_t, CellTerrain *>::Element *E = _active.front(); E; E = E->next()) {
		E->value()->set_shader_resources(albedo_texture_array, normal_texture_array,
				num_layers, texture_scale, height_blend_sharpness, layer_params_tex, layer_color_tex, detail_normal_strength,
				macro_variation_color1, macro_variation_color2, macro_variation_scale);
		E->value()->rebuild();
	}
	_has_last = false;
}

void CellTerrainManager::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			add_to_group("cell_terrain_managers");
			set_process(true);
			_thread_exit = false;
			_thread_running = true;
			_start_workers(_resolve_thread_count());

			// In the editor, terrain otherwise only streams in once something
			// (the sculpt tool, a mouse event) calls stream_to() — which meant
			// opening a scene showed nothing until you clicked the manager node.
			// Kick off one initial stream around target_path (or this node's own
			// position if unset) so the terrain is visible as soon as the scene loads.
			if (Engine::get_singleton()->is_editor_hint()) {
				Spatial *tgt = _get_target();
				Vector3 pos = tgt ? tgt->get_global_transform().origin : get_global_transform().origin;
				call_deferred("stream_to", pos);
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			// Baked regions are raw NavigationServer RIDs, not child nodes — unlike
			// _hlod_active's MeshInstances, they won't get freed by normal node
			// teardown, so they'd otherwise leak on the server for the game's
			// remaining lifetime.
			clear_baked_navmesh();
			if (_thread_running) {
				_stop_workers();
				_thread_running = false;
			}
			// Thread is stopped; safe to drain queues without locking.
			for (List<PendingLoad *>::Element *E = _load_queue.front(); E; E = E->next()) memdelete(E->get());
			_load_queue.clear();
			for (List<PendingLoad *>::Element *E = _ready_queue.front(); E; E = E->next()) memdelete(E->get());
			_ready_queue.clear();
			_loading.clear();
		} break;
		case NOTIFICATION_PROCESS: {
			// Drain async loads every frame (editor and runtime) to stream cells in
			// without blocking. The work limit keeps frame time predictable.
			if (_thread_running) _integrate_ready();
			if (Engine::get_singleton()->is_editor_hint()) break;
			// Under a GameWorld the focus is read once, there, and pushed in
			// through stream_to() for terrain, foliage and objects alike.
			if (external_focus || !streaming_enabled) break;
			// Follow the active camera, not a specific "player" node — terrain
			// should track what's actually being rendered. target_path is only
			// used as a fallback when no camera is current (e.g. headless).
			Camera *cam = _get_active_camera();
			if (cam) {
				stream_to(cam->get_global_transform().origin);
			} else {
				Spatial *tgt = _get_target();
				if (tgt) stream_to(tgt->get_global_transform().origin);
			}
		} break;
	}
}

// --- Property accessors ---
void CellTerrainManager::set_layers(const Array &p_layers) {
	for (int i = 0; i < layers.size(); i++) {
		Ref<CellTerrainLayer> old = layers[i];
		if (old.is_valid() && old->is_connected("changed", this, "_on_layer_changed")) {
			old->disconnect("changed", this, "_on_layer_changed");
		}
	}
	layers.clear();
	layers.resize(p_layers.size());
	for (int i = 0; i < p_layers.size(); i++) {
		Ref<CellTerrainLayer> l = p_layers[i];
		layers.write[i] = l;
		if (l.is_valid()) l->connect("changed", this, "_on_layer_changed");
	}
	num_layers = MAX(1, layers.size());
	_on_layer_changed();
}

Array CellTerrainManager::get_layers() const {
	Array out;
	out.resize(layers.size());
	for (int i = 0; i < layers.size(); i++) out[i] = layers[i];
	return out;
}

int CellTerrainManager::get_dominant_layer_index_at_world(float wx, float wz) const {
	Vector3 origin = get_global_transform().origin;
	float lx = wx - origin.x;
	float lz = wz - origin.z;
	int cx = (int)Math::floor(lx / cell_size);
	int cz = (int)Math::floor(lz / cell_size);
	CellTerrain *cell = get_cell(cx, cz);
	if (!cell) return -1;

	float spacing = cell_size / (float)(verts_per_side - 1);
	float cell_lx = lx - cx * cell_size;
	float cell_lz = lz - cz * cell_size;
	int vx = CLAMP((int)Math::round(cell_lx / spacing), 0, verts_per_side - 1);
	int vz = CLAMP((int)Math::round(cell_lz / spacing), 0, verts_per_side - 1);

	float weights[16] = {};
	for (int s = 0; s < 4; s++) {
		Ref<Image> splat = cell->get_splatmap(s);
		if (splat.is_null() || vx >= splat->get_width() || vz >= splat->get_height()) continue;
		splat->lock();
		Color c = splat->get_pixel(vx, vz);
		splat->unlock();
		weights[s * 4 + 0] = c.r; weights[s * 4 + 1] = c.g;
		weights[s * 4 + 2] = c.b; weights[s * 4 + 3] = c.a;
	}

	int best = -1;
	float best_w = 0.0f;
	for (int i = 0; i < num_layers && i < 16; i++) {
		if (weights[i] > best_w) { best_w = weights[i]; best = i; }
	}
	return best;
}

int CellTerrainManager::get_layer_id_at_world(float wx, float wz) const {
	int idx = get_dominant_layer_index_at_world(wx, wz);
	if (idx < 0 || idx >= layers.size() || layers[idx].is_null()) return -1;
	return layers[idx]->get_layer_id();
}

Ref<CellTerrainLayer> CellTerrainManager::get_layer_at_world(float wx, float wz) const {
	int idx = get_dominant_layer_index_at_world(wx, wz);
	if (idx < 0 || idx >= layers.size()) return Ref<CellTerrainLayer>();
	return layers[idx];
}

void CellTerrainManager::set_height_blend_sharpness(float s) { height_blend_sharpness = s; _push_height_blend_live(); }
float CellTerrainManager::get_height_blend_sharpness() const { return height_blend_sharpness; }

void CellTerrainManager::set_detail_normal_strength(float s) {
	detail_normal_strength = MAX(0.0f, s);
	// Was calling rebuild() per cell - a full mesh + collision + navmesh rebuild
	// for what is only a shader uniform. Pushing it straight to the material
	// gets the same result without touching geometry or the NavigationServer.
	if (terrain_material.is_valid()) terrain_material->set_shader_param("detail_normal_strength", detail_normal_strength);
	for (Map<int64_t, CellTerrain *>::Element *E = _active.front(); E; E = E->next())
		E->value()->set_shader_detail_normal_strength(detail_normal_strength);
	for (Map<int64_t, MeshInstance *>::Element *E = _hlod_active.front(); E; E = E->next()) {
		Ref<ShaderMaterial> m = E->value()->get_material_override();
		if (m.is_valid()) m->set_shader_param("detail_normal_strength", detail_normal_strength);
	}
}
float CellTerrainManager::get_detail_normal_strength()const{return detail_normal_strength;}

void CellTerrainManager::_push_macro_variation_live() {
	if (terrain_material.is_valid()) {
		terrain_material->set_shader_param("macro_variation_color1", macro_variation_color1);
		terrain_material->set_shader_param("macro_variation_color2", macro_variation_color2);
		terrain_material->set_shader_param("macro_variation_scale", macro_variation_scale);
	terrain_material->set_shader_param("parallax_max_distance", parallax_max_distance);
	terrain_material->set_shader_param("debug_view", debug_view);
	}
	for (Map<int64_t, CellTerrain *>::Element *E = _active.front(); E; E = E->next()) {
		E->value()->set_shader_resources(albedo_texture_array, normal_texture_array,
				num_layers, texture_scale, height_blend_sharpness, layer_params_tex, layer_color_tex, detail_normal_strength,
				macro_variation_color1, macro_variation_color2, macro_variation_scale);
		E->value()->rebuild();
	}
}
void CellTerrainManager::_push_height_blend_live() {
	if (terrain_material.is_valid()) terrain_material->set_shader_param("height_blend_sharpness", height_blend_sharpness);
	for (Map<int64_t, CellTerrain *>::Element *E = _active.front(); E; E = E->next())
		E->value()->set_shader_height_blend(height_blend_sharpness);
	for (Map<int64_t, MeshInstance *>::Element *E = _hlod_active.front(); E; E = E->next()) {
		Ref<ShaderMaterial> m = E->value()->get_material_override();
		if (m.is_valid()) m->set_shader_param("height_blend_sharpness", height_blend_sharpness);
	}
}

void CellTerrainManager::_push_texture_scale_live() {
	if (terrain_material.is_valid()) terrain_material->set_shader_param("texture_scale", texture_scale);
	for (Map<int64_t, CellTerrain *>::Element *E = _active.front(); E; E = E->next())
		E->value()->set_shader_texture_scale(texture_scale);
	for (Map<int64_t, MeshInstance *>::Element *E = _hlod_active.front(); E; E = E->next()) {
		Ref<ShaderMaterial> m = E->value()->get_material_override();
		if (m.is_valid()) m->set_shader_param("texture_scale", texture_scale);
	}
}

void CellTerrainManager::_push_parallax_live() {
	// Unlike _push_macro_variation_live(), this does NOT rebuild cells: a view
	// distance changes no geometry, splat, collision or nav data — only a
	// shader uniform on the already-built materials.
	if (terrain_material.is_valid()) terrain_material->set_shader_param("parallax_max_distance", parallax_max_distance);
	for (Map<int64_t, CellTerrain *>::Element *E = _active.front(); E; E = E->next())
		E->value()->set_shader_parallax_max_distance(parallax_max_distance);
	// HLOD chunks carry their own ShaderMaterial (see _load_hlod_chunk).
	for (Map<int64_t, MeshInstance *>::Element *E = _hlod_active.front(); E; E = E->next()) {
		Ref<ShaderMaterial> m = E->value()->get_material_override();
		if (m.is_valid()) m->set_shader_param("parallax_max_distance", parallax_max_distance);
	}
}
void CellTerrainManager::_push_debug_view_live() {
	if (terrain_material.is_valid()) terrain_material->set_shader_param("debug_view", debug_view);
	for (Map<int64_t, CellTerrain *>::Element *E = _active.front(); E; E = E->next())
		E->value()->set_shader_debug_view(debug_view);
	// HLOD chunks share the same shader, so they follow the overlay too —
	// otherwise distant terrain would keep rendering normally and the view
	// would only apply to the streamed ring.
	for (Map<int64_t, MeshInstance *>::Element *E = _hlod_active.front(); E; E = E->next()) {
		Ref<ShaderMaterial> m = E->value()->get_material_override();
		if (m.is_valid()) m->set_shader_param("debug_view", debug_view);
	}
}
void CellTerrainManager::set_debug_view(int mode) { debug_view = CLAMP(mode, 0, 3); _push_debug_view_live(); }
int CellTerrainManager::get_debug_view() const { return debug_view; }

void CellTerrainManager::set_parallax_max_distance(float d) { parallax_max_distance = MAX(0.0f, d); _push_parallax_live(); }
float CellTerrainManager::get_parallax_max_distance() const { return parallax_max_distance; }

void CellTerrainManager::set_macro_variation_color1(const Color &c) { macro_variation_color1 = c; _push_macro_variation_live(); }
Color CellTerrainManager::get_macro_variation_color1() const { return macro_variation_color1; }
void CellTerrainManager::set_macro_variation_color2(const Color &c) { macro_variation_color2 = c; _push_macro_variation_live(); }
Color CellTerrainManager::get_macro_variation_color2() const { return macro_variation_color2; }
void CellTerrainManager::set_macro_variation_scale(float s) { macro_variation_scale = MAX(0.0f, s); _push_macro_variation_live(); }
float CellTerrainManager::get_macro_variation_scale() const { return macro_variation_scale; }

void CellTerrainManager::set_enable_navmesh(bool e) {
	enable_navmesh = e;
	for (Map<int64_t, CellTerrain *>::Element *E = _active.front(); E; E = E->next()) {
		bool coll = E->value()->get_enable_collision();
		E->value()->set_enable_navmesh(coll && enable_navmesh);
		E->value()->rebuild();
	}
}
bool CellTerrainManager::get_enable_navmesh() const { return enable_navmesh; }

void CellTerrainManager::set_nav_max_slope_deg(float d) {
	nav_max_slope_deg = d;
	for (Map<int64_t, CellTerrain *>::Element *E = _active.front(); E; E = E->next()) {
		E->value()->set_nav_max_slope_deg(nav_max_slope_deg);
		E->value()->rebuild();
	}
}
float CellTerrainManager::get_nav_max_slope_deg() const { return nav_max_slope_deg; }

void CellTerrainManager::set_nav_downsample(int d) {
	nav_downsample = MAX(1, d);
	for (Map<int64_t, CellTerrain *>::Element *E = _active.front(); E; E = E->next())
		E->value()->set_nav_downsample(nav_downsample);
}
int CellTerrainManager::get_nav_downsample() const { return nav_downsample; }

void CellTerrainManager::set_target_path(const NodePath&p){target_path=p;} NodePath CellTerrainManager::get_target_path()const{return target_path;}
void CellTerrainManager::set_world_dir(const String&p){world_dir=p;} String CellTerrainManager::get_world_dir()const{return world_dir;}
void CellTerrainManager::set_verts_per_side(int v){verts_per_side=MAX(2,v);} int CellTerrainManager::get_verts_per_side()const{return verts_per_side;}
void CellTerrainManager::set_cell_size(float s){cell_size=s;} float CellTerrainManager::get_cell_size()const{return cell_size;}
void CellTerrainManager::set_height_scale(float s){height_scale=s;} float CellTerrainManager::get_height_scale()const{return height_scale;}
void CellTerrainManager::set_load_radius(int r){load_radius=MAX(0,r);} int CellTerrainManager::get_load_radius()const{return load_radius;}
void CellTerrainManager::set_lod_radius(int r){lod_radius=MAX(0,r);} int CellTerrainManager::get_lod_radius()const{return lod_radius;}
void CellTerrainManager::set_max_lod_level(int l) {
	max_lod_level = CLAMP(l, 1, 8);
	// Re-evaluate so live cells pick up the new mapping instead of waiting for
	// the next cell-boundary crossing.
	if (_has_last) _evaluate(_last_cx, _last_cz);
}
void CellTerrainManager::set_hlod_chunk_size(int s){hlod_chunk_size=MAX(1,s);} int CellTerrainManager::get_hlod_chunk_size()const{return hlod_chunk_size;}
void CellTerrainManager::set_hlod_radius(int r){hlod_radius=MAX(0,r);} int CellTerrainManager::get_hlod_radius()const{return hlod_radius;}
void CellTerrainManager::set_hlod_height_offset(float o){ hlod_height_offset=o; for (Map<int64_t,MeshInstance*>::Element *E=_hlod_active.front();E;E=E->next()) { Transform xf=E->value()->get_transform(); xf.origin.y=o; E->value()->set_transform(xf); } } float CellTerrainManager::get_hlod_height_offset()const{return hlod_height_offset;}
void CellTerrainManager::set_cull_lod_behind_camera(bool e) { cull_lod_behind_camera = e; }
void CellTerrainManager::set_frustum_cull_dot(float d) { frustum_cull_dot = CLAMP(d, -1.0f, 1.0f); }
void CellTerrainManager::set_preload_margin(float m) { preload_margin = CLAMP(m, 0.0f, 0.49f); }
void CellTerrainManager::set_streaming_lookahead_seconds(float s) { streaming_lookahead_seconds = MAX(0.0f, s); }
void CellTerrainManager::set_integrate_budget_ms(float ms) { integrate_budget_ms = MAX(0.0f, ms); }
void CellTerrainManager::set_streaming_threads(int n) {
	streaming_threads = MAX(0, n);
	// Applied live: swap the pool out right now rather than waiting for a
	// scene reload. Safe because a worker mid-job completes and pushes its
	// result before honouring the exit flag, and anything still queued is
	// picked up by the replacement pool.
	if (_thread_running) {
		_stop_workers();
		_start_workers(_resolve_thread_count());
	}
}
void CellTerrainManager::set_texture_scale(float s) { texture_scale = s; _push_texture_scale_live(); }
float CellTerrainManager::get_texture_scale() const { return texture_scale; }
void CellTerrainManager::set_brush_texture(const Ref<Texture>&t){brush_texture=t;} Ref<Texture> CellTerrainManager::get_brush_texture()const{return brush_texture;}
void CellTerrainManager::set_preview_in_editor(bool p_enabled){preview_in_editor=p_enabled;} bool CellTerrainManager::get_preview_in_editor()const{return preview_in_editor;}
void CellTerrainManager::set_show_all_in_editor(bool p_enabled) {
	show_all_in_editor = p_enabled;
	// Toggling with the camera stationary wouldn't otherwise re-trigger _evaluate().
	if (_has_last) _evaluate(_last_cx, _last_cz);
}
bool CellTerrainManager::get_show_all_in_editor() const { return show_all_in_editor; }

void CellTerrainManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("stream_to","p"),&CellTerrainManager::stream_to);
	ClassDB::bind_method(D_METHOD("has_collision_at_world","world_pos"),&CellTerrainManager::has_collision_at_world);
	ClassDB::bind_method(D_METHOD("get_loaded_cell_count"),&CellTerrainManager::get_loaded_cell_count);
	ClassDB::bind_method(D_METHOD("ensure_cell_sync","cx","cz"),&CellTerrainManager::ensure_cell_sync);
	ClassDB::bind_method(D_METHOD("ensure_collision_at_world","world_pos"),&CellTerrainManager::ensure_collision_at_world);
	ClassDB::bind_method(D_METHOD("set_preload_collision_on_start","e"),&CellTerrainManager::set_preload_collision_on_start);
	ClassDB::bind_method(D_METHOD("get_preload_collision_on_start"),&CellTerrainManager::get_preload_collision_on_start);
	ClassDB::bind_method(D_METHOD("rebuild_at","cx","cz"),&CellTerrainManager::rebuild_at);
	ClassDB::bind_method(D_METHOD("mark_dirty","cx","cz"),&CellTerrainManager::mark_dirty);
	ClassDB::bind_method(D_METHOD("save_dirty"),&CellTerrainManager::save_dirty);
	ClassDB::bind_method(D_METHOD("editor_focus","p"),&CellTerrainManager::editor_focus);
	ClassDB::bind_method(D_METHOD("editor_clear"),&CellTerrainManager::editor_clear);
	ClassDB::bind_method(D_METHOD("refresh"),&CellTerrainManager::refresh);
	ClassDB::bind_method(D_METHOD("bake_hlod"),&CellTerrainManager::bake_hlod);
	ClassDB::bind_method(D_METHOD("bake_hlod_chunk","cx","cz"),&CellTerrainManager::bake_hlod_chunk);
	ClassDB::bind_method(D_METHOD("clear_hlod"),&CellTerrainManager::clear_hlod);
	ClassDB::bind_method(D_METHOD("rebake_dirty_hlod"),&CellTerrainManager::rebake_dirty_hlod);
	ClassDB::bind_method(D_METHOD("bake_ground_texture"),&CellTerrainManager::bake_ground_texture);
	ClassDB::bind_method(D_METHOD("clear_ground_texture"),&CellTerrainManager::clear_ground_texture);
	ClassDB::bind_method(D_METHOD("has_ground_texture"),&CellTerrainManager::has_ground_texture);
	ClassDB::bind_method(D_METHOD("get_ground_albedo_texture"),&CellTerrainManager::get_ground_albedo_texture);
	ClassDB::bind_method(D_METHOD("get_ground_height_texture"),&CellTerrainManager::get_ground_height_texture);
	ClassDB::bind_method(D_METHOD("get_ground_bounds"),&CellTerrainManager::get_ground_bounds);
	ClassDB::bind_method(D_METHOD("set_ground_texels_per_unit","v"),&CellTerrainManager::set_ground_texels_per_unit);
	ClassDB::bind_method(D_METHOD("get_ground_texels_per_unit"),&CellTerrainManager::get_ground_texels_per_unit);
	ClassDB::bind_method(D_METHOD("bake_navmesh"),&CellTerrainManager::bake_navmesh);
	ClassDB::bind_method(D_METHOD("clear_baked_navmesh"),&CellTerrainManager::clear_baked_navmesh);
	ClassDB::bind_method(D_METHOD("has_baked_navmesh"),&CellTerrainManager::has_baked_navmesh);
	ClassDB::bind_method(D_METHOD("set_bake_navmesh_lod","l"),&CellTerrainManager::set_bake_navmesh_lod);
	ClassDB::bind_method(D_METHOD("get_bake_navmesh_lod"),&CellTerrainManager::get_bake_navmesh_lod);

	ClassDB::bind_method(D_METHOD("set_target_path","p"),&CellTerrainManager::set_target_path);
	ClassDB::bind_method(D_METHOD("get_target_path"),&CellTerrainManager::get_target_path);
	ClassDB::bind_method(D_METHOD("set_world_dir","p"),&CellTerrainManager::set_world_dir);
	ClassDB::bind_method(D_METHOD("get_world_dir"),&CellTerrainManager::get_world_dir);
	ClassDB::bind_method(D_METHOD("set_verts_per_side","v"),&CellTerrainManager::set_verts_per_side);
	ClassDB::bind_method(D_METHOD("get_verts_per_side"),&CellTerrainManager::get_verts_per_side);
	ClassDB::bind_method(D_METHOD("set_cell_size","s"),&CellTerrainManager::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"),&CellTerrainManager::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_height_scale","s"),&CellTerrainManager::set_height_scale);
	ClassDB::bind_method(D_METHOD("get_height_scale"),&CellTerrainManager::get_height_scale);
	ClassDB::bind_method(D_METHOD("set_load_radius","r"),&CellTerrainManager::set_load_radius);
	ClassDB::bind_method(D_METHOD("get_load_radius"),&CellTerrainManager::get_load_radius);
	ClassDB::bind_method(D_METHOD("set_lod_radius","r"),&CellTerrainManager::set_lod_radius);
	ClassDB::bind_method(D_METHOD("get_lod_radius"),&CellTerrainManager::get_lod_radius);
	ClassDB::bind_method(D_METHOD("set_max_lod_level","l"),&CellTerrainManager::set_max_lod_level);
	ClassDB::bind_method(D_METHOD("get_max_lod_level"),&CellTerrainManager::get_max_lod_level);
	ClassDB::bind_method(D_METHOD("set_hlod_chunk_size","s"),&CellTerrainManager::set_hlod_chunk_size);
	ClassDB::bind_method(D_METHOD("get_hlod_chunk_size"),&CellTerrainManager::get_hlod_chunk_size);
	ClassDB::bind_method(D_METHOD("set_hlod_radius","r"),&CellTerrainManager::set_hlod_radius);
	ClassDB::bind_method(D_METHOD("get_hlod_radius"),&CellTerrainManager::get_hlod_radius);
	ClassDB::bind_method(D_METHOD("set_hlod_height_offset","o"),&CellTerrainManager::set_hlod_height_offset);
	ClassDB::bind_method(D_METHOD("get_hlod_height_offset"),&CellTerrainManager::get_hlod_height_offset);
	ClassDB::bind_method(D_METHOD("set_cull_lod_behind_camera","e"),&CellTerrainManager::set_cull_lod_behind_camera);
	ClassDB::bind_method(D_METHOD("get_cull_lod_behind_camera"),&CellTerrainManager::get_cull_lod_behind_camera);
	ClassDB::bind_method(D_METHOD("set_frustum_cull_dot","d"),&CellTerrainManager::set_frustum_cull_dot);
	ClassDB::bind_method(D_METHOD("get_frustum_cull_dot"),&CellTerrainManager::get_frustum_cull_dot);
	ClassDB::bind_method(D_METHOD("set_preload_margin","m"),&CellTerrainManager::set_preload_margin);
	ClassDB::bind_method(D_METHOD("get_preload_margin"),&CellTerrainManager::get_preload_margin);
	ClassDB::bind_method(D_METHOD("set_streaming_lookahead_seconds","s"),&CellTerrainManager::set_streaming_lookahead_seconds);
	ClassDB::bind_method(D_METHOD("get_streaming_lookahead_seconds"),&CellTerrainManager::get_streaming_lookahead_seconds);
	ClassDB::bind_method(D_METHOD("set_preload_collision_upgrade","e"),&CellTerrainManager::set_preload_collision_upgrade);
	ClassDB::bind_method(D_METHOD("get_preload_collision_upgrade"),&CellTerrainManager::get_preload_collision_upgrade);
	ClassDB::bind_method(D_METHOD("set_integrate_budget_ms","ms"),&CellTerrainManager::set_integrate_budget_ms);
	ClassDB::bind_method(D_METHOD("get_integrate_budget_ms"),&CellTerrainManager::get_integrate_budget_ms);
	ClassDB::bind_method(D_METHOD("set_streaming_threads","n"),&CellTerrainManager::set_streaming_threads);
	ClassDB::bind_method(D_METHOD("get_streaming_threads"),&CellTerrainManager::get_streaming_threads);
	ClassDB::bind_method(D_METHOD("get_active_thread_count"),&CellTerrainManager::get_active_thread_count);
	ClassDB::bind_method(D_METHOD("set_texture_scale","s"),&CellTerrainManager::set_texture_scale);
	ClassDB::bind_method(D_METHOD("get_texture_scale"),&CellTerrainManager::get_texture_scale);
	ClassDB::bind_method(D_METHOD("get_num_layers"),&CellTerrainManager::get_num_layers);
	ClassDB::bind_method(D_METHOD("set_layers","layers"),&CellTerrainManager::set_layers);
	ClassDB::bind_method(D_METHOD("get_layers"),&CellTerrainManager::get_layers);
	ClassDB::bind_method(D_METHOD("_on_layer_changed"),&CellTerrainManager::_on_layer_changed);
	ClassDB::bind_method(D_METHOD("get_cell","cx","cz"),&CellTerrainManager::get_cell);
	ClassDB::bind_method(D_METHOD("get_cell_at_world", "world_pos"), &CellTerrainManager::get_cell_at_world);
	ClassDB::bind_method(D_METHOD("get_height_at_world","wx","wz"),&CellTerrainManager::get_height_at_world);
	ClassDB::bind_method(D_METHOD("raycast_terrain","from","dir"),&CellTerrainManager::raycast_terrain_v);
	ClassDB::bind_method(D_METHOD("set_cell_nav_obstacles","cx","cz","obstacles"),&CellTerrainManager::set_cell_nav_obstacles);
	ClassDB::bind_method(D_METHOD("get_pending_count"),&CellTerrainManager::get_pending_count);
	ClassDB::bind_method(D_METHOD("get_dominant_layer_index_at_world","wx","wz"),&CellTerrainManager::get_dominant_layer_index_at_world);
	ClassDB::bind_method(D_METHOD("get_layer_id_at_world","wx","wz"),&CellTerrainManager::get_layer_id_at_world);
	ClassDB::bind_method(D_METHOD("get_layer_at_world","wx","wz"),&CellTerrainManager::get_layer_at_world);
	ClassDB::bind_method(D_METHOD("set_brush_texture","t"),&CellTerrainManager::set_brush_texture);
	ClassDB::bind_method(D_METHOD("get_brush_texture"),&CellTerrainManager::get_brush_texture);
	ClassDB::bind_method(D_METHOD("set_preview_in_editor","p"),&CellTerrainManager::set_preview_in_editor);
	ClassDB::bind_method(D_METHOD("get_preview_in_editor"),&CellTerrainManager::get_preview_in_editor);
	ClassDB::bind_method(D_METHOD("set_show_all_in_editor","p"),&CellTerrainManager::set_show_all_in_editor);
	ClassDB::bind_method(D_METHOD("get_show_all_in_editor"),&CellTerrainManager::get_show_all_in_editor);

	ClassDB::bind_method(D_METHOD("set_height_blend_sharpness","s"),&CellTerrainManager::set_height_blend_sharpness); ClassDB::bind_method(D_METHOD("get_height_blend_sharpness"),&CellTerrainManager::get_height_blend_sharpness);
	ClassDB::bind_method(D_METHOD("set_detail_normal_strength","s"),&CellTerrainManager::set_detail_normal_strength); ClassDB::bind_method(D_METHOD("get_detail_normal_strength"),&CellTerrainManager::get_detail_normal_strength);
	ClassDB::bind_method(D_METHOD("set_macro_variation_color1","c"),&CellTerrainManager::set_macro_variation_color1); ClassDB::bind_method(D_METHOD("get_macro_variation_color1"),&CellTerrainManager::get_macro_variation_color1);
	ClassDB::bind_method(D_METHOD("set_macro_variation_color2","c"),&CellTerrainManager::set_macro_variation_color2); ClassDB::bind_method(D_METHOD("get_macro_variation_color2"),&CellTerrainManager::get_macro_variation_color2);
	ClassDB::bind_method(D_METHOD("set_macro_variation_scale","s"),&CellTerrainManager::set_macro_variation_scale); ClassDB::bind_method(D_METHOD("get_macro_variation_scale"),&CellTerrainManager::get_macro_variation_scale);
	ClassDB::bind_method(D_METHOD("set_enable_navmesh","e"),&CellTerrainManager::set_enable_navmesh); ClassDB::bind_method(D_METHOD("get_enable_navmesh"),&CellTerrainManager::get_enable_navmesh);
	ClassDB::bind_method(D_METHOD("set_nav_max_slope_deg","d"),&CellTerrainManager::set_nav_max_slope_deg); ClassDB::bind_method(D_METHOD("get_nav_max_slope_deg"),&CellTerrainManager::get_nav_max_slope_deg);
	ClassDB::bind_method(D_METHOD("set_debug_view","mode"),&CellTerrainManager::set_debug_view); ClassDB::bind_method(D_METHOD("get_debug_view"),&CellTerrainManager::get_debug_view);
	ClassDB::bind_method(D_METHOD("set_parallax_max_distance","d"),&CellTerrainManager::set_parallax_max_distance); ClassDB::bind_method(D_METHOD("get_parallax_max_distance"),&CellTerrainManager::get_parallax_max_distance);
	ClassDB::bind_method(D_METHOD("set_nav_downsample","d"),&CellTerrainManager::set_nav_downsample); ClassDB::bind_method(D_METHOD("get_nav_downsample"),&CellTerrainManager::get_nav_downsample);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH,"target_path"),"set_target_path","get_target_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING,"world_dir"),"set_world_dir","get_world_dir");
	ADD_PROPERTY(PropertyInfo(Variant::INT,"verts_per_side"),"set_verts_per_side","get_verts_per_side");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"cell_size"),"set_cell_size","get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"height_scale"),"set_height_scale","get_height_scale");
	ADD_PROPERTY(PropertyInfo(Variant::INT,"load_radius"),"set_load_radius","get_load_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT,"lod_radius"),"set_lod_radius","get_lod_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT,"max_lod_level",PROPERTY_HINT_RANGE,"1,8,1"),"set_max_lod_level","get_max_lod_level");
	ADD_PROPERTY(PropertyInfo(Variant::INT,"hlod_chunk_size"),"set_hlod_chunk_size","get_hlod_chunk_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT,"hlod_radius"),"set_hlod_radius","get_hlod_radius");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"hlod_height_offset"),"set_hlod_height_offset","get_hlod_height_offset");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL,"cull_lod_behind_camera"),"set_cull_lod_behind_camera","get_cull_lod_behind_camera");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"frustum_cull_dot",PROPERTY_HINT_RANGE,"-1.0,1.0,0.05"),"set_frustum_cull_dot","get_frustum_cull_dot");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"preload_margin",PROPERTY_HINT_RANGE,"0.0,0.49,0.01"),"set_preload_margin","get_preload_margin");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"streaming_lookahead_seconds",PROPERTY_HINT_RANGE,"0.0,3.0,0.05"),"set_streaming_lookahead_seconds","get_streaming_lookahead_seconds");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL,"preload_collision_upgrade"),"set_preload_collision_upgrade","get_preload_collision_upgrade");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"integrate_budget_ms",PROPERTY_HINT_RANGE,"0.1,16.0,0.1"),"set_integrate_budget_ms","get_integrate_budget_ms");
	ADD_PROPERTY(PropertyInfo(Variant::INT,"streaming_threads",PROPERTY_HINT_RANGE,"0,16,1"),"set_streaming_threads","get_streaming_threads");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL,"preload_collision_on_start"),"set_preload_collision_on_start","get_preload_collision_on_start");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"texture_scale"),"set_texture_scale","get_texture_scale");

	ADD_PROPERTY(PropertyInfo(Variant::BOOL,"preview_in_editor"),"set_preview_in_editor","get_preview_in_editor");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL,"show_all_in_editor"),"set_show_all_in_editor","get_show_all_in_editor");

	ADD_GROUP("Brush","brush_");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT,"brush_texture",PROPERTY_HINT_RESOURCE_TYPE,"Texture"),"set_brush_texture","get_brush_texture");

	ADD_GROUP("PBR Blending","");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"height_blend_sharpness",PROPERTY_HINT_RANGE,"0.01,1.0,0.01"),"set_height_blend_sharpness","get_height_blend_sharpness");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"detail_normal_strength",PROPERTY_HINT_RANGE,"0.0,4.0,0.05"),"set_detail_normal_strength","get_detail_normal_strength");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL,"enable_navmesh"),"set_enable_navmesh","get_enable_navmesh");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"nav_max_slope_deg",PROPERTY_HINT_RANGE,"0.0,89.0,0.5"),"set_nav_max_slope_deg","get_nav_max_slope_deg");
	ADD_PROPERTY(PropertyInfo(Variant::INT,"nav_downsample",PROPERTY_HINT_RANGE,"1,16,1"),"set_nav_downsample","get_nav_downsample");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"ground_texels_per_unit",PROPERTY_HINT_RANGE,"0.05,4.0,0.05"),"set_ground_texels_per_unit","get_ground_texels_per_unit");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"parallax_max_distance",PROPERTY_HINT_RANGE,"0.0,400.0,1.0"),"set_parallax_max_distance","get_parallax_max_distance");
	ADD_PROPERTY(PropertyInfo(Variant::INT,"debug_view",PROPERTY_HINT_ENUM,"Off,Splatmap Layers,Detail Normal,Nav Mask"),"set_debug_view","get_debug_view");
	ADD_PROPERTY(PropertyInfo(Variant::INT,"bake_navmesh_lod",PROPERTY_HINT_RANGE,"0,3,1"),"set_bake_navmesh_lod","get_bake_navmesh_lod");

	ADD_GROUP("Macro Variation","macro_variation_");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR,"macro_variation_color1"),"set_macro_variation_color1","get_macro_variation_color1");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR,"macro_variation_color2"),"set_macro_variation_color2","get_macro_variation_color2");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"macro_variation_scale",PROPERTY_HINT_RANGE,"0.0,1.0,0.001"),"set_macro_variation_scale","get_macro_variation_scale");

	ADD_GROUP("Terrain Layers","");
	// Each element is a CellTerrainLayer resource — name/id/textures/per-layer
	// shading knobs — instead of numbered texture slots (see cell_terrain_layer.h).
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY,"layers",PROPERTY_HINT_TYPE_STRING,
						 itos(Variant::OBJECT) + "/" + itos(PROPERTY_HINT_RESOURCE_TYPE) + ":CellTerrainLayer"),
			"set_layers","get_layers");
}
