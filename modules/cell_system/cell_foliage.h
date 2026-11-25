#ifndef CELL_FOLIAGE_H
#define CELL_FOLIAGE_H

#include "core/reference.h"
#include "core/vector.h"
#include "scene/3d/spatial.h"
#include "scene/3d/multimesh_instance.h"
#include "scene/resources/mesh.h"
#include "scene/resources/multimesh.h"
#include "scene/resources/shape.h"
#include "foliage_type.h"

class StaticBody;
class CollisionShape;
class Camera;

// One cell's worth of foliage. Owned by FoliageManager (added as its child),
// placed at the cell's world origin so local instance transforms map correctly.
//
// All LOD rendering (grass and tree) uses MultiMesh for scalability.
// For TYPE_TREE, meshes are extracted from tree_scene at rebuild() time by
// scanning for children named with "-lod{n}" / "-imposter" suffixes.
// TYPE_INTERACTOR spawns one scene node per instance.
class CellFoliage : public Spatial {
	GDCLASS(CellFoliage, Spatial);

public:
	struct Instance {
		Transform xform;
	};

	// One LOD level can be made of several sub-meshes (e.g. a tree LOD is
	// bark + leaf, each with its own material). Each sub-mesh gets its own
	// MultiMesh; they all share the same per-instance coverage value.
	struct LodLevel {
		Vector<MultiMeshInstance *> mmis;
		Vector<Ref<MultiMesh>>      mms;
		// Per-sub-mesh transform relative to the foliage instance origin
		// (identity for grass; bark/leaf offset for trees). Parallel to mms.
		Vector<Transform>           locals;
	};

	// A collision shape extracted from a tree scene, with its transform baked
	// relative to the tree root. The Ref<Shape> is shared across all instances;
	// only the per-instance StaticBody is unique.
	struct CollisionPart {
		Ref<Shape> shape;
		Transform  local;
	};

	struct TypeBucket {
		Ref<FoliageType> type;
		Vector<Instance> instances;

		// Actual number of LOD levels built for this bucket.
		// For GRASS: number of valid grass_mesh slots used.
		// For TREE:  number of distinct -lod{n} levels extracted from tree_scene.
		int lod_count = 0;

		// GRASS + TREE: one LodLevel per LOD (each holds 1+ sub-mesh MultiMeshes).
		Vector<LodLevel> lods;

		// TREE only: imposter extracted from the -imposter child, rendered
		// as a MultiMesh. Never culled by max_render_distance.
		bool               has_imposter = false;
		MultiMeshInstance *imposter_mmi = nullptr;
		Ref<MultiMesh>     imposter_mm;
		Transform          imposter_local;

		// INTERACTOR: one spawned scene node per instance.
		Vector<Spatial *> scenes;

		// TREE: collision shapes extracted from the scene, instanced near the
		// player as StaticBody nodes (one body per foliage instance).
		Vector<CollisionPart> collision_parts;

		Vector<int> instance_lod;
	};

private:
	float cell_size        = 64.0f;
	bool  collision_active = false;
	// When true, only buckets of keep_loaded types build render nodes; the rest
	// (e.g. grass) keep their instance data but render nothing. Used by the
	// manager to load tree cells far from the camera without loading grass.
	bool  _persistent_only = false;

	Vector<TypeBucket>   buckets;
	Vector<StaticBody *> collider_bodies;
	bool _built = false;

	void _clear_render();
	void _clear_colliders();
	void _build_render_nodes();
	void _rebuild_colliders(const Vector3 &p_cell_world_origin);
	int  _bucket_for(const Ref<FoliageType> &p_type);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void  set_cell_size(float s) { cell_size = s; }
	float get_cell_size() const  { return cell_size; }

	// Persistent-only mode: render only keep_loaded buckets (trees), not grass.
	void  set_persistent_only(bool v) { _persistent_only = v; }
	bool  get_persistent_only() const { return _persistent_only; }

	void configure_types(const Vector<Ref<FoliageType>> &p_types);

	void add_instance(int p_type_index, const Transform &p_local_xform);
	void clear_type(int p_type_index);
	void clear_all();
	int  get_type_count() const     { return buckets.size(); }
	int  get_instance_count(int p_type_index) const;

	int erase_in_radius(int p_type_index, const Vector3 &p_world_center,
			const Vector3 &p_cell_world_origin, float p_radius);

	void rebuild();

	void update_lod(const Vector3 &p_cam_world, const Vector3 &p_cell_world_origin);

	void set_collision_active(bool p_active, const Vector3 &p_cell_world_origin);
	bool get_collision_active() const { return collision_active; }

	// True if any bucket has instances of a type with keep_loaded enabled.
	bool has_persistent_instances() const;

	// World-space (x, z, radius) triples for instances whose FoliageType sets
	// nav_obstacle_radius - fed to the terrain so the navmesh routes around them.
	PoolRealArray collect_nav_obstacles(const Vector3 &p_cell_world_origin) const;

	PoolRealArray serialize() const;
	void          deserialize(const PoolRealArray &p_data);

	CellFoliage();
	~CellFoliage();
};

#endif // CELL_FOLIAGE_H
