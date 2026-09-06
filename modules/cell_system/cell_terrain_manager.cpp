#include "cell_terrain_manager.h"

#include "cell_terrain.h"
#include "cell_terrain_layer.h"
#include "terrain_shader.h"
#include "core/engine.h"
#include "core/os/dir_access.h"
#include "core/os/file_access.h"
#include "core/os/os.h"
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
	for (int s = 0; s < 4; s++) {
		Ref<Image> splat = t->get_splatmap(s);
		if (splat.is_valid()) { String sp = world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".splat" + itos(s) + ".png"); splat->save_png(sp); }
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
	for (int s = 0; s < 4; s++) t->set_splatmap(s, _load_splatmap(cx, cz, s));
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
	int d = dist - load_radius;
	int range = lod_radius - load_radius;
	if (range <= 0) return 0;
	int lod = 1 + (d * 2) / range; // spread LOD 1-3 across the range
	return CLAMP(lod, 1, 3);
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
	_load_hlod_chunks(cx, cz);

	_last_cx = cx; _last_cz = cz; _has_last = true;
}

// --- HLOD chunk management ---
void CellTerrainManager::_load_hlod_chunks(int center_cx, int center_cz) {
	int cs = hlod_chunk_size;
	if (cs <= 0) return;

	// Load every baked HLOD chunk ONCE and keep them resident forever.
	if (!_hlod_loaded) {
		_hlod_loaded = true;
		DirAccess *da = DirAccess::create_for_path(world_dir);
		if (da && da->change_dir(world_dir) == OK) {
			da->list_dir_begin();
			String fname = da->get_next();
			int loaded = 0;
			while (fname != "") {
				if (fname.begins_with("hlod_") && fname.ends_with(".mesh")) {
					String base = fname.get_basename();
					Vector<String> parts = base.split("_");
					if (parts.size() >= 3 && parts[0] == "hlod") {
						int hx = parts[1].to_int();
						int hz = parts[2].to_int();
						int64_t k = _key(hx, hz);
						if (!_hlod_active.has(k)) {
							MeshInstance *mi = _load_hlod_chunk(hx, hz);
							if (mi) {
								_hlod_active[k] = mi;
								loaded++;
								// _hlod_dirty is in-memory only and resets on every
								// editor restart, so a chunk that was mid-flight for
								// an auto-rebake at the moment of a restart silently
								// loses that flag forever even though the bake on
								// disk is now stale. Self-heal from the filesystem
								// instead of trusting runtime state alone: if any
								// constituent cell's height data is newer than this
								// chunk's bake, it's stale regardless of _hlod_dirty.
								uint64_t bake_time = FileAccess::get_modified_time(world_dir.plus_file(fname));
								bool stale = false;
								for (int cz = hz * cs; cz < (hz + 1) * cs && !stale; cz++) {
									for (int cx = hx * cs; cx < (hx + 1) * cs && !stale; cx++) {
										String hpath = world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".heights");
										if (FileAccess::exists(hpath) && FileAccess::get_modified_time(hpath) > bake_time) stale = true;
									}
								}
								if (stale) _hlod_dirty.insert(k);
							}
						}
					}
				}
				fname = da->get_next();
			}
			da->list_dir_end();
			print_line("HLOD: loaded " + itos(loaded) + " persistent chunks.");
		}
		if (da) memdelete(da);

		if (!_hlod_dirty.empty()) {
			print_line("HLOD: " + itos(_hlod_dirty.size()) + " chunk(s) were stale relative to saved height data — rebaking now.");
			rebake_dirty_hlod();
		}
	}

	// Each call: hide a chunk only once EVERY cell it covers has actually
	// finished streaming in, show it otherwise. Cell loading is staggered —
	// only _max_integrate_per_frame cells integrate per frame — so hiding on
	// *any* overlap (the previous behavior) made the whole chunk (many cells)
	// disappear the instant its first cell finished, while the rest of that
	// same chunk could still be several frames (or, after a big camera jump,
	// much longer) from being ready: a real gap for that whole window, not
	// just a one-frame flicker. Requiring full coverage keeps the chunk
	// resident under the cells that HAVE loaded — hlod_height_offset already
	// pushes it below them, so the depth test hides the overlap there — and
	// only drops it once nothing underneath still needs it to fill in.
	// A chunk straddling the edge of lod_radius (partially covered forever,
	// by design) simply never hides, staying as a hidden-by-depth floor under
	// whatever of it did load. Nothing ever unloads — only visibility toggles.
	for (Map<int64_t, MeshInstance *>::Element *E = _hlod_active.front(); E; E = E->next()) {
		int hx, hz; _unkey(E->key(), hx, hz);
		bool fully_covered = true;
		for (int cz = hz * cs; cz < (hz + 1) * cs && fully_covered; cz++)
			for (int cx = hx * cs; cx < (hx + 1) * cs && fully_covered; cx++)
				if (!_active.has(_key(cx, cz))) fully_covered = false;
		E->value()->set_visible(!fully_covered);
	}
}

MeshInstance *CellTerrainManager::_load_hlod_chunk(int chunk_x, int chunk_z) {
	String mesh_path = world_dir.plus_file("hlod_" + itos(chunk_x) + "_" + itos(chunk_z) + ".mesh");
	if (!FileAccess::exists(mesh_path)) return nullptr;

	Ref<Mesh> mesh = ResourceLoader::load(mesh_path, "Mesh");
	if (mesh.is_null()) return nullptr;

	MeshInstance *mi = memnew(MeshInstance);
	mi->set_mesh(mesh);

	_ensure_material();

	// Build a fresh ShaderMaterial for this chunk and bind ALL params explicitly.
	// (Material::duplicate() does not reliably carry the TextureArray params in 3.5.)
	Ref<ShaderMaterial> mat;
	mat.instance();
	mat->set_shader(terrain_shader);
	if (albedo_texture_array.is_valid()) mat->set_shader_param("albedo_arr", albedo_texture_array);
	if (normal_texture_array.is_valid()) mat->set_shader_param("normal_arr", normal_texture_array);
	mat->set_shader_param("num_layers", num_layers);
	mat->set_shader_param("height_blend_sharpness", height_blend_sharpness);
	mat->set_shader_param("layer_params", layer_params_tex);
	mat->set_shader_param("layer_color", layer_color_tex);
	mat->set_shader_param("macro_variation_color1", macro_variation_color1);
	mat->set_shader_param("macro_variation_color2", macro_variation_color2);
	mat->set_shader_param("macro_variation_scale", macro_variation_scale);
	mat->set_shader_param("parallax_max_distance", parallax_max_distance);
	mat->set_shader_param("debug_view", debug_view);
	// HLOD chunks don't bake their own detail normal map (they're already the
	// most decimated representation) — bind the flat/neutral placeholder so the
	// shader's sampler is never left unbound.
	mat->set_shader_param("detail_normal_map", _get_neutral_normal_texture());

	// HLOD UVs span the whole chunk (0..1). Scale tiling by the number of cells
	// so the albedo tiles at the same density as the detailed cells.
	mat->set_shader_param("texture_scale", texture_scale * (float)hlod_chunk_size);

	// Baked per-chunk splatmaps.
	for (int s = 0; s < 4; s++) {
		String sp = world_dir.plus_file("hlod_" + itos(chunk_x) + "_" + itos(chunk_z) + ".splat" + itos(s) + ".png");
		Ref<ImageTexture> tex;
		FileAccess *_hfa = FileAccess::open(sp, FileAccess::READ);
		if (_hfa) {
			PoolByteArray _hlod_bytes; _hlod_bytes.resize(_hfa->get_len());
			{ PoolByteArray::Write w = _hlod_bytes.write(); _hfa->get_buffer(w.ptr(), _hlod_bytes.size()); }
			memdelete(_hfa);
			Ref<Image> img; img.instance();
			if (img->load_png_from_buffer(_hlod_bytes) == OK) {
				tex.instance();
				tex->create_from_image(img, Texture::FLAG_FILTER);
			}
		}
		if (tex.is_null()) {
			// Default: splat0 = full layer 0, others zero.
			Ref<Image> img; img.instance();
			img->create(4, 4, false, Image::FORMAT_RGBA8);
			img->fill(s == 0 ? Color(1, 0, 0, 0) : Color(0, 0, 0, 0));
			tex.instance();
			tex->create_from_image(img, Texture::FLAG_FILTER);
		}
		mat->set_shader_param("splatmap" + itos(s), tex);
	}
	mi->set_surface_material(0, mat);

	add_child(mi);
	Transform xf;
	xf.origin = Vector3(chunk_x * hlod_chunk_size * cell_size, hlod_height_offset, chunk_z * hlod_chunk_size * cell_size);
	mi->set_transform(xf);
	return mi;
}

// --- HLOD baking ---
void CellTerrainManager::bake_hlod() {
	print_line("HLOD bake: scanning directory '" + world_dir + "'");

	DirAccess *da = DirAccess::create_for_path(world_dir);
	if (!da) {
		print_line("HLOD bake ERROR: could not open DirAccess for '" + world_dir + "'");
		return;
	}
	if (da->change_dir(world_dir) != OK) {
		print_line("HLOD bake ERROR: directory does not exist: '" + world_dir + "'");
		memdelete(da);
		return;
	}
	da->make_dir_recursive(world_dir);

	// Scan for existing cells to determine world bounds.
	Set<int64_t> cell_set;
	da->list_dir_begin();
	String fname = da->get_next();
	int file_count = 0;
	while (fname != "") {
		file_count++;
		if (fname.ends_with(".heights")) {
			String base = fname.get_basename(); // cell_X_Z
			Vector<String> parts = base.split("_");
			if (parts.size() >= 3 && parts[0] == "cell") {
				int cx = parts[1].to_int();
				int cz = parts[2].to_int();
				cell_set.insert(_key(cx, cz));
			}
		}
		fname = da->get_next();
	}
	da->list_dir_end();
	memdelete(da);

	print_line("HLOD bake: found " + itos(file_count) + " files total, " + itos(cell_set.size()) + " heightmap cells.");

	if (cell_set.size() == 0) {
		print_line("HLOD bake: no .heights files found. Have you painted/sculpted and does world_dir match where cells are saved?");
		return;
	}

	// Determine which chunks need baking.
	Set<int64_t> chunks;
	for (Set<int64_t>::Element *E = cell_set.front(); E; E = E->next()) {
		int cx, cz; _unkey(E->get(), cx, cz);
		int hx = (cx >= 0) ? cx / hlod_chunk_size : (cx - hlod_chunk_size + 1) / hlod_chunk_size;
		int hz = (cz >= 0) ? cz / hlod_chunk_size : (cz - hlod_chunk_size + 1) / hlod_chunk_size;
		chunks.insert(_key(hx, hz));
	}

	for (Set<int64_t>::Element *E = chunks.front(); E; E = E->next()) {
		int hx, hz; _unkey(E->get(), hx, hz);
		bake_hlod_chunk(hx, hz);
	}
	print_line("HLOD bake complete: " + itos(chunks.size()) + " chunks.");

	// Drop and reload the resident HLOD so freshly baked chunks appear now.
	for (Map<int64_t, MeshInstance *>::Element *E = _hlod_active.front(); E; E = E->next())
		E->value()->queue_delete();
	_hlod_active.clear();
	_hlod_loaded = false;
	_hlod_dirty.clear(); // everything is freshly baked
	_load_hlod_chunks(_last_cx, _last_cz);
}

void CellTerrainManager::clear_hlod() {
	// Remove the resident chunk instances from the scene.
	for (Map<int64_t, MeshInstance *>::Element *E = _hlod_active.front(); E; E = E->next())
		E->value()->queue_delete();
	_hlod_active.clear();
	_hlod_loaded = false;
	_hlod_dirty.clear();

	// Delete every baked HLOD file (hlod_*.mesh and hlod_*.splat*.png) from disk.
	DirAccess *da = DirAccess::create_for_path(world_dir);
	if (!da || da->change_dir(world_dir) != OK) {
		if (da) memdelete(da);
		print_line("HLOD clear: no world_dir to scan ('" + world_dir + "').");
		return;
	}

	Vector<String> to_delete;
	da->list_dir_begin();
	String fname = da->get_next();
	while (fname != "") {
		if (!da->current_is_dir() && fname.begins_with("hlod_")
				&& (fname.ends_with(".mesh") || fname.ends_with(".png"))) {
			to_delete.push_back(fname);
		}
		fname = da->get_next();
	}
	da->list_dir_end();

	int removed = 0;
	for (int i = 0; i < to_delete.size(); i++) {
		if (da->remove(world_dir.plus_file(to_delete[i])) == OK) removed++;
	}
	memdelete(da);

	print_line("HLOD clear: deleted " + itos(removed) + " files.");
}

void CellTerrainManager::_release_baked_navmesh(int64_t key) {
	Map<int64_t, RID>::Element *E = _baked_nav_regions.find(key);
	if (!E) return;
	NavigationServer::get_singleton()->free(E->get());
	_baked_nav_regions.erase(key);
}

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

void CellTerrainManager::bake_hlod_chunk(int chunk_x, int chunk_z) {
	int cs = hlod_chunk_size;
	int vps = verts_per_side;
	float full_spacing = cell_size / (float)(vps - 1);

	// HLOD resolution: 2 verts per cell edge = smooth enough for distance.
	int hlod_step = (vps - 1) / 2; // step through each cell's heightmap
	if (hlod_step < 1) hlod_step = 1;
	int cell_lod_vps = (vps - 1) / hlod_step + 1; // verts per cell at HLOD
	int chunk_vps = (cell_lod_vps - 1) * cs + 1;   // total verts per chunk side

	float chunk_world_size = cs * cell_size;

	PoolVector3Array vertices; vertices.resize(chunk_vps * chunk_vps);
	PoolVector3Array normals;  normals.resize(chunk_vps * chunk_vps);
	PoolVector2Array uvs;      uvs.resize(chunk_vps * chunk_vps);
	PoolRealArray    tangents; tangents.resize(chunk_vps * chunk_vps * 4);

	{
		PoolVector3Array::Write vw = vertices.write();
		PoolVector3Array::Write nw = normals.write();
		PoolVector2Array::Write uw = uvs.write();
		PoolRealArray::Write    tw = tangents.write();

		for (int gz = 0; gz < chunk_vps; gz++) {
			for (int gx = 0; gx < chunk_vps; gx++) {
				int i = gz * chunk_vps + gx;
				// Which cell and local position does this vertex belong to?
				int cell_lx = gx / (cell_lod_vps - 1);
				int local_x = gx % (cell_lod_vps - 1);
				int cell_lz = gz / (cell_lod_vps - 1);
				int local_z = gz % (cell_lod_vps - 1);
				// Clamp to last cell.
				if (cell_lx >= cs) { cell_lx = cs - 1; local_x = cell_lod_vps - 1; }
				if (cell_lz >= cs) { cell_lz = cs - 1; local_z = cell_lod_vps - 1; }

				int cx = chunk_x * cs + cell_lx;
				int cz = chunk_z * cs + cell_lz;
				int hx = local_x * hlod_step;
				int hz = local_z * hlod_step;

				// Load height from cell.
				float height = 0.0f;
				String hpath = world_dir.plus_file("cell_" + itos(cx) + "_" + itos(cz) + ".heights");
				if (FileAccess::exists(hpath)) {
					FileAccess *f = FileAccess::open(hpath, FileAccess::READ);
					if (f) {
						int idx = CLAMP(hz, 0, vps-1) * vps + CLAMP(hx, 0, vps-1);
						f->seek(idx * sizeof(float));
						height = f->get_float() * height_scale;
						f->close(); memdelete(f);
					}
				}

				float wx = (cell_lx * cell_size) + hx * full_spacing;
				float wz = (cell_lz * cell_size) + hz * full_spacing;
				vw[i] = Vector3(wx, height, wz);
				nw[i] = Vector3(0, 1, 0); // approximate; flat normal for HLOD
				uw[i] = Vector2(wx / chunk_world_size, wz / chunk_world_size);
				// Flat normal -> constant tangent. Without tangents the shader's
				// NORMALMAP has no TBN basis and the chunk lights up black.
				// Matches the detailed cell convention: T=(1,0,0), handedness=-1.
				tw[i*4+0] = 1.0f; tw[i*4+1] = 0.0f; tw[i*4+2] = 0.0f; tw[i*4+3] = -1.0f;
			}
		}
	}

	// Indices.
	int quad_count = (chunk_vps - 1) * (chunk_vps - 1);
	PoolIntArray indices; indices.resize(quad_count * 6);
	{
		PoolIntArray::Write iw = indices.write();
		int t = 0;
		for (int gz = 0; gz < chunk_vps - 1; gz++) {
			for (int gx = 0; gx < chunk_vps - 1; gx++) {
				int i0 = gz * chunk_vps + gx, i1 = i0 + 1;
				int i2 = i0 + chunk_vps, i3 = i2 + 1;
				iw[t++] = i0; iw[t++] = i1; iw[t++] = i2;
				iw[t++] = i1; iw[t++] = i3; iw[t++] = i2;
			}
		}
	}

	Array arrays; arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX]  = vertices;
	arrays[Mesh::ARRAY_NORMAL]  = normals;
	arrays[Mesh::ARRAY_TANGENT] = tangents;
	arrays[Mesh::ARRAY_TEX_UV]  = uvs;
	arrays[Mesh::ARRAY_INDEX]   = indices;

	Ref<ArrayMesh> mesh; mesh.instance();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

	String mesh_path = world_dir.plus_file("hlod_" + itos(chunk_x) + "_" + itos(chunk_z) + ".mesh");
	ResourceSaver::save(mesh_path, mesh);

	// Bake combined splatmaps: composite the cell splatmaps into one per chunk.
	int splat_res = cs * 16; // 16 px per cell in the baked splatmap
	for (int s = 0; s < 4; s++) {
		Ref<Image> combined; combined.instance();
		combined->create(splat_res, splat_res, false, Image::FORMAT_RGBA8);
		combined->fill(s == 0 ? Color(1, 0, 0, 0) : Color(0, 0, 0, 0));
		combined->lock();

		for (int cell_z = 0; cell_z < cs; cell_z++) {
			for (int cell_x = 0; cell_x < cs; cell_x++) {
				int cx = chunk_x * cs + cell_x;
				int cz = chunk_z * cs + cell_z;
				Ref<Image> src = _load_splatmap(cx, cz, s);
				if (src.is_null()) continue;
				src->lock();
				int px_per_cell = splat_res / cs;
				for (int py = 0; py < px_per_cell; py++) {
					for (int px = 0; px < px_per_cell; px++) {
						float u = (float)px / px_per_cell;
						float v = (float)py / px_per_cell;
						int sx = CLAMP((int)(u * src->get_width()), 0, src->get_width() - 1);
						int sy = CLAMP((int)(v * src->get_height()), 0, src->get_height() - 1);
						Color c = src->get_pixel(sx, sy);
						combined->set_pixel(cell_x * px_per_cell + px, cell_z * px_per_cell + py, c);
					}
				}
				src->unlock();
			}
		}
		combined->unlock();
		String sp = world_dir.plus_file("hlod_" + itos(chunk_x) + "_" + itos(chunk_z) + ".splat" + itos(s) + ".png");
		combined->save_png(sp);
	}
}

// ---- async loading helpers ----

void CellTerrainManager::_load_worker(void *p_self) {
	CellTerrainManager *self = (CellTerrainManager *)p_self;
	while (true) {
		self->_load_sem.wait();
		if (self->_thread_exit) break;

		self->_load_mutex.lock();
		PendingLoad *job = nullptr;
		if (!self->_load_queue.empty()) {
			job = self->_load_queue.front()->get();
			self->_load_queue.pop_front();
		}
		self->_load_mutex.unlock();

		if (!job) continue;

		CellTerrain::ComputeInput in;

		if (job->is_recompute) {
			// Already-active cell re-stitching after a neighbor/LOD change —
			// the main thread already filled these in from the live cell's
			// current state (see _submit_recompute), so there's no disk I/O
			// to do here at all.
			in.edge_n = job->edge_n; in.edge_s = job->edge_s; in.edge_e = job->edge_e; in.edge_w = job->edge_w;
		} else {
			// File I/O and PNG decode: no Godot scene-tree involvement, safe off-thread.
			// _load_heights/_load_splatmap only read world_dir and verts_per_side, which
			// never change during gameplay.
			job->heights = self->_load_heights(job->cx, job->cz);
			job->nav_mask = self->_load_nav_mask(job->cx, job->cz);
			for (int s = 0; s < 4; s++) {
				job->splatmaps[s] = self->_load_splatmap(job->cx, job->cz, s);
			}
			// Neighbor edges for stitching come from their saved .heights files
			// (not live nodes, which aren't safe to touch off the main thread) —
			// a best-effort match for the common case, corrected reactively via
			// a _submit_recompute of this cell if a neighbor's live state has
			// since drifted (see the neighbor loop in _integrate_ready()).
			self->_load_neighbor_edges_from_disk(job->cx, job->cz, in.edge_n, in.edge_s, in.edge_e, in.edge_w);
		}

		// Precompute mesh/navmesh/detail-normal-map arrays here too — pure CPU
		// math (see CellTerrain::compute()), no VisualServer/NavigationServer
		// touch, so it's just as safe off-thread as the I/O above.
		in.heights = job->heights;
		in.nav_mask = job->nav_mask;
		in.verts_per_side = self->verts_per_side;
		in.cell_size = self->cell_size;
		in.height_scale = self->height_scale;
		in.lod_level = job->lod;
		in.nav_max_slope_deg = self->nav_max_slope_deg;
		in.nav_downsample = self->nav_downsample;
		job->computed = CellTerrain::compute(in);

		self->_load_mutex.lock();
		self->_ready_queue.push_back(job);
		self->_load_mutex.unlock();
	}
}

void CellTerrainManager::_submit_load(int cx, int cz, int lod, bool collision) {
	_loading.insert(_key(cx, cz));
	PendingLoad *job = memnew(PendingLoad);
	job->cx = cx; job->cz = cz; job->lod = lod; job->collision = collision;
	_load_mutex.lock();
	_load_queue.push_back(job);
	_load_mutex.unlock();
	_load_sem.post();
}

void CellTerrainManager::_submit_recompute(int cx, int cz, bool p_update_collision) {
	int64_t k = _key(cx, cz);
	if (!_active.has(k) || _recomputing.has(k)) return;
	CellTerrain *cell = _active[k];

	PendingLoad *job = memnew(PendingLoad);
	job->cx = cx; job->cz = cz;
	job->lod = cell->get_lod_level();
	job->collision = cell->get_enable_collision();
	job->is_recompute = true;
	job->update_collision = p_update_collision;
	job->target = cell;

	// Cheap (COW array copies), main thread only — no disk I/O, unlike a
	// fresh _submit_load. The worker just runs CellTerrain::compute() on this
	// exact snapshot instead of re-deriving it from disk.
	CellTerrain::ComputeInput in = cell->get_compute_input();
	job->heights = in.heights;
	job->nav_mask = in.nav_mask;
	job->edge_n = in.edge_n; job->edge_s = in.edge_s; job->edge_e = in.edge_e; job->edge_w = in.edge_w;
	for (int s = 0; s < 4; s++) job->splatmaps[s] = cell->get_splatmap(s);

	_recomputing.insert(k);
	_load_mutex.lock();
	_load_queue.push_back(job);
	_load_mutex.unlock();
	_load_sem.post();
}

CellTerrain *CellTerrainManager::_create_cell_from_data(int cx, int cz, int lod, bool collision,
		const PoolRealArray &heights, const PoolRealArray &nav_mask, Ref<Image> *p_splatmaps) {
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
	t->set_heights(heights);
	t->set_nav_mask(nav_mask);
	for (int s = 0; s < 4; s++) t->set_splatmap(s, p_splatmaps[s]);
	t->set_material(terrain_material);
	t->set_shader_resources(albedo_texture_array, normal_texture_array,
			num_layers, texture_scale, height_blend_sharpness, layer_params_tex, layer_color_tex, detail_normal_strength,
				macro_variation_color1, macro_variation_color2, macro_variation_scale);
	if (_debug_normals) t->set_debug_normals(true, _debug_normal_length);
	if (_debug_navmesh) t->set_debug_navmesh(true);
	// The caller (_integrate_ready) already has this cell's mesh/nav/normal-map
	// precomputed and applies it explicitly right after add_child() below —
	// suppress the implicit rebuild() add_child() would otherwise trigger via
	// NOTIFICATION_READY so that work doesn't happen twice.
	t->set_skip_ready_rebuild(true);
	add_child(t);
	Transform xf;
	xf.origin = Vector3(cx * cell_size, 0.0f, cz * cell_size);
	t->set_transform(xf);
	return t;
}

void CellTerrainManager::_integrate_ready() {
	static const int dx[4] = { 1, -1, 0, 0 };
	static const int dz[4] = { 0, 0, 1, -1 };

	uint64_t start_usec = OS::get_singleton()->get_ticks_usec();
	uint64_t budget_usec = (uint64_t)(integrate_budget_ms * 1000.0f);

	for (int i = 0; i < _max_integrate_admission; i++) {
		// Time-budgeted, not count-budgeted: a job's real cost varies by ~2
		// orders of magnitude (a pure edge-restitch recompute is sub-ms; a
		// brand-new collision-bearing cell is tens of ms), so a fixed count
		// cap can't stop two expensive ones landing in the same frame. Always
		// let at least one job through so the queue can't stall outright if a
		// single job legitimately costs more than the budget.
		if (i > 0 && (OS::get_singleton()->get_ticks_usec() - start_usec) >= budget_usec) break;

		_load_mutex.lock();
		PendingLoad *job = nullptr;
		if (!_ready_queue.empty()) {
			job = _ready_queue.front()->get();
			_ready_queue.pop_front();
		}
		_load_mutex.unlock();
		if (!job) break;

		int64_t k = _key(job->cx, job->cz);

		if (job->is_recompute) {
			// Just re-applies already-precomputed data to a cell that was (and
			// still is) active — no new-cell setup, no add_child, nothing else.
			// Guard against the (removed then re-created between submission and
			// now) case by checking it's still the SAME instance, not just the
			// same map key.
			_recomputing.erase(k);
			if (_active.has(k) && _active[k] == job->target) _active[k]->apply_computed(job->computed, job->update_collision);
			memdelete(job);
			continue;
		}

		_loading.erase(k);

		// Discard if the player has since moved out of range or the cell loaded another way.
		bool still_needed = _has_last
				&& !_active.has(k)
				&& ABS(job->cx - _last_cx) <= lod_radius
				&& ABS(job->cz - _last_cz) <= lod_radius;

		if (still_needed) {
			CellTerrain *t = _create_cell_from_data(
					job->cx, job->cz, job->lod, job->collision,
					job->heights, job->nav_mask, job->splatmaps);
			_active[k] = t;
			_release_baked_navmesh(k);

			// Record this cell's own edges from its now-active neighbors (for
			// any FUTURE rebuild, e.g. live sculpting) — the mesh/nav/normal-
			// map already applied below were precomputed off-thread using
			// those same neighbors' saved height files, which match in the
			// common case (see _load_worker).
			_update_neighbor_edges(job->cx, job->cz);
			t->apply_computed(job->computed);
			t->set_skip_ready_rebuild(false);
			// This cell (and the mesh/collision/debug-view children apply_computed()
			// just created) is being positioned/populated for the very first time —
			// without this, physics interpolation (which the game relies on and
			// stays ON everywhere, including here) blends from whatever stale/
			// zeroed transform history a brand-new node starts with, causing a
			// visible flicker on the frame it appears. This snaps that history to
			// the current transform instead of turning interpolation off.
			t->reset_physics_interpolation();
			for (int d = 0; d < 4; d++) {
				int64_t nk = _key(job->cx + dx[d], job->cz + dz[d]);
				if (_active.has(nk)) {
					_update_neighbor_edges(job->cx + dx[d], job->cz + dz[d]);
					// Off the main thread (see _submit_recompute) — a direct
					// ->rebuild() here would re-block it on every single cell
					// integration, exactly the freeze this async loader exists
					// to avoid. update_collision=false: only this neighbor's
					// EDGE data changed (for mesh/navmesh stitching) — its own
					// heights and enable_collision are untouched, so rebuilding
					// its HeightMapShape here would be pure waste (and by far
					// the most expensive part, measured when this was diagnosed).
					_submit_recompute(job->cx + dx[d], job->cz + dz[d], false);
				}
			}
		}
		memdelete(job);
	}
}

void CellTerrainManager::_preload_if_needed(int x, int z, int lod, bool collision) {
	int64_t k = _key(x, z);
	if (_active.has(k) || _loading.has(k) || !_thread_running) return;
	_submit_load(x, z, lod, collision);
}

void CellTerrainManager::_preload_border(int cx, int cz, float fx, float fz, float margin) {
	int dx = 0, dz = 0;
	if (fx < margin) dx = -1; else if (fx > 1.0f - margin) dx = 1;
	if (fz < margin) dz = -1; else if (fz > 1.0f - margin) dz = 1;
	if (dx == 0 && dz == 0) return;

	// The row/column of cells that would newly enter the streaming radius
	// after crossing in this direction — same shape _evaluate()'s own sweep
	// would produce, just one step ahead of actually needing it, so the
	// (measured-expensive) first-time collision-shape build happens during
	// the frames spent approaching instead of bursting the whole new
	// row/column at once exactly when the reference cell changes.
	if (dx != 0) {
		int x = cx + dx * (lod_radius + 1);
		for (int z = cz - lod_radius; z <= cz + lod_radius; z++) {
			int dist = MAX(ABS(x - cx), ABS(z - cz));
			_preload_if_needed(x, z, _lod_for_distance(dist), dist <= load_radius);
		}
	}
	if (dz != 0) {
		int z = cz + dz * (lod_radius + 1);
		for (int x = cx - lod_radius; x <= cx + lod_radius; x++) {
			int dist = MAX(ABS(x - cx), ABS(z - cz));
			_preload_if_needed(x, z, _lod_for_distance(dist), dist <= load_radius);
		}
	}
	if (dx != 0 && dz != 0) {
		int x = cx + dx * (lod_radius + 1);
		int z = cz + dz * (lod_radius + 1);
		int dist = lod_radius + 1;
		_preload_if_needed(x, z, _lod_for_distance(dist), dist <= load_radius);
	}
}

void CellTerrainManager::_preload_collision_upgrade(int cx, int cz, float fx, float fz, float margin) {
	int dx = 0, dz = 0;
	if (fx < margin) dx = -1; else if (fx > 1.0f - margin) dx = 1;
	if (fz < margin) dz = -1; else if (fz > 1.0f - margin) dz = 1;
	if (dx == 0 && dz == 0) return;

	// The lod-tier cell that would newly become a collision cell once the
	// player actually crosses in this direction — same "dist <= load_radius"
	// test _evaluate() makes, one step ahead. Only touches cells that are
	// already active render-only cells; a not-yet-active cell is handled by
	// _preload_border above (loaded with the right collision flag from the
	// start once it's actually created).
	int upgrade_lod = _lod_for_distance(load_radius);
	auto try_upgrade = [&](int x, int z) {
		int64_t k = _key(x, z);
		if (!_active.has(k) || _recomputing.has(k)) return;
		CellTerrain *cell = _active[k];
		if (cell->get_enable_collision()) return;
		cell->set_lod_level(upgrade_lod);
		cell->set_enable_collision(true);
		cell->set_enable_navmesh(enable_navmesh);
		_submit_recompute(x, z, true);
	};
	if (dx != 0) {
		int x = cx + dx * (load_radius + 1);
		for (int z = cz - load_radius; z <= cz + load_radius; z++) try_upgrade(x, z);
	}
	if (dz != 0) {
		int z = cz + dz * (load_radius + 1);
		for (int x = cx - load_radius; x <= cx + load_radius; x++) try_upgrade(x, z);
	}
	if (dx != 0 && dz != 0) {
		try_upgrade(cx + dx * (load_radius + 1), cz + dz * (load_radius + 1));
	}
}

float CellTerrainManager::_estimate_speed(const Vector3 &world_pos) {
	uint64_t now_usec = OS::get_singleton()->get_ticks_usec();
	if (_has_last_stream_pos) {
		float dt = (now_usec - _last_stream_time_usec) / 1000000.0f;
		// Only NORMAL frame times produce a usable sample. A hitch frame is
		// exactly the case that must be thrown away, not measured: during a
		// 300-500ms stall the target legitimately covers a lot of ground, so
		// distance/dt reads as a huge speed, which widens the preload margin,
		// which promotes more cells to collision at once, which causes the
		// next stall — a feedback loop that makes spikes grow instead of
		// shrink. Out-of-range dt re-anchors the tracker and leaves the
		// smoothed speed alone rather than poisoning it.
		if (dt > 0.002f && dt < 0.1f) {
			float sample = MIN(world_pos.distance_to(_last_stream_pos) / dt, 40.0f);
			// Exponential smoothing so no single frame can swing the margin.
			_smoothed_speed = _has_smoothed_speed ? (_smoothed_speed * 0.8f + sample * 0.2f) : sample;
			_has_smoothed_speed = true;
		}
	}
	_last_stream_pos = world_pos;
	_last_stream_time_usec = now_usec;
	_has_last_stream_pos = true;
	return _has_smoothed_speed ? _smoothed_speed : 0.0f;
}

void CellTerrainManager::stream_to(const Vector3 &world_pos) {
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
	Ref<Image> splatmaps[4];
	int nsplat = (num_layers + 3) / 4;
	for (int i = 0; i < nsplat && i < 4; i++) {
		splatmaps[i] = _load_splatmap(cx, cz, i);
	}

	CellTerrain *cell = _create_cell_from_data(cx, cz, 0, true, heights, nav_mask, splatmaps);
	if (cell) {
		rebuild_at(cx, cz);
		_update_neighbor_edges(cx, cz);
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
			_load_thread.start(&CellTerrainManager::_load_worker, this);

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
				_thread_exit = true;
				_load_sem.post(); // wake worker so it sees the exit flag
				_load_thread.wait_to_finish();
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

void CellTerrainManager::set_height_blend_sharpness(float s){height_blend_sharpness=s; if(terrain_material.is_valid()) terrain_material->set_shader_param("height_blend_sharpness",s);} float CellTerrainManager::get_height_blend_sharpness()const{return height_blend_sharpness;}

void CellTerrainManager::set_detail_normal_strength(float s) {
	detail_normal_strength = MAX(0.0f, s);
	for (Map<int64_t, CellTerrain *>::Element *E = _active.front(); E; E = E->next()) {
		E->value()->set_shader_resources(albedo_texture_array, normal_texture_array,
				num_layers, texture_scale, height_blend_sharpness, layer_params_tex, layer_color_tex, detail_normal_strength,
				macro_variation_color1, macro_variation_color2, macro_variation_scale);
		E->value()->rebuild();
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
void CellTerrainManager::set_hlod_chunk_size(int s){hlod_chunk_size=MAX(1,s);} int CellTerrainManager::get_hlod_chunk_size()const{return hlod_chunk_size;}
void CellTerrainManager::set_hlod_radius(int r){hlod_radius=MAX(0,r);} int CellTerrainManager::get_hlod_radius()const{return hlod_radius;}
void CellTerrainManager::set_hlod_height_offset(float o){ hlod_height_offset=o; for (Map<int64_t,MeshInstance*>::Element *E=_hlod_active.front();E;E=E->next()) { Transform xf=E->value()->get_transform(); xf.origin.y=o; E->value()->set_transform(xf); } } float CellTerrainManager::get_hlod_height_offset()const{return hlod_height_offset;}
void CellTerrainManager::set_cull_lod_behind_camera(bool e) { cull_lod_behind_camera = e; }
void CellTerrainManager::set_frustum_cull_dot(float d) { frustum_cull_dot = CLAMP(d, -1.0f, 1.0f); }
void CellTerrainManager::set_preload_margin(float m) { preload_margin = CLAMP(m, 0.0f, 0.49f); }
void CellTerrainManager::set_streaming_lookahead_seconds(float s) { streaming_lookahead_seconds = MAX(0.0f, s); }
void CellTerrainManager::set_integrate_budget_ms(float ms) { integrate_budget_ms = MAX(0.0f, ms); }
void CellTerrainManager::set_texture_scale(float s){texture_scale=s;} float CellTerrainManager::get_texture_scale()const{return texture_scale;}
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
	ClassDB::bind_method(D_METHOD("ensure_cell_sync","cx","cz"),&CellTerrainManager::ensure_cell_sync);
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
	ClassDB::bind_method(D_METHOD("set_texture_scale","s"),&CellTerrainManager::set_texture_scale);
	ClassDB::bind_method(D_METHOD("get_texture_scale"),&CellTerrainManager::get_texture_scale);
	ClassDB::bind_method(D_METHOD("get_num_layers"),&CellTerrainManager::get_num_layers);
	ClassDB::bind_method(D_METHOD("set_layers","layers"),&CellTerrainManager::set_layers);
	ClassDB::bind_method(D_METHOD("get_layers"),&CellTerrainManager::get_layers);
	ClassDB::bind_method(D_METHOD("_on_layer_changed"),&CellTerrainManager::_on_layer_changed);
	ClassDB::bind_method(D_METHOD("get_height_at_world","wx","wz"),&CellTerrainManager::get_height_at_world);
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
	ADD_PROPERTY(PropertyInfo(Variant::INT,"hlod_chunk_size"),"set_hlod_chunk_size","get_hlod_chunk_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT,"hlod_radius"),"set_hlod_radius","get_hlod_radius");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"hlod_height_offset"),"set_hlod_height_offset","get_hlod_height_offset");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL,"cull_lod_behind_camera"),"set_cull_lod_behind_camera","get_cull_lod_behind_camera");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"frustum_cull_dot",PROPERTY_HINT_RANGE,"-1.0,1.0,0.05"),"set_frustum_cull_dot","get_frustum_cull_dot");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"preload_margin",PROPERTY_HINT_RANGE,"0.0,0.49,0.01"),"set_preload_margin","get_preload_margin");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"streaming_lookahead_seconds",PROPERTY_HINT_RANGE,"0.0,3.0,0.05"),"set_streaming_lookahead_seconds","get_streaming_lookahead_seconds");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL,"preload_collision_upgrade"),"set_preload_collision_upgrade","get_preload_collision_upgrade");
	ADD_PROPERTY(PropertyInfo(Variant::REAL,"integrate_budget_ms",PROPERTY_HINT_RANGE,"0.1,16.0,0.1"),"set_integrate_budget_ms","get_integrate_budget_ms");
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
