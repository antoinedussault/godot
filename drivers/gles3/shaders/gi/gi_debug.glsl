uniform sampler2D tex_depth;
uniform mat4 inv_projection;
uniform mat4 cam_matrix;
uniform vec2 full_size;
uniform int mode;
layout(location = 0) out vec4 frag_color;

void main() {
	vec2 uv = gl_FragCoord.xy / full_size;
	vec4 fv = inv_projection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
	vec4 nv = inv_projection * vec4(uv * 2.0 - 1.0, -1.0, 1.0);
	vec3 ro = (cam_matrix * vec4(nv.xyz / nv.w, 1.0)).xyz;
	vec3 rd = normalize((cam_matrix * vec4(fv.xyz / fv.w, 1.0)).xyz - ro);
	if (mode == 1 || mode == 2) {
		GIHit h;
		if (!gi_trace(ro, rd, 0, 1e6, h)) {
			frag_color = vec4(0.02, 0.02, 0.04, 1.0);
			return;
		}
		vec3 light;
		if (!gi_hit_radiance(h, rd, light)) {
			light = vec3(0.2, 0.0, 0.2);
		}
		frag_color = vec4(mode == 1 ? light : gi_unpack_albedo(texelFetch(gi_tex_albedo, gi_texel(h.cell, h.cascade), 0).r), 1.0);
		return;
	}
	float d = texelFetch(tex_depth, ivec2(gl_FragCoord.xy), 0).r;
	vec4 v = inv_projection * vec4(vec3(uv, d) * 2.0 - 1.0, 1.0);
	vec3 pos = (cam_matrix * vec4(v.xyz / v.w, 1.0)).xyz;
	vec3 n = normalize(cross(dFdx(pos), dFdy(pos)));
	if (d >= 0.999999) {
		discard;
	}
	vec3 irr;
	vec3 unused;
	float w = gi_sample(pos, n, n, 0, irr, unused);
	frag_color = vec4(w > 0.0 ? irr : vec3(0.3, 0.0, 0.3), 1.0);
}
