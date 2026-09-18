#ifndef BOX3D_SPACE_H
#define BOX3D_SPACE_H

#include "box3d_objects.h"
#include "core/rid.h"
#include "servers/physics_server.h"

#include "box3d/box3d.h"

class Box3DSpace;

// Query interface for one space. Every method here is a scene-query entry point
// (raycasts, shape casts, overlap tests) and is what KinematicBody, Area
// monitoring, RayCast and every intersect_* call in GDScript ultimately hits.
//
// Still unimplemented: all queries report "nothing hit". That is a deliberate,
// visible failure mode — the game keeps running and simply never collides via
// queries, rather than crashing, while these are filled in against b3World casts.
class Box3DPhysicsDirectSpaceState : public PhysicsDirectSpaceState {
	GDCLASS(Box3DPhysicsDirectSpaceState, PhysicsDirectSpaceState);

public:
	Box3DSpace *space;

	Box3DPhysicsDirectSpaceState(Box3DSpace *p_space);

	virtual int intersect_point(const Vector3 &p_point, ShapeResult *r_results, int p_result_max, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false);
	virtual bool intersect_ray(const Vector3 &p_from, const Vector3 &p_to, RayResult &r_result, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false, bool p_pick_ray = false);
	virtual int intersect_shape(const RID &p_shape, const Transform &p_xform, float p_margin, ShapeResult *r_results, int p_result_max, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false);
	virtual bool cast_motion(const RID &p_shape, const Transform &p_xform, const Vector3 &p_motion, float p_margin, float &r_closest_safe, float &r_closest_unsafe, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false, ShapeRestInfo *r_info = nullptr);
	virtual bool collide_shape(RID p_shape, const Transform &p_shape_xform, float p_margin, Vector3 *r_results, int p_result_max, int &r_result_count, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false);
	virtual bool rest_info(RID p_shape, const Transform &p_shape_xform, float p_margin, ShapeRestInfo *r_info, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false);
	virtual Vector3 get_closest_point_to_object_volume(RID p_object, const Vector3 p_point) const;
};

// Query plumbing shared with the PhysicsServer, which reuses it for
// body_test_motion.
b3QueryFilter b3_query_filter(uint32_t p_collision_mask);
Box3DBody *b3_body_from_shape(b3ShapeId p_shape_id, int *r_shape_index);
Box3DCollisionObject *b3_object_from_shape(b3ShapeId p_shape_id, int *r_shape_index);

struct Box3DClosestHitContext {
	const Set<RID> *exclude = nullptr;
	bool collide_with_bodies = true;

	// When set, hits whose surface does not OPPOSE this direction are ignored.
	// A body resting on the ground is already touching it, so a cast along the
	// ground reports a hit at fraction 0 and the body can never move. Only
	// surfaces the motion runs into should block it.
	bool reject_non_opposing = false;
	Vector3 motion_dir;

	bool hit = false;
	float fraction = 1.0f;
	Vector3 point;
	Vector3 normal;
	Box3DBody *body = nullptr;
	int shape_index = 0;
};

float b3_closest_hit_callback(b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction,
		uint64_t userMaterialId, int triangleIndex, int childIndex, void *context);

// One physics world, wrapping a b3WorldId.
class Box3DSpace : public RID_Data {
public:
	RID self;
	bool active = false;

	b3WorldId world_id = b3_nullWorldId;

	// Bodies currently resident in this world. Maintained by the server on
	// body_set_space(), and walked after each step to copy solver transforms
	// back into the Godot-side bookkeeping.
	Vector<Box3DBody *> bodies;
	Vector<Box3DArea *> areas;

	// SPACE_PARAM_*, stored so space_get_param() round-trips. Box3D exposes its
	// own solver tuning (contact hertz/damping/speed) which these do not map
	// onto one-for-one; wiring them across is a separate pass.
	real_t contact_recycle_radius = 0.01;
	real_t contact_max_separation = 0.05;
	real_t contact_max_allowed_penetration = 0.01;
	real_t body_linear_velocity_sleep_threshold = 0.1;
	real_t body_angular_velocity_sleep_threshold = 0.1;
	real_t body_time_to_sleep = 0.5;
	real_t body_angular_velocity_damp_ratio = 10.0;
	real_t constraint_default_bias = 0.3;

	// The default area carries the world gravity/damping that bodies fall back
	// to when no Area overrides them. Godot addresses it through the normal
	// area_set_param() API using the space's own RID.
	Box3DArea *default_area = nullptr;

	int sub_step_count = 4;

	// Reported to the force-integration callback as PhysicsDirectBodyState::get_step().
	real_t last_step_delta = 0.0;

	// True only while b3World_Step is running. Body state may be read at any
	// other time -- including from _physics_process, which the engine runs
	// between flush_queries() and step() (main.cpp:2262-2276). Guarding on
	// "are we flushing queries" instead would wrongly reject that window.
	bool locked = false;

	int max_debug_contacts = 0;
	Vector<Vector3> debug_contacts;

	Box3DPhysicsDirectSpaceState *direct_state = nullptr;

	Box3DSpace();
	~Box3DSpace();

	void create_world();
	void destroy_world();

	// Pushes the default area's gravity into the live world.
	void update_gravity();

	void add_body(Box3DBody *p_body);
	void remove_body(Box3DBody *p_body);
	void add_area(Box3DArea *p_area);
	void remove_area(Box3DArea *p_area);

	// Drains Box3D's sensor begin/end touch events into Godot's area monitor
	// callbacks.
	void flush_sensor_events();

	// Applies Godot's Area gravity/damp overrides. Box3D gravity is per-world, so
	// anything an Area changes has to be applied per body before each step.
	void apply_area_overrides();

	// Drops an area from every body that was overlapping it, so a freed area is
	// never dereferenced.
	void forget_area(Box3DArea *p_area);

	void step(real_t p_delta);
	void call_queries();
};

#endif // BOX3D_SPACE_H
