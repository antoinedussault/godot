#include "cell_world_editor_plugin.h"
#ifdef TOOLS_ENABLED

#include "editor/editor_node.h"
#include "editor_ui_helpers.h"
#include "game_world.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"

CellWorldEditorPlugin::CellWorldEditorPlugin(EditorNode *p_node) {
	VBoxContainer *content = nullptr;
	dock_root = cs_make_dock_root(&content);
	dock_root->set_name("GameWorldDock");

	cs_add_section_header(content, "Game World");

	status = memnew(Label);
	status->set_autowrap(true);
	content->add_child(status);

	Button *bake = memnew(Button);
	bake->set_text("Bake World");
	bake->set_tooltip("Saves the scene, then writes \"<scene>.world/\": one file per cell holding that\ncell's static objects, plus a copy of the scene without them.\nThe game then loads only the cells near the camera instead of the whole map.\nThe scene you are editing is not changed. Re-bake after editing it.");
	bake->connect("pressed", this, "_on_bake_pressed");
	content->add_child(bake);

	Button *clear = memnew(Button);
	clear->set_text("Clear Bake");
	clear->set_tooltip("Deletes the baked files. The game goes back to streaming this scene from memory.");
	clear->connect("pressed", this, "_on_clear_pressed");
	content->add_child(clear);

	cs_add_section_header(content, "Legacy");
	Button *migrate = memnew(Button);
	migrate->set_text("Migrate Legacy Scene");
	migrate->set_tooltip("For scenes made before the GameWorld overhaul: moves objects out of their\nCell_x_z / CellWorld / DynamicObjects wrappers (keeping their world position)\nand adopts the old terrain and foliage grid settings.\nNot undoable - the scene is only changed on disk when you save it.");
	migrate->connect("pressed", this, "_on_migrate_pressed");
	content->add_child(migrate);

	add_control_to_bottom_panel(dock_root, "World");
}

bool CellWorldEditorPlugin::handles(Object *p_object) const {
	return Object::cast_to<GameWorld>(p_object) != nullptr;
}

void CellWorldEditorPlugin::edit(Object *p_object) {
	world = Object::cast_to<GameWorld>(p_object);
	_update_status();
}

void CellWorldEditorPlugin::make_visible(bool p_visible) {
	if (dock_root && p_visible) {
		make_bottom_panel_item_visible(dock_root);
		_update_status();
	}
	if (!p_visible) {
		world = nullptr;
	}
}

void CellWorldEditorPlugin::_update_status() {
	if (!status) {
		return;
	}
	if (!world) {
		status->set_text("");
		return;
	}
	String t = world->get_has_terrain() ? "Exterior (terrain + foliage)." : "Interior (no terrain, no foliage).";
	t += world->has_bake() ? "\nBaked: " + world->get_bake_dir() : "\nNot baked: objects stream from memory.";
	status->set_text(t);
}

void CellWorldEditorPlugin::_on_bake_pressed() {
	if (!world) {
		return;
	}
	// Saving first does two things: the bake is stamped against the file on
	// disk (so it is not immediately "older than the scene"), and every plugin
	// gets its apply_changes(), which is where the Scene Layer dock restores
	// the real visibility of whatever an editor layer toggle is hiding.
	Error err = get_editor_interface()->save_scene();
	if (err != OK) {
		status->set_text("Bake cancelled: the scene could not be saved (new scenes need a file name first).");
		return;
	}
	err = world->bake_world();
	if (err != OK) {
		status->set_text("Bake failed (error " + itos(err) + "). See the Output panel.");
		return;
	}
	get_editor_interface()->get_resource_file_system()->scan();
	_update_status();
}

void CellWorldEditorPlugin::_on_clear_pressed() {
	if (!world) {
		return;
	}
	world->clear_bake();
	get_editor_interface()->get_resource_file_system()->scan();
	_update_status();
}

void CellWorldEditorPlugin::_on_migrate_pressed() {
	if (!world) {
		return;
	}
	world->migrate_legacy(get_editor_interface()->get_edited_scene_root());
	_update_status();
}

void CellWorldEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_bake_pressed"), &CellWorldEditorPlugin::_on_bake_pressed);
	ClassDB::bind_method(D_METHOD("_on_clear_pressed"), &CellWorldEditorPlugin::_on_clear_pressed);
	ClassDB::bind_method(D_METHOD("_on_migrate_pressed"), &CellWorldEditorPlugin::_on_migrate_pressed);
}

#endif // TOOLS_ENABLED
