#include "box3d_objects.h"
#include "box3d_types_converter.h"

#include "core/error_macros.h"
#include "core/math/math_funcs.h"

#include "box3d/collision.h"

// Godot shape -> Box3D geometry.
//
// Axis conventions differ per shape and are easy to get wrong:
//   - Godot's CapsuleShape is Z-aligned (servers/physics/shape_sw.cpp:546) and
//     its `height` is the CYLINDRICAL SECTION only, excluding the two caps.
//   - Godot's CylinderShape is Y-aligned and its `height` is the FULL height,
//     centered on the origin.
// Getting either wrong leaves a player capsule lying on its side.

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

	b3HullData *hull = b3CreateHull(points.ptr(), points.size(), points.size());
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

	b3HullData *hull = b3CreateHull(points.ptr(), points.size(), points.size());
	ERR_FAIL_NULL_V(hull, b3_nullShapeId);

	b3ShapeId id = b3CreateHullShape(p_body, p_def, hull);
	b3DestroyHull(hull);
	return id;
}

b3ShapeId Box3DShape::attach(b3BodyId p_body, const b3ShapeDef *p_def, const Transform &p_local_xform) const {
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
			// b3CreateMeshShape does NOT clone its input: the b3MeshData must be
			// owned here and outlive every shape built from it. Static bodies only.
			WARN_PRINT_ONCE("Box3D: ConcavePolygonShape is not implemented yet.");
			return b3_nullShapeId;

		case PhysicsServer::SHAPE_HEIGHTMAP:
			// Same non-cloning contract as mesh, and the one that matters most
			// here: CellTerrain rebuilds a HeightMapShape per streamed cell, so
			// the buffer lifetime has to survive that churn.
			WARN_PRINT_ONCE("Box3D: HeightMapShape is not implemented yet.");
			return b3_nullShapeId;

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
