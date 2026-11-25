#ifndef SPATIAL_CABLE_H
#define SPATIAL_CABLE_H

#include "core/map.h"
#include "core/math/transform.h"
#include "core/vector.h"
#include "scene/3d/spatial.h"
#include "scene/resources/material.h"

class ImmediateGeometry;
class StaticBody;
class CollisionShape;

// SpatialCable — a procedurally meshed, wind-driven cable/rope/wire strung
// between two points.
//
// The node itself defines endpoint A (its own origin, offset by
// `start_position`) and endpoint B (`end_position`, or the origin of the
// node at `attach_end`). Between them it runs one or more parallel strands,
// each a Verlet particle chain: pinned at both anchors, pulled by gravity,
// pushed by a global wind vector with gusts, and given a subtle idle sway.
//
// The visible geometry is a single rebuilt-every-frame ImmediateGeometry
// tube (round cross-section, `cable_sides` around, `cable_segments` along).
// It takes an ordinary Material and honours a cast-shadow toggle.
//
// Shooting: with `collision_enabled` the cable owns a StaticBody of capsule
// segments, so any hitscan ray in the game collides with it — the game then
// calls hit(collision_point). With `detect_shots` the cable also watches a
// node group ("bullet" by default) itself and severs on contact, no game
// code required. A severed segment splits the strand; each half keeps
// hanging from the anchor it still holds.
class SpatialCable : public Spatial {
	GDCLASS(SpatialCable, Spatial);

public:
	SpatialCable();
	~SpatialCable();

	// --- endpoints ---
	void set_start_position(const Vector3 &p_pos);
	Vector3 get_start_position() const;
	void set_end_position(const Vector3 &p_pos);
	Vector3 get_end_position() const;
	void set_attach_start(const NodePath &p_path);
	NodePath get_attach_start() const;
	void set_attach_end(const NodePath &p_path);
	NodePath get_attach_end() const;

	// --- cable shape ---
	void set_cable_count(int p_count);
	int get_cable_count() const;
	void set_cable_spacing(float p_spacing);
	float get_cable_spacing() const;
	void set_cable_radius(float p_radius);
	float get_cable_radius() const;
	void set_cable_sides(int p_sides);
	int get_cable_sides() const;
	void set_cable_segments(int p_segments);
	int get_cable_segments() const;
	void set_slack(float p_slack);
	float get_slack() const;

	// --- rendering ---
	void set_material(const Ref<Material> &p_material);
	Ref<Material> get_material() const;
	void set_cast_shadow(bool p_enabled);
	bool get_cast_shadow() const;

	// --- wind ---
	void set_wind_enabled(bool p_enabled);
	bool get_wind_enabled() const;
	void set_wind_direction(const Vector3 &p_dir);
	Vector3 get_wind_direction() const;
	void set_wind_force(float p_force);
	float get_wind_force() const;
	void set_wind_turbulence(float p_turbulence);
	float get_wind_turbulence() const;
	void set_wind_affect(float p_affect);
	float get_wind_affect() const;

	// --- sway ---
	void set_sway_enabled(bool p_enabled);
	bool get_sway_enabled() const;
	void set_sway_amount(float p_amount);
	float get_sway_amount() const;
	void set_sway_speed(float p_speed);
	float get_sway_speed() const;

	// --- simulation ---
	void set_simulate(bool p_enabled);
	bool get_simulate() const;
	void set_gravity(float p_gravity);
	float get_gravity() const;
	void set_stiffness(float p_stiffness);
	float get_stiffness() const;
	void set_damping(float p_damping);
	float get_damping() const;
	void set_substeps(int p_substeps);
	int get_substeps() const;
	void set_solver_iterations(int p_iterations);
	int get_solver_iterations() const;

	// --- breaking ---
	void set_breakable(bool p_breakable);
	bool get_breakable() const;
	void set_detect_shots(bool p_enabled);
	bool get_detect_shots() const;
	void set_projectile_group(const String &p_group);
	String get_projectile_group() const;
	void set_hit_radius(float p_radius);
	float get_hit_radius() const;
	void set_collision_enabled(bool p_enabled);
	bool get_collision_enabled() const;
	void set_collision_layer(uint32_t p_layer);
	uint32_t get_collision_layer() const;
	void set_collision_mask(uint32_t p_mask);
	uint32_t get_collision_mask() const;

	// --- runtime API ---
	// Register a hit at a world-space point: emits "cable_hit", and (when
	// breakable) severs the nearest strand segment and emits "cable_severed".
	void hit(const Vector3 &p_global_position);
	// Cut one specific strand segment directly.
	void sever(int p_cable_index, int p_segment_index);
	// Rejoin every cut and snap all strands back to their resting curve.
	void reset_cable();
	bool is_severed() const;
	// Rebuild strand/collision topology now (after a scripted property change).
	void rebuild();
	int get_point_count() const;
	// World-space position of a simulated particle.
	Vector3 get_point_position(int p_cable_index, int p_point_index) const;

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	struct Particle {
		Vector3 pos;
		Vector3 prev;
	};
	struct Strand {
		Vector<Particle> points; // cable_segments + 1
		Vector<bool> cut; // cable_segments
		float phase = 0.0f; // wind/sway desync between parallel strands
	};

	// endpoints
	Vector3 start_position = Vector3(0, 0, 0);
	Vector3 end_position = Vector3(3, 0, 0);
	NodePath attach_start;
	NodePath attach_end;

	// cable shape
	int cable_count = 1;
	float cable_spacing = 0.2f;
	float cable_radius = 0.03f;
	int cable_sides = 6;
	int cable_segments = 24;
	float slack = 0.2f;

	// rendering
	Ref<Material> material;
	bool cast_shadow = true;

	// wind
	bool wind_enabled = true;
	Vector3 wind_direction = Vector3(1, 0, 0);
	float wind_force = 1.5f;
	float wind_turbulence = 1.0f;
	float wind_affect = 1.0f;

	// sway
	bool sway_enabled = true;
	float sway_amount = 0.15f;
	float sway_speed = 1.2f;

	// simulation
	bool simulate = true;
	float gravity = 9.8f;
	float stiffness = 1.0f;
	float damping = 0.02f;
	int substeps = 2;
	int solver_iterations = 10;

	// breaking
	bool breakable = true;
	bool detect_shots = true;
	String projectile_group = "bullet";
	float hit_radius = 0.2f;
	bool collision_enabled = true;
	uint32_t collision_layer = 1;
	// GodotPhysics/Bullet ray queries only check the *target's* collision_layer
	// against the query's mask (servers/physics/space_sw.cpp:_can_collide_with),
	// so a body's own collision_mask is irrelevant to being hit and 0 would be
	// harmless there. Box3D's b3QueryFilter is bidirectional (Box2D-style: the
	// shape's own maskBits must also match the query's category), so a 0 mask
	// makes the cable invisible to every query under that backend specifically.
	// Default to 1 (matching collision_layer, and Godot's own CollisionObject
	// default) so the cable is hittable out of the box on every backend.
	uint32_t collision_mask = 1;

	Vector<Strand> _strands;
	ImmediateGeometry *_geom = nullptr;
	StaticBody *_body = nullptr;
	Vector<CollisionShape *> _shapes; // [cable * segments + segment]
	Vector<float> _shape_len; // cached capsule heights, parallel to _shapes

	bool _topology_dirty = true;
	bool _geo_dirty = true; // rebuild the mesh next frame (param/anchor change)
	float _time = 0.0f;
	Vector3 _last_a; // last resolved anchors, to catch endpoint moves while idle
	Vector3 _last_b;
	Map<ObjectID, Vector3> _proj_last; // last-seen positions of tracked projectiles

	void _resolve_anchors(Vector3 &r_a, Vector3 &r_b) const;
	Vector3 _spread_axis(const Vector3 &p_a, const Vector3 &p_b) const;
	void _rebuild_topology();
	void _reset_positions();
	void _rebuild_collision_nodes();
	void _update_collision_shapes();
	void _step(float p_delta);
	void _substep(float p_h);
	void _detect_shots();
	void _rebuild_geometry();
	void _apply_render_settings();
	bool _should_simulate() const;
	// Nearest strand segment to a world point; returns closest point on it.
	bool _closest_segment(const Vector3 &p_point, int &r_cable, int &r_segment, Vector3 &r_closest) const;
};

#endif // SPATIAL_CABLE_H
