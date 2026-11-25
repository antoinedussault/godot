// CellTerrainManager - tier 3: baked HLOD chunks (load, bake, clear).
// Split out of cell_terrain_manager.cpp; same class, no behaviour change.

#include "cell_terrain_manager.h"

#include "cell_terrain.h"
#include "cell_terrain_layer.h"
#include "terrain_shader.h"
#include "core/engine.h"
#include "core/message_queue.h"
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

// --- HLOD chunk management ---
void CellTerrainManager::_load_hlod_chunks(int center_cx, int center_cz) {
	int cs = hlod_chunk_size;
	if (cs <= 0) return;

	// Load every baked HLOD chunk ONCE and keep them resident forever.
	if (!_hlod_loaded) {
		_hlod_loaded = true;
#ifdef TOOLS_ENABLED
		// A passive label cannot paint while the main thread is blocked, so
		// this uses the editor progress dialog, whose step() force-refreshes
		// the UI mid-task. Measured at ~1100ms for 638 chunks.
		// Not while the message queue is flushing: the editor refuses a progress
		// task there, and the first editor stream is a call_deferred from _ready.
		const bool cs_progress = Engine::get_singleton()->is_editor_hint() && !MessageQueue::get_singleton()->is_flushing();
		if (cs_progress) {
			EditorNode::progress_add_task("cs_hlod_load", "Loading terrain HLOD chunks...", 0);
			EditorNode::progress_task_step("cs_hlod_load", "Scanning...", 0, true);
		}
#endif
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
#ifdef TOOLS_ENABLED
				// force_refresh pumps a FULL editor redraw, so this has to be
				// rare: at every 16 chunks it fired ~40 times and slowed the
				// load it was meant to report. Every 64 keeps it ~10.
				if (cs_progress && (loaded % 64) == 0) {
					EditorNode::progress_task_step("cs_hlod_load",
							"Loaded " + itos(loaded) + " chunks...", 0, true);
				}
#endif
			}
			da->list_dir_end();
			print_line("HLOD: loaded " + itos(loaded) + " persistent chunks.");
		}
		if (da) memdelete(da);
#ifdef TOOLS_ENABLED
		if (cs_progress) EditorNode::progress_end_task("cs_hlod_load");
#endif

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
	// HLOD chunks have no painted tint of their own (the bake covers many cells),
	// so bind white rather than leaving the sampler unbound and rendering black.
	mat->set_shader_param("color_map", _get_neutral_white_texture());

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
