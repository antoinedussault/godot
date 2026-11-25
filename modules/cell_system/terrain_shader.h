#ifndef TERRAIN_SHADER_H
#define TERRAIN_SHADER_H

// 16-layer PBR terrain shader with height-based blending.
// Textures are packed into 2 TextureArrays:
//   albedo_arr  : RGB = albedo, A = height
//   normal_arr  : RGB = normal map (tangent space), A = roughness
// Plus 4 splatmaps (RGBA each = 16 layer weights), and 2 tiny (16x1) lookup
// textures carrying each CellTerrainLayer's own per-layer shading knobs —
// Godot 3.x shader uniforms cannot be arrays ("Uniform arrays are not yet
// implemented"), so normal_strength/parallax_scale/roughness_scale/uv_scale/
// color_mod are packed one column per layer instead of 16 separate uniforms:
//   layer_params : R = normal_strength, G = parallax_scale, B = roughness_scale,
//                  A = uv_scale (multiplies the global texture_scale)
//   layer_color  : RGBA = color_mod (tint multiplied into albedo)
static const char *TERRAIN_SHADER_CODE = R"(
shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;

uniform sampler2DArray albedo_arr : hint_albedo;
uniform sampler2DArray normal_arr : hint_normal;
// Per-cell painted albedo tint (TerrainBrush::MODE_COLOR), multiplied over
// the blended layer albedo. hint_albedo so the painted sRGB colour decodes
// the same way a layer texture does. White is the identity - the C++ side
// always binds something here (a 4x4 white for unpainted cells), because an
// unbound sampler2D samples BLACK and this is a multiply.
uniform sampler2D color_map : hint_albedo;
uniform sampler2D splatmap0;
uniform sampler2D splatmap1;
uniform sampler2D splatmap2;
uniform sampler2D splatmap3;
uniform sampler2D layer_params; // 16x1, nearest/clamp — see header comment above
uniform sampler2D layer_color;  // 16x1, nearest/clamp
uniform float texture_scale = 16.0;
uniform int num_layers = 4;
uniform float height_blend_sharpness = 0.2; // smaller = sharper transitions
// Per-cell normal map baked from the FULL-resolution heightmap (see
// CellTerrain::_update_detail_normal_texture), sampled 1:1 across UV (not
// tiled). At coarse LOD the mesh itself has decimated vertices — this restores
// the bumps/dips that got skipped over so lighting still reads as detailed.
// Encoded relative to a flat XZ reference plane (T=+X, B=+Z, N=+Y), same
// convention as NORMALMAP, so it composes directly via whiteout blending.
uniform sampler2D detail_normal_map : hint_normal;
uniform float detail_normal_strength = 1.0;
// Max view distance (world units) at which parallax still applies. Past it the
// ray march is skipped entirely (eff_parallax hits 0, so the `if` below is
// never entered) — this is both a look control and the main POM perf knob.
// Parallax fades over the last PARALLAX_FADE_RANGE units approaching it rather
// than switching off hard, so there is no visible pop at the boundary.
uniform float parallax_max_distance = 48.0;

// --- Debug views (0 = off) ---
// 1 = SPLAT       : each layer flat-shaded a distinct hue, mixed by the SAME
//                   blend weights the real shading uses, so layer transitions
//                   show exactly where they actually fall.
// 2 = DETAIL_NORM : the per-cell heightmap-baked detail normal, raw RGB.
// 3 = NAV_MASK    : green = navigable, red = painted out of the navmesh.
// Unlit + full roughness so the map reads flat instead of being shaded.
uniform int debug_view = 0;
// Only sampled when debug_view == 3, so it is safe to leave unbound otherwise
// (an unbound sampler2D reads black, which would just render "all excluded").
uniform sampler2D nav_mask_map;

// Distinct, evenly-spread hue per layer index — no palette table needed, and
// it stays distinguishable for all 16 layers.
vec3 debug_layer_color(int idx) {
	float h = fract(float(idx) * 0.6180339887); // golden-ratio hue spacing
	vec3 k = vec3(3.0, 2.0, 1.0) / 3.0;
	vec3 p = abs(fract(vec3(h) + k) * 6.0 - 3.0);
	return clamp(p - 1.0, 0.0, 1.0);
}
const float PARALLAX_FADE_RANGE = 30.0;

// Macro variation: a large-scale (world-space, not UV-tiled — see vertex())
// procedural noise tints the whole blended albedo between two colors, same
// idea as Unreal/Terrain3D's "macro variation" — it breaks up the otherwise
// obviously-repeating look of a tiled texture at a much bigger scale than any
// one layer's own tiling. No texture asset needed (this module has no art
// pipeline): a cheap hash-based value noise stands in for one.
// macro_variation_scale <= 0 disables it entirely (same convention as
// parallax_scale).
uniform vec4 macro_variation_color1 : hint_color = vec4(1.0);
uniform vec4 macro_variation_color2 : hint_color = vec4(1.0);
uniform float macro_variation_scale = 0.0;

varying vec2 v_world_xz;

void vertex() {
	v_world_xz = (WORLD_MATRIX * vec4(VERTEX, 1.0)).xz;
}

vec4 layer_params_at(int i) {
	return texture(layer_params, vec2((float(i) + 0.5) / 16.0, 0.5));
}
vec4 layer_color_at(int i) {
	return texture(layer_color, vec2((float(i) + 0.5) / 16.0, 0.5));
}

float macro_hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float macro_noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	float a = macro_hash(i);
	float b = macro_hash(i + vec2(1.0, 0.0));
	float c = macro_hash(i + vec2(0.0, 1.0));
	float d = macro_hash(i + vec2(1.0, 1.0));
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

void fragment() {
	vec4 s0 = texture(splatmap0, UV);
	vec4 s1 = texture(splatmap1, UV);
	vec4 s2 = texture(splatmap2, UV);
	vec4 s3 = texture(splatmap3, UV);

	float weights[16];
	weights[0]  = s0.r; weights[1]  = s0.g; weights[2]  = s0.b; weights[3]  = s0.a;
	weights[4]  = s1.r; weights[5]  = s1.g; weights[6]  = s1.b; weights[7]  = s1.a;
	weights[8]  = s2.r; weights[9]  = s2.g; weights[10] = s2.b; weights[11] = s2.a;
	weights[12] = s3.r; weights[13] = s3.g; weights[14] = s3.b; weights[15] = s3.a;

	float total = 0.0;
	for (int i = 0; i < 16; i++) total += weights[i];
	if (total < 0.001) { weights[0] = 1.0; total = 1.0; }
	for (int i = 0; i < 16; i++) weights[i] /= total; // normalized so sum == 1

	vec2 base_uv = UV * texture_scale;

	// --- Parallax occlusion mapping (blend-aware) ---
	// The height field ray-marched below is the WEIGHT-BLENDED height of every
	// contributing layer, not the single "dominant" layer's. Picking a dominant
	// layer by argmax (what this used to do) makes the height source, the
	// parallax strength, and the UV space all flip discontinuously across the
	// 50/50 line between two layers — which is exactly what made layer
	// transitions look torn/swimming. Everything here is continuous in
	// `weights`, so a transition is now as smooth as the splat blend itself.
	//
	// The offset is accumulated in UN-TILED base_uv space and each layer scales
	// it by its own uv_scale at lookup time (below), so every layer is shifted
	// by the same PHYSICAL distance instead of the dominant layer's tiled units.
	vec2 parallax_offset = vec2(0.0);

	// Weighted parallax strength — continuous, unlike the old dominant-layer pick.
	float eff_parallax = 0.0;
	for (int i = 0; i < num_layers && i < 16; i++) {
		if (weights[i] > 0.001) eff_parallax += weights[i] * layer_params_at(i).g;
	}

	// Distance fade: parallax only reads up close. Fading it out avoids the
	// grazing-angle smearing that shows on distant / edge-on terrain and saves GPU.
	float pdist = length(VERTEX);
	// Fade band is clamped to the max distance itself, so a very short
	// parallax_max_distance still fades in over its whole range instead of
	// producing a negative start (which would leave parallax permanently
	// half-faded even up close).
	float fade_range = min(PARALLAX_FADE_RANGE, parallax_max_distance);
	float fade_start = parallax_max_distance - fade_range;
	float pfade = 1.0 - clamp((pdist - fade_start) / max(0.001, fade_range), 0.0, 1.0);
	eff_parallax *= pfade;

	if (eff_parallax > 0.0001) {
		// View direction in tangent space (points toward the camera).
		vec3 vts = normalize(vec3(dot(VIEW, TANGENT), dot(VIEW, BINORMAL), dot(VIEW, NORMAL)));
		const int MAX_STEPS = 32;
		// More steps at grazing angles where parallax is most visible.
		float n_steps = mix(float(MAX_STEPS), 8.0, clamp(abs(vts.z), 0.0, 1.0));
		float layer_depth = 1.0 / n_steps;
		// Offset-LIMITED parallax: scale by vts.xy directly (no division by vts.z).
		// This bounds the total UV offset to eff_parallax instead of exploding at
		// grazing angles, which is what produced the smeared edges.
		vec2 delta = vts.xy * eff_parallax / n_steps;

		// Blended height at the starting point. Sampling is inlined rather than
		// factored into a helper because Godot 3.x's shader language has no
		// array function parameters (and no #define), so `weights` can't be
		// passed. Each layer samples at ITS OWN tiling (uv * uv_scale_i) while
		// the marched offset stays in un-tiled base_uv space.
		float cur_depth = 0.0;
		vec2 cur_off = vec2(0.0);
		float cur_h = 0.0;
		{
			float h = 0.0;
			for (int i = 0; i < num_layers && i < 16; i++) {
				if (weights[i] > 0.001)
					h += weights[i] * texture(albedo_arr, vec3(base_uv * layer_params_at(i).a, float(i))).a;
			}
			cur_h = 1.0 - h;
		}
		// prev_h is carried through the loop instead of re-sampling the height
		// field a third time after it, which would cost another full blended fetch.
		float prev_h = cur_h;
		for (int st = 0; st < MAX_STEPS; st++) {
			if (float(st) >= n_steps || cur_depth >= cur_h) break;
			prev_h = cur_h;
			cur_off -= delta;
			float h = 0.0;
			for (int i = 0; i < num_layers && i < 16; i++) {
				if (weights[i] > 0.001)
					h += weights[i] * texture(albedo_arr, vec3((base_uv + cur_off) * layer_params_at(i).a, float(i))).a;
			}
			cur_h = 1.0 - h;
			cur_depth += layer_depth;
		}
		// Interpolate between the last two samples for a smooth intersection.
		vec2 prev_off = cur_off + delta;
		float after  = cur_h - cur_depth;
		float before = prev_h - (cur_depth - layer_depth);
		float denom = after - before;
		float wgt = (abs(denom) > 0.0001) ? clamp(after / denom, 0.0, 1.0) : 0.0;
		parallax_offset = mix(cur_off, prev_off, wgt);
	}

	// --- Height-based blending ---
	// albedo.a = height, normal.a = roughness.
	float heights[16];
	float max_h = -1.0;
	for (int i = 0; i < num_layers && i < 16; i++) {
		float w = weights[i];
		if (w > 0.001) {
			vec2 layer_uv = (base_uv + parallax_offset) * layer_params_at(i).a;
			float hm = texture(albedo_arr, vec3(layer_uv, float(i))).a;
			heights[i] = w + hm * w; // height contributes scaled by weight
		} else {
			heights[i] = 0.0;
		}
		if (heights[i] > max_h) max_h = heights[i];
	}

	// Blend factors: layers near the top height get drawn.
	float blend[16];
	float blend_total = 0.0;
	for (int i = 0; i < num_layers && i < 16; i++) {
		float b = max(0.0, heights[i] - (max_h - height_blend_sharpness));
		blend[i] = b;
		blend_total += b;
	}
	if (blend_total < 0.001) { blend[0] = 1.0; blend_total = 1.0; }

	// --- Accumulate albedo, roughness, normal ---
	vec3 albedo = vec3(0.0);
	float roughness = 0.0;
	vec3 nrm = vec3(0.0, 0.0, 0.0);

	for (int i = 0; i < num_layers && i < 16; i++) {
		float b = blend[i] / blend_total;
		if (b > 0.001) {
			vec4 params = layer_params_at(i);
			vec4 tint = layer_color_at(i);
			vec2 layer_uv = (base_uv + parallax_offset) * params.a; // params.a = uv_scale

			vec4 alb = texture(albedo_arr, vec3(layer_uv, float(i)));
			albedo += alb.rgb * tint.rgb * b;
			vec4 ntex = texture(normal_arr, vec3(layer_uv, float(i)));
			roughness += ntex.a * params.b * b; // params.b = roughness_scale
			vec3 n = ntex.rgb * 2.0 - 1.0;
			n.xy *= params.r; // params.r = normal_strength
			nrm += n * b;
		}
	}

	// Macro variation is applied ONCE, in the shading branch below. It used to
	// be applied here as well, which squared the tint for every lit pixel.

	// Godot 3.x forbids `return` inside fragment() ("results in undefined
	// behavior"), so the debug overlay and the normal shading path are an
	// if/else pair rather than an early-out.
	if (debug_view != 0) {
		vec3 dbg = vec3(0.0);
		if (debug_view == 1) {
			// Same blend weights the real shading uses, so transitions land
			// exactly where they actually do.
			for (int i = 0; i < num_layers && i < 16; i++) {
				dbg += debug_layer_color(i) * (blend[i] / blend_total);
			}
		} else if (debug_view == 2) {
			dbg = texture(detail_normal_map, UV).rgb;
		} else {
			// nav_mask: 1 = navigable (green), 0 = painted out (red).
			float m = texture(nav_mask_map, UV).r;
			dbg = mix(vec3(0.9, 0.1, 0.1), vec3(0.1, 0.9, 0.2), m);
		}
		// Emission as well as albedo so the map reads flat and legible under
		// any lighting / time of day instead of going dark in shadow.
		ALBEDO = dbg;
		EMISSION = dbg;
		ROUGHNESS = 1.0;
		METALLIC = 0.0;
		SPECULAR = 0.0;
		NORMALMAP = vec3(0.5, 0.5, 1.0);
	} else {
		if (macro_variation_scale > 0.0001) {
			float mv = macro_noise(v_world_xz * macro_variation_scale);
			albedo *= mix(macro_variation_color1.rgb, macro_variation_color2.rgb, mv);
		}

		// Painted colour tint. Comes last so it modulates the finished blend
		// rather than any single layer.
		albedo *= texture(color_map, UV).rgb;

		ALBEDO = albedo;
		ROUGHNESS = clamp(roughness, 0.0, 1.0);

		// Heightmap detail normal: baked at full resolution regardless of the
		// current mesh LOD, so it restores shape detail lost to vertex decimation.
		// The C++ side always binds a real texture here — either the per-cell bake,
		// or an explicit flat/neutral (0.5,0.5,1.0) placeholder for HLOD chunks and
		// the shared base material — an unbound sampler2D would sample as black and
		// decode to (-1,-1,-1), so this uniform must never be left unbound.
		vec3 hnrm = texture(detail_normal_map, UV).rgb * 2.0 - 1.0;
		hnrm.xy *= detail_normal_strength;
		hnrm.z = max(hnrm.z, 0.001);

		vec3 n;
		if (length(nrm) > 0.001) {
			n = normalize(nrm);
			// Tangent-space Z must be positive (pointing out of surface).
			// A null/unbound normal_arr returns (0,0,0,0) which gives n.z = -1;
			// clamping here prevents the surface going almost black under lighting.
			n.z = max(n.z, 0.001);
		} else {
			n = vec3(0.0, 0.0, 1.0);
		}
		// Whiteout blend: combine the material's micro-detail normal with the
		// heightmap's macro-detail normal (add XY, multiply Z, renormalize).
		vec3 combined = normalize(vec3(n.xy + hnrm.xy, n.z * hnrm.z));
		NORMALMAP = combined * 0.5 + 0.5;
	}
}
)";

#endif // TERRAIN_SHADER_H
