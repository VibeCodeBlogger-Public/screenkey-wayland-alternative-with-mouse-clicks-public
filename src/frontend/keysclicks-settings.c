// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger

#include "keysclicks-settings.h"

typedef struct {
	KeysClicksSettingsChangedFunc func;
	gpointer user_data;
} Listener;

static char *
settings_path(void)
{
	return g_build_filename(g_get_user_config_dir(), "keysclicks", "settings.ini",
				NULL);
}

static void
set_defaults(KeysClicksSettings *s)
{
	s->overlay_visible = TRUE;
	s->mode = KEYSCLICKS_MODE_COMPOSED;
	s->font_size = 20;
	s->show_keyboard = TRUE;
	s->show_mouse = TRUE;
	s->show_shift_separately = TRUE;
	s->draw_border = TRUE;
	s->align = KEYSCLICKS_ALIGN_CENTER;

	s->edge = KEYSCLICKS_EDGE_BOTTOM;
	s->margin_x = 0; // percent: 0 = centred horizontally
	s->margin_y = 5; // percent of monitor height from the edge
	s->monitor_connector = g_strdup("");
	s->keep_forever = FALSE;
	s->timeout_ms = 1800;
	s->bar_opacity = 1.0;

	// Mouse click-flash on by default (owner request). Original colours: yellow for
	// the left click, blue for the right click — restored after a wrong detour to
	// green/amber. (The dark-green identity is for the UI accent/window frame, not
	// the flash dots.)
	s->ripple_enabled = TRUE;
	gdk_rgba_parse(&s->ripple_left_color, "rgb(255,217,25)");  // yellow — left click
	gdk_rgba_parse(&s->ripple_right_color, "rgb(89,184,255)"); // blue — right click
	s->ripple_radius = 32.0;
	s->ripple_duration_ms = 800;
	s->ripple_opacity = 0.5;

	s->privacy_hide_keys = FALSE;
	s->panic_chord = g_strdup(""); // disabled by default
	s->overlay_toggle_chord = g_strdup(""); // disabled by default

	s->language = g_strdup(""); // auto: follow the system locale
	s->keyboard_layout = g_strdup(""); // system default xkb layout for the overlay
	s->keyboard_variant = g_strdup("");
}

// --- typed GKeyFile helpers that fall back to the current value ------------

static int
key_int(GKeyFile *kf, const char *group, const char *key, int fallback)
{
	GError *err = NULL;
	int v = g_key_file_get_integer(kf, group, key, &err);
	if (err != NULL) {
		g_clear_error(&err);
		return fallback;
	}
	return v;
}

static gboolean
key_bool(GKeyFile *kf, const char *group, const char *key, gboolean fallback)
{
	GError *err = NULL;
	gboolean v = g_key_file_get_boolean(kf, group, key, &err);
	if (err != NULL) {
		g_clear_error(&err);
		return fallback;
	}
	return v;
}

static double
key_double(GKeyFile *kf, const char *group, const char *key, double fallback)
{
	GError *err = NULL;
	double v = g_key_file_get_double(kf, group, key, &err);
	if (err != NULL) {
		g_clear_error(&err);
		return fallback;
	}
	return v;
}

static void
key_color(GKeyFile *kf, const char *group, const char *key, GdkRGBA *color)
{
	char *str = g_key_file_get_string(kf, group, key, NULL);
	if (str != NULL) {
		GdkRGBA parsed;
		if (gdk_rgba_parse(&parsed, str))
			*color = parsed;
		g_free(str);
	}
}

static void
load_from_disk(KeysClicksSettings *s)
{
	char *path = settings_path();
	GKeyFile *kf = g_key_file_new();
	if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
		s->overlay_visible = key_bool(kf, "overlay", "visible", s->overlay_visible);

		s->mode = key_int(kf, "keys", "mode", s->mode);
		s->font_size = key_int(kf, "keys", "font_size", s->font_size);
		s->show_keyboard = key_bool(kf, "keys", "show_keyboard", s->show_keyboard);
		s->show_mouse = key_bool(kf, "keys", "show_mouse", s->show_mouse);
		s->show_shift_separately =
			key_bool(kf, "keys", "show_shift_separately", s->show_shift_separately);
		s->draw_border = key_bool(kf, "keys", "draw_border", s->draw_border);
		s->align = key_int(kf, "keys", "align", s->align);

		s->edge = key_int(kf, "position", "edge", s->edge);
		s->margin_x = key_int(kf, "position", "margin_x", s->margin_x);
		s->margin_y = key_int(kf, "position", "margin_y", s->margin_y);
		char *connector = g_key_file_get_string(kf, "position", "monitor", NULL);
		if (connector != NULL) {
			g_free(s->monitor_connector);
			s->monitor_connector = connector;
		}
		s->keep_forever = key_bool(kf, "position", "keep_forever", s->keep_forever);
		s->timeout_ms = key_int(kf, "position", "timeout_ms", s->timeout_ms);
		s->bar_opacity = key_double(kf, "position", "opacity", s->bar_opacity);

		s->ripple_enabled = key_bool(kf, "ripple", "enabled", s->ripple_enabled);
		key_color(kf, "ripple", "left_color", &s->ripple_left_color);
		key_color(kf, "ripple", "right_color", &s->ripple_right_color);
		s->ripple_radius = key_double(kf, "ripple", "radius", s->ripple_radius);
		s->ripple_duration_ms =
			key_int(kf, "ripple", "duration_ms", s->ripple_duration_ms);
		s->ripple_opacity = key_double(kf, "ripple", "opacity", s->ripple_opacity);

		s->privacy_hide_keys =
			key_bool(kf, "privacy", "hide_keys", s->privacy_hide_keys);
		char *chord = g_key_file_get_string(kf, "privacy", "panic_chord", NULL);
		if (chord != NULL) {
			g_free(s->panic_chord);
			s->panic_chord = chord;
		}
		char *ov_chord = g_key_file_get_string(kf, "overlay", "toggle_chord", NULL);
		if (ov_chord != NULL) {
			g_free(s->overlay_toggle_chord);
			s->overlay_toggle_chord = ov_chord;
		}

		char *lang = g_key_file_get_string(kf, "general", "language", NULL);
		if (lang != NULL) {
			g_free(s->language);
			s->language = lang;
		}

		char *kbl = g_key_file_get_string(kf, "general", "keyboard_layout", NULL);
		if (kbl != NULL) {
			g_free(s->keyboard_layout);
			s->keyboard_layout = kbl;
		}
		char *kbv = g_key_file_get_string(kf, "general", "keyboard_variant", NULL);
		if (kbv != NULL) {
			g_free(s->keyboard_variant);
			s->keyboard_variant = kbv;
		}
	}
	g_key_file_free(kf);
	g_free(path);
}

KeysClicksSettings *
keysclicks_settings_new(void)
{
	KeysClicksSettings *s = g_new0(KeysClicksSettings, 1);
	s->listeners = g_ptr_array_new_with_free_func(g_free);
	set_defaults(s);
	load_from_disk(s);
	return s;
}

void
keysclicks_settings_free(KeysClicksSettings *self)
{
	if (self == NULL)
		return;
	g_ptr_array_free(self->listeners, TRUE);
	g_free(self->monitor_connector);
	g_free(self->panic_chord);
	g_free(self->overlay_toggle_chord);
	g_free(self->language);
	g_free(self->keyboard_layout);
	g_free(self->keyboard_variant);
	g_free(self);
}

void
keysclicks_settings_set_monitor(KeysClicksSettings *self, const char *connector)
{
	g_free(self->monitor_connector);
	self->monitor_connector = g_strdup(connector != NULL ? connector : "");
}

static void
save_to_disk(KeysClicksSettings *s)
{
	char *path = settings_path();
	GKeyFile *kf = g_key_file_new();
	// Preserve groups owned by other modules (e.g. [calibration:*]) by loading
	// the existing file before rewriting our own keys.
	g_key_file_load_from_file(kf, path, G_KEY_FILE_KEEP_COMMENTS, NULL);

	g_key_file_set_boolean(kf, "overlay", "visible", s->overlay_visible);
	g_key_file_set_string(kf, "overlay", "toggle_chord",
			      s->overlay_toggle_chord != NULL ? s->overlay_toggle_chord : "");

	g_key_file_set_integer(kf, "keys", "mode", s->mode);
	g_key_file_set_integer(kf, "keys", "font_size", s->font_size);
	g_key_file_set_boolean(kf, "keys", "show_keyboard", s->show_keyboard);
	g_key_file_set_boolean(kf, "keys", "show_mouse", s->show_mouse);
	g_key_file_set_boolean(kf, "keys", "show_shift_separately",
			       s->show_shift_separately);
	g_key_file_set_boolean(kf, "keys", "draw_border", s->draw_border);
	g_key_file_set_integer(kf, "keys", "align", s->align);

	g_key_file_set_integer(kf, "position", "edge", s->edge);
	g_key_file_set_integer(kf, "position", "margin_x", s->margin_x);
	g_key_file_set_integer(kf, "position", "margin_y", s->margin_y);
	g_key_file_set_string(kf, "position", "monitor",
			      s->monitor_connector != NULL ? s->monitor_connector : "");
	g_key_file_set_boolean(kf, "position", "keep_forever", s->keep_forever);
	g_key_file_set_integer(kf, "position", "timeout_ms", s->timeout_ms);
	g_key_file_set_double(kf, "position", "opacity", s->bar_opacity);

	g_key_file_set_boolean(kf, "ripple", "enabled", s->ripple_enabled);
	char *left = gdk_rgba_to_string(&s->ripple_left_color);
	char *right = gdk_rgba_to_string(&s->ripple_right_color);
	g_key_file_set_string(kf, "ripple", "left_color", left);
	g_key_file_set_string(kf, "ripple", "right_color", right);
	g_free(left);
	g_free(right);
	g_key_file_set_double(kf, "ripple", "radius", s->ripple_radius);
	g_key_file_set_integer(kf, "ripple", "duration_ms", s->ripple_duration_ms);
	g_key_file_set_double(kf, "ripple", "opacity", s->ripple_opacity);

	g_key_file_set_boolean(kf, "privacy", "hide_keys", s->privacy_hide_keys);
	g_key_file_set_string(kf, "privacy", "panic_chord",
			      s->panic_chord != NULL ? s->panic_chord : "");

	g_key_file_set_string(kf, "general", "language",
			      s->language != NULL ? s->language : "");
	g_key_file_set_string(kf, "general", "keyboard_layout",
			      s->keyboard_layout != NULL ? s->keyboard_layout : "");
	g_key_file_set_string(kf, "general", "keyboard_variant",
			      s->keyboard_variant != NULL ? s->keyboard_variant : "");

	char *dir = g_path_get_dirname(path);
	g_mkdir_with_parents(dir, 0700);
	g_key_file_save_to_file(kf, path, NULL);
	g_free(dir);
	g_free(path);
	g_key_file_free(kf);
}

void
keysclicks_settings_emit_changed(KeysClicksSettings *self)
{
	if (self == NULL)
		return;
	save_to_disk(self);
	for (guint i = 0; i < self->listeners->len; i++) {
		Listener *l = g_ptr_array_index(self->listeners, i);
		l->func(self, l->user_data);
	}
}

void
keysclicks_settings_add_listener(KeysClicksSettings *self,
				 KeysClicksSettingsChangedFunc func, gpointer user_data)
{
	Listener *l = g_new0(Listener, 1);
	l->func = func;
	l->user_data = user_data;
	g_ptr_array_add(self->listeners, l);
}

void
keysclicks_settings_remove_listener(KeysClicksSettings *self,
				    KeysClicksSettingsChangedFunc func, gpointer user_data)
{
	if (self == NULL)
		return;
	for (guint i = 0; i < self->listeners->len; i++) {
		Listener *l = g_ptr_array_index(self->listeners, i);
		if (l->func == func && l->user_data == user_data) {
			// The array owns the Listener (g_free free-func), so this also
			// frees it. Remove only the first match and stop.
			g_ptr_array_remove_index(self->listeners, i);
			return;
		}
	}
}
