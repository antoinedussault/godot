#include "cell_world_editor_plugin.h"
#ifdef TOOLS_ENABLED

#include "cell_world.h"
#include "editor/editor_node.h"
#include "editor_ui_helpers.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"

CellWorldEditorPlugin::CellWorldEditorPlugin(EditorNode *p_node) {
	VBoxContainer *content = nullptr;
	dock_root = cs_make_dock_root(&content);
	dock_root->set_name("CellWorldDock");

	cs_add_section_header(content, "Cell World");
	sort_button = memnew(Button);
	sort_button->set_text("Sort Into Cells");
	sort_button->set_tooltip("Reparent loose children into the Cell whose footprint they sit in");
	sort_button->connect("pressed", this, "_on_sort_pressed");
	content->add_child(sort_button);

	add_control_to_bottom_panel(dock_root, "World");
}

bool CellWorldEditorPlugin::handles(Object *p_object) const {
	return Object::cast_to<CellWorld>(p_object) != nullptr;
}

void CellWorldEditorPlugin::edit(Object *p_object) {
	world = Object::cast_to<CellWorld>(p_object);
}

void CellWorldEditorPlugin::make_visible(bool p_visible) {
	if (dock_root && p_visible) {
		make_bottom_panel_item_visible(dock_root);
	}
	if (!p_visible) world = nullptr;
}

void CellWorldEditorPlugin::_on_sort_pressed() {
	if (!world) return;
	// Pass the edited scene root as owner so the created cells and the moved
	// objects all save with the scene.
	Node *root = get_editor_interface()->get_edited_scene_root();
	world->sort_existing(root);
}

void CellWorldEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_sort_pressed"), &CellWorldEditorPlugin::_on_sort_pressed);
}

#endif // TOOLS_ENABLED
