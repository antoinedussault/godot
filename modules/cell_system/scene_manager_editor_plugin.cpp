#include "scene_manager_editor_plugin.h"
#ifdef TOOLS_ENABLED

#include "editor_ui_helpers.h"

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/os/file_access.h"
#include "editor/editor_file_dialog.h"
#include "editor/editor_node.h"
#include "editor/editor_settings.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/tree.h"

static const char *META_SECTION = "cell_system";
static const char *META_KEY = "scene_catalog_path";

SceneManagerEditorPlugin::SceneManagerEditorPlugin(EditorNode *p_node) {
	_build_dock();

	file_dialog = memnew(EditorFileDialog);
	file_dialog->set_access(EditorFileDialog::ACCESS_RESOURCES);
	file_dialog->connect("file_selected", this, "_on_file_selected");
	EditorNode::get_singleton()->get_gui_base()->add_child(file_dialog);

	add_control_to_bottom_panel(dock_root, "Scenes");

	// Restore the last-used catalog for this project.
	Ref<EditorSettings> es = get_editor_interface()->get_editor_settings();
	String saved = es->get_project_metadata(META_SECTION, META_KEY, "");
	if (!saved.empty() && FileAccess::exists(saved)) {
		_load_catalog(saved);
	}
}

void SceneManagerEditorPlugin::_build_dock() {
	dock_root = cs_make_dock_root(&dock_content);
	dock_root->set_name("SceneManagerDock");

	cs_add_section_header(dock_content, "Scene Catalog");

	HBoxContainer *path_row = memnew(HBoxContainer);
	path_field = memnew(LineEdit);
	path_field->set_editable(false);
	path_field->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	path_field->set_placeholder("No catalog loaded");
	path_row->add_child(path_field);

	Button *browse_btn = memnew(Button);
	browse_btn->set_text("Browse");
	browse_btn->connect("pressed", this, "_on_browse_pressed");
	path_row->add_child(browse_btn);

	Button *new_btn = memnew(Button);
	new_btn->set_text("New");
	new_btn->connect("pressed", this, "_on_new_pressed");
	path_row->add_child(new_btn);
	dock_content->add_child(path_row);

	cs_add_section_header(dock_content, "Scenes");

	tree = memnew(Tree);
	tree->set_hide_root(true);
	tree->set_columns(3);
	tree->set_column_titles_visible(true);
	tree->set_column_title(0, "Name");
	tree->set_column_title(1, "Id");
	tree->set_column_title(2, "Kind");
	tree->set_column_expand(2, false);
	tree->set_column_min_width(2, 70);
	tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tree->set_custom_minimum_size(Size2(0, 200));
	tree->connect("item_activated", this, "_on_item_activated");
	dock_content->add_child(tree);

	HBoxContainer *btn_row = memnew(HBoxContainer);
	Button *open_btn = memnew(Button);
	open_btn->set_text("Open");
	open_btn->set_tooltip("Open the selected scene in the editor");
	open_btn->connect("pressed", this, "_on_open_pressed");
	btn_row->add_child(open_btn);

	Button *play_btn = memnew(Button);
	play_btn->set_text("Play");
	play_btn->set_tooltip("Run the selected scene standalone");
	play_btn->connect("pressed", this, "_on_play_pressed");
	btn_row->add_child(play_btn);

	Button *add_btn = memnew(Button);
	add_btn->set_text("Add Current Scene");
	add_btn->set_tooltip("Register the scene currently open in the editor");
	add_btn->connect("pressed", this, "_on_add_current_pressed");
	btn_row->add_child(add_btn);

	Button *rm_btn = memnew(Button);
	rm_btn->set_text("Remove");
	rm_btn->connect("pressed", this, "_on_remove_pressed");
	btn_row->add_child(rm_btn);
	dock_content->add_child(btn_row);
}

void SceneManagerEditorPlugin::_load_catalog(const String &p_path) {
	Ref<SceneCatalog> c = ResourceLoader::load(p_path, "SceneCatalog");
	if (c.is_null()) {
		EditorNode::get_singleton()->show_warning(TTR("Could not load a SceneCatalog from:\n") + p_path);
		return;
	}
	catalog = c;
	catalog_path = p_path;
	path_field->set_text(p_path);

	Ref<EditorSettings> es = get_editor_interface()->get_editor_settings();
	es->set_project_metadata(META_SECTION, META_KEY, p_path);

	_refresh_tree();
}

void SceneManagerEditorPlugin::_save_catalog() {
	if (catalog.is_null() || catalog_path.empty()) {
		return;
	}
	Error err = ResourceSaver::save(catalog_path, catalog);
	if (err != OK) {
		EditorNode::get_singleton()->show_warning(TTR("Could not save the SceneCatalog."));
	}
}

void SceneManagerEditorPlugin::_refresh_tree() {
	if (!tree) {
		return;
	}
	tree->clear();
	if (catalog.is_null()) {
		return;
	}
	TreeItem *root = tree->create_item();
	for (int i = 0; i < catalog->get_entry_count(); i++) {
		Ref<SceneCatalogEntry> e = catalog->get_entry_by_index(i);
		if (e.is_null()) {
			continue;
		}
		TreeItem *it = tree->create_item(root);
		String name = e->get_display_name();
		if (name.empty()) {
			name = e->get_id();
		}
		it->set_text(0, name);
		it->set_text(1, e->get_id());
		it->set_text(2, e->get_kind() == SceneCatalogEntry::KIND_EXTERIOR ? "Exterior" : "Interior");
		it->set_tooltip(0, e->get_scene_path());
		it->set_metadata(0, i);
	}
}

Ref<SceneCatalogEntry> SceneManagerEditorPlugin::_selected_entry() const {
	if (catalog.is_null() || !tree) {
		return Ref<SceneCatalogEntry>();
	}
	TreeItem *sel = tree->get_selected();
	if (!sel) {
		return Ref<SceneCatalogEntry>();
	}
	int idx = sel->get_metadata(0);
	return catalog->get_entry_by_index(idx);
}

void SceneManagerEditorPlugin::_on_browse_pressed() {
	_fd_mode = FD_OPEN;
	file_dialog->clear_filters();
	file_dialog->add_filter("*.tres,*.res;SceneCatalog");
	file_dialog->set_mode(EditorFileDialog::MODE_OPEN_FILE);
	file_dialog->popup_centered_ratio(0.6);
}

void SceneManagerEditorPlugin::_on_new_pressed() {
	_fd_mode = FD_NEW;
	file_dialog->clear_filters();
	file_dialog->add_filter("*.tres;SceneCatalog");
	file_dialog->set_mode(EditorFileDialog::MODE_SAVE_FILE);
	file_dialog->set_current_file("scene_catalog.tres");
	file_dialog->popup_centered_ratio(0.6);
}

void SceneManagerEditorPlugin::_on_file_selected(const String &p_path) {
	if (_fd_mode == FD_NEW) {
		Ref<SceneCatalog> c;
		c.instance();
		Error err = ResourceSaver::save(p_path, c);
		if (err != OK) {
			EditorNode::get_singleton()->show_warning(TTR("Could not create the SceneCatalog."));
			return;
		}
	}
	_load_catalog(p_path);
}

void SceneManagerEditorPlugin::_on_open_pressed() {
	Ref<SceneCatalogEntry> e = _selected_entry();
	if (e.is_null() || e->get_scene_path().empty()) {
		return;
	}
	get_editor_interface()->open_scene_from_path(e->get_scene_path());
}

void SceneManagerEditorPlugin::_on_play_pressed() {
	Ref<SceneCatalogEntry> e = _selected_entry();
	if (e.is_null() || e->get_scene_path().empty()) {
		return;
	}
	get_editor_interface()->play_custom_scene(e->get_scene_path());
}

void SceneManagerEditorPlugin::_on_add_current_pressed() {
	if (catalog.is_null()) {
		EditorNode::get_singleton()->show_warning(TTR("Load or create a SceneCatalog first."));
		return;
	}
	Node *root = get_editor_interface()->get_edited_scene_root();
	if (!root) {
		return;
	}
	String path = root->get_filename();
	if (path.empty()) {
		EditorNode::get_singleton()->show_warning(TTR("Save the current scene before adding it."));
		return;
	}
	String id = path.get_file().get_basename();
	if (catalog->has_id(id)) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("The catalog already has an entry with id \"%s\"."), id));
		return;
	}
	Ref<SceneCatalogEntry> e;
	e.instance();
	e->set_id(id);
	e->set_scene_path(path);
	e->set_display_name(root->get_name());
	catalog->add_entry(e);
	_save_catalog();
	_refresh_tree();
}

void SceneManagerEditorPlugin::_on_remove_pressed() {
	Ref<SceneCatalogEntry> e = _selected_entry();
	if (e.is_null() || catalog.is_null()) {
		return;
	}
	catalog->remove_entry(e->get_id());
	_save_catalog();
	_refresh_tree();
}

void SceneManagerEditorPlugin::_on_item_activated() {
	_on_open_pressed();
}

void SceneManagerEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_browse_pressed"), &SceneManagerEditorPlugin::_on_browse_pressed);
	ClassDB::bind_method(D_METHOD("_on_new_pressed"), &SceneManagerEditorPlugin::_on_new_pressed);
	ClassDB::bind_method(D_METHOD("_on_file_selected", "path"), &SceneManagerEditorPlugin::_on_file_selected);
	ClassDB::bind_method(D_METHOD("_on_open_pressed"), &SceneManagerEditorPlugin::_on_open_pressed);
	ClassDB::bind_method(D_METHOD("_on_play_pressed"), &SceneManagerEditorPlugin::_on_play_pressed);
	ClassDB::bind_method(D_METHOD("_on_add_current_pressed"), &SceneManagerEditorPlugin::_on_add_current_pressed);
	ClassDB::bind_method(D_METHOD("_on_remove_pressed"), &SceneManagerEditorPlugin::_on_remove_pressed);
	ClassDB::bind_method(D_METHOD("_on_item_activated"), &SceneManagerEditorPlugin::_on_item_activated);
}

#endif // TOOLS_ENABLED
