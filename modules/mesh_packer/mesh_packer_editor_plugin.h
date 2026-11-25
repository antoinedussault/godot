/* mesh_packer_editor_plugin.h */
#ifndef MESH_PACKER_EDITOR_PLUGIN_H
#define MESH_PACKER_EDITOR_PLUGIN_H
#ifdef TOOLS_ENABLED

#include "editor/editor_plugin.h"

class MeshPacker;
class Button;
class HBoxContainer;
class Label;

// Adds a Pack / Unpack toolbar to the 3D viewport while a MeshPacker is
// selected, plus a one-line readout of what the last pack achieved. Everything
// the buttons do is also available from script (MeshPacker.pack/unpack).
class MeshPackerEditorPlugin : public EditorPlugin {
	GDCLASS(MeshPackerEditorPlugin, EditorPlugin);

	MeshPacker *packer = nullptr;

	HBoxContainer *toolbar = nullptr;
	Button *pack_button = nullptr;
	Button *unpack_button = nullptr;
	Label *stats_label = nullptr;

	void _update_toolbar();
	void _on_pack_pressed();
	void _on_unpack_pressed();

protected:
	static void _bind_methods();

public:
	virtual String get_name() const { return "MeshPacker"; }
	virtual bool has_main_screen() const { return false; }
	virtual bool handles(Object *p_object) const;
	virtual void edit(Object *p_object);
	virtual void make_visible(bool p_visible);

	MeshPackerEditorPlugin(EditorNode *p_node);
};

#endif // TOOLS_ENABLED
#endif // MESH_PACKER_EDITOR_PLUGIN_H
