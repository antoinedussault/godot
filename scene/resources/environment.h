/**************************************************************************/
/*  environment.h                                                         */
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

#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "core/resource.h"
#include "scene/resources/sky.h"
#include "scene/resources/texture.h"
#include "servers/visual_server.h"

class Environment : public Resource {
	GDCLASS(Environment, Resource);

public:
	enum BGMode {

		BG_CLEAR_COLOR,
		BG_COLOR,
		BG_SKY,
		BG_COLOR_SKY,
		BG_CANVAS,
		BG_KEEP,
		BG_CAMERA_FEED,
		BG_MAX
	};

	enum ToneMapper {
		TONE_MAPPER_LINEAR,
		TONE_MAPPER_REINHARDT,
		TONE_MAPPER_FILMIC,
		TONE_MAPPER_ACES,
		TONE_MAPPER_ACES_FITTED
	};

	enum GlowBlendMode {
		GLOW_BLEND_MODE_ADDITIVE,
		GLOW_BLEND_MODE_SCREEN,
		GLOW_BLEND_MODE_SOFTLIGHT,
		GLOW_BLEND_MODE_REPLACE,
	};

	enum DOFBlurQuality {
		DOF_BLUR_QUALITY_LOW,
		DOF_BLUR_QUALITY_MEDIUM,
		DOF_BLUR_QUALITY_HIGH,
	};

	enum DynamicGIDebug {
		DYNAMIC_GI_DEBUG_DISABLED,
		DYNAMIC_GI_DEBUG_LIGHTING,
		DYNAMIC_GI_DEBUG_ALBEDO,
		DYNAMIC_GI_DEBUG_IRRADIANCE,
		DYNAMIC_GI_DEBUG_SSGI,
	};

	enum SSAOBlur {
		SSAO_BLUR_DISABLED,
		SSAO_BLUR_1x1,
		SSAO_BLUR_2x2,
		SSAO_BLUR_3x3
	};

	enum SSAOQuality {
		SSAO_QUALITY_LOW,
		SSAO_QUALITY_MEDIUM,
		SSAO_QUALITY_HIGH
	};

private:
	RID environment;

	BGMode bg_mode;
	Ref<Sky> bg_sky;
	float bg_sky_custom_fov;
	Basis bg_sky_orientation;
	Color bg_color;
	float bg_energy;
	int bg_canvas_max_layer;
	Color ambient_color;
	float ambient_energy;
	float ambient_sky_contribution;
	int camera_feed_id;

	ToneMapper tone_mapper;
	float tonemap_exposure;
	float tonemap_white;
	bool tonemap_auto_exposure;
	float tonemap_auto_exposure_max;
	float tonemap_auto_exposure_min;
	float tonemap_auto_exposure_speed;
	float tonemap_auto_exposure_grey;

	bool adjustment_enabled;
	float adjustment_contrast;
	float adjustment_saturation;
	float adjustment_brightness;
	Ref<Texture> adjustment_color_correction;

	bool ssr_enabled;
	int ssr_max_steps;
	float ssr_fade_in;
	float ssr_fade_out;
	float ssr_depth_tolerance;
	bool ssr_roughness;

	bool ssao_enabled;
	float ssao_radius;
	float ssao_intensity;
	float ssao_radius2;
	float ssao_intensity2;
	float ssao_bias;
	float ssao_direct_light_affect;
	float ssao_ao_channel_affect;
	Color ssao_color;
	SSAOBlur ssao_blur;
	float ssao_edge_sharpness;
	SSAOQuality ssao_quality;

	bool glow_enabled;
	int glow_levels;
	float glow_intensity;
	float glow_strength;
	float glow_bloom;
	GlowBlendMode glow_blend_mode;
	float glow_hdr_bleed_threshold;
	float glow_hdr_bleed_scale;
	float glow_hdr_luminance_cap;
	bool glow_bicubic_upscale;
	bool glow_high_quality;

	bool dof_blur_far_enabled;
	float dof_blur_far_distance;
	float dof_blur_far_transition;
	float dof_blur_far_amount;
	DOFBlurQuality dof_blur_far_quality;

	bool dof_blur_near_enabled;
	float dof_blur_near_distance;
	float dof_blur_near_transition;
	float dof_blur_near_amount;
	DOFBlurQuality dof_blur_near_quality;

	bool fog_enabled;
	Color fog_color;
	Color fog_sun_color;
	float fog_sun_amount;

	bool fog_depth_enabled;
	float fog_depth_begin;
	float fog_depth_end;
	float fog_depth_curve;

	bool fog_transmit_enabled;
	float fog_transmit_curve;

	bool fog_height_enabled;
	float fog_height_min;
	float fog_height_max;
	float fog_height_curve;

	bool volumetric_fog_enabled;
	float volumetric_fog_density;
	float volumetric_fog_height_falloff;
	float volumetric_fog_height_ref;
	bool volumetric_fog_height_relative;
	Color volumetric_fog_albedo;
	Color volumetric_fog_ambient;
	float volumetric_fog_sun_energy;
	float volumetric_fog_anisotropy;
	float volumetric_fog_length;
	float volumetric_fog_extinction;
	int volumetric_fog_samples;
	float volumetric_fog_temporal_blend;

	void _update_volumetric_fog();

	// Dynamic GI and screen space GI.
	bool dynamic_gi_enabled;
	int dynamic_gi_cascades;
	float dynamic_gi_voxel_size;
	float dynamic_gi_energy;
	float dynamic_gi_bounce_feedback;
	float dynamic_gi_normal_bias;
	float dynamic_gi_hysteresis;
	int dynamic_gi_rays;
	bool dynamic_gi_reflection_occlusion;
	uint32_t dynamic_gi_cull_mask;
	DynamicGIDebug dynamic_gi_debug;
	bool ssgi_enabled;
	float ssgi_radius;
	float ssgi_intensity;
	int ssgi_rays;
	int ssgi_steps;
	float ssgi_thickness;
	float ssgi_temporal_blend;
	bool ssgi_half_resolution;

	void _update_dynamic_gi();
	void _update_ssgi();

	// ExpHeightFog (modules/exp_height_fog) ported onto Environment: a cheap
	// single-pass analytic exponential height fog, no shadow sampling. The node
	// still works standalone; this is its Environment home for the transition.
	bool exp_height_fog_enabled;
	float exp_height_fog_density;
	float exp_height_fog_falloff;
	float exp_height_fog_ref_height;
	bool exp_height_fog_ref_height_relative;
	float exp_height_fog_start_distance;
	float exp_height_fog_max_opacity;
	Color exp_height_fog_inscatter;
	bool exp_height_fog_directional_enabled;
	Color exp_height_fog_directional_color;
	Vector3 exp_height_fog_light_direction;
	float exp_height_fog_directional_exponent;

	void _update_exp_height_fog();

protected:
	static void _bind_methods();
	virtual void _validate_property(PropertyInfo &property) const;

public:
	void set_background(BGMode p_bg);
	void set_sky(const Ref<Sky> &p_sky);
	void set_sky_custom_fov(float p_scale);
	void set_sky_orientation(const Basis &p_orientation);
	void set_sky_rotation(const Vector3 &p_euler_rad);
	void set_sky_rotation_degrees(const Vector3 &p_euler_deg);
	void set_bg_color(const Color &p_color);
	void set_bg_energy(float p_energy);
	void set_canvas_max_layer(int p_max_layer);
	void set_ambient_light_color(const Color &p_color);
	void set_ambient_light_energy(float p_energy);
	void set_ambient_light_sky_contribution(float p_energy);
	void set_camera_feed_id(int p_camera_feed_id);

	BGMode get_background() const;
	Ref<Sky> get_sky() const;
	float get_sky_custom_fov() const;
	Basis get_sky_orientation() const;
	Vector3 get_sky_rotation() const;
	Vector3 get_sky_rotation_degrees() const;
	Color get_bg_color() const;
	float get_bg_energy() const;
	int get_canvas_max_layer() const;
	Color get_ambient_light_color() const;
	float get_ambient_light_energy() const;
	float get_ambient_light_sky_contribution() const;
	int get_camera_feed_id() const;

	void set_tonemapper(ToneMapper p_tone_mapper);
	ToneMapper get_tonemapper() const;

	void set_tonemap_exposure(float p_exposure);
	float get_tonemap_exposure() const;

	void set_tonemap_white(float p_white);
	float get_tonemap_white() const;

	void set_tonemap_auto_exposure(bool p_enabled);
	bool get_tonemap_auto_exposure() const;

	void set_tonemap_auto_exposure_max(float p_auto_exposure_max);
	float get_tonemap_auto_exposure_max() const;

	void set_tonemap_auto_exposure_min(float p_auto_exposure_min);
	float get_tonemap_auto_exposure_min() const;

	void set_tonemap_auto_exposure_speed(float p_auto_exposure_speed);
	float get_tonemap_auto_exposure_speed() const;

	void set_tonemap_auto_exposure_grey(float p_auto_exposure_grey);
	float get_tonemap_auto_exposure_grey() const;

	void set_adjustment_enable(bool p_enable);
	bool is_adjustment_enabled() const;

	void set_adjustment_brightness(float p_brightness);
	float get_adjustment_brightness() const;

	void set_adjustment_contrast(float p_contrast);
	float get_adjustment_contrast() const;

	void set_adjustment_saturation(float p_saturation);
	float get_adjustment_saturation() const;

	void set_adjustment_color_correction(const Ref<Texture> &p_ramp);
	Ref<Texture> get_adjustment_color_correction() const;

	void set_ssr_enabled(bool p_enable);
	bool is_ssr_enabled() const;

	void set_ssr_max_steps(int p_steps);
	int get_ssr_max_steps() const;

	void set_ssr_fade_in(float p_fade_in);
	float get_ssr_fade_in() const;

	void set_ssr_fade_out(float p_fade_out);
	float get_ssr_fade_out() const;

	void set_ssr_depth_tolerance(float p_depth_tolerance);
	float get_ssr_depth_tolerance() const;

	void set_ssr_rough(bool p_enable);
	bool is_ssr_rough() const;

	void set_ssao_enabled(bool p_enable);
	bool is_ssao_enabled() const;

	void set_ssao_radius(float p_radius);
	float get_ssao_radius() const;

	void set_ssao_intensity(float p_intensity);
	float get_ssao_intensity() const;

	void set_ssao_radius2(float p_radius);
	float get_ssao_radius2() const;

	void set_ssao_intensity2(float p_intensity);
	float get_ssao_intensity2() const;

	void set_ssao_bias(float p_bias);
	float get_ssao_bias() const;

	void set_ssao_direct_light_affect(float p_direct_light_affect);
	float get_ssao_direct_light_affect() const;

	void set_ssao_ao_channel_affect(float p_ao_channel_affect);
	float get_ssao_ao_channel_affect() const;

	void set_ssao_color(const Color &p_color);
	Color get_ssao_color() const;

	void set_ssao_blur(SSAOBlur p_blur);
	SSAOBlur get_ssao_blur() const;

	void set_ssao_quality(SSAOQuality p_quality);
	SSAOQuality get_ssao_quality() const;

	void set_ssao_edge_sharpness(float p_edge_sharpness);
	float get_ssao_edge_sharpness() const;

	void set_glow_enabled(bool p_enabled);
	bool is_glow_enabled() const;

	void set_glow_level(int p_level, bool p_enabled);
	bool is_glow_level_enabled(int p_level) const;

	void set_glow_intensity(float p_intensity);
	float get_glow_intensity() const;

	void set_glow_strength(float p_strength);
	float get_glow_strength() const;

	void set_glow_bloom(float p_threshold);
	float get_glow_bloom() const;

	void set_glow_blend_mode(GlowBlendMode p_mode);
	GlowBlendMode get_glow_blend_mode() const;

	void set_glow_hdr_bleed_threshold(float p_threshold);
	float get_glow_hdr_bleed_threshold() const;

	void set_glow_hdr_luminance_cap(float p_amount);
	float get_glow_hdr_luminance_cap() const;

	void set_glow_hdr_bleed_scale(float p_scale);
	float get_glow_hdr_bleed_scale() const;

	void set_glow_bicubic_upscale(bool p_enable);
	bool is_glow_bicubic_upscale_enabled() const;

	void set_glow_high_quality(bool p_enable);
	bool is_glow_high_quality_enabled() const;

	void set_dof_blur_far_enabled(bool p_enable);
	bool is_dof_blur_far_enabled() const;

	void set_dof_blur_far_distance(float p_distance);
	float get_dof_blur_far_distance() const;

	void set_dof_blur_far_transition(float p_distance);
	float get_dof_blur_far_transition() const;

	void set_dof_blur_far_amount(float p_amount);
	float get_dof_blur_far_amount() const;

	void set_dof_blur_far_quality(DOFBlurQuality p_quality);
	DOFBlurQuality get_dof_blur_far_quality() const;

	void set_dof_blur_near_enabled(bool p_enable);
	bool is_dof_blur_near_enabled() const;

	void set_dof_blur_near_distance(float p_distance);
	float get_dof_blur_near_distance() const;

	void set_dof_blur_near_transition(float p_distance);
	float get_dof_blur_near_transition() const;

	void set_dof_blur_near_amount(float p_amount);
	float get_dof_blur_near_amount() const;

	void set_dof_blur_near_quality(DOFBlurQuality p_quality);
	DOFBlurQuality get_dof_blur_near_quality() const;

	void set_fog_enabled(bool p_enabled);
	bool is_fog_enabled() const;

	void set_fog_color(const Color &p_color);
	Color get_fog_color() const;

	void set_fog_sun_color(const Color &p_color);
	Color get_fog_sun_color() const;

	void set_fog_sun_amount(float p_amount);
	float get_fog_sun_amount() const;

	void set_fog_depth_enabled(bool p_enabled);
	bool is_fog_depth_enabled() const;

	void set_fog_depth_begin(float p_distance);
	float get_fog_depth_begin() const;

	void set_fog_depth_end(float p_distance);
	float get_fog_depth_end() const;

	void set_fog_depth_curve(float p_curve);
	float get_fog_depth_curve() const;

	void set_fog_transmit_enabled(bool p_enabled);
	bool is_fog_transmit_enabled() const;

	void set_fog_transmit_curve(float p_curve);
	float get_fog_transmit_curve() const;

	void set_fog_height_enabled(bool p_enabled);
	bool is_fog_height_enabled() const;

	void set_fog_height_min(float p_distance);
	float get_fog_height_min() const;

	void set_fog_height_max(float p_distance);
	float get_fog_height_max() const;

	void set_fog_height_curve(float p_distance);
	float get_fog_height_curve() const;

	void set_volumetric_fog_enabled(bool p_enabled);
	bool is_volumetric_fog_enabled() const;
	void set_volumetric_fog_density(float p_density);
	float get_volumetric_fog_density() const;
	void set_volumetric_fog_height_falloff(float p_falloff);
	float get_volumetric_fog_height_falloff() const;
	void set_volumetric_fog_height_ref(float p_ref);
	float get_volumetric_fog_height_ref() const;
	void set_volumetric_fog_height_relative(bool p_relative);
	bool is_volumetric_fog_height_relative() const;
	void set_volumetric_fog_albedo(const Color &p_albedo);
	Color get_volumetric_fog_albedo() const;
	void set_volumetric_fog_ambient(const Color &p_ambient);
	Color get_volumetric_fog_ambient() const;
	void set_volumetric_fog_sun_energy(float p_energy);
	float get_volumetric_fog_sun_energy() const;
	void set_volumetric_fog_anisotropy(float p_anisotropy);
	float get_volumetric_fog_anisotropy() const;
	void set_volumetric_fog_length(float p_length);
	float get_volumetric_fog_length() const;
	void set_volumetric_fog_extinction(float p_extinction);
	float get_volumetric_fog_extinction() const;
	void set_volumetric_fog_samples(int p_samples);
	int get_volumetric_fog_samples() const;
	void set_volumetric_fog_temporal_blend(float p_blend);
	float get_volumetric_fog_temporal_blend() const;

	void set_dynamic_gi_enabled(bool p_value);
	bool is_dynamic_gi_enabled() const;
	void set_dynamic_gi_cascades(int p_value);
	int get_dynamic_gi_cascades() const;
	void set_dynamic_gi_voxel_size(float p_value);
	float get_dynamic_gi_voxel_size() const;
	void set_dynamic_gi_energy(float p_value);
	float get_dynamic_gi_energy() const;
	void set_dynamic_gi_bounce_feedback(float p_value);
	float get_dynamic_gi_bounce_feedback() const;
	void set_dynamic_gi_normal_bias(float p_value);
	float get_dynamic_gi_normal_bias() const;
	void set_dynamic_gi_hysteresis(float p_value);
	float get_dynamic_gi_hysteresis() const;
	void set_dynamic_gi_rays(int p_value);
	int get_dynamic_gi_rays() const;
	void set_dynamic_gi_reflection_occlusion(bool p_value);
	bool is_dynamic_gi_reflection_occlusion() const;
	void set_dynamic_gi_cull_mask(uint32_t p_value);
	uint32_t get_dynamic_gi_cull_mask() const;
	void set_dynamic_gi_debug(DynamicGIDebug p_value);
	DynamicGIDebug get_dynamic_gi_debug() const;
	void set_ssgi_enabled(bool p_value);
	bool is_ssgi_enabled() const;
	void set_ssgi_radius(float p_value);
	float get_ssgi_radius() const;
	void set_ssgi_intensity(float p_value);
	float get_ssgi_intensity() const;
	void set_ssgi_rays(int p_value);
	int get_ssgi_rays() const;
	void set_ssgi_steps(int p_value);
	int get_ssgi_steps() const;
	void set_ssgi_thickness(float p_value);
	float get_ssgi_thickness() const;
	void set_ssgi_temporal_blend(float p_value);
	float get_ssgi_temporal_blend() const;
	void set_ssgi_half_resolution(bool p_value);
	bool is_ssgi_half_resolution() const;

	void set_exp_height_fog_enabled(bool p_enabled);
	bool is_exp_height_fog_enabled() const;
	void set_exp_height_fog_density(float p_density);
	float get_exp_height_fog_density() const;
	void set_exp_height_fog_falloff(float p_falloff);
	float get_exp_height_fog_falloff() const;
	void set_exp_height_fog_ref_height(float p_ref_height);
	float get_exp_height_fog_ref_height() const;
	void set_exp_height_fog_ref_height_relative(bool p_relative);
	bool is_exp_height_fog_ref_height_relative() const;
	void set_exp_height_fog_start_distance(float p_distance);
	float get_exp_height_fog_start_distance() const;
	void set_exp_height_fog_max_opacity(float p_opacity);
	float get_exp_height_fog_max_opacity() const;
	void set_exp_height_fog_inscatter(const Color &p_color);
	Color get_exp_height_fog_inscatter() const;
	void set_exp_height_fog_directional_enabled(bool p_enabled);
	bool is_exp_height_fog_directional_enabled() const;
	void set_exp_height_fog_directional_color(const Color &p_color);
	Color get_exp_height_fog_directional_color() const;
	void set_exp_height_fog_light_direction(const Vector3 &p_dir);
	Vector3 get_exp_height_fog_light_direction() const;
	void set_exp_height_fog_directional_exponent(float p_exponent);
	float get_exp_height_fog_directional_exponent() const;

	virtual RID get_rid() const;

	Environment();
	~Environment();
};

VARIANT_ENUM_CAST(Environment::BGMode)
VARIANT_ENUM_CAST(Environment::ToneMapper)
VARIANT_ENUM_CAST(Environment::GlowBlendMode)
VARIANT_ENUM_CAST(Environment::DOFBlurQuality)
VARIANT_ENUM_CAST(Environment::SSAOQuality)
VARIANT_ENUM_CAST(Environment::DynamicGIDebug)
VARIANT_ENUM_CAST(Environment::SSAOBlur)

#endif // ENVIRONMENT_H
