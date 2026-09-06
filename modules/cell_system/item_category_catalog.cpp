#include "item_category_catalog.h"

//=============================================================
// ItemCategory
//=============================================================

void ItemCategory::set_category_name(const String &p_name) {
	category_name = p_name;
	emit_changed();
}

String ItemCategory::get_category_name() const {
	return category_name;
}

void ItemCategory::set_subcategories(const PoolStringArray &p_subs) {
	subcategories = p_subs;
	emit_changed();
}

PoolStringArray ItemCategory::get_subcategories() const {
	return subcategories;
}

void ItemCategory::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_category_name", "name"), &ItemCategory::set_category_name);
	ClassDB::bind_method(D_METHOD("get_category_name"), &ItemCategory::get_category_name);
	ClassDB::bind_method(D_METHOD("set_subcategories", "subs"), &ItemCategory::set_subcategories);
	ClassDB::bind_method(D_METHOD("get_subcategories"), &ItemCategory::get_subcategories);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "category_name"), "set_category_name", "get_category_name");
	ADD_PROPERTY(PropertyInfo(Variant::POOL_STRING_ARRAY, "subcategories"), "set_subcategories", "get_subcategories");
}

//=============================================================
// ItemCategoryCatalog
//=============================================================

Ref<ItemCategory> ItemCategoryCatalog::_find(const String &p_name) const {
	for (int i = 0; i < categories.size(); i++) {
		Ref<ItemCategory> c = categories[i];
		if (c.is_valid() && c->get_category_name() == p_name) {
			return c;
		}
	}
	return Ref<ItemCategory>();
}

void ItemCategoryCatalog::set_categories(const Array &p_categories) {
	categories = p_categories;
	emit_changed();
}

Array ItemCategoryCatalog::get_categories() const {
	return categories;
}

void ItemCategoryCatalog::set_assignments(const Dictionary &p_assignments) {
	assignments = p_assignments;
	emit_changed();
}

Dictionary ItemCategoryCatalog::get_assignments() const {
	return assignments;
}

PoolStringArray ItemCategoryCatalog::get_main_categories() const {
	PoolStringArray out;
	for (int i = 0; i < categories.size(); i++) {
		Ref<ItemCategory> c = categories[i];
		if (c.is_valid()) {
			out.push_back(c->get_category_name());
		}
	}
	return out;
}

PoolStringArray ItemCategoryCatalog::get_subcategories(const String &p_main) const {
	Ref<ItemCategory> c = _find(p_main);
	if (c.is_valid()) {
		return c->get_subcategories();
	}
	return PoolStringArray();
}

bool ItemCategoryCatalog::add_main_category(const String &p_name) {
	if (p_name.empty() || _find(p_name).is_valid()) {
		return false;
	}
	Ref<ItemCategory> c;
	c.instance();
	c->set_category_name(p_name);
	categories.push_back(c);
	emit_changed();
	return true;
}

bool ItemCategoryCatalog::remove_main_category(const String &p_name) {
	for (int i = 0; i < categories.size(); i++) {
		Ref<ItemCategory> c = categories[i];
		if (c.is_valid() && c->get_category_name() == p_name) {
			categories.remove(i);
			// Drop assignments that pointed at this main category.
			Array keys = assignments.keys();
			for (int k = 0; k < keys.size(); k++) {
				Dictionary a = assignments[keys[k]];
				if (String(a.get("main", "")) == p_name) {
					assignments.erase(keys[k]);
				}
			}
			emit_changed();
			return true;
		}
	}
	return false;
}

bool ItemCategoryCatalog::rename_main_category(const String &p_old, const String &p_new) {
	if (p_new.empty() || _find(p_new).is_valid()) {
		return false;
	}
	Ref<ItemCategory> c = _find(p_old);
	if (c.is_null()) {
		return false;
	}
	c->set_category_name(p_new);
	Array keys = assignments.keys();
	for (int k = 0; k < keys.size(); k++) {
		Dictionary a = assignments[keys[k]];
		if (String(a.get("main", "")) == p_old) {
			a["main"] = p_new;
			assignments[keys[k]] = a;
		}
	}
	emit_changed();
	return true;
}

bool ItemCategoryCatalog::add_subcategory(const String &p_main, const String &p_sub) {
	Ref<ItemCategory> c = _find(p_main);
	if (c.is_null() || p_sub.empty()) {
		return false;
	}
	PoolStringArray subs = c->get_subcategories();
	for (int i = 0; i < subs.size(); i++) {
		if (subs[i] == p_sub) {
			return false;
		}
	}
	subs.push_back(p_sub);
	c->set_subcategories(subs);
	emit_changed();
	return true;
}

bool ItemCategoryCatalog::remove_subcategory(const String &p_main, const String &p_sub) {
	Ref<ItemCategory> c = _find(p_main);
	if (c.is_null()) {
		return false;
	}
	PoolStringArray subs = c->get_subcategories();
	for (int i = 0; i < subs.size(); i++) {
		if (subs[i] == p_sub) {
			subs.remove(i);
			c->set_subcategories(subs);
			Array keys = assignments.keys();
			for (int k = 0; k < keys.size(); k++) {
				Dictionary a = assignments[keys[k]];
				if (String(a.get("main", "")) == p_main && String(a.get("sub", "")) == p_sub) {
					a["sub"] = "";
					assignments[keys[k]] = a;
				}
			}
			emit_changed();
			return true;
		}
	}
	return false;
}

void ItemCategoryCatalog::set_item_category(const String &p_id, const String &p_main, const String &p_sub) {
	if (p_id.empty()) {
		return;
	}
	if (p_main.empty()) {
		assignments.erase(p_id);
	} else {
		Dictionary a;
		a["main"] = p_main;
		a["sub"] = p_sub;
		assignments[p_id] = a;
	}
	emit_changed();
}

String ItemCategoryCatalog::get_item_main(const String &p_id) const {
	if (!assignments.has(p_id)) {
		return String();
	}
	Dictionary a = assignments[p_id];
	return a.get("main", "");
}

String ItemCategoryCatalog::get_item_sub(const String &p_id) const {
	if (!assignments.has(p_id)) {
		return String();
	}
	Dictionary a = assignments[p_id];
	return a.get("sub", "");
}

void ItemCategoryCatalog::clear_item(const String &p_id) {
	if (assignments.has(p_id)) {
		assignments.erase(p_id);
		emit_changed();
	}
}

PoolStringArray ItemCategoryCatalog::get_assigned_ids() const {
	PoolStringArray out;
	Array keys = assignments.keys();
	for (int i = 0; i < keys.size(); i++) {
		out.push_back(keys[i]);
	}
	return out;
}

void ItemCategoryCatalog::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_categories", "categories"), &ItemCategoryCatalog::set_categories);
	ClassDB::bind_method(D_METHOD("get_categories"), &ItemCategoryCatalog::get_categories);
	ClassDB::bind_method(D_METHOD("set_assignments", "assignments"), &ItemCategoryCatalog::set_assignments);
	ClassDB::bind_method(D_METHOD("get_assignments"), &ItemCategoryCatalog::get_assignments);

	ClassDB::bind_method(D_METHOD("get_main_categories"), &ItemCategoryCatalog::get_main_categories);
	ClassDB::bind_method(D_METHOD("get_subcategories", "main"), &ItemCategoryCatalog::get_subcategories);
	ClassDB::bind_method(D_METHOD("add_main_category", "name"), &ItemCategoryCatalog::add_main_category);
	ClassDB::bind_method(D_METHOD("remove_main_category", "name"), &ItemCategoryCatalog::remove_main_category);
	ClassDB::bind_method(D_METHOD("rename_main_category", "old_name", "new_name"), &ItemCategoryCatalog::rename_main_category);
	ClassDB::bind_method(D_METHOD("add_subcategory", "main", "sub"), &ItemCategoryCatalog::add_subcategory);
	ClassDB::bind_method(D_METHOD("remove_subcategory", "main", "sub"), &ItemCategoryCatalog::remove_subcategory);
	ClassDB::bind_method(D_METHOD("set_item_category", "id", "main", "sub"), &ItemCategoryCatalog::set_item_category);
	ClassDB::bind_method(D_METHOD("get_item_main", "id"), &ItemCategoryCatalog::get_item_main);
	ClassDB::bind_method(D_METHOD("get_item_sub", "id"), &ItemCategoryCatalog::get_item_sub);
	ClassDB::bind_method(D_METHOD("clear_item", "id"), &ItemCategoryCatalog::clear_item);
	ClassDB::bind_method(D_METHOD("get_assigned_ids"), &ItemCategoryCatalog::get_assigned_ids);

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "categories", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR),
			"set_categories", "get_categories");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "assignments", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR),
			"set_assignments", "get_assignments");
}
