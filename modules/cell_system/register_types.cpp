#include "register_types.h"

#include "core/class_db.h"
#include "cell_terrain.h"
#include "cell_terrain_manager.h"
#include "terrain_brush.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "terrain_editor_plugin.h"
#endif

void register_cell_system_types() {
    ClassDB::register_class<CellTerrain>();
    ClassDB::register_class<CellTerrainManager>();
    ClassDB::register_class<TerrainBrush>();
#ifdef TOOLS_ENABLED
    EditorPlugins::add_by_type<TerrainEditorPlugin>();
#endif
}

void unregister_cell_system_types() {
    // Nothing to free; ClassDB and EditorPlugins handle teardown.
}