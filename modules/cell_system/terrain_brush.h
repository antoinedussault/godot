#ifndef TERRAIN_BRUSH_H
#define TERRAIN_BRUSH_H

#include "core/map.h"
#include "core/image.h"
#include "core/reference.h"
#include "terrain_math_utils.h"

class CellTerrainManager;
class UndoRedo;

class TerrainBrush : public Reference {
	GDCLASS(TerrainBrush, Reference);

public:
	enum Mode { MODE_RAISE, MODE_LOWER, MODE_FLATTEN, MODE_PAINT, MODE_SMOOTH, MODE_NOISE, MODE_ERODE, MODE_NAV_MASK, MODE_HOLE, MODE_COLOR };

private:
	CellTerrainManager *manager = nullptr;
	int paint_layer = 0;
	// Slope filter for MODE_PAINT: the layer is only written where the terrain
	// angle falls inside [paint_min_angle, paint_max_angle] degrees (0 = flat,
	// 90 = vertical). Lets you paint rock only on cliffs, grass only on flats,
	// without masking by hand. Full range means no filtering and no cost.
	float paint_min_angle = 0.0f;
	float paint_max_angle = 90.0f;
	bool force_flatten_height = false;
	float flatten_height = 0.0f;
	Ref<Image> brush_image; // grayscale brush shape; null = default smoothstep circle // which texture layer to paint (0-15)
	int falloff_type = FALLOFF_SMOOTH;
	float noise_scale = 8.0f;
	int noise_seed = 0;
	float erosion_talus = 0.5f;
	bool nav_mask_erase = false; // MODE_NAV_MASK: false = paint "excluded", true = restore "navigable"
	// MODE_HOLE: false = cut the terrain away, true = fill it back in.
	// Unlike the nav mask this removes render geometry AND collision, so a
	// hole stroke forces a full rebuild of the cells it touches.
	bool hole_erase = false;
	// MODE_COLOR: the tint painted into each cell's colour map. White is the
	// identity, so painting white is also how you erase a tint.
	Color paint_color = Color(1, 1, 1, 1);

	Map<int64_t, PoolRealArray> _stroke_before;

	static float _hash_noise(float x, float z, int seed);

protected:
	static void _bind_methods();

public:
	void set_manager(CellTerrainManager *m) { manager = m; }
	void set_paint_layer(int l) { paint_layer = CLAMP(l, 0, 15); }
	void set_paint_min_angle(float v) { paint_min_angle = CLAMP(v, 0.0f, 90.0f); }
	float get_paint_min_angle() const { return paint_min_angle; }
	void set_paint_max_angle(float v) { paint_max_angle = CLAMP(v, 0.0f, 90.0f); }
	float get_paint_max_angle() const { return paint_max_angle; }
	int get_paint_layer() const { return paint_layer; }

	void set_force_flatten_height(bool f) { force_flatten_height = f; }
	bool get_force_flatten_height() const { return force_flatten_height; }

	void set_flatten_height(float h) { flatten_height = h; }
	float get_flatten_height() const { return flatten_height; }

	void set_brush_image(const Ref<Image> &img) { brush_image = img; }

	void set_falloff_type(int t) { falloff_type = t; }
	int get_falloff_type() const { return falloff_type; }

	void set_noise_scale(float s) { noise_scale = MAX(0.01f, s); }
	float get_noise_scale() const { return noise_scale; }
	void set_noise_seed(int s) { noise_seed = s; }
	int get_noise_seed() const { return noise_seed; }

	void set_erosion_talus(float t) { erosion_talus = MAX(0.0f, t); }
	float get_erosion_talus() const { return erosion_talus; }

	void set_nav_mask_erase(bool e) { nav_mask_erase = e; }
	bool get_nav_mask_erase() const { return nav_mask_erase; }

	void set_hole_erase(bool e) { hole_erase = e; }
	bool get_hole_erase() const { return hole_erase; }

	void set_paint_color(const Color &c) { paint_color = c; }
	Color get_paint_color() const { return paint_color; }

	void begin_stroke();
	void apply_brush(const Vector3 &world_pos, float radius, float strength, int mode);
	void end_stroke(UndoRedo *undo_redo);

	TerrainBrush() {}
};

VARIANT_ENUM_CAST(TerrainBrush::Mode);
VARIANT_ENUM_CAST(TerrainFalloffType);

#endif
