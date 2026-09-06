#ifndef CELL_WORLD_EDITOR_PLUGIN_H
#define CELL_WORLD_EDITOR_PLUGIN_H
#ifdef TOOLS_ENABLED

#include "editor/editor_plugin.h"

class CellWorld;
class Control;
class Button;

// Activates when a CellWorld is selected. Left dock panel (consistent with the
// other cell_system tools) with a "Sort Into Cells" button that reparents the
// world's loose children into their owning cells.
class CellWorldEditorPlugin : public EditorPlugin {
	GDCLASS(CellWorldEditorPlugin, EditorPlugin);

	CellWorld *world = nullptr;
	Control *dock_root = nullptr;
	Button *sort_button = nullptr;

	void _on_sort_pressed();

protected:
	static void _bind_methods();

public:
	virtual String get_name() const { return "CellWorld"; }
	virtual bool has_main_screen() const { return false; }
	virtual bool handles(Object *p_object) const;
	virtual void edit(Object *p_object);
	virtual void make_visible(bool p_visible);

	CellWorldEditorPlugin(EditorNode *p_node);
};

#endif // TOOLS_ENABLED
#endif // CELL_WORLD_EDITOR_PLUGIN_H
