#include "tree_importer.h"
#ifdef TOOLS_ENABLED

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/os/file_access.h"
#include "editor/import/resource_importer_scene.h"
#include "foliage_type.h"
#include "modules/gltf/editor_scene_importer_gltf.h"
#include "scene/3d/collision_shape.h"
#include "scene/3d/mesh_instance.h"
#include "scene/resources/packed_scene.h"

void TreeImporter::_bind_methods() {}

String TreeImporter::get_importer_name() const { return "tuna.tree"; }
String TreeImporter::get_visible_name() const { return "Tuna Tree"; }

void TreeImporter::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("glb");
	p_extensions->push_back("gltf");
}

int TreeImporter::get_preset_count() const { return 1; }
String TreeImporter::get_preset_name(int) const { return "Default"; }
// Binary: the model's meshes are stored inside the FoliageType.
String TreeImporter::get_save_extension() const { return "res"; }
String TreeImporter::get_resource_type() const { return "FoliageType"; }
float TreeImporter::get_priority() const { return 0.1f; }
int TreeImporter::get_import_order() const { return 0; }

void TreeImporter::get_import_options(List<ImportOption> *r_options, int) const {
	r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "kind", PROPERTY_HINT_ENUM, "Tree,Rock"), 0));
	r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "keep_loaded"), false));

	r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "lod_count", PROPERTY_HINT_RANGE, "1,5,1"), 1));
	r_options->push_back(ImportOption(PropertyInfo(Variant::REAL, "lod_distance_0", PROPERTY_HINT_RANGE, "1,2000,0.5"), 20.0));
	r_options->push_back(ImportOption(PropertyInfo(Variant::REAL, "lod_distance_1", PROPERTY_HINT_RANGE, "1,2000,0.5"), 50.0));
	r_options->push_back(ImportOption(PropertyInfo(Variant::REAL, "lod_distance_2", PROPERTY_HINT_RANGE, "1,2000,0.5"), 100.0));
	r_options->push_back(ImportOption(PropertyInfo(Variant::REAL, "lod_distance_3", PROPERTY_HINT_RANGE, "1,2000,0.5"), 200.0));
	r_options->push_back(ImportOption(PropertyInfo(Variant::REAL, "max_render_distance", PROPERTY_HINT_RANGE, "10,5000,1"), 100.0));
	r_options->push_back(ImportOption(PropertyInfo(Variant::REAL, "fade_band", PROPERTY_HINT_RANGE, "0,64,0.5"), 8.0));

	r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "cast_shadow"), true));
	r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "shadow_lod_count", PROPERTY_HINT_RANGE, "0,5,1"), 1));
	// One material for the whole type, replacing everything else. Leave empty
	// to keep per-part materials (see the header for how those are found).
	r_options->push_back(ImportOption(PropertyInfo(Variant::STRING, "material_override", PROPERTY_HINT_FILE, "*.tres,*.material,*.res"), ""));

	r_options->push_back(ImportOption(PropertyInfo(Variant::REAL, "scatter/density", PROPERTY_HINT_RANGE, "0,10,0.001"), 0.1));
	r_options->push_back(ImportOption(PropertyInfo(Variant::REAL, "scatter/min_scale", PROPERTY_HINT_RANGE, "0.01,10,0.01"), 0.9));
	r_options->push_back(ImportOption(PropertyInfo(Variant::REAL, "scatter/max_scale", PROPERTY_HINT_RANGE, "0.01,10,0.01"), 1.1));
	r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "scatter/random_yaw"), true));
	r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "scatter/align_to_normal"), false));
	r_options->push_back(ImportOption(PropertyInfo(Variant::REAL, "scatter/align_amount", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.0));
	r_options->push_back(ImportOption(PropertyInfo(Variant::REAL, "scatter/slope_limit_deg", PROPERTY_HINT_RANGE, "0,90,1"), 45.0));
	r_options->push_back(ImportOption(PropertyInfo(Variant::REAL, "scatter/min_spacing", PROPERTY_HINT_RANGE, "0,32,0.1"), 1.0));
	r_options->push_back(ImportOption(PropertyInfo(Variant::REAL, "scatter/y_offset_min", PROPERTY_HINT_RANGE, "-4,4,0.01"), 0.0));
	r_options->push_back(ImportOption(PropertyInfo(Variant::REAL, "scatter/y_offset_max", PROPERTY_HINT_RANGE, "-4,4,0.01"), 0.0));

	r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "collision/enabled"), true));
	r_options->push_back(ImportOption(PropertyInfo(Variant::REAL, "collision/nav_obstacle_radius", PROPERTY_HINT_RANGE, "0,32,0.1"), 0.0));
}

bool TreeImporter::get_option_visibility(const String &, const Map<StringName, Variant> &) const {
	return true;
}

// Option lookup that tolerates an older .import file missing newer keys.
static Variant _opt(const Map<StringName, Variant> &p_options, const char *p_name, const Variant &p_default) {
	const Map<StringName, Variant>::Element *E = p_options.find(StringName(p_name));
	return E ? E->get() : p_default;
}

// Counts -lod{n} meshes (any depth) to detect the LOD count actually authored.
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
				if (idx >= 0 && idx < p_max) {
					found = MAX(found, idx + 1);
				}
			}
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		found = MAX(found, _count_tree_lods(p_node->get_child(i), p_max));
	}
	return found;
}

// Same test the stock scene importer uses for its "-colonly" hints: trailing
// digits and dots are ignored (Blender's ".001"), and both "-" and "_" count.
static bool _has_suffix(const String &p_name, const String &p_suffix, String &r_base) {
	String what = p_name;
	while (what.length() && ((what[what.length() - 1] >= '0' && what[what.length() - 1] <= '9') || what[what.length() - 1] <= 32 || what[what.length() - 1] == '.')) {
		what = what.substr(0, what.length() - 1);
	}
	const String low = what.to_lower();
	if (low.ends_with("-" + p_suffix) || low.ends_with("_" + p_suffix)) {
		r_base = what.substr(0, what.length() - p_suffix.length() - 1);
		return true;
	}
	return false;
}

static void _find_collision_meshes(Node *p_node, Node *p_root, Vector<MeshInstance *> &r_out) {
	MeshInstance *mi = Object::cast_to<MeshInstance>(p_node);
	String base;
	if (mi && p_node != p_root && (_has_suffix(mi->get_name(), "convcolonly", base) || _has_suffix(mi->get_name(), "colonly", base))) {
		r_out.push_back(mi);
		return;
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_find_collision_meshes(p_node->get_child(i), p_root, r_out);
	}
}

// The stock importer turns "-colonly" meshes into collision when it imports a
// scene. This importer reads the glTF itself, so it has to do the same - what
// the foliage system collects is CollisionShape nodes.
static int _convert_collision_meshes(Node *p_root) {
	Vector<MeshInstance *> found;
	_find_collision_meshes(p_root, p_root, found);
	int made = 0;
	for (int i = 0; i < found.size(); i++) {
		MeshInstance *mi = found[i];
		Node *parent = mi->get_parent();
		Ref<Mesh> mesh = mi->get_mesh();
		String base;
		const bool convex = _has_suffix(mi->get_name(), "convcolonly", base);
		if (!convex) {
			_has_suffix(mi->get_name(), "colonly", base);
		}
		Ref<Shape> shape;
		if (mesh.is_valid()) {
			shape = convex ? mesh->create_convex_shape() : mesh->create_trimesh_shape();
		}
		if (shape.is_valid() && parent) {
			CollisionShape *cs = memnew(CollisionShape);
			cs->set_shape(shape);
			cs->set_name(base + "_collision");
			cs->set_transform(mi->get_transform());
			parent->add_child(cs);
			made++;
		}
		if (parent) {
			parent->remove_child(mi);
		}
		memdelete(mi);
	}
	return made;
}

// A surface whose material is called NAME uses NAME.tres / NAME.material from
// the model's folder if there is one. Referenced, not copied, so editing that
// material updates every painted instance.
static int _apply_external_materials(Node *p_node, const String &p_dir) {
	int replaced = 0;
	MeshInstance *mi = Object::cast_to<MeshInstance>(p_node);
	if (mi && mi->get_mesh().is_valid()) {
		Ref<Mesh> mesh = mi->get_mesh();
		for (int s = 0; s < mesh->get_surface_count(); s++) {
			Ref<Material> mat = mesh->surface_get_material(s);
			if (mat.is_null() || mat->get_name().empty()) {
				continue;
			}
			static const char *exts[] = { ".tres", ".material", ".res", nullptr };
			for (int e = 0; exts[e]; e++) {
				const String path = p_dir.plus_file(mat->get_name() + exts[e]);
				if (!FileAccess::exists(path)) {
					continue;
				}
				Ref<Material> ext = ResourceLoader::load(path, "Material");
				if (ext.is_valid()) {
					mesh->surface_set_material(s, ext);
					replaced++;
				}
				break;
			}
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		replaced += _apply_external_materials(p_node->get_child(i), p_dir);
	}
	return replaced;
}

static void _own_all(Node *p_node, Node *p_root) {
	if (p_node != p_root) {
		p_node->set_owner(p_root);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_own_all(p_node->get_child(i), p_root);
	}
}

Error TreeImporter::import(const String &p_source_file, const String &p_save_path,
		const Map<StringName, Variant> &p_options,
		List<String> *r_platform_variants, List<String> *r_gen_files, Variant *r_metadata) {
	// Read the glTF directly. Loading p_source_file through ResourceLoader
	// would come back here: this importer is what that path now resolves to.
	Ref<EditorSceneImporterGLTF> gltf;
	gltf.instance();
	Error err = OK;
	List<String> missing;
	Node *scene = gltf->import_scene(p_source_file,
			EditorSceneImporter::IMPORT_SCENE | EditorSceneImporter::IMPORT_GENERATE_TANGENT_ARRAYS,
			15, Mesh::ARRAY_COMPRESS_DEFAULT, &missing, &err);
	ERR_FAIL_COND_V_MSG(!scene, err != OK ? err : ERR_CANT_OPEN, "Tuna Tree: could not read " + p_source_file);

	const int detected = _count_tree_lods(scene, FoliageType::MAX_LODS);
	if (detected == 0) {
		WARN_PRINT("Tuna Tree: '" + p_source_file + "' has no mesh named *-lod0 .. *-lod4, so nothing will be drawn. Rename the mesh objects in Blender (lowercase, with the hyphen).");
	}
	const int colliders = _convert_collision_meshes(scene);
	const int ext_mats = _apply_external_materials(scene, p_source_file.get_base_dir());

	_own_all(scene, scene);
	Ref<PackedScene> packed;
	packed.instance();
	err = packed->pack(scene);
	memdelete(scene);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Tuna Tree: could not pack " + p_source_file);

	// lod_count left at 1 means "whatever the file has"; a higher value is a
	// cap, so a model with 4 LODs can be imported using only the first 2.
	int want_lods = _opt(p_options, "lod_count", 1);
	if (detected > 0) {
		want_lods = want_lods > 1 ? MIN(detected, want_lods) : detected;
	}

	Ref<FoliageType> ft;
	ft.instance();
	ft->set_type_name(p_source_file.get_file().get_basename());
	ft->set_type((int)_opt(p_options, "kind", 0) == 1 ? FoliageType::TYPE_ROCK : FoliageType::TYPE_TREE);
	ft->set_tree_scene(packed);
	ft->set_keep_loaded(_opt(p_options, "keep_loaded", false));

	ft->set_lod_count(CLAMP(want_lods, 1, FoliageType::MAX_LODS));
	const char *dist_keys[4] = { "lod_distance_0", "lod_distance_1", "lod_distance_2", "lod_distance_3" };
	const float dist_defaults[4] = { 20.0f, 50.0f, 100.0f, 200.0f };
	for (int i = 0; i < 4; i++) {
		ft->set_lod_distance(i, _opt(p_options, dist_keys[i], dist_defaults[i]));
	}
	ft->set_max_render_distance(_opt(p_options, "max_render_distance", 100.0));
	ft->set_fade_band(_opt(p_options, "fade_band", 8.0));

	ft->set_cast_shadow(_opt(p_options, "cast_shadow", true));
	ft->set_shadow_lod_count(_opt(p_options, "shadow_lod_count", 1));
	const String override_path = _opt(p_options, "material_override", "");
	if (!override_path.empty()) {
		Ref<Material> m = ResourceLoader::load(override_path, "Material");
		if (m.is_valid()) {
			ft->set_material_override(m);
		} else {
			WARN_PRINT("Tuna Tree: material_override '" + override_path + "' is not a material.");
		}
	}

	ft->set_density(_opt(p_options, "scatter/density", 0.1));
	ft->set_min_scale(_opt(p_options, "scatter/min_scale", 0.9));
	ft->set_max_scale(_opt(p_options, "scatter/max_scale", 1.1));
	ft->set_random_yaw(_opt(p_options, "scatter/random_yaw", true));
	ft->set_align_to_normal(_opt(p_options, "scatter/align_to_normal", false));
	ft->set_align_amount(_opt(p_options, "scatter/align_amount", 0.0));
	ft->set_slope_limit_deg(_opt(p_options, "scatter/slope_limit_deg", 45.0));
	ft->set_min_spacing(_opt(p_options, "scatter/min_spacing", 1.0));
	ft->set_y_offset_min(_opt(p_options, "scatter/y_offset_min", 0.0));
	ft->set_y_offset_max(_opt(p_options, "scatter/y_offset_max", 0.0));

	ft->set_collision_enabled(_opt(p_options, "collision/enabled", true));
	ft->set_nav_obstacle_radius(_opt(p_options, "collision/nav_obstacle_radius", 0.0));

	print_line("Tuna Tree: " + p_source_file.get_file() + " -> " + itos(ft->get_lod_count()) + " LOD(s), " +
			itos(colliders) + " collision shape(s), " + itos(ext_mats) + " external material(s).");

	return ResourceSaver::save(p_save_path + ".res", ft, ResourceSaver::FLAG_COMPRESS);
}

#endif // TOOLS_ENABLED
