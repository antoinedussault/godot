/* exp_height_fog.cpp */
#include "exp_height_fog.h"

#include "core/engine.h"
#include "scene/3d/camera.h"
#include "servers/visual_server.h"

// Godot 3.x spatial shader. Runs as a fullscreen pass via a quad placed in
// front of the camera. Reconstructs world position from DEPTH_TEXTURE and
// applies a UE-style exponential height fog integral.
static const char *FOG_SHADER_CODE =
		"shader_type spatial;\n"
		"render_mode unshaded, depth_test_disable, cull_disabled, blend_mix;\n"
		"\n"
		"uniform bool fog_enabled = true;\n"
		"uniform float density = 0.04;\n"
		"uniform float falloff = 0.15;\n"
		"uniform float ref_height = 0.0;\n"
		"uniform float start_distance = 0.0;\n"
		"uniform float max_opacity = 1.0;\n"
		"uniform vec4 inscatter : hint_color = vec4(0.55, 0.62, 0.72, 1.0);\n"
		"\n"
		"uniform bool directional_enabled = false;\n"
		"uniform vec4 directional_color : hint_color = vec4(1.0, 0.9, 0.7, 1.0);\n"
		"uniform vec3 light_direction = vec3(0.0, -1.0, 0.0);\n"
		"uniform float directional_exponent = 4.0;\n"
		"\n"
		"void vertex() {\n"
		"    // Place the quad in front of the camera in view space and keep it\n"
		"    // glued there. POSITION (clip-space passthrough) draws fullscreen.\n"
		"    POSITION = vec4(VERTEX.xy, 1.0, 1.0);\n"
		"}\n"
		"\n"
		"void fragment() {\n"
		"    if (!fog_enabled) {\n"
		"        ALPHA = 0.0;\n"
		"    } else {\n"
		"        // Reconstruct view-space position from depth.\n"
		"        float depth = texture(DEPTH_TEXTURE, SCREEN_UV).r;\n"
		"        vec3 ndc = vec3(SCREEN_UV * 2.0 - 1.0, depth * 2.0 - 1.0);\n"
		"        vec4 view = INV_PROJECTION_MATRIX * vec4(ndc, 1.0);\n"
		"        view.xyz /= view.w;\n"
		"\n"
		"        // To world space.\n"
		"        vec3 frag_world = (CAMERA_MATRIX * vec4(view.xyz, 1.0)).xyz;\n"
		"        vec3 cam_world = CAMERA_MATRIX[3].xyz;\n"
		"\n"
		"        vec3 ray = frag_world - cam_world;\n"
		"        float ray_len = length(ray);\n"
		"        float effective_len = max(0.0, ray_len - start_distance);\n"
		"\n"
		"        float fog_amount = 0.0;\n"
		"        if (effective_len > 0.0) {\n"
		"            float fo = max(falloff, 0.0001);\n"
		"            float dz = ray.y; // Godot is Y-up.\n"
		"            float d_cam = density * exp(-fo * (cam_world.y - ref_height));\n"
		"            float fd = fo * dz;\n"
		"            float integral;\n"
		"            if (abs(fd) > 0.0001) {\n"
		"                integral = d_cam * (1.0 - exp(-fd)) / fd;\n"
		"            } else {\n"
		"                integral = d_cam;\n"
		"            }\n"
		"            float optical_depth = integral * effective_len;\n"
		"            fog_amount = clamp(1.0 - exp(-optical_depth), 0.0, max_opacity);\n"
		"        }\n"
		"\n"
		"        vec3 fog_color = inscatter.rgb;\n"
		"        if (directional_enabled) {\n"
		"            vec3 vdir = normalize(ray);\n"
		"            vec3 ldir = normalize(-light_direction);\n"
		"            float d = clamp(dot(vdir, ldir), 0.0, 1.0);\n"
		"            float g = pow(d, directional_exponent);\n"
		"            fog_color = mix(inscatter.rgb, directional_color.rgb, g);\n"
		"        }\n"
		"\n"
		"        ALBEDO = fog_color;\n"
		"        ALPHA = fog_amount;\n"
		"    }\n"
		"}\n";

void ExpHeightFog::_build_resources() {
	if (shader.is_null()) {
		shader.instance();
		shader->set_code(FOG_SHADER_CODE);
	}
	if (material.is_null()) {
		material.instance();
		material->set_shader(shader);
	}
	if (quad_mesh.is_null()) {
		// A simple [-1,1] quad; the vertex() stage maps it to clip space.
		quad_mesh.instance();
		PoolVector<Vector3> verts;
		verts.push_back(Vector3(-1, -1, 0));
		verts.push_back(Vector3(3, -1, 0)); // oversized tri-style quad
		verts.push_back(Vector3(-1, 3, 0));

		PoolVector<int> indices;
		indices.push_back(0);
		indices.push_back(1);
		indices.push_back(2);

		Array arr;
		arr.resize(VS::ARRAY_MAX);
		arr[VS::ARRAY_VERTEX] = verts;
		arr[VS::ARRAY_INDEX] = indices;
		quad_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arr);
		quad_mesh->surface_set_material(0, material);

		// Huge AABB so the renderer never frustum-culls the fog quad.
		AABB big(Vector3(-100000, -100000, -100000), Vector3(200000, 200000, 200000));
		quad_mesh->set_custom_aabb(big);
	}
}

void ExpHeightFog::_update_material_params() {
	if (material.is_null()) {
		return;
	}
	material->set_shader_param("fog_enabled", fog_enabled);
	material->set_shader_param("density", density);
	material->set_shader_param("falloff", falloff);
	material->set_shader_param("ref_height", ref_height);
	material->set_shader_param("start_distance", start_distance);
	material->set_shader_param("max_opacity", max_opacity);
	material->set_shader_param("inscatter", inscatter.to_linear());

	material->set_shader_param("directional_enabled", directional_enabled);
	material->set_shader_param("directional_color", directional_color.to_linear());
	material->set_shader_param("light_direction", light_direction.normalized());
	material->set_shader_param("directional_exponent", directional_exponent);
}

void ExpHeightFog::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_WORLD: {
			_build_resources();
			set_base(quad_mesh->get_rid());
			_update_material_params();
		} break;
		case NOTIFICATION_EXIT_WORLD: {
			set_base(RID());
		} break;
	}
}

AABB ExpHeightFog::get_aabb() const {
	return AABB(Vector3(-100000, -100000, -100000), Vector3(200000, 200000, 200000));
}

PoolVector<Face3> ExpHeightFog::get_faces(uint32_t p_usage_flags) const {
	return PoolVector<Face3>();
}

// ---- Setters / getters ----

void ExpHeightFog::set_fog_enabled(bool p_enabled) {
	fog_enabled = p_enabled;
	_update_material_params();
}
bool ExpHeightFog::is_fog_enabled() const { return fog_enabled; }

void ExpHeightFog::set_density(float p_v) {
	density = p_v;
	_update_material_params();
}
float ExpHeightFog::get_density() const { return density; }

void ExpHeightFog::set_falloff(float p_v) {
	falloff = p_v;
	_update_material_params();
}
float ExpHeightFog::get_falloff() const { return falloff; }

void ExpHeightFog::set_ref_height(float p_v) {
	ref_height = p_v;
	_update_material_params();
}
float ExpHeightFog::get_ref_height() const { return ref_height; }

void ExpHeightFog::set_start_distance(float p_v) {
	start_distance = p_v;
	_update_material_params();
}
float ExpHeightFog::get_start_distance() const { return start_distance; }

void ExpHeightFog::set_max_opacity(float p_v) {
	max_opacity = p_v;
	_update_material_params();
}
float ExpHeightFog::get_max_opacity() const { return max_opacity; }

void ExpHeightFog::set_inscatter(const Color &p_c) {
	inscatter = p_c;
	_update_material_params();
}
Color ExpHeightFog::get_inscatter() const { return inscatter; }

void ExpHeightFog::set_directional_enabled(bool p_enabled) {
	directional_enabled = p_enabled;
	_update_material_params();
}
bool ExpHeightFog::is_directional_enabled() const { return directional_enabled; }

void ExpHeightFog::set_directional_color(const Color &p_c) {
	directional_color = p_c;
	_update_material_params();
}
Color ExpHeightFog::get_directional_color() const { return directional_color; }

void ExpHeightFog::set_light_direction(const Vector3 &p_dir) {
	light_direction = p_dir;
	_update_material_params();
}
Vector3 ExpHeightFog::get_light_direction() const { return light_direction; }

void ExpHeightFog::set_directional_exponent(float p_v) {
	directional_exponent = p_v;
	_update_material_params();
}
float ExpHeightFog::get_directional_exponent() const { return directional_exponent; }

void ExpHeightFog::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_fog_enabled", "enabled"), &ExpHeightFog::set_fog_enabled);
	ClassDB::bind_method(D_METHOD("is_fog_enabled"), &ExpHeightFog::is_fog_enabled);
	ClassDB::bind_method(D_METHOD("set_density", "density"), &ExpHeightFog::set_density);
	ClassDB::bind_method(D_METHOD("get_density"), &ExpHeightFog::get_density);
	ClassDB::bind_method(D_METHOD("set_falloff", "falloff"), &ExpHeightFog::set_falloff);
	ClassDB::bind_method(D_METHOD("get_falloff"), &ExpHeightFog::get_falloff);
	ClassDB::bind_method(D_METHOD("set_ref_height", "ref_height"), &ExpHeightFog::set_ref_height);
	ClassDB::bind_method(D_METHOD("get_ref_height"), &ExpHeightFog::get_ref_height);
	ClassDB::bind_method(D_METHOD("set_start_distance", "start"), &ExpHeightFog::set_start_distance);
	ClassDB::bind_method(D_METHOD("get_start_distance"), &ExpHeightFog::get_start_distance);
	ClassDB::bind_method(D_METHOD("set_max_opacity", "max_opacity"), &ExpHeightFog::set_max_opacity);
	ClassDB::bind_method(D_METHOD("get_max_opacity"), &ExpHeightFog::get_max_opacity);
	ClassDB::bind_method(D_METHOD("set_inscatter", "color"), &ExpHeightFog::set_inscatter);
	ClassDB::bind_method(D_METHOD("get_inscatter"), &ExpHeightFog::get_inscatter);

	ClassDB::bind_method(D_METHOD("set_directional_enabled", "enabled"), &ExpHeightFog::set_directional_enabled);
	ClassDB::bind_method(D_METHOD("is_directional_enabled"), &ExpHeightFog::is_directional_enabled);
	ClassDB::bind_method(D_METHOD("set_directional_color", "color"), &ExpHeightFog::set_directional_color);
	ClassDB::bind_method(D_METHOD("get_directional_color"), &ExpHeightFog::get_directional_color);
	ClassDB::bind_method(D_METHOD("set_light_direction", "dir"), &ExpHeightFog::set_light_direction);
	ClassDB::bind_method(D_METHOD("get_light_direction"), &ExpHeightFog::get_light_direction);
	ClassDB::bind_method(D_METHOD("set_directional_exponent", "exponent"), &ExpHeightFog::set_directional_exponent);
	ClassDB::bind_method(D_METHOD("get_directional_exponent"), &ExpHeightFog::get_directional_exponent);

	ADD_GROUP("Fog", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fog_enabled"), "set_fog_enabled", "is_fog_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "density", PROPERTY_HINT_RANGE, "0,1,0.0001"), "set_density", "get_density");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "falloff", PROPERTY_HINT_RANGE, "0,2,0.001"), "set_falloff", "get_falloff");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ref_height"), "set_ref_height", "get_ref_height");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "start_distance", PROPERTY_HINT_RANGE, "0,10000,0.1"), "set_start_distance", "get_start_distance");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "max_opacity", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_max_opacity", "get_max_opacity");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "inscatter"), "set_inscatter", "get_inscatter");

	ADD_GROUP("Directional Inscattering", "directional_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "directional_enabled"), "set_directional_enabled", "is_directional_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "directional_color"), "set_directional_color", "get_directional_color");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "directional_light_direction"), "set_light_direction", "get_light_direction");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "directional_exponent", PROPERTY_HINT_RANGE, "1,32,0.1"), "set_directional_exponent", "get_directional_exponent");
}

ExpHeightFog::ExpHeightFog() {
	set_notify_transform(false);
}

ExpHeightFog::~ExpHeightFog() {
}
