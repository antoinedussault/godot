/**************************************************************************/
/*  tuna_edit_bridge.h                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             TUNA ENGINE                                */
/**************************************************************************/

#ifndef TUNA_EDIT_BRIDGE_H
#define TUNA_EDIT_BRIDGE_H

#include "core/os/input_event.h"
#include "core/ustring.h"
#include "main/tuna_edit_cell_bounds.h"
#include "scene/main/node.h"

// Editor -> engine command channel for --tuna-edit.
//
// The external editor owns this process, so its stdin is the cheapest channel that already
// exists: no port, no named pipe, no handshake, and it closes itself when the editor exits.
// Replies go the other way as prefixed stdout lines (see TunaEditSelection).
//
// stdin is polled without blocking rather than read on a worker thread: a thread parked in a
// blocking read cannot be woken at shutdown, which would leave the engine unable to exit
// cleanly. Polling costs one PeekNamedPipe per frame and has no teardown problem.
//
// Commands are one per line:
//   cell_bounds <0|1>       show or hide the cell border grid
//   cell_bounds_radius <n>  how many cells out to draw
class TunaEditBridge : public Node {
	GDCLASS(TunaEditBridge, Node);

	String pending; // partial line carried between polls
	TunaEditCellBounds *cell_bounds;

	float report_accumulator;
	Vector3 last_reported;

	void _poll_stdin();
	void _report_camera();
	void _dispatch(const String &p_line);

protected:
	void _notification(int p_what);
	static void _bind_methods();

	// The engine window takes keyboard focus when clicked (it has to, or captured-mode mouse
	// look is discarded), which means the editor's own menu shortcuts never reach it. Editor
	// level keys are forwarded back up the channel so the editor can act on them.
	void _input(const Ref<InputEvent> &p_event);

public:
	void set_cell_bounds(TunaEditCellBounds *p_bounds) { cell_bounds = p_bounds; }

	TunaEditBridge();
};

#endif // TUNA_EDIT_BRIDGE_H
