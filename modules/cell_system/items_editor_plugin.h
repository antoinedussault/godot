#ifndef ITEMS_EDITOR_PLUGIN_H
#define ITEMS_EDITOR_PLUGIN_H
#ifdef TOOLS_ENABLED

#include "editor/editor_plugin.h"

#include "core/set.h"
#include "item_category_catalog.h"

class Button;
class ConfirmationDialog;
class EditorFileDialog;
class EditorInspector;
class GridContainer;
class HBoxContainer;
class HSplitContainer;
class ItemList;
class Label;
class LineEdit;
class OptionButton;
class PanelContainer;
class PopupMenu;
class Tree;
class VBoxContainer;

// Main-screen "Items & Recipes" tab. Three views:
//   Items      - browse/create/edit .tres resources whose script extends ItemResource
//   Recipes    - same for ItemRecipeResource, plus an ingredient validator
//   Categories - edit the shared ItemCategoryCatalog (main + sub categories)
//
// The resource folder must be set first in Project Settings
// (`cell_system/items/directory`); until it is, only a setup prompt is shown.
class ItemsEditor : public VBoxContainer {
	GDCLASS(ItemsEditor, VBoxContainer);

	enum View {
		VIEW_ITEMS,
		VIEW_RECIPES,
		VIEW_CATEGORIES,
	};

	enum DialogMode {
		DIALOG_NONE,
		DIALOG_RENAME,
		DIALOG_DELETE,
		DIALOG_NEW_MAIN,
		DIALOG_NEW_SUB,
		DIALOG_RENAME_CAT,
	};

	View view = VIEW_ITEMS;
	DialogMode dialog_mode = DIALOG_NONE;

	String items_dir;
	String recipes_dir;
	String catalog_path;
	Ref<ItemCategoryCatalog> catalog;

	String selected_path;
	Ref<Resource> selected_res;

	// --- widgets ---
	HBoxContainer *view_bar = nullptr;
	Button *btn_items = nullptr;
	Button *btn_recipes = nullptr;
	Button *btn_categories = nullptr;

	PanelContainer *setup_panel = nullptr;
	Label *setup_label = nullptr;

	HSplitContainer *split = nullptr;
	Button *b_new = nullptr;
	Button *b_add_sub = nullptr;
	Button *b_dup = nullptr;
	Button *b_ren = nullptr;
	Button *b_del = nullptr;
	Tree *list = nullptr;

	EditorInspector *inspector = nullptr;
	HBoxContainer *assign_row = nullptr;
	OptionButton *opt_main = nullptr;
	OptionButton *opt_sub = nullptr;
	Button *b_validate = nullptr;
	ItemList *validation = nullptr;

	PopupMenu *new_menu = nullptr;
	ConfirmationDialog *dialog = nullptr;
	LineEdit *dialog_line = nullptr;
	Vector<String> new_menu_classes;

	// "New item" dialog: name + category + scene; the id is an auto-assigned FormID.
	String pending_class;
	String pending_id;
	ConfirmationDialog *create_dialog = nullptr;
	LineEdit *create_name = nullptr;
	OptionButton *create_main = nullptr;
	OptionButton *create_sub = nullptr;
	LineEdit *create_scene = nullptr;
	Label *create_id_preview = nullptr;
	EditorFileDialog *scene_dialog = nullptr;

	void _read_settings();
	bool _setup_ok() const;
	void _update_visibility();

	static bool _script_inherits(const String &p_class, const String &p_target);
	static String _global_class_for_script(const String &p_script_path);
	String _resource_base_class(const String &p_res_path) const;
	Vector<String> _scan_dir(const String &p_dir) const;
	Vector<String> _list_resources(const String &p_base_class) const;

	void _refresh();
	void _refresh_list();
	void _refresh_categories_tree();
	void _select_view(int p_view);
	void _on_item_selected();
	void _edit_resource(const String &p_path);

	void _collect_item_ids(Set<String> *r_ids) const;
	static String _slugify(const String &p_name);
	// Creation-Engine-style FormID (8 uppercase hex digits), one past the
	// highest existing item id.
	String _make_form_id() const;
	Ref<Resource> _instance_scripted(const String &p_global_class);

	void _rebuild_new_menu();
	void _on_new_pressed();
	void _on_new_menu_id(int p_id);
	void _create_recipe(const String &p_global_class);

	void _open_create_dialog(const String &p_global_class);
	void _populate_create_categories();
	void _on_create_main_selected(int p_idx);
	void _on_create_name_changed(const String &p_text);
	void _on_create_scene_browse();
	void _on_create_scene_picked(const String &p_path);
	void _on_create_confirmed();

	void _on_duplicate_pressed();
	void _on_rename_pressed();
	void _on_delete_pressed();
	void _on_add_sub_pressed();
	void _on_dialog_confirmed();

	void _on_open_settings();
	void _on_validate_pressed();

	void _load_catalog();
	void _save_catalog();
	void _populate_category_options();
	void _on_main_selected(int p_idx);
	void _on_sub_selected(int p_idx);

	String _selected_tree_meta(int p_col = 0) const;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void refresh() { _refresh(); }
	ItemsEditor();
};

class ItemsEditorPlugin : public EditorPlugin {
	GDCLASS(ItemsEditorPlugin, EditorPlugin);

	ItemsEditor *editor_control = nullptr;

public:
	virtual String get_name() const { return "Items & Recipes"; }
	virtual const Ref<Texture> get_icon() const;
	virtual bool has_main_screen() const { return true; }
	virtual void make_visible(bool p_visible);

	ItemsEditorPlugin(EditorNode *p_node);
};

#endif // TOOLS_ENABLED
#endif // ITEMS_EDITOR_PLUGIN_H
