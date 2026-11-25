#ifndef BOX3D_DIRECT_BODY_STATE_H
#define BOX3D_DIRECT_BODY_STATE_H

#include "box3d_objects.h"
#include "servers/physics_server.h"

// The object handed to _integrate_forces() and to RigidBody's
// _direct_state_changed(). CellItemBody uses the latter to detect a
// CellPlayerBody shoving it, so getting this class right matters earlier in this
// project than the joint or soft-body APIs do.
//
// It is a thin view over one Box3DBody plus the contact list gathered during the
// step, and is reused across bodies rather than allocated per body — same as the
// other backends.
class Box3DPhysicsDirectBodyState : public PhysicsDirectBodyState {
	GDCLASS(Box3DPhysicsDirectBodyState, PhysicsDirectBodyState);

public:
	struct ContactData {
		Vector3 local_position;
		Vector3 local_normal;
		real_t impulse = 0.0;
		int local_shape = 0;
		RID collider;
		Vector3 collider_position;
		ObjectID collider_id = 0;
		int collider_shape = 0;
		Vector3 collider_velocity_at_position;
	};

	static Box3DPhysicsDirectBodyState *singleton;

	Box3DBody *body = nullptr;
	real_t step_delta = 0.0;

	// Gathered the first time they are asked for, since most bodies never are.
	mutable Vector<ContactData> contacts;
	mutable bool contacts_gathered = false;

	void set_body(Box3DBody *p_body, real_t p_step);
	void gather_contacts() const;

	static void init_singleton();
	static void free_singleton();

	virtual Vector3 get_total_gravity() const;
	virtual float get_total_angular_damp() const;
	virtual float get_total_linear_damp() const;

	virtual Vector3 get_center_of_mass() const;
	virtual Basis get_principal_inertia_axes() const;
	virtual float get_inverse_mass() const;
	virtual Vector3 get_inverse_inertia() const;
	virtual Basis get_inverse_inertia_tensor() const;

	virtual void set_linear_velocity(const Vector3 &p_velocity);
	virtual Vector3 get_linear_velocity() const;

	virtual void set_angular_velocity(const Vector3 &p_velocity);
	virtual Vector3 get_angular_velocity() const;

	virtual void set_transform(const Transform &p_transform);
	virtual Transform get_transform() const;

	virtual Vector3 get_velocity_at_local_position(const Vector3 &p_position) const;

	virtual void add_central_force(const Vector3 &p_force);
	virtual void add_force(const Vector3 &p_force, const Vector3 &p_pos);
	virtual void add_torque(const Vector3 &p_torque);
	virtual void apply_central_impulse(const Vector3 &p_j);
	virtual void apply_impulse(const Vector3 &p_pos, const Vector3 &p_j);
	virtual void apply_torque_impulse(const Vector3 &p_j);

	virtual void set_sleep_state(bool p_enable);
	virtual bool is_sleeping() const;

	virtual int get_contact_count() const;

	virtual Vector3 get_contact_local_position(int p_contact_idx) const;
	virtual Vector3 get_contact_local_normal(int p_contact_idx) const;
	virtual float get_contact_impulse(int p_contact_idx) const;
	virtual int get_contact_local_shape(int p_contact_idx) const;

	virtual RID get_contact_collider(int p_contact_idx) const;
	virtual Vector3 get_contact_collider_position(int p_contact_idx) const;
	virtual ObjectID get_contact_collider_id(int p_contact_idx) const;
	virtual int get_contact_collider_shape(int p_contact_idx) const;
	virtual Vector3 get_contact_collider_velocity_at_position(int p_contact_idx) const;

	virtual real_t get_step() const;

	virtual PhysicsDirectSpaceState *get_space_state();
};

#endif // BOX3D_DIRECT_BODY_STATE_H
