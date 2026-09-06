#ifndef SCENE_SPAWN_POINT_H
#define SCENE_SPAWN_POINT_H

#include "scene/3d/spatial.h"

// A named arrival marker inside a scene. When SceneManager finishes loading a
// scene it looks for the SceneSpawnPoint whose spawn_id matches the door's
// target_spawn (searching the "scene_spawn_points" group) and reports that
// node's global transform so the master node can place the player there.
//
// Drop one wherever the player should appear — usually just inside each door.
class SceneSpawnPoint : public Spatial {
	GDCLASS(SceneSpawnPoint, Spatial);

	String spawn_id;
	bool use_flat_orientation = true; // keep yaw, drop pitch/roll for the placed body

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_spawn_id(const String &p_id) { spawn_id = p_id; }
	String get_spawn_id() const { return spawn_id; }

	void set_use_flat_orientation(bool p_flat) { use_flat_orientation = p_flat; }
	bool get_use_flat_orientation() const { return use_flat_orientation; }

	// Global transform the player should be moved to. With use_flat_orientation
	// the basis is a pure yaw rotation (upright), otherwise the marker's full
	// global basis is used.
	Transform get_spawn_transform() const;

	SceneSpawnPoint() {}
};

#endif // SCENE_SPAWN_POINT_H
