#ifndef CELL_WORLD_H
#define CELL_WORLD_H

#include "scene/3d/spatial.h"

// One grid cell. Owns the static objects whose position falls in its footprint.
class Cell : public Spatial {
	GDCLASS(Cell, Spatial);

	int cell_x = 0;
	int cell_z = 0;

protected:
	static void _bind_methods();

public:
	void set_cell_x(int v) { cell_x = v; }
	int get_cell_x() const { return cell_x; }
	void set_cell_z(int v) { cell_z = v; }
	int get_cell_z() const { return cell_z; }

	Cell() {}
};

// Owns a grid of Cell nodes plus one container for movable objects. Sorts each
// static object into the Cell whose footprint it sits in; movable bodies go to
// a DynamicObjects container instead.
//
// Streaming: call stream_around(cx, cz) every time the player changes cell.
// Cells within load_radius are shown and process; cells outside are hidden and
// paused. This mirrors how the terrain manager streams tiles.
class CellWorld : public Spatial {
	GDCLASS(CellWorld, Spatial);

	float cell_size = 64.0f;
	int load_radius = 2;
	String dynamic_name = "DynamicObjects";

	bool _has_focus = false;
	int _focus_cx = 0;
	int _focus_cz = 0;

	static bool _is_movable(Node *n);
	Spatial *_get_or_create_dynamic(Node *p_owner);
	void _set_cell_active(Cell *c, bool active);

protected:
	static void _bind_methods();

public:
	void set_cell_size(float s) { cell_size = s; }
	float get_cell_size() const { return cell_size; }

	void set_load_radius(int r) { load_radius = MAX(0, r); }
	int get_load_radius() const { return load_radius; }

	Cell *get_or_create_cell(int cx, int cz, Node *p_owner = nullptr);
	Cell *get_cell(int cx, int cz) const;
	void assign_node(Node *n, Node *p_owner = nullptr);
	void sort_existing(Node *p_owner = nullptr);

	// Streaming: show cells near (cx,cz), hide the rest.
	void stream_around(int cx, int cz);

	CellWorld() {}
};

#endif // CELL_WORLD_H
