/* mesh_packer.cpp */
#include "mesh_packer.h"

#include "core/engine.h"
#include "scene/3d/mesh_instance.h"
#include "scene/3d/multimesh_instance.h"
#include "scene/3d/skeleton.h"
#include "scene/resources/multimesh.h"

const char *MeshPacker::OUTPUT_NODE_NAME = "PackedOutput";
const char *MeshPacker::OUTPUT_GROUP = "__mesh_packer_output";

/* -------------------------------------------------------------------------- */
/* Key ordering                                                               */
/* -------------------------------------------------------------------------- */

bool MeshPacker::InstanceSettings::operator<(const InstanceSettings &p_other) const {
	if (cast_shadows != p_other.cast_shadows) {
		return cast_shadows < p_other.cast_shadows;
	}
	if (layer_mask != p_other.layer_mask) {
		return layer_mask < p_other.layer_mask;
	}
	if (use_baked_light != p_other.use_baked_light) {
		return (int)use_baked_light < (int)p_other.use_baked_light;
	}
	return material_overlay < p_other.material_overlay;
}

bool MeshPacker::CellCoord::operator<(const CellCoord &p_other) const {
	if (x != p_other.x) {
		return x < p_other.x;
	}
	if (y != p_other.y) {
		return y < p_other.y;
	}
	return z < p_other.z;
}

bool MeshPacker::MergeKey::operator<(const MergeKey &p_other) const {
	if (material != p_other.material) {
		return material < p_other.material;
	}
	if (settings < p_other.settings) {
		return true;
	}
	if (p_other.settings < settings) {
		return false;
	}
	return cell < p_other.cell;
}

bool MeshPacker::InstanceKey::operator<(const InstanceKey &p_other) const {
	if (mesh != p_other.mesh) {
		return mesh < p_other.mesh;
	}
	if (material_override != p_other.material_override) {
		return material_override < p_other.material_override;
	}
	if (surface_materials.size() != p_other.surface_materials.size()) {
		return surface_materials.size() < p_other.surface_materials.size();
	}
	for (int i = 0; i < surface_materials.size(); i++) {
		if (surface_materials[i] != p_other.surface_materials[i]) {
			return surface_materials[i] < p_other.surface_materials[i];
		}
	}
	if (settings < p_other.settings) {
		return true;
	}
	if (p_other.settings < settings) {
		return false;
	}
	return cell < p_other.cell;
}

/* -------------------------------------------------------------------------- */
/* Source discovery                                                           */
/* -------------------------------------------------------------------------- */

Spatial *MeshPacker::_find_output() const {
	for (int i = 0; i < get_child_count(); i++) {
		Node *child = get_child(i);
		if (child->is_in_group(OUTPUT_GROUP) && !child->is_queued_for_deletion()) {
			return Object::cast_to<Spatial>(child);
		}
	}
	return nullptr;
}

Node *MeshPacker::_serialization_owner() const {
	// Generated nodes only need an owner in the editor, where they have to be
	// written into the .tscn so the game does not pay the packing cost on load.
	if (!Engine::get_singleton()->is_editor_hint()) {
		return nullptr;
	}
	return get_owner() ? get_owner() : const_cast<MeshPacker *>(this);
}

bool MeshPacker::_is_packable(MeshInstance *p_mi) const {
	if (!p_mi->is_visible()) {
		return false; // Deliberately hidden by the user: leave it alone.
	}
	Ref<Mesh> mesh = p_mi->get_mesh();
	if (mesh.is_null() || mesh->get_surface_count() == 0) {
		return false;
	}
	if (mesh->get_blend_shape_count() > 0) {
		return false; // Blend shapes need their own surfaces.
	}
	// MeshInstance defaults its skeleton path to "..", so the path only means
	// anything if it actually resolves to a Skeleton.
	if (!p_mi->get_skeleton_path().is_empty() &&
			Object::cast_to<Skeleton>(p_mi->get_node_or_null(p_mi->get_skeleton_path()))) {
		return false; // Skinned: the vertices move at runtime.
	}
	return true;
}

void MeshPacker::_collect_sources(Node *p_node, LocalVector<MeshInstance *> &r_sources) const {
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *child = p_node->get_child(i);
		if (child->is_in_group(OUTPUT_GROUP)) {
			continue; // Our own output, or a stale one waiting to be freed.
		}
		if (Object::cast_to<MeshPacker>(child)) {
			continue; // Nested packers manage their own subtree.
		}
		MeshInstance *mi = Object::cast_to<MeshInstance>(child);
		if (mi && _is_packable(mi)) {
			r_sources.push_back(mi);
		}
		_collect_sources(child, r_sources);
	}
}

MeshPacker::InstanceSettings MeshPacker::_settings_of(MeshInstance *p_mi) const {
	InstanceSettings s;
	s.cast_shadows = (int)p_mi->get_cast_shadows_setting();
	s.layer_mask = p_mi->get_layer_mask();
	s.use_baked_light = p_mi->get_flag(GeometryInstance::FLAG_USE_BAKED_LIGHT);
	Ref<Material> overlay = p_mi->get_material_overlay();
	s.material_overlay = overlay.is_valid() ? overlay->get_instance_id() : 0;
	return s;
}

Vector<ObjectID> MeshPacker::_surface_materials_of(MeshInstance *p_mi) const {
	Vector<ObjectID> ids;
	int count = p_mi->get_surface_material_count();
	ids.resize(count);
	for (int i = 0; i < count; i++) {
		Ref<Material> m = p_mi->get_surface_material(i);
		ids.write[i] = m.is_valid() ? m->get_instance_id() : 0;
	}
	return ids;
}

MeshPacker::CellCoord MeshPacker::_cell_of(const Vector3 &p_local_origin) const {
	CellCoord c;
	if (cell_size <= 0.0f) {
		return c;
	}
	c.x = (int)Math::floor(p_local_origin.x / cell_size);
	c.y = (int)Math::floor(p_local_origin.y / cell_size);
	c.z = (int)Math::floor(p_local_origin.z / cell_size);
	return c;
}

/* -------------------------------------------------------------------------- */
/* Geometry accumulation                                                      */
/* -------------------------------------------------------------------------- */

void MeshPacker::_append_surface(MergeSurface &r_surface, const Array &p_arrays, const Transform &p_xform) const {
	PoolVector<Vector3> verts = p_arrays[Mesh::ARRAY_VERTEX];
	const int vertex_count = verts.size();
	if (vertex_count == 0) {
		return;
	}

	PoolVector<Vector3> normals = p_arrays[Mesh::ARRAY_NORMAL];
	PoolVector<real_t> tangents = p_arrays[Mesh::ARRAY_TANGENT];
	PoolVector<Color> colors = p_arrays[Mesh::ARRAY_COLOR];
	PoolVector<Vector2> uvs = p_arrays[Mesh::ARRAY_TEX_UV];
	PoolVector<Vector2> uv2s = p_arrays[Mesh::ARRAY_TEX_UV2];
	PoolVector<int> indices = p_arrays[Mesh::ARRAY_INDEX];

	const uint32_t base = r_surface.vertices.size();
	const Basis normal_basis = p_xform.basis.inverse().transposed();
	// A mirrored (negative determinant) transform reverses triangle winding, so
	// the faces have to be flipped back or the batch culls inside out.
	const bool flip_winding = p_xform.basis.determinant() < 0.0f;

	r_surface.channels |= Mesh::ARRAY_FORMAT_VERTEX;

	{
		PoolVector<Vector3>::Read r = verts.read();
		r_surface.vertices.resize(base + vertex_count);
		for (int i = 0; i < vertex_count; i++) {
			r_surface.vertices[base + i] = p_xform.xform(r[i]);
		}
	}

	// Every channel is kept in lockstep with the vertex array and sources that
	// lack one get a neutral default, so meshes with different vertex formats
	// can still share a surface.
	r_surface.normals.resize(base + vertex_count);
	if (normals.size() == vertex_count) {
		r_surface.channels |= Mesh::ARRAY_FORMAT_NORMAL;
		PoolVector<Vector3>::Read r = normals.read();
		for (int i = 0; i < vertex_count; i++) {
			r_surface.normals[base + i] = normal_basis.xform(r[i]).normalized();
		}
	} else {
		for (int i = 0; i < vertex_count; i++) {
			r_surface.normals[base + i] = Vector3(0, 1, 0);
		}
	}

	r_surface.tangents.resize((base + vertex_count) * 4);
	if (tangents.size() == vertex_count * 4) {
		r_surface.channels |= Mesh::ARRAY_FORMAT_TANGENT;
		PoolVector<real_t>::Read r = tangents.read();
		for (int i = 0; i < vertex_count; i++) {
			const int src = i * 4;
			Vector3 t = normal_basis.xform(Vector3(r[src], r[src + 1], r[src + 2])).normalized();
			const uint32_t dst = (base + i) * 4;
			r_surface.tangents[dst + 0] = t.x;
			r_surface.tangents[dst + 1] = t.y;
			r_surface.tangents[dst + 2] = t.z;
			// The binormal sign follows the handedness of the transform.
			r_surface.tangents[dst + 3] = flip_winding ? -r[src + 3] : r[src + 3];
		}
	} else {
		for (int i = 0; i < vertex_count; i++) {
			const uint32_t dst = (base + i) * 4;
			r_surface.tangents[dst + 0] = 1.0f;
			r_surface.tangents[dst + 1] = 0.0f;
			r_surface.tangents[dst + 2] = 0.0f;
			r_surface.tangents[dst + 3] = 1.0f;
		}
	}

	r_surface.colors.resize(base + vertex_count);
	if (colors.size() == vertex_count) {
		r_surface.channels |= Mesh::ARRAY_FORMAT_COLOR;
		PoolVector<Color>::Read r = colors.read();
		for (int i = 0; i < vertex_count; i++) {
			r_surface.colors[base + i] = r[i];
		}
	} else {
		for (int i = 0; i < vertex_count; i++) {
			r_surface.colors[base + i] = Color(1, 1, 1, 1);
		}
	}

	r_surface.uvs.resize(base + vertex_count);
	if (uvs.size() == vertex_count) {
		r_surface.channels |= Mesh::ARRAY_FORMAT_TEX_UV;
		PoolVector<Vector2>::Read r = uvs.read();
		for (int i = 0; i < vertex_count; i++) {
			r_surface.uvs[base + i] = r[i];
		}
	} else {
		for (int i = 0; i < vertex_count; i++) {
			r_surface.uvs[base + i] = Vector2();
		}
	}

	r_surface.uv2s.resize(base + vertex_count);
	if (uv2s.size() == vertex_count) {
		r_surface.channels |= Mesh::ARRAY_FORMAT_TEX_UV2;
		PoolVector<Vector2>::Read r = uv2s.read();
		for (int i = 0; i < vertex_count; i++) {
			r_surface.uv2s[base + i] = r[i];
		}
	} else {
		for (int i = 0; i < vertex_count; i++) {
			r_surface.uv2s[base + i] = Vector2();
		}
	}

	const uint32_t index_base = r_surface.indices.size();
	if (indices.size() > 0) {
		const int index_count = indices.size();
		PoolVector<int>::Read r = indices.read();
		r_surface.indices.resize(index_base + index_count);
		if (flip_winding && (index_count % 3) == 0) {
			for (int i = 0; i < index_count; i += 3) {
				r_surface.indices[index_base + i + 0] = base + r[i + 2];
				r_surface.indices[index_base + i + 1] = base + r[i + 1];
				r_surface.indices[index_base + i + 2] = base + r[i + 0];
			}
		} else {
			for (int i = 0; i < index_count; i++) {
				r_surface.indices[index_base + i] = base + r[i];
			}
		}
	} else {
		// Non-indexed source: indices become mandatory once several sources
		// share a surface, so generate the implicit sequential ones.
		r_surface.indices.resize(index_base + vertex_count);
		if (flip_winding && (vertex_count % 3) == 0) {
			for (int i = 0; i < vertex_count; i += 3) {
				r_surface.indices[index_base + i + 0] = base + i + 2;
				r_surface.indices[index_base + i + 1] = base + i + 1;
				r_surface.indices[index_base + i + 2] = base + i + 0;
			}
		} else {
			for (int i = 0; i < vertex_count; i++) {
				r_surface.indices[index_base + i] = base + i;
			}
		}
	}
}

/* -------------------------------------------------------------------------- */
/* Output                                                                     */
/* -------------------------------------------------------------------------- */

void MeshPacker::_adopt(Spatial *p_output, Node *p_child, Node *p_owner) const {
	p_output->add_child(p_child);
	if (p_owner) {
		p_child->set_owner(p_owner);
	}
}

void MeshPacker::_copy_instance_settings(MeshInstance *p_from, GeometryInstance *p_to) const {
	p_to->set_cast_shadows_setting(p_from->get_cast_shadows_setting());
	p_to->set_layer_mask(p_from->get_layer_mask());
	p_to->set_flag(GeometryInstance::FLAG_USE_BAKED_LIGHT, p_from->get_flag(GeometryInstance::FLAG_USE_BAKED_LIGHT));
	p_to->set_material_overlay(p_from->get_material_overlay());
}

void MeshPacker::_emit_merge_batch(Spatial *p_output, const MergeBatch &p_batch, int p_index, Node *p_owner) {
	// Geometry is rebased around its own centre: it keeps float precision sane
	// far from the origin and gives the instance a tight AABB to cull against.
	AABB bounds;
	bool bounds_set = false;
	for (uint32_t s = 0; s < p_batch.surfaces.size(); s++) {
		const MergeSurface *surface = p_batch.surfaces[s];
		for (uint32_t i = 0; i < surface->vertices.size(); i++) {
			if (!bounds_set) {
				bounds.position = surface->vertices[i];
				bounds_set = true;
			} else {
				bounds.expand_to(surface->vertices[i]);
			}
		}
	}
	if (!bounds_set) {
		return;
	}
	const Vector3 centre = bounds.position + bounds.size * 0.5f;

	Ref<ArrayMesh> mesh;
	mesh.instance();

	for (uint32_t s = 0; s < p_batch.surfaces.size(); s++) {
		const MergeSurface *surface = p_batch.surfaces[s];
		const int vertex_count = (int)surface->vertices.size();
		if (vertex_count == 0) {
			continue;
		}

		uint32_t channels = surface->channels;
		if ((channels & Mesh::ARRAY_FORMAT_TANGENT) && !(channels & Mesh::ARRAY_FORMAT_NORMAL)) {
			channels &= ~(uint32_t)Mesh::ARRAY_FORMAT_TANGENT; // Meaningless without normals.
		}

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);

		{
			PoolVector<Vector3> pool;
			pool.resize(vertex_count);
			PoolVector<Vector3>::Write w = pool.write();
			for (int i = 0; i < vertex_count; i++) {
				w[i] = surface->vertices[i] - centre;
			}
			w.release();
			arrays[Mesh::ARRAY_VERTEX] = pool;
		}

		if (channels & Mesh::ARRAY_FORMAT_NORMAL) {
			PoolVector<Vector3> pool;
			pool.resize(vertex_count);
			PoolVector<Vector3>::Write w = pool.write();
			for (int i = 0; i < vertex_count; i++) {
				w[i] = surface->normals[i];
			}
			w.release();
			arrays[Mesh::ARRAY_NORMAL] = pool;
		}

		if (channels & Mesh::ARRAY_FORMAT_TANGENT) {
			PoolVector<real_t> pool;
			pool.resize(vertex_count * 4);
			PoolVector<real_t>::Write w = pool.write();
			for (int i = 0; i < vertex_count * 4; i++) {
				w[i] = surface->tangents[i];
			}
			w.release();
			arrays[Mesh::ARRAY_TANGENT] = pool;
		}

		if (channels & Mesh::ARRAY_FORMAT_COLOR) {
			PoolVector<Color> pool;
			pool.resize(vertex_count);
			PoolVector<Color>::Write w = pool.write();
			for (int i = 0; i < vertex_count; i++) {
				w[i] = surface->colors[i];
			}
			w.release();
			arrays[Mesh::ARRAY_COLOR] = pool;
		}

		if (channels & Mesh::ARRAY_FORMAT_TEX_UV) {
			PoolVector<Vector2> pool;
			pool.resize(vertex_count);
			PoolVector<Vector2>::Write w = pool.write();
			for (int i = 0; i < vertex_count; i++) {
				w[i] = surface->uvs[i];
			}
			w.release();
			arrays[Mesh::ARRAY_TEX_UV] = pool;
		}

		if (channels & Mesh::ARRAY_FORMAT_TEX_UV2) {
			PoolVector<Vector2> pool;
			pool.resize(vertex_count);
			PoolVector<Vector2>::Write w = pool.write();
			for (int i = 0; i < vertex_count; i++) {
				w[i] = surface->uv2s[i];
			}
			w.release();
			arrays[Mesh::ARRAY_TEX_UV2] = pool;
		}

		{
			const int index_count = (int)surface->indices.size();
			PoolVector<int> pool;
			pool.resize(index_count);
			PoolVector<int>::Write w = pool.write();
			for (int i = 0; i < index_count; i++) {
				w[i] = surface->indices[i];
			}
			w.release();
			arrays[Mesh::ARRAY_INDEX] = pool;
		}

		const uint32_t flags = compress_result ? (uint32_t)Mesh::ARRAY_COMPRESS_DEFAULT : 0;
		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), flags);
		const int surface_index = mesh->get_surface_count() - 1;
		if (surface_index < 0) {
			continue;
		}
		mesh->surface_set_material(surface_index, p_batch.material);
		if (p_batch.material.is_valid() && !p_batch.material->get_name().empty()) {
			mesh->surface_set_name(surface_index, p_batch.material->get_name());
		}
	}

	if (mesh->get_surface_count() == 0) {
		return;
	}

	MeshInstance *mi = memnew(MeshInstance);
	mi->set_name("Merged" + itos(p_index));
	mi->set_mesh(mesh);
	mi->set_cast_shadows_setting((GeometryInstance::ShadowCastingSetting)p_batch.settings.cast_shadows);
	mi->set_layer_mask(p_batch.settings.layer_mask);
	mi->set_flag(GeometryInstance::FLAG_USE_BAKED_LIGHT, p_batch.settings.use_baked_light);
	mi->set_material_overlay(p_batch.overlay);
	_adopt(p_output, mi, p_owner);
	mi->set_translation(centre);
}

void MeshPacker::_emit_instance_batch(Spatial *p_output, const InstanceBatch &p_batch, int p_index, Node *p_owner) {
	const uint32_t count = p_batch.transforms.size();
	if (count == 0 || !p_batch.prototype) {
		return;
	}

	Ref<Mesh> mesh = p_batch.prototype->get_mesh();
	ERR_FAIL_COND(mesh.is_null());

	// A MultiMesh has no per-surface material slots, so per-surface overrides
	// have to be baked into a copy of the mesh. Every instance in the batch
	// shares the same override set, so one copy covers all of them.
	bool has_surface_overrides = false;
	for (int i = 0; i < p_batch.prototype->get_surface_material_count(); i++) {
		if (p_batch.prototype->get_surface_material(i).is_valid()) {
			has_surface_overrides = true;
			break;
		}
	}
	if (has_surface_overrides) {
		Ref<Mesh> copy = Object::cast_to<Mesh>(*mesh->duplicate());
		if (copy.is_valid()) {
			const int overrides = MIN(p_batch.prototype->get_surface_material_count(), copy->get_surface_count());
			for (int i = 0; i < overrides; i++) {
				Ref<Material> m = p_batch.prototype->get_surface_material(i);
				if (m.is_valid()) {
					copy->surface_set_material(i, m);
				}
			}
			mesh = copy;
		}
	}

	Vector3 centre;
	for (uint32_t i = 0; i < count; i++) {
		centre += p_batch.transforms[i].origin;
	}
	centre /= (real_t)count;

	Ref<MultiMesh> multimesh;
	multimesh.instance();
	multimesh->set_transform_format(MultiMesh::TRANSFORM_3D);
	multimesh->set_color_format(MultiMesh::COLOR_NONE);
	multimesh->set_custom_data_format(MultiMesh::CUSTOM_DATA_NONE);
	multimesh->set_mesh(mesh);
	multimesh->set_instance_count(count);
	for (uint32_t i = 0; i < count; i++) {
		Transform xform = p_batch.transforms[i];
		xform.origin -= centre;
		multimesh->set_instance_transform(i, xform);
	}

	MultiMeshInstance *mmi = memnew(MultiMeshInstance);
	mmi->set_name("Instanced" + itos(p_index));
	mmi->set_multimesh(multimesh);
	mmi->set_material_override(p_batch.prototype->get_material_override());
	_copy_instance_settings(p_batch.prototype, mmi);
	_adopt(p_output, mmi, p_owner);
	mmi->set_translation(centre);
}

void MeshPacker::_apply_source_handling(MeshInstance *p_mi, bool p_allow_remove) {
	if (source_handling == SOURCE_HANDLING_KEEP) {
		return;
	}
	// Removing is only allowed at runtime, and only for leaf nodes: a source
	// MeshInstance often parents collision shapes, lights or attachment points
	// that have to outlive the geometry.
	if (source_handling == SOURCE_HANDLING_REMOVE && p_allow_remove && p_mi->get_child_count() == 0) {
		p_mi->get_parent()->remove_child(p_mi);
		p_mi->queue_delete();
		return;
	}
	hidden_sources.push_back(get_path_to(p_mi));
	p_mi->set_visible(false);
}

/* -------------------------------------------------------------------------- */
/* Packing                                                                    */
/* -------------------------------------------------------------------------- */

void MeshPacker::pack() {
	ERR_FAIL_COND_MSG(!is_inside_tree(), "MeshPacker has to be inside the scene tree to pack.");

	if (packed) {
		unpack();
	}

	LocalVector<MeshInstance *> sources;
	_collect_sources(this, sources);
	if (sources.size() == 0) {
		WARN_PRINT("MeshPacker '" + String(get_name()) + "' found no packable MeshInstance below it.");
		return;
	}

	const Transform base_inv = get_global_transform().affine_inverse();
	const bool allow_remove = !Engine::get_singleton()->is_editor_hint();
	if (source_handling == SOURCE_HANDLING_REMOVE && !allow_remove) {
		print_line("MeshPacker: source_handling is Remove, but sources are only hidden in the editor so the pack stays reversible.");
	}

	// Pass 1: how often does each (mesh, materials, settings, cell) combination
	// occur? Anything common enough is cheaper as a MultiMesh than as merged
	// geometry, because the vertex data is shared instead of duplicated.
	LocalVector<Transform> local_xforms;
	LocalVector<InstanceKey> keys;
	local_xforms.resize(sources.size());
	keys.resize(sources.size());
	Map<InstanceKey, int> counts;
	for (uint32_t i = 0; i < sources.size(); i++) {
		MeshInstance *mi = sources[i];
		const Transform local = base_inv * mi->get_global_transform();
		local_xforms[i] = local;

		InstanceKey key;
		key.mesh = mi->get_mesh()->get_instance_id();
		Ref<Material> override_material = mi->get_material_override();
		key.material_override = override_material.is_valid() ? override_material->get_instance_id() : 0;
		key.surface_materials = _surface_materials_of(mi);
		key.settings = _settings_of(mi);
		key.cell = _cell_of(local.origin);
		keys[i] = key;

		Map<InstanceKey, int>::Element *E = counts.find(key);
		if (E) {
			E->get() += 1;
		} else {
			counts.insert(key, 1);
		}
	}

	int instancing_threshold;
	switch (pack_mode) {
		case PACK_MODE_MULTIMESH: {
			instancing_threshold = 1;
		} break;
		case PACK_MODE_AUTO: {
			instancing_threshold = MAX(2, multimesh_threshold);
		} break;
		default: {
			instancing_threshold = 0x7FFFFFFF; // Merge everything.
		} break;
	}

	// Pass 2: sort every source into its batch.
	Map<InstanceKey, InstanceBatch *> instance_batches;
	Map<MergeKey, MergeBatch *> merge_batches;
	int surfaces_before = 0;
	int skipped_surfaces = 0;
	int merged_sources = 0;
	int instanced_sources = 0;

	for (uint32_t i = 0; i < sources.size(); i++) {
		MeshInstance *mi = sources[i];
		Ref<Mesh> mesh = mi->get_mesh();
		surfaces_before += mesh->get_surface_count();

		Map<InstanceKey, int>::Element *C = counts.find(keys[i]);
		if (C && C->get() >= instancing_threshold) {
			Map<InstanceKey, InstanceBatch *>::Element *E = instance_batches.find(keys[i]);
			if (!E) {
				InstanceBatch *batch = memnew(InstanceBatch);
				batch->prototype = mi;
				E = instance_batches.insert(keys[i], batch);
			}
			E->get()->transforms.push_back(local_xforms[i]);
			instanced_sources++;
			continue;
		}

		bool merged_anything = false;
		for (int s = 0; s < mesh->get_surface_count(); s++) {
			if (mesh->surface_get_primitive_type(s) != Mesh::PRIMITIVE_TRIANGLES) {
				skipped_surfaces++;
				continue;
			}
			Array arrays = mesh->surface_get_arrays(s);
			if (arrays.size() != Mesh::ARRAY_MAX) {
				skipped_surfaces++;
				continue;
			}
			PoolVector<Vector3> verts = arrays[Mesh::ARRAY_VERTEX];
			const int vertex_count = verts.size();
			if (vertex_count == 0) {
				skipped_surfaces++;
				continue;
			}

			Ref<Material> material = mi->get_active_material(s);

			MergeKey key;
			key.material = material.is_valid() ? material->get_instance_id() : 0;
			key.settings = keys[i].settings;
			key.cell = keys[i].cell;

			Map<MergeKey, MergeBatch *>::Element *E = merge_batches.find(key);
			if (!E) {
				MergeBatch *batch = memnew(MergeBatch);
				batch->material = material;
				batch->overlay = mi->get_material_overlay();
				batch->settings = keys[i].settings;
				E = merge_batches.insert(key, batch);
			}
			MergeBatch *batch = E->get();

			// Surfaces are split once they would outgrow the budget, which by
			// default keeps every surface inside the 16-bit index range.
			MergeSurface *target = batch->surfaces.size() ? batch->surfaces[batch->surfaces.size() - 1] : nullptr;
			if (!target || (vertex_budget > 0 && target->vertices.size() > 0 && (int)target->vertices.size() + vertex_count > vertex_budget)) {
				target = memnew(MergeSurface);
				batch->surfaces.push_back(target);
			}

			_append_surface(*target, arrays, local_xforms[i]);
			merged_anything = true;
		}
		if (merged_anything) {
			merged_sources++;
		}
	}

	// Pass 3: build the output nodes.
	Spatial *output = memnew(Spatial);
	output->set_name(OUTPUT_NODE_NAME);
	output->add_to_group(OUTPUT_GROUP, true);
	add_child(output);
	Node *owner = _serialization_owner();
	if (owner) {
		output->set_owner(owner);
	}

	int vertex_total = 0;
	int surfaces_after = 0;
	int index = 0;
	for (Map<MergeKey, MergeBatch *>::Element *E = merge_batches.front(); E; E = E->next()) {
		MergeBatch *batch = E->get();
		for (uint32_t s = 0; s < batch->surfaces.size(); s++) {
			vertex_total += (int)batch->surfaces[s]->vertices.size();
		}
		surfaces_after += (int)batch->surfaces.size();
		_emit_merge_batch(output, *batch, index++, owner);
		for (uint32_t s = 0; s < batch->surfaces.size(); s++) {
			memdelete(batch->surfaces[s]);
		}
		memdelete(batch);
	}

	index = 0;
	for (Map<InstanceKey, InstanceBatch *>::Element *E = instance_batches.front(); E; E = E->next()) {
		InstanceBatch *batch = E->get();
		surfaces_after += batch->prototype->get_mesh()->get_surface_count();
		_emit_instance_batch(output, *batch, index++, owner);
		memdelete(batch);
	}

	// Pass 4: get the sources out of the way.
	hidden_sources.clear();
	for (uint32_t i = 0; i < sources.size(); i++) {
		_apply_source_handling(sources[i], allow_remove);
	}

	packed = true;

	stats = Dictionary();
	stats["sources"] = (int)sources.size();
	stats["merged_sources"] = merged_sources;
	stats["instanced_sources"] = instanced_sources;
	stats["merged_meshes"] = merge_batches.size();
	stats["multimeshes"] = instance_batches.size();
	stats["surfaces_before"] = surfaces_before;
	stats["surfaces_after"] = surfaces_after;
	stats["skipped_surfaces"] = skipped_surfaces;
	stats["vertices"] = vertex_total;

	print_line("MeshPacker '" + String(get_name()) + "': " + itos(sources.size()) + " sources, " +
			itos(surfaces_before) + " surfaces -> " + itos(surfaces_after) + " (" +
			itos(merge_batches.size()) + " merged meshes, " + itos(instance_batches.size()) +
			" multimeshes, " + itos(vertex_total) + " vertices).");

	update_configuration_warning();
	_change_notify();
	emit_signal("pack_completed");
}

void MeshPacker::unpack() {
	Spatial *output = _find_output();
	if (output) {
		// Renamed first so an immediate repack can reuse the name; the group
		// tag keeps it out of source collection until the tree frees it.
		output->set_name("PackedOutputDiscarded");
		output->queue_delete();
		remove_child(output);
	}

	for (int i = 0; i < hidden_sources.size(); i++) {
		Node *node = get_node_or_null(hidden_sources[i]);
		MeshInstance *mi = Object::cast_to<MeshInstance>(node);
		if (mi) {
			mi->set_visible(true);
		}
	}
	hidden_sources.clear();

	packed = false;
	stats = Dictionary();
	update_configuration_warning();
	_change_notify();
	emit_signal("unpack_completed");
}

int MeshPacker::get_source_count() const {
	if (!is_inside_tree()) {
		return 0;
	}
	LocalVector<MeshInstance *> sources;
	_collect_sources(const_cast<MeshPacker *>(this), sources);
	return (int)sources.size();
}

Dictionary MeshPacker::get_pack_stats() const {
	return stats;
}

/* -------------------------------------------------------------------------- */
/* Properties                                                                 */
/* -------------------------------------------------------------------------- */

void MeshPacker::set_pack_mode(PackMode p_mode) {
	pack_mode = p_mode;
	_change_notify();
}

MeshPacker::PackMode MeshPacker::get_pack_mode() const {
	return pack_mode;
}

void MeshPacker::set_multimesh_threshold(int p_threshold) {
	multimesh_threshold = MAX(2, p_threshold);
}

int MeshPacker::get_multimesh_threshold() const {
	return multimesh_threshold;
}

void MeshPacker::set_source_handling(SourceHandling p_handling) {
	source_handling = p_handling;
}

MeshPacker::SourceHandling MeshPacker::get_source_handling() const {
	return source_handling;
}

void MeshPacker::set_cell_size(float p_size) {
	cell_size = MAX(0.0f, p_size);
}

float MeshPacker::get_cell_size() const {
	return cell_size;
}

void MeshPacker::set_vertex_budget(int p_budget) {
	vertex_budget = MAX(0, p_budget);
}

int MeshPacker::get_vertex_budget() const {
	return vertex_budget;
}

void MeshPacker::set_compress_result(bool p_compress) {
	compress_result = p_compress;
}

bool MeshPacker::get_compress_result() const {
	return compress_result;
}

void MeshPacker::set_pack_on_ready(bool p_enabled) {
	pack_on_ready = p_enabled;
}

bool MeshPacker::get_pack_on_ready() const {
	return pack_on_ready;
}

void MeshPacker::set_packed_state(bool p_packed) {
	packed = p_packed;
}

bool MeshPacker::is_packed() const {
	return packed;
}

void MeshPacker::set_hidden_sources(const Array &p_sources) {
	hidden_sources = p_sources;
}

Array MeshPacker::get_hidden_sources() const {
	return hidden_sources;
}

String MeshPacker::get_configuration_warning() const {
	String warning = Spatial::get_configuration_warning();
	if (packed) {
		return warning;
	}
	if (get_source_count() == 0) {
		if (!warning.empty()) {
			warning += "\n\n";
		}
		warning += TTR("MeshPacker has no visible MeshInstance below it, so there is nothing to pack.");
	}
	return warning;
}

void MeshPacker::_validate_property(PropertyInfo &property) const {
	if (property.name == "multimesh_threshold" && pack_mode != PACK_MODE_AUTO) {
		property.usage = PROPERTY_USAGE_NOEDITOR;
	}
}

void MeshPacker::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!Engine::get_singleton()->is_editor_hint() && pack_on_ready && !packed) {
				pack();
			}
		} break;
	}
}

void MeshPacker::_bind_methods() {
	ClassDB::bind_method(D_METHOD("pack"), &MeshPacker::pack);
	ClassDB::bind_method(D_METHOD("unpack"), &MeshPacker::unpack);
	ClassDB::bind_method(D_METHOD("get_source_count"), &MeshPacker::get_source_count);
	ClassDB::bind_method(D_METHOD("get_pack_stats"), &MeshPacker::get_pack_stats);

	ClassDB::bind_method(D_METHOD("set_pack_mode", "mode"), &MeshPacker::set_pack_mode);
	ClassDB::bind_method(D_METHOD("get_pack_mode"), &MeshPacker::get_pack_mode);
	ClassDB::bind_method(D_METHOD("set_multimesh_threshold", "threshold"), &MeshPacker::set_multimesh_threshold);
	ClassDB::bind_method(D_METHOD("get_multimesh_threshold"), &MeshPacker::get_multimesh_threshold);
	ClassDB::bind_method(D_METHOD("set_source_handling", "handling"), &MeshPacker::set_source_handling);
	ClassDB::bind_method(D_METHOD("get_source_handling"), &MeshPacker::get_source_handling);
	ClassDB::bind_method(D_METHOD("set_cell_size", "size"), &MeshPacker::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &MeshPacker::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_vertex_budget", "budget"), &MeshPacker::set_vertex_budget);
	ClassDB::bind_method(D_METHOD("get_vertex_budget"), &MeshPacker::get_vertex_budget);
	ClassDB::bind_method(D_METHOD("set_compress_result", "compress"), &MeshPacker::set_compress_result);
	ClassDB::bind_method(D_METHOD("get_compress_result"), &MeshPacker::get_compress_result);
	ClassDB::bind_method(D_METHOD("set_pack_on_ready", "enabled"), &MeshPacker::set_pack_on_ready);
	ClassDB::bind_method(D_METHOD("get_pack_on_ready"), &MeshPacker::get_pack_on_ready);
	ClassDB::bind_method(D_METHOD("set_packed_state", "packed"), &MeshPacker::set_packed_state);
	ClassDB::bind_method(D_METHOD("is_packed"), &MeshPacker::is_packed);
	ClassDB::bind_method(D_METHOD("set_hidden_sources", "sources"), &MeshPacker::set_hidden_sources);
	ClassDB::bind_method(D_METHOD("get_hidden_sources"), &MeshPacker::get_hidden_sources);

	ADD_GROUP("Packing", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "pack_mode", PROPERTY_HINT_ENUM, "Merge,MultiMesh,Auto"), "set_pack_mode", "get_pack_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "multimesh_threshold", PROPERTY_HINT_RANGE, "2,256,1,or_greater"), "set_multimesh_threshold", "get_multimesh_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "cell_size", PROPERTY_HINT_RANGE, "0,256,0.1,or_greater"), "set_cell_size", "get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "vertex_budget", PROPERTY_HINT_RANGE, "0,65536,1,or_greater"), "set_vertex_budget", "get_vertex_budget");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "compress_result"), "set_compress_result", "get_compress_result");

	ADD_GROUP("Sources", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "source_handling", PROPERTY_HINT_ENUM, "Hide,Remove,Keep"), "set_source_handling", "get_source_handling");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "pack_on_ready"), "set_pack_on_ready", "get_pack_on_ready");

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "packed", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR), "set_packed_state", "is_packed");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "hidden_sources", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR), "set_hidden_sources", "get_hidden_sources");

	ADD_SIGNAL(MethodInfo("pack_completed"));
	ADD_SIGNAL(MethodInfo("unpack_completed"));

	BIND_ENUM_CONSTANT(PACK_MODE_MERGE);
	BIND_ENUM_CONSTANT(PACK_MODE_MULTIMESH);
	BIND_ENUM_CONSTANT(PACK_MODE_AUTO);

	BIND_ENUM_CONSTANT(SOURCE_HANDLING_HIDE);
	BIND_ENUM_CONSTANT(SOURCE_HANDLING_REMOVE);
	BIND_ENUM_CONSTANT(SOURCE_HANDLING_KEEP);
}

MeshPacker::MeshPacker() {
	pack_mode = PACK_MODE_AUTO;
	multimesh_threshold = 8;
	source_handling = SOURCE_HANDLING_HIDE;
	cell_size = 0.0f;
	vertex_budget = 65536;
	compress_result = true;
	pack_on_ready = false;
	packed = false;
}
