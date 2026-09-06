#include "scene_layer_editor_plugin.h"
#ifdef TOOLS_ENABLED

#include "editor_ui_helpers.h"
#include "game_world.h"
#include "core/undo_redo.h"
#include "editor/editor_data.h"
#include "editor/editor_node.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/tree.h"

// --- Layer-data tree helpers ---
// layer_data shape: { "next_id": int, "layers": Array }, each entry (and
// recursively each entry's "children"): { "id": int, "name": String,
// "visible": bool, "children": Array }. These operate on an already-deep-
// duplicated Array (see _commit_layer_data callers) so in-place mutation
// here never aliases the live GameWorld data.

static bool _find_and_set_field(Array &p_layers, int p_id, const String &p_field, const Variant &p_value) {
	for (int i = 0; i < p_layers.size(); i++) {
		Dictionary d = p_layers[i];
		int id = d.has("id") ? (int)d["id"] : -1;
		if (id == p_id) {
			d[p_field] = p_value;
			p_layers[i] = d;
			return true;
		}
		Array children = d.has("children") ? (Array)d["children"] : Array();
		if (_find_and_set_field(children, p_id, p_field, p_value)) {
			d["children"] = children;
			p_layers[i] = d;
			return true;
		}
	}
	return false;
}

static bool _find_and_delete(Array &p_layers, int p_id) {
	for (int i = 0; i < p_layers.size(); i++) {
		Dictionary d = p_layers[i];
		int id = d.has("id") ? (int)d["id"] : -1;
		if (id == p_id) {
			p_layers.remove(i);
			return true;
		}
		Array children = d.has("children") ? (Array)d["children"] : Array();
		if (_find_and_delete(children, p_id)) {
			d["children"] = children;
			p_layers[i] = d;
			return true;
		}
	}
	return false;
}

static bool _find_and_add_child(Array &p_layers, int p_id, const Dictionary &p_new_child) {
	for (int i = 0; i < p_layers.size(); i++) {
		Dictionary d = p_layers[i];
		int id = d.has("id") ? (int)d["id"] : -1;
		Array children = d.has("children") ? (Array)d["children"] : Array();
		if (id == p_id) {
			children.append(p_new_child);
			d["children"] = children;
			p_layers[i] = d;
			return true;
		}
		if (_find_and_add_child(children, p_id, p_new_child)) {
			d["children"] = children;
			p_layers[i] = d;
			return true;
		}
	}
	return false;
}

// Effective visibility = this layer's own flag AND every ancestor's — hiding
// a parent layer hides everything nested under it, regardless of the child
// layers' own toggle state.
static void _collect_effective_visibility(const Array &p_layers, bool p_parent_visible, Map<int, bool> &r_out) {
	for (int i = 0; i < p_layers.size(); i++) {
		Dictionary d = p_layers[i];
		int id = d.has("id") ? (int)d["id"] : -1;
		bool own = d.has("visible") ? (bool)d["visible"] : true;
		bool eff = p_parent_visible && own;
		r_out[id] = eff;
		Array children = d.has("children") ? (Array)d["children"] : Array();
		_collect_effective_visibility(children, eff, r_out);
	}
}

SceneLayerEditorPlugin::SceneLayerEditorPlugin(EditorNode *p_node) {
	_build_dock();
}

void SceneLayerEditorPlugin::_build_dock() {
	dock_root = cs_make_dock_root(&dock_content);
	dock_root->set_name("SceneLayerDock");

	HBoxContainer *toolbar = memnew(HBoxContainer);
	Button *new_layer_btn = memnew(Button);
	new_layer_btn->set_text("New Layer");
	new_layer_btn->connect("pressed", this, "_on_new_layer_pressed");
	toolbar->add_child(new_layer_btn);

	Button *new_sublayer_btn = memnew(Button);
	new_sublayer_btn->set_text("New Sub-Layer");
	new_sublayer_btn->set_tooltip("Adds a sub-layer under the currently selected layer row");
	new_sublayer_btn->connect("pressed", this, "_on_new_sublayer_pressed");
	toolbar->add_child(new_sublayer_btn);
	dock_content->add_child(toolbar);

	tree = memnew(Tree);
	tree->set_hide_root(true);
	// Two columns, not one: a CELL_MODE_CHECK cell's click handling is the
	// checkbox toggle itself, so cramming the (also editable, double-click-
	// to-rename) name into the same cell made selecting a row to use as the
	// New Sub-Layer parent, or renaming, fight with toggling its visibility.
	// Column 0 = visibility checkbox only; column 1 = name (+ the action
	// buttons) — normal CELL_MODE_STRING, so selection/rename behave normally.
	tree->set_columns(2);
	tree->set_column_expand(0, false);
	tree->set_column_min_width(0, 28);
	// Without this, Tree's default (allow_reselect=false) treats *any* click
	// on an unselected editable cell as bring_up_editor=true — i.e. the very
	// click that selects a row also immediately arms rename-edit mode on it,
	// which is what made both "just select a row" and "double-click to
	// rename" feel broken. true requires the cell to already be selected
	// before a click opens the editor — normal click-to-select-then-
	// click/double-click-to-rename behavior.
	tree->set_allow_reselect(true);
	tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tree->set_custom_minimum_size(Size2(0, 220));
	tree->connect("item_edited", this, "_on_item_edited");
	tree->connect("button_pressed", this, "_on_button_pressed");
	dock_content->add_child(tree);
}

void SceneLayerEditorPlugin::edited_scene_changed() {
	Node *root = get_editor_interface()->get_edited_scene_root();
	GameWorld *gw = Object::cast_to<GameWorld>(root);

	if (gw) {
		world = gw;
		if (!_panel_added) {
			add_control_to_bottom_panel(dock_root, "Scene Layer");
			_panel_added = true;
		}
		_refresh_tree();
		call_deferred("_apply_layer_visibility");
	} else {
		world = nullptr;
		if (_panel_added) {
			remove_control_from_bottom_panel(dock_root);
			_panel_added = false;
		}
	}
}

void SceneLayerEditorPlugin::_refresh_tree() {
	call_deferred("_refresh_tree_now");
}

void SceneLayerEditorPlugin::_refresh_tree_now() {
	if (!tree) return;
	tree->clear();
	if (!world) return;
	TreeItem *root = tree->create_item();
	Dictionary data = world->get_layer_data();
	Array layers = data.has("layers") ? (Array)data["layers"] : Array();
	_add_layer_items(root, layers);
}

void SceneLayerEditorPlugin::_add_layer_items(TreeItem *p_parent_item, const Array &p_layers) {
	for (int i = 0; i < p_layers.size(); i++) {
		Dictionary d = p_layers[i];
		TreeItem *item = tree->create_item(p_parent_item);
		int id = d.has("id") ? (int)d["id"] : -1;
		String name = d.has("name") ? (String)d["name"] : String("Layer");
		bool vis = d.has("visible") ? (bool)d["visible"] : true;

		// Column 0: visibility checkbox only.
		item->set_cell_mode(0, TreeItem::CELL_MODE_CHECK);
		item->set_editable(0, true);
		item->set_checked(0, vis);
		item->set_metadata(0, id);

		// Column 1: name (double-click to rename) + action buttons. Kept off
		// column 0 so clicking to select/rename a row doesn't also toggle it.
		item->set_editable(1, true);
		item->set_text(1, name);
		item->set_metadata(1, id);
		item->add_button(1, tree->get_icon("Add", "EditorIcons"), BTN_ASSIGN, false, "Assign selected nodes to this layer");
		item->add_button(1, tree->get_icon("Remove", "EditorIcons"), BTN_UNASSIGN, false, "Unassign selected nodes from this layer");
		item->add_button(1, tree->get_icon("Close", "EditorIcons"), BTN_DELETE, false, "Delete this layer");

		if (id == _pending_select_id) {
			item->select(1);
			_pending_select_id = -1;
		}

		Array children = d.has("children") ? (Array)d["children"] : Array();
		if (children.size() > 0) _add_layer_items(item, children);
	}
}

void SceneLayerEditorPlugin::_commit_layer_data(const String &p_action_name, const Dictionary &p_new_data) {
	if (!world) return;
	Dictionary old_data = world->get_layer_data();
	UndoRedo *ur = &get_undo_redo();
	ur->create_action(p_action_name);
	ur->add_do_method(world, "set_layer_data", p_new_data);
	ur->add_do_method(this, "_refresh_tree");
	ur->add_do_method(this, "_apply_layer_visibility");
	ur->add_undo_method(world, "set_layer_data", old_data);
	ur->add_undo_method(this, "_refresh_tree");
	ur->add_undo_method(this, "_apply_layer_visibility");
	ur->commit_action();
}

void SceneLayerEditorPlugin::_on_new_layer_pressed() {
	if (!world) return;
	Dictionary new_data = world->get_layer_data().duplicate(true);
	int next_id = new_data.has("next_id") ? (int)new_data["next_id"] : 0;

	Dictionary new_layer;
	new_layer["id"] = next_id;
	new_layer["name"] = "New Layer";
	new_layer["visible"] = true;
	new_layer["children"] = Array();
	new_data["next_id"] = next_id + 1;

	Array layers = new_data.has("layers") ? (Array)new_data["layers"] : Array();
	layers.append(new_layer);
	new_data["layers"] = layers;
	_pending_select_id = next_id;
	_commit_layer_data("New Scene Layer", new_data);
}

void SceneLayerEditorPlugin::_on_new_sublayer_pressed() {
	if (!world || !tree) return;
	TreeItem *sel = tree->get_selected();
	if (!sel) return;
	int parent_id = sel->get_metadata(0);

	Dictionary new_data = world->get_layer_data().duplicate(true);
	int next_id = new_data.has("next_id") ? (int)new_data["next_id"] : 0;

	Dictionary new_layer;
	new_layer["id"] = next_id;
	new_layer["name"] = "New Sub-Layer";
	new_layer["visible"] = true;
	new_layer["children"] = Array();
	new_data["next_id"] = next_id + 1;

	Array layers = new_data.has("layers") ? (Array)new_data["layers"] : Array();
	_find_and_add_child(layers, parent_id, new_layer);
	new_data["layers"] = layers;
	_pending_select_id = next_id;
	_commit_layer_data("New Scene Sub-Layer", new_data);
}

void SceneLayerEditorPlugin::_on_item_edited() {
	if (!world || !tree) return;
	TreeItem *item = tree->get_edited();
	if (!item) return;
	int id = item->get_metadata(0);
	int col = tree->get_edited_column();

	Dictionary new_data = world->get_layer_data().duplicate(true);
	Array layers = new_data.has("layers") ? (Array)new_data["layers"] : Array();
	if (col == 0) {
		_find_and_set_field(layers, id, "visible", item->is_checked(0));
	} else {
		_find_and_set_field(layers, id, "name", item->get_text(1));
	}
	new_data["layers"] = layers;
	_commit_layer_data("Edit Scene Layer", new_data);
}

void SceneLayerEditorPlugin::_on_button_pressed(Object *p_item, int p_column, int p_id) {
	if (!world) return;
	TreeItem *item = Object::cast_to<TreeItem>(p_item);
	if (!item) return;
	int layer_id = item->get_metadata(0);

	if (p_id == BTN_DELETE) {
		Dictionary new_data = world->get_layer_data().duplicate(true);
		Array layers = new_data.has("layers") ? (Array)new_data["layers"] : Array();
		_find_and_delete(layers, layer_id);
		new_data["layers"] = layers;
		_commit_layer_data("Delete Scene Layer", new_data);
		return;
	}

	// Assign / unassign the current Scene-tree selection.
	List<Node *> sel = get_editor_interface()->get_selection()->get_full_selected_node_list();
	if (sel.empty()) return;

	UndoRedo *ur = &get_undo_redo();
	ur->create_action(p_id == BTN_ASSIGN ? "Assign To Scene Layer" : "Unassign From Scene Layer");
	bool any = false;
	for (List<Node *>::Element *E = sel.front(); E; E = E->next()) {
		Node *n = E->get();
		Array ids = n->has_meta("_scene_layer_ids") ? (Array)n->get_meta("_scene_layer_ids") : Array();
		Array new_ids = ids.duplicate(false);
		bool has_id = new_ids.has(layer_id);
		if (p_id == BTN_ASSIGN && !has_id) {
			new_ids.append(layer_id);
		} else if (p_id == BTN_UNASSIGN && has_id) {
			new_ids.erase(layer_id);
		} else {
			continue;
		}
		ur->add_do_method(n, "set_meta", "_scene_layer_ids", new_ids);
		ur->add_undo_method(n, "set_meta", "_scene_layer_ids", ids);
		any = true;
	}
	if (any) {
		ur->add_do_method(this, "_apply_layer_visibility");
		ur->add_undo_method(this, "_apply_layer_visibility");
		ur->commit_action();
	} else {
		ur->commit_action(); // empty action, effectively a no-op
	}
}

void SceneLayerEditorPlugin::_apply_layer_visibility() {
	if (!world) return;
	Dictionary data = world->get_layer_data();
	Array layers = data.has("layers") ? (Array)data["layers"] : Array();
	Map<int, bool> effective;
	_collect_effective_visibility(layers, true, effective);

	Node *root = get_editor_interface()->get_edited_scene_root();
	if (root) _walk_and_apply(root, effective);
}

void SceneLayerEditorPlugin::_walk_and_apply(Node *p_node, const Map<int, bool> &p_effective) {
	if (p_node->has_meta("_scene_layer_ids")) {
		Spatial *sp = Object::cast_to<Spatial>(p_node);
		if (sp) {
			if (!_original_visibility.has(p_node)) _original_visibility[p_node] = sp->is_visible();
			Array ids = p_node->get_meta("_scene_layer_ids");
			bool vis = true;
			for (int i = 0; i < ids.size(); i++) {
				int id = ids[i];
				const Map<int, bool>::Element *E = p_effective.find(id);
				if (E && !E->value()) { vis = false; break; }
			}
			sp->set_visible(vis);
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_walk_and_apply(p_node->get_child(i), p_effective);
	}
}

void SceneLayerEditorPlugin::apply_changes() {
	// Safety net: whatever a layer toggle currently has hidden gets restored
	// to its true pre-layer-toggle visibility right before the scene is
	// written to disk, so a hidden layer can never leak into the saved file.
	// Re-applied immediately after (deferred) so the viewport doesn't flash.
	for (Map<Node *, bool>::Element *E = _original_visibility.front(); E; E = E->next()) {
		Spatial *sp = Object::cast_to<Spatial>(E->key());
		if (sp) sp->set_visible(E->value());
	}
	call_deferred("_apply_layer_visibility");
}

void SceneLayerEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_refresh_tree"), &SceneLayerEditorPlugin::_refresh_tree);
	ClassDB::bind_method(D_METHOD("_refresh_tree_now"), &SceneLayerEditorPlugin::_refresh_tree_now);
	ClassDB::bind_method(D_METHOD("_apply_layer_visibility"), &SceneLayerEditorPlugin::_apply_layer_visibility);
	ClassDB::bind_method(D_METHOD("_on_new_layer_pressed"), &SceneLayerEditorPlugin::_on_new_layer_pressed);
	ClassDB::bind_method(D_METHOD("_on_new_sublayer_pressed"), &SceneLayerEditorPlugin::_on_new_sublayer_pressed);
	ClassDB::bind_method(D_METHOD("_on_item_edited"), &SceneLayerEditorPlugin::_on_item_edited);
	ClassDB::bind_method(D_METHOD("_on_button_pressed", "item", "column", "id"), &SceneLayerEditorPlugin::_on_button_pressed);
}

#endif
