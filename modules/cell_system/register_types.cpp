#include "register_types.h"

#include "core/class_db.h"
#include "core/engine.h"
#include "core/project_settings.h"
#include "scene/main/scene_tree.h"

// Existing cell_system classes.
#include "cell_terrain.h"
#include "cell_terrain_layer.h"
#include "cell_terrain_manager.h"
#include "cell_world.h"
#include "cell_player_body.h"
#include "cell_character_body.h"
#include "cell_grid_visualizer.h"
#include "terrain_brush.h"
#include "terrain_path.h"
#include "terrain_path_point.h"

// Foliage system additions.
#include "foliage_type.h"
#include "cell_foliage.h"
#include "foliage_manager.h"

// Scene Layers.
#include "game_world.h"

// Scene Manager (Creation-Engine-style scene travel).
#include "scene_catalog.h"
#include "scene_manager.h"
#include "scene_transition.h"
#include "scene_spawn_point.h"
#include "scene_door.h"
#include "world_location_area.h"

// Items, recipes, categories + the pushable in-world item body.
#include "cell_item_body.h"
#include "item_category_catalog.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "terrain_editor_plugin.h"
#include "cell_world_editor_plugin.h"
#include "foliage_editor_plugin.h"
#include "terrain_path_editor_plugin.h"
#include "scene_layer_editor_plugin.h"
#include "scene_manager_editor_plugin.h"
#include "texture_packer_dialog.h"
#include "items_editor_plugin.h"
#endif

static SceneManager *_scene_manager = nullptr;

void register_cell_system_types() {
	// Terrain + world.
	ClassDB::register_class<CellTerrain>();
	ClassDB::register_class<CellTerrainLayer>();
	ClassDB::register_class<CellTerrainManager>();
	ClassDB::register_class<Cell>();
	ClassDB::register_class<CellWorld>();
	ClassDB::register_class<CellPlayerBody>();
	ClassDB::register_class<CellCharacterBody>();
	ClassDB::register_class<CellGridVisualizer>();
	ClassDB::register_class<TerrainBrush>();
	ClassDB::register_class<TerrainPath>();
	ClassDB::register_class<TerrainPathPoint>();

	// Foliage.
	ClassDB::register_class<FoliageType>();
	ClassDB::register_class<CellFoliage>();
	ClassDB::register_class<FoliageManager>();

	// Scene Layers.
	ClassDB::register_class<GameWorld>();

	// Scene Manager.
	ClassDB::register_class<SceneCatalogEntry>();
	ClassDB::register_class<SceneCatalog>();
	ClassDB::register_class<SceneManager>();
	ClassDB::register_class<SceneTransition>();
	ClassDB::register_class<SceneSpawnPoint>();
	ClassDB::register_class<SceneDoor>();
	ClassDB::register_class<WorldLocationArea>();

	// Items / recipes / categories.
	ClassDB::register_class<CellItemBody>();
	ClassDB::register_class<ItemCategory>();
	ClassDB::register_class<ItemCategoryCatalog>();

	GLOBAL_DEF("cell_system/items/directory", "res://items");
	ProjectSettings::get_singleton()->set_custom_property_info("cell_system/items/directory",
			PropertyInfo(Variant::STRING, "cell_system/items/directory", PROPERTY_HINT_DIR));
	GLOBAL_DEF("cell_system/items/recipes_subdirectory", "recipes");
	GLOBAL_DEF("cell_system/items/category_catalog", "res://items/categories.tres");
	ProjectSettings::get_singleton()->set_custom_property_info("cell_system/items/category_catalog",
			PropertyInfo(Variant::STRING, "cell_system/items/category_catalog", PROPERTY_HINT_FILE, "*.tres"));

	_scene_manager = memnew(SceneManager);
	Engine::get_singleton()->add_singleton(Engine::Singleton("SceneManager", SceneManager::get_singleton()));
	SceneTree::add_idle_callback(SceneManager::idle_callback);

#ifdef TOOLS_ENABLED
	ClassDB::register_class<TexturePackerDialog>();
	EditorPlugins::add_by_type<TerrainEditorPlugin>();
	EditorPlugins::add_by_type<CellWorldEditorPlugin>();
	EditorPlugins::add_by_type<FoliageEditorPlugin>();
	EditorPlugins::add_by_type<TerrainPathEditorPlugin>();
	EditorPlugins::add_by_type<SceneLayerEditorPlugin>();
	EditorPlugins::add_by_type<SceneManagerEditorPlugin>();
	EditorPlugins::add_by_type<ItemsEditorPlugin>();
#endif
}

void unregister_cell_system_types() {
	// ClassDB and EditorPlugins handle teardown.
	if (_scene_manager) {
		memdelete(_scene_manager);
		_scene_manager = nullptr;
	}
}
