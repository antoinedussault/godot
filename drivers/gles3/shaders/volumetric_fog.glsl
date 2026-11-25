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
uniform highp sampler2D source_depth; //texunit:3

uniform highp mat4 inv_projection;
uniform highp mat4 cam_matrix;
uniform highp mat4 prev_view_projection;

uniform highp mat4 shadow_matrix1;
uniform highp mat4 shadow_matrix2;
uniform highp mat4 shadow_matrix3;
uniform highp mat4 shadow_matrix4;
uniform highp vec4 shadow_split_offsets;
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
uniform float cam_far;
uniform float extinction_scale;
uniform int num_samples;

uniform float blend;
uniform float motion_reject;

uniform vec2 blur_direction;

layout(location = 0) out vec4 frag_color;

vec3 sanitize3(vec3 v) {
	v = mix(vec3(0.0), v, equal(v, v));
	return clamp(v, vec3(0.0), vec3(60000.0));
}

vec4 sanitize4(vec4 v) {
	v = mix(vec4(0.0), v, equal(v, v));
	return clamp(v, vec4(0.0), vec4(60000.0));
}

// Hyperbolic [0,1] depth-buffer value -> positive eye-space distance.
float linear_depth(float d) {
	float ndc = d * 2.0 - 1.0;
	return (2.0 * cam_near * cam_far) / (cam_far + cam_near - ndc * (cam_far - cam_near));
}

// Henyey-Greenstein phase function.
float hg_phase(float cos_theta, float g) {
	float g2 = g * g;
	float denom = 1.0 + g2 - 2.0 * g * cos_theta;
	return (1.0 - g2) / (12.566370614 * pow(max(denom, 1e-4), 1.5));
}

// Sun visibility of a view-space march point. Selects the PSSM cascade that
// actually contains the point by its view-space depth, exactly like the scene
// shader -- using a single cascade for the whole ray makes near-field points
// fall outside that cascade's frustum, so they flip between lit and shadowed as
// the camera moves (fog "pops" in and out).
float sample_sun_shadow(vec3 view_pos) {
	if (shadow_splits <= 0) {
		return 1.0;
	}

	float depth_z = -view_pos.z;
	float far_split = (shadow_splits >= 4) ? shadow_split_offsets.w : ((shadow_splits >= 2) ? shadow_split_offsets.y : shadow_split_offsets.x);

	// Past the shadowed range there is no data: treat as lit, faded in over the
	// last slice so the fog does not hard-cut where the cascades end.
	if (depth_z >= far_split) {
		return 1.0;
	}

	highp mat4 sm;
	if (shadow_splits >= 4 && depth_z >= shadow_split_offsets.z) {
		sm = shadow_matrix4;
	} else if (shadow_splits >= 3 && depth_z >= shadow_split_offsets.y) {
		sm = shadow_matrix3;
	} else if (shadow_splits >= 2 && depth_z >= shadow_split_offsets.x) {
		sm = shadow_matrix2;
	} else {
		sm = shadow_matrix1;
	}

	highp vec4 splane = sm * vec4(view_pos, 1.0);
	vec3 coord = splane.xyz / splane.w;
	if (coord.z < 0.0 || coord.z > 1.0) {
		return 1.0;
	}
	coord.xy = clamp(coord.xy, vec2(0.0), vec2(1.0));

	float lit = texture(directional_shadow, coord);
	// Fade the shadow out over the last 15% of the range.
	float fade = clamp((far_split - depth_z) / (0.15 * far_split), 0.0, 1.0);
	return mix(1.0, lit, fade);
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

	int steps = clamp(num_samples, 8, 512);
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

	// Reproject this pixel into the previous frame using its world position, so
	// the history follows the scene as the camera moves instead of smearing.
	float raw_depth = texture(source_depth, uv_interp).r;
	vec3 ndc = vec3(uv_interp * 2.0 - 1.0, raw_depth * 2.0 - 1.0);
	vec4 vp = inv_projection * vec4(ndc, 1.0);
	vec3 view_pos = vp.xyz / vp.w;
	vec4 world = cam_matrix * vec4(view_pos, 1.0);
	vec4 pc = prev_view_projection * world;
	vec2 prev_uv = (pc.xy / pc.w) * 0.5 + 0.5;

	float b = blend;
	if (pc.w <= 0.0 || prev_uv.x < 0.0 || prev_uv.x > 1.0 || prev_uv.y < 0.0 || prev_uv.y > 1.0) {
		b = 0.0; // disoccluded / off the previous frame
	}
	b *= (1.0 - clamp(motion_reject, 0.0, 1.0));

	vec4 hist = texture(source_b, clamp(prev_uv, vec2(0.0), vec2(1.0)));
	frag_color = sanitize4(mix(cur, hist, b));

#endif

#ifdef MODE_BLUR

	// Separable Gaussian on the half-res fog, one axis per pass. Fog is
	// low-frequency so a plain blur (not bilateral) is enough to trade raymarch
	// step count for smoothness -- the analog of Godot 4's "Use Filter".
	vec4 c = texture(source_a, uv_interp) * 0.375;
	c += texture(source_a, uv_interp + blur_direction) * 0.25;
	c += texture(source_a, uv_interp - blur_direction) * 0.25;
	c += texture(source_a, uv_interp + 2.0 * blur_direction) * 0.0625;
	c += texture(source_a, uv_interp - 2.0 * blur_direction) * 0.0625;
	frag_color = sanitize4(c);

#endif

#ifdef MODE_COMPOSITE

	vec4 scene = texture(source_a, uv_interp);

	// Depth-aware ("bilateral") upsample of the half-res fog. A plain bilinear
	// fetch blends the bright long-ray in-scatter of a background texel onto the
	// foreground mesh across a silhouette -> a lit halo hugging every edge. Weight
	// the four half-res taps by how well their depth matches this full-res pixel.
	vec2 half_size = vec2(textureSize(source_b, 0));
	vec2 half_texel = 1.0 / half_size;

	float center_lin = linear_depth(texture(source_depth, uv_interp).r);

	vec2 huv = uv_interp * half_size - 0.5;
	vec2 hfr = fract(huv);
	vec2 hbase = (floor(huv) + 0.5) * half_texel;

	vec4 fog = vec4(0.0);
	float wsum = 0.0;
	float nearest_dd = 1e20;
	vec4 nearest_fog = vec4(0.0);
	for (int i = 0; i < 4; i++) {
		vec2 o = vec2(float(i & 1), float(i >> 1));
		vec2 tap = hbase + o * half_texel;
		float bw = mix(1.0 - hfr.x, hfr.x, o.x) * mix(1.0 - hfr.y, hfr.y, o.y);
		float tap_lin = linear_depth(texture(source_depth, tap).r);
		vec4 tap_fog = sanitize4(texture(source_b, tap));

		// Relative depth mismatch: ~0 across a continuous surface, huge across a
		// silhouette. The sharp exponential blends within a few percent of depth
		// and hard-rejects everything past it, so the bright background in-scatter
		// never bleeds onto a foreground edge.
		float rel = abs(tap_lin - center_lin) / max(center_lin, 0.1);
		float w = bw * exp(-rel * 24.0);
		fog += tap_fog * w;
		wsum += w;

		float dd = abs(tap_lin - center_lin);
		if (dd < nearest_dd) {
			nearest_dd = dd;
			nearest_fog = tap_fog;
		}
	}
	// If every tap is a poor depth match (thin feature between the half-res
	// texels), fall back to the single closest one rather than a bad blend.
	fog = (wsum > 0.0001) ? fog / wsum : nearest_fog;

	// fog.rgb is pre-multiplied in-scatter, fog.a is transmittance.
	vec3 res = scene.rgb * fog.a + fog.rgb;
	frag_color = vec4(sanitize3(res), scene.a);

#endif
}
