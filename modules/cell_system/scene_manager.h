#ifndef SCENE_MANAGER_H
#define SCENE_MANAGER_H

#include "core/io/resource_loader.h"
#include "core/object.h"
#include "core/reference.h"

#include "scene_catalog.h"

class Node;
class SceneTransition;

// Engine singleton (`SceneManager` in GDScript) that owns runtime scene travel,
// Creation-Kit style: one PackedScene is current at a time; travelling to
// another async-loads it behind a loading screen, swaps it in as the current
// scene, then reports a spawn transform so the game's master node can move the
// player.
//
// It is a plain Object (no _process); a single SceneTransition CanvasLayer is
// created under the root viewport and the load is pumped from a SceneTree idle
// callback (registered in register_types.cpp) — the safe point to add/remove
// scene nodes.
class SceneManager : public Object {
	GDCLASS(SceneManager, Object);

	static SceneManager *singleton;

	// --- Configuration ---
	Ref<SceneCatalog> catalog;
	String loading_screen_path;
	NodePath player_target_path = NodePath("/root/GameMaster");
	StringName player_target_method = "place_player";
	int max_polls_per_frame = 4;

	// --- Runtime state ---
	String current_id;
	String current_spawn;
	Vector<String> history;
	bool _suppress_history = false;

	bool travelling = false;
	bool _load_done = false;
	float progress = 0.0f;
	String target_id;
	String target_spawn;
	Ref<ResourceInteractiveLoader> loader;

	bool _bootstrapped = false;
	ObjectID _transition_id = 0;

	SceneTransition *_get_transition() const;
	void _ensure_runtime();
	void _idle();
	void _pump();
	void _finish_load();
	void _fail(const String &p_id, const String &p_reason);
	bool _resolve_spawn(Node *p_scene_root, const String &p_spawn_id, Transform &r_xform) const;

protected:
	static void _bind_methods();

public:
	static SceneManager *get_singleton() { return singleton; }
	static void idle_callback(); // registered with SceneTree::add_idle_callback

	void set_catalog(const Ref<SceneCatalog> &p_catalog);
	Ref<SceneCatalog> get_catalog() const { return catalog; }
	void set_catalog_path(const String &p_path);

	void set_loading_screen(const String &p_path);
	String get_loading_screen() const { return loading_screen_path; }

	void set_player_target(const NodePath &p_path, const StringName &p_method);
	NodePath get_player_target_path() const { return player_target_path; }
	StringName get_player_target_method() const { return player_target_method; }

	void set_max_polls_per_frame(int p_n);
	int get_max_polls_per_frame() const { return max_polls_per_frame; }

	void bootstrap();

	void travel_to(const String &p_scene_id, const String &p_spawn_id = String());
	void reload_current();
	void travel_back();

	String get_current_scene_id() const { return current_id; }
	bool can_travel_back() const { return !history.empty(); }
	bool is_travelling() const { return travelling; }
	float get_progress() const { return progress; }

	SceneManager();
	~SceneManager();
};

#endif // SCENE_MANAGER_H
