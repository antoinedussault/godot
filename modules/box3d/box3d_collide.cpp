#include "box3d_collide.h"

#include "box3d_space.h"
#include "box3d_types_converter.h"

#include "box3d/constants.h"

// Everything is computed relative to the query shape's origin, so precision does
// not degrade far from the world origin.
struct Box3DQueryGeometry {
	b3ShapeType type = b3_sphereShape;
	b3Sphere sphere;
	b3Capsule capsule;
	b3HullData *hull = nullptr;
	b3AABB bounds;

	~Box3DQueryGeometry() {
		if (hull) {
			b3DestroyHull(hull);
		}
	}

	bool build(const Box3DShape *p_shape, const Basis &p_basis, real_t p_inflate) {
		Vector<b3Vec3> points;
		b3ShapeProxy proxy;
		if (!p_shape->build_proxy(points, Transform(p_basis, Vector3()), &proxy)) {
			return false;
		}

		float grow = proxy.radius;
		switch (p_shape->type) {
			case PhysicsServer::SHAPE_SPHERE: {
				type = b3_sphereShape;
				sphere.center = points[0];
				sphere.radius = grow = proxy.radius + (float)p_inflate;
			} break;
			case PhysicsServer::SHAPE_CAPSULE: {
				grow = proxy.radius + (float)p_inflate;
				const b3Vec3 d = b3Sub(points[1], points[0]);
				if (b3Length(d) <= B3_MIN_CAPSULE_LENGTH) {
					type = b3_sphereShape;
					sphere.center = points[0];
					sphere.radius = grow;
				} else {
					type = b3_capsuleShape;
					capsule.center1 = points[0];
					capsule.center2 = points[1];
					capsule.radius = grow;
				}
			} break;
			default: {
				type = b3_hullShape;
				hull = p_shape->create_hull(Transform(p_basis, Vector3()));
				if (!hull) {
					return false;
				}
				// A hull cannot be inflated. The narrow phase reports contacts out to
				// the speculative distance anyway, which is all a margin needs.
				grow = B3_SPECULATIVE_DISTANCE;
			} break;
		}

		bounds.lowerBound = bounds.upperBound = points[0];
		for (int i = 1; i < points.size(); i++) {
			bounds.lowerBound = b3Min(bounds.lowerBound, points[i]);
			bounds.upperBound = b3Max(bounds.upperBound, points[i]);
		}
		const b3Vec3 g = { grow, grow, grow };
		bounds.lowerBound = b3Sub(bounds.lowerBound, g);
		bounds.upperBound = b3Add(bounds.upperBound, g);
		return true;
	}
};

#define B3_QUERY_MAX_POINTS 32

struct Box3DCollidePair {
	const Box3DQueryGeometry *geometry = nullptr;
	Box3DCollisionObject *other = nullptr;
	int other_shape = 0;
	Vector3 origin; // of the query frame
	b3Transform xform; // of the other shape, in the query frame
	real_t max_separation = 0;
	Vector<Box3DContact> *contacts = nullptr;

	// The query geometry in the other shape's frame, for triangles.
	b3Sphere local_sphere;
	b3Capsule local_capsule;
	b3HullData *local_hull = nullptr;

	// p_flip when the query shape was shape A, i.e. the manifold is in its frame and
	// the normal points away from it.
	void emit(const b3LocalManifold &p_manifold, bool p_flip) {
		for (int i = 0; i < p_manifold.pointCount; i++) {
			const b3LocalManifoldPoint &p = p_manifold.points[i];
			if (p.separation > max_separation) {
				continue;
			}
			Box3DContact c;
			if (p_flip) {
				c.point = origin + b3_to_godot(p.point);
				c.normal = -b3_to_godot(p_manifold.normal);
			} else {
				c.point = origin + b3_to_godot(b3TransformPoint(xform, p.point));
				c.normal = b3_to_godot(b3RotateVector(xform.q, p_manifold.normal));
			}
			if (c.normal.length_squared() <= CMP_EPSILON) {
				continue;
			}
			c.normal.normalize();
			c.separation = p.separation;
			c.other = other;
			c.other_shape = other_shape;
			contacts->push_back(c);
		}
	}
};

static bool b3_collide_triangle(b3Vec3 a, b3Vec3 b, b3Vec3 c, int triangleIndex, void *context) {
	Box3DCollidePair *pair = static_cast<Box3DCollidePair *>(context);

	b3LocalManifoldPoint buffer[B3_QUERY_MAX_POINTS];
	b3LocalManifold m = {};
	m.points = buffer;
	const b3Vec3 triangle[3] = { a, b, c };

	switch (pair->geometry->type) {
		case b3_sphereShape:
			b3CollideTriangleAndSphere(&m, B3_QUERY_MAX_POINTS, triangle, &pair->local_sphere);
			break;
		case b3_capsuleShape: {
			b3SimplexCache cache = {};
			b3CollideTriangleAndCapsule(&m, B3_QUERY_MAX_POINTS, triangle, &pair->local_capsule, &cache);
		} break;
		default: {
			b3SATCache cache = {};
			b3CollideTriangleAndHull(&m, B3_QUERY_MAX_POINTS, a, b, c, 0, pair->local_hull, &cache, true);
		} break;
	}
	pair->emit(m, false);
	return true;
}

static void b3_collide_pair(Box3DCollidePair &p_pair, b3ShapeId p_shape) {
	const Box3DQueryGeometry &g = *p_pair.geometry;
	const b3Transform &xf = p_pair.xform;
	const b3Transform identity = b3Transform_identity;
	// Query frame -> other frame.
	const b3Transform inv = b3InvMulTransforms(xf, identity);

	b3LocalManifoldPoint buffer[B3_QUERY_MAX_POINTS];
	b3LocalManifold m = {};
	m.points = buffer;
	b3SimplexCache simplex = {};
	b3SATCache sat = {};
	const int cap = B3_QUERY_MAX_POINTS;

	switch (b3Shape_GetType(p_shape)) {
		case b3_sphereShape: {
			const b3Sphere s = b3Shape_GetSphere(p_shape);
			if (g.type == b3_sphereShape) {
				b3CollideSpheres(&m, cap, &s, &g.sphere, inv);
				p_pair.emit(m, false);
			} else {
				if (g.type == b3_capsuleShape) {
					b3CollideCapsuleAndSphere(&m, cap, &g.capsule, &s, xf);
				} else {
					b3CollideHullAndSphere(&m, cap, g.hull, &s, xf, &simplex);
				}
				p_pair.emit(m, true);
			}
		} break;

		case b3_capsuleShape: {
			const b3Capsule c = b3Shape_GetCapsule(p_shape);
			if (g.type == b3_hullShape) {
				b3CollideHullAndCapsule(&m, cap, g.hull, &c, xf, &simplex);
				p_pair.emit(m, true);
			} else {
				if (g.type == b3_sphereShape) {
					b3CollideCapsuleAndSphere(&m, cap, &c, &g.sphere, inv);
				} else {
					b3CollideCapsules(&m, cap, &c, &g.capsule, inv);
				}
				p_pair.emit(m, false);
			}
		} break;

		case b3_hullShape: {
			const b3HullData *h = b3Shape_GetHull(p_shape);
			if (g.type == b3_sphereShape) {
				b3CollideHullAndSphere(&m, cap, h, &g.sphere, inv, &simplex);
			} else if (g.type == b3_capsuleShape) {
				b3CollideHullAndCapsule(&m, cap, h, &g.capsule, inv, &simplex);
			} else {
				b3CollideHulls(&m, cap, h, g.hull, inv, &sat);
			}
			p_pair.emit(m, false);
		} break;

		case b3_meshShape:
		case b3_heightShape: {
			if (g.type == b3_sphereShape) {
				p_pair.local_sphere.center = b3TransformPoint(inv, g.sphere.center);
				p_pair.local_sphere.radius = g.sphere.radius;
			} else if (g.type == b3_capsuleShape) {
				p_pair.local_capsule.center1 = b3TransformPoint(inv, g.capsule.center1);
				p_pair.local_capsule.center2 = b3TransformPoint(inv, g.capsule.center2);
				p_pair.local_capsule.radius = g.capsule.radius;
			} else {
				const b3Vec3 one = { 1.0f, 1.0f, 1.0f };
				p_pair.local_hull = b3CloneAndTransformHull(g.hull, inv, one);
				if (!p_pair.local_hull) {
					return;
				}
			}

			const b3AABB bounds = b3AABB_Transform(inv, g.bounds);
			if (b3Shape_GetType(p_shape) == b3_meshShape) {
				const b3Mesh mesh = b3Shape_GetMesh(p_shape);
				b3QueryMesh(&mesh, bounds, b3_collide_triangle, &p_pair);
			} else {
				b3QueryHeightField(b3Shape_GetHeightField(p_shape), bounds, b3_collide_triangle, &p_pair);
			}

			if (p_pair.local_hull) {
				b3DestroyHull(p_pair.local_hull);
				p_pair.local_hull = nullptr;
			}
		} break;

		default:
			break;
	}
}

struct Box3DCollectContext {
	Vector<b3ShapeId> shapes;
};

static bool b3_collect_callback(b3ShapeId shapeId, void *context) {
	static_cast<Box3DCollectContext *>(context)->shapes.push_back(shapeId);
	return true;
}

void b3_collide_query(b3WorldId p_world, const Box3DShape *p_shape, const Transform &p_xform, real_t p_margin,
		const Box3DCollideFilter &p_filter, Vector<Box3DContact> &r_contacts) {
	Box3DQueryGeometry geometry;
	if (!geometry.build(p_shape, p_xform.basis, p_margin)) {
		return;
	}

	b3AABB aabb = geometry.bounds;
	const b3Vec3 origin = b3_from_godot(p_xform.origin);
	aabb.lowerBound = b3Add(aabb.lowerBound, origin);
	aabb.upperBound = b3Add(aabb.upperBound, origin);

	// Collected first: the narrow phase should not run inside a tree traversal.
	Box3DCollectContext collected;
	b3World_OverlapAABB(p_world, aabb, b3_query_filter(p_filter.mask), b3_collect_callback, &collected);

	for (int i = 0; i < collected.shapes.size(); i++) {
		int shape_index = 0;
		Box3DCollisionObject *other = b3_object_from_shape(collected.shapes[i], &shape_index);
		if (!other || (p_filter.exclude && p_filter.exclude->has(other->self))) {
			continue;
		}
		if (other->type == Box3DCollisionObject::TYPE_BODY) {
			if (!p_filter.bodies || (p_filter.skip_dynamic && static_cast<Box3DBody *>(other)->is_dynamic())) {
				continue;
			}
		} else if (!p_filter.areas) {
			continue;
		}

		const b3WorldTransform wxf = b3Body_GetTransform(b3Shape_GetBody(collected.shapes[i]));

		Box3DCollidePair pair;
		pair.geometry = &geometry;
		pair.other = other;
		pair.other_shape = shape_index;
		pair.origin = p_xform.origin;
		pair.xform.q = wxf.q;
		pair.xform.p = b3_from_godot(b3_pos_to_godot(wxf.p) - p_xform.origin);
		// Round shapes were inflated by the margin, so their separation already
		// includes it.
		pair.max_separation = geometry.type == b3_hullShape ? p_margin : (real_t)0.0;
		pair.contacts = &r_contacts;

		const int first = r_contacts.size();
		b3_collide_pair(pair, collected.shapes[i]);

		if (geometry.type != b3_hullShape && p_margin != 0) {
			// Undo the inflation, so callers see the real gap and the real surface.
			for (int j = first; j < r_contacts.size(); j++) {
				Box3DContact &c = r_contacts.write[j];
				c.separation += p_margin;
				c.point += c.normal * (p_margin * 0.5);
			}
		}
	}
}
