/**************************************************************************/
/*  tuna_edit_bridge.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             TUNA ENGINE                                */
/**************************************************************************/

#include "tuna_edit_bridge.h"

#include "core/os/keyboard.h"
#include "scene/main/scene_tree.h"
#include "scene/3d/camera.h"
#include "scene/main/viewport.h"
#include "core/print_string.h"

#ifdef WINDOWS_ENABLED
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

void TunaEditBridge::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_input", "event"), &TunaEditBridge::_input);
}

void TunaEditBridge::_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key = p_event;
	if (key.is_null() || !key->is_pressed() || key->is_echo()) {
		return;
	}

	const uint32_t code = key->get_scancode();

	// B toggles cell borders, the same key the Creation Kit uses. Handled here rather than
	// forwarded, so it responds immediately; the ack keeps the editor's menu check in step.
	if (code == KEY_B && !key->get_control() && !key->get_alt() && cell_bounds) {
		const bool on = !cell_bounds->is_enabled();
		cell_bounds->set_enabled(on);
		print_line(String("TUNA_ACK:cell_bounds ") + (on ? "1" : "0"));
		return;
	}


	// Only what an editor binds menus to. Escape is deliberately excluded: the camera uses it
	// to release a captured pointer, and that must stay local and instant.
	const bool is_function_key = (code >= KEY_F1 && code <= KEY_F12);
	if (!is_function_key && !key->get_control()) {
		return;
	}
	if (code == KEY_ESCAPE || code == KEY_CONTROL || code == KEY_SHIFT || code == KEY_ALT) {
		return;
	}

	String name;
	if (key->get_control()) {
		name += "ctrl+";
	}
	if (key->get_shift()) {
		name += "shift+";
	}
	if (key->get_alt()) {
		name += "alt+";
	}
	name += keycode_get_string(code);

	print_line("TUNA_KEY:" + name);
}

// The editor shows the camera position in the World Editor title bar, so it needs a feed.
// Throttled twice over: at most ten times a second, and only when the camera has actually
// moved -- a title that rewrites every frame is a stream of pointless traffic and taskbar
// repaints while you stand still.
void TunaEditBridge::_report_camera() {
	Viewport *viewport = get_viewport();
	Camera *camera = viewport ? viewport->get_camera() : nullptr;
	if (!camera) {
		return;
	}

	report_accumulator += get_process_delta_time();
	if (report_accumulator < 0.1f) {
		return;
	}
	report_accumulator = 0.0f;

	const Vector3 position = camera->get_global_transform().origin;
	if (position.distance_squared_to(last_reported) < 0.01f) {
		return;
	}
	last_reported = position;

	print_line("TUNA_CAM:" + rtos(position.x) + "," + rtos(position.y) + "," + rtos(position.z));
}

void TunaEditBridge::_poll_stdin() {
#ifdef WINDOWS_ENABLED
	HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
	if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
		return;
	}

	// Only a pipe is ours to drain. Run from a console with no redirection and stdin is a
	// console handle, where PeekNamedPipe fails -- in that case there is simply nothing to read.
	for (;;) {
		DWORD available = 0;
		if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
			return;
		}
		if (available == 0) {
			return;
		}

		char buffer[512];
		DWORD wanted = MIN((DWORD)sizeof(buffer), available);
		DWORD read = 0;
		if (!ReadFile(handle, buffer, wanted, &read, nullptr) || read == 0) {
			return;
		}

		for (DWORD i = 0; i < read; i++) {
			const char c = buffer[i];
			if (c == '\n') {
				String line = pending.strip_edges();
				pending = String();
				if (!line.empty()) {
					_dispatch(line);
				}
			} else if (c != '\r') {
				pending += String::chr(c);
			}
		}
	}
#endif
}

void TunaEditBridge::_dispatch(const String &p_line) {
	Vector<String> parts = p_line.split(" ", false);
	if (parts.empty()) {
		return;
	}

	const String command = parts[0];

	if (command == "cell_bounds") {
		if (!cell_bounds) {
			return;
		}
		const bool on = (parts.size() > 1) ? (parts[1].to_int() != 0) : true;
		cell_bounds->set_enabled(on);
		print_line(String("TUNA_ACK:cell_bounds ") + (on ? "1" : "0"));
		return;
	}

	if (command == "cell_bounds_radius") {
		if (!cell_bounds || parts.size() < 2) {
			return;
		}
		cell_bounds->set_radius(parts[1].to_int());
		print_line("TUNA_ACK:cell_bounds_radius " + itos(cell_bounds->get_radius()));
		return;
	}

	if (command == "debug_draw") {
		SceneTree *tree = get_tree();
		if (!tree || !tree->get_root() || parts.size() < 2) {
			return;
		}
		const int mode = CLAMP(parts[1].to_int(), 0, 3);
		tree->get_root()->set_debug_draw((Viewport::DebugDraw)mode);
		print_line("TUNA_ACK:debug_draw " + itos(mode));
		return;
	}

	print_line("TUNA_ERR:unknown command '" + command + "'");
}

void TunaEditBridge::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process(true);
			set_process_input(true);
		} break;

		case NOTIFICATION_PROCESS: {
			_poll_stdin();
			_report_camera();
		} break;
	}
}

TunaEditBridge::TunaEditBridge() {
	cell_bounds = nullptr;
	report_accumulator = 0.0f;
}
