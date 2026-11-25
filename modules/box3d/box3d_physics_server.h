#ifndef BOX3D_PHYSICS_SERVER_H
#define BOX3D_PHYSICS_SERVER_H

#include "box3d_direct_body_state.h"
#include "box3d_objects.h"
#include "box3d_space.h"
#include "core/rid.h"
#include "servers/physics_server.h"

// PhysicsServer backend built on Box3D (https://github.com/erincatto/box3d).
//
// Everything but soft bodies is implemented; see README.md for how, and for the
// places where Box3D itself behaves differently from the other backends.
//
// It registers alongside "Bullet" and "GodotPhysics" through PhysicsServerManager
// and is NOT the default, so enabling this module cannot change how an existing
// project behaves. Select it per project with
// `physics/3d/physics_engine = "Box3D"`.
class Box3DPhysicsServer : public PhysicsServer {
	GDCLASS(Box3DPhysicsServer, PhysicsServer);

	friend class Box3DPhysicsDirectSpaceState;

	bool active = true;
	int collision_iterations = 8;

	Vector<Box3DSpace *> active_spaces;

	mutable RID_Owner<Box3DSpace> space_owner;
	mutable RID_Owner<Box3DShape> shape_owner;
	mutable RID_Owner<Box3DArea> area_owner;
	mutable RID_Owner<Box3DBody> body_owner;
	mutable RID_Owner<Box3DJoint> joint_owner;

	// True only during flush_queries().
	bool doing_sync = false;

protected:
	static void _bind_methods();

public:
	Box3DPhysicsServer();
	~Box3DPhysicsServer();

	static Box3DPhysicsServer *get_box3d_singleton() {
		return static_cast<Box3DPhysicsServer *>(get_singleton());
	}

	Box3DSpace *get_space(RID p_rid) const { return space_owner.getornull(p_rid); }
	Box3DBody *get_body(RID p_rid) const { return body_owner.getornull(p_rid); }
	Box3DArea *get_area(RID p_rid) const { return area_owner.getornull(p_rid); }
	Box3DShape *get_shape(RID p_rid) const { return shape_owner.getornull(p_rid); }
	bool owns_area(RID p_rid) const { return area_owner.owns(p_rid); }

	// Resolves an RID that may be either a body or an area, the way most of the
	// CollisionObject-shaped APIs need.
	Box3DCollisionObject *get_collision_object(RID p_rid) const;

private:
	// Solver-side body lifetime. The Godot bookkeeping on Box3DBody stays
	// authoritative; these create and tear down the matching b3 objects when a
	// body joins or leaves a space.
	void _body_enter_world(Box3DBody *p_body, Box3DSpace *p_space);
	void _body_exit_world(Box3DBody *p_body);
	void _body_rebuild_shapes(Box3DBody *p_body);
	// Rebuilds a single shape entry; O(1) instead of O(N) per edit.
	void _body_rebuild_shape_at(Box3DBody *p_body, int p_index);
	// Recomputes offset/transform/mass after shape edits. No geometry rebuilt.
	void _body_finish_shape_edit(Box3DBody *p_body);
	void _body_apply_mass(Box3DBody *p_body);
	void _body_apply_locks(Box3DBody *p_body);
	// Friction, bounce and conveyor velocity.
	b3SurfaceMaterial _body_make_material(const Box3DBody *p_body) const;
	void _body_update_material(Box3DBody *p_body);
	// Makes the solver re-evaluate who this body collides with.
	void _body_refilter(Box3DBody *p_body);
	void _release_shape(RID p_owner, RID p_shape);

	// Pushes the body's collision layer/mask onto its live b3 shapes. Godot sets
	// these independently of the shape list, and often after the shapes exist.
	void _body_update_filters(Box3DBody *p_body);

	// Pushes the body's transform into the solver, including the height field
	// origin correction when one applies.
	void _body_sync_transform(Box3DBody *p_body);

	// Works out whether this body needs the height field origin offset, and what
	// it is. Also enforces the two cases the offset cannot express.
	void _body_update_shape_origin_offset(Box3DBody *p_body);

	// Areas mirror the body lifetime, but their shapes are sensors.
	void _area_enter_world(Box3DArea *p_area, Box3DSpace *p_space);
	void _area_exit_world(Box3DArea *p_area);
	void _area_rebuild_shapes(Box3DArea *p_area);
	void _area_rebuild_shape_at(Box3DArea *p_area, int p_index);
	void _area_drop_shape(Box3DArea *p_area, int p_index);
	void _area_sync_transform(Box3DArea *p_area);

	// Joints, see box3d_joints.cpp.
	RID _joint_make(JointType p_type, RID p_body_a, const Transform &p_frame_a, RID p_body_b, const Transform &p_frame_b);
	void _joint_create(Box3DJoint *p_joint);
	void _joint_destroy(Box3DJoint *p_joint);
	void _joint_update(Box3DJoint *p_joint);
	void _joint_free(Box3DJoint *p_joint);
	b3GenericAxis _joint_generic_axis(const Box3DJoint *p_joint, int p_row) const;
	void _body_destroy_joints(Box3DBody *p_body);
	void _body_create_joints(Box3DBody *p_body);
	void _body_break_joints(Box3DBody *p_body);

public:

	/* SHAPE API */
	virtual RID shape_create(ShapeType p_shape);
	virtual void shape_set_data(RID p_shape, const Variant &p_data);
	virtual ShapeType shape_get_type(RID p_shape) const;
	virtual Variant shape_get_data(RID p_shape) const;

	virtual void shape_set_margin(RID p_shape, real_t p_margin);
	virtual real_t shape_get_margin(RID p_shape) const;

	virtual void shape_set_custom_solver_bias(RID p_shape, real_t p_bias);
	virtual real_t shape_get_custom_solver_bias(RID p_shape) const;

	/* SPACE API */

	virtual RID space_create();
	virtual void space_set_active(RID p_space, bool p_active);
	virtual bool space_is_active(RID p_space) const;

	virtual void space_set_param(RID p_space, SpaceParameter p_param, real_t p_value);
	virtual real_t space_get_param(RID p_space, SpaceParameter p_param) const;

	virtual PhysicsDirectSpaceState *space_get_direct_state(RID p_space);

	virtual void space_set_debug_contacts(RID p_space, int p_max_contacts);
	virtual Vector<Vector3> space_get_contacts(RID p_space) const;
	virtual int space_get_contact_count(RID p_space) const;

	/* AREA API */

	virtual RID area_create();

	virtual void area_set_space(RID p_area, RID p_space);
	virtual RID area_get_space(RID p_area) const;

	virtual void area_set_space_override_mode(RID p_area, AreaSpaceOverrideMode p_mode);
	virtual AreaSpaceOverrideMode area_get_space_override_mode(RID p_area) const;

	virtual void area_add_shape(RID p_area, RID p_shape, const Transform &p_transform = Transform(), bool p_disabled = false);
	virtual void area_set_shape(RID p_area, int p_shape_idx, RID p_shape);
	virtual void area_set_shape_transform(RID p_area, int p_shape_idx, const Transform &p_transform);
	virtual int area_get_shape_count(RID p_area) const;
	virtual RID area_get_shape(RID p_area, int p_shape_idx) const;
	virtual Transform area_get_shape_transform(RID p_area, int p_shape_idx) const;
	virtual void area_remove_shape(RID p_area, int p_shape_idx);
	virtual void area_clear_shapes(RID p_area);
	virtual void area_set_shape_disabled(RID p_area, int p_shape_idx, bool p_disabled);
	virtual void area_attach_object_instance_id(RID p_area, ObjectID p_id);
	virtual ObjectID area_get_object_instance_id(RID p_area) const;

	virtual void area_set_param(RID p_area, AreaParameter p_param, const Variant &p_value);
	virtual Variant area_get_param(RID p_area, AreaParameter p_param) const;

	virtual void area_set_transform(RID p_area, const Transform &p_transform);
	virtual Transform area_get_transform(RID p_area) const;

	virtual void area_set_collision_mask(RID p_area, uint32_t p_mask);
	virtual void area_set_collision_layer(RID p_area, uint32_t p_layer);

	virtual void area_set_monitorable(RID p_area, bool p_monitorable);
	virtual void area_set_monitor_callback(RID p_area, Object *p_receiver, const StringName &p_method);
	virtual void area_set_area_monitor_callback(RID p_area, Object *p_receiver, const StringName &p_method);
	virtual void area_set_ray_pickable(RID p_area, bool p_enable);
	virtual bool area_is_ray_pickable(RID p_area) const;

	/* RIGID BODY API */

	virtual RID body_create(BodyMode p_mode = BODY_MODE_RIGID, bool p_init_sleeping = false);

	virtual void body_set_space(RID p_body, RID p_space);
	virtual RID body_get_space(RID p_body) const;

	virtual void body_set_mode(RID p_body, BodyMode p_mode);
	virtual BodyMode body_get_mode(RID p_body) const;

	virtual void body_add_shape(RID p_body, RID p_shape, const Transform &p_transform = Transform(), bool p_disabled = false);
	virtual void body_set_shape(RID p_body, int p_shape_idx, RID p_shape);
	virtual void body_set_shape_transform(RID p_body, int p_shape_idx, const Transform &p_transform);

	virtual int body_get_shape_count(RID p_body) const;
	virtual RID body_get_shape(RID p_body, int p_shape_idx) const;
	virtual Transform body_get_shape_transform(RID p_body, int p_shape_idx) const;

	virtual void body_set_shape_disabled(RID p_body, int p_shape_idx, bool p_disabled);

	virtual void body_remove_shape(RID p_body, int p_shape_idx);
	virtual void body_clear_shapes(RID p_body);

	virtual void body_attach_object_instance_id(RID p_body, uint32_t p_id);
	virtual uint32_t body_get_object_instance_id(RID p_body) const;

	virtual void body_set_enable_continuous_collision_detection(RID p_body, bool p_enable);
	virtual bool body_is_continuous_collision_detection_enabled(RID p_body) const;

	virtual void body_set_collision_layer(RID p_body, uint32_t p_layer);
	virtual uint32_t body_get_collision_layer(RID p_body) const;

	virtual void body_set_collision_mask(RID p_body, uint32_t p_mask);
	virtual uint32_t body_get_collision_mask(RID p_body) const;

	virtual void body_set_user_flags(RID p_body, uint32_t p_flags);
	virtual uint32_t body_get_user_flags(RID p_body) const;

	virtual void body_set_param(RID p_body, BodyParameter p_param, float p_value);
	virtual float body_get_param(RID p_body, BodyParameter p_param) const;

	virtual void body_set_kinematic_safe_margin(RID p_body, real_t p_margin);
	virtual real_t body_get_kinematic_safe_margin(RID p_body) const;

	virtual void body_set_state(RID p_body, BodyState p_state, const Variant &p_variant);
	virtual Variant body_get_state(RID p_body, BodyState p_state) const;

	virtual void body_set_applied_force(RID p_body, const Vector3 &p_force);
	virtual Vector3 body_get_applied_force(RID p_body) const;

	virtual void body_set_applied_torque(RID p_body, const Vector3 &p_torque);
	virtual Vector3 body_get_applied_torque(RID p_body) const;

	virtual void body_add_central_force(RID p_body, const Vector3 &p_force);
	virtual void body_add_force(RID p_body, const Vector3 &p_force, const Vector3 &p_pos);
	virtual void body_add_torque(RID p_body, const Vector3 &p_torque);

	virtual void body_apply_central_impulse(RID p_body, const Vector3 &p_impulse);
	virtual void body_apply_impulse(RID p_body, const Vector3 &p_pos, const Vector3 &p_impulse);
	virtual void body_apply_torque_impulse(RID p_body, const Vector3 &p_impulse);
	virtual void body_set_axis_velocity(RID p_body, const Vector3 &p_axis_velocity);

	virtual void body_set_axis_lock(RID p_body, BodyAxis p_axis, bool p_lock);
	virtual bool body_is_axis_locked(RID p_body, BodyAxis p_axis) const;

	virtual void body_add_collision_exception(RID p_body, RID p_body_b);
	virtual void body_remove_collision_exception(RID p_body, RID p_body_b);
	virtual void body_get_collision_exceptions(RID p_body, List<RID> *p_exceptions);

	virtual void body_set_max_contacts_reported(RID p_body, int p_contacts);
	virtual int body_get_max_contacts_reported(RID p_body) const;

	virtual void body_set_contacts_reported_depth_threshold(RID p_body, float p_threshold);
	virtual float body_get_contacts_reported_depth_threshold(RID p_body) const;

	virtual void body_set_omit_force_integration(RID p_body, bool p_omit);
	virtual bool body_is_omitting_force_integration(RID p_body) const;

	virtual void body_set_force_integration_callback(RID p_body, Object *p_receiver, const StringName &p_method, const Variant &p_udata = Variant());

	virtual void body_set_ray_pickable(RID p_body, bool p_enable);
	virtual bool body_is_ray_pickable(RID p_body) const;

	virtual PhysicsDirectBodyState *body_get_direct_state(RID p_body);

	virtual bool body_test_motion(RID p_body, const Transform &p_from, const Vector3 &p_motion, bool p_infinite_inertia, MotionResult *r_result = nullptr, bool p_exclude_raycast_shapes = true, const Set<RID> &p_exclude = Set<RID>());
	virtual int body_test_ray_separation(RID p_body, const Transform &p_transform, bool p_infinite_inertia, Vector3 &r_recover_motion, SeparationResult *r_results, int p_result_max, float p_margin = 0.001);

	/* SOFT BODY API */

	// Box3D has no soft bodies and none are planned. These stay unsupported;
	// GodotPhysics stubs them the same way, so a SoftBody node simply does
	// nothing rather than breaking the build.

	virtual RID soft_body_create(bool p_init_sleeping = false);

	virtual void soft_body_update_visual_server(RID p_body, class SoftBodyVisualServerHandler *p_visual_server_handler);

	virtual void soft_body_set_space(RID p_body, RID p_space);
	virtual RID soft_body_get_space(RID p_body) const;

	virtual void soft_body_set_mesh(RID p_body, const REF &p_mesh);

	virtual void soft_body_set_collision_layer(RID p_body, uint32_t p_layer);
	virtual uint32_t soft_body_get_collision_layer(RID p_body) const;

	virtual void soft_body_set_collision_mask(RID p_body, uint32_t p_mask);
	virtual uint32_t soft_body_get_collision_mask(RID p_body) const;

	virtual void soft_body_add_collision_exception(RID p_body, RID p_body_b);
	virtual void soft_body_remove_collision_exception(RID p_body, RID p_body_b);
	virtual void soft_body_get_collision_exceptions(RID p_body, List<RID> *p_exceptions);

	virtual void soft_body_set_state(RID p_body, BodyState p_state, const Variant &p_variant);
	virtual Variant soft_body_get_state(RID p_body, BodyState p_state) const;

	virtual void soft_body_set_transform(RID p_body, const Transform &p_transform);
	virtual Vector3 soft_body_get_vertex_position(RID p_body, int vertex_index) const;

	virtual void soft_body_set_ray_pickable(RID p_body, bool p_enable);
	virtual bool soft_body_is_ray_pickable(RID p_body) const;

	virtual void soft_body_set_simulation_precision(RID p_body, int p_simulation_precision);
	virtual int soft_body_get_simulation_precision(RID p_body);

	virtual void soft_body_set_total_mass(RID p_body, real_t p_total_mass);
	virtual real_t soft_body_get_total_mass(RID p_body);

	virtual void soft_body_set_linear_stiffness(RID p_body, real_t p_stiffness);
	virtual real_t soft_body_get_linear_stiffness(RID p_body);

	virtual void soft_body_set_areaAngular_stiffness(RID p_body, real_t p_stiffness);
	virtual real_t soft_body_get_areaAngular_stiffness(RID p_body);

	virtual void soft_body_set_volume_stiffness(RID p_body, real_t p_stiffness);
	virtual real_t soft_body_get_volume_stiffness(RID p_body);

	virtual void soft_body_set_pressure_coefficient(RID p_body, real_t p_pressure_coefficient);
	virtual real_t soft_body_get_pressure_coefficient(RID p_body);

	virtual void soft_body_set_pose_matching_coefficient(RID p_body, real_t p_pose_matching_coefficient);
	virtual real_t soft_body_get_pose_matching_coefficient(RID p_body);

	virtual void soft_body_set_damping_coefficient(RID p_body, real_t p_damping_coefficient);
	virtual real_t soft_body_get_damping_coefficient(RID p_body);

	virtual void soft_body_set_drag_coefficient(RID p_body, real_t p_drag_coefficient);
	virtual real_t soft_body_get_drag_coefficient(RID p_body);

	virtual void soft_body_move_point(RID p_body, int p_point_index, const Vector3 &p_global_position);
	virtual Vector3 soft_body_get_point_global_position(RID p_body, int p_point_index);

	virtual Vector3 soft_body_get_point_offset(RID p_body, int p_point_index) const;

	virtual void soft_body_remove_all_pinned_points(RID p_body);
	virtual void soft_body_pin_point(RID p_body, int p_point_index, bool p_pin);
	virtual bool soft_body_is_point_pinned(RID p_body, int p_point_index);

	/* JOINT API */

	virtual JointType joint_get_type(RID p_joint) const;

	virtual void joint_set_solver_priority(RID p_joint, int p_priority);
	virtual int joint_get_solver_priority(RID p_joint) const;

	virtual void joint_disable_collisions_between_bodies(RID p_joint, const bool p_disable);
	virtual bool joint_is_disabled_collisions_between_bodies(RID p_joint) const;

	virtual RID joint_create_pin(RID p_body_A, const Vector3 &p_local_A, RID p_body_B, const Vector3 &p_local_B);

	virtual void pin_joint_set_param(RID p_joint, PinJointParam p_param, float p_value);
	virtual float pin_joint_get_param(RID p_joint, PinJointParam p_param) const;

	virtual void pin_joint_set_local_a(RID p_joint, const Vector3 &p_A);
	virtual Vector3 pin_joint_get_local_a(RID p_joint) const;

	virtual void pin_joint_set_local_b(RID p_joint, const Vector3 &p_B);
	virtual Vector3 pin_joint_get_local_b(RID p_joint) const;

	virtual RID joint_create_hinge(RID p_body_A, const Transform &p_hinge_A, RID p_body_B, const Transform &p_hinge_B);
	virtual RID joint_create_hinge_simple(RID p_body_A, const Vector3 &p_pivot_A, const Vector3 &p_axis_A, RID p_body_B, const Vector3 &p_pivot_B, const Vector3 &p_axis_B);

	virtual void hinge_joint_set_param(RID p_joint, HingeJointParam p_param, float p_value);
	virtual float hinge_joint_get_param(RID p_joint, HingeJointParam p_param) const;

	virtual void hinge_joint_set_flag(RID p_joint, HingeJointFlag p_flag, bool p_value);
	virtual bool hinge_joint_get_flag(RID p_joint, HingeJointFlag p_flag) const;

	virtual RID joint_create_slider(RID p_body_A, const Transform &p_local_frame_A, RID p_body_B, const Transform &p_local_frame_B);

	virtual void slider_joint_set_param(RID p_joint, SliderJointParam p_param, float p_value);
	virtual float slider_joint_get_param(RID p_joint, SliderJointParam p_param) const;

	virtual RID joint_create_cone_twist(RID p_body_A, const Transform &p_local_frame_A, RID p_body_B, const Transform &p_local_frame_B);

	virtual void cone_twist_joint_set_param(RID p_joint, ConeTwistJointParam p_param, float p_value);
	virtual float cone_twist_joint_get_param(RID p_joint, ConeTwistJointParam p_param) const;

	virtual RID joint_create_generic_6dof(RID p_body_A, const Transform &p_local_frame_A, RID p_body_B, const Transform &p_local_frame_B);

	virtual void generic_6dof_joint_set_param(RID p_joint, Vector3::Axis p_axis, G6DOFJointAxisParam p_param, float p_value);
	virtual float generic_6dof_joint_get_param(RID p_joint, Vector3::Axis p_axis, G6DOFJointAxisParam p_param);

	virtual void generic_6dof_joint_set_flag(RID p_joint, Vector3::Axis p_axis, G6DOFJointAxisFlag p_flag, bool p_enable);
	virtual bool generic_6dof_joint_get_flag(RID p_joint, Vector3::Axis p_axis, G6DOFJointAxisFlag p_flag);

	/* MISC */

	virtual void free(RID p_rid);

	virtual void set_active(bool p_active) { active = p_active; }

	virtual void init();
	virtual void step(float p_delta);
	virtual void flush_queries();
	virtual void finish();

	virtual bool is_flushing_queries() const { return doing_sync; }

	virtual void set_collision_iterations(int p_iterations);

	virtual int get_process_info(ProcessInfo p_info);
};

#endif // BOX3D_PHYSICS_SERVER_H
