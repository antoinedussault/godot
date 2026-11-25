#ifndef DYNAMIC_GI_GLES3_H
#define DYNAMIC_GI_GLES3_H

#include "core/local_vector.h"
#include "core/math/aabb.h"
#include "core/math/transform.h"
#include "core/rid.h"

#include "platform_config.h"
#ifndef GLES3_INCLUDE_H
#include <GLES3/gl3.h>
#else
#include GLES3_INCLUDE_H
#endif

// Dynamic global illumination: camera-following voxel cascades, lit in compute,
// traced by irradiance probes that scene.glsl samples. One state per scenario.
// The layout constants mirror shaders/gi/gi_common.glsl.
struct DynamicGIGLES3 {
	enum {
		RES = 64, // voxels per cascade side
		TILE = 8, // voxels per tile side, the unit of scrolling and re-voxelization
		TILES = RES / TILE,
		TILE_COUNT = TILES * TILES * TILES,
		PROBE_RES = 16,
		PROBES_PER_CASCADE = PROBE_RES * PROBE_RES * PROBE_RES,
		MAX_CASCADES = 6,
		MAX_LIGHTS = 64,
		PROBE_STRIDE = 4, // every probe refreshes once per this many frames
	};

	struct UBO {
		int32_t config[4];
		float params[4];
		float ambient[4];
		float sky[4];
		float sky_xform[16];
		int32_t cascade_origin[MAX_CASCADES][4];
		float cascade_size[MAX_CASCADES][4];
	};

	struct LightData {
		float pos_type[4];
		float color_inv_range[4];
		float dir_att[4];
		float spot[4];
	};

	struct Cascade {
		int origin[3] = { 0, 0, 0 }; // in voxels, always a multiple of TILE
		float voxel_size = 0.25;
		bool valid = false;
		bool bricks_dirty = false;
		uint32_t scan_cursor = 0;
		uint8_t dirty[TILE_COUNT];
	};

	struct Region {
		int cascade;
		int tile_min[3]; // world tile coordinates
		int tile_size[3];
	};

	struct State {
		int cascade_count = 0;
		float voxel_size = 0.0;
		Cascade cascades[MAX_CASCADES];

		GLuint tex_albedo = 0;
		GLuint tex_emission = 0;
		GLuint tex_facing = 0;
		GLuint tex_radiance = 0;
		GLuint tex_bricks = 0;
		GLuint tex_irradiance = 0;
		GLuint tex_visibility = 0;
		GLuint probe_ssbo = 0;
		GLuint light_ssbo = 0;
		GLuint ubo = 0;

		LocalVector<Region> regions;
		uint32_t next_region = 0;
		LocalVector<Region> retry;
		int retry_delay = 0;
		int retries_left = 0;

		const void *owner = nullptr; // the only render target allowed to move the cascades
		uint64_t last_update_frame = 0;
		uint32_t update_index = 0;
		Transform last_camera;
		uint32_t last_hash = 0;
		int converge = 0;
	};
};

#endif // DYNAMIC_GI_GLES3_H
