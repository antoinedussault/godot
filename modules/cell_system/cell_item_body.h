#ifndef CELL_ITEM_BODY_H
#define CELL_ITEM_BODY_H

#include "scene/3d/physics_body.h"

// A RigidBody that represents a single dropped ItemResource in the world.
//
// Point `item` at an ItemResource (.tres) authored in the "Items & Recipes"
// editor tab. On entering the tree the body instances the item's `scene_path`
// for visuals, gives itself collision, and takes its mass from the item's
// `weight`.
//
// Collision comes from the first of these that applies:
//   1. CollisionShapes authored inside the item's own scene. Godot only honours
//      a CollisionShape that is a DIRECT child of the body, so any found nested
//      in the instanced scene are mirrored up onto the body. Add a shape to an
//      item scene and it wins - no code change needed.
//   2. `collision_mode`: a box sized to the visual bounds (the default), or a
//      convex hull fitted to the visual mesh, or nothing.
// Setting `auto_build` off skips all of it and leaves you to author the
// CollisionShape children on this node yourself.
//
// It plugs straight into the cell_system streaming: CellWorld already routes any
// RigidBody into its DynamicObjects container. A CellPlayerBody walking into the
// body shoves it (see `_direct_state_changed`); `push()` does the same from code,
// and `pickup()` removes it and emits `picked_up`.
class CellItemBody : public RigidBody {
	GDCLASS(CellItemBody, RigidBody);

public:
	enum CollisionMode {
		COLLISION_BOX,    // one box sized to the visual bounds
		COLLISION_CONVEX, // convex hull fitted to the visual mesh
		COLLISION_NONE,   // no generated shape
	};

private:
	Ref<Resource> item;
	float push_strength = 1.0f;
	bool auto_build = true;
	CollisionMode collision_mode = COLLISION_BOX;

	bool _built = false;
	Node *_visual = nullptr;
	Vector<Node *> _shapes;

	void _clear_build();
	void _rebuild();
	void _apply_item_props();

	// Mirrors CollisionShapes authored inside the instanced scene onto this body.
	// Returns how many it found.
	int _adopt_authored_shapes();
	void _build_generated_shape();

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

	void set_collision_mode(CollisionMode p_mode);
	CollisionMode get_collision_mode() const;

	// Item id from the referenced resource, or "" — used by the category catalog.
	String get_item_id() const;

	void push(const Vector3 &p_impulse);
	void pickup();

	CellItemBody();
};

VARIANT_ENUM_CAST(CellItemBody::CollisionMode);

#endif // CELL_ITEM_BODY_H
