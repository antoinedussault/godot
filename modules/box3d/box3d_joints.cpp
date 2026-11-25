#include "box3d_physics_server.h"
#include "box3d_types_converter.h"

#include "core/engine.h"
#include "core/error_macros.h"

// Joints.
//
// The bookkeeping on Box3DJoint is authoritative and outlives the b3 joint, which
// exists only while both bodies are in the same space. Leaving a space destroys it
// and re-entering one recreates it.
//
//   pin        -> b3SphericalJoint
//   hinge      -> b3RevoluteJoint   (both hinge about the frame's Z)
//   cone twist -> b3SphericalJoint  (Godot twists about X, Box3D about Z)
//   slider     -> b3GenericJoint    (Godot's slider may also turn about its axis,
//                                    which b3PrismaticJoint cannot)
//   6DOF       -> b3GenericJoint
//
// Box3D measures B relative to A. Godot's hinge and 6DOF angles measure A relative
// to B (see HingeJointSW::get_hinge_angle and
// Generic6DOFJointSW::calculateAngleInfo), so their limits and motor speeds flip.
//
// Softness, bias, relaxation and restitution tune the sequential impulse solvers of
// the other backends. Box3D's soft step solver has no counterpart; they are stored
// and returned, and have no effect.

static b3Quat b3_rotate_x_to_z() {
	// Maps the frame's Z onto Godot's X.
	return b3_quat_from_godot(Quat(Vector3(0, 1, 0), Math_PI * 0.5));
}

static real_t b3_physics_step() {
	return 1.0 / MAX(1, Engine::get_singleton()->get_iterations_per_second());
}

// p_frame is in the Godot body's local space, which may carry scale, and may be
// offset from the b3 body for height fields.
static b3Transform b3_joint_frame(const Box3DBody *p_body, const Transform &p_frame) {
	Transform local = p_frame;
	if (p_body) {
		local = p_body->get_solver_transform().affine_inverse() * p_body->transform * p_frame;
	}
	b3Transform xf;
	xf.p = b3_from_godot(local.origin);
	xf.q = b3_quat_from_godot(local.basis.get_rotation_quat());
	return xf;
}

b3GenericAxis Box3DPhysicsServer::_joint_generic_axis(const Box3DJoint *p_joint, int p_row) const {
	b3GenericAxis axis = {};

	if (p_joint->type == JOINT_SLIDER) {
		axis.enableLimit = true;
		if (p_row == 0) {
			axis.lower = p_joint->params[SLIDER_JOINT_LINEAR_LIMIT_LOWER];
			axis.upper = p_joint->params[SLIDER_JOINT_LINEAR_LIMIT_UPPER];
		} else if (p_row == 3) {
			axis.lower = p_joint->params[SLIDER_JOINT_ANGULAR_LIMIT_LOWER];
			axis.upper = p_joint->params[SLIDER_JOINT_ANGULAR_LIMIT_UPPER];
		}
		return axis;
	}

	const int i = p_row % 3;
	const float *params = p_joint->g6dof_params[i];
	const bool *flags = p_joint->g6dof_flags[i];
	if (p_row < 3) {
		axis.enableLimit = flags[G6DOF_JOINT_FLAG_ENABLE_LINEAR_LIMIT];
		axis.lower = params[G6DOF_JOINT_LINEAR_LOWER_LIMIT];
		axis.upper = params[G6DOF_JOINT_LINEAR_UPPER_LIMIT];
		axis.enableMotor = flags[G6DOF_JOINT_FLAG_ENABLE_LINEAR_MOTOR];
		axis.motorSpeed = params[G6DOF_JOINT_LINEAR_MOTOR_TARGET_VELOCITY];
		axis.maxMotorForce = params[G6DOF_JOINT_LINEAR_MOTOR_FORCE_LIMIT];
		axis.enableSpring = flags[G6DOF_JOINT_FLAG_ENABLE_LINEAR_SPRING];
		axis.stiffness = params[G6DOF_JOINT_LINEAR_SPRING_STIFFNESS];
		axis.damping = params[G6DOF_JOINT_LINEAR_SPRING_DAMPING];
		axis.equilibrium = params[G6DOF_JOINT_LINEAR_SPRING_EQUILIBRIUM_POINT];
	} else {
		axis.enableLimit = flags[G6DOF_JOINT_FLAG_ENABLE_ANGULAR_LIMIT];
		axis.lower = -params[G6DOF_JOINT_ANGULAR_UPPER_LIMIT];
		axis.upper = -params[G6DOF_JOINT_ANGULAR_LOWER_LIMIT];
		axis.enableMotor = flags[G6DOF_JOINT_FLAG_ENABLE_MOTOR];
		axis.motorSpeed = -params[G6DOF_JOINT_ANGULAR_MOTOR_TARGET_VELOCITY];
		axis.maxMotorForce = params[G6DOF_JOINT_ANGULAR_MOTOR_FORCE_LIMIT];
		axis.enableSpring = flags[G6DOF_JOINT_FLAG_ENABLE_ANGULAR_SPRING];
		axis.stiffness = params[G6DOF_JOINT_ANGULAR_SPRING_STIFFNESS];
		axis.damping = params[G6DOF_JOINT_ANGULAR_SPRING_DAMPING];
		axis.equilibrium = -params[G6DOF_JOINT_ANGULAR_SPRING_EQUILIBRIUM_POINT];
	}
	axis.maxMotorForce = MAX(axis.maxMotorForce, 0.0f);
	return axis;
}

// Pushes the stored parameters into the live b3 joint.
void Box3DPhysicsServer::_joint_update(Box3DJoint *p_joint) {
	if (!b3Joint_IsValid(p_joint->joint_id)) {
		return;
	}

	const b3JointId id = p_joint->joint_id;
	switch (p_joint->type) {
		case JOINT_PIN: {
		} break;

		case JOINT_HINGE: {
			const float lower = p_joint->params[HINGE_JOINT_LIMIT_LOWER];
			const float upper = p_joint->params[HINGE_JOINT_LIMIT_UPPER];
			const bool use_limit = p_joint->hinge_flags[HINGE_JOINT_FLAG_USE_LIMIT] && lower <= upper;
			if (use_limit) {
				b3RevoluteJoint_SetLimits(id, -upper, -lower);
			}
			b3RevoluteJoint_EnableLimit(id, use_limit);
			b3RevoluteJoint_SetMotorSpeed(id, -p_joint->params[HINGE_JOINT_MOTOR_TARGET_VELOCITY]);
			// Godot gives the motor an impulse per physics step, Box3D a torque.
			b3RevoluteJoint_SetMaxMotorTorque(id, MAX(0.0f, p_joint->params[HINGE_JOINT_MOTOR_MAX_IMPULSE]) / b3_physics_step());
			b3RevoluteJoint_EnableMotor(id, p_joint->hinge_flags[HINGE_JOINT_FLAG_ENABLE_MOTOR]);
		} break;

		case JOINT_CONE_TWIST: {
			const float swing = p_joint->params[CONE_TWIST_JOINT_SWING_SPAN];
			const float twist = p_joint->params[CONE_TWIST_JOINT_TWIST_SPAN];
			// Box3D's cone stops at a right angle. A wider one is treated as no limit,
			// rather than as a much tighter limit than was asked for.
			const bool use_cone = swing >= 0 && swing <= Math_PI * 0.5 + 0.001;
			if (use_cone) {
				b3SphericalJoint_SetConeLimit(id, CLAMP(swing, 0.0f, (float)(Math_PI * 0.5 - 0.0001)));
			}
			b3SphericalJoint_EnableConeLimit(id, use_cone);
			const bool use_twist = twist >= 0 && twist < Math_PI;
			if (use_twist) {
				b3SphericalJoint_SetTwistLimits(id, -twist, twist);
			}
			b3SphericalJoint_EnableTwistLimit(id, use_twist);
		} break;

		case JOINT_SLIDER:
		case JOINT_6DOF: {
			for (int i = 0; i < 6; i++) {
				b3GenericJoint_SetAxis(id, i, _joint_generic_axis(p_joint, i));
			}
		} break;

		default:
			break;
	}

	b3Joint_WakeBodies(id);
}

void Box3DPhysicsServer::_joint_destroy(Box3DJoint *p_joint) {
	// b3DestroyBody destroys a body's joints, so the id may already be stale.
	if (b3Joint_IsValid(p_joint->joint_id)) {
		b3DestroyJoint(p_joint->joint_id, true);
	}
	p_joint->joint_id = b3_nullJointId;
}

void Box3DPhysicsServer::_joint_create(Box3DJoint *p_joint) {
	_joint_destroy(p_joint);

	Box3DBody *a = p_joint->body_a;
	Box3DBody *b = p_joint->body_b;
	// A joint without body B is anchored to the world. One whose body B was freed is
	// dead: p_joint->body_b is null then too, hence the flag.
	if (!a || !a->in_world() || p_joint->broken) {
		return;
	}
	if (b && (!b->in_world() || b->space != a->space)) {
		return;
	}
	Box3DSpace *space = space_owner.getornull(a->space);
	ERR_FAIL_NULL(space);

	b3JointDef base = b3DefaultFilterJointDef().base;
	base.bodyIdA = a->body_id;
	base.bodyIdB = b ? b->body_id : space->get_ground_body();
	base.localFrameA = b3_joint_frame(a, p_joint->frame_a);
	base.localFrameB = b3_joint_frame(b, p_joint->frame_b);
	base.collideConnected = !p_joint->collisions_disabled;
	base.userData = p_joint;

	switch (p_joint->type) {
		case JOINT_PIN: {
			b3SphericalJointDef def = b3DefaultSphericalJointDef();
			def.base = base;
			p_joint->joint_id = b3CreateSphericalJoint(space->world_id, &def);
		} break;
		case JOINT_HINGE: {
			b3RevoluteJointDef def = b3DefaultRevoluteJointDef();
			def.base = base;
			p_joint->joint_id = b3CreateRevoluteJoint(space->world_id, &def);
		} break;
		case JOINT_CONE_TWIST: {
			b3SphericalJointDef def = b3DefaultSphericalJointDef();
			def.base = base;
			def.base.localFrameA.q = b3MulQuat(base.localFrameA.q, b3_rotate_x_to_z());
			def.base.localFrameB.q = b3MulQuat(base.localFrameB.q, b3_rotate_x_to_z());
			p_joint->joint_id = b3CreateSphericalJoint(space->world_id, &def);
		} break;
		case JOINT_SLIDER:
		case JOINT_6DOF: {
			b3GenericJointDef def = b3DefaultGenericJointDef();
			def.base = base;
			for (int i = 0; i < 6; i++) {
				def.axes[i] = _joint_generic_axis(p_joint, i);
			}
			p_joint->joint_id = b3CreateGenericJoint(space->world_id, &def);
		} break;
		default:
			return;
	}

	_joint_update(p_joint);
}

void Box3DPhysicsServer::_body_destroy_joints(Box3DBody *p_body) {
	for (Set<Box3DJoint *>::Element *E = p_body->joints.front(); E; E = E->next()) {
		_joint_destroy(E->get());
	}
}

void Box3DPhysicsServer::_body_create_joints(Box3DBody *p_body) {
	for (Set<Box3DJoint *>::Element *E = p_body->joints.front(); E; E = E->next()) {
		_joint_create(E->get());
	}
}

void Box3DPhysicsServer::_body_break_joints(Box3DBody *p_body) {
	for (Set<Box3DJoint *>::Element *E = p_body->joints.front(); E; E = E->next()) {
		Box3DJoint *joint = E->get();
		_joint_destroy(joint);
		joint->broken = true;
		if (joint->body_a == p_body) {
			joint->body_a = nullptr;
		}
		if (joint->body_b == p_body) {
			joint->body_b = nullptr;
		}
	}
	p_body->joints.clear();
}

RID Box3DPhysicsServer::_joint_make(JointType p_type, RID p_body_a, const Transform &p_frame_a, RID p_body_b, const Transform &p_frame_b) {
	Box3DBody *a = body_owner.getornull(p_body_a);
	ERR_FAIL_NULL_V(a, RID());
	Box3DBody *b = nullptr;
	if (p_body_b.is_valid()) {
		b = body_owner.getornull(p_body_b);
		ERR_FAIL_NULL_V(b, RID());
	}
	ERR_FAIL_COND_V_MSG(a == b, RID(), "Box3D: a joint needs two different bodies.");

	Box3DJoint *joint = memnew(Box3DJoint(p_type));
	joint->body_a = a;
	joint->body_b = b;
	joint->frame_a = p_frame_a;
	joint->frame_b = p_frame_b;

	// The defaults of the other backends, for code that uses the server directly.
	// The joint nodes set every parameter themselves.
	switch (p_type) {
		case JOINT_PIN:
			joint->params[PIN_JOINT_BIAS] = 0.3;
			joint->params[PIN_JOINT_DAMPING] = 1;
			break;
		case JOINT_HINGE:
			joint->params[HINGE_JOINT_BIAS] = 0.3;
			joint->params[HINGE_JOINT_LIMIT_UPPER] = -Math_PI;
			joint->params[HINGE_JOINT_LIMIT_LOWER] = Math_PI;
			joint->params[HINGE_JOINT_LIMIT_BIAS] = 0.3;
			joint->params[HINGE_JOINT_LIMIT_SOFTNESS] = 0.9;
			joint->params[HINGE_JOINT_LIMIT_RELAXATION] = 1;
			break;
		case JOINT_SLIDER:
			for (int i = 0; i < SLIDER_JOINT_MAX; i++) {
				joint->params[i] = 1;
			}
			joint->params[SLIDER_JOINT_LINEAR_LIMIT_UPPER] = -1;
			joint->params[SLIDER_JOINT_LINEAR_LIMIT_LOWER] = 1;
			joint->params[SLIDER_JOINT_LINEAR_MOTION_DAMPING] = 0;
			joint->params[SLIDER_JOINT_ANGULAR_LIMIT_UPPER] = 0;
			joint->params[SLIDER_JOINT_ANGULAR_LIMIT_LOWER] = 0;
			joint->params[SLIDER_JOINT_ANGULAR_MOTION_DAMPING] = 0;
			break;
		case JOINT_CONE_TWIST:
			joint->params[CONE_TWIST_JOINT_SWING_SPAN] = Math_PI * 0.25;
			joint->params[CONE_TWIST_JOINT_TWIST_SPAN] = Math_PI * 2;
			joint->params[CONE_TWIST_JOINT_BIAS] = 0.3;
			joint->params[CONE_TWIST_JOINT_SOFTNESS] = 0.8;
			joint->params[CONE_TWIST_JOINT_RELAXATION] = 1;
			break;
		case JOINT_6DOF:
			for (int i = 0; i < 3; i++) {
				joint->g6dof_flags[i][G6DOF_JOINT_FLAG_ENABLE_LINEAR_LIMIT] = true;
				joint->g6dof_flags[i][G6DOF_JOINT_FLAG_ENABLE_ANGULAR_LIMIT] = true;
			}
			break;
		default:
			break;
	}

	a->joints.insert(joint);
	if (b) {
		b->joints.insert(joint);
	}

	RID rid = joint_owner.make_rid(joint);
	_joint_create(joint);
	return rid;
}

void Box3DPhysicsServer::_joint_free(Box3DJoint *p_joint) {
	_joint_destroy(p_joint);
	if (p_joint->body_a) {
		p_joint->body_a->joints.erase(p_joint);
	}
	if (p_joint->body_b) {
		p_joint->body_b->joints.erase(p_joint);
	}
}

/* JOINT API */

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
	if (b3Joint_IsValid(joint->joint_id)) {
		b3Joint_SetCollideConnected(joint->joint_id, !p_disable);
	}
}

bool Box3DPhysicsServer::joint_is_disabled_collisions_between_bodies(RID p_joint) const {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL_V(joint, true);
	return joint->collisions_disabled;
}

RID Box3DPhysicsServer::joint_create_pin(RID p_body_A, const Vector3 &p_local_A, RID p_body_B, const Vector3 &p_local_B) {
	return _joint_make(JOINT_PIN, p_body_A, Transform(Basis(), p_local_A), p_body_B, Transform(Basis(), p_local_B));
}

void Box3DPhysicsServer::pin_joint_set_param(RID p_joint, PinJointParam p_param, float p_value) {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL(joint);
	ERR_FAIL_COND(joint->type != JOINT_PIN);
	ERR_FAIL_INDEX((int)p_param, (int)PIN_JOINT_IMPULSE_CLAMP + 1);
	joint->params[p_param] = p_value;
}

float Box3DPhysicsServer::pin_joint_get_param(RID p_joint, PinJointParam p_param) const {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL_V(joint, 0);
	ERR_FAIL_COND_V(joint->type != JOINT_PIN, 0);
	ERR_FAIL_INDEX_V((int)p_param, (int)PIN_JOINT_IMPULSE_CLAMP + 1, 0);
	return joint->params[p_param];
}

void Box3DPhysicsServer::pin_joint_set_local_a(RID p_joint, const Vector3 &p_A) {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL(joint);
	ERR_FAIL_COND(joint->type != JOINT_PIN);
	joint->frame_a.origin = p_A;
	if (b3Joint_IsValid(joint->joint_id)) {
		b3Joint_SetLocalFrameA(joint->joint_id, b3_joint_frame(joint->body_a, joint->frame_a));
		b3Joint_WakeBodies(joint->joint_id);
	}
}

Vector3 Box3DPhysicsServer::pin_joint_get_local_a(RID p_joint) const {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL_V(joint, Vector3());
	ERR_FAIL_COND_V(joint->type != JOINT_PIN, Vector3());
	return joint->frame_a.origin;
}

void Box3DPhysicsServer::pin_joint_set_local_b(RID p_joint, const Vector3 &p_B) {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL(joint);
	ERR_FAIL_COND(joint->type != JOINT_PIN);
	joint->frame_b.origin = p_B;
	if (b3Joint_IsValid(joint->joint_id)) {
		b3Joint_SetLocalFrameB(joint->joint_id, b3_joint_frame(joint->body_b, joint->frame_b));
		b3Joint_WakeBodies(joint->joint_id);
	}
}

Vector3 Box3DPhysicsServer::pin_joint_get_local_b(RID p_joint) const {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL_V(joint, Vector3());
	ERR_FAIL_COND_V(joint->type != JOINT_PIN, Vector3());
	return joint->frame_b.origin;
}

RID Box3DPhysicsServer::joint_create_hinge(RID p_body_A, const Transform &p_hinge_A, RID p_body_B, const Transform &p_hinge_B) {
	return _joint_make(JOINT_HINGE, p_body_A, p_hinge_A, p_body_B, p_hinge_B);
}

RID Box3DPhysicsServer::joint_create_hinge_simple(RID p_body_A, const Vector3 &p_pivot_A, const Vector3 &p_axis_A, RID p_body_B, const Vector3 &p_pivot_B, const Vector3 &p_axis_B) {
	// Only the axes are given, so any zero angle is as good as another.
	const Vector3 axis_a = p_axis_A.normalized();
	const Vector3 axis_b = p_axis_B.normalized();
	ERR_FAIL_COND_V(axis_a == Vector3() || axis_b == Vector3(), RID());

	const Vector3 up = Math::abs(axis_a.y) < 0.9 ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
	const Vector3 x = up.cross(axis_a).normalized();
	Basis basis_a;
	basis_a.set_axis(0, x);
	basis_a.set_axis(1, axis_a.cross(x));
	basis_a.set_axis(2, axis_a);

	// The shortest rotation from one axis to the other keeps the two frames aligned.
	Basis arc;
	if (axis_a.dot(axis_b) < -0.9999) {
		arc = Basis(x, Math_PI);
	} else {
		arc = Basis(Quat(axis_a, axis_b));
	}
	return _joint_make(JOINT_HINGE, p_body_A, Transform(basis_a, p_pivot_A), p_body_B, Transform(arc * basis_a, p_pivot_B));
}

void Box3DPhysicsServer::hinge_joint_set_param(RID p_joint, HingeJointParam p_param, float p_value) {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL(joint);
	ERR_FAIL_COND(joint->type != JOINT_HINGE);
	ERR_FAIL_INDEX((int)p_param, (int)HINGE_JOINT_MAX);
	joint->params[p_param] = p_value;
	_joint_update(joint);
}

float Box3DPhysicsServer::hinge_joint_get_param(RID p_joint, HingeJointParam p_param) const {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL_V(joint, 0);
	ERR_FAIL_COND_V(joint->type != JOINT_HINGE, 0);
	ERR_FAIL_INDEX_V((int)p_param, (int)HINGE_JOINT_MAX, 0);
	return joint->params[p_param];
}

void Box3DPhysicsServer::hinge_joint_set_flag(RID p_joint, HingeJointFlag p_flag, bool p_value) {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL(joint);
	ERR_FAIL_COND(joint->type != JOINT_HINGE);
	ERR_FAIL_INDEX((int)p_flag, (int)HINGE_JOINT_FLAG_MAX);
	joint->hinge_flags[p_flag] = p_value;
	_joint_update(joint);
}

bool Box3DPhysicsServer::hinge_joint_get_flag(RID p_joint, HingeJointFlag p_flag) const {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL_V(joint, false);
	ERR_FAIL_COND_V(joint->type != JOINT_HINGE, false);
	ERR_FAIL_INDEX_V((int)p_flag, (int)HINGE_JOINT_FLAG_MAX, false);
	return joint->hinge_flags[p_flag];
}

RID Box3DPhysicsServer::joint_create_slider(RID p_body_A, const Transform &p_local_frame_A, RID p_body_B, const Transform &p_local_frame_B) {
	return _joint_make(JOINT_SLIDER, p_body_A, p_local_frame_A, p_body_B, p_local_frame_B);
}

void Box3DPhysicsServer::slider_joint_set_param(RID p_joint, SliderJointParam p_param, float p_value) {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL(joint);
	ERR_FAIL_COND(joint->type != JOINT_SLIDER);
	ERR_FAIL_INDEX((int)p_param, (int)SLIDER_JOINT_MAX);
	joint->params[p_param] = p_value;
	_joint_update(joint);
}

float Box3DPhysicsServer::slider_joint_get_param(RID p_joint, SliderJointParam p_param) const {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL_V(joint, 0);
	ERR_FAIL_COND_V(joint->type != JOINT_SLIDER, 0);
	ERR_FAIL_INDEX_V((int)p_param, (int)SLIDER_JOINT_MAX, 0);
	return joint->params[p_param];
}

RID Box3DPhysicsServer::joint_create_cone_twist(RID p_body_A, const Transform &p_local_frame_A, RID p_body_B, const Transform &p_local_frame_B) {
	return _joint_make(JOINT_CONE_TWIST, p_body_A, p_local_frame_A, p_body_B, p_local_frame_B);
}

void Box3DPhysicsServer::cone_twist_joint_set_param(RID p_joint, ConeTwistJointParam p_param, float p_value) {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL(joint);
	ERR_FAIL_COND(joint->type != JOINT_CONE_TWIST);
	ERR_FAIL_INDEX((int)p_param, (int)CONE_TWIST_MAX);
	joint->params[p_param] = p_value;
	_joint_update(joint);
}

float Box3DPhysicsServer::cone_twist_joint_get_param(RID p_joint, ConeTwistJointParam p_param) const {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL_V(joint, 0);
	ERR_FAIL_COND_V(joint->type != JOINT_CONE_TWIST, 0);
	ERR_FAIL_INDEX_V((int)p_param, (int)CONE_TWIST_MAX, 0);
	return joint->params[p_param];
}

RID Box3DPhysicsServer::joint_create_generic_6dof(RID p_body_A, const Transform &p_local_frame_A, RID p_body_B, const Transform &p_local_frame_B) {
	return _joint_make(JOINT_6DOF, p_body_A, p_local_frame_A, p_body_B, p_local_frame_B);
}

void Box3DPhysicsServer::generic_6dof_joint_set_param(RID p_joint, Vector3::Axis p_axis, G6DOFJointAxisParam p_param, float p_value) {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL(joint);
	ERR_FAIL_COND(joint->type != JOINT_6DOF);
	ERR_FAIL_INDEX((int)p_axis, 3);
	ERR_FAIL_INDEX((int)p_param, (int)G6DOF_JOINT_MAX);
	joint->g6dof_params[p_axis][p_param] = p_value;
	_joint_update(joint);
}

float Box3DPhysicsServer::generic_6dof_joint_get_param(RID p_joint, Vector3::Axis p_axis, G6DOFJointAxisParam p_param) {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL_V(joint, 0);
	ERR_FAIL_COND_V(joint->type != JOINT_6DOF, 0);
	ERR_FAIL_INDEX_V((int)p_axis, 3, 0);
	ERR_FAIL_INDEX_V((int)p_param, (int)G6DOF_JOINT_MAX, 0);
	return joint->g6dof_params[p_axis][p_param];
}

void Box3DPhysicsServer::generic_6dof_joint_set_flag(RID p_joint, Vector3::Axis p_axis, G6DOFJointAxisFlag p_flag, bool p_enable) {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL(joint);
	ERR_FAIL_COND(joint->type != JOINT_6DOF);
	ERR_FAIL_INDEX((int)p_axis, 3);
	ERR_FAIL_INDEX((int)p_flag, (int)G6DOF_JOINT_FLAG_MAX);
	joint->g6dof_flags[p_axis][p_flag] = p_enable;
	_joint_update(joint);
}

bool Box3DPhysicsServer::generic_6dof_joint_get_flag(RID p_joint, Vector3::Axis p_axis, G6DOFJointAxisFlag p_flag) {
	Box3DJoint *joint = joint_owner.getornull(p_joint);
	ERR_FAIL_NULL_V(joint, false);
	ERR_FAIL_COND_V(joint->type != JOINT_6DOF, false);
	ERR_FAIL_INDEX_V((int)p_axis, 3, false);
	ERR_FAIL_INDEX_V((int)p_flag, (int)G6DOF_JOINT_FLAG_MAX, false);
	return joint->g6dof_flags[p_axis][p_flag];
}
