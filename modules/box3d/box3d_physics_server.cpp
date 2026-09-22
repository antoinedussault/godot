#include "box3d_physics_server.h"
#include "box3d_types_converter.h"

#include "box3d/constants.h"

#include "core/error_macros.h"
#include "core/os/os.h"
#include "core/project_settings.h"

#include <float.h> // FLT_MAX, for fully rigid mover planes

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

// Set BOX3D_DEBUG_MOTION=1 to trace every character motion test. Off by default
// so it costs one already-cached bool in the hot path.
static bool b3_debug_motion() {
	static int enabled = -1;
	if (enabled < 0) {
		enabled = OS::get_singleton()->get_environment("BOX3D_DEBUG_MOTION") == "1" ? 1 : 0;
	}
	return enabled == 1;
}

/* SOLVER-SIDE BODY LIFETIME */

// Box3D asserts on non-finite or negative material values and aborts, so every
// value handed to it has to be sanitised first.
static float b3_sanitize_material(real_t p_value) {
	const float v = (float)p_value;
	// This Godot version has is_nan/is_inf but no is_finite.
	if (Math::is_nan(v) || Math::is_inf(v)) {
		return 0.0f;
	}
	return v < 0.0f ? -v : v;
}

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
	const Transform solver_xf = p_body->get_solver_transform();
	def.position = b3_pos_from_godot(solver_xf.origin);
	def.rotation = b3_quat_from_godot(solver_xf.basis.get_rotation_quat());
	def.linearVelocity = b3_from_godot(p_body->linear_velocity);
	def.angularVelocity = b3_from_godot(p_body->angular_velocity);
	def.gravityScale = (float)p_body->gravity_scale;
	def.enableSleep = p_body->can_sleep;
	def.isAwake = !p_body->sleeping;

	// Lets a query result walk b3ShapeId -> b3BodyId -> Box3DBody in one hop,
	// which is how every scene query resolves what it hit.
	def.userData = static_cast<Box3DCollisionObject *>(p_body);

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
		p_body->shapes.write[i].free_owned_geometry();
	}
}

void Box3DPhysicsServer::_body_update_filters(Box3DBody *p_body) {
	if (!p_body->in_world()) {
		return;
	}

	b3Filter filter;
	filter.categoryBits = p_body->collision_layer;
	filter.maskBits = p_body->collision_mask;
	filter.groupIndex = 0;

	for (int i = 0; i < p_body->shapes.size(); i++) {
		if (!B3_IS_NULL(p_body->shapes[i].shape_id)) {
			b3Shape_SetFilter(p_body->shapes[i].shape_id, filter, true);
		}
	}
}

void Box3DPhysicsServer::_body_sync_transform(Box3DBody *p_body) {
	if (!p_body->in_world()) {
		return;
	}
	const Transform t = p_body->get_solver_transform();

	// A KINEMATIC body is moved by Godot as a teleport every frame. Box3D moves
	// kinematic bodies by velocity, and it is the velocity that contacts read --
	// so teleporting alone makes a moving platform or a pushing character slide
	// through dynamic bodies without imparting anything. Give the solver the
	// implied velocity as well so contacts push, while the teleport keeps the
	// position exactly where Godot wants it.
	if (p_body->mode == BODY_MODE_KINEMATIC) {
		Box3DSpace *space = space_owner.getornull(p_body->space);
		const real_t dt = space ? space->last_step_delta : 0.0;
		if (dt > 0) {
			const Transform now = b3_transform_to_godot(b3Body_GetTransform(p_body->body_id));
			b3Body_SetLinearVelocity(p_body->body_id, b3_from_godot((t.origin - now.origin) / dt));
		}
	}

	b3Body_SetTransform(p_body->body_id,
			b3_pos_from_godot(t.origin),
			b3_quat_from_godot(t.basis.get_rotation_quat()));

	if (b3_debug_motion()) {
		const Transform after = b3_transform_to_godot(b3Body_GetTransform(p_body->body_id));
		print_line(String("B3SYNC mode=") + itos((int)p_body->mode) +
				" want=" + String(Variant(t.origin)) +
				" got=" + String(Variant(after.origin)));
	}
}

void Box3DPhysicsServer::_body_update_shape_origin_offset(Box3DBody *p_body) {
	p_body->has_shape_origin_offset = false;
	p_body->shape_origin_local = Transform();
	p_body->shape_corner_offset = Vector3();

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

	// Scale lives in the body basis in Godot, and is baked into the shape for
	// Box3D, so the offset has to be computed in the same baked frame.
	Transform scale_xf;
	scale_xf.basis.scale(p_body->transform.basis.get_scale());
	const Transform local = scale_xf * p_body->shapes[heightmap_index].xform;
	const Vector3 scale = local.basis.get_scale();

	// Godot centers the field on the shape origin and spans (width-1) by
	// (depth-1) grid steps; Box3D runs from its origin in +X/+Z. The step size is
	// the scale, so the shift is half the SCALED extent -- using grid units here
	// would misplace any terrain whose spacing is not exactly 1.
	p_body->has_shape_origin_offset = true;
	p_body->shape_origin_local = local;
	p_body->shape_corner_offset = Vector3(
			-(real_t)(width - 1) * scale.x * 0.5,
			0,
			-(real_t)(depth - 1) * scale.z * 0.5);
}

// Rebuilds ONE shape entry. Everything expensive about a shape lives here --
// hull construction, height field blob creation, mesh welding -- so the
// difference between rebuilding one entry and all of them is the difference
// between O(1) and O(N) per edit, and O(N) and O(N^2) for building a body up one
// shape at a time. SpatialCable and CellTerrain both do exactly that.
void Box3DPhysicsServer::_body_rebuild_shape_at(Box3DBody *p_body, int p_index) {
	ERR_FAIL_NULL(p_body);
	ERR_FAIL_INDEX(p_index, p_body->shapes.size());
	if (!p_body->in_world()) {
		return;
	}

	Box3DShapeEntry &entry = p_body->shapes.write[p_index];

	if (!B3_IS_NULL(entry.shape_id)) {
		b3DestroyShape(entry.shape_id, false);
		entry.shape_id = b3_nullShapeId;
	}
	// Free after the shape referencing it is gone, never before.
	entry.free_owned_geometry();
	if (entry.disabled) {
		return;
	}

	Box3DShape *shape = shape_owner.getornull(entry.shape);
	if (!shape) {
		return;
	}

	Transform scale_xf;
	scale_xf.basis.scale(p_body->baked_scale);

	b3ShapeDef def = b3DefaultShapeDef();
	def.density = 1.0f;
	// Godot encodes PhysicsMaterial's "rough" and "absorbent" flags as a
	// NEGATIVE friction/bounce (scene/resources/physics_material.h:57-69).
	// Box3D asserts both are finite and non-negative and aborts the process
	// otherwise, so take the magnitude.
	def.baseMaterial.friction = b3_sanitize_material(p_body->friction);
	def.baseMaterial.restitution = b3_sanitize_material(p_body->bounce);
	def.filter.categoryBits = p_body->collision_layer;
	def.filter.maskBits = p_body->collision_mask;
	// Sensor events are opt-in on BOTH sides -- "This applies to sensors and
	// non-sensors. False by default, even for sensors." Without this on the
	// visiting body, an Area sees nothing enter it. Only convex shapes can act
	// as visitors, so a trimesh or height field body never triggers an Area.
	def.enableSensorEvents = true;
	// Mass is recomputed by the caller once, after all edits.
	def.updateBodyMass = false;

	entry.shape_id = shape->attach(p_body->body_id, &def, scale_xf * entry.xform, &entry);
}

// Settles everything that depends on the body's overall shape layout. Cheap:
// no solver geometry is rebuilt here.
void Box3DPhysicsServer::_body_finish_shape_edit(Box3DBody *p_body) {
	if (!p_body->in_world()) {
		return;
	}
	_body_update_shape_origin_offset(p_body);
	_body_sync_transform(p_body);
	_body_apply_mass(p_body);
}

void Box3DPhysicsServer::_body_rebuild_shapes(Box3DBody *p_body) {
	ERR_FAIL_NULL(p_body);
	if (!p_body->in_world()) {
		return;
	}

	// Godot keeps scale in the body's basis; Box3D bodies are rigid. Bake the
	// body scale into each shape's local transform so nothing is lost, and give
	// the solver body the rotation-only transform (get_solver_transform()).
	p_body->baked_scale = p_body->transform.basis.get_scale();

	for (int i = 0; i < p_body->shapes.size(); i++) {
		_body_rebuild_shape_at(p_body, i);
	}

	_body_finish_shape_edit(p_body);
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

/* AREA SOLVER LIFETIME */

void Box3DPhysicsServer::_area_sync_transform(Box3DArea *p_area) {
	if (!p_area->in_world()) {
		return;
	}
	const Transform rigid(p_area->transform.basis.orthonormalized(), p_area->transform.origin);
	b3Body_SetTransform(p_area->body_id,
			b3_pos_from_godot(rigid.origin),
			b3_quat_from_godot(rigid.basis.get_rotation_quat()));
}

void Box3DPhysicsServer::_area_rebuild_shapes(Box3DArea *p_area) {
	if (!p_area->in_world()) {
		return;
	}

	Transform scale_xf;
	scale_xf.basis.scale(p_area->transform.basis.get_scale());

	for (int i = 0; i < p_area->shapes.size(); i++) {
		Box3DShapeEntry &entry = p_area->shapes.write[i];

		if (!B3_IS_NULL(entry.shape_id)) {
			b3DestroyShape(entry.shape_id, false);
			entry.shape_id = b3_nullShapeId;
		}
		entry.free_owned_geometry();
		if (entry.disabled) {
			continue;
		}

		Box3DShape *shape = shape_owner.getornull(entry.shape);
		if (!shape) {
			continue;
		}

		b3ShapeDef def = b3DefaultShapeDef();
		def.density = 0.0f;
		// A sensor detects overlap without resolving it, which is exactly what an
		// Area is. Sensor events are what the monitor callbacks are driven from.
		def.isSensor = true;
		def.enableSensorEvents = true;
		def.filter.categoryBits = p_area->collision_layer;
		def.filter.maskBits = p_area->collision_mask;
		def.updateBodyMass = false;

		entry.shape_id = shape->attach(p_area->body_id, &def, scale_xf * entry.xform, &entry);
	}
}

void Box3DPhysicsServer::_area_enter_world(Box3DArea *p_area, Box3DSpace *p_space) {
	ERR_FAIL_NULL(p_area);
	ERR_FAIL_NULL(p_space);
	if (B3_IS_NULL(p_space->world_id)) {
		p_space->create_world();
	}
	ERR_FAIL_COND(B3_IS_NULL(p_space->world_id));

	const Transform rigid(p_area->transform.basis.orthonormalized(), p_area->transform.origin);

	b3BodyDef def = b3DefaultBodyDef();
	def.type = b3_staticBody;
	def.position = b3_pos_from_godot(rigid.origin);
	def.rotation = b3_quat_from_godot(rigid.basis.get_rotation_quat());
	// Shared with bodies, so a sensor event can resolve either side back to its
	// Godot object; Box3DCollisionObject::type says which.
	def.userData = static_cast<Box3DCollisionObject *>(p_area);

	p_area->body_id = b3CreateBody(p_space->world_id, &def);
	p_space->add_area(p_area);
	_area_rebuild_shapes(p_area);
}

void Box3DPhysicsServer::_area_exit_world(Box3DArea *p_area) {
	ERR_FAIL_NULL(p_area);

	if (Box3DSpace *space = space_owner.getornull(p_area->space)) {
		space->remove_area(p_area);
		// Bodies hold raw pointers to overlapping areas for the gravity override.
		space->forget_area(p_area);
	}
	if (p_area->in_world()) {
		b3DestroyBody(p_area->body_id);
		p_area->body_id = b3_nullBodyId;
	}
	for (int i = 0; i < p_area->shapes.size(); i++) {
		p_area->shapes.write[i].shape_id = b3_nullShapeId;
		p_area->shapes.write[i].free_owned_geometry();
	}
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
		return;
	}

	for (Set<RID>::Element *E = shape->owners.front(); E; E = E->next()) {
		Box3DBody *body = body_owner.getornull(E->get());
		if (!body || !body->in_world()) {
			continue;
		}
		for (int i = 0; i < body->shapes.size(); i++) {
			if (body->shapes[i].shape != p_shape) {
				continue;
			}
			if (!B3_IS_NULL(body->shapes[i].shape_id)) {
				b3DestroyShape(body->shapes[i].shape_id, false);
				body->shapes.write[i].shape_id = b3_nullShapeId;
			}
		body->shapes.write[i].free_owned_geometry();
		}
	}

	for (Set<RID>::Element *E = shape->owners.front(); E; E = E->next()) {
		Box3DBody *body = body_owner.getornull(E->get());
		if (!body) {
			continue;
		}
		// Only the entries referencing this shape are stale. A terrain body
		// carrying several shapes should not rebuild them all because one
		// height field's data changed -- this is the cell-streaming hot path.
		for (int i = 0; i < body->shapes.size(); i++) {
			if (body->shapes[i].shape == E->get() || body->shapes[i].shape == p_shape) {
				_body_rebuild_shape_at(body, i);
			}
		}
		_body_finish_shape_edit(body);
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
	if (area->space == p_space) {
		return;
	}
	if (area->space.is_valid()) {
		_area_exit_world(area);
	}
	area->space = p_space;
	if (p_space.is_valid()) {
		Box3DSpace *space = space_owner.getornull(p_space);
		ERR_FAIL_NULL(space);
		_area_enter_world(area, space);
	}
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
	_area_rebuild_shapes(area);
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
	_area_sync_transform(area);
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
	// Only the new entry needs building. Rebuilding the whole list here made
	// assembling an N-shape body cost O(N^2) of hull/mesh construction.
	_body_rebuild_shape_at(body, body->shapes.size() - 1);
	_body_finish_shape_edit(body);
}

void Box3DPhysicsServer::body_set_shape(RID p_body, int p_shape_idx, RID p_shape) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	ERR_FAIL_INDEX(p_shape_idx, body->shapes.size());
	Box3DShape *shape = shape_owner.getornull(p_shape);
	ERR_FAIL_NULL(shape);

	body->shapes.write[p_shape_idx].shape = p_shape;
	shape->owners.insert(p_body);
	_body_rebuild_shape_at(body, p_shape_idx);
	_body_finish_shape_edit(body);
}

void Box3DPhysicsServer::body_set_shape_transform(RID p_body, int p_shape_idx, const Transform &p_transform) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	ERR_FAIL_INDEX(p_shape_idx, body->shapes.size());
	body->shapes.write[p_shape_idx].xform = p_transform;
	_body_rebuild_shape_at(body, p_shape_idx);
	_body_finish_shape_edit(body);
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
	_body_rebuild_shape_at(body, p_shape_idx);
	_body_finish_shape_edit(body);
}

void Box3DPhysicsServer::body_remove_shape(RID p_body, int p_shape_idx) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	ERR_FAIL_INDEX(p_shape_idx, body->shapes.size());

	if (body->in_world() && !B3_IS_NULL(body->shapes[p_shape_idx].shape_id)) {
		b3DestroyShape(body->shapes[p_shape_idx].shape_id, false);
	}
		body->shapes.write[p_shape_idx].free_owned_geometry();

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

	// The surviving entries keep their b3 shapes; only the body-level state
	// needs settling.
	_body_finish_shape_edit(body);
}

void Box3DPhysicsServer::body_clear_shapes(RID p_body) {
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);

	for (int i = 0; i < body->shapes.size(); i++) {
		if (body->in_world() && !B3_IS_NULL(body->shapes[i].shape_id)) {
			b3DestroyShape(body->shapes[i].shape_id, false);
		}
		body->shapes.write[i].free_owned_geometry();
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
	// Godot sets layer and mask independently of the shape list, and usually
	// after the shapes already exist. Without this the solver keeps the filter
	// captured at attach time -- which for a default-constructed body is layer 1,
	// so anything on another layer silently stops colliding.
	_body_update_filters(body);
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
	_body_update_filters(body);
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
			if (b3_debug_motion()) {
				print_line(String("B3STATE mode=") + itos((int)body->mode) +
						" in_world=" + itos((int)body->in_world()) +
						" pos=" + String(Variant(body->transform.origin)));
			}

			// Scale is baked into the shapes, not carried by the solver body, so a
			// change to it invalidates them. Rebuilding only when the scale
			// actually moved keeps ordinary movement cheap -- this runs on every
			// moved body, every frame.
			const Vector3 new_scale = body->transform.basis.get_scale();
			if (!new_scale.is_equal_approx(body->baked_scale)) {
				_body_rebuild_shapes(body); // also re-syncs the transform
			} else {
				_body_sync_transform(body);
			}
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
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL(body);
	if (body->in_world()) {
		b3Body_ApplyAngularImpulse(body->body_id, b3_from_godot(p_impulse), true);
	}
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
	if (!body_owner.owns(p_body)) {
		return nullptr;
	}
	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, nullptr);

	Box3DSpace *space = space_owner.getornull(body->space);
	if (!space) {
		return nullptr;
	}
	ERR_FAIL_COND_V_MSG(space->locked, nullptr, "Body state is inaccessible right now, wait for iteration or physics process notification.");
	ERR_FAIL_NULL_V(Box3DPhysicsDirectBodyState::singleton, nullptr);

	Box3DPhysicsDirectBodyState::singleton->body = body;
	return Box3DPhysicsDirectBodyState::singleton;
}

// Depenetration planes gathered by b3World_CollideMover.
#define B3_MAX_MOVER_PLANES 32

// True for bodies the solver drives itself - a RigidBody, in any of its modes.
// Static and kinematic geometry is NOT dynamic: a moving platform is expected
// to carry and shove a character around.
static inline bool b3_is_dynamic(const Box3DBody *p_body) {
	return p_body->mode == PhysicsServer::BODY_MODE_RIGID ||
			p_body->mode == PhysicsServer::BODY_MODE_CHARACTER;
}

struct Box3DMoverPlaneContext {
	const Set<RID> *exclude = nullptr;
	// Drop planes belonging to dynamic bodies. Set when the planes are going to
	// be turned into a depenetration push applied to the CHARACTER: a prop must
	// never move the character, only the other way round. Left false when the
	// planes are only being read for a contact normal, since the character does
	// still need to know it touched the prop.
	bool skip_dynamic = false;
	b3CollisionPlane planes[B3_MAX_MOVER_PLANES];
	// Parallel to `planes`, so the winning plane can name what was touched.
	Box3DBody *plane_bodies[B3_MAX_MOVER_PLANES];
	Vector3 plane_points[B3_MAX_MOVER_PLANES];
	int plane_shapes[B3_MAX_MOVER_PLANES];
	int count = 0;
};

struct Box3DMoverSweepContext {
	const Set<RID> *exclude = nullptr;
	// Godot's documented infinite_inertia: the character pushes RigidBodies but
	// stops detecting collisions with them entirely.
	bool skip_dynamic = false;
};

static bool b3_mover_filter_callback(b3ShapeId shapeId, void *context) {
	const Box3DMoverSweepContext *ctx = static_cast<const Box3DMoverSweepContext *>(context);
	Box3DBody *body = b3_body_from_shape(shapeId, nullptr);
	if (!body) {
		return false;
	}
	if (ctx->exclude && ctx->exclude->has(body->self)) {
		return false;
	}
	if (ctx->skip_dynamic && b3_is_dynamic(body)) {
		return false;
	}
	return true;
}

static bool b3_mover_plane_callback(b3ShapeId shapeId, const b3PlaneResult *planes, int planeCount, void *context) {
	Box3DMoverPlaneContext *ctx = static_cast<Box3DMoverPlaneContext *>(context);

	int shape_index = 0;
	Box3DBody *body = b3_body_from_shape(shapeId, &shape_index);
	if (!body) {
		return true;
	}
	if (ctx->exclude && ctx->exclude->has(body->self)) {
		return true;
	}
	if (ctx->skip_dynamic && b3_is_dynamic(body)) {
		return true;
	}

	for (int i = 0; i < planeCount && ctx->count < B3_MAX_MOVER_PLANES; i++) {
		ctx->plane_bodies[ctx->count] = body;
		ctx->plane_points[ctx->count] = b3_to_godot(planes[i].point);
		ctx->plane_shapes[ctx->count] = shape_index;

		b3CollisionPlane &cp = ctx->planes[ctx->count++];
		cp.plane = planes[i].plane;
		// FLT_MAX makes the plane fully rigid. A character standing on terrain
		// must not be allowed to sink into it, so no softness here.
		cp.pushLimit = FLT_MAX;
		cp.push = 0.0f;
		cp.clipVelocity = true;
	}
	return true;
}

// Builds the mover capsule for a body, in world space, if the body is shaped
// like a character: exactly one enabled capsule or sphere. Returns false
// otherwise, which costs the body its depenetration but not its motion test.
static bool b3_build_mover_capsule(const Box3DBody *p_body, Box3DPhysicsServer *p_server, const Transform &p_from, b3Capsule *r_capsule) {
	// Use the first enabled capsule or sphere. A character rig often carries extra
	// shapes (a hitbox, a crouch volume); requiring exactly one would drop such a
	// body onto the shape-cast path, where standing on the ground reports impact
	// fraction 0 and the character cannot move at all.
	for (int i = 0; i < p_body->shapes.size(); i++) {
		if (p_body->shapes[i].disabled) {
			continue;
		}

		Box3DShape *shape = p_server->get_shape(p_body->shapes[i].shape);
		if (!shape) {
			continue;
		}

		const Transform xf = p_from * p_body->shapes[i].xform;

		if (shape->type == PhysicsServer::SHAPE_CAPSULE) {
			Dictionary d = shape->data;
			if (!d.has("radius") || !d.has("height")) {
				continue;
			}
			const real_t radius = d["radius"];
			const real_t height = d["height"];
			// Godot's capsule is Z-aligned, its height excluding the caps.
			r_capsule->center1 = b3_from_godot(xf.xform(Vector3(0, 0, height * 0.5)));
			r_capsule->center2 = b3_from_godot(xf.xform(Vector3(0, 0, -height * 0.5)));
			r_capsule->radius = (float)radius;
			return true;
		}

		if (shape->type == PhysicsServer::SHAPE_SPHERE) {
			// A sphere is a capsule with coincident endpoints.
			const b3Vec3 c = b3_from_godot(xf.origin);
			r_capsule->center1 = c;
			r_capsule->center2 = c;
			r_capsule->radius = (float)(real_t)shape->data;
			return true;
		}
	}

	return false;
}

bool Box3DPhysicsServer::body_test_motion(RID p_body, const Transform &p_from, const Vector3 &p_motion, bool p_infinite_inertia, MotionResult *r_result, bool p_exclude_raycast_shapes, const Set<RID> &p_exclude) {
	// This is what gives KinematicBody.move_and_slide() its behaviour. Godot
	// performs the sliding itself; what it needs back is how far the body may
	// travel, what it would touch, and the surface normal to slide along.
	//
	// Two stages:
	//   1. Depenetration. Box3D's mover planes (b3World_CollideMover) plus
	//      b3SolvePlanes resolve any existing overlap. Without this a character
	//      that ends a frame slightly inside the ground stays stuck there,
	//      because a shape cast treats initial overlap as a miss.
	//   2. A shape cast along the motion for the time of impact and normal.
	//
	// Stage 1 is capsule-only because that is what Box3D's mover API accepts;
	// other shapes still get stage 2.
	if (r_result) {
		r_result->motion = p_motion;
		r_result->remainder = Vector3();
		r_result->collision_depth = 0;
		r_result->collision_safe_fraction = 0;
		r_result->collision_unsafe_fraction = 0;
	}

	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, false);

	Box3DSpace *space = space_owner.getornull(body->space);
	if (!space || B3_IS_NULL(space->world_id)) {
		return false;
	}

	// A body must never collide with itself, and Godot's collision exceptions
	// apply here just as they do during simulation.
	Set<RID> exclude = p_exclude;
	exclude.insert(p_body);
	for (Set<RID>::Element *E = body->collision_exceptions.front(); E; E = E->next()) {
		exclude.insert(E->get());
	}

	const b3QueryFilter filter = b3_query_filter(body->collision_mask);
	const b3Pos world_origin = b3_pos_from_godot(Vector3());

	// --- Character path ---------------------------------------------------
	//
	// body_test_motion's contract is "move until first contact, then STOP". It
	// must NOT slide: move_and_slide() does that itself by re-calling with the
	// remainder projected along the normal returned here.
	//
	// Solving contact planes against the full motion slides instead of sweeping,
	// and that is fatal for the downward probe move_and_slide_with_snap() sends
	// every frame: asked to move (0, -0.8, 0) on a slope, a plane solve projects
	// that onto the slope and answers with DOWN-SLOPE travel -- horizontal motion
	// the caller never asked for. Godot applies it verbatim, so the character
	// teleports downhill a little every frame until it loses the floor.
	//
	// So: use the plane solver only to push out of existing penetration (target
	// delta zero -- no sliding), then sweep.
	b3Capsule mover;
	if (b3_build_mover_capsule(body, this, p_from, &mover)) {
		// 1. Depenetration.
		Vector3 recover;
		{
			Box3DMoverPlaneContext pctx;
			pctx.exclude = &exclude;
			// A RigidBody must never depenetrate a character.
			//
			// This is what launched a player who leaned on a pushable prop. The
			// recovery delta is unbounded (b3SolvePlanes with pushLimit FLT_MAX
			// treats every plane as perfectly rigid), it is folded into
			// MotionResult::motion, and move_and_slide() applies that to the
			// body on EVERY iteration - up to max_slides times per frame, plus
			// the snap probe. A prop resting against the capsule regenerates the
			// overlap each frame, so the character gets shoved again and again
			// and climbs away at hundreds of units a second.
			//
			// Characters collide against the world; props are pushed BY
			// characters. That is the standard one-way contract, and skipping
			// dynamic bodies here is what enforces it. The sweep below still
			// sees the prop, so the character is blocked by it and still reports
			// the contact through get_slide_collision() - which is what game
			// code uses to apply the push in the correct direction.
			pctx.skip_dynamic = true;
			b3World_CollideMover(space->world_id, world_origin, &mover, filter,
					b3_mover_plane_callback, &pctx);
			if (pctx.count > 0) {
				b3Vec3 zero = { 0.0f, 0.0f, 0.0f };
				recover = b3_to_godot(b3SolvePlanes(zero, pctx.planes, pctx.count).delta);

				// Tolerate penetration up to the kinematic margin instead of
				// pushing fully out every frame.
				//
				// The recovery is folded into MotionResult::motion, and
				// move_and_slide_with_snap() applies that to the body -- so a
				// push that is regenerated every frame gets re-applied every
				// frame. Resting on a surface, the solver's own contact pushout
				// keeps the body a hair inside, so fighting it produces a
				// permanent up-down fight: visible as Y jitter, and on a slope
				// the horizontal part of the push eats into walking speed.
				// The floor for this is Box3D's own resting tolerance, not Godot's
				// margin. B3_LINEAR_SLOP is 0.005: the solver deliberately keeps
				// resting bodies up to 5mm deep. Godot's default kinematic margin
				// is 0.001, five times tighter -- so recovering to it pushes out
				// penetration the solver immediately recreates, every frame,
				// forever. That residual fight is the remaining Y jitter.
				const real_t margin = MAX(body->kinematic_safe_margin, (real_t)B3_LINEAR_SLOP);
				const real_t len = recover.length();
				if (len <= margin) {
					recover = Vector3();
				} else {
					recover *= (len - margin) / len;
				}
			}
		}

		const Transform from(p_from.basis, p_from.origin + recover);

		b3Capsule swept;
		if (!b3_build_mover_capsule(body, this, from, &swept)) {
			return false;
		}

		// 2. Sweep to first contact.
		Box3DMoverSweepContext sctx;
		sctx.exclude = &exclude;
		// infinite_inertia was accepted and then ignored here, so move_and_slide's
		// last argument did nothing at all. Honour it: true means "walk through
		// RigidBodies, shoving them aside"; false keeps the character blocked.
		sctx.skip_dynamic = p_infinite_inertia;
		const float fraction = b3World_CastMover(space->world_id, world_origin, &swept,
				b3_from_godot(p_motion), filter, b3_mover_filter_callback, &sctx);
		real_t travel_fraction = CLAMP((real_t)fraction, (real_t)0.0, (real_t)1.0);
		Vector3 travel = p_motion * travel_fraction;

		// A gap narrower than the solver's own slop means "already touching".
		// Without this the snap probe closes that sub-millimetre gap every frame
		// while the contact solver pushes back out within B3_LINEAR_SLOP, and the
		// body oscillates inside the slop band forever -- small, but it is exactly
		// the residual Y jitter a resting character shows. Report the contact, but
		// no travel.
		if (travel_fraction < 1.0 && travel.length() < (real_t)B3_LINEAR_SLOP) {
			travel = Vector3();
			travel_fraction = 0.0;
		}

		// Nothing blocked the motion. Depenetration on its own is NOT a collision:
		// move_and_collide() applies result.motion either way, so the recovery
		// still lands, but the boolean must stay false.
		//
		// Reporting true here manufactures a contact out of leftover recovery
		// planes, and move_and_slide() then believes it. Walking off a cliff, the
		// capsule still grazes the edge, the most floor-like leftover plane is the
		// cliff top, so is_on_floor() stays true and the character hangs in the
		// air instead of falling. Jumping, move_and_slide() slides the velocity
		// along that phantom up-normal, which cancels the jump outright.
		if (travel_fraction >= 1.0) {
			if (r_result) {
				r_result->motion = recover + p_motion;
				r_result->remainder = Vector3();
				r_result->collision_safe_fraction = 1.0;
				r_result->collision_unsafe_fraction = 1.0;
			}
			return false;
		}

		// 3. The contact normal, measured where the sweep stopped, so
		// move_and_slide() slides along the surface actually hit.
		Vector3 normal;
		Vector3 point;
		real_t depth = 0;
		Box3DBody *other = nullptr;
		int other_shape = 0;
		{
			const Transform at(from.basis, from.origin + travel);
			b3Capsule contact_mover;
			if (b3_build_mover_capsule(body, this, at, &contact_mover)) {
				Box3DMoverPlaneContext pctx;
				pctx.exclude = &exclude;
				b3World_CollideMover(space->world_id, world_origin, &contact_mover, filter,
						b3_mover_plane_callback, &pctx);

				const Vector3 dir = p_motion.length_squared() > CMP_EPSILON ? p_motion.normalized() : Vector3();
				real_t best = -1e20;
				int best_plane = -1;
				for (int i = 0; i < pctx.count; i++) {
					const Vector3 n = b3_to_godot(pctx.planes[i].plane.normal);
					if (n.length_squared() <= CMP_EPSILON) {
						continue;
					}
					// Prefer whatever stands in the way of the requested motion;
					// with no motion, the most floor-like contact.
					const real_t score = dir == Vector3() ? n.normalized().y : -n.normalized().dot(dir);
					if (score > best) {
						best = score;
						best_plane = i;
					}
				}
				if (best_plane >= 0) {
					normal = b3_to_godot(pctx.planes[best_plane].plane.normal).normalized();
					point = pctx.plane_points[best_plane];
					depth = pctx.planes[best_plane].push;
					other = pctx.plane_bodies[best_plane];
					other_shape = pctx.plane_shapes[best_plane];
				}
			}
		}

		if (normal.length_squared() <= CMP_EPSILON) {
			normal = p_motion.length_squared() > CMP_EPSILON ? -p_motion.normalized() : Vector3(0, 1, 0);
		}

		if (r_result) {
			r_result->motion = recover + travel;
			r_result->remainder = p_motion - travel;
			r_result->collision_safe_fraction = travel_fraction;
			r_result->collision_unsafe_fraction = travel_fraction;
			r_result->collision_normal = normal;
			r_result->collision_point = point;
			r_result->collision_depth = depth;
			r_result->collision_local_shape = 0;
			if (other) {
				r_result->collider = other->self;
				r_result->collider_id = other->instance_id;
				r_result->collider_shape = other_shape;
				r_result->collider_velocity = other->linear_velocity;
			}
		}

		if (b3_debug_motion()) {
			print_line(String("B3MOVE motion=") + String(Variant(p_motion)) +
					" recover=" + String(Variant(recover)) +
					" travel=" + String(Variant(travel)) +
					" frac=" + rtos(travel_fraction) +
					" n=" + String(Variant(normal)));
		}
		return true;
	}

	// --- Generic path: shape cast -----------------------------------------
	//
	// Non-capsule bodies get no depenetration, because Box3D's plane gathering is
	// capsule-only. They still get a correct time of impact.
	Box3DClosestHitContext best;
	best.fraction = 1.0f;

	for (int i = 0; i < body->shapes.size(); i++) {
		if (body->shapes[i].disabled) {
			continue;
		}
		Box3DShape *shape = shape_owner.getornull(body->shapes[i].shape);
		if (!shape) {
			continue;
		}
		if (p_exclude_raycast_shapes && shape->type == SHAPE_RAY) {
			continue;
		}

		Vector<b3Vec3> points;
		b3ShapeProxy proxy;
		if (!shape->build_proxy(points, p_from * body->shapes[i].xform, &proxy)) {
			continue; // concave/heightmap/plane cannot be cast, only cast against
		}

		Box3DClosestHitContext ctx;
		ctx.exclude = &exclude;
		if (p_motion.length_squared() > CMP_EPSILON) {
			ctx.reject_non_opposing = true;
			ctx.motion_dir = p_motion.normalized();
		}
		b3World_CastShape(space->world_id, world_origin, &proxy, b3_from_godot(p_motion),
				filter, b3_closest_hit_callback, &ctx);

		if (ctx.hit && ctx.fraction <= best.fraction) {
			best = ctx;
			best.shape_index = i;
		}
	}

	if (!best.hit || !best.body) {
		return false;
	}

	if (r_result) {
		const real_t safe = (real_t)best.fraction;
		r_result->motion = p_motion * safe;
		r_result->remainder = p_motion - r_result->motion;
		r_result->collision_safe_fraction = safe;
		r_result->collision_unsafe_fraction = safe;

		// Box3D can hand back a degenerate normal when the cast starts already
		// touching. Godot asserts on a non-unit normal (vector3.h:442).
		Vector3 normal = best.normal;
		if (normal.length_squared() > CMP_EPSILON) {
			normal.normalize();
		} else {
			normal = p_motion.length_squared() > CMP_EPSILON ? -p_motion.normalized() : Vector3(0, 1, 0);
		}

		r_result->collision_point = best.point;
		r_result->collision_normal = normal;
		r_result->collision_local_shape = best.shape_index;
		r_result->collider = best.body->self;
		r_result->collider_id = best.body->instance_id;
		r_result->collider_shape = best.shape_index;
		r_result->collider_velocity = best.body->linear_velocity;
	}
	return true;
}

int Box3DPhysicsServer::body_test_ray_separation(RID p_body, const Transform &p_transform, bool p_infinite_inertia, Vector3 &r_recover_motion, SeparationResult *r_results, int p_result_max, float p_margin) {
	// move_and_slide() calls this to unstick a body before moving it (and for
	// snap-to-floor). The separation results feed RayShape handling, which Box3D
	// has no equivalent for, but the recover motion is exactly the mover-plane
	// depenetration used by body_test_motion -- so that part is real.
	r_recover_motion = Vector3();

	Box3DBody *body = body_owner.getornull(p_body);
	ERR_FAIL_NULL_V(body, 0);

	Box3DSpace *space = space_owner.getornull(body->space);
	if (!space || B3_IS_NULL(space->world_id)) {
		return 0;
	}

	b3Capsule mover;
	if (!b3_build_mover_capsule(body, this, p_transform, &mover)) {
		return 0;
	}

	Set<RID> exclude;
	exclude.insert(p_body);
	for (Set<RID>::Element *E = body->collision_exceptions.front(); E; E = E->next()) {
		exclude.insert(E->get());
	}

	Box3DMoverPlaneContext pctx;
	pctx.exclude = &exclude;
	// Same one-way rule as body_test_motion: move_and_slide() applies this
	// recover motion to the character directly, so a prop must not contribute
	// to it.
	pctx.skip_dynamic = true;
	b3World_CollideMover(space->world_id, b3_pos_from_godot(Vector3()), &mover,
			b3_query_filter(body->collision_mask), b3_mover_plane_callback, &pctx);

	if (pctx.count > 0) {
		b3Vec3 zero = { 0.0f, 0.0f, 0.0f };
		r_recover_motion = b3_to_godot(b3SolvePlanes(zero, pctx.planes, pctx.count).delta);
	}

	// No RayShape support, so no separation results to report.
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
		Box3DShape *shape = shape_owner.getornull(p_rid);
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
		_area_exit_world(area);
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

	WARN_PRINT("Box3D physics backend is partially implemented. Working: rigid bodies, "
			   "KinematicBody movement, height fields, trimesh, and ray/shape/point "
			   "queries. Not wired up: Areas (so no Area signals or gravity overrides), "
			   "joints, contact reporting in _integrate_forces, and RayShape. Set "
			   "physics/3d/physics_engine back to \"DEFAULT\" for a fully working world.");
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
