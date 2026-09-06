#ifndef TEXTURE_PACKER_DIALOG_H
#define TEXTURE_PACKER_DIALOG_H
#ifdef TOOLS_ENABLED

#include "scene/gui/dialogs.h"

class LineEdit;
class EditorFileDialog;

// Packs separate Albedo / Height / Normal / Roughness source images into the
// two channel-packed textures the terrain shader actually consumes (see
// terrain_shader.h): "<name>_albedo_height.png" (RGB=albedo, A=height) and
// "<name>_normal_roughness.png" (RGB=normal, A=roughness) — assign the two
// outputs directly to a CellTerrainManager's Albedo/Normal Layer N slots.
// Same channel layout Terrain3D and most other packed-terrain-texture
// systems use, so existing asset sets are usually already split this way.
class TexturePackerDialog : public AcceptDialog {
	GDCLASS(TexturePackerDialog, AcceptDialog);

	LineEdit *albedo_edit = nullptr;
	LineEdit *height_edit = nullptr;
	LineEdit *normal_edit = nullptr;
	LineEdit *roughness_edit = nullptr;
	LineEdit *output_dir_edit = nullptr;
	LineEdit *name_edit = nullptr;

	EditorFileDialog *file_dialog = nullptr;
	LineEdit *_file_dialog_target = nullptr;

	static Ref<Image> _load_image(const String &p_path);

	void _build();
	void _on_browse_pressed(int p_field);
	void _on_file_selected(String p_path);
	void _on_dir_selected(String p_dir);
	void _on_confirmed();

protected:
	static void _bind_methods();

public:
	TexturePackerDialog();
};

#endif
#endif
