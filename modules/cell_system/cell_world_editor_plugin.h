#ifndef CELL_WORLD_EDITOR_PLUGIN_H
#define CELL_WORLD_EDITOR_PLUGIN_H
#ifdef TOOLS_ENABLED

#include "editor/editor_plugin.h"

class GameWorld;
class Control;
class Button;
class Label;

// "World" bottom panel, shown while a GameWorld is selected.
//
// There is no "Sort Into Cells" any more: GameWorld assigns objects to cells by
// position at runtime and never touches the hierarchy, so there is nothing to
// sort. What is left are the operations that write files:
//
//   Bake World       - one file per cell + a stripped world.scn (see
//                      GameWorld::bake_world). Saves the scene first.
//   Clear Bake       - removes them; the scene streams from memory again.
//   Migrate Legacy   - flattens Cell / CellWorld / DynamicObjects wrappers from
//                      scenes made before the overhaul.
class CellWorldEditorPlugin : public EditorPlugin {
	GDCLASS(CellWorldEditorPlugin, EditorPlugin);

	GameWorld *world = nullptr;
	Control *dock_root = nullptr;
	Label *status = nullptr;

	void _on_bake_pressed();
	void _on_clear_pressed();
	void _on_migrate_pressed();
	void _update_status();

protected:
	static void _bind_methods();

public:
	virtual String get_name() const { return "GameWorld"; }
	virtual bool has_main_screen() const { return false; }
	virtual bool handles(Object *p_object) const;
	virtual void edit(Object *p_object);
	virtual void make_visible(bool p_visible);

	CellWorldEditorPlugin(EditorNode *p_node);
};

#endif // TOOLS_ENABLED
#endif // CELL_WORLD_EDITOR_PLUGIN_H
