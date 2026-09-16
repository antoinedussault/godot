#ifndef BOX3D_OBJECTS_H
#define BOX3D_OBJECTS_H

#include "core/object.h"
#include "core/rid.h"
#include "core/set.h"
#include "core/variant.h"
#include "core/vector.h"
#include "servers/physics_server.h"

#include "box3d/box3d.h"

// Internal object model for the Box3D PhysicsServer backend.
//
// Each struct pairs Godot-side bookkeeping (what the engine set) with the Box3D
// handle it currently maps to. The bookkeeping stays authoritative: a body can be
// fully configured before it has a space, and b3 objects are created when it
// joins one and destroyed when it leaves.

class Box3DSpace;

struct Box3DShapeEntry {
	RID shape;
	Transform xform;
	bool disabled = false;
	b3ShapeId shape_id = b3_nullShapeId;
};

// A shape resource. `data` is kept in the exact Variant form Godot handed us so
// shape_get_data() round-trips losslessly. Box3D geometry is built per
// attachment rather than stored here, because a b3 shape belongs to a body.
class Box3DShape : public RID_Data {
public:
	PhysicsServer::ShapeType type = PhysicsServer::SHAPE_SPHERE;
	Variant data;
	real_t margin = 0.04;
	real_t custom_bias = 0.0;

	// Every collision object currently referencing this shape, so shape_set_data()
	// can rebuild their solver-side geometry.
	Set<RID> owners;

	// Box3D holds a REFERENCE to height field data rather than cloning it, so
	// this blob must outlive every b3 shape built from it. Owned here, shared
	// across attachments of the same shape, and released by free_geometry().
	b3HeightFieldData *heightfield_data = nullptr;

	// Builds this shape's Box3D geometry and attaches it to a body. Returns
	// b3_nullShapeId for shape types not yet supported.
	//
	// Sphere, capsule and hull geometry is fully cloned by Box3D, so the
	// temporaries built inside are safe to discard; the height field is not.
	b3ShapeId attach(b3BodyId p_body, const b3ShapeDef *p_def, const Transform &p_local_xform);

	// Releases owned geometry. Must run only when no b3 shape still references
	// it — i.e. after the shapes built from it are destroyed.
	void free_geometry();

	// For SHAPE_HEIGHTMAP, the grid dimensions in grid points. Returns false for
	// any other type or malformed data.
	bool get_heightmap_dimensions(int &r_width, int &r_depth) const;

	// Builds a point-cloud proxy of this shape for shape casts and overlap
	// queries, with `p_xform` applied. Points are written into `r_points`, which
	// the caller owns and must keep alive for the duration of the query.
	// Returns false for shape types that cannot be proxied.
	bool build_proxy(Vector<b3Vec3> &r_points, const Transform &p_xform, b3ShapeProxy *r_proxy) const;
};

class Box3DCollisionObject : public RID_Data {
public:
	enum Type {
		TYPE_AREA,
		TYPE_BODY,
	};

	Type type;
	RID self;
	RID space;
	ObjectID instance_id = 0;
	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;
	Transform transform;
	Vector<Box3DShapeEntry> shapes;
	bool ray_pickable = true;

	Box3DCollisionObject(Type p_type) :
			type(p_type) {}
	virtual ~Box3DCollisionObject() {}
};

class Box3DArea : public Box3DCollisionObject {
public:
	PhysicsServer::AreaSpaceOverrideMode space_override_mode = PhysicsServer::AREA_SPACE_OVERRIDE_DISABLED;

	Vector3 gravity_vector = Vector3(0, -1, 0);
	real_t gravity = 9.8;
	bool gravity_is_point = false;
	real_t gravity_distance_scale = 0.0;
	real_t point_attenuation = 1.0;
	real_t linear_damp = 0.1;
	real_t angular_damp = 0.1;
	real_t priority = 0.0;

	bool monitorable = false;

	ObjectID monitor_callback_id = 0;
	StringName monitor_callback_method;
	ObjectID area_monitor_callback_id = 0;
	StringName area_monitor_callback_method;

	Box3DArea() :
			Box3DCollisionObject(TYPE_AREA) {}
};

class Box3DBody : public Box3DCollisionObject {
public:
	// Null until the body joins a space.
	b3BodyId body_id = b3_nullBodyId;

	PhysicsServer::BodyMode mode = PhysicsServer::BODY_MODE_RIGID;

	real_t bounce = 0.0;
	real_t friction = 1.0;
	real_t mass = 1.0;
	real_t gravity_scale = 1.0;
	real_t linear_damp = -1.0;
	real_t angular_damp = -1.0;

	Vector3 linear_velocity;
	Vector3 angular_velocity;
	bool sleeping = false;
	bool can_sleep = true;

	Vector3 applied_force;
	Vector3 applied_torque;

	bool ccd_enabled = false;
	bool omit_force_integration = false;
	uint32_t user_flags = 0;
	uint16_t locked_axis = 0;
	real_t kinematic_safe_margin = 0.001;

	int max_contacts_reported = 0;
	real_t contacts_reported_depth_threshold = 0.0;

	ObjectID force_integration_callback_id = 0;
	StringName force_integration_callback_method;
	Variant force_integration_callback_udata;

	Set<RID> collision_exceptions;

	// Box3D anchors a height field at the body origin extending positive, while
	// Godot centers it, and b3CreateHeightFieldShape takes no transform to
	// correct with. For a static body carrying only a height field, the fix is
	// to shift the b3 body itself: this is the body-local transform that maps
	// Godot's placement onto Box3D's corner anchor. Identity when unused.
	//
	// Safe specifically because static bodies are skipped during transform
	// writeback, so the shifted b3 position is never read back into `transform`.
	bool has_shape_origin_offset = false;
	Transform shape_origin_offset;

	_FORCE_INLINE_ bool in_world() const { return !B3_IS_NULL(body_id); }

	// The transform the b3 body should actually sit at, offset included.
	_FORCE_INLINE_ Transform get_solver_transform() const {
		return has_shape_origin_offset ? (transform * shape_origin_offset) : transform;
	}

	Box3DBody() :
			Box3DCollisionObject(TYPE_BODY) {}
};

class Box3DJoint : public RID_Data {
public:
	PhysicsServer::JointType type = PhysicsServer::JOINT_PIN;
	RID body_a;
	RID body_b;
	int solver_priority = 1;
	bool collisions_disabled = false;

	Vector3 pin_local_a;
	Vector3 pin_local_b;

	Box3DJoint(PhysicsServer::JointType p_type) :
			type(p_type) {}
};

#endif // BOX3D_OBJECTS_H
