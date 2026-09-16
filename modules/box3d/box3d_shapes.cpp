#include "box3d_objects.h"
#include "box3d_types_converter.h"

#include "core/error_macros.h"
#include "core/math/math_funcs.h"

#include "box3d/collision.h"
#include "box3d/constants.h"

// Godot shape -> Box3D geometry.
//
// Axis conventions differ per shape and are easy to get wrong:
//   - Godot's CapsuleShape is Z-aligned (servers/physics/shape_sw.cpp:546) and
//     its `height` is the CYLINDRICAL SECTION only, excluding the two caps.
//   - Godot's CylinderShape is Y-aligned and its `height` is the FULL height,
//     centered on the origin.
// Getting either wrong leaves a player capsule lying on its side.

// Builds a hull within Box3D's structural limits.
//
// The binding constraint is edges, not vertices: B3_MAX_HULL_EDGES (128) means
// 256 half-edges, and a triangulated convex hull has E = 3V - 6, so the real
// ceiling is around 44 vertices -- not the 128 that B3_MAX_HULL_VERTICES
// suggests. Detailed collision meshes blow straight past it ("half edge count
// of 372 exceeds limit of 256").
//
// b3CreateHull's vertex budget is a simplification target, so stepping it down
// until the result fits keeps the shape rather than dropping it.
static b3HullData *b3_build_hull_within_limits(const Vector<b3Vec3> &p_points) {
	static const int budgets[] = { 44, 32, 24, 16, 12, 8 };

	for (int i = 0; i < 6; i++) {
		const int budget = MIN(p_points.size(), budgets[i]);
		if (budget < 4) {
			break;
		}
		if (b3HullData *hull = b3CreateHull(p_points.ptr(), p_points.size(), budget)) {
			if (i > 0) {
				WARN_PRINT_ONCE("Box3D: a convex shape had to be simplified further than "
								"expected to fit Box3D's hull limits; its collision is coarser "
								"than the source shape.");
			}
			return hull;
		}
	}
	return nullptr;
}

static b3ShapeId attach_sphere(b3BodyId p_body, const b3ShapeDef *p_def, const Variant &p_data, const Transform &p_local) {
	b3Sphere sphere;
	sphere.center = b3_from_godot(p_local.origin);
	sphere.radius = (float)(real_t)p_data;
	return b3CreateSphereShape(p_body, p_def, &sphere);
}

static b3ShapeId attach_box(b3BodyId p_body, const b3ShapeDef *p_def, const Variant &p_data, const Transform &p_local) {
	Vector3 extents = p_data;

	// b3MakeBoxHull returns a self-contained value; it must NOT be passed to
	// b3DestroyHull. b3CreateTransformedHullShape clones it, so the stack copy
	// can die at the end of this scope.
	b3BoxHull box = b3MakeBoxHull((float)extents.x, (float)extents.y, (float)extents.z);

	b3Transform xform;
	xform.p = b3_from_godot(p_local.origin);
	xform.q = b3_quat_from_godot(p_local.basis.get_rotation_quat());

	b3Vec3 scale = { 1.0f, 1.0f, 1.0f };
	return b3CreateTransformedHullShape(p_body, p_def, &box.base, xform, scale);
}

static b3ShapeId attach_capsule(b3BodyId p_body, const b3ShapeDef *p_def, const Variant &p_data, const Transform &p_local) {
	Dictionary d = p_data;
	ERR_FAIL_COND_V(!d.has("radius"), b3_nullShapeId);
	ERR_FAIL_COND_V(!d.has("height"), b3_nullShapeId);

	real_t radius = d["radius"];
	real_t height = d["height"];

	// Z-aligned, so the two sphere centers sit at +/- height/2 along local Z.
	Vector3 c1 = p_local.xform(Vector3(0, 0, height * 0.5));
	Vector3 c2 = p_local.xform(Vector3(0, 0, -height * 0.5));

	b3Capsule capsule;
	capsule.center1 = b3_from_godot(c1);
	capsule.center2 = b3_from_godot(c2);
	capsule.radius = (float)radius;
	return b3CreateCapsuleShape(p_body, p_def, &capsule);
}

static b3ShapeId attach_cylinder(b3BodyId p_body, const b3ShapeDef *p_def, const Variant &p_data, const Transform &p_local) {
	Dictionary d = p_data;
	ERR_FAIL_COND_V(!d.has("radius"), b3_nullShapeId);
	ERR_FAIL_COND_V(!d.has("height"), b3_nullShapeId);

	real_t radius = d["radius"];
	real_t height = d["height"];

	// Box3D ships b3CreateCylinder(height, radius, yOffset, sides), but the
	// meaning of yOffset is undocumented and Godot's cylinder must end up
	// centered on the origin. Generating the ring points directly removes the
	// guess: 2 * SIDES points, Y-aligned, centered — exactly Godot's convention.
	//
	// Note this is a tessellated hull, not a true cylinder. Box3D has no
	// cylinder primitive, so a rolling cylinder will settle on a facet rather
	// than roll forever. That is a real behaviour difference from Bullet and
	// belongs in the docs, not just here.
	const int SIDES = 16;
	Vector<b3Vec3> points;
	points.resize(SIDES * 2);
	b3Vec3 *pw = points.ptrw();

	const real_t half_h = height * 0.5;
	for (int i = 0; i < SIDES; i++) {
		real_t a = (Math_TAU * (real_t)i) / (real_t)SIDES;
		real_t x = Math::cos(a) * radius;
		real_t z = Math::sin(a) * radius;
		pw[i * 2 + 0] = b3_from_godot(p_local.xform(Vector3(x, half_h, z)));
		pw[i * 2 + 1] = b3_from_godot(p_local.xform(Vector3(x, -half_h, z)));
	}

	b3HullData *hull = b3_build_hull_within_limits(points);
	ERR_FAIL_NULL_V(hull, b3_nullShapeId);

	b3ShapeId id = b3CreateHullShape(p_body, p_def, hull);
	b3DestroyHull(hull); // Cloned by b3CreateHullShape.
	return id;
}

static b3ShapeId attach_convex(b3BodyId p_body, const b3ShapeDef *p_def, const Variant &p_data, const Transform &p_local) {
	Vector<Vector3> godot_points = p_data;
	ERR_FAIL_COND_V_MSG(godot_points.size() < 4, b3_nullShapeId, "Box3D: a convex shape needs at least 4 points.");

	Vector<b3Vec3> points;
	points.resize(godot_points.size());
	b3Vec3 *pw = points.ptrw();
	for (int i = 0; i < godot_points.size(); i++) {
		pw[i] = b3_from_godot(p_local.xform(godot_points[i]));
	}

	b3HullData *hull = b3_build_hull_within_limits(points);
	if (!hull) {
		WARN_PRINT_ONCE("Box3D: a ConvexPolygonShape was too complex to simplify into a hull; "
						"that shape has no collision. Reduce its point count.");
		return b3_nullShapeId;
	}

	b3ShapeId id = b3CreateHullShape(p_body, p_def, hull);
	b3DestroyHull(hull);
	return id;
}

bool Box3DShape::get_heightmap_dimensions(int &r_width, int &r_depth) const {
	if (type != PhysicsServer::SHAPE_HEIGHTMAP) {
		return false;
	}
	Dictionary d = data;
	if (!d.has("width") || !d.has("depth")) {
		return false;
	}
	r_width = d["width"];
	r_depth = d["depth"];
	return r_width > 1 && r_depth > 1;
}

static b3ShapeId attach_heightmap(b3BodyId p_body, const b3ShapeDef *p_def, Box3DShape *p_shape,
		const Vector3 &p_scale, Box3DShapeEntry *r_owner) {
	// Deliberately NOT cached on the shape. The grid spacing is baked into the
	// blob via b3HeightFieldDef::scale, so a cached blob silently keeps the scale
	// it was first built with -- which is how CellTerrain ended up colliding at
	// one unit per grid step after applying its real spacing.
	Dictionary d = p_shape->data;
	ERR_FAIL_COND_V(!d.has("width") || !d.has("depth") || !d.has("heights"), b3_nullShapeId);

	const int width = d["width"];
	const int depth = d["depth"];
	PoolRealArray heights = d["heights"];
	ERR_FAIL_COND_V(width <= 1 || depth <= 1, b3_nullShapeId);
	ERR_FAIL_COND_V(heights.size() != width * depth, b3_nullShapeId);

	// Box3D quantizes heights into uint16 between a global min and max, so those
	// bounds must be supplied. Godot hands them over when it has them; otherwise
	// scan. Neighbouring cells sharing bounds is what makes their edges line up
	// exactly, which matters directly for streamed terrain — every CellTerrain
	// must agree on this range or seams open up between cells.
	float min_height = 0.0f;
	float max_height = 0.0f;
	if (d.has("min_height") && d.has("max_height")) {
		min_height = (real_t)d["min_height"];
		max_height = (real_t)d["max_height"];
	} else {
		PoolRealArray::Read r = heights.read();
		for (int i = 0; i < heights.size(); i++) {
			if (r[i] < min_height) {
				min_height = r[i];
			} else if (r[i] > max_height) {
				max_height = r[i];
			}
		}
	}
	ERR_FAIL_COND_V(min_height > max_height, b3_nullShapeId);

	// Box3D wants a plain float array; Godot's PoolRealArray is real_t, which is
	// float in a single-precision build but must still be copied explicitly.
	Vector<float> flat;
	flat.resize(heights.size());
	{
		PoolRealArray::Read r = heights.read();
		float *w = flat.ptrw();
		for (int i = 0; i < heights.size(); i++) {
			w[i] = (float)r[i];
		}
	}

	b3HeightFieldDef def = {};
	def.heights = flat.ptrw();
	def.materialIndices = nullptr;
	// Godot's HeightMapShape always has a one-unit grid; real spacing comes from
	// the scale on the shape's transform. CellTerrain relies on exactly this --
	// it scales its StaticBody by cell_size/(verts_per_side-1)
	// (cell_terrain.cpp:661) -- so assuming 1 collapses every terrain cell to a
	// tiny patch and the player falls straight through.
	const Vector3 scale = p_scale;
	ERR_FAIL_COND_V_MSG(scale.x <= 0 || scale.y <= 0 || scale.z <= 0, b3_nullShapeId,
			"Box3D: height field scale must be positive on every axis.");
	def.scale = { (float)scale.x, (float)scale.y, (float)scale.z };
	def.countX = width;
	def.countZ = depth;
	def.globalMinimumHeight = min_height;
	def.globalMaximumHeight = max_height;
	def.clockwiseWinding = false;

	// b3CreateHeightField copies the def ("No pointers are held to this data"),
	// but b3CreateHeightFieldShape does NOT copy the result — so the blob is
	// owned by the shape and released in free_geometry().
	//
	// Built once and shared by every body this shape is attached to. Freeing it
	// here on each attach would dangle any b3 shape another body still holds,
	// so invalidation is driven from shape_set_data(), which tears down every
	// referencing b3 shape first.
	b3HeightFieldData *blob = b3CreateHeightField(&def);
	ERR_FAIL_NULL_V(blob, b3_nullShapeId);

	b3ShapeId id = b3CreateHeightFieldShape(p_body, p_def, blob);
	if (B3_IS_NULL(id)) {
		b3DestroyHeightField(blob);
		return b3_nullShapeId;
	}

	ERR_FAIL_NULL_V(r_owner, id);
	r_owner->heightfield_data = blob;
	return id;
}

static b3ShapeId attach_concave(b3BodyId p_body, const b3ShapeDef *p_def, Box3DShape *p_shape,
		const Transform &p_local, Box3DShapeEntry *r_owner) {
	// Godot stores a ConcavePolygonShape as a flat run of triangle corners --
	// three Vector3 per triangle, no index buffer (shape_sw.cpp:_setup).
	PoolVector<Vector3> faces = p_shape->data;
	const int count = faces.size();
	if (count == 0) {
		return b3_nullShapeId; // An empty concave shape is legal; it just has no geometry.
	}
	ERR_FAIL_COND_V_MSG(count < 3 || (count % 3) != 0, b3_nullShapeId,
			"Box3D: a ConcavePolygonShape needs a multiple of 3 vertices.");

	Vector<b3Vec3> verts;
	Vector<int32_t> indices;
	verts.resize(count);
	indices.resize(count);
	{
		PoolVector<Vector3>::Read r = faces.read();
		b3Vec3 *vw = verts.ptrw();
		int32_t *iw = indices.ptrw();
		for (int i = 0; i < count; i++) {
			// b3CreateMeshShape takes a scale but no transform, so the shape's
			// local placement is baked into the vertices instead. Unlike a height
			// field, a mesh has arbitrary vertices, so this costs nothing and
			// handles rotation and translation as well as scale.
			vw[i] = b3_from_godot(p_local.xform(r[i]));
			iw[i] = i;
		}
	}

	b3MeshDef def = {};
	def.vertices = verts.ptrw();
	def.stride = 0;
	def.indices = indices.ptrw();
	def.materialIndices = nullptr;
	def.weldTolerance = 0.0001f;
	def.vertexCount = count;
	def.triangleCount = count / 3;
	// The corners arrive unshared; welding rebuilds the shared edges Box3D needs
	// for adjacency, which is what stops a character catching on interior seams.
	def.weldVertices = true;
	def.useMedianSplit = false;
	def.identifyEdges = true;
	// Godot winds front faces clockwise. Box3D assumes counter-clockwise unless
	// told otherwise, and it ignores back-side collision on meshes -- so getting
	// this backwards makes a floor solid only from underneath.
	def.clockWiseWinding = true;

	b3MeshData *mesh = b3CreateMesh(&def, nullptr, 0);
	ERR_FAIL_NULL_V(mesh, b3_nullShapeId);

	const b3Vec3 unit_scale = { 1.0f, 1.0f, 1.0f };
	b3ShapeId id = b3CreateMeshShape(p_body, p_def, mesh, unit_scale);
	if (B3_IS_NULL(id)) {
		b3DestroyMesh(mesh);
		return b3_nullShapeId;
	}

	ERR_FAIL_NULL_V(r_owner, id);
	r_owner->mesh_data = mesh;
	return id;
}

b3ShapeId Box3DShape::attach(b3BodyId p_body, const b3ShapeDef *p_def, const Transform &p_local_xform, Box3DShapeEntry *r_owner) {
	switch (type) {
		case PhysicsServer::SHAPE_SPHERE:
			return attach_sphere(p_body, p_def, data, p_local_xform);
		case PhysicsServer::SHAPE_BOX:
			return attach_box(p_body, p_def, data, p_local_xform);
		case PhysicsServer::SHAPE_CAPSULE:
			return attach_capsule(p_body, p_def, data, p_local_xform);
		case PhysicsServer::SHAPE_CYLINDER:
			return attach_cylinder(p_body, p_def, data, p_local_xform);
		case PhysicsServer::SHAPE_CONVEX_POLYGON:
			return attach_convex(p_body, p_def, data, p_local_xform);

		case PhysicsServer::SHAPE_CONCAVE_POLYGON:
			// Box3D only generates mesh contacts against static bodies, which
			// matches Godot's own restriction on concave shapes.
			return attach_concave(p_body, p_def, this, p_local_xform, r_owner);

		case PhysicsServer::SHAPE_HEIGHTMAP:
			// Only the SCALE of the local transform is used here. Box3D anchors a
			// height field at the body origin and offers no per-shape transform,
			// so rotation and translation are applied to the body instead (see
			// Box3DBody::get_solver_transform). Static bodies only.
			return attach_heightmap(p_body, p_def, this, p_local_xform.basis.get_scale(), r_owner);

		case PhysicsServer::SHAPE_PLANE:
			WARN_PRINT_ONCE("Box3D: PlaneShape has no Box3D equivalent (use a large box for a floor).");
			return b3_nullShapeId;

		case PhysicsServer::SHAPE_RAY:
			WARN_PRINT_ONCE("Box3D: RayShape has no Box3D equivalent.");
			return b3_nullShapeId;

		default:
			return b3_nullShapeId;
	}
}

// Point-cloud proxies for shape casts and overlap queries.
//
// Box3D expresses every castable convex shape as a point cloud plus an external
// radius, so a sphere is one point with a radius and a box is eight points with
// none. Points are world-space: the caller passes the query transform in, which
// is what keeps the cast precise far from the origin.
bool Box3DShape::build_proxy(Vector<b3Vec3> &r_points, const Transform &p_xform, b3ShapeProxy *r_proxy) const {
	r_points.clear();

	switch (type) {
		case PhysicsServer::SHAPE_SPHERE: {
			r_points.push_back(b3_from_godot(p_xform.origin));
			r_proxy->radius = (float)(real_t)data;
		} break;

		case PhysicsServer::SHAPE_BOX: {
			Vector3 e = data;
			for (int i = 0; i < 8; i++) {
				Vector3 corner(
						(i & 1) ? e.x : -e.x,
						(i & 2) ? e.y : -e.y,
						(i & 4) ? e.z : -e.z);
				r_points.push_back(b3_from_godot(p_xform.xform(corner)));
			}
			r_proxy->radius = 0.0f;
		} break;

		case PhysicsServer::SHAPE_CAPSULE: {
			Dictionary d = data;
			ERR_FAIL_COND_V(!d.has("radius") || !d.has("height"), false);
			real_t radius = d["radius"];
			real_t height = d["height"];
			// Z-aligned, matching CapsuleShapeSW.
			r_points.push_back(b3_from_godot(p_xform.xform(Vector3(0, 0, height * 0.5))));
			r_points.push_back(b3_from_godot(p_xform.xform(Vector3(0, 0, -height * 0.5))));
			r_proxy->radius = (float)radius;
		} break;

		case PhysicsServer::SHAPE_CYLINDER: {
			Dictionary d = data;
			ERR_FAIL_COND_V(!d.has("radius") || !d.has("height"), false);
			real_t radius = d["radius"];
			real_t half_h = (real_t)d["height"] * 0.5;
			const int SIDES = 8; // Half the solid's tessellation; proxies are point-count bound.
			for (int i = 0; i < SIDES; i++) {
				real_t a = (Math_TAU * (real_t)i) / (real_t)SIDES;
				real_t x = Math::cos(a) * radius;
				real_t z = Math::sin(a) * radius;
				r_points.push_back(b3_from_godot(p_xform.xform(Vector3(x, half_h, z))));
				r_points.push_back(b3_from_godot(p_xform.xform(Vector3(x, -half_h, z))));
			}
			r_proxy->radius = 0.0f;
		} break;

		case PhysicsServer::SHAPE_CONVEX_POLYGON: {
			Vector<Vector3> pts = data;
			ERR_FAIL_COND_V(pts.size() < 4, false);
			for (int i = 0; i < pts.size(); i++) {
				r_points.push_back(b3_from_godot(p_xform.xform(pts[i])));
			}
			r_proxy->radius = 0.0f;
		} break;

		default:
			// Concave, heightmap, plane and ray are not convex point clouds and
			// cannot be cast this way — they are only ever cast *against*.
			return false;
	}

	// The point budget is a hard limit in Box3D, not a hint: overrunning it
	// corrupts the cast rather than degrading it.
	ERR_FAIL_COND_V_MSG(r_points.size() > B3_MAX_SHAPE_CAST_POINTS, false,
			"Box3D: shape proxy exceeds B3_MAX_SHAPE_CAST_POINTS.");

	r_proxy->points = r_points.ptr();
	r_proxy->count = r_points.size();
	return true;
}
