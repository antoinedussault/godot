uniform usampler3D gi_tex_albedo;
uniform usampler3D gi_tex_bricks;
uniform sampler3D gi_tex_radiance;
uniform usampler3D gi_tex_facing;

struct GIHit {
	float t;
	ivec3 cell;
	int cascade;
};

ivec3 gi_texel(ivec3 cell, int c) {
	return (cell & ivec3(GI_RES - 1)) + ivec3(0, 0, c * GI_RES);
}

bool gi_solid(ivec3 cell, int c) {
	return texelFetch(gi_tex_albedo, gi_texel(cell, c), 0).r != 0u;
}

// Packed as G, R, B from the high bits down (see RENDER_GI_VOXELS in scene.glsl).
vec3 gi_unpack_albedo(uint v) {
	return vec3(float((v >> 10u) & 1023u), float((v >> 20u) & 1023u), float(v & 1023u)) / 1023.0;
}

vec3 gi_unpack_emission(uint v) {
	float s = exp2(float(v >> 27u) - 15.0) / 511.0;
	return vec3(float((v >> 18u) & 511u), float((v >> 9u) & 511u), float(v & 511u)) * s;
}

// Two-level walk (4x4x4 bricks, then voxels) that moves out to the next cascade
// when a ray leaves the current one.
bool gi_trace(vec3 ro, vec3 rd, int first, float tmax, out GIHit hit) {
	hit.t = tmax;
	hit.cell = ivec3(0);
	hit.cascade = 0;
	rd = mix(rd, vec3(1e-5), lessThan(abs(rd), vec3(1e-5)));
	vec3 inv = 1.0 / rd;
	vec3 side = step(vec3(0.0), rd);
	float t = 0.0;
	for (int c = first; c < gi_config.x; c++) {
		float vs = gi_cascade_size[c].x;
		ivec3 org = gi_cascade_origin[c].xyz;
		float nudge = vs * 2e-3;
		for (int i = 0; i < 112; i++) {
			vec3 p = ro + rd * (t + nudge);
			ivec3 cell = ivec3(floor(p / vs));
			ivec3 rel = cell - org;
			if (any(lessThan(rel, ivec3(0))) || any(greaterThanEqual(rel, ivec3(GI_RES)))) {
				break;
			}
			float size = vs;
			if (texelFetch(gi_tex_bricks, ((cell >> 2) & ivec3(15)) + ivec3(0, 0, c * 16), 0).r == 0u) {
				size = vs * 4.0;
			} else if (gi_solid(cell, c)) {
				hit.t = t;
				hit.cell = cell;
				hit.cascade = c;
				return t < tmax;
			}
			vec3 tv = ((floor(p / size) + side) * size - ro) * inv;
			t = max(t + nudge, min(tv.x, min(tv.y, tv.z)));
			if (t > tmax) {
				return false;
			}
		}
	}
	return false;
}

vec3 gi_face_radiance(ivec3 cell, int c, int face) {
	return texelFetch(gi_tex_radiance, gi_texel(cell, c) + ivec3(0, face * GI_RES, 0), 0).rgb;
}

bool gi_face_open(ivec3 cell, int c, int face, uint facing) {
	if ((facing & (1u << uint(face))) == 0u) {
		return false;
	}
	ivec3 n = ivec3(0);
	n[face >> 1] = 1 - 2 * (face & 1);
	ivec3 nb = cell + n;
	ivec3 rel = nb - gi_cascade_origin[c].xyz;
	bool inside = all(greaterThanEqual(rel, ivec3(0))) && all(lessThan(rel, ivec3(GI_RES)));
	return !(inside && gi_solid(nb, c));
}

// Blends only faces that are open and look at the ray. One-voxel walls carry
// different light on each side, so nothing may be averaged across them. Returns
// false for hits on the inside of a surface.
bool gi_hit_radiance(GIHit h, vec3 rd, out vec3 r_light) {
	uint facing = texelFetch(gi_tex_facing, gi_texel(h.cell, h.cascade), 0).r;
	r_light = vec3(0.0);
	float wsum = 0.0;
	for (int a = 0; a < 3; a++) {
		float w = rd[a] * rd[a];
		int face = a * 2 + (rd[a] < 0.0 ? 0 : 1);
		if (w > 1e-6 && gi_face_open(h.cell, h.cascade, face, facing)) {
			r_light += gi_face_radiance(h.cell, h.cascade, face) * w;
			wsum += w;
		}
	}
	if (wsum <= 0.0) {
		return false;
	}
	r_light /= wsum;
	return true;
}
