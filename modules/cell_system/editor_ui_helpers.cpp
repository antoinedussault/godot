#include "editor_ui_helpers.h"
#ifdef TOOLS_ENABLED

#include "core/image.h"
#include "core/math/math_funcs.h"
#include "editor/editor_spin_slider.h"
#include "scene/gui/base_button.h" // ButtonGroup
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/container.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/resources/texture.h"

Control *cs_make_dock_root(VBoxContainer **r_content) {
	ScrollContainer *scroll = memnew(ScrollContainer);
	scroll->set_enable_h_scroll(false);
	scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);

	MarginContainer *margin = memnew(MarginContainer);
	margin->add_constant_override("margin_left", 8);
	margin->add_constant_override("margin_right", 8);
	margin->add_constant_override("margin_top", 8);
	margin->add_constant_override("margin_bottom", 8);
	margin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->add_child(margin);

	VBoxContainer *vb = memnew(VBoxContainer);
	vb->add_constant_override("separation", 6);
	vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	margin->add_child(vb);

	*r_content = vb;
	return scroll;
}

void cs_add_section_header(VBoxContainer *p_parent, const String &p_text) {
	if (p_parent->get_child_count() > 0) {
		HSeparator *sep = memnew(HSeparator);
		p_parent->add_child(sep);
	}
	Label *l = memnew(Label);
	l->set_text(p_text.to_upper());
	l->add_color_override("font_color", Color(0.55f, 0.72f, 0.95f));
	p_parent->add_child(l);
}

Button *cs_add_mode_button(GridContainer *p_grid, const Ref<ButtonGroup> &p_group,
		const String &p_text, Object *p_target, const StringName &p_callback, int p_index) {
	Button *b = memnew(Button);
	b->set_text(p_text);
	b->set_toggle_mode(true);
	b->set_button_group(p_group);
	b->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	b->connect("pressed", p_target, p_callback, varray(p_index));
	p_grid->add_child(b);
	return b;
}

EditorSpinSlider *cs_add_slider_row(VBoxContainer *p_parent, const String &p_label,
		double p_min, double p_max, double p_step, double p_default,
		Object *p_target, const StringName &p_callback) {
	EditorSpinSlider *s = memnew(EditorSpinSlider);
	s->set_label(p_label);
	s->set_min(p_min);
	s->set_max(p_max);
	s->set_step(p_step);
	s->set_value(p_default);
	s->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	s->connect("value_changed", p_target, p_callback);
	p_parent->add_child(s);
	return s;
}

Button *cs_add_icon_mode_button(Container *p_parent, const Ref<ButtonGroup> &p_group,
		const Ref<Texture> &p_icon, const String &p_tooltip,
		Object *p_target, const StringName &p_callback, int p_index) {
	Button *b = memnew(Button);
	b->set_icon(p_icon);
	b->set_tooltip(p_tooltip);
	b->set_toggle_mode(true);
	b->set_button_group(p_group);
	b->set_flat(true);
	b->connect("pressed", p_target, p_callback, varray(p_index));
	p_parent->add_child(b);
	return b;
}

// --- Tiny procedural icon drawing ---

Ref<Image> cs_icon_new(int p_size) {
	Ref<Image> img;
	img.instance();
	img->create(p_size, p_size, false, Image::FORMAT_RGBA8);
	img->fill(Color(0, 0, 0, 0));
	return img;
}

void cs_icon_fill_rect(Ref<Image> p_img, float p_x, float p_y, float p_w, float p_h, const Color &p_color) {
	int x0 = MAX(0, (int)p_x), y0 = MAX(0, (int)p_y);
	int x1 = MIN(p_img->get_width(), (int)Math::ceil(p_x + p_w));
	int y1 = MIN(p_img->get_height(), (int)Math::ceil(p_y + p_h));
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			p_img->set_pixel(x, y, p_color);
}

void cs_icon_fill_circle(Ref<Image> p_img, const Vector2 &p_center, float p_radius, const Color &p_color) {
	int w = p_img->get_width(), h = p_img->get_height();
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			if (Vector2(x + 0.5f, y + 0.5f).distance_to(p_center) <= p_radius)
				p_img->set_pixel(x, y, p_color);
		}
	}
}

static bool _cs_point_in_triangle(const Vector2 &p, const Vector2 &a, const Vector2 &b, const Vector2 &c) {
	float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
	float d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
	float d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
	bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
	bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
	return !(has_neg && has_pos);
}

void cs_icon_fill_triangle(Ref<Image> p_img, const Vector2 &p_a, const Vector2 &p_b, const Vector2 &p_c, const Color &p_color) {
	int w = p_img->get_width(), h = p_img->get_height();
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			if (_cs_point_in_triangle(Vector2(x + 0.5f, y + 0.5f), p_a, p_b, p_c))
				p_img->set_pixel(x, y, p_color);
		}
	}
}

void cs_icon_line(Ref<Image> p_img, const Vector2 &p_a, const Vector2 &p_b, float p_thickness, const Color &p_color) {
	int w = p_img->get_width(), h = p_img->get_height();
	Vector2 ab = p_b - p_a;
	float len2 = ab.length_squared();
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			Vector2 p(x + 0.5f, y + 0.5f);
			float t = len2 > 0.0001f ? CLAMP(ab.dot(p - p_a) / len2, 0.0f, 1.0f) : 0.0f;
			Vector2 closest = p_a + ab * t;
			if (p.distance_to(closest) <= p_thickness * 0.5f)
				p_img->set_pixel(x, y, p_color);
		}
	}
}

Ref<ImageTexture> cs_icon_to_texture(const Ref<Image> &p_img) {
	Ref<ImageTexture> tex;
	tex.instance();
	tex->create_from_image(p_img, Texture::FLAG_FILTER);
	return tex;
}

#endif
