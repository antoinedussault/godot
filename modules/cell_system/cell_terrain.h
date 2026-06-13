#ifndef CELL_TERRAIN_H
#define CELL_TERRAIN_H

// Complete version with neighbour-edge + per-vertex editing support folded in.
// Replaces any earlier cell_terrain.h.
#include "core/reference.h"
#include "scene/3d/spatial.h"
#include "scene/resources/mesh.h"

class MeshInstance;
class StaticBody;
class CollisionShape;

class CellTerrain : public Spatial {
	GDCLASS(CellTerrain, Spatial);

private:
	int verts_per_side = 65;
	float cell_size = 64.0f;
	float height_scale = 1.0f;

	PoolRealArray heights;
	PoolRealArray edge_n, edge_s, edge_e, edge_w; // neighbour rows for seam normals

	MeshInstance *mesh_instance = nullptr;
	StaticBody *static_body = nullptr;
	CollisionShape *collision_shape = nullptr;

	float _height_at(int x, int z) const;
	Vector3 _sample_normal(int x, int z) const;
	Ref<ArrayMesh> _build_mesh() const;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_heights(const PoolRealArray &p_heights);
	PoolRealArray get_heights() const;

	void set_verts_per_side(int p_v);
	int get_verts_per_side() const;

	void set_cell_size(float p_s);
	float get_cell_size() const;

	void set_height_scale(float p_s);
	float get_height_scale() const;

	void set_neighbor_edges(const PoolRealArray &n, const PoolRealArray &s,
			const PoolRealArray &e, const PoolRealArray &w);

	float get_height_at(int lx, int lz) const; // raw stored value
	void set_height_at(int lx, int lz, float h); // no rebuild; batch then rebuild()

	void rebuild();

	CellTerrain();
	~CellTerrain();
};

#endif // CELL_TERRAIN_H
