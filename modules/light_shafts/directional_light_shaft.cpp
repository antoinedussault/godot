#include "directional_light_shaft.h"

void DirectionalLightShaft::_apply() {
	set_godrays_enabled(shaft_enabled);
	set_godrays_intensity(bloom_enabled ? bloom_intensity : 0.0f);
	set_godrays_threshold(bloom_threshold);
	set_godrays_tint(bloom_tint);
	set_godrays_occlusion(occlusion_enabled ? occlusion_darkness : 0.0f);
	set_godrays_samples(shaft_samples);
	set_godrays_density(shaft_density);
	set_godrays_decay(shaft_decay);
	set_godrays_weight(shaft_weight);
	set_godrays_temporal_blend(temporal_blend);
}

void DirectionalLightShaft::set_shaft_enabled(bool p_enabled) {
	shaft_enabled = p_enabled;
	_apply();
}
bool DirectionalLightShaft::is_shaft_enabled() const {
	return shaft_enabled;
}

void DirectionalLightShaft::set_bloom_enabled(bool p_enabled) {
	bloom_enabled = p_enabled;
	_apply();
	_change_notify();
}
bool DirectionalLightShaft::is_bloom_enabled() const {
	return bloom_enabled;
}

void DirectionalLightShaft::set_bloom_intensity(float p_intensity) {
	bloom_intensity = MAX(p_intensity, 0.0f);
	_apply();
}
float DirectionalLightShaft::get_bloom_intensity() const {
	return bloom_intensity;
}

void DirectionalLightShaft::set_bloom_threshold(float p_threshold) {
	bloom_threshold = p_threshold;
	_apply();
}
float DirectionalLightShaft::get_bloom_threshold() const {
	return bloom_threshold;
}

void DirectionalLightShaft::set_bloom_tint(const Color &p_tint) {
	bloom_tint = p_tint;
	_apply();
}
Color DirectionalLightShaft::get_bloom_tint() const {
	return bloom_tint;
}

void DirectionalLightShaft::set_occlusion_enabled(bool p_enabled) {
	occlusion_enabled = p_enabled;
	_apply();
	_change_notify();
}
bool DirectionalLightShaft::is_occlusion_enabled() const {
	return occlusion_enabled;
}

void DirectionalLightShaft::set_occlusion_darkness(float p_darkness) {
	occlusion_darkness = CLAMP(p_darkness, 0.0f, 1.0f);
	_apply();
}
float DirectionalLightShaft::get_occlusion_darkness() const {
	return occlusion_darkness;
}

void DirectionalLightShaft::set_shaft_samples(int p_samples) {
	shaft_samples = CLAMP(p_samples, 4, 128);
	_apply();
}
int DirectionalLightShaft::get_shaft_samples() const {
	return shaft_samples;
}

void DirectionalLightShaft::set_shaft_density(float p_density) {
	shaft_density = CLAMP(p_density, 0.01f, 1.0f);
	_apply();
}
float DirectionalLightShaft::get_shaft_density() const {
	return shaft_density;
}

void DirectionalLightShaft::set_shaft_decay(float p_decay) {
	shaft_decay = CLAMP(p_decay, 0.0f, 1.0f);
	_apply();
}
float DirectionalLightShaft::get_shaft_decay() const {
	return shaft_decay;
}

void DirectionalLightShaft::set_shaft_weight(float p_weight) {
	shaft_weight = MAX(p_weight, 0.0f);
	_apply();
}
float DirectionalLightShaft::get_shaft_weight() const {
	return shaft_weight;
}

void DirectionalLightShaft::set_temporal_blend(float p_blend) {
	temporal_blend = CLAMP(p_blend, 0.0f, 0.95f);
	_apply();
}
float DirectionalLightShaft::get_temporal_blend() const {
	return temporal_blend;
}

void DirectionalLightShaft::_validate_property(PropertyInfo &property) const {
	if (!bloom_enabled && (property.name == "bloom_intensity" || property.name == "bloom_threshold" || property.name == "bloom_tint")) {
		property.usage = PROPERTY_USAGE_NOEDITOR;
	}
	if (!occlusion_enabled && property.name == "occlusion_darkness") {
		property.usage = PROPERTY_USAGE_NOEDITOR;
	}
	// This node predates DirectionalLight's own god rays and drives them through
	// its older properties; showing or saving both would let them fight.
	if (property.name.begins_with("godrays_")) {
		property.usage = 0;
		return;
	}
	DirectionalLight::_validate_property(property);
}

void DirectionalLightShaft::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_shaft_enabled", "enabled"), &DirectionalLightShaft::set_shaft_enabled);
	ClassDB::bind_method(D_METHOD("is_shaft_enabled"), &DirectionalLightShaft::is_shaft_enabled);

	ClassDB::bind_method(D_METHOD("set_bloom_enabled", "enabled"), &DirectionalLightShaft::set_bloom_enabled);
	ClassDB::bind_method(D_METHOD("is_bloom_enabled"), &DirectionalLightShaft::is_bloom_enabled);
	ClassDB::bind_method(D_METHOD("set_bloom_intensity", "intensity"), &DirectionalLightShaft::set_bloom_intensity);
	ClassDB::bind_method(D_METHOD("get_bloom_intensity"), &DirectionalLightShaft::get_bloom_intensity);
	ClassDB::bind_method(D_METHOD("set_bloom_threshold", "threshold"), &DirectionalLightShaft::set_bloom_threshold);
	ClassDB::bind_method(D_METHOD("get_bloom_threshold"), &DirectionalLightShaft::get_bloom_threshold);
	ClassDB::bind_method(D_METHOD("set_bloom_tint", "tint"), &DirectionalLightShaft::set_bloom_tint);
	ClassDB::bind_method(D_METHOD("get_bloom_tint"), &DirectionalLightShaft::get_bloom_tint);

	ClassDB::bind_method(D_METHOD("set_occlusion_enabled", "enabled"), &DirectionalLightShaft::set_occlusion_enabled);
	ClassDB::bind_method(D_METHOD("is_occlusion_enabled"), &DirectionalLightShaft::is_occlusion_enabled);
	ClassDB::bind_method(D_METHOD("set_occlusion_darkness", "darkness"), &DirectionalLightShaft::set_occlusion_darkness);
	ClassDB::bind_method(D_METHOD("get_occlusion_darkness"), &DirectionalLightShaft::get_occlusion_darkness);

	ClassDB::bind_method(D_METHOD("set_shaft_samples", "samples"), &DirectionalLightShaft::set_shaft_samples);
	ClassDB::bind_method(D_METHOD("get_shaft_samples"), &DirectionalLightShaft::get_shaft_samples);
	ClassDB::bind_method(D_METHOD("set_shaft_density", "density"), &DirectionalLightShaft::set_shaft_density);
	ClassDB::bind_method(D_METHOD("get_shaft_density"), &DirectionalLightShaft::get_shaft_density);
	ClassDB::bind_method(D_METHOD("set_shaft_decay", "decay"), &DirectionalLightShaft::set_shaft_decay);
	ClassDB::bind_method(D_METHOD("get_shaft_decay"), &DirectionalLightShaft::get_shaft_decay);
	ClassDB::bind_method(D_METHOD("set_shaft_weight", "weight"), &DirectionalLightShaft::set_shaft_weight);
	ClassDB::bind_method(D_METHOD("get_shaft_weight"), &DirectionalLightShaft::get_shaft_weight);
	ClassDB::bind_method(D_METHOD("set_temporal_blend", "blend"), &DirectionalLightShaft::set_temporal_blend);
	ClassDB::bind_method(D_METHOD("get_temporal_blend"), &DirectionalLightShaft::get_temporal_blend);

	ADD_GROUP("Light Shafts", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shaft_enabled"), "set_shaft_enabled", "is_shaft_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shaft_samples", PROPERTY_HINT_RANGE, "4,128,1"), "set_shaft_samples", "get_shaft_samples");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "shaft_density", PROPERTY_HINT_RANGE, "0.01,1.0,0.001"), "set_shaft_density", "get_shaft_density");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "shaft_decay", PROPERTY_HINT_RANGE, "0.0,1.0,0.001"), "set_shaft_decay", "get_shaft_decay");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "shaft_weight", PROPERTY_HINT_RANGE, "0.0,4.0,0.01"), "set_shaft_weight", "get_shaft_weight");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "temporal_blend", PROPERTY_HINT_RANGE, "0.0,0.95,0.01"), "set_temporal_blend", "get_temporal_blend");

	ADD_GROUP("Light Shaft Bloom", "bloom_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bloom_enabled"), "set_bloom_enabled", "is_bloom_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "bloom_intensity", PROPERTY_HINT_RANGE, "0.0,8.0,0.01"), "set_bloom_intensity", "get_bloom_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "bloom_threshold", PROPERTY_HINT_RANGE, "0.0,4.0,0.01"), "set_bloom_threshold", "get_bloom_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "bloom_tint", PROPERTY_HINT_COLOR_NO_ALPHA), "set_bloom_tint", "get_bloom_tint");

	ADD_GROUP("Light Shaft Occlusion", "occlusion_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "occlusion_enabled"), "set_occlusion_enabled", "is_occlusion_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "occlusion_darkness", PROPERTY_HINT_RANGE, "0.0,1.0,0.001"), "set_occlusion_darkness", "get_occlusion_darkness");

	BIND_ENUM_CONSTANT(SHAFT_QUALITY_LOW);
	BIND_ENUM_CONSTANT(SHAFT_QUALITY_MEDIUM);
	BIND_ENUM_CONSTANT(SHAFT_QUALITY_HIGH);
	BIND_ENUM_CONSTANT(SHAFT_QUALITY_ULTRA);
}

DirectionalLightShaft::DirectionalLightShaft() {
	_apply();
}
