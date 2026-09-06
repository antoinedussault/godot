#include "foliage_type.h"

FoliageType::FoliageType() {
	for (int i = 0; i < MAX_LODS - 1; i++)
		lod_distances[i] = 0.0f;
}

void   FoliageType::set_type_name(const String &p)  { type_name = p; }
String FoliageType::get_type_name()  const           { return type_name; }

void FoliageType::set_type(int t)  { type = (Type)CLAMP(t, 0, 2); _change_notify(); }
int  FoliageType::get_type() const { return (int)type; }

void  FoliageType::set_max_distance(bool v)     { max_distance = v; }
bool  FoliageType::get_max_distance() const      { return max_distance; }
void  FoliageType::set_max_render_distance(float v) { max_render_distance = MAX(0.0f, v); }
float FoliageType::get_max_render_distance() const  { return max_render_distance; }
void  FoliageType::set_keep_loaded(bool v)       { keep_loaded = v; }
bool  FoliageType::get_keep_loaded() const        { return keep_loaded; }

void  FoliageType::set_lod_count(int n)  { lod_count = CLAMP(n, 1, MAX_LODS); }
int   FoliageType::get_lod_count() const { return lod_count; }

void  FoliageType::set_lod_distance(int idx, float v) {
	if (idx < 0 || idx >= MAX_LODS - 1) return;
	lod_distances[idx] = MAX(0.0f, v);
}
float FoliageType::get_lod_distance(int idx) const {
	if (idx < 0 || idx >= MAX_LODS - 1) return 0.0f;
	return lod_distances[idx];
}

void  FoliageType::set_fade_band(float v) { fade_band = MAX(0.0f, v); }
float FoliageType::get_fade_band() const   { return fade_band; }

// GRASS
void FoliageType::set_grass_mesh(int idx, const Ref<Mesh> &m) {
	if (idx < 0 || idx >= MAX_LODS) return;
	grass_mesh[idx] = m;
}
Ref<Mesh> FoliageType::get_grass_mesh(int idx) const {
	if (idx < 0 || idx >= MAX_LODS) return Ref<Mesh>();
	return grass_mesh[idx];
}
void FoliageType::set_grass_material(int idx, const Ref<Material> &m) {
	if (idx < 0 || idx >= MAX_LODS) return;
	grass_material[idx] = m;
}
Ref<Material> FoliageType::get_grass_material(int idx) const {
	if (idx < 0 || idx >= MAX_LODS) return Ref<Material>();
	return grass_material[idx];
}

// TREE
void             FoliageType::set_tree_scene(const Ref<PackedScene> &s)       { tree_scene = s; }
Ref<PackedScene> FoliageType::get_tree_scene() const                           { return tree_scene; }

// INTERACTOR
void             FoliageType::set_interactor_scene(const Ref<PackedScene> &s) { interactor_scene = s; }
Ref<PackedScene> FoliageType::get_interactor_scene() const                     { return interactor_scene; }

// Scatter
void  FoliageType::set_density(float v)         { density        = MAX(0.0f, v); }
float FoliageType::get_density() const           { return density; }
void  FoliageType::set_min_scale(float v)        { min_scale      = MAX(0.001f, v); }
float FoliageType::get_min_scale() const         { return min_scale; }
void  FoliageType::set_max_scale(float v)        { max_scale      = MAX(0.001f, v); }
float FoliageType::get_max_scale() const         { return max_scale; }
void  FoliageType::set_random_yaw(bool v)        { random_yaw     = v; }
bool  FoliageType::get_random_yaw() const        { return random_yaw; }
void  FoliageType::set_align_to_normal(bool v)   { align_to_normal = v; }
bool  FoliageType::get_align_to_normal() const   { return align_to_normal; }
void  FoliageType::set_align_amount(float v)     { align_amount    = CLAMP(v, 0.0f, 1.0f); }
float FoliageType::get_align_amount() const      { return align_amount; }
void  FoliageType::set_slope_limit_deg(float v)  { slope_limit_deg = CLAMP(v, 0.0f, 90.0f); }
float FoliageType::get_slope_limit_deg() const   { return slope_limit_deg; }
void  FoliageType::set_min_spacing(float v)      { min_spacing     = MAX(0.0f, v); }
float FoliageType::get_min_spacing() const       { return min_spacing; }
void  FoliageType::set_y_offset_min(float v)     { y_offset_min    = v; }
float FoliageType::get_y_offset_min() const      { return y_offset_min; }
void  FoliageType::set_y_offset_max(float v)     { y_offset_max    = v; }
float FoliageType::get_y_offset_max() const      { return y_offset_max; }

// Collision
void  FoliageType::set_collision_enabled(bool v) { collision_enabled = v; }
bool  FoliageType::get_collision_enabled() const  { return collision_enabled; }

// Rendering
void         FoliageType::set_cast_shadow(bool v)               { cast_shadow       = v; }
bool         FoliageType::get_cast_shadow() const                { return cast_shadow; }
void         FoliageType::set_shadow_lod_count(int v)            { shadow_lod_count  = CLAMP(v, 0, MAX_LODS); }
int          FoliageType::get_shadow_lod_count() const           { return shadow_lod_count; }
void         FoliageType::set_material_override(const Ref<Material> &m) { material_override = m; }
Ref<Material> FoliageType::get_material_override() const         { return material_override; }

// ---------------------------------------------------------------------------

int FoliageType::lod_for_distance(float dist) const {
	if (type == TYPE_INTERACTOR || lod_count <= 1) return 0;
	for (int i = 0; i < lod_count - 1; i++) {
		float d = lod_distances[i];
		if (d <= 0.0f) return lod_count - 1; // unset distance — jump to last
		if (dist < d)  return i;
	}
	return lod_count - 1;
}

bool FoliageType::is_culled(float dist) const {
	return max_distance && dist > max_render_distance;
}

bool FoliageType::is_persistent() const {
	return type == TYPE_TREE || keep_loaded;
}

// ---------------------------------------------------------------------------

void FoliageType::_validate_property(PropertyInfo &p_property) const {
	const String &name = p_property.name;

	bool has_lod = (type != TYPE_INTERACTOR);

	if (name == "lod_count" || name == "lod_fade_band" || name.begins_with("lod_distance_")) {
		if (!has_lod) p_property.usage = PROPERTY_USAGE_NOEDITOR;
		return;
	}
	if (name.begins_with("grass_")) {
		if (type != TYPE_GRASS) p_property.usage = PROPERTY_USAGE_NOEDITOR;
		return;
	}
	if (name == "tree_scene") {
		if (type != TYPE_TREE) p_property.usage = PROPERTY_USAGE_NOEDITOR;
		return;
	}
	if (name == "interactor_scene") {
		if (type != TYPE_INTERACTOR) p_property.usage = PROPERTY_USAGE_NOEDITOR;
		return;
	}
	if (name == "collision_enabled") {
		if (type != TYPE_TREE) p_property.usage = PROPERTY_USAGE_NOEDITOR;
		return;
	}
	if (name == "keep_loaded") {
		// Trees are always persistent, so the toggle only applies to other types.
		if (type == TYPE_TREE) p_property.usage = PROPERTY_USAGE_NOEDITOR;
		return;
	}
	if (name == "shadow_lod_count") {
		// Only the LOD'd MultiMesh types have per-LOD shadow control.
		if (type == TYPE_INTERACTOR) p_property.usage = PROPERTY_USAGE_NOEDITOR;
		return;
	}
}

// ---------------------------------------------------------------------------

void FoliageType::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_type_name", "name"), &FoliageType::set_type_name);
	ClassDB::bind_method(D_METHOD("get_type_name"),         &FoliageType::get_type_name);

	ClassDB::bind_method(D_METHOD("set_type", "t"),                   &FoliageType::set_type);
	ClassDB::bind_method(D_METHOD("get_type"),                        &FoliageType::get_type);
	ClassDB::bind_method(D_METHOD("set_max_distance", "v"),           &FoliageType::set_max_distance);
	ClassDB::bind_method(D_METHOD("get_max_distance"),                &FoliageType::get_max_distance);
	ClassDB::bind_method(D_METHOD("set_max_render_distance", "v"),    &FoliageType::set_max_render_distance);
	ClassDB::bind_method(D_METHOD("get_max_render_distance"),         &FoliageType::get_max_render_distance);
	ClassDB::bind_method(D_METHOD("set_keep_loaded", "v"),            &FoliageType::set_keep_loaded);
	ClassDB::bind_method(D_METHOD("get_keep_loaded"),                 &FoliageType::get_keep_loaded);

	ClassDB::bind_method(D_METHOD("set_lod_count", "n"),              &FoliageType::set_lod_count);
	ClassDB::bind_method(D_METHOD("get_lod_count"),                   &FoliageType::get_lod_count);
	ClassDB::bind_method(D_METHOD("set_lod_distance", "idx", "v"),    &FoliageType::set_lod_distance);
	ClassDB::bind_method(D_METHOD("get_lod_distance", "idx"),         &FoliageType::get_lod_distance);
	ClassDB::bind_method(D_METHOD("set_fade_band", "v"),              &FoliageType::set_fade_band);
	ClassDB::bind_method(D_METHOD("get_fade_band"),                   &FoliageType::get_fade_band);

	ClassDB::bind_method(D_METHOD("set_grass_mesh", "idx", "m"),      &FoliageType::set_grass_mesh);
	ClassDB::bind_method(D_METHOD("get_grass_mesh", "idx"),           &FoliageType::get_grass_mesh);
	ClassDB::bind_method(D_METHOD("set_grass_material", "idx", "m"),  &FoliageType::set_grass_material);
	ClassDB::bind_method(D_METHOD("get_grass_material", "idx"),       &FoliageType::get_grass_material);

	ClassDB::bind_method(D_METHOD("set_tree_scene", "s"),             &FoliageType::set_tree_scene);
	ClassDB::bind_method(D_METHOD("get_tree_scene"),                  &FoliageType::get_tree_scene);

	ClassDB::bind_method(D_METHOD("set_interactor_scene", "s"),       &FoliageType::set_interactor_scene);
	ClassDB::bind_method(D_METHOD("get_interactor_scene"),            &FoliageType::get_interactor_scene);

	ClassDB::bind_method(D_METHOD("set_density", "v"),                &FoliageType::set_density);
	ClassDB::bind_method(D_METHOD("get_density"),                     &FoliageType::get_density);
	ClassDB::bind_method(D_METHOD("set_min_scale", "v"),              &FoliageType::set_min_scale);
	ClassDB::bind_method(D_METHOD("get_min_scale"),                   &FoliageType::get_min_scale);
	ClassDB::bind_method(D_METHOD("set_max_scale", "v"),              &FoliageType::set_max_scale);
	ClassDB::bind_method(D_METHOD("get_max_scale"),                   &FoliageType::get_max_scale);
	ClassDB::bind_method(D_METHOD("set_random_yaw", "v"),             &FoliageType::set_random_yaw);
	ClassDB::bind_method(D_METHOD("get_random_yaw"),                  &FoliageType::get_random_yaw);
	ClassDB::bind_method(D_METHOD("set_align_to_normal", "v"),        &FoliageType::set_align_to_normal);
	ClassDB::bind_method(D_METHOD("get_align_to_normal"),             &FoliageType::get_align_to_normal);
	ClassDB::bind_method(D_METHOD("set_align_amount", "v"),           &FoliageType::set_align_amount);
	ClassDB::bind_method(D_METHOD("get_align_amount"),                &FoliageType::get_align_amount);
	ClassDB::bind_method(D_METHOD("set_slope_limit_deg", "v"),        &FoliageType::set_slope_limit_deg);
	ClassDB::bind_method(D_METHOD("get_slope_limit_deg"),             &FoliageType::get_slope_limit_deg);
	ClassDB::bind_method(D_METHOD("set_min_spacing", "v"),            &FoliageType::set_min_spacing);
	ClassDB::bind_method(D_METHOD("get_min_spacing"),                 &FoliageType::get_min_spacing);
	ClassDB::bind_method(D_METHOD("set_y_offset_min", "v"),           &FoliageType::set_y_offset_min);
	ClassDB::bind_method(D_METHOD("get_y_offset_min"),                &FoliageType::get_y_offset_min);
	ClassDB::bind_method(D_METHOD("set_y_offset_max", "v"),           &FoliageType::set_y_offset_max);
	ClassDB::bind_method(D_METHOD("get_y_offset_max"),                &FoliageType::get_y_offset_max);

	ClassDB::bind_method(D_METHOD("set_collision_enabled", "v"),      &FoliageType::set_collision_enabled);
	ClassDB::bind_method(D_METHOD("get_collision_enabled"),           &FoliageType::get_collision_enabled);

	ClassDB::bind_method(D_METHOD("set_cast_shadow", "v"),            &FoliageType::set_cast_shadow);
	ClassDB::bind_method(D_METHOD("get_cast_shadow"),                 &FoliageType::get_cast_shadow);
	ClassDB::bind_method(D_METHOD("set_shadow_lod_count", "v"),       &FoliageType::set_shadow_lod_count);
	ClassDB::bind_method(D_METHOD("get_shadow_lod_count"),            &FoliageType::get_shadow_lod_count);
	ClassDB::bind_method(D_METHOD("set_material_override", "m"),      &FoliageType::set_material_override);
	ClassDB::bind_method(D_METHOD("get_material_override"),           &FoliageType::get_material_override);

	ClassDB::bind_method(D_METHOD("lod_for_distance", "dist"),        &FoliageType::lod_for_distance);
	ClassDB::bind_method(D_METHOD("is_culled", "dist"),               &FoliageType::is_culled);

	BIND_ENUM_CONSTANT(TYPE_GRASS);
	BIND_ENUM_CONSTANT(TYPE_TREE);
	BIND_ENUM_CONSTANT(TYPE_INTERACTOR);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "type_name"),
			"set_type_name", "get_type_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "type", PROPERTY_HINT_ENUM, "Grass,Tree,Interactor"),
			"set_type", "get_type");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "max_distance"),
			"set_max_distance", "get_max_distance");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "max_render_distance", PROPERTY_HINT_RANGE, "0,5000,1"),
			"set_max_render_distance", "get_max_render_distance");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "keep_loaded"),
			"set_keep_loaded", "get_keep_loaded");

	ADD_GROUP("LOD", "lod_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "lod_count", PROPERTY_HINT_RANGE, "1,5,1"),
			"set_lod_count", "get_lod_count");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "lod_fade_band", PROPERTY_HINT_RANGE, "0,64,0.5"),
			"set_fade_band", "get_fade_band");
	for (int i = 0; i < MAX_LODS - 1; i++) {
		ADD_PROPERTYI(PropertyInfo(Variant::REAL, "lod_distance_" + itos(i), PROPERTY_HINT_RANGE, "0,5000,1"),
				"set_lod_distance", "get_lod_distance", i);
	}

	ADD_GROUP("Grass", "grass_");
	for (int i = 0; i < MAX_LODS; i++) {
		ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, "grass_mesh_" + itos(i),
				PROPERTY_HINT_RESOURCE_TYPE, "Mesh"),
				"set_grass_mesh", "get_grass_mesh", i);
		ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, "grass_material_" + itos(i),
				PROPERTY_HINT_RESOURCE_TYPE, "Material"),
				"set_grass_material", "get_grass_material", i);
	}

	ADD_GROUP("Tree", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "tree_scene",
			PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"),
			"set_tree_scene", "get_tree_scene");

	ADD_GROUP("Interactor", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "interactor_scene",
			PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"),
			"set_interactor_scene", "get_interactor_scene");

	ADD_GROUP("Scatter", "");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "density",         PROPERTY_HINT_RANGE, "0,10,0.001"),   "set_density",         "get_density");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "min_scale",       PROPERTY_HINT_RANGE, "0.01,10,0.01"), "set_min_scale",       "get_min_scale");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "max_scale",       PROPERTY_HINT_RANGE, "0.01,10,0.01"), "set_max_scale",       "get_max_scale");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "random_yaw"),                                            "set_random_yaw",      "get_random_yaw");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "align_to_normal"),                                       "set_align_to_normal", "get_align_to_normal");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "align_amount",    PROPERTY_HINT_RANGE, "0,1,0.01"),     "set_align_amount",    "get_align_amount");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "slope_limit_deg", PROPERTY_HINT_RANGE, "0,90,1"),       "set_slope_limit_deg", "get_slope_limit_deg");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "min_spacing",     PROPERTY_HINT_RANGE, "0,32,0.1"),     "set_min_spacing",     "get_min_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "y_offset_min",    PROPERTY_HINT_RANGE, "-4,4,0.01"),    "set_y_offset_min",    "get_y_offset_min");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "y_offset_max",    PROPERTY_HINT_RANGE, "-4,4,0.01"),    "set_y_offset_max",    "get_y_offset_max");

	ADD_GROUP("Collision", "collision_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collision_enabled"),                                          "set_collision_enabled", "get_collision_enabled");

	ADD_GROUP("Rendering", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "cast_shadow"),                                                "set_cast_shadow",       "get_cast_shadow");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shadow_lod_count", PROPERTY_HINT_RANGE, "0,5,1"),              "set_shadow_lod_count",  "get_shadow_lod_count");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material_override", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_material_override", "get_material_override");
}
