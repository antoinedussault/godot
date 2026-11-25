#ifndef CELL_WORLD_H
#define CELL_WORLD_H

#include "scene/3d/spatial.h"

// DEPRECATED shells, kept only so scenes authored before the GameWorld overhaul
// still load.
//
// Objects used to be reparented into Cell nodes ("Sort Into Cells") and a
// CellWorld then showed and hid those nodes. That was never streaming - a
// hidden cell kept its colliders in the physics server and freed nothing - and
// it overwrote whatever hierarchy the scene had been organised into.
//
// GameWorld now partitions objects itself, by position, without touching the
// hierarchy (see game_world.h). To it, both classes below are plain folders: it
// walks straight through them, so an old scene full of Cell_x_z nodes streams
// correctly as-is. GameWorld::migrate_legacy() flattens them away for good.
class Cell : public Spatial {
	GDCLASS(Cell, Spatial);

protected:
	static void _bind_methods() {}

public:
	Cell() {}
};

class CellWorld : public Spatial {
	GDCLASS(CellWorld, Spatial);

	// Still read from old scenes, only so GameWorld can adopt it.
	int load_radius = 2;

protected:
	static void _bind_methods();

public:
	void set_load_radius(int r) { load_radius = MAX(0, r); }
	int get_load_radius() const { return load_radius; }
	virtual String get_configuration_warning() const;
	CellWorld() {}
};

#endif // CELL_WORLD_H
