#ifndef CELL_SYSTEM_EDITOR_UI_HELPERS_H
#define CELL_SYSTEM_EDITOR_UI_HELPERS_H
#ifdef TOOLS_ENABLED

// Small shared builders so the cell_system dock panels (terrain sculpt, path
// deform, foliage paint, cell world) look like one coherent tool instead of
// four independently-styled toolbars — grouped labeled sections and a
// mode button-grid, closer to Unreal's Landscape mode panel than a plain
// top-of-viewport strip of controls.

#include "core/color.h"
#include "core/math/vector2.h"
#include "core/reference.h"
#include "core/ustring.h"
#include "core/string_name.h"

class Container;
class Control;
class VBoxContainer;
class GridContainer;
class Button;
class ButtonGroup;
class EditorSpinSlider;
class Image;
class ImageTexture;
class Object;
class Texture;

// Builds a ScrollContainer > MarginContainer > VBoxContainer stack (the usual
// shape for a left-side inspector-style dock) and returns the outer, dock-
// addable Control. *r_content receives the inner VBoxContainer to add rows to.
Control *cs_make_dock_root(VBoxContainer **r_content);

// A small dimmed, uppercased label used as a section title, with a separator
// above it when it isn't the first section.
void cs_add_section_header(VBoxContainer *p_parent, const String &p_text);

// One toggle button in p_grid, mutually exclusive within p_group, wired to
// p_target's p_callback("pressed") with p_index bound as the sole argument.
Button *cs_add_mode_button(GridContainer *p_grid, const Ref<ButtonGroup> &p_group,
		const String &p_text, Object *p_target, const StringName &p_callback, int p_index);

// A labeled slider+number row (EditorSpinSlider), wired to p_target's
// p_callback via "value_changed" (receives the new float value).
EditorSpinSlider *cs_add_slider_row(VBoxContainer *p_parent, const String &p_label,
		double p_min, double p_max, double p_step, double p_default,
		Object *p_target, const StringName &p_callback);

// Flat, icon-only toggle button (no text) in p_parent, mutually exclusive
// within p_group, wired to p_target's p_callback("pressed") with p_index
// bound — for a compact icon row in the viewport's top toolbar rather than a
// text-label grid. p_tooltip carries the name since there's no visible label.
Button *cs_add_icon_mode_button(Container *p_parent, const Ref<ButtonGroup> &p_group,
		const Ref<Texture> &p_icon, const String &p_tooltip,
		Object *p_target, const StringName &p_callback, int p_index);

// --- Tiny procedural icon drawing ---
// This module has no art pipeline, so toolbar icons are rasterized in code:
// flat white silhouettes on a transparent square, matching Godot's own
// monochrome toolbar icon style. p_size is both width and height.
Ref<Image> cs_icon_new(int p_size);
void cs_icon_fill_rect(Ref<Image> p_img, float p_x, float p_y, float p_w, float p_h, const Color &p_color);
void cs_icon_fill_circle(Ref<Image> p_img, const Vector2 &p_center, float p_radius, const Color &p_color);
void cs_icon_fill_triangle(Ref<Image> p_img, const Vector2 &p_a, const Vector2 &p_b, const Vector2 &p_c, const Color &p_color);
void cs_icon_line(Ref<Image> p_img, const Vector2 &p_a, const Vector2 &p_b, float p_thickness, const Color &p_color);
// Wraps a finished Image (call icon draw calls between p_img->lock()/unlock())
// into an ImageTexture.
Ref<ImageTexture> cs_icon_to_texture(const Ref<Image> &p_img);

#endif
#endif
