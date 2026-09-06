#ifndef SCENE_LAYER_EDITOR_PLUGIN_H
#define SCENE_LAYER_EDITOR_PLUGIN_H
#ifdef TOOLS_ENABLED

#include "core/map.h"
#include "editor/editor_plugin.h"

class GameWorld;
class Control;
class VBoxContainer;
class Tree;
class TreeItem;

// Bottom-panel "Scene Layer" tool: organizes a GameWorld scene's nodes into a
// hierarchy of editor-only layers/sub-layers (assignable to the current Scene
// selection, toggle-able for visibility) without touching the scene tree
// structure. Only appears while the edited scene's root is a GameWorld —
// driven by edited_scene_changed(), not by node selection like the other
// cell_system docks (handles()/edit()/make_visible() are unused here).
class SceneLayerEditorPlugin : public EditorPlugin {
	GDCLASS(SceneLayerEditorPlugin, EditorPlugin);

	enum ButtonId { BTN_ASSIGN, BTN_UNASSIGN, BTN_DELETE };

	GameWorld *world = nullptr;
	bool _panel_added = false;

	Control *dock_root = nullptr;
	VBoxContainer *dock_content = nullptr;
	Tree *tree = nullptr;

	// A freshly created layer isn't selected in the rebuilt tree (create_item
	// doesn't select), so chaining "New Sub-Layer" right after "New Layer"
	// would silently no-op with nothing selected to nest under. Set right
	// before _commit_layer_data() for a create action; _add_layer_items()
	// selects the matching row as it rebuilds and clears this back to -1.
	int _pending_select_id = -1;

	// Per-node "visible" value as it was before any layer toggle touched it,
	// so the pre-save safety net can restore exactly that (not just force
	// true) — see apply_changes(). Intentionally never cleared on scene
	// switch: entries for closed scenes just go stale/unused, which is safer
	// than losing track of a node's true original state mid-session.
	Map<Node *, bool> _original_visibility;

	void _build_dock();
	// _refresh_tree() only schedules the rebuild (call_deferred); the real
	// work is _refresh_tree_now(). It's invoked as an UndoRedo do/undo method
	// from inside Tree's own synchronous signal handling (item_edited /
	// button_pressed both fire, and commit_action() calls do-methods
	// synchronously) — clearing/rebuilding the Tree's items right there,
	// still inside Tree's own click-handling call stack, is a use-after-free.
	// Deferring to the next idle frame sidesteps that entirely.
	void _refresh_tree();
	void _refresh_tree_now();
	void _add_layer_items(TreeItem *p_parent_item, const Array &p_layers);
	void _commit_layer_data(const String &p_action_name, const Dictionary &p_new_data);
	void _walk_and_apply(Node *p_node, const Map<int, bool> &p_effective);

	void _on_new_layer_pressed();
	void _on_new_sublayer_pressed();
	void _on_item_edited();
	void _on_button_pressed(Object *p_item, int p_column, int p_id);

protected:
	static void _bind_methods();

public:
	virtual String get_name() const { return "SceneLayer"; }
	virtual bool has_main_screen() const { return false; }
	virtual void edited_scene_changed();
	virtual void apply_changes();

	// Bound for UndoRedo/call_deferred use (see .cpp) — not meant to be
	// called directly otherwise.
	void _apply_layer_visibility();

	SceneLayerEditorPlugin(EditorNode *p_node);
};

#endif
#endif
