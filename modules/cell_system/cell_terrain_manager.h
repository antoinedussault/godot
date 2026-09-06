#ifndef CELL_TERRAIN_MANAGER_H
#define CELL_TERRAIN_MANAGER_H

#include "cell_terrain.h" // needed for ComputedData as a PendingLoad value member, not just a pointer
#include "core/array.h"
#include "core/list.h"
#include "core/map.h"
#include "core/os/mutex.h"
#include "core/os/semaphore.h"
#include "core/os/thread.h"
#include "core/set.h"
#include "scene/3d/spatial.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
#include "scene/resources/texture.h"

class MeshInstance;
class Camera;
class CellTerrainLayer;

class CellTerrainManager : public Spatial {
	GDCLASS(CellTerrainManager, Spatial);

private:
	NodePath target_path;
	String world_dir = "user://world";
	int verts_per_side = 65;
	float cell_size = 64.0f;
	float height_scale = 1.0f;
	float texture_scale = 16.0f;

	// Tier 1: full detail (collision + full mesh).
	int load_radius = 1;
	// Tier 2: LOD cells (reduced mesh, no collision).
	int lod_radius = 4;
	// Tier 3: HLOD baked chunks.
	int hlod_chunk_size = 4; // NxN cells per chunk
	int hlod_radius = 3;    // in chunks, not cells
	float hlod_height_offset = -2.0f; // push HLOD below detailed terrain to avoid showing through

	// --- Streaming smoothness ---
	// Frustum-defers loading NEW render-only LOD cells (dist > load_radius)
	// that are behind the camera — deliberately NOT applied to load_radius
	// cells, which need collision in every direction regardless of where the
	// camera is looking (the player's own ground can't disappear just
	// because they're not looking at it). Already-active cells are never
	// unloaded just because the camera turned away, to avoid load/unload
	// churn as it swings around.
	bool cull_lod_behind_camera = true;
	float frustum_cull_dot = -0.4f; // cos of the half-angle behind which a cell is deferred
	// Fraction of a cell's width, near its far edge, at which the neighbor
	// beyond that edge starts preloading — spreads the (measured-expensive,
	// see CellTerrain::_apply_collision) first-time collision-shape creation
	// for an about-to-be-entered row/column of cells across the frames spent
	// approaching it, instead of bursting all at once exactly when the
	// player's reference cell actually changes. 0 disables preloading.
	float preload_margin = 0.25f;
	// How many seconds of travel, at the target's current speed, to add to
	// preload_margin's fixed lead time — walking pace keeps ~preload_margin's
	// worth of lead, sprinting/vehicles get proportionally more so the async
	// pipeline (I/O + compute + budgeted integration) still finishes before
	// arrival instead of the fixed margin being outrun. 0 disables the speed
	// term (equivalent to the old fixed-margin-only behavior).
	// DEFAULT OFF (0). The speed term regressed frame time badly when first
	// tried: a hitch frame read as a huge speed, widening the margin, causing
	// more collision promotions at once, causing a bigger hitch. _estimate_speed
	// now rejects hitch-frame samples and smooths/clamps the result, but this
	// stays opt-in until the plain fixed-margin baseline is confirmed good.
	float streaming_lookahead_seconds = 0.0f;
	// DEFAULT OFF, same reason: proactively promoting an about-to-be-entered
	// cell to collision-bearing is only a win if the promotion itself is
	// cheap enough to not be the thing causing the spike. Turn on to test.
	bool preload_collision_upgrade = false;
	bool _has_smoothed_speed = false;
	float _smoothed_speed = 0.0f;
	// Frame-to-frame delta of the position passed to stream_to(), used purely
	// to estimate speed for the lookahead above — deliberately not tied to
	// any specific player/camera node, since stream_to() is fed generically.
	bool _has_last_stream_pos = false;
	Vector3 _last_stream_pos;
	uint64_t _last_stream_time_usec = 0;
	float _estimate_speed(const Vector3 &world_pos);
	void _preload_border(int cx, int cz, float fx, float fz, float margin);
	// Mirrors _preload_border but at the load_radius boundary: proactively
	// flips an already-active, not-yet-collision cell's enable_collision on
	// and submits its (expensive, first-time) collision recompute ahead of
	// the player actually crossing into load_radius, instead of only ever
	// doing so reactively on the exact frame _evaluate() notices dist crossed
	// the threshold. _evaluate() later sees the flags already match and
	// no-ops the cell — idempotent, no double work.
	void _preload_collision_upgrade(int cx, int cz, float fx, float fz, float margin);
	void _preload_if_needed(int x, int z, int lod, bool collision);

	// Terrain3D-style: each paintable texture is a self-contained
	// CellTerrainLayer resource (name/id/textures/per-layer shading knobs)
	// instead of numbered texture slots. A layer's position in this array is
	// still what splatmap painting targets (see TerrainBrush::paint_layer);
	// layer_id is a separate, freely assignable identifier — see
	// get_layer_id_at_world().
	Vector<Ref<CellTerrainLayer> > layers;
	int num_layers = 4; // cached layers.size(), kept in sync by set_layers()
	void _on_layer_changed();

	Ref<Texture> brush_texture;
	Ref<TextureArray> albedo_texture_array;
	Ref<TextureArray> normal_texture_array;
	// Tiny 16x1 lookup textures carrying each layer's normal_strength/
	// parallax_scale/roughness_scale/uv_scale/color_mod — Godot 3.x shader
	// uniforms can't be arrays, so these stand in for a per-layer uniform
	// array (see terrain_shader.h). Always a full 16 columns (neutral
	// defaults past layers.size()) since they're bound unconditionally,
	// same hazard as detail_normal_map below.
	Ref<ImageTexture> layer_params_tex;
	Ref<ImageTexture> layer_color_tex;
	void _rebuild_layer_params_textures();
	bool _texarray_dirty = true;
	float height_blend_sharpness = 0.2f;
	// Large-scale (world-space) procedural tint that breaks up an otherwise
	// obviously-repeating tiled texture — see terrain_shader.h's macro_noise().
	// macro_variation_scale <= 0 disables it (same convention as parallax_scale).
	Color macro_variation_color1 = Color(1, 1, 1, 1);
	Color macro_variation_color2 = Color(1, 1, 1, 1);
	float macro_variation_scale = 0.0f;
	void _push_macro_variation_live();
	// Max view distance (world units) at which parallax still applies; past it
	// the shader skips the ray march entirely, so this is the main POM perf
	// knob as well as a look control. Pushed live — no cell rebuild needed.
	float parallax_max_distance = 48.0f;
	void _push_parallax_live();
	// Terrain debug overlay (see terrain_shader.h's debug_view uniform):
	// 0 = off, 1 = splatmap/layer blend, 2 = detail normal map, 3 = nav mask.
	// Shader-only, pushed live — changes no geometry, collision or nav data.
	int debug_view = 0;
	void _push_debug_view_live();
	// Strength of the heightmap-baked detail normal map (see CellTerrain::
	// _update_detail_normal_texture) that keeps coarse-LOD cells reading as
	// detailed under lighting. 0 disables it.
	float detail_normal_strength = 1.0f;
	// Flat/neutral (0.5,0.5,1.0) placeholder bound wherever a real per-cell
	// bake doesn't apply (HLOD chunks, the shared base material) — the shader
	// always needs a valid detail_normal_map texture, never an unbound sampler.
	Ref<ImageTexture> _neutral_normal_tex;
	Ref<ImageTexture> _get_neutral_normal_texture();

	// Navmesh: only cells within load_radius (same tier as collision) ever
	// carry a nav region — see CellTerrain::_rebuild_navmesh().
	bool enable_navmesh = true;
	float nav_max_slope_deg = 55.0f;
	// Navmesh grid step relative to the render grid — see
	// CellTerrain::ComputeInput::nav_downsample. Measured: building the navmesh
	// at render resolution (downsample 1) costs ~250ms frame stalls as cells
	// stream in, because NavigationServer re-syncs polygon connectivity over
	// the whole map on every region change. This is the single biggest
	// streaming cost in the module; do not set it to 1.
	int nav_downsample = 4;

	// "Bake Navmesh": persistent NavigationServer regions for every saved cell
	// OUTSIDE the live streaming radius, so AI can path across the whole saved
	// world instead of only the small area around the player. Registered
	// directly (not owned by any CellTerrain node) and kept resident until
	// cleared or re-baked — see CellTerrainManager::bake_navmesh(). Skips
	// (and, once active, frees) any cell already covered by its own live
	// CellTerrain navmesh region to avoid double-registering the same area.
	Map<int64_t, RID> _baked_nav_regions;
	int bake_navmesh_lod = 1;
	void _release_baked_navmesh(int64_t key);

	Ref<Shader> terrain_shader;
	Ref<ShaderMaterial> terrain_material;

	bool _has_last = false;
	int _last_cx = 0, _last_cz = 0;

	bool _debug_normals = false;
	float _debug_normal_length = 1.0f;
	bool _debug_navmesh = false;

	// When true (editor only — has no effect at runtime, gated by
	// Engine::is_editor_hint() in _evaluate()), every cell that has saved height
	// data on disk stays loaded regardless of lod_radius, so the whole sculpted
	// world is visible while editing instead of only the streamed-in radius.
	bool show_all_in_editor = false;
	void _scan_all_cell_keys(Set<int64_t> &r_out) const;

	// --- Async cell loading ---
	// File I/O, PNG decode, AND the mesh/navmesh/detail-normal-map math (see
	// CellTerrain::compute()) all run on _load_thread — none of it touches a
	// Node, VisualServer, or NavigationServer, so none of it needs the main
	// thread. The main thread integrates completed jobs at most
	// _max_integrate_per_frame cells per PROCESS tick, and for each just
	// uploads the already-computed data (CellTerrain::apply_computed()) —
	// the one part that does need the main thread — instead of recomputing
	// it via a full rebuild(), so cell transitions no longer spike frame time
	// on the CPU-heavy part.
	struct PendingLoad {
		int cx = 0, cz = 0;
		int lod = 0;
		bool collision = false;
		// True for a "recompute" job (see _submit_recompute): an already-active
		// cell whose neighbor edges or LOD changed. heights/nav_mask/splatmaps/
		// edge_* are pre-filled from the live cell's current state (cheap, main
		// thread, no disk I/O) instead of being loaded from disk by the worker.
		bool is_recompute = false;
		// Recompute jobs only: whether the cell's own collision needs
		// reapplying. False for a pure neighbor-edge-restitch recompute
		// (heights/enable_collision provably unchanged) — skipping it avoids
		// rebuilding+reassigning a whole HeightMapShape, by far the most
		// expensive single step measured here (a PhysicsServer/broadphase
		// cost, not a rendering one).
		bool update_collision = true;
		// Recompute jobs only: the exact instance this was computed for, so a
		// stale job can't get applied to a different cell that came to occupy
		// the same (cx,cz) key later (removed-then-recreated in the meantime).
		CellTerrain *target = nullptr;
		PoolRealArray heights;
		PoolRealArray nav_mask;
		PoolRealArray edge_n, edge_s, edge_e, edge_w;
		Ref<Image> splatmaps[4];
		CellTerrain::ComputedData computed;
	};
	Thread    _load_thread;
	Mutex     _load_mutex;
	Semaphore _load_sem;
	List<PendingLoad *> _load_queue;   // worker input  (guarded by _load_mutex)
	List<PendingLoad *> _ready_queue;  // worker output (guarded by _load_mutex)
	Set<int64_t> _loading;             // cells currently in-flight (main thread only)
	// Already-active cells with a recompute job in flight (see _submit_recompute)
	// — separate from _loading, which only ever gates cells NOT YET in _active.
	Set<int64_t> _recomputing;
	bool _thread_running = false;
	bool _thread_exit = false;
	// Wall-clock budget for _integrate_ready(), replacing what used to be a
	// fixed job COUNT (2/frame). A job's real main-thread cost varies by ~2
	// orders of magnitude (a pure edge-restitch recompute is sub-millisecond;
	// a brand-new collision-bearing cell is tens of ms for mesh upload +
	// HeightMapShape accelerator rebuild) — a count cap can't stop two
	// expensive ones landing in the same frame, which is exactly what showed
	// up as multi-hundred-ms Physics Process spikes. At least one job is
	// always integrated per call so the queue can't stall outright if a
	// single job legitimately costs more than the budget.
	float integrate_budget_ms = 2.0f;
	// Pure safety valve, not a tuning knob: caps how many jobs one call can
	// drain even if every single one finishes suspiciously fast, so a timing
	// glitch can't turn into an infinite loop.
	static const int _max_integrate_admission = 64;

	static void _load_worker(void *p_self);
	void _submit_load(int cx, int cz, int lod, bool collision);
	// Re-stitches an already-active cell's mesh/navmesh/detail-normal-map off
	// the main thread — used instead of calling CellTerrain::rebuild()
	// directly whenever a live cell's neighbor edges or LOD changed (as
	// opposed to _submit_load, which is for a cell that isn't loaded at all
	// yet). No-op if the cell isn't active or already has one in flight.
	// p_update_collision: see PendingLoad::update_collision.
	void _submit_recompute(int cx, int cz, bool p_update_collision = true);
	void _integrate_ready();
	CellTerrain *_create_cell_from_data(int cx, int cz, int lod, bool collision,
			const PoolRealArray &heights, const PoolRealArray &nav_mask, Ref<Image> *p_splatmaps);
	// Edge rows/columns for stitching, read from the 4 neighbors' saved
	// .heights files directly rather than their live nodes (which aren't
	// safe to touch off the main thread) — safe to call from the worker
	// thread, same profile as the already-thread-safe _load_heights().
	// Leaves an output array at size 0 if that neighbor has no saved file.
	void _load_neighbor_edges_from_disk(int cx, int cz,
			PoolRealArray &r_n, PoolRealArray &r_s, PoolRealArray &r_e, PoolRealArray &r_w) const;

	// When true, the manager streams to the editor camera every frame in the
	// editor, regardless of selection, so the terrain stays visible while you
	// work on other nodes. No effect at runtime (the player drives streaming
	// there). Editor-only logic is guarded by TOOLS_ENABLED in the .cpp.
	bool preview_in_editor = false;

	// Active cells (tier 1 + tier 2).
	Map<int64_t, CellTerrain *> _active;
	Set<int64_t> _dirty;

	// Active HLOD chunks (tier 3). Loaded once, kept resident.
	Map<int64_t, MeshInstance *> _hlod_active;
	bool _hlod_loaded = false;
	// HLOD chunks whose cells changed since the last bake (editor auto-rebake).
	Set<int64_t> _hlod_dirty;
	int64_t _chunk_key_for_cell(int cx, int cz) const;

	static int64_t _key(int x, int z);
	static void _unkey(int64_t k, int &x, int &z);

	Spatial *_get_target() const;
	// Preferred runtime streaming source: whatever camera is currently rendering
	// this manager's viewport, so terrain follows what's actually on screen
	// instead of requiring a player body / target_path to be wired up correctly.
	// target_path remains a fallback for headless setups with no active camera.
	Camera *_get_active_camera() const;
	CellTerrain *_spawn_cell(int cx, int cz, int lod, bool collision);
	PoolRealArray _load_heights(int cx, int cz) const;
	PoolRealArray _load_nav_mask(int cx, int cz) const;
	Ref<Image> _load_splatmap(int cx, int cz, int idx) const;
	bool _save_cell(int cx, int cz);
	void _update_neighbor_edges(int cx, int cz);
	void _evaluate(int cx, int cz);
	void _ensure_material();
	void _rebuild_texture_array();

	int _lod_for_distance(int dist) const;
	void _load_hlod_chunks(int center_cx, int center_cz);
	MeshInstance *_load_hlod_chunk(int chunk_x, int chunk_z);


protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	CellTerrain *get_cell(int cx, int cz) const;
	void rebuild_at(int cx, int cz);
	void mark_dirty(int cx, int cz);
	void save_dirty();
	void stream_to(const Vector3 &world_pos);
	void ensure_cell_sync(int cx, int cz);
	void editor_focus(const Vector3 &world_pos);
	void editor_clear();

	// Rebuilds a set of cells (plus their neighbors for edge stitching) exactly
	// once each, instead of repeatedly via rebuild_at. Used by the sculpt brush
	// to avoid rebuilding shared neighbor cells many times per stroke.
	void rebuild_region(const Set<int64_t> &p_cells);

	// Re-bakes only the HLOD chunks whose cells changed since the last bake
	// (saving those cells to disk first), then reloads them. Called by the editor
	// after editing settles so distant HLOD stays in sync without a manual bake.
	void rebake_dirty_hlod();
	bool has_dirty_hlod() const { return !_hlod_dirty.empty(); }

	// HLOD baking (call from editor).
	void bake_hlod();
	void bake_hlod_chunk(int chunk_x, int chunk_z);
	// Deletes every baked HLOD chunk (mesh + splatmaps) from disk and removes
	// the resident chunk instances from the scene.
	void clear_hlod();

	// Persistent whole-world navmesh baking — see the _baked_nav_regions
	// field comment. Safe to call from the editor (dock button) or at
	// runtime (e.g. your own game-start script) if you want this automatic
	// in a shipped build; it is NOT called automatically by this module.
	void bake_navmesh();
	void clear_baked_navmesh();
	bool has_baked_navmesh() const { return !_baked_nav_regions.empty(); }
	void set_bake_navmesh_lod(int l);
	int get_bake_navmesh_lod() const { return bake_navmesh_lod; }

	void set_target_path(const NodePath &p); NodePath get_target_path() const;
	void set_world_dir(const String &p);     String get_world_dir() const;
	void set_verts_per_side(int v);          int get_verts_per_side() const;
	void set_cell_size(float s);             float get_cell_size() const;
	void set_height_scale(float s);          float get_height_scale() const;
	void set_load_radius(int r);             int get_load_radius() const;
	void set_lod_radius(int r);              int get_lod_radius() const;
	void set_hlod_chunk_size(int s);         int get_hlod_chunk_size() const;
	void set_hlod_radius(int r);             int get_hlod_radius() const;
	void set_hlod_height_offset(float o);    float get_hlod_height_offset() const;
	void set_cull_lod_behind_camera(bool e); bool get_cull_lod_behind_camera() const { return cull_lod_behind_camera; }
	void set_frustum_cull_dot(float d);      float get_frustum_cull_dot() const { return frustum_cull_dot; }
	void set_preload_margin(float m);        float get_preload_margin() const { return preload_margin; }
	void set_streaming_lookahead_seconds(float s); float get_streaming_lookahead_seconds() const { return streaming_lookahead_seconds; }
	void set_preload_collision_upgrade(bool e) { preload_collision_upgrade = e; }
	bool get_preload_collision_upgrade() const { return preload_collision_upgrade; }
	void set_integrate_budget_ms(float ms);  float get_integrate_budget_ms() const { return integrate_budget_ms; }
	void set_texture_scale(float s);         float get_texture_scale() const;
	int get_num_layers() const { return num_layers; } // derived from layers.size()
	void set_brush_texture(const Ref<Texture> &t); Ref<Texture> get_brush_texture() const;

	// Typed array of CellTerrainLayer — see the field comment above.
	void set_layers(const Array &p_layers);
	Array get_layers() const;

	// "Which texture is at this position" queries, Terrain3D-style. Index is
	// the layer's position in `layers` (also its splatmap channel); id is
	// that layer's own freely-assignable CellTerrainLayer::layer_id. All
	// return -1 / a null Ref when there's no cell or no dominant layer there.
	int get_dominant_layer_index_at_world(float wx, float wz) const;
	int get_layer_id_at_world(float wx, float wz) const;
	Ref<CellTerrainLayer> get_layer_at_world(float wx, float wz) const;

	void set_height_blend_sharpness(float s); float get_height_blend_sharpness() const;
	void set_detail_normal_strength(float s); float get_detail_normal_strength() const;
	void set_macro_variation_color1(const Color &c); Color get_macro_variation_color1() const;
	void set_macro_variation_color2(const Color &c); Color get_macro_variation_color2() const;
	void set_macro_variation_scale(float s);         float get_macro_variation_scale() const;
	void set_enable_navmesh(bool e);          bool get_enable_navmesh() const;
	void set_nav_max_slope_deg(float d);      float get_nav_max_slope_deg() const;
	void set_nav_downsample(int d);           int get_nav_downsample() const;
	void set_parallax_max_distance(float d);  float get_parallax_max_distance() const;
	void set_debug_view(int mode);            int get_debug_view() const;

	void set_preview_in_editor(bool p_enabled); bool get_preview_in_editor() const;
	void set_show_all_in_editor(bool p_enabled); bool get_show_all_in_editor() const;

	void refresh();
	void set_debug_normals(bool enabled, float length);
	// Editor "Show Navmesh" toggle — draws each active cell's navmesh via a
	// debug MeshInstance regardless of Godot's own is_debugging_navigation_hint()
	// (see CellTerrain::nav_debug_view for why that alone isn't enough here).
	void set_debug_navmesh(bool enabled);
	float get_height_at_world(float wx, float wz) const;

	CellTerrainManager();
};

#endif
