#ifndef TREE_IMPORTER_H
#define TREE_IMPORTER_H
#ifdef TOOLS_ENABLED

#include "editor/import/editor_import_plugin.h"

// EditorImportPlugin for tree GLB/GLTF files.
//
// Naming convention expected in the source file:
//   *-lod0 .. *-lod{n}       — MeshInstance for each LOD level.
//   *-imposter / *_imposter  — Billboard/imposter MeshInstance (optional).
//
// Output: a FoliageType .tres resource (TYPE_TREE) with tree_scene pointing
// at the source .glb directly. Mesh extraction and MultiMesh building happen
// at CellFoliage::rebuild() time, not at import time.
//
// Select "LowDot Tree" in the Import dock for any tree .glb to use this importer.
class TreeImporter : public EditorImportPlugin {
	GDCLASS(TreeImporter, EditorImportPlugin);

protected:
	static void _bind_methods();

public:
	virtual String get_importer_name() const;
	virtual String get_visible_name() const;
	virtual void get_recognized_extensions(List<String> *p_extensions) const;
	virtual String get_preset_name(int p_idx) const;
	virtual int get_preset_count() const;
	virtual String get_save_extension() const;
	virtual String get_resource_type() const;
	virtual float get_priority() const;
	virtual int get_import_order() const;
	virtual void get_import_options(List<ImportOption> *r_options, int p_preset) const;
	virtual bool get_option_visibility(const String &p_option,
			const Map<StringName, Variant> &p_options) const;
	virtual Error import(const String &p_source_file, const String &p_save_path,
			const Map<StringName, Variant> &p_options,
			List<String> *r_platform_variants,
			List<String> *r_gen_files);
};

#endif // TOOLS_ENABLED
#endif // TREE_IMPORTER_H
