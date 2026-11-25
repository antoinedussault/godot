/**************************************************************************/
/*  editor_window_wrapper.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#ifndef EDITOR_WINDOW_WRAPPER_H
#define EDITOR_WINDOW_WRAPPER_H

#include "core/os/os.h"
#include "scene/gui/panel_container.h"
#include "scene/main/viewport.h"

// Hosts an editor Control in a separate OS window.
//
// Godot 3.x is single-window by construction, so this leans on the OS::window_*
// API: the wrapper is a Viewport that presents into the extra window, and the
// wrapped Control is re-parented into it. Calling dock_back() restores the
// Control to the exact parent and sibling index it came from.
class EditorWindowWrapper : public Viewport {
	GDCLASS(EditorWindowWrapper, Viewport);

	int window_id;

	Control *wrapped;
	Node *original_parent;
	int original_index;

	PanelContainer *holder;
	String window_title;

	void _update_theme();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static bool is_supported();

	void set_window_title(const String &p_title);

	bool make_floating(Control *p_control, const Rect2 &p_rect);
	void dock_back();
	bool is_floating() const { return window_id != OS::INVALID_WINDOW_ID; }

	Control *get_wrapped() const { return wrapped; }

	EditorWindowWrapper();
};

#endif // EDITOR_WINDOW_WRAPPER_H
