#include "box3d_space.h"
#include "box3d_types_converter.h"

#include "core/error_macros.h"

#define B3_QUERY_TODO(m_what) WARN_PRINT_ONCE("Box3D: " m_what "() is not implemented yet; returning no hit.")

Box3DPhysicsDirectSpaceState::Box3DPhysicsDirectSpaceState(Box3DSpace *p_space) :
		PhysicsDirectSpaceState(),
		space(p_space) {
}

int Box3DPhysicsDirectSpaceState::intersect_point(const Vector3 &p_point, ShapeResult *r_results, int p_result_max, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas) {
	B3_QUERY_TODO("intersect_point");
	return 0;
}

bool Box3DPhysicsDirectSpaceState::intersect_ray(const Vector3 &p_from, const Vector3 &p_to, RayResult &r_result, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas, bool p_pick_ray) {
	B3_QUERY_TODO("intersect_ray");
	return false;
}

int Box3DPhysicsDirectSpaceState::intersect_shape(const RID &p_shape, const Transform &p_xform, float p_margin, ShapeResult *r_results, int p_result_max, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas) {
	B3_QUERY_TODO("intersect_shape");
	return 0;
}

bool Box3DPhysicsDirectSpaceState::cast_motion(const RID &p_shape, const Transform &p_xform, const Vector3 &p_motion, float p_margin, float &r_closest_safe, float &r_closest_unsafe, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas, ShapeRestInfo *r_info) {
	B3_QUERY_TODO("cast_motion");
	// "Whole motion is safe" is the correct no-obstacle answer; returning 0 here
	// would instead read as "blocked immediately" and freeze every caller.
	r_closest_safe = 1.0;
	r_closest_unsafe = 1.0;
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
