/* clang-format off */
[vertex]

layout(location = 0) in highp vec4 vertex_attrib;
/* clang-format on */

#ifdef SKY_SCREEN_PASS
layout(location = 4) in vec3 sky_dir_in;
out vec3 sky_dir_interp;
#else
layout(location = 4) in vec2 uv_in;
out vec2 uv_interp;
#endif

/* clang-format off */
#if defined(USE_MATERIAL)

layout(std140) uniform UniformData { //ubo:1

MATERIAL_UNIFORMS

};

#endif

VERTEX_SHADER_GLOBALS

/* clang-format on */

void main() {
#ifdef SKY_SCREEN_PASS
	sky_dir_interp = sky_dir_in;
#else
	uv_interp = uv_in;
#endif
	gl_Position = vertex_attrib;

	{
		/* clang-format off */

VERTEX_SHADER_CODE

		/* clang-format on */
	}
}

/* clang-format off */
[fragment]
/* clang-format on */

#define M_PI 3.14159265359

#ifdef SKY_SCREEN_PASS
in vec3 sky_dir_interp;
uniform highp mat4 sky_orientation;
uniform float sky_energy;
#else
in vec2 uv_interp;
#endif

uniform highp float time;
uniform vec3 sky_position;

uniform bool sky_light0_enabled;
uniform vec3 sky_light0_direction;
uniform highp float sky_light0_energy;
uniform vec3 sky_light0_color;

uniform bool sky_light1_enabled;
uniform vec3 sky_light1_direction;
uniform highp float sky_light1_energy;
uniform vec3 sky_light1_color;

uniform bool sky_light2_enabled;
uniform vec3 sky_light2_direction;
uniform highp float sky_light2_energy;
uniform vec3 sky_light2_color;

uniform bool sky_light3_enabled;
uniform vec3 sky_light3_direction;
uniform highp float sky_light3_energy;
uniform vec3 sky_light3_color;

layout(location = 0) out vec4 frag_color;

#ifdef SKY_SCREEN_PASS
const bool at_cubemap_pass = false;
#else
const bool at_cubemap_pass = true;
#endif
#define SKY_OUTPUT_IS_SRGB false

/* clang-format off */
#if defined(USE_MATERIAL)

layout(std140) uniform UniformData { //ubo:1

MATERIAL_UNIFORMS

};

#endif

FRAGMENT_SHADER_GLOBALS

/* clang-format on */

#if defined(USE_LIGHT_SHADER_CODE)
// Sky shaders have no light() function; this stub only exists so the shader
// template splitter can locate the FRAGMENT_SHADER_CODE tag below.
void _sky_unused_light_stub() {
	/* clang-format off */

LIGHT_SHADER_CODE

	/* clang-format on */
}
#endif

vec3 panorama_to_direction(vec2 uv) {
	float phi = uv.x * M_PI * 2.0;
	float theta = uv.y * M_PI;
	float sin_theta = sin(theta);
	return vec3(sin(phi) * sin_theta, cos(theta), cos(phi) * sin_theta);
}

vec2 direction_to_panorama(vec3 n) {
	vec2 st = vec2(atan(n.x, n.z), acos(n.y));
	if (st.x < 0.0) {
		st.x += M_PI * 2.0;
	}
	return st / vec2(M_PI * 2.0, M_PI);
}

void main() {
#ifdef SKY_SCREEN_PASS
	// sky_dir_interp is a clean world-space view ray (built on the CPU). Apply the sky
	// orientation only (no panorama-sampling z-flips) so EYEDIR is a real direction and
	// stays consistent with LIGHT*_DIRECTION, which is rotated the same way on the CPU.
	vec3 cube_normal = normalize(mat3(sky_orientation) * normalize(sky_dir_interp));
	vec2 panorama_coords = direction_to_panorama(cube_normal);
#else
	vec3 cube_normal = normalize(panorama_to_direction(uv_interp));
	vec2 panorama_coords = uv_interp;
#endif

	vec3 color = vec3(0.0);
	float alpha = 1.0;

	{
		/* clang-format off */

FRAGMENT_SHADER_CODE

		/* clang-format on */
	}

	frag_color = vec4(color, alpha);

#ifdef SKY_SCREEN_PASS
	frag_color.rgb *= sky_energy;
#endif
}
