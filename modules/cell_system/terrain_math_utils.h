#ifndef TERRAIN_MATH_UTILS_H
#define TERRAIN_MATH_UTILS_H

// Shared grid math + falloff curves used by both the circular sculpt brush
// (terrain_brush.cpp) and the path/road deformer (terrain_path_deformer.cpp),
// so the two never drift apart on how a world vertex maps to a cell/local index.

#include "core/math/math_funcs.h"

enum TerrainFalloffType {
	FALLOFF_SMOOTH,
	FALLOFF_LINEAR,
	FALLOFF_SPHERICAL,
	FALLOFF_TIP,
};

// t01 is 1.0 at the brush/path center and 0.0 at the outer edge.
inline float terrain_falloff(float t01, int p_type) {
	t01 = CLAMP(t01, 0.0f, 1.0f);
	switch (p_type) {
		case FALLOFF_LINEAR:
			return t01;
		case FALLOFF_SPHERICAL: {
			float x = 1.0f - t01;
			return Math::sqrt(MAX(0.0f, 1.0f - x * x));
		}
		case FALLOFF_TIP:
			return t01 * t01;
		case FALLOFF_SMOOTH:
		default:
			return t01 * t01 * (3.0f - 2.0f * t01);
	}
}

inline int terrain_floordiv(int a, int b) {
	int d = a / b;
	int m = a % b;
	if (m != 0 && ((m < 0) != (b < 0))) d--;
	return d;
}

// Maps a global vertex index g (along one grid axis) covering cells of seg
// segments each to the 1 or 2 (cell,local) pairs that share that vertex
// (2 when g falls exactly on a cell boundary, since both neighboring cells
// own that shared edge vertex).
inline int terrain_axis_cells(int g, int seg, int *cell_out, int *local_out) {
	int q = terrain_floordiv(g, seg);
	int rem = g - q * seg;
	if (rem == 0) {
		cell_out[0] = q - 1; local_out[0] = seg;
		cell_out[1] = q;     local_out[1] = 0;
		return 2;
	}
	cell_out[0] = q; local_out[0] = rem;
	return 1;
}

#endif
