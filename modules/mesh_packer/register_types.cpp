/* register_types.cpp */
#include "register_types.h"

#include "core/class_db.h"

#include "mesh_packer.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "mesh_packer_editor_plugin.h"
#endif

void register_mesh_packer_types() {
	ClassDB::register_class<MeshPacker>();

#ifdef TOOLS_ENABLED
	EditorPlugins::add_by_type<MeshPackerEditorPlugin>();
#endif
}

void unregister_mesh_packer_types() {
	// ClassDB and EditorPlugins handle teardown.
}
