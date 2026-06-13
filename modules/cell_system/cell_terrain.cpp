#include "cell_terrain.h"

#include "scene/3d/collision_shape.h"
#include "scene/3d/mesh_instance.h"
#include "scene/3d/physics_body.h"          // StaticBody
#include "scene/resources/height_map_shape.h"

CellTerrain::CellTerrain() {}
CellTerrain::~CellTerrain() {}

void CellTerrain::set_heights(const PoolRealArray &p_heights) { heights = p_heights; }
PoolRealArray CellTerrain::get_heights() const { return heights; }

void CellTerrain::set_verts_per_side(int p_v) { verts_per_side = MAX(2, p_v); }
int CellTerrain::get_verts_per_side() const { return verts_per_side; }

void CellTerrain::set_cell_size(float p_s) { cell_size = p_s; }
float CellTerrain::get_cell_size() const { return cell_size; }

void CellTerrain::set_height_scale(float p_s) { height_scale = p_s; }
float CellTerrain::get_height_scale() const { return height_scale; }

void CellTerrain::set_neighbor_edges(const PoolRealArray &n, const PoolRealArray &s,
		const PoolRealArray &e, const PoolRealArray &w) {
	edge_n = n; edge_s = s; edge_e = e; edge_w = w;
}

float CellTerrain::get_height_at(int lx, int lz) const {
	lx = CLAMP(lx, 0, verts_per_side - 1);
	lz = CLAMP(lz, 0, verts_per_side - 1);
	return heights[lz * verts_per_side + lx]; // raw; height_scale applied at build
}

void CellTerrain::set_height_at(int lx, int lz, float h) {
	if (lx < 0 || lx >= verts_per_side || lz < 0 || lz >= verts_per_side) return;
	PoolRealArray::Write w = heights.write();
	w[lz * verts_per_side + lx] = h;
}

float CellTerrain::_height_at(int x, int z) const {
	const int vps = verts_per_side;
	// Use neighbour edge rows just outside this cell so seam normals match;
	// fall back to clamping when no neighbour has been supplied.
	if (x < 0  && edge_w.size() == vps) return edge_w[CLAMP(z, 0, vps - 1)] * height_scale;
	if (x >= vps && edge_e.size() == vps) return edge_e[CLAMP(z, 0, vps - 1)] * height_scale;
	if (z < 0  && edge_s.size() == vps) return edge_s[CLAMP(x, 0, vps - 1)] * height_scale;
	if (z >= vps && edge_n.size() == vps) return edge_n[CLAMP(x, 0, vps - 1)] * height_scale;
	int cx = CLAMP(x, 0, vps - 1);
	int cz = CLAMP(z, 0, vps - 1);
	return heights[cz * vps + cx] * height_scale;
}

Vector3 CellTerrain::_sample_normal(int x, int z) const {
	const float spacing = cell_size / (float)(verts_per_side - 1);
	float hl = _height_at(x - 1, z);
	float hr = _height_at(x + 1, z);
	float hd = _height_at(x, z - 1);
	float hu = _height_at(x, z + 1);
	Vector3 n(hl - hr, 2.0f * spacing, hd - hu);
	return n.normalized();
}

Ref<ArrayMesh> CellTerrain::_build_mesh() const {
	const int vps = verts_per_side;
	const float spacing = cell_size / (float)(vps - 1);

	PoolVector3Array vertices;
	PoolVector3Array normals;
	PoolVector2Array uvs;
	PoolIntArray indices;

	vertices.resize(vps * vps);
	normals.resize(vps * vps);
	uvs.resize(vps * vps);

	{
		PoolVector3Array::Write vw = vertices.write();
		PoolVector3Array::Write nw = normals.write();
		PoolVector2Array::Write uw = uvs.write();
		for (int z = 0; z < vps; z++) {
			for (int x = 0; x < vps; x++) {
				int i = z * vps + x;
				float y = heights[i] * height_scale;
				vw[i] = Vector3(x * spacing, y, z * spacing);
				nw[i] = _sample_normal(x, z);
				uw[i] = Vector2((float)x / (vps - 1), (float)z / (vps - 1));
			}
		}
	}

	indices.resize((vps - 1) * (vps - 1) * 6);
	{
		PoolIntArray::Write iw = indices.write();
		int t = 0;
		for (int z = 0; z < vps - 1; z++) {
			for (int x = 0; x < vps - 1; x++) {
				int i0 = z * vps + x;
				int i1 = z * vps + (x + 1);
				int i2 = (z + 1) * vps + x;
				int i3 = (z + 1) * vps + (x + 1);
				// If the surface is culled when viewed from above, swap i2/i1.
				iw[t++] = i0; iw[t++] = i2; iw[t++] = i1;
				iw[t++] = i1; iw[t++] = i2; iw[t++] = i3;
			}
		}
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_INDEX] = indices;

	Ref<ArrayMesh> mesh;
	mesh.instance();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

void CellTerrain::rebuild() {
	if (heights.size() != verts_per_side * verts_per_side) {
		return; // no valid heightmap assigned yet
	}

	if (!mesh_instance) {
		mesh_instance = memnew(MeshInstance);
		add_child(mesh_instance);
	}
	mesh_instance->set_mesh(_build_mesh());

	if (!static_body) {
		static_body = memnew(StaticBody);
		add_child(static_body);
		collision_shape = memnew(CollisionShape);
		static_body->add_child(collision_shape);
	}

	PoolRealArray map_data;
	map_data.resize(verts_per_side * verts_per_side);
	{
		PoolRealArray::Write mw = map_data.write();
		for (int i = 0; i < heights.size(); i++) {
			mw[i] = heights[i] * height_scale;
		}
	}

	Ref<HeightMapShape> shape;
	shape.instance();
	shape->set_map_width(verts_per_side);
	shape->set_map_depth(verts_per_side);
	shape->set_map_data(map_data);
	collision_shape->set_shape(shape);

	Transform xf;
	xf.origin = Vector3(cell_size * 0.5f, 0.0f, cell_size * 0.5f);
	static_body->set_transform(xf);
}

void CellTerrain::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		rebuild();
	}
}

void CellTerrain::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_heights", "heights"), &CellTerrain::set_heights);
	ClassDB::bind_method(D_METHOD("get_heights"), &CellTerrain::get_heights);
	ClassDB::bind_method(D_METHOD("set_verts_per_side", "v"), &CellTerrain::set_verts_per_side);
	ClassDB::bind_method(D_METHOD("get_verts_per_side"), &CellTerrain::get_verts_per_side);
	ClassDB::bind_method(D_METHOD("set_cell_size", "s"), &CellTerrain::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &CellTerrain::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_height_scale", "s"), &CellTerrain::set_height_scale);
	ClassDB::bind_method(D_METHOD("get_height_scale"), &CellTerrain::get_height_scale);
	ClassDB::bind_method(D_METHOD("rebuild"), &CellTerrain::rebuild);

	ADD_PROPERTY(PropertyInfo(Variant::POOL_REAL_ARRAY, "heights"), "set_heights", "get_heights");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "verts_per_side"), "set_verts_per_side", "get_verts_per_side");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "cell_size"), "set_cell_size", "get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "height_scale"), "set_height_scale", "get_height_scale");
}
