#ifndef BOX3D_COLLIDE_H
#define BOX3D_COLLIDE_H

#include "box3d_objects.h"

// Contact generation for shapes that are not in the world.
//
// Box3D's world queries answer "does it overlap" and "when does it hit", but not
// "how deep". collide_shape(), rest_info() and the depenetration of non-capsule
// characters need depth, so this runs Box3D's own narrow phase (b3Collide*)
// between a free-standing query shape and whatever it overlaps.

struct Box3DContact {
	// Midway between the two surfaces, in world space.
	Vector3 point;
	// Points from the other object towards the query shape.
	Vector3 normal;
	// Negative when penetrating.
	real_t separation = 0;
	Box3DCollisionObject *other = nullptr;
	int other_shape = 0;

	_FORCE_INLINE_ Vector3 point_on_query() const { return point + normal * (separation * 0.5); }
	_FORCE_INLINE_ Vector3 point_on_other() const { return point - normal * (separation * 0.5); }
};

struct Box3DCollideFilter {
	const Set<RID> *exclude = nullptr;
	uint32_t mask = 0xFFFFFFFF;
	bool bodies = true;
	bool areas = false;
	// Skip bodies the solver drives itself.
	bool skip_dynamic = false;
};

// Appends every contact closer than p_margin. Shapes that are not convex (and
// PlaneShape and RayShape) can be collided against but not with, as elsewhere in
// Godot; they produce no contacts as the query shape.
void b3_collide_query(b3WorldId p_world, const Box3DShape *p_shape, const Transform &p_xform, real_t p_margin,
		const Box3DCollideFilter &p_filter, Vector<Box3DContact> &r_contacts);

#endif // BOX3D_COLLIDE_H
