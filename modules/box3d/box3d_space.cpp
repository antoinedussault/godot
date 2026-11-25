#include "box3d_space.h"
#include "box3d_collide.h"
#include "box3d_physics_server.h"
#include "box3d_types_converter.h"

#include "box3d/constants.h"

#include "core/error_macros.h"
#include "core/project_settings.h"

// Scene queries.
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
//
// p_shape_id can be stale: sensor overlap events are captured during
// b3World_Step() and only resolved here a few calls later, once
// Box3DSpace::call_queries() reaches flush_sensor_events(). A force-integration
// callback earlier in call_queries() (or anything else that runs between the
// step and this resolve) can destroy/rebuild the very shape an event refers to
// -- shape_set_data, body_add_shape and friends all destroy-and-recreate the b3
// side, which bumps its generation. b3Shape_GetBody() asserts on a generation
// mismatch instead of failing softly, so a stale id has to be caught here first
// (this is Box3D's own documented contract -- see the b3Shape_IsValid comment
// on b3World_OverlapAABB in box3d.h).
Box3DCollisionObject *b3_object_from_shape(b3ShapeId p_shape_id, int *r_shape_index) {
	if (!b3Shape_IsValid(p_shape_id)) {
		return nullptr;
	}

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
	Box3DCollisionObject *object = b3_object_from_shape(shapeId, &shape_index);
	// -1 means "ignore this shape and keep going", which is what an excluded or
	// unresolvable hit needs. Returning 0 would terminate the whole cast.
	if (!object || (object->type == Box3DCollisionObject::TYPE_BODY ? !ctx->collide_with_bodies : !ctx->collide_with_areas)) {
		return -1.0f;
	}
	if (ctx->exclude && ctx->exclude->has(object->self)) {
		return -1.0f;
	}
	if (ctx->pick_ray && !object->ray_pickable) {
		return -1.0f;
	}
	if (ctx->skip_dynamic && object->type == Box3DCollisionObject::TYPE_BODY && static_cast<Box3DBody *>(object)->is_dynamic()) {
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
	ctx->object = object;
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
	bool collide_with_areas = false;
	PhysicsDirectSpaceState::ShapeResult *results = nullptr;
	int result_max = 0;
	int count = 0;
};

static bool b3_overlap_callback(b3ShapeId shapeId, void *context) {
	Box3DOverlapContext *ctx = static_cast<Box3DOverlapContext *>(context);

	int shape_index = 0;
	Box3DCollisionObject *body = b3_object_from_shape(shapeId, &shape_index);
	if (!body || (body->type == Box3DCollisionObject::TYPE_BODY ? !ctx->collide_with_bodies : !ctx->collide_with_areas)) {
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
	ctx.collide_with_areas = p_collide_with_areas;
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
	ctx.collide_with_areas = p_collide_with_areas;

	ctx.pick_ray = p_pick_ray;

	// Box3D takes an origin plus a translation, not two endpoints.
	b3World_CastRay(space->world_id,
			b3_pos_from_godot(p_from),
			b3_from_godot(p_to - p_from),
			b3_query_filter(p_collision_mask),
			b3_closest_hit_callback,
			&ctx);

	if (!ctx.hit || !ctx.object) {
		return false;
	}

	r_result.position = ctx.point;
	r_result.normal = ctx.normal;
	r_result.rid = ctx.object->self;
	r_result.collider_id = ctx.object->instance_id;
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
	ctx.collide_with_areas = p_collide_with_areas;
	ctx.results = r_results;
	ctx.result_max = p_result_max;

	b3World_OverlapShape(space->world_id, b3_pos_from_godot(Vector3()), &proxy,
			b3_query_filter(p_collision_mask), b3_overlap_callback, &ctx);

	return ctx.count;
}

static Vector3 b3_velocity_at(const Box3DCollisionObject *p_object, const Vector3 &p_point) {
	if (p_object->type != Box3DCollisionObject::TYPE_BODY) {
		return Vector3();
	}
	return static_cast<const Box3DBody *>(p_object)->get_velocity_at(p_point - p_object->transform.origin);
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
	proxy.radius += (float)p_margin;

	Box3DClosestHitContext ctx;
	ctx.exclude = &p_exclude;
	ctx.collide_with_bodies = p_collide_with_bodies;
	ctx.collide_with_areas = p_collide_with_areas;

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

	// Godot separates the last provably-free fraction from the first touching one.
	// Box3D returns a single impact fraction, so keep a hair on either side of it.
	const real_t length = p_motion.length();
	const real_t gap = length > CMP_EPSILON ? (real_t)0.001 / length : (real_t)0.0;
	r_closest_safe = CLAMP(ctx.fraction - gap, (real_t)0.0, (real_t)1.0);
	r_closest_unsafe = ctx.fraction > 0 ? CLAMP(ctx.fraction + gap, (real_t)0.0, (real_t)1.0) : 0.0;

	if (r_info && ctx.object) {
		r_info->point = ctx.point;
		r_info->normal = ctx.normal;
		r_info->rid = ctx.object->self;
		r_info->collider_id = ctx.object->instance_id;
		r_info->shape = ctx.shape_index;
		r_info->linear_velocity = b3_velocity_at(ctx.object, ctx.point);
	}
	return true;
}

bool Box3DPhysicsDirectSpaceState::collide_shape(RID p_shape, const Transform &p_shape_xform, float p_margin, Vector3 *r_results, int p_result_max, int &r_result_count, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas) {
	r_result_count = 0;
	ERR_FAIL_NULL_V(space, false);
	if (B3_IS_NULL(space->world_id) || p_result_max <= 0) {
		return false;
	}
	Box3DShape *shape = Box3DPhysicsServer::get_box3d_singleton()->get_shape(p_shape);
	ERR_FAIL_NULL_V(shape, false);

	Box3DCollideFilter filter;
	filter.exclude = &p_exclude;
	filter.mask = p_collision_mask;
	filter.bodies = p_collide_with_bodies;
	filter.areas = p_collide_with_areas;

	Vector<Box3DContact> contacts;
	b3_collide_query(space->world_id, shape, p_shape_xform, p_margin, filter, contacts);

	// Pairs: the point on the query shape, then the one on what it touches.
	r_result_count = MIN(contacts.size(), p_result_max);
	for (int i = 0; i < r_result_count; i++) {
		r_results[i * 2 + 0] = contacts[i].point_on_query();
		r_results[i * 2 + 1] = contacts[i].point_on_other();
	}
	return r_result_count > 0;
}

bool Box3DPhysicsDirectSpaceState::rest_info(RID p_shape, const Transform &p_shape_xform, float p_margin, ShapeRestInfo *r_info, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas) {
	ERR_FAIL_NULL_V(space, false);
	if (B3_IS_NULL(space->world_id)) {
		return false;
	}
	Box3DShape *shape = Box3DPhysicsServer::get_box3d_singleton()->get_shape(p_shape);
	ERR_FAIL_NULL_V(shape, false);

	Box3DCollideFilter filter;
	filter.exclude = &p_exclude;
	filter.mask = p_collision_mask;
	filter.bodies = p_collide_with_bodies;
	filter.areas = p_collide_with_areas;

	Vector<Box3DContact> contacts;
	b3_collide_query(space->world_id, shape, p_shape_xform, p_margin, filter, contacts);

	int best = -1;
	for (int i = 0; i < contacts.size(); i++) {
		if (best < 0 || contacts[i].separation < contacts[best].separation) {
			best = i;
		}
	}
	if (best < 0) {
		return false;
	}

	const Box3DContact &c = contacts[best];
	r_info->point = c.point_on_other();
	r_info->normal = c.normal;
	r_info->rid = c.other->self;
	r_info->collider_id = c.other->instance_id;
	r_info->shape = c.other_shape;
	r_info->linear_velocity = b3_velocity_at(c.other, r_info->point);
	return true;
}

// Closest triangle corner search for shapes GJK cannot take whole.
struct Box3DClosestTriangleContext {
	b3Vec3 target;
	b3Vec3 best;
	float distance_squared = FLT_MAX;
};

static bool b3_closest_triangle(b3Vec3 a, b3Vec3 b, b3Vec3 c, int triangleIndex, void *context) {
	Box3DClosestTriangleContext *ctx = static_cast<Box3DClosestTriangleContext *>(context);
	const b3Vec3 triangle[3] = { a, b, c };

	b3DistanceInput input = {};
	input.proxyA.points = triangle;
	input.proxyA.count = 3;
	input.proxyB.points = &ctx->target;
	input.proxyB.count = 1;
	input.transform = b3Transform_identity;
	b3SimplexCache cache = {};
	const b3DistanceOutput out = b3ShapeDistance(&input, &cache, nullptr, 0);

	const float d = out.distance * out.distance;
	if (d < ctx->distance_squared) {
		ctx->distance_squared = d;
		ctx->best = out.pointA;
	}
	return true;
}

Vector3 Box3DPhysicsDirectSpaceState::get_closest_point_to_object_volume(RID p_object, const Vector3 p_point) const {
	Box3DCollisionObject *object = Box3DPhysicsServer::get_box3d_singleton()->get_collision_object(p_object);
	ERR_FAIL_NULL_V(object, Vector3());
	ERR_FAIL_COND_V(object->space != space->self, Vector3());

	Vector3 best = object->transform.origin;
	real_t best_distance = 1e20;
	for (int i = 0; i < object->shapes.size(); i++) {
		const b3ShapeId id = object->shapes[i].shape_id;
		if (B3_IS_NULL(id)) {
			continue;
		}

		Vector3 p;
		const b3ShapeType type = b3Shape_GetType(id);
		if (type == b3_meshShape || type == b3_heightShape) {
			const Transform xf = b3_transform_to_godot(b3Body_GetTransform(object->body_id));
			Box3DClosestTriangleContext ctx;
			ctx.target = b3_from_godot(xf.xform_inv(p_point));
			b3AABB everything;
			const b3AABB world = b3Shape_GetAABB(id);
			// The shape's own bounds in its own frame, with room for rounding.
			const b3Transform inv = b3InvMulTransforms(b3Body_GetTransform(object->body_id), b3Transform_identity);
			everything = b3AABB_Transform(inv, world);
			if (type == b3_meshShape) {
				const b3Mesh mesh = b3Shape_GetMesh(id);
				b3QueryMesh(&mesh, everything, b3_closest_triangle, &ctx);
			} else {
				b3QueryHeightField(b3Shape_GetHeightField(id), everything, b3_closest_triangle, &ctx);
			}
			if (ctx.distance_squared == FLT_MAX) {
				continue;
			}
			p = xf.xform(b3_to_godot(ctx.best));
		} else {
			p = b3_to_godot(b3Shape_GetClosestPoint(id, b3_from_godot(p_point)));
		}

		const real_t d = p.distance_squared_to(p_point);
		if (d < best_distance) {
			best_distance = d;
			best = p;
		}
	}
	return best;
}

// Godot's rules, for both simulation and sensors. Runs on Box3D's worker threads:
// read only.
static bool b3_custom_filter(b3ShapeId shapeIdA, b3ShapeId shapeIdB, void *context) {
	const Box3DCollisionObject *a = static_cast<Box3DCollisionObject *>(b3Body_GetUserData(b3Shape_GetBody(shapeIdA)));
	const Box3DCollisionObject *b = static_cast<Box3DCollisionObject *>(b3Body_GetUserData(b3Shape_GetBody(shapeIdB)));
	if (!a || !b) {
		return false;
	}
	// CollisionObjectSW::test_collision_mask: one side is enough.
	if (!(a->collision_layer & b->collision_mask) && !(b->collision_layer & a->collision_mask)) {
		return false;
	}

	const bool a_is_area = a->type == Box3DCollisionObject::TYPE_AREA;
	const bool b_is_area = b->type == Box3DCollisionObject::TYPE_AREA;
	if (a_is_area && b_is_area) {
		// The sensor comes first; the visitor has to be monitorable.
		return static_cast<const Box3DArea *>(b)->monitorable;
	}
	if (a_is_area || b_is_area) {
		return true;
	}
	return !static_cast<const Box3DBody *>(a)->collision_exceptions.has(b->self) &&
			!static_cast<const Box3DBody *>(b)->collision_exceptions.has(a->self);
}

// The mixing rules of the other backends (body_pair_sw.cpp). A negative value means
// rough or absorbent, and Box3D cannot carry it, see _body_make_material().
static float b3_mix_friction(float frictionA, uint64_t materialA, float frictionB, uint64_t materialB) {
	const float a = (materialA & B3_MATERIAL_ROUGH) ? -frictionA : frictionA;
	const float b = (materialB & B3_MATERIAL_ROUGH) ? -frictionB : frictionB;
	return Math::abs(MIN(a, b));
}

static float b3_mix_restitution(float restitutionA, uint64_t materialA, float restitutionB, uint64_t materialB) {
	const float a = (materialA & B3_MATERIAL_ABSORBENT) ? -restitutionA : restitutionA;
	const float b = (materialB & B3_MATERIAL_ABSORBENT) ? -restitutionB : restitutionB;
	return CLAMP(a + b, 0.0f, 1.0f);
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

	def.frictionCallback = b3_mix_friction;
	def.restitutionCallback = b3_mix_restitution;

	// Above one, Box3D runs its own scheduler with that many threads.
	def.workerCount = MAX(1, (int)GLOBAL_GET("physics/3d/box3d/worker_count"));

	world_id = b3CreateWorld(&def);
	b3World_SetCustomFilterCallback(world_id, b3_custom_filter, nullptr);
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
	areas.clear();
	overlaps.clear();
	area_events.clear();

	b3DestroyWorld(world_id);
	world_id = b3_nullWorldId;
	ground_body = b3_nullBodyId;
}

b3BodyId Box3DSpace::get_ground_body() {
	if (B3_IS_NULL(ground_body)) {
		b3BodyDef def = b3DefaultBodyDef();
		def.type = b3_staticBody;
		ground_body = b3CreateBody(world_id, &def);
	}
	return ground_body;
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

void Box3DSpace::queue_area_event(const Box3DOverlap &p_overlap, int p_status) {
	Box3DAreaEvent event;
	event.area = p_overlap.area;
	event.status = p_status;
	event.other_is_area = p_overlap.other_is_area;
	event.other_rid = p_overlap.other_rid;
	event.other_id = p_overlap.other_id;
	event.other_shape = p_overlap.other_shape;
	event.self_shape = p_overlap.self_shape;
	area_events.push_back(event);
}

static bool b3_has_monitor(const Box3DArea *p_area, bool p_areas) {
	return (p_areas ? p_area->area_monitor_callback_id : p_area->monitor_callback_id) != 0;
}

void Box3DSpace::end_overlap(Map<Box3DOverlapKey, Box3DOverlap>::Element *p_element) {
	const Box3DOverlap overlap = p_element->get();
	overlaps.erase(p_element);

	if (!overlap.other_is_area && overlap.other) {
		Map<Box3DArea *, int> &map = static_cast<Box3DBody *>(overlap.other)->areas;
		Map<Box3DArea *, int>::Element *E = map.find(overlap.area);
		if (E && --E->get() <= 0) {
			map.erase(E);
		}
	}
	if (overlap.reported) {
		queue_area_event(overlap, PhysicsServer::AREA_BODY_REMOVED);
	}
}

void Box3DSpace::collect_sensor_events() {
	const b3SensorEvents events = b3World_GetSensorEvents(world_id);

	// Ends first: a shape that was rebuilt ends under its old id and begins under
	// its new one in the same step.
	for (int i = 0; i < events.endCount; i++) {
		Box3DOverlapKey key;
		key.sensor = b3StoreShapeId(events.endEvents[i].sensorShapeId);
		key.visitor = b3StoreShapeId(events.endEvents[i].visitorShapeId);
		// Missing if it was ended by hand when one of the two was destroyed.
		if (Map<Box3DOverlapKey, Box3DOverlap>::Element *E = overlaps.find(key)) {
			end_overlap(E);
		}
	}

	for (int i = 0; i < events.beginCount; i++) {
		Box3DOverlap overlap;
		Box3DCollisionObject *sensor = b3_object_from_shape(events.beginEvents[i].sensorShapeId, &overlap.self_shape);
		Box3DCollisionObject *visitor = b3_object_from_shape(events.beginEvents[i].visitorShapeId, &overlap.other_shape);
		if (!sensor || !visitor || sensor->type != Box3DCollisionObject::TYPE_AREA) {
			continue;
		}

		Box3DOverlapKey key;
		key.sensor = b3StoreShapeId(events.beginEvents[i].sensorShapeId);
		key.visitor = b3StoreShapeId(events.beginEvents[i].visitorShapeId);
		if (overlaps.has(key)) {
			continue;
		}

		overlap.area = static_cast<Box3DArea *>(sensor);
		overlap.other = visitor;
		overlap.other_rid = visitor->self;
		overlap.other_id = visitor->instance_id;
		overlap.other_is_area = visitor->type == Box3DCollisionObject::TYPE_AREA;
		if (!overlap.other_is_area) {
			// Remembered so gravity/damp overrides can be applied each step.
			Map<Box3DArea *, int> &map = static_cast<Box3DBody *>(visitor)->areas;
			Map<Box3DArea *, int>::Element *E = map.find(overlap.area);
			if (E) {
				E->get()++;
			} else {
				map.insert(overlap.area, 1);
			}
		}
		if (b3_has_monitor(overlap.area, overlap.other_is_area)) {
			overlap.reported = true;
			queue_area_event(overlap, PhysicsServer::AREA_BODY_ADDED);
		}
		overlaps.insert(key, overlap);
	}
}

// Godot's Area expects _body_inout(status, body_rid, instance_id, body_shape,
// area_shape); area-vs-area monitoring takes the same shape.
void Box3DSpace::flush_area_events() {
	// Callbacks run arbitrary game code, which may queue events, purge them, or free
	// the very areas they are about. Take them one at a time, and leave any new ones
	// for the next flush.
	int remaining = area_events.size();
	while (remaining-- > 0 && area_events.front()) {
		const Box3DAreaEvent event = area_events.front()->get();
		area_events.pop_front();

		const ObjectID receiver = event.other_is_area ? event.area->area_monitor_callback_id : event.area->monitor_callback_id;
		const StringName method = event.other_is_area ? event.area->area_monitor_callback_method : event.area->monitor_callback_method;
		Object *obj = receiver ? ObjectDB::get_instance(receiver) : nullptr;
		if (!obj) {
			continue;
		}

		Variant v_status = event.status;
		Variant v_rid = event.other_rid;
		Variant v_instance = event.other_id;
		Variant v_other_shape = event.other_shape;
		Variant v_self_shape = event.self_shape;
		const Variant *argv[5] = { &v_status, &v_rid, &v_instance, &v_other_shape, &v_self_shape };

		Variant::CallError ce;
		obj->call(method, argv, 5, ce);
	}
}

void Box3DSpace::monitor_changed(Box3DArea *p_area, bool p_areas) {
	const bool monitoring = b3_has_monitor(p_area, p_areas);

	if (!monitoring) {
		// The node forgets everything it was told, so there is nothing left to undo.
		for (List<Box3DAreaEvent>::Element *E = area_events.front(); E;) {
			List<Box3DAreaEvent>::Element *N = E->next();
			if (E->get().area == p_area && E->get().other_is_area == p_areas) {
				area_events.erase(E);
			}
			E = N;
		}
	}

	for (Map<Box3DOverlapKey, Box3DOverlap>::Element *E = overlaps.front(); E; E = E->next()) {
		Box3DOverlap &overlap = E->get();
		if (overlap.area != p_area || overlap.other_is_area != p_areas || overlap.reported == monitoring) {
			continue;
		}
		overlap.reported = monitoring;
		if (monitoring) {
			// It was already inside when monitoring started.
			queue_area_event(overlap, PhysicsServer::AREA_BODY_ADDED);
		}
	}
}

void Box3DSpace::forget_area(Box3DArea *p_area) {
	for (Map<Box3DOverlapKey, Box3DOverlap>::Element *E = overlaps.front(); E;) {
		Map<Box3DOverlapKey, Box3DOverlap>::Element *N = E->next();
		if (E->get().area == p_area || E->get().other == p_area) {
			end_overlap(E);
		}
		E = N;
	}
	// Nothing may be delivered to it any more.
	for (List<Box3DAreaEvent>::Element *E = area_events.front(); E;) {
		List<Box3DAreaEvent>::Element *N = E->next();
		if (E->get().area == p_area) {
			area_events.erase(E);
		}
		E = N;
	}
}

void Box3DSpace::forget_body(Box3DBody *p_body) {
	if (p_body->areas.empty()) {
		return; // By far the most common case, and it keeps streaming cheap.
	}
	for (Map<Box3DOverlapKey, Box3DOverlap>::Element *E = overlaps.front(); E;) {
		Map<Box3DOverlapKey, Box3DOverlap>::Element *N = E->next();
		if (E->get().other == p_body) {
			end_overlap(E);
		}
		E = N;
	}
}

void Box3DSpace::forget_sensor(b3ShapeId p_shape) {
	const uint64_t sensor = b3StoreShapeId(p_shape);
	for (Map<Box3DOverlapKey, Box3DOverlap>::Element *E = overlaps.front(); E;) {
		Map<Box3DOverlapKey, Box3DOverlap>::Element *N = E->next();
		if (E->key().sensor == sensor) {
			end_overlap(E);
		}
		E = N;
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
			Vector<Box3DArea *> sorted;
			for (Map<Box3DArea *, int>::Element *E = body->areas.front(); E; E = E->next()) {
				sorted.push_back(E->key());
			}
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
		gravity *= body->gravity_scale;
		body->total_gravity = gravity;
		body->total_linear_damp = area_linear_damp;
		body->total_angular_damp = area_angular_damp;

		if (body->omit_force_integration) {
			// The body integrates its own forces: no gravity, damping or applied forces.
			b3Body_SetGravityScale(body->body_id, 0.0f);
			b3Body_SetLinearDamping(body->body_id, 0.0f);
			b3Body_SetAngularDamping(body->body_id, 0.0f);
			continue;
		}

		b3Body_SetGravityScale(body->body_id, (float)body->gravity_scale);
		b3Body_SetLinearDamping(body->body_id, (float)MAX((real_t)0.0, area_linear_damp));
		b3Body_SetAngularDamping(body->body_id, (float)MAX((real_t)0.0, area_angular_damp));

		// Box3D already integrates world_gravity * the body's gravity scale, so
		// only the DIFFERENCE from world gravity has to be applied. Bodies in no
		// overriding area therefore cost nothing and behave exactly as before.
		// A resting body must be allowed to fall asleep in an override, so don't wake.
		const Vector3 delta = gravity - world_gravity * body->gravity_scale;
		if (delta.length_squared() > CMP_EPSILON) {
			const float mass = b3Body_GetMass(body->body_id);
			if (mass > 0.0f) {
				b3Body_ApplyForceToCenter(body->body_id, b3_from_godot(delta * mass), false);
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
			body->applied_force = Vector3();
			body->applied_torque = Vector3();
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

// A kinematic body moved further than this in one step was teleported. Replaying
// that as velocity would sweep it through everything in between.
#define B3_KINEMATIC_TELEPORT_DISTANCE 4.0

void Box3DSpace::begin_kinematics(real_t p_delta) {
	for (int i = 0; i < bodies.size(); i++) {
		Box3DBody *body = bodies[i];
		if (body->mode != PhysicsServer::BODY_MODE_KINEMATIC || !body->in_world()) {
			continue;
		}

		if (!body->kinematic_moved) {
			if (body->kinematic_has_velocity) {
				// It stopped. Without this it would coast on its last velocity.
				const b3Vec3 zero = { 0.0f, 0.0f, 0.0f };
				b3Body_SetLinearVelocity(body->body_id, zero);
				b3Body_SetAngularVelocity(body->body_id, zero);
				body->linear_velocity = Vector3();
				body->angular_velocity = Vector3();
				body->kinematic_has_velocity = false;
			}
			continue;
		}

		const Transform target = body->get_solver_transform();
		const Transform &from = body->kinematic_from;
		if (p_delta > 0 && from.origin.distance_to(target.origin) < B3_KINEMATIC_TELEPORT_DISTANCE) {
			b3Body_SetTransform(body->body_id, b3_pos_from_godot(from.origin), b3_quat_from_godot(from.basis.get_rotation_quat()));
			b3WorldTransform xf;
			xf.p = b3_pos_from_godot(target.origin);
			xf.q = b3_quat_from_godot(target.basis.get_rotation_quat());
			b3Body_SetTargetTransform(body->body_id, xf, (float)p_delta, true);
			body->linear_velocity = b3_to_godot(b3Body_GetLinearVelocity(body->body_id));
			body->angular_velocity = b3_to_godot(b3Body_GetAngularVelocity(body->body_id));
			body->kinematic_has_velocity = true;
		} else {
			const b3Vec3 zero = { 0.0f, 0.0f, 0.0f };
			b3Body_SetLinearVelocity(body->body_id, zero);
			b3Body_SetAngularVelocity(body->body_id, zero);
			body->linear_velocity = Vector3();
			body->angular_velocity = Vector3();
		}
		body->kinematic_moved = false;
		body->kinematic_settle = true;
	}
}

void Box3DSpace::end_kinematics() {
	for (int i = 0; i < bodies.size(); i++) {
		Box3DBody *body = bodies[i];
		if (!body->kinematic_settle) {
			continue;
		}
		body->kinematic_settle = false;
		// Godot is the authority on where it is; integration only gets close.
		const Transform target = body->get_solver_transform();
		b3Body_SetTransform(body->body_id, b3_pos_from_godot(target.origin), b3_quat_from_godot(target.basis.get_rotation_quat()));
	}
}

bool Box3DSpace::cast_ray_shape(const Box3DBody *p_body, int p_shape, const Transform &p_xform, const Set<RID> &p_exclude, uint32_t p_mask, bool p_skip_dynamic, Box3DRayHit *r_hit) {
	if (p_body->shapes[p_shape].disabled) {
		return false;
	}
	const Box3DShape *shape = Box3DPhysicsServer::get_box3d_singleton()->get_shape(p_body->shapes[p_shape].shape);
	real_t length = 0;
	bool slips = false;
	if (!shape || !shape->get_ray(length, slips)) {
		return false;
	}

	const Transform xf = p_xform * p_body->shapes[p_shape].xform;
	const Vector3 ray = xf.basis.get_axis(2) * length;
	const real_t world_length = ray.length();
	if (world_length <= CMP_EPSILON) {
		return false;
	}

	Box3DClosestHitContext ctx;
	ctx.exclude = &p_exclude;
	ctx.skip_dynamic = p_skip_dynamic;
	b3World_CastRay(world_id, b3_pos_from_godot(xf.origin), b3_from_godot(ray), b3_query_filter(p_mask), b3_closest_hit_callback, &ctx);
	if (!ctx.hit || !ctx.object || ctx.fraction <= 0.0f) {
		return false;
	}

	const Vector3 dir = ray / world_length;
	const real_t along = world_length * (1.0 - ctx.fraction);
	r_hit->point = ctx.point;
	r_hit->body = static_cast<Box3DBody *>(ctx.object);
	r_hit->shape = ctx.shape_index;
	if (slips && ctx.normal.length_squared() > CMP_EPSILON) {
		r_hit->normal = ctx.normal.normalized();
		// How far to move along the normal to pull the ray's tip out.
		r_hit->depth = along * Math::abs(r_hit->normal.dot(dir));
	} else {
		r_hit->normal = -dir;
		r_hit->depth = along;
	}
	return true;
}

void Box3DSpace::solve_rays(real_t p_delta) {
	for (int i = 0; i < bodies.size(); i++) {
		Box3DBody *body = bodies[i];
		if (body->ray_shape_count == 0 || !body->in_world() || !body->is_dynamic() || !b3Body_IsAwake(body->body_id)) {
			continue;
		}

		Set<RID> exclude;
		exclude.insert(body->self);
		for (Set<RID>::Element *E = body->collision_exceptions.front(); E; E = E->next()) {
			exclude.insert(E->get());
		}

		for (int j = 0; j < body->shapes.size(); j++) {
			Box3DRayHit hit;
			if (!cast_ray_shape(body, j, body->transform, exclude, body->collision_mask, false, &hit)) {
				continue;
			}

			// Remove a fifth of the depth per step, at a speed that cannot launch
			// anything, and what gravity is about to add.
			const Vector3 r = hit.point - body->transform.origin - body->get_center_of_mass();
			const Vector3 vel = b3_to_godot(b3Body_GetLinearVelocity(body->body_id)) +
					b3_to_godot(b3Body_GetAngularVelocity(body->body_id)).cross(r) -
					hit.body->get_velocity_at(hit.point - hit.body->transform.origin);
			const real_t target = MIN(hit.depth * 0.2 / p_delta, 3.0) - MIN(body->total_gravity.dot(hit.normal), 0.0) * p_delta;
			const real_t speed = vel.dot(hit.normal);
			if (speed >= target) {
				continue;
			}

			const Vector3 rn = r.cross(hit.normal);
			const b3Matrix3 inv_inertia = b3Body_GetWorldInverseRotationalInertia(body->body_id);
			real_t k = b3Body_GetInverseMass(body->body_id) + rn.dot(b3_to_godot(b3MulMV(inv_inertia, b3_from_godot(rn))));
			const bool push_other = hit.body->is_dynamic() && hit.body->in_world();
			if (push_other) {
				k += b3Body_GetInverseMass(hit.body->body_id);
			}
			if (k <= 0) {
				continue;
			}
			const Vector3 impulse = hit.normal * ((target - speed) / k);
			b3Body_ApplyLinearImpulse(body->body_id, b3_from_godot(impulse), b3_pos_from_godot(hit.point), true);
			if (push_other) {
				b3Body_ApplyLinearImpulse(hit.body->body_id, b3_from_godot(-impulse), b3_pos_from_godot(hit.point), true);
			}
		}
	}
}

void Box3DSpace::collect_debug_contacts() {
	debug_contacts.resize(0);
	Vector<b3ContactData> data;
	for (int i = 0; i < bodies.size() && debug_contacts.size() < max_debug_contacts; i++) {
		if (!bodies[i]->in_world() || !bodies[i]->is_dynamic()) {
			continue;
		}
		data.resize(b3Body_GetContactCapacity(bodies[i]->body_id));
		if (data.empty()) {
			continue;
		}
		const int count = b3Body_GetContactData(bodies[i]->body_id, data.ptrw(), data.size());
		for (int j = 0; j < count; j++) {
			const Vector3 center = b3_pos_to_godot(b3Body_GetWorldCenter(b3Shape_GetBody(data[j].shapeIdA)));
			for (int m = 0; m < data[j].manifoldCount; m++) {
				for (int p = 0; p < data[j].manifolds[m].pointCount && debug_contacts.size() < max_debug_contacts; p++) {
					debug_contacts.push_back(center + b3_to_godot(data[j].manifolds[m].points[p].anchorA));
				}
			}
		}
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
	begin_kinematics(p_delta);
	solve_rays(p_delta);

	locked = true;
	b3World_Step(world_id, (float)p_delta, sub_step_count);
	locked = false;

	end_kinematics();

	// Copy solver transforms back into the Godot-side bookkeeping. Static and
	// kinematic bodies are skipped: the engine is the authority on those, and
	// reading them back would fight anything that moved them this frame.
	for (int i = 0; i < bodies.size(); i++) {
		Box3DBody *body = bodies[i];
		if (!body->in_world() || !body->is_dynamic()) {
			continue;
		}

		const bool sleeping = !b3Body_IsAwake(body->body_id);
		if (sleeping && body->sleeping) {
			continue; // Has not moved since it was last copied.
		}
		body->sleeping = sleeping;
		body->report_state = true;

		body->transform = b3_transform_to_godot(b3Body_GetTransform(body->body_id));
		// The scale lives in the shapes. Keep it in the transform too, or the node
		// would lose it, and setting the transform back would rebuild the shapes.
		if (body->baked_scale != Vector3(1, 1, 1)) {
			body->transform.basis.scale_local(body->baked_scale);
		}
		body->linear_velocity = b3_to_godot(b3Body_GetLinearVelocity(body->body_id));
		body->angular_velocity = b3_to_godot(b3Body_GetAngularVelocity(body->body_id));
	}

	collect_sensor_events();
	if (max_debug_contacts > 0) {
		collect_debug_contacts();
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
		if (body->is_dynamic()) {
			// A sleeping body is reported once, when it falls asleep.
			if (!body->report_state) {
				continue;
			}
			body->report_state = false;
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
		state->set_body(body, last_step_delta);

		Variant v = state;
		const Variant *vp[2] = { &v, &body->force_integration_callback_udata };
		Variant::CallError ce;
		const int argc = (body->force_integration_callback_udata.get_type() == Variant::NIL) ? 1 : 2;
		obj->call(body->force_integration_callback_method, vp, argc, ce);
	}

	flush_area_events();
}
