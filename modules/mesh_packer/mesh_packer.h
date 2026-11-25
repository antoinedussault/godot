/* mesh_packer.h */
#ifndef MESH_PACKER_H
#define MESH_PACKER_H

#include "core/local_vector.h"
#include "scene/3d/spatial.h"
#include "scene/3d/visual_instance.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"

class MeshInstance;

// Collapses every MeshInstance below this node into as few draw calls as
// possible, then parents the result under a single "PackedOutput" child.
//
// Two strategies are used, and PACK_MODE_AUTO picks per source:
//   * merge     - geometry is baked into one ArrayMesh, one surface per
//                 (material, shadow/layer settings, spatial cell). Best for
//                 many small *unique* meshes (kitbashed rocks, modular walls).
//   * multimesh - instances that share a mesh + material become a single
//                 MultiMeshInstance. Best for many *repeated* meshes, and it
//                 costs no extra vertex memory.
//
// Nothing is destroyed by default: sources are hidden, not deleted, and
// unpack() puts the scene back exactly as it was. The engine's own
// MeshInstance.merge_meshes() is a lower-level primitive (it demands identical
// surface counts, formats and materials and merges into a single surface);
// this node does the grouping, spatial splitting and node bookkeeping on top.
class MeshPacker : public Spatial {
	GDCLASS(MeshPacker, Spatial);

public:
	enum PackMode {
		PACK_MODE_MERGE,
		PACK_MODE_MULTIMESH,
		PACK_MODE_AUTO,
	};

	enum SourceHandling {
		SOURCE_HANDLING_HIDE,
		SOURCE_HANDLING_REMOVE,
		SOURCE_HANDLING_KEEP,
	};

private:
	// Per-instance renderer settings that must match for two sources to share
	// a batch, because they change how the whole instance is drawn.
	struct InstanceSettings {
		int cast_shadows;
		uint32_t layer_mask;
		bool use_baked_light;
		ObjectID material_overlay;

		bool operator<(const InstanceSettings &p_other) const;
		InstanceSettings() :
				cast_shadows(0), layer_mask(1), use_baked_light(false), material_overlay(0) {}
	};

	struct CellCoord {
		int x;
		int y;
		int z;

		bool operator<(const CellCoord &p_other) const;
		CellCoord() :
				x(0), y(0), z(0) {}
	};

	// One merged surface, accumulated in packer-local space.
	struct MergeSurface {
		LocalVector<Vector3> vertices;
		LocalVector<Vector3> normals;
		LocalVector<real_t> tangents;
		LocalVector<Color> colors;
		LocalVector<Vector2> uvs;
		LocalVector<Vector2> uv2s;
		LocalVector<int> indices;
		uint32_t channels; // Mesh::ARRAY_FORMAT_* bits actually present in a source.

		MergeSurface() :
				channels(0) {}
	};

	struct MergeKey {
		ObjectID material;
		InstanceSettings settings;
		CellCoord cell;

		bool operator<(const MergeKey &p_other) const;
		MergeKey() :
				material(0) {}
	};

	struct MergeBatch {
		Ref<Material> material;
		Ref<Material> overlay;
		InstanceSettings settings;
		LocalVector<MergeSurface *> surfaces; // Owned, freed at the end of pack().
	};

	struct InstanceKey {
		ObjectID mesh;
		ObjectID material_override;
		Vector<ObjectID> surface_materials;
		InstanceSettings settings;
		CellCoord cell;

		bool operator<(const InstanceKey &p_other) const;
		InstanceKey() :
				mesh(0), material_override(0) {}
	};

	struct InstanceBatch {
		MeshInstance *prototype;
		LocalVector<Transform> transforms;

		InstanceBatch() :
				prototype(nullptr) {}
	};

	static const char *OUTPUT_NODE_NAME;
	static const char *OUTPUT_GROUP;

	PackMode pack_mode;
	int multimesh_threshold;
	SourceHandling source_handling;
	float cell_size;
	int vertex_budget;
	bool compress_result;
	bool pack_on_ready;

	bool packed;
	Array hidden_sources; // NodePaths relative to this node, restored by unpack().
	Dictionary stats;

	Spatial *_find_output() const;
	Node *_serialization_owner() const;
	void _collect_sources(Node *p_node, LocalVector<MeshInstance *> &r_sources) const;
	bool _is_packable(MeshInstance *p_mi) const;
	InstanceSettings _settings_of(MeshInstance *p_mi) const;
	Vector<ObjectID> _surface_materials_of(MeshInstance *p_mi) const;
	CellCoord _cell_of(const Vector3 &p_local_origin) const;
	void _append_surface(MergeSurface &r_surface, const Array &p_arrays, const Transform &p_xform) const;
	void _emit_merge_batch(Spatial *p_output, const MergeBatch &p_batch, int p_index, Node *p_owner);
	void _emit_instance_batch(Spatial *p_output, const InstanceBatch &p_batch, int p_index, Node *p_owner);
	void _apply_source_handling(MeshInstance *p_mi, bool p_allow_remove);
	void _copy_instance_settings(MeshInstance *p_from, GeometryInstance *p_to) const;
	void _adopt(Spatial *p_output, Node *p_child, Node *p_owner) const;

protected:
	void _notification(int p_what);
	void _validate_property(PropertyInfo &property) const;
	static void _bind_methods();

public:
	void set_pack_mode(PackMode p_mode);
	PackMode get_pack_mode() const;

	void set_multimesh_threshold(int p_threshold);
	int get_multimesh_threshold() const;

	void set_source_handling(SourceHandling p_handling);
	SourceHandling get_source_handling() const;

	void set_cell_size(float p_size);
	float get_cell_size() const;

	void set_vertex_budget(int p_budget);
	int get_vertex_budget() const;

	void set_compress_result(bool p_compress);
	bool get_compress_result() const;

	void set_pack_on_ready(bool p_enabled);
	bool get_pack_on_ready() const;

	void set_packed_state(bool p_packed); // Storage only, written by scene loading.
	bool is_packed() const;

	void set_hidden_sources(const Array &p_sources); // Storage only.
	Array get_hidden_sources() const;

	// Bakes every descendant MeshInstance into the "PackedOutput" child.
	// Repacking is safe: an existing pack is undone first.
	void pack();
	// Deletes the packed output and restores the sources this node hid.
	void unpack();

	int get_source_count() const;
	Dictionary get_pack_stats() const;

	virtual String get_configuration_warning() const;

	MeshPacker();
};

VARIANT_ENUM_CAST(MeshPacker::PackMode);
VARIANT_ENUM_CAST(MeshPacker::SourceHandling);

#endif // MESH_PACKER_H
