/**************************************************************************/
/*  editor_window_wrapper.cpp                                             */
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

#include "editor_window_wrapper.h"

#include "core/os/os.h"
#include "editor_node.h"
#include "scene/resources/style_box.h"

void EditorWindowWrapper::_bind_methods() {
	ADD_SIGNAL(MethodInfo("docked_back"));
}

bool EditorWindowWrapper::is_supported() {
	return OS::get_singleton()->has_multiple_windows();
}

void EditorWindowWrapper::set_window_title(const String &p_title) {
	window_title = p_title;
}

// The wrapper is not a Control, so it breaks the theme owner chain the wrapped
// editor UI relies on. Re-seed it from the editor's own theme.
void EditorWindowWrapper::_update_theme() {
	if (!EditorNode::get_singleton()) {
		return;
	}

	Control *gui_base = EditorNode::get_singleton()->get_gui_base();
	Ref<Theme> editor_theme = gui_base->get_theme();
	if (editor_theme.is_null()) {
		return;
	}
	holder->set_theme(editor_theme);

	// The default panel style insets its child by the theme's content margins, which
	// would leave a border around the hosted editor. Use a flat background with no
	// margins so the content fills the window edge to edge.
	Ref<StyleBoxFlat> flat;
	flat.instance();
	flat->set_bg_color(gui_base->get_color("base_color", "Editor"));
	flat->set_default_margin(MARGIN_LEFT, 0);
	flat->set_default_margin(MARGIN_RIGHT, 0);
	flat->set_default_margin(MARGIN_TOP, 0);
	flat->set_default_margin(MARGIN_BOTTOM, 0);
	holder->add_style_override("panel", flat);
}

bool EditorWindowWrapper::make_floating(Control *p_control, const Rect2 &p_rect) {
	ERR_FAIL_COND_V(!p_control, false);
	ERR_FAIL_COND_V(is_floating(), false);
	ERR_FAIL_COND_V(!p_control->get_parent(), false);
	ERR_FAIL_COND_V_MSG(!is_supported(), false, "This platform does not support additional OS windows.");

	// The editor theme may not have existed when this wrapper entered the tree.
	_update_theme();

	window_id = OS::get_singleton()->window_create(window_title, p_rect);
	if (window_id == OS::INVALID_WINDOW_ID) {
		return false;
	}

	const Size2 window_size = OS::get_singleton()->window_get_size(window_id);
	set_size(window_size);
	set_attach_to_window(window_id);
	set_attach_to_screen_rect(Rect2(Point2(), window_size));
	set_update_mode(Viewport::UPDATE_ALWAYS);

	// Remember where the control came from so it can be put back exactly.
	original_parent = p_control->get_parent();
	original_index = p_control->get_index();
	wrapped = p_control;

	original_parent->remove_child(p_control);
	holder->add_child(p_control);
	p_control->set_anchors_and_margins_preset(Control::PRESET_WIDE);
	p_control->show();

	return true;
}

void EditorWindowWrapper::dock_back() {
	if (!is_floating()) {
		return;
	}

	if (wrapped) {
		holder->remove_child(wrapped);
		original_parent->add_child(wrapped);
		original_parent->move_child(wrapped, original_index);
		wrapped->set_anchors_and_margins_preset(Control::PRESET_WIDE);
		wrapped = NULL;
	}
	original_parent = NULL;

	set_update_mode(Viewport::UPDATE_DISABLED);
	set_attach_to_screen_rect(Rect2());
	set_attach_to_window(OS::MAIN_WINDOW_ID);

	OS::get_singleton()->window_destroy(window_id);
	window_id = OS::INVALID_WINDOW_ID;
}

void EditorWindowWrapper::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_update_theme();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			dock_back();
		} break;

		case NOTIFICATION_PROCESS: {
			if (!is_floating()) {
				break;
			}
			if (OS::get_singleton()->window_get_close_requested(window_id)) {
				OS::get_singleton()->window_clear_close_request(window_id);
				dock_back();
				emit_signal("docked_back");
				break;
			}

			// The OS window can be resized by the user at any time; there is no
			// resize notification routed to this viewport yet, so poll it.
			const Size2 window_size = OS::get_singleton()->window_get_size(window_id);
			if (window_size.width > 0 && window_size.height > 0 && window_size != get_size()) {
				set_size(window_size);
				set_attach_to_screen_rect(Rect2(Point2(), window_size));
			}
		} break;
	}
}

EditorWindowWrapper::EditorWindowWrapper() {
	window_id = OS::INVALID_WINDOW_ID;
	wrapped = NULL;
	original_parent = NULL;
	original_index = 0;
	window_title = "Godot";

	set_disable_3d(true);
	set_usage(Viewport::USAGE_2D);
	set_update_mode(Viewport::UPDATE_DISABLED);

	// This is a top-level viewport backing an OS window, so it must handle input the
	// way the scene tree's root does. With the Viewport default (locally), a Control
	// calling accept_event() would only set this viewport's local flag, leaving the
	// tree-global one clear -- so SceneTree would still run the unhandled pass and
	// editor menu shortcuts would fire a second time on top of the Control that had
	// already consumed the key (a Ctrl+V pasting twice, and so on).
	set_handle_input_locally(false);

	holder = memnew(PanelContainer);
	holder->set_anchors_and_margins_preset(Control::PRESET_WIDE);
	add_child(holder);

	set_process(true);
}
