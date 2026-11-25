layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
layout(r8ui, binding = 4) uniform writeonly uimage3D img_bricks;
uniform usampler3D gi_tex_albedo;
uniform int cascade;

void main() {
	ivec3 b = ivec3(gl_GlobalInvocationID);
	uint any_solid = 0u;
	for (int i = 0; i < 64; i++) {
		any_solid |= texelFetch(gi_tex_albedo, b * 4 + ivec3(i & 3, (i >> 2) & 3, i >> 4) + ivec3(0, 0, cascade * 64), 0).r;
	}
	imageStore(img_bricks, b + ivec3(0, 0, cascade * 16), uvec4(any_solid != 0u ? 1u : 0u));
}
