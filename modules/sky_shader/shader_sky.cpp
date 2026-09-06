#include "shader_sky.h"

#include "core/core_string_names.h"
#include "servers/visual_server.h"

void ShaderSky::_update() {
	static const int rsize[RADIANCE_SIZE_MAX] = {
		32, 64, 128, 256, 512, 1024, 2048
	};

	RID material_rid;
	if (sky_material.is_valid()) {
		material_rid = sky_material->get_rid();
	}

	VisualServer::get_singleton()->sky_set_material(
			sky,
			material_rid,
			panorama_size,
			rsize[get_radiance_size()],
			process_mode == PROCESS_MODE_REALTIME ? VisualServer::SKY_PROCESS_MODE_REALTIME : VisualServer::SKY_PROCESS_MODE_BAKE_ONCE);
}

void ShaderSky::_radiance_changed() {
	_update();
}

void ShaderSky::_material_changed() {
	_update();
}

void ShaderSky::set_sky_material(const Ref<ShaderMaterial> &p_material) {
	if (sky_material == p_material) {
		return;
	}

	if (sky_material.is_valid() && sky_material->is_connected(CoreStringNames::get_singleton()->changed, this, "_material_changed")) {
		sky_material->disconnect(CoreStringNames::get_singleton()->changed, this, "_material_changed");
	}

	sky_material = p_material;

	if (sky_material.is_valid()) {
		sky_material->connect(CoreStringNames::get_singleton()->changed, this, "_material_changed");
	}

	_update();
	_change_notify();
}

Ref<ShaderMaterial> ShaderSky::get_sky_material() const {
	return sky_material;
}

void ShaderSky::set_panorama_size(int p_size) {
	p_size = CLAMP(p_size, 32, 2048);
	if (panorama_size == p_size) {
		return;
	}
	panorama_size = p_size;
	_update();
}

int ShaderSky::get_panorama_size() const {
	return panorama_size;
}

void ShaderSky::set_process_mode(ProcessMode p_mode) {
	if (process_mode == p_mode) {
		return;
	}
	process_mode = p_mode;
	_update();
}

ShaderSky::ProcessMode ShaderSky::get_process_mode() const {
	return process_mode;
}

RID ShaderSky::get_rid() const {
	return sky;
}

void ShaderSky::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_sky_material", "material"), &ShaderSky::set_sky_material);
	ClassDB::bind_method(D_METHOD("get_sky_material"), &ShaderSky::get_sky_material);

	ClassDB::bind_method(D_METHOD("set_panorama_size", "size"), &ShaderSky::set_panorama_size);
	ClassDB::bind_method(D_METHOD("get_panorama_size"), &ShaderSky::get_panorama_size);

	ClassDB::bind_method(D_METHOD("set_process_mode", "mode"), &ShaderSky::set_process_mode);
	ClassDB::bind_method(D_METHOD("get_process_mode"), &ShaderSky::get_process_mode);

	ClassDB::bind_method(D_METHOD("_material_changed"), &ShaderSky::_material_changed);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sky_material", PROPERTY_HINT_RESOURCE_TYPE, "ShaderMaterial"), "set_sky_material", "get_sky_material");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "radiance_panorama_size", PROPERTY_HINT_ENUM, "64:64,128:128,256:256,512:512"), "set_panorama_size", "get_panorama_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "process_mode", PROPERTY_HINT_ENUM, "On Change,Realtime"), "set_process_mode", "get_process_mode");

	BIND_ENUM_CONSTANT(PROCESS_MODE_ON_CHANGE);
	BIND_ENUM_CONSTANT(PROCESS_MODE_REALTIME);
}

ShaderSky::ShaderSky() {
	panorama_size = 128;
	process_mode = PROCESS_MODE_ON_CHANGE;
	sky = VisualServer::get_singleton()->sky_create();
	_update();
}

ShaderSky::~ShaderSky() {
	VisualServer::get_singleton()->free(sky);
}
