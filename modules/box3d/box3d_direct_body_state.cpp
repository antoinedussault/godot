#include "box3d_direct_body_state.h"

#include "box3d_physics_server.h"
#include "box3d_space.h"
#include "box3d_types_converter.h"
#include "core/error_macros.h"

#include "box3d/constants.h"

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

void Box3DPhysicsDirectBodyState::set_body(Box3DBody *p_body, real_t p_step) {
	body = p_body;
	step_delta = p_step;
	contacts_gathered = false;
	contacts.clear();
}

void Box3DPhysicsDirectBodyState::gather_contacts() const {
	if (contacts_gathered) {
		return;
	}
	contacts_gathered = true;
	contacts.clear();
	if (!body || !body->in_world() || body->max_contacts_reported <= 0) {
		return;
	}

	Vector<b3ContactData> data;
	data.resize(b3Body_GetContactCapacity(body->body_id));
	if (data.empty()) {
		return;
	}
	const int count = b3Body_GetContactData(body->body_id, data.ptrw(), data.size());

	// Parallel to `contacts`: the gap of each, to keep the deepest when there are
	// more than the body asked for.
	Vector<float> separations;

	for (int i = 0; i < count; i++) {
		int shape_a = 0;
		int shape_b = 0;
		Box3DBody *a = b3_body_from_shape(data[i].shapeIdA, &shape_a);
		Box3DBody *b = b3_body_from_shape(data[i].shapeIdB, &shape_b);
		if (!a || !b) {
			continue;
		}
		const bool self_is_a = a == body;
		Box3DBody *other = self_is_a ? b : a;
		const Vector3 center_a = b3_pos_to_godot(b3Body_GetWorldCenter(a->body_id));

		for (int m = 0; m < data[i].manifoldCount; m++) {
			const b3Manifold &manifold = data[i].manifolds[m];
			for (int p = 0; p < manifold.pointCount; p++) {
				const b3ManifoldPoint &mp = manifold.points[p];
				// Box3D also keeps speculative points, which are not touching yet.
				if (mp.separation > 2.0f * B3_LINEAR_SLOP && mp.totalNormalImpulse <= 0.0f) {
					continue;
				}

				int slot = contacts.size();
				if (slot >= body->max_contacts_reported) {
					slot = 0;
					for (int k = 1; k < separations.size(); k++) {
						if (separations[k] > separations[slot]) {
							slot = k;
						}
					}
					if (separations[slot] <= mp.separation) {
						continue;
					}
				} else {
					contacts.push_back(ContactData());
					separations.push_back(0);
				}

				// What the other backends report as "local" is the point relative to the
				// collider's origin, in global axes.
				const Vector3 point = center_a + b3_to_godot(mp.anchorA);
				const Vector3 normal = b3_to_godot(manifold.normal); // A to B
				const Vector3 to_self = self_is_a ? -normal : normal;

				ContactData &c = contacts.write[slot];
				separations.write[slot] = mp.separation;
				c.collider_position = point - to_self * (mp.separation * 0.5);
				c.local_position = c.collider_position - other->transform.origin;
				c.local_normal = to_self;
				c.impulse = mp.totalNormalImpulse;
				c.local_shape = self_is_a ? shape_a : shape_b;
				c.collider_shape = self_is_a ? shape_b : shape_a;
				c.collider = other->self;
				c.collider_id = other->instance_id;
				c.collider_velocity_at_position = other->get_velocity_at(point - other->transform.origin);
			}
		}
	}
}

Vector3 Box3DPhysicsDirectBodyState::get_total_gravity() const {
	ERR_FAIL_NULL_V(body, Vector3());
	return body->total_gravity;
}

float Box3DPhysicsDirectBodyState::get_total_angular_damp() const {
	ERR_FAIL_NULL_V(body, 0.0);
	return body->total_angular_damp;
}

float Box3DPhysicsDirectBodyState::get_total_linear_damp() const {
	ERR_FAIL_NULL_V(body, 0.0);
	return body->total_linear_damp;
}

Vector3 Box3DPhysicsDirectBodyState::get_center_of_mass() const {
	ERR_FAIL_NULL_V(body, Vector3());
	return body->get_center_of_mass();
}

static Basis b3_basis_to_godot(const b3Matrix3 &p_m) {
	Basis b;
	b.set_axis(0, b3_to_godot(p_m.cx));
	b.set_axis(1, b3_to_godot(p_m.cy));
	b.set_axis(2, b3_to_godot(p_m.cz));
	return b;
}

// Splits the inverse inertia into principal axes (body-local) and moments.
static void b3_principal_inertia(const Box3DBody *p_body, Basis &r_axes, Vector3 &r_inverse) {
	r_axes = Basis();
	r_inverse = Vector3();
	if (!p_body->in_world() || !p_body->is_dynamic()) {
		return;
	}
	Basis tensor = b3_basis_to_godot(b3Body_GetLocalRotationalInertia(p_body->body_id));
	r_axes = tensor.diagonalize().transposed();
	for (int i = 0; i < 3; i++) {
		const real_t m = tensor.elements[i][i];
		r_inverse[i] = m > 0 ? 1.0 / m : 0.0;
	}
}

Basis Box3DPhysicsDirectBodyState::get_principal_inertia_axes() const {
	ERR_FAIL_NULL_V(body, Basis());
	Basis axes;
	Vector3 inverse;
	b3_principal_inertia(body, axes, inverse);
	return body->transform.basis.orthonormalized() * axes;
}

float Box3DPhysicsDirectBodyState::get_inverse_mass() const {
	ERR_FAIL_NULL_V(body, 0.0);
	return body->is_dynamic() && body->mass > 0 ? 1.0 / body->mass : 0.0;
}

Vector3 Box3DPhysicsDirectBodyState::get_inverse_inertia() const {
	ERR_FAIL_NULL_V(body, Vector3());
	Basis axes;
	Vector3 inverse;
	b3_principal_inertia(body, axes, inverse);
	return body->mode == PhysicsServer::BODY_MODE_RIGID ? inverse : Vector3();
}

Basis Box3DPhysicsDirectBodyState::get_inverse_inertia_tensor() const {
	ERR_FAIL_NULL_V(body, Basis());
	if (!body->in_world() || body->mode != PhysicsServer::BODY_MODE_RIGID) {
		return Basis(0, 0, 0, 0, 0, 0, 0, 0, 0);
	}
	return b3_basis_to_godot(b3Body_GetWorldInverseRotationalInertia(body->body_id));
}

// Everything below goes through the server, so a change made in
// _integrate_forces() reaches the solver and not just the bookkeeping.

void Box3DPhysicsDirectBodyState::set_linear_velocity(const Vector3 &p_velocity) {
	ERR_FAIL_NULL(body);
	Box3DPhysicsServer::get_box3d_singleton()->body_set_state(body->self, PhysicsServer::BODY_STATE_LINEAR_VELOCITY, p_velocity);
}

Vector3 Box3DPhysicsDirectBodyState::get_linear_velocity() const {
	ERR_FAIL_NULL_V(body, Vector3());
	return body->linear_velocity;
}

void Box3DPhysicsDirectBodyState::set_angular_velocity(const Vector3 &p_velocity) {
	ERR_FAIL_NULL(body);
	Box3DPhysicsServer::get_box3d_singleton()->body_set_state(body->self, PhysicsServer::BODY_STATE_ANGULAR_VELOCITY, p_velocity);
}

Vector3 Box3DPhysicsDirectBodyState::get_angular_velocity() const {
	ERR_FAIL_NULL_V(body, Vector3());
	return body->angular_velocity;
}

void Box3DPhysicsDirectBodyState::set_transform(const Transform &p_transform) {
	ERR_FAIL_NULL(body);
	Box3DPhysicsServer::get_box3d_singleton()->body_set_state(body->self, PhysicsServer::BODY_STATE_TRANSFORM, p_transform);
}

Transform Box3DPhysicsDirectBodyState::get_transform() const {
	ERR_FAIL_NULL_V(body, Transform());
	return body->transform;
}

Vector3 Box3DPhysicsDirectBodyState::get_velocity_at_local_position(const Vector3 &p_position) const {
	ERR_FAIL_NULL_V(body, Vector3());
	return body->get_velocity_at(p_position);
}

void Box3DPhysicsDirectBodyState::add_central_force(const Vector3 &p_force) {
	ERR_FAIL_NULL(body);
	Box3DPhysicsServer::get_box3d_singleton()->body_add_central_force(body->self, p_force);
}

void Box3DPhysicsDirectBodyState::add_force(const Vector3 &p_force, const Vector3 &p_pos) {
	ERR_FAIL_NULL(body);
	Box3DPhysicsServer::get_box3d_singleton()->body_add_force(body->self, p_force, p_pos);
}

void Box3DPhysicsDirectBodyState::add_torque(const Vector3 &p_torque) {
	ERR_FAIL_NULL(body);
	Box3DPhysicsServer::get_box3d_singleton()->body_add_torque(body->self, p_torque);
}

void Box3DPhysicsDirectBodyState::apply_central_impulse(const Vector3 &p_j) {
	ERR_FAIL_NULL(body);
	Box3DPhysicsServer::get_box3d_singleton()->body_apply_central_impulse(body->self, p_j);
}

void Box3DPhysicsDirectBodyState::apply_impulse(const Vector3 &p_pos, const Vector3 &p_j) {
	ERR_FAIL_NULL(body);
	Box3DPhysicsServer::get_box3d_singleton()->body_apply_impulse(body->self, p_pos, p_j);
}

void Box3DPhysicsDirectBodyState::apply_torque_impulse(const Vector3 &p_j) {
	ERR_FAIL_NULL(body);
	Box3DPhysicsServer::get_box3d_singleton()->body_apply_torque_impulse(body->self, p_j);
}

void Box3DPhysicsDirectBodyState::set_sleep_state(bool p_enable) {
	ERR_FAIL_NULL(body);
	Box3DPhysicsServer::get_box3d_singleton()->body_set_state(body->self, PhysicsServer::BODY_STATE_SLEEPING, p_enable);
}

bool Box3DPhysicsDirectBodyState::is_sleeping() const {
	ERR_FAIL_NULL_V(body, false);
	return body->sleeping;
}

int Box3DPhysicsDirectBodyState::get_contact_count() const {
	gather_contacts();
	return contacts.size();
}

Vector3 Box3DPhysicsDirectBodyState::get_contact_local_position(int p_contact_idx) const {
	gather_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), Vector3());
	return contacts[p_contact_idx].local_position;
}

Vector3 Box3DPhysicsDirectBodyState::get_contact_local_normal(int p_contact_idx) const {
	gather_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), Vector3());
	return contacts[p_contact_idx].local_normal;
}

float Box3DPhysicsDirectBodyState::get_contact_impulse(int p_contact_idx) const {
	gather_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), 0.0);
	return contacts[p_contact_idx].impulse;
}

int Box3DPhysicsDirectBodyState::get_contact_local_shape(int p_contact_idx) const {
	gather_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), 0);
	return contacts[p_contact_idx].local_shape;
}

RID Box3DPhysicsDirectBodyState::get_contact_collider(int p_contact_idx) const {
	gather_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), RID());
	return contacts[p_contact_idx].collider;
}

Vector3 Box3DPhysicsDirectBodyState::get_contact_collider_position(int p_contact_idx) const {
	gather_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), Vector3());
	return contacts[p_contact_idx].collider_position;
}

ObjectID Box3DPhysicsDirectBodyState::get_contact_collider_id(int p_contact_idx) const {
	gather_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), 0);
	return contacts[p_contact_idx].collider_id;
}

int Box3DPhysicsDirectBodyState::get_contact_collider_shape(int p_contact_idx) const {
	gather_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, contacts.size(), 0);
	return contacts[p_contact_idx].collider_shape;
}

Vector3 Box3DPhysicsDirectBodyState::get_contact_collider_velocity_at_position(int p_contact_idx) const {
	gather_contacts();
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
