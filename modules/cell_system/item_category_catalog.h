#ifndef ITEM_CATEGORY_CATALOG_H
#define ITEM_CATEGORY_CATALOG_H

#include "core/resource.h"

// One main category and the subcategories nested under it.
class ItemCategory : public Resource {
	GDCLASS(ItemCategory, Resource);

	String category_name;
	PoolStringArray subcategories;

protected:
	static void _bind_methods();

public:
	void set_category_name(const String &p_name);
	String get_category_name() const;

	void set_subcategories(const PoolStringArray &p_subs);
	PoolStringArray get_subcategories() const;

	ItemCategory() {}
};

// Project-wide catalog of item categories plus the item -> (main, sub) mapping.
//
// Items themselves only store an `id`; categorisation lives here so the item
// scripts stay untouched. Saved as a .tres at the path in Project Settings
// (`cell_system/items/category_catalog`) and edited from the "Categories"
// sub-tab of the Items & Recipes editor.
class ItemCategoryCatalog : public Resource {
	GDCLASS(ItemCategoryCatalog, Resource);

	// Array of Ref<ItemCategory>.
	Array categories;
	// id (String) -> { "main": String, "sub": String }.
	Dictionary assignments;

	Ref<ItemCategory> _find(const String &p_name) const;

protected:
	static void _bind_methods();

public:
	void set_categories(const Array &p_categories);
	Array get_categories() const;

	void set_assignments(const Dictionary &p_assignments);
	Dictionary get_assignments() const;

	PoolStringArray get_main_categories() const;
	PoolStringArray get_subcategories(const String &p_main) const;

	bool add_main_category(const String &p_name);
	bool remove_main_category(const String &p_name);
	bool rename_main_category(const String &p_old, const String &p_new);

	bool add_subcategory(const String &p_main, const String &p_sub);
	bool remove_subcategory(const String &p_main, const String &p_sub);

	void set_item_category(const String &p_id, const String &p_main, const String &p_sub);
	String get_item_main(const String &p_id) const;
	String get_item_sub(const String &p_id) const;
	void clear_item(const String &p_id);
	PoolStringArray get_assigned_ids() const;

	ItemCategoryCatalog() {}
};

#endif // ITEM_CATEGORY_CATALOG_H
