#include "box3d_physics_server.h"
#include "box3d_types_converter.h"

#include "core/error_macros.h"
#include "core/project_settings.h"

// Two distinct kinds of "not done":
//
//   B3_TODO         — planned, needs the Box3D solver underneath it.
//   B3_UNSUPPORTED  — Box3D has no equivalent and none is planned (soft bodies).
//
// Both warn once per call site, so a play session prints a precise, deduplicated
// list of what the project actually exercises.
#define B3_TODO(m_what) WARN_PRINT_ONCE("Box3D: " m_what " is not implemented yet.")
#define B3_UNSUPPORTED(m_what) WARN_PRINT_ONCE("Box3D: " m_what " is not supported by this backend.")

Box3DPhysicsServer::Box3DPhysicsServer() :
		PhysicsServer() {
}

Box3DPhysicsServer::~Box3DPhysicsServer() {
}

void Box3DPhysicsServer::_bind_methods() {
}

// Every "which kind of RID is this?" probe below uses owns() rather than
// getornull(). getornull() emits an ERROR on a miss, so probing three owners to
// identify one RID would print two errors for every successful lookup — which is
// exactly what free() did on shutdown.
Box3DCollisionObject *Box3DPhysicsServer::get_collision_object(RID p_rid) const {
	if (body_owner.owns(p_rid)) {
		return static_cast<Box3DCollisionObject *>(body_owner.getornull(p_rid));
	}
	if (area_owner.owns(p_rid)) {
		return static_cast<Box3DCollisionObject *>(area_owner.getornull(p_rid));
	}
	return nullptr;
}

/* SOLVER-SIDE BODY LIFETIME */

static b3BodyType b3_body_type_from_godot(PhysicsServer::BodyMode p_mode) {
	switch (p_mode) {
		case PhysicsServer::BODY_MODE_STATIC:
			return b3_staticBody;
		case PhysicsServer::BODY_MODE_KINEMATIC:
			return b3_kinematicBody;
		default:
			// RIGID and CHARACTER are both dynamic; CHARACTER additionally locks
			// rotation, which is applied through b3BodyDef::motionLocks.
			return b3_dynamicBody;
	}
}

void Box3DPhysicsServer::_body_enter_world(Box3DBody *p_body, Box3DSpace *p_space) {
	ERR_FAIL_NULL(p_body);
	ERR_FAIL_NULL(p_space);

	if (B3_IS_NULL(p_space->world_id)) {
		p_space->create_world();
	}
	ERR_FAIL_COND(B3_IS_NULL(p_space->world_id));

	b3BodyDef def = b3DefaultBodyDef();
	def.type = b3_body_type_from_godot(p_body->mode);
	def.position = b3_pos_from_godot(p_body->transform.origin);
	def.rotation = b3_quat_from_godot(p_body->transform.basis.get_rotation_quat());
	def.linearVelocity = b3_from_godot(p_body->linear_velocity);
	def.angularVelocity = b3_from_godot(p_body->angular_velocity);
	def.gravityScale = (float)p_body->gravity_scale;
	def.enableSleep = p_body->can_sleep;
	def.isAwake = !p_body->sleeping;

	// Lets a query result walk b3ShapeId -> b3BodyId -> Box3DBody in one hop,
	// which is how every scene query resolves what it hit.
	def.userData = p_body;

	// Godot treats a negative damp as "inherit from the area"; Box3D has no such
	// sentinel, so clamp it to zero here. Area-driven damping is a later pass.
	def.linearDamping = p_body->linear_damp >= 0 ? (float)p_body->linear_damp : 0.0f;
	def.angularDamping = p_body->angular_damp >= 0 ? (float)p_body->angular_damp : 0.0f;

	if (p_body->mode == PhysicsServer::BODY_MODE_CHARACTER) {
		def.motionLocks.angularX = true;
		def.motionLocks.angularY = true;
		def.motionLocks.angularZ = true;
	} else {
		def.motionLocks.linearX = (p_body->locked_axis & BODY_AXIS_LINEAR_X) != 0;
		def.motionLocks.linearY = (p_body->locked_axis & BODY_AXIS_LINEAR_Y) != 0;
		def.motionLocks.linearZ = (p_body->locked_axis & BODY_AXIS_LINEAR_Z) != 0;
		def.motionLocks.angularX = (p_body->locked_axis & BODY_AXIS_ANGULAR_X) != 0;
		def.motionLocks.angularY = (p_body->locked_axis & BODY_AXIS_ANGULAR_Y) != 0;
		def.motionLocks.angularZ = (p_body->locked_axis & BODY_AXIS_ANGULAR_Z) != 0;
	}

	p_body->body_id = b3CreateBody(p_space->world_id, &def);
	p_space->add_body(p_body);

	_body_rebuild_shapes(p_body);
}

void Box3DPhysicsServer::_body_exit_world(Box3DBody *p_body) {
	ERR_FAIL_NULL(p_body);

	if (Box3DSpace *space = space_owner.getornull(p_body->space)) {
		space->remove_body(p_body);
	}

	if (p_body->in_world()) {
		// Destroying the body takes its shapes with it, so the shape ids only
		// need clearing, not individual destruction.
		b3DestroyBody(p_body->body_id);
		p_body->body_id = b3_nullBodyId;
	}

	for (int i = 0; i < p_body->shapes.size(); i++) {
		p_body->shapes.write[i].shape_id = b3_nullShapeId;
	}
}

void Box3DPhysicsServer::_body_sync_transform(Box3DBody *p_body) {
	if (!p_body->in_world()) {
		return;
	}
	const Transform t = p_body->get_solver_transform();
	b3Body_SetTransform(p_body->body_id,
			b3_pos_from_godot(t.origin),
			b3_quat_from_godot(t.basis.get_rotation_quat()));
}

void Box3DPhysicsServer::_body_update_shape_origin_offset(Box3DBody *p_body) {
	p_body->has_shape_origin_offset = false;
	p_body->shape_origin_offset = Transform();

	int heightmap_index = -1;
	bool has_other_shapes = false;

	for (int i = 0; i < p_body->shapes.size(); i++) {
		if (p_body->shapes[i].disabled) {
			continue;
		}
		Box3DShape *shape = shape_owner.getornull(p_body->shapes[i].shape);
		if (!shape) {
			continue;
		}
		if (shape->type == SHAPE_HEIGHTMAP) {
			heightmap_index = i;
		} else {
			has_other_shapes = true;
		}
	}

	if (heightmap_index < 0) {
		return;
	}

	// Two cases the body-offset trick cannot express. Both warn rather than
	// silently placing terrain half a cell out, which is the failure that would
	// otherwise be found by walking off the edge of the world.
	if (p_body->mode != BODY_MODE_STATIC) {
		WARN_PRINT_ONCE("Box3D: HeightMapShape is only supported on static bodies.");
		return;
	}
	if (has_other_shapes) {
		WARN_PRINT_ONCE("Box3D: a HeightMapShape must be the only shape on its body; "
						"the height field cannot be offset independently of the other shapes.");
		return;
	}

	Box3DShape *shape = shape_owner.getornull(p_body->shapes[heightmap_index].shape);
	int width = 0;
	int depth = 0;
	if (!shape || !shape->get_heightmap_dimensions(width, depth)) {
		return;
	}

	// Godot centers the field on the shape's origin and spans (width-1) by
	// (depth-1) at one unit per grid step; Box3D runs from its origin in +X/+Z.
	// Shifting the body back by half the extent lines the two up.
	Transform offset = p_body->shapes[heightmap_index].xform;
	offset.origin += offset.basis.xform(Vector3(-(width - 1) * 0.5, 0, -(depth - 1) * 0.5));

	p_body->has_shape_origin_offset = true;
	p_body->shape_origin_offset = offset;
}

void Box3DPhysicsServer::_body_rebuild_shapes(Box3DBody *p_body) {
	ERR_FAIL_NULL(p_body);
	if (!p_body->in_world()) {
		return;
	}

	for (int i = 0; i < p_body->shapes.size(); i++) {
		Box3DShapeEntry &entry = p_body->shapes.write[i];

		if (!B3_IS_NULL(entry.shape_id)) {
			b3DestroyShape(entry.shape_id, false);
			entry.shape_id = b3_nullShapeId;
		}
		if (entry.disabled) {
			continue;
		}

		Box3DShape *shape = shape_owner.getornull(entry.shape);
		if (!shape) {
			continue;
		}

		b3ShapeDef def = b3DefaultShapeDef();
		def.density = 1.0f;
		def.baseMaterial.friction = (float)p_body->friction;
		def.baseMaterial.restitution = (float)p_body->bounce;
		def.filter.categoryBits = p_body->collision_layer;
		def.filter.maskBits = p_body->collision_mask;
		// Mass is recomputed once, after every shape is attached.
		def.updateBodyMass = false;

		entry.shape_id = shape->attach(p_body->body_id, &def, entry.xform);
	}

	// The offset depends on which shapes are attached, so it is recomputed here
	// and pushed into the solver before mass, which reads the final layout.
	_body_update_shape_origin_offset(p_body);
	_body_sync_transform(p_body);

	_body_apply_mass(p_body);
}

void Box3DPhysicsServer::_body_apply_mass(Box3DBody *p_body) {
	if (!p_body->in_world() || p_body->mode != BODY_MODE_RIGID) {
		if (p_body->in_world()) {
			b3Body_ApplyMassFromShapes(p_body->body_id);
		}
		return;
	}

	// Box3D derives mass from shape density; Godot sets it explicitly. Compute
	// the density-1 mass first, then rescale mass and inertia by the ratio that
	// lands on Godot's number. Inertia is linear in mass for a fixed shape, so
	// one multiply per component is exact rather than an approximation.
	b3Body_ApplyMassFromShapes(p_body->body_id);

	b3MassData md = b3Body_GetMassData(p_body->body_id);
	if (md.mass <= 0.0f || p_body->mass <= 0) {
		return;
	}

	const float ratio = (float)p_body->mass / md.mass;
	md.mass *= ratio;
	md.inertia.cx.x *= ratio;
	md.inertia.cx.y *= ratio;
	md.inertia.cx.z *= ratio;
	md.inertia.cy.x *= ratio;
	md.inertia.cy.y *= ratio;
	md.inertia.cy.z *= ratio;
	md.inertia.cz.x *= ratio;
	md.inertia.cz.y *= ratio;
	md.inertia.cz.z *= ratio;

	b3Body_SetMassData(p_body->body_id, md);
}

/* SHAPE API */

RID Box3DPhysicsServer::shape_create(ShapeType p_shape) {
	ERR_FAIL_COND_V_MSG(p_shape == SHAPE_CUSTOM, RID(), "Box3D: custom shapes are not supported.");

	// Creation is accepted for every type so scenes load; the actual geometry is
	// built at body-attach time (see Box3DShape::attach in box3d_shapes.cpp),
	// which is also where the unsupported types report themselves.
	Box3DShape *shape = memnew(Box3DShape);
	shape->type = p_shape;
	return shape_owner.make_rid(shape);
}

void Box3DPhysicsServer::shape_set_data(RID p_shape, const Variant &p_data) {
	Box3DShape *shape = shape_owner.getornull(p_shape);
	ERR_FAIL_NULL(shape);
	shape->data = p_data;

	// Solver geometry built from the old data is now stale. This is the hot path
	// for streamed terrain: CellTerrain reassigns a HeightMapShape's data every
	// time a cell loads.
	//
	// Order matters. The height field blob is shared by every body this shape is
	// attached to, so every referencing b3 shape must be destroyed BEFORE the
	// blob is freed — otherwise rebuilding one body leaves another holding a
	// dangling pointer into freed height data.
	if (shape->owners.empty()) {
		shape->free_geometry();
		return;
	}

	for (Set<RID>::Element *E = shape->owners.front(); E; E = E->next()) {
		Box3DBody *body = body_owner.getornull(E->get());
		if (!body || !body->in_world()) {
			continue;
		}
		for (int i = 0; i < body->shapes.size(); i++) {
			if (body->shapes[i].shape == p_shape && !B3_IS_NULL(body->shapes[i].shape_id)) {
				b3DestroyShape(body->shapes[i].shape_id, false);
				body->shapes.write[i].shape_id = b3_nullShapeId;
			}
		}
	}

	shape->free_geometry();

	for (Set<RID>::Element *E = shape->owners.front(); E; E = E->next()) {
		if (Box3DBody *body = body_owner.getornull(E->get())) {
			_body_rebuild_shapes(body);
		}
	}
}

PhysicsServer::ShapeType Box3DPhysicsServer::shape_get_type(RID p_shape) const {
	Box3DShape *shape = shape_owner.getornull(p_shape);
	ERR_FAIL_NULL_V(shape, SHAPE_CUSTOM);
	return shape->type;
}

Variant Box3DPhysicsServer::shape_get_data(RID p_shape) const {
	Box3DShape *shape = shape_owner.getornull(p_shape);
	ERR_FAIL_NULL_V(shape, Variant());
	return shape->data;
}

void Box3DPhysicsServer::shape_set_margin(RID p_shape, real_t p_margin) {
	Box3DShape *shape = shape_owner.getornull(p_shape);
	ERR_FAIL_NULL(shape);
	shape->margin = p_margin;
}

real_t Box3DPhysicsServer::shape_get_margin(RID p_shape) const {
	Box3DShape *shape = shape_owner.getornull(p_shape);
	ERR_FAIL_NULL_V(shape, 0.0);
	return shape->margin;
}

void Box3DPhysicsServer::shape_set_custom_solver_bias(RID p_shape, real_t p_bias) {
	Box3DShape *shape = shape_owner.getornull(p_shape);
	ERR_FAIL_NULL(shape);
	shape->custom_bias = p_bias;
}

real_t Box3DPhysicsServer::shape_get_custom_solver_bias(RID p_shape) const {
	Box3DShape *shape = shape_owner.getornull(p_shape);
	ERR_FAIL_NULL_V(shape, 0.0);
	return shape->custom_bias;
}

/* SPACE API */

RID Box3DPhysicsServer::space_create() {
	Box3DSpace *space = memnew(Box3DSpace);
	RID rid = space_owner.make_rid(space);
	space->self = rid;

	// World gravity and damping are addressed through the area API using the
	// space's own RID (see scene/resources/world.cpp:339), so every space needs
	// a default area standing by from the moment it exists.
	space->default_area = memnew(Box3DArea);
	space->default_area->self = rid;

	space->sub_step_count = collision_iterations;
	space->create_world();

	return rid;
}

void Box3DPhysicsServer::space_set_active(RID p_space, bool p_active) {
	Box3DSpace *space = space_owner.getornull(p_space);
	ERR_FAIL_NULL(space);

	if (space->active == p_active) {
		return;
	}
	space->active = p_active;

	if (p_active) {
		active_spaces.push_back(space);
	} else {
		active_spaces.erase(space);
	}
}

bool Box3DPhysicsServer::space_is_active(RID p_space) const {
	Box3DSpace *space = space_owner.getornull(p_space);
	ERR_FAIL_NULL_V(space, false);
	return space->active;
}

void Box3DPhysicsServer::space_set_param(RID p_space, SpaceParameter p_param, real_t p_value) {
	Box3DSpace *space = space_owner.getornull(p_space);
	ERR_FAIL_NULL(space);

	switch (p_param) {
		case SPACE_PARAM_CONTACT_RECYCLE_RADIUS:
			space->contact_recycle_radius = p_value;
			break;
		case SPACE_PARAM_CONTACT_MAX_SEPARATION:
			space->contact_max_separation = p_value;
			break;
		case SPACE_PARAM_BODY_MAX_ALLOWED_PENETRATION:
			space->contact_max_allowed_penetration = p_value;
			break;
		case SPACE_PARAM_BODY_LINEAR_VELOCITY_SLEEP_THRESHOLD:
			space->body_linear_velocity_sleep_threshold = p_value;
			break;
		case SPACE_PARAM_BODY_ANGULAR_VELOCITY_SLEEP_THRESHOLD:
			space->body_angular_velocity_sleep_threshold = p_value;
			break;
		case SPACE_PARAM_BODY_TIME_TO_SLEEP:
			space->body_time_to_sleep = p_value;
			break;
		case SPACE_PARAM_BODY_ANGULAR_VELOCITY_DAMP_RATIO:
			space->body_angular_velocity_damp_ratio = p_value;
			break;
		case SPACE_PARAM_CONSTRAINT_DEFAULT_BIAS:
			space->constraint_default_bias = p_value;
			break;
		default:
			break;
	}
}

real_t Box3DPhysicsServer::space_get_param(RID p_space, SpaceParameter p_param) const {
	Box3DSpace *space = space_owner.getornull(p_space);
	ERR_FAIL_NULL_V(space, 0.0);

	switch (p_param) {
		case SPACE_PARAM_CONTACT_RECYCLE_RADIUS:
			return space->contact_recycle_radius;
		case SPACE_PARAM_CONTACT_MAX_SEPARATION:
			return space->contact_max_separation;
		case SPACE_PARAM_BODY_MAX_ALLOWED_PENETRATION:
			return space->contact_max_allowed_penetration;
		case SPACE_PARAM_BODY_LINEAR_VELOCITY_SLEEP_THRESHOLD:
			return space->body_linear_velocity_sleep_threshold;
		case SPACE_PARAM_BODY_ANGULAR_VELOCITY_SLEEP_THRESHOLD:
			return space->body_angular_velocity_sleep_threshold;
		case SPACE_PARAM_BODY_TIME_TO_SLEEP:
			return space->body_time_to_sleep;
		case SPACE_PARAM_BODY_ANGULAR_VELOCITY_DAMP_RATIO:
			return space->body_angular_velocity_damp_ratio;
		case SPACE_PARAM_CONSTRAINT_DEFAULT_BIAS:
			return space->constraint_default_bias;
		default:
			return 0.0;
	}
}

PhysicsDirectSpaceState *Box3DPhysicsServer::space_get_direct_state(RID p_space) {
	Box3DSpace *space = space_owner.getornull(p_space);
	ERR_FAIL_NULL_V(space, nullptr);
	ERR_FAIL_COND_V_MSG(is_flushing_queries(), nullptr, "Space state is inaccessible right now, wait for iteration or physics process notification.");
	return space->direct_state;
}

void Box3DPhysicsServer::space_set_debug_contacts(RID p_space, int p_max_contacts) {
	Box3DSpace *space = space_owner.getornull(p_space);
	ERR_FAIL_NULL(space);
	space->max_debug_contacts = p_max_contacts;
	space->debug_contacts.resize(p_max_contacts);
	space->debug_contacts.resize(0);
}

Vector<Vector3> Box3DPhysicsServer::space_get_contacts(RID p_space) const {
	Box3DSpace *space = space_owner.getornull(p_space);
	ERR_FAIL_NULL_V(space, Vector<Vector3>());
	return space->debug_contacts;
}

int Box3DPhysicsServer::space_get_contact_count(RID p_space) const {
	Box3DSpace *space = space_owner.getornull(p_space);
	ERR_FAIL_NULL_V(space, 0);
	return space->debug_contacts.size();
}

/* AREA API */

RID Box3DPhysicsServer::area_create() {
	Box3DArea *area = memnew(Box3DArea);
	RID rid = area_owner.make_rid(area);
	area->self = rid;
	return rid;
}

void Box3DPhysicsServer::area_set_space(RID p_area, RID p_space) {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL(area);
	area->space = p_space;
}

RID Box3DPhysicsServer::area_get_space(RID p_area) const {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL_V(area, RID());
	return area->space;
}

void Box3DPhysicsServer::area_set_space_override_mode(RID p_area, AreaSpaceOverrideMode p_mode) {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL(area);
	area->space_override_mode = p_mode;
}

PhysicsServer::AreaSpaceOverrideMode Box3DPhysicsServer::area_get_space_override_mode(RID p_area) const {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL_V(area, AREA_SPACE_OVERRIDE_DISABLED);
	return area->space_override_mode;
}

void Box3DPhysicsServer::area_add_shape(RID p_area, RID p_shape, const Transform &p_transform, bool p_disabled) {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL(area);
	Box3DShape *shape = shape_owner.getornull(p_shape);
	ERR_FAIL_NULL(shape);

	Box3DShapeEntry entry;
	entry.shape = p_shape;
	entry.xform = p_transform;
	entry.disabled = p_disabled;
	area->shapes.push_back(entry);
	shape->owners.insert(p_area);
}

void Box3DPhysicsServer::area_set_shape(RID p_area, int p_shape_idx, RID p_shape) {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL(area);
	ERR_FAIL_INDEX(p_shape_idx, area->shapes.size());
	Box3DShape *shape = shape_owner.getornull(p_shape);
	ERR_FAIL_NULL(shape);

	area->shapes.write[p_shape_idx].shape = p_shape;
	shape->owners.insert(p_area);
}

void Box3DPhysicsServer::area_set_shape_transform(RID p_area, int p_shape_idx, const Transform &p_transform) {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL(area);
	ERR_FAIL_INDEX(p_shape_idx, area->shapes.size());
	area->shapes.write[p_shape_idx].xform = p_transform;
}

int Box3DPhysicsServer::area_get_shape_count(RID p_area) const {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL_V(area, 0);
	return area->shapes.size();
}

RID Box3DPhysicsServer::area_get_shape(RID p_area, int p_shape_idx) const {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL_V(area, RID());
	ERR_FAIL_INDEX_V(p_shape_idx, area->shapes.size(), RID());
	return area->shapes[p_shape_idx].shape;
}

Transform Box3DPhysicsServer::area_get_shape_transform(RID p_area, int p_shape_idx) const {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL_V(area, Transform());
	ERR_FAIL_INDEX_V(p_shape_idx, area->shapes.size(), Transform());
	return area->shapes[p_shape_idx].xform;
}

void Box3DPhysicsServer::area_remove_shape(RID p_area, int p_shape_idx) {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL(area);
	ERR_FAIL_INDEX(p_shape_idx, area->shapes.size());
	area->shapes.remove(p_shape_idx);
}

void Box3DPhysicsServer::area_clear_shapes(RID p_area) {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL(area);
	area->shapes.clear();
}

void Box3DPhysicsServer::area_set_shape_disabled(RID p_area, int p_shape_idx, bool p_disabled) {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL(area);
	ERR_FAIL_INDEX(p_shape_idx, area->shapes.size());
	area->shapes.write[p_shape_idx].disabled = p_disabled;
}

void Box3DPhysicsServer::area_attach_object_instance_id(RID p_area, ObjectID p_id) {
	if (space_owner.owns(p_area)) {
		return; // The default area has no node behind it.
	}
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL(area);
	area->instance_id = p_id;
}

ObjectID Box3DPhysicsServer::area_get_object_instance_id(RID p_area) const {
	if (space_owner.owns(p_area)) {
		return 0;
	}
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL_V(area, 0);
	return area->instance_id;
}

void Box3DPhysicsServer::area_set_param(RID p_area, AreaParameter p_param, const Variant &p_value) {
	// A space RID here means "the world's default area" — that is how World sets
	// default gravity and damping.
	Box3DArea *area = nullptr;
	if (space_owner.owns(p_area)) {
		Box3DSpace *space = space_owner.getornull(p_area);
		area = space->default_area;
	} else {
		area = area_owner.getornull(p_area);
	}
	ERR_FAIL_NULL(area);

	switch (p_param) {
		case AREA_PARAM_GRAVITY:
			area->gravity = p_value;
			break;
		case AREA_PARAM_GRAVITY_VECTOR:
			area->gravity_vector = p_value;
			break;
		// (the default-area case pushes these into the world below)
		case AREA_PARAM_GRAVITY_IS_POINT:
			area->gravity_is_point = p_value;
			break;
		case AREA_PARAM_GRAVITY_DISTANCE_SCALE:
			area->gravity_distance_scale = p_value;
			break;
		case AREA_PARAM_GRAVITY_POINT_ATTENUATION:
			area->point_attenuation = p_value;
			break;
		case AREA_PARAM_LINEAR_DAMP:
			area->linear_damp = p_value;
			break;
		case AREA_PARAM_ANGULAR_DAMP:
			area->angular_damp = p_value;
			break;
		case AREA_PARAM_PRIORITY:
			area->priority = p_value;
			break;
		default:
			break;
	}

	// When this is a space's default area, gravity belongs to the live world.
	if (space_owner.owns(p_area)) {
		Box3DSpace *space = space_owner.getornull(p_area);
		space->update_gravity();
	}
}

Variant Box3DPhysicsServer::area_get_param(RID p_area, AreaParameter p_param) const {
	Box3DArea *area = nullptr;
	if (space_owner.owns(p_area)) {
		Box3DSpace *space = space_owner.getornull(p_area);
		area = space->default_area;
	} else {
		area = area_owner.getornull(p_area);
	}
	ERR_FAIL_NULL_V(area, Variant());

	switch (p_param) {
		case AREA_PARAM_GRAVITY:
			return area->gravity;
		case AREA_PARAM_GRAVITY_VECTOR:
			return area->gravity_vector;
		case AREA_PARAM_GRAVITY_IS_POINT:
			return area->gravity_is_point;
		case AREA_PARAM_GRAVITY_DISTANCE_SCALE:
			return area->gravity_distance_scale;
		case AREA_PARAM_GRAVITY_POINT_ATTENUATION:
			return area->point_attenuation;
		case AREA_PARAM_LINEAR_DAMP:
			return area->linear_damp;
		case AREA_PARAM_ANGULAR_DAMP:
			return area->angular_damp;
		case AREA_PARAM_PRIORITY:
			return area->priority;
		default:
			return Variant();
	}
}

void Box3DPhysicsServer::area_set_transform(RID p_area, const Transform &p_transform) {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL(area);
	area->transform = p_transform;
}

Transform Box3DPhysicsServer::area_get_transform(RID p_area) const {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL_V(area, Transform());
	return area->transform;
}

void Box3DPhysicsServer::area_set_collision_mask(RID p_area, uint32_t p_mask) {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL(area);
	area->collision_mask = p_mask;
}

void Box3DPhysicsServer::area_set_collision_layer(RID p_area, uint32_t p_layer) {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL(area);
	area->collision_layer = p_layer;
}

void Box3DPhysicsServer::area_set_monitorable(RID p_area, bool p_monitorable) {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL(area);
	area->monitorable = p_monitorable;
}

void Box3DPhysicsServer::area_set_monitor_callback(RID p_area, Object *p_receiver, const StringName &p_method) {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL(area);
	area->monitor_callback_id = p_receiver ? p_receiver->get_instance_id() : 0;
	area->monitor_callback_method = p_method;
}

void Box3DPhysicsServer::area_set_area_monitor_callback(RID p_area, Object *p_receiver, const StringName &p_method) {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL(area);
	area->area_monitor_callback_id = p_receiver ? p_receiver->get_instance_id() : 0;
	area->area_monitor_callback_method = p_method;
}

void Box3DPhysicsServer::area_set_ray_pickable(RID p_area, bool p_enable) {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL(area);
	area->ray_pickable = p_enable;
}

bool Box3DPhysicsServer::area_is_ray_pickable(RID p_area) const {
	Box3DArea *area = area_owner.getornull(p_area);
	ERR_FAIL_NULL_V(area, false);
	return area->ray_pickable;
}

/* RIGID BODY API */

RID Box3DPhysicsServer::body_create(BodyMode p_mode, bool p_init_sleeping) {
	Box3DBody *body = memnew(Box3DBody);
	body->mode = p_mode;
	body->sleeping = p_init_sleeping;
	RID rid = body_owner.make_rid(body);
	body->self = rid;
	return rid;
}

void Box3DPhysicsServer::body_set_space(RID p_body, RID p_space) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);

	if (body->space == p_space) {
		return;
	}

	if (body->space.is_valid()) {
		_body_exit_world(body);
	}

	body->space = p_space;

	if (p_space.is_valid()) {
		Box3DSpace *space = space_owner.getornull(p_space);
		ERR_FAIL_NULL(space);
		_body_enter_world(body, space);
	}
}

RID Box3DPhysicsServer::body_get_space(RID p_body) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, RID());
	return body->space;
}

void Box3DPhysicsServer::body_set_mode(RID p_body, BodyMode p_mode) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->mode = p_mode;

	if (body->in_world()) {
		b3Body_SetType(body->body_id, b3_body_type_from_godot(p_mode));
		_body_apply_mass(body);
	}
}

PhysicsServer::BodyMode Box3DPhysicsServer::body_get_mode(RID p_body) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, BODY_MODE_STATIC);
	return body->mode;
}

void Box3DPhysicsServer::body_add_shape(RID p_body, RID p_shape, const Transform &p_transform, bool p_disabled) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	Box3DShape *shape = shape_owner.getornull(p_shape);
	ERR_FAIL_NULL(shape);

	Box3DShapeEntry entry;
	entry.shape = p_shape;
	entry.xform = p_transform;
	entry.disabled = p_disabled;
	body->shapes.push_back(entry);
	shape->owners.insert(p_body);
	_body_rebuild_shapes(body);
}

void Box3DPhysicsServer::body_set_shape(RID p_body, int p_shape_idx, RID p_shape) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	ERR_FAIL_INDEX(p_shape_idx, body->shapes.size());
	Box3DShape *shape = shape_owner.getornull(p_shape);
	ERR_FAIL_NULL(shape);

	body->shapes.write[p_shape_idx].shape = p_shape;
	shape->owners.insert(p_body);
	_body_rebuild_shapes(body);
}

void Box3DPhysicsServer::body_set_shape_transform(RID p_body, int p_shape_idx, const Transform &p_transform) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	ERR_FAIL_INDEX(p_shape_idx, body->shapes.size());
	body->shapes.write[p_shape_idx].xform = p_transform;
	_body_rebuild_shapes(body);
}

int Box3DPhysicsServer::body_get_shape_count(RID p_body) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->shapes.size();
}

RID Box3DPhysicsServer::body_get_shape(RID p_body, int p_shape_idx) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, RID());
	ERR_FAIL_INDEX_V(p_shape_idx, body->shapes.size(), RID());
	return body->shapes[p_shape_idx].shape;
}

Transform Box3DPhysicsServer::body_get_shape_transform(RID p_body, int p_shape_idx) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, Transform());
	ERR_FAIL_INDEX_V(p_shape_idx, body->shapes.size(), Transform());
	return body->shapes[p_shape_idx].xform;
}

void Box3DPhysicsServer::body_set_shape_disabled(RID p_body, int p_shape_idx, bool p_disabled) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	ERR_FAIL_INDEX(p_shape_idx, body->shapes.size());
	body->shapes.write[p_shape_idx].disabled = p_disabled;
	_body_rebuild_shapes(body);
}

void Box3DPhysicsServer::body_remove_shape(RID p_body, int p_shape_idx) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	ERR_FAIL_INDEX(p_shape_idx, body->shapes.size());

	if (body->in_world() && !B3_IS_NULL(body->shapes[p_shape_idx].shape_id)) {
		b3DestroyShape(body->shapes[p_shape_idx].shape_id, false);
	}

	const RID removed = body->shapes[p_shape_idx].shape;
	body->shapes.remove(p_shape_idx);

	// Drop the back-reference once no entry uses this shape any more. RIDs are
	// recycled, so a stale owner would later point shape_set_data() at whatever
	// body inherited the id.
	bool still_used = false;
	for (int i = 0; i < body->shapes.size(); i++) {
		if (body->shapes[i].shape == removed) {
			still_used = true;
			break;
		}
	}
	if (!still_used) {
		if (Box3DShape *shape = shape_owner.getornull(removed)) {
			shape->owners.erase(p_body);
		}
	}

	_body_rebuild_shapes(body);
}

void Box3DPhysicsServer::body_clear_shapes(RID p_body) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);

	for (int i = 0; i < body->shapes.size(); i++) {
		if (body->in_world() && !B3_IS_NULL(body->shapes[i].shape_id)) {
			b3DestroyShape(body->shapes[i].shape_id, false);
		}
		if (Box3DShape *shape = shape_owner.getornull(body->shapes[i].shape)) {
			shape->owners.erase(p_body);
		}
	}
	body->shapes.clear();

	_body_update_shape_origin_offset(body);
	_body_sync_transform(body);
	_body_apply_mass(body);
}

void Box3DPhysicsServer::body_attach_object_instance_id(RID p_body, uint32_t p_id) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->instance_id = p_id;
}

uint32_t Box3DPhysicsServer::body_get_object_instance_id(RID p_body) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->instance_id;
}

void Box3DPhysicsServer::body_set_enable_continuous_collision_detection(RID p_body, bool p_enable) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->ccd_enabled = p_enable;
}

bool Box3DPhysicsServer::body_is_continuous_collision_detection_enabled(RID p_body) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, false);
	return body->ccd_enabled;
}

void Box3DPhysicsServer::body_set_collision_layer(RID p_body, uint32_t p_layer) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->collision_layer = p_layer;
}

uint32_t Box3DPhysicsServer::body_get_collision_layer(RID p_body) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->collision_layer;
}

void Box3DPhysicsServer::body_set_collision_mask(RID p_body, uint32_t p_mask) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->collision_mask = p_mask;
}

uint32_t Box3DPhysicsServer::body_get_collision_mask(RID p_body) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->collision_mask;
}

void Box3DPhysicsServer::body_set_user_flags(RID p_body, uint32_t p_flags) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->user_flags = p_flags;
}

uint32_t Box3DPhysicsServer::body_get_user_flags(RID p_body) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->user_flags;
}

void Box3DPhysicsServer::body_set_param(RID p_body, BodyParameter p_param, float p_value) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);

	switch (p_param) {
		case BODY_PARAM_BOUNCE:
			body->bounce = p_value;
			break;
		case BODY_PARAM_FRICTION:
			body->friction = p_value;
			break;
		case BODY_PARAM_MASS:
			body->mass = p_value;
			break;
		case BODY_PARAM_GRAVITY_SCALE:
			body->gravity_scale = p_value;
			break;
		case BODY_PARAM_LINEAR_DAMP:
			body->linear_damp = p_value;
			break;
		case BODY_PARAM_ANGULAR_DAMP:
			body->angular_damp = p_value;
			break;
		default:
			break;
	}
}

float Box3DPhysicsServer::body_get_param(RID p_body, BodyParameter p_param) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, 0.0);

	switch (p_param) {
		case BODY_PARAM_BOUNCE:
			return body->bounce;
		case BODY_PARAM_FRICTION:
			return body->friction;
		case BODY_PARAM_MASS:
			return body->mass;
		case BODY_PARAM_GRAVITY_SCALE:
			return body->gravity_scale;
		case BODY_PARAM_LINEAR_DAMP:
			return body->linear_damp;
		case BODY_PARAM_ANGULAR_DAMP:
			return body->angular_damp;
		default:
			return 0.0;
	}
}

void Box3DPhysicsServer::body_set_kinematic_safe_margin(RID p_body, real_t p_margin) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->kinematic_safe_margin = p_margin;
}

real_t Box3DPhysicsServer::body_get_kinematic_safe_margin(RID p_body) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, 0.0);
	return body->kinematic_safe_margin;
}

void Box3DPhysicsServer::body_set_state(RID p_body, BodyState p_state, const Variant &p_variant) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);

	switch (p_state) {
		case BODY_STATE_TRANSFORM: {
			body->transform = p_variant;
			_body_sync_transform(body);
		} break;
		case BODY_STATE_LINEAR_VELOCITY: {
			body->linear_velocity = p_variant;
			if (body->in_world()) {
				b3Body_SetLinearVelocity(body->body_id, b3_from_godot(body->linear_velocity));
			}
		} break;
		case BODY_STATE_ANGULAR_VELOCITY: {
			body->angular_velocity = p_variant;
			if (body->in_world()) {
				b3Body_SetAngularVelocity(body->body_id, b3_from_godot(body->angular_velocity));
			}
		} break;
		case BODY_STATE_SLEEPING: {
			body->sleeping = p_variant;
			if (body->in_world()) {
				b3Body_SetAwake(body->body_id, !body->sleeping);
			}
		} break;
		case BODY_STATE_CAN_SLEEP: {
			body->can_sleep = p_variant;
			if (body->in_world()) {
				b3Body_EnableSleep(body->body_id, body->can_sleep);
			}
		} break;
	}
}

Variant Box3DPhysicsServer::body_get_state(RID p_body, BodyState p_state) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, Variant());

	switch (p_state) {
		case BODY_STATE_TRANSFORM:
			return body->transform;
		case BODY_STATE_LINEAR_VELOCITY:
			return body->linear_velocity;
		case BODY_STATE_ANGULAR_VELOCITY:
			return body->angular_velocity;
		case BODY_STATE_SLEEPING:
			return body->sleeping;
		case BODY_STATE_CAN_SLEEP:
			return body->can_sleep;
	}
	return Variant();
}

void Box3DPhysicsServer::body_set_applied_force(RID p_body, const Vector3 &p_force) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->applied_force = p_force;
}

Vector3 Box3DPhysicsServer::body_get_applied_force(RID p_body) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, Vector3());
	return body->applied_force;
}

void Box3DPhysicsServer::body_set_applied_torque(RID p_body, const Vector3 &p_torque) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->applied_torque = p_torque;
}

Vector3 Box3DPhysicsServer::body_get_applied_torque(RID p_body) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, Vector3());
	return body->applied_torque;
}

void Box3DPhysicsServer::body_add_central_force(RID p_body, const Vector3 &p_force) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->applied_force += p_force;
}

void Box3DPhysicsServer::body_add_force(RID p_body, const Vector3 &p_force, const Vector3 &p_pos) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->applied_force += p_force;
	body->applied_torque += p_pos.cross(p_force);
}

void Box3DPhysicsServer::body_add_torque(RID p_body, const Vector3 &p_torque) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->applied_torque += p_torque;
}

void Box3DPhysicsServer::body_apply_central_impulse(RID p_body, const Vector3 &p_impulse) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);

	if (body->in_world()) {
		b3Body_ApplyLinearImpulseToCenter(body->body_id, b3_from_godot(p_impulse), true);
		return;
	}
	// Not in a world yet: fold it into the starting velocity instead of losing it.
	body->linear_velocity += p_impulse * (body->mass > 0 ? 1.0 / body->mass : 0.0);
}

void Box3DPhysicsServer::body_apply_impulse(RID p_body, const Vector3 &p_pos, const Vector3 &p_impulse) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);

	if (body->in_world()) {
		// Godot passes the application point in body-local space; Box3D wants it
		// in world space.
		b3Body_ApplyLinearImpulse(body->body_id,
				b3_from_godot(p_impulse),
				b3_pos_from_godot(body->transform.xform(p_pos)),
				true);
		return;
	}
	body->linear_velocity += p_impulse * (body->mass > 0 ? 1.0 / body->mass : 0.0);
}

void Box3DPhysicsServer::body_apply_torque_impulse(RID p_body, const Vector3 &p_impulse) {
	B3_TODO("body_apply_torque_impulse()");
}

void Box3DPhysicsServer::body_set_axis_velocity(RID p_body, const Vector3 &p_axis_velocity) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);

	Vector3 axis = p_axis_velocity.normalized();
	Vector3 velocity = body->linear_velocity;
	velocity -= axis * axis.dot(velocity);
	velocity += p_axis_velocity;
	body->linear_velocity = velocity;
}

void Box3DPhysicsServer::body_set_axis_lock(RID p_body, BodyAxis p_axis, bool p_lock) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	if (p_lock) {
		body->locked_axis |= p_axis;
	} else {
		body->locked_axis &= ~p_axis;
	}
}

bool Box3DPhysicsServer::body_is_axis_locked(RID p_body, BodyAxis p_axis) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, false);
	return body->locked_axis & p_axis;
}

void Box3DPhysicsServer::body_add_collision_exception(RID p_body, RID p_body_b) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->collision_exceptions.insert(p_body_b);
}

void Box3DPhysicsServer::body_remove_collision_exception(RID p_body, RID p_body_b) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->collision_exceptions.erase(p_body_b);
}

void Box3DPhysicsServer::body_get_collision_exceptions(RID p_body, List<RID> *p_exceptions) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	for (Set<RID>::Element *E = body->collision_exceptions.front(); E; E = E->next()) {
		p_exceptions->push_back(E->get());
	}
}

void Box3DPhysicsServer::body_set_max_contacts_reported(RID p_body, int p_contacts) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->max_contacts_reported = p_contacts;
}

int Box3DPhysicsServer::body_get_max_contacts_reported(RID p_body) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->max_contacts_reported;
}

void Box3DPhysicsServer::body_set_contacts_reported_depth_threshold(RID p_body, float p_threshold) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->contacts_reported_depth_threshold = p_threshold;
}

float Box3DPhysicsServer::body_get_contacts_reported_depth_threshold(RID p_body) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, 0.0);
	return body->contacts_reported_depth_threshold;
}

void Box3DPhysicsServer::body_set_omit_force_integration(RID p_body, bool p_omit) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->omit_force_integration = p_omit;
}

bool Box3DPhysicsServer::body_is_omitting_force_integration(RID p_body) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, false);
	return body->omit_force_integration;
}

void Box3DPhysicsServer::body_set_force_integration_callback(RID p_body, Object *p_receiver, const StringName &p_method, const Variant &p_udata) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->force_integration_callback_id = p_receiver ? p_receiver->get_instance_id() : 0;
	body->force_integration_callback_method = p_method;
	body->force_integration_callback_udata = p_udata;
}

void Box3DPhysicsServer::body_set_ray_pickable(RID p_body, bool p_enable) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	body->ray_pickable = p_enable;
}

bool Box3DPhysicsServer::body_is_ray_pickable(RID p_body) const {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, false);
	return body->ray_pickable;
}

PhysicsDirectBodyState *Box3DPhysicsServer::body_get_direct_state(RID p_body) {
	ERR_FAIL_COND_V_MSG(!doing_sync, nullptr, "Body state is inaccessible right now, wait for iteration or physics process notification.");

	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, nullptr);
	ERR_FAIL_NULL_V(Box3DPhysicsDirectBodyState::singleton, nullptr);

	Box3DPhysicsDirectBodyState::singleton->body = body;
	return Box3DPhysicsDirectBodyState::singleton;
}

bool Box3DPhysicsServer::body_test_motion(RID p_body, const Transform &p_from, const Vector3 &p_motion, bool p_infinite_inertia, MotionResult *r_result, bool p_exclude_raycast_shapes, const Set<RID> &p_exclude) {
	// This one method defines the entire feel of KinematicBody.move_and_slide():
	// depenetration recovery, the collision margin, and the safe/unsafe fraction
	// split. Box3D's own character mover is a different model and cannot be
	// dropped in behind this signature — the real implementation is a shape cast
	// plus a recovery loop written against b3World, matching Godot's semantics
	// rather than Box3D's.
	//
	// Reporting "no collision" keeps CellPlayerBody moving in a straight line
	// (through geometry) instead of locking in place, which is the more
	// debuggable failure while this is unimplemented.
	B3_TODO("body_test_motion()");

	if (r_result) {
		r_result->motion = p_motion;
		r_result->remainder = Vector3();
		r_result->collision_depth = 0;
		r_result->collision_safe_fraction = 1.0;
		r_result->collision_unsafe_fraction = 1.0;
	}
	return false;
}

int Box3DPhysicsServer::body_test_ray_separation(RID p_body, const Transform &p_transform, bool p_infinite_inertia, Vector3 &r_recover_motion, SeparationResult *r_results, int p_result_max, float p_margin) {
	B3_TODO("body_test_ray_separation()");
	r_recover_motion = Vector3();
	return 0;
}

/* SOFT BODY API */

RID Box3DPhysicsServer::soft_body_create(bool p_init_sleeping) {
	B3_UNSUPPORTED("SoftBody");
	return RID();
}

void Box3DPhysicsServer::soft_body_update_visual_server(RID p_body, class SoftBodyVisualServerHandler *p_visual_server_handler) {}
void Box3DPhysicsServer::soft_body_set_space(RID p_body, RID p_space) {}
RID Box3DPhysicsServer::soft_body_get_space(RID p_body) const { return RID(); }
void Box3DPhysicsServer::soft_body_set_mesh(RID p_body, const REF &p_mesh) {}
void Box3DPhysicsServer::soft_body_set_collision_layer(RID p_body, uint32_t p_layer) {}
uint32_t Box3DPhysicsServer::soft_body_get_collision_layer(RID p_body) const { return 0; }
void Box3DPhysicsServer::soft_body_set_collision_mask(RID p_body, uint32_t p_mask) {}
uint32_t Box3DPhysicsServer::soft_body_get_collision_mask(RID p_body) const { return 0; }
void Box3DPhysicsServer::soft_body_add_collision_exception(RID p_body, RID p_body_b) {}
void Box3DPhysicsServer::soft_body_remove_collision_exception(RID p_body, RID p_body_b) {}
void Box3DPhysicsServer::soft_body_get_collision_exceptions(RID p_body, List<RID> *p_exceptions) {}
void Box3DPhysicsServer::soft_body_set_state(RID p_body, BodyState p_state, const Variant &p_variant) {}
Variant Box3DPhysicsServer::soft_body_get_state(RID p_body, BodyState p_state) const { return Variant(); }
void Box3DPhysicsServer::soft_body_set_transform(RID p_body, const Transform &p_transform) {}
Vector3 Box3DPhysicsServer::soft_body_get_vertex_position(RID p_body, int vertex_index) const { return Vector3(); }
void Box3DPhysicsServer::soft_body_set_ray_pickable(RID p_body, bool p_enable) {}
bool Box3DPhysicsServer::soft_body_is_ray_pickable(RID p_body) const { return false; }
void Box3DPhysicsServer::soft_body_set_simulation_precision(RID p_body, int p_simulation_precision) {}
int Box3DPhysicsServer::soft_body_get_simulation_precision(RID p_body) { return 0; }
void Box3DPhysicsServer::soft_body_set_total_mass(RID p_body, real_t p_total_mass) {}
real_t Box3DPhysicsServer::soft_body_get_total_mass(RID p_body) { return 0.0; }
void Box3DPhysicsServer::soft_body_set_linear_stiffness(RID p_body, real_t p_stiffness) {}
real_t Box3DPhysicsServer::soft_body_get_linear_stiffness(RID p_body) { return 0.0; }
void Box3DPhysicsServer::soft_body_set_areaAngular_stiffness(RID p_body, real_t p_stiffness) {}
real_t Box3DPhysicsServer::soft_body_get_areaAngular_stiffness(RID p_body) { return 0.0; }
void Box3DPhysicsServer::soft_body_set_volume_stiffness(RID p_body, real_t p_stiffness) {}
real_t Box3DPhysicsServer::soft_body_get_volume_stiffness(RID p_body) { return 0.0; }
void Box3DPhysicsServer::soft_body_set_pressure_coefficient(RID p_body, real_t p_pressure_coefficient) {}
real_t Box3DPhysicsServer::soft_body_get_pressure_coefficient(RID p_body) { return 0.0; }
void Box3DPhysicsServer::soft_body_set_pose_matching_coefficient(RID p_body, real_t p_pose_matching_coefficient) {}
real_t Box3DPhysicsServer::soft_body_get_pose_matching_coefficient(RID p_body) { return 0.0; }
void Box3DPhysicsServer::soft_body_set_damping_coefficient(RID p_body, real_t p_damping_coefficient) {}
real_t Box3DPhysicsServer::soft_body_get_damping_coefficient(RID p_body) { return 0.0; }
void Box3DPhysicsServer::soft_body_set_drag_coefficient(RID p_body, real_t p_drag_coefficient) {}
real_t Box3DPhysicsServer::soft_body_get_drag_coefficient(RID p_body) { return 0.0; }
void Box3DPhysicsServer::soft_body_move_point(RID p_body, int p_point_index, const Vector3 &p_global_position) {}
Vector3 Box3DPhysicsServer::soft_body_get_point_global_position(RID p_body, int p_point_index) { return Vector3(); }
Vector3 Box3DPhysicsServer::soft_body_get_point_offset(RID p_body, int p_point_index) const { return Vector3(); }
void Box3DPhysicsServer::soft_body_remove_all_pinned_points(RID p_body) {}
void Box3DPhysicsServer::soft_body_pin_point(RID p_body, int p_point_index, bool p_pin) {}
bool Box3DPhysicsServer::soft_body_is_point_pinned(RID p_body, int p_point_index) { return false; }

/* JOINT API */

// Box3D ships parallel, distance, motor, filter, prismatic, revolute, spherical,
// weld and wheel joints. Godot's set maps onto that well:
//   hinge      -> b3RevoluteJoint    (direct)
//   slider     -> b3PrismaticJoint   (direct)
//   pin        -> b3SphericalJoint   (direct)
//   cone twist -> b3SphericalJoint with enableConeLimit + enableTwistLimit,
//                 which carries Godot's whole cone-twist parameter set
//   6DOF       -> no equivalent; the only real gap
// Creation returns a valid RID in every case so scenes keep loading.

PhysicsServer::JointType Box3DPhysicsServer::joint_get_type(RID p_joint) const {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL_V(joint, JOINT_PIN);
	return joint->type;
}

void Box3DPhysicsServer::joint_set_solver_priority(RID p_joint, int p_priority) {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL(joint);
	joint->solver_priority = p_priority;
}

int Box3DPhysicsServer::joint_get_solver_priority(RID p_joint) const {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL_V(joint, 0);
	return joint->solver_priority;
}

void Box3DPhysicsServer::joint_disable_collisions_between_bodies(RID p_joint, const bool p_disable) {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL(joint);
	joint->collisions_disabled = p_disable;
}

bool Box3DPhysicsServer::joint_is_disabled_collisions_between_bodies(RID p_joint) const {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL_V(joint, false);
	return joint->collisions_disabled;
}

RID Box3DPhysicsServer::joint_create_pin(RID p_body_A, const Vector3 &p_local_A, RID p_body_B, const Vector3 &p_local_B) {
	B3_TODO("PinJoint (maps to b3SphericalJoint)");
	Box3DJoint *joint = memnew(Box3DJoint(JOINT_PIN));
	joint->body_a = p_body_A;
	joint->body_b = p_body_B;
	joint->pin_local_a = p_local_A;
	joint->pin_local_b = p_local_B;
	return joint_owner.make_rid(joint);
}

void Box3DPhysicsServer::pin_joint_set_param(RID p_joint, PinJointParam p_param, float p_value) {}
float Box3DPhysicsServer::pin_joint_get_param(RID p_joint, PinJointParam p_param) const { return 0.0; }

void Box3DPhysicsServer::pin_joint_set_local_a(RID p_joint, const Vector3 &p_A) {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL(joint);
	joint->pin_local_a = p_A;
}

Vector3 Box3DPhysicsServer::pin_joint_get_local_a(RID p_joint) const {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL_V(joint, Vector3());
	return joint->pin_local_a;
}

void Box3DPhysicsServer::pin_joint_set_local_b(RID p_joint, const Vector3 &p_B) {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL(joint);
	joint->pin_local_b = p_B;
}

Vector3 Box3DPhysicsServer::pin_joint_get_local_b(RID p_joint) const {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL_V(joint, Vector3());
	return joint->pin_local_b;
}

RID Box3DPhysicsServer::joint_create_hinge(RID p_body_A, const Transform &p_hinge_A, RID p_body_B, const Transform &p_hinge_B) {
	B3_TODO("HingeJoint (maps to b3RevoluteJoint)");
	Box3DJoint *joint = memnew(Box3DJoint(JOINT_HINGE));
	joint->body_a = p_body_A;
	joint->body_b = p_body_B;
	return joint_owner.make_rid(joint);
}

RID Box3DPhysicsServer::joint_create_hinge_simple(RID p_body_A, const Vector3 &p_pivot_A, const Vector3 &p_axis_A, RID p_body_B, const Vector3 &p_pivot_B, const Vector3 &p_axis_B) {
	B3_TODO("HingeJoint (maps to b3RevoluteJoint)");
	Box3DJoint *joint = memnew(Box3DJoint(JOINT_HINGE));
	joint->body_a = p_body_A;
	joint->body_b = p_body_B;
	return joint_owner.make_rid(joint);
}

void Box3DPhysicsServer::hinge_joint_set_param(RID p_joint, HingeJointParam p_param, float p_value) {}
float Box3DPhysicsServer::hinge_joint_get_param(RID p_joint, HingeJointParam p_param) const { return 0.0; }
void Box3DPhysicsServer::hinge_joint_set_flag(RID p_joint, HingeJointFlag p_flag, bool p_value) {}
bool Box3DPhysicsServer::hinge_joint_get_flag(RID p_joint, HingeJointFlag p_flag) const { return false; }

RID Box3DPhysicsServer::joint_create_slider(RID p_body_A, const Transform &p_local_frame_A, RID p_body_B, const Transform &p_local_frame_B) {
	B3_TODO("SliderJoint (maps to b3PrismaticJoint)");
	Box3DJoint *joint = memnew(Box3DJoint(JOINT_SLIDER));
	joint->body_a = p_body_A;
	joint->body_b = p_body_B;
	return joint_owner.make_rid(joint);
}

void Box3DPhysicsServer::slider_joint_set_param(RID p_joint, SliderJointParam p_param, float p_value) {}
float Box3DPhysicsServer::slider_joint_get_param(RID p_joint, SliderJointParam p_param) const { return 0.0; }

RID Box3DPhysicsServer::joint_create_cone_twist(RID p_body_A, const Transform &p_local_frame_A, RID p_body_B, const Transform &p_local_frame_B) {
	B3_TODO("ConeTwistJoint (maps to b3SphericalJoint cone+twist limits)");
	Box3DJoint *joint = memnew(Box3DJoint(JOINT_CONE_TWIST));
	joint->body_a = p_body_A;
	joint->body_b = p_body_B;
	return joint_owner.make_rid(joint);
}

void Box3DPhysicsServer::cone_twist_joint_set_param(RID p_joint, ConeTwistJointParam p_param, float p_value) {}
float Box3DPhysicsServer::cone_twist_joint_get_param(RID p_joint, ConeTwistJointParam p_param) const { return 0.0; }

RID Box3DPhysicsServer::joint_create_generic_6dof(RID p_body_A, const Transform &p_local_frame_A, RID p_body_B, const Transform &p_local_frame_B) {
	B3_TODO("Generic6DOFJoint (no Box3D equivalent)");
	Box3DJoint *joint = memnew(Box3DJoint(JOINT_6DOF));
	joint->body_a = p_body_A;
	joint->body_b = p_body_B;
	return joint_owner.make_rid(joint);
}

void Box3DPhysicsServer::generic_6dof_joint_set_param(RID p_joint, Vector3::Axis p_axis, G6DOFJointAxisParam p_param, float p_value) {}
float Box3DPhysicsServer::generic_6dof_joint_get_param(RID p_joint, Vector3::Axis p_axis, G6DOFJointAxisParam p_param) { return 0.0; }
void Box3DPhysicsServer::generic_6dof_joint_set_flag(RID p_joint, Vector3::Axis p_axis, G6DOFJointAxisFlag p_flag, bool p_enable) {}
bool Box3DPhysicsServer::generic_6dof_joint_get_flag(RID p_joint, Vector3::Axis p_axis, G6DOFJointAxisFlag p_flag) { return false; }

/* MISC */

void Box3DPhysicsServer::free(RID p_rid) {
	if (shape_owner.owns(p_rid)) {
		// Godot frees a shape only once nothing references it, so any b3 shapes
		// built from this height field are already gone by now.
		Box3DShape *shape = shape_owner.getornull(p_rid);
		shape->free_geometry();
		shape_owner.free(p_rid);
		memdelete(shape);

	} else if (body_owner.owns(p_rid)) {
		// Leave the world before the bookkeeping goes away, or the space would be
		// left holding a dangling Box3DBody pointer in its body list.
		Box3DBody *body = body_owner.getornull(p_rid);
		_body_exit_world(body);
		body_owner.free(p_rid);
		memdelete(body);

	} else if (area_owner.owns(p_rid)) {
		Box3DArea *area = area_owner.getornull(p_rid);
		area_owner.free(p_rid);
		memdelete(area);

	} else if (joint_owner.owns(p_rid)) {
		Box3DJoint *joint = joint_owner.getornull(p_rid);
		joint_owner.free(p_rid);
		memdelete(joint);

	} else if (space_owner.owns(p_rid)) {
		Box3DSpace *space = space_owner.getornull(p_rid);
		space_set_active(p_rid, false);
		if (space->default_area) {
			memdelete(space->default_area);
			space->default_area = nullptr;
		}
		space_owner.free(p_rid);
		memdelete(space);

	} else {
		ERR_FAIL_MSG("Box3D: attempted to free an invalid RID.");
	}
}

void Box3DPhysicsServer::init() {
	Box3DPhysicsDirectBodyState::init_singleton();

	WARN_PRINT("Box3D physics backend is partially implemented: rigid bodies simulate "
			   "(sphere/box/capsule/cylinder/convex shapes), but scene queries, "
			   "areas, joints, trimesh/heightmap shapes and KinematicBody motion "
			   "are not wired up yet. Set physics/3d/physics_engine back to "
			   "\"DEFAULT\" for a fully working world.");
}

void Box3DPhysicsServer::step(float p_delta) {
	if (!active) {
		return;
	}

	for (int i = 0; i < active_spaces.size(); i++) {
		active_spaces[i]->step(p_delta);
	}
}

void Box3DPhysicsServer::flush_queries() {
	if (!active) {
		return;
	}

	doing_sync = true;
	for (int i = 0; i < active_spaces.size(); i++) {
		active_spaces[i]->call_queries();
	}
	doing_sync = false;
}

void Box3DPhysicsServer::finish() {
	Box3DPhysicsDirectBodyState::free_singleton();
}

void Box3DPhysicsServer::set_collision_iterations(int p_iterations) {
	collision_iterations = p_iterations;
	for (int i = 0; i < active_spaces.size(); i++) {
		active_spaces[i]->sub_step_count = p_iterations;
	}
}

int Box3DPhysicsServer::get_process_info(ProcessInfo p_info) {
	return 0;
}
