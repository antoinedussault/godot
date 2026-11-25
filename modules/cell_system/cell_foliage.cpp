#include "cell_foliage.h"

#include "scene/3d/collision_shape.h"
#include "scene/3d/mesh_instance.h"
#include "scene/3d/physics_body.h"
#include "scene/3d/camera.h"

CellFoliage::CellFoliage() {}
CellFoliage::~CellFoliage() {}

static uint32_t _name_hash(const String &s) { return s.hash(); }

int CellFoliage::_bucket_for(const Ref<FoliageType> &p_type) {
	if (p_type.is_null()) return -1;
	String n = p_type->get_type_name();
	for (int i = 0; i < buckets.size(); i++) {
		if (buckets[i].type.is_valid() && buckets[i].type->get_type_name() == n)
			return i;
	}
	TypeBucket b;
	b.type = p_type;
	buckets.push_back(b);
	return buckets.size() - 1;
}

void CellFoliage::configure_types(const Vector<Ref<FoliageType>> &p_types) {
	for (int i = 0; i < p_types.size(); i++) {
		if (p_types[i].is_valid()) _bucket_for(p_types[i]);
	}
}

void CellFoliage::add_instance(int p_type_index, const Transform &p_local_xform) {
	if (p_type_index < 0 || p_type_index >= buckets.size()) return;
	Instance inst;
	inst.xform = p_local_xform;
	buckets.write[p_type_index].instances.push_back(inst);
}

void CellFoliage::clear_type(int p_type_index) {
	if (p_type_index < 0 || p_type_index >= buckets.size()) return;
	buckets.write[p_type_index].instances.clear();
}

void CellFoliage::clear_all() {
	for (int i = 0; i < buckets.size(); i++)
		buckets.write[i].instances.clear();
}

int CellFoliage::get_instance_count(int p_type_index) const {
	if (p_type_index < 0 || p_type_index >= buckets.size()) return 0;
	return buckets[p_type_index].instances.size();
}

int CellFoliage::erase_in_radius(int p_type_index, const Vector3 &p_world_center,
		const Vector3 &p_cell_world_origin, float p_radius) {
	if (p_type_index < 0 || p_type_index >= buckets.size()) return 0;
	float r2 = p_radius * p_radius;
	Vector<Instance> &list = buckets.write[p_type_index].instances;
	int removed = 0;
	for (int i = list.size() - 1; i >= 0; i--) {
		Vector3 wp = p_cell_world_origin + list[i].xform.origin;
		if (wp.distance_squared_to(p_world_center) <= r2) {
			list.remove(i);
			removed++;
		}
	}
	return removed;
}

// ---------------------------------------------------------------------------
// Tree mesh extraction
// ---------------------------------------------------------------------------

// A single extracted sub-mesh of a tree, with its transform baked relative
// to the tree's root node (so nested bark/leaf hierarchies render correctly).
// material is the MeshInstance's instance-level material (override or per-surface)
// captured at extraction time, since get_mesh() does not carry those.
struct TreePart {
	Ref<Mesh>     mesh;
	Transform     local;
	Ref<Material> material;
};

// Returns the material assigned on the MeshInstance itself (not baked into the
// mesh): material_override first, then the first instance-level surface material.
// Null if the instance has none (the mesh's own surface material is used then).
static Ref<Material> _instance_material(MeshInstance *p_mi) {
	Ref<Material> m = p_mi->get_material_override();
	if (m.is_valid()) return m;
	int sc = p_mi->get_surface_material_count();
	for (int i = 0; i < sc; i++) {
		Ref<Material> sm = p_mi->get_surface_material(i);
		if (sm.is_valid()) return sm;
	}
	return Ref<Material>();
}

static int _lod_index_from_name(const String &name) {
	int pos = name.rfind("-lod");
	if (pos < 0) return -1;
	String tail = name.substr(pos + 4);
	return tail.is_valid_integer() ? tail.to_int() : -1;
}

static bool _is_imposter_node(const String &name) {
	return name.ends_with("-imposter") || name.ends_with("_imposter");
}

// Accumulate the transform from p_node up to (but excluding) p_root.
static Transform _xform_to_root(Node *p_node, Node *p_root) {
	Transform t;
	Node *cur = p_node;
	while (cur && cur != p_root) {
		Spatial *s = Object::cast_to<Spatial>(cur);
		if (s) t = s->get_transform() * t;
		cur = cur->get_parent();
	}
	return t;
}

// Walk the instanced tree scene and collect every MeshInstance into per-LOD
// part lists (bark, leaf, ...). Non-mesh nodes (StaticBody, CollisionShape)
// are ignored. The imposter is collected separately and never culled.
static void _extract_tree_meshes(Node *p_node, Node *p_root, int p_max_lods,
		Map<int, Vector<TreePart>> &r_lods,
		Ref<Mesh> &r_imposter, Transform &r_imposter_local,
		Ref<Material> &r_imposter_material) {
	MeshInstance *mi = Object::cast_to<MeshInstance>(p_node);
	if (mi && mi->get_mesh().is_valid()) {
		String name = mi->get_name();
		if (_is_imposter_node(name)) {
			if (r_imposter.is_null()) {
				r_imposter          = mi->get_mesh();
				r_imposter_local    = _xform_to_root(mi, p_root);
				r_imposter_material = _instance_material(mi);
			}
		} else {
			int lod = _lod_index_from_name(name);
			if (lod >= 0 && lod < p_max_lods) {
				TreePart part;
				part.mesh     = mi->get_mesh();
				part.local    = _xform_to_root(mi, p_root);
				part.material = _instance_material(mi);
				r_lods[lod].push_back(part);
			}
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++)
		_extract_tree_meshes(p_node->get_child(i), p_root, p_max_lods,
				r_lods, r_imposter, r_imposter_local, r_imposter_material);
}

// Collect every CollisionShape authored in the tree scene, with each shape's
// transform baked relative to the tree root.
static void _extract_tree_collision(Node *p_node, Node *p_root,
		Vector<CellFoliage::CollisionPart> &r_parts) {
	CollisionShape *cs = Object::cast_to<CollisionShape>(p_node);
	if (cs && cs->get_shape().is_valid()) {
		CellFoliage::CollisionPart part;
		part.shape = cs->get_shape();
		part.local = _xform_to_root(cs, p_root);
		r_parts.push_back(part);
	}
	for (int i = 0; i < p_node->get_child_count(); i++)
		_extract_tree_collision(p_node->get_child(i), p_root, r_parts);
}

// ---------------------------------------------------------------------------
// _clear_render
// ---------------------------------------------------------------------------

void CellFoliage::_clear_render() {
	for (int b = 0; b < buckets.size(); b++) {
		TypeBucket &bk = buckets.write[b];

		for (int l = 0; l < bk.lods.size(); l++) {
			LodLevel &lvl = bk.lods.write[l];
			for (int s = 0; s < lvl.mmis.size(); s++) {
				if (lvl.mmis[s]) lvl.mmis[s]->queue_delete();
			}
		}
		bk.lods.clear();
		bk.lod_count = 0;

		if (bk.imposter_mmi) {
			bk.imposter_mmi->queue_delete();
			bk.imposter_mmi = nullptr;
		}
		bk.imposter_mm  = Ref<MultiMesh>();
		bk.has_imposter = false;

		for (int i = 0; i < bk.scenes.size(); i++) {
			if (bk.scenes[i]) bk.scenes[i]->queue_delete();
		}
		bk.scenes.clear();

		bk.collision_parts.clear();
	}
	_built = false;
}

// ---------------------------------------------------------------------------
// _build_render_nodes
// ---------------------------------------------------------------------------

// Builds a MultiMesh for one sub-mesh. p_local is the part's transform relative
// to the foliage instance origin (identity for grass; bark/leaf offset for trees).
// Capacity is inst_count, but only the instances actually at this LOD are rendered
// (packed to the front) via set_visible_instance_count, updated by update_lod.
// p_visible: initially all instances are at LOD0, so LOD0 starts fully visible and
// every other level starts at zero visible instances.
static Ref<MultiMesh> _make_multimesh(const Ref<Mesh> &mesh, int inst_count,
		const Vector<CellFoliage::Instance> &instances, const Transform &p_local,
		bool p_visible) {
	Ref<MultiMesh> mm;
	mm.instance();
	mm->set_transform_format(MultiMesh::TRANSFORM_3D);
	mm->set_color_format(MultiMesh::COLOR_NONE);
	mm->set_mesh(mesh);
	mm->set_instance_count(inst_count);
	if (p_visible) {
		for (int i = 0; i < inst_count; i++)
			mm->set_instance_transform(i, instances[i].xform * p_local);
		mm->set_visible_instance_count(inst_count);
	} else {
		mm->set_visible_instance_count(0);
	}
	return mm;
}

void CellFoliage::_build_render_nodes() {
	_clear_render();

	for (int b = 0; b < buckets.size(); b++) {
		TypeBucket &bk = buckets.write[b];
		if (bk.type.is_null()) continue;
		// In persistent-only mode, skip render for non-persistent buckets (grass).
		if (_persistent_only && !bk.type->is_persistent()) continue;
		int inst_count = bk.instances.size();
		if (inst_count == 0) continue;

		bk.instance_lod.resize(inst_count);
		for (int i = 0; i < inst_count; i++) bk.instance_lod.write[i] = 0;

		bool       shadows     = bk.type->get_cast_shadow();
		int        shadow_lods = bk.type->get_shadow_lod_count();
		Ref<Material> mat_ovr  = bk.type->get_material_override();
		const GeometryInstance::ShadowCastingSetting SHADOW_OFF =
				GeometryInstance::SHADOW_CASTING_SETTING_OFF;
		const GeometryInstance::ShadowCastingSetting SHADOW_ON =
				GeometryInstance::SHADOW_CASTING_SETTING_ON;

		int cat = bk.type->get_type();

		// ------------------------------------------------------------- GRASS
		if (cat == FoliageType::TYPE_GRASS) {
			int want = bk.type->get_lod_count();

			int built = 0;
			for (int l = 0; l < want; l++) {
				Ref<Mesh> mesh = bk.type->get_grass_mesh(l);
				if (mesh.is_null()) break; // slots must be contiguous

				// Only LOD0 visible initially; update_lod refines per camera distance.
				Ref<MultiMesh> mm = _make_multimesh(mesh, inst_count, bk.instances,
						Transform(), l == 0);

				MultiMeshInstance *mmi = memnew(MultiMeshInstance);
				mmi->set_multimesh(mm);
				// Instance transforms/LOD are refreshed from the camera each
				// rendered frame (see _update_all_lod()), not physics ticks —
				// physics interpolation doesn't apply here and its "set outside
				// physics process" warning is just noise for foliage.
				mmi->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
				mmi->set_cast_shadows_setting((shadows && l < shadow_lods) ? SHADOW_ON : SHADOW_OFF);
				Ref<Material> lod_mat = bk.type->get_grass_material(l);
				if (lod_mat.is_valid())
					mmi->set_material_override(lod_mat);
				else if (mat_ovr.is_valid())
					mmi->set_material_override(mat_ovr);
				add_child(mmi);

				LodLevel lvl;
				lvl.mmis.push_back(mmi);
				lvl.mms.push_back(mm);
				lvl.locals.push_back(Transform());
				bk.lods.push_back(lvl);
				built = l + 1;
			}
			bk.lod_count = built;

		// -------------------------------------------------------------- TREE
		} else if (cat == FoliageType::TYPE_TREE || cat == FoliageType::TYPE_ROCK) {
			Ref<PackedScene> scene = bk.type->get_tree_scene();
			if (scene.is_null()) continue;

			// Instance temporarily to extract meshes — never added to the scene tree.
			Node *tmp = scene->instance();
			if (!tmp) continue;

			int want = bk.type->get_lod_count();
			Map<int, Vector<TreePart>> lod_parts;
			Ref<Mesh> imposter_mesh;
			Transform imposter_local;
			Ref<Material> imposter_material;
			_extract_tree_meshes(tmp, tmp, want, lod_parts, imposter_mesh,
					imposter_local, imposter_material);

			// Pull authored collision shapes (trunk, etc.) from the same scene.
			if (bk.type->get_collision_enabled())
				_extract_tree_collision(tmp, tmp, bk.collision_parts);

			memdelete(tmp);

			// Determine actual contiguous LOD count.
			int built = 0;
			for (int l = 0; l < want; l++) {
				if (lod_parts.has(l) && lod_parts[l].size() > 0) built = l + 1;
				else break;
			}
			// Far (persistent-only) cells render the imposter ONLY — no LOD meshes.
			// This keeps memory, draw calls and per-frame LOD work bounded across
			// the whole world; the cell upgrades to full LODs when the camera nears.
			bk.lod_count = _persistent_only ? 0 : MAX(0, built);

			for (int l = 0; l < bk.lod_count; l++) {
				const Vector<TreePart> &parts = lod_parts[l];
				LodLevel lvl;
				for (int s = 0; s < parts.size(); s++) {
					// Only LOD0 visible initially; update_lod refines per distance.
					Ref<MultiMesh> mm = _make_multimesh(parts[s].mesh, inst_count,
							bk.instances, parts[s].local, l == 0);

					MultiMeshInstance *mmi = memnew(MultiMeshInstance);
					mmi->set_multimesh(mm);
					mmi->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
					mmi->set_cast_shadows_setting((shadows && l < shadow_lods) ? SHADOW_ON : SHADOW_OFF);
					// FoliageType override wins; otherwise use the material captured
					// from the MeshInstance (mesh-surface materials apply on their own).
					if (mat_ovr.is_valid()) mmi->set_material_override(mat_ovr);
					else if (parts[s].material.is_valid()) mmi->set_material_override(parts[s].material);
					add_child(mmi);

					lvl.mmis.push_back(mmi);
					lvl.mms.push_back(mm);
					lvl.locals.push_back(parts[s].local);
				}
				bk.lods.push_back(lvl);
			}

			// Imposter MultiMesh — always-on, never culled by max_render_distance.
			// Visible initially when there are no mesh LODs to show (far cells, or
			// trees with no scene LODs).
			if (imposter_mesh.is_valid()) {
				bool imp_initial = (bk.lod_count == 0);
				Ref<MultiMesh> mm = _make_multimesh(imposter_mesh, inst_count,
						bk.instances, imposter_local, imp_initial);

				MultiMeshInstance *mmi = memnew(MultiMeshInstance);
				mmi->set_multimesh(mm);
				mmi->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
				mmi->set_cast_shadows_setting(SHADOW_OFF); // imposter never casts shadows
				if (mat_ovr.is_valid()) mmi->set_material_override(mat_ovr);
				else if (imposter_material.is_valid()) mmi->set_material_override(imposter_material);
				add_child(mmi);

				bk.imposter_mmi  = mmi;
				bk.imposter_mm   = mm;
				bk.imposter_local = imposter_local;
				bk.has_imposter  = true;
			}

		// --------------------------------------------------------- INTERACTOR
		} else if (cat == FoliageType::TYPE_INTERACTOR) {
			Ref<PackedScene> scene = bk.type->get_interactor_scene();
			if (scene.is_null()) continue;
			for (int i = 0; i < inst_count; i++) {
				Node *n = scene->instance();
				Spatial *sp = Object::cast_to<Spatial>(n);
				if (!sp) { if (n) memdelete(n); continue; }
				sp->set_transform(bk.instances[i].xform);
				sp->set_visible(true);
				add_child(sp);
				bk.scenes.push_back(sp);
			}
		}
	}

	_built = true;
}

void CellFoliage::rebuild() {
	_build_render_nodes();
	if (collision_active)
		_rebuild_colliders(get_global_transform().origin);
}

// ---------------------------------------------------------------------------
// update_lod
// ---------------------------------------------------------------------------

void CellFoliage::update_lod(const Vector3 &p_cam_world, const Vector3 &p_cell_world_origin) {
	if (!_built) return;
	// Far cells are imposter-only (built visible); they need no per-frame LOD work.
	// Skipping them keeps the whole-world tree set from costing anything per frame.
	if (_persistent_only) return;

	for (int b = 0; b < buckets.size(); b++) {
		TypeBucket &bk = buckets.write[b];
		if (bk.type.is_null()) continue;

		int cat        = bk.type->get_type();
		int inst_count = bk.instances.size();
		if (inst_count == 0) continue;

		// ---------------------------------------------------- GRASS and TREE
		if (cat == FoliageType::TYPE_GRASS || cat == FoliageType::TYPE_TREE || cat == FoliageType::TYPE_ROCK) {
			int lod_count = bk.lod_count;
			// Imposter occupies the level index just past the last mesh LOD.
			const int imposter_level = lod_count;

			// Recompute each instance's target level; note whether any changed.
			//   0 .. lod_count-1 -> that mesh LOD
			//   imposter_level   -> show imposter (trees only)
			//   -1               -> fully hidden (culled, no imposter)
			bool changed = false;
			for (int i = 0; i < inst_count; i++) {
				Vector3 wp   = p_cell_world_origin + bk.instances[i].xform.origin;
				float   dist = wp.distance_to(p_cam_world);

				int sel;
				if (bk.has_imposter) {
					float imp_d = bk.type->get_lod_distance(lod_count - 1);
					if (imp_d <= 0.0f) imp_d = bk.type->get_max_render_distance();
					if (dist >= imp_d) {
						sel = imposter_level;
					} else {
						sel = bk.type->lod_for_distance(dist);
						if (sel >= lod_count) sel = lod_count - 1;
					}
				} else {
					if (bk.type->is_culled(dist)) {
						sel = -1;
					} else {
						sel = bk.type->lod_for_distance(dist);
						if (sel >= lod_count) sel = lod_count - 1;
					}
				}

				if (i < bk.instance_lod.size() && bk.instance_lod[i] != sel) {
					changed = true;
					bk.instance_lod.write[i] = sel;
				}
			}
			if (!changed) continue; // nothing crossed a LOD boundary this frame

			// Recompact: pack the instances at each level to the front of that
			// level's MultiMeshes and cap rendering with visible_instance_count, so
			// each tree is vertex-processed once (in its current LOD) instead of in
			// every LOD mesh.
			for (int l = 0; l < lod_count; l++) {
				LodLevel &lvl = bk.lods.write[l];
				for (int s = 0; s < lvl.mms.size(); s++) {
					if (lvl.mms[s].is_null()) continue;
					int vis = 0;
					for (int i = 0; i < inst_count; i++) {
						if (bk.instance_lod[i] == l) {
							lvl.mms.write[s]->set_instance_transform(vis, bk.instances[i].xform * lvl.locals[s]);
							vis++;
						}
					}
					lvl.mms.write[s]->set_visible_instance_count(vis);
				}
			}
			if (bk.has_imposter && bk.imposter_mm.is_valid()) {
				int vis = 0;
				for (int i = 0; i < inst_count; i++) {
					if (bk.instance_lod[i] == imposter_level) {
						bk.imposter_mm->set_instance_transform(vis, bk.instances[i].xform * bk.imposter_local);
						vis++;
					}
				}
				bk.imposter_mm->set_visible_instance_count(vis);
			}

		// ------------------------------------------------------- INTERACTOR
		} else if (cat == FoliageType::TYPE_INTERACTOR) {
			for (int i = 0; i < bk.scenes.size() && i < inst_count; i++) {
				if (!bk.scenes[i]) continue;
				Vector3 wp   = p_cell_world_origin + bk.instances[i].xform.origin;
				float   dist = wp.distance_to(p_cam_world);
				bk.scenes[i]->set_visible(!bk.type->is_culled(dist));
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Colliders
// ---------------------------------------------------------------------------

void CellFoliage::_clear_colliders() {
	for (int i = 0; i < collider_bodies.size(); i++) {
		if (collider_bodies[i]) collider_bodies[i]->queue_delete();
	}
	collider_bodies.clear();
}

void CellFoliage::_rebuild_colliders(const Vector3 &p_cell_world_origin) {
	_clear_colliders();
	for (int b = 0; b < buckets.size(); b++) {
		const TypeBucket &bk = buckets[b];
		// Only buckets that extracted shapes from their scene get colliders.
		if (bk.collision_parts.empty()) continue;
		for (int i = 0; i < bk.instances.size(); i++) {
			StaticBody *body = memnew(StaticBody);
			for (int s = 0; s < bk.collision_parts.size(); s++) {
				CollisionShape *cs = memnew(CollisionShape);
				cs->set_shape(bk.collision_parts[s].shape); // shared resource
				body->add_child(cs);
				cs->set_transform(bk.collision_parts[s].local);
			}
			add_child(body);
			body->set_transform(bk.instances[i].xform);
			collider_bodies.push_back(body);
		}
	}
}

PoolRealArray CellFoliage::collect_nav_obstacles(const Vector3 &p_cell_world_origin) const {
	// Flattened world-space (x, z, radius) triples for every instance of a type
	// that declares a nav_obstacle_radius. Returned as plain floats so the
	// terrain can hand them straight to its off-thread navmesh build.
	PoolRealArray out;
	for (int b = 0; b < buckets.size(); b++) {
		const TypeBucket &bk = buckets[b];
		if (bk.type.is_null()) continue;
		float base_r = bk.type->get_nav_obstacle_radius();
		if (base_r <= 0.0f) continue;
		for (int i = 0; i < bk.instances.size(); i++) {
			const Transform &xf = bk.instances[i].xform;
			// Instances are stored cell-local; the obstacle list is world-space.
			Vector3 wp = p_cell_world_origin + xf.origin;
			// Scale is uniform for scattered foliage, so any basis axis length
			// gives the instance scale.
			float s = xf.basis.get_axis(0).length();
			if (s <= 0.0f) s = 1.0f;
			out.push_back(wp.x);
			out.push_back(wp.z);
			out.push_back(base_r * s);
		}
	}
	return out;
}

void CellFoliage::set_collision_active(bool p_active, const Vector3 &p_cell_world_origin) {
	if (p_active == collision_active) return;
	collision_active = p_active;
	if (collision_active) _rebuild_colliders(p_cell_world_origin);
	else                  _clear_colliders();
}

bool CellFoliage::has_persistent_instances() const {
	for (int b = 0; b < buckets.size(); b++) {
		if (buckets[b].type.is_valid()
				&& buckets[b].type->is_persistent()
				&& !buckets[b].instances.empty()) {
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

PoolRealArray CellFoliage::serialize() const {
	PoolRealArray out;
	out.push_back((float)buckets.size());
	for (int b = 0; b < buckets.size(); b++) {
		const TypeBucket &bk = buckets[b];
		uint32_t h = bk.type.is_valid() ? _name_hash(bk.type->get_type_name()) : 0;
		float hf; memcpy(&hf, &h, sizeof(hf));
		out.push_back(hf);
		out.push_back((float)bk.instances.size());
		for (int i = 0; i < bk.instances.size(); i++) {
			const Transform &t  = bk.instances[i].xform;
			const Basis     &ba = t.basis;
			out.push_back(ba.elements[0].x); out.push_back(ba.elements[0].y); out.push_back(ba.elements[0].z);
			out.push_back(ba.elements[1].x); out.push_back(ba.elements[1].y); out.push_back(ba.elements[1].z);
			out.push_back(ba.elements[2].x); out.push_back(ba.elements[2].y); out.push_back(ba.elements[2].z);
			out.push_back(t.origin.x);       out.push_back(t.origin.y);       out.push_back(t.origin.z);
		}
	}
	return out;
}

void CellFoliage::deserialize(const PoolRealArray &p_data) {
	for (int b = 0; b < buckets.size(); b++)
		buckets.write[b].instances.clear();

	int n = p_data.size();
	if (n < 1) return;

	PoolRealArray::Read r = p_data.read();
	int p = 0;
	int bucket_count = (int)r[p++];
	for (int b = 0; b < bucket_count && p < n; b++) {
		float hf = r[p++]; uint32_t h; memcpy(&h, &hf, sizeof(h));
		int icount = (int)r[p++];
		int local = -1;
		for (int j = 0; j < buckets.size(); j++) {
			if (buckets[j].type.is_valid() && _name_hash(buckets[j].type->get_type_name()) == h) {
				local = j;
				break;
			}
		}
		for (int i = 0; i < icount; i++) {
			if (p + 12 > n) return;
			Transform t;
			t.basis.elements[0] = Vector3(r[p+0], r[p+1], r[p+2]);
			t.basis.elements[1] = Vector3(r[p+3], r[p+4], r[p+5]);
			t.basis.elements[2] = Vector3(r[p+6], r[p+7], r[p+8]);
			t.origin = Vector3(r[p+9], r[p+10], r[p+11]);
			p += 12;
			if (local >= 0) {
				Instance inst;
				inst.xform = t;
				buckets.write[local].instances.push_back(inst);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Notifications / bind
// ---------------------------------------------------------------------------

void CellFoliage::_notification(int p_what) {
	if (p_what == NOTIFICATION_EXIT_TREE)
		_clear_colliders();
}

void CellFoliage::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_cell_size", "s"),              &CellFoliage::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"),                   &CellFoliage::get_cell_size);
	ClassDB::bind_method(D_METHOD("rebuild"),                         &CellFoliage::rebuild);
	ClassDB::bind_method(D_METHOD("clear_all"),                       &CellFoliage::clear_all);
	ClassDB::bind_method(D_METHOD("get_type_count"),                  &CellFoliage::get_type_count);
	ClassDB::bind_method(D_METHOD("get_instance_count", "type_index"), &CellFoliage::get_instance_count);
}
