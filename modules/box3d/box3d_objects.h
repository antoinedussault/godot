#ifndef BOX3D_OBJECTS_H
#define BOX3D_OBJECTS_H

#include "core/map.h"
#include "core/object.h"
#include "core/rid.h"
#include "core/set.h"
#include "core/variant.h"
#include "core/vector.h"
#include "servers/physics_server.h"

#include "box3d/box3d.h"
#include "box3d/collision.h"

// Internal object model for the Box3D PhysicsServer backend.
//
// Each struct pairs Godot-side bookkeeping (what the engine set) with the Box3D
// handle it currently maps to. The bookkeeping stays authoritative: a body can be
// fully configured before it has a space, and b3 objects are created when it
// joins one and destroyed when it leaves.

class Box3DSpace;
class Box3DArea;
class Box3DJoint;

// Godot's layers and masks occupy the low 32 bits of Box3D's 64 bit filters. These
// mark what kind of object a shape belongs to, so that an object on no layer still
// reaches the custom filter (see b3_custom_filter in box3d_space.cpp).
#define B3_CATEGORY_BODY ((uint64_t)1 << 32)
#define B3_CATEGORY_AREA ((uint64_t)1 << 33)
// Meaningless by itself. Flipping it is how a filter is forced to be re-evaluated.
#define B3_CATEGORY_TOGGLE ((uint64_t)1 << 34)

struct Box3DShapeEntry {
	RID shape;
	Transform xform;
	bool disabled = false;
	b3ShapeId shape_id = b3_nullShapeId;

	// Box3D holds a REFERENCE to height field and mesh data instead of cloning
	// it, so these must outlive the shape built from them. They are owned per
	// ATTACHMENT, not per shape resource: the transform is baked in, and two
	// bodies can use one shape resource at different scales.
	b3HeightFieldData *heightfield_data = nullptr;
	b3MeshData *mesh_data = nullptr;

	// Must run only after the b3 shape built from this geometry is destroyed.
	_FORCE_INLINE_ void free_owned_geometry() {
		if (heightfield_data) {
			b3DestroyHeightField(heightfield_data);
			heightfield_data = nullptr;
		}
		if (mesh_data) {
			b3DestroyMesh(mesh_data);
			mesh_data = nullptr;
		}
	}
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

	// Builds this shape's Box3D geometry and attaches it to a body. Returns
	// b3_nullShapeId for shape types not yet supported.
	//
	// Sphere, capsule and hull geometry is fully cloned by Box3D and the
	// temporaries built here are safe to discard. A height field is NOT cloned:
	// the blob is handed back through r_owned_heightfield and becomes the
	// caller's to free once the shape built from it is destroyed.
	b3ShapeId attach(b3BodyId p_body, const b3ShapeDef *p_def, const Transform &p_local_xform,
			Box3DShapeEntry *r_owner = nullptr);

	// For SHAPE_HEIGHTMAP, the grid dimensions in grid points. Returns false for
	// any other type or malformed data.
	bool get_heightmap_dimensions(int &r_width, int &r_depth) const;

	// Builds a point-cloud proxy of this shape for shape casts and overlap
	// queries, with `p_xform` applied. Points are written into `r_points`, which
	// the caller owns and must keep alive for the duration of the query.
	// Returns false for shape types that cannot be proxied.
	bool build_proxy(Vector<b3Vec3> &r_points, const Transform &p_xform, b3ShapeProxy *r_proxy) const;

	// Builds this shape as a hull with `p_xform` applied, scale and shear included.
	// The caller owns it. Null for shapes that are not polyhedral.
	b3HullData *create_hull(const Transform &p_xform) const;

	// For SHAPE_RAY. Returns false for any other type or malformed data.
	bool get_ray(real_t &r_length, bool &r_slips_on_slope) const;
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

	// The solver-side body. Null until the object joins a space. An Area lives in
	// the solver as a STATIC body whose shapes are sensors.
	b3BodyId body_id = b3_nullBodyId;
	_FORCE_INLINE_ bool in_world() const { return !B3_IS_NULL(body_id); }

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

	// Scale is baked into the shapes, as it is for bodies.
	Vector3 baked_scale = Vector3(1, 1, 1);

	Box3DArea() :
			Box3DCollisionObject(TYPE_AREA) {}
};

class Box3DBody : public Box3DCollisionObject {
public:
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

	// Areas currently overlapping this body, and by how many shape pairs. Maintained
	// from Box3D sensor events. Box3D has no per-area gravity, so the override is
	// recomputed and applied per body each step.
	Map<Box3DArea *, int> areas;

	// Joints attached to this body, whether or not they are live.
	Set<Box3DJoint *> joints;

	// What the areas added up to at the last step, for the direct body state.
	Vector3 total_gravity;
	real_t total_linear_damp = 0.0;
	real_t total_angular_damp = 0.0;

	// Kinematic bodies are teleported by Godot. Queries must see that at once, but
	// contacts need the motion as velocity, so the step replays it from
	// kinematic_from (see Box3DSpace::step).
	bool kinematic_moved = false;
	bool kinematic_settle = false;
	bool kinematic_has_velocity = false;
	Transform kinematic_from;

	// Set when the sleep state changed, so the node hears about it once.
	bool report_state = false;

	int ray_shape_count = 0;

	// Box3D anchors a height field at the body origin extending positive, while
	// Godot centers it, and b3CreateHeightFieldShape takes no transform to
	// correct with. For a static body carrying only a height field, the fix is
	// to shift the b3 body itself.
	//
	// Safe specifically because static bodies are skipped during transform
	// writeback, so the shifted b3 position is never read back into `transform`.
	// The body scale that is currently baked into the attached shapes. Godot may
	// set a body's transform long after its shapes exist -- CellTerrain attaches
	// the HeightMapShape and only then applies the scale that defines its grid
	// spacing (cell_terrain.cpp:653-664) -- so a scale change has to rebuild them.
	Vector3 baked_scale = Vector3(1, 1, 1);

	bool has_shape_origin_offset = false;
	// The height field shape's local transform, scale included.
	Transform shape_origin_local;
	// Half the field's scaled extent, negated: the corner Box3D anchors at.
	Vector3 shape_corner_offset;

	// True for bodies the solver drives itself.
	_FORCE_INLINE_ bool is_dynamic() const {
		return mode == PhysicsServer::BODY_MODE_RIGID || mode == PhysicsServer::BODY_MODE_CHARACTER;
	}

	// Center of mass relative to the origin, in global axes. Godot measures force
	// and impulse positions in that frame too.
	_FORCE_INLINE_ Vector3 get_center_of_mass() const {
		if (!in_world() || !is_dynamic()) {
			return Vector3();
		}
		const b3Vec3 c = b3Body_GetLocalCenter(body_id);
		return transform.basis.orthonormalized().xform(Vector3(c.x, c.y, c.z));
	}

	// p_offset is relative to the origin, in global axes.
	_FORCE_INLINE_ Vector3 get_velocity_at(const Vector3 &p_offset) const {
		return linear_velocity + angular_velocity.cross(p_offset - get_center_of_mass());
	}

	// Godot bakes scale into a body's basis; Box3D transforms are strictly rigid
	// and carry none. Scale is therefore baked into the SHAPES instead (see
	// Box3DPhysicsServer::_body_rebuild_shapes), and the solver body gets the
	// rotation-only basis. Dropping the scale here without compensating is what
	// made scaled terrain collide at the wrong size.
	_FORCE_INLINE_ Transform get_solver_transform() const {
		const Transform rigid(transform.basis.orthonormalized(), transform.origin);
		if (!has_shape_origin_offset) {
			return rigid;
		}
		const Transform world = rigid * shape_origin_local;
		const Basis wrot = world.basis.orthonormalized();
		return Transform(wrot, world.origin + wrot.xform(shape_corner_offset));
	}

	Box3DBody() :
			Box3DCollisionObject(TYPE_BODY) {}
};

class Box3DJoint : public RID_Data {
public:
	PhysicsServer::JointType type = PhysicsServer::JOINT_PIN;

	// Live only while both bodies share a space. See box3d_joints.cpp.
	b3JointId joint_id = b3_nullJointId;

	// body_b is null for a joint anchored to the world.
	Box3DBody *body_a = nullptr;
	Box3DBody *body_b = nullptr;
	// One of the bodies was freed.
	bool broken = false;

	// In the local space of each body, or in world space without a body B.
	Transform frame_a;
	Transform frame_b;

	int solver_priority = 1;
	bool collisions_disabled = false;

	// Indexed by the parameter enum of the joint type. 6DOF has its own, per axis.
	float params[PhysicsServer::SLIDER_JOINT_MAX] = {};
	bool hinge_flags[PhysicsServer::HINGE_JOINT_FLAG_MAX] = {};
	float g6dof_params[3][PhysicsServer::G6DOF_JOINT_MAX] = {};
	bool g6dof_flags[3][PhysicsServer::G6DOF_JOINT_FLAG_MAX] = {};

	Box3DJoint(PhysicsServer::JointType p_type) :
			type(p_type) {}
};

#endif // BOX3D_OBJECTS_H
