/**************************************************************************/
/*  editor_themes.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "editor_themes.h"

#include "core/io/resource_loader.h"
#include "editor_fonts.h"
#include "editor_icons.gen.h"
#include "editor_scale.h"
#include "editor_settings.h"

#include "modules/modules_enabled.gen.h" // For svg.
#ifdef MODULE_SVG_ENABLED
#include "modules/svg/image_loader_svg.h"
#endif

static Ref<StyleBoxTexture> make_stylebox(Ref<Texture> p_texture, float p_left, float p_top, float p_right, float p_bottom, float p_margin_left = -1, float p_margin_top = -1, float p_margin_right = -1, float p_margin_bottom = -1, bool p_draw_center = true) {
	Ref<StyleBoxTexture> style(memnew(StyleBoxTexture));
	style->set_texture(p_texture);
	style->set_margin_size(MARGIN_LEFT, p_left * EDSCALE);
	style->set_margin_size(MARGIN_RIGHT, p_right * EDSCALE);
	style->set_margin_size(MARGIN_BOTTOM, p_bottom * EDSCALE);
	style->set_margin_size(MARGIN_TOP, p_top * EDSCALE);
	style->set_default_margin(MARGIN_LEFT, p_margin_left * EDSCALE);
	style->set_default_margin(MARGIN_RIGHT, p_margin_right * EDSCALE);
	style->set_default_margin(MARGIN_BOTTOM, p_margin_bottom * EDSCALE);
	style->set_default_margin(MARGIN_TOP, p_margin_top * EDSCALE);
	style->set_draw_center(p_draw_center);
	return style;
}

static Ref<StyleBoxEmpty> make_empty_stylebox(float p_margin_left = -1, float p_margin_top = -1, float p_margin_right = -1, float p_margin_bottom = -1) {
	Ref<StyleBoxEmpty> style(memnew(StyleBoxEmpty));
	style->set_default_margin(MARGIN_LEFT, p_margin_left * EDSCALE);
	style->set_default_margin(MARGIN_RIGHT, p_margin_right * EDSCALE);
	style->set_default_margin(MARGIN_BOTTOM, p_margin_bottom * EDSCALE);
	style->set_default_margin(MARGIN_TOP, p_margin_top * EDSCALE);
	return style;
}

static Ref<StyleBoxFlat> make_flat_stylebox(Color p_color, float p_margin_left = -1, float p_margin_top = -1, float p_margin_right = -1, float p_margin_bottom = -1) {
	Ref<StyleBoxFlat> style(memnew(StyleBoxFlat));
	style->set_bg_color(p_color);
	style->set_default_margin(MARGIN_LEFT, p_margin_left * EDSCALE);
	style->set_default_margin(MARGIN_RIGHT, p_margin_right * EDSCALE);
	style->set_default_margin(MARGIN_BOTTOM, p_margin_bottom * EDSCALE);
	style->set_default_margin(MARGIN_TOP, p_margin_top * EDSCALE);
	return style;
}

static Ref<StyleBoxLine> make_line_stylebox(Color p_color, int p_thickness = 1, float p_grow_begin = 1, float p_grow_end = 1, bool p_vertical = false) {
	Ref<StyleBoxLine> style(memnew(StyleBoxLine));
	style->set_color(p_color);
	style->set_grow_begin(p_grow_begin);
	style->set_grow_end(p_grow_end);
	style->set_thickness(p_thickness);
	style->set_vertical(p_vertical);
	return style;
}

// Solid-color stylebox with a 1px-style border and square, non-antialiased corners.
// Used by the "Windows 7" preset to mimic classic desktop-app widgets.
static Ref<StyleBoxFlat> make_win_stylebox(Color p_bg, Color p_border, int p_border_width, float p_margin_left, float p_margin_top, float p_margin_right, float p_margin_bottom) {
	Ref<StyleBoxFlat> style = make_flat_stylebox(p_bg, p_margin_left, p_margin_top, p_margin_right, p_margin_bottom);
	style->set_border_width_all(p_border_width);
	style->set_border_color(p_border);
	style->set_corner_radius_all(0);
	style->set_anti_aliased(false);
	return style;
}

// Vertical-gradient 9-patch stylebox with a baked 1px border and a top gloss line.
// Used by the "Windows 7" preset for glossy Aero-style buttons and dropdowns.
static Ref<StyleBoxTexture> make_aero_stylebox(Color p_top, Color p_mid_top, Color p_mid_bot, Color p_bot, Color p_border, float p_gloss, float p_margin_h, float p_margin_v) {
	const int w = 8;
	const int h = 28;

	Ref<Image> img;
	img.instance();
	img->create(w, h, false, Image::FORMAT_RGBA8);
	img->lock();
	for (int y = 0; y < h; y++) {
		const float t = float(y) / float(h - 1);
		const Color c = (t < 0.5) ? p_top.linear_interpolate(p_mid_top, t * 2.0) : p_mid_bot.linear_interpolate(p_bot, (t - 0.5) * 2.0);
		for (int x = 0; x < w; x++) {
			img->set_pixel(x, y, c);
		}
	}
	// Inner gloss highlight just below the top border.
	for (int x = 1; x < w - 1; x++) {
		img->set_pixel(x, 1, img->get_pixel(x, 1).linear_interpolate(Color(1, 1, 1), p_gloss));
	}
	// 1px outer border.
	for (int x = 0; x < w; x++) {
		img->set_pixel(x, 0, p_border);
		img->set_pixel(x, h - 1, p_border);
	}
	for (int y = 0; y < h; y++) {
		img->set_pixel(0, y, p_border);
		img->set_pixel(w - 1, y, p_border);
	}
	img->unlock();

	Ref<ImageTexture> tex;
	tex.instance();
	tex->create_from_image(img, Texture::FLAG_FILTER);

	Ref<StyleBoxTexture> style;
	style.instance();
	style->set_texture(tex);
	style->set_margin_size(MARGIN_LEFT, 3);
	style->set_margin_size(MARGIN_RIGHT, 3);
	style->set_margin_size(MARGIN_TOP, 3);
	style->set_margin_size(MARGIN_BOTTOM, 3);
	style->set_default_margin(MARGIN_LEFT, p_margin_h * EDSCALE);
	style->set_default_margin(MARGIN_RIGHT, p_margin_h * EDSCALE);
	style->set_default_margin(MARGIN_TOP, p_margin_v * EDSCALE);
	style->set_default_margin(MARGIN_BOTTOM, p_margin_v * EDSCALE);
	return style;
}

// Transparent-centre 9-patch with just a 1px coloured border, for a focus ring
// that overlays the state stylebox without hiding it.
static Ref<StyleBoxTexture> make_aero_focus_stylebox(Color p_border, float p_margin_h, float p_margin_v) {
	const int w = 8;
	const int h = 28;

	Ref<Image> img;
	img.instance();
	img->create(w, h, false, Image::FORMAT_RGBA8);
	img->fill(Color(0, 0, 0, 0));
	img->lock();
	for (int x = 0; x < w; x++) {
		img->set_pixel(x, 0, p_border);
		img->set_pixel(x, h - 1, p_border);
	}
	for (int y = 0; y < h; y++) {
		img->set_pixel(0, y, p_border);
		img->set_pixel(w - 1, y, p_border);
	}
	img->unlock();

	Ref<ImageTexture> tex;
	tex.instance();
	tex->create_from_image(img, Texture::FLAG_FILTER);

	Ref<StyleBoxTexture> style;
	style.instance();
	style->set_texture(tex);
	style->set_margin_size(MARGIN_LEFT, 2);
	style->set_margin_size(MARGIN_RIGHT, 2);
	style->set_margin_size(MARGIN_TOP, 2);
	style->set_margin_size(MARGIN_BOTTOM, 2);
	style->set_default_margin(MARGIN_LEFT, p_margin_h * EDSCALE);
	style->set_default_margin(MARGIN_RIGHT, p_margin_h * EDSCALE);
	style->set_default_margin(MARGIN_TOP, p_margin_v * EDSCALE);
	style->set_default_margin(MARGIN_BOTTOM, p_margin_v * EDSCALE);
	style->set_draw_center(false);
	return style;
}

static Ref<Texture> flip_icon(Ref<Texture> p_texture, bool p_flip_y = false, bool p_flip_x = false) {
	if (!p_flip_y && !p_flip_x) {
		return p_texture;
	}

	Ref<ImageTexture> texture(memnew(ImageTexture));
	Ref<Image> img = p_texture->get_data();
	img = img->duplicate();

	if (p_flip_y) {
		img->flip_y();
	}
	if (p_flip_x) {
		img->flip_x();
	}

	texture->create_from_image(img);
	return texture;
}

#ifdef MODULE_SVG_ENABLED
static Ref<ImageTexture> editor_generate_icon(int p_index, bool p_convert_color, float p_scale = EDSCALE, bool p_force_filter = false) {
	Ref<ImageTexture> icon = memnew(ImageTexture);
	Ref<Image> img = memnew(Image);

	// dumb gizmo check
	bool is_gizmo = String(editor_icons_names[p_index]).begins_with("Gizmo");

	// Upsample icon generation only if the editor scale isn't an integer multiplier.
	// Generating upsampled icons is slower, and the benefit is hardly visible
	// with integer editor scales.
	const bool upsample = !Math::is_equal_approx(Math::round(p_scale), p_scale);
	ImageLoaderSVG::create_image_from_string(img, editor_icons_sources[p_index], p_scale, upsample, p_convert_color);

	if ((p_scale - (float)((int)p_scale)) > 0.0 || is_gizmo || p_force_filter) {
		icon->create_from_image(img); // in this case filter really helps
	} else {
		icon->create_from_image(img, 0);
	}

	return icon;
}
#endif

#ifndef ADD_CONVERT_COLOR
#define ADD_CONVERT_COLOR(dictionary, old_color, new_color) dictionary[Color::html(old_color)] = Color::html(new_color)
#endif

void editor_register_and_generate_icons(Ref<Theme> p_theme, bool p_dark_theme = true, int p_thumb_size = 32, bool p_only_thumbs = false) {
#ifdef MODULE_SVG_ENABLED
	// The default icon theme is designed to be used for a dark theme.
	// This dictionary stores color codes to convert to other colors
	// for better readability on a light theme.
	Dictionary dark_icon_color_dictionary;

	// The names of the icons to never convert, even if one of their colors
	// are contained in the dictionary above.
	Set<StringName> exceptions;

	if (!p_dark_theme) {
		// convert color:                             FROM       TO
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#e0e0e0", "#5a5a5a"); // common icon color
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#ffffff", "#414141"); // white
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#b4b4b4", "#363636"); // script darker color
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#f9f9f9", "#606060"); // scrollbar grabber highlight color

		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#cea4f1", "#a85de9"); // animation
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#fc9c9c", "#cd3838"); // spatial
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#a5b7f3", "#3d64dd"); // 2d
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#708cea", "#1a3eac"); // 2d dark
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#a5efac", "#2fa139"); // control
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#ffdd65", "#ca8a04"); // node warning

		// rainbow
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#ff7070", "#ff2929"); // red
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#ffeb70", "#ffe337"); // yellow
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#9dff70", "#74ff34"); // green
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#70ffb9", "#2cff98"); // aqua
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#70deff", "#22ccff"); // blue
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#9f70ff", "#702aff"); // purple
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#ff70ac", "#ff2781"); // pink

		// audio gradient
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#ff8484", "#ff4040"); // red
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#e1dc7a", "#d6cf4b"); // yellow
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#84ffb1", "#00f010"); // green

		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#ffd684", "#fea900"); // mesh (orange)
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#40a2ff", "#68b6ff"); // shape (blue)

		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#ff8484", "#ff3333"); // remove (red)
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#84ffb1", "#00db50"); // add (green)
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#84c2ff", "#5caeff"); // selection (blue)

		// Animation editor tracks
		// The property track icon color is set by the common icon color
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#ea9568", "#bd5e2c"); // 3D Transform track
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#66f376", "#16a827"); // Call Method track
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#5792f6", "#236be6"); // Bezier Curve track
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#eae668", "#9f9722"); // Audio Playback track
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#b76ef0", "#9853ce"); // Animation Playback track

		// TileSet editor icons
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#fce844", "#aa8d24"); // New Single Tile
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#4490fc", "#0350bd"); // New Autotile
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#c9cfd4", "#828f9b"); // New Atlas

		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#69ecbd", "#25e3a0"); // VS variant
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#8da6f0", "#6d8eeb"); // VS bool
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#7dc6ef", "#4fb2e9"); // VS int
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#61daf4", "#27ccf0"); // VS float
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#6ba7ec", "#4690e7"); // VS string
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#bd91f1", "#ad76ee"); // VS vector2
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#f191a5", "#ee758e"); // VS rect
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#e286f0", "#dc6aed"); // VS vector3
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#c4ec69", "#96ce1a"); // VS transform2D
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#f77070", "#f77070"); // VS plane
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#ec69a3", "#ec69a3"); // VS quat
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#ee7991", "#ee7991"); // VS aabb
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#e3ec69", "#b2bb19"); // VS basis
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#f6a86e", "#f49047"); // VS transform
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#6993ec", "#6993ec"); // VS path
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#69ec9a", "#2ce573"); // VS rid
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#79f3e8", "#12d5c3"); // VS object
		ADD_CONVERT_COLOR(dark_icon_color_dictionary, "#77edb1", "#57e99f"); // VS dict

		exceptions.insert("EditorPivot");
		exceptions.insert("EditorHandle");
		exceptions.insert("Editor3DHandle");
		exceptions.insert("Godot");
		exceptions.insert("PanoramaSky");
		exceptions.insert("ProceduralSky");
		exceptions.insert("EditorControlAnchor");
		exceptions.insert("DefaultProjectIcon");
		exceptions.insert("GuiChecked");
		exceptions.insert("GuiRadioChecked");
		exceptions.insert("GuiCloseCustomizable");
		exceptions.insert("GuiGraphNodePort");
		exceptions.insert("GuiResizer");
		exceptions.insert("ZoomMore");
		exceptions.insert("ZoomLess");
		exceptions.insert("ZoomReset");
		exceptions.insert("LockViewport");
		exceptions.insert("GroupViewport");
		exceptions.insert("StatusError");
		exceptions.insert("StatusSuccess");
		exceptions.insert("StatusWarning");
		exceptions.insert("OverbrightIndicator");
	}

	// These ones should be converted even if we are using a dark theme.
	const Color error_color = p_theme->get_color("error_color", "Editor");
	const Color success_color = p_theme->get_color("success_color", "Editor");
	const Color warning_color = p_theme->get_color("warning_color", "Editor");
	dark_icon_color_dictionary[Color::html("#ff0000")] = error_color;
	dark_icon_color_dictionary[Color::html("#45ff8b")] = success_color;
	dark_icon_color_dictionary[Color::html("#dbab09")] = warning_color;

	ImageLoaderSVG::set_convert_colors(&dark_icon_color_dictionary);

	// Generate icons.
	if (!p_only_thumbs) {
		for (int i = 0; i < editor_icons_count; i++) {
			const int is_exception = exceptions.has(editor_icons_names[i]);
			const Ref<ImageTexture> icon = editor_generate_icon(i, !is_exception);

			p_theme->set_icon(editor_icons_names[i], "EditorIcons", icon);
		}
	}

	// Generate thumbnail icons with the given thumbnail size.
	// We don't need filtering when generating at one of the default resolutions.
	const bool force_filter = p_thumb_size != 64 && p_thumb_size != 32;
	if (p_thumb_size >= 64) {
		const float scale = (float)p_thumb_size / 64.0 * EDSCALE;
		for (int i = 0; i < editor_bg_thumbs_count; i++) {
			const int index = editor_bg_thumbs_indices[i];
			const int is_exception = exceptions.has(editor_icons_names[index]);
			const Ref<ImageTexture> icon = editor_generate_icon(index, !p_dark_theme && !is_exception, scale, force_filter);

			p_theme->set_icon(editor_icons_names[index], "EditorIcons", icon);
		}
	} else {
		const float scale = (float)p_thumb_size / 32.0 * EDSCALE;
		for (int i = 0; i < editor_md_thumbs_count; i++) {
			const int index = editor_md_thumbs_indices[i];
			const bool is_exception = exceptions.has(editor_icons_names[index]);
			const Ref<ImageTexture> icon = editor_generate_icon(index, !p_dark_theme && !is_exception, scale, force_filter);

			p_theme->set_icon(editor_icons_names[index], "EditorIcons", icon);
		}
	}

	ImageLoaderSVG::set_convert_colors(nullptr);
#else
	WARN_PRINT("SVG support disabled, editor icons won't be rendered.");
#endif
}

Ref<Theme> create_editor_theme(const Ref<Theme> p_theme) {
	Ref<Theme> theme = Ref<Theme>(memnew(Theme));

	const float default_contrast = 0.25;

	//Theme settings
	Color accent_color = EDITOR_GET("interface/theme/accent_color");
	Color base_color = EDITOR_GET("interface/theme/base_color");
	float contrast = EDITOR_GET("interface/theme/contrast");
	float relationship_line_opacity = EDITOR_GET("interface/theme/relationship_line_opacity");

	String preset = EDITOR_GET("interface/theme/preset");

	bool highlight_tabs = EDITOR_GET("interface/theme/highlight_tabs");
	int border_size = EDITOR_GET("interface/theme/border_size");

	bool use_gn_headers = EDITOR_GET("interface/theme/use_graph_node_headers");

	Color preset_accent_color;
	Color preset_base_color;
	float preset_contrast = 0;

	// Please, use alphabet order if you've added new theme here(After "Default" and "Custom")

	if (preset == "Default") {
		preset_accent_color = Color(0.41, 0.61, 0.91);
		preset_base_color = Color(0.2, 0.23, 0.31);
		preset_contrast = default_contrast;
	} else if (preset == "Custom") {
		accent_color = EDITOR_GET("interface/theme/accent_color");
		base_color = EDITOR_GET("interface/theme/base_color");
		contrast = EDITOR_GET("interface/theme/contrast");
	} else if (preset == "Alien") {
		preset_accent_color = Color(0.11, 1.0, 0.6);
		preset_base_color = Color(0.18, 0.22, 0.25);
		preset_contrast = 0.25;
	} else if (preset == "Arc") {
		preset_accent_color = Color(0.32, 0.58, 0.89);
		preset_base_color = Color(0.22, 0.24, 0.29);
		preset_contrast = 0.25;
	} else if (preset == "Godot 2") {
		preset_accent_color = Color(0.53, 0.67, 0.89);
		preset_base_color = Color(0.24, 0.23, 0.27);
		preset_contrast = 0.25;
	} else if (preset == "Grey") {
		preset_accent_color = Color(0.44, 0.73, 0.98);
		preset_base_color = Color(0.24, 0.24, 0.24);
		preset_contrast = 0.2;
	} else if (preset == "Light") {
		preset_accent_color = Color(0.13, 0.44, 1.0);
		preset_base_color = Color(1, 1, 1);
		preset_contrast = 0.08;
	} else if (preset == "Solarized (Dark)") {
		preset_accent_color = Color(0.15, 0.55, 0.82);
		preset_base_color = Color(0.03, 0.21, 0.26);
		preset_contrast = 0.23;
	} else if (preset == "Solarized (Light)") {
		preset_accent_color = Color(0.15, 0.55, 0.82);
		preset_base_color = Color(0.99, 0.96, 0.89);
		preset_contrast = 0.06;
	} else if (preset == "Windows 7") {
		// Light blue-grey window backdrop with a classic Windows accent blue.
		preset_accent_color = Color(0.18, 0.46, 0.74);
		preset_base_color = Color(0.90, 0.93, 0.97);
		preset_contrast = 0.08;
	} else if (preset == "Windows 7 Dark") {
		// Dark charcoal backdrop, same Aero widget feel, muted blue accent.
		preset_accent_color = Color(0.33, 0.56, 0.82);
		preset_base_color = Color(0.19, 0.20, 0.22);
		preset_contrast = 0.2;
	} else { // Default
		preset_accent_color = Color(0.41, 0.61, 0.91);
		preset_base_color = Color(0.2, 0.23, 0.31);
		preset_contrast = default_contrast;
	}

	if (preset != "Custom") {
		accent_color = preset_accent_color;
		base_color = preset_base_color;
		contrast = preset_contrast;
		EditorSettings::get_singleton()->set_initial_value("interface/theme/accent_color", accent_color);
		EditorSettings::get_singleton()->set_initial_value("interface/theme/base_color", base_color);
		EditorSettings::get_singleton()->set_initial_value("interface/theme/contrast", contrast);
	}
	EditorSettings::get_singleton()->set_manually("interface/theme/preset", preset);
	EditorSettings::get_singleton()->set_manually("interface/theme/accent_color", accent_color);
	EditorSettings::get_singleton()->set_manually("interface/theme/base_color", base_color);
	EditorSettings::get_singleton()->set_manually("interface/theme/contrast", contrast);

	//Colors
	bool dark_theme = EditorSettings::get_singleton()->is_dark_theme();

	const Color dark_color_1 = base_color.linear_interpolate(Color(0, 0, 0, 1), contrast);
	const Color dark_color_2 = base_color.linear_interpolate(Color(0, 0, 0, 1), contrast * 1.5);
	const Color dark_color_3 = base_color.linear_interpolate(Color(0, 0, 0, 1), contrast * 2);

	const Color background_color = dark_color_2;

	// white (dark theme) or black (light theme), will be used to generate the rest of the colors
	const Color mono_color = dark_theme ? Color(1, 1, 1) : Color(0, 0, 0);

	const Color contrast_color_1 = base_color.linear_interpolate(mono_color, MAX(contrast, default_contrast));
	const Color contrast_color_2 = base_color.linear_interpolate(mono_color, MAX(contrast * 1.5, default_contrast * 1.5));

	const Color font_color = mono_color.linear_interpolate(base_color, 0.25);
	const Color font_color_hl = mono_color.linear_interpolate(base_color, 0.15);
	const Color font_color_focus = mono_color.linear_interpolate(base_color, 0.15);
	const Color font_color_disabled = Color(mono_color.r, mono_color.g, mono_color.b, 0.3);
	const Color font_color_readonly = Color(mono_color.r, mono_color.g, mono_color.b, 0.65);
	const Color font_color_selection = accent_color * Color(1, 1, 1, 0.4);
	const Color color_disabled = mono_color.inverted().linear_interpolate(base_color, 0.7);
	const Color color_disabled_bg = mono_color.inverted().linear_interpolate(base_color, 0.9);

	const Color icon_color_normal = Color(1, 1, 1);
	Color icon_color_hover = icon_color_normal * (dark_theme ? 1.15 : 1.45);
	icon_color_hover.a = 1.0;
	// Make the pressed icon color overbright because icons are not completely white on a dark theme.
	// On a light theme, icons are dark, so we need to modulate them with an even brighter color.
	Color icon_color_pressed = accent_color * (dark_theme ? 1.15 : 3.5);
	icon_color_pressed.a = 1.0;

	const Color separator_color = Color(mono_color.r, mono_color.g, mono_color.b, 0.1);

	const Color highlight_color = Color(mono_color.r, mono_color.g, mono_color.b, 0.2);

	theme->set_color("accent_color", "Editor", accent_color);
	theme->set_color("highlight_color", "Editor", highlight_color);
	theme->set_color("base_color", "Editor", base_color);
	theme->set_color("dark_color_1", "Editor", dark_color_1);
	theme->set_color("dark_color_2", "Editor", dark_color_2);
	theme->set_color("dark_color_3", "Editor", dark_color_3);
	theme->set_color("contrast_color_1", "Editor", contrast_color_1);
	theme->set_color("contrast_color_2", "Editor", contrast_color_2);
	theme->set_color("box_selection_fill_color", "Editor", accent_color * Color(1, 1, 1, 0.3));
	theme->set_color("box_selection_stroke_color", "Editor", accent_color * Color(1, 1, 1, 0.8));

	theme->set_color("axis_x_color", "Editor", Color(0.96, 0.20, 0.32));
	theme->set_color("axis_y_color", "Editor", Color(0.53, 0.84, 0.01));
	theme->set_color("axis_z_color", "Editor", Color(0.16, 0.55, 0.96));

	theme->set_color("font_color", "Editor", font_color);
	theme->set_color("highlighted_font_color", "Editor", font_color_hl);
	theme->set_color("disabled_font_color", "Editor", font_color_disabled);

	theme->set_color("mono_color", "Editor", mono_color);

	Color success_color = Color(0.45, 0.95, 0.5);
	Color warning_color = Color(1, 0.87, 0.4);
	Color error_color = Color(1, 0.47, 0.42);
	Color property_color = font_color.linear_interpolate(Color(0.5, 0.5, 0.5), 0.5);

	if (!dark_theme) {
		// Darken some colors to be readable on a light background
		success_color = success_color.linear_interpolate(mono_color, 0.35);
		warning_color = warning_color.linear_interpolate(mono_color, 0.35);
		error_color = error_color.linear_interpolate(mono_color, 0.25);
	}

	theme->set_color("success_color", "Editor", success_color);
	theme->set_color("warning_color", "Editor", warning_color);
	theme->set_color("error_color", "Editor", error_color);
	theme->set_color("property_color", "Editor", property_color);

	const int thumb_size = EDITOR_GET("filesystem/file_dialog/thumbnail_size");
	theme->set_constant("scale", "Editor", EDSCALE);
	theme->set_constant("thumb_size", "Editor", thumb_size);
	theme->set_constant("dark_theme", "Editor", dark_theme);

	//Register icons + font

	// the resolution and the icon color (dark_theme bool) has not changed, so we do not regenerate the icons
	if (p_theme != nullptr && fabs(p_theme->get_constant("scale", "Editor") - EDSCALE) < 0.00001 && (bool)p_theme->get_constant("dark_theme", "Editor") == dark_theme) {
		// register already generated icons
		for (int i = 0; i < editor_icons_count; i++) {
			theme->set_icon(editor_icons_names[i], "EditorIcons", p_theme->get_icon(editor_icons_names[i], "EditorIcons"));
		}
	} else {
		editor_register_and_generate_icons(theme, dark_theme, thumb_size);
	}
	// thumbnail size has changed, so we regenerate the medium sizes
	if (p_theme != nullptr && fabs((double)p_theme->get_constant("thumb_size", "Editor") - thumb_size) > 0.00001) {
		editor_register_and_generate_icons(p_theme, dark_theme, thumb_size, true);
	}

	editor_register_fonts(theme);

	// Highlighted tabs and border width
	Color tab_color = highlight_tabs ? base_color.linear_interpolate(font_color, contrast) : base_color;
	// Ensure borders are visible when using an editor scale below 100%.
	const int border_width = CLAMP(border_size, 0, 3) * MAX(1, EDSCALE);

	const int default_margin_size = 4;
	const int margin_size_extra = default_margin_size + CLAMP(border_size, 0, 3);

	// styleboxes
	// this is the most commonly used stylebox, variations should be made as duplicate of this
	Ref<StyleBoxFlat> style_default = make_flat_stylebox(base_color, default_margin_size, default_margin_size, default_margin_size, default_margin_size);
	style_default->set_border_width_all(border_width);
	style_default->set_border_color(base_color);
	style_default->set_draw_center(true);

	// Button and widgets
	const float extra_spacing = EDITOR_GET("interface/theme/additional_spacing");

	Ref<StyleBoxFlat> style_widget = style_default->duplicate();
	style_widget->set_default_margin(MARGIN_LEFT, (extra_spacing + 6) * EDSCALE);
	style_widget->set_default_margin(MARGIN_TOP, (extra_spacing + default_margin_size) * EDSCALE);
	style_widget->set_default_margin(MARGIN_RIGHT, (extra_spacing + 6) * EDSCALE);
	style_widget->set_default_margin(MARGIN_BOTTOM, (extra_spacing + default_margin_size) * EDSCALE);
	style_widget->set_bg_color(dark_color_1);
	style_widget->set_border_color(dark_color_2);

	Ref<StyleBoxFlat> style_widget_disabled = style_widget->duplicate();
	style_widget_disabled->set_border_color(color_disabled);
	style_widget_disabled->set_bg_color(color_disabled_bg);

	Ref<StyleBoxFlat> style_widget_focus = style_widget->duplicate();
	style_widget_focus->set_border_color(accent_color);

	Ref<StyleBoxFlat> style_widget_pressed = style_widget->duplicate();
	style_widget_pressed->set_border_color(accent_color);

	Ref<StyleBoxFlat> style_widget_hover = style_widget->duplicate();
	style_widget_hover->set_border_color(contrast_color_1);

	// style for windows, popups, etc..
	Ref<StyleBoxFlat> style_popup = style_default->duplicate();
	const int popup_margin_size = default_margin_size * EDSCALE * 2;
	style_popup->set_default_margin(MARGIN_LEFT, popup_margin_size);
	style_popup->set_default_margin(MARGIN_TOP, popup_margin_size);
	style_popup->set_default_margin(MARGIN_RIGHT, popup_margin_size);
	style_popup->set_default_margin(MARGIN_BOTTOM, popup_margin_size);
	style_popup->set_border_color(contrast_color_1);
	style_popup->set_border_width_all(MAX(EDSCALE, border_width));
	const Color shadow_color = Color(0, 0, 0, dark_theme ? 0.3 : 0.1);
	style_popup->set_shadow_color(shadow_color);
	style_popup->set_shadow_size(4 * EDSCALE);

	Ref<StyleBoxLine> style_popup_separator(memnew(StyleBoxLine));
	style_popup_separator->set_color(separator_color);
	style_popup_separator->set_grow_begin(popup_margin_size - MAX(EDSCALE, border_width));
	style_popup_separator->set_grow_end(popup_margin_size - MAX(EDSCALE, border_width));
	style_popup_separator->set_thickness(MAX(EDSCALE, border_width));

	Ref<StyleBoxLine> style_popup_labeled_separator_left(memnew(StyleBoxLine));
	style_popup_labeled_separator_left->set_grow_begin(popup_margin_size - MAX(EDSCALE, border_width));
	style_popup_labeled_separator_left->set_color(separator_color);
	style_popup_labeled_separator_left->set_thickness(MAX(EDSCALE, border_width));

	Ref<StyleBoxLine> style_popup_labeled_separator_right(memnew(StyleBoxLine));
	style_popup_labeled_separator_right->set_grow_end(popup_margin_size - MAX(EDSCALE, border_width));
	style_popup_labeled_separator_right->set_color(separator_color);
	style_popup_labeled_separator_right->set_thickness(MAX(EDSCALE, border_width));

	Ref<StyleBoxEmpty> style_empty = make_empty_stylebox(default_margin_size, default_margin_size, default_margin_size, default_margin_size);

	// Tabs

	const int tab_default_margin_side = 10 * EDSCALE + extra_spacing * EDSCALE;
	const int tab_default_margin_vertical = 5 * EDSCALE + extra_spacing * EDSCALE;

	Ref<StyleBoxFlat> style_tab_selected = style_widget->duplicate();

	style_tab_selected->set_border_width_all(border_width);
	style_tab_selected->set_border_width(MARGIN_BOTTOM, 0);
	style_tab_selected->set_border_color(dark_color_3);
	style_tab_selected->set_expand_margin_size(MARGIN_BOTTOM, border_width);
	style_tab_selected->set_default_margin(MARGIN_LEFT, tab_default_margin_side);
	style_tab_selected->set_default_margin(MARGIN_RIGHT, tab_default_margin_side);
	style_tab_selected->set_default_margin(MARGIN_BOTTOM, tab_default_margin_vertical);
	style_tab_selected->set_default_margin(MARGIN_TOP, tab_default_margin_vertical);
	style_tab_selected->set_bg_color(tab_color);

	Ref<StyleBoxFlat> style_tab_unselected = style_tab_selected->duplicate();
	style_tab_unselected->set_bg_color(dark_color_1);
	style_tab_unselected->set_border_color(dark_color_2);

	Ref<StyleBoxFlat> style_tab_disabled = style_tab_selected->duplicate();
	style_tab_disabled->set_bg_color(color_disabled_bg);
	style_tab_disabled->set_border_color(color_disabled);

	// Editor background
	Color background_color_opaque = background_color;
	background_color_opaque.a = 1.0;
	theme->set_stylebox("Background", "EditorStyles", make_flat_stylebox(background_color_opaque, default_margin_size, default_margin_size, default_margin_size, default_margin_size));

	// Focus
	Ref<StyleBoxFlat> style_focus = style_default->duplicate();
	style_focus->set_draw_center(false);
	style_focus->set_border_color(contrast_color_2);
	theme->set_stylebox("Focus", "EditorStyles", style_focus);

	// Menu
	Ref<StyleBoxFlat> style_menu = style_widget->duplicate();
	style_menu->set_draw_center(false);
	style_menu->set_border_width_all(0);
	theme->set_stylebox("panel", "PanelContainer", style_menu);
	theme->set_stylebox("MenuPanel", "EditorStyles", style_menu);

	// CanvasItem Editor
	Ref<StyleBoxFlat> style_canvas_editor_info = make_flat_stylebox(Color(0.0, 0.0, 0.0, 0.2));
	style_canvas_editor_info->set_expand_margin_size_all(4 * EDSCALE);
	theme->set_stylebox("CanvasItemInfoOverlay", "EditorStyles", style_canvas_editor_info);

	// Script Editor
	theme->set_stylebox("ScriptEditorPanel", "EditorStyles", make_empty_stylebox(default_margin_size, 0, default_margin_size, default_margin_size));
	theme->set_stylebox("ScriptEditor", "EditorStyles", make_empty_stylebox(0, 0, 0, 0));

	// Play button group
	theme->set_stylebox("PlayButtonPanel", "EditorStyles", style_empty);

	//MenuButton
	Ref<StyleBoxFlat> style_menu_hover_border = style_widget->duplicate();
	style_menu_hover_border->set_draw_center(false);
	style_menu_hover_border->set_border_width_all(0);
	style_menu_hover_border->set_border_width(MARGIN_BOTTOM, border_width);
	style_menu_hover_border->set_border_color(accent_color);

	Ref<StyleBoxFlat> style_menu_hover_bg = style_widget->duplicate();
	style_menu_hover_bg->set_border_width_all(0);
	style_menu_hover_bg->set_bg_color(dark_color_1);

	theme->set_stylebox("normal", "MenuButton", style_menu);
	theme->set_stylebox("hover", "MenuButton", style_menu);
	theme->set_stylebox("pressed", "MenuButton", style_menu);
	theme->set_stylebox("focus", "MenuButton", style_menu);
	theme->set_stylebox("disabled", "MenuButton", style_menu);

	theme->set_stylebox("normal", "PopupMenu", style_menu);
	theme->set_stylebox("hover", "PopupMenu", style_menu_hover_bg);
	theme->set_stylebox("pressed", "PopupMenu", style_menu);
	theme->set_stylebox("focus", "PopupMenu", style_menu);
	theme->set_stylebox("disabled", "PopupMenu", style_menu);

	theme->set_stylebox("normal", "ToolButton", style_menu);
	theme->set_stylebox("hover", "ToolButton", style_menu);
	theme->set_stylebox("pressed", "ToolButton", style_menu);
	theme->set_stylebox("focus", "ToolButton", style_menu);
	theme->set_stylebox("disabled", "ToolButton", style_menu);

	theme->set_color("font_color", "MenuButton", font_color);
	theme->set_color("font_color_hover", "MenuButton", font_color_hl);
	theme->set_color("font_color_focus", "MenuButton", font_color_focus);
	theme->set_color("font_color", "ToolButton", font_color);
	theme->set_color("font_color_hover", "ToolButton", font_color_hl);
	theme->set_color("font_color_focus", "ToolButton", font_color_focus);
	theme->set_color("font_color_pressed", "ToolButton", accent_color);

	theme->set_stylebox("MenuHover", "EditorStyles", style_menu_hover_border);

	// Buttons
	theme->set_stylebox("normal", "Button", style_widget);
	theme->set_stylebox("hover", "Button", style_widget_hover);
	theme->set_stylebox("pressed", "Button", style_widget_pressed);
	theme->set_stylebox("focus", "Button", style_widget_focus);
	theme->set_stylebox("disabled", "Button", style_widget_disabled);

	theme->set_color("font_color", "Button", font_color);
	theme->set_color("font_color_hover", "Button", font_color_hl);
	theme->set_color("font_color_focus", "Button", font_color_focus);
	theme->set_color("font_color_pressed", "Button", accent_color);
	theme->set_color("font_color_disabled", "Button", font_color_disabled);
	theme->set_color("icon_color_normal", "Button", icon_color_normal);
	theme->set_color("icon_color_hover", "Button", icon_color_hover);
	theme->set_color("icon_color_pressed", "Button", icon_color_pressed);

	// OptionButton
	theme->set_stylebox("normal", "OptionButton", style_widget);
	theme->set_stylebox("hover", "OptionButton", style_widget_hover);
	theme->set_stylebox("pressed", "OptionButton", style_widget_pressed);
	theme->set_stylebox("focus", "OptionButton", style_widget_focus);
	theme->set_stylebox("disabled", "OptionButton", style_widget_disabled);

	theme->set_color("font_color", "OptionButton", font_color);
	theme->set_color("font_color_hover", "OptionButton", font_color_hl);
	theme->set_color("font_color_focus", "OptionButton", font_color_focus);
	theme->set_color("font_color_pressed", "OptionButton", accent_color);
	theme->set_color("font_color_disabled", "OptionButton", font_color_disabled);
	theme->set_color("icon_color_hover", "OptionButton", icon_color_hover);
	theme->set_icon("arrow", "OptionButton", theme->get_icon("GuiOptionArrow", "EditorIcons"));
	theme->set_constant("arrow_margin", "OptionButton", default_margin_size * EDSCALE);
	theme->set_constant("modulate_arrow", "OptionButton", true);
	theme->set_constant("hseparation", "OptionButton", 4 * EDSCALE);

	// CheckButton
	theme->set_stylebox("normal", "CheckButton", style_menu);
	theme->set_stylebox("pressed", "CheckButton", style_menu);
	theme->set_stylebox("disabled", "CheckButton", style_menu);
	theme->set_stylebox("hover", "CheckButton", style_menu);

	theme->set_icon("on", "CheckButton", theme->get_icon("GuiToggleOn", "EditorIcons"));
	theme->set_icon("on_disabled", "CheckButton", theme->get_icon("GuiToggleOnDisabled", "EditorIcons"));
	theme->set_icon("off", "CheckButton", theme->get_icon("GuiToggleOff", "EditorIcons"));
	theme->set_icon("off_disabled", "CheckButton", theme->get_icon("GuiToggleOffDisabled", "EditorIcons"));

	theme->set_color("font_color", "CheckButton", font_color);
	theme->set_color("font_color_hover", "CheckButton", font_color_hl);
	theme->set_color("font_color_focus", "CheckButton", font_color_focus);
	theme->set_color("font_color_pressed", "CheckButton", accent_color);
	theme->set_color("font_color_disabled", "CheckButton", font_color_disabled);
	theme->set_color("icon_color_hover", "CheckButton", icon_color_hover);

	theme->set_constant("hseparation", "CheckButton", 4 * EDSCALE);
	theme->set_constant("check_vadjust", "CheckButton", 0 * EDSCALE);

	// Checkbox
	Ref<StyleBoxFlat> sb_checkbox = style_menu->duplicate();
	sb_checkbox->set_default_margin(MARGIN_LEFT, default_margin_size * EDSCALE);
	sb_checkbox->set_default_margin(MARGIN_RIGHT, default_margin_size * EDSCALE);
	sb_checkbox->set_default_margin(MARGIN_TOP, default_margin_size * EDSCALE);
	sb_checkbox->set_default_margin(MARGIN_BOTTOM, default_margin_size * EDSCALE);

	theme->set_stylebox("normal", "CheckBox", sb_checkbox);
	theme->set_stylebox("pressed", "CheckBox", sb_checkbox);
	theme->set_stylebox("disabled", "CheckBox", sb_checkbox);
	theme->set_stylebox("hover", "CheckBox", sb_checkbox);
	theme->set_icon("checked", "CheckBox", theme->get_icon("GuiChecked", "EditorIcons"));
	theme->set_icon("unchecked", "CheckBox", theme->get_icon("GuiUnchecked", "EditorIcons"));
	theme->set_icon("checked_disabled", "CheckBox", theme->get_icon("GuiCheckedDisabled", "EditorIcons"));
	theme->set_icon("unchecked_disabled", "CheckBox", theme->get_icon("GuiUncheckedDisabled", "EditorIcons"));
	theme->set_icon("radio_checked", "CheckBox", theme->get_icon("GuiRadioChecked", "EditorIcons"));
	theme->set_icon("radio_unchecked", "CheckBox", theme->get_icon("GuiRadioUnchecked", "EditorIcons"));
	theme->set_icon("radio_checked_disabled", "CheckBox", theme->get_icon("GuiRadioCheckedDisabled", "EditorIcons"));
	theme->set_icon("radio_unchecked_disabled", "CheckBox", theme->get_icon("GuiRadioUncheckedDisabled", "EditorIcons"));

	theme->set_color("font_color", "CheckBox", font_color);
	theme->set_color("font_color_hover", "CheckBox", font_color_hl);
	theme->set_color("font_color_focus", "CheckBox", font_color_focus);
	theme->set_color("font_color_pressed", "CheckBox", accent_color);
	theme->set_color("font_color_disabled", "CheckBox", font_color_disabled);
	theme->set_color("icon_color_hover", "CheckBox", icon_color_hover);

	theme->set_constant("hseparation", "CheckBox", 4 * EDSCALE);
	theme->set_constant("check_vadjust", "CheckBox", 0 * EDSCALE);

	// PopupDialog
	theme->set_stylebox("panel", "PopupDialog", style_popup);

	// PopupMenu
	theme->set_stylebox("panel", "PopupMenu", style_popup);
	theme->set_stylebox("separator", "PopupMenu", style_popup_separator);
	theme->set_stylebox("labeled_separator_left", "PopupMenu", style_popup_labeled_separator_left);
	theme->set_stylebox("labeled_separator_right", "PopupMenu", style_popup_labeled_separator_right);

	theme->set_color("font_color", "PopupMenu", font_color);
	theme->set_color("font_color_hover", "PopupMenu", font_color_hl);
	theme->set_color("font_color_accel", "PopupMenu", font_color_disabled);
	theme->set_color("font_color_disabled", "PopupMenu", font_color_disabled);
	theme->set_color("font_color_separator", "PopupMenu", font_color_disabled);
	theme->set_icon("checked", "PopupMenu", theme->get_icon("GuiChecked", "EditorIcons"));
	theme->set_icon("unchecked", "PopupMenu", theme->get_icon("GuiUnchecked", "EditorIcons"));
	theme->set_icon("radio_checked", "PopupMenu", theme->get_icon("GuiRadioChecked", "EditorIcons"));
	theme->set_icon("radio_unchecked", "PopupMenu", theme->get_icon("GuiRadioUnchecked", "EditorIcons"));
	theme->set_icon("submenu", "PopupMenu", theme->get_icon("ArrowRight", "EditorIcons"));
	theme->set_icon("visibility_hidden", "PopupMenu", theme->get_icon("GuiVisibilityHidden", "EditorIcons"));
	theme->set_icon("visibility_visible", "PopupMenu", theme->get_icon("GuiVisibilityVisible", "EditorIcons"));
	theme->set_icon("visibility_xray", "PopupMenu", theme->get_icon("GuiVisibilityXray", "EditorIcons"));
	theme->set_constant("vseparation", "PopupMenu", (extra_spacing + default_margin_size + 1) * EDSCALE);

	for (int i = 0; i < 16; i++) {
		Color si_base_color = accent_color;

		float hue_rotate = (i * 2 % 16) / 16.0;
		si_base_color.set_hsv(Math::fmod(float(si_base_color.get_h() + hue_rotate), float(1.0)), si_base_color.get_s(), si_base_color.get_v());
		si_base_color = accent_color.linear_interpolate(si_base_color, float(EDITOR_GET("docks/property_editor/subresource_hue_tint")));

		Ref<StyleBoxFlat> sub_inspector_bg;

		sub_inspector_bg = make_flat_stylebox(dark_color_1.linear_interpolate(si_base_color, 0.08), 2, 0, 2, 2);

		sub_inspector_bg->set_border_width_all(2);
		sub_inspector_bg->set_default_margin(MARGIN_LEFT, 4 * EDSCALE);
		sub_inspector_bg->set_default_margin(MARGIN_RIGHT, 4 * EDSCALE);
		sub_inspector_bg->set_default_margin(MARGIN_BOTTOM, 4 * EDSCALE);
		sub_inspector_bg->set_default_margin(MARGIN_TOP, 4 * EDSCALE);
		sub_inspector_bg->set_border_color(si_base_color * Color(0.7, 0.7, 0.7, 0.8));
		sub_inspector_bg->set_draw_center(true);

		theme->set_stylebox("sub_inspector_bg" + itos(i), "Editor", sub_inspector_bg);

		Ref<StyleBoxFlat> bg_color;
		bg_color.instance();
		bg_color->set_bg_color(si_base_color * Color(0.7, 0.7, 0.7, 0.8));
		bg_color->set_border_width_all(0);

		Ref<StyleBoxFlat> bg_color_selected;
		bg_color_selected.instance();
		bg_color_selected->set_border_width_all(0);
		bg_color_selected->set_bg_color(si_base_color * Color(0.8, 0.8, 0.8, 0.8));

		theme->set_stylebox("sub_inspector_property_bg" + itos(i), "Editor", bg_color);
		theme->set_stylebox("sub_inspector_property_bg_selected" + itos(i), "Editor", bg_color_selected);
	}

	theme->set_color("sub_inspector_property_color", "Editor", dark_theme ? Color(1, 1, 1, 1) : Color(0, 0, 0, 1));
	theme->set_constant("sub_inspector_font_offset", "Editor", 4 * EDSCALE);

	Ref<StyleBoxFlat> style_property_bg = style_default->duplicate();
	style_property_bg->set_bg_color(highlight_color);
	style_property_bg->set_border_width_all(0);

	theme->set_constant("font_offset", "EditorProperty", 1 * EDSCALE);
	theme->set_stylebox("bg_selected", "EditorProperty", style_property_bg);
	theme->set_stylebox("bg", "EditorProperty", Ref<StyleBoxEmpty>(memnew(StyleBoxEmpty)));
	theme->set_constant("vseparation", "EditorProperty", (extra_spacing + default_margin_size) * EDSCALE);
	theme->set_color("warning_color", "EditorProperty", warning_color);
	theme->set_color("property_color", "EditorProperty", property_color);

	theme->set_constant("inspector_margin", "Editor", 8 * EDSCALE);

	// Tree & ItemList background
	Ref<StyleBoxFlat> style_tree_bg = style_default->duplicate();
	style_tree_bg->set_bg_color(dark_color_1);
	style_tree_bg->set_border_color(dark_color_3);
	theme->set_stylebox("bg", "Tree", style_tree_bg);

	const Color guide_color = Color(mono_color.r, mono_color.g, mono_color.b, 0.05);
	Color relationship_line_color = Color(mono_color.r, mono_color.g, mono_color.b, relationship_line_opacity);
	// Tree
	theme->set_icon("checked", "Tree", theme->get_icon("GuiChecked", "EditorIcons"));
	theme->set_icon("unchecked", "Tree", theme->get_icon("GuiUnchecked", "EditorIcons"));
	theme->set_icon("arrow", "Tree", theme->get_icon("GuiTreeArrowDown", "EditorIcons"));
	theme->set_icon("arrow_collapsed", "Tree", theme->get_icon("GuiTreeArrowRight", "EditorIcons"));
	theme->set_icon("updown", "Tree", theme->get_icon("GuiTreeUpdown", "EditorIcons"));
	theme->set_icon("select_arrow", "Tree", theme->get_icon("GuiDropdown", "EditorIcons"));
	theme->set_stylebox("bg_focus", "Tree", style_focus);
	theme->set_stylebox("custom_button", "Tree", make_empty_stylebox());
	theme->set_stylebox("custom_button_pressed", "Tree", make_empty_stylebox());
	theme->set_stylebox("custom_button_hover", "Tree", style_widget);
	theme->set_color("custom_button_font_highlight", "Tree", font_color_hl);
	theme->set_color("font_color", "Tree", font_color);
	theme->set_color("font_color_selected", "Tree", mono_color);
	theme->set_color("title_button_color", "Tree", font_color);
	theme->set_color("guide_color", "Tree", guide_color);
	theme->set_color("relationship_line_color", "Tree", relationship_line_color);
	theme->set_color("drop_position_color", "Tree", accent_color);
	theme->set_constant("vseparation", "Tree", (extra_spacing + default_margin_size) * EDSCALE);
	theme->set_constant("hseparation", "Tree", (extra_spacing + default_margin_size) * EDSCALE);
	theme->set_constant("item_margin", "Tree", 3 * default_margin_size * EDSCALE);
	theme->set_constant("button_margin", "Tree", default_margin_size * EDSCALE);
	theme->set_constant("draw_relationship_lines", "Tree", relationship_line_opacity >= 0.01);
	theme->set_constant("draw_guides", "Tree", relationship_line_opacity < 0.01);
	theme->set_constant("scroll_border", "Tree", 40 * EDSCALE);
	theme->set_constant("scroll_speed", "Tree", 12);

	Ref<StyleBoxFlat> style_tree_btn = style_default->duplicate();
	style_tree_btn->set_bg_color(contrast_color_1);
	style_tree_btn->set_border_width_all(0);
	theme->set_stylebox("button_pressed", "Tree", style_tree_btn);

	Ref<StyleBoxFlat> style_tree_hover = style_default->duplicate();
	style_tree_hover->set_bg_color(highlight_color * Color(1, 1, 1, 0.4));
	style_tree_hover->set_border_width_all(0);
	theme->set_stylebox("hover", "Tree", style_tree_hover);

	Ref<StyleBoxFlat> style_tree_focus = style_default->duplicate();
	style_tree_focus->set_bg_color(highlight_color);
	style_tree_focus->set_border_width_all(0);
	theme->set_stylebox("selected_focus", "Tree", style_tree_focus);

	Ref<StyleBoxFlat> style_tree_selected = style_tree_focus->duplicate();
	theme->set_stylebox("selected", "Tree", style_tree_selected);

	Ref<StyleBoxFlat> style_tree_cursor = style_default->duplicate();
	style_tree_cursor->set_draw_center(false);
	style_tree_cursor->set_border_width_all(MAX(1, border_width));
	style_tree_cursor->set_border_color(contrast_color_1);

	Ref<StyleBoxFlat> style_tree_title = style_default->duplicate();
	style_tree_title->set_bg_color(dark_color_3);
	style_tree_title->set_border_width_all(0);
	theme->set_stylebox("cursor", "Tree", style_tree_cursor);
	theme->set_stylebox("cursor_unfocused", "Tree", style_tree_cursor);
	theme->set_stylebox("title_button_normal", "Tree", style_tree_title);
	theme->set_stylebox("title_button_hover", "Tree", style_tree_title);
	theme->set_stylebox("title_button_pressed", "Tree", style_tree_title);

	Color prop_category_color = dark_color_1.linear_interpolate(mono_color, 0.12);
	Color prop_section_color = dark_color_1.linear_interpolate(mono_color, 0.09);
	Color prop_subsection_color = dark_color_1.linear_interpolate(mono_color, 0.06);
	theme->set_color("prop_category", "Editor", prop_category_color);
	theme->set_color("prop_section", "Editor", prop_section_color);
	theme->set_color("prop_subsection", "Editor", prop_subsection_color);
	theme->set_color("drop_position_color", "Tree", accent_color);

	// ItemList
	Ref<StyleBoxFlat> style_itemlist_bg = style_default->duplicate();
	style_itemlist_bg->set_bg_color(dark_color_1);
	style_itemlist_bg->set_border_width_all(border_width);
	style_itemlist_bg->set_border_color(dark_color_3);

	Ref<StyleBoxFlat> style_itemlist_cursor = style_default->duplicate();
	style_itemlist_cursor->set_draw_center(false);
	style_itemlist_cursor->set_border_width_all(border_width);
	style_itemlist_cursor->set_border_color(highlight_color);
	theme->set_stylebox("cursor", "ItemList", style_itemlist_cursor);
	theme->set_stylebox("cursor_unfocused", "ItemList", style_itemlist_cursor);
	theme->set_stylebox("selected_focus", "ItemList", style_tree_focus);
	theme->set_stylebox("selected", "ItemList", style_tree_selected);
	theme->set_stylebox("bg_focus", "ItemList", style_focus);
	theme->set_stylebox("bg", "ItemList", style_itemlist_bg);
	theme->set_color("font_color", "ItemList", font_color);
	theme->set_color("font_color_selected", "ItemList", mono_color);
	theme->set_color("guide_color", "ItemList", guide_color);
	theme->set_constant("vseparation", "ItemList", 3 * EDSCALE);
	theme->set_constant("hseparation", "ItemList", 3 * EDSCALE);
	theme->set_constant("icon_margin", "ItemList", default_margin_size * EDSCALE);
	theme->set_constant("line_separation", "ItemList", 3 * EDSCALE);

	// Tabs & TabContainer
	theme->set_stylebox("tab_fg", "TabContainer", style_tab_selected);
	theme->set_stylebox("tab_bg", "TabContainer", style_tab_unselected);
	theme->set_stylebox("tab_disabled", "TabContainer", style_tab_disabled);
	theme->set_stylebox("tab_fg", "Tabs", style_tab_selected);
	theme->set_stylebox("tab_bg", "Tabs", style_tab_unselected);
	theme->set_stylebox("tab_disabled", "Tabs", style_tab_disabled);
	theme->set_color("font_color_fg", "TabContainer", font_color);
	theme->set_color("font_color_bg", "TabContainer", font_color_disabled);
	theme->set_color("font_color_fg", "Tabs", font_color);
	theme->set_color("font_color_bg", "Tabs", font_color_disabled);
	theme->set_icon("menu", "TabContainer", theme->get_icon("GuiTabMenu", "EditorIcons"));
	theme->set_icon("menu_highlight", "TabContainer", theme->get_icon("GuiTabMenuHl", "EditorIcons"));
	theme->set_stylebox("SceneTabFG", "EditorStyles", style_tab_selected);
	theme->set_stylebox("SceneTabBG", "EditorStyles", style_tab_unselected);
	theme->set_icon("close", "Tabs", theme->get_icon("GuiClose", "EditorIcons"));
	theme->set_stylebox("button_pressed", "Tabs", style_menu);
	theme->set_stylebox("button", "Tabs", style_menu);
	theme->set_icon("increment", "TabContainer", theme->get_icon("GuiScrollArrowRight", "EditorIcons"));
	theme->set_icon("decrement", "TabContainer", theme->get_icon("GuiScrollArrowLeft", "EditorIcons"));
	theme->set_icon("increment", "Tabs", theme->get_icon("GuiScrollArrowRight", "EditorIcons"));
	theme->set_icon("decrement", "Tabs", theme->get_icon("GuiScrollArrowLeft", "EditorIcons"));
	theme->set_icon("increment_highlight", "Tabs", theme->get_icon("GuiScrollArrowRightHl", "EditorIcons"));
	theme->set_icon("decrement_highlight", "Tabs", theme->get_icon("GuiScrollArrowLeftHl", "EditorIcons"));
	theme->set_icon("increment_highlight", "TabContainer", theme->get_icon("GuiScrollArrowRightHl", "EditorIcons"));
	theme->set_icon("decrement_highlight", "TabContainer", theme->get_icon("GuiScrollArrowLeftHl", "EditorIcons"));
	theme->set_constant("hseparation", "Tabs", 4 * EDSCALE);

	// Content of each tab
	Ref<StyleBoxFlat> style_content_panel = style_default->duplicate();
	style_content_panel->set_border_color(dark_color_3);
	style_content_panel->set_border_width_all(border_width);
	// compensate the border
	style_content_panel->set_default_margin(MARGIN_TOP, margin_size_extra * EDSCALE);
	style_content_panel->set_default_margin(MARGIN_RIGHT, margin_size_extra * EDSCALE);
	style_content_panel->set_default_margin(MARGIN_BOTTOM, margin_size_extra * EDSCALE);
	style_content_panel->set_default_margin(MARGIN_LEFT, margin_size_extra * EDSCALE);

	// These styleboxes can be used on tabs against the base color background (e.g. nested tabs).
	Ref<StyleBoxFlat> style_tab_selected_odd = style_tab_selected->duplicate();
	style_tab_selected_odd->set_bg_color(color_disabled_bg);
	theme->set_stylebox("tab_selected_odd", "TabContainer", style_tab_selected_odd);

	Ref<StyleBoxFlat> style_content_panel_odd = style_content_panel->duplicate();
	style_content_panel_odd->set_bg_color(color_disabled_bg);
	theme->set_stylebox("panel_odd", "TabContainer", style_content_panel_odd);

	// This stylebox is used in 3d and 2d viewports (no borders).
	Ref<StyleBoxFlat> style_content_panel_vp = style_content_panel->duplicate();
	style_content_panel_vp->set_default_margin(MARGIN_LEFT, border_width * 2);
	style_content_panel_vp->set_default_margin(MARGIN_TOP, default_margin_size * EDSCALE);
	style_content_panel_vp->set_default_margin(MARGIN_RIGHT, border_width * 2);
	style_content_panel_vp->set_default_margin(MARGIN_BOTTOM, border_width * 2);
	theme->set_stylebox("panel", "TabContainer", style_content_panel);
	theme->set_stylebox("Content", "EditorStyles", style_content_panel_vp);

	// This stylebox is used by preview tabs in the Theme Editor.
	Ref<StyleBoxFlat> style_theme_preview_tab = style_tab_selected_odd->duplicate();
	style_theme_preview_tab->set_expand_margin_size(MARGIN_BOTTOM, 3 * EDSCALE);
	theme->set_stylebox("ThemeEditorPreviewFG", "EditorStyles", style_theme_preview_tab);
	Ref<StyleBoxFlat> style_theme_preview_bg_tab = style_tab_unselected->duplicate();
	style_theme_preview_bg_tab->set_expand_margin_size(MARGIN_BOTTOM, 2 * EDSCALE);
	theme->set_stylebox("ThemeEditorPreviewBG", "EditorStyles", style_theme_preview_bg_tab);

	// Separators
	theme->set_stylebox("separator", "HSeparator", make_line_stylebox(separator_color, border_width));
	theme->set_stylebox("separator", "VSeparator", make_line_stylebox(separator_color, border_width, 0, 0, true));

	// Debugger

	Ref<StyleBoxFlat> style_panel_debugger = style_content_panel->duplicate();
	style_panel_debugger->set_border_width(MARGIN_BOTTOM, 0);
	theme->set_stylebox("DebuggerPanel", "EditorStyles", style_panel_debugger);
	theme->set_stylebox("DebuggerTabFG", "EditorStyles", style_tab_selected);
	theme->set_stylebox("DebuggerTabBG", "EditorStyles", style_tab_unselected);

	Ref<StyleBoxFlat> style_panel_invisible_top = style_content_panel->duplicate();
	int stylebox_offset = theme->get_font("tab_fg", "TabContainer")->get_height() + theme->get_stylebox("tab_fg", "TabContainer")->get_minimum_size().height + theme->get_stylebox("panel", "TabContainer")->get_default_margin(MARGIN_TOP);
	style_panel_invisible_top->set_expand_margin_size(MARGIN_TOP, -stylebox_offset);
	style_panel_invisible_top->set_default_margin(MARGIN_TOP, 0);
	theme->set_stylebox("BottomPanelDebuggerOverride", "EditorStyles", style_panel_invisible_top);

	// LineEdit
	theme->set_stylebox("normal", "LineEdit", style_widget);
	theme->set_stylebox("focus", "LineEdit", style_widget_focus);
	theme->set_stylebox("read_only", "LineEdit", style_widget_disabled);
	theme->set_icon("clear", "LineEdit", theme->get_icon("GuiClose", "EditorIcons"));
	theme->set_color("read_only", "LineEdit", font_color_disabled);
	theme->set_color("font_color", "LineEdit", font_color);
	theme->set_color("font_color_selected", "LineEdit", mono_color);
	theme->set_color("font_color_uneditable", "LineEdit", font_color_readonly);
	theme->set_color("cursor_color", "LineEdit", font_color);
	theme->set_color("selection_color", "LineEdit", font_color_selection);
	theme->set_color("clear_button_color", "LineEdit", font_color);
	theme->set_color("clear_button_color_pressed", "LineEdit", accent_color);

	// TextEdit
	theme->set_stylebox("normal", "TextEdit", style_widget);
	theme->set_stylebox("focus", "TextEdit", style_widget_hover);
	theme->set_stylebox("read_only", "TextEdit", style_widget_disabled);
	theme->set_constant("side_margin", "TabContainer", 0);
	theme->set_icon("tab", "TextEdit", theme->get_icon("GuiTab", "EditorIcons"));
	theme->set_icon("space", "TextEdit", theme->get_icon("GuiSpace", "EditorIcons"));
	theme->set_icon("folded", "TextEdit", theme->get_icon("GuiTreeArrowRight", "EditorIcons"));
	theme->set_icon("fold", "TextEdit", theme->get_icon("GuiTreeArrowDown", "EditorIcons"));
	theme->set_color("font_color", "TextEdit", font_color);
	theme->set_color("font_color_readonly", "TextEdit", font_color_readonly);
	theme->set_color("caret_color", "TextEdit", font_color);
	theme->set_color("selection_color", "TextEdit", font_color_selection);

	// H/VSplitContainer
	theme->set_stylebox("bg", "VSplitContainer", make_stylebox(theme->get_icon("GuiVsplitBg", "EditorIcons"), 1, 1, 1, 1));
	theme->set_stylebox("bg", "HSplitContainer", make_stylebox(theme->get_icon("GuiHsplitBg", "EditorIcons"), 1, 1, 1, 1));

	theme->set_icon("grabber", "VSplitContainer", theme->get_icon("GuiVsplitter", "EditorIcons"));
	theme->set_icon("grabber", "HSplitContainer", theme->get_icon("GuiHsplitter", "EditorIcons"));

	theme->set_constant("separation", "HSplitContainer", default_margin_size * 2 * EDSCALE);
	theme->set_constant("separation", "VSplitContainer", default_margin_size * 2 * EDSCALE);

	// Containers
	theme->set_constant("separation", "BoxContainer", default_margin_size * EDSCALE);
	theme->set_constant("separation", "HBoxContainer", default_margin_size * EDSCALE);
	theme->set_constant("separation", "VBoxContainer", default_margin_size * EDSCALE);
	theme->set_constant("margin_left", "MarginContainer", 0);
	theme->set_constant("margin_top", "MarginContainer", 0);
	theme->set_constant("margin_right", "MarginContainer", 0);
	theme->set_constant("margin_bottom", "MarginContainer", 0);
	theme->set_constant("hseparation", "GridContainer", default_margin_size * EDSCALE);
	theme->set_constant("vseparation", "GridContainer", default_margin_size * EDSCALE);
	theme->set_constant("hseparation", "FlowContainer", default_margin_size * EDSCALE);
	theme->set_constant("vseparation", "FlowContainer", default_margin_size * EDSCALE);
	theme->set_constant("hseparation", "HFlowContainer", default_margin_size * EDSCALE);
	theme->set_constant("vseparation", "HFlowContainer", default_margin_size * EDSCALE);
	theme->set_constant("hseparation", "VFlowContainer", default_margin_size * EDSCALE);
	theme->set_constant("vseparation", "VFlowContainer", default_margin_size * EDSCALE);

	// WindowDialog
	Ref<StyleBoxFlat> style_window = style_popup->duplicate();
	style_window->set_border_color(tab_color);
	style_window->set_border_width(MARGIN_TOP, 24 * EDSCALE);
	style_window->set_expand_margin_size(MARGIN_TOP, 24 * EDSCALE);
	theme->set_stylebox("panel", "WindowDialog", style_window);
	theme->set_color("title_color", "WindowDialog", font_color);
	theme->set_icon("close", "WindowDialog", theme->get_icon("GuiClose", "EditorIcons"));
	theme->set_icon("close_highlight", "WindowDialog", theme->get_icon("GuiClose", "EditorIcons"));
	theme->set_constant("close_h_ofs", "WindowDialog", 22 * EDSCALE);
	theme->set_constant("close_v_ofs", "WindowDialog", 20 * EDSCALE);
	theme->set_constant("title_height", "WindowDialog", 24 * EDSCALE);
	theme->set_font("title_font", "WindowDialog", theme->get_font("title", "EditorFonts"));

	// complex window, for now only Editor settings and Project settings
	Ref<StyleBoxFlat> style_complex_window = style_window->duplicate();
	style_complex_window->set_bg_color(dark_color_2);
	style_complex_window->set_border_color(highlight_tabs ? tab_color : dark_color_2);
	theme->set_stylebox("panel", "EditorSettingsDialog", style_complex_window);
	theme->set_stylebox("panel", "ProjectSettingsEditor", style_complex_window);
	theme->set_stylebox("panel", "EditorAbout", style_complex_window);

	// HScrollBar
	Ref<Texture> empty_icon = memnew(ImageTexture);

	theme->set_stylebox("scroll", "HScrollBar", make_stylebox(theme->get_icon("GuiScrollBg", "EditorIcons"), 5, 5, 5, 5, 0, 0, 0, 0));
	theme->set_stylebox("scroll_focus", "HScrollBar", make_stylebox(theme->get_icon("GuiScrollBg", "EditorIcons"), 5, 5, 5, 5, 0, 0, 0, 0));
	theme->set_stylebox("grabber", "HScrollBar", make_stylebox(theme->get_icon("GuiScrollGrabber", "EditorIcons"), 6, 6, 6, 6, 2, 2, 2, 2));
	theme->set_stylebox("grabber_highlight", "HScrollBar", make_stylebox(theme->get_icon("GuiScrollGrabberHl", "EditorIcons"), 5, 5, 5, 5, 2, 2, 2, 2));
	theme->set_stylebox("grabber_pressed", "HScrollBar", make_stylebox(theme->get_icon("GuiScrollGrabberPressed", "EditorIcons"), 6, 6, 6, 6, 2, 2, 2, 2));

	theme->set_icon("increment", "HScrollBar", empty_icon);
	theme->set_icon("increment_highlight", "HScrollBar", empty_icon);
	theme->set_icon("increment_pressed", "HScrollBar", empty_icon);
	theme->set_icon("decrement", "HScrollBar", empty_icon);
	theme->set_icon("decrement_highlight", "HScrollBar", empty_icon);
	theme->set_icon("decrement_pressed", "HScrollBar", empty_icon);

	// VScrollBar
	theme->set_stylebox("scroll", "VScrollBar", make_stylebox(theme->get_icon("GuiScrollBg", "EditorIcons"), 5, 5, 5, 5, 0, 0, 0, 0));
	theme->set_stylebox("scroll_focus", "VScrollBar", make_stylebox(theme->get_icon("GuiScrollBg", "EditorIcons"), 5, 5, 5, 5, 0, 0, 0, 0));
	theme->set_stylebox("grabber", "VScrollBar", make_stylebox(theme->get_icon("GuiScrollGrabber", "EditorIcons"), 6, 6, 6, 6, 2, 2, 2, 2));
	theme->set_stylebox("grabber_highlight", "VScrollBar", make_stylebox(theme->get_icon("GuiScrollGrabberHl", "EditorIcons"), 5, 5, 5, 5, 2, 2, 2, 2));
	theme->set_stylebox("grabber_pressed", "VScrollBar", make_stylebox(theme->get_icon("GuiScrollGrabberPressed", "EditorIcons"), 6, 6, 6, 6, 2, 2, 2, 2));

	theme->set_icon("increment", "VScrollBar", empty_icon);
	theme->set_icon("increment_highlight", "VScrollBar", empty_icon);
	theme->set_icon("increment_pressed", "VScrollBar", empty_icon);
	theme->set_icon("decrement", "VScrollBar", empty_icon);
	theme->set_icon("decrement_highlight", "VScrollBar", empty_icon);
	theme->set_icon("decrement_pressed", "VScrollBar", empty_icon);

	// HSlider
	theme->set_icon("grabber_highlight", "HSlider", theme->get_icon("GuiSliderGrabberHl", "EditorIcons"));
	theme->set_icon("grabber", "HSlider", theme->get_icon("GuiSliderGrabber", "EditorIcons"));
	theme->set_stylebox("slider", "HSlider", make_flat_stylebox(dark_color_3, 0, default_margin_size / 2, 0, default_margin_size / 2));
	theme->set_stylebox("grabber_area", "HSlider", make_flat_stylebox(contrast_color_1, 0, default_margin_size / 2, 0, default_margin_size / 2));
	theme->set_stylebox("grabber_area_highlight", "HSlider", make_flat_stylebox(contrast_color_1, 0, default_margin_size / 2, 0, default_margin_size / 2));

	// VSlider
	theme->set_icon("grabber", "VSlider", theme->get_icon("GuiSliderGrabber", "EditorIcons"));
	theme->set_icon("grabber_highlight", "VSlider", theme->get_icon("GuiSliderGrabberHl", "EditorIcons"));
	theme->set_stylebox("slider", "VSlider", make_flat_stylebox(dark_color_3, default_margin_size / 2, 0, default_margin_size / 2, 0));
	theme->set_stylebox("grabber_area", "VSlider", make_flat_stylebox(contrast_color_1, default_margin_size / 2, 0, default_margin_size / 2, 0));
	theme->set_stylebox("grabber_area_highlight", "VSlider", make_flat_stylebox(contrast_color_1, default_margin_size / 2, 0, default_margin_size / 2, 0));

	//RichTextLabel
	theme->set_color("default_color", "RichTextLabel", font_color);
	theme->set_color("font_color_shadow", "RichTextLabel", Color(0, 0, 0, 0));
	theme->set_constant("shadow_offset_x", "RichTextLabel", 1 * EDSCALE);
	theme->set_constant("shadow_offset_y", "RichTextLabel", 1 * EDSCALE);
	theme->set_constant("shadow_as_outline", "RichTextLabel", 0 * EDSCALE);
	theme->set_stylebox("focus", "RichTextLabel", make_empty_stylebox());
	theme->set_stylebox("normal", "RichTextLabel", style_tree_bg);

	theme->set_color("headline_color", "EditorHelp", mono_color);

	// Panel
	theme->set_stylebox("panel", "Panel", make_flat_stylebox(dark_color_1, 6, 4, 6, 4));

	// Label
	theme->set_stylebox("normal", "Label", style_empty);
	theme->set_color("font_color", "Label", font_color);
	theme->set_color("font_color_shadow", "Label", Color(0, 0, 0, 0));
	theme->set_constant("shadow_offset_x", "Label", 1 * EDSCALE);
	theme->set_constant("shadow_offset_y", "Label", 1 * EDSCALE);
	theme->set_constant("shadow_as_outline", "Label", 0 * EDSCALE);
	theme->set_constant("line_spacing", "Label", 3 * EDSCALE);

	// LinkButton
	theme->set_stylebox("focus", "LinkButton", style_empty);
	theme->set_color("font_color", "LinkButton", font_color);
	theme->set_color("font_color_hover", "LinkButton", font_color_hl);
	theme->set_color("font_color_focus", "LinkButton", font_color_focus);
	theme->set_color("font_color_pressed", "LinkButton", accent_color);
	theme->set_color("font_color_disabled", "LinkButton", font_color_disabled);

	// TooltipPanel
	Ref<StyleBoxFlat> style_tooltip = style_popup->duplicate();
	float v = MAX(border_size * EDSCALE, 1.0);
	style_tooltip->set_default_margin(MARGIN_LEFT, v);
	style_tooltip->set_default_margin(MARGIN_TOP, v);
	style_tooltip->set_default_margin(MARGIN_RIGHT, v);
	style_tooltip->set_default_margin(MARGIN_BOTTOM, v);
	style_tooltip->set_bg_color(Color(mono_color.r, mono_color.g, mono_color.b, 0.9));
	style_tooltip->set_border_width_all(border_width);
	style_tooltip->set_border_color(mono_color);
	theme->set_color("font_color", "TooltipLabel", font_color.inverted());
	theme->set_color("font_color_shadow", "TooltipLabel", mono_color.inverted() * Color(1, 1, 1, 0.1));
	theme->set_stylebox("panel", "TooltipPanel", style_tooltip);

	// PopupPanel
	theme->set_stylebox("panel", "PopupPanel", style_popup);

	// SpinBox
	theme->set_icon("updown", "SpinBox", theme->get_icon("GuiSpinboxUpdown", "EditorIcons"));

	// ProgressBar
	theme->set_stylebox("bg", "ProgressBar", make_stylebox(theme->get_icon("GuiProgressBar", "EditorIcons"), 4, 4, 4, 4, 0, 0, 0, 0));
	theme->set_stylebox("fg", "ProgressBar", make_stylebox(theme->get_icon("GuiProgressFill", "EditorIcons"), 6, 6, 6, 6, 2, 1, 2, 1));
	theme->set_color("font_color", "ProgressBar", font_color);

	// GraphEdit
	theme->set_stylebox("bg", "GraphEdit", style_tree_bg);
	if (dark_theme) {
		theme->set_color("grid_major", "GraphEdit", Color(1.0, 1.0, 1.0, 0.15));
		theme->set_color("grid_minor", "GraphEdit", Color(1.0, 1.0, 1.0, 0.07));
	} else {
		theme->set_color("grid_major", "GraphEdit", Color(0.0, 0.0, 0.0, 0.15));
		theme->set_color("grid_minor", "GraphEdit", Color(0.0, 0.0, 0.0, 0.07));
	}
	theme->set_color("selection_fill", "GraphEdit", theme->get_color("box_selection_fill_color", "Editor"));
	theme->set_color("selection_stroke", "GraphEdit", theme->get_color("box_selection_stroke_color", "Editor"));
	theme->set_color("activity", "GraphEdit", accent_color);
	theme->set_icon("minus", "GraphEdit", theme->get_icon("ZoomLess", "EditorIcons"));
	theme->set_icon("more", "GraphEdit", theme->get_icon("ZoomMore", "EditorIcons"));
	theme->set_icon("reset", "GraphEdit", theme->get_icon("ZoomReset", "EditorIcons"));
	theme->set_icon("snap", "GraphEdit", theme->get_icon("SnapGrid", "EditorIcons"));
	theme->set_icon("minimap", "GraphEdit", theme->get_icon("GridMinimap", "EditorIcons"));
	theme->set_constant("bezier_len_pos", "GraphEdit", 80 * EDSCALE);
	theme->set_constant("bezier_len_neg", "GraphEdit", 160 * EDSCALE);

	// GraphEditMinimap
	Ref<StyleBoxFlat> style_minimap_bg = make_flat_stylebox(dark_color_1, 0, 0, 0, 0);
	style_minimap_bg->set_border_color(dark_color_3);
	style_minimap_bg->set_border_width_all(1);
	theme->set_stylebox("bg", "GraphEditMinimap", style_minimap_bg);

	Ref<StyleBoxFlat> style_minimap_camera;
	Ref<StyleBoxFlat> style_minimap_node;
	if (dark_theme) {
		style_minimap_camera = make_flat_stylebox(Color(0.65, 0.65, 0.65, 0.2), 0, 0, 0, 0);
		style_minimap_camera->set_border_color(Color(0.65, 0.65, 0.65, 0.45));
		style_minimap_node = make_flat_stylebox(Color(1, 1, 1), 0, 0, 0, 0);
	} else {
		style_minimap_camera = make_flat_stylebox(Color(0.38, 0.38, 0.38, 0.2), 0, 0, 0, 0);
		style_minimap_camera->set_border_color(Color(0.38, 0.38, 0.38, 0.45));
		style_minimap_node = make_flat_stylebox(Color(0, 0, 0), 0, 0, 0, 0);
	}
	style_minimap_camera->set_border_width_all(1);
	style_minimap_node->set_corner_radius_all(1);
	theme->set_stylebox("camera", "GraphEditMinimap", style_minimap_camera);
	theme->set_stylebox("node", "GraphEditMinimap", style_minimap_node);

	Ref<Texture> minimap_resizer_icon = theme->get_icon("GuiResizer", "EditorIcons");
	Color minimap_resizer_color;
	if (dark_theme) {
		minimap_resizer_color = Color(1, 1, 1, 0.65);
	} else {
		minimap_resizer_color = Color(0, 0, 0, 0.65);
	}
	theme->set_icon("resizer", "GraphEditMinimap", flip_icon(minimap_resizer_icon, true, true));
	theme->set_color("resizer_color", "GraphEditMinimap", minimap_resizer_color);

	// GraphNode
	const float mv = dark_theme ? 0.0 : 1.0;
	const float mv2 = 1.0 - mv;
	const int gn_margin_side = 28;
	Ref<StyleBoxFlat> graphsb = make_flat_stylebox(Color(mv, mv, mv, 0.7), gn_margin_side, 24, gn_margin_side, 5);
	graphsb->set_border_width_all(border_width);
	graphsb->set_border_color(Color(mv2, mv2, mv2, 0.9));
	Ref<StyleBoxFlat> graphsbselected = make_flat_stylebox(Color(mv, mv, mv, 0.9), gn_margin_side, 24, gn_margin_side, 5);
	graphsbselected->set_border_width_all(border_width);
	graphsbselected->set_border_color(Color(accent_color.r, accent_color.g, accent_color.b, 0.9));
	graphsbselected->set_shadow_size(8 * EDSCALE);
	graphsbselected->set_shadow_color(shadow_color);
	Ref<StyleBoxFlat> graphsbcomment = make_flat_stylebox(Color(mv, mv, mv, 0.3), gn_margin_side, 24, gn_margin_side, 5);
	graphsbcomment->set_border_width_all(border_width);
	graphsbcomment->set_border_color(Color(mv2, mv2, mv2, 0.9));
	Ref<StyleBoxFlat> graphsbcommentselected = make_flat_stylebox(Color(mv, mv, mv, 0.4), gn_margin_side, 24, gn_margin_side, 5);
	graphsbcommentselected->set_border_width_all(border_width);
	graphsbcommentselected->set_border_color(Color(mv2, mv2, mv2, 0.9));
	Ref<StyleBoxFlat> graphsbbreakpoint = graphsbselected->duplicate();
	graphsbbreakpoint->set_draw_center(false);
	graphsbbreakpoint->set_border_color(warning_color);
	graphsbbreakpoint->set_shadow_color(warning_color * Color(1.0, 1.0, 1.0, 0.1));
	Ref<StyleBoxFlat> graphsbposition = graphsbselected->duplicate();
	graphsbposition->set_draw_center(false);
	graphsbposition->set_border_color(error_color);
	graphsbposition->set_shadow_color(error_color * Color(1.0, 1.0, 1.0, 0.2));
	Ref<StyleBoxFlat> smgraphsb = make_flat_stylebox(Color(mv, mv, mv, 0.7), gn_margin_side, 24, gn_margin_side, 5);
	smgraphsb->set_border_width_all(border_width);
	smgraphsb->set_border_color(Color(mv2, mv2, mv2, 0.9));
	Ref<StyleBoxFlat> smgraphsbselected = make_flat_stylebox(Color(mv, mv, mv, 0.9), gn_margin_side, 24, gn_margin_side, 5);
	smgraphsbselected->set_border_width_all(border_width);
	smgraphsbselected->set_border_color(Color(accent_color.r, accent_color.g, accent_color.b, 0.9));
	smgraphsbselected->set_shadow_size(8 * EDSCALE);
	smgraphsbselected->set_shadow_color(shadow_color);

	if (use_gn_headers) {
		graphsb->set_border_width(MARGIN_TOP, 24 * EDSCALE);
		graphsbselected->set_border_width(MARGIN_TOP, 24 * EDSCALE);
		graphsbcomment->set_border_width(MARGIN_TOP, 24 * EDSCALE);
		graphsbcommentselected->set_border_width(MARGIN_TOP, 24 * EDSCALE);
	}

	theme->set_stylebox("frame", "GraphNode", graphsb);
	theme->set_stylebox("selectedframe", "GraphNode", graphsbselected);
	theme->set_stylebox("comment", "GraphNode", graphsbcomment);
	theme->set_stylebox("commentfocus", "GraphNode", graphsbcommentselected);
	theme->set_stylebox("breakpoint", "GraphNode", graphsbbreakpoint);
	theme->set_stylebox("position", "GraphNode", graphsbposition);
	theme->set_stylebox("state_machine_frame", "GraphNode", smgraphsb);
	theme->set_stylebox("state_machine_selectedframe", "GraphNode", smgraphsbselected);

	Color default_node_color = Color(mv2, mv2, mv2);
	theme->set_color("title_color", "GraphNode", default_node_color);
	default_node_color.a = 0.7;
	theme->set_color("close_color", "GraphNode", default_node_color);
	theme->set_color("resizer_color", "GraphNode", default_node_color);

	theme->set_constant("port_offset", "GraphNode", 14 * EDSCALE);
	theme->set_constant("title_h_offset", "GraphNode", -16 * EDSCALE);
	theme->set_constant("title_offset", "GraphNode", 20 * EDSCALE);
	theme->set_constant("close_h_offset", "GraphNode", 20 * EDSCALE);
	theme->set_constant("close_offset", "GraphNode", 20 * EDSCALE);
	theme->set_constant("separation", "GraphNode", 1 * EDSCALE);

	theme->set_icon("close", "GraphNode", theme->get_icon("GuiCloseCustomizable", "EditorIcons"));
	theme->set_icon("resizer", "GraphNode", theme->get_icon("GuiResizer", "EditorIcons"));
	theme->set_icon("port", "GraphNode", theme->get_icon("GuiGraphNodePort", "EditorIcons"));

	// GridContainer
	theme->set_constant("vseparation", "GridContainer", (extra_spacing + default_margin_size) * EDSCALE);

	// FileDialog
	theme->set_icon("folder", "FileDialog", theme->get_icon("Folder", "EditorIcons"));
	theme->set_icon("parent_folder", "FileDialog", theme->get_icon("ArrowUp", "EditorIcons"));
	theme->set_icon("reload", "FileDialog", theme->get_icon("Reload", "EditorIcons"));
	theme->set_icon("toggle_hidden", "FileDialog", theme->get_icon("GuiVisibilityVisible", "EditorIcons"));
	// Use a different color for folder icons to make them easier to distinguish from files.
	// On a light theme, the icon will be dark, so we need to lighten it before blending it with the accent color.
	theme->set_color("folder_icon_modulate", "FileDialog", (dark_theme ? Color(1, 1, 1) : Color(4.25, 4.25, 4.25)).linear_interpolate(accent_color, 0.7));
	theme->set_color("files_disabled", "FileDialog", font_color_disabled);

	// color picker
	theme->set_constant("margin", "ColorPicker", popup_margin_size);
	theme->set_constant("sv_width", "ColorPicker", 256 * EDSCALE);
	theme->set_constant("sv_height", "ColorPicker", 256 * EDSCALE);
	theme->set_constant("h_width", "ColorPicker", 30 * EDSCALE);
	theme->set_constant("label_width", "ColorPicker", 10 * EDSCALE);
	theme->set_icon("screen_picker", "ColorPicker", theme->get_icon("ColorPick", "EditorIcons"));
	theme->set_icon("add_preset", "ColorPicker", theme->get_icon("Add", "EditorIcons"));
	theme->set_icon("preset_bg", "ColorPicker", theme->get_icon("GuiMiniCheckerboard", "EditorIcons"));
	theme->set_icon("overbright_indicator", "ColorPicker", theme->get_icon("OverbrightIndicator", "EditorIcons"));

	theme->set_icon("bg", "ColorPickerButton", theme->get_icon("GuiMiniCheckerboard", "EditorIcons"));

	// ColorPresetButton
	Ref<StyleBoxFlat> preset_sb = make_flat_stylebox(Color(1, 1, 1), 2, 2, 2, 2);

	preset_sb->set_anti_aliased(false);
	theme->set_stylebox("preset_fg", "ColorPresetButton", preset_sb);
	theme->set_icon("preset_bg_icon", "ColorPresetButton", theme->get_icon("GuiMiniCheckerboard", "EditorIcons"));
	theme->set_icon("overbright_indicator", "ColorPresetButton", theme->get_icon("OverbrightIndicator", "EditorIcons"));

	// Information on 3D viewport
	Ref<StyleBoxFlat> style_info_3d_viewport = style_default->duplicate();
	style_info_3d_viewport->set_bg_color(style_info_3d_viewport->get_bg_color() * Color(1, 1, 1, 0.5));
	style_info_3d_viewport->set_border_width_all(0);
	theme->set_stylebox("Information3dViewport", "EditorStyles", style_info_3d_viewport);

	// Theme editor.
	theme->set_color("preview_picker_overlay_color", "ThemeEditor", Color(0.1, 0.1, 0.1, 0.25));
	Color theme_preview_picker_bg_color = accent_color;
	theme_preview_picker_bg_color.a = 0.2;
	Ref<StyleBoxFlat> theme_preview_picker_sb = make_flat_stylebox(theme_preview_picker_bg_color, 0, 0, 0, 0);
	theme_preview_picker_sb->set_border_color(accent_color);
	theme_preview_picker_sb->set_border_width_all(1.0 * EDSCALE);
	theme->set_stylebox("preview_picker_overlay", "ThemeEditor", theme_preview_picker_sb);
	Color theme_preview_picker_label_bg_color = accent_color;
	theme_preview_picker_label_bg_color.set_hsv(theme_preview_picker_label_bg_color.get_h(), theme_preview_picker_label_bg_color.get_s(), 0.5);
	Ref<StyleBoxFlat> theme_preview_picker_label_sb = make_flat_stylebox(theme_preview_picker_label_bg_color, 4.0, 1.0, 4.0, 3.0);
	theme->set_stylebox("preview_picker_label", "ThemeEditor", theme_preview_picker_label_sb);

	// Windows 7 / classic desktop-app skin, in light and dark variants.
	// StyleBoxFlat cannot draw gradients, so buttons use a baked-texture Aero fill;
	// everything else is solid colors with thin borders and square corners.
	if (preset == "Windows 7" || preset == "Windows 7 Dark") {
		const bool win_dark = (preset == "Windows 7 Dark");
		const int win_bw = MAX(1, EDSCALE);

		const Color win_window_bg = win_dark ? Color(0.13, 0.135, 0.15) : Color(0.75, 0.83, 0.92);
		const Color win_field_bg = win_dark ? Color(0.165, 0.17, 0.185) : Color(1, 1, 1);
		const Color win_control_hover = win_dark ? Color(0.22, 0.28, 0.36) : Color(0.86, 0.94, 0.99);
		const Color win_control_disabled = win_dark ? Color(0.15, 0.155, 0.17) : Color(0.94, 0.94, 0.94);
		const Color win_border = win_dark ? Color(0.34, 0.35, 0.38) : Color(0.50, 0.62, 0.73);
		const Color win_border_accent = win_dark ? Color(0.33, 0.56, 0.82) : Color(0.24, 0.55, 0.80);
		const Color win_border_disabled = win_dark ? Color(0.24, 0.245, 0.26) : Color(0.72, 0.73, 0.75);
		const Color win_selection = win_dark ? Color(0.20, 0.36, 0.55) : Color(0.80, 0.91, 0.98);
		const Color win_selection_border = win_dark ? Color(0.33, 0.52, 0.75) : Color(0.49, 0.64, 0.81);
		const Color win_text = win_dark ? Color(0.88, 0.89, 0.91) : Color(0, 0, 0);
		const Color win_popup_bg = win_dark ? Color(0.17, 0.175, 0.19) : Color(1, 1, 1);
		const Color win_popup_border = win_dark ? Color(0.36, 0.37, 0.40) : Color(0.55, 0.58, 0.63);
		const Color win_popup_hover = win_dark ? Color(0.24, 0.34, 0.48) : Color(0.78, 0.88, 0.98);

		// Unscaled margins (make_*_stylebox multiplies by EDSCALE internally).
		const float wm_h = extra_spacing + 6;
		const float wm_v = extra_spacing + default_margin_size;

		// Editor backdrop (area around docks and viewports).
		theme->set_stylebox("Background", "EditorStyles", make_flat_stylebox(win_window_bg, default_margin_size, default_margin_size, default_margin_size, default_margin_size));

		// Buttons and dropdowns: glossy vertical-gradient Aero fill (baked texture).
		Ref<StyleBoxTexture> wb_normal, wb_hover, wb_pressed, wb_disabled, wb_focus;
		if (win_dark) {
			wb_normal = make_aero_stylebox(Color(0.32, 0.33, 0.36), Color(0.28, 0.29, 0.32), Color(0.22, 0.23, 0.26), Color(0.18, 0.19, 0.22), Color(0.10, 0.10, 0.11), 0.14, wm_h, wm_v);
			wb_hover = make_aero_stylebox(Color(0.30, 0.37, 0.46), Color(0.27, 0.34, 0.44), Color(0.21, 0.30, 0.42), Color(0.17, 0.26, 0.38), Color(0.33, 0.56, 0.82), 0.16, wm_h, wm_v);
			wb_pressed = make_aero_stylebox(Color(0.14, 0.17, 0.21), Color(0.16, 0.20, 0.25), Color(0.19, 0.24, 0.30), Color(0.22, 0.28, 0.35), Color(0.25, 0.42, 0.62), 0.05, wm_h, wm_v);
			wb_disabled = make_aero_stylebox(Color(0.20, 0.205, 0.22), Color(0.19, 0.195, 0.21), Color(0.18, 0.185, 0.20), Color(0.17, 0.175, 0.19), Color(0.24, 0.245, 0.26), 0.04, wm_h, wm_v);
			wb_focus = make_aero_focus_stylebox(Color(0.30, 0.60, 0.95), wm_h, wm_v);
		} else {
			// Normal is the neutral grey Win7 button; hover/pressed pick up the Aero
			// blue; focus gets the gold "default button" border from the screenshot.
			wb_normal = make_aero_stylebox(Color(0.953, 0.953, 0.953), Color(0.925, 0.925, 0.925), Color(0.859, 0.859, 0.859), Color(0.804, 0.804, 0.804), Color(0.44, 0.44, 0.44), 0.5, wm_h, wm_v);
			wb_hover = make_aero_stylebox(Color(0.918, 0.965, 0.992), Color(0.851, 0.941, 0.988), Color(0.745, 0.902, 0.992), Color(0.655, 0.851, 0.961), Color(0.235, 0.498, 0.694), 0.5, wm_h, wm_v);
			wb_pressed = make_aero_stylebox(Color(0.612, 0.792, 0.902), Color(0.678, 0.843, 0.933), Color(0.792, 0.906, 0.965), Color(0.882, 0.949, 0.984), Color(0.173, 0.384, 0.545), 0.35, wm_h, wm_v);
			wb_disabled = make_aero_stylebox(Color(0.949, 0.949, 0.949), Color(0.937, 0.937, 0.937), Color(0.925, 0.925, 0.925), Color(0.914, 0.914, 0.914), Color(0.70, 0.70, 0.70), 0.3, wm_h, wm_v);
			wb_focus = make_aero_focus_stylebox(Color(0.87, 0.62, 0.16), wm_h, wm_v);
		}

		static const char *wb_classes[] = { "Button", "OptionButton" };
		for (int i = 0; i < 2; i++) {
			theme->set_stylebox("normal", wb_classes[i], wb_normal);
			theme->set_stylebox("hover", wb_classes[i], wb_hover);
			theme->set_stylebox("pressed", wb_classes[i], wb_pressed);
			theme->set_stylebox("focus", wb_classes[i], wb_focus);
			theme->set_stylebox("disabled", wb_classes[i], wb_disabled);
			theme->set_color("font_color_pressed", wb_classes[i], win_text);
		}

		// Text fields (LineEdit / TextEdit / SpinBox).
		Ref<StyleBoxFlat> wf_normal = make_win_stylebox(win_field_bg, win_border, win_bw, wm_h, wm_v, wm_h, wm_v);
		Ref<StyleBoxFlat> wf_focus = make_win_stylebox(win_field_bg, win_border_accent, win_bw, wm_h, wm_v, wm_h, wm_v);
		Ref<StyleBoxFlat> wf_readonly = make_win_stylebox(win_control_disabled, win_border_disabled, win_bw, wm_h, wm_v, wm_h, wm_v);
		theme->set_stylebox("normal", "LineEdit", wf_normal);
		theme->set_stylebox("focus", "LineEdit", wf_focus);
		theme->set_stylebox("read_only", "LineEdit", wf_readonly);
		theme->set_stylebox("normal", "TextEdit", wf_normal);
		theme->set_stylebox("focus", "TextEdit", wf_focus);
		theme->set_stylebox("read_only", "TextEdit", wf_readonly);

		// Trees & item lists: TreeView / ListBox surfaces with a blue selection.
		Ref<StyleBoxFlat> wl_bg = make_win_stylebox(win_field_bg, win_border, win_bw, default_margin_size, default_margin_size, default_margin_size, default_margin_size);

		Ref<StyleBoxFlat> wl_selected = make_flat_stylebox(win_selection, 4, 4, 4, 4);
		wl_selected->set_border_width_all(win_bw);
		wl_selected->set_border_color(win_selection_border);
		wl_selected->set_corner_radius_all(0);
		wl_selected->set_anti_aliased(false);

		Ref<StyleBoxFlat> wl_hover = make_flat_stylebox(win_control_hover, 4, 4, 4, 4);
		wl_hover->set_border_width_all(win_bw);
		wl_hover->set_border_color(win_selection_border);
		wl_hover->set_corner_radius_all(0);
		wl_hover->set_anti_aliased(false);

		theme->set_stylebox("bg", "Tree", wl_bg);
		theme->set_stylebox("selected", "Tree", wl_selected);
		theme->set_stylebox("selected_focus", "Tree", wl_selected);
		theme->set_stylebox("hover", "Tree", wl_hover);
		theme->set_stylebox("bg", "ItemList", wl_bg);
		theme->set_stylebox("selected", "ItemList", wl_selected);
		theme->set_stylebox("selected_focus", "ItemList", wl_selected);
		theme->set_color("font_color_selected", "Tree", win_dark ? Color(1, 1, 1) : Color(0, 0, 0));
		theme->set_color("font_color_selected", "ItemList", win_dark ? Color(1, 1, 1) : Color(0, 0, 0));
		theme->set_stylebox("normal", "RichTextLabel", wl_bg);

		// Tab content area matches the window backdrop.
		theme->set_stylebox("panel", "TabContainer", make_win_stylebox(win_window_bg, win_border, win_bw, margin_size_extra, margin_size_extra, margin_size_extra, margin_size_extra));

		// ProgressBar: dark/light trough with the classic Windows green fill.
		theme->set_stylebox("bg", "ProgressBar", make_win_stylebox(win_dark ? Color(0.13, 0.135, 0.15) : Color(0.96, 0.96, 0.96), win_border, win_bw, 1, 1, 1, 1));
		Ref<StyleBoxFlat> wpb_fg = make_flat_stylebox(win_dark ? Color(0.10, 0.62, 0.18) : Color(0.024, 0.690, 0.145), 1, 1, 1, 1);
		wpb_fg->set_corner_radius_all(0);
		wpb_fg->set_anti_aliased(false);
		theme->set_stylebox("fg", "ProgressBar", wpb_fg);
		theme->set_color("font_color", "ProgressBar", win_text);

		// Scrollbars: flat Windows-style track and grabber. StyleBoxFlat reports no
		// center size, so the bar's thickness has to come from the track's cross-axis
		// content margins -- otherwise ScrollBar::get_minimum_size() is 0 and the bar
		// collapses to nothing. Hence separate H/V tracks.
		const Color wsc_track_col = win_dark ? Color(0.11, 0.115, 0.13) : Color(0.94, 0.95, 0.97);
		const Color wsc_track_border = win_dark ? Color(0.24, 0.25, 0.27) : Color(0.80, 0.82, 0.86);
		const Color wsc_grab_border = win_dark ? Color(0.20, 0.21, 0.23) : Color(0.60, 0.63, 0.68);
		const int wsc_half = 7; // both content margins sum to a ~14px bar

		Ref<StyleBoxFlat> wsc_track_h = make_flat_stylebox(wsc_track_col, 2, wsc_half, 2, wsc_half);
		Ref<StyleBoxFlat> wsc_track_v = make_flat_stylebox(wsc_track_col, wsc_half, 2, wsc_half, 2);
		wsc_track_h->set_border_width_all(win_bw);
		wsc_track_h->set_border_color(wsc_track_border);
		wsc_track_h->set_corner_radius_all(0);
		wsc_track_h->set_anti_aliased(false);
		wsc_track_v->set_border_width_all(win_bw);
		wsc_track_v->set_border_color(wsc_track_border);
		wsc_track_v->set_corner_radius_all(0);
		wsc_track_v->set_anti_aliased(false);

		Ref<StyleBoxFlat> wsc_grab = make_flat_stylebox(win_dark ? Color(0.28, 0.29, 0.32) : Color(0.80, 0.83, 0.88), 2, 2, 2, 2);
		wsc_grab->set_border_width_all(win_bw);
		wsc_grab->set_border_color(wsc_grab_border);
		wsc_grab->set_corner_radius_all(0);
		wsc_grab->set_anti_aliased(false);
		Ref<StyleBoxFlat> wsc_grab_hl = wsc_grab->duplicate();
		wsc_grab_hl->set_bg_color(win_dark ? Color(0.35, 0.45, 0.58) : Color(0.66, 0.80, 0.94));
		wsc_grab_hl->set_border_color(win_border_accent);

		theme->set_stylebox("scroll", "HScrollBar", wsc_track_h);
		theme->set_stylebox("scroll_focus", "HScrollBar", wsc_track_h);
		theme->set_stylebox("scroll", "VScrollBar", wsc_track_v);
		theme->set_stylebox("scroll_focus", "VScrollBar", wsc_track_v);
		static const char *wsc_classes[] = { "HScrollBar", "VScrollBar" };
		for (int i = 0; i < 2; i++) {
			theme->set_stylebox("grabber", wsc_classes[i], wsc_grab);
			theme->set_stylebox("grabber_highlight", wsc_classes[i], wsc_grab_hl);
			theme->set_stylebox("grabber_pressed", wsc_classes[i], wsc_grab_hl);
		}

		// Popups and menus: solid surface, thin frame, blue hover.
		Ref<StyleBoxFlat> wpop = make_flat_stylebox(win_popup_bg, default_margin_size * 2, default_margin_size * 2, default_margin_size * 2, default_margin_size * 2);
		wpop->set_border_width_all(win_bw);
		wpop->set_border_color(win_popup_border);
		wpop->set_corner_radius_all(0);
		wpop->set_anti_aliased(false);
		theme->set_stylebox("panel", "PopupMenu", wpop);
		theme->set_stylebox("panel", "PopupPanel", wpop);
		theme->set_stylebox("panel", "PopupDialog", wpop);

		Ref<StyleBoxFlat> wpop_hover = make_flat_stylebox(win_popup_hover, 2, 2, 2, 2);
		wpop_hover->set_border_width_all(win_bw);
		wpop_hover->set_border_color(win_border_accent);
		wpop_hover->set_corner_radius_all(0);
		wpop_hover->set_anti_aliased(false);
		theme->set_stylebox("hover", "PopupMenu", wpop_hover);
		theme->set_stylebox("hover", "MenuButton", wpop_hover);
		theme->set_color("font_color", "PopupMenu", win_text);
		theme->set_color("font_color_hover", "PopupMenu", win_text);
		theme->set_color("font_color_hover", "MenuButton", win_text);
	}

	// adaptive script theme constants
	// for comments and elements with lower relevance
	const Color dim_color = Color(font_color.r, font_color.g, font_color.b, 0.5);

	const float mono_value = mono_color.r;
	const Color alpha1 = Color(mono_value, mono_value, mono_value, 0.07);
	const Color alpha2 = Color(mono_value, mono_value, mono_value, 0.14);
	const Color alpha3 = Color(mono_value, mono_value, mono_value, 0.7);

	// editor main color
	const Color main_color = dark_theme ? Color(0.34, 0.7, 1.0) : Color(0.02, 0.5, 1.0);

	const Color symbol_color = Color(0.34, 0.57, 1.0).linear_interpolate(mono_color, dark_theme ? 0.5 : 0.3);
	const Color keyword_color = Color(1.0, 0.44, 0.52);
	const Color control_flow_keyword_color = dark_theme ? Color(1.0, 0.55, 0.8) : Color(0.8, 0.4, 0.6);
	const Color basetype_color = dark_theme ? Color(0.26, 1.0, 0.76) : Color(0.0, 0.76, 0.38);
	const Color type_color = basetype_color.linear_interpolate(mono_color, dark_theme ? 0.4 : 0.3);
	const Color usertype_color = basetype_color.linear_interpolate(mono_color, dark_theme ? 0.7 : 0.5);
	const Color comment_color = dim_color;
	const Color string_color = (dark_theme ? Color(1.0, 0.85, 0.26) : Color(1.0, 0.82, 0.09)).linear_interpolate(mono_color, dark_theme ? 0.5 : 0.3);

	const Color te_background_color = dark_theme ? background_color : base_color;
	const Color completion_background_color = dark_theme ? base_color : background_color;
	const Color completion_selected_color = alpha1;
	const Color completion_existing_color = alpha2;
	const Color completion_scroll_color = Color(mono_value, mono_value, mono_value, 0.29);
	const Color completion_font_color = font_color;
	const Color text_color = font_color;
	const Color line_number_color = dim_color;
	const Color safe_line_number_color = dim_color * Color(1, 1.2, 1, 1.5);
	const Color caret_color = mono_color;
	const Color caret_background_color = mono_color.inverted();
	const Color text_selected_color = dark_color_3;
	const Color selection_color = accent_color * Color(1, 1, 1, 0.35);
	const Color brace_mismatch_color = error_color;
	const Color current_line_color = alpha1;
	const Color line_length_guideline_color = dark_theme ? base_color : background_color;
	const Color word_highlighted_color = alpha1;
	const Color number_color = basetype_color.linear_interpolate(mono_color, dark_theme ? 0.5 : 0.3);
	const Color function_color = main_color;
	const Color member_variable_color = main_color.linear_interpolate(mono_color, 0.6);
	const Color mark_color = Color(error_color.r, error_color.g, error_color.b, 0.3);
	const Color bookmark_color = Color(0.08, 0.49, 0.98);
	const Color breakpoint_color = error_color;
	const Color executing_line_color = Color(0.2, 0.8, 0.2, 0.4);
	const Color code_folding_color = alpha3;
	const Color search_result_color = alpha1;
	const Color search_result_border_color = Color(0.41, 0.61, 0.91, 0.38);

	EditorSettings *setting = EditorSettings::get_singleton();
	String text_editor_color_theme = setting->get("text_editor/theme/color_theme");
	if (text_editor_color_theme == "Adaptive") {
		setting->set_initial_value("text_editor/highlighting/symbol_color", symbol_color, true);
		setting->set_initial_value("text_editor/highlighting/keyword_color", keyword_color, true);
		setting->set_initial_value("text_editor/highlighting/control_flow_keyword_color", control_flow_keyword_color, true);
		setting->set_initial_value("text_editor/highlighting/base_type_color", basetype_color, true);
		setting->set_initial_value("text_editor/highlighting/engine_type_color", type_color, true);
		setting->set_initial_value("text_editor/highlighting/user_type_color", usertype_color, true);
		setting->set_initial_value("text_editor/highlighting/comment_color", comment_color, true);
		setting->set_initial_value("text_editor/highlighting/string_color", string_color, true);
		setting->set_initial_value("text_editor/highlighting/background_color", te_background_color, true);
		setting->set_initial_value("text_editor/highlighting/completion_background_color", completion_background_color, true);
		setting->set_initial_value("text_editor/highlighting/completion_selected_color", completion_selected_color, true);
		setting->set_initial_value("text_editor/highlighting/completion_existing_color", completion_existing_color, true);
		setting->set_initial_value("text_editor/highlighting/completion_scroll_color", completion_scroll_color, true);
		setting->set_initial_value("text_editor/highlighting/completion_font_color", completion_font_color, true);
		setting->set_initial_value("text_editor/highlighting/text_color", text_color, true);
		setting->set_initial_value("text_editor/highlighting/line_number_color", line_number_color, true);
		setting->set_initial_value("text_editor/highlighting/safe_line_number_color", safe_line_number_color, true);
		setting->set_initial_value("text_editor/highlighting/caret_color", caret_color, true);
		setting->set_initial_value("text_editor/highlighting/caret_background_color", caret_background_color, true);
		setting->set_initial_value("text_editor/highlighting/text_selected_color", text_selected_color, true);
		setting->set_initial_value("text_editor/highlighting/selection_color", selection_color, true);
		setting->set_initial_value("text_editor/highlighting/brace_mismatch_color", brace_mismatch_color, true);
		setting->set_initial_value("text_editor/highlighting/current_line_color", current_line_color, true);
		setting->set_initial_value("text_editor/highlighting/line_length_guideline_color", line_length_guideline_color, true);
		setting->set_initial_value("text_editor/highlighting/word_highlighted_color", word_highlighted_color, true);
		setting->set_initial_value("text_editor/highlighting/number_color", number_color, true);
		setting->set_initial_value("text_editor/highlighting/function_color", function_color, true);
		setting->set_initial_value("text_editor/highlighting/member_variable_color", member_variable_color, true);
		setting->set_initial_value("text_editor/highlighting/mark_color", mark_color, true);
		setting->set_initial_value("text_editor/highlighting/bookmark_color", bookmark_color, true);
		setting->set_initial_value("text_editor/highlighting/breakpoint_color", breakpoint_color, true);
		setting->set_initial_value("text_editor/highlighting/executing_line_color", executing_line_color, true);
		setting->set_initial_value("text_editor/highlighting/code_folding_color", code_folding_color, true);
		setting->set_initial_value("text_editor/highlighting/search_result_color", search_result_color, true);
		setting->set_initial_value("text_editor/highlighting/search_result_border_color", search_result_border_color, true);
	} else if (text_editor_color_theme == "Default") {
		setting->load_text_editor_theme();
	}

	return theme;
}

Ref<Theme> create_custom_theme(const Ref<Theme> p_theme) {
	Ref<Theme> theme = create_editor_theme(p_theme);

	const String custom_theme_path = EditorSettings::get_singleton()->get("interface/theme/custom_theme");
	if (custom_theme_path != "") {
		Ref<Theme> custom_theme = ResourceLoader::load(custom_theme_path);
		if (custom_theme.is_valid()) {
			theme->merge_with(custom_theme);
		}
	}

	return theme;
}

Ref<ImageTexture> create_unscaled_default_project_icon() {
#ifdef MODULE_SVG_ENABLED
	for (int i = 0; i < editor_icons_count; i++) {
		// ESCALE should never affect size of the icon
		if (strcmp(editor_icons_names[i], "DefaultProjectIcon") == 0) {
			return editor_generate_icon(i, false, 1.0);
		}
	}
#endif
	return Ref<ImageTexture>(memnew(ImageTexture));
}
