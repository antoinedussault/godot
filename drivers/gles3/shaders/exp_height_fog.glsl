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

uniform highp sampler2D source_color; //texunit:0
uniform highp sampler2D source_depth; //texunit:1

uniform highp mat4 inv_projection;
uniform highp mat4 cam_matrix;

uniform float density;
uniform float height_falloff;
uniform float height_ref;
uniform float start_distance;
uniform float max_opacity;
uniform vec3 inscatter;

uniform bool directional_enabled;
uniform vec3 directional_color;
uniform vec3 light_direction;
uniform float directional_exponent;

layout(location = 0) out vec4 frag_color;

// Same analytic exponential-height integral as the ExpHeightFog node's spatial
// shader (see modules/exp_height_fog), ported to a post-process pass so it can
// be driven from Environment instead of a scene node.
void main() {
	vec4 scene = texture(source_color, uv_interp);

	float depth = texture(source_depth, uv_interp).r;
	vec3 ndc = vec3(uv_interp * 2.0 - 1.0, depth * 2.0 - 1.0);
	vec4 vp = inv_projection * vec4(ndc, 1.0);
	vec3 view_pos = vp.xyz / vp.w;

	vec3 frag_world = (cam_matrix * vec4(view_pos, 1.0)).xyz;
	vec3 cam_world = cam_matrix[3].xyz;

	vec3 ray = frag_world - cam_world;
	float ray_len = length(ray);
	float effective_len = max(0.0, ray_len - start_distance);

	float fog_amount = 0.0;
	vec3 fog_color = inscatter;

	if (effective_len > 0.0) {
		float fo = max(height_falloff, 0.0001);
		float dz = ray.y; // Godot is Y-up.
		float d_cam = density * exp(-fo * (cam_world.y - height_ref));
		float fd = fo * dz;
		float integral = (abs(fd) > 0.0001) ? (d_cam * (1.0 - exp(-fd)) / fd) : d_cam;
		float optical_depth = integral * effective_len;
		fog_amount = clamp(1.0 - exp(-optical_depth), 0.0, max_opacity);

		if (directional_enabled) {
			vec3 vdir = normalize(ray);
			vec3 ldir = normalize(-light_direction);
			float d = clamp(dot(vdir, ldir), 0.0, 1.0);
			float g = pow(d, directional_exponent);
			fog_color = mix(inscatter, directional_color, g);
		}
	}

	vec3 res = mix(scene.rgb, fog_color, fog_amount);
	frag_color = vec4(res, scene.a);
}
