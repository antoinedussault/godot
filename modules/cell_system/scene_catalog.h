#ifndef SCENE_CATALOG_H
#define SCENE_CATALOG_H

#include "core/array.h"
#include "core/resource.h"
#include "core/vector.h"

// The "cell list": a project-authored registry of every travellable location.
// One SceneCatalog .tres is kept under version control; SceneDoor and
// SceneManager::travel_to() refer to scenes by their string id, never by raw
// path, so a scene can be moved on disk without touching every door.
//
// A "scene" here is a whole PackedScene (.tscn) loaded one at a time as the
// SceneTree's current scene. An exterior location that needs streamed terrain
// simply contains its own CellTerrainManager inside that .tscn.

class SceneCatalogEntry : public Resource {
	GDCLASS(SceneCatalogEntry, Resource);

public:
	enum Kind {
		KIND_INTERIOR = 0,
		KIND_EXTERIOR = 1,
	};

private:
	String id;
	String scene_path; // res:// path to the .tscn — kept as a path, not a
	                   // Ref<PackedScene>, so the catalog stays lazy.
	String display_name;
	Kind kind = KIND_INTERIOR;
	String default_spawn; // spawn_id used when travel_to() is given no spawn.
	bool keep_loaded = false; // reserved; not honoured yet (see README).

protected:
	static void _bind_methods();

public:
	void set_id(const String &p_id) { id = p_id; }
	String get_id() const { return id; }

	void set_scene_path(const String &p_path) { scene_path = p_path; }
	String get_scene_path() const { return scene_path; }

	void set_display_name(const String &p_name) { display_name = p_name; }
	String get_display_name() const { return display_name; }

	void set_kind(int p_kind) { kind = (Kind)p_kind; }
	int get_kind() const { return kind; }

	void set_default_spawn(const String &p_spawn) { default_spawn = p_spawn; }
	String get_default_spawn() const { return default_spawn; }

	void set_keep_loaded(bool p_keep) { keep_loaded = p_keep; }
	bool get_keep_loaded() const { return keep_loaded; }

	SceneCatalogEntry() {}
};

VARIANT_ENUM_CAST(SceneCatalogEntry::Kind);

class SceneCatalog : public Resource {
	GDCLASS(SceneCatalog, Resource);

	Vector<Ref<SceneCatalogEntry> > entries;

protected:
	static void _bind_methods();

public:
	void set_entries(const Array &p_entries);
	Array get_entries() const;

	int get_entry_count() const { return entries.size(); }
	Ref<SceneCatalogEntry> get_entry(const String &p_id) const;
	Ref<SceneCatalogEntry> get_entry_by_index(int p_idx) const;
	bool has_id(const String &p_id) const;
	String get_scene_path(const String &p_id) const;
	String get_default_spawn(const String &p_id) const;
	PoolStringArray get_ids() const;

	// Editor helpers (used by SceneManagerEditorPlugin).
	void add_entry(const Ref<SceneCatalogEntry> &p_entry);
	void remove_entry(const String &p_id);

	SceneCatalog() {}
};

#endif // SCENE_CATALOG_H
