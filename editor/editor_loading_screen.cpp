/**************************************************************************/
/*  editor_loading_screen.cpp                                             */
/**************************************************************************/

#include "editor_loading_screen.h"

#include "core/io/resource_loader.h"
#include "core/os/input_event.h"
#include "core/os/os.h"
#include "core/project_settings.h"
#include "core/version.h"
#include "editor_file_system.h"
#include "editor_node.h"
#include "editor_scale.h"
#include "main/app_icon.gen.h"
#include "progress_dialog.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/center_container.h"
#include "scene/gui/label.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/texture_rect.h"
#include "scene/main/viewport.h"
#include "scene/resources/texture.h"

EditorLoadingScreen *EditorLoadingScreen::singleton = nullptr;

void EditorLoadingScreen::_set_stage(Stage p_stage) {
	stage = p_stage;

	const Color font_color = get_color("font_color", "Label");
	for (int i = 0; i < STAGE_MAX; i++) {
		Color c = font_color;
		if (i == stage) {
			c = get_color("accent_color", "Editor");
		} else if (i > stage) {
			c.a *= 0.4;
		} else {
			c.a *= 0.75;
		}
		stage_labels[i]->add_color_override("font_color", c);
	}
}

void EditorLoadingScreen::_update_display() {
	String text;
	String count;
	float ratio = -1;

	EditorFileSystem *efs = EditorFileSystem::get_singleton();

	if (task_active) {
		text = task_state.empty() ? task_label : task_label + ": " + task_state;
		if (task_steps > 0) {
			ratio = float(task_step_value) / task_steps;
			count = itos(task_step_value) + " / " + itos(task_steps);
		}
	} else if (stage == STAGE_SCAN) {
		const int scanned = efs->get_scan_file_count();
		const int expected = efs->get_scan_file_estimate();
		text = efs->get_scan_current_file();
		if (expected > 0) {
			// Last session's file count. Far steadier than the scan's own ratio,
			// which gives every directory the same weight whatever its size.
			ratio = float(scanned) / MAX(expected, scanned);
			count = vformat(TTR("%d / %d files"), scanned, MAX(expected, scanned));
		} else {
			ratio = efs->get_scanning_progress();
			count = vformat(TTR("%d files"), scanned);
		}
	} else if (stage == STAGE_IMPORT) {
		text = TTR("Checking imports and building the file tree...");
	} else if (stage == STAGE_SCENES && scene_idx < scenes.size()) {
		text = scenes[scene_idx].path;
		ratio = 0;
		if (scene_prepared) {
			if (dep_idx < deps.size()) {
				text = deps[dep_idx];
			}
			ratio = float(dep_idx) / (deps.size() + 1);
			count = itos(dep_idx) + " / " + itos(deps.size());
		}
		ratio = (scene_idx + ratio) / scenes.size();
	} else {
		text = TTR("Preparing the editor...");
		ratio = 1;
	}

	detail->set_text(text);
	counter->set_text(count);
	if (ratio >= 0) { // Steps that cannot report one keep the last value.
		progress->set_value(CLAMP(ratio, 0.0f, 1.0f) * 100);
	}

	// Since engine start, so the splash counts too.
	const int secs = OS::get_singleton()->get_ticks_msec() / 1000;
	elapsed->set_text(itos(secs / 60) + ":" + itos(secs % 60).pad_zeros(2));
}

void EditorLoadingScreen::_step_scenes() {
	if (scene_idx >= scenes.size()) {
		held.clear();
		finish_start_msec = OS::get_singleton()->get_ticks_msec();
		_set_stage(STAGE_FINISH);
		return;
	}

	// Copied: load_scene() can pump the main loop.
	const PendingScene ps = scenes[scene_idx];

	if (!scene_prepared) {
		scene_prepared = true;
		dep_idx = 0;
		deps.clear();
		if (FileAccess::exists(ps.path)) {
			List<String> list;
			ResourceLoader::get_dependencies(ps.path, &list);
			for (List<String>::Element *E = list.front(); E; E = E->next()) {
				// Missing ones are left for load_scene() to report.
				if (ResourceLoader::exists(E->get())) {
					deps.push_back(E->get());
				}
			}
		}
		return; // Let a frame show what is about to load.
	}

	const uint64_t slice_start = OS::get_singleton()->get_ticks_msec();
	while (dep_idx < deps.size()) {
		RES res = ResourceLoader::load(deps[dep_idx]);
		if (res.is_valid()) {
			held.push_back(res);
		}
		dep_idx++;
		if (OS::get_singleton()->get_ticks_msec() - slice_start > 25) {
			return;
		}
	}

	EditorNode::get_singleton()->load_startup_scene(ps.path, ps.restoring);
	scene_idx++;
	scene_prepared = false;

	if (scene_idx == scenes.size() && ps.restoring) {
		EditorNode::get_singleton()->save_layout();
	}
}

void EditorLoadingScreen::_step_finish() {
	// The first frames of a scene compile shaders and stream terrain in. Wait for
	// them to calm down, but never hold the editor hostage over it.
	const bool quiet = !task_active && get_process_delta_time() < 0.1;
	settle_frames = quiet ? settle_frames + 1 : 0;

	if (settle_frames >= 5 || OS::get_singleton()->get_ticks_msec() - finish_start_msec > 5000) {
		done = true;
		set_process(false);
		set_process_input(false);
		hide();
		queue_delete();
	}
}

void EditorLoadingScreen::_background_pressed() {
	dismissed = true;
	set_process_input(false);
	hide();
}

void EditorLoadingScreen::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_set_stage(STAGE_SCAN);
			set_process(true);
			set_process_input(true);
		} break;
		case NOTIFICATION_DRAW: {
			draw_rect(Rect2(Point2(), get_size()), get_color("dark_color_2", "Editor"));
		} break;
		case NOTIFICATION_PROCESS: {
			if (!dismissed) {
				// A dialog may need an answer (a broken dependency, quit...), and it
				// would open underneath. Step aside until it is dealt with.
				Control *modal = get_viewport()->get_modal_stack_top();
				set_visible(!modal || modal == ProgressDialog::get_singleton());
			}

			if (stage == STAGE_SCAN && EditorFileSystem::get_singleton()->is_first_scan_settling()) {
				_set_stage(STAGE_IMPORT);
			}

			if (!busy && !task_active) {
				busy = true;
				if (stage == STAGE_SCENES) {
					_step_scenes();
				} else if (stage == STAGE_FINISH) {
					_step_finish();
				}
				busy = false;
			}

			if (!done && is_visible()) {
				_update_display();
				update(); // Low processor mode only redraws on changes.
			}
		} break;
	}
}

void EditorLoadingScreen::_input(const Ref<InputEvent> &p_event) {
	// The mouse is stopped by this control; keep editor shortcuts out as well.
	Ref<InputEventKey> key = p_event;
	if (key.is_valid() && is_visible() && !get_viewport()->gui_has_modal_stack()) {
		get_tree()->set_input_as_handled();
	}
}

void EditorLoadingScreen::queue_scene(const String &p_path, bool p_restoring) {
	PendingScene ps;
	ps.path = p_path;
	ps.restoring = p_restoring;
	scenes.push_back(ps);
}

void EditorLoadingScreen::first_scan_finished() {
	_set_stage(STAGE_SCENES);
}

void EditorLoadingScreen::task_begin(const String &p_label, int p_steps) {
	task_active = true;
	task_label = p_label;
	task_state = String();
	task_step_value = 0;
	task_steps = p_steps;
}

void EditorLoadingScreen::task_update(const String &p_state, int p_step) {
	task_state = p_state;
	task_step_value = p_step < 0 ? task_step_value + 1 : p_step;
}

void EditorLoadingScreen::task_end() {
	task_active = false;
}

void EditorLoadingScreen::_bind_methods() {
	ClassDB::bind_method("_input", &EditorLoadingScreen::_input);
	ClassDB::bind_method("_background_pressed", &EditorLoadingScreen::_background_pressed);
}

EditorLoadingScreen::EditorLoadingScreen() {
	singleton = this;
	stage = STAGE_SCAN;
	done = false;
	dismissed = false;
	busy = false;
	scene_idx = 0;
	scene_prepared = false;
	dep_idx = 0;
	settle_frames = 0;
	finish_start_msec = 0;
	task_active = false;
	task_step_value = 0;
	task_steps = 0;

	set_anchors_and_margins_preset(PRESET_WIDE);
	set_mouse_filter(MOUSE_FILTER_STOP);

	CenterContainer *center = memnew(CenterContainer);
	center->set_anchors_and_margins_preset(PRESET_WIDE);
	add_child(center);

	VBoxContainer *vb = memnew(VBoxContainer);
	vb->set_custom_minimum_size(Size2(560, 0) * EDSCALE);
	vb->add_constant_override("separation", 8 * EDSCALE);
	center->add_child(vb);

	Ref<Image> icon = memnew(Image(app_icon_png));
	Ref<ImageTexture> icon_tex;
	icon_tex.instance();
	icon_tex->create_from_image(icon);
	TextureRect *logo = memnew(TextureRect);
	logo->set_texture(icon_tex);
	logo->set_expand(true);
	logo->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	logo->set_custom_minimum_size(Size2(0, 128) * EDSCALE);
	vb->add_child(logo);

	Label *title = memnew(Label);
	String project = ProjectSettings::get_singleton()->get("application/config/name");
	title->set_text(project.empty() ? String(VERSION_NAME) : vformat(TTR("Loading %s"), project));
	title->set_align(Label::ALIGN_CENTER);
	vb->add_child(title);
	vb->add_spacer();

	const String names[STAGE_MAX] = {
		TTR("Scanning project files"),
		TTR("Importing assets"),
		TTR("Opening scenes"),
		TTR("Finishing up"),
	};
	for (int i = 0; i < STAGE_MAX; i++) {
		stage_labels[i] = memnew(Label);
		stage_labels[i]->set_text(names[i]);
		vb->add_child(stage_labels[i]);
	}
	vb->add_spacer();

	progress = memnew(ProgressBar);
	progress->set_max(100);
	progress->set_step(0.1);
	vb->add_child(progress);

	detail = memnew(Label);
	detail->set_clip_text(true);
	vb->add_child(detail);

	HBoxContainer *hb = memnew(HBoxContainer);
	vb->add_child(hb);
	counter = memnew(Label);
	counter->set_h_size_flags(SIZE_EXPAND_FILL);
	hb->add_child(counter);
	elapsed = memnew(Label);
	hb->add_child(elapsed);

	background = memnew(Button);
	background->set_text(TTR("Continue in Background"));
	background->set_h_size_flags(SIZE_SHRINK_CENTER);
	background->set_focus_mode(FOCUS_NONE);
	background->connect("pressed", this, "_background_pressed");
	vb->add_child(background);
}

EditorLoadingScreen::~EditorLoadingScreen() {
	singleton = nullptr;
}
