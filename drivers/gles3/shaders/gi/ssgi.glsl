layout(local_size_x = 8, local_size_y = 8) in;
uniform sampler2D tex_depth;
uniform sampler2D tex_normal;
uniform sampler2D tex_color;
uniform sampler2D tex_input;
uniform sampler2D tex_history;
uniform sampler2D tex_history_depth;
uniform mat4 projection;
uniform mat4 inv_projection;
uniform mat4 cam_to_prev; // current view space to previous view space
uniform mat4 prev_projection;
uniform ivec2 full_size;
uniform ivec2 size;
uniform int res_scale;
uniform int frame;
uniform int ray_count;
uniform int step_count;
uniform int atrous_step;
uniform int history_valid;
uniform float radius;
uniform float thickness;
uniform float blend;

layout(rgba16f, binding = 0) uniform writeonly image2D img_out;
layout(r32f, binding = 1) uniform writeonly image2D img_depth_out;

ivec2 full_px(ivec2 p) {
	return min(p * res_scale + res_scale / 2, full_size - 1);
}

vec3 view_pos_uv(vec2 uv, float d) {
	vec4 v = inv_projection * vec4(vec3(uv, d) * 2.0 - 1.0, 1.0);
	return v.xyz / v.w;
}

vec3 view_pos(ivec2 fp, float d) {
	return view_pos_uv((vec2(fp) + 0.5) / vec2(full_size), d);
}

vec3 normal_at(ivec2 fp) {
	vec3 n = texelFetch(tex_normal, fp, 0).rgb * 2.0 - 1.0;
	float l = length(n);
	return l > 0.1 ? n / l : vec3(0.0, 0.0, 1.0);
}

float ign(vec2 p) {
	return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

#if defined(MODE_TRACE)
void main() {
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(p, size))) {
		return;
	}
	ivec2 fp = full_px(p);
	float d = texelFetch(tex_depth, fp, 0).r;
	if (d >= 0.999999) {
		imageStore(img_out, p, vec4(0.0));
		return;
	}
	vec3 pos = view_pos(fp, d);
	vec3 n = normal_at(fp);
	pos += n * (0.005 + 0.003 * abs(pos.z));
	vec3 up = abs(n.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 tx = normalize(cross(up, n));
	vec3 ty = cross(n, tx);

	float offs = float(frame & 63) * 5.588238;
	float n0 = ign(vec2(p) + offs);
	float n1 = ign(vec2(p.yx) + offs + 47.0);
	vec4 acc = vec4(0.0);
	for (int i = 0; i < ray_count; i++) {
		vec2 u = fract(vec2(n0, n1) + float(i) * vec2(0.754877666, 0.569840291));
		float r = sqrt(u.x);
		float a = u.y * 6.28318530718;
		vec3 dir = tx * (r * cos(a)) + ty * (r * sin(a)) + n * sqrt(max(0.0, 1.0 - u.x));
		float len = radius;
		// Keep the end point in front of the eye.
		if (pos.z + dir.z * len > -0.05) {
			len = max(0.0, (-0.05 - pos.z) / dir.z);
		}
		float jit = fract(n0 + n1 + float(i) * 0.618);
		for (int s = 0; s < step_count; s++) {
			float f = (float(s) + 0.15 + 0.85 * jit) / float(step_count);
			float t = f * f * len;
			vec3 q = pos + dir * t;
			vec4 c = projection * vec4(q, 1.0);
			vec2 uv = c.xy / c.w * 0.5 + 0.5;
			if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0)))) {
				break;
			}
			ivec2 hp = ivec2(uv * vec2(full_size));
			float hd = texelFetch(tex_depth, hp, 0).r;
			if (hd >= 0.999999) {
				continue;
			}
			float hz = view_pos_uv(uv, hd).z;
			float behind = hz - q.z;
			if (behind > 0.0 && behind < thickness * (1.0 + t * 0.15)) {
				if (dot(normal_at(hp), dir) < 0.05) {
					vec3 col = textureLod(tex_color, uv, clamp(log2(1.0 + t * 2.0), 0.0, 3.0)).rgb;
					col = mix(vec3(0.0), col, equal(col, col));
					float lum = max(col.r, max(col.g, col.b));
					col *= min(1.0, 16.0 / max(lum, 1e-4));
					acc += vec4(max(col, vec3(0.0)), 1.0);
				}
				break;
			}
		}
	}
	imageStore(img_out, p, acc / float(ray_count));
}
#endif

#if defined(MODE_TEMPORAL)
void main() {
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(p, size))) {
		return;
	}
	ivec2 fp = full_px(p);
	float d = texelFetch(tex_depth, fp, 0).r;
	vec4 cur = texelFetch(tex_input, p, 0);
	if (d >= 0.999999) {
		imageStore(img_out, p, vec4(0.0));
		imageStore(img_depth_out, p, vec4(0.0));
		return;
	}
	vec3 pos = view_pos(fp, d);
	imageStore(img_depth_out, p, vec4(-pos.z));
	vec4 res = cur;
	if (history_valid != 0) {
		vec3 pv = (cam_to_prev * vec4(pos, 1.0)).xyz;
		vec4 c = prev_projection * vec4(pv, 1.0);
		vec2 uv = c.xy / c.w * 0.5 + 0.5;
		if (c.w > 0.0 && all(greaterThan(uv, vec2(0.0))) && all(lessThan(uv, vec2(1.0)))) {
			// Bilinear history, skipping texels that belong to another surface.
			vec2 hp = uv * vec2(full_size) / float(res_scale) - (float(res_scale / 2) + 0.5) / float(res_scale);
			ivec2 b = ivec2(floor(hp));
			vec2 f = hp - vec2(b);
			vec4 hist = vec4(0.0);
			float wsum = 0.0;
			for (int i = 0; i < 4; i++) {
				ivec2 o = ivec2(i & 1, i >> 1);
				ivec2 q = clamp(b + o, ivec2(0), size - 1);
				float hz = texelFetch(tex_history_depth, q, 0).r;
				if (abs(hz + pv.z) > 0.06 * abs(pv.z) + 0.02) {
					continue;
				}
				vec2 wv = mix(1.0 - f, f, vec2(o));
				float w = wv.x * wv.y + 1e-4;
				hist += texelFetch(tex_history, q, 0) * w;
				wsum += w;
			}
			if (wsum > 1e-3) {
				hist /= wsum;
				vec4 m1 = vec4(0.0);
				vec4 m2 = vec4(0.0);
				for (int y = -2; y <= 2; y++) {
					for (int x = -2; x <= 2; x++) {
						vec4 s = texelFetch(tex_input, clamp(p + ivec2(x, y), ivec2(0), size - 1), 0);
						m1 += s;
						m2 += s * s;
					}
				}
				m1 /= 25.0;
				vec4 sd = sqrt(max(m2 / 25.0 - m1 * m1, vec4(0.0))) * 1.5 + 0.02;
				hist = clamp(hist, m1 - sd, m1 + sd);
				res = mix(cur, hist, blend);
			}
		}
	}
	imageStore(img_out, p, mix(vec4(0.0), res, equal(res, res)));
}
#endif

#if defined(MODE_ATROUS)
void main() {
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(p, size))) {
		return;
	}
	ivec2 fp = full_px(p);
	float d = texelFetch(tex_depth, fp, 0).r;
	if (d >= 0.999999) {
		imageStore(img_out, p, vec4(0.0));
		return;
	}
	vec3 pos = view_pos(fp, d);
	vec3 n = normal_at(fp);
	const float k[3] = float[](0.375, 0.25, 0.0625);
	vec4 acc = vec4(0.0);
	float wsum = 0.0;
	for (int y = -2; y <= 2; y++) {
		for (int x = -2; x <= 2; x++) {
			ivec2 q = p + ivec2(x, y) * atrous_step;
			if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, size))) {
				continue;
			}
			ivec2 fq = full_px(q);
			float dq = texelFetch(tex_depth, fq, 0).r;
			if (dq >= 0.999999) {
				continue;
			}
			float plane = abs(dot(view_pos(fq, dq) - pos, n));
			float w = k[abs(x)] * k[abs(y)];
			w *= exp(-plane / (0.01 * abs(pos.z) + 0.01));
			w *= pow(max(dot(n, normal_at(fq)), 0.0), 8.0);
			acc += texelFetch(tex_input, q, 0) * w;
			wsum += w;
		}
	}
	imageStore(img_out, p, wsum > 1e-5 ? acc / wsum : texelFetch(tex_input, p, 0));
}
#endif
