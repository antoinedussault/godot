#ifndef CELL_TERRAIN_H
#define CELL_TERRAIN_H

#include "core/reference.h"
#include "core/color.h"
#include "core/image.h"
#include "core/rid.h"
#include "scene/3d/spatial.h"
#include "scene/resources/mesh.h"
#include "scene/resources/material.h"
#include "scene/resources/height_map_shape.h"
#include "scene/resources/navigation_mesh.h"
#include "scene/resources/texture.h"

class MeshInstance;
class StaticBody;
class CollisionShape;
class ImmediateGeometry;

class CellTerrain : public Spatial {
	GDCLASS(CellTerrain, Spatial);

public:
	// Inputs to compute() — a snapshot of everything the mesh/navmesh/detail-
	// normal-map math needs, independent of any live CellTerrain instance.
	// PoolArray copies are O(1) (copy-on-write), so building one of these from
	// an instance's own members, or from data freshly loaded off-thread, is cheap.
	struct ComputeInput {
		PoolRealArray heights;
		PoolRealArray edge_n, edge_s, edge_e, edge_w;
		// Per-vertex paintable navmesh mask (0 = excluded, 1 = navigable — see
		// TerrainBrush::MODE_NAV_MASK). Same layout as heights; empty/wrong-size
		// is treated as "navigable everywhere" (see _mask_at_s).
		PoolRealArray nav_mask;
		int verts_per_side = 65;
		float cell_size = 64.0f;
		float height_scale = 1.0f;
		int lod_level = 0;
		float skirt_depth = 4.0f;
		float nav_max_slope_deg = 55.0f;
		// Navmesh grid step relative to the render grid. The navmesh must NEVER
		// be built at render resolution: at verts_per_side=65 that is 64*64*2 =
		// 8192 nav polygons PER CELL, and Godot's NavigationServer re-syncs
		// polygon connectivity across the whole map whenever any region
		// changes — measured at ~250ms frame stalls with only 9 such cells
		// live. 4 cuts that 16x, 8 cuts it 64x, for navmesh detail that is
		// still far finer than any agent needs.
		int nav_downsample = 4;
	};

	// Everything compute() produces — pure CPU data, nothing GPU/server-side
	// yet. apply_computed() is the other half that uploads this.
	struct ComputedData {
		Array mesh_arrays; // ready for ArrayMesh::add_surface_from_arrays
		PoolVector3Array nav_vertices;
		Vector<Vector<int> > nav_polygons; // one entry per walkable quad-triangle
		Ref<Image> detail_normal_img;
	};

	// Pure-CPU precompute: mesh vertex/normal/tangent/uv/index arrays, navmesh
	// vertices+polygons, and the detail normal map's pixel data. Touches no
	// Node, VisualServer, or NavigationServer state at all, so it's safe to
	// call from any thread — this is what lets CellTerrainManager's streaming
	// worker thread do this work instead of the main thread. Mirrors exactly
	// what rebuild() computes from a live instance's own members.
	static ComputedData compute(const ComputeInput &p_input);

private:
	int verts_per_side = 65;
	float cell_size = 64.0f;
	float height_scale = 1.0f;
	int lod_level = 0;
	float skirt_depth = 4.0f;
	bool enable_collision = true;

	// Navmesh region: built directly from the same surface vertices as the
	// render mesh (no Recast bake needed — the geometry is already known),
	// only for cells that also carry collision (same radius as load_radius).
	// Registers on the Spatial's World's default navigation map, so any
	// NavigationAgent in the scene paths across it automatically — no agent-
	// side wiring needed (see NavigationAgent::get_navigation_map()).
	bool enable_navmesh = true;
	float nav_max_slope_deg = 55.0f;
	int nav_downsample = 4; // see ComputeInput::nav_downsample
	RID nav_region;
	bool _nav_dirty = true;
	// Godot's "Visible Navigation" viewport debug overlay is drawn per-node by
	// NavigationMeshInstance (scene/3d/navigation_mesh_instance.cpp) — it is
	// NOT queried from NavigationServer generically. Since we register with
	// the server directly via a raw region RID (no such node exists here),
	// nothing would ever show in that overlay without this: a debug MeshInstance
	// built the same way, gated the same way (get_tree()->is_debugging_navigation_hint()).
	MeshInstance *nav_debug_view = nullptr;
	// Cached so set_debug_navmesh() can refresh/build the debug view instantly
	// (just a mesh assignment) without forcing a full rebuild() — see that
	// method's comment for why that distinction matters.
	Ref<NavigationMesh> _last_navmesh;
	void _apply_navmesh(const PoolVector3Array &p_verts, const Vector<Vector<int> > &p_polys);
	void _clear_nav_debug_view();

	PoolRealArray heights;
	PoolRealArray edge_n, edge_s, edge_e, edge_w;
	// Paintable navmesh mask, see ComputeInput::nav_mask. Lazily sized/filled
	// to 1.0 (fully navigable) on first touch by _ensure_nav_mask().
	PoolRealArray nav_mask;
	void _ensure_nav_mask();

	Ref<Image> splatmaps[4];
	Ref<ImageTexture> splatmap_texes[4];

	// Heightmap-derived detail normal map, baked at full resolution (see
	// compute()) so lighting stays detailed even when the LOD mesh itself is
	// decimated. Rebaked whenever heights actually change.
	Ref<Image> detail_normal_img;
	Ref<ImageTexture> detail_normal_tex;
	bool _normal_map_dirty = true;
	float shader_detail_normal_strength = 1.0f;
	void _upload_detail_normal_texture(); // GPU upload only — image already computed

	Ref<Material> material;
	// Cached per-cell shader instance. Built once and reused across rebuilds so
	// sculpting doesn't reallocate the material / re-upload splatmaps every frame.
	// Invalidated when the source material or shader resources change.
	Ref<ShaderMaterial> _material_inst;
	Ref<TextureArray> albedo_array;
	Ref<TextureArray> normal_array;
	int shader_num_layers = 4;
	float shader_texture_scale = 16.0f;
	float shader_height_blend = 0.2f;
	Ref<Texture> shader_layer_params;
	Ref<Texture> shader_layer_color;
	Color shader_macro_variation_color1 = Color(1, 1, 1, 1);
	Color shader_macro_variation_color2 = Color(1, 1, 1, 1);
	float shader_macro_variation_scale = 0.0f;
	// Set separately from set_shader_resources() (which already carries 11
	// args across 5 call sites) — see set_shader_parallax_max_distance().
	float shader_parallax_max_distance = 48.0f;
	// Debug view mode (see terrain_shader.h's debug_view uniform). 0 = off.
	int shader_debug_view = 0;
	// Built lazily, only while the nav-mask debug view is active — the mask
	// itself lives on the CPU (see nav_mask) and is never needed on the GPU
	// for normal rendering.
	Ref<ImageTexture> nav_mask_tex;
	void _update_nav_mask_texture();
	void _apply_material();
	void _apply_collision();

	MeshInstance *mesh_instance = nullptr;
	StaticBody *static_body = nullptr;
	CollisionShape *collision_shape = nullptr;
	// Kept alive and reused across every _apply_collision() call instead of
	// instancing a fresh HeightMapShape each time: reassigning collision_shape's
	// shape (CollisionShape::set_shape) tears the old shape all the way down to
	// zero shapes and rebuilds from zero, which round-trips through Bullet's
	// full shape_owner remove/add -> reload_shapes()/reload_body() TWICE (once
	// to drop the old shape, once to add the new one) for every terrain update.
	// Mutating this shape's data in place instead costs only ONE such cycle,
	// halving the broadphase remove/re-add + heightfield-accelerator-rebuild
	// cost every time a collision-bearing cell streams in or gets resculpted.
	Ref<HeightMapShape> height_shape;
	ImmediateGeometry *normal_viz = nullptr;
	bool normal_viz_enabled = false;
	float normal_viz_length = 1.0f;
	// Editor-time "Show Navmesh" debug toggle (see terrain_editor_plugin.cpp's
	// dock) — independent of Godot's own is_debugging_navigation_hint(), which
	// nav_debug_view also honors but which most people never think to enable.
	bool nav_viz_enabled = false;

	// When true, NOTIFICATION_READY does not auto-call rebuild(). Set by
	// CellTerrainManager's async streaming path right before add_child(),
	// which — for a child added to an already-in-tree parent — fires READY
	// synchronously; without this guard that would trigger one full (edge-
	// less, since neighbor stitching hasn't run yet) rebuild() immediately,
	// then apply_computed() right after would do the real work a second
	// time — exactly the CPU spike this precompute path exists to avoid.
	bool _skip_ready_rebuild = false;

	ComputeInput _make_compute_input() const;
	void _ensure_splatmaps();

	// Static, instance-free versions — the actual algorithms. Everything
	// above just builds a ComputeInput from live members and calls these.
	static float _height_at_s(const ComputeInput &in, int x, int z);
	static float _height_sampled_s(const ComputeInput &in, int lod_x, int lod_z, int step);
	// 1.0 (navigable) wherever nav_mask is empty/undersized, matching the
	// "unpainted == fully navigable" default.
	static float _mask_at_s(const ComputeInput &in, int x, int z);
	static Vector3 _sample_normal_s(const ComputeInput &in, int lod_x, int lod_z, int step);
	static Array _build_mesh_arrays_s(const ComputeInput &in);
	static void _build_navmesh_data_s(const ComputeInput &in, PoolVector3Array &r_verts, Vector<Vector<int> > &r_polys);
	static Ref<Image> _build_detail_normal_image_s(const ComputeInput &in);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_heights(const PoolRealArray &p_heights);
	PoolRealArray get_heights() const;
	void set_verts_per_side(int p_v); int get_verts_per_side() const;
	void set_cell_size(float p_s);    float get_cell_size() const;
	void set_height_scale(float p_s); float get_height_scale() const;
	void set_lod_level(int l);        int get_lod_level() const;
	void set_skirt_depth(float d);    float get_skirt_depth() const;
	void set_enable_collision(bool e); bool get_enable_collision() const;
	void set_enable_navmesh(bool e);  bool get_enable_navmesh() const;
	void set_nav_max_slope_deg(float d); float get_nav_max_slope_deg() const;
	void set_nav_downsample(int d); int get_nav_downsample() const;
	// Applied to the cached per-cell ShaderMaterial without forcing a full
	// rebuild() — a distance knob changes no geometry, splat or nav data.
	void set_shader_parallax_max_distance(float d);
	void set_shader_debug_view(int mode);

	void set_neighbor_edges(const PoolRealArray &n, const PoolRealArray &s,
			const PoolRealArray &e, const PoolRealArray &w);
	float get_height_at(int lx, int lz) const;
	void set_height_at(int lx, int lz, float h);

	// Paintable navmesh mask — see ComputeInput::nav_mask.
	void set_nav_mask(const PoolRealArray &p_mask);
	PoolRealArray get_nav_mask() const;
	float get_nav_mask_at(int lx, int lz) const;
	void set_nav_mask_at(int lx, int lz, float v);

	void set_material(const Ref<Material> &m); Ref<Material> get_material() const;

	// Shader resources pushed from the manager so the per-cell material instance
	// can bind them explicitly (duplicate() drops TextureArray params in 3.5).
	// layer_params/layer_color are the manager's tiny 16x1 per-CellTerrainLayer
	// lookup textures (see terrain_shader.h) — normal_strength/parallax_scale/
	// roughness_scale/color_mod live per layer now, not as flat scalars here.
	void set_shader_resources(const Ref<TextureArray> &albedo, const Ref<TextureArray> &normal,
			int num_layers, float texture_scale, float height_blend,
			const Ref<Texture> &layer_params, const Ref<Texture> &layer_color,
			float detail_normal_strength,
			const Color &macro_variation_color1, const Color &macro_variation_color2,
			float macro_variation_scale);
	Ref<Image> get_splatmap(int idx) const;
	void set_splatmap(int idx, const Ref<Image> &img);
	void update_splatmap_textures();
	void paint_layer(int lx, int lz, int layer, float strength);

	void rebuild();
	void set_skip_ready_rebuild(bool e) { _skip_ready_rebuild = e; }

	// The fast half of rebuild(): applies data a caller already computed (see
	// compute()) instead of recomputing it — mesh upload, material, collision,
	// navmesh region, detail-normal-map upload. Used by CellTerrainManager's
	// streaming integration path when the manager's worker thread already
	// precomputed p_data for this cell off the main thread. Must still run on
	// the main thread itself (it touches VisualServer/NavigationServer/etc).
	//
	// p_update_collision: _apply_collision() rebuilds a whole new HeightMapShape
	// and reassigns it to the PhysicsServer every time it runs — measured as
	// the single most expensive step here by far (a physics-side broadphase
	// cost, not a rendering one). It only actually needs to happen when this
	// cell's OWN heights or enable_collision changed. A neighbor-edge-restitch
	// recompute (see CellTerrainManager::_submit_recompute) never touches
	// either, so callers that know that should pass false.
	void apply_computed(const ComputedData &p_data, bool p_update_collision = true);

	// Snapshot of everything compute() needs, built from this instance's
	// current live state (heights/edges/nav_mask/etc) — lets a caller (see
	// CellTerrainManager::_submit_recompute) dispatch a fresh compute() for
	// an already-active cell to the worker thread instead of calling
	// rebuild() synchronously on the main thread.
	ComputeInput get_compute_input() const { return _make_compute_input(); }

	void set_debug_normals(bool enabled, float length);
	void rebuild_normal_viz();
	// Toggles the navmesh debug MeshInstance (see nav_debug_view) regardless
	// of Godot's own is_debugging_navigation_hint(). Re-applies the navmesh
	// immediately if one is already registered, so toggling is instant.
	void set_debug_navmesh(bool enabled);

	CellTerrain();
	~CellTerrain();
};

#endif
