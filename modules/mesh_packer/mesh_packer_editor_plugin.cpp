/* mesh_packer_editor_plugin.cpp */
#include "mesh_packer_editor_plugin.h"
#ifdef TOOLS_ENABLED

#include "core/undo_redo.h"
#include "editor/editor_node.h"
#include "mesh_packer.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/separator.h"

MeshPackerEditorPlugin::MeshPackerEditorPlugin(EditorNode *p_node) {
	toolbar = memnew(HBoxContainer);

	VSeparator *separator = memnew(VSeparator);
	toolbar->add_child(separator);

	pack_button = memnew(Button);
	pack_button->set_text(TTR("Pack"));
	pack_button->set_tooltip(TTR("Bake every MeshInstance below this node into merged meshes and MultiMeshes."));
	pack_button->set_flat(true);
	pack_button->connect("pressed", this, "_on_pack_pressed");
	toolbar->add_child(pack_button);

	unpack_button = memnew(Button);
	unpack_button->set_text(TTR("Unpack"));
	unpack_button->set_tooltip(TTR("Delete the packed output and show the original meshes again."));
	unpack_button->set_flat(true);
	unpack_button->connect("pressed", this, "_on_unpack_pressed");
	toolbar->add_child(unpack_button);

	stats_label = memnew(Label);
	toolbar->add_child(stats_label);

	toolbar->hide();
	add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, toolbar);
}

bool MeshPackerEditorPlugin::handles(Object *p_object) const {
	return Object::cast_to<MeshPacker>(p_object) != nullptr;
}

void MeshPackerEditorPlugin::edit(Object *p_object) {
	packer = Object::cast_to<MeshPacker>(p_object);
	_update_toolbar();
}

void MeshPackerEditorPlugin::make_visible(bool p_visible) {
	if (toolbar) {
		toolbar->set_visible(p_visible);
	}
	if (!p_visible) {
		packer = nullptr;
	}
}

void MeshPackerEditorPlugin::_update_toolbar() {
	if (!packer) {
		stats_label->set_text("");
		return;
	}

	const bool is_packed = packer->is_packed();
	pack_button->set_text(is_packed ? TTR("Repack") : TTR("Pack"));
	unpack_button->set_disabled(!is_packed);

	if (!is_packed) {
		stats_label->set_text(vformat(TTR("  %d meshes to pack"), packer->get_source_count()));
		return;
	}

	Dictionary stats = packer->get_pack_stats();
	if (stats.empty()) {
		stats_label->set_text(TTR("  packed"));
		return;
	}
	stats_label->set_text(vformat(TTR("  %d meshes -> %d merged + %d multimesh (%d surfaces -> %d)"),
			(int)stats["sources"], (int)stats["merged_meshes"], (int)stats["multimeshes"],
			(int)stats["surfaces_before"], (int)stats["surfaces_after"]));
}

void MeshPackerEditorPlugin::_on_pack_pressed() {
	if (!packer) {
		return;
	}
	// pack() and unpack() are exact inverses, so they double as the undo pair
	// and this also marks the scene as modified.
	UndoRedo *undo_redo = EditorNode::get_singleton()->get_undo_redo();
	undo_redo->create_action(TTR("Pack Meshes"));
	undo_redo->add_do_method(packer, "pack");
	undo_redo->add_undo_method(packer, "unpack");
	undo_redo->commit_action();
	_update_toolbar();
}

void MeshPackerEditorPlugin::_on_unpack_pressed() {
	if (!packer) {
		return;
	}
	UndoRedo *undo_redo = EditorNode::get_singleton()->get_undo_redo();
	undo_redo->create_action(TTR("Unpack Meshes"));
	undo_redo->add_do_method(packer, "unpack");
	undo_redo->add_undo_method(packer, "pack");
	undo_redo->commit_action();
	_update_toolbar();
}

void MeshPackerEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_pack_pressed"), &MeshPackerEditorPlugin::_on_pack_pressed);
	ClassDB::bind_method(D_METHOD("_on_unpack_pressed"), &MeshPackerEditorPlugin::_on_unpack_pressed);
}

#endif // TOOLS_ENABLED
