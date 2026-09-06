#ifndef GAME_WORLD_H
#define GAME_WORLD_H

#include "scene/3d/spatial.h"

// Marker root class for a scene that gets the "Scene Layer" editor tool
// (see SceneLayerEditorPlugin). Holds only the layer *definitions*
// (hierarchy, names, per-layer visibility) as a plain serializable
// Dictionary — actual node membership lives as metadata on each member node
// ("_scene_layer_ids"), not here. This class has no runtime behavior of its
// own; it's purely a marker + data bucket the editor tool reads and writes.
//
// layer_data shape: { "next_id": int, "layers": Array }
// each entry in "layers" (and recursively each entry's "children"):
//   { "id": int, "name": String, "visible": bool, "children": Array }
class GameWorld : public Spatial {
	GDCLASS(GameWorld, Spatial);

	Dictionary layer_data;

protected:
	static void _bind_methods();

public:
	void set_layer_data(const Dictionary &p_data) { layer_data = p_data; }
	Dictionary get_layer_data() const { return layer_data; }

	GameWorld() {}
};

#endif // GAME_WORLD_H
