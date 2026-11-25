/**************************************************************************/
/*  tuna_edit_gizmo.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             TUNA ENGINE                                */
/**************************************************************************/

#ifndef TUNA_EDIT_GIZMO_H
#define TUNA_EDIT_GIZMO_H

#include "core/os/input_event.h"
#include "main/tuna_edit_selection.h"
#include "scene/3d/spatial.h"
#include "scene/resources/material.h"

// Move / rotate / scale handles for the selected object, in --tuna-edit.
//
// Drawn straight into the VisualServer as an immediate mesh, like the other overlays, so it is
// never a Node and can never be saved into the edited scene.
//
// Input ordering matters: SceneTree walks the input group in reverse, so this node is added
// AFTER the selection node and therefore sees clicks first. When a handle is grabbed the event
// is marked handled, which is what stops a drag from also re-picking whatever is behind it.
class TunaEditGizmo : public Spatial {
	GDCLASS(TunaEditGizmo, Spatial);

public:
	enum Mode {
		MODE_MOVE,
		MODE_ROTATE,
		MODE_SCALE,
	};

private:
	enum Axis {
		AXIS_NONE = -1,
		AXIS_X = 0,
		AXIS_Y = 1,
		AXIS_Z = 2,
	};

	TunaEditSelection *selection;

	Mode mode;
	Axis hover_axis;
	Axis drag_axis;
	bool dragging;

	// Captured when the drag starts, so every frame is measured against the grab rather than
	// accumulating error from frame to frame.
	Transform drag_start_transform;
	float drag_start_value;
	float gizmo_scale;

	RID overlay_immediate;
	RID overlay_instance;
	Ref<SpatialMaterial> overlay_material;

	Spatial *_target() const;
	Camera *_camera() const;

	// Screen-constant sizing: handles must stay grabbable whatever the distance.
	float _scale_for(const Vector3 &p_origin) const;

	Vector3 _axis_vector(Axis p_axis) const;
	Axis _pick_axis(const Point2 &p_screen, float p_scale, const Transform &p_transform) const;

	bool _axis_value(const Point2 &p_screen, Axis p_axis, const Transform &p_transform,
			float p_scale, float &r_value) const;

	void _begin_drag(Axis p_axis, const Point2 &p_screen);
	void _update_drag(const Point2 &p_screen);
	void _end_drag();

	void _ensure_overlay();
	void _redraw();
	void _hide();

protected:
	void _notification(int p_what);
	static void _bind_methods();

	// Reached by name through ClassDB, as with the other edit-mode nodes.
	void _input(const Ref<InputEvent> &p_event);

public:
	void set_selection(TunaEditSelection *p_selection) { selection = p_selection; }

	void set_mode(Mode p_mode);
	Mode get_mode() const { return mode; }

	TunaEditGizmo();
	~TunaEditGizmo();
};

#endif // TUNA_EDIT_GIZMO_H
