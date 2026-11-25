/**************************************************************************/
/*  tuna_edit_cell_bounds.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             TUNA ENGINE                                */
/**************************************************************************/

#ifndef TUNA_EDIT_CELL_BOUNDS_H
#define TUNA_EDIT_CELL_BOUNDS_H

#include "core/set.h"
#include "scene/3d/spatial.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

// Creation-Kit style cell borders for --tuna-edit.
//
// Draws the CellWorld grid around the camera: a floor rectangle per cell plus corner posts,
// with cells that actually exist in the world picked out from empty ground and the cell the
// camera is standing in highlighted again.
//
// CellWorld and Cell are reached by class name and bound properties (cell_size, cell_x,
// cell_z), so main/ keeps no build dependency on modules/cell_system.
//
// NOTE: CellWorld's grid is its own. CellTerrain runs a different cell_size, and indices are
// never interchangeable between them -- everything here is derived from CellWorld alone.
class TunaEditCellBounds : public Spatial {
	GDCLASS(TunaEditCellBounds, Spatial);

	bool enabled;
	int radius; // in cells, each side of the camera

	// What the last rebuild was drawn for, so the grid is only re-emitted when it would differ.
	int drawn_cx;
	int drawn_cz;
	bool drawn_valid;
	bool reported; // one diagnostic line per enable, not one per frame

	RID overlay_immediate;
	RID overlay_instance;
	Ref<SpatialMaterial> overlay_material;
	Ref<ImageTexture> stripes;

	Spatial *_find_cell_world() const;
	static Spatial *_find_by_class(Node *p_node, const String &p_class);
	void _ensure_overlay();
	void _rebuild(Spatial *p_world, int p_cx, int p_cz);
	void _emit_band(const Vector3 &p_from, const Vector3 &p_to, float p_width);
	void _hide();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_enabled(bool p_enabled);
	bool is_enabled() const { return enabled; }

	void set_radius(int p_radius);
	int get_radius() const { return radius; }

	TunaEditCellBounds();
	~TunaEditCellBounds();
};

#endif // TUNA_EDIT_CELL_BOUNDS_H
