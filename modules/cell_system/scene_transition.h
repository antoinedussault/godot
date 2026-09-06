#ifndef SCENE_TRANSITION_H
#define SCENE_TRANSITION_H

#include "scene/main/canvas_layer.h"

class ColorRect;
class Node;

// The loading-screen host. SceneManager creates exactly one of these under the
// root viewport and drives it: show_loading() when a travel starts,
// set_progress() each frame while the scene loads, hide_loading() once the
// player has been placed.
//
// If a loading-screen .tscn is configured it is instanced on top of a solid
// backdrop; its root may implement `set_progress(ratio: float)` to drive a bar.
// With no scene configured it falls back to a plain colour fill (fade_color)
// so travel still works with zero assets.
//
// Not a node users place in a scene — it exists only at runtime.
class SceneTransition : public CanvasLayer {
	GDCLASS(SceneTransition, CanvasLayer);

public:
	enum State {
		STATE_HIDDEN,
		STATE_IN,      // fading up to fully covering
		STATE_COVERED, // loading screen fully visible
		STATE_OUT,     // fading back down to the game
	};

private:
	Color fade_color = Color(0, 0, 0, 1);
	float fade_time = 0.35f;
	float min_display_time = 0.4f; // covered screen stays at least this long

	State _state = STATE_HIDDEN;
	float _alpha = 0.0f;
	float _covered_elapsed = 0.0f;
	bool _hide_requested = false;
	float _progress = 0.0f;

	ColorRect *_backdrop = nullptr;
	Node *_loading_inst = nullptr;
	String _loading_scene_path;

	void _apply_alpha();
	void _clear_loading_instance();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_fade_color(const Color &p_color);
	Color get_fade_color() const { return fade_color; }
	void set_fade_time(float p_time);
	float get_fade_time() const { return fade_time; }
	void set_min_display_time(float p_time);
	float get_min_display_time() const { return min_display_time; }

	// p_scene_path empty -> plain colour fade.
	void show_loading(const String &p_scene_path);
	void set_progress(float p_ratio);
	void hide_loading();

	bool is_covered() const { return _state == STATE_COVERED; }
	bool is_busy() const { return _state != STATE_HIDDEN; }

	SceneTransition();
};

VARIANT_ENUM_CAST(SceneTransition::State);

#endif // SCENE_TRANSITION_H
