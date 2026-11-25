// CellTerrainManager - the async streaming pipeline: worker pool, load and
// recompute jobs, budgeted main-thread integration, border preloading.
// Split out of cell_terrain_manager.cpp; same class, no behaviour change.

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

// ---- async loading helpers ----

int CellTerrainManager::_resolve_thread_count() const {
	if (streaming_threads > 0) return streaming_threads;
	// Auto. Measured on this workload, extra workers did NOT help: the cost is
	// main-thread (navmesh re-sync and mesh upload), so a single worker already
	// keeps the queue fed, and more of them only add scheduling contention.
	// Auto therefore stays at 1; raise streaming_threads by hand if a
	// genuinely compute-bound workload shows up.
	return 1;
}

void CellTerrainManager::_start_workers(int p_count) {
	int count = MAX(1, p_count);
	_thread_exit = false;
	for (int i = 0; i < count; i++) {
		Thread *t = memnew(Thread);
		t->start(&CellTerrainManager::_load_worker, this);
		_load_threads.push_back(t);
	}
	_active_thread_count = count;
}

void CellTerrainManager::_stop_workers() {
	if (_load_threads.empty()) {
		_active_thread_count = 0;
		return;
	}
	_thread_exit = true;
	// One post per worker: each consumes exactly one wakeup and then sees the
	// exit flag. Posting once would leave the rest blocked forever.
	for (int i = 0; i < _load_threads.size(); i++) {
		_load_sem.post();
	}
	for (int i = 0; i < _load_threads.size(); i++) {
		_load_threads[i]->wait_to_finish();
		memdelete(_load_threads[i]);
	}
	_load_threads.clear();
	_active_thread_count = 0;
	// A worker mid-job finishes it and pushes the result before exiting, so
	// nothing queued is lost - the new pool picks the queue up where this left off.
	_thread_exit = false;
}

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
			job->hole_mask = self->_load_hole_mask(job->cx, job->cz);
			for (int s = 0; s < 4; s++) {
				job->splatmaps[s] = self->_load_splatmap(job->cx, job->cz, s);
			}
			job->color_map = self->_load_color_map(job->cx, job->cz);
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
		in.hole_mask = job->hole_mask;
		in.nav_obstacles = job->nav_obstacles;
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
	if (!_active.has(k)) return;
	if (_recomputing.has(k)) {
		// Already in flight against an OLDER snapshot. Queue a follow-up
		// instead of dropping this request - see _recompute_again.
		Map<int64_t, bool>::Element *E = _recompute_again.find(k);
		if (E) {
			E->value() = E->value() || p_update_collision;
		} else {
			_recompute_again[k] = p_update_collision;
		}
		return;
	}
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
	job->hole_mask = in.hole_mask;
	job->nav_obstacles = in.nav_obstacles;
	job->edge_n = in.edge_n; job->edge_s = in.edge_s; job->edge_e = in.edge_e; job->edge_w = in.edge_w;
	for (int s = 0; s < 4; s++) job->splatmaps[s] = cell->get_splatmap(s);
	job->color_map = cell->get_color_map();

	_recomputing.insert(k);
	_load_mutex.lock();
	_load_queue.push_back(job);
	_load_mutex.unlock();
	_load_sem.post();
}

CellTerrain *CellTerrainManager::_create_cell_from_data(int cx, int cz, int lod, bool collision,
		const PoolRealArray &heights, const PoolRealArray &nav_mask,
		const PoolRealArray &hole_mask, Ref<Image> *p_splatmaps, const Ref<Image> &color_map) {
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
	t->set_hole_mask(hole_mask);
	for (int s = 0; s < 4; s++) t->set_splatmap(s, p_splatmaps[s]);
	t->set_color_map(color_map);
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
	int _tr_jobs = 0;

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
		_tr_jobs++;

		int64_t k = _key(job->cx, job->cz);

		if (job->is_recompute) {
			// Captured before the memdelete below, for the re-issue at the end.
			const int job_cx = job->cx;
			const int job_cz = job->cz;
			// Just re-applies already-precomputed data to a cell that was (and
			// still is) active — no new-cell setup, no add_child, nothing else.
			// Guard against the (removed then re-created between submission and
			// now) case by checking it's still the SAME instance, not just the
			// same map key.
			_recomputing.erase(k);
			if (_active.has(k) && _active[k] == job->target) _active[k]->apply_computed(job->computed, job->update_collision);
			memdelete(job);
			// Something changed this cell again while that job was in flight;
			// re-issue now that the slot is free, so the newer edge data is
			// what actually ends up on the mesh.
			Map<int64_t, bool>::Element *again = _recompute_again.find(k);
			if (again) {
				const bool want_collision = again->value();
				_recompute_again.erase(k);
				_submit_recompute(job_cx, job_cz, want_collision);
			}
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
					job->heights, job->nav_mask, job->hole_mask, job->splatmaps, job->color_map);
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
	if (cs_trace_enabled()) {
		cs_trace_report("_integrate_ready", start_usec, "jobs=" + itos(_tr_jobs) +
				" ready_left=" + itos(_ready_queue.size()) + " active=" + itos(_active.size()));
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
