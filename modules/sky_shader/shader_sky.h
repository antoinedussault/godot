#ifndef SHADER_SKY_H
#define SHADER_SKY_H

#include "scene/resources/material.h"
#include "scene/resources/sky.h"

// A Sky resource whose contents are produced by a "shader_type sky;" shader.
// The shader is baked into an equirectangular panorama that feeds the regular
// radiance / reflection pipeline, so it also lights the scene.
//
// GLES3 only. Under GLES2 the sky is left unchanged (use PanoramaSky/ProceduralSky).
class ShaderSky : public Sky {
	GDCLASS(ShaderSky, Sky);

public:
	enum ProcessMode {
		PROCESS_MODE_ON_CHANGE, // re-bake only when the material or its params change
		PROCESS_MODE_REALTIME, // re-bake every frame (for animated skies)
	};

private:
	RID sky;
	Ref<ShaderMaterial> sky_material;
	int panorama_size;
	ProcessMode process_mode;

	void _update();

protected:
	static void _bind_methods();
	virtual void _radiance_changed();

	void _material_changed();

public:
	void set_sky_material(const Ref<ShaderMaterial> &p_material);
	Ref<ShaderMaterial> get_sky_material() const;

	void set_panorama_size(int p_size);
	int get_panorama_size() const;

	void set_process_mode(ProcessMode p_mode);
	ProcessMode get_process_mode() const;

	virtual RID get_rid() const;

	ShaderSky();
	~ShaderSky();
};

VARIANT_ENUM_CAST(ShaderSky::ProcessMode)

#endif // SHADER_SKY_H
