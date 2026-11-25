// Dynamic GI and SSGI parts of RasterizerSceneGLES3.

#include "rasterizer_scene_gles3.h"

#include "core/math/random_pcg.h"
#include "rasterizer_canvas_gles3.h"
#include "servers/visual/visual_server_raster.h"

#include "drivers/gles3/shaders/gi/gi_sources.gen.h"

typedef DynamicGIGLES3 GI;

enum {
	GI_PROGRAM_CLEAR,
	GI_PROGRAM_BRICKS,
	GI_PROGRAM_LIGHT,
	GI_PROGRAM_PROBES,
	GI_PROGRAM_DEBUG,
	GI_PROGRAM_SSGI_TRACE,
	GI_PROGRAM_SSGI_TEMPORAL,
	GI_PROGRAM_SSGI_ATROUS,
	GI_PROGRAM_SSGI_COMPOSITE,
};

static GLuint _gi_compile_stage(GLenum p_type, const char **p_parts, int p_count, const char *p_name) {
	GLuint shader = glCreateShader(p_type);
	glShaderSource(shader, p_count, p_parts, nullptr);
	glCompileShader(shader);
	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[4096] = { 0 };
		glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
		ERR_PRINT("GI shader '" + String(p_name) + "' failed to compile:\n" + String(log));
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static GLuint _gi_link(GLuint p_a, GLuint p_b, const char *p_name) {
	GLuint program = glCreateProgram();
	glAttachShader(program, p_a);
	if (p_b) {
		glAttachShader(program, p_b);
	}
	glLinkProgram(program);
	GLint ok = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	glDeleteShader(p_a);
	if (p_b) {
		glDeleteShader(p_b);
	}
	if (!ok) {
		char log[4096] = { 0 };
		glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
		ERR_PRINT("GI shader '" + String(p_name) + "' failed to link:\n" + String(log));
		glDeleteProgram(program);
		return 0;
	}
	GLuint block = glGetUniformBlockIndex(program, "DynamicGIData");
	if (block != GL_INVALID_INDEX) {
		glUniformBlockBinding(program, block, 8);
	}
	return program;
}

static GLuint _gi_compute(const char *p_name, const char *p_defines, bool p_common, bool p_trace, const char *p_body) {
	const char *parts[6] = {
		"#version 460 core\n#define GI_PROBE_ACCESS restrict\n",
		p_defines,
		p_common ? gi_common_source : "",
		p_trace ? gi_trace_source : "",
		p_body,
		"\n"
	};
	GLuint cs = _gi_compile_stage(GL_COMPUTE_SHADER, parts, 6, p_name);
	return cs ? _gi_link(cs, 0, p_name) : 0;
}

static GLuint _gi_fullscreen(const char *p_name, bool p_common, const char *p_body) {
	const char *vs_src[1] = { "#version 460 core\nvoid main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n" };
	const char *parts[5] = {
		"#version 460 core\n#define GI_PROBE_ACCESS readonly\n",
		p_common ? gi_common_source : "",
		p_common ? gi_trace_source : "",
		p_body,
		"\n"
	};
	GLuint vs = _gi_compile_stage(GL_VERTEX_SHADER, vs_src, 1, p_name);
	GLuint fs = _gi_compile_stage(GL_FRAGMENT_SHADER, parts, 5, p_name);
	if (!vs || !fs) {
		return 0;
	}
	return _gi_link(vs, fs, p_name);
}

static _FORCE_INLINE_ void _u1i(GLuint p, const char *n, int v) {
	glUniform1i(glGetUniformLocation(p, n), v);
}
static _FORCE_INLINE_ void _u1f(GLuint p, const char *n, float v) {
	glUniform1f(glGetUniformLocation(p, n), v);
}
static _FORCE_INLINE_ void _um4(GLuint p, const char *n, const CameraMatrix &m) {
	GLfloat f[16];
	for (int i = 0; i < 16; i++) {
		f[i] = m.matrix[i / 4][i % 4];
	}
	glUniformMatrix4fv(glGetUniformLocation(p, n), 1, GL_FALSE, f);
}
static void _gi_tex(GLuint p_program, const char *p_name, int p_unit, GLenum p_target, GLuint p_tex) {
	glActiveTexture(GL_TEXTURE0 + p_unit);
	glBindTexture(p_target, p_tex);
	_u1i(p_program, p_name, p_unit);
}

static int _floor_div(int a, int b) {
	return a >= 0 ? a / b : -((-a + b - 1) / b);
}

static int _tile_slot(int x, int y, int z) {
	return (x & 7) + (y & 7) * 8 + (z & 7) * 64;
}

GLuint RasterizerSceneGLES3::_gi_get_program(int p_which) {
	if (gi.program_tried[p_which]) {
		return gi.programs[p_which];
	}
	gi.program_tried[p_which] = true;
	GLuint p = 0;
	switch (p_which) {
		case GI_PROGRAM_CLEAR:
			p = _gi_compute("gi_clear", "", false, false, gi_clear_source);
			break;
		case GI_PROGRAM_BRICKS:
			p = _gi_compute("gi_bricks", "", false, false, gi_bricks_source);
			break;
		case GI_PROGRAM_LIGHT:
			p = _gi_compute("gi_light", "", true, true, gi_light_source);
			break;
		case GI_PROGRAM_PROBES:
			p = _gi_compute("gi_probes", "", true, true, gi_probes_source);
			break;
		case GI_PROGRAM_DEBUG:
			p = _gi_fullscreen("gi_debug", true, gi_debug_source);
			break;
		case GI_PROGRAM_SSGI_TRACE:
			p = _gi_compute("ssgi_trace", "#define MODE_TRACE\n", false, false, ssgi_source);
			break;
		case GI_PROGRAM_SSGI_TEMPORAL:
			p = _gi_compute("ssgi_temporal", "#define MODE_TEMPORAL\n", false, false, ssgi_source);
			break;
		case GI_PROGRAM_SSGI_ATROUS:
			p = _gi_compute("ssgi_atrous", "#define MODE_ATROUS\n", false, false, ssgi_source);
			break;
		case GI_PROGRAM_SSGI_COMPOSITE:
			p = _gi_fullscreen("ssgi_composite", false, ssgi_composite_source);
			break;
	}
	gi.programs[p_which] = p;
	return p;
}

void RasterizerSceneGLES3::_gi_initialize() {
	GI::UBO zero;
	memset(&zero, 0, sizeof(zero));
	glGenBuffers(1, &gi.zero_ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, gi.zero_ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(zero), &zero, GL_STATIC_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	uint8_t zeros[64] = { 0 };
	glGenBuffers(1, &gi.zero_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, gi.zero_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(zeros), zeros, GL_STATIC_DRAW);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	glGenVertexArrays(1, &gi.empty_vao);

	// Attachment-less target: voxelization only writes images.
	glGenFramebuffers(1, &gi.voxel_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, gi.voxel_fbo);
	glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH, GI::RES * 2);
	glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_HEIGHT, GI::RES * 2);
	glBindFramebuffer(GL_FRAMEBUFFER, RasterizerStorageGLES3::system_fbo);

	_gi_bind(nullptr);
}

void RasterizerSceneGLES3::_gi_bind(GI::State *p_state) {
	glBindBufferBase(GL_UNIFORM_BUFFER, 8, p_state ? p_state->ubo : gi.zero_ubo);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, p_state ? p_state->probe_ssbo : gi.zero_ssbo);
	glActiveTexture(GL_TEXTURE0 + storage->config.max_texture_image_units - 15);
	glBindTexture(GL_TEXTURE_2D_ARRAY, p_state ? p_state->tex_irradiance : storage->resources.white_tex_array);
	glActiveTexture(GL_TEXTURE0 + storage->config.max_texture_image_units - 16);
	glBindTexture(GL_TEXTURE_2D_ARRAY, p_state ? p_state->tex_visibility : storage->resources.white_tex_array);
}

void RasterizerSceneGLES3::_gi_free_state(GI::State *p_state) {
	GLuint textures[7] = { p_state->tex_albedo, p_state->tex_emission, p_state->tex_facing, p_state->tex_radiance, p_state->tex_bricks, p_state->tex_irradiance, p_state->tex_visibility };
	glDeleteTextures(7, textures);
	GLuint buffers[3] = { p_state->probe_ssbo, p_state->light_ssbo, p_state->ubo };
	glDeleteBuffers(3, buffers);
	memdelete(p_state);
}

static GLuint _gi_texture(GLenum p_target, GLenum p_format, int p_w, int p_h, int p_d, bool p_linear) {
	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(p_target, tex);
	glTexStorage3D(p_target, 1, p_format, p_w, p_h, p_d);
	GLenum filter = p_linear ? GL_LINEAR : GL_NEAREST;
	glTexParameteri(p_target, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(p_target, GL_TEXTURE_MAG_FILTER, filter);
	glTexParameteri(p_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(p_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(p_target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	return tex;
}

GI::State *RasterizerSceneGLES3::_gi_create_state(const Environment *p_env) {
	GI::State *s = memnew(GI::State);
	s->cascade_count = p_env->dynamic_gi_cascades;
	s->voxel_size = p_env->dynamic_gi_voxel_size;
	int c = s->cascade_count;
	for (int i = 0; i < GI::MAX_CASCADES; i++) {
		s->cascades[i].voxel_size = s->voxel_size * float(1 << i);
		memset(s->cascades[i].dirty, 0, sizeof(s->cascades[i].dirty));
	}

	glActiveTexture(GL_TEXTURE0);
	s->tex_albedo = _gi_texture(GL_TEXTURE_3D, GL_R32UI, GI::RES, GI::RES, GI::RES * c, false);
	s->tex_emission = _gi_texture(GL_TEXTURE_3D, GL_R32UI, GI::RES, GI::RES, GI::RES * c, false);
	s->tex_facing = _gi_texture(GL_TEXTURE_3D, GL_R32UI, GI::RES, GI::RES, GI::RES * c, false);
	s->tex_radiance = _gi_texture(GL_TEXTURE_3D, GL_R11F_G11F_B10F, GI::RES, GI::RES * 6, GI::RES * c, false);
	s->tex_bricks = _gi_texture(GL_TEXTURE_3D, GL_R8UI, 16, 16, 16 * c, false);
	int ap = GI::PROBE_RES * GI::PROBE_RES;
	s->tex_irradiance = _gi_texture(GL_TEXTURE_2D_ARRAY, GL_RGBA16F, ap * 8, GI::PROBE_RES * 8, c, true);
	s->tex_visibility = _gi_texture(GL_TEXTURE_2D_ARRAY, GL_RG16F, ap * 16, GI::PROBE_RES * 16, c, true);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
	glBindTexture(GL_TEXTURE_3D, 0);

	glClearTexImage(s->tex_albedo, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
	glClearTexImage(s->tex_emission, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
	glClearTexImage(s->tex_facing, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
	glClearTexImage(s->tex_radiance, 0, GL_RGB, GL_FLOAT, nullptr);
	glClearTexImage(s->tex_bricks, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);
	glClearTexImage(s->tex_irradiance, 0, GL_RGBA, GL_FLOAT, nullptr);
	glClearTexImage(s->tex_visibility, 0, GL_RG, GL_FLOAT, nullptr);

	glGenBuffers(1, &s->probe_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, s->probe_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, GI::PROBES_PER_CASCADE * c * 32, nullptr, GL_DYNAMIC_DRAW);
	glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
	glGenBuffers(1, &s->light_ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, s->light_ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GI::LightData) * GI::MAX_LIGHTS, nullptr, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	glGenBuffers(1, &s->ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, s->ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(GI::UBO), nullptr, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	return s;
}

void RasterizerSceneGLES3::dynamic_gi_free(RID p_scenario) {
	Map<RID, GI::State *>::Element *E = gi.states.find(p_scenario);
	if (E) {
		_gi_free_state(E->get());
		gi.states.erase(E);
	}
	gi.current = nullptr;
}

void RasterizerSceneGLES3::dynamic_gi_mark_dirty(RID p_scenario, const AABB &p_aabb) {
	if (gi.states.empty()) {
		return;
	}
	Map<RID, GI::State *>::Element *E = gi.states.find(p_scenario);
	if (!E) {
		return;
	}
	GI::State *s = E->get();
	for (int c = 0; c < s->cascade_count; c++) {
		GI::Cascade &cas = s->cascades[c];
		if (!cas.valid) {
			continue;
		}
		float ts = cas.voxel_size * GI::TILE;
		int from[3], to[3];
		bool empty = false;
		for (int a = 0; a < 3; a++) {
			int o = cas.origin[a] / GI::TILE;
			// Clamp in float first: huge bounds would overflow the cast.
			float lo = CLAMP(Math::floor(p_aabb.position[a] / ts - 0.02f), float(o - 1), float(o + GI::TILES));
			float hi = CLAMP(Math::floor((p_aabb.position[a] + p_aabb.size[a]) / ts + 0.02f), float(o - 1), float(o + GI::TILES));
			from[a] = MAX(int(lo), o);
			to[a] = MIN(int(hi), o + GI::TILES - 1);
			empty = empty || from[a] > to[a];
		}
		if (empty) {
			continue;
		}
		for (int z = from[2]; z <= to[2]; z++) {
			for (int y = from[1]; y <= to[1]; y++) {
				for (int x = from[0]; x <= to[0]; x++) {
					cas.dirty[_tile_slot(x, y, z)] = 1;
				}
			}
		}
	}
	s->converge = 160;
	VisualServerRaster::redraw_request(false);
}

void RasterizerSceneGLES3::_gi_clear_tiles(GI::State *p_state, const GI::Region &p_region, bool p_radiance) {
	GLuint prog = _gi_get_program(GI_PROGRAM_CLEAR);
	glUseProgram(prog);
	glBindImageTexture(4, p_state->tex_albedo, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);
	glBindImageTexture(5, p_state->tex_emission, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);
	glBindImageTexture(6, p_state->tex_radiance, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R11F_G11F_B10F);
	glBindImageTexture(7, p_state->tex_facing, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);
	glUniform3i(glGetUniformLocation(prog, "box_min"), p_region.tile_min[0] * GI::TILE, p_region.tile_min[1] * GI::TILE, p_region.tile_min[2] * GI::TILE);
	_u1i(prog, "cascade", p_region.cascade);
	_u1i(prog, "clear_radiance", p_radiance);
	glDispatchCompute(p_region.tile_size[0], p_region.tile_size[1], p_region.tile_size[2]);
}

bool RasterizerSceneGLES3::_gi_take_region(GI::State *p_state, int p_cascade, GI::Region &r_region) {
	GI::Cascade &cas = p_state->cascades[p_cascade];
	int o[3] = { cas.origin[0] / GI::TILE, cas.origin[1] / GI::TILE, cas.origin[2] / GI::TILE };
	for (int n = 0; n < GI::TILE_COUNT; n++) {
		uint32_t i = (cas.scan_cursor + n) % GI::TILE_COUNT;
		int rx = i & 7, ry = (i >> 3) & 7, rz = i >> 6;
#define GI_DIRTY(m_x, m_y, m_z) cas.dirty[_tile_slot(o[0] + (m_x), o[1] + (m_y), o[2] + (m_z))]
		if (!GI_DIRTY(rx, ry, rz)) {
			continue;
		}
		// Grow the box along each axis while everything in it is dirty.
		int sx = 1, sy = 1, sz = 1;
		while (rx + sx < GI::TILES && GI_DIRTY(rx + sx, ry, rz)) {
			sx++;
		}
		bool grow = true;
		while (grow && ry + sy < GI::TILES) {
			for (int x = 0; x < sx && grow; x++) {
				grow = GI_DIRTY(rx + x, ry + sy, rz);
			}
			sy += grow ? 1 : 0;
		}
		grow = true;
		while (grow && rz + sz < GI::TILES) {
			for (int y = 0; y < sy && grow; y++) {
				for (int x = 0; x < sx && grow; x++) {
					grow = GI_DIRTY(rx + x, ry + y, rz + sz);
				}
			}
			sz += grow ? 1 : 0;
		}
		for (int z = 0; z < sz; z++) {
			for (int y = 0; y < sy; y++) {
				for (int x = 0; x < sx; x++) {
					GI_DIRTY(rx + x, ry + y, rz + z) = 0;
				}
			}
		}
#undef GI_DIRTY
		r_region.cascade = p_cascade;
		r_region.tile_min[0] = o[0] + rx;
		r_region.tile_min[1] = o[1] + ry;
		r_region.tile_min[2] = o[2] + rz;
		r_region.tile_size[0] = sx;
		r_region.tile_size[1] = sy;
		r_region.tile_size[2] = sz;
		// Start the next scan past this box, so constantly dirty tiles cannot
		// starve the rest.
		cas.scan_cursor = (i + 1) % GI::TILE_COUNT;
		return true;
	}
	return false;
}

bool RasterizerSceneGLES3::dynamic_gi_begin(RID p_scenario, RID p_environment, const Transform &p_cam_transform, bool p_reflection_probe) {
	gi.current = nullptr;
	Environment *env = environment_owner.getornull(p_environment);
	Map<RID, GI::State *>::Element *E = gi.states.find(p_scenario);
	RasterizerStorageGLES3::RenderTarget *rt = storage->frame.current_rt;

	bool can_own = !p_reflection_probe && rt && rt->buffers.active && !rt->flags[RasterizerStorage::RENDER_TARGET_TRANSPARENT] && !rt->flags[RasterizerStorage::RENDER_TARGET_NO_3D] && !rt->flags[RasterizerStorage::RENDER_TARGET_NO_3D_EFFECTS];

	if (!env || !env->dynamic_gi_enabled) {
		// Drop the memory once the view that drove it turns the feature off.
		if (E && can_own && E->get()->owner == rt) {
			dynamic_gi_free(p_scenario);
		}
		return false;
	}

	if (E && (E->get()->cascade_count != env->dynamic_gi_cascades || E->get()->voxel_size != env->dynamic_gi_voxel_size)) {
		dynamic_gi_free(p_scenario);
		E = nullptr;
	}
	if (!E) {
		if (!can_own) {
			return false;
		}
		for (int i = GI_PROGRAM_CLEAR; i <= GI_PROGRAM_PROBES; i++) {
			if (!_gi_get_program(i)) {
				state.scene_shader.unbind();
				return false;
			}
		}
		E = gi.states.insert(p_scenario, _gi_create_state(env));
	}

	GI::State *s = E->get();
	gi.current = s;

	uint64_t frame = storage->frame.count;
	if (!can_own || state.debug_draw != VS::VIEWPORT_DEBUG_DRAW_DISABLED) {
		return false;
	}
	if (s->owner != rt) {
		if (s->owner && frame < s->last_update_frame + 3) {
			return false; // another view is driving the cascades
		}
		s->owner = rt;
	} else if (s->update_index > 0 && s->last_update_frame == frame) {
		return false; // second eye
	}
	s->last_update_frame = frame;
	s->update_index++;
	s->regions.clear();
	s->next_region = 0;

	if (s->last_camera != p_cam_transform) {
		s->last_camera = p_cam_transform;
		s->converge = 160;
	}

	glDisable(GL_SCISSOR_TEST);

	// Follow the camera in whole tiles.
	for (int c = 0; c < s->cascade_count; c++) {
		GI::Cascade &cas = s->cascades[c];
		int new_origin[3];
		bool moved = !cas.valid;
		for (int a = 0; a < 3; a++) {
			int want = int(Math::floor(p_cam_transform.origin[a] / cas.voxel_size)) - GI::RES / 2;
			new_origin[a] = cas.origin[a];
			if (!cas.valid || ABS(want - cas.origin[a]) > GI::TILE) {
				new_origin[a] = _floor_div(want + GI::TILE / 2, GI::TILE) * GI::TILE;
				moved = moved || new_origin[a] != cas.origin[a];
			}
		}
		if (!moved) {
			continue;
		}
		int o[3], old[3];
		for (int a = 0; a < 3; a++) {
			o[a] = new_origin[a] / GI::TILE;
			old[a] = cas.origin[a] / GI::TILE;
		}
		for (int z = 0; z < GI::TILES; z++) {
			for (int y = 0; y < GI::TILES; y++) {
				for (int x = 0; x < GI::TILES; x++) {
					int t[3] = { o[0] + x, o[1] + y, o[2] + z };
					bool kept = cas.valid;
					for (int a = 0; a < 3; a++) {
						kept = kept && t[a] >= old[a] && t[a] < old[a] + GI::TILES;
					}
					if (kept) {
						continue;
					}
					cas.dirty[_tile_slot(t[0], t[1], t[2])] = 1;
					if (cas.valid) {
						// The slot still holds what was on the far side.
						GI::Region r = { c, { t[0], t[1], t[2] }, { 1, 1, 1 } };
						_gi_clear_tiles(s, r, true);
					}
				}
			}
		}
		for (int a = 0; a < 3; a++) {
			cas.origin[a] = new_origin[a];
		}
		cas.valid = true;
		cas.bricks_dirty = true;
		s->retries_left = 40;
		s->converge = 160;
	}

	// Tiles drawn while some of their shaders were still compiling.
	if (s->retry.size() && --s->retry_delay <= 0) {
		for (uint32_t i = 0; i < s->retry.size(); i++) {
			const GI::Region &r = s->retry[i];
			GI::Cascade &cas = s->cascades[r.cascade];
			for (int z = 0; z < r.tile_size[2]; z++) {
				for (int y = 0; y < r.tile_size[1]; y++) {
					for (int x = 0; x < r.tile_size[0]; x++) {
						cas.dirty[_tile_slot(r.tile_min[0] + x, r.tile_min[1] + y, r.tile_min[2] + z)] = 1;
					}
				}
			}
		}
		s->retry.clear();
		s->converge = 160;
	}

	// Slow background refresh, for changes nothing reports (textures, animation).
	if (s->update_index % 16 == 0) {
		uint32_t step = s->update_index / 16;
		GI::Cascade &cas = s->cascades[step % s->cascade_count];
		int slab = (step / s->cascade_count) % GI::TILES;
		for (int i = 0; i < GI::TILES * GI::TILES; i++) {
			cas.dirty[_tile_slot(i & 7, slab, i >> 3)] = 1;
		}
	}

	for (int c = 0; c < s->cascade_count; c++) {
		GI::Region r;
		for (int n = 0; n < (c == 0 ? 2 : 1); n++) {
			if (_gi_take_region(s, c, r)) {
				s->regions.push_back(r);
			}
		}
	}
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	state.scene_shader.unbind();
	return true;
}

bool RasterizerSceneGLES3::dynamic_gi_next_region(AABB &r_aabb) {
	GI::State *s = gi.current;
	if (!s || s->next_region >= s->regions.size()) {
		return false;
	}
	const GI::Region &r = s->regions[s->next_region++];
	float ts = s->cascades[r.cascade].voxel_size * GI::TILE;
	r_aabb.position = Vector3(r.tile_min[0], r.tile_min[1], r.tile_min[2]) * ts;
	r_aabb.size = Vector3(r.tile_size[0], r.tile_size[1], r.tile_size[2]) * ts;
	return true;
}

void RasterizerSceneGLES3::dynamic_gi_render_region(RID p_environment, InstanceBase **p_cull_result, int p_cull_count) {
	GI::State *s = gi.current;
	Environment *env = environment_owner.getornull(p_environment);
	ERR_FAIL_COND(!s || !env || s->next_region == 0);
	const GI::Region r = s->regions[s->next_region - 1];
	GI::Cascade &cas = s->cascades[r.cascade];

	_gi_clear_tiles(s, r, false);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	state.scene_shader.unbind();
	cas.bricks_dirty = true;

	int count = 0;
	for (int i = 0; i < p_cull_count; i++) {
		InstanceBase *inst = p_cull_result[i];
		if ((inst->layer_mask & env->dynamic_gi_cull_mask) && inst->base_type != VS::INSTANCE_PARTICLES) {
			p_cull_result[count++] = inst;
		}
	}
	if (count == 0) {
		return;
	}

	render_pass++;
	directional_light = nullptr;
	first_directional_light = RID();
	render_list.clear();
	_fill_render_list(p_cull_result, count, false, false);

	// Opaque surfaces, plus cutouts that go through the alpha prepass. Unshaded
	// surfaces (sky domes, gizmos) neither block nor bounce light.
	LocalVector<RenderList::Element *> elements;
	for (int i = 0; i < render_list.element_count; i++) {
		RenderList::Element *e = render_list.elements[i];
		if (!(e->sort_key & SORT_KEY_UNSHADED_FLAG)) {
			elements.push_back(e);
		}
	}
	for (int i = 0; i < render_list.alpha_element_count; i++) {
		RenderList::Element *e = render_list.elements[render_list.max_elements - render_list.alpha_element_count + i];
		if (!(e->sort_key & SORT_KEY_UNSHADED_FLAG) && e->material->shader->spatial.depth_draw_mode == RasterizerStorageGLES3::Shader::Spatial::DEPTH_DRAW_ALPHA_PREPASS) {
			elements.push_back(e);
		}
	}
	if (elements.size() == 0) {
		return;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, gi.voxel_fbo);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glColorMask(0, 0, 0, 0);
	glBindImageTexture(4, s->tex_albedo, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	glBindImageTexture(5, s->tex_emission, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	glBindImageTexture(7, s->tex_facing, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

	// Keep the shadow sampler unit complete (GL UB message 131222): this can run
	// before the first scene pass has bound a shadow atlas.
	glActiveTexture(GL_TEXTURE0 + storage->config.max_texture_image_units - 6);
	glBindTexture(GL_TEXTURE_2D, storage->resources.depth_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);

	state.gi_voxel_pass = true;
	state.gi_voxel_incomplete = false;
	state.gi_voxel_size = cas.voxel_size;
	state.gi_voxel_cascade = r.cascade;
	for (int a = 0; a < 3; a++) {
		state.gi_voxel_box_min[a] = r.tile_min[a] * GI::TILE;
		state.gi_voxel_box_max[a] = (r.tile_min[a] + r.tile_size[a]) * GI::TILE;
	}
	state.scene_shader.set_ubershader_fallback_enabled(false);
	state.scene_shader.set_conditional(SceneShaderGLES3::RENDER_GI_VOXELS, true);
	state.scene_shader.set_conditional(SceneShaderGLES3::USE_MULTIPLE_RENDER_TARGETS, false);

	Vector3 bmin = Vector3(r.tile_min[0], r.tile_min[1], r.tile_min[2]) * (cas.voxel_size * GI::TILE);
	Vector3 bsize = Vector3(r.tile_size[0], r.tile_size[1], r.tile_size[2]) * (cas.voxel_size * GI::TILE);
	Vector3 bcenter = bmin + bsize * 0.5;

	// One orthographic view per axis, so every triangle faces at least one of
	// them, at two fragments per voxel.
	for (int axis = 0; axis < 3; axis++) {
		int ax = (axis + 1) % 3;
		int ay = (axis + 2) % 3;
		Transform xf;
		Vector3 bx, by, bz;
		bx[ax] = 1;
		by[ay] = 1;
		bz[axis] = 1;
		xf.basis.set_axis(0, bx);
		xf.basis.set_axis(1, by);
		xf.basis.set_axis(2, bz);
		xf.origin = bcenter;
		xf.origin[axis] = bmin[axis] + bsize[axis] + cas.voxel_size;
		CameraMatrix cm;
		cm.set_orthogonal(-bsize[ax] * 0.5, bsize[ax] * 0.5, -bsize[ay] * 0.5, bsize[ay] * 0.5, 0.01, bsize[axis] + cas.voxel_size * 2.0);

		glViewport(0, 0, r.tile_size[ax] * GI::TILE * 2, r.tile_size[ay] * GI::TILE * 2);
		_setup_environment(nullptr, cm, xf);
		_render_list(elements.ptr(), elements.size(), xf, cm, nullptr, false, false, true, false, false);
	}

	state.scene_shader.set_conditional(SceneShaderGLES3::RENDER_GI_VOXELS, false);
	state.scene_shader.set_ubershader_fallback_enabled(true);
	state.gi_voxel_pass = false;
	glColorMask(1, 1, 1, 1);
	glDepthMask(GL_TRUE);
	glBindFramebuffer(GL_FRAMEBUFFER, RasterizerStorageGLES3::system_fbo);

	if (state.gi_voxel_incomplete && s->retries_left > 0) {
		s->retries_left--;
		if (s->retry.size() == 0) {
			s->retry_delay = 20;
		}
		s->retry.push_back(r);
		s->converge = 160;
	}
}

AABB RasterizerSceneGLES3::dynamic_gi_get_bounds() {
	AABB aabb;
	if (gi.current) {
		const GI::Cascade &cas = gi.current->cascades[gi.current->cascade_count - 1];
		aabb.position = Vector3(cas.origin[0], cas.origin[1], cas.origin[2]) * cas.voxel_size;
		aabb.size = Vector3(1, 1, 1) * (cas.voxel_size * GI::RES);
	}
	return aabb;
}

void RasterizerSceneGLES3::dynamic_gi_finish(RID p_environment, const RID *p_lights, int p_light_count) {
	GI::State *s = gi.current;
	Environment *env = environment_owner.getornull(p_environment);
	ERR_FAIL_COND(!s || !env);

	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

	/* Lights */

	GI::LightData lights[GI::MAX_LIGHTS];
	memset(lights, 0, sizeof(lights));
	int light_count = 0;
	for (int i = 0; i < p_light_count && light_count < GI::MAX_LIGHTS; i++) {
		LightInstance *li = light_instance_owner.getornull(p_lights[i]);
		if (!li || li->light_ptr->bake_mode == VS::LIGHT_BAKE_DISABLED) {
			continue;
		}
		const RasterizerStorageGLES3::Light *l = li->light_ptr;
		GI::LightData &d = lights[light_count++];
		d.pos_type[0] = li->transform.origin.x;
		d.pos_type[1] = li->transform.origin.y;
		d.pos_type[2] = li->transform.origin.z;
		d.pos_type[3] = l->type == VS::LIGHT_DIRECTIONAL ? 0.0 : (l->type == VS::LIGHT_OMNI ? 1.0 : 2.0);
		Color col = l->color.to_linear();
		float energy = l->param[VS::LIGHT_PARAM_ENERGY] * l->param[VS::LIGHT_PARAM_INDIRECT_ENERGY] * (l->negative ? -1.0 : 1.0);
		d.color_inv_range[0] = col.r * energy;
		d.color_inv_range[1] = col.g * energy;
		d.color_inv_range[2] = col.b * energy;
		d.color_inv_range[3] = 1.0 / MAX(0.001, l->param[VS::LIGHT_PARAM_RANGE]);
		Vector3 dir = li->transform.basis.xform(Vector3(0, 0, -1)).normalized();
		d.dir_att[0] = dir.x;
		d.dir_att[1] = dir.y;
		d.dir_att[2] = dir.z;
		d.dir_att[3] = l->param[VS::LIGHT_PARAM_ATTENUATION];
		d.spot[0] = Math::cos(Math::deg2rad(l->param[VS::LIGHT_PARAM_SPOT_ANGLE]));
		d.spot[1] = l->param[VS::LIGHT_PARAM_SPOT_ATTENUATION];
		d.spot[2] = l->shadow ? 1.0 : 0.0;
	}

	/* Parameters */

	RasterizerStorageGLES3::Sky *sky = nullptr;
	if (env->bg_mode == VS::ENV_BG_SKY || env->bg_mode == VS::ENV_BG_COLOR_SKY) {
		sky = storage->sky_owner.getornull(env->sky);
	}
	bool sky_array = storage->config.use_texture_array_environment;

	GI::UBO ubo;
	memset(&ubo, 0, sizeof(ubo));
	ubo.config[0] = s->cascade_count;
	ubo.config[2] = env->dynamic_gi_reflection_occlusion;
	ubo.config[3] = CLAMP(env->dynamic_gi_rays / 64, 1, 3);
	ubo.params[0] = env->dynamic_gi_energy;
	ubo.params[1] = env->dynamic_gi_normal_bias;
	ubo.params[2] = env->dynamic_gi_bounce_feedback;
	ubo.params[3] = env->dynamic_gi_hysteresis;
	Color ambient = env->ambient_color.to_linear();
	ubo.ambient[0] = ambient.r * env->ambient_energy;
	ubo.ambient[1] = ambient.g * env->ambient_energy;
	ubo.ambient[2] = ambient.b * env->ambient_energy;
	ubo.ambient[3] = env->ambient_sky_contribution;
	ubo.sky[0] = env->bg_energy * env->ambient_energy;
	ubo.sky[1] = (sky && sky->radiance) ? (sky_array ? 1.0 : 2.0) : 0.0;
	ubo.sky[2] = light_count;
	ubo.sky[3] = storage->config.use_physical_light_attenuation ? 1.0 : 0.0;
	Basis sky_inv = env->sky_orientation.inverse();
	for (int col = 0; col < 3; col++) {
		for (int row = 0; row < 3; row++) {
			ubo.sky_xform[col * 4 + row] = sky_inv.elements[row][col];
		}
	}
	ubo.sky_xform[15] = 1.0;
	for (int c = 0; c < s->cascade_count; c++) {
		for (int a = 0; a < 3; a++) {
			ubo.cascade_origin[c][a] = s->cascades[c].origin[a];
		}
		ubo.cascade_size[c][0] = s->cascades[c].voxel_size;
	}

	uint32_t hash = hash_djb2_buffer((const uint8_t *)&ubo, sizeof(ubo));
	hash = hash_djb2_buffer((const uint8_t *)lights, sizeof(lights), hash);
	if (hash != s->last_hash) {
		s->last_hash = hash;
		s->converge = 160;
	}
	// Keep drawing until the probes have settled, even in an idle editor.
	if (s->converge > 0) {
		s->converge--;
		VisualServerRaster::redraw_request(false);
	}
	ubo.config[1] = s->update_index;

	glBindBuffer(GL_UNIFORM_BUFFER, s->ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ubo), &ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, s->light_ssbo);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(lights), lights);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	glBindBufferBase(GL_UNIFORM_BUFFER, 8, s->ubo);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, s->probe_ssbo);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, s->light_ssbo);

	/* Brick occupancy */

	GLuint prog = _gi_get_program(GI_PROGRAM_BRICKS);
	glUseProgram(prog);
	_gi_tex(prog, "gi_tex_albedo", 0, GL_TEXTURE_3D, s->tex_albedo);
	glBindImageTexture(4, s->tex_bricks, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R8UI);
	for (int c = 0; c < s->cascade_count; c++) {
		if (s->cascades[c].bricks_dirty) {
			s->cascades[c].bricks_dirty = false;
			_u1i(prog, "cascade", c);
			glDispatchCompute(4, 4, 4);
		}
	}
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

	/* Voxel lighting: one cascade per frame */

	prog = _gi_get_program(GI_PROGRAM_LIGHT);
	glUseProgram(prog);
	_gi_tex(prog, "gi_tex_albedo", 0, GL_TEXTURE_3D, s->tex_albedo);
	_gi_tex(prog, "gi_tex_bricks", 1, GL_TEXTURE_3D, s->tex_bricks);
	_gi_tex(prog, "gi_tex_emission", 2, GL_TEXTURE_3D, s->tex_emission);
	_gi_tex(prog, "gi_irradiance", 3, GL_TEXTURE_2D_ARRAY, s->tex_irradiance);
	_gi_tex(prog, "gi_visibility", 4, GL_TEXTURE_2D_ARRAY, s->tex_visibility);
	_gi_tex(prog, "gi_tex_facing", 5, GL_TEXTURE_3D, s->tex_facing);
	_gi_tex(prog, "gi_tex_radiance", 6, GL_TEXTURE_3D, 0);
	glBindImageTexture(6, s->tex_radiance, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R11F_G11F_B10F);
	_u1i(prog, "cascade", s->update_index % s->cascade_count);
	glDispatchCompute(16, 16, 16);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

	/* Probes */

	prog = _gi_get_program(GI_PROGRAM_PROBES);
	glUseProgram(prog);
	_gi_tex(prog, "gi_tex_albedo", 0, GL_TEXTURE_3D, s->tex_albedo);
	_gi_tex(prog, "gi_tex_bricks", 1, GL_TEXTURE_3D, s->tex_bricks);
	_gi_tex(prog, "gi_tex_radiance", 2, GL_TEXTURE_3D, s->tex_radiance);
	_gi_tex(prog, "gi_sky_array", 3, GL_TEXTURE_2D_ARRAY, (sky && sky_array && sky->radiance) ? sky->radiance : storage->resources.white_tex_array);
	_gi_tex(prog, "gi_sky_irradiance", 4, GL_TEXTURE_2D, (sky && sky->irradiance) ? sky->irradiance : storage->resources.black_tex);
	_gi_tex(prog, "gi_tex_facing", 5, GL_TEXTURE_3D, s->tex_facing);
	// Unused here, but samplers of different kinds must not share a unit.
	_gi_tex(prog, "gi_irradiance", 6, GL_TEXTURE_2D_ARRAY, s->tex_irradiance);
	_gi_tex(prog, "gi_visibility", 7, GL_TEXTURE_2D_ARRAY, s->tex_visibility);
	glBindImageTexture(4, s->tex_irradiance, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
	glBindImageTexture(5, s->tex_visibility, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RG16F);

	RandomPCG rng(s->update_index * 7919 + 17);
	Vector3 axis(rng.random(-1.0f, 1.0f), rng.random(-1.0f, 1.0f), rng.random(-1.0f, 1.0f));
	if (axis.length_squared() < 0.01) {
		axis = Vector3(0, 1, 0);
	}
	Basis rot(axis.normalized(), rng.random(0.0f, float(Math_TAU)));
	GLfloat rotf[9];
	for (int i = 0; i < 9; i++) {
		rotf[i] = rot.elements[i % 3][i / 3];
	}
	glUniformMatrix3fv(glGetUniformLocation(prog, "ray_rotation"), 1, GL_FALSE, rotf);

	// Until the first few rounds have settled, refresh every probe each frame.
	int stride = s->update_index < 24 ? 1 : int(GI::PROBE_STRIDE);
	_u1i(prog, "stride", stride);
	_u1i(prog, "first_probe", s->update_index % stride);
	for (int c = 0; c < s->cascade_count; c++) {
		_u1i(prog, "cascade", c);
		glDispatchCompute(GI::PROBES_PER_CASCADE / stride, 1, 1);
	}
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

	for (int i = 0; i < 8; i++) {
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(GL_TEXTURE_3D, 0);
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	state.scene_shader.unbind();
}

/* SSGI */

static GLuint _ssgi_texture(GLenum p_format, int p_w, int p_h, int p_mips, GLenum p_min, GLenum p_mag) {
	GLuint tex;
	glGenTextures(1, &tex);
	// The active unit may be one the scene pass relies on (shadow maps).
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexStorage2D(GL_TEXTURE_2D, p_mips, p_format, p_w, p_h);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, p_min);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, p_mag);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return tex;
}

// Adds the albedo and ambient attachments SSGI needs to the scene buffers.
bool RasterizerSceneGLES3::_ssgi_prepare_buffers(RasterizerStorageGLES3::RenderTarget *rt) {
	if (rt->buffers.gi_albedo) {
		return true;
	}
	// Needs a float target: the composite subtracts as well as adds.
	if (!rt->buffers.active || !rt->buffers.effects_active || !rt->flags[RasterizerStorage::RENDER_TARGET_HDR]) {
		return false;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, rt->buffers.fbo);
	GLint samples = 0;
	glBindRenderbuffer(GL_RENDERBUFFER, rt->buffers.diffuse);
	glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &samples);

	GLuint rb[2];
	GLenum formats[2] = { GL_RGBA8, GL_R11F_G11F_B10F };
	glGenRenderbuffers(2, rb);
	for (int i = 0; i < 2; i++) {
		glBindRenderbuffer(GL_RENDERBUFFER, rb[i]);
		if (samples == 0) {
			glRenderbufferStorage(GL_RENDERBUFFER, formats[i], rt->width, rt->height);
		} else {
			glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, formats[i], rt->width, rt->height);
		}
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4 + i, GL_RENDERBUFFER, rb[i]);
	}
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		for (int i = 0; i < 2; i++) {
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4 + i, GL_RENDERBUFFER, 0);
		}
		glDeleteRenderbuffers(2, rb);
		ERR_PRINT_ONCE("SSGI: could not attach the albedo and ambient buffers.");
		return false;
	}
	rt->buffers.gi_albedo = rb[0];
	rt->buffers.gi_ambient = rb[1];

	RasterizerStorageGLES3::RenderTarget::Effects::SSGI &e = rt->effects.ssgi;
	e.mips = 1;
	while (e.mips < 4 && (MIN(rt->width, rt->height) >> e.mips) > 0) {
		e.mips++;
	}
	// Multisampled resolves need matching formats.
	e.color = _ssgi_texture(rt->flags[RasterizerStorage::RENDER_TARGET_USE_32_BPC_DEPTH] ? GL_RGBA32F : GL_RGBA16F, rt->width, rt->height, e.mips, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);
	e.albedo = _ssgi_texture(GL_RGBA8, rt->width, rt->height, 1, GL_NEAREST, GL_NEAREST);
	e.ambient = _ssgi_texture(GL_R11F_G11F_B10F, rt->width, rt->height, 1, GL_NEAREST, GL_NEAREST);
	GLuint targets[3] = { e.color, e.albedo, e.ambient };
	glGenFramebuffers(3, e.resolve_fbo);
	for (int i = 0; i < 3; i++) {
		glBindFramebuffer(GL_FRAMEBUFFER, e.resolve_fbo[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targets[i], 0);
	}
	return true;
}

void RasterizerSceneGLES3::_render_ssgi(Environment *env, const CameraMatrix &p_cam_projection, const Transform &p_cam_transform) {
	RasterizerStorageGLES3::RenderTarget *rt = storage->frame.current_rt;
	RasterizerStorageGLES3::RenderTarget::Effects::SSGI &e = rt->effects.ssgi;
	if (!rt->buffers.gi_albedo) {
		return;
	}
	for (int i = GI_PROGRAM_SSGI_TRACE; i <= GI_PROGRAM_SSGI_COMPOSITE; i++) {
		if (!_gi_get_program(i)) {
			return;
		}
	}

	int scale = env->ssgi_half_resolution ? 2 : 1;
	int w = MAX(1, (rt->width + scale - 1) / scale);
	int h = MAX(1, (rt->height + scale - 1) / scale);
	if (e.width != w || e.height != h) {
		if (e.width) {
			glDeleteTextures(4, e.work);
			glDeleteTextures(2, e.history_depth);
		}
		e.width = w;
		e.height = h;
		for (int i = 0; i < 4; i++) {
			e.work[i] = _ssgi_texture(GL_RGBA16F, w, h, 1, GL_NEAREST, GL_NEAREST);
		}
		for (int i = 0; i < 2; i++) {
			e.history_depth[i] = _ssgi_texture(GL_R32F, w, h, 1, GL_NEAREST, GL_NEAREST);
		}
		e.history_valid = false;
		e.still_frames = 0;
	}

	// Resolve what the opaque pass wrote.
	static const GLenum sources[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT4, GL_COLOR_ATTACHMENT5 };
	glBindFramebuffer(GL_READ_FRAMEBUFFER, rt->buffers.fbo);
	for (int i = 0; i < 3; i++) {
		glReadBuffer(sources[i]);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, e.resolve_fbo[i]);
		glBlitFramebuffer(0, 0, rt->width, rt->height, 0, 0, rt->width, rt->height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, e.color);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, rt->depth);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

	int cur = e.frame & 1; // work[0..1]: history ping-pong, work[2..3]: scratch
	int prev = 1 - cur;
	Transform cam_to_prev = e.prev_camera.affine_inverse() * p_cam_transform;

	for (int pass = GI_PROGRAM_SSGI_TRACE; pass <= GI_PROGRAM_SSGI_TEMPORAL; pass++) {
		GLuint p = _gi_get_program(pass);
		glUseProgram(p);
		_gi_tex(p, "tex_depth", 0, GL_TEXTURE_2D, rt->depth);
		_gi_tex(p, "tex_normal", 1, GL_TEXTURE_2D, rt->buffers.effect);
		_gi_tex(p, "tex_color", 2, GL_TEXTURE_2D, e.color);
		_gi_tex(p, "tex_input", 3, GL_TEXTURE_2D, e.work[2]);
		_gi_tex(p, "tex_history", 4, GL_TEXTURE_2D, e.work[prev]);
		_gi_tex(p, "tex_history_depth", 5, GL_TEXTURE_2D, e.history_depth[prev]);
		_um4(p, "projection", p_cam_projection);
		_um4(p, "inv_projection", p_cam_projection.inverse());
		_um4(p, "cam_to_prev", CameraMatrix(cam_to_prev));
		_um4(p, "prev_projection", e.prev_projection);
		glUniform2i(glGetUniformLocation(p, "full_size"), rt->width, rt->height);
		glUniform2i(glGetUniformLocation(p, "size"), w, h);
		_u1i(p, "res_scale", scale);
		_u1i(p, "frame", e.frame);
		_u1i(p, "ray_count", env->ssgi_rays);
		_u1i(p, "step_count", env->ssgi_steps);
		_u1i(p, "history_valid", e.history_valid);
		_u1f(p, "radius", env->ssgi_radius);
		_u1f(p, "thickness", env->ssgi_thickness);
		_u1f(p, "blend", env->ssgi_temporal_blend);
		glBindImageTexture(0, pass == GI_PROGRAM_SSGI_TRACE ? e.work[2] : e.work[cur], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		glBindImageTexture(1, e.history_depth[cur], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
		glDispatchCompute((w + 7) / 8, (h + 7) / 8, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
	}

	GLuint p = _gi_get_program(GI_PROGRAM_SSGI_ATROUS);
	glUseProgram(p);
	_gi_tex(p, "tex_depth", 0, GL_TEXTURE_2D, rt->depth);
	_gi_tex(p, "tex_normal", 1, GL_TEXTURE_2D, rt->buffers.effect);
	_um4(p, "inv_projection", p_cam_projection.inverse());
	glUniform2i(glGetUniformLocation(p, "full_size"), rt->width, rt->height);
	glUniform2i(glGetUniformLocation(p, "size"), w, h);
	_u1i(p, "res_scale", scale);
	GLuint src = e.work[cur];
	for (int i = 0; i < 3; i++) {
		GLuint dst = e.work[2 + (i & 1)];
		_gi_tex(p, "tex_input", 3, GL_TEXTURE_2D, src);
		_u1i(p, "atrous_step", 1 << i);
		glBindImageTexture(0, dst, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		glDispatchCompute((w + 7) / 8, (h + 7) / 8, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
		src = dst;
	}

	// Add the change straight into the (possibly multisampled) diffuse buffer;
	// alpha keeps the SSAO ambient scale.
	e.result = src;
	glBindFramebuffer(GL_FRAMEBUFFER, rt->buffers.fbo);
	glViewport(0, 0, rt->width, rt->height);
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_ONE, GL_ONE);
	glColorMask(1, 1, 1, 0);
	_ssgi_draw_composite(env, p_cam_projection, false);
	glColorMask(1, 1, 1, 1);
	glDisable(GL_BLEND);

	if (e.prev_camera != p_cam_transform) {
		e.still_frames = 0;
	}
	if (e.still_frames < 48) {
		e.still_frames++;
		VisualServerRaster::redraw_request(false);
	}
	e.prev_camera = p_cam_transform;
	e.prev_projection = p_cam_projection;
	e.history_valid = true;
	e.frame++;

	for (int i = 1; i < 6; i++) {
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glActiveTexture(GL_TEXTURE0);
	state.scene_shader.unbind();
}

void RasterizerSceneGLES3::_ssgi_draw_composite(Environment *env, const CameraMatrix &p_cam_projection, bool p_show_only) {
	RasterizerStorageGLES3::RenderTarget *rt = storage->frame.current_rt;
	RasterizerStorageGLES3::RenderTarget::Effects::SSGI &e = rt->effects.ssgi;
	GLuint p = _gi_get_program(GI_PROGRAM_SSGI_COMPOSITE);
	glUseProgram(p);
	_gi_tex(p, "tex_depth", 0, GL_TEXTURE_2D, rt->depth);
	_gi_tex(p, "tex_albedo", 1, GL_TEXTURE_2D, e.albedo);
	_gi_tex(p, "tex_ambient", 2, GL_TEXTURE_2D, e.ambient);
	_gi_tex(p, "tex_input", 3, GL_TEXTURE_2D, e.result);
	glUniform2i(glGetUniformLocation(p, "full_size"), rt->width, rt->height);
	glUniform2i(glGetUniformLocation(p, "size"), e.width, e.height);
	_u1i(p, "res_scale", env->ssgi_half_resolution ? 2 : 1);
	_u1f(p, "intensity", env->ssgi_intensity);
	_u1f(p, "z_near", p_cam_projection.get_z_near());
	_u1f(p, "z_far", p_cam_projection.get_z_far());
	_u1i(p, "orthogonal", p_cam_projection.is_orthogonal());
	_u1i(p, "show_only", p_show_only);
	glBindVertexArray(gi.empty_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
}

void RasterizerSceneGLES3::_gi_draw_debug(Environment *env, const CameraMatrix &p_cam_projection, const Transform &p_cam_transform) {
	RasterizerStorageGLES3::RenderTarget *rt = storage->frame.current_rt;
	int mode = env->dynamic_gi_debug;
	bool ssgi = mode == VS::ENV_DYNAMIC_GI_DEBUG_SSGI;
	if (ssgi ? (!env->ssgi_enabled || !rt->effects.ssgi.result) : !gi.current) {
		return;
	}
	_prepare_depth_texture();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, rt->depth);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
	glBindFramebuffer(GL_FRAMEBUFFER, rt->buffers.fbo);
	glViewport(0, 0, rt->width, rt->height);
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDepthMask(GL_FALSE);

	if (ssgi) {
		_ssgi_draw_composite(env, p_cam_projection, true);
	} else {
		GLuint p = _gi_get_program(GI_PROGRAM_DEBUG);
		if (p) {
			GI::State *s = gi.current;
			glUseProgram(p);
			_gi_tex(p, "tex_depth", 0, GL_TEXTURE_2D, rt->depth);
			_gi_tex(p, "gi_tex_albedo", 1, GL_TEXTURE_3D, s->tex_albedo);
			_gi_tex(p, "gi_tex_bricks", 2, GL_TEXTURE_3D, s->tex_bricks);
			_gi_tex(p, "gi_tex_radiance", 3, GL_TEXTURE_3D, s->tex_radiance);
			_gi_tex(p, "gi_irradiance", 4, GL_TEXTURE_2D_ARRAY, s->tex_irradiance);
			_gi_tex(p, "gi_visibility", 5, GL_TEXTURE_2D_ARRAY, s->tex_visibility);
			_gi_tex(p, "gi_tex_facing", 6, GL_TEXTURE_3D, s->tex_facing);
			_um4(p, "inv_projection", p_cam_projection.inverse());
			_um4(p, "cam_matrix", CameraMatrix(p_cam_transform));
			glUniform2f(glGetUniformLocation(p, "full_size"), rt->width, rt->height);
			_u1i(p, "mode", mode);
			glBindVertexArray(gi.empty_vao);
			glDrawArrays(GL_TRIANGLES, 0, 3);
			glBindVertexArray(0);
		}
	}
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	state.scene_shader.unbind();
}
