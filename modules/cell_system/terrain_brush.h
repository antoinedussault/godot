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
	enum Mode { MODE_RAISE, MODE_LOWER, MODE_FLATTEN, MODE_PAINT, MODE_SMOOTH, MODE_NOISE, MODE_ERODE, MODE_NAV_MASK };

private:
	CellTerrainManager *manager = nullptr;
	int paint_layer = 0;
	bool force_flatten_height = false;
	float flatten_height = 0.0f;
	Ref<Image> brush_image; // grayscale brush shape; null = default smoothstep circle // which texture layer to paint (0-15)
	int falloff_type = FALLOFF_SMOOTH;
	float noise_scale = 8.0f;
	int noise_seed = 0;
	float erosion_talus = 0.5f;
	bool nav_mask_erase = false; // MODE_NAV_MASK: false = paint "excluded", true = restore "navigable"

	Map<int64_t, PoolRealArray> _stroke_before;

	static float _hash_noise(float x, float z, int seed);

protected:
	static void _bind_methods();

public:
	void set_manager(CellTerrainManager *m) { manager = m; }
	void set_paint_layer(int l) { paint_layer = CLAMP(l, 0, 15); }
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

	void begin_stroke();
	void apply_brush(const Vector3 &world_pos, float radius, float strength, int mode);
	void end_stroke(UndoRedo *undo_redo);

	TerrainBrush() {}
};

VARIANT_ENUM_CAST(TerrainBrush::Mode);
VARIANT_ENUM_CAST(TerrainFalloffType);

#endif
