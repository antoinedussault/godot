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
	bool collide_with_areas = false;
	bool skip_dynamic = false;
	// Input picking skips objects that opted out of it.
	bool pick_ray = false;

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
	Box3DCollisionObject *object = nullptr;
	int shape_index = 0;
};

// What a RayShape stands on.
struct Box3DRayHit {
	Vector3 point;
	// The direction to push: the surface normal if the shape slips on slopes, back
	// along the ray otherwise.
	Vector3 normal;
	real_t depth = 0;
	Box3DBody *body = nullptr;
	int shape = 0;
};

// Bits of b3SurfaceMaterial::userMaterialId, see _body_make_material().
#define B3_MATERIAL_ROUGH 1
#define B3_MATERIAL_ABSORBENT 2

// One sensor shape overlapping one shape of a body or another area. Box3D reports
// changes by shape id, which may be dead by the time the end is reported, so
// everything the end has to say is recorded at the beginning.
struct Box3DOverlapKey {
	uint64_t sensor = 0;
	uint64_t visitor = 0;
	_FORCE_INLINE_ bool operator<(const Box3DOverlapKey &p_key) const {
		return sensor == p_key.sensor ? visitor < p_key.visitor : sensor < p_key.sensor;
	}
};

struct Box3DOverlap {
	Box3DArea *area = nullptr;
	Box3DCollisionObject *other = nullptr;
	RID other_rid;
	ObjectID other_id = 0;
	bool other_is_area = false;
	int other_shape = 0;
	int self_shape = 0;
	// The area was told about it, so it must also be told when it ends.
	bool reported = false;
};

struct Box3DAreaEvent {
	Box3DArea *area = nullptr;
	int status = 0;
	bool other_is_area = false;
	RID other_rid;
	ObjectID other_id = 0;
	int other_shape = 0;
	int self_shape = 0;
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

	// What joints without a second body are attached to. Created on demand.
	b3BodyId ground_body = b3_nullBodyId;

	Map<Box3DOverlapKey, Box3DOverlap> overlaps;
	// Waiting for call_queries().
	List<Box3DAreaEvent> area_events;

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

	b3BodyId get_ground_body();

	// Turns Box3D's sensor begin/end touch events into overlaps and area events.
	// Runs right after the step, while the shape ids are still good.
	void collect_sensor_events();
	// Calls the area monitor callbacks.
	void flush_area_events();

	void queue_area_event(const Box3DOverlap &p_overlap, int p_status);
	// Ends an overlap: releases the gravity override and reports the exit.
	void end_overlap(Map<Box3DOverlapKey, Box3DOverlap>::Element *p_element);
	// A monitor callback was set or cleared.
	void monitor_changed(Box3DArea *p_area, bool p_areas);

	// Applies Godot's Area gravity/damp overrides. Box3D gravity is per-world, so
	// anything an Area changes has to be applied per body before each step.
	void apply_area_overrides();

	// Re-applies forces/torques set through the PhysicsServer API. Box3D clears
	// its force accumulator each step; Godot's are persistent until changed.
	void apply_accumulated_forces();

	// Ends every overlap involving something about to be destroyed.
	void forget_area(Box3DArea *p_area);
	void forget_body(Box3DBody *p_body);
	void forget_sensor(b3ShapeId p_shape);

	// Kinematic motion, see Box3DBody::kinematic_moved.
	void begin_kinematics(real_t p_delta);
	void end_kinematics();

	// RayShape. Box3D has no such thing, so the ray is cast and the result applied
	// to the velocity of dynamic bodies.
	bool cast_ray_shape(const Box3DBody *p_body, int p_shape, const Transform &p_xform, const Set<RID> &p_exclude, uint32_t p_mask, bool p_skip_dynamic, Box3DRayHit *r_hit);
	void solve_rays(real_t p_delta);

	void collect_debug_contacts();

	void step(real_t p_delta);
	void call_queries();
};

#endif // BOX3D_SPACE_H
