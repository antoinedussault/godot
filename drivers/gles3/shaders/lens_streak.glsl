/* clang-format off */
[vertex]

layout(location = 0) in highp vec4 vertex_attrib;
/* clang-format on */

uniform highp sampler2D source_depth; //texunit:0

uniform vec2 light_uv;
uniform vec2 source_radius;
uniform highp float ref_depth;
uniform vec2 half_size;
uniform vec2 rotation;
uniform float aspect;

out vec2 local_interp;
flat out float visibility;

#define OCCLUSION_TAPS 32

void main() {
	// Fraction of the source disc that nothing stands in front of. Golden-angle
	// spiral, the taps are fixed so the result does not flicker.
	float lit = 0.0;
	for (int i = 0; i < OCCLUSION_TAPS; i++) {
		float r = sqrt((float(i) + 0.5) / float(OCCLUSION_TAPS));
		float a = float(i) * 2.39996323;
		vec2 uv = clamp(light_uv + vec2(cos(a), sin(a)) * r * source_radius, vec2(0.0005), vec2(0.9995));
		lit += step(ref_depth, textureLod(source_depth, uv, 0.0).r);
	}
	visibility = lit / float(OCCLUSION_TAPS);

	local_interp = vertex_attrib.xy;

	// Fully hidden: collapse the quad so that it costs no fill.
	vec2 p = vertex_attrib.xy * half_size * step(0.0001, visibility);
	p = vec2(p.x * rotation.x - p.y * rotation.y, p.x * rotation.y + p.y * rotation.x);
	p.x /= aspect;

	gl_Position = vec4(light_uv * 2.0 - 1.0 + p, 0.0, 1.0);
}

/* clang-format off */
[fragment]

#if !defined(GLES_OVER_GL)
precision highp float;
precision highp int;
#endif
/* clang-format on */

in vec2 local_interp;
flat in float visibility;

uniform vec3 streak_color;

layout(location = 0) out vec4 frag_color;

void main() {
	float a = 1.0 - min(abs(local_interp.x), 1.0);

	// Sharp hot centre with a long tail, narrowing to a point at the ends.
	float along = a * a * (0.25 + 0.75 * a * a * a * a);
	float v = local_interp.y / mix(0.2, 1.0, a);
	float across = exp(-v * v * 6.0) * (1.0 - smoothstep(0.8, 1.0, abs(local_interp.y)));

	frag_color = vec4(min(streak_color * (visibility * along * across), vec3(1000.0)), 0.0);
}
