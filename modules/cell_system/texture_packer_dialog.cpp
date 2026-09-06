#include "texture_packer_dialog.h"
#ifdef TOOLS_ENABLED

#include "core/image.h"
#include "core/os/dir_access.h"
#include "editor/editor_file_dialog.h"
#include "editor/editor_file_system.h"
#include "editor/editor_node.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"

TexturePackerDialog::TexturePackerDialog() {
	_build();
}

void TexturePackerDialog::_build() {
	set_title("Pack Terrain Textures");
	set_resizable(true);

	VBoxContainer *vb = memnew(VBoxContainer);
	vb->set_custom_minimum_size(Size2(460, 0));
	vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	vb->set_v_size_flags(Control::SIZE_EXPAND_FILL);

	struct FieldDef { const char *label; LineEdit **edit; int field_id; };
	// Filled in below since LineEdit* targets need to exist first.
	albedo_edit = memnew(LineEdit);
	height_edit = memnew(LineEdit);
	normal_edit = memnew(LineEdit);
	roughness_edit = memnew(LineEdit);
	output_dir_edit = memnew(LineEdit);

	FieldDef fields[5] = {
		{ "Albedo (required)", &albedo_edit, 0 },
		{ "Height (grayscale)", &height_edit, 1 },
		{ "Normal", &normal_edit, 2 },
		{ "Roughness (grayscale)", &roughness_edit, 3 },
		{ "Output Directory", &output_dir_edit, 4 },
	};

	for (int i = 0; i < 5; i++) {
		Label *l = memnew(Label);
		l->set_text(fields[i].label);
		vb->add_child(l);

		HBoxContainer *row = memnew(HBoxContainer);
		LineEdit *edit = *fields[i].edit;
		edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		edit->set_editable(false); // set via the file dialog only, avoids typos in paths
		row->add_child(edit);

		Button *browse = memnew(Button);
		browse->set_text("Browse...");
		browse->connect("pressed", this, "_on_browse_pressed", varray(fields[i].field_id));
		row->add_child(browse);

		vb->add_child(row);
	}

	Label *name_label = memnew(Label);
	name_label->set_text("Layer Name (output filename prefix)");
	vb->add_child(name_label);
	name_edit = memnew(LineEdit);
	name_edit->set_text("layer");
	vb->add_child(name_edit);

	Label *hint = memnew(Label);
	hint->set_text("Writes <name>_albedo_height.png (RGB=albedo, A=height) and\n<name>_normal_roughness.png (RGB=normal, A=roughness) —\nassign them to a CellTerrainLayer resource's Albedo/Normal Texture slots.\nMissing Height/Roughness default to a neutral mid value; missing Normal defaults to flat (0.5,0.5,1.0).");
	hint->set_autowrap(true);
	vb->add_child(hint);

	add_child(vb);

	get_ok()->set_text("Pack");
	connect("confirmed", this, "_on_confirmed");

	file_dialog = memnew(EditorFileDialog);
	file_dialog->add_filter("*.png,*.jpg,*.jpeg,*.tga,*.bmp,*.exr,*.hdr;Images");
	file_dialog->connect("file_selected", this, "_on_file_selected");
	file_dialog->connect("dir_selected", this, "_on_dir_selected");
	add_child(file_dialog);
}

void TexturePackerDialog::_on_browse_pressed(int p_field) {
	switch (p_field) {
		case 0: _file_dialog_target = albedo_edit; break;
		case 1: _file_dialog_target = height_edit; break;
		case 2: _file_dialog_target = normal_edit; break;
		case 3: _file_dialog_target = roughness_edit; break;
		case 4: _file_dialog_target = output_dir_edit; break;
		default: return;
	}
	if (p_field == 4) {
		file_dialog->set_mode(EditorFileDialog::MODE_OPEN_DIR);
	} else {
		file_dialog->set_mode(EditorFileDialog::MODE_OPEN_FILE);
	}
	file_dialog->popup_centered_ratio(0.6);
}

void TexturePackerDialog::_on_file_selected(String p_path) {
	if (_file_dialog_target) _file_dialog_target->set_text(p_path);
}

void TexturePackerDialog::_on_dir_selected(String p_dir) {
	if (_file_dialog_target) _file_dialog_target->set_text(p_dir);
}

Ref<Image> TexturePackerDialog::_load_image(const String &p_path) {
	if (p_path.empty()) return Ref<Image>();
	Ref<Image> img;
	img.instance();
	if (img->load(p_path) != OK) return Ref<Image>();
	if (img->is_compressed()) img->decompress();
	if (img->get_format() != Image::FORMAT_RGBA8) img->convert(Image::FORMAT_RGBA8);
	return img;
}

void TexturePackerDialog::_on_confirmed() {
	String albedo_path = albedo_edit->get_text();
	String out_dir = output_dir_edit->get_text();
	if (albedo_path.empty() || out_dir.empty()) {
		EditorNode::get_singleton()->show_warning("Albedo and Output Directory are required.");
		return;
	}

	Ref<Image> albedo = _load_image(albedo_path);
	if (albedo.is_null()) {
		EditorNode::get_singleton()->show_warning("Couldn't load the Albedo image:\n" + albedo_path);
		return;
	}

	const int w = albedo->get_width();
	const int h = albedo->get_height();

	Ref<Image> height = _load_image(height_edit->get_text());
	if (height.is_valid() && (height->get_width() != w || height->get_height() != h)) height->resize(w, h);
	Ref<Image> normal = _load_image(normal_edit->get_text());
	if (normal.is_valid() && (normal->get_width() != w || normal->get_height() != h)) normal->resize(w, h);
	Ref<Image> roughness = _load_image(roughness_edit->get_text());
	if (roughness.is_valid() && (roughness->get_width() != w || roughness->get_height() != h)) roughness->resize(w, h);

	Ref<Image> albedo_height;
	albedo_height.instance();
	albedo_height->create(w, h, false, Image::FORMAT_RGBA8);
	Ref<Image> normal_roughness;
	normal_roughness.instance();
	normal_roughness->create(w, h, false, Image::FORMAT_RGBA8);

	albedo->lock();
	if (height.is_valid()) height->lock();
	if (normal.is_valid()) normal->lock();
	if (roughness.is_valid()) roughness->lock();
	albedo_height->lock();
	normal_roughness->lock();

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			Color a = albedo->get_pixel(x, y);
			float hgt = height.is_valid() ? height->get_pixel(x, y).r : 0.5f;
			albedo_height->set_pixel(x, y, Color(a.r, a.g, a.b, hgt));

			Color n = normal.is_valid() ? normal->get_pixel(x, y) : Color(0.5f, 0.5f, 1.0f);
			float rgh = roughness.is_valid() ? roughness->get_pixel(x, y).r : 0.5f;
			normal_roughness->set_pixel(x, y, Color(n.r, n.g, n.b, rgh));
		}
	}

	albedo->unlock();
	if (height.is_valid()) height->unlock();
	if (normal.is_valid()) normal->unlock();
	if (roughness.is_valid()) roughness->unlock();
	albedo_height->unlock();
	normal_roughness->unlock();

	String name = name_edit->get_text();
	if (name.empty()) name = "layer";

	DirAccess *da = DirAccess::create_for_path(out_dir);
	if (da) { da->make_dir_recursive(out_dir); memdelete(da); }

	String albedo_out = out_dir.plus_file(name + "_albedo_height.png");
	String normal_out = out_dir.plus_file(name + "_normal_roughness.png");
	Error e1 = albedo_height->save_png(albedo_out);
	Error e2 = normal_roughness->save_png(normal_out);

	if (e1 != OK || e2 != OK) {
		EditorNode::get_singleton()->show_warning("Failed to save one or both packed textures to:\n" + out_dir);
		return;
	}

	if (EditorFileSystem::get_singleton()) EditorFileSystem::get_singleton()->scan();

	EditorNode::get_singleton()->show_warning(
			"Packed:\n" + albedo_out + "\n" + normal_out +
			"\n\nAssign these to a CellTerrainLayer resource's Albedo/Normal Texture slots.",
			"Texture Packer");
}

void TexturePackerDialog::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_browse_pressed", "field"), &TexturePackerDialog::_on_browse_pressed);
	ClassDB::bind_method(D_METHOD("_on_file_selected", "path"), &TexturePackerDialog::_on_file_selected);
	ClassDB::bind_method(D_METHOD("_on_dir_selected", "dir"), &TexturePackerDialog::_on_dir_selected);
	ClassDB::bind_method(D_METHOD("_on_confirmed"), &TexturePackerDialog::_on_confirmed);
}

#endif
