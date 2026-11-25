/**************************************************************************/
/*  tuna_edit_selection.h                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             TUNA ENGINE                                */
/**************************************************************************/

#ifndef TUNA_EDIT_SELECTION_H
#define TUNA_EDIT_SELECTION_H

#include "core/os/input_event.h"
#include "scene/3d/spatial.h"
#include "scene/resources/material.h"

// Click-to-select for --tuna-edit.
//
// Picking goes through VisualServer::instances_cull_ray, not a physics raycast: edit mode
// freezes the physics server, and plenty of world geometry has no collider anyway.
//
// A hit is then resolved up to the item actually placed in the world. The world is authored as
// CellWorld -> Cell_<x>_<z> -> items, so clicking a rock selects the instanced rock, not some
// MeshInstance inside it. The Cell is matched by class name so main/ keeps no build dependency
// on modules/cell_system.
//
// The selection outline is drawn straight into the VisualServer as an immediate mesh, the way
// editor gizmos do. It is never a Node, so it cannot show up in the edited scene or be saved
// into it.
class TunaEditSelection : public Spatial {
	GDCLASS(TunaEditSelection, Spatial);

	ObjectID selected;

	RID overlay_immediate;
	RID overlay_instance;
	Ref<SpatialMaterial> overlay_material;

	Spatial *_pick(const Point2 &p_screen) const;
	static Spatial *_resolve_item(Spatial *p_hit);
	static AABB _item_bounds(Node *p_node, bool &r_has);

	void _ensure_overlay();
	void _draw_outline(const AABB &p_bounds);

protected:
	void _notification(int p_what);
	static void _bind_methods();

	// Reached by name through Object::call_multilevel; see TunaEditCamera for why.
	void _input(const Ref<InputEvent> &p_event);

public:
	void select(Spatial *p_node);
	void clear_selection();
	Spatial *get_selected() const;

	TunaEditSelection();
	~TunaEditSelection();
};

#endif // TUNA_EDIT_SELECTION_H
