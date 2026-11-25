/* exp_height_fog.h */
#ifndef EXP_HEIGHT_FOG_H
#define EXP_HEIGHT_FOG_H

#include "scene/3d/visual_instance.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/shader.h"

class ExpHeightFog : public VisualInstance {
	GDCLASS(ExpHeightFog, VisualInstance);

private:
	bool fog_enabled = true;
	float density = 0.04f;
	float falloff = 0.15f;
	float ref_height = 0.0f;
	bool ref_height_relative = false;
	float start_distance = 0.0f;
	float max_opacity = 1.0f;
	Color inscatter = Color(0.55, 0.62, 0.72);

	// Optional directional (sun) inscattering.
	bool directional_enabled = false;
	Color directional_color = Color(1.0, 0.9, 0.7);
	Vector3 light_direction = Vector3(0, -1, 0);
	float directional_exponent = 4.0f;

	Ref<Shader> shader;
	Ref<ShaderMaterial> material;
	Ref<ArrayMesh> quad_mesh;

	void _build_resources();
	void _update_material_params();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	virtual AABB get_aabb() const;
	virtual PoolVector<Face3> get_faces(uint32_t p_usage_flags) const;

	void set_fog_enabled(bool p_enabled);
	bool is_fog_enabled() const;

	void set_density(float p_v);
	float get_density() const;
	void set_falloff(float p_v);
	float get_falloff() const;
	void set_ref_height(float p_v);
	float get_ref_height() const;
	void set_ref_height_relative(bool p_enabled);
	bool is_ref_height_relative() const;
	void set_start_distance(float p_v);
	float get_start_distance() const;
	void set_max_opacity(float p_v);
	float get_max_opacity() const;
	void set_inscatter(const Color &p_c);
	Color get_inscatter() const;

	void set_directional_enabled(bool p_enabled);
	bool is_directional_enabled() const;
	void set_directional_color(const Color &p_c);
	Color get_directional_color() const;
	void set_light_direction(const Vector3 &p_dir);
	Vector3 get_light_direction() const;
	void set_directional_exponent(float p_v);
	float get_directional_exponent() const;

	ExpHeightFog();
	~ExpHeightFog();
};

#endif // EXP_HEIGHT_FOG_H
