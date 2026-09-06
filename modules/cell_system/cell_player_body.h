#ifndef CELL_PLAYER_BODY_H
#define CELL_PLAYER_BODY_H

#include "scene/3d/physics_body.h"

class CellTerrainManager;
class CellWorld;
class Camera;

// A KinematicBody that drives cell streaming. Point terrain_manager_path and
// cell_world_path at the corresponding nodes in the Inspector.
class CellPlayerBody : public KinematicBody {
	GDCLASS(CellPlayerBody, KinematicBody);

private:
	float cell_size = 64.0f;
	NodePath terrain_manager_path;
	NodePath cell_world_path;

	bool _has_cell = false;
	int _cell_x = 0;
	int _cell_z = 0;
	bool _waiting_for_cell = false;

	// Approximate world-space velocity, derived from the transform delta each
	// physics frame. CellPlayerBody is a KinematicBody driven by external game
	// code, so it has no velocity of its own; CellItemBody reads this to know
	// how hard the player is shoving it.
	Vector3 _last_pos;
	Vector3 _linear_velocity;
	bool _has_last_pos = false;

	CellTerrainManager *_get_terrain_manager() const;
	CellWorld *_get_cell_world() const;
	// The active viewport camera, if any — streaming follows this rather than
	// the body's own transform, so it tracks what's actually on screen (matters
	// for third-person or detached-camera setups). Falls back to this node's
	// own position when no camera is current.
	Camera *_get_active_camera() const;
	Vector3 _get_stream_pos() const;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_cell_size(float s);
	float get_cell_size() const;

	void set_terrain_manager_path(const NodePath &p);
	NodePath get_terrain_manager_path() const;

	void set_cell_world_path(const NodePath &p);
	NodePath get_cell_world_path() const;

	int get_cell_x() const { return _cell_x; }
	int get_cell_z() const { return _cell_z; }
	bool is_terrain_loaded() const;

	Vector3 get_linear_velocity() const { return _linear_velocity; }

	CellPlayerBody();
};

#endif // CELL_PLAYER_BODY_H
