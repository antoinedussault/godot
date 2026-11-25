// Shared by scene.glsl and every dynamic GI program. This file goes through the
// legacy shader builder, so conditionals must be written with #if defined().

#define GI_RES 64
#define GI_PROBE_RES 16
#define GI_PROBE_STEP 4
#define GI_PROBES_PER_CASCADE 4096
#define GI_MAX_CASCADES 6

layout(std140) uniform DynamicGIData { // ubo:8
	highp ivec4 gi_config; // cascade count (0 disables), frame, reflection occlusion, rays per probe / 64
	highp vec4 gi_params; // energy, normal bias (voxels), bounce feedback, hysteresis
	highp vec4 gi_ambient; // ambient color * energy, sky contribution
	highp vec4 gi_sky; // sky energy, sky mode, light count, physical attenuation
	highp mat4 gi_sky_xform;
	highp ivec4 gi_cascade_origin[GI_MAX_CASCADES];
	highp vec4 gi_cascade_size[GI_MAX_CASCADES];
};

struct GIProbe {
	highp ivec4 coord; // world probe coordinate, state (1 = usable)
	highp vec4 offset; // relocation offset, age
};

layout(std430, binding = 3) GI_PROBE_ACCESS buffer GIProbeBuffer {
	GIProbe gi_probes[];
};

uniform highp sampler2DArray gi_irradiance; // texunit:-15
uniform highp sampler2DArray gi_visibility; // texunit:-16

vec2 gi_oct_encode(vec3 v) {
	v /= abs(v.x) + abs(v.y) + abs(v.z);
	vec2 s = vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
	return v.z >= 0.0 ? v.xy : (1.0 - abs(v.yx)) * s;
}

vec3 gi_oct_decode(vec2 e) {
	vec3 v = vec3(e, 1.0 - abs(e.x) - abs(e.y));
	if (v.z < 0.0) {
		vec2 s = vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
		v.xy = (1.0 - abs(v.yx)) * s;
	}
	return normalize(v);
}

int gi_probe_index(int c, ivec3 slot) {
	return c * GI_PROBES_PER_CASCADE + slot.x + slot.y * GI_PROBE_RES + slot.z * GI_PROBE_RES * GI_PROBE_RES;
}

vec2 gi_atlas_uv(ivec3 slot, vec2 oct, float tile) {
	vec2 base = vec2(float(slot.x + slot.z * GI_PROBE_RES), float(slot.y)) * tile;
	vec2 px = base + 1.0 + (oct * 0.5 + 0.5) * (tile - 2.0);
	return px / (vec2(float(GI_PROBE_RES * GI_PROBE_RES), float(GI_PROBE_RES)) * tile);
}

// Two lookups (diffuse normal and a second direction) share one set of weights.
float gi_sample_cascade(int c, highp vec3 pos, vec3 n, vec3 dir2, out vec3 r_irr, out vec3 r_irr2) {
	r_irr = vec3(0.0);
	r_irr2 = vec3(0.0);

	highp float spacing = gi_cascade_size[c].x * float(GI_PROBE_STEP);
	highp vec3 bp = pos + n * gi_params.y * gi_cascade_size[c].x;
	highp vec3 g = bp / spacing - 0.5;
	ivec3 org = gi_cascade_origin[c].xyz >> 2;
	vec3 edge = min(g - vec3(org), vec3(org + ivec3(GI_PROBE_RES - 1)) - g);
	float d_edge = min(edge.x, min(edge.y, edge.z));
	if (d_edge <= 0.0) {
		return 0.0;
	}
	float fade = clamp(d_edge / 1.5, 0.0, 1.0);

	ivec3 base = ivec3(floor(g));
	vec3 f = g - vec3(base);
	float wsum = 0.0;

	for (int i = 0; i < 8; i++) {
		ivec3 o = ivec3(i & 1, (i >> 1) & 1, i >> 2);
		ivec3 pc = base + o;
		ivec3 slot = pc & ivec3(GI_PROBE_RES - 1);
		int idx = gi_probe_index(c, slot);
		highp ivec4 coord = gi_probes[idx].coord;
		if (coord.w != 1 || coord.xyz != pc) {
			continue;
		}
		highp vec3 ppos = (vec3(pc) + 0.5) * spacing + gi_probes[idx].offset.xyz;

		vec3 to_probe = ppos - pos;
		float wrap = (dot(to_probe / max(length(to_probe), 1e-4), n) + 1.0) * 0.5;
		float w = wrap * wrap + 0.05;

		highp vec3 from_probe = bp - ppos;
		highp float dist = length(from_probe);
		vec2 oct = gi_oct_encode(from_probe / max(dist, 1e-4) + vec3(0.0, 1e-5, 0.0));
		highp vec2 m = textureLod(gi_visibility, vec3(gi_atlas_uv(slot, oct, 16.0), float(c)), 0.0).rg;
		if (dist > m.x) {
			highp float variance = abs(m.x * m.x - m.y) + 1e-4;
			highp float dd = dist - m.x;
			highp float ch = variance / (variance + dd * dd);
			w *= ch * ch * ch;
		}
		w = max(w, 1e-6);
		if (w < 0.2) {
			w *= w * w / (0.2 * 0.2);
		}

		vec3 t = mix(1.0 - f, f, vec3(o));
		w *= (t.x + 1e-3) * (t.y + 1e-3) * (t.z + 1e-3);

		r_irr += w * textureLod(gi_irradiance, vec3(gi_atlas_uv(slot, gi_oct_encode(n), 8.0), float(c)), 0.0).rgb;
		r_irr2 += w * textureLod(gi_irradiance, vec3(gi_atlas_uv(slot, gi_oct_encode(dir2), 8.0), float(c)), 0.0).rgb;
		wsum += w;
	}

	if (wsum < 1e-12) {
		return 0.0;
	}
	r_irr /= wsum;
	r_irr2 /= wsum;
	// Stored as square root, for perceptually smoother blending.
	r_irr *= r_irr;
	r_irr2 *= r_irr2;
	return fade;
}

float gi_sample(highp vec3 pos, vec3 n, vec3 dir2, int first, out vec3 r_irr, out vec3 r_irr2) {
	r_irr = vec3(0.0);
	r_irr2 = vec3(0.0);
	float total = 0.0;
	for (int c = first; c < gi_config.x; c++) {
		vec3 a;
		vec3 b;
		float w = gi_sample_cascade(c, pos, n, dir2, a, b) * (1.0 - total);
		r_irr += a * w;
		r_irr2 += b * w;
		total += w;
		if (total > 0.995) {
			break;
		}
	}
	if (total > 0.0) {
		r_irr /= total;
		r_irr2 /= total;
	}
	return total;
}
