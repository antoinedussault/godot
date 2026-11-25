#include "cell_terrain.h"

#include "scene/3d/collision_shape.h"
#include "scene/3d/immediate_geometry.h"
#include "scene/3d/mesh_instance.h"
#include "scene/3d/physics_body.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/height_map_shape.h"
#include "scene/resources/material.h"
#include "scene/resources/world.h"
#include "servers/navigation_server.h"

CellTerrain::CellTerrain() {}
CellTerrain::~CellTerrain() {
	if (nav_region.is_valid()) NavigationServer::get_singleton()->free(nav_region);
}

void CellTerrain::set_heights(const PoolRealArray &p) { heights = p; _normal_map_dirty = true; _nav_dirty = true; }
PoolRealArray CellTerrain::get_heights() const { return heights; }
void CellTerrain::set_verts_per_side(int v) { verts_per_side = MAX(2, v); }
int CellTerrain::get_verts_per_side() const { return verts_per_side; }
void CellTerrain::set_cell_size(float s) { cell_size = s; }
float CellTerrain::get_cell_size() const { return cell_size; }
void CellTerrain::set_height_scale(float s) { height_scale = s; }
float CellTerrain::get_height_scale() const { return height_scale; }
// Upper bound is what the grid can actually represent: each level halves the
// step, so verts_per_side=65 (seg=64) tops out at level 6 (a 2x2 quad). The
// mesh builder clamps step to seg as well, so an out-of-range level degrades to
// the coarsest valid mesh instead of producing a degenerate one.
void CellTerrain::set_lod_level(int l) { lod_level = CLAMP(l, 0, 8); }
int CellTerrain::get_lod_level() const { return lod_level; }
void CellTerrain::set_skirt_depth(float d) { skirt_depth = d; }
float CellTerrain::get_skirt_depth() const { return skirt_depth; }
void CellTerrain::set_enable_collision(bool e) { enable_collision = e; }
bool CellTerrain::get_enable_collision() const { return enable_collision; }
void CellTerrain::set_enable_navmesh(bool e) { if (enable_navmesh == e) return; enable_navmesh = e; _nav_dirty = true; }
bool CellTerrain::get_enable_navmesh() const { return enable_navmesh; }
void CellTerrain::set_nav_max_slope_deg(float d) { nav_max_slope_deg = d; _nav_dirty = true; }
float CellTerrain::get_nav_max_slope_deg() const { return nav_max_slope_deg; }
void CellTerrain::set_nav_downsample(int d) { nav_downsample = MAX(1, d); _nav_dirty = true; }
void CellTerrain::set_shader_debug_view(int mode) {
	shader_debug_view = mode;
	// The nav-mask texture only exists while that view is on; build it the
	// moment it's selected (and whenever it's re-selected, so mask painting
	// since last time is reflected).
	if (mode == 3) _update_nav_mask_texture();
	if (_material_inst.is_valid()) {
		_material_inst->set_shader_param("debug_view", shader_debug_view);
		if (mode == 3) _material_inst->set_shader_param("nav_mask_map", nav_mask_tex);
	}
}

void CellTerrain::set_shader_height_blend(float v) {
	shader_height_blend = v;
	if (_material_inst.is_valid()) _material_inst->set_shader_param("height_blend_sharpness", shader_height_blend);
}

void CellTerrain::set_shader_texture_scale(float v) {
	shader_texture_scale = v;
	if (_material_inst.is_valid()) _material_inst->set_shader_param("texture_scale", shader_texture_scale);
}

void CellTerrain::set_shader_detail_normal_strength(float v) {
	shader_detail_normal_strength = v;
	if (_material_inst.is_valid()) _material_inst->set_shader_param("detail_normal_strength", shader_detail_normal_strength);
}

void CellTerrain::set_shader_parallax_max_distance(float d) {
	shader_parallax_max_distance = MAX(0.0f, d);
	// Push straight to the live instance if there is one — no rebuild needed.
	if (_material_inst.is_valid()) _material_inst->set_shader_param("parallax_max_distance", shader_parallax_max_distance);
}
int CellTerrain::get_nav_downsample() const { return nav_downsample; }

void CellTerrain::set_neighbor_edges(const PoolRealArray &n, const PoolRealArray &s,
		const PoolRealArray &e, const PoolRealArray &w) {
	edge_n = n; edge_s = s; edge_e = e; edge_w = w;
	_nav_dirty = true; // neighbor edges affect slope at the cell's own border rows/cols
}
float CellTerrain::get_height_at(int lx, int lz) const {
	lx = CLAMP(lx, 0, verts_per_side - 1); lz = CLAMP(lz, 0, verts_per_side - 1);
	return heights[lz * verts_per_side + lx];
}
void CellTerrain::set_height_at(int lx, int lz, float h) {
	if (lx < 0 || lx >= verts_per_side || lz < 0 || lz >= verts_per_side) return;
	PoolRealArray::Write w = heights.write();
	w[lz * verts_per_side + lx] = h;
	_normal_map_dirty = true;
	_nav_dirty = true;
}

void CellTerrain::_ensure_nav_mask() {
	if (nav_mask.size() == verts_per_side * verts_per_side) return;
	nav_mask.resize(verts_per_side * verts_per_side);
	PoolRealArray::Write w = nav_mask.write();
	for (int i = 0; i < nav_mask.size(); i++) w[i] = 1.0f; // unpainted == fully navigable
}

void CellTerrain::set_nav_mask(const PoolRealArray &p_mask) { nav_mask = p_mask; _nav_dirty = true; }
// Obstacles come from FoliageManager when rock/tree placement changes and are
// consumed by the next navmesh build, so this only has to mark nav dirty --
// collision and the render mesh are unaffected (see
// CellTerrainManager::set_cell_nav_obstacles).
void CellTerrain::set_nav_obstacles(const PoolRealArray &p_obstacles) { nav_obstacles = p_obstacles; _nav_dirty = true; }
PoolRealArray CellTerrain::get_nav_mask() const { return nav_mask; }

float CellTerrain::get_nav_mask_at(int lx, int lz) const {
	if (nav_mask.size() != verts_per_side * verts_per_side) return 1.0f;
	lx = CLAMP(lx, 0, verts_per_side - 1); lz = CLAMP(lz, 0, verts_per_side - 1);
	return nav_mask[lz * verts_per_side + lx];
}
void CellTerrain::set_nav_mask_at(int lx, int lz, float v) {
	if (lx < 0 || lx >= verts_per_side || lz < 0 || lz >= verts_per_side) return;
	_ensure_nav_mask();
	PoolRealArray::Write w = nav_mask.write();
	w[lz * verts_per_side + lx] = CLAMP(v, 0.0f, 1.0f);
	_nav_dirty = true;
}

void CellTerrain::_ensure_hole_mask() {
	if (hole_mask.size() == verts_per_side * verts_per_side) return;
	hole_mask.resize(verts_per_side * verts_per_side);
	PoolRealArray::Write w = hole_mask.write();
	for (int i = 0; i < hole_mask.size(); i++) w[i] = 1.0f; // unpainted == solid
}

void CellTerrain::set_hole_mask(const PoolRealArray &p_mask) {
	hole_mask = p_mask;
	// A hole removes geometry, so unlike the nav mask this invalidates the
	// render mesh and the collision shape as well as the navmesh.
	_nav_dirty = true;
}
PoolRealArray CellTerrain::get_hole_mask() const { return hole_mask; }

float CellTerrain::get_hole_at(int lx, int lz) const {
	if (hole_mask.size() != verts_per_side * verts_per_side) return 1.0f;
	lx = CLAMP(lx, 0, verts_per_side - 1); lz = CLAMP(lz, 0, verts_per_side - 1);
	return hole_mask[lz * verts_per_side + lx];
}
void CellTerrain::set_hole_at(int lx, int lz, float v) {
	if (lx < 0 || lx >= verts_per_side || lz < 0 || lz >= verts_per_side) return;
	_ensure_hole_mask();
	PoolRealArray::Write w = hole_mask.write();
	w[lz * verts_per_side + lx] = CLAMP(v, 0.0f, 1.0f);
	_nav_dirty = true;
}

bool CellTerrain::has_holes() const {
	if (hole_mask.size() != verts_per_side * verts_per_side) return false;
	PoolRealArray::Read r = hole_mask.read();
	for (int i = 0; i < hole_mask.size(); i++) {
		if (r[i] < 0.5f) return true;
	}
	return false;
}

// ---------------------------------------------------------- painted colour
void CellTerrain::_ensure_color_map() {
	if (color_map.is_valid() && color_map->get_width() == verts_per_side && color_map->get_height() == verts_per_side) return;
	color_map.instance();
	color_map->create(verts_per_side, verts_per_side, false, Image::FORMAT_RGBA8);
	color_map->fill(Color(1, 1, 1, 1)); // white multiplies to no change
	color_map->lock();
	color_map->unlock();
}

Ref<Image> CellTerrain::get_color_map() const { return color_map; }
void CellTerrain::set_color_map(const Ref<Image> &p_img) {
	color_map = p_img;
	// Force the next _apply_material() to re-upload rather than keep a texture
	// pointing at the previous cell's image.
	color_map_tex = Ref<ImageTexture>();
}

void CellTerrain::update_color_texture() {
	Ref<Image> src = color_map;
	if (src.is_null()) {
		// Unpainted. Bind a tiny white image rather than allocating a full
		// per-cell map: an UNBOUND sampler2D samples black, and the shader
		// MULTIPLIES by this, so leaving it unbound turns the cell black.
		src.instance();
		src->create(4, 4, false, Image::FORMAT_RGBA8);
		src->fill(Color(1, 1, 1, 1));
	}
	if (color_map_tex.is_null() || color_map_tex->get_width() != src->get_width() ||
			color_map_tex->get_height() != src->get_height()) {
		// ImageTexture::set_data cannot change dimensions, so the first paint
		// (4x4 white -> full-size map) has to recreate.
		color_map_tex.instance();
		color_map_tex->create_from_image(src, Texture::FLAG_FILTER);
	} else {
		// In place, so a paint stroke does not reallocate the texture per frame
		// (same reasoning as update_splatmap_textures).
		color_map_tex->set_data(src);
	}
	if (_material_inst.is_valid()) _material_inst->set_shader_param("color_map", color_map_tex);
}

void CellTerrain::paint_color_at(int lx, int lz, const Color &p_color, float p_strength) {
	if (lx < 0 || lx >= verts_per_side || lz < 0 || lz >= verts_per_side) return;
	_ensure_color_map();
	const float t = CLAMP(p_strength, 0.0f, 1.0f);
	if (t <= 0.0f) return;
	color_map->lock();
	Color cur = color_map->get_pixel(lx, lz);
	color_map->set_pixel(lx, lz, Color(
			Math::lerp(cur.r, p_color.r, t),
			Math::lerp(cur.g, p_color.g, t),
			Math::lerp(cur.b, p_color.b, t),
			1.0f));
	color_map->unlock();
}

void CellTerrain::set_material(const Ref<Material> &m) { material = m; _material_inst = Ref<ShaderMaterial>(); }
Ref<Material> CellTerrain::get_material() const { return material; }

void CellTerrain::set_shader_resources(const Ref<TextureArray> &albedo, const Ref<TextureArray> &normal,
		int num_layers, float texture_scale, float height_blend,
		const Ref<Texture> &layer_params, const Ref<Texture> &layer_color,
		float detail_normal_strength,
		const Color &macro_variation_color1, const Color &macro_variation_color2,
		float macro_variation_scale) {
	albedo_array = albedo;
	normal_array = normal;
	shader_num_layers = num_layers;
	shader_texture_scale = texture_scale;
	shader_height_blend = height_blend;
	shader_layer_params = layer_params;
	shader_layer_color = layer_color;
	shader_detail_normal_strength = detail_normal_strength;
	shader_macro_variation_color1 = macro_variation_color1;
	shader_macro_variation_color2 = macro_variation_color2;
	shader_macro_variation_scale = macro_variation_scale;
	_material_inst = Ref<ShaderMaterial>(); // force rebuild of the cached instance
}

Ref<Image> CellTerrain::get_splatmap(int idx) const { return (idx >= 0 && idx < 4) ? splatmaps[idx] : Ref<Image>(); }
void CellTerrain::set_splatmap(int idx, const Ref<Image> &img) { if (idx >= 0 && idx < 4) splatmaps[idx] = img; }

void CellTerrain::_ensure_splatmaps() {
	for (int i = 0; i < 4; i++) {
		if (splatmaps[i].is_valid() && splatmaps[i]->get_width() == verts_per_side && splatmaps[i]->get_height() == verts_per_side) continue;
		splatmaps[i].instance();
		splatmaps[i]->create(verts_per_side, verts_per_side, false, Image::FORMAT_RGBA8);
		splatmaps[i]->fill(i == 0 ? Color(1, 0, 0, 0) : Color(0, 0, 0, 0));
		// Touch the data via lock/unlock so the image is in the same committed
		// state as an edited (painted) splatmap before GPU upload.
		splatmaps[i]->lock();
		splatmaps[i]->unlock();
	}
}

void CellTerrain::update_splatmap_textures() {
	_ensure_splatmaps();
	for (int i = 0; i < 4; i++) {
		if (splatmap_texes[i].is_null()) {
			splatmap_texes[i].instance();
			splatmap_texes[i]->create_from_image(splatmaps[i], Texture::FLAG_FILTER);
		} else {
			// Update the existing texture in place so the bound material picks it up
			// without reallocating (cheap enough to call while painting).
			splatmap_texes[i]->set_data(splatmaps[i]);
		}
	}
}

void CellTerrain::paint_layer(int lx, int lz, int layer, float strength) {
	if (lx < 0 || lx >= verts_per_side || lz < 0 || lz >= verts_per_side) return;
	if (layer < 0 || layer >= 16) return;
	_ensure_splatmaps();
	float w[16];
	for (int m = 0; m < 4; m++) {
		splatmaps[m]->lock();
		Color c = splatmaps[m]->get_pixel(lx, lz);
		w[m*4+0] = c.r; w[m*4+1] = c.g; w[m*4+2] = c.b; w[m*4+3] = c.a;
		splatmaps[m]->unlock();
	}
	float target = w[layer];
	float new_target = MIN(target + strength, 1.0f);
	float added = new_target - target;
	if (added <= 0.0f) return;
	w[layer] = new_target;
	float others_sum = 0.0f;
	for (int i = 0; i < 16; i++) if (i != layer) others_sum += w[i];
	if (others_sum > 0.0f) { float scale = MAX(0.0f, (others_sum - added) / others_sum); for (int i = 0; i < 16; i++) if (i != layer) w[i] *= scale; }
	for (int m = 0; m < 4; m++) {
		Color c(w[m*4+0], w[m*4+1], w[m*4+2], w[m*4+3]);
		splatmaps[m]->lock(); splatmaps[m]->set_pixel(lx, lz, c); splatmaps[m]->unlock();
	}
}

CellTerrain::ComputeInput CellTerrain::_make_compute_input() const {
	ComputeInput in;
	in.heights = heights;
	in.edge_n = edge_n; in.edge_s = edge_s; in.edge_e = edge_e; in.edge_w = edge_w;
	in.nav_mask = nav_mask; // may be empty (unpainted) — _mask_at_s treats that as "fully navigable"
	in.hole_mask = hole_mask; // may be empty (unpainted) — _hole_at_s treats that as "solid"
	// Without this the whole foliage-obstacle path is dead: FoliageManager
	// stamps rocks into CellTerrain::nav_obstacles, but compute() never saw
	// them, so no rock ever carved the navmesh.
	in.nav_obstacles = nav_obstacles;
	in.verts_per_side = verts_per_side;
	in.cell_size = cell_size;
	in.height_scale = height_scale;
	in.lod_level = lod_level;
	in.skirt_depth = skirt_depth;
	in.nav_max_slope_deg = nav_max_slope_deg;
	in.nav_downsample = nav_downsample;
	return in;
}

// --- Static, instance-free compute (safe from any thread) ---

float CellTerrain::_height_at_s(const ComputeInput &in, int x, int z) {
	const int vps = in.verts_per_side;
	if (x < 0  && in.edge_w.size() == vps) return in.edge_w[CLAMP(z,0,vps-1)] * in.height_scale;
	if (x >= vps && in.edge_e.size() == vps) return in.edge_e[CLAMP(z,0,vps-1)] * in.height_scale;
	if (z < 0  && in.edge_s.size() == vps) return in.edge_s[CLAMP(x,0,vps-1)] * in.height_scale;
	if (z >= vps && in.edge_n.size() == vps) return in.edge_n[CLAMP(x,0,vps-1)] * in.height_scale;
	return in.heights[CLAMP(z,0,vps-1) * vps + CLAMP(x,0,vps-1)] * in.height_scale;
}

float CellTerrain::_mask_at_s(const ComputeInput &in, int x, int z) {
	const int vps = in.verts_per_side;
	if (in.nav_mask.size() != vps * vps) return 1.0f; // unpainted == fully navigable
	x = CLAMP(x, 0, vps - 1); z = CLAMP(z, 0, vps - 1);
	return in.nav_mask[z * vps + x];
}

float CellTerrain::_hole_at_s(const ComputeInput &in, int x, int z) {
	const int vps = in.verts_per_side;
	if (in.hole_mask.size() != vps * vps) return 1.0f; // unpainted == solid
	x = CLAMP(x, 0, vps - 1); z = CLAMP(z, 0, vps - 1);
	return in.hole_mask[z * vps + x];
}

bool CellTerrain::_quad_is_hole_s(const ComputeInput &in, int lod_x, int lod_z, int step) {
	if (in.hole_mask.size() != in.verts_per_side * in.verts_per_side) return false;
	// Test the four heightmap samples this quad is actually built from - the
	// same ones _height_sampled_s reads - so the cut-out follows the mesh at
	// every LOD instead of drifting off it. A quad goes if ANY corner is holed:
	// erring toward removing geometry keeps the opening clear rather than
	// leaving a tooth hanging into it.
	const int x0 = lod_x * step, x1 = (lod_x + 1) * step;
	const int z0 = lod_z * step, z1 = (lod_z + 1) * step;
	return _hole_at_s(in, x0, z0) < 0.5f || _hole_at_s(in, x1, z0) < 0.5f ||
			_hole_at_s(in, x0, z1) < 0.5f || _hole_at_s(in, x1, z1) < 0.5f;
}

float CellTerrain::_height_sampled_s(const ComputeInput &in, int lod_x, int lod_z, int step) {
	int hx = CLAMP(lod_x * step, 0, in.verts_per_side - 1);
	int hz = CLAMP(lod_z * step, 0, in.verts_per_side - 1);
	return in.heights[hz * in.verts_per_side + hx] * in.height_scale;
}

Vector3 CellTerrain::_sample_normal_s(const ComputeInput &in, int lod_x, int lod_z, int step) {
	const float spacing = in.cell_size / (float)(in.verts_per_side - 1) * step;
	int hx = lod_x * step, hz = lod_z * step;
	float hl = _height_at_s(in, hx - step, hz);
	float hr = _height_at_s(in, hx + step, hz);
	float hd = _height_at_s(in, hx, hz - step);
	float hu = _height_at_s(in, hx, hz + step);
	Vector3 n(hl - hr, 2.0f * spacing, hd - hu);
	return n.normalized();
}

// --- LOD mesh with skirts (pure compute) ---
Array CellTerrain::_build_mesh_arrays_s(const ComputeInput &in) {
	const int vps = in.verts_per_side;
	const int seg = vps - 1;
	// Clamp so a high LOD level cannot decimate past a single quad: without
	// this, step > seg gives lod_vps = 1 and a degenerate (empty) mesh.
	int step = 1 << in.lod_level;
	if (step > seg) step = seg;
	if (step < 1) step = 1;
	const int lod_vps = seg / step + 1; // vertices per side at this LOD

	// Skirt depth MUST scale with the LOD step.
	//
	// There is no edge stitching in this builder - _height_sampled_s() reads
	// only this cell's own heightmap, and the edge_* arrays are not consulted
	// here - so the ONLY thing hiding a seam against a neighbour at a different
	// LOD is the skirt hanging below the edge.
	//
	// A coarse cell interpolates linearly across step*spacing metres, so its
	// deviation from the real surface grows linearly with step. A fixed 4-unit
	// skirt covers LOD 0-1 and then silently fails: at LOD 5 the mesh spans 32m
	// per segment and can sit tens of units off the true ground on a slope,
	// leaving an open gap with the skirt's dark underside showing through.
	const float lod_skirt_depth = in.skirt_depth * (float)step;
	const float full_spacing = in.cell_size / (float)seg;

	// Surface vertices.
	const int surf_count = lod_vps * lod_vps;
	// Skirt: 4 edges, each lod_vps verts duplicated (top reused, bottom added).
	const int skirt_verts = 4 * lod_vps;
	const int total_verts = surf_count + skirt_verts;

	PoolVector3Array vertices; vertices.resize(total_verts);
	PoolVector3Array normals;  normals.resize(total_verts);
	PoolVector2Array uvs;      uvs.resize(total_verts);
	PoolRealArray tangents;    tangents.resize(total_verts * 4);

	{
		PoolVector3Array::Write vw = vertices.write();
		PoolVector3Array::Write nw = normals.write();
		PoolVector2Array::Write uw = uvs.write();
		PoolRealArray::Write   tw = tangents.write();

		// Surface.
		for (int lz = 0; lz < lod_vps; lz++) {
			for (int lx = 0; lx < lod_vps; lx++) {
				int i = lz * lod_vps + lx;
				int hx = lx * step, hz = lz * step;
				float y = _height_sampled_s(in, lx, lz, step);
				vw[i] = Vector3(hx * full_spacing, y, hz * full_spacing);
				nw[i] = _sample_normal_s(in, lx, lz, step);
				uw[i] = Vector2((float)hx / seg, (float)hz / seg);

				// Tangent along U (X-direction), derived from the height slope so
				// the TBN matrix is correct even on a perfectly flat surface.
				// Binormal sign=-1 because cross(N,T) with N=(0,1,0),T=(1,0,0)
				// gives (0,0,-1), so *-1 = (0,0,+1) = correct V/Z direction.
				float hl = _height_at_s(in, hx - step, hz);
				float hr = _height_at_s(in, hx + step, hz);
				Vector3 t = Vector3(2.0f * step * full_spacing, hr - hl, 0.0f).normalized();
				tw[i*4+0] = t.x; tw[i*4+1] = t.y; tw[i*4+2] = t.z; tw[i*4+3] = -1.0f;
			}
		}

		// Skirt vertices: duplicates of edge verts pushed down.
		int si = surf_count;
		// South edge (lz = 0)
		for (int lx = 0; lx < lod_vps; lx++, si++) {
			int hx = lx * step;
			float y = _height_sampled_s(in, lx, 0, step);
			vw[si] = Vector3(hx * full_spacing, y - lod_skirt_depth, 0);
			nw[si] = Vector3(0, 0, -1);
			uw[si] = Vector2((float)hx / seg, 0);
			tw[si*4+0] = 1; tw[si*4+1] = 0; tw[si*4+2] = 0; tw[si*4+3] = -1;
		}
		// North edge (lz = lod_vps-1)
		for (int lx = 0; lx < lod_vps; lx++, si++) {
			int hx = lx * step, hz = (lod_vps - 1) * step;
			float y = _height_sampled_s(in, lx, lod_vps - 1, step);
			vw[si] = Vector3(hx * full_spacing, y - lod_skirt_depth, hz * full_spacing);
			nw[si] = Vector3(0, 0, 1);
			uw[si] = Vector2((float)hx / seg, (float)hz / seg);
			tw[si*4+0] = 1; tw[si*4+1] = 0; tw[si*4+2] = 0; tw[si*4+3] = -1;
		}
		// West edge (lx = 0)
		for (int lz = 0; lz < lod_vps; lz++, si++) {
			int hz = lz * step;
			float y = _height_sampled_s(in, 0, lz, step);
			vw[si] = Vector3(0, y - lod_skirt_depth, hz * full_spacing);
			nw[si] = Vector3(-1, 0, 0);
			uw[si] = Vector2(0, (float)hz / seg);
			tw[si*4+0] = 1; tw[si*4+1] = 0; tw[si*4+2] = 0; tw[si*4+3] = -1;
		}
		// East edge (lx = lod_vps-1)
		for (int lz = 0; lz < lod_vps; lz++, si++) {
			int hx = (lod_vps - 1) * step, hz = lz * step;
			float y = _height_sampled_s(in, lod_vps - 1, lz, step);
			vw[si] = Vector3(hx * full_spacing, y - lod_skirt_depth, hz * full_spacing);
			nw[si] = Vector3(1, 0, 0);
			uw[si] = Vector2((float)hx / seg, (float)hz / seg);
			tw[si*4+0] = 1; tw[si*4+1] = 0; tw[si*4+2] = 0; tw[si*4+3] = -1;
		}
	}

	// Surface indices. Sized for a full grid and trimmed afterwards, because
	// painted holes remove quads (see _quad_is_hole_s) and the final count is
	// only known once the loop has run.
	int quad_count = (lod_vps - 1) * (lod_vps - 1);
	int skirt_quads = 4 * (lod_vps - 1);
	PoolIntArray indices; indices.resize((quad_count + skirt_quads) * 6);
	int index_count = 0;
	{
		PoolIntArray::Write iw = indices.write();
		int t = 0;

		// Surface triangles.
		for (int lz = 0; lz < lod_vps - 1; lz++) {
			for (int lx = 0; lx < lod_vps - 1; lx++) {
				// Painted hole: emit no triangles here. The vertices stay in the
				// buffer (unreferenced verts are harmless and keep the index
				// arithmetic below trivial); only the quad disappears.
				if (_quad_is_hole_s(in, lx, lz, step)) continue;
				int i0 = lz * lod_vps + lx;
				int i1 = i0 + 1;
				int i2 = i0 + lod_vps;
				int i3 = i2 + 1;
				iw[t++] = i0; iw[t++] = i1; iw[t++] = i2;
				iw[t++] = i1; iw[t++] = i3; iw[t++] = i2;
			}
		}

		// Skirt triangles. For each edge, connect surface verts to skirt verts.
		int s_south = surf_count;
		int s_north = s_south + lod_vps;
		int s_west  = s_north + lod_vps;
		int s_east  = s_west  + lod_vps;

		// South skirt (surface row lz=0 -> skirt below).
		for (int lx = 0; lx < lod_vps - 1; lx++) {
			int top0 = lx, top1 = lx + 1;
			int bot0 = s_south + lx, bot1 = s_south + lx + 1;
			iw[t++] = top0; iw[t++] = bot0; iw[t++] = top1;
			iw[t++] = top1; iw[t++] = bot0; iw[t++] = bot1;
		}
		// North skirt (surface row lz=lod_vps-1 -> skirt below).
		for (int lx = 0; lx < lod_vps - 1; lx++) {
			int top0 = (lod_vps - 1) * lod_vps + lx, top1 = top0 + 1;
			int bot0 = s_north + lx, bot1 = s_north + lx + 1;
			iw[t++] = top0; iw[t++] = top1; iw[t++] = bot0;
			iw[t++] = top1; iw[t++] = bot1; iw[t++] = bot0;
		}
		// West skirt (surface col lx=0 -> skirt below).
		for (int lz = 0; lz < lod_vps - 1; lz++) {
			int top0 = lz * lod_vps, top1 = (lz + 1) * lod_vps;
			int bot0 = s_west + lz, bot1 = s_west + lz + 1;
			iw[t++] = top0; iw[t++] = top1; iw[t++] = bot0;
			iw[t++] = top1; iw[t++] = bot1; iw[t++] = bot0;
		}
		// East skirt (surface col lx=lod_vps-1 -> skirt below).
		for (int lx = lod_vps - 1, lz = 0; lz < lod_vps - 1; lz++) {
			int top0 = lz * lod_vps + lx, top1 = (lz + 1) * lod_vps + lx;
			int bot0 = s_east + lz, bot1 = s_east + lz + 1;
			iw[t++] = top0; iw[t++] = bot0; iw[t++] = top1;
			iw[t++] = top1; iw[t++] = bot0; iw[t++] = bot1;
		}
		index_count = t;
	}
	// Trim the unused tail left by skipped quads. Must happen after the Write
	// above is released.
	if (index_count != indices.size()) indices.resize(index_count);

	Array arrays; arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX]  = vertices;
	arrays[Mesh::ARRAY_NORMAL]  = normals;
	arrays[Mesh::ARRAY_TANGENT] = tangents;
	arrays[Mesh::ARRAY_TEX_UV]  = uvs;
	arrays[Mesh::ARRAY_INDEX]   = indices;
	return arrays;
}

// Navmesh vertices+polygons (pure compute) — mirrors the render mesh's
// surface (not the skirt). Quads steeper than nav_max_slope_deg are left out
// entirely (a gap = impassable), checked via the same gradient-based vertex
// normal the mesh already uses, not a per-triangle winding-dependent cross
// product, so it can't come out upside down.
void CellTerrain::_build_navmesh_data_s(const ComputeInput &in, PoolVector3Array &r_verts, Vector<Vector<int> > &r_polys) {
	r_verts.resize(0);
	r_polys.clear();
	if (in.heights.size() != in.verts_per_side * in.verts_per_side) return;

	const int vps = in.verts_per_side;
	const int seg = vps - 1;
	// Coarser than the render mesh on purpose — see ComputeInput::nav_downsample.
	// Halve the step until it divides the grid evenly, so the coarse nav grid
	// still lands exactly on the cell's edges (no gap at the seam).
	int step = (1 << in.lod_level) * MAX(1, in.nav_downsample);
	if (step > seg) step = seg;
	while (step > 1 && (seg % step) != 0) step >>= 1;
	const int lod_vps = seg / step + 1;
	const float full_spacing = in.cell_size / (float)seg;

	PoolVector3Array verts;
	verts.resize(lod_vps * lod_vps);
	{
		PoolVector3Array::Write w = verts.write();
		for (int lz = 0; lz < lod_vps; lz++) {
			for (int lx = 0; lx < lod_vps; lx++) {
				int hx = lx * step, hz = lz * step;
				w[lz * lod_vps + lx] = Vector3(hx * full_spacing, _height_sampled_s(in, lx, lz, step), hz * full_spacing);
			}
		}
	}

	// Foliage obstacles (rocks and anything else with a nav_obstacle_radius),
	// already converted to this cell's local space by
	// FoliageManager::refresh_nav_obstacles.
	const int obstacle_count = in.nav_obstacles.size() / 3;
	PoolRealArray::Read obstacles = in.nav_obstacles.read();

	Vector<Vector<int> > polys;
	const float slope_cos = Math::cos(Math::deg2rad(in.nav_max_slope_deg));
	const float quad_extent = step * full_spacing;
	for (int lz = 0; lz < lod_vps - 1; lz++) {
		for (int lx = 0; lx < lod_vps - 1; lx++) {
			if (_sample_normal_s(in, lx, lz, step).y < slope_cos) continue; // too steep — leave a hole
			if (_quad_is_hole_s(in, lx, lz, step)) continue; // painted hole: no ground here at all
			if (_mask_at_s(in, lx * step, lz * step) < 0.5f) continue; // manually excluded

			// Drop the quad if ANY obstacle disc touches it - a circle/rect
			// overlap test, not a centre-in-circle one. A nav quad is
			// nav_downsample times coarser than the render grid (4m at the
			// defaults), so a rock smaller than that would sit entirely inside
			// a quad and never contain its centre - agents would path straight
			// through it. Over-carving is the safe direction here.
			if (obstacle_count > 0) {
				const float qx0 = lx * quad_extent;
				const float qz0 = lz * quad_extent;
				const float qx1 = qx0 + quad_extent;
				const float qz1 = qz0 + quad_extent;
				bool blocked = false;
				for (int o = 0; o < obstacle_count; o++) {
					const float ox = obstacles[o * 3 + 0];
					const float oz = obstacles[o * 3 + 1];
					const float orad = obstacles[o * 3 + 2];
					// Nearest point on the quad to the disc centre.
					const float nx = CLAMP(ox, qx0, qx1);
					const float nz = CLAMP(oz, qz0, qz1);
					const float ddx = ox - nx;
					const float ddz = oz - nz;
					if (ddx * ddx + ddz * ddz <= orad * orad) { blocked = true; break; }
				}
				if (blocked) continue;
			}

			int i0 = lz * lod_vps + lx;
			int i1 = i0 + 1;
			int i2 = i0 + lod_vps;
			int i3 = i2 + 1;

			Vector<int> tri_a;
			tri_a.push_back(i0); tri_a.push_back(i1); tri_a.push_back(i2);
			polys.push_back(tri_a);
			Vector<int> tri_b;
			tri_b.push_back(i1); tri_b.push_back(i3); tri_b.push_back(i2);
			polys.push_back(tri_b);
		}
	}

	r_verts = verts;
	r_polys = polys;
}

// Per-texel heightmap normal at full resolution (pure compute) — see
// terrain_shader.h for how this gets blended in. Encoded relative to a flat
// XZ reference plane (T=+X, B=+Z, N=+Y): world-space (nx, ny, nz) ->
// tangent-space (nx, nz, ny), matching the flat tangent basis
// _build_mesh_arrays_s() already uses (T=(1,0,0), B=cross(N,T)*-1=(0,0,1)).
Ref<Image> CellTerrain::_build_detail_normal_image_s(const ComputeInput &in) {
	const int vps = in.verts_per_side;
	if (vps < 2 || in.heights.size() != vps * vps) return Ref<Image>();

	Ref<Image> img;
	img.instance();
	img->create(vps, vps, false, Image::FORMAT_RGB8);

	img->lock();
	for (int z = 0; z < vps; z++) {
		for (int x = 0; x < vps; x++) {
			Vector3 n = _sample_normal_s(in, x, z, 1);
			img->set_pixel(x, z, Color(n.x * 0.5f + 0.5f, n.z * 0.5f + 0.5f, n.y * 0.5f + 0.5f));
		}
	}
	img->unlock();
	return img;
}

CellTerrain::ComputedData CellTerrain::compute(const ComputeInput &in) {
	ComputedData out;
	if (in.heights.size() != in.verts_per_side * in.verts_per_side) return out;
	out.mesh_arrays = _build_mesh_arrays_s(in);
	_build_navmesh_data_s(in, out.nav_vertices, out.nav_polygons);
	out.detail_normal_img = _build_detail_normal_image_s(in);
	return out;
}

// --- The "apply" half: GPU/server uploads only, must run on the main thread ---

void CellTerrain::_update_nav_mask_texture() {
	const int vps = verts_per_side;
	Ref<Image> img;
	img.instance();
	img->create(vps, vps, false, Image::FORMAT_L8);
	img->lock();
	bool have = (nav_mask.size() == vps * vps);
	PoolRealArray::Read r = nav_mask.read();
	for (int z = 0; z < vps; z++) {
		for (int x = 0; x < vps; x++) {
			// An empty/unsized mask means "navigable everywhere" (same
			// convention as _mask_at_s), so it reads as fully white.
			float v = have ? r[z * vps + x] : 1.0f;
			img->set_pixel(x, z, Color(v, v, v));
		}
	}
	img->unlock();
	if (nav_mask_tex.is_null()) {
		nav_mask_tex.instance();
		nav_mask_tex->create_from_image(img, Texture::FLAG_FILTER);
	} else {
		nav_mask_tex->set_data(img);
	}
}

void CellTerrain::_upload_detail_normal_texture() {
	if (detail_normal_img.is_null()) return;
	if (detail_normal_tex.is_null()) {
		detail_normal_tex.instance();
		detail_normal_tex->create_from_image(detail_normal_img, Texture::FLAG_FILTER);
	} else {
		// Update the existing texture in place — the bound material already
		// references this same Texture object, same as splatmap_texes.
		detail_normal_tex->set_data(detail_normal_img);
	}
}

void CellTerrain::_apply_navmesh(const PoolVector3Array &p_verts, const Vector<Vector<int> > &p_polys) {
	Ref<NavigationMesh> navmesh;
	navmesh.instance();
	navmesh->set_vertices(p_verts);
	for (int i = 0; i < p_polys.size(); i++) navmesh->add_polygon(p_polys[i]);
	_last_navmesh = navmesh;

	const NavigationServer *ns = NavigationServer::get_singleton();
	if (!nav_region.is_valid()) nav_region = ns->region_create();
	ns->region_set_navmesh(nav_region, navmesh);
	if (is_inside_tree()) {
		ns->region_set_transform(nav_region, get_global_transform());
		Ref<World> w = get_world();
		if (w.is_valid()) ns->region_set_map(nav_region, w->get_navigation_map());

		// See the field comment on nav_debug_view: mirrors
		// NavigationMeshInstance::set_navigation_mesh() exactly, since that's
		// the only thing that actually feeds Godot's "Visible Navigation"
		// viewport overlay — the server has no generic way to draw it. Also
		// honors our own dock-level "Show Navmesh" toggle (nav_viz_enabled),
		// since most people never discover the native hint.
		bool want_debug = nav_viz_enabled || (get_tree() && get_tree()->is_debugging_navigation_hint());
		if (want_debug) {
			if (!nav_debug_view) {
				nav_debug_view = memnew(MeshInstance);
				nav_debug_view->set_material_override(get_tree()->get_debug_navigation_material());
				add_child(nav_debug_view);
			}
			nav_debug_view->set_mesh(navmesh->get_debug_mesh());
		} else {
			_clear_nav_debug_view();
		}
	}
}

void CellTerrain::_clear_nav_debug_view() {
	if (nav_debug_view) {
		nav_debug_view->queue_delete();
		nav_debug_view = nullptr;
	}
}

void CellTerrain::_apply_material() {
	if (!material.is_valid()) return;
	Ref<ShaderMaterial> sm = material;
	if (sm.is_valid()) {
		// Build the per-cell instance only once (or after the source material /
		// shader resources change). Reused across sculpt rebuilds so we don't
		// reallocate the material or re-upload splatmaps every mouse-move.
		// We do NOT use duplicate() because in Godot 3.5 it does not carry
		// TextureArray params, leaving cells rendering black until a rebuild.
		if (_material_inst.is_null()) {
			update_splatmap_textures(); // create splatmap textures once

			Ref<ShaderMaterial> inst;
			inst.instance();
			inst->set_shader(sm->get_shader());
			Ref<TextureArray> alb = albedo_array;
			Ref<TextureArray> nrm = normal_array;
			if (alb.is_null()) {
				Variant v = sm->get_shader_param("albedo_arr");
				if (v.get_type() == Variant::OBJECT)
					alb = Ref<TextureArray>(Object::cast_to<TextureArray>(v.operator Object *()));
			}
			if (nrm.is_null()) {
				Variant v = sm->get_shader_param("normal_arr");
				if (v.get_type() == Variant::OBJECT)
					nrm = Ref<TextureArray>(Object::cast_to<TextureArray>(v.operator Object *()));
			}
			if (alb.is_valid()) inst->set_shader_param("albedo_arr", alb);
			if (nrm.is_valid()) inst->set_shader_param("normal_arr", nrm);
			inst->set_shader_param("num_layers", shader_num_layers);
			inst->set_shader_param("texture_scale", shader_texture_scale);
			inst->set_shader_param("height_blend_sharpness", shader_height_blend);
			// Always bound (manager guarantees a neutral fallback texture even
			// with zero layers) — an unbound sampler2D samples black, same
			// hazard as detail_normal_map below.
			inst->set_shader_param("layer_params", shader_layer_params);
			inst->set_shader_param("layer_color", shader_layer_color);
			inst->set_shader_param("macro_variation_color1", shader_macro_variation_color1);
			inst->set_shader_param("macro_variation_color2", shader_macro_variation_color2);
			inst->set_shader_param("macro_variation_scale", shader_macro_variation_scale);
			inst->set_shader_param("parallax_max_distance", shader_parallax_max_distance);
			inst->set_shader_param("debug_view", shader_debug_view);
			if (shader_debug_view == 3) {
				if (nav_mask_tex.is_null()) _update_nav_mask_texture();
				inst->set_shader_param("nav_mask_map", nav_mask_tex);
			}
			inst->set_shader_param("detail_normal_map", detail_normal_tex);
			inst->set_shader_param("detail_normal_strength", shader_detail_normal_strength);
			// Must always be bound: colour is a MULTIPLY and an unbound
			// sampler2D samples black (same hazard as detail_normal_map).
			// update_color_texture() binds a 4x4 white when nothing is painted.
			if (color_map_tex.is_null()) update_color_texture();
			inst->set_shader_param("color_map", color_map_tex);
			inst->set_shader_param("splatmap0", splatmap_texes[0]);
			inst->set_shader_param("splatmap1", splatmap_texes[1]);
			inst->set_shader_param("splatmap2", splatmap_texes[2]);
			inst->set_shader_param("splatmap3", splatmap_texes[3]);
			_material_inst = inst;
		}
		// set_mesh() resets surface materials, so (re)assign the cached instance.
		mesh_instance->set_surface_material(0, _material_inst);
	} else {
		mesh_instance->set_surface_material(0, material);
	}
}

PoolVector3Array CellTerrain::_build_collision_faces() const {
	// Triangle soup matching the LOD-0 render surface, holes removed. Built in
	// cell-local metres, so the owning StaticBody needs NO transform - unlike
	// the heightfield path, which is anchored at the body origin and scaled.
	const int vps = verts_per_side;
	const int seg = vps - 1;
	const float spacing = cell_size / (float)seg;

	const ComputeInput in = _make_compute_input();

	PoolVector3Array faces;
	faces.resize(seg * seg * 6);
	int n = 0;
	{
		PoolVector3Array::Write w = faces.write();
		for (int z = 0; z < seg; z++) {
			for (int x = 0; x < seg; x++) {
				if (_quad_is_hole_s(in, x, z, 1)) continue;
				const Vector3 v00(x * spacing, _height_at_s(in, x, z) * height_scale, z * spacing);
				const Vector3 v10((x + 1) * spacing, _height_at_s(in, x + 1, z) * height_scale, z * spacing);
				const Vector3 v01(x * spacing, _height_at_s(in, x, z + 1) * height_scale, (z + 1) * spacing);
				const Vector3 v11((x + 1) * spacing, _height_at_s(in, x + 1, z + 1) * height_scale, (z + 1) * spacing);
				// Same diagonal split and winding as the render mesh.
				w[n++] = v00; w[n++] = v10; w[n++] = v01;
				w[n++] = v10; w[n++] = v11; w[n++] = v01;
			}
		}
	}
	faces.resize(n);
	return faces;
}

void CellTerrain::_apply_collision() {
	if (enable_collision) {
		if (!static_body) {
			static_body = memnew(StaticBody);
			add_child(static_body);
			collision_shape = memnew(CollisionShape); static_body->add_child(collision_shape);
		}

		// A HeightMapShape is a dense grid of samples with no way to express a
		// missing quad, so a cell with painted holes cannot use one - you would
		// still walk across the cave mouth. Those cells fall back to a triangle
		// soup of exactly the quads the render mesh kept. Every other cell stays
		// on the heightfield, which is far cheaper to build and to query.
		if (has_holes()) {
			if (hole_shape.is_null()) hole_shape.instance();
			hole_shape->set_faces(_build_collision_faces());
			if (!_collision_is_concave) {
				collision_shape->set_shape(hole_shape);
				_collision_is_concave = true;
				// Drop the heightfield: it would otherwise keep a full
				// verts_per_side^2 map alive for a cell that cannot use it
				// again unless the holes are painted back in.
				height_shape = Ref<HeightMapShape>();
			}
			// Faces are already in cell-local space.
			static_body->set_transform(Transform());
			static_body->set_visible(true);
			return;
		}
		if (_collision_is_concave) {
			// Last hole was filled back in - return to the fast path.
			_collision_is_concave = false;
			hole_shape = Ref<ConcavePolygonShape>();
			collision_shape->set_shape(Ref<Shape>());
		}

		PoolRealArray map_data; map_data.resize(verts_per_side * verts_per_side);
		{ PoolRealArray::Write mw = map_data.write(); for (int i = 0; i < heights.size(); i++) mw[i] = heights[i] * height_scale; }
		if (height_shape.is_null()) {
			// First time this StaticBody gets a shape: nothing owns it yet, so
			// these setters are free (HeightMapShape only notifies owners, and
			// there are none until collision_shape->set_shape() below).
			height_shape.instance();
			height_shape->set_map_width(verts_per_side);
			height_shape->set_map_depth(verts_per_side);
			height_shape->set_map_data(map_data);
			collision_shape->set_shape(height_shape);
		} else {
			// Reuse: mutate the existing shape's data instead of swapping in a
			// new Shape resource. See the comment on height_shape in the header.
			if (height_shape->get_map_width() != verts_per_side) height_shape->set_map_width(verts_per_side);
			if (height_shape->get_map_depth() != verts_per_side) height_shape->set_map_depth(verts_per_side);
			height_shape->set_map_data(map_data);
		}
		float spacing = cell_size / (float)(verts_per_side - 1);
		Transform xf; xf.basis.scale(Vector3(spacing, 1.0f, spacing));
		xf.origin = Vector3(cell_size * 0.5f, 0.0f, cell_size * 0.5f);
		static_body->set_transform(xf);
		static_body->set_visible(true);
	} else if (static_body) {
		static_body->set_visible(false);
	}
}

void CellTerrain::set_debug_normals(bool enabled, float length) {
	normal_viz_enabled = enabled;
	normal_viz_length = length;
	if (!enabled && normal_viz) {
		normal_viz->clear();
	} else if (enabled) {
		rebuild_normal_viz();
	}
}

void CellTerrain::rebuild_normal_viz() {
	if (heights.size() != verts_per_side * verts_per_side) return;

	if (!normal_viz) {
		normal_viz = memnew(ImmediateGeometry);
		Ref<SpatialMaterial> mat;
		mat.instance();
		mat->set_flag(SpatialMaterial::FLAG_UNSHADED, true);
		mat->set_albedo(Color(0.0f, 1.0f, 0.3f));
		normal_viz->set_material_override(mat);
		add_child(normal_viz);
	}

	ComputeInput in = _make_compute_input();
	const int step = 1 << lod_level;
	const int seg = verts_per_side - 1;
	const int lod_vps = seg / step + 1;
	const float spacing = cell_size / (float)seg;

	normal_viz->clear();
	normal_viz->begin(Mesh::PRIMITIVE_LINES);
	for (int lz = 0; lz < lod_vps; lz++) {
		for (int lx = 0; lx < lod_vps; lx++) {
			float y = _height_sampled_s(in, lx, lz, step);
			Vector3 pos(lx * step * spacing, y, lz * step * spacing);
			Vector3 n = _sample_normal_s(in, lx, lz, step);
			normal_viz->add_vertex(pos);
			normal_viz->add_vertex(pos + n * normal_viz_length);
		}
	}
	normal_viz->end();
}

void CellTerrain::set_debug_navmesh(bool enabled) {
	nav_viz_enabled = enabled;
	if (!enabled) {
		_clear_nav_debug_view();
		return;
	}
	// IMPORTANT: never call rebuild() here. This is invoked for every single
	// newly-streamed cell whenever CellTerrainManager's "Show Navmesh" toggle
	// is on (see _create_cell_from_data) — a full rebuild() there reintroduces
	// exactly the main-thread streaming freeze this module's async loader
	// exists to avoid. If this cell's navmesh has already been computed at
	// least once, _last_navmesh lets us refresh the debug view instantly
	// (just a mesh assignment). If not (e.g. a cell that's still streaming
	// in), do nothing here — its own first _apply_navmesh() call will see
	// nav_viz_enabled already set and build the debug view then, for free.
	if (_last_navmesh.is_valid() && is_inside_tree() && get_tree()) {
		if (!nav_debug_view) {
			nav_debug_view = memnew(MeshInstance);
			nav_debug_view->set_material_override(get_tree()->get_debug_navigation_material());
			add_child(nav_debug_view);
		}
		nav_debug_view->set_mesh(_last_navmesh->get_debug_mesh());
	}
}

void CellTerrain::rebuild() {
	if (heights.size() != verts_per_side * verts_per_side) return;

	ComputeInput in = _make_compute_input();

	if (!mesh_instance) {
		mesh_instance = memnew(MeshInstance);
		add_child(mesh_instance);
	}
	Ref<ArrayMesh> mesh; mesh.instance();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, _build_mesh_arrays_s(in));
	mesh_instance->set_mesh(mesh);

	// Heights may have changed since the last rebuild (sculpting) even when the
	// material instance itself is still cached below — rebake unconditionally
	// so the detail normal map never goes stale.
	if (_normal_map_dirty || detail_normal_tex.is_null()) {
		detail_normal_img = _build_detail_normal_image_s(in);
		_upload_detail_normal_texture();
		_normal_map_dirty = false;
	}

	_apply_material();
	_apply_collision();

	// Navmesh: mirrors the collision gate above (only cells near enough to
	// carry collision need to be pathable either).
	if (enable_navmesh) {
		if (_nav_dirty || !nav_region.is_valid()) {
			PoolVector3Array nverts;
			Vector<Vector<int> > npolys;
			_build_navmesh_data_s(in, nverts, npolys);
			_apply_navmesh(nverts, npolys);
			_nav_dirty = false;
		}
	} else if (nav_region.is_valid()) {
		NavigationServer::get_singleton()->free(nav_region);
		nav_region = RID();
		_clear_nav_debug_view();
	}

	if (normal_viz_enabled)
		rebuild_normal_viz();
	else if (normal_viz)
		normal_viz->clear();
}

void CellTerrain::apply_computed(const ComputedData &p_data, bool p_update_collision) {
	if (heights.size() != verts_per_side * verts_per_side) return;

	if (!mesh_instance) {
		mesh_instance = memnew(MeshInstance);
		add_child(mesh_instance);
	}
	Ref<ArrayMesh> mesh; mesh.instance();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, p_data.mesh_arrays);
	mesh_instance->set_mesh(mesh);

	detail_normal_img = p_data.detail_normal_img;
	_upload_detail_normal_texture();
	_normal_map_dirty = false;

	_apply_material();
	if (p_update_collision) _apply_collision();

	if (enable_navmesh) {
		_apply_navmesh(p_data.nav_vertices, p_data.nav_polygons);
	} else if (nav_region.is_valid()) {
		NavigationServer::get_singleton()->free(nav_region);
		nav_region = RID();
		_clear_nav_debug_view();
	}
	_nav_dirty = false;

	if (normal_viz_enabled)
		rebuild_normal_viz();
	else if (normal_viz)
		normal_viz->clear();
}

void CellTerrain::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY && !_skip_ready_rebuild) rebuild();
}

void CellTerrain::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_heights", "h"), &CellTerrain::set_heights);
	ClassDB::bind_method(D_METHOD("get_heights"), &CellTerrain::get_heights);
	ClassDB::bind_method(D_METHOD("set_nav_mask", "m"), &CellTerrain::set_nav_mask);
	ClassDB::bind_method(D_METHOD("get_nav_mask"), &CellTerrain::get_nav_mask);
	ClassDB::bind_method(D_METHOD("set_hole_mask", "m"), &CellTerrain::set_hole_mask);
	ClassDB::bind_method(D_METHOD("get_hole_mask"), &CellTerrain::get_hole_mask);
	ClassDB::bind_method(D_METHOD("get_hole_at", "lx", "lz"), &CellTerrain::get_hole_at);
	ClassDB::bind_method(D_METHOD("set_hole_at", "lx", "lz", "v"), &CellTerrain::set_hole_at);
	ClassDB::bind_method(D_METHOD("has_holes"), &CellTerrain::has_holes);
	ClassDB::bind_method(D_METHOD("set_color_map", "img"), &CellTerrain::set_color_map);
	ClassDB::bind_method(D_METHOD("get_color_map"), &CellTerrain::get_color_map);
	ClassDB::bind_method(D_METHOD("update_color_texture"), &CellTerrain::update_color_texture);
	ClassDB::bind_method(D_METHOD("paint_color_at", "lx", "lz", "color", "strength"), &CellTerrain::paint_color_at);
	ClassDB::bind_method(D_METHOD("set_verts_per_side", "v"), &CellTerrain::set_verts_per_side);
	ClassDB::bind_method(D_METHOD("get_verts_per_side"), &CellTerrain::get_verts_per_side);
	ClassDB::bind_method(D_METHOD("set_cell_size", "s"), &CellTerrain::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &CellTerrain::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_height_scale", "s"), &CellTerrain::set_height_scale);
	ClassDB::bind_method(D_METHOD("get_height_scale"), &CellTerrain::get_height_scale);
	ClassDB::bind_method(D_METHOD("set_lod_level", "l"), &CellTerrain::set_lod_level);
	ClassDB::bind_method(D_METHOD("get_lod_level"), &CellTerrain::get_lod_level);
	ClassDB::bind_method(D_METHOD("set_skirt_depth", "d"), &CellTerrain::set_skirt_depth);
	ClassDB::bind_method(D_METHOD("get_skirt_depth"), &CellTerrain::get_skirt_depth);
	ClassDB::bind_method(D_METHOD("set_enable_collision", "e"), &CellTerrain::set_enable_collision);
	ClassDB::bind_method(D_METHOD("get_enable_collision"), &CellTerrain::get_enable_collision);
	ClassDB::bind_method(D_METHOD("set_enable_navmesh", "e"), &CellTerrain::set_enable_navmesh);
	ClassDB::bind_method(D_METHOD("get_enable_navmesh"), &CellTerrain::get_enable_navmesh);
	ClassDB::bind_method(D_METHOD("set_nav_max_slope_deg", "d"), &CellTerrain::set_nav_max_slope_deg);
	ClassDB::bind_method(D_METHOD("get_nav_max_slope_deg"), &CellTerrain::get_nav_max_slope_deg);
	ClassDB::bind_method(D_METHOD("set_nav_downsample", "d"), &CellTerrain::set_nav_downsample);
	ClassDB::bind_method(D_METHOD("get_nav_downsample"), &CellTerrain::get_nav_downsample);
	ClassDB::bind_method(D_METHOD("set_material", "m"), &CellTerrain::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &CellTerrain::get_material);
	ClassDB::bind_method(D_METHOD("paint_layer", "lx", "lz", "layer", "strength"), &CellTerrain::paint_layer);
	ClassDB::bind_method(D_METHOD("rebuild"), &CellTerrain::rebuild);
	ClassDB::bind_method(D_METHOD("update_splatmap_textures"), &CellTerrain::update_splatmap_textures);
}
