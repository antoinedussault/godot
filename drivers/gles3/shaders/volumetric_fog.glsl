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

// MODE_RAYMARCH: source_a is scene depth, directional_shadow is the sun atlas.
// MODE_TEMPORAL / MODE_COMPOSITE: source_a and source_b are colour buffers.
uniform highp sampler2D source_a; //texunit:0
uniform highp sampler2D source_b; //texunit:1
uniform highp sampler2DShadow directional_shadow; //texunit:2

uniform highp mat4 inv_projection;
uniform highp mat4 cam_matrix;

uniform highp mat4 shadow_matrix1;
uniform highp vec4 light_clamp;
uniform int shadow_splits;

uniform vec3 sun_direction;
uniform vec3 sun_color;
uniform vec3 fog_albedo;
uniform vec3 ambient_color;

uniform float fog_density;
uniform float height_falloff;
uniform float height_ref;
uniform float anisotropy;
uniform float fog_length;
uniform float cam_near;
uniform float extinction_scale;
uniform int num_samples;

uniform vec2 uv_reproject;
uniform float blend;
uniform float motion_reject;

layout(location = 0) out vec4 frag_color;

vec3 sanitize3(vec3 v) {
	v = mix(vec3(0.0), v, equal(v, v));
	return clamp(v, vec3(0.0), vec3(60000.0));
}

vec4 sanitize4(vec4 v) {
	v = mix(vec4(0.0), v, equal(v, v));
	return clamp(v, vec4(0.0), vec4(60000.0));
}

// Henyey-Greenstein phase function.
float hg_phase(float cos_theta, float g) {
	float g2 = g * g;
	float denom = 1.0 + g2 - 2.0 * g * cos_theta;
	return (1.0 - g2) / (12.566370614 * pow(max(denom, 1e-4), 1.5));
}

// Sun visibility of a view-space point. The fog march only uses the last (widest)
// cascade so there are no cascade seams across the shafts.
float sample_sun_shadow(vec3 view_pos) {
	if (shadow_splits <= 0) {
		return 1.0;
	}

	highp vec4 splane = shadow_matrix1 * vec4(view_pos, 1.0);
	vec3 coord = splane.xyz / splane.w;

	if (coord.x < light_clamp.x || coord.y < light_clamp.y ||
			coord.x > light_clamp.x + light_clamp.z || coord.y > light_clamp.y + light_clamp.w ||
			coord.z < 0.0 || coord.z > 1.0) {
		return 1.0;
	}
	return texture(directional_shadow, coord);
}

void main() {
#ifdef MODE_RAYMARCH

	float raw_depth = texture(source_a, uv_interp).r;

	// View-space position of this pixel's surface.
	vec3 ndc = vec3(uv_interp * 2.0 - 1.0, raw_depth * 2.0 - 1.0);
	vec4 vp = inv_projection * vec4(ndc, 1.0);
	vec3 view_pos = vp.xyz / vp.w;

	float frag_dist = length(view_pos);
	vec3 ray_dir = view_pos / max(frag_dist, 1e-4);

	// Sky pixels (raw_depth ~ 1.0) march to the fog range limit.
	float end_dist = min(frag_dist, fog_length);
	float start_dist = max(cam_near, 0.05);

	int steps = clamp(num_samples, 8, 128);
	float seg = (end_dist - start_dist) / float(steps);

	float phase = hg_phase(dot(ray_dir, normalize(sun_direction)), clamp(anisotropy, -0.95, 0.95));

	// Interleaved-gradient-noise start offset so the low step count dithers
	// instead of banding; the temporal pass resolves the rest.
	float ign = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));

	vec3 inscatter = vec3(0.0);
	float transmittance = 1.0;

	if (seg > 0.0) {
		for (int i = 0; i < steps; i++) {
			float t = start_dist + seg * (float(i) + ign);
			vec3 p = ray_dir * t;

			float world_y = (cam_matrix * vec4(p, 1.0)).y;
			float dens = fog_density * exp(-height_falloff * (world_y - height_ref));
			dens = clamp(dens, 0.0, 16.0);
			if (dens <= 0.00001) {
				continue;
			}

			// Non-absorbing fog: scattering coefficient equals extinction.
			float sigma_t = dens * extinction_scale;
			float shadow = sample_sun_shadow(p);

			vec3 src = (sun_color * (shadow * phase) + ambient_color) * fog_albedo;

			// Analytic single-scattering integral over the segment.
			float t_step = exp(-sigma_t * seg);
			vec3 seg_inscatter = sigma_t * src * (1.0 - t_step) / max(sigma_t, 1e-4);
			inscatter += transmittance * seg_inscatter;
			transmittance *= t_step;

			if (transmittance < 0.003) {
				break;
			}
		}
	}

	frag_color = sanitize4(vec4(inscatter, transmittance));

#endif

#ifdef MODE_TEMPORAL

	vec4 cur = texture(source_a, uv_interp);
	vec4 hist = texture(source_b, clamp(uv_interp + uv_reproject, vec2(0.0), vec2(1.0)));
	float b = blend * (1.0 - clamp(motion_reject, 0.0, 1.0));
	frag_color = sanitize4(mix(cur, hist, b));

#endif

#ifdef MODE_COMPOSITE

	vec4 scene = texture(source_a, uv_interp);

	// Half-res fog, bilinearly upsampled. Fog is low-frequency so silhouette
	// bleed stays sub-pixel and a bilateral tap is not worth it here.
	vec4 fog = sanitize4(texture(source_b, uv_interp));

	// fog.rgb is pre-multiplied in-scatter, fog.a is transmittance.
	vec3 res = scene.rgb * fog.a + fog.rgb;
	frag_color = vec4(sanitize3(res), scene.a);

#endif
}
