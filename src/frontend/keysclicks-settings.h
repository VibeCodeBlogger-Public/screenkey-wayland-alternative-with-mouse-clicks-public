// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

// How key/button labels are rendered.
typedef enum {
	KEYSCLICKS_MODE_COMPOSED, // readable, layout-composed (default)
	KEYSCLICKS_MODE_RAW,      // raw evdev names (KEY_A, BTN_LEFT)
	KEYSCLICKS_MODE_COMPACT,  // short forms (LMB, RMB, ...)
} KeysClicksMode;

// Horizontal placement of the bar.
typedef enum {
	KEYSCLICKS_ALIGN_START,
	KEYSCLICKS_ALIGN_CENTER,
	KEYSCLICKS_ALIGN_END,
} KeysClicksAlign;

// Which screen edge the bar hugs vertically.
typedef enum {
	KEYSCLICKS_EDGE_TOP,
	KEYSCLICKS_EDGE_BOTTOM,
} KeysClicksEdge;

// Plain settings record. Fields are read directly by the overlays and written
// by the settings window; after any change the writer calls
// keysclicks_settings_emit_changed(), which persists to disk and notifies
// listeners so the overlays re-apply live.
typedef struct _KeysClicksSettings KeysClicksSettings;

struct _KeysClicksSettings {
	// Global
	gboolean overlay_visible;

	// Key / click display
	KeysClicksMode mode;
	int font_size;
	gboolean show_keyboard;
	gboolean show_mouse;
	gboolean show_shift_separately;
	gboolean draw_border;
	KeysClicksAlign align;

	// Position & behaviour of the bar
	KeysClicksEdge edge;
	int margin_x;
	int margin_y;
	char *monitor_connector; // "" = let the compositor decide
	gboolean keep_forever;   // TRUE: chips never time out
	int timeout_ms;          // used when keep_forever is FALSE
	double bar_opacity;      // 0.0 .. 1.0

	// Click flash (ripple)
	gboolean ripple_enabled;
	GdkRGBA ripple_left_color;
	GdkRGBA ripple_right_color;
	double ripple_radius;
	int ripple_duration_ms;
	double ripple_opacity; // 0.0 .. 1.0

	// Privacy
	gboolean privacy_hide_keys; // manual master toggle: mask keystrokes
	char *panic_chord;          // '+'-joined evdev keycodes; "" = disabled

	// Global hotkey that toggles the whole overlay on/off (same mechanism as
	// panic_chord: detected over the libinput stream). '+'-joined evdev keycodes; "" = disabled.
	char *overlay_toggle_chord;

	// UI language
	char *language; // "" = auto (system locale); else a supported code, e.g. "ru"

	// Overlay keyboard layout (DISPLAY only — which xkb layout turns keycodes into
	// symbols in the overlay). "" = system default; else an xkb layout name, e.g. "ru"
	// (from the full xkb registry, see keysclicks-layouts).
	char *keyboard_layout;
	char *keyboard_variant; // xkb variant for keyboard_layout, or "" for the default

	// listeners (internal)
	GPtrArray *listeners;
};

typedef void (*KeysClicksSettingsChangedFunc)(KeysClicksSettings *settings,
					      gpointer user_data);

// Create, loading persisted values from
// $XDG_CONFIG_HOME/keysclicks/settings.ini (falling back to defaults).
KeysClicksSettings *keysclicks_settings_new(void);

void keysclicks_settings_free(KeysClicksSettings *self);

// Replace the monitor connector string (takes a copy).
void keysclicks_settings_set_monitor(KeysClicksSettings *self, const char *connector);

// Persist to disk immediately, then notify every registered listener.
void keysclicks_settings_emit_changed(KeysClicksSettings *self);

void keysclicks_settings_add_listener(KeysClicksSettings *self,
				      KeysClicksSettingsChangedFunc func,
				      gpointer user_data);

// Remove a previously added listener matching both func and user_data. Safe to
// call for a listener that was never added (no-op). Must be called before the
// user_data it references is freed (e.g. from a window's destroy notify).
void keysclicks_settings_remove_listener(KeysClicksSettings *self,
					 KeysClicksSettingsChangedFunc func,
					 gpointer user_data);

G_END_DECLS
