layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
layout(r11f_g11f_b10f, binding = 6) uniform writeonly image3D img_radiance;
uniform usampler3D gi_tex_emission;
uniform int cascade;

struct GILight {
	vec4 pos_type; // w: 0 directional, 1 omni, 2 spot
	vec4 color_inv_range;
	vec4 dir_att;
	vec4 spot; // cos cutoff, spot attenuation, shadowed
};

layout(std430, binding = 2) readonly buffer GILights {
	GILight lights[];
};

const vec3 NORMALS[6] = vec3[](vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1));

float distance_attenuation(float d, float inv_range, float att) {
	float nd = d * inv_range;
	if (nd >= 1.0) {
		return 0.0;
	}
	if (gi_sky.w > 0.5) {
		nd *= nd;
		nd = 1.0 - nd * nd;
		return nd * nd * pow(max(d, 0.1), -att);
	}
	return pow(1.0 - nd, att);
}

vec3 direct_light(vec3 p, vec3 n, float vs) {
	vec3 sum = vec3(0.0);
	int count = int(gi_sky.z);
	for (int i = 0; i < count; i++) {
		GILight l = lights[i];
		vec3 ldir = -l.dir_att.xyz;
		float dist = 1e6;
		float att = 1.0;
		if (l.pos_type.w > 0.5) {
			vec3 rel = l.pos_type.xyz - p;
			dist = length(rel);
			ldir = rel / max(dist, 1e-4);
			att = distance_attenuation(dist, l.color_inv_range.w, l.dir_att.w);
			if (l.pos_type.w > 1.5) {
				float scos = max(dot(-ldir, l.dir_att.xyz), l.spot.x);
				float rim = max(0.0001, (1.0 - scos) / max(1.0 - l.spot.x, 1e-4));
				att *= 1.0 - pow(rim, l.spot.y);
			}
		}
		float ndl = dot(n, ldir);
		if (ndl <= 0.0 || att <= 1e-4) {
			continue;
		}
		if (l.spot.z > 0.5) {
			GIHit h;
			if (gi_trace(p, ldir, cascade, max(dist - vs * 1.5, 0.0), h)) {
				continue;
			}
		}
		sum += l.color_inv_range.rgb * (ndl * att);
	}
	return sum;
}

void main() {
	ivec3 cell = gi_cascade_origin[cascade].xyz + ivec3(gl_GlobalInvocationID);
	ivec3 tc = gi_texel(cell, cascade);
	if (texelFetch(gi_tex_bricks, ((cell >> 2) & ivec3(15)) + ivec3(0, 0, cascade * 16), 0).r == 0u) {
		return;
	}
	uint packed_albedo = texelFetch(gi_tex_albedo, tc, 0).r;
	if (packed_albedo == 0u) {
		return;
	}
	float vs = gi_cascade_size[cascade].x;
	vec3 albedo = min(gi_unpack_albedo(packed_albedo), vec3(0.9));
	vec3 emission = gi_unpack_emission(texelFetch(gi_tex_emission, tc, 0).r);

	uint facing = texelFetch(gi_tex_facing, tc, 0).r;

	for (int f = 0; f < 6; f++) {
		vec3 light = vec3(0.0);
		if (gi_face_open(cell, cascade, f, facing)) {
			vec3 n = NORMALS[f];
			vec3 p = (vec3(cell + ivec3(n)) + 0.5) * vs;
			vec3 irr;
			vec3 unused;
			gi_sample(p, n, n, cascade, irr, unused);
			light = albedo * (direct_light(p, n, vs) + irr * gi_params.z) + emission;
			light = mix(vec3(0.0), light, equal(light, light));
			light = clamp(light, vec3(0.0), vec3(512.0));
		}
		imageStore(img_radiance, tc + ivec3(0, f * GI_RES, 0), vec4(light, 1.0));
	}
}
