// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger
//
// keysclicks: the GTK4/libadwaita front-end of the On-Screen Keys & Clicks
// Visualizer. It presents the settings window plus an always-on-top layer-shell
// label bar, spawns the privileged keysclicks-input backend through pkexec, and
// renders each keyboard key and mouse button the backend reports as a chip.

#include <adwaita.h>
#include <dirent.h>
#include <glib/gi18n.h>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <sys/types.h>

#include "keysclicks-backend.h"
#include "keysclicks-i18n.h"
#include "keysclicks-keymap.h"
#include "keysclicks-languages.h"
#include "keysclicks-overlay.h"
#include "keysclicks-privacy.h"
#include "keysclicks-settings.h"
#include "keysclicks-window.h"

// Neutral placeholder chip shown instead of a keystroke while masking is on
// (U+2022 BULLET). The real keysym never reaches the widget tree.
#define KEYSCLICKS_MASK_CHIP "\xe2\x80\xa2"

typedef struct {
	KeysClicksSettings *settings;
	KeysClicksPrivacy *privacy;
	GtkWidget *overlay;              // bottom key/click label bar
	GtkWidget *window;               // settings / control panel
	KeysClicksKeymap *keymap;
	KeysClicksBackend *backend;
	gboolean open_window;

	// Optional panic chord (read-only detection over the backend's libinput
	// event stream; never injected).
	guint8 held[KEY_CNT];
	int panic_keys[8];
	int panic_count;
	gboolean panic_satisfied;
	char *parsed_chord; // chord string currently parsed into panic_keys

	char *applied_layout; // keyboard_layout code currently applied to the keymap
} KeysClicksApp;

// Parse settings->panic_chord ("29+56+63" style evdev keycodes) into the app.
static void
parse_panic_chord(KeysClicksApp *app)
{
	g_free(app->parsed_chord);
	app->parsed_chord =
		g_strdup(app->settings->panic_chord != NULL ? app->settings->panic_chord : "");
	app->panic_count = 0;
	app->panic_satisfied = FALSE;
	const char *chord = app->settings->panic_chord;
	if (chord == NULL || *chord == '\0')
		return; // disabled by default

	char **parts = g_strsplit(chord, "+", -1);
	for (int i = 0; parts[i] != NULL &&
			app->panic_count < (int)G_N_ELEMENTS(app->panic_keys);
	     i++) {
		char *token = g_strstrip(parts[i]);
		if (*token == '\0')
			continue;
		gint64 code = g_ascii_strtoll(token, NULL, 10);
		if (code > 0 && code < KEY_CNT)
			app->panic_keys[app->panic_count++] = (int)code;
	}
	g_strfreev(parts);
}

// Toggle the privacy mask on the rising edge of the configured chord.
static void
panic_chord_update(KeysClicksApp *app)
{
	if (app->panic_count == 0) {
		app->panic_satisfied = FALSE;
		return;
	}
	gboolean all_held = TRUE;
	for (int i = 0; i < app->panic_count; i++) {
		int k = app->panic_keys[i];
		if (k < 0 || k >= KEY_CNT || !app->held[k]) {
			all_held = FALSE;
			break;
		}
	}
	if (all_held && !app->panic_satisfied) {
		app->settings->privacy_hide_keys = !app->settings->privacy_hide_keys;
		keysclicks_settings_emit_changed(app->settings);
	}
	app->panic_satisfied = all_held;
}

// Composed label for a pointer button (evdev button code). Caller frees it.
static char *
mouse_button_label(guint32 code)
{
	switch (code) {
	case BTN_LEFT:
		return g_strdup(_("Left Click"));
	case BTN_RIGHT:
		return g_strdup(_("Right Click"));
	case BTN_MIDDLE:
		return g_strdup(_("Middle Click"));
	case BTN_SIDE:
		return g_strdup("MouseBack");
	case BTN_EXTRA:
		return g_strdup("MouseForward");
	case BTN_FORWARD:
		return g_strdup("MouseForward");
	case BTN_BACK:
		return g_strdup("MouseBack");
	case BTN_TASK:
		return g_strdup("MouseTask");
	default:
		return g_strdup_printf(_("Mouse(%u)"), code);
	}
}

// Label for a keyboard key honouring the chosen display mode. Caller frees it.
static char *
make_key_label(KeysClicksApp *app, guint32 code)
{
	if (app->settings->mode == KEYSCLICKS_MODE_RAW) {
		const char *name = libevdev_event_code_get_name(EV_KEY, code);
		return g_strdup(name != NULL ? name : "KEY");
	}
	// Composed and Compact both use the readable, layout-composed label, with
	// held Ctrl/Alt/Super folded into the chord (e.g. "Ctrl+Shift+T") so a
	// combination shows as one label rather than only the last key. Returned as
	// Pango markup (dim "+" separators) — overlay chips are rendered as markup.
	return keysclicks_keymap_combo_markup(app->keymap, code);
}

// Label for a mouse button honouring the chosen display mode. Caller frees it.
static char *
make_button_label(guint32 code, KeysClicksMode mode)
{
	if (mode == KEYSCLICKS_MODE_RAW) {
		const char *name = libevdev_event_code_get_name(EV_KEY, code);
		return g_strdup(name != NULL ? name : "BTN");
	}
	if (mode == KEYSCLICKS_MODE_COMPACT) {
		switch (code) {
		case BTN_LEFT:
			return g_strdup("LMB");
		case BTN_RIGHT:
			return g_strdup("RMB");
		case BTN_MIDDLE:
			return g_strdup("MMB");
		case BTN_SIDE:
		case BTN_BACK:
			return g_strdup("M-Back");
		case BTN_EXTRA:
		case BTN_FORWARD:
			return g_strdup("M-Fwd");
		case BTN_TASK:
			return g_strdup("M-Task");
		default:
			return g_strdup_printf("M%u", code);
		}
	}
	return mouse_button_label(code);
}

// Runs on the main context for every backend event.
static void
on_input_event(KeysClicksEventKind kind, guint32 code, gboolean pressed, double dx,
	       double dy, gpointer user_data)
{
	KeysClicksApp *app = user_data;
	KeysClicksSettings *s = app->settings;

	switch (kind) {
	case KEYSCLICKS_EVENT_KEY: {
		// Track held keys and check the optional panic chord on every event.
		if (code < KEY_CNT)
			app->held[code] = pressed ? 1 : 0;
		panic_chord_update(app);

		if (pressed) {
			// Effective mask, evaluated at render time. When masked, the real
			// keysym is never even computed, so nothing can leak into the
			// widget tree in any label mode.
			gboolean masked = s->privacy_hide_keys ||
					  keysclicks_privacy_should_mask(app->privacy);
			char *label = masked ? NULL : make_key_label(app, code);
			keysclicks_keymap_feed(app->keymap, code, TRUE);

			gboolean is_shift = code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT;
			gboolean suppress_shift = is_shift && !s->show_shift_separately;
			if (s->show_keyboard && !suppress_shift)
				keysclicks_overlay_push(app->overlay,
							masked ? KEYSCLICKS_MASK_CHIP : label);
			g_free(label);
		} else {
			keysclicks_keymap_feed(app->keymap, code, FALSE);
		}
		break;
	}

	case KEYSCLICKS_EVENT_BUTTON:
		// Mouse buttons show as labels in the bar. (The positioned click-flash
		// under the cursor was removed: a Wayland client cannot know the true
		// pointer position without capturing input, which would block clicks.)
		if (pressed && s->show_mouse) {
			char *label = make_button_label(code, s->mode);
			keysclicks_overlay_push(app->overlay, label);
			g_free(label);
		}
		break;

	case KEYSCLICKS_EVENT_MOTION:
		(void)dx;
		(void)dy;
		break; // pointer position is not tracked (no cursor flash)
	}
}

// Apply the persisted overlay keyboard-layout choice to the keymap. An empty (or
// unknown) code means the system default. The chosen language code is mapped to its
// xkb layout/variant; on failure the previous layout is kept and a warning is logged.
static void
apply_keyboard_layout(KeysClicksApp *app)
{
	if (app->keymap == NULL)
		return;
	// keyboard_layout/keyboard_variant are xkb names straight from keysclicks-layouts;
	// "" for either means the system default for that field.
	const char *layout = app->settings->keyboard_layout;
	const char *variant = app->settings->keyboard_variant;
	if (!keysclicks_keymap_set_layout(app->keymap, layout, variant))
		g_warning("keysclicks: keyboard layout '%s%s%s' is unavailable; keeping the "
			  "previous layout", (layout != NULL && *layout != '\0') ? layout : "system",
			  (variant != NULL && *variant != '\0') ? "/" : "",
			  (variant != NULL && *variant != '\0') ? variant : "");
	// Remember what is applied as "<layout>\t<variant>" so a change in EITHER field is
	// detected (switching only the variant keeps the same layout name).
	g_free(app->applied_layout);
	app->applied_layout = g_strdup_printf("%s\t%s", layout != NULL ? layout : "",
					      variant != NULL ? variant : "");
}

// Re-apply settings to the live overlays whenever the settings change.
static void
on_settings_changed(KeysClicksSettings *settings, gpointer user_data)
{
	(void)settings;
	KeysClicksApp *app = user_data;
	keysclicks_overlay_apply_settings(app->overlay);
	// Re-read the hotkey chord ONLY when it actually changed (re-assigned in the UI).
	// Re-parsing on EVERY settings change reset panic_satisfied mid-hold — including
	// when the chord itself fired (emit_changed) — which broke toggling the mask off
	// again with the same shortcut.
	if (g_strcmp0(app->parsed_chord, app->settings->panic_chord) != 0)
		parse_panic_chord(app);
	// Re-apply the overlay keyboard layout only when it actually changed (layout OR
	// variant): a rebuild resets modifier state, so we must not do it on every
	// unrelated settings change.
	char *want = g_strdup_printf("%s\t%s",
				     app->settings->keyboard_layout != NULL
					     ? app->settings->keyboard_layout : "",
				     app->settings->keyboard_variant != NULL
					     ? app->settings->keyboard_variant : "");
	if (g_strcmp0(app->applied_layout, want) != 0)
		apply_keyboard_layout(app);
	g_free(want);
}

// Kill any OTHER keysclicks front-end processes before we start. The app runs
// NON_UNIQUE (each launch is its own process), so a fresh launch always replaces a
// previous one — even a hung/wedged instance that ignores its Quit action. This is
// what lets "quit + relaunch" reliably pick up a new build. Matches the exact comm
// "keysclicks" (not "keysclicks-input", the backend) and skips our own PID.
static void
kill_stale_frontends(void)
{
	pid_t self = getpid();
	DIR *proc = opendir("/proc");
	if (proc == NULL)
		return;
	struct dirent *e;
	while ((e = readdir(proc)) != NULL) {
		pid_t pid = (pid_t)atoi(e->d_name);
		if (pid <= 0 || pid == self)
			continue;
		char path[64];
		snprintf(path, sizeof path, "/proc/%d/comm", pid);
		FILE *f = fopen(path, "r");
		if (f == NULL)
			continue;
		char comm[32] = { 0 };
		if (fgets(comm, sizeof comm, f) != NULL && strcmp(comm, "keysclicks\n") == 0)
			kill(pid, SIGKILL); // hung instances ignore SIGTERM — force it
		fclose(f);
	}
	closedir(proc);
}

// TRUE if any keyboard lock LED matching `suffix` ("::numlock"/"::capslock") is
// lit. sysfs LEDs are user-readable, so the app learns the initial NumLock/CapsLock
// state without the privileged backend — otherwise its xkb state starts unlocked
// and the numpad shows navigation keys while the system has NumLock on.
static gboolean
read_lock_led(const char *suffix)
{
	GDir *dir = g_dir_open("/sys/class/leds", 0, NULL);
	if (dir == NULL)
		return FALSE;
	gboolean on = FALSE;
	const char *name;
	while ((name = g_dir_read_name(dir)) != NULL) {
		if (!g_str_has_suffix(name, suffix))
			continue;
		char *path = g_build_filename("/sys/class/leds", name, "brightness", NULL);
		char *val = NULL;
		if (g_file_get_contents(path, &val, NULL, NULL)) {
			if (g_ascii_strtoll(val, NULL, 10) > 0)
				on = TRUE;
			g_free(val);
		}
		g_free(path);
	}
	g_dir_close(dir);
	return on;
}

static void
on_activate(GtkApplication *gtk_app, gpointer user_data)
{
	KeysClicksApp *app = user_data;

	app->settings = keysclicks_settings_new();
	// Launching the overlay app means you want the overlay: force it visible on
	// every start, regardless of the persisted value. The user can still toggle
	// it off for the current session from the settings window.
	app->settings->overlay_visible = TRUE;
	app->privacy = keysclicks_privacy_new(); // fail-open plugin load
	parse_panic_chord(app);

	// Apply the persisted UI language before building any window ("" = system), and
	// mirror the layout for right-to-left languages (Arabic/Persian/Urdu).
	keysclicks_i18n_apply_language(app->settings->language);
	{
		const char *lc = keysclicks_language_resolve(app->settings->language);
		gtk_widget_set_default_direction(keysclicks_language_is_rtl(lc)
							 ? GTK_TEXT_DIR_RTL
							 : GTK_TEXT_DIR_LTR);
	}

	app->overlay = keysclicks_overlay_new(gtk_app, app->settings);
	keysclicks_settings_add_listener(app->settings, on_settings_changed, app);

	if (app->open_window) {
		app->window = keysclicks_window_new(gtk_app, app->settings, app->privacy);
		gtk_window_present(GTK_WINDOW(app->window));
	}

	app->keymap = keysclicks_keymap_new();
	if (app->keymap == NULL)
		g_warning("keysclicks: failed to initialise the xkb keymap; key labels "
			  "may be missing");
	else
		keysclicks_keymap_set_locks(app->keymap, read_lock_led("::numlock"),
					    read_lock_led("::capslock"));

	// Apply the persisted overlay keyboard layout (after the locks are seeded).
	apply_keyboard_layout(app);

	app->backend = keysclicks_backend_new(on_input_event, app);

	GError *error = NULL;
	if (!keysclicks_backend_start(app->backend, &error)) {
		g_warning("keysclicks: could not start the input backend: %s",
			  error != NULL ? error->message : "unknown error");
		g_clear_error(&error);
	}
}

static gint
on_local_options(GApplication *application, GVariantDict *options, gpointer user_data)
{
	(void)application;
	KeysClicksApp *app = user_data;
	if (g_variant_dict_contains(options, "no-window"))
		app->open_window = FALSE;
	return -1; // let default processing continue (activate still fires)
}

int
main(int argc, char **argv)
{
	KeysClicksApp app = { .open_window = TRUE };

	// Replace any previous (possibly hung) instance so a relaunch always runs THIS
	// binary. NON_UNIQUE makes each launch its own process (no single-instance
	// activation of a stale one), and killing the old front-end first frees the
	// overlay for the new one.
	kill_stale_frontends();

	// Internationalization: pick up the system locale and bind the gettext domain
	// so the UI opens in the user's native language (a persisted choice overrides
	// this in on_activate).
	keysclicks_i18n_init();

	AdwApplication *adw_app = adw_application_new(
		"io.github.vibecodeblogger.KeysAndClicksVisualizer",
		G_APPLICATION_NON_UNIQUE);
	g_application_add_main_option(G_APPLICATION(adw_app), "no-window", 0,
				      G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
				      _("Start without opening the settings window"), NULL);
	g_signal_connect(adw_app, "handle-local-options", G_CALLBACK(on_local_options),
			 &app);
	g_signal_connect(adw_app, "activate", G_CALLBACK(on_activate), &app);

	int status = g_application_run(G_APPLICATION(adw_app), argc, argv);

	if (app.backend != NULL)
		keysclicks_backend_free(app.backend);
	if (app.keymap != NULL)
		keysclicks_keymap_free(app.keymap);
	if (app.privacy != NULL)
		keysclicks_privacy_free(app.privacy);
	if (app.settings != NULL)
		keysclicks_settings_free(app.settings);
	g_free(app.parsed_chord);
	g_free(app.applied_layout);
	g_object_unref(adw_app);
	return status;
}
