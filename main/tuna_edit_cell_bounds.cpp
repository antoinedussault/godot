/**************************************************************************/
/*  tuna_edit_cell_bounds.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             TUNA ENGINE                                */
/**************************************************************************/

#include "tuna_edit_cell_bounds.h"

#include "core/math/math_funcs.h"
#include "core/print_string.h"
#include "scene/3d/camera.h"
#include "scene/main/viewport.h"
#include "scene/resources/texture.h"
#include "servers/visual_server.h"

void TunaEditCellBounds::_bind_methods() {
}

void TunaEditCellBounds::set_enabled(bool p_enabled) {
	if (enabled == p_enabled) {
		return;
	}
	enabled = p_enabled;
	set_process(enabled);

	if (!enabled) {
		_hide();
	} else {
		reported = false;
		// Force the next _process to emit rather than deciding nothing changed.
		drawn_valid = false;
	}
}

void TunaEditCellBounds::set_radius(int p_radius) {
	radius = CLAMP(p_radius, 1, 32);
	drawn_valid = false;
}

Spatial *TunaEditCellBounds::_find_by_class(Node *p_node, const String &p_class) {
	if (p_node->is_class(p_class)) {
		return Object::cast_to<Spatial>(p_node);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Spatial *found = _find_by_class(p_node->get_child(i), p_class);
		if (found) {
			return found;
		}
	}
	return nullptr;
}

Spatial *TunaEditCellBounds::_find_cell_world() const {
	SceneTree *tree = get_tree();
	if (!tree || !tree->get_root()) {
		return nullptr;
	}
	return _find_by_class(tree->get_root(), "CellWorld");
}

// Hazard stripes, generated rather than shipped as an asset: diagonal yellow and black, the
// banding the Creation Kit uses for cell borders.
static Ref<ImageTexture> _make_stripe_texture() {
	const int size = 64;
	const int band = 10; // diagonal band width in pixels

	PoolVector<uint8_t> data;
	data.resize(size * size * 4);
	{
		PoolVector<uint8_t>::Write w = data.write();
		for (int y = 0; y < size; y++) {
			for (int x = 0; x < size; x++) {
				// (x + y) makes the bands run diagonally; wrapping on `size` keeps them
				// continuous across tiles so the repeat seam does not show.
				const bool yellow = (((x + y) % size) / band) % 2 == 0;
				const int i = (y * size + x) * 4;
				w[i + 0] = yellow ? 255 : 20;
				w[i + 1] = yellow ? 202 : 20;
				w[i + 2] = yellow ? 20 : 20;
				w[i + 3] = 255;
			}
		}
	}

	Ref<Image> image;
	image.instance();
	image->create(size, size, false, Image::FORMAT_RGBA8, data);

	Ref<ImageTexture> texture;
	texture.instance();
	texture->create_from_image(image, Texture::FLAG_REPEAT | Texture::FLAG_FILTER);
	return texture;
}

void TunaEditCellBounds::_ensure_overlay() {
	if (overlay_immediate.is_valid()) {
		return;
	}

	Ref<World> world = get_world();
	if (world.is_null()) {
		return;
	}

	VisualServer *vs = VisualServer::get_singleton();

	stripes = _make_stripe_texture();

	overlay_material.instance();
	overlay_material->set_flag(SpatialMaterial::FLAG_UNSHADED, true);
	// Walls are seen from both sides as you fly through the grid.
	overlay_material->set_cull_mode(SpatialMaterial::CULL_DISABLED);
	// Cells sit at y = 0 while terrain is above it, so depth-tested borders are simply buried.
	overlay_material->set_flag(SpatialMaterial::FLAG_DISABLE_DEPTH_TEST, true);
	overlay_material->set_feature(SpatialMaterial::FEATURE_TRANSPARENT, true);
	overlay_material->set_texture(SpatialMaterial::TEXTURE_ALBEDO, stripes);
	overlay_material->set_albedo(Color(1, 1, 1, 0.9f));
	overlay_material->set_render_priority(1);

	overlay_immediate = vs->immediate_create();
	vs->immediate_set_material(overlay_immediate, overlay_material->get_rid());
	overlay_instance = vs->instance_create2(overlay_immediate, world->get_scenario());
	vs->instance_geometry_set_cast_shadows_setting(overlay_instance, VisualServer::SHADOW_CASTING_SETTING_OFF);
}

void TunaEditCellBounds::_hide() {
	if (overlay_instance.is_valid()) {
		VisualServer::get_singleton()->instance_set_visible(overlay_instance, false);
	}
	drawn_valid = false;
}

// One striped band lying flat on the cell floor. Flat, not a wall: the Creation Kit marks
// cells as squares outlined on the ground, and a wall would hide the very geometry you are
// placing inside the cell.
void TunaEditCellBounds::_emit_band(const Vector3 &p_from, const Vector3 &p_to, float p_width) {
	VisualServer *vs = VisualServer::get_singleton();

	const Vector3 along = (p_to - p_from);
	const float length = along.length();
	if (length < CMP_EPSILON) {
		return;
	}

	// Across the run, in the ground plane, so the band straddles the boundary line.
	const Vector3 across = Vector3(0, 1, 0).cross(along.normalized()) * (p_width * 0.5f);

	const Vector3 a = p_from - across;
	const Vector3 b = p_to - across;
	const Vector3 c = p_to + across;
	const Vector3 d = p_from + across;

	// World-scale UVs: stripes keep one size however long the run is, instead of stretching.
	const float u = length / p_width;

	vs->immediate_uv(overlay_immediate, Vector2(0, 0));
	vs->immediate_vertex(overlay_immediate, a);
	vs->immediate_uv(overlay_immediate, Vector2(u, 0));
	vs->immediate_vertex(overlay_immediate, b);
	vs->immediate_uv(overlay_immediate, Vector2(u, 1));
	vs->immediate_vertex(overlay_immediate, c);

	vs->immediate_uv(overlay_immediate, Vector2(0, 0));
	vs->immediate_vertex(overlay_immediate, a);
	vs->immediate_uv(overlay_immediate, Vector2(u, 1));
	vs->immediate_vertex(overlay_immediate, c);
	vs->immediate_uv(overlay_immediate, Vector2(0, 1));
	vs->immediate_vertex(overlay_immediate, d);
}

void TunaEditCellBounds::_rebuild(Spatial *p_world, int p_cx, int p_cz) {
	_ensure_overlay();
	if (!overlay_immediate.is_valid()) {
		return;
	}

	const float cell_size = p_world->get("cell_size");
	if (cell_size <= 0.0f) {
		return;
	}

	Set<int64_t> existing;
	for (int i = 0; i < p_world->get_child_count(); i++) {
		Node *child = p_world->get_child(i);
		if (!child->is_class("Cell")) {
			continue;
		}
		const int cx = child->get("cell_x");
		const int cz = child->get("cell_z");
		existing.insert((int64_t(cx) << 32) ^ uint32_t(cz));
	}

	VisualServer *vs = VisualServer::get_singleton();
	vs->immediate_clear(overlay_immediate);
	// Emitted in CellWorld space, so moving the world moves the grid with it.
	vs->instance_set_transform(overlay_instance, p_world->get_global_transform());
	vs->instance_set_visible(overlay_instance, true);

	const float width = MAX(cell_size * 0.04f, 0.5f);
	const float lo_x = (p_cx - radius) * cell_size;
	const float hi_x = (p_cx + radius + 1) * cell_size;
	const float lo_z = (p_cz - radius) * cell_size;
	const float hi_z = (p_cz + radius + 1) * cell_size;

	vs->immediate_begin(overlay_immediate, VisualServer::PRIMITIVE_TRIANGLES, RID());

	// Bands along the grid lines rather than four per cell: neighbouring cells share an edge,
	// so per-cell borders would draw every interior boundary twice and double the blending.
	for (int cx = p_cx - radius; cx <= p_cx + radius + 1; cx++) {
		const float x = cx * cell_size;
		_emit_band(Vector3(x, 0, lo_z), Vector3(x, 0, hi_z), width);
	}
	for (int cz = p_cz - radius; cz <= p_cz + radius + 1; cz++) {
		const float z = cz * cell_size;
		_emit_band(Vector3(lo_x, 0, z), Vector3(hi_x, 0, z), width);
	}

	vs->immediate_end(overlay_immediate);

	drawn_cx = p_cx;
	drawn_cz = p_cz;
	drawn_valid = true;

	if (!reported) {
		reported = true;
		print_line("TUNA_ACK:cell_bounds drawn cell_size=" + rtos(cell_size)
				+ " authored=" + itos(existing.size())
				+ " at_cell=" + itos(p_cx) + "," + itos(p_cz));
	}
}

void TunaEditCellBounds::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process(enabled);
		} break;

		case NOTIFICATION_EXIT_TREE: {
			_hide();
		} break;

		case NOTIFICATION_PROCESS: {
			if (!enabled) {
				return;
			}

			Spatial *world = _find_cell_world();
			if (!world) {
				if (!reported) {
					reported = true;
					print_line("TUNA_ERR:cell_bounds: no CellWorld node in this scene");
				}
				_hide();
				return;
			}

			Viewport *viewport = get_viewport();
			Camera *camera = viewport ? viewport->get_camera() : nullptr;
			if (!camera) {
				return;
			}

			const float cell_size = world->get("cell_size");
			if (cell_size <= 0.0f) {
				return;
			}

			// Camera position in CellWorld space, so the grid follows the world's own transform
			// rather than assuming it sits at the origin.
			const Vector3 local = world->get_global_transform().affine_inverse().xform(
					camera->get_global_transform().origin);

			const int cx = int(Math::floor(local.x / cell_size));
			const int cz = int(Math::floor(local.z / cell_size));

			// Only re-emit when the grid would actually change.
			if (!drawn_valid || cx != drawn_cx || cz != drawn_cz) {
				_rebuild(world, cx, cz);
			}
		} break;
	}
}

TunaEditCellBounds::TunaEditCellBounds() {
	enabled = false;
	radius = 4;
	drawn_cx = 0;
	drawn_cz = 0;
	drawn_valid = false;
	reported = false;
	set_process(false);
}

TunaEditCellBounds::~TunaEditCellBounds() {
	VisualServer *vs = VisualServer::get_singleton();
	if (!vs) {
		return;
	}
	if (overlay_instance.is_valid()) {
		vs->free(overlay_instance);
		overlay_instance = RID();
	}
	if (overlay_immediate.is_valid()) {
		vs->free(overlay_immediate);
		overlay_immediate = RID();
	}
}
