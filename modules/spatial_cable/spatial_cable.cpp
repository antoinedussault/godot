#include "spatial_cable.h"

#include "core/class_db.h"
#include "core/engine.h"
#include "core/list.h"
#include "core/math/plane.h"
#include "core/math/vector2.h"
#include "core/set.h"
#include "scene/3d/collision_shape.h"
#include "scene/3d/immediate_geometry.h"
#include "scene/3d/physics_body.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/capsule_shape.h"

// ---------------------------------------------------------------------------
// tube-mesh helpers (file-local)
// ---------------------------------------------------------------------------

// A stable perpendicular to p_t, used to seed the parallel-transport frame.
static Vector3 _ortho(const Vector3 &p_t) {
	Vector3 a = Math::abs(p_t.y) < 0.99f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
	Vector3 n = a - p_t * p_t.dot(a);
	if (n.length() < CMP_EPSILON) {
		a = Vector3(1, 0, 0);
		n = a - p_t * p_t.dot(a);
	}
	return n.normalized();
}

struct RingVert {
	Vector3 p;
	Vector3 n;
	Vector3 tan;
	Vector2 uv;
};

// One vertex on the ring around p_center. p_u in [0,1] goes around, matching
// Godot's own CapsuleMesh cylinder convention so the winding below produces
// outward-facing triangles.
static RingVert _ring_vert(const Vector3 &p_center, const Vector3 &p_N, const Vector3 &p_B,
		float p_u, float p_v, float p_radius) {
	float ang = p_u * Math_TAU;
	float x = Math::sin(ang);
	float y = -Math::cos(ang);
	Vector3 radial = p_N * x + p_B * y;
	RingVert r;
	r.n = radial.normalized();
	r.p = p_center + radial * p_radius;
	r.tan = p_N * (-y) + p_B * x;
	r.uv = Vector2(p_u, p_v);
	return r;
}

static void _emit(ImmediateGeometry *p_ig, const RingVert &p_v) {
	p_ig->set_normal(p_v.n);
	p_ig->set_tangent(Plane(p_v.tan, 1.0));
	p_ig->set_uv(p_v.uv);
	p_ig->add_vertex(p_v.p);
}

// Emits the tube wall for one uncut run (points [p_a..p_b]) plus flat caps at
// both ends of the run, so severed ends read as solid rather than hollow.
static void _emit_run(ImmediateGeometry *p_ig,
		const Vector<Vector3> &C, const Vector<Vector3> &N, const Vector<Vector3> &B,
		const Vector<float> &V, int p_a, int p_b, int p_sides, float p_radius, float p_total) {
	for (int j = p_a; j < p_b; j++) {
		float v0 = V[j] / p_total;
		float v1 = V[j + 1] / p_total;
		for (int k = 0; k < p_sides; k++) {
			float u0 = (float)k / (float)p_sides;
			float u1 = (float)(k + 1) / (float)p_sides;
			RingVert p00 = _ring_vert(C[j], N[j], B[j], u0, v0, p_radius);
			RingVert p01 = _ring_vert(C[j], N[j], B[j], u1, v0, p_radius);
			RingVert p10 = _ring_vert(C[j + 1], N[j + 1], B[j + 1], u0, v1, p_radius);
			RingVert p11 = _ring_vert(C[j + 1], N[j + 1], B[j + 1], u1, v1, p_radius);
			_emit(p_ig, p00);
			_emit(p_ig, p01);
			_emit(p_ig, p10);
			_emit(p_ig, p01);
			_emit(p_ig, p11);
			_emit(p_ig, p10);
		}
	}

	Vector3 t_start = (p_b > p_a) ? (C[p_a + 1] - C[p_a]) : Vector3(0, 0, 1);
	if (t_start.length() < CMP_EPSILON) {
		t_start = Vector3(0, 0, 1);
	}
	t_start.normalize();
	Vector3 t_end = (p_b > p_a) ? (C[p_b] - C[p_b - 1]) : Vector3(0, 0, 1);
	if (t_end.length() < CMP_EPSILON) {
		t_end = Vector3(0, 0, 1);
	}
	t_end.normalize();

	for (int k = 0; k < p_sides; k++) {
		float u0 = (float)k / (float)p_sides;
		float u1 = (float)(k + 1) / (float)p_sides;

		RingVert a0 = _ring_vert(C[p_a], N[p_a], B[p_a], u0, 0.0f, p_radius);
		RingVert a1 = _ring_vert(C[p_a], N[p_a], B[p_a], u1, 0.0f, p_radius);
		p_ig->set_normal(-t_start);
		p_ig->set_tangent(Plane(N[p_a], 1.0));
		p_ig->set_uv(Vector2(0.5, 0.5));
		p_ig->add_vertex(C[p_a]);
		p_ig->set_normal(-t_start);
		p_ig->set_tangent(Plane(N[p_a], 1.0));
		p_ig->set_uv(a1.uv);
		p_ig->add_vertex(a1.p);
		p_ig->set_normal(-t_start);
		p_ig->set_tangent(Plane(N[p_a], 1.0));
		p_ig->set_uv(a0.uv);
		p_ig->add_vertex(a0.p);

		RingVert b0 = _ring_vert(C[p_b], N[p_b], B[p_b], u0, 1.0f, p_radius);
		RingVert b1 = _ring_vert(C[p_b], N[p_b], B[p_b], u1, 1.0f, p_radius);
		p_ig->set_normal(t_end);
		p_ig->set_tangent(Plane(N[p_b], 1.0));
		p_ig->set_uv(Vector2(0.5, 0.5));
		p_ig->add_vertex(C[p_b]);
		p_ig->set_normal(t_end);
		p_ig->set_tangent(Plane(N[p_b], 1.0));
		p_ig->set_uv(b0.uv);
		p_ig->add_vertex(b0.p);
		p_ig->set_normal(t_end);
		p_ig->set_tangent(Plane(N[p_b], 1.0));
		p_ig->set_uv(b1.uv);
		p_ig->add_vertex(b1.p);
	}
}

// ---------------------------------------------------------------------------
// SpatialCable
// ---------------------------------------------------------------------------

SpatialCable::SpatialCable() {}
SpatialCable::~SpatialCable() {}

bool SpatialCable::_should_simulate() const {
	return simulate && is_inside_tree() && !Engine::get_singleton()->is_editor_hint();
}

void SpatialCable::_resolve_anchors(Vector3 &r_a, Vector3 &r_b) const {
	Transform gx = get_global_transform();
	r_a = gx.xform(start_position);
	r_b = gx.xform(end_position);
	if (!attach_start.is_empty()) {
		Spatial *sp = Object::cast_to<Spatial>(const_cast<SpatialCable *>(this)->get_node_or_null(attach_start));
		if (sp) {
			r_a = sp->get_global_transform().origin;
		}
	}
	if (!attach_end.is_empty()) {
		Spatial *sp = Object::cast_to<Spatial>(const_cast<SpatialCable *>(this)->get_node_or_null(attach_end));
		if (sp) {
			r_b = sp->get_global_transform().origin;
		}
	}
}

Vector3 SpatialCable::_spread_axis(const Vector3 &p_a, const Vector3 &p_b) const {
	Vector3 axis = (p_b - p_a).cross(Vector3(0, 1, 0));
	if (axis.length() < CMP_EPSILON) {
		axis = Vector3(1, 0, 0);
	}
	return axis.normalized();
}

void SpatialCable::_rebuild_topology() {
	int n = CLAMP(cable_segments, 2, 128) + 1;
	int cc = CLAMP(cable_count, 1, 64);
	_strands.clear();
	_strands.resize(cc);
	for (int c = 0; c < cc; c++) {
		Strand &s = _strands.write[c];
		s.points.resize(n);
		s.cut.resize(n - 1);
		for (int i = 0; i < n - 1; i++) {
			s.cut.write[i] = false;
		}
		s.phase = Math::randf() * Math_TAU;
	}
	_reset_positions();
	_rebuild_collision_nodes();
	_topology_dirty = false;
}

void SpatialCable::_reset_positions() {
	if (_strands.empty()) {
		return;
	}
	Vector3 a_world, b_world;
	_resolve_anchors(a_world, b_world);
	Vector3 axis = _spread_axis(a_world, b_world);
	int cc = _strands.size();
	for (int c = 0; c < cc; c++) {
		Strand &s = _strands.write[c];
		int n = s.points.size();
		if (n < 2) {
			continue;
		}
		float centered = (float)c - (float)(cc - 1) * 0.5f;
		Vector3 off = axis * (centered * cable_spacing);
		Vector3 pa = a_world + off;
		Vector3 pb = b_world + off;
		float chord = pa.distance_to(pb);
		float sag = chord * MAX(0.0f, slack) * 0.9f;
		for (int j = 0; j < n; j++) {
			float t = (float)j / (float)(n - 1);
			Vector3 base = pa.linear_interpolate(pb, t);
			base.y -= sag * 4.0f * t * (1.0f - t);
			s.points.write[j].pos = base;
			s.points.write[j].prev = base;
		}
	}
}

void SpatialCable::_rebuild_collision_nodes() {
	if (!_body) {
		return;
	}
	for (int i = 0; i < _shapes.size(); i++) {
		if (_shapes[i]) {
			_shapes[i]->queue_delete();
		}
	}
	_shapes.clear();
	_shape_len.clear();

	_body->set_collision_layer(collision_layer);
	_body->set_collision_mask(collision_mask);

	if (!collision_enabled || _strands.empty()) {
		return;
	}
	int segs = _strands[0].points.size() - 1;
	int cc = _strands.size();
	if (segs <= 0 || cc * segs > 512) {
		return;
	}
	for (int c = 0; c < cc; c++) {
		for (int seg = 0; seg < segs; seg++) {
			CollisionShape *cs = memnew(CollisionShape);
			Ref<CapsuleShape> cap;
			cap.instance();
			cap->set_radius(MAX(0.01f, cable_radius));
			cap->set_height(0.01f);
			cs->set_shape(cap);
			_body->add_child(cs);
			_shapes.push_back(cs);
			_shape_len.push_back(-1.0f);
		}
	}
}

void SpatialCable::_update_collision_shapes() {
	if (_shapes.empty() || _strands.empty()) {
		return;
	}
	int segs = _strands[0].points.size() - 1;
	if (segs <= 0) {
		return;
	}
	Transform inv = get_global_transform().affine_inverse();
	float cap_r = MAX(0.01f, cable_radius);
	int cc = _strands.size();
	for (int c = 0; c < cc; c++) {
		const Strand &s = _strands[c];
		for (int seg = 0; seg < segs; seg++) {
			int idx = c * segs + seg;
			if (idx >= _shapes.size()) {
				break;
			}
			CollisionShape *cs = _shapes[idx];
			if (!cs) {
				continue;
			}
			if (s.cut[seg]) {
				if (!cs->is_disabled()) {
					cs->set_disabled(true);
				}
				continue;
			}
			Vector3 A = inv.xform(s.points[seg].pos);
			Vector3 D = inv.xform(s.points[seg + 1].pos);
			Vector3 dir = D - A;
			float len = dir.length();
			if (len < CMP_EPSILON) {
				if (!cs->is_disabled()) {
					cs->set_disabled(true);
				}
				continue;
			}
			if (cs->is_disabled()) {
				cs->set_disabled(false);
			}
			dir /= len;
			Vector3 up = Math::abs(dir.y) < 0.9f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
			Vector3 x = up.cross(dir).normalized();
			Vector3 y = dir.cross(x).normalized();
			Basis b;
			b.set_axis(0, x);
			b.set_axis(1, y);
			b.set_axis(2, dir);
			Transform t;
			t.basis = b;
			t.origin = (A + D) * 0.5f;
			cs->set_transform(t);

			float want = MAX(0.0f, len - 2.0f * cap_r);
			if (Math::abs(want - _shape_len[idx]) > 0.005f) {
				Ref<CapsuleShape> cap = cs->get_shape();
				if (cap.is_valid()) {
					cap->set_height(want);
				}
				_shape_len.write[idx] = want;
			}
		}
	}
}

void SpatialCable::_step(float p_delta) {
	int ss = CLAMP(substeps, 1, 8);
	float h = p_delta / (float)ss;
	for (int i = 0; i < ss; i++) {
		_substep(h);
	}
}

void SpatialCable::_substep(float p_h) {
	Vector3 a_world, b_world;
	_resolve_anchors(a_world, b_world);
	Vector3 axis = _spread_axis(a_world, b_world);

	Vector3 gvec(0, -gravity, 0);
	Vector3 wdir = wind_direction;
	if (wdir.length() < CMP_EPSILON) {
		wdir = Vector3(1, 0, 0);
	}
	wdir.normalize();

	float damp = CLAMP(damping, 0.0f, 1.0f);
	// Frame-rate independent: `damping` is the velocity fraction lost per 1/60 s,
	// re-exponentiated to the actual substep length so substeps/FPS don't change
	// how draggy the cable feels.
	float damp_ret = Math::pow(MAX(0.0f, 1.0f - damp), p_h * 60.0f);
	float stiff = CLAMP(stiffness, 0.0f, 1.0f);
	int iters = CLAMP(solver_iterations, 1, 32);
	int cc = _strands.size();

	for (int c = 0; c < cc; c++) {
		Strand &s = _strands.write[c];
		int n = s.points.size();
		if (n < 2) {
			continue;
		}
		float centered = (float)c - (float)(cc - 1) * 0.5f;
		Vector3 off = axis * (centered * cable_spacing);
		Vector3 anchor_a = a_world + off;
		Vector3 anchor_b = b_world + off;

		float chord = anchor_a.distance_to(anchor_b);
		float rest_total = MAX(chord, 0.001f) * (1.0f + MAX(0.0f, slack));
		float rest_seg = rest_total / (float)(n - 1);

		float gust = 1.0f;
		if (wind_turbulence > 0.0f) {
			float w = 0.5f * (Math::sin(_time * 1.9f + s.phase) + Math::sin(_time * 0.7f + s.phase * 1.7f));
			gust = 1.0f + wind_turbulence * 0.5f * w;
		}
		Vector3 wind_vec = wdir * (wind_force * gust * wind_affect);

		for (int j = 1; j < n - 1; j++) {
			Particle &p = s.points.write[j];
			float t = (float)j / (float)(n - 1);
			float endw = 4.0f * t * (1.0f - t);

			Vector3 acc = gvec;
			if (wind_enabled) {
				acc += wind_vec;
				acc += Vector3(0, 1, 0) * (Math::sin(_time * 5.0f + s.phase + j * 0.6f) * wind_force * 0.05f * wind_affect);
			}
			if (sway_enabled) {
				acc += axis * (Math::sin(_time * sway_speed + s.phase + t * Math_PI) * sway_amount * 2.0f * endw);
			}

			Vector3 vel = (p.pos - p.prev) * damp_ret;
			Vector3 np = p.pos + vel + acc * (p_h * p_h);
			p.prev = p.pos;
			p.pos = np;
		}

		// Pin anchors with no injected velocity.
		s.points.write[0].pos = anchor_a;
		s.points.write[0].prev = anchor_a;
		s.points.write[n - 1].pos = anchor_b;
		s.points.write[n - 1].prev = anchor_b;

		for (int it = 0; it < iters; it++) {
			for (int seg = 0; seg < n - 1; seg++) {
				if (s.cut[seg]) {
					continue;
				}
				Particle &pa = s.points.write[seg];
				Particle &pb = s.points.write[seg + 1];
				Vector3 d = pb.pos - pa.pos;
				float len = d.length();
				if (len < CMP_EPSILON) {
					continue;
				}
				float diff = (len - rest_seg) / len;
				float wa = (seg == 0) ? 0.0f : 1.0f;
				float wb = (seg + 1 == n - 1) ? 0.0f : 1.0f;
				float ws = wa + wb;
				if (ws <= 0.0f) {
					continue;
				}
				Vector3 corr = d * (diff * stiff);
				pa.pos += corr * (wa / ws);
				pb.pos -= corr * (wb / ws);
			}
		}
	}
}

bool SpatialCable::_closest_segment(const Vector3 &p_point, int &r_cable, int &r_segment, Vector3 &r_closest) const {
	float best = 1e30f;
	bool found = false;
	for (int c = 0; c < _strands.size(); c++) {
		const Strand &s = _strands[c];
		int n = s.points.size();
		for (int seg = 0; seg < n - 1; seg++) {
			if (s.cut[seg]) {
				continue;
			}
			const Vector3 &a = s.points[seg].pos;
			const Vector3 &b = s.points[seg + 1].pos;
			Vector3 ab = b - a;
			float ab2 = ab.length_squared();
			float t = ab2 > CMP_EPSILON ? CLAMP((p_point - a).dot(ab) / ab2, 0.0f, 1.0f) : 0.0f;
			Vector3 cp = a + ab * t;
			float d = cp.distance_to(p_point);
			if (d < best) {
				best = d;
				r_cable = c;
				r_segment = seg;
				r_closest = cp;
				found = true;
			}
		}
	}
	return found;
}

void SpatialCable::_detect_shots() {
	if (projectile_group.empty()) {
		return;
	}
	SceneTree *st = get_tree();
	if (!st) {
		return;
	}
	List<Node *> nodes;
	st->get_nodes_in_group(projectile_group, &nodes);
	if (nodes.empty()) {
		if (!_proj_last.empty()) {
			_proj_last.clear();
		}
		return;
	}

	float thresh = hit_radius + cable_radius;
	Set<ObjectID> seen;
	for (List<Node *>::Element *E = nodes.front(); E; E = E->next()) {
		Spatial *sp = Object::cast_to<Spatial>(E->get());
		if (!sp) {
			continue;
		}
		ObjectID id = sp->get_instance_id();
		seen.insert(id);
		Vector3 cur = sp->get_global_transform().origin;
		Vector3 prev = cur;
		Map<ObjectID, Vector3>::Element *L = _proj_last.find(id);
		if (L) {
			prev = L->get();
		}
		_proj_last[id] = cur;

		// Sample along the projectile's motion this frame so fast rounds
		// don't tunnel straight through a thin cable.
		int samples = 1 + (int)((cur - prev).length() / MAX(0.05f, thresh));
		samples = CLAMP(samples, 1, 16);
		for (int k = 0; k <= samples; k++) {
			Vector3 probe = prev.linear_interpolate(cur, (float)k / (float)samples);
			int bc = -1, bs = -1;
			Vector3 cp;
			if (_closest_segment(probe, bc, bs, cp) && probe.distance_to(cp) <= thresh) {
				hit(cp);
				break;
			}
		}
	}

	List<ObjectID> dead;
	for (Map<ObjectID, Vector3>::Element *E = _proj_last.front(); E; E = E->next()) {
		if (!seen.has(E->key())) {
			dead.push_back(E->key());
		}
	}
	for (List<ObjectID>::Element *E = dead.front(); E; E = E->next()) {
		_proj_last.erase(E->get());
	}
}

void SpatialCable::_rebuild_geometry() {
	if (!_geom) {
		return;
	}

	bool any = false;
	for (int c = 0; c < _strands.size() && !any; c++) {
		const Strand &s = _strands[c];
		for (int seg = 0; seg < s.cut.size(); seg++) {
			if (!s.cut[seg]) {
				any = true;
				break;
			}
		}
	}
	if (!any) {
		_geom->clear();
		return;
	}

	int sides = CLAMP(cable_sides, 3, 24);
	float radius = MAX(0.001f, cable_radius);
	Transform inv = get_global_transform().affine_inverse();

	_geom->clear();
	_geom->begin(Mesh::PRIMITIVE_TRIANGLES);

	for (int c = 0; c < _strands.size(); c++) {
		const Strand &s = _strands[c];
		int n = s.points.size();
		if (n < 2) {
			continue;
		}

		Vector<Vector3> C;
		C.resize(n);
		for (int j = 0; j < n; j++) {
			C.write[j] = inv.xform(s.points[j].pos);
		}

		Vector<Vector3> N, B;
		N.resize(n);
		B.resize(n);
		Vector3 prev_t = C[1] - C[0];
		if (prev_t.length() < CMP_EPSILON) {
			prev_t = Vector3(0, 0, 1);
		}
		prev_t.normalize();
		Vector3 nrm = _ortho(prev_t);
		for (int j = 0; j < n; j++) {
			Vector3 t;
			if (j == 0) {
				t = C[1] - C[0];
			} else if (j == n - 1) {
				t = C[n - 1] - C[n - 2];
			} else {
				t = C[j + 1] - C[j - 1];
			}
			if (t.length() < CMP_EPSILON) {
				t = prev_t;
			}
			t.normalize();

			Vector3 rot_axis = prev_t.cross(t);
			float sa = rot_axis.length();
			if (sa > 1e-5f) {
				float ang = Math::atan2(sa, prev_t.dot(t));
				nrm = nrm.rotated(rot_axis / sa, ang);
			}
			nrm = nrm - t * t.dot(nrm);
			if (nrm.length() < CMP_EPSILON) {
				nrm = _ortho(t);
			}
			nrm.normalize();

			N.write[j] = nrm;
			B.write[j] = nrm.cross(t);
			prev_t = t;
		}

		Vector<float> V;
		V.resize(n);
		V.write[0] = 0.0f;
		float acc = 0.0f;
		for (int j = 1; j < n; j++) {
			acc += C[j].distance_to(C[j - 1]);
			V.write[j] = acc;
		}
		float total = acc > CMP_EPSILON ? acc : 1.0f;

		int seg = 0;
		while (seg < n - 1) {
			if (s.cut[seg]) {
				seg++;
				continue;
			}
			int a = seg;
			while (seg < n - 1 && !s.cut[seg]) {
				seg++;
			}
			int b = seg;
			_emit_run(_geom, C, N, B, V, a, b, sides, radius, total);
		}
	}

	_geom->end();
}

void SpatialCable::_apply_render_settings() {
	if (!_geom) {
		return;
	}
	_geom->set_material_override(material);
	_geom->set_cast_shadows_setting(cast_shadow
					? GeometryInstance::SHADOW_CASTING_SETTING_ON
					: GeometryInstance::SHADOW_CASTING_SETTING_OFF);
}

// ---------------------------------------------------------------------------
// runtime API
// ---------------------------------------------------------------------------

void SpatialCable::hit(const Vector3 &p_global_position) {
	int c = -1, seg = -1;
	Vector3 cp;
	if (!_closest_segment(p_global_position, c, seg, cp)) {
		return;
	}
	emit_signal("cable_hit", cp, c);
	if (!breakable) {
		return;
	}
	if (c >= 0 && c < _strands.size() && seg >= 0 && seg < _strands[c].cut.size()) {
		if (!_strands[c].cut[seg]) {
			_strands.write[c].cut.write[seg] = true;
			_geo_dirty = true;
			emit_signal("cable_severed", c, seg, cp);
		}
	}
}

void SpatialCable::sever(int p_cable_index, int p_segment_index) {
	if (p_cable_index < 0 || p_cable_index >= _strands.size()) {
		return;
	}
	Strand &s = _strands.write[p_cable_index];
	if (p_segment_index < 0 || p_segment_index >= s.cut.size()) {
		return;
	}
	if (s.cut[p_segment_index]) {
		return;
	}
	s.cut.write[p_segment_index] = true;
	_geo_dirty = true;
	Vector3 pos = (s.points[p_segment_index].pos + s.points[p_segment_index + 1].pos) * 0.5f;
	emit_signal("cable_severed", p_cable_index, p_segment_index, pos);
}

void SpatialCable::reset_cable() {
	for (int c = 0; c < _strands.size(); c++) {
		Strand &s = _strands.write[c];
		for (int i = 0; i < s.cut.size(); i++) {
			s.cut.write[i] = false;
		}
	}
	_reset_positions();
	_geo_dirty = true;
}

bool SpatialCable::is_severed() const {
	for (int c = 0; c < _strands.size(); c++) {
		const Strand &s = _strands[c];
		for (int i = 0; i < s.cut.size(); i++) {
			if (s.cut[i]) {
				return true;
			}
		}
	}
	return false;
}

void SpatialCable::rebuild() {
	_topology_dirty = true;
	if (is_inside_tree()) {
		_rebuild_topology();
		_rebuild_geometry();
	}
}

int SpatialCable::get_point_count() const {
	if (!_strands.empty()) {
		return _strands[0].points.size();
	}
	return CLAMP(cable_segments, 2, 128) + 1;
}

Vector3 SpatialCable::get_point_position(int p_cable_index, int p_point_index) const {
	if (p_cable_index < 0 || p_cable_index >= _strands.size()) {
		return Vector3();
	}
	const Strand &s = _strands[p_cable_index];
	if (p_point_index < 0 || p_point_index >= s.points.size()) {
		return Vector3();
	}
	return s.points[p_point_index].pos;
}

// ---------------------------------------------------------------------------
// node lifecycle
// ---------------------------------------------------------------------------

void SpatialCable::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process(true);
			set_physics_process(true);
			_geo_dirty = true;
		} break;

		case NOTIFICATION_READY: {
			if (!_geom) {
				_geom = memnew(ImmediateGeometry);
				// Rebuilt from the camera-frame sim state, not physics ticks —
				// interpolation would just smear it.
				_geom->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
				add_child(_geom);
			}
			if (!_body) {
				_body = memnew(StaticBody);
				add_child(_body);
			}
			_apply_render_settings();
			_rebuild_topology();
			_rebuild_geometry();
			set_process(true);
			set_physics_process(true);
		} break;

		case NOTIFICATION_EXIT_TREE: {
			_proj_last.clear();
		} break;

		case NOTIFICATION_PROCESS: {
			if (_topology_dirty) {
				_rebuild_topology();
				_geo_dirty = true;
			}
			bool sim = _should_simulate();
			if (!sim) {
				// Editor, or dynamics disabled: keep the resting curve pinned
				// to wherever the endpoints currently are, but only spend the
				// rebuild when an endpoint actually moved or a param changed.
				Vector3 a, b;
				_resolve_anchors(a, b);
				if (_geo_dirty || _last_a.distance_to(a) > 0.0001f || _last_b.distance_to(b) > 0.0001f) {
					_last_a = a;
					_last_b = b;
					_reset_positions();
					_geo_dirty = true;
				}
			}
			if (sim || _geo_dirty) {
				_rebuild_geometry();
				_update_collision_shapes();
				_geo_dirty = false;
			}
		} break;

		case NOTIFICATION_PHYSICS_PROCESS: {
			if (Engine::get_singleton()->is_editor_hint() || _topology_dirty) {
				break;
			}
			float dt = get_physics_process_delta_time();
			_time += dt;
			if (simulate) {
				_step(dt);
			}
			if (detect_shots && breakable) {
				_detect_shots();
			}
		} break;
	}
}

// ---------------------------------------------------------------------------
// property accessors
// ---------------------------------------------------------------------------

void SpatialCable::set_start_position(const Vector3 &p_pos) {
	start_position = p_pos;
}
Vector3 SpatialCable::get_start_position() const { return start_position; }

void SpatialCable::set_end_position(const Vector3 &p_pos) {
	end_position = p_pos;
}
Vector3 SpatialCable::get_end_position() const { return end_position; }

void SpatialCable::set_attach_start(const NodePath &p_path) {
	attach_start = p_path;
}
NodePath SpatialCable::get_attach_start() const { return attach_start; }

void SpatialCable::set_attach_end(const NodePath &p_path) {
	attach_end = p_path;
}
NodePath SpatialCable::get_attach_end() const { return attach_end; }

void SpatialCable::set_cable_count(int p_count) {
	p_count = CLAMP(p_count, 1, 64);
	if (p_count == cable_count) {
		return;
	}
	cable_count = p_count;
	_topology_dirty = true;
}
int SpatialCable::get_cable_count() const { return cable_count; }

void SpatialCable::set_cable_spacing(float p_spacing) {
	cable_spacing = p_spacing;
	_geo_dirty = true;
}
float SpatialCable::get_cable_spacing() const { return cable_spacing; }

void SpatialCable::set_cable_radius(float p_radius) {
	cable_radius = MAX(0.001f, p_radius);
	_geo_dirty = true;
}
float SpatialCable::get_cable_radius() const { return cable_radius; }

void SpatialCable::set_cable_sides(int p_sides) {
	cable_sides = CLAMP(p_sides, 3, 24);
	_geo_dirty = true;
}
int SpatialCable::get_cable_sides() const { return cable_sides; }

void SpatialCable::set_cable_segments(int p_segments) {
	p_segments = CLAMP(p_segments, 2, 128);
	if (p_segments == cable_segments) {
		return;
	}
	cable_segments = p_segments;
	_topology_dirty = true;
}
int SpatialCable::get_cable_segments() const { return cable_segments; }

void SpatialCable::set_slack(float p_slack) {
	slack = MAX(0.0f, p_slack);
	_geo_dirty = true;
}
float SpatialCable::get_slack() const { return slack; }

void SpatialCable::set_material(const Ref<Material> &p_material) {
	material = p_material;
	_apply_render_settings();
}
Ref<Material> SpatialCable::get_material() const { return material; }

void SpatialCable::set_cast_shadow(bool p_enabled) {
	cast_shadow = p_enabled;
	_apply_render_settings();
}
bool SpatialCable::get_cast_shadow() const { return cast_shadow; }

void SpatialCable::set_wind_enabled(bool p_enabled) { wind_enabled = p_enabled; }
bool SpatialCable::get_wind_enabled() const { return wind_enabled; }

void SpatialCable::set_wind_direction(const Vector3 &p_dir) { wind_direction = p_dir; }
Vector3 SpatialCable::get_wind_direction() const { return wind_direction; }

void SpatialCable::set_wind_force(float p_force) { wind_force = p_force; }
float SpatialCable::get_wind_force() const { return wind_force; }

void SpatialCable::set_wind_turbulence(float p_turbulence) {
	wind_turbulence = MAX(0.0f, p_turbulence);
}
float SpatialCable::get_wind_turbulence() const { return wind_turbulence; }

void SpatialCable::set_wind_affect(float p_affect) {
	wind_affect = MAX(0.0f, p_affect);
}
float SpatialCable::get_wind_affect() const { return wind_affect; }

void SpatialCable::set_sway_enabled(bool p_enabled) { sway_enabled = p_enabled; }
bool SpatialCable::get_sway_enabled() const { return sway_enabled; }

void SpatialCable::set_sway_amount(float p_amount) {
	sway_amount = MAX(0.0f, p_amount);
}
float SpatialCable::get_sway_amount() const { return sway_amount; }

void SpatialCable::set_sway_speed(float p_speed) {
	sway_speed = MAX(0.0f, p_speed);
}
float SpatialCable::get_sway_speed() const { return sway_speed; }

void SpatialCable::set_simulate(bool p_enabled) {
	simulate = p_enabled;
	if (!p_enabled) {
		_reset_positions();
	}
}
bool SpatialCable::get_simulate() const { return simulate; }

void SpatialCable::set_gravity(float p_gravity) { gravity = p_gravity; }
float SpatialCable::get_gravity() const { return gravity; }

void SpatialCable::set_stiffness(float p_stiffness) {
	stiffness = CLAMP(p_stiffness, 0.0f, 1.0f);
}
float SpatialCable::get_stiffness() const { return stiffness; }

void SpatialCable::set_damping(float p_damping) {
	damping = CLAMP(p_damping, 0.0f, 1.0f);
}
float SpatialCable::get_damping() const { return damping; }

void SpatialCable::set_substeps(int p_substeps) {
	substeps = CLAMP(p_substeps, 1, 8);
}
int SpatialCable::get_substeps() const { return substeps; }

void SpatialCable::set_solver_iterations(int p_iterations) {
	solver_iterations = CLAMP(p_iterations, 1, 32);
}
int SpatialCable::get_solver_iterations() const { return solver_iterations; }

void SpatialCable::set_breakable(bool p_breakable) { breakable = p_breakable; }
bool SpatialCable::get_breakable() const { return breakable; }

void SpatialCable::set_detect_shots(bool p_enabled) { detect_shots = p_enabled; }
bool SpatialCable::get_detect_shots() const { return detect_shots; }

void SpatialCable::set_projectile_group(const String &p_group) { projectile_group = p_group; }
String SpatialCable::get_projectile_group() const { return projectile_group; }

void SpatialCable::set_hit_radius(float p_radius) {
	hit_radius = MAX(0.0f, p_radius);
}
float SpatialCable::get_hit_radius() const { return hit_radius; }

void SpatialCable::set_collision_enabled(bool p_enabled) {
	if (p_enabled == collision_enabled) {
		return;
	}
	collision_enabled = p_enabled;
	if (is_inside_tree()) {
		_rebuild_collision_nodes();
	}
}
bool SpatialCable::get_collision_enabled() const { return collision_enabled; }

void SpatialCable::set_collision_layer(uint32_t p_layer) {
	collision_layer = p_layer;
	if (_body) {
		_body->set_collision_layer(p_layer);
	}
}
uint32_t SpatialCable::get_collision_layer() const { return collision_layer; }

void SpatialCable::set_collision_mask(uint32_t p_mask) {
	collision_mask = p_mask;
	if (_body) {
		_body->set_collision_mask(p_mask);
	}
}
uint32_t SpatialCable::get_collision_mask() const { return collision_mask; }

// ---------------------------------------------------------------------------
// bindings
// ---------------------------------------------------------------------------

void SpatialCable::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_start_position", "position"), &SpatialCable::set_start_position);
	ClassDB::bind_method(D_METHOD("get_start_position"), &SpatialCable::get_start_position);
	ClassDB::bind_method(D_METHOD("set_end_position", "position"), &SpatialCable::set_end_position);
	ClassDB::bind_method(D_METHOD("get_end_position"), &SpatialCable::get_end_position);
	ClassDB::bind_method(D_METHOD("set_attach_start", "path"), &SpatialCable::set_attach_start);
	ClassDB::bind_method(D_METHOD("get_attach_start"), &SpatialCable::get_attach_start);
	ClassDB::bind_method(D_METHOD("set_attach_end", "path"), &SpatialCable::set_attach_end);
	ClassDB::bind_method(D_METHOD("get_attach_end"), &SpatialCable::get_attach_end);

	ClassDB::bind_method(D_METHOD("set_cable_count", "count"), &SpatialCable::set_cable_count);
	ClassDB::bind_method(D_METHOD("get_cable_count"), &SpatialCable::get_cable_count);
	ClassDB::bind_method(D_METHOD("set_cable_spacing", "spacing"), &SpatialCable::set_cable_spacing);
	ClassDB::bind_method(D_METHOD("get_cable_spacing"), &SpatialCable::get_cable_spacing);
	ClassDB::bind_method(D_METHOD("set_cable_radius", "radius"), &SpatialCable::set_cable_radius);
	ClassDB::bind_method(D_METHOD("get_cable_radius"), &SpatialCable::get_cable_radius);
	ClassDB::bind_method(D_METHOD("set_cable_sides", "sides"), &SpatialCable::set_cable_sides);
	ClassDB::bind_method(D_METHOD("get_cable_sides"), &SpatialCable::get_cable_sides);
	ClassDB::bind_method(D_METHOD("set_cable_segments", "segments"), &SpatialCable::set_cable_segments);
	ClassDB::bind_method(D_METHOD("get_cable_segments"), &SpatialCable::get_cable_segments);
	ClassDB::bind_method(D_METHOD("set_slack", "slack"), &SpatialCable::set_slack);
	ClassDB::bind_method(D_METHOD("get_slack"), &SpatialCable::get_slack);

	ClassDB::bind_method(D_METHOD("set_material", "material"), &SpatialCable::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &SpatialCable::get_material);
	ClassDB::bind_method(D_METHOD("set_cast_shadow", "enabled"), &SpatialCable::set_cast_shadow);
	ClassDB::bind_method(D_METHOD("get_cast_shadow"), &SpatialCable::get_cast_shadow);

	ClassDB::bind_method(D_METHOD("set_wind_enabled", "enabled"), &SpatialCable::set_wind_enabled);
	ClassDB::bind_method(D_METHOD("get_wind_enabled"), &SpatialCable::get_wind_enabled);
	ClassDB::bind_method(D_METHOD("set_wind_direction", "direction"), &SpatialCable::set_wind_direction);
	ClassDB::bind_method(D_METHOD("get_wind_direction"), &SpatialCable::get_wind_direction);
	ClassDB::bind_method(D_METHOD("set_wind_force", "force"), &SpatialCable::set_wind_force);
	ClassDB::bind_method(D_METHOD("get_wind_force"), &SpatialCable::get_wind_force);
	ClassDB::bind_method(D_METHOD("set_wind_turbulence", "turbulence"), &SpatialCable::set_wind_turbulence);
	ClassDB::bind_method(D_METHOD("get_wind_turbulence"), &SpatialCable::get_wind_turbulence);
	ClassDB::bind_method(D_METHOD("set_wind_affect", "affect"), &SpatialCable::set_wind_affect);
	ClassDB::bind_method(D_METHOD("get_wind_affect"), &SpatialCable::get_wind_affect);

	ClassDB::bind_method(D_METHOD("set_sway_enabled", "enabled"), &SpatialCable::set_sway_enabled);
	ClassDB::bind_method(D_METHOD("get_sway_enabled"), &SpatialCable::get_sway_enabled);
	ClassDB::bind_method(D_METHOD("set_sway_amount", "amount"), &SpatialCable::set_sway_amount);
	ClassDB::bind_method(D_METHOD("get_sway_amount"), &SpatialCable::get_sway_amount);
	ClassDB::bind_method(D_METHOD("set_sway_speed", "speed"), &SpatialCable::set_sway_speed);
	ClassDB::bind_method(D_METHOD("get_sway_speed"), &SpatialCable::get_sway_speed);

	ClassDB::bind_method(D_METHOD("set_simulate", "enabled"), &SpatialCable::set_simulate);
	ClassDB::bind_method(D_METHOD("get_simulate"), &SpatialCable::get_simulate);
	ClassDB::bind_method(D_METHOD("set_gravity", "gravity"), &SpatialCable::set_gravity);
	ClassDB::bind_method(D_METHOD("get_gravity"), &SpatialCable::get_gravity);
	ClassDB::bind_method(D_METHOD("set_stiffness", "stiffness"), &SpatialCable::set_stiffness);
	ClassDB::bind_method(D_METHOD("get_stiffness"), &SpatialCable::get_stiffness);
	ClassDB::bind_method(D_METHOD("set_damping", "damping"), &SpatialCable::set_damping);
	ClassDB::bind_method(D_METHOD("get_damping"), &SpatialCable::get_damping);
	ClassDB::bind_method(D_METHOD("set_substeps", "substeps"), &SpatialCable::set_substeps);
	ClassDB::bind_method(D_METHOD("get_substeps"), &SpatialCable::get_substeps);
	ClassDB::bind_method(D_METHOD("set_solver_iterations", "iterations"), &SpatialCable::set_solver_iterations);
	ClassDB::bind_method(D_METHOD("get_solver_iterations"), &SpatialCable::get_solver_iterations);

	ClassDB::bind_method(D_METHOD("set_breakable", "breakable"), &SpatialCable::set_breakable);
	ClassDB::bind_method(D_METHOD("get_breakable"), &SpatialCable::get_breakable);
	ClassDB::bind_method(D_METHOD("set_detect_shots", "enabled"), &SpatialCable::set_detect_shots);
	ClassDB::bind_method(D_METHOD("get_detect_shots"), &SpatialCable::get_detect_shots);
	ClassDB::bind_method(D_METHOD("set_projectile_group", "group"), &SpatialCable::set_projectile_group);
	ClassDB::bind_method(D_METHOD("get_projectile_group"), &SpatialCable::get_projectile_group);
	ClassDB::bind_method(D_METHOD("set_hit_radius", "radius"), &SpatialCable::set_hit_radius);
	ClassDB::bind_method(D_METHOD("get_hit_radius"), &SpatialCable::get_hit_radius);
	ClassDB::bind_method(D_METHOD("set_collision_enabled", "enabled"), &SpatialCable::set_collision_enabled);
	ClassDB::bind_method(D_METHOD("get_collision_enabled"), &SpatialCable::get_collision_enabled);
	ClassDB::bind_method(D_METHOD("set_collision_layer", "layer"), &SpatialCable::set_collision_layer);
	ClassDB::bind_method(D_METHOD("get_collision_layer"), &SpatialCable::get_collision_layer);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &SpatialCable::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &SpatialCable::get_collision_mask);

	ClassDB::bind_method(D_METHOD("hit", "global_position"), &SpatialCable::hit);
	ClassDB::bind_method(D_METHOD("sever", "cable_index", "segment_index"), &SpatialCable::sever);
	ClassDB::bind_method(D_METHOD("reset_cable"), &SpatialCable::reset_cable);
	ClassDB::bind_method(D_METHOD("is_severed"), &SpatialCable::is_severed);
	ClassDB::bind_method(D_METHOD("rebuild"), &SpatialCable::rebuild);
	ClassDB::bind_method(D_METHOD("get_point_count"), &SpatialCable::get_point_count);
	ClassDB::bind_method(D_METHOD("get_point_position", "cable_index", "point_index"), &SpatialCable::get_point_position);

	ADD_GROUP("Endpoints", "");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "start_position"), "set_start_position", "get_start_position");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "end_position"), "set_end_position", "get_end_position");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "attach_start", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Spatial"), "set_attach_start", "get_attach_start");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "attach_end", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Spatial"), "set_attach_end", "get_attach_end");

	ADD_GROUP("Cable", "cable_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cable_count", PROPERTY_HINT_RANGE, "1,64,1"), "set_cable_count", "get_cable_count");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "cable_spacing", PROPERTY_HINT_RANGE, "-4,4,0.01"), "set_cable_spacing", "get_cable_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "cable_radius", PROPERTY_HINT_RANGE, "0.001,2,0.001"), "set_cable_radius", "get_cable_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cable_sides", PROPERTY_HINT_RANGE, "3,24,1"), "set_cable_sides", "get_cable_sides");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cable_segments", PROPERTY_HINT_RANGE, "2,128,1"), "set_cable_segments", "get_cable_segments");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "cable_slack", PROPERTY_HINT_RANGE, "0,3,0.01"), "set_slack", "get_slack");

	ADD_GROUP("Rendering", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_material", "get_material");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "cast_shadow"), "set_cast_shadow", "get_cast_shadow");

	ADD_GROUP("Wind", "wind_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "wind_enabled"), "set_wind_enabled", "get_wind_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "wind_direction"), "set_wind_direction", "get_wind_direction");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "wind_force", PROPERTY_HINT_RANGE, "0,50,0.1"), "set_wind_force", "get_wind_force");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "wind_turbulence", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_wind_turbulence", "get_wind_turbulence");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "wind_affect", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_wind_affect", "get_wind_affect");

	ADD_GROUP("Sway", "sway_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sway_enabled"), "set_sway_enabled", "get_sway_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "sway_amount", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_sway_amount", "get_sway_amount");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "sway_speed", PROPERTY_HINT_RANGE, "0,10,0.01"), "set_sway_speed", "get_sway_speed");

	ADD_GROUP("Simulation", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "simulate"), "set_simulate", "get_simulate");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "gravity", PROPERTY_HINT_RANGE, "-50,50,0.1"), "set_gravity", "get_gravity");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "stiffness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_stiffness", "get_stiffness");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "damping", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_damping", "get_damping");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "substeps", PROPERTY_HINT_RANGE, "1,8,1"), "set_substeps", "get_substeps");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "solver_iterations", PROPERTY_HINT_RANGE, "1,32,1"), "set_solver_iterations", "get_solver_iterations");

	ADD_GROUP("Breaking", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "breakable"), "set_breakable", "get_breakable");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "detect_shots"), "set_detect_shots", "get_detect_shots");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "projectile_group"), "set_projectile_group", "get_projectile_group");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "hit_radius", PROPERTY_HINT_RANGE, "0,5,0.01"), "set_hit_radius", "get_hit_radius");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collision_enabled"), "set_collision_enabled", "get_collision_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_layer", "get_collision_layer");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");

	ADD_SIGNAL(MethodInfo("cable_hit", PropertyInfo(Variant::VECTOR3, "position"), PropertyInfo(Variant::INT, "cable_index")));
	ADD_SIGNAL(MethodInfo("cable_severed", PropertyInfo(Variant::INT, "cable_index"), PropertyInfo(Variant::INT, "segment_index"), PropertyInfo(Variant::VECTOR3, "position")));
}
