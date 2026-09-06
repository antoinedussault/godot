#include "cell_terrain_layer.h"

void CellTerrainLayer::set_layer_name(const String &p_name) { layer_name = p_name; emit_changed(); }
String CellTerrainLayer::get_layer_name() const { return layer_name; }

void CellTerrainLayer::set_layer_id(int p_id) { layer_id = p_id; }
int CellTerrainLayer::get_layer_id() const { return layer_id; }

void CellTerrainLayer::set_albedo_texture(const Ref<Texture> &p_tex) { albedo_texture = p_tex; emit_changed(); }
Ref<Texture> CellTerrainLayer::get_albedo_texture() const { return albedo_texture; }

void CellTerrainLayer::set_normal_texture(const Ref<Texture> &p_tex) { normal_texture = p_tex; emit_changed(); }
Ref<Texture> CellTerrainLayer::get_normal_texture() const { return normal_texture; }

void CellTerrainLayer::set_normal_strength(float p_s) { normal_strength = p_s; emit_changed(); }
float CellTerrainLayer::get_normal_strength() const { return normal_strength; }

void CellTerrainLayer::set_parallax_scale(float p_s) { parallax_scale = MAX(0.0f, p_s); emit_changed(); }
float CellTerrainLayer::get_parallax_scale() const { return parallax_scale; }

void CellTerrainLayer::set_roughness_scale(float p_s) { roughness_scale = MAX(0.0f, p_s); emit_changed(); }
float CellTerrainLayer::get_roughness_scale() const { return roughness_scale; }

void CellTerrainLayer::set_uv_scale(float p_s) { uv_scale = MAX(0.001f, p_s); emit_changed(); }
float CellTerrainLayer::get_uv_scale() const { return uv_scale; }

void CellTerrainLayer::set_color_mod(const Color &p_c) { color_mod = p_c; emit_changed(); }
Color CellTerrainLayer::get_color_mod() const { return color_mod; }

void CellTerrainLayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_layer_name", "name"), &CellTerrainLayer::set_layer_name);
	ClassDB::bind_method(D_METHOD("get_layer_name"), &CellTerrainLayer::get_layer_name);
	ClassDB::bind_method(D_METHOD("set_layer_id", "id"), &CellTerrainLayer::set_layer_id);
	ClassDB::bind_method(D_METHOD("get_layer_id"), &CellTerrainLayer::get_layer_id);
	ClassDB::bind_method(D_METHOD("set_albedo_texture", "texture"), &CellTerrainLayer::set_albedo_texture);
	ClassDB::bind_method(D_METHOD("get_albedo_texture"), &CellTerrainLayer::get_albedo_texture);
	ClassDB::bind_method(D_METHOD("set_normal_texture", "texture"), &CellTerrainLayer::set_normal_texture);
	ClassDB::bind_method(D_METHOD("get_normal_texture"), &CellTerrainLayer::get_normal_texture);
	ClassDB::bind_method(D_METHOD("set_normal_strength", "strength"), &CellTerrainLayer::set_normal_strength);
	ClassDB::bind_method(D_METHOD("get_normal_strength"), &CellTerrainLayer::get_normal_strength);
	ClassDB::bind_method(D_METHOD("set_parallax_scale", "scale"), &CellTerrainLayer::set_parallax_scale);
	ClassDB::bind_method(D_METHOD("get_parallax_scale"), &CellTerrainLayer::get_parallax_scale);
	ClassDB::bind_method(D_METHOD("set_roughness_scale", "scale"), &CellTerrainLayer::set_roughness_scale);
	ClassDB::bind_method(D_METHOD("get_roughness_scale"), &CellTerrainLayer::get_roughness_scale);
	ClassDB::bind_method(D_METHOD("set_uv_scale", "scale"), &CellTerrainLayer::set_uv_scale);
	ClassDB::bind_method(D_METHOD("get_uv_scale"), &CellTerrainLayer::get_uv_scale);
	ClassDB::bind_method(D_METHOD("set_color_mod", "color"), &CellTerrainLayer::set_color_mod);
	ClassDB::bind_method(D_METHOD("get_color_mod"), &CellTerrainLayer::get_color_mod);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "layer_name"), "set_layer_name", "get_layer_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "layer_id"), "set_layer_id", "get_layer_id");
	ADD_GROUP("Textures", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "albedo_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture"), "set_albedo_texture", "get_albedo_texture");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "normal_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture"), "set_normal_texture", "get_normal_texture");
	ADD_GROUP("Shading", "");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "normal_strength", PROPERTY_HINT_RANGE, "0.0,4.0,0.05"), "set_normal_strength", "get_normal_strength");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "parallax_scale", PROPERTY_HINT_RANGE, "0.0,0.3,0.005"), "set_parallax_scale", "get_parallax_scale");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "roughness_scale", PROPERTY_HINT_RANGE, "0.0,2.0,0.01"), "set_roughness_scale", "get_roughness_scale");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "uv_scale", PROPERTY_HINT_RANGE, "0.05,8.0,0.05"), "set_uv_scale", "get_uv_scale");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color_mod"), "set_color_mod", "get_color_mod");
}
