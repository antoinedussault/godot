// One workgroup per probe: every invocation traces rays, then the group blends
// them into that probe's irradiance and visibility tiles (borders included).
layout(local_size_x = 64) in;
layout(rgba16f, binding = 4) uniform image2DArray img_irradiance;
layout(rg16f, binding = 5) uniform image2DArray img_visibility;
uniform sampler2DArray gi_sky_array;
uniform sampler2D gi_sky_irradiance;
uniform int cascade;
uniform int stride;
uniform int first_probe;
uniform mat3 ray_rotation;

shared vec4 ray_light[192]; // radiance, distance
shared vec3 s_offset;
shared float s_age;
shared int s_state;
shared int s_backfaces;

vec3 fibonacci(int i, int n) {
	float phi = 6.28318530718 * fract(float(i) * 0.61803398875);
	float z = 1.0 - (2.0 * float(i) + 1.0) / float(n);
	float r = sqrt(max(0.0, 1.0 - z * z));
	return vec3(cos(phi) * r, sin(phi) * r, z);
}

vec3 sky_radiance(vec3 d) {
	if (gi_sky.y < 0.5) {
		return gi_ambient.rgb;
	}
	vec3 v = normalize(mat3(gi_sky_xform) * d);
	vec2 uv = v.xy / (1.0 + abs(v.z));
	uv = uv * vec2(0.5, 0.25) + vec2(0.5, 0.25);
	if (v.z > 0.0) {
		uv.y = 1.0 - uv.y;
	}
	vec3 s = gi_sky.y < 1.5 ? textureLod(gi_sky_array, vec3(uv, 2.0), 0.0).rgb : textureLod(gi_sky_irradiance, uv, 0.0).rgb;
	return mix(gi_ambient.rgb, s * gi_sky.x, gi_ambient.a);
}

vec3 texel_dir(ivec2 px, int size) {
	// Mirror border texels onto the interior texel they duplicate.
	int last = size - 1;
	ivec2 p = px;
	bool bx = p.x == 0 || p.x == last;
	bool by = p.y == 0 || p.y == last;
	if (bx && by) {
		p = ivec2(p.x == 0 ? last - 1 : 1, p.y == 0 ? last - 1 : 1);
	} else if (bx) {
		p = ivec2(p.x == 0 ? 1 : last - 1, last - p.y);
	} else if (by) {
		p = ivec2(last - p.x, p.y == 0 ? 1 : last - 1);
	}
	return gi_oct_decode((vec2(p - 1) + 0.5) / float(size - 2) * 2.0 - 1.0);
}

void main() {
	int pidx = int(gl_WorkGroupID.x) * stride + first_probe;
	ivec3 slot = ivec3(pidx & 15, (pidx >> 4) & 15, pidx >> 8);
	ivec3 org = gi_cascade_origin[cascade].xyz >> 2;
	ivec3 pc = org + ((slot - org) & ivec3(15));
	int idx = gi_probe_index(cascade, slot);
	float vs = gi_cascade_size[cascade].x;
	float spacing = vs * float(GI_PROBE_STEP);
	vec3 center = (vec3(pc) + 0.5) * spacing;
	int lid = int(gl_LocalInvocationIndex);

	if (lid == 0) {
		GIProbe pr = gi_probes[idx];
		bool fresh = pr.coord.xyz != pc || pr.coord.w != 1;
		s_age = fresh ? 0.0 : pr.offset.w;
		s_state = 2;
		s_backfaces = 0;
		s_offset = vec3(0.0);
		// Move out of geometry: the nearest free voxel of the brick that is not
		// squeezed against a surface wins.
		float best = 1e9;
		for (int i = 0; i < 64; i++) {
			ivec3 o = ivec3(i & 3, (i >> 2) & 3, i >> 4);
			ivec3 cell = pc * 4 + o;
			if (gi_solid(cell, cascade)) {
				continue;
			}
			vec3 off = (vec3(o) + 0.5 - 2.0) * vs;
			float score = dot(off, off) / (vs * vs);
			for (int f = 0; f < 6; f++) {
				ivec3 d = ivec3(0);
				d[f >> 1] = (f & 1) * 2 - 1;
				ivec3 nb = cell + d;
				ivec3 rel = nb - gi_cascade_origin[cascade].xyz;
				if (all(greaterThanEqual(rel, ivec3(0))) && all(lessThan(rel, ivec3(GI_RES))) && gi_solid(nb, cascade)) {
					score += 1.5;
				}
			}
			if (score < best) {
				best = score;
				s_offset = off;
				s_state = 1;
			}
		}
		// A relocated probe sees a different scene; let it adapt quickly.
		if (!fresh && distance(s_offset, pr.offset.xyz) > vs * 0.1) {
			s_age = min(s_age, 2.0);
		}
	}
	memoryBarrierShared();
	barrier();

	int rays = gi_config.w * 64;
	float max_dist = spacing * 2.0;
	if (s_state == 1) {
		for (int r = lid; r < rays; r += 64) {
			vec3 rd = ray_rotation * fibonacci(r, rays);
			GIHit h;
			if (gi_trace(center + s_offset, rd, cascade, 1e6, h)) {
				vec3 light;
				if (!gi_hit_radiance(h, rd, light)) {
					// Inside of a wall: no light, and pull visibility in.
					ray_light[r] = vec4(0.0, 0.0, 0.0, h.t * 0.2);
					atomicAdd(s_backfaces, 1);
				} else {
					ray_light[r] = vec4(light, h.t);
				}
			} else {
				ray_light[r] = vec4(sky_radiance(rd), 1e6);
			}
		}
	}
	memoryBarrierShared();
	barrier();

	// Mostly surrounded by back faces: the probe sits inside an object.
	bool usable = s_state == 1 && s_backfaces * 4 <= rays;
	if (usable) {
		ivec2 tile = ivec2(slot.x + slot.z * 16, slot.y);
		float keep = min(gi_params.w, s_age / (s_age + 1.0));

		{
			ivec2 px = ivec2(lid & 7, lid >> 3);
			vec3 d = texel_dir(px, 8);
			vec4 acc = vec4(0.0);
			for (int r = 0; r < rays; r++) {
				float w = max(0.0, dot(d, ray_rotation * fibonacci(r, rays)));
				acc += vec4(ray_light[r].rgb * w, w);
			}
			vec3 v = sqrt(max(acc.rgb / max(acc.a, 1e-4), vec3(0.0)));
			ivec3 tc = ivec3(tile * 8 + px, cascade);
			vec3 old = imageLoad(img_irradiance, tc).rgb;
			float k = keep;
			// React faster to big lighting changes.
			float change = max(abs(v.r - old.r), max(abs(v.g - old.g), abs(v.b - old.b)));
			float top = max(0.05, max(max(old.r, max(old.g, old.b)), max(v.r, max(v.g, v.b))));
			if (change > 0.5 * top) {
				k *= 0.8;
			}
			imageStore(img_irradiance, tc, vec4(s_age < 0.5 ? v : mix(v, old, k), 1.0));
		}
		for (int t = lid; t < 256; t += 64) {
			ivec2 px = ivec2(t & 15, t >> 4);
			vec3 d = texel_dir(px, 16);
			vec3 acc = vec3(0.0);
			for (int r = 0; r < rays; r++) {
				float w = max(0.0, dot(d, ray_rotation * fibonacci(r, rays)));
				w *= w;
				w *= w;
				w *= w;
				w *= w;
				w *= w;
				w *= w; // ^64
				float dist = min(ray_light[r].a, max_dist);
				acc += vec3(dist, dist * dist, 1.0) * w;
			}
			ivec3 tc = ivec3(tile * 16 + px, cascade);
			vec2 old = imageLoad(img_visibility, tc).rg;
			vec2 v = acc.z > 1e-5 ? acc.xy / acc.z : (s_age < 0.5 ? vec2(max_dist, max_dist * max_dist) : old);
			imageStore(img_visibility, tc, vec4(s_age < 0.5 ? v : mix(v, old, keep), 0.0, 0.0));
		}
	}
	if (lid == 0) {
		gi_probes[idx].coord = ivec4(pc, usable ? 1 : 2);
		gi_probes[idx].offset = vec4(s_offset, min(s_age + 1.0, 1000.0));
	}
}
