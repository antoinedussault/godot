/* clang-format off */
[vertex]

layout(location = 0) in highp vec4 vertex_attrib;
/* clang-format on */
layout(location = 4) in vec2 uv_in;

out vec2 uv_interp;

void main() {
	uv_interp = uv_in;
	gl_Position = vertex_attrib;
}

/* clang-format off */
[fragment]

#if !defined(GLES_OVER_GL)
precision highp float;
precision highp int;
#endif
/* clang-format on */

in vec2 uv_interp;

uniform sampler2D source_color; //texunit:0
uniform highp sampler2D source_aux; //texunit:1

// MODE_DOWNSAMPLE_MASK
uniform float bloom_threshold;
uniform float has_depth;
uniform float source_radius;

// MODE_RADIAL_BLUR
uniform vec2 light_uv;
uniform float blur_scale;
uniform float density;
uniform float decay;
uniform int num_samples;

// MODE_TEMPORAL
uniform vec2 uv_reproject;
uniform float blend;
uniform float motion_reject;

// MODE_COMPOSITE
uniform vec3 bloom_tint;
uniform float bloom_intensity;
uniform float occlusion_darkness;
uniform float weight;
uniform float effect_fade;

layout(location = 0) out vec4 frag_color;

float luma(vec3 c) {
	return dot(c, vec3(0.299, 0.587, 0.114));
}

// bvec select (no arithmetic) so NaN never survives, then clamp Inf away.
vec3 sanitize3(vec3 v) {
	v = mix(vec3(0.0), v, equal(v, v));
	return clamp(v, vec3(0.0), vec3(60000.0));
}

vec4 sanitize4(vec4 v) {
	v = mix(vec4(0.0), v, equal(v, v));
	return clamp(v, vec4(0.0), vec4(60000.0));
}

void main() {
#ifdef MODE_DOWNSAMPLE_MASK

	vec3 c = sanitize3(texture(source_color, uv_interp).rgb);
	float l = luma(c);

	// The shaft source is only bright pixels *near the sun's screen position*.
	// Without this, a bright sunset horizon on the far side of the frame gets
	// smeared into a coloured band by the radial blur.
	float near_sun = 1.0 - smoothstep(source_radius * 0.35, source_radius, distance(uv_interp, light_uv));

	// Wide soft knee at the threshold so the shaft ramps in gradually as the sun
	// brightens (a narrow knee pops, and any downstream glow HDR-bleed amplifies
	// the pop). Then compressed hard so it can't spike the glow threshold.
	float knee = smoothstep(bloom_threshold * 0.5, bloom_threshold * 1.6 + 0.001, l);
	vec3 bloom = c * (knee * near_sun);
	bloom = bloom / (vec3(1.0) + 0.35 * bloom); // asymptotes to ~2.85

	// Occlusion: a pixel is "lit" (not a blocker) if it is at the far plane OR
	// bright enough to be sky/sun. The brightness term keeps this working when
	// the sky is a mesh instead of the depth-far background.
	float lit_bright = smoothstep(bloom_threshold * 0.4, bloom_threshold * 1.2, l);
	float lit_depth = 0.0;
	if (has_depth > 0.5) {
		float d = texture(source_aux, uv_interp).r;
		lit_depth = smoothstep(0.99950, 0.99995, d);
	}
	float occ = max(lit_depth, lit_bright);

	frag_color = vec4(bloom, occ);

#endif

#ifdef MODE_RADIAL_BLUR

	vec2 to_light = uv_interp - light_uv;
	vec2 delta = to_light * (density * blur_scale / float(num_samples));

	// Interleaved-gradient-noise jitter of the start offset: much smoother than
	// a hash, so neighbouring pixels stay correlated and the tent blur / temporal
	// filter can resolve what's left.
	float ign = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));

	vec2 uv = uv_interp - delta * (0.5 + 0.5 * ign);
	vec3 rgb_accum = vec3(0.0);
	float a_accum = 0.0;
	float illum = 1.0;
	float wsum = 0.0;

	for (int i = 0; i < num_samples; i++) {
		uv -= delta;
		// Samples that march off screen contribute nothing (no edge smear when
		// the sun is near or past a screen border).
		float inb = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);
		vec4 s = texture(source_color, clamp(uv, vec2(0.0), vec2(1.0)));
		rgb_accum += s.rgb * illum * inb;
		a_accum += mix(1.0, s.a, inb) * illum; // off-screen counts as "lit sky"
		wsum += illum;
		illum *= decay;
	}

	// Both channels are a plain weighted AVERAGE of the samples: rgb ~ "how much
	// sun colour is along the ray toward the light" (bounded by the mask's soft
	// cap ~2.85), a ~ "how lit that ray is" in [0,1]. No accumulation blow-up.
	float inv = 1.0 / max(wsum, 0.0001);
	vec3 rgb_out = rgb_accum * inv * weight;
	float a_out = a_accum * inv;
	frag_color = sanitize4(vec4(rgb_out, a_out));

#endif

#ifdef MODE_SMOOTH

	// Small tent blur on the half-res shaft buffer to erase the jitter grain.
	vec2 px = blur_scale / vec2(textureSize(source_color, 0));
	vec4 acc = texture(source_color, uv_interp) * 4.0;
	acc += texture(source_color, uv_interp + vec2(px.x, 0.0)) * 2.0;
	acc += texture(source_color, uv_interp - vec2(px.x, 0.0)) * 2.0;
	acc += texture(source_color, uv_interp + vec2(0.0, px.y)) * 2.0;
	acc += texture(source_color, uv_interp - vec2(0.0, px.y)) * 2.0;
	acc += texture(source_color, uv_interp + px);
	acc += texture(source_color, uv_interp - px);
	acc += texture(source_color, uv_interp + vec2(px.x, -px.y));
	acc += texture(source_color, uv_interp + vec2(-px.x, px.y));
	frag_color = sanitize4(acc / 16.0);

#endif

#ifdef MODE_TEMPORAL

	vec4 cur = texture(source_color, uv_interp);
	vec4 hist = texture(source_aux, clamp(uv_interp + uv_reproject, vec2(0.0), vec2(1.0)));
	float b = blend * (1.0 - clamp(motion_reject, 0.0, 1.0));
	frag_color = sanitize4(mix(cur, hist, b));

#endif

#ifdef MODE_COMPOSITE

	vec4 scene_full = texture(source_color, uv_interp);
	vec3 scene = sanitize3(scene_full.rgb);
	vec4 shaft = sanitize4(texture(source_aux, uv_interp));

	float dist = distance(uv_interp, light_uv);

	// Spatial envelope: the effect is confined to a region around the sun and
	// rolls to exactly zero away from it — it must not lift the whole frame.
	float region = 1.0 - smoothstep(0.15, 1.1, dist);

	// Occlusion: gentle extra dimming toward a blocked sun, localized and floored.
	float blocked = 1.0 - clamp(shaft.a, 0.0, 1.0);
	float occ_factor = 1.0 - occlusion_darkness * region * blocked * 0.6;
	occ_factor = clamp(occ_factor, 0.5, 1.0);

	// Additive shaft light: gentle, spatially confined, HARD-capped so it can
	// only ever be a highlight — never a slab of clipped white.
	vec3 add = shaft.rgb * bloom_intensity * bloom_tint * region;
	add = min(add, vec3(2.5));

	vec3 shafted = scene * occ_factor + add;

	// Master fade: continuously return to the untouched scene as the sun leaves
	// the frame, so there is no step when the effect finally switches off.
	vec3 res = mix(scene, shafted, clamp(effect_fade, 0.0, 1.0));

	frag_color = vec4(sanitize3(res), scene_full.a);

#endif
}
