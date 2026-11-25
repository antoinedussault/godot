/**************************************************************************/
/*  light.h                                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#ifndef LIGHT_H
#define LIGHT_H

#include "scene/3d/visual_instance.h"
#include "scene/resources/texture.h"
#include "servers/visual_server.h"

class Light : public VisualInstance {
	GDCLASS(Light, VisualInstance);
	OBJ_CATEGORY("3D Light Nodes");

public:
	enum Param {
		PARAM_ENERGY = VS::LIGHT_PARAM_ENERGY,
		PARAM_INDIRECT_ENERGY = VS::LIGHT_PARAM_INDIRECT_ENERGY,
		PARAM_SIZE = VS::LIGHT_PARAM_SIZE,
		PARAM_SPECULAR = VS::LIGHT_PARAM_SPECULAR,
		PARAM_RANGE = VS::LIGHT_PARAM_RANGE,
		PARAM_ATTENUATION = VS::LIGHT_PARAM_ATTENUATION,
		PARAM_SPOT_ANGLE = VS::LIGHT_PARAM_SPOT_ANGLE,
		PARAM_SPOT_ATTENUATION = VS::LIGHT_PARAM_SPOT_ATTENUATION,
		PARAM_CONTACT_SHADOW_SIZE = VS::LIGHT_PARAM_CONTACT_SHADOW_SIZE,
		PARAM_SHADOW_MAX_DISTANCE = VS::LIGHT_PARAM_SHADOW_MAX_DISTANCE,
		PARAM_SHADOW_SPLIT_1_OFFSET = VS::LIGHT_PARAM_SHADOW_SPLIT_1_OFFSET,
		PARAM_SHADOW_SPLIT_2_OFFSET = VS::LIGHT_PARAM_SHADOW_SPLIT_2_OFFSET,
		PARAM_SHADOW_SPLIT_3_OFFSET = VS::LIGHT_PARAM_SHADOW_SPLIT_3_OFFSET,
		PARAM_SHADOW_NORMAL_BIAS = VS::LIGHT_PARAM_SHADOW_NORMAL_BIAS,
		PARAM_SHADOW_BIAS = VS::LIGHT_PARAM_SHADOW_BIAS,
		PARAM_SHADOW_BIAS_SPLIT_SCALE = VS::LIGHT_PARAM_SHADOW_BIAS_SPLIT_SCALE,
		PARAM_MAX = VS::LIGHT_PARAM_MAX
	};

	enum BakeMode {
		BAKE_DISABLED,
		BAKE_INDIRECT,
		BAKE_ALL
	};

private:
	Color color;
	float param[PARAM_MAX];
	Color shadow_color;
	Ref<Texture> projector;
	bool lens_streak_enabled = false;
	Color lens_streak_color = Color(1, 1, 1);
	float lens_streak_intensity = 1.0;
	float lens_streak_length = 0.6;
	float lens_streak_thickness = 0.02;
	float lens_streak_angle = 0.0;
	float lens_streak_source_size = 0.1;
	float lens_streak_max_distance = 100.0;
	void _update_lens_streak();
	bool shadow;
	bool negative;
	bool reverse_cull;
	uint32_t cull_mask;
	VS::LightType type;
	bool editor_only;
	void _update_visibility();
	BakeMode bake_mode;

	// bind helpers

	virtual void owner_changed_notify();

protected:
	RID light;

	static void _bind_methods();
	void _notification(int p_what);
	virtual void _validate_property(PropertyInfo &property) const;

	Light(VisualServer::LightType p_type);

public:
	VS::LightType get_light_type() const { return type; }

	void set_editor_only(bool p_editor_only);
	bool is_editor_only() const;

	void set_param(Param p_param, float p_value);
	float get_param(Param p_param) const;

	void set_shadow(bool p_enable);
	bool has_shadow() const;

	void set_negative(bool p_enable);
	bool is_negative() const;

	void set_cull_mask(uint32_t p_cull_mask);
	uint32_t get_cull_mask() const;

	void set_color(const Color &p_color);
	Color get_color() const;

	void set_shadow_color(const Color &p_shadow_color);
	Color get_shadow_color() const;

	void set_projector(const Ref<Texture> &p_texture);
	Ref<Texture> get_projector() const;

	void set_lens_streak_enabled(bool p_enabled);
	bool is_lens_streak_enabled() const;
	void set_lens_streak_color(const Color &p_color);
	Color get_lens_streak_color() const;
	void set_lens_streak_intensity(float p_intensity);
	float get_lens_streak_intensity() const;
	void set_lens_streak_length(float p_length);
	float get_lens_streak_length() const;
	void set_lens_streak_thickness(float p_thickness);
	float get_lens_streak_thickness() const;
	void set_lens_streak_angle(float p_angle);
	float get_lens_streak_angle() const;
	void set_lens_streak_source_size(float p_source_size);
	float get_lens_streak_source_size() const;
	void set_lens_streak_max_distance(float p_max_distance);
	float get_lens_streak_max_distance() const;

	void set_shadow_reverse_cull_face(bool p_enable);
	bool get_shadow_reverse_cull_face() const;

	void set_bake_mode(BakeMode p_mode);
	BakeMode get_bake_mode() const;

	virtual AABB get_aabb() const;
	virtual PoolVector<Face3> get_faces(uint32_t p_usage_flags) const;

	Light();
	~Light();
};

VARIANT_ENUM_CAST(Light::Param);
VARIANT_ENUM_CAST(Light::BakeMode);

class DirectionalLight : public Light {
	GDCLASS(DirectionalLight, Light);

public:
	enum ShadowMode {
		SHADOW_ORTHOGONAL,
		SHADOW_PARALLEL_2_SPLITS,
		SHADOW_PARALLEL_4_SPLITS
	};

	enum ShadowDepthRange {
		SHADOW_DEPTH_RANGE_STABLE = VS::LIGHT_DIRECTIONAL_SHADOW_DEPTH_RANGE_STABLE,
		SHADOW_DEPTH_RANGE_OPTIMIZED = VS::LIGHT_DIRECTIONAL_SHADOW_DEPTH_RANGE_OPTIMIZED,
	};

private:
	bool blend_splits;
	ShadowMode shadow_mode;
	ShadowDepthRange shadow_depth_range;

	bool godrays_enabled = false;
	float godrays_intensity = 0.6;
	float godrays_threshold = 1.0;
	Color godrays_tint = Color(1.0, 0.95, 0.85);
	float godrays_occlusion = 0.35;
	int godrays_samples = 64;
	float godrays_density = 0.7;
	float godrays_decay = 0.9;
	float godrays_weight = 1.0;
	float godrays_temporal_blend = 0.0;
	void _update_godrays();

protected:
	static void _bind_methods();
	virtual void _validate_property(PropertyInfo &property) const;

public:
	void set_shadow_mode(ShadowMode p_mode);
	ShadowMode get_shadow_mode() const;

	void set_shadow_depth_range(ShadowDepthRange p_range);
	ShadowDepthRange get_shadow_depth_range() const;

	void set_blend_splits(bool p_enable);
	bool is_blend_splits_enabled() const;

	void set_godrays_enabled(bool p_enabled);
	bool is_godrays_enabled() const;
	void set_godrays_intensity(float p_intensity);
	float get_godrays_intensity() const;
	void set_godrays_threshold(float p_threshold);
	float get_godrays_threshold() const;
	void set_godrays_tint(const Color &p_tint);
	Color get_godrays_tint() const;
	void set_godrays_occlusion(float p_occlusion);
	float get_godrays_occlusion() const;
	void set_godrays_samples(int p_samples);
	int get_godrays_samples() const;
	void set_godrays_density(float p_density);
	float get_godrays_density() const;
	void set_godrays_decay(float p_decay);
	float get_godrays_decay() const;
	void set_godrays_weight(float p_weight);
	float get_godrays_weight() const;
	void set_godrays_temporal_blend(float p_temporal_blend);
	float get_godrays_temporal_blend() const;

	DirectionalLight();
};

VARIANT_ENUM_CAST(DirectionalLight::ShadowMode)
VARIANT_ENUM_CAST(DirectionalLight::ShadowDepthRange)

class OmniLight : public Light {
	GDCLASS(OmniLight, Light);

public:
	// omni light
	enum ShadowMode {
		SHADOW_DUAL_PARABOLOID,
		SHADOW_CUBE,
	};

	// omni light
	enum ShadowDetail {
		SHADOW_DETAIL_VERTICAL,
		SHADOW_DETAIL_HORIZONTAL
	};

private:
	ShadowMode shadow_mode;
	ShadowDetail shadow_detail;

protected:
	static void _bind_methods();

public:
	void set_shadow_mode(ShadowMode p_mode);
	ShadowMode get_shadow_mode() const;

	void set_shadow_detail(ShadowDetail p_detail);
	ShadowDetail get_shadow_detail() const;

	OmniLight();
};

VARIANT_ENUM_CAST(OmniLight::ShadowMode)
VARIANT_ENUM_CAST(OmniLight::ShadowDetail)

class SpotLight : public Light {
	GDCLASS(SpotLight, Light);

protected:
	static void _bind_methods();

public:
	virtual String get_configuration_warning() const;

	SpotLight() :
			Light(VisualServer::LIGHT_SPOT) {}
};

#endif // LIGHT_H
