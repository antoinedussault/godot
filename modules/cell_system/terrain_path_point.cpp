#include "terrain_path_point.h"

#include "terrain_path.h"

void TerrainPathPoint::_notify_parent() {
	TerrainPath *parent = Object::cast_to<TerrainPath>(get_parent());
	if (parent) parent->notify_point_changed();
}

void TerrainPathPoint::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
			// Opt in to NOTIFICATION_TRANSFORM_CHANGED, which fires continuously
			// while the editor's Move gizmo is live-dragging this node (and again
			// on undo/redo of the resulting Translate action), so TerrainPath
			// stays in sync with zero custom drag code of its own.
			set_notify_transform(true);
			_notify_parent();
			break;
		case NOTIFICATION_TRANSFORM_CHANGED:
			_notify_parent();
			break;
	}
}
