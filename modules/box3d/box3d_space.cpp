#include "box3d_space.h"
#include "box3d_physics_server.h"
#include "box3d_types_converter.h"

#include "core/error_macros.h"

#define B3_QUERY_TODO(m_what) WARN_PRINT_ONCE("Box3D: " m_what "() is not implemented yet; returning no hit.")

// Scene queries.
//
// Areas are not in the b3 world at all yet, so `p_collide_with_areas` can never
// match anything here. That is a gap in the Area work, not in these queries.
//
// Box3D's filter semantics: a query matches a shape when the query's maskBits
// overlap the shape's categoryBits. Shapes are attached with
// categoryBits = the body's collision layer, so passing Godot's collision mask
// straight through as maskBits gives Godot's behaviour.
b3QueryFilter b3_query_filter(uint32_t p_collision_mask) {
	b3QueryFilter filter = b3DefaultQueryFilter();
	filter.categoryBits = ~(uint64_t)0;
	filter.maskBits = (uint64_t)p_collision_mask;
	return filter;
}

// Resolves a hit shape back to the Godot side. Returns nullptr if the shape has
// no Box3DBody behind it.
Box3DCollisionObject *b3_object_from_shape(b3ShapeId p_shape_id, int *r_shape_index) {
	b3BodyId body_id = b3Shape_GetBody(p_shape_id);
	if (B3_IS_NULL(body_id)) {
		return nullptr;
	}

	Box3DCollisionObject *obj = static_cast<Box3DCollisionObject *>(b3Body_GetUserData(body_id));
	if (!obj) {
		return nullptr;
	}

	if (r_shape_index) {
		*r_shape_index = 0;
		for (int i = 0; i < obj->shapes.size(); i++) {
			if (B3_ID_EQUALS(obj->shapes[i].shape_id, p_shape_id)) {
				*r_shape_index = i;
				break;
			}
		}
	}
	return obj;
}

// Bodies only. Queries and the character mover must never resolve a sensor as
// something solid.
Box3DBody *b3_body_from_shape(b3ShapeId p_shape_id, int *r_shape_index) {
	Box3DCollisionObject *obj = b3_object_from_shape(p_shape_id, r_shape_index);
	if (!obj || obj->type != Box3DCollisionObject::TYPE_BODY) {
		return nullptr;
	}
	return static_cast<Box3DBody *>(obj);
}

float b3_closest_hit_callback(b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction,
		uint64_t userMaterialId, int triangleIndex, int childIndex, void *context) {
	Box3DClosestHitContext *ctx = static_cast<Box3DClosestHitContext *>(context);

	int shape_index = 0;
	Box3DBody *body = b3_body_from_shape(shapeId, &shape_index);
	// -1 means "ignore this shape and keep going", which is what an excluded or
	// unresolvable hit needs. Returning 0 would terminate the whole cast.
	if (!body || !ctx->collide_with_bodies) {
		return -1.0f;
	}
	if (ctx->exclude && ctx->exclude->has(body->self)) {
		return -1.0f;
	}

	if (ctx->reject_non_opposing) {
		const Vector3 n = b3_to_godot(normal);
		// Facing the same way as the motion, or perpendicular to it: the body is
		// sliding along this surface, not into it.
		if (n.length_squared() <= CMP_EPSILON || n.normalized().dot(ctx->motion_dir) >= -CMP_EPSILON) {
			return -1.0f;
		}
	}

	ctx->hit = true;
	ctx->fraction = fraction;
	ctx->point = b3_pos_to_godot(point);
	ctx->normal = b3_to_godot(normal);
	ctx->body = body;
	ctx->shape_index = shape_index;

	// Clip the cast to here so the traversal keeps narrowing toward the closest.
	return fraction;
}

Box3DPhysicsDirectSpaceState::Box3DPhysicsDirectSpaceState(Box3DSpace *p_space) :
		PhysicsDirectSpaceState(),
		space(p_space) {
}

// Collects every overlapping shape, up to the caller's result cap.
struct Box3DOverlapContext {
	const Set<RID> *exclude = nullptr;
	bool collide_with_bodies = true;
	PhysicsDirectSpaceState::ShapeResult *results = nullptr;
	int result_max = 0;
	int count = 0;
};

static bool b3_overlap_callback(b3ShapeId shapeId, void *context) {
	Box3DOverlapContext *ctx = static_cast<Box3DOverlapContext *>(context);

	int shape_index = 0;
	Box3DBody *body = b3_body_from_shape(shapeId, &shape_index);
	if (!body || !ctx->collide_with_bodies) {
		return true; // keep searching
	}
	if (ctx->exclude && ctx->exclude->has(body->self)) {
		return true;
	}

	PhysicsDirectSpaceState::ShapeResult &r = ctx->results[ctx->count];
	r.rid = body->self;
	r.collider_id = body->instance_id;
	r.collider = r.collider_id ? ObjectDB::get_instance(r.collider_id) : nullptr;
	r.shape = shape_index;
	ctx->count++;

	// Returning false stops the traversal, which is what the result cap means.
	return ctx->count < ctx->result_max;
}

int Box3DPhysicsDirectSpaceState::intersect_point(const Vector3 &p_point, ShapeResult *r_results, int p_result_max, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas) {
	ERR_FAIL_NULL_V(space, 0);
	if (B3_IS_NULL(space->world_id) || p_result_max <= 0) {
		return 0;
	}

	// A point is a zero-radius one-point cloud.
	b3Vec3 point = b3_from_godot(p_point);
	b3ShapeProxy proxy;
	proxy.points = &point;
	proxy.count = 1;
	proxy.radius = 0.0f;

	Box3DOverlapContext ctx;
	ctx.exclude = &p_exclude;
	ctx.collide_with_bodies = p_collide_with_bodies;
	ctx.results = r_results;
	ctx.result_max = p_result_max;

	b3World_OverlapShape(space->world_id, b3_pos_from_godot(Vector3()), &proxy,
			b3_query_filter(p_collision_mask), b3_overlap_callback, &ctx);

	return ctx.count;
}

bool Box3DPhysicsDirectSpaceState::intersect_ray(const Vector3 &p_from, const Vector3 &p_to, RayResult &r_result, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas, bool p_pick_ray) {
	ERR_FAIL_NULL_V(space, false);
	if (B3_IS_NULL(space->world_id)) {
		return false;
	}

	Box3DClosestHitContext ctx;
	ctx.exclude = &p_exclude;
	ctx.collide_with_bodies = p_collide_with_bodies;

	// Box3D takes an origin plus a translation, not two endpoints.
	b3World_CastRay(space->world_id,
			b3_pos_from_godot(p_from),
			b3_from_godot(p_to - p_from),
			b3_query_filter(p_collision_mask),
			b3_closest_hit_callback,
			&ctx);

	if (!ctx.hit || !ctx.body) {
		return false;
	}

	r_result.position = ctx.point;
	r_result.normal = ctx.normal;
	r_result.rid = ctx.body->self;
	r_result.collider_id = ctx.body->instance_id;
	r_result.collider = r_result.collider_id ? ObjectDB::get_instance(r_result.collider_id) : nullptr;
	r_result.shape = ctx.shape_index;
	return true;
}

int Box3DPhysicsDirectSpaceState::intersect_shape(const RID &p_shape, const Transform &p_xform, float p_margin, ShapeResult *r_results, int p_result_max, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas) {
	ERR_FAIL_NULL_V(space, 0);
	if (B3_IS_NULL(space->world_id) || p_result_max <= 0) {
		return 0;
	}

	Box3DPhysicsServer *server = Box3DPhysicsServer::get_box3d_singleton();
	ERR_FAIL_NULL_V(server, 0);
	Box3DShape *shape = server->get_shape(p_shape);
	ERR_FAIL_NULL_V(shape, 0);

	Vector<b3Vec3> points;
	b3ShapeProxy proxy;
	if (!shape->build_proxy(points, p_xform, &proxy)) {
		return 0;
	}
	// Godot's query margin inflates the shape; Box3D's proxy radius does exactly
	// that, so it adds on top of whatever radius the shape already carries.
	proxy.radius += (float)p_margin;

	Box3DOverlapContext ctx;
	ctx.exclude = &p_exclude;
	ctx.collide_with_bodies = p_collide_with_bodies;
	ctx.results = r_results;
	ctx.result_max = p_result_max;

	b3World_OverlapShape(space->world_id, b3_pos_from_godot(Vector3()), &proxy,
			b3_query_filter(p_collision_mask), b3_overlap_callback, &ctx);

	return ctx.count;
}

bool Box3DPhysicsDirectSpaceState::cast_motion(const RID &p_shape, const Transform &p_xform, const Vector3 &p_motion, float p_margin, float &r_closest_safe, float &r_closest_unsafe, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas, ShapeRestInfo *r_info) {
	// "Whole motion is safe" is the correct no-obstacle answer; 0 would instead
	// read as "blocked immediately" and freeze every caller.
	r_closest_safe = 1.0;
	r_closest_unsafe = 1.0;

	ERR_FAIL_NULL_V(space, false);
	if (B3_IS_NULL(space->world_id)) {
		return true;
	}

	Box3DPhysicsServer *server = Box3DPhysicsServer::get_box3d_singleton();
	ERR_FAIL_NULL_V(server, true);
	Box3DShape *shape = server->get_shape(p_shape);
	ERR_FAIL_NULL_V(shape, true);

	Vector<b3Vec3> points;
	b3ShapeProxy proxy;
	if (!shape->build_proxy(points, p_xform, &proxy)) {
		return true;
	}

	Box3DClosestHitContext ctx;
	ctx.exclude = &p_exclude;
	ctx.collide_with_bodies = p_collide_with_bodies;

	b3World_CastShape(space->world_id,
			b3_pos_from_godot(Vector3()),
			&proxy,
			b3_from_godot(p_motion),
			b3_query_filter(p_collision_mask),
			b3_closest_hit_callback,
			&ctx);

	if (!ctx.hit) {
		return true;
	}

	// Godot separates the last provably-free fraction from the first touching
	// one. Box3D returns a single impact fraction, so both collapse onto it —
	// callers that rely on a non-empty safe/unsafe gap (notably move_and_slide's
	// depenetration) will need the margin-aware version that lands with
	// body_test_motion.
	r_closest_safe = ctx.fraction;
	r_closest_unsafe = ctx.fraction;

	if (r_info && ctx.body) {
		r_info->point = ctx.point;
		r_info->normal = ctx.normal;
		r_info->rid = ctx.body->self;
		r_info->collider_id = ctx.body->instance_id;
		r_info->shape = ctx.shape_index;
		r_info->linear_velocity = ctx.body->linear_velocity;
	}
	return true;
}

bool Box3DPhysicsDirectSpaceState::collide_shape(RID p_shape, const Transform &p_shape_xform, float p_margin, Vector3 *r_results, int p_result_max, int &r_result_count, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas) {
	B3_QUERY_TODO("collide_shape");
	r_result_count = 0;
	return false;
}

bool Box3DPhysicsDirectSpaceState::rest_info(RID p_shape, const Transform &p_shape_xform, float p_margin, ShapeRestInfo *r_info, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas) {
	B3_QUERY_TODO("rest_info");
	return false;
}

Vector3 Box3DPhysicsDirectSpaceState::get_closest_point_to_object_volume(RID p_object, const Vector3 p_point) const {
	B3_QUERY_TODO("get_closest_point_to_object_volume");
	return p_point;
}

Box3DSpace::Box3DSpace() {
	direct_state = memnew(Box3DPhysicsDirectSpaceState(this));
}

Box3DSpace::~Box3DSpace() {
	destroy_world();

	if (direct_state) {
		memdelete(direct_state);
		direct_state = nullptr;
	}
}

void Box3DSpace::create_world() {
	if (!B3_IS_NULL(world_id)) {
		return;
	}

	b3WorldDef def = b3DefaultWorldDef();
	if (default_area) {
		def.gravity = b3_from_godot(default_area->gravity_vector * default_area->gravity);
	}

	// Single-threaded for now. Box3D takes an enqueueTask/finishTask callback
	// pair plus a workerCount; handing it a real thread pool is its own step and
	// should not be mixed into first-light bring-up.
	def.workerCount = 1;

	world_id = b3CreateWorld(&def);
}

void Box3DSpace::destroy_world() {
	if (B3_IS_NULL(world_id)) {
		return;
	}

	// Bodies belong to the world, so b3DestroyWorld takes them with it. Clear
	// the Godot-side handles first so nothing is left pointing into freed
	// storage — the bookkeeping survives, only the solver objects go.
	for (int i = 0; i < bodies.size(); i++) {
		bodies.write[i]->body_id = b3_nullBodyId;
		for (int j = 0; j < bodies[i]->shapes.size(); j++) {
			bodies.write[i]->shapes.write[j].shape_id = b3_nullShapeId;
			bodies.write[i]->shapes.write[j].free_owned_geometry();
		}
	}
	bodies.clear();

	b3DestroyWorld(world_id);
	world_id = b3_nullWorldId;
}

void Box3DSpace::update_gravity() {
	if (B3_IS_NULL(world_id) || !default_area) {
		return;
	}
	b3World_SetGravity(world_id, b3_from_godot(default_area->gravity_vector * default_area->gravity));
}

void Box3DSpace::add_body(Box3DBody *p_body) {
	if (bodies.find(p_body) == -1) {
		bodies.push_back(p_body);
	}
}

void Box3DSpace::remove_body(Box3DBody *p_body) {
	bodies.erase(p_body);
}

void Box3DSpace::add_area(Box3DArea *p_area) {
	if (areas.find(p_area) == -1) {
		areas.push_back(p_area);
	}
}

void Box3DSpace::remove_area(Box3DArea *p_area) {
	areas.erase(p_area);
}

// Godot's Area expects _body_inout(status, body_rid, instance_id, body_shape,
// area_shape); area-vs-area monitoring takes the same shape.
static void b3_dispatch_area_event(ObjectID p_receiver, const StringName &p_method,
		int p_status, const Box3DCollisionObject *p_other, int p_other_shape, int p_self_shape) {
	if (!p_receiver || p_method == StringName()) {
		return;
	}
	Object *obj = ObjectDB::get_instance(p_receiver);
	if (!obj) {
		return;
	}

	Variant v_status = p_status;
	Variant v_rid = p_other->self;
	Variant v_instance = p_other->instance_id;
	Variant v_other_shape = p_other_shape;
	Variant v_self_shape = p_self_shape;
	const Variant *argv[5] = { &v_status, &v_rid, &v_instance, &v_other_shape, &v_self_shape };

	Variant::CallError ce;
	obj->call(p_method, argv, 5, ce);
}

void Box3DSpace::flush_sensor_events() {
	if (B3_IS_NULL(world_id)) {
		return;
	}

	const b3SensorEvents events = b3World_GetSensorEvents(world_id);

	for (int i = 0; i < events.beginCount; i++) {
		int sensor_shape = 0;
		int visitor_shape = 0;
		Box3DCollisionObject *sensor = b3_object_from_shape(events.beginEvents[i].sensorShapeId, &sensor_shape);
		Box3DCollisionObject *visitor = b3_object_from_shape(events.beginEvents[i].visitorShapeId, &visitor_shape);
		if (!sensor || !visitor || sensor->type != Box3DCollisionObject::TYPE_AREA) {
			continue;
		}

		Box3DArea *area = static_cast<Box3DArea *>(sensor);
		if (visitor->type == Box3DCollisionObject::TYPE_BODY) {
			// Remembered so gravity/damp overrides can be applied each step.
			Box3DBody *vb = static_cast<Box3DBody *>(visitor);
			if (vb->areas.find(area) == -1) {
				vb->areas.push_back(area);
			}
			b3_dispatch_area_event(area->monitor_callback_id, area->monitor_callback_method,
					PhysicsServer::AREA_BODY_ADDED, visitor, visitor_shape, sensor_shape);
		} else {
			b3_dispatch_area_event(area->area_monitor_callback_id, area->area_monitor_callback_method,
					PhysicsServer::AREA_BODY_ADDED, visitor, visitor_shape, sensor_shape);
		}
	}

	for (int i = 0; i < events.endCount; i++) {
		int sensor_shape = 0;
		int visitor_shape = 0;
		Box3DCollisionObject *sensor = b3_object_from_shape(events.endEvents[i].sensorShapeId, &sensor_shape);
		Box3DCollisionObject *visitor = b3_object_from_shape(events.endEvents[i].visitorShapeId, &visitor_shape);
		if (!sensor || !visitor || sensor->type != Box3DCollisionObject::TYPE_AREA) {
			continue;
		}

		Box3DArea *area = static_cast<Box3DArea *>(sensor);
		if (visitor->type == Box3DCollisionObject::TYPE_BODY) {
			static_cast<Box3DBody *>(visitor)->areas.erase(area);
			b3_dispatch_area_event(area->monitor_callback_id, area->monitor_callback_method,
					PhysicsServer::AREA_BODY_REMOVED, visitor, visitor_shape, sensor_shape);
		} else {
			b3_dispatch_area_event(area->area_monitor_callback_id, area->area_monitor_callback_method,
					PhysicsServer::AREA_BODY_REMOVED, visitor, visitor_shape, sensor_shape);
		}
	}
}

void Box3DSpace::forget_area(Box3DArea *p_area) {
	for (int i = 0; i < bodies.size(); i++) {
		bodies.write[i]->areas.erase(p_area);
	}
}

// Godot's per-area gravity/damp accumulation (mirrors BodySW::integrate_forces).
static void b3_accumulate_area(const Box3DArea *p_area, const Vector3 &p_body_pos,
		Vector3 *r_gravity, real_t *r_linear_damp, real_t *r_angular_damp) {
	if (p_area->gravity_is_point) {
		// gravity_vector is the point, in the area's local space.
		const Vector3 to_point = p_area->transform.xform(p_area->gravity_vector) - p_body_pos;
		if (p_area->gravity_distance_scale > 0) {
			const real_t falloff = Math::pow(to_point.length() * p_area->gravity_distance_scale + 1, (real_t)2.0);
			*r_gravity += to_point.normalized() * (p_area->gravity / falloff);
		} else {
			*r_gravity += to_point.normalized() * p_area->gravity;
		}
	} else {
		*r_gravity += p_area->gravity_vector * p_area->gravity;
	}
	*r_linear_damp += p_area->linear_damp;
	*r_angular_damp += p_area->angular_damp;
}

void Box3DSpace::apply_area_overrides() {
	if (B3_IS_NULL(world_id) || !default_area) {
		return;
	}

	const Vector3 world_gravity = default_area->gravity_vector * default_area->gravity;

	for (int i = 0; i < bodies.size(); i++) {
		Box3DBody *body = bodies[i];
		if (!body->in_world() || body->mode == PhysicsServer::BODY_MODE_STATIC ||
				body->mode == PhysicsServer::BODY_MODE_KINEMATIC) {
			continue;
		}

		Vector3 gravity;
		real_t area_linear_damp = 0;
		real_t area_angular_damp = 0;
		bool stopped = false;

		if (!body->areas.empty()) {
			// Highest priority first, matching Godot's ordering.
			Vector<Box3DArea *> sorted = body->areas;
			for (int a = 1; a < sorted.size(); a++) {
				Box3DArea *key = sorted[a];
				int j = a - 1;
				while (j >= 0 && sorted[j]->priority < key->priority) {
					sorted.write[j + 1] = sorted[j];
					j--;
				}
				sorted.write[j + 1] = key;
			}

			for (int a = 0; a < sorted.size() && !stopped; a++) {
				switch (sorted[a]->space_override_mode) {
					case PhysicsServer::AREA_SPACE_OVERRIDE_COMBINE:
					case PhysicsServer::AREA_SPACE_OVERRIDE_COMBINE_REPLACE: {
						b3_accumulate_area(sorted[a], body->transform.origin, &gravity, &area_linear_damp, &area_angular_damp);
						stopped = sorted[a]->space_override_mode == PhysicsServer::AREA_SPACE_OVERRIDE_COMBINE_REPLACE;
					} break;
					case PhysicsServer::AREA_SPACE_OVERRIDE_REPLACE:
					case PhysicsServer::AREA_SPACE_OVERRIDE_REPLACE_COMBINE: {
						gravity = Vector3();
						area_linear_damp = 0;
						area_angular_damp = 0;
						b3_accumulate_area(sorted[a], body->transform.origin, &gravity, &area_linear_damp, &area_angular_damp);
						stopped = sorted[a]->space_override_mode == PhysicsServer::AREA_SPACE_OVERRIDE_REPLACE;
					} break;
					default:
						break;
				}
			}
		}

		if (!stopped) {
			b3_accumulate_area(default_area, body->transform.origin, &gravity, &area_linear_damp, &area_angular_damp);
		}

		// A non-negative body damp REPLACES the accumulated area damp.
		if (body->linear_damp >= 0) {
			area_linear_damp = body->linear_damp;
		}
		if (body->angular_damp >= 0) {
			area_angular_damp = body->angular_damp;
		}
		b3Body_SetLinearDamping(body->body_id, (float)MAX((real_t)0.0, area_linear_damp));
		b3Body_SetAngularDamping(body->body_id, (float)MAX((real_t)0.0, area_angular_damp));

		// Box3D already integrates world_gravity * the body's gravity scale, so
		// only the DIFFERENCE from world gravity has to be applied. Bodies in no
		// overriding area therefore cost nothing and behave exactly as before.
		const Vector3 delta = (gravity - world_gravity) * body->gravity_scale;
		if (delta.length_squared() > CMP_EPSILON) {
			const float mass = b3Body_GetMass(body->body_id);
			if (mass > 0.0f) {
				b3Body_ApplyForceToCenter(body->body_id, b3_from_godot(delta * mass), true);
			}
		}
	}
}

// Pushes forces and torques accumulated through the PhysicsServer API into the
// solver.
//
// add_central_force(), add_force(), add_torque() and set_applied_force() only
// ever wrote to Box3DBody bookkeeping; nothing read it back, so every one of
// them silently did nothing. Godot's contract is that these persist until
// changed -- unlike impulses, which are one-shot -- so they are re-applied every
// step. Box3D clears its own force accumulator each step, which is exactly the
// behaviour that requires this.
void Box3DSpace::apply_accumulated_forces() {
	if (B3_IS_NULL(world_id)) {
		return;
	}

	for (int i = 0; i < bodies.size(); i++) {
		Box3DBody *body = bodies[i];
		if (!body->in_world() || body->mode == PhysicsServer::BODY_MODE_STATIC ||
				body->mode == PhysicsServer::BODY_MODE_KINEMATIC) {
			continue;
		}
		if (body->omit_force_integration) {
			continue;
		}

		if (body->applied_force.length_squared() > CMP_EPSILON) {
			b3Body_ApplyForceToCenter(body->body_id, b3_from_godot(body->applied_force), true);
		}
		if (body->applied_torque.length_squared() > CMP_EPSILON) {
			b3Body_ApplyTorque(body->body_id, b3_from_godot(body->applied_torque), true);
		}

		// Godot's applied force/torque are a PER-STEP accumulator, not a standing
		// value: add_central_force() is expected to be called every frame while
		// the force should act. Leaving them set compounds the caller's own
		// per-frame additions -- measured as 25x the correct displacement.
		body->applied_force = Vector3();
		body->applied_torque = Vector3();
	}
}

void Box3DSpace::step(real_t p_delta) {
	if (B3_IS_NULL(world_id)) {
		return;
	}

	last_step_delta = p_delta;

	// Must run before the step: Box3D clears applied forces after integrating.
	apply_area_overrides();
	apply_accumulated_forces();

	locked = true;
	b3World_Step(world_id, (float)p_delta, sub_step_count);
	locked = false;

	// Copy solver transforms back into the Godot-side bookkeeping. Static bodies
	// are skipped: the engine is the authority on those, and reading them back
	// would fight anything that moved them this frame.
	for (int i = 0; i < bodies.size(); i++) {
		Box3DBody *body = bodies[i];
		if (!body->in_world() || body->mode == PhysicsServer::BODY_MODE_STATIC) {
			continue;
		}

		body->transform = b3_transform_to_godot(b3Body_GetTransform(body->body_id));
		body->linear_velocity = b3_to_godot(b3Body_GetLinearVelocity(body->body_id));
		body->angular_velocity = b3_to_godot(b3Body_GetAngularVelocity(body->body_id));
	}
}

void Box3DSpace::call_queries() {
	// This is how a moving body reaches the scene. Godot's RigidBody does not
	// poll the server — it is pushed its state through the force-integration
	// callback (RigidBody::_direct_state_changed), which then applies the
	// transform to the node. Without this, bodies simulate correctly inside
	// Box3D and nothing on screen ever moves.
	for (int i = 0; i < bodies.size(); i++) {
		Box3DBody *body = bodies[i];
		if (!body->force_integration_callback_id) {
			continue;
		}

		Object *obj = ObjectDB::get_instance(body->force_integration_callback_id);
		if (!obj) {
			// The node is gone; drop the callback rather than resolving a dead
			// id every frame.
			body->force_integration_callback_id = 0;
			body->force_integration_callback_method = StringName();
			continue;
		}

		Box3DPhysicsDirectBodyState *state = Box3DPhysicsDirectBodyState::singleton;
		if (!state) {
			continue;
		}
		state->body = body;
		state->step_delta = last_step_delta;

		Variant v = state;
		const Variant *vp[2] = { &v, &body->force_integration_callback_udata };
		Variant::CallError ce;
		const int argc = (body->force_integration_callback_udata.get_type() == Variant::NIL) ? 1 : 2;
		obj->call(body->force_integration_callback_method, vp, argc, ce);
	}

	flush_sensor_events();

	// Still to flush here: the debug contact list.
}
