uniform sampler2D tex_depth;
uniform sampler2D tex_albedo;
uniform sampler2D tex_ambient;
uniform sampler2D tex_input;
uniform ivec2 full_size;
uniform ivec2 size;
uniform int res_scale;
uniform float intensity;
uniform float z_near;
uniform float z_far;
uniform int orthogonal;
uniform int show_only;
layout(location = 0) out vec4 frag_color;

float lin(float d) {
	if (orthogonal != 0) {
		return mix(z_near, z_far, d);
	}
	return 2.0 * z_near * z_far / (z_far + z_near - (d * 2.0 - 1.0) * (z_far - z_near));
}

void main() {
	ivec2 fp = ivec2(gl_FragCoord.xy);
	float d = texelFetch(tex_depth, fp, 0).r;
	if (d >= 0.999999) {
		discard;
	}
	float z = lin(d);
	vec2 hp = gl_FragCoord.xy / float(res_scale) - (float(res_scale / 2) + 0.5) / float(res_scale);
	ivec2 b = ivec2(floor(hp));
	vec2 f = hp - vec2(b);
	vec4 acc = vec4(0.0);
	float wsum = 0.0;
	for (int i = 0; i < 4; i++) {
		ivec2 o = ivec2(i & 1, i >> 1);
		ivec2 q = clamp(b + o, ivec2(0), size - 1);
		float zq = lin(texelFetch(tex_depth, min(q * res_scale + res_scale / 2, full_size - 1), 0).r);
		vec2 wv = mix(1.0 - f, f, vec2(o)) + 0.01;
		float w = wv.x * wv.y * exp(-abs(zq - z) / (0.02 * z + 0.005)) + 1e-6;
		acc += texelFetch(tex_input, q, 0) * w;
		wsum += w;
	}
	acc /= wsum;
	if (show_only != 0) {
		frag_color = vec4(acc.rgb, 1.0);
		return;
	}
	// Hit rays replace the same share of ambient light they occlude.
	vec3 albedo = texelFetch(tex_albedo, fp, 0).rgb;
	vec3 ambient = texelFetch(tex_ambient, fp, 0).rgb;
	frag_color = vec4(albedo * acc.rgb * intensity - ambient * clamp(acc.a, 0.0, 1.0), 0.0);
}
