#include "cell_item_body.h"

#include "cell_player_body.h"
#include "core/engine.h"
#include "core/io/resource_loader.h"
#include "scene/3d/collision_shape.h"
#include "scene/3d/visual_instance.h"
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
	if (_shape) {
		_shape->queue_delete();
		_shape = nullptr;
	}
	if (_visual) {
		_visual->queue_delete();
		_visual = nullptr;
	}
	_built = false;
}

static void _collect_visual_aabb(Node *p_node, const Transform &p_parent_xf, AABB *r_acc, bool *r_found) {
	Transform xf = p_parent_xf;
	Spatial *sp = Object::cast_to<Spatial>(p_node);
	if (sp) {
		xf = p_parent_xf * sp->get_transform();
	}

	VisualInstance *vi = Object::cast_to<VisualInstance>(p_node);
	if (vi) {
		AABB local = vi->get_aabb();
		if (local.size != Vector3()) {
			AABB world = xf.xform(local);
			if (!*r_found) {
				*r_acc = world;
				*r_found = true;
			} else {
				*r_acc = r_acc->merge(world);
			}
		}
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_visual_aabb(p_node->get_child(i), xf, r_acc, r_found);
	}
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

	// Auto collision: one box sized to the visual bounds (falls back to a small
	// cube when the scene has no VisualInstance or none was instanced).
	AABB bounds;
	bool found = false;
	if (_visual) {
		_collect_visual_aabb(_visual, Transform(), &bounds, &found);
	}
	if (!found || bounds.size == Vector3()) {
		bounds = AABB(Vector3(-0.25, -0.25, -0.25), Vector3(0.5, 0.5, 0.5));
	}

	Ref<BoxShape> box;
	box.instance();
	box->set_extents(bounds.size * 0.5f);

	CollisionShape *cs = memnew(CollisionShape);
	cs->set_name("ItemShape");
	cs->set_shape(box);
	Transform cst;
	cst.origin = bounds.position + bounds.size * 0.5f;
	cs->set_transform(cst);
	add_child(cs);
	_shape = cs;

	_apply_item_props();
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
	ClassDB::bind_method(D_METHOD("get_item_id"), &CellItemBody::get_item_id);
	ClassDB::bind_method(D_METHOD("push", "impulse"), &CellItemBody::push);
	ClassDB::bind_method(D_METHOD("pickup"), &CellItemBody::pickup);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "item", PROPERTY_HINT_RESOURCE_TYPE, "ItemResource"), "set_item", "get_item");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_build"), "set_auto_build", "get_auto_build");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "push_strength", PROPERTY_HINT_RANGE, "0,10,0.05,or_greater"), "set_push_strength", "get_push_strength");

	ADD_SIGNAL(MethodInfo("picked_up", PropertyInfo(Variant::OBJECT, "item", PROPERTY_HINT_RESOURCE_TYPE, "Resource")));
}
