#ifndef FOLIAGE_TYPE_H
#define FOLIAGE_TYPE_H

#include "core/resource.h"
#include "scene/resources/mesh.h"
#include "scene/resources/material.h"
#include "scene/resources/packed_scene.h"

class FoliageType : public Resource {
	GDCLASS(FoliageType, Resource);

public:
	enum Type {
		TYPE_GRASS      = 0,
		TYPE_TREE       = 1,
		TYPE_INTERACTOR = 2,
	};

	static const int MAX_LODS = 5;

private:
	// Used as a stable identity key in save files.
	String type_name;

	Type  type             = TYPE_GRASS;
	bool  max_distance     = true;
	float max_render_distance = 100.0f;
	// When true, a cell containing this type stays loaded (and rendering) even
	// after its terrain cell streams out — e.g. distant trees with imposters.
	bool  keep_loaded      = false;

	// GRASS + TREE: how many LOD levels are active (1..MAX_LODS).
	int   lod_count        = 1;
	// Ascending transition distances between LOD levels.
	// lod_distances[i] is the distance at which LOD i switches to LOD i+1.
	// lod_count levels need lod_count-1 entries.
	float lod_distances[MAX_LODS - 1];
	float fade_band        = 8.0f;

	// GRASS: up to MAX_LODS (mesh + material) pairs, one per LOD.
	Ref<Mesh>     grass_mesh[MAX_LODS];
	Ref<Material> grass_material[MAX_LODS];

	// TREE: single PackedScene whose children use -lod{n} / -imposter naming.
	// Meshes are extracted at rebuild() time and rendered via MultiMesh.
	Ref<PackedScene> tree_scene;

	// INTERACTOR: one scene instance per placed foliage item; no LOD.
	Ref<PackedScene> interactor_scene;

	// --- Scatter rules (used by the paint brush) ---
	float density       = 0.1f;
	float min_scale     = 0.9f;
	float max_scale     = 1.1f;
	bool  random_yaw    = true;
	bool  align_to_normal = false;
	float align_amount  = 0.0f;
	float slope_limit_deg = 45.0f;
	float min_spacing   = 1.0f;
	float y_offset_min  = 0.0f;
	float y_offset_max  = 0.0f;

	// --- Collision (TREE only; shapes are taken from the tree scene) ---
	bool  collision_enabled = true;

	// --- Rendering ---
	bool          cast_shadow       = true;
	// Number of closest mesh LODs that cast shadows (GRASS/TREE). LODs at or
	// beyond this index — and the imposter — never cast, to cut the shadow pass.
	int           shadow_lod_count  = 1;
	Ref<Material> material_override;

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	void   set_type_name(const String &p);  String get_type_name()  const;

	void set_type(int t);                   int    get_type()        const;
	void set_max_distance(bool v);          bool   get_max_distance() const;
	void set_max_render_distance(float v);  float  get_max_render_distance() const;
	void set_keep_loaded(bool v);           bool   get_keep_loaded() const;

	// GRASS + TREE
	void  set_lod_count(int n);             int   get_lod_count()   const;
	void  set_lod_distance(int idx, float v); float get_lod_distance(int idx) const;
	void  set_fade_band(float v);           float get_fade_band()   const;

	// GRASS
	void         set_grass_mesh(int idx, const Ref<Mesh> &m);     Ref<Mesh>     get_grass_mesh(int idx)     const;
	void         set_grass_material(int idx, const Ref<Material> &m); Ref<Material> get_grass_material(int idx) const;

	// TREE
	void         set_tree_scene(const Ref<PackedScene> &s);        Ref<PackedScene> get_tree_scene()        const;

	// INTERACTOR
	void         set_interactor_scene(const Ref<PackedScene> &s);  Ref<PackedScene> get_interactor_scene()  const;

	// Scatter
	void  set_density(float v);          float get_density()          const;
	void  set_min_scale(float v);        float get_min_scale()        const;
	void  set_max_scale(float v);        float get_max_scale()        const;
	void  set_random_yaw(bool v);        bool  get_random_yaw()       const;
	void  set_align_to_normal(bool v);   bool  get_align_to_normal()  const;
	void  set_align_amount(float v);     float get_align_amount()     const;
	void  set_slope_limit_deg(float v);  float get_slope_limit_deg()  const;
	void  set_min_spacing(float v);      float get_min_spacing()      const;
	void  set_y_offset_min(float v);     float get_y_offset_min()     const;
	void  set_y_offset_max(float v);     float get_y_offset_max()     const;

	// Collision
	void  set_collision_enabled(bool v); bool  get_collision_enabled() const;

	// Rendering
	void  set_cast_shadow(bool v);       bool  get_cast_shadow()       const;
	void  set_shadow_lod_count(int v);   int   get_shadow_lod_count()  const;
	void  set_material_override(const Ref<Material> &m); Ref<Material> get_material_override() const;

	// Returns which LOD index to use at the given camera distance.
	// Uses type's lod_count; result is clamped to [0, p_built_count-1] by caller.
	int  lod_for_distance(float dist) const;

	// True when this instance should be fully hidden (mesh LODs culled).
	// For TYPE_TREE with an imposter, the imposter itself ignores this and stays visible.
	bool is_culled(float dist) const;

	// True if cells containing this type should be loaded everywhere at game
	// start and never streamed out. Trees are always persistent; other types
	// opt in via keep_loaded.
	bool is_persistent() const;

	FoliageType();
};

VARIANT_ENUM_CAST(FoliageType::Type);

#endif // FOLIAGE_TYPE_H
