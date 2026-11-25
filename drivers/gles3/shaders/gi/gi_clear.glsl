layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;
layout(r32ui, binding = 4) uniform writeonly uimage3D img_albedo;
layout(r32ui, binding = 5) uniform writeonly uimage3D img_emission;
layout(r11f_g11f_b10f, binding = 6) uniform writeonly image3D img_radiance;
layout(r32ui, binding = 7) uniform writeonly uimage3D img_facing;
uniform ivec3 box_min;
uniform int cascade;
uniform int clear_radiance;

void main() {
	ivec3 cell = box_min + ivec3(gl_GlobalInvocationID);
	ivec3 tc = (cell & ivec3(63)) + ivec3(0, 0, cascade * 64);
	imageStore(img_albedo, tc, uvec4(0u));
	imageStore(img_emission, tc, uvec4(0u));
	imageStore(img_facing, tc, uvec4(0u));
	if (clear_radiance != 0) {
		for (int f = 0; f < 6; f++) {
			imageStore(img_radiance, tc + ivec3(0, f * 64, 0), vec4(0.0));
		}
	}
}
