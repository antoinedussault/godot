#include "items_editor_plugin.h"
#ifdef TOOLS_ENABLED

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/os/dir_access.h"
#include "core/os/file_access.h"
#include "core/project_settings.h"
#include "core/script_language.h"
#include "core/set.h"
#include "editor/editor_file_dialog.h"
#include "editor/editor_file_system.h"
#include "editor/editor_inspector.h"
#include "editor/editor_node.h"
#include "editor/editor_scale.h"
#include "editor/project_settings_editor.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tree.h"

static const char *ITEM_BASE_CLASS = "ItemResource";
static const char *RECIPE_BASE_CLASS = "ItemRecipeResource";

// A scripted resource returns a NIL Variant for an absent property, which
// stringifies to "Null" — always go through the type check.
static String _res_string(const Ref<Resource> &p_res, const String &p_field) {
	if (p_res.is_null()) {
		return String();
	}
	Variant v = p_res->get(p_field);
	return v.get_type() == Variant::STRING ? String(v) : String();
}

//=============================================================
// Script-class helpers
//=============================================================

bool ItemsEditor::_script_inherits(const String &p_class, const String &p_target) {
	String c = p_class;
	int guard = 0;
	while (!c.empty() && guard++ < 64) {
		if (c == p_target) {
			return true;
		}
		if (!ScriptServer::is_global_class(c)) {
			break;
		}
		c = ScriptServer::get_global_class_base(c);
	}
	return false;
}

String ItemsEditor::_global_class_for_script(const String &p_script_path) {
	if (p_script_path.empty()) {
		return String();
	}
	List<StringName> classes;
	ScriptServer::get_global_class_list(&classes);
	for (List<StringName>::Element *E = classes.front(); E; E = E->next()) {
		if (ScriptServer::get_global_class_path(E->get()) == p_script_path) {
			return E->get();
		}
	}
	return String();
}

String ItemsEditor::_resource_base_class(const String &p_res_path) const {
	Ref<Resource> res = ResourceLoader::load(p_res_path, "", false);
	if (res.is_null()) {
		return String();
	}
	Ref<Script> scr = res->get_script();
	if (scr.is_null()) {
		return String();
	}
	return _global_class_for_script(scr->get_path());
}

//=============================================================
// Filesystem
//=============================================================

Vector<String> ItemsEditor::_scan_dir(const String &p_dir) const {
	Vector<String> out;
	if (p_dir.empty()) {
		return out;
	}
	DirAccess *da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (!da) {
		return out;
	}
	if (da->change_dir(p_dir) != OK) {
		memdelete(da);
		return out;
	}
	da->list_dir_begin();
	String n = da->get_next();
	while (!n.empty()) {
		if (n != "." && n != "..") {
			String full = p_dir.plus_file(n);
			if (da->current_is_dir()) {
				Vector<String> sub = _scan_dir(full);
				for (int i = 0; i < sub.size(); i++) {
					out.push_back(sub[i]);
				}
			} else if (n.get_extension() == "tres" || n.get_extension() == "res") {
				out.push_back(full);
			}
		}
		n = da->get_next();
	}
	da->list_dir_end();
	memdelete(da);
	return out;
}

Vector<String> ItemsEditor::_list_resources(const String &p_base_class) const {
	Vector<String> out;
	Vector<String> all = _scan_dir(items_dir);
	for (int i = 0; i < all.size(); i++) {
		String gc = _resource_base_class(all[i]);
		if (!gc.empty() && _script_inherits(gc, p_base_class)) {
			out.push_back(all[i]);
		}
	}
	return out;
}

//=============================================================
// Settings / setup gate
//=============================================================

void ItemsEditor::_read_settings() {
	items_dir = GLOBAL_GET("cell_system/items/directory");
	String sub = GLOBAL_GET("cell_system/items/recipes_subdirectory");
	items_dir = items_dir.strip_edges();
	if (!items_dir.empty() && items_dir.ends_with("/")) {
		items_dir = items_dir.substr(0, items_dir.length() - 1);
	}
	recipes_dir = items_dir.empty() ? String() : items_dir.plus_file(sub.empty() ? String("recipes") : sub);
	catalog_path = String(GLOBAL_GET("cell_system/items/category_catalog")).strip_edges();
}

bool ItemsEditor::_setup_ok() const {
	if (items_dir.empty()) {
		return false;
	}
	DirAccess *da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	bool ok = da && da->dir_exists(items_dir);
	if (da) {
		memdelete(da);
	}
	return ok;
}

void ItemsEditor::_update_visibility() {
	bool ok = _setup_ok();
	setup_panel->set_visible(!ok);
	split->set_visible(ok);
	view_bar->set_visible(ok);

	if (!ok) {
		setup_label->set_text(
				vformat("Set the item folder in Project Settings before editing.\n\n"
						"cell_system/items/directory  =  \"%s\"  (folder not found)",
						items_dir.empty() ? String("<unset>") : items_dir));
		return;
	}

	bool items_like = (view == VIEW_ITEMS || view == VIEW_RECIPES);
	inspector->set_visible(items_like);
	assign_row->set_visible(view == VIEW_ITEMS);
	b_validate->set_visible(view == VIEW_RECIPES);
	validation->set_visible(view == VIEW_RECIPES);
	b_add_sub->set_visible(view == VIEW_CATEGORIES);
	b_dup->set_visible(items_like);

	btn_items->set_pressed(view == VIEW_ITEMS);
	btn_recipes->set_pressed(view == VIEW_RECIPES);
	btn_categories->set_pressed(view == VIEW_CATEGORIES);
}

//=============================================================
// Refresh
//=============================================================

void ItemsEditor::_refresh() {
	_read_settings();
	_load_catalog();
	_update_visibility();
	if (!_setup_ok()) {
		return;
	}
	_rebuild_new_menu();
	if (view == VIEW_CATEGORIES) {
		_refresh_categories_tree();
	} else {
		_refresh_list();
	}
}

void ItemsEditor::_refresh_list() {
	list->set_block_signals(true);
	list->clear();
	list->set_hide_root(true);
	TreeItem *root = list->create_item();

	const char *base = (view == VIEW_RECIPES) ? RECIPE_BASE_CLASS : ITEM_BASE_CLASS;
	Vector<String> paths = _list_resources(base);
	paths.sort();

	bool found_sel = false;
	for (int i = 0; i < paths.size(); i++) {
		TreeItem *it = list->create_item(root);
		it->set_text(0, paths[i].get_file());
		it->set_tooltip(0, paths[i]);
		it->set_metadata(0, paths[i]);
		if (paths[i] == selected_path) {
			it->select(0);
			found_sel = true;
		}
	}
	list->set_block_signals(false);

	if (found_sel) {
		_edit_resource(selected_path);
	} else {
		selected_path = "";
		selected_res.unref();
		inspector->edit(nullptr);
		_populate_category_options();
	}
}

void ItemsEditor::_refresh_categories_tree() {
	list->set_block_signals(true);
	list->clear();
	list->set_hide_root(true);
	TreeItem *root = list->create_item();
	if (catalog.is_null()) {
		list->set_block_signals(false);
		return;
	}
	PoolStringArray mains = catalog->get_main_categories();
	for (int i = 0; i < mains.size(); i++) {
		String m = mains[i];
		TreeItem *mi = list->create_item(root);
		mi->set_text(0, m);
		mi->set_metadata(0, "main:" + m);
		PoolStringArray subs = catalog->get_subcategories(m);
		for (int j = 0; j < subs.size(); j++) {
			TreeItem *si = list->create_item(mi);
			si->set_text(0, subs[j]);
			si->set_metadata(0, "sub:" + m + "/" + String(subs[j]));
		}
		mi->set_collapsed(false);
	}
	list->set_block_signals(false);
}

void ItemsEditor::_select_view(int p_view) {
	view = (View)p_view;
	selected_path = "";
	selected_res.unref();
	inspector->edit(nullptr);
	_refresh();
}

//=============================================================
// Selection / editing
//=============================================================

String ItemsEditor::_selected_tree_meta(int p_col) const {
	TreeItem *ti = list->get_selected();
	return ti ? String(ti->get_metadata(p_col)) : String();
}

void ItemsEditor::_on_item_selected() {
	if (view == VIEW_CATEGORIES) {
		return;
	}
	String path = _selected_tree_meta();
	if (path.empty() || !path.begins_with("res://")) {
		return;
	}
	_edit_resource(path);
}

void ItemsEditor::_edit_resource(const String &p_path) {
	selected_path = p_path;
	selected_res = ResourceLoader::load(p_path, "", false);
	inspector->edit(selected_res.ptr());
	_populate_category_options();
}

//=============================================================
// New resource
//=============================================================

void ItemsEditor::_rebuild_new_menu() {
	new_menu->clear();
	new_menu_classes.clear();

	const char *base = (view == VIEW_RECIPES) ? RECIPE_BASE_CLASS : ITEM_BASE_CLASS;

	List<StringName> classes;
	ScriptServer::get_global_class_list(&classes);
	List<String> names;
	for (List<StringName>::Element *E = classes.front(); E; E = E->next()) {
		if (_script_inherits(E->get(), base)) {
			names.push_back(E->get());
		}
	}
	names.sort();
	for (List<String>::Element *E = names.front(); E; E = E->next()) {
		new_menu->add_item(E->get(), new_menu_classes.size());
		new_menu_classes.push_back(E->get());
	}
	if (new_menu_classes.empty()) {
		new_menu->add_item(vformat("(no script extends %s)", base), 0);
		new_menu->set_item_disabled(0, true);
	}
}

void ItemsEditor::_on_new_pressed() {
	if (view == VIEW_CATEGORIES) {
		dialog_mode = DIALOG_NEW_MAIN;
		dialog->set_title(TTR("New Main Category"));
		dialog_line->set_text("");
		dialog->popup_centered(Size2(320, 90) * EDSCALE);
		dialog_line->grab_focus();
		return;
	}
	Vector2 pos = b_new->get_global_position() + Vector2(0, b_new->get_size().y);
	new_menu->set_position(pos);
	new_menu->set_size(Vector2());
	new_menu->popup();
}

void ItemsEditor::_on_new_menu_id(int p_id) {
	if (p_id < 0 || p_id >= new_menu_classes.size()) {
		return;
	}
	if (view == VIEW_RECIPES) {
		_create_recipe(new_menu_classes[p_id]);
	} else {
		_open_create_dialog(new_menu_classes[p_id]);
	}
}

Ref<Resource> ItemsEditor::_instance_scripted(const String &p_global_class) {
	String script_path = ScriptServer::get_global_class_path(p_global_class);
	Ref<Script> scr = ResourceLoader::load(script_path, "Script");
	if (scr.is_null()) {
		EditorNode::get_singleton()->show_warning(vformat("Could not load script for %s", p_global_class));
		return Ref<Resource>();
	}
	StringName native = ScriptServer::get_global_class_native_base(p_global_class);
	if (native == StringName()) {
		native = "Resource";
	}
	Object *obj = ClassDB::instance(native);
	Resource *r = Object::cast_to<Resource>(obj);
	if (!r) {
		if (obj) {
			memdelete(obj);
		}
		return Ref<Resource>();
	}
	Ref<Resource> res(r);
	res->set_script(scr.get_ref_ptr());
	return res;
}

void ItemsEditor::_collect_item_ids(Set<String> *r_ids) const {
	Vector<String> items = _list_resources(ITEM_BASE_CLASS);
	for (int i = 0; i < items.size(); i++) {
		Ref<Resource> r = ResourceLoader::load(items[i], "", false);
		String id = _res_string(r, "id");
		if (!id.empty()) {
			r_ids->insert(id);
		}
	}
}

String ItemsEditor::_slugify(const String &p_name) {
	String s = p_name.strip_edges().to_lower();
	String slug;
	for (int i = 0; i < s.length(); i++) {
		CharType c = s[i];
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
			slug += String::chr(c);
		} else if (c == ' ' || c == '/' || c == '.' || c == '\t') {
			slug += "_";
		}
	}
	while (slug.find("__") >= 0) {
		slug = slug.replace("__", "_");
	}
	slug = slug.trim_prefix("_").trim_suffix("_");
	return slug.empty() ? String("item") : slug;
}

String ItemsEditor::_make_item_id(const String &p_name) const {
	String slug = _slugify(p_name);

	Set<String> ids;
	_collect_item_ids(&ids);
	String cand = slug;
	int n = 2;
	while (ids.has(cand)) {
		cand = slug + "_" + itos(n++);
	}
	return cand;
}

void ItemsEditor::_create_recipe(const String &p_global_class) {
	Ref<Resource> res = _instance_scripted(p_global_class);
	if (res.is_null()) {
		return;
	}
	DirAccess *da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (da && !da->dir_exists(recipes_dir)) {
		da->make_dir_recursive(recipes_dir);
	}
	if (da) {
		memdelete(da);
	}

	String base_name = p_global_class.to_lower();
	String target = recipes_dir.plus_file(base_name + ".tres");
	int n = 2;
	while (FileAccess::exists(target)) {
		target = recipes_dir.plus_file(vformat("%s_%d.tres", base_name, n++));
	}

	Error err = ResourceSaver::save(target, res);
	if (err != OK) {
		EditorNode::get_singleton()->show_warning(vformat("Failed to save %s (error %d)", target, err));
		return;
	}
	EditorFileSystem::get_singleton()->scan();
	selected_path = target;
	_refresh();
	_edit_resource(target);
}

//=============================================================
// New item dialog (name + category + scene; id derived from name)
//=============================================================

void ItemsEditor::_open_create_dialog(const String &p_global_class) {
	pending_class = p_global_class;
	create_name->set_text("");
	create_scene->set_text("");
	create_id_preview->set_text("");
	_populate_create_categories();
	create_dialog->get_ok()->set_disabled(true);
	create_dialog->set_title(vformat(TTR("New %s"), p_global_class));
	create_dialog->popup_centered(Size2(480, 220) * EDSCALE);
	create_name->grab_focus();
}

void ItemsEditor::_populate_create_categories() {
	create_main->clear();
	create_main->add_item(TTR("<none>"));
	create_main->set_item_metadata(0, "");

	if (catalog.is_valid()) {
		PoolStringArray mains = catalog->get_main_categories();
		for (int i = 0; i < mains.size(); i++) {
			create_main->add_item(mains[i]);
			create_main->set_item_metadata(create_main->get_item_count() - 1, mains[i]);
		}
	}
	create_main->select(0);
	_on_create_main_selected(0);
}

void ItemsEditor::_on_create_main_selected(int p_idx) {
	create_sub->clear();
	create_sub->add_item(TTR("<none>"));
	create_sub->set_item_metadata(0, "");
	String main = create_main->get_item_metadata(p_idx);
	if (main.empty() || catalog.is_null()) {
		create_sub->set_disabled(true);
		return;
	}
	PoolStringArray subs = catalog->get_subcategories(main);
	for (int i = 0; i < subs.size(); i++) {
		create_sub->add_item(subs[i]);
		create_sub->set_item_metadata(create_sub->get_item_count() - 1, subs[i]);
	}
	create_sub->set_disabled(subs.size() == 0);
	create_sub->select(0);
}

void ItemsEditor::_on_create_name_changed(const String &p_text) {
	bool empty = p_text.strip_edges().empty();
	create_dialog->get_ok()->set_disabled(empty);
	create_id_preview->set_text(empty ? String() : vformat(TTR("id: %s"), _slugify(p_text)));
}

void ItemsEditor::_on_create_scene_browse() {
	scene_dialog->popup_centered_ratio(0.6);
}

void ItemsEditor::_on_create_scene_picked(const String &p_path) {
	create_scene->set_text(p_path);
}

void ItemsEditor::_on_create_confirmed() {
	if (pending_class.empty()) {
		return;
	}
	String name = create_name->get_text().strip_edges();
	if (name.empty()) {
		return;
	}
	String main = create_main->get_selected() >= 0 ? String(create_main->get_item_metadata(create_main->get_selected())) : String();
	String sub = (!main.empty() && create_sub->get_selected() >= 0) ? String(create_sub->get_item_metadata(create_sub->get_selected())) : String();
	String scene = create_scene->get_text().strip_edges();

	Ref<Resource> res = _instance_scripted(pending_class);
	if (res.is_null()) {
		return;
	}

	String id = _make_item_id(name);
	res->set("id", id);
	res->set("name", name);
	if (!scene.empty()) {
		res->set("scene_path", scene);
	}

	DirAccess *da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (da && !da->dir_exists(items_dir)) {
		da->make_dir_recursive(items_dir);
	}
	if (da) {
		memdelete(da);
	}

	String target = items_dir.plus_file(id + ".tres");
	Error err = ResourceSaver::save(target, res);
	if (err != OK) {
		EditorNode::get_singleton()->show_warning(vformat("Failed to save %s (error %d)", target, err));
		return;
	}

	if (!main.empty() && catalog.is_valid()) {
		catalog->set_item_category(id, main, sub);
		_save_catalog();
	}

	EditorFileSystem::get_singleton()->scan();
	selected_path = target;
	pending_class = "";
	_refresh();
	_edit_resource(target);
}

//=============================================================
// Duplicate / rename / delete / add-sub
//=============================================================

void ItemsEditor::_on_duplicate_pressed() {
	if (selected_path.empty()) {
		return;
	}
	String target = selected_path.get_basename() + "_copy.tres";
	int n = 1;
	while (FileAccess::exists(target)) {
		target = vformat("%s_copy%d.tres", selected_path.get_basename(), n++);
	}
	DirAccess *da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	Error err = da ? da->copy(selected_path, target) : FAILED;
	if (da) {
		memdelete(da);
	}
	if (err == OK) {
		EditorFileSystem::get_singleton()->scan();
		selected_path = target;
		_refresh();
		_edit_resource(target);
	}
}

void ItemsEditor::_on_rename_pressed() {
	if (view == VIEW_CATEGORIES) {
		String meta = _selected_tree_meta();
		if (!meta.begins_with("main:")) {
			return;
		}
		dialog_mode = DIALOG_RENAME_CAT;
		dialog->set_title(TTR("Rename Main Category"));
		dialog_line->set_text(meta.substr(5));
		dialog->popup_centered(Size2(320, 90) * EDSCALE);
		dialog_line->grab_focus();
		dialog_line->select_all();
		return;
	}
	if (selected_path.empty()) {
		return;
	}
	dialog_mode = DIALOG_RENAME;
	dialog->set_title(TTR("Rename Resource"));
	dialog_line->set_text(selected_path.get_file().get_basename());
	dialog->popup_centered(Size2(320, 90) * EDSCALE);
	dialog_line->grab_focus();
	dialog_line->select_all();
}

void ItemsEditor::_on_delete_pressed() {
	if (view == VIEW_CATEGORIES) {
		String meta = _selected_tree_meta();
		if (meta.empty()) {
			return;
		}
		dialog_mode = DIALOG_DELETE;
		dialog->set_title(TTR("Confirm"));
		dialog_line->set_text("");
		dialog_line->hide();
		dialog->set_text(vformat(TTR("Remove '%s' from the catalog?"), meta.get_slice(":", 1)));
		dialog->popup_centered(Size2(320, 110) * EDSCALE);
		return;
	}
	if (selected_path.empty()) {
		return;
	}
	dialog_mode = DIALOG_DELETE;
	dialog->set_title(TTR("Confirm"));
	dialog_line->set_text("");
	dialog_line->hide();
	dialog->set_text(vformat(TTR("Delete file '%s'? This cannot be undone."), selected_path.get_file()));
	dialog->popup_centered(Size2(340, 110) * EDSCALE);
}

void ItemsEditor::_on_add_sub_pressed() {
	String meta = _selected_tree_meta();
	String main;
	if (meta.begins_with("main:")) {
		main = meta.substr(5);
	} else if (meta.begins_with("sub:")) {
		main = meta.substr(4).get_slice("/", 0);
	}
	if (main.empty()) {
		return;
	}
	dialog_mode = DIALOG_NEW_SUB;
	dialog->set_meta("main", main);
	dialog->set_title(vformat(TTR("New Subcategory of '%s'"), main));
	dialog_line->show();
	dialog_line->set_text("");
	dialog->popup_centered(Size2(320, 90) * EDSCALE);
	dialog_line->grab_focus();
}

void ItemsEditor::_on_dialog_confirmed() {
	String txt = dialog_line->get_text().strip_edges();
	dialog_line->show();

	switch (dialog_mode) {
		case DIALOG_RENAME: {
			if (txt.empty() || selected_path.empty()) {
				break;
			}
			String target = selected_path.get_base_dir().plus_file(txt + ".tres");
			DirAccess *da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
			Error err = da ? da->rename(selected_path, target) : FAILED;
			if (da) {
				memdelete(da);
			}
			if (err == OK) {
				EditorFileSystem::get_singleton()->scan();
				selected_path = target;
				_refresh();
				_edit_resource(target);
			}
		} break;

		case DIALOG_DELETE: {
			if (view == VIEW_CATEGORIES) {
				String meta = _selected_tree_meta();
				if (catalog.is_null()) {
					break;
				}
				if (meta.begins_with("main:")) {
					catalog->remove_main_category(meta.substr(5));
				} else if (meta.begins_with("sub:")) {
					String rest = meta.substr(4);
					catalog->remove_subcategory(rest.get_slice("/", 0), rest.get_slice("/", 1));
				}
				_save_catalog();
				_refresh_categories_tree();
			} else if (!selected_path.empty()) {
				DirAccess *da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
				if (da) {
					da->remove(selected_path);
					memdelete(da);
				}
				EditorFileSystem::get_singleton()->scan();
				selected_path = "";
				selected_res.unref();
				inspector->edit(nullptr);
				_refresh();
			}
		} break;

		case DIALOG_NEW_MAIN: {
			if (!txt.empty() && catalog.is_valid()) {
				catalog->add_main_category(txt);
				_save_catalog();
				_refresh_categories_tree();
			}
		} break;

		case DIALOG_NEW_SUB: {
			String main = dialog->get_meta("main");
			if (!txt.empty() && !main.empty() && catalog.is_valid()) {
				catalog->add_subcategory(main, txt);
				_save_catalog();
				_refresh_categories_tree();
			}
		} break;

		case DIALOG_RENAME_CAT: {
			String meta = _selected_tree_meta();
			if (meta.begins_with("main:") && !txt.empty() && catalog.is_valid()) {
				catalog->rename_main_category(meta.substr(5), txt);
				_save_catalog();
				_refresh_categories_tree();
			}
		} break;

		default:
			break;
	}
	dialog_mode = DIALOG_NONE;
}

//=============================================================
// Project Settings shortcut
//=============================================================

void ItemsEditor::_on_open_settings() {
	EditorNode::get_singleton()->get_project_settings()->popup_project_settings();
}

//=============================================================
// Recipe validation
//=============================================================

void ItemsEditor::_on_validate_pressed() {
	validation->clear();

	// Known item ids.
	Set<String> ids;
	_collect_item_ids(&ids);

	int problems = 0;
	Vector<String> recipes = _list_resources(RECIPE_BASE_CLASS);
	for (int i = 0; i < recipes.size(); i++) {
		Ref<Resource> r = ResourceLoader::load(recipes[i], "", false);
		if (r.is_null()) {
			continue;
		}
		List<PropertyInfo> plist;
		r->get_property_list(&plist);
		for (List<PropertyInfo>::Element *E = plist.front(); E; E = E->next()) {
			const PropertyInfo &pi = E->get();
			if (!(pi.usage & PROPERTY_USAGE_EDITOR)) {
				continue;
			}
			Variant v = r->get(pi.name);
			Vector<String> refs;
			if (v.get_type() == Variant::STRING) {
				String s = v;
				String ln = pi.name.to_lower();
				if (!s.empty() && (ln.find("id") >= 0 || ln.find("item") >= 0 || ln.find("ingredient") >= 0 || ln.find("result") >= 0)) {
					refs.push_back(s);
				}
			} else if (v.get_type() == Variant::ARRAY) {
				Array a = v;
				for (int j = 0; j < a.size(); j++) {
					if (a[j].get_type() == Variant::STRING) {
						refs.push_back(a[j]);
					} else if (a[j].get_type() == Variant::NIL) {
						validation->add_item(vformat("%s : '%s' has an empty (null) entry", recipes[i].get_file(), pi.name));
						problems++;
					}
				}
			}
			for (int j = 0; j < refs.size(); j++) {
				if (!ids.has(refs[j])) {
					validation->add_item(vformat("%s : '%s' -> unknown item id \"%s\"", recipes[i].get_file(), pi.name, refs[j]));
					problems++;
				}
			}
		}
	}

	if (problems == 0) {
		validation->add_item(TTR("No problems found."));
	}
}

//=============================================================
// Category catalog
//=============================================================

void ItemsEditor::_load_catalog() {
	catalog.unref();
	if (catalog_path.empty()) {
		return;
	}
	if (FileAccess::exists(catalog_path)) {
		catalog = ResourceLoader::load(catalog_path, "", false);
	}
	if (catalog.is_null()) {
		catalog.instance();
	}
}

void ItemsEditor::_save_catalog() {
	if (catalog.is_null() || catalog_path.empty()) {
		return;
	}
	DirAccess *da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (da) {
		String d = catalog_path.get_base_dir();
		if (!d.empty() && !da->dir_exists(d)) {
			da->make_dir_recursive(d);
		}
		memdelete(da);
	}
	ResourceSaver::save(catalog_path, catalog);
	EditorFileSystem::get_singleton()->scan();
}

void ItemsEditor::_populate_category_options() {
	opt_main->clear();
	opt_sub->clear();
	opt_main->add_item(TTR("<none>"));
	opt_main->set_item_metadata(0, "");

	bool enabled = selected_res.is_valid() && catalog.is_valid();
	opt_main->set_disabled(!enabled);
	opt_sub->set_disabled(!enabled);
	if (!enabled) {
		return;
	}

	String id = _res_string(selected_res, "id");
	PoolStringArray mains = catalog->get_main_categories();
	String cur_main = catalog->get_item_main(id);
	String cur_sub = catalog->get_item_sub(id);

	int sel = 0;
	for (int i = 0; i < mains.size(); i++) {
		opt_main->add_item(mains[i]);
		opt_main->set_item_metadata(opt_main->get_item_count() - 1, mains[i]);
		if (mains[i] == cur_main) {
			sel = opt_main->get_item_count() - 1;
		}
	}
	opt_main->select(sel);

	opt_sub->add_item(TTR("<none>"));
	opt_sub->set_item_metadata(0, "");
	int subsel = 0;
	if (!cur_main.empty()) {
		PoolStringArray subs = catalog->get_subcategories(cur_main);
		for (int i = 0; i < subs.size(); i++) {
			opt_sub->add_item(subs[i]);
			opt_sub->set_item_metadata(opt_sub->get_item_count() - 1, subs[i]);
			if (subs[i] == cur_sub) {
				subsel = opt_sub->get_item_count() - 1;
			}
		}
	}
	opt_sub->select(subsel);
	opt_sub->set_disabled(cur_main.empty());
}

void ItemsEditor::_on_main_selected(int p_idx) {
	if (selected_res.is_null() || catalog.is_null()) {
		return;
	}
	String id = _res_string(selected_res, "id");
	if (id.empty()) {
		EditorNode::get_singleton()->show_warning(TTR("This item has no 'id' set; category cannot be stored."));
		return;
	}
	String main = opt_main->get_item_metadata(p_idx);
	catalog->set_item_category(id, main, "");
	_save_catalog();
	_populate_category_options();
}

void ItemsEditor::_on_sub_selected(int p_idx) {
	if (selected_res.is_null() || catalog.is_null()) {
		return;
	}
	String id = _res_string(selected_res, "id");
	if (id.empty()) {
		return;
	}
	String main = catalog->get_item_main(id);
	String sub = opt_sub->get_item_metadata(p_idx);
	catalog->set_item_category(id, main, sub);
	_save_catalog();
}

//=============================================================
// Lifecycle / UI construction
//=============================================================

void ItemsEditor::_notification(int p_what) {
	if (p_what == NOTIFICATION_VISIBILITY_CHANGED && is_visible_in_tree()) {
		_refresh();
	}
}

void ItemsEditor::_bind_methods() {
	ClassDB::bind_method("_select_view", &ItemsEditor::_select_view);
	ClassDB::bind_method("_on_item_selected", &ItemsEditor::_on_item_selected);
	ClassDB::bind_method("_on_new_pressed", &ItemsEditor::_on_new_pressed);
	ClassDB::bind_method("_on_new_menu_id", &ItemsEditor::_on_new_menu_id);
	ClassDB::bind_method("_on_duplicate_pressed", &ItemsEditor::_on_duplicate_pressed);
	ClassDB::bind_method("_on_rename_pressed", &ItemsEditor::_on_rename_pressed);
	ClassDB::bind_method("_on_delete_pressed", &ItemsEditor::_on_delete_pressed);
	ClassDB::bind_method("_on_add_sub_pressed", &ItemsEditor::_on_add_sub_pressed);
	ClassDB::bind_method("_on_dialog_confirmed", &ItemsEditor::_on_dialog_confirmed);
	ClassDB::bind_method("_on_create_main_selected", &ItemsEditor::_on_create_main_selected);
	ClassDB::bind_method("_on_create_name_changed", &ItemsEditor::_on_create_name_changed);
	ClassDB::bind_method("_on_create_scene_browse", &ItemsEditor::_on_create_scene_browse);
	ClassDB::bind_method("_on_create_scene_picked", &ItemsEditor::_on_create_scene_picked);
	ClassDB::bind_method("_on_create_confirmed", &ItemsEditor::_on_create_confirmed);
	ClassDB::bind_method("_on_open_settings", &ItemsEditor::_on_open_settings);
	ClassDB::bind_method("_on_validate_pressed", &ItemsEditor::_on_validate_pressed);
	ClassDB::bind_method("_on_main_selected", &ItemsEditor::_on_main_selected);
	ClassDB::bind_method("_on_sub_selected", &ItemsEditor::_on_sub_selected);
	ClassDB::bind_method("refresh", &ItemsEditor::refresh);
}

ItemsEditor::ItemsEditor() {
	set_v_size_flags(SIZE_EXPAND_FILL);
	set_h_size_flags(SIZE_EXPAND_FILL);

	view_bar = memnew(HBoxContainer);
	add_child(view_bar);
	btn_items = memnew(Button);
	btn_items->set_text(TTR("Items"));
	btn_items->set_toggle_mode(true);
	btn_items->set_pressed(true);
	btn_items->connect("pressed", this, "_select_view", varray(VIEW_ITEMS));
	view_bar->add_child(btn_items);
	btn_recipes = memnew(Button);
	btn_recipes->set_text(TTR("Recipes"));
	btn_recipes->set_toggle_mode(true);
	btn_recipes->connect("pressed", this, "_select_view", varray(VIEW_RECIPES));
	view_bar->add_child(btn_recipes);
	btn_categories = memnew(Button);
	btn_categories->set_text(TTR("Categories"));
	btn_categories->set_toggle_mode(true);
	btn_categories->connect("pressed", this, "_select_view", varray(VIEW_CATEGORIES));
	view_bar->add_child(btn_categories);

	// Setup gate.
	setup_panel = memnew(PanelContainer);
	setup_panel->set_v_size_flags(SIZE_EXPAND_FILL);
	add_child(setup_panel);
	VBoxContainer *sv = memnew(VBoxContainer);
	sv->set_alignment(BoxContainer::ALIGN_CENTER);
	setup_panel->add_child(sv);
	setup_label = memnew(Label);
	setup_label->set_align(Label::ALIGN_CENTER);
	sv->add_child(setup_label);
	Button *open_settings = memnew(Button);
	open_settings->set_text(TTR("Open Project Settings"));
	open_settings->connect("pressed", this, "_on_open_settings");
	sv->add_child(open_settings);

	// Main body.
	split = memnew(HSplitContainer);
	split->set_v_size_flags(SIZE_EXPAND_FILL);
	split->set_split_offset(260 * EDSCALE);
	add_child(split);

	VBoxContainer *left = memnew(VBoxContainer);
	left->set_h_size_flags(SIZE_EXPAND_FILL);
	split->add_child(left);

	HBoxContainer *toolbar = memnew(HBoxContainer);
	left->add_child(toolbar);
	b_new = memnew(Button);
	b_new->set_text(TTR("New"));
	b_new->connect("pressed", this, "_on_new_pressed");
	toolbar->add_child(b_new);
	b_add_sub = memnew(Button);
	b_add_sub->set_text(TTR("+ Sub"));
	b_add_sub->connect("pressed", this, "_on_add_sub_pressed");
	toolbar->add_child(b_add_sub);
	b_dup = memnew(Button);
	b_dup->set_text(TTR("Duplicate"));
	b_dup->connect("pressed", this, "_on_duplicate_pressed");
	toolbar->add_child(b_dup);
	b_ren = memnew(Button);
	b_ren->set_text(TTR("Rename"));
	b_ren->connect("pressed", this, "_on_rename_pressed");
	toolbar->add_child(b_ren);
	b_del = memnew(Button);
	b_del->set_text(TTR("Delete"));
	b_del->connect("pressed", this, "_on_delete_pressed");
	toolbar->add_child(b_del);

	list = memnew(Tree);
	list->set_v_size_flags(SIZE_EXPAND_FILL);
	list->set_hide_root(true);
	list->set_allow_reselect(true);
	list->connect("item_selected", this, "_on_item_selected");
	left->add_child(list);

	VBoxContainer *right = memnew(VBoxContainer);
	right->set_h_size_flags(SIZE_EXPAND_FILL);
	split->add_child(right);

	assign_row = memnew(HBoxContainer);
	right->add_child(assign_row);
	Label *cl = memnew(Label);
	cl->set_text(TTR("Category:"));
	assign_row->add_child(cl);
	opt_main = memnew(OptionButton);
	opt_main->set_h_size_flags(SIZE_EXPAND_FILL);
	opt_main->connect("item_selected", this, "_on_main_selected");
	assign_row->add_child(opt_main);
	opt_sub = memnew(OptionButton);
	opt_sub->set_h_size_flags(SIZE_EXPAND_FILL);
	opt_sub->connect("item_selected", this, "_on_sub_selected");
	assign_row->add_child(opt_sub);

	inspector = memnew(EditorInspector);
	inspector->set_v_size_flags(SIZE_EXPAND_FILL);
	inspector->set_undo_redo(EditorNode::get_undo_redo());
	right->add_child(inspector);

	b_validate = memnew(Button);
	b_validate->set_text(TTR("Validate Recipes"));
	b_validate->connect("pressed", this, "_on_validate_pressed");
	right->add_child(b_validate);
	validation = memnew(ItemList);
	validation->set_custom_minimum_size(Size2(0, 140) * EDSCALE);
	right->add_child(validation);

	new_menu = memnew(PopupMenu);
	new_menu->connect("id_pressed", this, "_on_new_menu_id");
	add_child(new_menu);

	dialog = memnew(ConfirmationDialog);
	dialog->connect("confirmed", this, "_on_dialog_confirmed");
	dialog_line = memnew(LineEdit);
	dialog->add_child(dialog_line);
	dialog->register_text_enter(dialog_line);
	add_child(dialog);

	// "New item" dialog.
	create_dialog = memnew(ConfirmationDialog);
	create_dialog->set_title(TTR("New Item"));
	create_dialog->connect("confirmed", this, "_on_create_confirmed");
	{
		GridContainer *grid = memnew(GridContainer);
		grid->set_columns(2);
		grid->set_h_size_flags(SIZE_EXPAND_FILL);
		create_dialog->add_child(grid);

		Label *l1 = memnew(Label);
		l1->set_text(TTR("Name"));
		grid->add_child(l1);
		create_name = memnew(LineEdit);
		create_name->set_h_size_flags(SIZE_EXPAND_FILL);
		create_name->connect("text_changed", this, "_on_create_name_changed");
		grid->add_child(create_name);
		create_dialog->register_text_enter(create_name);

		grid->add_child(memnew(Control)); // spacer under "Name"
		create_id_preview = memnew(Label);
		create_id_preview->add_color_override("font_color", Color(1, 1, 1, 0.5));
		grid->add_child(create_id_preview);

		Label *l2 = memnew(Label);
		l2->set_text(TTR("Main Category"));
		grid->add_child(l2);
		create_main = memnew(OptionButton);
		create_main->set_h_size_flags(SIZE_EXPAND_FILL);
		create_main->connect("item_selected", this, "_on_create_main_selected");
		grid->add_child(create_main);

		Label *l3 = memnew(Label);
		l3->set_text(TTR("Subcategory"));
		grid->add_child(l3);
		create_sub = memnew(OptionButton);
		create_sub->set_h_size_flags(SIZE_EXPAND_FILL);
		grid->add_child(create_sub);

		Label *l4 = memnew(Label);
		l4->set_text(TTR("Scene"));
		grid->add_child(l4);
		HBoxContainer *scene_row = memnew(HBoxContainer);
		scene_row->set_h_size_flags(SIZE_EXPAND_FILL);
		grid->add_child(scene_row);
		create_scene = memnew(LineEdit);
		create_scene->set_h_size_flags(SIZE_EXPAND_FILL);
		create_scene->set_placeholder(TTR("res://... .tscn (optional)"));
		scene_row->add_child(create_scene);
		Button *scene_browse = memnew(Button);
		scene_browse->set_text("...");
		scene_browse->connect("pressed", this, "_on_create_scene_browse");
		scene_row->add_child(scene_browse);
	}
	add_child(create_dialog);

	scene_dialog = memnew(EditorFileDialog);
	scene_dialog->set_access(EditorFileDialog::ACCESS_RESOURCES);
	scene_dialog->set_mode(EditorFileDialog::MODE_OPEN_FILE);
	scene_dialog->add_filter("*.tscn;PackedScene");
	scene_dialog->connect("file_selected", this, "_on_create_scene_picked");
	add_child(scene_dialog);

	_read_settings();
	_update_visibility();
}

//=============================================================
// Plugin wrapper
//=============================================================

const Ref<Texture> ItemsEditorPlugin::get_icon() const {
	Control *gb = EditorNode::get_singleton()->get_gui_base();
	if (gb && gb->has_icon("Object", "EditorIcons")) {
		return gb->get_icon("Object", "EditorIcons");
	}
	return Ref<Texture>();
}

void ItemsEditorPlugin::make_visible(bool p_visible) {
	if (editor_control) {
		editor_control->set_visible(p_visible);
		if (p_visible) {
			editor_control->refresh();
		}
	}
}

ItemsEditorPlugin::ItemsEditorPlugin(EditorNode *p_node) {
	editor_control = memnew(ItemsEditor);
	editor_control->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	p_node->get_viewport()->add_child(editor_control);
	editor_control->set_anchors_and_margins_preset(Control::PRESET_WIDE);
	editor_control->hide();
}

#endif // TOOLS_ENABLED
