#ifndef TREE_IMPORTER_H
#define TREE_IMPORTER_H
#ifdef TOOLS_ENABLED

#include "editor/import/editor_import_plugin.h"

// "Tuna Tree" importer: turns a tree / rock .glb straight into a FoliageType.
//
// Select the .glb, pick "Tuna Tree" in the Import dock, Reimport. From then on
// the .glb IS a FoliageType: drag it into a slot of the Foliage node's `types`.
// (It is not a scene any more, so it does not go into a "Tree Scene" slot.)
//
// Naming convention expected in the source file:
//   *-lod0 .. *-lod4              one or more meshes per LOD level
//   *-imposter / *_imposter       optional far billboard
//   *-colonly / *-convcolonly     collision mesh (triangle / convex), not drawn
//
// The glTF is read here, directly. The model is packed into a scene that is
// stored INSIDE the FoliageType, so the result is one self-contained file.
// (The first version tried to point tree_scene back at the .glb, which cannot
// work: a file has one importer, and once that is this one the .glb no longer
// loads as a scene.)
//
// Materials: a surface whose material is called NAME uses "NAME.tres" (or
// "NAME.material") from the .glb's folder when that file exists; otherwise it
// keeps the material that came from Blender.
//
// Every FoliageType setting is an import option, because an imported resource
// cannot be edited in the Inspector - the Import dock is where it is tuned.
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
	// Must match ResourceImporter::import exactly, r_metadata included. Without
	// it this is a different function, the base class version runs instead, and
	// every import fails with "Error importing".
	virtual Error import(const String &p_source_file, const String &p_save_path,
			const Map<StringName, Variant> &p_options,
			List<String> *r_platform_variants,
			List<String> *r_gen_files = nullptr,
			Variant *r_metadata = nullptr);
};

#endif // TOOLS_ENABLED
#endif // TREE_IMPORTER_H
