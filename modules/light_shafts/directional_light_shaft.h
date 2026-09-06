#ifndef DIRECTIONAL_LIGHT_SHAFT_H
#define DIRECTIONAL_LIGHT_SHAFT_H

#include "scene/3d/light.h"

// DirectionalLightShaft
//
// A DirectionalLight that also renders Unreal-style light shafts ("god rays").
//
// The effect is a real GLES3 post-process stage in the engine renderer: it
// downsamples the frame, builds a depth-based occlusion mask (near geometry =
// blocker, sky = lit) plus a bloom mask, radial-blurs both toward the light's
// on-screen position over several growing-radius passes, temporally filters the
// result against the previous frame, then composites:
//
//   scene *= lerp(1, occlusionMask, occlusion_darkness)   (near the sun)
//   scene += bloomTint * blurredBloom * bloom_intensity
//
// Mirrors Unreal's split into "Light Shaft Bloom" and "Light Shaft Occlusion".
// GLES3 only; a no-op on the GLES2 backend.
class DirectionalLightShaft : public DirectionalLight {
	GDCLASS(DirectionalLightShaft, DirectionalLight);

public:
	enum ShaftQuality {
		SHAFT_QUALITY_LOW = 16,
		SHAFT_QUALITY_MEDIUM = 32,
		SHAFT_QUALITY_HIGH = 64,
		SHAFT_QUALITY_ULTRA = 96,
	};

private:
	bool shaft_enabled = true;

	bool bloom_enabled = true;
	float bloom_intensity = 0.6f;
	float bloom_threshold = 1.0f;
	Color bloom_tint = Color(1.0f, 0.95f, 0.85f);

	bool occlusion_enabled = true;
	float occlusion_darkness = 0.35f;

	int shaft_samples = SHAFT_QUALITY_HIGH;
	float shaft_density = 0.7f;
	float shaft_decay = 0.9f;
	float shaft_weight = 1.0f;
	float temporal_blend = 0.0f;

	void _apply();

protected:
	static void _bind_methods();
	virtual void _validate_property(PropertyInfo &property) const;

public:
	void set_shaft_enabled(bool p_enabled);
	bool is_shaft_enabled() const;

	void set_bloom_enabled(bool p_enabled);
	bool is_bloom_enabled() const;
	void set_bloom_intensity(float p_intensity);
	float get_bloom_intensity() const;
	void set_bloom_threshold(float p_threshold);
	float get_bloom_threshold() const;
	void set_bloom_tint(const Color &p_tint);
	Color get_bloom_tint() const;

	void set_occlusion_enabled(bool p_enabled);
	bool is_occlusion_enabled() const;
	void set_occlusion_darkness(float p_darkness);
	float get_occlusion_darkness() const;

	void set_shaft_samples(int p_samples);
	int get_shaft_samples() const;
	void set_shaft_density(float p_density);
	float get_shaft_density() const;
	void set_shaft_decay(float p_decay);
	float get_shaft_decay() const;
	void set_shaft_weight(float p_weight);
	float get_shaft_weight() const;
	void set_temporal_blend(float p_blend);
	float get_temporal_blend() const;

	DirectionalLightShaft();
};

VARIANT_ENUM_CAST(DirectionalLightShaft::ShaftQuality);

#endif // DIRECTIONAL_LIGHT_SHAFT_H
