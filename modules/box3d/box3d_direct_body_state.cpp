#include "box3d_direct_body_state.h"

#include "box3d_physics_server.h"
#include "box3d_space.h"
#include "core/error_macros.h"

Box3DPhysicsDirectBodyState *Box3DPhysicsDirectBodyState::singleton = nullptr;

void Box3DPhysicsDirectBodyState::init_singleton() {
	if (!singleton) {
		singleton = memnew(Box3DPhysicsDirectBodyState);
	}
}

void Box3DPhysicsDirectBodyState::free_singleton() {
	if (singleton) {
		memdelete(singleton);
		singleton = nullptr;
	}
}

// The accessors below read straight from the Box3DBody bookkeeping, so they are
// already correct for anything the game *sets*. What they cannot report yet is
// anything the solver would have produced: contacts, and gravity/damp accumulated
// from overlapping Areas.

Vector3 Box3DPhysicsDirectBodyState::get_total_gravity() const {
	ERR_FAIL_NULL_V(body, Vector3());
	// Once areas are live this is the area-override accumulation, not the raw
	// space gravity.
	Box3DSpace *space = Box3DPhysicsServer::get_box3d_singleton()->get_space(body->space);
	if (space && space->default_area) {
		return space->default_area->gravity_vector * space->default_area->gravity * body->gravity_scale;
	}
	return Vector3();
}

float Box3DPhysicsDirectBodyState::get_total_angular_damp() const {
	ERR_FAIL_NULL_V(body, 0.0);
	return body->angular_damp >= 0 ? body->angular_damp : 0.0;
}

float Box3DPhysicsDirectBodyState::get_total_linear_damp() const {
	ERR_FAIL_NULL_V(body, 0.0);
	return body->linear_damp >= 0 ? body->linear_damp : 0.0;
}

Vector3 Box3DPhysicsDirectBodyState::get_center_of_mass() const {
	ERR_FAIL_NULL_V(body, Vector3());
	return Vector3();
}

Basis Box3DPhysicsDirectBodyState::get_principal_inertia_axes() const {
	return Basis();
}

float Box3DPhysicsDirectBodyState::get_inverse_mass() const {
	ERR_FAIL_NULL_V(body, 0.0);
	return body->mass > 0 ? 1.0 / body->mass : 0.0;
}

Vector3 Box3DPhysicsDirectBodyState::get_inverse_inertia() const {
	ERR_FAIL_NULL_V(body, Vector3());
	return Vector3();
}

Basis Box3DPhysicsDirectBodyState::get_inverse_inertia_tensor() const {
	return Basis();
}

void Box3DPhysicsDirectBodyState::set_linear_velocity(const Vector3 &p_velocity) {
	ERR_FAIL_NULL(body);
	body->linear_velocity = p_velocity;
}

Vector3 Box3DPhysicsDirectBodyState::get_linear_velocity() const {
	ERR_FAIL_NULL_V(body, Vector3());
	return body->linear_velocity;
}

void Box3DPhysicsDirectBodyState::set_angular_velocity(const Vector3 &p_velocity) {
	ERR_FAIL_NULL(body);
	body->angular_velocity = p_velocity;
}

Vector3 Box3DPhysicsDirectBodyState::get_angular_velocity() const {
	ERR_FAIL_NULL_V(body, Vector3());
	return body->angular_velocity;
}

void Box3DPhysicsDirectBodyState::set_transform(const Transform &p_transform) {
	ERR_FAIL_NULL(body);
	body->transform = p_transform;
}

Transform Box3DPhysicsDirectBodyState::get_transform() const {
	ERR_FAIL_NULL_V(body, Transform());
	return body->transform;
}

Vector3 Box3DPhysicsDirectBodyState::get_velocity_at_local_position(const Vector3 &p_position) const {
	ERR_FAIL_NULL_V(body, Vector3());
	return body->linear_velocity + body->angular_velocity.cross(body->transform.basis.xform(p_position));
}

void Box3DPhysicsDirectBodyState::add_central_force(const Vector3 &p_force) {
	ERR_FAIL_NULL(body);
	body->applied_force += p_force;
}

void Box3DPhysicsDirectBodyState::add_force(const Vector3 &p_force, const Vector3 &p_pos) {
	ERR_FAIL_NULL(body);
	body->applied_force += p_force;
	body->applied_torque += p_pos.cross(p_force);
}

void Box3DPhysicsDirectBodyState::add_torque(const Vector3 &p_torque) {
	ERR_FAIL_NULL(body);
	body->applied_torque += p_torque;
}

void Box3DPhysicsDirectBodyState::apply_central_impulse(const Vector3 &p_j) {
	ERR_FAIL_NULL(body);
	body->linear_velocity += p_j * (body->mass > 0 ? 1.0 / body->mass : 0.0);
}

void Box3DPhysicsDirectBodyState::apply_impulse(const Vector3 &p_pos, const Vector3 &p_j) {
	ERR_FAIL_NULL(body);
	// Without an inertia tensor the angular half cannot be applied correctly, so
	// only the linear part is honoured until the solver is wired up.
	body->linear_velocity += p_j * (body->mass > 0 ? 1.0 / body->mass : 0.0);
}

void Box3DPhysicsDirectBodyState::apply_torque_impulse(const Vector3 &p_j) {
	ERR_FAIL_NULL(body);
	WARN_PRINT_ONCE("Box3D: apply_torque_impulse() needs an inertia tensor; ignored for now.");
}

void Box3DPhysicsDirectBodyState::set_sleep_state(bool p_enable) {
	ERR_FAIL_NULL(body);
	body->sleeping = p_enable;
}

bool Box3DPhysicsDirectBodyState::is_sleeping() const {
	ERR_FAIL_NULL_V(body, false);
	return body->sleeping;
}

int Box3DPhysicsDirectBodyState::get_contact_count() const {
	return contacts.size();
}

Vector3 Box3DPhysicsDirectBodyState::get_contact_local_position(int p_contact_idx) const {
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), Vector3());
	return contacts[p_contact_idx].local_position;
}

Vector3 Box3DPhysicsDirectBodyState::get_contact_local_normal(int p_contact_idx) const {
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), Vector3());
	return contacts[p_contact_idx].local_normal;
}

float Box3DPhysicsDirectBodyState::get_contact_impulse(int p_contact_idx) const {
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), 0.0);
	return contacts[p_contact_idx].impulse;
}

int Box3DPhysicsDirectBodyState::get_contact_local_shape(int p_contact_idx) const {
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), 0);
	return contacts[p_contact_idx].local_shape;
}

RID Box3DPhysicsDirectBodyState::get_contact_collider(int p_contact_idx) const {
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), RID());
	return contacts[p_contact_idx].collider;
}

Vector3 Box3DPhysicsDirectBodyState::get_contact_collider_position(int p_contact_idx) const {
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), Vector3());
	return contacts[p_contact_idx].collider_position;
}

ObjectID Box3DPhysicsDirectBodyState::get_contact_collider_id(int p_contact_idx) const {
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), 0);
	return contacts[p_contact_idx].collider_id;
}

int Box3DPhysicsDirectBodyState::get_contact_collider_shape(int p_contact_idx) const {
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), 0);
	return contacts[p_contact_idx].collider_shape;
}

Vector3 Box3DPhysicsDirectBodyState::get_contact_collider_velocity_at_position(int p_contact_idx) const {
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), Vector3());
	return contacts[p_contact_idx].collider_velocity_at_position;
}

real_t Box3DPhysicsDirectBodyState::get_step() const {
	return step_delta;
}

PhysicsDirectSpaceState *Box3DPhysicsDirectBodyState::get_space_state() {
	ERR_FAIL_NULL_V(body, nullptr);
	Box3DSpace *space = Box3DPhysicsServer::get_box3d_singleton()->get_space(body->space);
	ERR_FAIL_NULL_V(space, nullptr);
	return space->direct_state;
}
