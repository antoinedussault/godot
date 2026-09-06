// foliage_dither.shader
// Material override for foliage LOD MultiMeshes. The per-instance dissolve
// value is delivered through INSTANCE_CUSTOM (the MultiMesh custom-data buffer),
// NOT a uniform — that is what lets thousands of instances each fade
// independently while staying in one instanced draw call.
//
// CellFoliage packs per-instance data into the MultiMesh custom data as:
//   INSTANCE_CUSTOM.r = dither coverage (0 = fully dithered out, 1 = visible)
//   INSTANCE_CUSTOM.g = reserved (e.g. per-instance tint/seed)
//   INSTANCE_CUSTOM.b = reserved
//   INSTANCE_CUSTOM.a = reserved
//
// Assign this as the FoliageType.material_override (or per mesh). Set albedo_tex
// to the foliage atlas. cull_disabled suits cards/leaves; switch to cull_back
// for solid trunks.

shader_type spatial;
render_mode cull_disabled, diffuse_burley, specular_schlick_ggx;

uniform sampler2D albedo_tex : hint_albedo;
uniform vec4 albedo_color : hint_color = vec4(1.0);
uniform float roughness : hint_range(0.0, 1.0) = 1.0;
uniform float alpha_scissor_threshold : hint_range(0.0, 1.0) = 0.5;
uniform bool use_albedo_alpha = true;

// Dither cell size in pixels and optional temporal jitter. These are global
// (uniform) because they are not per-instance; only the coverage is.
uniform float dither_scale : hint_range(1.0, 8.0) = 1.0;
uniform bool dither_animated = false;

// Pass-through of the per-instance coverage from vertex to fragment stage.
varying float v_coverage;

// Interleaved Gradient Noise: one closed-form per-pixel threshold. No array
// indexing, no texture, GLES3-safe. Genuinely per-pixel so coverage 0.5 keeps
// ~half the pixels, dissolving smoothly.
float ign(vec2 p) {
	return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void vertex() {
	// INSTANCE_CUSTOM is per-instance MultiMesh custom data. r holds coverage.
	v_coverage = INSTANCE_CUSTOM.r;
}

void fragment() {
	vec4 tex = texture(albedo_tex, UV) * albedo_color;
	ALBEDO = tex.rgb;
	ROUGHNESS = roughness;

	// Foliage alpha cutout first.
	if (use_albedo_alpha && tex.a < alpha_scissor_threshold) {
		discard;
	}

	// Per-instance LOD dissolve.
	vec2 p = FRAGCOORD.xy / max(dither_scale, 1.0);
	if (dither_animated) {
		p += vec2(fract(TIME * 0.61803399) * 137.0);
	}
	float th = ign(p);
	if (v_coverage <= th) {
		discard;
	}
}
