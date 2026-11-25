#ifndef CELL_PLAYER_BODY_H
#define CELL_PLAYER_BODY_H

#include "scene/3d/physics_body.h"

class CellTerrainManager;
class GameWorld;

// A KinematicBody that knows about the world it is standing in.
//
// It no longer DRIVES streaming - GameWorld does that, from the camera, for
// terrain, foliage and objects alike. That used to be split between this node
// and each manager's own camera polling, on grids with different cell sizes,
// which is where the frozen player and the objects flickering on every cell
// boundary came from. What is left here is what genuinely belongs to a body:
//
//   - On spawn, make sure the ground under it exists before the first physics
//     step, so it does not fall through an unloaded cell.
//   - If it ever gets ahead of the streamer, hold still until the ground under
//     it has arrived instead of falling.
//   - Report which cell it is in (cell_changed) and how fast it is moving
//     (get_linear_velocity, which CellItemBody reads to be pushed).
//
// It needs no setup: it finds the GameWorld of the current scene by itself, and
// finds it again after a scene change. It is never streamed out.
class CellPlayerBody : public KinematicBody {
	GDCLASS(CellPlayerBody, KinematicBody);

private:
	// Only used when there is NO GameWorld in the scene (a bare terrain test
	// scene). Under a GameWorld it is ignored.
	NodePath terrain_manager_path;

	ObjectID _world_id = 0;

	bool _has_cell = false;
	int _cell_x = 0;
	int _cell_z = 0;
	bool _waiting_for_ground = false;

	// Approximate world-space velocity, derived from the transform delta each
	// physics frame. A KinematicBody driven by game code has no velocity of its
	// own; CellItemBody reads this to know how hard it is being shoved.
	Vector3 _last_pos;
	Vector3 _linear_velocity;
	bool _has_last_pos = false;

	GameWorld *_get_world();
	CellTerrainManager *_get_fallback_terrain() const;
	bool _ground_ready(const Vector3 &p_pos);
	void _ensure_ground(const Vector3 &p_pos);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_terrain_manager_path(const NodePath &p);
	NodePath get_terrain_manager_path() const;

	// The cell this body is in, on the GameWorld's base grid.
	int get_cell_x() const { return _cell_x; }
	int get_cell_z() const { return _cell_z; }
	bool is_terrain_loaded();
	// The GameWorld this body is currently in (null if the scene has none).
	Node *get_game_world();

	Vector3 get_linear_velocity() const { return _linear_velocity; }

	CellPlayerBody();
};

#endif // CELL_PLAYER_BODY_H
