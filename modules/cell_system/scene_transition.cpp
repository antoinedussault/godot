#include "scene_transition.h"

#include "core/io/resource_loader.h"
#include "core/math/math_funcs.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/control.h"
#include "scene/resources/packed_scene.h"

SceneTransition::SceneTransition() {
	set_layer(128); // above everything the game draws

	_backdrop = memnew(ColorRect);
	_backdrop->set_name("Backdrop");
	_backdrop->set_frame_color(fade_color);
	_backdrop->set_anchors_and_margins_preset(Control::PRESET_WIDE);
	_backdrop->set_mouse_filter(Control::MOUSE_FILTER_STOP);
	_backdrop->set_modulate(Color(1, 1, 1, 0));
	add_child(_backdrop);

	set_visible(false);
	set_process(false);
}

void SceneTransition::_clear_loading_instance() {
	if (_loading_inst) {
		_loading_inst->queue_delete();
		_loading_inst = nullptr;
	}
}

void SceneTransition::_apply_alpha() {
	_backdrop->set_modulate(Color(1, 1, 1, _alpha));
	CanvasItem *ci = Object::cast_to<CanvasItem>(_loading_inst);
	if (ci) {
		ci->set_modulate(Color(1, 1, 1, _alpha));
	}
}

void SceneTransition::show_loading(const String &p_scene_path) {
	_hide_requested = false;
	_covered_elapsed = 0.0f;
	_progress = 0.0f;

	if (p_scene_path != _loading_scene_path || !_loading_inst) {
		_clear_loading_instance();
		_loading_scene_path = p_scene_path;
		if (!p_scene_path.empty()) {
			Ref<PackedScene> ps = ResourceLoader::load(p_scene_path);
			if (ps.is_valid()) {
				_loading_inst = ps->instance();
				Control *c = Object::cast_to<Control>(_loading_inst);
				if (c) {
					c->set_anchors_and_margins_preset(Control::PRESET_WIDE);
					c->set_mouse_filter(Control::MOUSE_FILTER_STOP);
				}
				if (_loading_inst) {
					add_child(_loading_inst);
				}
			} else {
				ERR_PRINT("SceneTransition: could not load loading screen '" + p_scene_path + "'");
			}
		}
	}

	set_visible(true);
	set_process(true);
	_state = STATE_IN;
	_apply_alpha();
}

void SceneTransition::set_progress(float p_ratio) {
	_progress = CLAMP(p_ratio, 0.0f, 1.0f);
	if (_loading_inst && _loading_inst->has_method("set_progress")) {
		_loading_inst->call("set_progress", _progress);
	}
}

void SceneTransition::hide_loading() {
	_hide_requested = true;
}

void SceneTransition::_notification(int p_what) {
	if (p_what != NOTIFICATION_PROCESS) {
		return;
	}
	float delta = get_process_delta_time();
	float step = (fade_time > 0.0f) ? (delta / fade_time) : 1.0f;

	switch (_state) {
		case STATE_HIDDEN: {
			set_process(false);
		} break;
		case STATE_IN: {
			_alpha = MIN(1.0f, _alpha + step);
			_apply_alpha();
			if (_alpha >= 1.0f) {
				_state = STATE_COVERED;
				_covered_elapsed = 0.0f;
			}
		} break;
		case STATE_COVERED: {
			_covered_elapsed += delta;
			if (_hide_requested && _covered_elapsed >= min_display_time) {
				_state = STATE_OUT;
			}
		} break;
		case STATE_OUT: {
			_alpha = MAX(0.0f, _alpha - step);
			_apply_alpha();
			if (_alpha <= 0.0f) {
				_state = STATE_HIDDEN;
				_hide_requested = false;
				set_visible(false);
				set_process(false);
				_clear_loading_instance();
				_loading_scene_path = String();
			}
		} break;
	}
}

void SceneTransition::set_fade_color(const Color &p_color) {
	fade_color = p_color;
	if (_backdrop) {
		_backdrop->set_frame_color(fade_color);
	}
}

void SceneTransition::set_fade_time(float p_time) {
	fade_time = MAX(0.0f, p_time);
}

void SceneTransition::set_min_display_time(float p_time) {
	min_display_time = MAX(0.0f, p_time);
}

void SceneTransition::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_fade_color", "color"), &SceneTransition::set_fade_color);
	ClassDB::bind_method(D_METHOD("get_fade_color"), &SceneTransition::get_fade_color);
	ClassDB::bind_method(D_METHOD("set_fade_time", "time"), &SceneTransition::set_fade_time);
	ClassDB::bind_method(D_METHOD("get_fade_time"), &SceneTransition::get_fade_time);
	ClassDB::bind_method(D_METHOD("set_min_display_time", "time"), &SceneTransition::set_min_display_time);
	ClassDB::bind_method(D_METHOD("get_min_display_time"), &SceneTransition::get_min_display_time);
	ClassDB::bind_method(D_METHOD("show_loading", "scene_path"), &SceneTransition::show_loading);
	ClassDB::bind_method(D_METHOD("set_progress", "ratio"), &SceneTransition::set_progress);
	ClassDB::bind_method(D_METHOD("hide_loading"), &SceneTransition::hide_loading);
	ClassDB::bind_method(D_METHOD("is_covered"), &SceneTransition::is_covered);
	ClassDB::bind_method(D_METHOD("is_busy"), &SceneTransition::is_busy);

	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "fade_color"), "set_fade_color", "get_fade_color");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "fade_time", PROPERTY_HINT_RANGE, "0,3,0.01"), "set_fade_time", "get_fade_time");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "min_display_time", PROPERTY_HINT_RANGE, "0,10,0.01"), "set_min_display_time", "get_min_display_time");

	BIND_ENUM_CONSTANT(STATE_HIDDEN);
	BIND_ENUM_CONSTANT(STATE_IN);
	BIND_ENUM_CONSTANT(STATE_COVERED);
	BIND_ENUM_CONSTANT(STATE_OUT);
}
