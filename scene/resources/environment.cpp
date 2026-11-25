/**************************************************************************/
/*  environment.cpp                                                       */
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

#include "environment.h"

#include "core/project_settings.h"
#include "servers/visual_server.h"
#include "texture.h"

RID Environment::get_rid() const {
	return environment;
}

void Environment::set_background(BGMode p_bg) {
	bg_mode = p_bg;
	VS::get_singleton()->environment_set_background(environment, VS::EnvironmentBG(p_bg));
	_change_notify();
}

void Environment::set_sky(const Ref<Sky> &p_sky) {
	bg_sky = p_sky;

	RID sb_rid;
	if (bg_sky.is_valid()) {
		sb_rid = bg_sky->get_rid();
	}

	VS::get_singleton()->environment_set_sky(environment, sb_rid);
}

void Environment::set_sky_custom_fov(float p_scale) {
	bg_sky_custom_fov = p_scale;
	VS::get_singleton()->environment_set_sky_custom_fov(environment, p_scale);
}
void Environment::set_sky_orientation(const Basis &p_orientation) {
	bg_sky_orientation = p_orientation;
	_change_notify("background_sky_rotation");
	_change_notify("background_sky_rotation_degrees");
	VS::get_singleton()->environment_set_sky_orientation(environment, bg_sky_orientation);
}
void Environment::set_sky_rotation(const Vector3 &p_euler_rad) {
	bg_sky_orientation.set_euler(p_euler_rad);
	_change_notify("background_sky_orientation");
	_change_notify("background_sky_rotation_degrees");
	VS::get_singleton()->environment_set_sky_orientation(environment, bg_sky_orientation);
}
void Environment::set_sky_rotation_degrees(const Vector3 &p_euler_deg) {
	set_sky_rotation(p_euler_deg * Math_PI / 180.0);
	_change_notify("background_sky_rotation");
}
void Environment::set_bg_color(const Color &p_color) {
	bg_color = p_color;
	VS::get_singleton()->environment_set_bg_color(environment, p_color);
}
void Environment::set_bg_energy(float p_energy) {
	bg_energy = p_energy;
	VS::get_singleton()->environment_set_bg_energy(environment, p_energy);
}
void Environment::set_canvas_max_layer(int p_max_layer) {
	bg_canvas_max_layer = p_max_layer;
	VS::get_singleton()->environment_set_canvas_max_layer(environment, p_max_layer);
}
void Environment::set_ambient_light_color(const Color &p_color) {
	ambient_color = p_color;
	VS::get_singleton()->environment_set_ambient_light(environment, ambient_color, ambient_energy, ambient_sky_contribution);
}
void Environment::set_ambient_light_energy(float p_energy) {
	ambient_energy = p_energy;
	VS::get_singleton()->environment_set_ambient_light(environment, ambient_color, ambient_energy, ambient_sky_contribution);
}
void Environment::set_ambient_light_sky_contribution(float p_energy) {
	// Sky contribution values outside the [0.0; 1.0] range don't make sense and
	// can result in negative colors.
	ambient_sky_contribution = CLAMP(p_energy, 0.0, 1.0);
	VS::get_singleton()->environment_set_ambient_light(environment, ambient_color, ambient_energy, ambient_sky_contribution);
}

void Environment::set_camera_feed_id(int p_camera_feed_id) {
	camera_feed_id = p_camera_feed_id;
	VS::get_singleton()->environment_set_camera_feed_id(environment, camera_feed_id);
};

Environment::BGMode Environment::get_background() const {
	return bg_mode;
}
Ref<Sky> Environment::get_sky() const {
	return bg_sky;
}

float Environment::get_sky_custom_fov() const {
	return bg_sky_custom_fov;
}

Basis Environment::get_sky_orientation() const {
	return bg_sky_orientation;
}

Vector3 Environment::get_sky_rotation() const {
	// should we cache this? maybe overkill
	return bg_sky_orientation.get_euler();
}

Vector3 Environment::get_sky_rotation_degrees() const {
	return get_sky_rotation() * 180.0 / Math_PI;
}

Color Environment::get_bg_color() const {
	return bg_color;
}
float Environment::get_bg_energy() const {
	return bg_energy;
}
int Environment::get_canvas_max_layer() const {
	return bg_canvas_max_layer;
}
Color Environment::get_ambient_light_color() const {
	return ambient_color;
}
float Environment::get_ambient_light_energy() const {
	return ambient_energy;
}
float Environment::get_ambient_light_sky_contribution() const {
	return ambient_sky_contribution;
}
int Environment::get_camera_feed_id() const {
	return camera_feed_id;
}

void Environment::set_tonemapper(ToneMapper p_tone_mapper) {
	tone_mapper = p_tone_mapper;
	VS::get_singleton()->environment_set_tonemap(environment, VS::EnvironmentToneMapper(tone_mapper), tonemap_exposure, tonemap_white, tonemap_auto_exposure, tonemap_auto_exposure_min, tonemap_auto_exposure_max, tonemap_auto_exposure_speed, tonemap_auto_exposure_grey);
	_change_notify();
}

Environment::ToneMapper Environment::get_tonemapper() const {
	return tone_mapper;
}

void Environment::set_tonemap_exposure(float p_exposure) {
	tonemap_exposure = p_exposure;
	VS::get_singleton()->environment_set_tonemap(environment, VS::EnvironmentToneMapper(tone_mapper), tonemap_exposure, tonemap_white, tonemap_auto_exposure, tonemap_auto_exposure_min, tonemap_auto_exposure_max, tonemap_auto_exposure_speed, tonemap_auto_exposure_grey);
}

float Environment::get_tonemap_exposure() const {
	return tonemap_exposure;
}

void Environment::set_tonemap_white(float p_white) {
	tonemap_white = p_white;
	VS::get_singleton()->environment_set_tonemap(environment, VS::EnvironmentToneMapper(tone_mapper), tonemap_exposure, tonemap_white, tonemap_auto_exposure, tonemap_auto_exposure_min, tonemap_auto_exposure_max, tonemap_auto_exposure_speed, tonemap_auto_exposure_grey);
}
float Environment::get_tonemap_white() const {
	return tonemap_white;
}

void Environment::set_tonemap_auto_exposure(bool p_enabled) {
	tonemap_auto_exposure = p_enabled;
	VS::get_singleton()->environment_set_tonemap(environment, VS::EnvironmentToneMapper(tone_mapper), tonemap_exposure, tonemap_white, tonemap_auto_exposure, tonemap_auto_exposure_min, tonemap_auto_exposure_max, tonemap_auto_exposure_speed, tonemap_auto_exposure_grey);
	_change_notify();
}
bool Environment::get_tonemap_auto_exposure() const {
	return tonemap_auto_exposure;
}

void Environment::set_tonemap_auto_exposure_max(float p_auto_exposure_max) {
	tonemap_auto_exposure_max = p_auto_exposure_max;
	VS::get_singleton()->environment_set_tonemap(environment, VS::EnvironmentToneMapper(tone_mapper), tonemap_exposure, tonemap_white, tonemap_auto_exposure, tonemap_auto_exposure_min, tonemap_auto_exposure_max, tonemap_auto_exposure_speed, tonemap_auto_exposure_grey);
}
float Environment::get_tonemap_auto_exposure_max() const {
	return tonemap_auto_exposure_max;
}

void Environment::set_tonemap_auto_exposure_min(float p_auto_exposure_min) {
	tonemap_auto_exposure_min = p_auto_exposure_min;
	VS::get_singleton()->environment_set_tonemap(environment, VS::EnvironmentToneMapper(tone_mapper), tonemap_exposure, tonemap_white, tonemap_auto_exposure, tonemap_auto_exposure_min, tonemap_auto_exposure_max, tonemap_auto_exposure_speed, tonemap_auto_exposure_grey);
}
float Environment::get_tonemap_auto_exposure_min() const {
	return tonemap_auto_exposure_min;
}

void Environment::set_tonemap_auto_exposure_speed(float p_auto_exposure_speed) {
	tonemap_auto_exposure_speed = p_auto_exposure_speed;
	VS::get_singleton()->environment_set_tonemap(environment, VS::EnvironmentToneMapper(tone_mapper), tonemap_exposure, tonemap_white, tonemap_auto_exposure, tonemap_auto_exposure_min, tonemap_auto_exposure_max, tonemap_auto_exposure_speed, tonemap_auto_exposure_grey);
}
float Environment::get_tonemap_auto_exposure_speed() const {
	return tonemap_auto_exposure_speed;
}

void Environment::set_tonemap_auto_exposure_grey(float p_auto_exposure_grey) {
	tonemap_auto_exposure_grey = p_auto_exposure_grey;
	VS::get_singleton()->environment_set_tonemap(environment, VS::EnvironmentToneMapper(tone_mapper), tonemap_exposure, tonemap_white, tonemap_auto_exposure, tonemap_auto_exposure_min, tonemap_auto_exposure_max, tonemap_auto_exposure_speed, tonemap_auto_exposure_grey);
}
float Environment::get_tonemap_auto_exposure_grey() const {
	return tonemap_auto_exposure_grey;
}

void Environment::set_adjustment_enable(bool p_enable) {
	adjustment_enabled = p_enable;
	VS::get_singleton()->environment_set_adjustment(environment, adjustment_enabled, adjustment_brightness, adjustment_contrast, adjustment_saturation, adjustment_color_correction.is_valid() ? adjustment_color_correction->get_rid() : RID());
	_change_notify();
}

bool Environment::is_adjustment_enabled() const {
	return adjustment_enabled;
}

void Environment::set_adjustment_brightness(float p_brightness) {
	adjustment_brightness = p_brightness;
	VS::get_singleton()->environment_set_adjustment(environment, adjustment_enabled, adjustment_brightness, adjustment_contrast, adjustment_saturation, adjustment_color_correction.is_valid() ? adjustment_color_correction->get_rid() : RID());
}
float Environment::get_adjustment_brightness() const {
	return adjustment_brightness;
}

void Environment::set_adjustment_contrast(float p_contrast) {
	adjustment_contrast = p_contrast;
	VS::get_singleton()->environment_set_adjustment(environment, adjustment_enabled, adjustment_brightness, adjustment_contrast, adjustment_saturation, adjustment_color_correction.is_valid() ? adjustment_color_correction->get_rid() : RID());
}
float Environment::get_adjustment_contrast() const {
	return adjustment_contrast;
}

void Environment::set_adjustment_saturation(float p_saturation) {
	adjustment_saturation = p_saturation;
	VS::get_singleton()->environment_set_adjustment(environment, adjustment_enabled, adjustment_brightness, adjustment_contrast, adjustment_saturation, adjustment_color_correction.is_valid() ? adjustment_color_correction->get_rid() : RID());
}
float Environment::get_adjustment_saturation() const {
	return adjustment_saturation;
}

void Environment::set_adjustment_color_correction(const Ref<Texture> &p_ramp) {
	adjustment_color_correction = p_ramp;
	VS::get_singleton()->environment_set_adjustment(environment, adjustment_enabled, adjustment_brightness, adjustment_contrast, adjustment_saturation, adjustment_color_correction.is_valid() ? adjustment_color_correction->get_rid() : RID());
}
Ref<Texture> Environment::get_adjustment_color_correction() const {
	return adjustment_color_correction;
}

void Environment::_validate_property(PropertyInfo &property) const {
	if (property.name == "background_sky" || property.name == "background_sky_custom_fov" || property.name == "background_sky_orientation" || property.name == "background_sky_rotation" || property.name == "background_sky_rotation_degrees" || property.name == "ambient_light/sky_contribution") {
		if (bg_mode != BG_SKY && bg_mode != BG_COLOR_SKY) {
			property.usage = PROPERTY_USAGE_NOEDITOR | PROPERTY_USAGE_INTERNAL;
		}
	}

	if (property.name == "background_color") {
		if (bg_mode != BG_COLOR && bg_mode != BG_COLOR_SKY) {
			property.usage = PROPERTY_USAGE_NOEDITOR | PROPERTY_USAGE_INTERNAL;
		}
	}

	if (property.name == "background_canvas_max_layer") {
		if (bg_mode != BG_CANVAS) {
			property.usage = PROPERTY_USAGE_NOEDITOR | PROPERTY_USAGE_INTERNAL;
		}
	}

	if (property.name == "background_camera_feed_id") {
		if (bg_mode != BG_CAMERA_FEED) {
			property.usage = PROPERTY_USAGE_NOEDITOR;
		}
	}

	if (property.name == "tonemap_white") {
		if (tone_mapper == TONE_MAPPER_LINEAR) {
			property.usage = PROPERTY_USAGE_NOEDITOR | PROPERTY_USAGE_INTERNAL;
		}
	}

	static const char *hide_prefixes[] = {
		"fog_",
		"auto_exposure_",
		"ss_reflections_",
		"ssao_",
		"dof_blur_far_",
		"dof_blur_near_",
		"glow_",
		"adjustment_",
		nullptr

	};

	static const char *high_end_prefixes[] = {
		"auto_exposure_",
		"tonemap_",
		"ss_reflections_",
		"ssao_",
		nullptr

	};

	const char **prefixes = hide_prefixes;
	while (*prefixes) {
		String prefix = String(*prefixes);

		String enabled = prefix + "enabled";
		if (property.name.begins_with(prefix) && property.name != enabled && !bool(get(enabled))) {
			property.usage = PROPERTY_USAGE_NOEDITOR | PROPERTY_USAGE_INTERNAL;
			return;
		}

		prefixes++;
	}

	if (VisualServer::get_singleton()->is_low_end()) {
		prefixes = high_end_prefixes;
		while (*prefixes) {
			String prefix = String(*prefixes);

			if (property.name.begins_with(prefix)) {
				property.usage = PROPERTY_USAGE_NOEDITOR | PROPERTY_USAGE_INTERNAL;
				return;
			}

			prefixes++;
		}
	}
}

void Environment::set_ssr_enabled(bool p_enable) {
	ssr_enabled = p_enable;
	VS::get_singleton()->environment_set_ssr(environment, ssr_enabled, ssr_max_steps, ssr_fade_in, ssr_fade_out, ssr_depth_tolerance, ssr_roughness);
	_change_notify();
}

bool Environment::is_ssr_enabled() const {
	return ssr_enabled;
}

void Environment::set_ssr_max_steps(int p_steps) {
	ssr_max_steps = p_steps;
	VS::get_singleton()->environment_set_ssr(environment, ssr_enabled, ssr_max_steps, ssr_fade_in, ssr_fade_out, ssr_depth_tolerance, ssr_roughness);
}
int Environment::get_ssr_max_steps() const {
	return ssr_max_steps;
}

void Environment::set_ssr_fade_in(float p_fade_in) {
	ssr_fade_in = p_fade_in;
	VS::get_singleton()->environment_set_ssr(environment, ssr_enabled, ssr_max_steps, ssr_fade_in, ssr_fade_out, ssr_depth_tolerance, ssr_roughness);
}
float Environment::get_ssr_fade_in() const {
	return ssr_fade_in;
}

void Environment::set_ssr_fade_out(float p_fade_out) {
	ssr_fade_out = p_fade_out;
	VS::get_singleton()->environment_set_ssr(environment, ssr_enabled, ssr_max_steps, ssr_fade_in, ssr_fade_out, ssr_depth_tolerance, ssr_roughness);
}
float Environment::get_ssr_fade_out() const {
	return ssr_fade_out;
}

void Environment::set_ssr_depth_tolerance(float p_depth_tolerance) {
	ssr_depth_tolerance = p_depth_tolerance;
	VS::get_singleton()->environment_set_ssr(environment, ssr_enabled, ssr_max_steps, ssr_fade_in, ssr_fade_out, ssr_depth_tolerance, ssr_roughness);
}
float Environment::get_ssr_depth_tolerance() const {
	return ssr_depth_tolerance;
}

void Environment::set_ssr_rough(bool p_enable) {
	ssr_roughness = p_enable;
	VS::get_singleton()->environment_set_ssr(environment, ssr_enabled, ssr_max_steps, ssr_fade_in, ssr_fade_out, ssr_depth_tolerance, ssr_roughness);
}
bool Environment::is_ssr_rough() const {
	return ssr_roughness;
}

void Environment::set_ssao_enabled(bool p_enable) {
	ssao_enabled = p_enable;
	VS::get_singleton()->environment_set_ssao(environment, ssao_enabled, ssao_radius, ssao_intensity, ssao_radius2, ssao_intensity2, ssao_bias, ssao_direct_light_affect, ssao_ao_channel_affect, ssao_color, VS::EnvironmentSSAOQuality(ssao_quality), VS::EnvironmentSSAOBlur(ssao_blur), ssao_edge_sharpness);
	_change_notify();
}

bool Environment::is_ssao_enabled() const {
	return ssao_enabled;
}

void Environment::set_ssao_radius(float p_radius) {
	ssao_radius = p_radius;
	VS::get_singleton()->environment_set_ssao(environment, ssao_enabled, ssao_radius, ssao_intensity, ssao_radius2, ssao_intensity2, ssao_bias, ssao_direct_light_affect, ssao_ao_channel_affect, ssao_color, VS::EnvironmentSSAOQuality(ssao_quality), VS::EnvironmentSSAOBlur(ssao_blur), ssao_edge_sharpness);
}
float Environment::get_ssao_radius() const {
	return ssao_radius;
}

void Environment::set_ssao_intensity(float p_intensity) {
	ssao_intensity = p_intensity;
	VS::get_singleton()->environment_set_ssao(environment, ssao_enabled, ssao_radius, ssao_intensity, ssao_radius2, ssao_intensity2, ssao_bias, ssao_direct_light_affect, ssao_ao_channel_affect, ssao_color, VS::EnvironmentSSAOQuality(ssao_quality), VS::EnvironmentSSAOBlur(ssao_blur), ssao_edge_sharpness);
}

float Environment::get_ssao_intensity() const {
	return ssao_intensity;
}

void Environment::set_ssao_radius2(float p_radius) {
	ssao_radius2 = p_radius;
	VS::get_singleton()->environment_set_ssao(environment, ssao_enabled, ssao_radius, ssao_intensity, ssao_radius2, ssao_intensity2, ssao_bias, ssao_direct_light_affect, ssao_ao_channel_affect, ssao_color, VS::EnvironmentSSAOQuality(ssao_quality), VS::EnvironmentSSAOBlur(ssao_blur), ssao_edge_sharpness);
}
float Environment::get_ssao_radius2() const {
	return ssao_radius2;
}

void Environment::set_ssao_intensity2(float p_intensity) {
	ssao_intensity2 = p_intensity;
	VS::get_singleton()->environment_set_ssao(environment, ssao_enabled, ssao_radius, ssao_intensity, ssao_radius2, ssao_intensity2, ssao_bias, ssao_direct_light_affect, ssao_ao_channel_affect, ssao_color, VS::EnvironmentSSAOQuality(ssao_quality), VS::EnvironmentSSAOBlur(ssao_blur), ssao_edge_sharpness);
}
float Environment::get_ssao_intensity2() const {
	return ssao_intensity2;
}

void Environment::set_ssao_bias(float p_bias) {
	ssao_bias = p_bias;
	VS::get_singleton()->environment_set_ssao(environment, ssao_enabled, ssao_radius, ssao_intensity, ssao_radius2, ssao_intensity2, ssao_bias, ssao_direct_light_affect, ssao_ao_channel_affect, ssao_color, VS::EnvironmentSSAOQuality(ssao_quality), VS::EnvironmentSSAOBlur(ssao_blur), ssao_edge_sharpness);
}
float Environment::get_ssao_bias() const {
	return ssao_bias;
}

void Environment::set_ssao_direct_light_affect(float p_direct_light_affect) {
	ssao_direct_light_affect = p_direct_light_affect;
	VS::get_singleton()->environment_set_ssao(environment, ssao_enabled, ssao_radius, ssao_intensity, ssao_radius2, ssao_intensity2, ssao_bias, ssao_direct_light_affect, ssao_ao_channel_affect, ssao_color, VS::EnvironmentSSAOQuality(ssao_quality), VS::EnvironmentSSAOBlur(ssao_blur), ssao_edge_sharpness);
}
float Environment::get_ssao_direct_light_affect() const {
	return ssao_direct_light_affect;
}

void Environment::set_ssao_ao_channel_affect(float p_ao_channel_affect) {
	ssao_ao_channel_affect = p_ao_channel_affect;
	VS::get_singleton()->environment_set_ssao(environment, ssao_enabled, ssao_radius, ssao_intensity, ssao_radius2, ssao_intensity2, ssao_bias, ssao_direct_light_affect, ssao_ao_channel_affect, ssao_color, VS::EnvironmentSSAOQuality(ssao_quality), VS::EnvironmentSSAOBlur(ssao_blur), ssao_edge_sharpness);
}
float Environment::get_ssao_ao_channel_affect() const {
	return ssao_ao_channel_affect;
}

void Environment::set_ssao_color(const Color &p_color) {
	ssao_color = p_color;
	VS::get_singleton()->environment_set_ssao(environment, ssao_enabled, ssao_radius, ssao_intensity, ssao_radius2, ssao_intensity2, ssao_bias, ssao_direct_light_affect, ssao_ao_channel_affect, ssao_color, VS::EnvironmentSSAOQuality(ssao_quality), VS::EnvironmentSSAOBlur(ssao_blur), ssao_edge_sharpness);
}

Color Environment::get_ssao_color() const {
	return ssao_color;
}

void Environment::set_ssao_blur(SSAOBlur p_blur) {
	ssao_blur = p_blur;
	VS::get_singleton()->environment_set_ssao(environment, ssao_enabled, ssao_radius, ssao_intensity, ssao_radius2, ssao_intensity2, ssao_bias, ssao_direct_light_affect, ssao_ao_channel_affect, ssao_color, VS::EnvironmentSSAOQuality(ssao_quality), VS::EnvironmentSSAOBlur(ssao_blur), ssao_edge_sharpness);
}
Environment::SSAOBlur Environment::get_ssao_blur() const {
	return ssao_blur;
}

void Environment::set_ssao_quality(SSAOQuality p_quality) {
	ssao_quality = p_quality;
	VS::get_singleton()->environment_set_ssao(environment, ssao_enabled, ssao_radius, ssao_intensity, ssao_radius2, ssao_intensity2, ssao_bias, ssao_direct_light_affect, ssao_ao_channel_affect, ssao_color, VS::EnvironmentSSAOQuality(ssao_quality), VS::EnvironmentSSAOBlur(ssao_blur), ssao_edge_sharpness);
}

Environment::SSAOQuality Environment::get_ssao_quality() const {
	return ssao_quality;
}

void Environment::set_ssao_edge_sharpness(float p_edge_sharpness) {
	ssao_edge_sharpness = p_edge_sharpness;
	VS::get_singleton()->environment_set_ssao(environment, ssao_enabled, ssao_radius, ssao_intensity, ssao_radius2, ssao_intensity2, ssao_bias, ssao_direct_light_affect, ssao_ao_channel_affect, ssao_color, VS::EnvironmentSSAOQuality(ssao_quality), VS::EnvironmentSSAOBlur(ssao_blur), ssao_edge_sharpness);
}

float Environment::get_ssao_edge_sharpness() const {
	return ssao_edge_sharpness;
}

void Environment::set_glow_enabled(bool p_enabled) {
	glow_enabled = p_enabled;
	VS::get_singleton()->environment_set_glow(environment, glow_enabled, glow_levels, glow_intensity, glow_strength, glow_bloom, VS::EnvironmentGlowBlendMode(glow_blend_mode), glow_hdr_bleed_threshold, glow_hdr_bleed_scale, glow_hdr_luminance_cap, glow_bicubic_upscale, glow_high_quality);
	_change_notify();
}

bool Environment::is_glow_enabled() const {
	return glow_enabled;
}

void Environment::set_glow_level(int p_level, bool p_enabled) {
	ERR_FAIL_INDEX(p_level, VS::MAX_GLOW_LEVELS);

	if (p_enabled) {
		glow_levels |= (1 << p_level);
	} else {
		glow_levels &= ~(1 << p_level);
	}

	VS::get_singleton()->environment_set_glow(environment, glow_enabled, glow_levels, glow_intensity, glow_strength, glow_bloom, VS::EnvironmentGlowBlendMode(glow_blend_mode), glow_hdr_bleed_threshold, glow_hdr_bleed_scale, glow_hdr_luminance_cap, glow_bicubic_upscale, glow_high_quality);
}
bool Environment::is_glow_level_enabled(int p_level) const {
	ERR_FAIL_INDEX_V(p_level, VS::MAX_GLOW_LEVELS, false);

	return glow_levels & (1 << p_level);
}

void Environment::set_glow_intensity(float p_intensity) {
	glow_intensity = p_intensity;

	VS::get_singleton()->environment_set_glow(environment, glow_enabled, glow_levels, glow_intensity, glow_strength, glow_bloom, VS::EnvironmentGlowBlendMode(glow_blend_mode), glow_hdr_bleed_threshold, glow_hdr_bleed_scale, glow_hdr_luminance_cap, glow_bicubic_upscale, glow_high_quality);
}
float Environment::get_glow_intensity() const {
	return glow_intensity;
}

void Environment::set_glow_strength(float p_strength) {
	glow_strength = p_strength;
	VS::get_singleton()->environment_set_glow(environment, glow_enabled, glow_levels, glow_intensity, glow_strength, glow_bloom, VS::EnvironmentGlowBlendMode(glow_blend_mode), glow_hdr_bleed_threshold, glow_hdr_bleed_scale, glow_hdr_luminance_cap, glow_bicubic_upscale, glow_high_quality);
}
float Environment::get_glow_strength() const {
	return glow_strength;
}

void Environment::set_glow_bloom(float p_threshold) {
	glow_bloom = p_threshold;

	VS::get_singleton()->environment_set_glow(environment, glow_enabled, glow_levels, glow_intensity, glow_strength, glow_bloom, VS::EnvironmentGlowBlendMode(glow_blend_mode), glow_hdr_bleed_threshold, glow_hdr_bleed_scale, glow_hdr_luminance_cap, glow_bicubic_upscale, glow_high_quality);
}
float Environment::get_glow_bloom() const {
	return glow_bloom;
}

void Environment::set_glow_blend_mode(GlowBlendMode p_mode) {
	glow_blend_mode = p_mode;

	VS::get_singleton()->environment_set_glow(environment, glow_enabled, glow_levels, glow_intensity, glow_strength, glow_bloom, VS::EnvironmentGlowBlendMode(glow_blend_mode), glow_hdr_bleed_threshold, glow_hdr_bleed_scale, glow_hdr_luminance_cap, glow_bicubic_upscale, glow_high_quality);
}
Environment::GlowBlendMode Environment::get_glow_blend_mode() const {
	return glow_blend_mode;
}

void Environment::set_glow_hdr_bleed_threshold(float p_threshold) {
	glow_hdr_bleed_threshold = p_threshold;

	VS::get_singleton()->environment_set_glow(environment, glow_enabled, glow_levels, glow_intensity, glow_strength, glow_bloom, VS::EnvironmentGlowBlendMode(glow_blend_mode), glow_hdr_bleed_threshold, glow_hdr_bleed_scale, glow_hdr_luminance_cap, glow_bicubic_upscale, glow_high_quality);
}
float Environment::get_glow_hdr_bleed_threshold() const {
	return glow_hdr_bleed_threshold;
}

void Environment::set_glow_hdr_luminance_cap(float p_amount) {
	glow_hdr_luminance_cap = p_amount;

	VS::get_singleton()->environment_set_glow(environment, glow_enabled, glow_levels, glow_intensity, glow_strength, glow_bloom, VS::EnvironmentGlowBlendMode(glow_blend_mode), glow_hdr_bleed_threshold, glow_hdr_bleed_scale, glow_hdr_luminance_cap, glow_bicubic_upscale, glow_high_quality);
}
float Environment::get_glow_hdr_luminance_cap() const {
	return glow_hdr_luminance_cap;
}

void Environment::set_glow_hdr_bleed_scale(float p_scale) {
	glow_hdr_bleed_scale = p_scale;

	VS::get_singleton()->environment_set_glow(environment, glow_enabled, glow_levels, glow_intensity, glow_strength, glow_bloom, VS::EnvironmentGlowBlendMode(glow_blend_mode), glow_hdr_bleed_threshold, glow_hdr_bleed_scale, glow_hdr_luminance_cap, glow_bicubic_upscale, glow_high_quality);
}
float Environment::get_glow_hdr_bleed_scale() const {
	return glow_hdr_bleed_scale;
}

void Environment::set_glow_bicubic_upscale(bool p_enable) {
	glow_bicubic_upscale = p_enable;
	VS::get_singleton()->environment_set_glow(environment, glow_enabled, glow_levels, glow_intensity, glow_strength, glow_bloom, VS::EnvironmentGlowBlendMode(glow_blend_mode), glow_hdr_bleed_threshold, glow_hdr_bleed_scale, glow_hdr_luminance_cap, glow_bicubic_upscale, glow_high_quality);
}

bool Environment::is_glow_bicubic_upscale_enabled() const {
	return glow_bicubic_upscale;
}

void Environment::set_glow_high_quality(bool p_enable) {
	glow_high_quality = p_enable;
	VS::get_singleton()->environment_set_glow(environment, glow_enabled, glow_levels, glow_intensity, glow_strength, glow_bloom, VS::EnvironmentGlowBlendMode(glow_blend_mode), glow_hdr_bleed_threshold, glow_hdr_bleed_scale, glow_hdr_luminance_cap, glow_bicubic_upscale, glow_high_quality);
}

bool Environment::is_glow_high_quality_enabled() const {
	return glow_high_quality;
}

void Environment::set_dof_blur_far_enabled(bool p_enable) {
	dof_blur_far_enabled = p_enable;
	VS::get_singleton()->environment_set_dof_blur_far(environment, dof_blur_far_enabled, dof_blur_far_distance, dof_blur_far_transition, dof_blur_far_amount, VS::EnvironmentDOFBlurQuality(dof_blur_far_quality));
	_change_notify();
}

bool Environment::is_dof_blur_far_enabled() const {
	return dof_blur_far_enabled;
}

void Environment::set_dof_blur_far_distance(float p_distance) {
	dof_blur_far_distance = p_distance;
	VS::get_singleton()->environment_set_dof_blur_far(environment, dof_blur_far_enabled, dof_blur_far_distance, dof_blur_far_transition, dof_blur_far_amount, VS::EnvironmentDOFBlurQuality(dof_blur_far_quality));
}
float Environment::get_dof_blur_far_distance() const {
	return dof_blur_far_distance;
}

void Environment::set_dof_blur_far_transition(float p_distance) {
	dof_blur_far_transition = p_distance;
	VS::get_singleton()->environment_set_dof_blur_far(environment, dof_blur_far_enabled, dof_blur_far_distance, dof_blur_far_transition, dof_blur_far_amount, VS::EnvironmentDOFBlurQuality(dof_blur_far_quality));
}
float Environment::get_dof_blur_far_transition() const {
	return dof_blur_far_transition;
}

void Environment::set_dof_blur_far_amount(float p_amount) {
	dof_blur_far_amount = p_amount;
	VS::get_singleton()->environment_set_dof_blur_far(environment, dof_blur_far_enabled, dof_blur_far_distance, dof_blur_far_transition, dof_blur_far_amount, VS::EnvironmentDOFBlurQuality(dof_blur_far_quality));
}
float Environment::get_dof_blur_far_amount() const {
	return dof_blur_far_amount;
}

void Environment::set_dof_blur_far_quality(DOFBlurQuality p_quality) {
	dof_blur_far_quality = p_quality;
	VS::get_singleton()->environment_set_dof_blur_far(environment, dof_blur_far_enabled, dof_blur_far_distance, dof_blur_far_transition, dof_blur_far_amount, VS::EnvironmentDOFBlurQuality(dof_blur_far_quality));
}

Environment::DOFBlurQuality Environment::get_dof_blur_far_quality() const {
	return dof_blur_far_quality;
}

void Environment::set_dof_blur_near_enabled(bool p_enable) {
	dof_blur_near_enabled = p_enable;
	VS::get_singleton()->environment_set_dof_blur_near(environment, dof_blur_near_enabled, dof_blur_near_distance, dof_blur_near_transition, dof_blur_near_amount, VS::EnvironmentDOFBlurQuality(dof_blur_near_quality));
	_change_notify();
}

bool Environment::is_dof_blur_near_enabled() const {
	return dof_blur_near_enabled;
}

void Environment::set_dof_blur_near_distance(float p_distance) {
	dof_blur_near_distance = p_distance;
	VS::get_singleton()->environment_set_dof_blur_near(environment, dof_blur_near_enabled, dof_blur_near_distance, dof_blur_near_transition, dof_blur_near_amount, VS::EnvironmentDOFBlurQuality(dof_blur_near_quality));
}

float Environment::get_dof_blur_near_distance() const {
	return dof_blur_near_distance;
}

void Environment::set_dof_blur_near_transition(float p_distance) {
	dof_blur_near_transition = p_distance;
	VS::get_singleton()->environment_set_dof_blur_near(environment, dof_blur_near_enabled, dof_blur_near_distance, dof_blur_near_transition, dof_blur_near_amount, VS::EnvironmentDOFBlurQuality(dof_blur_near_quality));
}

float Environment::get_dof_blur_near_transition() const {
	return dof_blur_near_transition;
}

void Environment::set_dof_blur_near_amount(float p_amount) {
	dof_blur_near_amount = p_amount;
	VS::get_singleton()->environment_set_dof_blur_near(environment, dof_blur_near_enabled, dof_blur_near_distance, dof_blur_near_transition, dof_blur_near_amount, VS::EnvironmentDOFBlurQuality(dof_blur_near_quality));
}

float Environment::get_dof_blur_near_amount() const {
	return dof_blur_near_amount;
}

void Environment::set_dof_blur_near_quality(DOFBlurQuality p_quality) {
	dof_blur_near_quality = p_quality;
	VS::get_singleton()->environment_set_dof_blur_near(environment, dof_blur_near_enabled, dof_blur_near_distance, dof_blur_near_transition, dof_blur_near_amount, VS::EnvironmentDOFBlurQuality(dof_blur_near_quality));
}

Environment::DOFBlurQuality Environment::get_dof_blur_near_quality() const {
	return dof_blur_near_quality;
}

void Environment::set_fog_enabled(bool p_enabled) {
	fog_enabled = p_enabled;
	VS::get_singleton()->environment_set_fog(environment, fog_enabled, fog_color, fog_sun_color, fog_sun_amount);
	_change_notify();
}

bool Environment::is_fog_enabled() const {
	return fog_enabled;
}

void Environment::set_fog_color(const Color &p_color) {
	fog_color = p_color;
	VS::get_singleton()->environment_set_fog(environment, fog_enabled, fog_color, fog_sun_color, fog_sun_amount);
}
Color Environment::get_fog_color() const {
	return fog_color;
}

void Environment::set_fog_sun_color(const Color &p_color) {
	fog_sun_color = p_color;
	VS::get_singleton()->environment_set_fog(environment, fog_enabled, fog_color, fog_sun_color, fog_sun_amount);
}
Color Environment::get_fog_sun_color() const {
	return fog_sun_color;
}

void Environment::set_fog_sun_amount(float p_amount) {
	fog_sun_amount = p_amount;
	VS::get_singleton()->environment_set_fog(environment, fog_enabled, fog_color, fog_sun_color, fog_sun_amount);
}
float Environment::get_fog_sun_amount() const {
	return fog_sun_amount;
}

void Environment::set_fog_depth_enabled(bool p_enabled) {
	fog_depth_enabled = p_enabled;
	VS::get_singleton()->environment_set_fog_depth(environment, fog_depth_enabled, fog_depth_begin, fog_depth_end, fog_depth_curve, fog_transmit_enabled, fog_transmit_curve);
}
bool Environment::is_fog_depth_enabled() const {
	return fog_depth_enabled;
}

void Environment::set_fog_depth_begin(float p_distance) {
	fog_depth_begin = p_distance;
	VS::get_singleton()->environment_set_fog_depth(environment, fog_depth_enabled, fog_depth_begin, fog_depth_end, fog_depth_curve, fog_transmit_enabled, fog_transmit_curve);
}
float Environment::get_fog_depth_begin() const {
	return fog_depth_begin;
}

void Environment::set_fog_depth_end(float p_distance) {
	fog_depth_end = p_distance;
	VS::get_singleton()->environment_set_fog_depth(environment, fog_depth_enabled, fog_depth_begin, fog_depth_end, fog_depth_curve, fog_transmit_enabled, fog_transmit_curve);
}

float Environment::get_fog_depth_end() const {
	return fog_depth_end;
}

void Environment::set_fog_depth_curve(float p_curve) {
	fog_depth_curve = p_curve;
	VS::get_singleton()->environment_set_fog_depth(environment, fog_depth_enabled, fog_depth_begin, fog_depth_end, fog_depth_curve, fog_transmit_enabled, fog_transmit_curve);
}
float Environment::get_fog_depth_curve() const {
	return fog_depth_curve;
}

void Environment::set_fog_transmit_enabled(bool p_enabled) {
	fog_transmit_enabled = p_enabled;
	VS::get_singleton()->environment_set_fog_depth(environment, fog_depth_enabled, fog_depth_begin, fog_depth_end, fog_depth_curve, fog_transmit_enabled, fog_transmit_curve);
}
bool Environment::is_fog_transmit_enabled() const {
	return fog_transmit_enabled;
}

void Environment::set_fog_transmit_curve(float p_curve) {
	fog_transmit_curve = p_curve;
	VS::get_singleton()->environment_set_fog_depth(environment, fog_depth_enabled, fog_depth_begin, fog_depth_end, fog_depth_curve, fog_transmit_enabled, fog_transmit_curve);
}
float Environment::get_fog_transmit_curve() const {
	return fog_transmit_curve;
}

void Environment::set_fog_height_enabled(bool p_enabled) {
	fog_height_enabled = p_enabled;
	VS::get_singleton()->environment_set_fog_height(environment, fog_height_enabled, fog_height_min, fog_height_max, fog_height_curve);
}
bool Environment::is_fog_height_enabled() const {
	return fog_height_enabled;
}

void Environment::set_fog_height_min(float p_distance) {
	fog_height_min = p_distance;
	VS::get_singleton()->environment_set_fog_height(environment, fog_height_enabled, fog_height_min, fog_height_max, fog_height_curve);
}
float Environment::get_fog_height_min() const {
	return fog_height_min;
}

void Environment::set_fog_height_max(float p_distance) {
	fog_height_max = p_distance;
	VS::get_singleton()->environment_set_fog_height(environment, fog_height_enabled, fog_height_min, fog_height_max, fog_height_curve);
}
float Environment::get_fog_height_max() const {
	return fog_height_max;
}

void Environment::set_fog_height_curve(float p_distance) {
	fog_height_curve = p_distance;
	VS::get_singleton()->environment_set_fog_height(environment, fog_height_enabled, fog_height_min, fog_height_max, fog_height_curve);
}
float Environment::get_fog_height_curve() const {
	return fog_height_curve;
}

void Environment::_update_volumetric_fog() {
	VS::get_singleton()->environment_set_volumetric_fog(environment, volumetric_fog_enabled, volumetric_fog_density, volumetric_fog_height_falloff, volumetric_fog_height_ref, volumetric_fog_albedo, volumetric_fog_ambient, volumetric_fog_sun_energy, volumetric_fog_anisotropy, volumetric_fog_length, volumetric_fog_extinction, volumetric_fog_samples, volumetric_fog_temporal_blend);
}

void Environment::set_volumetric_fog_enabled(bool p_enabled) {
	volumetric_fog_enabled = p_enabled;
	_update_volumetric_fog();
	_change_notify();
}
bool Environment::is_volumetric_fog_enabled() const {
	return volumetric_fog_enabled;
}

void Environment::set_volumetric_fog_density(float p_density) {
	volumetric_fog_density = p_density;
	_update_volumetric_fog();
}
float Environment::get_volumetric_fog_density() const {
	return volumetric_fog_density;
}

void Environment::set_volumetric_fog_height_falloff(float p_falloff) {
	volumetric_fog_height_falloff = p_falloff;
	_update_volumetric_fog();
}
float Environment::get_volumetric_fog_height_falloff() const {
	return volumetric_fog_height_falloff;
}

void Environment::set_volumetric_fog_height_ref(float p_ref) {
	volumetric_fog_height_ref = p_ref;
	_update_volumetric_fog();
}
float Environment::get_volumetric_fog_height_ref() const {
	return volumetric_fog_height_ref;
}

void Environment::set_volumetric_fog_height_relative(bool p_relative) {
	volumetric_fog_height_relative = p_relative;
	VS::get_singleton()->environment_set_volumetric_fog_height_relative(environment, volumetric_fog_height_relative);
}
bool Environment::is_volumetric_fog_height_relative() const {
	return volumetric_fog_height_relative;
}

void Environment::set_volumetric_fog_albedo(const Color &p_albedo) {
	volumetric_fog_albedo = p_albedo;
	_update_volumetric_fog();
}
Color Environment::get_volumetric_fog_albedo() const {
	return volumetric_fog_albedo;
}

void Environment::set_volumetric_fog_ambient(const Color &p_ambient) {
	volumetric_fog_ambient = p_ambient;
	_update_volumetric_fog();
}
Color Environment::get_volumetric_fog_ambient() const {
	return volumetric_fog_ambient;
}

void Environment::set_volumetric_fog_sun_energy(float p_energy) {
	volumetric_fog_sun_energy = p_energy;
	_update_volumetric_fog();
}
float Environment::get_volumetric_fog_sun_energy() const {
	return volumetric_fog_sun_energy;
}

void Environment::set_volumetric_fog_anisotropy(float p_anisotropy) {
	volumetric_fog_anisotropy = CLAMP(p_anisotropy, -0.95, 0.95);
	_update_volumetric_fog();
}
float Environment::get_volumetric_fog_anisotropy() const {
	return volumetric_fog_anisotropy;
}

void Environment::set_volumetric_fog_length(float p_length) {
	volumetric_fog_length = MAX(p_length, 0.0);
	_update_volumetric_fog();
}
float Environment::get_volumetric_fog_length() const {
	return volumetric_fog_length;
}

void Environment::set_volumetric_fog_extinction(float p_extinction) {
	volumetric_fog_extinction = MAX(p_extinction, 0.0);
	_update_volumetric_fog();
}
float Environment::get_volumetric_fog_extinction() const {
	return volumetric_fog_extinction;
}

void Environment::set_volumetric_fog_samples(int p_samples) {
	volumetric_fog_samples = CLAMP(p_samples, 8, 512);
	_update_volumetric_fog();
}
int Environment::get_volumetric_fog_samples() const {
	return volumetric_fog_samples;
}

void Environment::set_volumetric_fog_temporal_blend(float p_blend) {
	volumetric_fog_temporal_blend = CLAMP(p_blend, 0.0, 0.98);
	_update_volumetric_fog();
}
float Environment::get_volumetric_fog_temporal_blend() const {
	return volumetric_fog_temporal_blend;
}

void Environment::_update_dynamic_gi() {
	VS::get_singleton()->environment_set_dynamic_gi(environment, dynamic_gi_enabled, dynamic_gi_cascades, dynamic_gi_voxel_size, dynamic_gi_energy, dynamic_gi_bounce_feedback, dynamic_gi_normal_bias, dynamic_gi_hysteresis, dynamic_gi_rays, dynamic_gi_reflection_occlusion, dynamic_gi_cull_mask, VS::EnvironmentDynamicGIDebug(dynamic_gi_debug));
}

void Environment::_update_ssgi() {
	VS::get_singleton()->environment_set_ssgi(environment, ssgi_enabled, ssgi_radius, ssgi_intensity, ssgi_rays, ssgi_steps, ssgi_thickness, ssgi_temporal_blend, ssgi_half_resolution);
}

void Environment::set_dynamic_gi_enabled(bool p_value) {
	dynamic_gi_enabled = p_value;
	_update_dynamic_gi();
	_change_notify();
}
bool Environment::is_dynamic_gi_enabled() const {
	return dynamic_gi_enabled;
}

void Environment::set_dynamic_gi_cascades(int p_value) {
	dynamic_gi_cascades = CLAMP(p_value, 1, 6);
	_update_dynamic_gi();
}
int Environment::get_dynamic_gi_cascades() const {
	return dynamic_gi_cascades;
}

void Environment::set_dynamic_gi_voxel_size(float p_value) {
	dynamic_gi_voxel_size = MAX(p_value, 0.01f);
	_update_dynamic_gi();
}
float Environment::get_dynamic_gi_voxel_size() const {
	return dynamic_gi_voxel_size;
}

void Environment::set_dynamic_gi_energy(float p_value) {
	dynamic_gi_energy = p_value;
	_update_dynamic_gi();
}
float Environment::get_dynamic_gi_energy() const {
	return dynamic_gi_energy;
}

void Environment::set_dynamic_gi_bounce_feedback(float p_value) {
	dynamic_gi_bounce_feedback = p_value;
	_update_dynamic_gi();
}
float Environment::get_dynamic_gi_bounce_feedback() const {
	return dynamic_gi_bounce_feedback;
}

void Environment::set_dynamic_gi_normal_bias(float p_value) {
	dynamic_gi_normal_bias = p_value;
	_update_dynamic_gi();
}
float Environment::get_dynamic_gi_normal_bias() const {
	return dynamic_gi_normal_bias;
}

void Environment::set_dynamic_gi_hysteresis(float p_value) {
	dynamic_gi_hysteresis = CLAMP(p_value, 0.0f, 0.99f);
	_update_dynamic_gi();
}
float Environment::get_dynamic_gi_hysteresis() const {
	return dynamic_gi_hysteresis;
}

void Environment::set_dynamic_gi_rays(int p_value) {
	dynamic_gi_rays = CLAMP(p_value / 64, 1, 3) * 64;
	_update_dynamic_gi();
}
int Environment::get_dynamic_gi_rays() const {
	return dynamic_gi_rays;
}

void Environment::set_dynamic_gi_reflection_occlusion(bool p_value) {
	dynamic_gi_reflection_occlusion = p_value;
	_update_dynamic_gi();
}
bool Environment::is_dynamic_gi_reflection_occlusion() const {
	return dynamic_gi_reflection_occlusion;
}

void Environment::set_dynamic_gi_cull_mask(uint32_t p_value) {
	dynamic_gi_cull_mask = p_value;
	_update_dynamic_gi();
}
uint32_t Environment::get_dynamic_gi_cull_mask() const {
	return dynamic_gi_cull_mask;
}

void Environment::set_dynamic_gi_debug(DynamicGIDebug p_value) {
	dynamic_gi_debug = p_value;
	_update_dynamic_gi();
}
Environment::DynamicGIDebug Environment::get_dynamic_gi_debug() const {
	return dynamic_gi_debug;
}

void Environment::set_ssgi_enabled(bool p_value) {
	ssgi_enabled = p_value;
	_update_ssgi();
	_change_notify();
}
bool Environment::is_ssgi_enabled() const {
	return ssgi_enabled;
}

void Environment::set_ssgi_radius(float p_value) {
	ssgi_radius = p_value;
	_update_ssgi();
}
float Environment::get_ssgi_radius() const {
	return ssgi_radius;
}

void Environment::set_ssgi_intensity(float p_value) {
	ssgi_intensity = p_value;
	_update_ssgi();
}
float Environment::get_ssgi_intensity() const {
	return ssgi_intensity;
}

void Environment::set_ssgi_rays(int p_value) {
	ssgi_rays = CLAMP(p_value, 1, 16);
	_update_ssgi();
}
int Environment::get_ssgi_rays() const {
	return ssgi_rays;
}

void Environment::set_ssgi_steps(int p_value) {
	ssgi_steps = CLAMP(p_value, 4, 64);
	_update_ssgi();
}
int Environment::get_ssgi_steps() const {
	return ssgi_steps;
}

void Environment::set_ssgi_thickness(float p_value) {
	ssgi_thickness = p_value;
	_update_ssgi();
}
float Environment::get_ssgi_thickness() const {
	return ssgi_thickness;
}

void Environment::set_ssgi_temporal_blend(float p_value) {
	ssgi_temporal_blend = CLAMP(p_value, 0.0f, 0.99f);
	_update_ssgi();
}
float Environment::get_ssgi_temporal_blend() const {
	return ssgi_temporal_blend;
}

void Environment::set_ssgi_half_resolution(bool p_value) {
	ssgi_half_resolution = p_value;
	_update_ssgi();
}
bool Environment::is_ssgi_half_resolution() const {
	return ssgi_half_resolution;
}

void Environment::_update_exp_height_fog() {
	VS::get_singleton()->environment_set_exp_height_fog(environment, exp_height_fog_enabled, exp_height_fog_density, exp_height_fog_falloff, exp_height_fog_ref_height, exp_height_fog_ref_height_relative, exp_height_fog_start_distance, exp_height_fog_max_opacity, exp_height_fog_inscatter, exp_height_fog_directional_enabled, exp_height_fog_directional_color, exp_height_fog_light_direction, exp_height_fog_directional_exponent);
}

void Environment::set_exp_height_fog_enabled(bool p_enabled) {
	exp_height_fog_enabled = p_enabled;
	_update_exp_height_fog();
	_change_notify();
}
bool Environment::is_exp_height_fog_enabled() const {
	return exp_height_fog_enabled;
}

void Environment::set_exp_height_fog_density(float p_density) {
	exp_height_fog_density = p_density;
	_update_exp_height_fog();
}
float Environment::get_exp_height_fog_density() const {
	return exp_height_fog_density;
}

void Environment::set_exp_height_fog_falloff(float p_falloff) {
	exp_height_fog_falloff = p_falloff;
	_update_exp_height_fog();
}
float Environment::get_exp_height_fog_falloff() const {
	return exp_height_fog_falloff;
}

void Environment::set_exp_height_fog_ref_height(float p_ref_height) {
	exp_height_fog_ref_height = p_ref_height;
	_update_exp_height_fog();
}
float Environment::get_exp_height_fog_ref_height() const {
	return exp_height_fog_ref_height;
}

void Environment::set_exp_height_fog_ref_height_relative(bool p_relative) {
	exp_height_fog_ref_height_relative = p_relative;
	_update_exp_height_fog();
}
bool Environment::is_exp_height_fog_ref_height_relative() const {
	return exp_height_fog_ref_height_relative;
}

void Environment::set_exp_height_fog_start_distance(float p_distance) {
	exp_height_fog_start_distance = p_distance;
	_update_exp_height_fog();
}
float Environment::get_exp_height_fog_start_distance() const {
	return exp_height_fog_start_distance;
}

void Environment::set_exp_height_fog_max_opacity(float p_opacity) {
	exp_height_fog_max_opacity = p_opacity;
	_update_exp_height_fog();
}
float Environment::get_exp_height_fog_max_opacity() const {
	return exp_height_fog_max_opacity;
}

void Environment::set_exp_height_fog_inscatter(const Color &p_color) {
	exp_height_fog_inscatter = p_color;
	_update_exp_height_fog();
}
Color Environment::get_exp_height_fog_inscatter() const {
	return exp_height_fog_inscatter;
}

void Environment::set_exp_height_fog_directional_enabled(bool p_enabled) {
	exp_height_fog_directional_enabled = p_enabled;
	_update_exp_height_fog();
}
bool Environment::is_exp_height_fog_directional_enabled() const {
	return exp_height_fog_directional_enabled;
}

void Environment::set_exp_height_fog_directional_color(const Color &p_color) {
	exp_height_fog_directional_color = p_color;
	_update_exp_height_fog();
}
Color Environment::get_exp_height_fog_directional_color() const {
	return exp_height_fog_directional_color;
}

void Environment::set_exp_height_fog_light_direction(const Vector3 &p_dir) {
	exp_height_fog_light_direction = p_dir;
	_update_exp_height_fog();
}
Vector3 Environment::get_exp_height_fog_light_direction() const {
	return exp_height_fog_light_direction;
}

void Environment::set_exp_height_fog_directional_exponent(float p_exponent) {
	exp_height_fog_directional_exponent = p_exponent;
	_update_exp_height_fog();
}
float Environment::get_exp_height_fog_directional_exponent() const {
	return exp_height_fog_directional_exponent;
}

void Environment::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_background", "mode"), &Environment::set_background);
	ClassDB::bind_method(D_METHOD("set_sky", "sky"), &Environment::set_sky);
	ClassDB::bind_method(D_METHOD("set_sky_custom_fov", "scale"), &Environment::set_sky_custom_fov);
	ClassDB::bind_method(D_METHOD("set_sky_orientation", "orientation"), &Environment::set_sky_orientation);
	ClassDB::bind_method(D_METHOD("set_sky_rotation", "euler_radians"), &Environment::set_sky_rotation);
	ClassDB::bind_method(D_METHOD("set_sky_rotation_degrees", "euler_degrees"), &Environment::set_sky_rotation_degrees);
	ClassDB::bind_method(D_METHOD("set_bg_color", "color"), &Environment::set_bg_color);
	ClassDB::bind_method(D_METHOD("set_bg_energy", "energy"), &Environment::set_bg_energy);
	ClassDB::bind_method(D_METHOD("set_canvas_max_layer", "layer"), &Environment::set_canvas_max_layer);
	ClassDB::bind_method(D_METHOD("set_ambient_light_color", "color"), &Environment::set_ambient_light_color);
	ClassDB::bind_method(D_METHOD("set_ambient_light_energy", "energy"), &Environment::set_ambient_light_energy);
	ClassDB::bind_method(D_METHOD("set_ambient_light_sky_contribution", "energy"), &Environment::set_ambient_light_sky_contribution);
	ClassDB::bind_method(D_METHOD("set_camera_feed_id", "camera_feed_id"), &Environment::set_camera_feed_id);

	ClassDB::bind_method(D_METHOD("get_background"), &Environment::get_background);
	ClassDB::bind_method(D_METHOD("get_sky"), &Environment::get_sky);
	ClassDB::bind_method(D_METHOD("get_sky_custom_fov"), &Environment::get_sky_custom_fov);
	ClassDB::bind_method(D_METHOD("get_sky_orientation"), &Environment::get_sky_orientation);
	ClassDB::bind_method(D_METHOD("get_sky_rotation"), &Environment::get_sky_rotation);
	ClassDB::bind_method(D_METHOD("get_sky_rotation_degrees"), &Environment::get_sky_rotation_degrees);
	ClassDB::bind_method(D_METHOD("get_bg_color"), &Environment::get_bg_color);
	ClassDB::bind_method(D_METHOD("get_bg_energy"), &Environment::get_bg_energy);
	ClassDB::bind_method(D_METHOD("get_canvas_max_layer"), &Environment::get_canvas_max_layer);
	ClassDB::bind_method(D_METHOD("get_ambient_light_color"), &Environment::get_ambient_light_color);
	ClassDB::bind_method(D_METHOD("get_ambient_light_energy"), &Environment::get_ambient_light_energy);
	ClassDB::bind_method(D_METHOD("get_ambient_light_sky_contribution"), &Environment::get_ambient_light_sky_contribution);
	ClassDB::bind_method(D_METHOD("get_camera_feed_id"), &Environment::get_camera_feed_id);

	ADD_GROUP("Background", "background_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "background_mode", PROPERTY_HINT_ENUM, "Clear Color,Custom Color,Sky,Color+Sky,Canvas,Keep,Camera Feed"), "set_background", "get_background");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "background_sky", PROPERTY_HINT_RESOURCE_TYPE, "Sky"), "set_sky", "get_sky");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "background_sky_custom_fov", PROPERTY_HINT_RANGE, "0,180,0.1"), "set_sky_custom_fov", "get_sky_custom_fov");
	ADD_PROPERTY(PropertyInfo(Variant::BASIS, "background_sky_orientation"), "set_sky_orientation", "get_sky_orientation");
	// Only display rotation in degrees in the inspector (like in Spatial).
	// This avoids displaying the same information twice.
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "background_sky_rotation", PROPERTY_HINT_NONE, "", 0), "set_sky_rotation", "get_sky_rotation");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "background_sky_rotation_degrees", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR), "set_sky_rotation_degrees", "get_sky_rotation_degrees");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "background_color"), "set_bg_color", "get_bg_color");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "background_energy", PROPERTY_HINT_RANGE, "0,16,0.01"), "set_bg_energy", "get_bg_energy");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "background_canvas_max_layer", PROPERTY_HINT_RANGE, "-1000,1000,1"), "set_canvas_max_layer", "get_canvas_max_layer");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "background_camera_feed_id", PROPERTY_HINT_RANGE, "1,10,1"), "set_camera_feed_id", "get_camera_feed_id");
	ADD_GROUP("Ambient Light", "ambient_light_");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "ambient_light_color"), "set_ambient_light_color", "get_ambient_light_color");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ambient_light_energy", PROPERTY_HINT_RANGE, "0,16,0.01"), "set_ambient_light_energy", "get_ambient_light_energy");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ambient_light_sky_contribution", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_ambient_light_sky_contribution", "get_ambient_light_sky_contribution");

	ClassDB::bind_method(D_METHOD("set_fog_enabled", "enabled"), &Environment::set_fog_enabled);
	ClassDB::bind_method(D_METHOD("is_fog_enabled"), &Environment::is_fog_enabled);

	ClassDB::bind_method(D_METHOD("set_fog_color", "color"), &Environment::set_fog_color);
	ClassDB::bind_method(D_METHOD("get_fog_color"), &Environment::get_fog_color);

	ClassDB::bind_method(D_METHOD("set_fog_sun_color", "color"), &Environment::set_fog_sun_color);
	ClassDB::bind_method(D_METHOD("get_fog_sun_color"), &Environment::get_fog_sun_color);

	ClassDB::bind_method(D_METHOD("set_fog_sun_amount", "amount"), &Environment::set_fog_sun_amount);
	ClassDB::bind_method(D_METHOD("get_fog_sun_amount"), &Environment::get_fog_sun_amount);

	ClassDB::bind_method(D_METHOD("set_fog_depth_enabled", "enabled"), &Environment::set_fog_depth_enabled);
	ClassDB::bind_method(D_METHOD("is_fog_depth_enabled"), &Environment::is_fog_depth_enabled);

	ClassDB::bind_method(D_METHOD("set_fog_depth_begin", "distance"), &Environment::set_fog_depth_begin);
	ClassDB::bind_method(D_METHOD("get_fog_depth_begin"), &Environment::get_fog_depth_begin);

	ClassDB::bind_method(D_METHOD("set_fog_depth_end", "distance"), &Environment::set_fog_depth_end);
	ClassDB::bind_method(D_METHOD("get_fog_depth_end"), &Environment::get_fog_depth_end);

	ClassDB::bind_method(D_METHOD("set_fog_depth_curve", "curve"), &Environment::set_fog_depth_curve);
	ClassDB::bind_method(D_METHOD("get_fog_depth_curve"), &Environment::get_fog_depth_curve);

	ClassDB::bind_method(D_METHOD("set_fog_transmit_enabled", "enabled"), &Environment::set_fog_transmit_enabled);
	ClassDB::bind_method(D_METHOD("is_fog_transmit_enabled"), &Environment::is_fog_transmit_enabled);

	ClassDB::bind_method(D_METHOD("set_fog_transmit_curve", "curve"), &Environment::set_fog_transmit_curve);
	ClassDB::bind_method(D_METHOD("get_fog_transmit_curve"), &Environment::get_fog_transmit_curve);

	ClassDB::bind_method(D_METHOD("set_fog_height_enabled", "enabled"), &Environment::set_fog_height_enabled);
	ClassDB::bind_method(D_METHOD("is_fog_height_enabled"), &Environment::is_fog_height_enabled);

	ClassDB::bind_method(D_METHOD("set_fog_height_min", "height"), &Environment::set_fog_height_min);
	ClassDB::bind_method(D_METHOD("get_fog_height_min"), &Environment::get_fog_height_min);

	ClassDB::bind_method(D_METHOD("set_fog_height_max", "height"), &Environment::set_fog_height_max);
	ClassDB::bind_method(D_METHOD("get_fog_height_max"), &Environment::get_fog_height_max);

	ClassDB::bind_method(D_METHOD("set_fog_height_curve", "curve"), &Environment::set_fog_height_curve);
	ClassDB::bind_method(D_METHOD("get_fog_height_curve"), &Environment::get_fog_height_curve);

	ADD_GROUP("Fog", "fog_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fog_enabled"), "set_fog_enabled", "is_fog_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "fog_color"), "set_fog_color", "get_fog_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "fog_sun_color"), "set_fog_sun_color", "get_fog_sun_color");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "fog_sun_amount", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_fog_sun_amount", "get_fog_sun_amount");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fog_depth_enabled"), "set_fog_depth_enabled", "is_fog_depth_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "fog_depth_begin", PROPERTY_HINT_RANGE, "0,4000,0.1"), "set_fog_depth_begin", "get_fog_depth_begin");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "fog_depth_end", PROPERTY_HINT_RANGE, "0,4000,0.1,or_greater"), "set_fog_depth_end", "get_fog_depth_end");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "fog_depth_curve", PROPERTY_HINT_EXP_EASING), "set_fog_depth_curve", "get_fog_depth_curve");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fog_transmit_enabled"), "set_fog_transmit_enabled", "is_fog_transmit_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "fog_transmit_curve", PROPERTY_HINT_EXP_EASING), "set_fog_transmit_curve", "get_fog_transmit_curve");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fog_height_enabled"), "set_fog_height_enabled", "is_fog_height_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "fog_height_min", PROPERTY_HINT_RANGE, "-4000,4000,0.1,or_lesser,or_greater"), "set_fog_height_min", "get_fog_height_min");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "fog_height_max", PROPERTY_HINT_RANGE, "-4000,4000,0.1,or_lesser,or_greater"), "set_fog_height_max", "get_fog_height_max");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "fog_height_curve", PROPERTY_HINT_EXP_EASING), "set_fog_height_curve", "get_fog_height_curve");

	ClassDB::bind_method(D_METHOD("set_volumetric_fog_enabled", "enabled"), &Environment::set_volumetric_fog_enabled);
	ClassDB::bind_method(D_METHOD("is_volumetric_fog_enabled"), &Environment::is_volumetric_fog_enabled);
	ClassDB::bind_method(D_METHOD("set_volumetric_fog_density", "density"), &Environment::set_volumetric_fog_density);
	ClassDB::bind_method(D_METHOD("get_volumetric_fog_density"), &Environment::get_volumetric_fog_density);
	ClassDB::bind_method(D_METHOD("set_volumetric_fog_height_falloff", "falloff"), &Environment::set_volumetric_fog_height_falloff);
	ClassDB::bind_method(D_METHOD("get_volumetric_fog_height_falloff"), &Environment::get_volumetric_fog_height_falloff);
	ClassDB::bind_method(D_METHOD("set_volumetric_fog_height_ref", "ref_height"), &Environment::set_volumetric_fog_height_ref);
	ClassDB::bind_method(D_METHOD("get_volumetric_fog_height_ref"), &Environment::get_volumetric_fog_height_ref);
	ClassDB::bind_method(D_METHOD("set_volumetric_fog_height_relative", "relative"), &Environment::set_volumetric_fog_height_relative);
	ClassDB::bind_method(D_METHOD("is_volumetric_fog_height_relative"), &Environment::is_volumetric_fog_height_relative);
	ClassDB::bind_method(D_METHOD("set_volumetric_fog_albedo", "albedo"), &Environment::set_volumetric_fog_albedo);
	ClassDB::bind_method(D_METHOD("get_volumetric_fog_albedo"), &Environment::get_volumetric_fog_albedo);
	ClassDB::bind_method(D_METHOD("set_volumetric_fog_ambient", "ambient"), &Environment::set_volumetric_fog_ambient);
	ClassDB::bind_method(D_METHOD("get_volumetric_fog_ambient"), &Environment::get_volumetric_fog_ambient);
	ClassDB::bind_method(D_METHOD("set_volumetric_fog_sun_energy", "energy"), &Environment::set_volumetric_fog_sun_energy);
	ClassDB::bind_method(D_METHOD("get_volumetric_fog_sun_energy"), &Environment::get_volumetric_fog_sun_energy);
	ClassDB::bind_method(D_METHOD("set_volumetric_fog_anisotropy", "anisotropy"), &Environment::set_volumetric_fog_anisotropy);
	ClassDB::bind_method(D_METHOD("get_volumetric_fog_anisotropy"), &Environment::get_volumetric_fog_anisotropy);
	ClassDB::bind_method(D_METHOD("set_volumetric_fog_length", "length"), &Environment::set_volumetric_fog_length);
	ClassDB::bind_method(D_METHOD("get_volumetric_fog_length"), &Environment::get_volumetric_fog_length);
	ClassDB::bind_method(D_METHOD("set_volumetric_fog_extinction", "extinction"), &Environment::set_volumetric_fog_extinction);
	ClassDB::bind_method(D_METHOD("get_volumetric_fog_extinction"), &Environment::get_volumetric_fog_extinction);
	ClassDB::bind_method(D_METHOD("set_volumetric_fog_samples", "samples"), &Environment::set_volumetric_fog_samples);
	ClassDB::bind_method(D_METHOD("get_volumetric_fog_samples"), &Environment::get_volumetric_fog_samples);
	ClassDB::bind_method(D_METHOD("set_volumetric_fog_temporal_blend", "blend"), &Environment::set_volumetric_fog_temporal_blend);
	ClassDB::bind_method(D_METHOD("get_volumetric_fog_temporal_blend"), &Environment::get_volumetric_fog_temporal_blend);

	ADD_GROUP("Volumetric Fog", "volumetric_fog_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "volumetric_fog_enabled"), "set_volumetric_fog_enabled", "is_volumetric_fog_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "volumetric_fog_density", PROPERTY_HINT_RANGE, "0,1,0.0001"), "set_volumetric_fog_density", "get_volumetric_fog_density");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "volumetric_fog_height_falloff", PROPERTY_HINT_RANGE, "0,2,0.001,or_greater"), "set_volumetric_fog_height_falloff", "get_volumetric_fog_height_falloff");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "volumetric_fog_height_ref", PROPERTY_HINT_RANGE, "-4000,4000,0.1,or_lesser,or_greater"), "set_volumetric_fog_height_ref", "get_volumetric_fog_height_ref");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "volumetric_fog_height_relative"), "set_volumetric_fog_height_relative", "is_volumetric_fog_height_relative");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "volumetric_fog_albedo"), "set_volumetric_fog_albedo", "get_volumetric_fog_albedo");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "volumetric_fog_ambient"), "set_volumetric_fog_ambient", "get_volumetric_fog_ambient");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "volumetric_fog_sun_energy", PROPERTY_HINT_RANGE, "0,16,0.01,or_greater"), "set_volumetric_fog_sun_energy", "get_volumetric_fog_sun_energy");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "volumetric_fog_anisotropy", PROPERTY_HINT_RANGE, "-0.95,0.95,0.01"), "set_volumetric_fog_anisotropy", "get_volumetric_fog_anisotropy");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "volumetric_fog_length", PROPERTY_HINT_RANGE, "1,4000,0.1,or_greater"), "set_volumetric_fog_length", "get_volumetric_fog_length");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "volumetric_fog_extinction", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_volumetric_fog_extinction", "get_volumetric_fog_extinction");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "volumetric_fog_samples", PROPERTY_HINT_RANGE, "8,512,1"), "set_volumetric_fog_samples", "get_volumetric_fog_samples");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "volumetric_fog_temporal_blend", PROPERTY_HINT_RANGE, "0,0.98,0.01"), "set_volumetric_fog_temporal_blend", "get_volumetric_fog_temporal_blend");

	ClassDB::bind_method(D_METHOD("set_dynamic_gi_enabled", "value"), &Environment::set_dynamic_gi_enabled);
	ClassDB::bind_method(D_METHOD("is_dynamic_gi_enabled"), &Environment::is_dynamic_gi_enabled);
	ClassDB::bind_method(D_METHOD("set_dynamic_gi_cascades", "value"), &Environment::set_dynamic_gi_cascades);
	ClassDB::bind_method(D_METHOD("get_dynamic_gi_cascades"), &Environment::get_dynamic_gi_cascades);
	ClassDB::bind_method(D_METHOD("set_dynamic_gi_voxel_size", "value"), &Environment::set_dynamic_gi_voxel_size);
	ClassDB::bind_method(D_METHOD("get_dynamic_gi_voxel_size"), &Environment::get_dynamic_gi_voxel_size);
	ClassDB::bind_method(D_METHOD("set_dynamic_gi_energy", "value"), &Environment::set_dynamic_gi_energy);
	ClassDB::bind_method(D_METHOD("get_dynamic_gi_energy"), &Environment::get_dynamic_gi_energy);
	ClassDB::bind_method(D_METHOD("set_dynamic_gi_bounce_feedback", "value"), &Environment::set_dynamic_gi_bounce_feedback);
	ClassDB::bind_method(D_METHOD("get_dynamic_gi_bounce_feedback"), &Environment::get_dynamic_gi_bounce_feedback);
	ClassDB::bind_method(D_METHOD("set_dynamic_gi_normal_bias", "value"), &Environment::set_dynamic_gi_normal_bias);
	ClassDB::bind_method(D_METHOD("get_dynamic_gi_normal_bias"), &Environment::get_dynamic_gi_normal_bias);
	ClassDB::bind_method(D_METHOD("set_dynamic_gi_hysteresis", "value"), &Environment::set_dynamic_gi_hysteresis);
	ClassDB::bind_method(D_METHOD("get_dynamic_gi_hysteresis"), &Environment::get_dynamic_gi_hysteresis);
	ClassDB::bind_method(D_METHOD("set_dynamic_gi_rays", "value"), &Environment::set_dynamic_gi_rays);
	ClassDB::bind_method(D_METHOD("get_dynamic_gi_rays"), &Environment::get_dynamic_gi_rays);
	ClassDB::bind_method(D_METHOD("set_dynamic_gi_reflection_occlusion", "value"), &Environment::set_dynamic_gi_reflection_occlusion);
	ClassDB::bind_method(D_METHOD("is_dynamic_gi_reflection_occlusion"), &Environment::is_dynamic_gi_reflection_occlusion);
	ClassDB::bind_method(D_METHOD("set_dynamic_gi_cull_mask", "value"), &Environment::set_dynamic_gi_cull_mask);
	ClassDB::bind_method(D_METHOD("get_dynamic_gi_cull_mask"), &Environment::get_dynamic_gi_cull_mask);
	ClassDB::bind_method(D_METHOD("set_dynamic_gi_debug", "value"), &Environment::set_dynamic_gi_debug);
	ClassDB::bind_method(D_METHOD("get_dynamic_gi_debug"), &Environment::get_dynamic_gi_debug);

	ADD_GROUP("Dynamic GI", "dynamic_gi_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "dynamic_gi_enabled"), "set_dynamic_gi_enabled", "is_dynamic_gi_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "dynamic_gi_cascades", PROPERTY_HINT_RANGE, "1,6,1"), "set_dynamic_gi_cascades", "get_dynamic_gi_cascades");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "dynamic_gi_voxel_size", PROPERTY_HINT_RANGE, "0.05,4,0.01,or_greater"), "set_dynamic_gi_voxel_size", "get_dynamic_gi_voxel_size");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "dynamic_gi_energy", PROPERTY_HINT_RANGE, "0,8,0.01,or_greater"), "set_dynamic_gi_energy", "get_dynamic_gi_energy");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "dynamic_gi_bounce_feedback", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_dynamic_gi_bounce_feedback", "get_dynamic_gi_bounce_feedback");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "dynamic_gi_normal_bias", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_dynamic_gi_normal_bias", "get_dynamic_gi_normal_bias");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "dynamic_gi_hysteresis", PROPERTY_HINT_RANGE, "0,0.99,0.01"), "set_dynamic_gi_hysteresis", "get_dynamic_gi_hysteresis");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "dynamic_gi_rays", PROPERTY_HINT_ENUM, "64:64,128:128,192:192"), "set_dynamic_gi_rays", "get_dynamic_gi_rays");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "dynamic_gi_reflection_occlusion"), "set_dynamic_gi_reflection_occlusion", "is_dynamic_gi_reflection_occlusion");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "dynamic_gi_cull_mask", PROPERTY_HINT_LAYERS_3D_RENDER), "set_dynamic_gi_cull_mask", "get_dynamic_gi_cull_mask");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "dynamic_gi_debug", PROPERTY_HINT_ENUM, "Disabled,Voxel Lighting,Voxel Albedo,Irradiance,SSGI"), "set_dynamic_gi_debug", "get_dynamic_gi_debug");

	ClassDB::bind_method(D_METHOD("set_ssgi_enabled", "value"), &Environment::set_ssgi_enabled);
	ClassDB::bind_method(D_METHOD("is_ssgi_enabled"), &Environment::is_ssgi_enabled);
	ClassDB::bind_method(D_METHOD("set_ssgi_radius", "value"), &Environment::set_ssgi_radius);
	ClassDB::bind_method(D_METHOD("get_ssgi_radius"), &Environment::get_ssgi_radius);
	ClassDB::bind_method(D_METHOD("set_ssgi_intensity", "value"), &Environment::set_ssgi_intensity);
	ClassDB::bind_method(D_METHOD("get_ssgi_intensity"), &Environment::get_ssgi_intensity);
	ClassDB::bind_method(D_METHOD("set_ssgi_rays", "value"), &Environment::set_ssgi_rays);
	ClassDB::bind_method(D_METHOD("get_ssgi_rays"), &Environment::get_ssgi_rays);
	ClassDB::bind_method(D_METHOD("set_ssgi_steps", "value"), &Environment::set_ssgi_steps);
	ClassDB::bind_method(D_METHOD("get_ssgi_steps"), &Environment::get_ssgi_steps);
	ClassDB::bind_method(D_METHOD("set_ssgi_thickness", "value"), &Environment::set_ssgi_thickness);
	ClassDB::bind_method(D_METHOD("get_ssgi_thickness"), &Environment::get_ssgi_thickness);
	ClassDB::bind_method(D_METHOD("set_ssgi_temporal_blend", "value"), &Environment::set_ssgi_temporal_blend);
	ClassDB::bind_method(D_METHOD("get_ssgi_temporal_blend"), &Environment::get_ssgi_temporal_blend);
	ClassDB::bind_method(D_METHOD("set_ssgi_half_resolution", "value"), &Environment::set_ssgi_half_resolution);
	ClassDB::bind_method(D_METHOD("is_ssgi_half_resolution"), &Environment::is_ssgi_half_resolution);

	ADD_GROUP("SSGI", "ssgi_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ssgi_enabled"), "set_ssgi_enabled", "is_ssgi_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ssgi_radius", PROPERTY_HINT_RANGE, "0.1,32,0.1,or_greater"), "set_ssgi_radius", "get_ssgi_radius");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ssgi_intensity", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_ssgi_intensity", "get_ssgi_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ssgi_rays", PROPERTY_HINT_RANGE, "1,16,1"), "set_ssgi_rays", "get_ssgi_rays");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ssgi_steps", PROPERTY_HINT_RANGE, "4,64,1"), "set_ssgi_steps", "get_ssgi_steps");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ssgi_thickness", PROPERTY_HINT_RANGE, "0.01,4,0.01"), "set_ssgi_thickness", "get_ssgi_thickness");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ssgi_temporal_blend", PROPERTY_HINT_RANGE, "0,0.99,0.01"), "set_ssgi_temporal_blend", "get_ssgi_temporal_blend");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ssgi_half_resolution"), "set_ssgi_half_resolution", "is_ssgi_half_resolution");

	ClassDB::bind_method(D_METHOD("set_exp_height_fog_enabled", "enabled"), &Environment::set_exp_height_fog_enabled);
	ClassDB::bind_method(D_METHOD("is_exp_height_fog_enabled"), &Environment::is_exp_height_fog_enabled);
	ClassDB::bind_method(D_METHOD("set_exp_height_fog_density", "density"), &Environment::set_exp_height_fog_density);
	ClassDB::bind_method(D_METHOD("get_exp_height_fog_density"), &Environment::get_exp_height_fog_density);
	ClassDB::bind_method(D_METHOD("set_exp_height_fog_falloff", "falloff"), &Environment::set_exp_height_fog_falloff);
	ClassDB::bind_method(D_METHOD("get_exp_height_fog_falloff"), &Environment::get_exp_height_fog_falloff);
	ClassDB::bind_method(D_METHOD("set_exp_height_fog_ref_height", "ref_height"), &Environment::set_exp_height_fog_ref_height);
	ClassDB::bind_method(D_METHOD("get_exp_height_fog_ref_height"), &Environment::get_exp_height_fog_ref_height);
	ClassDB::bind_method(D_METHOD("set_exp_height_fog_ref_height_relative", "relative"), &Environment::set_exp_height_fog_ref_height_relative);
	ClassDB::bind_method(D_METHOD("is_exp_height_fog_ref_height_relative"), &Environment::is_exp_height_fog_ref_height_relative);
	ClassDB::bind_method(D_METHOD("set_exp_height_fog_start_distance", "distance"), &Environment::set_exp_height_fog_start_distance);
	ClassDB::bind_method(D_METHOD("get_exp_height_fog_start_distance"), &Environment::get_exp_height_fog_start_distance);
	ClassDB::bind_method(D_METHOD("set_exp_height_fog_max_opacity", "opacity"), &Environment::set_exp_height_fog_max_opacity);
	ClassDB::bind_method(D_METHOD("get_exp_height_fog_max_opacity"), &Environment::get_exp_height_fog_max_opacity);
	ClassDB::bind_method(D_METHOD("set_exp_height_fog_inscatter", "color"), &Environment::set_exp_height_fog_inscatter);
	ClassDB::bind_method(D_METHOD("get_exp_height_fog_inscatter"), &Environment::get_exp_height_fog_inscatter);
	ClassDB::bind_method(D_METHOD("set_exp_height_fog_directional_enabled", "enabled"), &Environment::set_exp_height_fog_directional_enabled);
	ClassDB::bind_method(D_METHOD("is_exp_height_fog_directional_enabled"), &Environment::is_exp_height_fog_directional_enabled);
	ClassDB::bind_method(D_METHOD("set_exp_height_fog_directional_color", "color"), &Environment::set_exp_height_fog_directional_color);
	ClassDB::bind_method(D_METHOD("get_exp_height_fog_directional_color"), &Environment::get_exp_height_fog_directional_color);
	ClassDB::bind_method(D_METHOD("set_exp_height_fog_light_direction", "dir"), &Environment::set_exp_height_fog_light_direction);
	ClassDB::bind_method(D_METHOD("get_exp_height_fog_light_direction"), &Environment::get_exp_height_fog_light_direction);
	ClassDB::bind_method(D_METHOD("set_exp_height_fog_directional_exponent", "exponent"), &Environment::set_exp_height_fog_directional_exponent);
	ClassDB::bind_method(D_METHOD("get_exp_height_fog_directional_exponent"), &Environment::get_exp_height_fog_directional_exponent);

	ADD_GROUP("Exp Height Fog", "exp_height_fog_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "exp_height_fog_enabled"), "set_exp_height_fog_enabled", "is_exp_height_fog_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "exp_height_fog_density", PROPERTY_HINT_RANGE, "0,1,0.0001"), "set_exp_height_fog_density", "get_exp_height_fog_density");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "exp_height_fog_falloff", PROPERTY_HINT_RANGE, "0,2,0.001"), "set_exp_height_fog_falloff", "get_exp_height_fog_falloff");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "exp_height_fog_ref_height", PROPERTY_HINT_RANGE, "-4000,4000,0.1,or_lesser,or_greater"), "set_exp_height_fog_ref_height", "get_exp_height_fog_ref_height");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "exp_height_fog_ref_height_relative"), "set_exp_height_fog_ref_height_relative", "is_exp_height_fog_ref_height_relative");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "exp_height_fog_start_distance", PROPERTY_HINT_RANGE, "0,10000,0.1"), "set_exp_height_fog_start_distance", "get_exp_height_fog_start_distance");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "exp_height_fog_max_opacity", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_exp_height_fog_max_opacity", "get_exp_height_fog_max_opacity");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "exp_height_fog_inscatter"), "set_exp_height_fog_inscatter", "get_exp_height_fog_inscatter");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "exp_height_fog_directional_enabled"), "set_exp_height_fog_directional_enabled", "is_exp_height_fog_directional_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "exp_height_fog_directional_color"), "set_exp_height_fog_directional_color", "get_exp_height_fog_directional_color");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "exp_height_fog_light_direction"), "set_exp_height_fog_light_direction", "get_exp_height_fog_light_direction");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "exp_height_fog_directional_exponent", PROPERTY_HINT_RANGE, "1,32,0.1"), "set_exp_height_fog_directional_exponent", "get_exp_height_fog_directional_exponent");

	ClassDB::bind_method(D_METHOD("set_tonemapper", "mode"), &Environment::set_tonemapper);
	ClassDB::bind_method(D_METHOD("get_tonemapper"), &Environment::get_tonemapper);

	ClassDB::bind_method(D_METHOD("set_tonemap_exposure", "exposure"), &Environment::set_tonemap_exposure);
	ClassDB::bind_method(D_METHOD("get_tonemap_exposure"), &Environment::get_tonemap_exposure);

	ClassDB::bind_method(D_METHOD("set_tonemap_white", "white"), &Environment::set_tonemap_white);
	ClassDB::bind_method(D_METHOD("get_tonemap_white"), &Environment::get_tonemap_white);

	ClassDB::bind_method(D_METHOD("set_tonemap_auto_exposure", "auto_exposure"), &Environment::set_tonemap_auto_exposure);
	ClassDB::bind_method(D_METHOD("get_tonemap_auto_exposure"), &Environment::get_tonemap_auto_exposure);

	ClassDB::bind_method(D_METHOD("set_tonemap_auto_exposure_max", "exposure_max"), &Environment::set_tonemap_auto_exposure_max);
	ClassDB::bind_method(D_METHOD("get_tonemap_auto_exposure_max"), &Environment::get_tonemap_auto_exposure_max);

	ClassDB::bind_method(D_METHOD("set_tonemap_auto_exposure_min", "exposure_min"), &Environment::set_tonemap_auto_exposure_min);
	ClassDB::bind_method(D_METHOD("get_tonemap_auto_exposure_min"), &Environment::get_tonemap_auto_exposure_min);

	ClassDB::bind_method(D_METHOD("set_tonemap_auto_exposure_speed", "exposure_speed"), &Environment::set_tonemap_auto_exposure_speed);
	ClassDB::bind_method(D_METHOD("get_tonemap_auto_exposure_speed"), &Environment::get_tonemap_auto_exposure_speed);

	ClassDB::bind_method(D_METHOD("set_tonemap_auto_exposure_grey", "exposure_grey"), &Environment::set_tonemap_auto_exposure_grey);
	ClassDB::bind_method(D_METHOD("get_tonemap_auto_exposure_grey"), &Environment::get_tonemap_auto_exposure_grey);

	ADD_GROUP("Tonemap", "tonemap_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tonemap_mode", PROPERTY_HINT_ENUM, "Linear,Reinhard,Filmic,ACES,ACES Fitted"), "set_tonemapper", "get_tonemapper");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "tonemap_exposure", PROPERTY_HINT_RANGE, "0,16,0.01"), "set_tonemap_exposure", "get_tonemap_exposure");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "tonemap_white", PROPERTY_HINT_RANGE, "0,16,0.01"), "set_tonemap_white", "get_tonemap_white");
	ADD_GROUP("Auto Exposure", "auto_exposure_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_exposure_enabled"), "set_tonemap_auto_exposure", "get_tonemap_auto_exposure");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "auto_exposure_scale", PROPERTY_HINT_RANGE, "0.01,64,0.01"), "set_tonemap_auto_exposure_grey", "get_tonemap_auto_exposure_grey");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "auto_exposure_min_luma", PROPERTY_HINT_RANGE, "0,16,0.01"), "set_tonemap_auto_exposure_min", "get_tonemap_auto_exposure_min");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "auto_exposure_max_luma", PROPERTY_HINT_RANGE, "0,16,0.01"), "set_tonemap_auto_exposure_max", "get_tonemap_auto_exposure_max");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "auto_exposure_speed", PROPERTY_HINT_RANGE, "0.01,64,0.01"), "set_tonemap_auto_exposure_speed", "get_tonemap_auto_exposure_speed");

	ClassDB::bind_method(D_METHOD("set_ssr_enabled", "enabled"), &Environment::set_ssr_enabled);
	ClassDB::bind_method(D_METHOD("is_ssr_enabled"), &Environment::is_ssr_enabled);

	ClassDB::bind_method(D_METHOD("set_ssr_max_steps", "max_steps"), &Environment::set_ssr_max_steps);
	ClassDB::bind_method(D_METHOD("get_ssr_max_steps"), &Environment::get_ssr_max_steps);

	ClassDB::bind_method(D_METHOD("set_ssr_fade_in", "fade_in"), &Environment::set_ssr_fade_in);
	ClassDB::bind_method(D_METHOD("get_ssr_fade_in"), &Environment::get_ssr_fade_in);

	ClassDB::bind_method(D_METHOD("set_ssr_fade_out", "fade_out"), &Environment::set_ssr_fade_out);
	ClassDB::bind_method(D_METHOD("get_ssr_fade_out"), &Environment::get_ssr_fade_out);

	ClassDB::bind_method(D_METHOD("set_ssr_depth_tolerance", "depth_tolerance"), &Environment::set_ssr_depth_tolerance);
	ClassDB::bind_method(D_METHOD("get_ssr_depth_tolerance"), &Environment::get_ssr_depth_tolerance);

	ClassDB::bind_method(D_METHOD("set_ssr_rough", "rough"), &Environment::set_ssr_rough);
	ClassDB::bind_method(D_METHOD("is_ssr_rough"), &Environment::is_ssr_rough);

	ADD_GROUP("SS Reflections", "ss_reflections_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ss_reflections_enabled"), "set_ssr_enabled", "is_ssr_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ss_reflections_max_steps", PROPERTY_HINT_RANGE, "1,512,1"), "set_ssr_max_steps", "get_ssr_max_steps");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ss_reflections_fade_in", PROPERTY_HINT_EXP_EASING), "set_ssr_fade_in", "get_ssr_fade_in");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ss_reflections_fade_out", PROPERTY_HINT_EXP_EASING), "set_ssr_fade_out", "get_ssr_fade_out");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ss_reflections_depth_tolerance", PROPERTY_HINT_RANGE, "0.1,128,0.1"), "set_ssr_depth_tolerance", "get_ssr_depth_tolerance");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ss_reflections_roughness"), "set_ssr_rough", "is_ssr_rough");

	ClassDB::bind_method(D_METHOD("set_ssao_enabled", "enabled"), &Environment::set_ssao_enabled);
	ClassDB::bind_method(D_METHOD("is_ssao_enabled"), &Environment::is_ssao_enabled);

	ClassDB::bind_method(D_METHOD("set_ssao_radius", "radius"), &Environment::set_ssao_radius);
	ClassDB::bind_method(D_METHOD("get_ssao_radius"), &Environment::get_ssao_radius);

	ClassDB::bind_method(D_METHOD("set_ssao_intensity", "intensity"), &Environment::set_ssao_intensity);
	ClassDB::bind_method(D_METHOD("get_ssao_intensity"), &Environment::get_ssao_intensity);

	ClassDB::bind_method(D_METHOD("set_ssao_radius2", "radius"), &Environment::set_ssao_radius2);
	ClassDB::bind_method(D_METHOD("get_ssao_radius2"), &Environment::get_ssao_radius2);

	ClassDB::bind_method(D_METHOD("set_ssao_intensity2", "intensity"), &Environment::set_ssao_intensity2);
	ClassDB::bind_method(D_METHOD("get_ssao_intensity2"), &Environment::get_ssao_intensity2);

	ClassDB::bind_method(D_METHOD("set_ssao_bias", "bias"), &Environment::set_ssao_bias);
	ClassDB::bind_method(D_METHOD("get_ssao_bias"), &Environment::get_ssao_bias);

	ClassDB::bind_method(D_METHOD("set_ssao_direct_light_affect", "amount"), &Environment::set_ssao_direct_light_affect);
	ClassDB::bind_method(D_METHOD("get_ssao_direct_light_affect"), &Environment::get_ssao_direct_light_affect);

	ClassDB::bind_method(D_METHOD("set_ssao_ao_channel_affect", "amount"), &Environment::set_ssao_ao_channel_affect);
	ClassDB::bind_method(D_METHOD("get_ssao_ao_channel_affect"), &Environment::get_ssao_ao_channel_affect);

	ClassDB::bind_method(D_METHOD("set_ssao_color", "color"), &Environment::set_ssao_color);
	ClassDB::bind_method(D_METHOD("get_ssao_color"), &Environment::get_ssao_color);

	ClassDB::bind_method(D_METHOD("set_ssao_blur", "mode"), &Environment::set_ssao_blur);
	ClassDB::bind_method(D_METHOD("get_ssao_blur"), &Environment::get_ssao_blur);

	ClassDB::bind_method(D_METHOD("set_ssao_quality", "quality"), &Environment::set_ssao_quality);
	ClassDB::bind_method(D_METHOD("get_ssao_quality"), &Environment::get_ssao_quality);

	ClassDB::bind_method(D_METHOD("set_ssao_edge_sharpness", "edge_sharpness"), &Environment::set_ssao_edge_sharpness);
	ClassDB::bind_method(D_METHOD("get_ssao_edge_sharpness"), &Environment::get_ssao_edge_sharpness);

	ADD_GROUP("SSAO", "ssao_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ssao_enabled"), "set_ssao_enabled", "is_ssao_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ssao_radius", PROPERTY_HINT_RANGE, "0.1,128,0.01"), "set_ssao_radius", "get_ssao_radius");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ssao_intensity", PROPERTY_HINT_RANGE, "0.0,128,0.01"), "set_ssao_intensity", "get_ssao_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ssao_radius2", PROPERTY_HINT_RANGE, "0.0,128,0.01"), "set_ssao_radius2", "get_ssao_radius2");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ssao_intensity2", PROPERTY_HINT_RANGE, "0.0,128,0.01"), "set_ssao_intensity2", "get_ssao_intensity2");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ssao_bias", PROPERTY_HINT_RANGE, "0.001,8,0.001"), "set_ssao_bias", "get_ssao_bias");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ssao_light_affect", PROPERTY_HINT_RANGE, "0.00,1,0.01"), "set_ssao_direct_light_affect", "get_ssao_direct_light_affect");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ssao_ao_channel_affect", PROPERTY_HINT_RANGE, "0.00,1,0.01"), "set_ssao_ao_channel_affect", "get_ssao_ao_channel_affect");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "ssao_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_ssao_color", "get_ssao_color");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ssao_quality", PROPERTY_HINT_ENUM, "Low,Medium,High"), "set_ssao_quality", "get_ssao_quality");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ssao_blur", PROPERTY_HINT_ENUM, "Disabled,1x1,2x2,3x3"), "set_ssao_blur", "get_ssao_blur");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ssao_edge_sharpness", PROPERTY_HINT_RANGE, "0,32,0.01"), "set_ssao_edge_sharpness", "get_ssao_edge_sharpness");

	ClassDB::bind_method(D_METHOD("set_dof_blur_far_enabled", "enabled"), &Environment::set_dof_blur_far_enabled);
	ClassDB::bind_method(D_METHOD("is_dof_blur_far_enabled"), &Environment::is_dof_blur_far_enabled);

	ClassDB::bind_method(D_METHOD("set_dof_blur_far_distance", "intensity"), &Environment::set_dof_blur_far_distance);
	ClassDB::bind_method(D_METHOD("get_dof_blur_far_distance"), &Environment::get_dof_blur_far_distance);

	ClassDB::bind_method(D_METHOD("set_dof_blur_far_transition", "intensity"), &Environment::set_dof_blur_far_transition);
	ClassDB::bind_method(D_METHOD("get_dof_blur_far_transition"), &Environment::get_dof_blur_far_transition);

	ClassDB::bind_method(D_METHOD("set_dof_blur_far_amount", "intensity"), &Environment::set_dof_blur_far_amount);
	ClassDB::bind_method(D_METHOD("get_dof_blur_far_amount"), &Environment::get_dof_blur_far_amount);

	ClassDB::bind_method(D_METHOD("set_dof_blur_far_quality", "intensity"), &Environment::set_dof_blur_far_quality);
	ClassDB::bind_method(D_METHOD("get_dof_blur_far_quality"), &Environment::get_dof_blur_far_quality);

	ClassDB::bind_method(D_METHOD("set_dof_blur_near_enabled", "enabled"), &Environment::set_dof_blur_near_enabled);
	ClassDB::bind_method(D_METHOD("is_dof_blur_near_enabled"), &Environment::is_dof_blur_near_enabled);

	ClassDB::bind_method(D_METHOD("set_dof_blur_near_distance", "intensity"), &Environment::set_dof_blur_near_distance);
	ClassDB::bind_method(D_METHOD("get_dof_blur_near_distance"), &Environment::get_dof_blur_near_distance);

	ClassDB::bind_method(D_METHOD("set_dof_blur_near_transition", "intensity"), &Environment::set_dof_blur_near_transition);
	ClassDB::bind_method(D_METHOD("get_dof_blur_near_transition"), &Environment::get_dof_blur_near_transition);

	ClassDB::bind_method(D_METHOD("set_dof_blur_near_amount", "intensity"), &Environment::set_dof_blur_near_amount);
	ClassDB::bind_method(D_METHOD("get_dof_blur_near_amount"), &Environment::get_dof_blur_near_amount);

	ClassDB::bind_method(D_METHOD("set_dof_blur_near_quality", "level"), &Environment::set_dof_blur_near_quality);
	ClassDB::bind_method(D_METHOD("get_dof_blur_near_quality"), &Environment::get_dof_blur_near_quality);

	ADD_GROUP("DOF Far Blur", "dof_blur_far_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "dof_blur_far_enabled"), "set_dof_blur_far_enabled", "is_dof_blur_far_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "dof_blur_far_distance", PROPERTY_HINT_EXP_RANGE, "0.01,8192,0.01"), "set_dof_blur_far_distance", "get_dof_blur_far_distance");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "dof_blur_far_transition", PROPERTY_HINT_EXP_RANGE, "0.01,8192,0.01"), "set_dof_blur_far_transition", "get_dof_blur_far_transition");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "dof_blur_far_amount", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_dof_blur_far_amount", "get_dof_blur_far_amount");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "dof_blur_far_quality", PROPERTY_HINT_ENUM, "Low,Medium,High"), "set_dof_blur_far_quality", "get_dof_blur_far_quality");

	ADD_GROUP("DOF Near Blur", "dof_blur_near_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "dof_blur_near_enabled"), "set_dof_blur_near_enabled", "is_dof_blur_near_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "dof_blur_near_distance", PROPERTY_HINT_EXP_RANGE, "0.01,8192,0.01"), "set_dof_blur_near_distance", "get_dof_blur_near_distance");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "dof_blur_near_transition", PROPERTY_HINT_EXP_RANGE, "0.01,8192,0.01"), "set_dof_blur_near_transition", "get_dof_blur_near_transition");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "dof_blur_near_amount", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_dof_blur_near_amount", "get_dof_blur_near_amount");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "dof_blur_near_quality", PROPERTY_HINT_ENUM, "Low,Medium,High"), "set_dof_blur_near_quality", "get_dof_blur_near_quality");

	ClassDB::bind_method(D_METHOD("set_glow_enabled", "enabled"), &Environment::set_glow_enabled);
	ClassDB::bind_method(D_METHOD("is_glow_enabled"), &Environment::is_glow_enabled);

	ClassDB::bind_method(D_METHOD("set_glow_level", "idx", "enabled"), &Environment::set_glow_level);
	ClassDB::bind_method(D_METHOD("is_glow_level_enabled", "idx"), &Environment::is_glow_level_enabled);

	ClassDB::bind_method(D_METHOD("set_glow_intensity", "intensity"), &Environment::set_glow_intensity);
	ClassDB::bind_method(D_METHOD("get_glow_intensity"), &Environment::get_glow_intensity);

	ClassDB::bind_method(D_METHOD("set_glow_strength", "strength"), &Environment::set_glow_strength);
	ClassDB::bind_method(D_METHOD("get_glow_strength"), &Environment::get_glow_strength);

	ClassDB::bind_method(D_METHOD("set_glow_bloom", "amount"), &Environment::set_glow_bloom);
	ClassDB::bind_method(D_METHOD("get_glow_bloom"), &Environment::get_glow_bloom);

	ClassDB::bind_method(D_METHOD("set_glow_blend_mode", "mode"), &Environment::set_glow_blend_mode);
	ClassDB::bind_method(D_METHOD("get_glow_blend_mode"), &Environment::get_glow_blend_mode);

	ClassDB::bind_method(D_METHOD("set_glow_hdr_bleed_threshold", "threshold"), &Environment::set_glow_hdr_bleed_threshold);
	ClassDB::bind_method(D_METHOD("get_glow_hdr_bleed_threshold"), &Environment::get_glow_hdr_bleed_threshold);

	ClassDB::bind_method(D_METHOD("set_glow_hdr_luminance_cap", "amount"), &Environment::set_glow_hdr_luminance_cap);
	ClassDB::bind_method(D_METHOD("get_glow_hdr_luminance_cap"), &Environment::get_glow_hdr_luminance_cap);

	ClassDB::bind_method(D_METHOD("set_glow_hdr_bleed_scale", "scale"), &Environment::set_glow_hdr_bleed_scale);
	ClassDB::bind_method(D_METHOD("get_glow_hdr_bleed_scale"), &Environment::get_glow_hdr_bleed_scale);

	ClassDB::bind_method(D_METHOD("set_glow_bicubic_upscale", "enabled"), &Environment::set_glow_bicubic_upscale);
	ClassDB::bind_method(D_METHOD("is_glow_bicubic_upscale_enabled"), &Environment::is_glow_bicubic_upscale_enabled);

	ClassDB::bind_method(D_METHOD("set_glow_high_quality", "enabled"), &Environment::set_glow_high_quality);
	ClassDB::bind_method(D_METHOD("is_glow_high_quality_enabled"), &Environment::is_glow_high_quality_enabled);

	ADD_GROUP("Glow", "glow_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "glow_enabled"), "set_glow_enabled", "is_glow_enabled");
	ADD_PROPERTYI(PropertyInfo(Variant::BOOL, "glow_levels/1"), "set_glow_level", "is_glow_level_enabled", 0);
	ADD_PROPERTYI(PropertyInfo(Variant::BOOL, "glow_levels/2"), "set_glow_level", "is_glow_level_enabled", 1);
	ADD_PROPERTYI(PropertyInfo(Variant::BOOL, "glow_levels/3"), "set_glow_level", "is_glow_level_enabled", 2);
	ADD_PROPERTYI(PropertyInfo(Variant::BOOL, "glow_levels/4"), "set_glow_level", "is_glow_level_enabled", 3);
	ADD_PROPERTYI(PropertyInfo(Variant::BOOL, "glow_levels/5"), "set_glow_level", "is_glow_level_enabled", 4);
	ADD_PROPERTYI(PropertyInfo(Variant::BOOL, "glow_levels/6"), "set_glow_level", "is_glow_level_enabled", 5);
	ADD_PROPERTYI(PropertyInfo(Variant::BOOL, "glow_levels/7"), "set_glow_level", "is_glow_level_enabled", 6);

	ADD_PROPERTY(PropertyInfo(Variant::REAL, "glow_intensity", PROPERTY_HINT_RANGE, "0.0,8.0,0.01"), "set_glow_intensity", "get_glow_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "glow_strength", PROPERTY_HINT_RANGE, "0.0,2.0,0.01"), "set_glow_strength", "get_glow_strength");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "glow_bloom", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_glow_bloom", "get_glow_bloom");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "glow_blend_mode", PROPERTY_HINT_ENUM, "Additive,Screen,Softlight,Replace"), "set_glow_blend_mode", "get_glow_blend_mode");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "glow_hdr_threshold", PROPERTY_HINT_RANGE, "0.0,4.0,0.01"), "set_glow_hdr_bleed_threshold", "get_glow_hdr_bleed_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "glow_hdr_luminance_cap", PROPERTY_HINT_RANGE, "0.0,256.0,0.01"), "set_glow_hdr_luminance_cap", "get_glow_hdr_luminance_cap");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "glow_hdr_scale", PROPERTY_HINT_RANGE, "0.0,4.0,0.01"), "set_glow_hdr_bleed_scale", "get_glow_hdr_bleed_scale");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "glow_bicubic_upscale"), "set_glow_bicubic_upscale", "is_glow_bicubic_upscale_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "glow_high_quality"), "set_glow_high_quality", "is_glow_high_quality_enabled");

	ClassDB::bind_method(D_METHOD("set_adjustment_enable", "enabled"), &Environment::set_adjustment_enable);
	ClassDB::bind_method(D_METHOD("is_adjustment_enabled"), &Environment::is_adjustment_enabled);

	ClassDB::bind_method(D_METHOD("set_adjustment_brightness", "brightness"), &Environment::set_adjustment_brightness);
	ClassDB::bind_method(D_METHOD("get_adjustment_brightness"), &Environment::get_adjustment_brightness);

	ClassDB::bind_method(D_METHOD("set_adjustment_contrast", "contrast"), &Environment::set_adjustment_contrast);
	ClassDB::bind_method(D_METHOD("get_adjustment_contrast"), &Environment::get_adjustment_contrast);

	ClassDB::bind_method(D_METHOD("set_adjustment_saturation", "saturation"), &Environment::set_adjustment_saturation);
	ClassDB::bind_method(D_METHOD("get_adjustment_saturation"), &Environment::get_adjustment_saturation);

	ClassDB::bind_method(D_METHOD("set_adjustment_color_correction", "color_correction"), &Environment::set_adjustment_color_correction);
	ClassDB::bind_method(D_METHOD("get_adjustment_color_correction"), &Environment::get_adjustment_color_correction);

	ADD_GROUP("Adjustments", "adjustment_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "adjustment_enabled"), "set_adjustment_enable", "is_adjustment_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "adjustment_brightness", PROPERTY_HINT_RANGE, "0.01,8,0.01"), "set_adjustment_brightness", "get_adjustment_brightness");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "adjustment_contrast", PROPERTY_HINT_RANGE, "0.01,8,0.01"), "set_adjustment_contrast", "get_adjustment_contrast");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "adjustment_saturation", PROPERTY_HINT_RANGE, "0.01,8,0.01"), "set_adjustment_saturation", "get_adjustment_saturation");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "adjustment_color_correction", PROPERTY_HINT_RESOURCE_TYPE, "Texture"), "set_adjustment_color_correction", "get_adjustment_color_correction");

	BIND_ENUM_CONSTANT(BG_KEEP);
	BIND_ENUM_CONSTANT(BG_CLEAR_COLOR);
	BIND_ENUM_CONSTANT(BG_COLOR);
	BIND_ENUM_CONSTANT(BG_SKY);
	BIND_ENUM_CONSTANT(BG_COLOR_SKY);
	BIND_ENUM_CONSTANT(BG_CANVAS);
	BIND_ENUM_CONSTANT(BG_CAMERA_FEED);
	BIND_ENUM_CONSTANT(BG_MAX);

	BIND_ENUM_CONSTANT(GLOW_BLEND_MODE_ADDITIVE);
	BIND_ENUM_CONSTANT(GLOW_BLEND_MODE_SCREEN);
	BIND_ENUM_CONSTANT(GLOW_BLEND_MODE_SOFTLIGHT);
	BIND_ENUM_CONSTANT(GLOW_BLEND_MODE_REPLACE);

	BIND_ENUM_CONSTANT(TONE_MAPPER_LINEAR);
	BIND_ENUM_CONSTANT(TONE_MAPPER_REINHARDT);
	BIND_ENUM_CONSTANT(TONE_MAPPER_FILMIC);
	BIND_ENUM_CONSTANT(TONE_MAPPER_ACES);
	BIND_ENUM_CONSTANT(TONE_MAPPER_ACES_FITTED);

	BIND_ENUM_CONSTANT(DOF_BLUR_QUALITY_LOW);
	BIND_ENUM_CONSTANT(DOF_BLUR_QUALITY_MEDIUM);
	BIND_ENUM_CONSTANT(DOF_BLUR_QUALITY_HIGH);

	BIND_ENUM_CONSTANT(DYNAMIC_GI_DEBUG_DISABLED);
	BIND_ENUM_CONSTANT(DYNAMIC_GI_DEBUG_LIGHTING);
	BIND_ENUM_CONSTANT(DYNAMIC_GI_DEBUG_ALBEDO);
	BIND_ENUM_CONSTANT(DYNAMIC_GI_DEBUG_IRRADIANCE);
	BIND_ENUM_CONSTANT(DYNAMIC_GI_DEBUG_SSGI);

	BIND_ENUM_CONSTANT(SSAO_BLUR_DISABLED);
	BIND_ENUM_CONSTANT(SSAO_BLUR_1x1);
	BIND_ENUM_CONSTANT(SSAO_BLUR_2x2);
	BIND_ENUM_CONSTANT(SSAO_BLUR_3x3);

	BIND_ENUM_CONSTANT(SSAO_QUALITY_LOW);
	BIND_ENUM_CONSTANT(SSAO_QUALITY_MEDIUM);
	BIND_ENUM_CONSTANT(SSAO_QUALITY_HIGH);
}

Environment::Environment() :
		bg_mode(BG_CLEAR_COLOR),
		tone_mapper(TONE_MAPPER_LINEAR),
		ssao_blur(SSAO_BLUR_3x3),
		ssao_quality(SSAO_QUALITY_MEDIUM),
		glow_blend_mode(GLOW_BLEND_MODE_ADDITIVE),
		dof_blur_far_quality(DOF_BLUR_QUALITY_LOW),
		dof_blur_near_quality(DOF_BLUR_QUALITY_LOW) {
	environment = RID_PRIME(VS::get_singleton()->environment_create());

	bg_mode = BG_CLEAR_COLOR;
	bg_sky_custom_fov = 0;
	bg_sky_orientation = Basis();
	bg_energy = 1.0;
	bg_canvas_max_layer = 0;
	ambient_energy = 1.0;
	//ambient_sky_contribution = 1.0;
	set_ambient_light_sky_contribution(1.0);
	set_camera_feed_id(1);

	tone_mapper = TONE_MAPPER_LINEAR;
	tonemap_exposure = 1.0;
	tonemap_white = 1.0;
	tonemap_auto_exposure = false;
	tonemap_auto_exposure_max = 8;
	tonemap_auto_exposure_min = 0.05;
	tonemap_auto_exposure_speed = 0.5;
	tonemap_auto_exposure_grey = 0.4;

	set_tonemapper(tone_mapper); //update

	adjustment_enabled = false;
	adjustment_contrast = 1.0;
	adjustment_saturation = 1.0;
	adjustment_brightness = 1.0;

	set_adjustment_enable(adjustment_enabled); //update

	ssr_enabled = false;
	ssr_max_steps = 64;
	ssr_fade_in = 0.15;
	ssr_fade_out = 2.0;
	ssr_depth_tolerance = 0.2;
	ssr_roughness = true;

	ssao_enabled = false;
	ssao_radius = 1;
	ssao_intensity = 1;
	ssao_radius2 = 0;
	ssao_intensity2 = 1;
	ssao_bias = 0.01;
	ssao_direct_light_affect = 0.0;
	ssao_ao_channel_affect = 0.0;
	ssao_blur = SSAO_BLUR_3x3;
	set_ssao_edge_sharpness(4);
	set_ssao_quality(SSAO_QUALITY_MEDIUM);

	glow_enabled = false;
	glow_levels = (1 << 2) | (1 << 4);
	glow_intensity = 0.8;
	glow_strength = 1.0;
	glow_bloom = 0.0;
	glow_blend_mode = GLOW_BLEND_MODE_SOFTLIGHT;
	glow_hdr_bleed_threshold = 1.0;
	glow_hdr_luminance_cap = 12.0;
	glow_hdr_bleed_scale = 2.0;
	glow_bicubic_upscale = false;
	glow_high_quality = false;

	dof_blur_far_enabled = false;
	dof_blur_far_distance = 10;
	dof_blur_far_transition = 5;
	dof_blur_far_amount = 0.1;
	dof_blur_far_quality = DOF_BLUR_QUALITY_MEDIUM;

	dof_blur_near_enabled = false;
	dof_blur_near_distance = 2;
	dof_blur_near_transition = 1;
	dof_blur_near_amount = 0.1;
	dof_blur_near_quality = DOF_BLUR_QUALITY_MEDIUM;

	fog_enabled = false;
	fog_color = Color(0.5, 0.5, 0.5);
	fog_sun_color = Color(0.8, 0.8, 0.0);
	fog_sun_amount = 0;

	fog_depth_enabled = true;

	fog_depth_begin = 10;
	fog_depth_end = 100;
	fog_depth_curve = 1;

	fog_transmit_enabled = false;
	fog_transmit_curve = 1;

	fog_height_enabled = false;
	fog_height_min = 10;
	fog_height_max = 0;
	fog_height_curve = 1;

	volumetric_fog_enabled = false;
	volumetric_fog_density = 0.03;
	volumetric_fog_height_falloff = 0.1;
	volumetric_fog_height_ref = 0.0;
	volumetric_fog_height_relative = false;
	volumetric_fog_albedo = Color(1, 1, 1);
	volumetric_fog_ambient = Color(0.05, 0.06, 0.08);
	volumetric_fog_sun_energy = 1.0;
	volumetric_fog_anisotropy = 0.3;
	volumetric_fog_length = 160.0;
	volumetric_fog_extinction = 1.0;
	volumetric_fog_samples = 48;
	volumetric_fog_temporal_blend = 0.9;

	dynamic_gi_enabled = false;
	dynamic_gi_cascades = 4;
	dynamic_gi_voxel_size = 0.25;
	dynamic_gi_energy = 1.0;
	dynamic_gi_bounce_feedback = 0.8;
	dynamic_gi_normal_bias = 1.1;
	dynamic_gi_hysteresis = 0.95;
	dynamic_gi_rays = 64;
	dynamic_gi_reflection_occlusion = true;
	dynamic_gi_cull_mask = 0xFFFFF;
	dynamic_gi_debug = DYNAMIC_GI_DEBUG_DISABLED;
	ssgi_enabled = false;
	ssgi_radius = 4.0;
	ssgi_intensity = 1.0;
	ssgi_rays = 4;
	ssgi_steps = 12;
	ssgi_thickness = 0.5;
	ssgi_temporal_blend = 0.95;
	ssgi_half_resolution = true;

	// Defaults mirror the ExpHeightFog node's own field defaults.
	exp_height_fog_enabled = false;
	exp_height_fog_density = 0.04;
	exp_height_fog_falloff = 0.15;
	exp_height_fog_ref_height = 0.0;
	exp_height_fog_ref_height_relative = false;
	exp_height_fog_start_distance = 0.0;
	exp_height_fog_max_opacity = 1.0;
	exp_height_fog_inscatter = Color(0.55, 0.62, 0.72);
	exp_height_fog_directional_enabled = false;
	exp_height_fog_directional_color = Color(1.0, 0.9, 0.7);
	exp_height_fog_light_direction = Vector3(0, -1, 0);
	exp_height_fog_directional_exponent = 4.0;

	set_fog_color(Color(0.5, 0.6, 0.7));
	set_fog_sun_color(Color(1.0, 0.9, 0.7));
	_update_volumetric_fog();
	VS::get_singleton()->environment_set_volumetric_fog_height_relative(environment, volumetric_fog_height_relative);
	_update_dynamic_gi();
	_update_ssgi();
	_update_exp_height_fog();
}

Environment::~Environment() {
	VS::get_singleton()->free(environment);
}
