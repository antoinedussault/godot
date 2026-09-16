#include "cell_item_body.h"

#include "cell_player_body.h"
#include "core/engine.h"
#include "core/io/resource_loader.h"
#include "scene/3d/collision_shape.h"
#include "scene/3d/mesh_instance.h"
#include "scene/3d/visual_instance.h"
#include "scene/resources/convex_polygon_shape.h"
#include "scene/resources/mesh.h"
#include "scene/resources/box_shape.h"
#include "scene/resources/packed_scene.h"
#include "servers/physics_server.h"

// Impulse tuning: per contact-frame impulse is
//   dir * player_speed * push_strength * step * PUSH_K
// which behaves like a bounded continuous acceleration while the player leans in.
static const float PUSH_K = 6.0f;

CellItemBody::CellItemBody() {
	set_contact_monitor(true);
	set_max_contacts_reported(8);
	set_can_sleep(true);
}

void CellItemBody::_clear_build() {
	for (int i = 0; i < _shapes.size(); i++) {
		if (_shapes[i]) {
			_shapes.write[i]->queue_delete();
		}
	}
	_shapes.clear();
	if (_visual) {
		_visual->queue_delete();
		_visual = nullptr;
	}
	_built = false;
}

// Gathers actual mesh vertices in body-local space.
//
// Merging each child's AABB instead (what this used to do) inflates the result
// whenever a child is rotated: the AABB of a rotated box is bigger than the box.
// A long thin item lying at 45 degrees got a noticeably oversized collider.
// Vertices transform exactly, so the bounds come out tight.
static void _collect_visual_points(Node *p_node, const Transform &p_parent_xf, Vector<Vector3> *r_points, AABB *r_fallback, bool *r_found_fallback) {
	Transform xf = p_parent_xf;
	Spatial *sp = Object::cast_to<Spatial>(p_node);
	if (sp) {
		xf = p_parent_xf * sp->get_transform();
	}

	MeshInstance *mi = Object::cast_to<MeshInstance>(p_node);
	if (mi && mi->get_mesh().is_valid()) {
		Ref<Mesh> mesh = mi->get_mesh();
		for (int si = 0; si < mesh->get_surface_count(); si++) {
			Array arrays = mesh->surface_get_arrays(si);
			if (arrays.size() <= Mesh::ARRAY_VERTEX) {
				continue;
			}
			PoolVector<Vector3> verts = arrays[Mesh::ARRAY_VERTEX];
			PoolVector<Vector3>::Read r = verts.read();
			for (int i = 0; i < verts.size(); i++) {
				r_points->push_back(xf.xform(r[i]));
			}
		}
	} else {
		// Non-mesh visuals (particles, sprites) only expose an AABB.
		VisualInstance *vi = Object::cast_to<VisualInstance>(p_node);
		if (vi) {
			AABB local = vi->get_aabb();
			if (local.size != Vector3()) {
				AABB world = xf.xform(local);
				*r_fallback = *r_found_fallback ? r_fallback->merge(world) : world;
				*r_found_fallback = true;
			}
		}
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_visual_points(p_node->get_child(i), xf, r_points, r_fallback, r_found_fallback);
	}
}

static void _collect_authored_shapes(Node *p_node, const Transform &p_parent_xf, Vector<Ref<Shape> > *r_shapes, Vector<Transform> *r_xforms) {
	Transform xf = p_parent_xf;
	Spatial *sp = Object::cast_to<Spatial>(p_node);
	if (sp) {
		xf = p_parent_xf * sp->get_transform();
	}

	CollisionShape *cs = Object::cast_to<CollisionShape>(p_node);
	if (cs && cs->get_shape().is_valid()) {
		r_shapes->push_back(cs->get_shape());
		r_xforms->push_back(xf);
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_authored_shapes(p_node->get_child(i), xf, r_shapes, r_xforms);
	}
}

// Reduces a point cloud to at most p_max points that still span its extremes.
//
// Box3D caps a convex hull well below what a render mesh carries -- its edge
// limit works out to roughly 44 vertices -- and handing it more makes hull
// construction fail outright, leaving the item with NO collision. Sampling the
// furthest point along a spread of directions keeps the silhouette while
// staying inside that budget.
static Vector<Vector3> _reduce_to_hull_points(const Vector<Vector3> &p_points, int p_max) {
	if (p_points.size() <= p_max) {
		return p_points;
	}

	Vector3 center;
	for (int i = 0; i < p_points.size(); i++) {
		center += p_points[i];
	}
	center /= (real_t)p_points.size();

	Vector<Vector3> out;
	const real_t golden = Math_PI * (3.0 - Math::sqrt(5.0));
	for (int d = 0; d < p_max; d++) {
		// Fibonacci sphere: evenly spread directions, no clustering at the poles.
		const real_t y = 1.0 - (real_t)d / (real_t)(p_max - 1) * 2.0;
		const real_t radius = Math::sqrt(MAX((real_t)0.0, (real_t)1.0 - y * y));
		const real_t theta = golden * (real_t)d;
		const Vector3 dir(Math::cos(theta) * radius, y, Math::sin(theta) * radius);

		int best = -1;
		real_t best_dot = -1e20;
		for (int i = 0; i < p_points.size(); i++) {
			const real_t dot = (p_points[i] - center).dot(dir);
			if (dot > best_dot) {
				best_dot = dot;
				best = i;
			}
		}
		if (best >= 0 && out.find(p_points[best]) == -1) {
			out.push_back(p_points[best]);
		}
	}
	return out;
}

void CellItemBody::_apply_item_props() {
	if (item.is_null()) {
		return;
	}
	Variant w = item->get("weight");
	if (w.get_type() == Variant::REAL || w.get_type() == Variant::INT) {
		set_mass(MAX(0.05f, (float)w));
	}
}

void CellItemBody::_rebuild() {
	_clear_build();

	if (item.is_null() || !auto_build) {
		_apply_item_props();
		return;
	}

	String scene_path;
	Variant sp = item->get("scene_path");
	if (sp.get_type() == Variant::STRING) {
		scene_path = sp;
	}

	if (!scene_path.empty() && ResourceLoader::exists(scene_path)) {
		Ref<PackedScene> ps = ResourceLoader::load(scene_path);
		if (ps.is_valid()) {
			Node *inst = ps->instance();
			if (inst) {
				inst->set_name("ItemVisual");
				add_child(inst);
				_visual = inst;
			}
		}
	}

	// Collision. Shapes authored inside the item's own scene win; only if there
	// are none do we generate something.
	if (_adopt_authored_shapes() == 0) {
		_build_generated_shape();
	}

	_built = true;
}

void CellItemBody::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_rebuild();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			_clear_build();
		} break;
	}
}

void CellItemBody::_direct_state_changed(Object *p_state) {
	RigidBody::_direct_state_changed(p_state);

	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	PhysicsDirectBodyState *state = Object::cast_to<PhysicsDirectBodyState>(p_state);
	if (!state) {
		return;
	}

	int count = state->get_contact_count();
	for (int i = 0; i < count; i++) {
		Object *obj = state->get_contact_collider_object(i);
		CellPlayerBody *player = Object::cast_to<CellPlayerBody>(obj);
		if (!player) {
			continue;
		}

		Vector3 pv = player->get_linear_velocity();
		pv.y = 0.0f;
		float speed = pv.length();
		if (speed < 0.05f) {
			continue;
		}
		Vector3 dir = pv / speed;

		// Impulse independent of mass, so heavier items shove less (Δv = J / mass).
		// Self-limiting: once the item outruns the player they stop touching.
		float mag = speed * push_strength * state->get_step() * PUSH_K;
		state->apply_central_impulse(dir * mag);
	}
}

int CellItemBody::_adopt_authored_shapes() {
	if (!_visual) {
		return 0;
	}

	Vector<Ref<Shape> > shapes;
	Vector<Transform> xforms;
	_collect_authored_shapes(_visual, Transform(), &shapes, &xforms);

	// Godot only honours a CollisionShape that is a DIRECT child of the body, so
	// one nested inside the instanced scene is inert. Mirror them onto this node;
	// the originals stay where they are, harmless.
	for (int i = 0; i < shapes.size(); i++) {
		CollisionShape *cs = memnew(CollisionShape);
		cs->set_name(vformat("ItemShape%d", i));
		cs->set_shape(shapes[i]);
		cs->set_transform(xforms[i]);
		add_child(cs);
		_shapes.push_back(cs);
	}
	return shapes.size();
}

void CellItemBody::_build_generated_shape() {
	if (collision_mode == COLLISION_NONE) {
		return;
	}

	Vector<Vector3> points;
	AABB fallback;
	bool have_fallback = false;
	if (_visual) {
		_collect_visual_points(_visual, Transform(), &points, &fallback, &have_fallback);
	}

	if (points.empty() && !have_fallback) {
		fallback = AABB(Vector3(-0.25, -0.25, -0.25), Vector3(0.5, 0.5, 0.5));
		have_fallback = true;
	}

	CollisionShape *cs = memnew(CollisionShape);
	cs->set_name("ItemShape");

	if (collision_mode == COLLISION_CONVEX && points.size() >= 4) {
		// 44 is Box3D's practical ceiling: its half-edge limit of 256 with
		// E = 3V - 6 lands there, and overshooting makes hull construction fail
		// outright, leaving the item with NO collision. Bullet tolerates more,
		// but staying inside the tightest backend keeps items consistent
		// whichever engine the project is set to.
		const int MAX_HULL_POINTS = 44;
		Vector<Vector3> hull = _reduce_to_hull_points(points, MAX_HULL_POINTS);

		// ConvexPolygonShape wants a PoolVector, not a Vector.
		PoolVector<Vector3> hull_pool;
		hull_pool.resize(hull.size());
		{
			PoolVector<Vector3>::Write w = hull_pool.write();
			for (int i = 0; i < hull.size(); i++) {
				w[i] = hull[i];
			}
		}

		Ref<ConvexPolygonShape> convex;
		convex.instance();
		convex->set_points(hull_pool);
		cs->set_shape(convex);
		add_child(cs);
		_shapes.push_back(cs);
		return;
	}

	AABB bounds;
	if (!points.empty()) {
		bounds = AABB(points[0], Vector3());
		for (int i = 1; i < points.size(); i++) {
			bounds.expand_to(points[i]);
		}
		if (have_fallback) {
			bounds = bounds.merge(fallback);
		}
	} else {
		bounds = fallback;
	}
	if (bounds.size == Vector3()) {
		bounds = AABB(Vector3(-0.25, -0.25, -0.25), Vector3(0.5, 0.5, 0.5));
	}

	Ref<BoxShape> box;
	box.instance();
	box->set_extents(bounds.size * 0.5f);
	Transform cst;
	cst.origin = bounds.position + bounds.size * 0.5f;
	cs->set_shape(box);
	cs->set_transform(cst);
	add_child(cs);
	_shapes.push_back(cs);
}

void CellItemBody::set_item(const Ref<Resource> &p_item) {
	if (item == p_item) {
		return;
	}
	item = p_item;
	if (is_inside_tree()) {
		_rebuild();
	}
}

Ref<Resource> CellItemBody::get_item() const {
	return item;
}

void CellItemBody::set_push_strength(float p_strength) {
	push_strength = MAX(0.0f, p_strength);
}

float CellItemBody::get_push_strength() const {
	return push_strength;
}

void CellItemBody::set_auto_build(bool p_enabled) {
	if (auto_build == p_enabled) {
		return;
	}
	auto_build = p_enabled;
	if (is_inside_tree()) {
		_rebuild();
	}
}

void CellItemBody::set_collision_mode(CollisionMode p_mode) {
	if (collision_mode == p_mode) {
		return;
	}
	collision_mode = p_mode;
	if (is_inside_tree()) {
		_rebuild();
	}
}

CellItemBody::CollisionMode CellItemBody::get_collision_mode() const {
	return collision_mode;
}

bool CellItemBody::get_auto_build() const {
	return auto_build;
}

String CellItemBody::get_item_id() const {
	if (item.is_null()) {
		return String();
	}
	Variant id = item->get("id");
	if (id.get_type() == Variant::STRING) {
		return id;
	}
	return String();
}

void CellItemBody::push(const Vector3 &p_impulse) {
	PhysicsServer::get_singleton()->body_apply_central_impulse(get_rid(), p_impulse);
}

void CellItemBody::pickup() {
	emit_signal("picked_up", item);
	call_deferred("queue_free");
}

void CellItemBody::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_item", "item"), &CellItemBody::set_item);
	ClassDB::bind_method(D_METHOD("get_item"), &CellItemBody::get_item);
	ClassDB::bind_method(D_METHOD("set_push_strength", "strength"), &CellItemBody::set_push_strength);
	ClassDB::bind_method(D_METHOD("get_push_strength"), &CellItemBody::get_push_strength);
	ClassDB::bind_method(D_METHOD("set_auto_build", "enabled"), &CellItemBody::set_auto_build);
	ClassDB::bind_method(D_METHOD("get_auto_build"), &CellItemBody::get_auto_build);
	ClassDB::bind_method(D_METHOD("set_collision_mode", "mode"), &CellItemBody::set_collision_mode);
	ClassDB::bind_method(D_METHOD("get_collision_mode"), &CellItemBody::get_collision_mode);

	BIND_ENUM_CONSTANT(COLLISION_BOX);
	BIND_ENUM_CONSTANT(COLLISION_CONVEX);
	BIND_ENUM_CONSTANT(COLLISION_NONE);
	ClassDB::bind_method(D_METHOD("get_item_id"), &CellItemBody::get_item_id);
	ClassDB::bind_method(D_METHOD("push", "impulse"), &CellItemBody::push);
	ClassDB::bind_method(D_METHOD("pickup"), &CellItemBody::pickup);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "item", PROPERTY_HINT_RESOURCE_TYPE, "ItemResource"), "set_item", "get_item");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_build"), "set_auto_build", "get_auto_build");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mode", PROPERTY_HINT_ENUM, "Box,Convex Hull,None"), "set_collision_mode", "get_collision_mode");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "push_strength", PROPERTY_HINT_RANGE, "0,10,0.05,or_greater"), "set_push_strength", "get_push_strength");

	ADD_SIGNAL(MethodInfo("picked_up", PropertyInfo(Variant::OBJECT, "item", PROPERTY_HINT_RESOURCE_TYPE, "Resource")));
}
