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
static b3QueryFilter b3_query_filter(uint32_t p_collision_mask) {
	b3QueryFilter filter = b3DefaultQueryFilter();
	filter.categoryBits = ~(uint64_t)0;
	filter.maskBits = (uint64_t)p_collision_mask;
	return filter;
}

// Resolves a hit shape back to the Godot side. Returns nullptr if the shape has
// no Box3DBody behind it.
static Box3DBody *b3_body_from_shape(b3ShapeId p_shape_id, int *r_shape_index) {
	b3BodyId body_id = b3Shape_GetBody(p_shape_id);
    if (B3_IS_NULL(body_id)) {
		return nullptr;
	}

	Box3DBody *body = static_cast<Box3DBody *>(b3Body_GetUserData(body_id));
	if (!body) {
		return nullptr;
	}

	if (r_shape_index) {
		*r_shape_index = 0;
		for (int i = 0; i < body->shapes.size(); i++) {
			if (B3_ID_EQUALS(body->shapes[i].shape_id, p_shape_id)) {
				*r_shape_index = i;
				break;
			}
		}
	}
	return body;
}

// Shared context for the closest-hit ray and shape casts.
struct Box3DClosestHitContext {
	const Set<RID> *exclude = nullptr;
	bool collide_with_bodies = true;

	bool hit = false;
	float fraction = 1.0f;
	Vector3 point;
	Vector3 normal;
	Box3DBody *body = nullptr;
	int shape_index = 0;
};

static float b3_closest_hit_callback(b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction,
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

void Box3DSpace::step(real_t p_delta) {
	if (B3_IS_NULL(world_id)) {
		return;
	}

	b3World_Step(world_id, (float)p_delta, sub_step_count);

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
	// Flush deferred callbacks: body force-integration callbacks, area
	// monitor/area-monitor enter-exit events, and the debug contact list.
}
