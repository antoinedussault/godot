#ifndef CELL_TERRAIN_LAYER_H
#define CELL_TERRAIN_LAYER_H

#include "core/color.h"
#include "core/resource.h"
#include "scene/resources/texture.h"

// One paintable terrain texture layer, Terrain3D-style: a self-contained
// resource instead of a pair of numbered CellTerrainManager texture slots,
// so a layer's full look (textures + how the shader treats them) travels
// and is reusable as one .tres. Assigned into CellTerrainManager's `layers`
// array; a layer's position in that array is still what splatmap painting
// targets (see TerrainBrush::paint_layer) — `layer_id` is a separate, freely
// assignable identifier for gameplay code to check against (e.g. "am I
// standing on gravel"), independent of array order — see
// CellTerrainManager::get_layer_id_at_world().
class CellTerrainLayer : public Resource {
	GDCLASS(CellTerrainLayer, Resource);

	String layer_name = "Layer";
	int layer_id = 0;
	Ref<Texture> albedo_texture; // RGB = albedo, A = height
	Ref<Texture> normal_texture; // RGB = tangent-space normal, A = roughness
	float normal_strength = 1.0f;
	float parallax_scale = 0.0f;
	float roughness_scale = 1.0f;
	float uv_scale = 1.0f; // multiplier on top of CellTerrainManager's global texture_scale
	Color color_mod = Color(1, 1, 1, 1);

protected:
	static void _bind_methods();

public:
	void set_layer_name(const String &p_name); String get_layer_name() const;
	void set_layer_id(int p_id);               int get_layer_id() const;
	void set_albedo_texture(const Ref<Texture> &p_tex); Ref<Texture> get_albedo_texture() const;
	void set_normal_texture(const Ref<Texture> &p_tex); Ref<Texture> get_normal_texture() const;
	void set_normal_strength(float p_s);  float get_normal_strength() const;
	void set_parallax_scale(float p_s);   float get_parallax_scale() const;
	void set_roughness_scale(float p_s);  float get_roughness_scale() const;
	void set_uv_scale(float p_s);         float get_uv_scale() const;
	void set_color_mod(const Color &p_c); Color get_color_mod() const;

	CellTerrainLayer() {}
};

#endif
