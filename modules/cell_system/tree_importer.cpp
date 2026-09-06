#include "tree_importer.h"
#ifdef TOOLS_ENABLED

#include "foliage_type.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "scene/3d/mesh_instance.h"
#include "scene/resources/packed_scene.h"

void TreeImporter::_bind_methods() {}

String TreeImporter::get_importer_name()  const { return "lowdot.tree"; }
String TreeImporter::get_visible_name()   const { return "LowDot Tree"; }

void TreeImporter::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("glb");
	p_extensions->push_back("gltf");
}

int    TreeImporter::get_preset_count()   const { return 1; }
String TreeImporter::get_preset_name(int) const { return "Default"; }
String TreeImporter::get_save_extension() const { return "tres"; }
String TreeImporter::get_resource_type()  const { return "FoliageType"; }
float  TreeImporter::get_priority()       const { return 0.1f; }
int    TreeImporter::get_import_order()   const { return 0; }

void TreeImporter::get_import_options(List<ImportOption> *r_options, int) const {
	r_options->push_back(ImportOption(
			PropertyInfo(Variant::INT, "lod_count", PROPERTY_HINT_RANGE, "1,5,1"), 1));
	r_options->push_back(ImportOption(
			PropertyInfo(Variant::REAL, "lod_distance_0", PROPERTY_HINT_RANGE, "1,2000,0.5"), 20.0));
	r_options->push_back(ImportOption(
			PropertyInfo(Variant::REAL, "lod_distance_1", PROPERTY_HINT_RANGE, "1,2000,0.5"), 50.0));
	r_options->push_back(ImportOption(
			PropertyInfo(Variant::REAL, "lod_distance_2", PROPERTY_HINT_RANGE, "1,2000,0.5"), 100.0));
	r_options->push_back(ImportOption(
			PropertyInfo(Variant::REAL, "lod_distance_3", PROPERTY_HINT_RANGE, "1,2000,0.5"), 200.0));
	r_options->push_back(ImportOption(
			PropertyInfo(Variant::REAL, "max_render_distance", PROPERTY_HINT_RANGE, "10,5000,1"), 100.0));
	r_options->push_back(ImportOption(
			PropertyInfo(Variant::REAL, "fade_band", PROPERTY_HINT_RANGE, "0,64,0.5"), 8.0));
	r_options->push_back(ImportOption(
			PropertyInfo(Variant::BOOL, "cast_shadow"), true));
}

bool TreeImporter::get_option_visibility(const String &, const Map<StringName, Variant> &) const {
	return true;
}

// Counts -lod{n} children (any depth) to detect the actual LOD count in the scene.
static int _count_tree_lods(Node *p_node, int p_max) {
	int found = 0;
	MeshInstance *mi = Object::cast_to<MeshInstance>(p_node);
	if (mi) {
		String name = mi->get_name();
		int pos = name.rfind("-lod");
		if (pos >= 0) {
			String tail = name.substr(pos + 4);
			if (tail.is_valid_integer()) {
				int idx = tail.to_int();
				if (idx >= 0 && idx < p_max) found = MAX(found, idx + 1);
			}
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++)
		found = MAX(found, _count_tree_lods(p_node->get_child(i), p_max));
	return found;
}

Error TreeImporter::import(const String &p_source_file, const String &p_save_path,
		const Map<StringName, Variant> &p_options,
		List<String> *r_platform_variants, List<String> *r_gen_files) {

	Ref<PackedScene> glb = ResourceLoader::load(p_source_file, "PackedScene");
	ERR_FAIL_COND_V_MSG(!glb.is_valid(), ERR_CANT_OPEN,
			"TreeImporter: failed to load " + p_source_file);

	// Auto-detect LOD count from scene children so users get a sensible default.
	int want_lods = (int)p_options["lod_count"];
	Node *tmp = glb->instance();
	if (tmp) {
		int detected = _count_tree_lods(tmp, FoliageType::MAX_LODS);
		if (detected > 0) want_lods = MIN(detected, want_lods > 1 ? want_lods : detected);
		memdelete(tmp);
	}

	Ref<FoliageType> ft;
	ft.instance();
	ft->set_type_name(p_source_file.get_file().get_basename());
	ft->set_type(FoliageType::TYPE_TREE);
	ft->set_tree_scene(glb);
	ft->set_lod_count(CLAMP(want_lods, 1, FoliageType::MAX_LODS));
	ft->set_max_render_distance((float)p_options["max_render_distance"]);
	ft->set_fade_band((float)p_options["fade_band"]);
	ft->set_cast_shadow((bool)p_options["cast_shadow"]);
	ft->set_collision_enabled(true);

	const char *dist_keys[4] = { "lod_distance_0", "lod_distance_1", "lod_distance_2", "lod_distance_3" };
	for (int i = 0; i < 4; i++)
		ft->set_lod_distance(i, (float)p_options[StringName(dist_keys[i])]);

	return ResourceSaver::save(p_save_path + ".tres", ft);
}

#endif // TOOLS_ENABLED
