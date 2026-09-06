#include "cell_world.h"

#include "core/vector.h"
#include "scene/3d/physics_body.h"
#include "scene/3d/vehicle_body.h"

// ---- Cell ----
void Cell::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_cell_x", "v"), &Cell::set_cell_x);
	ClassDB::bind_method(D_METHOD("get_cell_x"), &Cell::get_cell_x);
	ClassDB::bind_method(D_METHOD("set_cell_z", "v"), &Cell::set_cell_z);
	ClassDB::bind_method(D_METHOD("get_cell_z"), &Cell::get_cell_z);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cell_x"), "set_cell_x", "get_cell_x");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cell_z"), "set_cell_z", "get_cell_z");
}

// ---- CellWorld ----
bool CellWorld::_is_movable(Node *n) {
	if (Object::cast_to<RigidBody>(n)) return true;
	if (Object::cast_to<KinematicBody>(n)) return true;
	if (Object::cast_to<VehicleBody>(n)) return true;
	if (n->is_in_group("movable")) return true;
	if (n->has_meta("movable") && (bool)n->get_meta("movable")) return true;
	return false;
}

Spatial *CellWorld::_get_or_create_dynamic(Node *p_owner) {
	for (int i = 0; i < get_child_count(); i++) {
		if (get_child(i)->get_name() == dynamic_name) {
			Spatial *s = Object::cast_to<Spatial>(get_child(i));
			if (s) return s;
		}
	}
	Spatial *d = memnew(Spatial);
	d->set_name(dynamic_name);
	add_child(d);
	if (p_owner) d->set_owner(p_owner);
	return d;
}

Cell *CellWorld::get_cell(int cx, int cz) const {
	for (int i = 0; i < get_child_count(); i++) {
		Cell *c = Object::cast_to<Cell>(get_child(i));
		if (c && c->get_cell_x() == cx && c->get_cell_z() == cz) return c;
	}
	return nullptr;
}

Cell *CellWorld::get_or_create_cell(int cx, int cz, Node *p_owner) {
	Cell *existing = get_cell(cx, cz);
	if (existing) return existing;

	Cell *c = memnew(Cell);
	c->set_name("Cell_" + itos(cx) + "_" + itos(cz));
	c->set_cell_x(cx);
	c->set_cell_z(cz);
	add_child(c);
	Transform xf;
	xf.origin = Vector3(cx * cell_size, 0.0f, cz * cell_size);
	c->set_transform(xf);
	if (p_owner) c->set_owner(p_owner);
	return c;
}

void CellWorld::assign_node(Node *n, Node *p_owner) {
	if (!n) return;

	Node *target = nullptr;
	if (_is_movable(n)) {
		target = _get_or_create_dynamic(p_owner);
	} else {
		Spatial *s = Object::cast_to<Spatial>(n);
		Vector3 pos = s ? s->get_global_transform().origin : Vector3();
		int cx = (int)Math::floor(pos.x / cell_size);
		int cz = (int)Math::floor(pos.z / cell_size);
		target = get_or_create_cell(cx, cz, p_owner);
	}
	if (!target || n->get_parent() == target) return;

	Spatial *s = Object::cast_to<Spatial>(n);
	bool has_xf = (s != nullptr);
	Transform gxf;
	if (has_xf) gxf = s->get_global_transform();

	if (n->get_parent()) n->get_parent()->remove_child(n);
	target->add_child(n);
	if (p_owner) n->set_owner(p_owner);
	if (has_xf) s->set_global_transform(gxf);
}

void CellWorld::sort_existing(Node *p_owner) {
	Vector<Node *> kids;
	for (int i = 0; i < get_child_count(); i++) {
		Node *ch = get_child(i);
		if (Object::cast_to<Cell>(ch)) continue;
		if (ch->get_name() == dynamic_name) continue;
		kids.push_back(ch);
	}
	for (int i = 0; i < kids.size(); i++) {
		assign_node(kids[i], p_owner);
	}
}

// ---- Streaming ----
void CellWorld::_set_cell_active(Cell *c, bool active) {
	if (!c) return;
	c->set_visible(active);
	// Pause/unpause the cell's subtree so hidden objects don't tick.
	c->set_process(active);
	c->set_physics_process(active);
	// Also pause children individually so their _process/_physics_process stop.
	for (int i = 0; i < c->get_child_count(); i++) {
		Node *ch = c->get_child(i);
		ch->set_process(active);
		ch->set_physics_process(active);
	}
}

void CellWorld::stream_around(int cx, int cz) {
	if (_has_focus && cx == _focus_cx && cz == _focus_cz) return;
	_focus_cx = cx;
	_focus_cz = cz;
	_has_focus = true;

	for (int i = 0; i < get_child_count(); i++) {
		Cell *c = Object::cast_to<Cell>(get_child(i));
		if (!c) continue;
		bool in_range = (ABS(c->get_cell_x() - cx) <= load_radius &&
						 ABS(c->get_cell_z() - cz) <= load_radius);
		_set_cell_active(c, in_range);
	}
}

void CellWorld::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_cell_size", "s"), &CellWorld::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &CellWorld::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_load_radius", "r"), &CellWorld::set_load_radius);
	ClassDB::bind_method(D_METHOD("get_load_radius"), &CellWorld::get_load_radius);
	ClassDB::bind_method(D_METHOD("assign_node", "node", "owner"), &CellWorld::assign_node, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("sort_existing", "owner"), &CellWorld::sort_existing, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("stream_around", "cx", "cz"), &CellWorld::stream_around);

	ADD_PROPERTY(PropertyInfo(Variant::REAL, "cell_size"), "set_cell_size", "get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "load_radius"), "set_load_radius", "get_load_radius");
}
