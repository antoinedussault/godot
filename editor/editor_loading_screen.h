/**************************************************************************/
/*  editor_loading_screen.h                                               */
/**************************************************************************/

#ifndef EDITOR_LOADING_SCREEN_H
#define EDITOR_LOADING_SCREEN_H

#include "core/resource.h"
#include "scene/gui/control.h"

class Button;
class Label;
class ProgressBar;

// Covers the editor from its first frame until the project is ready: the first
// filesystem scan, any (re)imports, and the scenes opened at startup. It also
// drives that last step, one slice per frame, so the bar keeps moving.
//
// "Continue in Background" only hides it; loading still runs to completion.
class EditorLoadingScreen : public Control {
	GDCLASS(EditorLoadingScreen, Control);

	enum Stage {
		STAGE_SCAN,
		STAGE_IMPORT,
		STAGE_SCENES,
		STAGE_FINISH,
		STAGE_MAX
	};

	static EditorLoadingScreen *singleton;

	struct PendingScene {
		String path;
		bool restoring;
	};

	Stage stage;
	bool done;
	bool dismissed; // Sent to background by the user.
	bool busy; // Loading from _process(); nested Main::iteration() re-enters it.

	Vector<PendingScene> scenes;
	int scene_idx;
	bool scene_prepared;
	Vector<String> deps;
	int dep_idx;
	Vector<RES> held; // Keeps preloaded dependencies cached until their scene opens.

	int settle_frames;
	uint64_t finish_start_msec;

	bool task_active;
	String task_label;
	String task_state;
	int task_step_value;
	int task_steps;

	Label *stage_labels[STAGE_MAX];
	Label *detail;
	Label *counter;
	Label *elapsed;
	ProgressBar *progress;
	Button *background;

	void _set_stage(Stage p_stage);
	void _update_display();
	void _step_scenes();
	void _step_finish();
	void _background_pressed();

protected:
	void _notification(int p_what);
	void _input(const Ref<InputEvent> &p_event);
	static void _bind_methods();

public:
	static EditorLoadingScreen *get_singleton() { return singleton; }

	// True while it is what the user sees, so foreground progress belongs in it.
	bool is_covering() const { return !done && !dismissed && is_inside_tree(); }

	// Scenes still to open; the saved layout must not be overwritten meanwhile.
	bool has_pending_scenes() const { return !done && scene_idx < scenes.size(); }

	void queue_scene(const String &p_path, bool p_restoring);
	void first_scan_finished();

	void task_begin(const String &p_label, int p_steps);
	void task_update(const String &p_state, int p_step);
	void task_end();

	EditorLoadingScreen();
	~EditorLoadingScreen();
};

#endif // EDITOR_LOADING_SCREEN_H
