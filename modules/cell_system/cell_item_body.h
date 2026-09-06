#ifndef CELL_ITEM_BODY_H
#define CELL_ITEM_BODY_H

#include "scene/3d/physics_body.h"

// A RigidBody that represents a single dropped ItemResource in the world.
//
// Point `item` at an ItemResource (.tres) authored in the "Items & Recipes"
// editor tab. On entering the tree the body instances the item's `scene_path`
// for visuals, auto-generates a box CollisionShape from that scene's combined
// visual bounds (unless `auto_build` is off, in which case you author the
// CollisionShape children yourself) and takes its mass from the item's `weight`.
//
// It plugs straight into the cell_system streaming: CellWorld already routes any
// RigidBody into its DynamicObjects container. A CellPlayerBody walking into the
// body shoves it (see `_direct_state_changed`); `push()` does the same from code,
// and `pickup()` removes it and emits `picked_up`.
class CellItemBody : public RigidBody {
	GDCLASS(CellItemBody, RigidBody);

	Ref<Resource> item;
	float push_strength = 1.0f;
	bool auto_build = true;

	bool _built = false;
	Node *_visual = nullptr;
	Node *_shape = nullptr;

	void _clear_build();
	void _rebuild();
	void _apply_item_props();

protected:
	static void _bind_methods();
	void _notification(int p_what);
	void _direct_state_changed(Object *p_state);

public:
	void set_item(const Ref<Resource> &p_item);
	Ref<Resource> get_item() const;

	void set_push_strength(float p_strength);
	float get_push_strength() const;

	void set_auto_build(bool p_enabled);
	bool get_auto_build() const;

	// Item id from the referenced resource, or "" — used by the category catalog.
	String get_item_id() const;

	void push(const Vector3 &p_impulse);
	void pickup();

	CellItemBody();
};

#endif // CELL_ITEM_BODY_H
