#ifndef SCENE_MANAGER_EDITOR_PLUGIN_H
#define SCENE_MANAGER_EDITOR_PLUGIN_H
#ifdef TOOLS_ENABLED

#include "editor/editor_plugin.h"

#include "scene_catalog.h"

class Control;
class VBoxContainer;
class LineEdit;
class Tree;
class EditorFileDialog;

// Bottom-panel "Scenes" dock: point it at the project's SceneCatalog .tres and
// it lists every travellable scene with Open / Play buttons, plus "Add Current
// Scene" to register the scene you're editing. Editor convenience only — the
// runtime travel path is SceneManager / SceneDoor.
class SceneManagerEditorPlugin : public EditorPlugin {
	GDCLASS(SceneManagerEditorPlugin, EditorPlugin);

	enum FileDialogMode { FD_OPEN, FD_NEW };

	Control *dock_root = nullptr;
	VBoxContainer *dock_content = nullptr;
	LineEdit *path_field = nullptr;
	Tree *tree = nullptr;
	EditorFileDialog *file_dialog = nullptr;
	FileDialogMode _fd_mode = FD_OPEN;

	String catalog_path;
	Ref<SceneCatalog> catalog;

	void _build_dock();
	void _load_catalog(const String &p_path);
	void _save_catalog();
	void _refresh_tree();
	Ref<SceneCatalogEntry> _selected_entry() const;

	void _on_browse_pressed();
	void _on_new_pressed();
	void _on_file_selected(const String &p_path);
	void _on_open_pressed();
	void _on_play_pressed();
	void _on_add_current_pressed();
	void _on_remove_pressed();
	void _on_item_activated();

protected:
	static void _bind_methods();

public:
	virtual String get_name() const { return "Scenes"; }
	virtual bool has_main_screen() const { return false; }

	SceneManagerEditorPlugin(EditorNode *p_node);
};

#endif // TOOLS_ENABLED
#endif // SCENE_MANAGER_EDITOR_PLUGIN_H
