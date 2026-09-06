#include "scene_catalog.h"

// ---- SceneCatalogEntry ----

void SceneCatalogEntry::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_id", "id"), &SceneCatalogEntry::set_id);
	ClassDB::bind_method(D_METHOD("get_id"), &SceneCatalogEntry::get_id);
	ClassDB::bind_method(D_METHOD("set_scene_path", "path"), &SceneCatalogEntry::set_scene_path);
	ClassDB::bind_method(D_METHOD("get_scene_path"), &SceneCatalogEntry::get_scene_path);
	ClassDB::bind_method(D_METHOD("set_display_name", "name"), &SceneCatalogEntry::set_display_name);
	ClassDB::bind_method(D_METHOD("get_display_name"), &SceneCatalogEntry::get_display_name);
	ClassDB::bind_method(D_METHOD("set_kind", "kind"), &SceneCatalogEntry::set_kind);
	ClassDB::bind_method(D_METHOD("get_kind"), &SceneCatalogEntry::get_kind);
	ClassDB::bind_method(D_METHOD("set_default_spawn", "spawn_id"), &SceneCatalogEntry::set_default_spawn);
	ClassDB::bind_method(D_METHOD("get_default_spawn"), &SceneCatalogEntry::get_default_spawn);
	ClassDB::bind_method(D_METHOD("set_keep_loaded", "keep"), &SceneCatalogEntry::set_keep_loaded);
	ClassDB::bind_method(D_METHOD("get_keep_loaded"), &SceneCatalogEntry::get_keep_loaded);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "id"), "set_id", "get_id");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "scene_path", PROPERTY_HINT_FILE, "*.tscn,*.scn,*.res"), "set_scene_path", "get_scene_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name"), "set_display_name", "get_display_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "kind", PROPERTY_HINT_ENUM, "Interior,Exterior"), "set_kind", "get_kind");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "default_spawn"), "set_default_spawn", "get_default_spawn");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "keep_loaded"), "set_keep_loaded", "get_keep_loaded");

	BIND_ENUM_CONSTANT(KIND_INTERIOR);
	BIND_ENUM_CONSTANT(KIND_EXTERIOR);
}

// ---- SceneCatalog ----

void SceneCatalog::set_entries(const Array &p_entries) {
	entries.clear();
	entries.resize(p_entries.size());
	for (int i = 0; i < p_entries.size(); i++) {
		entries.write[i] = p_entries[i];
	}
}

Array SceneCatalog::get_entries() const {
	Array out;
	out.resize(entries.size());
	for (int i = 0; i < entries.size(); i++) {
		out[i] = entries[i];
	}
	return out;
}

Ref<SceneCatalogEntry> SceneCatalog::get_entry(const String &p_id) const {
	for (int i = 0; i < entries.size(); i++) {
		const Ref<SceneCatalogEntry> &e = entries[i];
		if (e.is_valid() && e->get_id() == p_id) {
			return e;
		}
	}
	return Ref<SceneCatalogEntry>();
}

Ref<SceneCatalogEntry> SceneCatalog::get_entry_by_index(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, entries.size(), Ref<SceneCatalogEntry>());
	return entries[p_idx];
}

bool SceneCatalog::has_id(const String &p_id) const {
	return get_entry(p_id).is_valid();
}

String SceneCatalog::get_scene_path(const String &p_id) const {
	Ref<SceneCatalogEntry> e = get_entry(p_id);
	return e.is_valid() ? e->get_scene_path() : String();
}

String SceneCatalog::get_default_spawn(const String &p_id) const {
	Ref<SceneCatalogEntry> e = get_entry(p_id);
	return e.is_valid() ? e->get_default_spawn() : String();
}

PoolStringArray SceneCatalog::get_ids() const {
	PoolStringArray out;
	for (int i = 0; i < entries.size(); i++) {
		if (entries[i].is_valid()) {
			out.push_back(entries[i]->get_id());
		}
	}
	return out;
}

void SceneCatalog::add_entry(const Ref<SceneCatalogEntry> &p_entry) {
	ERR_FAIL_COND(p_entry.is_null());
	entries.push_back(p_entry);
	_change_notify("entries");
}

void SceneCatalog::remove_entry(const String &p_id) {
	for (int i = 0; i < entries.size(); i++) {
		if (entries[i].is_valid() && entries[i]->get_id() == p_id) {
			entries.remove(i);
			_change_notify("entries");
			return;
		}
	}
}

void SceneCatalog::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_entries", "entries"), &SceneCatalog::set_entries);
	ClassDB::bind_method(D_METHOD("get_entries"), &SceneCatalog::get_entries);
	ClassDB::bind_method(D_METHOD("get_entry_count"), &SceneCatalog::get_entry_count);
	ClassDB::bind_method(D_METHOD("get_entry", "id"), &SceneCatalog::get_entry);
	ClassDB::bind_method(D_METHOD("get_entry_by_index", "index"), &SceneCatalog::get_entry_by_index);
	ClassDB::bind_method(D_METHOD("has_id", "id"), &SceneCatalog::has_id);
	ClassDB::bind_method(D_METHOD("get_scene_path", "id"), &SceneCatalog::get_scene_path);
	ClassDB::bind_method(D_METHOD("get_default_spawn", "id"), &SceneCatalog::get_default_spawn);
	ClassDB::bind_method(D_METHOD("get_ids"), &SceneCatalog::get_ids);
	ClassDB::bind_method(D_METHOD("add_entry", "entry"), &SceneCatalog::add_entry);
	ClassDB::bind_method(D_METHOD("remove_entry", "id"), &SceneCatalog::remove_entry);

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "entries", PROPERTY_HINT_TYPE_STRING,
						 itos(Variant::OBJECT) + "/" + itos(PROPERTY_HINT_RESOURCE_TYPE) + ":SceneCatalogEntry"),
			"set_entries", "get_entries");
}
