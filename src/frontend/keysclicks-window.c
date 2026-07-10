// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger

#include "keysclicks-window.h"

#include "keysclicks-accent-css.h"
#include "keysclicks-dock.h"
#include "keysclicks-feedback.h"
#include "keysclicks-i18n.h"
#include "keysclicks-languages.h"
#include "keysclicks-layouts.h"
#include "version.h"

#include <glib/gi18n.h>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>

// Identifies which settings field a control edits, so a single handler per
// widget kind can dispatch to the right field.
typedef enum {
	F_MODE,
	F_FONT_SIZE,
	F_SHOW_KEYBOARD,
	F_SHOW_MOUSE,
	F_SHOW_SHIFT,
	F_DRAW_BORDER,
	F_ALIGN,
	F_EDGE,
	F_MARGIN_X,
	F_MARGIN_Y,
	F_MONITOR,
	F_KEEP,
	F_TIMEOUT,
	F_BAR_OPACITY,
} FieldId;

typedef struct {
	GtkApplication *app;
	GtkWidget *window;
	KeysClicksSettings *settings;
	KeysClicksPrivacy *privacy;
	GtkWidget *timeout_row; // toggled sensitive by the "keep" switch

	// Privacy toggle mirrored between the header button and the settings row.
	GtkWidget *privacy_toggle;
	GtkWidget *privacy_switch;
	gboolean privacy_updating;

	// "Hide keys" shortcut (panic chord) capture.
	GtkWidget *chord_row;    // AdwActionRow showing the current chord + subtitle
	GtkWidget *chord_button; // "Set" / "Press keys…" button
	gboolean chord_capturing;
	GArray *chord_captured; // guint evdev codes captured this pass

	// TRUE while (re)building the content, so the language combo's initial
	// selection does not re-trigger a rebuild.
	gboolean rebuilding;
} WinCtx;

static void populate_content(WinCtx *ctx);

// Apply a new privacy state to both mirrored controls and persist it. The guard
// prevents the two controls' handlers from bouncing off each other.
static void
set_privacy(WinCtx *ctx, gboolean value)
{
	if (ctx->privacy_updating)
		return;
	ctx->privacy_updating = TRUE;
	ctx->settings->privacy_hide_keys = value;
	if (ctx->privacy_toggle != NULL)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ctx->privacy_toggle), value);
	if (ctx->privacy_switch != NULL)
		adw_switch_row_set_active(ADW_SWITCH_ROW(ctx->privacy_switch), value);
	keysclicks_settings_emit_changed(ctx->settings);
	ctx->privacy_updating = FALSE;
}

static void
on_privacy_toggle(GtkToggleButton *button, gpointer user_data)
{
	set_privacy(user_data, gtk_toggle_button_get_active(button));
}

static void
on_privacy_switch(GObject *row, GParamSpec *pspec, gpointer user_data)
{
	(void)pspec;
	set_privacy(user_data, adw_switch_row_get_active(ADW_SWITCH_ROW(row)));
}

// Settings listener: when privacy_hide_keys changes from outside this window
// (e.g. the panic-chord hotkey toggles the mask), move both mirrored controls to
// match. The privacy_updating guard both skips our own writes (set_privacy is
// mid-update) and stops the controls' handlers from bouncing back into
// set_privacy while we set them here.
static void
on_settings_privacy_sync(KeysClicksSettings *settings, gpointer user_data)
{
	WinCtx *ctx = user_data;
	if (ctx->privacy_updating)
		return;
	gboolean v = settings->privacy_hide_keys;
	ctx->privacy_updating = TRUE;
	if (ctx->privacy_toggle != NULL)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ctx->privacy_toggle), v);
	if (ctx->privacy_switch != NULL)
		adw_switch_row_set_active(ADW_SWITCH_ROW(ctx->privacy_switch), v);
	ctx->privacy_updating = FALSE;
}

static int
field_of(gpointer object)
{
	return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(object), "field"));
}

// Changing the overlay monitor needs a restart (a live layer-surface output change
// crashes COSMIC). Show a clear message and quit cleanly instead of crashing; the
// app relaunches reliably and picks up the new monitor at creation.
static void
on_monitor_restart_response(AdwMessageDialog *dlg, const char *response, gpointer user_data)
{
	(void)dlg;
	(void)response;
	(void)user_data;
	GApplication *app = g_application_get_default();
	if (app != NULL)
		g_application_quit(app);
}

static void
prompt_monitor_restart(GtkWidget *any_child)
{
	GtkRoot *root = gtk_widget_get_root(any_child);
	GtkWindow *parent = GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;
	// The button only CLOSES the app — it does not relaunch. Word it honestly and
	// reassure with a green check that closing here is the expected, safe action;
	// the new monitor is picked up when the owner opens the app again.
	GtkWidget *dlg = adw_message_dialog_new(
		parent, _("Close to switch monitor"),
		_("The overlay moves to the new monitor the next time you open the app. "
		  "Your settings are already saved — closing now is safe."));

	GtkWidget *check = gtk_image_new_from_icon_name("object-select-symbolic");
	gtk_image_set_pixel_size(GTK_IMAGE(check), 44);
	gtk_widget_add_css_class(check, "success");
	gtk_widget_set_margin_top(check, 6);
	gtk_widget_set_margin_bottom(check, 2);
	adw_message_dialog_set_extra_child(ADW_MESSAGE_DIALOG(dlg), check);

	adw_message_dialog_add_response(ADW_MESSAGE_DIALOG(dlg), "ok", _("Close"));
	adw_message_dialog_set_response_appearance(ADW_MESSAGE_DIALOG(dlg), "ok",
						   ADW_RESPONSE_SUGGESTED);
	adw_message_dialog_set_default_response(ADW_MESSAGE_DIALOG(dlg), "ok");
	g_signal_connect(dlg, "response", G_CALLBACK(on_monitor_restart_response), NULL);
	gtk_window_present(GTK_WINDOW(dlg));
}

// Changing the language re-translates the settings window live, but the on-screen
// overlay (a mapped layer-shell surface with its own translated labels and text
// direction) only switches language on a fresh start — recreating that surface live
// risks the same COSMIC layer-shell crash as a monitor switch. So prompt a clean
// restart, exactly like the monitor flow (same dialog, check icon and Close-quits).
static void
prompt_language_restart(GtkWindow *parent)
{
	GtkWidget *dlg = adw_message_dialog_new(
		parent, _("Close to switch language"),
		_("The overlay switches to the new language the next time you open the app. "
		  "Your settings are already saved — closing now is safe."));

	GtkWidget *check = gtk_image_new_from_icon_name("object-select-symbolic");
	gtk_image_set_pixel_size(GTK_IMAGE(check), 44);
	gtk_widget_add_css_class(check, "success");
	gtk_widget_set_margin_top(check, 6);
	gtk_widget_set_margin_bottom(check, 2);
	adw_message_dialog_set_extra_child(ADW_MESSAGE_DIALOG(dlg), check);

	adw_message_dialog_add_response(ADW_MESSAGE_DIALOG(dlg), "ok", _("Close"));
	adw_message_dialog_set_response_appearance(ADW_MESSAGE_DIALOG(dlg), "ok",
						   ADW_RESPONSE_SUGGESTED);
	adw_message_dialog_set_default_response(ADW_MESSAGE_DIALOG(dlg), "ok");
	g_signal_connect(dlg, "response", G_CALLBACK(on_monitor_restart_response), NULL);
	gtk_window_present(GTK_WINDOW(dlg));
}

// --- change handlers -------------------------------------------------------

static void
on_switch(GObject *row, GParamSpec *pspec, gpointer user_data)
{
	(void)pspec;
	WinCtx *ctx = user_data;
	gboolean v = adw_switch_row_get_active(ADW_SWITCH_ROW(row));
	switch (field_of(row)) {
	case F_SHOW_KEYBOARD:
		ctx->settings->show_keyboard = v;
		break;
	case F_SHOW_MOUSE:
		ctx->settings->show_mouse = v;
		break;
	case F_SHOW_SHIFT:
		ctx->settings->show_shift_separately = v;
		break;
	case F_DRAW_BORDER:
		ctx->settings->draw_border = v;
		break;
	case F_KEEP:
		ctx->settings->keep_forever = v;
		if (ctx->timeout_row != NULL)
			gtk_widget_set_sensitive(ctx->timeout_row, !v);
		break;
	default:
		break;
	}
	keysclicks_settings_emit_changed(ctx->settings);
}

static void
on_combo(GObject *row, GParamSpec *pspec, gpointer user_data)
{
	(void)pspec;
	WinCtx *ctx = user_data;
	guint sel = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
	switch (field_of(row)) {
	case F_MODE:
		ctx->settings->mode = (KeysClicksMode)sel;
		break;
	case F_ALIGN:
		ctx->settings->align = (KeysClicksAlign)sel;
		break;
	case F_EDGE:
		ctx->settings->edge = (KeysClicksEdge)sel;
		break;
	case F_MONITOR: {
		// Index 0 is "Automatic"; the rest map to the stored connector list.
		const char *const *connectors = g_object_get_data(row, "connectors");
		if (sel == 0 || connectors == NULL)
			keysclicks_settings_set_monitor(ctx->settings, "");
		else
			keysclicks_settings_set_monitor(ctx->settings, connectors[sel - 1]);
		keysclicks_settings_emit_changed(ctx->settings); // persist the new monitor
		prompt_monitor_restart(GTK_WIDGET(row)); // then restart to apply (no crash)
		return;
	}
	default:
		break;
	}
	keysclicks_settings_emit_changed(ctx->settings);
}

// Change the UI language, persist it, and rebuild the window so every label is
// re-translated live. Index 0 is "System (default)" (settings->language = "").
static void
on_language(GObject *row, GParamSpec *pspec, gpointer user_data)
{
	(void)pspec;
	WinCtx *ctx = user_data;
	if (ctx->rebuilding)
		return;

	guint sel = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
	const char *code = "";
	if (sel > 0) {
		guint n;
		const KeysClicksLanguage *langs = keysclicks_languages(&n);
		if (sel - 1 < n)
			code = langs[sel - 1].code;
	}

	g_free(ctx->settings->language);
	ctx->settings->language = g_strdup(code);
	keysclicks_settings_emit_changed(ctx->settings);
	keysclicks_i18n_apply_language(code);

	// Mirror the layout for RTL languages (Arabic/Persian/Urdu) before rebuilding.
	gtk_widget_set_default_direction(
		keysclicks_language_is_rtl(keysclicks_language_resolve(code))
			? GTK_TEXT_DIR_RTL
			: GTK_TEXT_DIR_LTR);

	// The combo that emitted this signal lives inside the content we replace;
	// GObject holds a ref on it for the duration of the emission, so rebuilding
	// synchronously here is safe.
	populate_content(ctx);

	// The settings window is re-translated live above, but the on-screen overlay only
	// switches language on a fresh start (recreating a mapped layer-shell surface risks
	// a COSMIC crash) — prompt a clean restart, like the monitor switch. Parent on the
	// window (survives the rebuild), not the now-replaced combo row.
	prompt_language_restart(GTK_WINDOW(ctx->window));
}

// Change the overlay keyboard layout (display only), persist it, and let the settings
// listener in main.c rebuild the keymap live. Index 0 is "System (default)"
// (settings->keyboard_layout = ""). No window rebuild — this changes no UI strings.
static void
on_keyboard_layout(GObject *row, GParamSpec *pspec, gpointer user_data)
{
	(void)pspec;
	WinCtx *ctx = user_data;
	if (ctx->rebuilding)
		return;

	guint sel = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
	// Two segments after "System (default)" (row 0): first the language rows (native
	// name → its xkb layout), then the full xkb registry. nlang marks the boundary.
	guint nlang = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "nlang"));
	const char *layout = "";
	const char *variant = "";
	if (sel > 0 && sel <= nlang) {
		guint n;
		const KeysClicksLanguage *langs = keysclicks_languages(&n);
		const char *v = NULL;
		const char *l = keysclicks_language_xkb_layout(langs[sel - 1].code, &v);
		if (l != NULL) {
			layout = l;
			variant = v != NULL ? v : "";
		}
	} else if (sel > nlang) {
		guint n;
		const KeysClicksLayout *list = keysclicks_layouts(&n);
		guint xi = sel - 1 - nlang;
		if (xi < n) {
			layout = list[xi].layout;
			variant = list[xi].variant != NULL ? list[xi].variant : "";
		}
	}

	g_free(ctx->settings->keyboard_layout);
	ctx->settings->keyboard_layout = g_strdup(layout);
	g_free(ctx->settings->keyboard_variant);
	ctx->settings->keyboard_variant = g_strdup(variant);
	keysclicks_settings_emit_changed(ctx->settings);
}

static void
on_spin(GtkAdjustment *adj, gpointer user_data)
{
	WinCtx *ctx = user_data;
	double v = gtk_adjustment_get_value(adj);
	switch (field_of(adj)) {
	case F_FONT_SIZE:
		ctx->settings->font_size = (int)v;
		break;
	case F_MARGIN_X:
		ctx->settings->margin_x = (int)v;
		break;
	case F_MARGIN_Y:
		ctx->settings->margin_y = (int)v;
		break;
	case F_TIMEOUT:
		ctx->settings->timeout_ms = (int)v;
		break;
	case F_BAR_OPACITY:
		ctx->settings->bar_opacity = v / 100.0; // control is a percentage
		break;
	default:
		break;
	}
	keysclicks_settings_emit_changed(ctx->settings);
}

// --- row builders ----------------------------------------------------------

static AdwPreferencesGroup *
add_group(AdwPreferencesPage *page, const char *title)
{
	AdwPreferencesGroup *group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
	adw_preferences_group_set_title(group, title);
	adw_preferences_page_add(page, group);
	return group;
}

static GtkWidget *
add_switch(AdwPreferencesGroup *group, WinCtx *ctx, const char *title, FieldId field,
	   gboolean value)
{
	GtkWidget *row = adw_switch_row_new();
	g_object_set(row, "title", title, NULL);
	adw_switch_row_set_active(ADW_SWITCH_ROW(row), value);
	g_object_set_data(G_OBJECT(row), "field", GINT_TO_POINTER(field));
	g_signal_connect(row, "notify::active", G_CALLBACK(on_switch), ctx);
	adw_preferences_group_add(group, row);
	return row;
}

static GtkWidget *
add_combo(AdwPreferencesGroup *group, WinCtx *ctx, const char *title, FieldId field,
	  const char *const *items, guint selected)
{
	GtkWidget *row = adw_combo_row_new();
	g_object_set(row, "title", title, NULL);
	GtkStringList *model = gtk_string_list_new(items);
	adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(model));
	adw_combo_row_set_selected(ADW_COMBO_ROW(row), selected);
	g_object_set_data(G_OBJECT(row), "field", GINT_TO_POINTER(field));
	// Connect after setting the initial value so setup does not fire a save.
	g_signal_connect(row, "notify::selected", G_CALLBACK(on_combo), ctx);
	adw_preferences_group_add(group, row);
	return row;
}

// Find the GtkSpinButton nested inside an AdwSpinRow (depth-first).
static GtkSpinButton *
find_spin_button(GtkWidget *w)
{
	if (GTK_IS_SPIN_BUTTON(w))
		return GTK_SPIN_BUTTON(w);
	for (GtkWidget *c = gtk_widget_get_first_child(w); c != NULL;
	     c = gtk_widget_get_next_sibling(c)) {
		GtkSpinButton *sb = find_spin_button(c);
		if (sb != NULL)
			return sb;
	}
	return NULL;
}

// Commit a typed value when the field loses focus, so the number applies on
// unfocus (click/tab away) — not only when Enter is pressed.
static void
on_spin_focus_leave(GtkEventControllerFocus *ctrl, gpointer user_data)
{
	(void)ctrl;
	GtkSpinButton *sb = find_spin_button(GTK_WIDGET(user_data));
	if (sb != NULL)
		gtk_spin_button_update(sb); // parse the entry text into the adjustment
}

// Reject non-numeric input in a number field. Digits and a leading minus pass;
// anything else cancels the insertion, so a typed or pasted letter never lands in the
// field. This is a belt over AdwSpinRow's numeric mode, which on some libadwaita
// versions still lets letters through.
static void
on_numeric_insert(GtkEditable *editable, const char *text, int length, int *position,
		  gpointer user_data)
{
	(void)position;
	(void)user_data;
	for (int i = 0; i < length; i++) {
		if (!g_ascii_isdigit((guchar)text[i]) && text[i] != '-') {
			g_signal_stop_emission_by_name(editable, "insert-text");
			return;
		}
	}
}

static GtkWidget *
add_spin(AdwPreferencesGroup *group, WinCtx *ctx, const char *title, FieldId field,
	 double min, double max, double step, double value)
{
	GtkWidget *row = adw_spin_row_new_with_range(min, max, step);
	g_object_set(row, "title", title, NULL);
	// Numeric mode makes the embedded spin button reject non-digit input, so
	// typed letters are ignored instead of being inserted.
	adw_spin_row_set_numeric(ADW_SPIN_ROW(row), TRUE);
	GtkAdjustment *adj = adw_spin_row_get_adjustment(ADW_SPIN_ROW(row));
	gtk_adjustment_set_value(adj, value);
	g_object_set_data(G_OBJECT(adj), "field", GINT_TO_POINTER(field));
	g_signal_connect(adj, "value-changed", G_CALLBACK(on_spin), ctx);

	// Apply the typed value on unfocus, not only on Enter.
	GtkEventController *focus = gtk_event_controller_focus_new();
	g_signal_connect(focus, "leave", G_CALLBACK(on_spin_focus_leave), row);
	gtk_widget_add_controller(row, focus);

	// Keep the number entry (and its green pill) tight: fixed at 5 characters and
	// NOT allowed to expand, so it sits next to the +/- buttons instead of eating
	// the whole row.
	GtkSpinButton *sb = find_spin_button(row);
	if (sb != NULL) {
		gtk_spin_button_set_numeric(sb, TRUE); // reject non-digits at the embedded button
		// Belt over numeric mode: filter the editable delegate so a typed/pasted letter
		// never lands here even if this libadwaita's numeric mode lets it through.
		GtkEditable *deleg = gtk_editable_get_delegate(GTK_EDITABLE(sb));
		if (deleg != NULL)
			g_signal_connect(deleg, "insert-text", G_CALLBACK(on_numeric_insert), NULL);
		gtk_editable_set_width_chars(GTK_EDITABLE(sb), 5);
		gtk_editable_set_max_width_chars(GTK_EDITABLE(sb), 5);
		// hexpand TRUE gives the control a full-width cell; halign END then draws
		// it at its natural (5-char) size flush against the RIGHT edge, so the
		// digits+/- sit on the right instead of stretching or drifting left.
		gtk_widget_set_hexpand(GTK_WIDGET(sb), FALSE);
		gtk_widget_set_halign(GTK_WIDGET(sb), GTK_ALIGN_END);
	}

	adw_preferences_group_add(group, row);
	return row;
}

// Keep the mouse wheel from changing a margin slider — it should scroll the
// settings page instead (the slider is drag-only, per owner request). Consumes
// the scroll in the CAPTURE phase before GtkRange reacts, and forwards it to the
// enclosing scrolled page so scrolling over the slider still moves the list.
static gboolean
on_slider_scroll(GtkEventControllerScroll *ctrl, double dx, double dy, gpointer user_data)
{
	(void)dx;
	(void)user_data;
	GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl));
	GtkWidget *sw = gtk_widget_get_ancestor(w, GTK_TYPE_SCROLLED_WINDOW);
	if (sw != NULL) {
		GtkAdjustment *adj =
			gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw));
		double step = gtk_adjustment_get_step_increment(adj);
		if (step <= 0.0)
			step = 40.0;
		double v = gtk_adjustment_get_value(adj) + dy * step * 3.0;
		double hi = gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj);
		gtk_adjustment_set_value(adj, CLAMP(v, gtk_adjustment_get_lower(adj), hi));
	}
	return GDK_EVENT_STOP; // never let the wheel move the slider
}

// A horizontal slider row (GtkScale in an action row) — used where a drag is
// nicer than a spin button. Feeds the same on_spin handler via its adjustment.
static GtkWidget *
add_slider(AdwPreferencesGroup *group, WinCtx *ctx, const char *title, FieldId field,
	   double min, double max, double step, double value)
{
	GtkWidget *row = adw_action_row_new();
	g_object_set(row, "title", title, NULL);

	GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, min, max, step);
	gtk_range_set_value(GTK_RANGE(scale), value);
	gtk_widget_set_hexpand(scale, TRUE);
	gtk_widget_set_size_request(scale, 220, -1);
	gtk_widget_set_valign(scale, GTK_ALIGN_CENTER);
	gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE); // show the live number
	gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_LEFT);

	GtkAdjustment *adj = gtk_range_get_adjustment(GTK_RANGE(scale));
	g_object_set_data(G_OBJECT(adj), "field", GINT_TO_POINTER(field));
	g_signal_connect(adj, "value-changed", G_CALLBACK(on_spin), ctx);

	// Wheel scrolls the page, not the slider (drag-only).
	GtkEventController *scroll =
		gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
	gtk_event_controller_set_propagation_phase(scroll, GTK_PHASE_CAPTURE);
	g_signal_connect(scroll, "scroll", G_CALLBACK(on_slider_scroll), NULL);
	gtk_widget_add_controller(scale, scroll);

	adw_action_row_add_suffix(ADW_ACTION_ROW(row), scale);
	adw_preferences_group_add(group, row);
	return row;
}

// Language selector: "System (default)" followed by every supported language in
// its own native name. The native names are endonyms and are never translated.
static void
add_language_combo(AdwPreferencesGroup *group, WinCtx *ctx)
{
	GtkWidget *row = adw_combo_row_new();
	g_object_set(row, "title", _("Language"), NULL);
	adw_combo_row_set_enable_search(ADW_COMBO_ROW(row), TRUE); // 31 languages — make them searchable
	// Same requirement as the keyboard-layout picker: the search box needs an expression
	// that yields each row's text, otherwise typing filters nothing (AdwComboRow).
	adw_combo_row_set_expression(
		ADW_COMBO_ROW(row),
		gtk_property_expression_new(GTK_TYPE_STRING_OBJECT, NULL, "string"));

	guint n;
	const KeysClicksLanguage *langs = keysclicks_languages(&n);
	GPtrArray *items = g_ptr_array_new();
	g_ptr_array_add(items, g_strdup(_("System (default)")));
	for (guint i = 0; i < n; i++)
		g_ptr_array_add(items, g_strdup(langs[i].native_name));
	g_ptr_array_add(items, NULL);
	char **strv = (char **)g_ptr_array_free(items, FALSE);
	GtkStringList *model = gtk_string_list_new((const char *const *)strv);
	g_strfreev(strv);
	adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(model));

	guint selected = 0; // "System (default)"
	const char *cur = ctx->settings->language;
	if (cur != NULL && *cur != '\0') {
		int idx = keysclicks_language_index(cur);
		if (idx >= 0)
			selected = (guint)idx + 1;
	}
	adw_combo_row_set_selected(ADW_COMBO_ROW(row), selected);
	g_signal_connect(row, "notify::selected", G_CALLBACK(on_language), ctx);
	adw_preferences_group_add(group, row);
}

// Overlay keyboard-layout selector, shown right under the UI-language picker. Unlike the
// language picker it lists the FULL xkb layout set (hundreds of entries — deliberately
// richer than the UI languages), with search enabled; picking one changes which layout
// the overlay uses to turn keycodes into symbols (display only, independent of the UI
// language). "System (default)" (row 0) keeps the pre-feature behaviour.
static void
add_keyboard_layout_combo(AdwPreferencesGroup *group, WinCtx *ctx)
{
	GtkWidget *row = adw_combo_row_new();
	g_object_set(row, "title", _("Keyboard layout"), NULL);
	adw_combo_row_set_enable_search(ADW_COMBO_ROW(row), TRUE); // the list is long
	// The model holds GtkStringObjects; the search entry needs an expression that yields
	// each row's text to match against. Without it the popup shows a search box but typing
	// filters nothing (and the 590-item list is unusable). Required by AdwComboRow.
	adw_combo_row_set_expression(
		ADW_COMBO_ROW(row),
		gtk_property_expression_new(GTK_TYPE_STRING_OBJECT, NULL, "string"));

	// Two segments after "System (default)": first the supported LANGUAGES by their
	// native name (mapped to their xkb layout) — restored so a per-language keyboard
	// display never silently disappears again (guarded by tests/unit/test_layout_map.c);
	// then the full xkb registry from libxkbregistry (deliberately richer). Independent
	// of the UI-language picker above.
	guint nlang, nxkb;
	const KeysClicksLanguage *langs = keysclicks_languages(&nlang);
	const KeysClicksLayout *list = keysclicks_layouts(&nxkb);
	GPtrArray *items = g_ptr_array_new();
	g_ptr_array_add(items, g_strdup(_("System (default)")));
	for (guint i = 0; i < nlang; i++)
		g_ptr_array_add(items, g_strdup(langs[i].native_name));
	for (guint i = 0; i < nxkb; i++)
		g_ptr_array_add(items, g_strdup(list[i].name));
	g_ptr_array_add(items, NULL);
	char **strv = (char **)g_ptr_array_free(items, FALSE);
	GtkStringList *model = gtk_string_list_new((const char *const *)strv);
	g_strfreev(strv);
	adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(model));
	g_object_set_data(G_OBJECT(row), "nlang", GUINT_TO_POINTER(nlang));

	// Initial selection: prefer the LANGUAGE row whose xkb matches the saved layout,
	// otherwise the matching xkb-registry row.
	guint selected = 0; // "System (default)"
	const char *cur = ctx->settings->keyboard_layout;
	const char *curv = ctx->settings->keyboard_variant;
	if (cur != NULL && *cur != '\0') {
		int li = -1;
		for (guint i = 0; i < nlang; i++) {
			const char *v = NULL;
			const char *l = keysclicks_language_xkb_layout(langs[i].code, &v);
			if (l != NULL && g_strcmp0(l, cur) == 0 &&
			    g_strcmp0(v != NULL ? v : "", curv != NULL ? curv : "") == 0) {
				li = (int)i;
				break;
			}
		}
		if (li >= 0) {
			selected = 1 + (guint)li;
		} else {
			int idx = keysclicks_layouts_index_of(cur, curv);
			if (idx >= 0)
				selected = 1 + nlang + (guint)idx;
		}
	}
	adw_combo_row_set_selected(ADW_COMBO_ROW(row), selected);
	g_signal_connect(row, "notify::selected", G_CALLBACK(on_keyboard_layout), ctx);
	adw_preferences_group_add(group, row);
}

// Build the monitor combo from the live monitor list; index 0 is "Automatic".
static void
add_monitor_combo(AdwPreferencesGroup *group, WinCtx *ctx)
{
	GListModel *monitors = gdk_display_get_monitors(gdk_display_get_default());
	guint n = g_list_model_get_n_items(monitors);

	GPtrArray *labels = g_ptr_array_new();
	GPtrArray *connectors = g_ptr_array_new();
	g_ptr_array_add(labels, g_strdup(_("Automatic")));
	for (guint i = 0; i < n; i++) {
		GdkMonitor *m = g_list_model_get_item(monitors, i);
		const char *c = gdk_monitor_get_connector(m);
		g_ptr_array_add(connectors, g_strdup(c != NULL ? c : ""));
		g_ptr_array_add(labels, c != NULL ? g_strdup(c)
						  : g_strdup_printf(_("Monitor %u"), i + 1));
		g_object_unref(m);
	}
	g_ptr_array_add(labels, NULL);
	g_ptr_array_add(connectors, NULL);
	char **label_strv = (char **)g_ptr_array_free(labels, FALSE);
	char **connector_strv = (char **)g_ptr_array_free(connectors, FALSE);

	guint selected = 0;
	const char *want = ctx->settings->monitor_connector;
	if (want != NULL && *want != '\0') {
		for (guint i = 0; connector_strv[i] != NULL; i++) {
			if (g_strcmp0(connector_strv[i], want) == 0) {
				selected = i + 1;
				break;
			}
		}
	}

	GtkWidget *row = add_combo(group, ctx, _("Overlay monitor"),
				   F_MONITOR, (const char *const *)label_strv, selected);
	g_object_set_data_full(G_OBJECT(row), "connectors", connector_strv,
			       (GDestroyNotify)g_strfreev);
	g_strfreev(label_strv);
}

// --- public API ------------------------------------------------------------

// Override libadwaita's blue accent with the "calm green" palette.
static void
install_accent_style(void)
{
	static gboolean installed = FALSE;
	if (installed)
		return;
	installed = TRUE;

	GtkCssProvider *provider = gtk_css_provider_new();
	gtk_css_provider_load_from_string(provider, keysclicks_accent_css());
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
						   GTK_STYLE_PROVIDER(provider),
						   GTK_STYLE_PROVIDER_PRIORITY_USER);
	g_object_unref(provider);
}

// Reflect the current pin state in the header button's label/tooltip.
static void
update_pin_button(GtkButton *button)
{
	gboolean pinned = keysclicks_dock_is_pinned();
	gtk_button_set_label(button, pinned ? _("Unpin from dock") : _("Pin to dock"));
	gtk_widget_set_tooltip_text(GTK_WIDGET(button),
				    pinned ? _("Remove this app from the COSMIC dock")
					   : _("Pin this app to the COSMIC dock"));
}

static void
on_pin_clicked(GtkButton *button, gpointer user_data)
{
	(void)user_data;
	gboolean pinned = keysclicks_dock_is_pinned();
	GError *error = NULL;
	if (!keysclicks_dock_set_pinned(!pinned, &error)) {
		g_warning("keysclicks: could not update COSMIC dock favourites: %s",
			  error != NULL ? error->message : "unknown error");
		g_clear_error(&error);
	}
	update_pin_button(button);
}

// Open the OS keyboard settings so the user can bind a global/compositor shortcut
// (Wayland has no client-side global hotkeys, so this must be done in the OS).
static void
on_open_os_shortcuts(GtkButton *button, gpointer user_data)
{
	(void)button;
	(void)user_data;
	char *argv[] = { (char *)"cosmic-settings", (char *)"input", NULL };
	if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL)) {
		char *fallback[] = { (char *)"cosmic-settings", NULL };
		g_spawn_async(NULL, fallback, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL,
			      NULL);
	}
}

// Human-readable rendering of a panic-chord string ("29+42+35" evdev codes) using
// friendly evdev names with the "KEY_" prefix stripped ("LEFTCTRL + LEFTSHIFT + H").
static char *
chord_display(const char *panic_chord)
{
	if (panic_chord == NULL || *panic_chord == '\0')
		return g_strdup(_("None — click Set, then hold a shortcut (e.g. Ctrl+Shift+H)"));
	GString *s = g_string_new(NULL);
	char **parts = g_strsplit(panic_chord, "+", -1);
	for (int i = 0; parts[i] != NULL; i++) {
		int code = (int)g_ascii_strtoll(g_strstrip(parts[i]), NULL, 10);
		if (code <= 0)
			continue;
		const char *name = libevdev_event_code_get_name(EV_KEY, code);
		if (s->len)
			g_string_append(s, " + ");
		if (name != NULL && g_str_has_prefix(name, "KEY_"))
			g_string_append(s, name + 4);
		else if (name != NULL)
			g_string_append(s, name);
		else
			g_string_append_printf(s, "%d", code);
	}
	g_strfreev(parts);
	return g_string_free(s, FALSE);
}

static void
refresh_chord_row(WinCtx *ctx)
{
	char *disp = chord_display(ctx->settings->panic_chord);
	adw_action_row_set_subtitle(ADW_ACTION_ROW(ctx->chord_row), disp);
	g_free(disp);
	gtk_button_set_label(GTK_BUTTON(ctx->chord_button),
			     ctx->chord_capturing ? _("Press keys…") : _("Set"));
	// Green while actively capturing (owner request): reuse the suggested-action
	// deep-green fill so the button clearly reads as "armed / waiting for keys".
	if (ctx->chord_capturing)
		gtk_widget_add_css_class(ctx->chord_button, "suggested-action");
	else
		gtk_widget_remove_css_class(ctx->chord_button, "suggested-action");
}

static gboolean
on_chord_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
		     GdkModifierType state, gpointer user_data)
{
	(void)ctrl;
	(void)state;
	WinCtx *ctx = user_data;
	if (!ctx->chord_capturing)
		return GDK_EVENT_PROPAGATE;
	if (keyval == GDK_KEY_Escape) { // cancel, keep the previous chord
		ctx->chord_capturing = FALSE;
		g_array_set_size(ctx->chord_captured, 0);
		refresh_chord_row(ctx);
		return GDK_EVENT_STOP;
	}
	guint code = keycode >= 8 ? keycode - 8 : keycode; // hardware keycode = evdev + 8
	for (guint i = 0; i < ctx->chord_captured->len; i++)
		if (g_array_index(ctx->chord_captured, guint, i) == code)
			return GDK_EVENT_STOP; // already held
	g_array_append_val(ctx->chord_captured, code);

	GString *s = g_string_new(_("Hold the shortcut…  "));
	for (guint i = 0; i < ctx->chord_captured->len; i++) {
		guint c = g_array_index(ctx->chord_captured, guint, i);
		const char *name = libevdev_event_code_get_name(EV_KEY, c);
		if (i)
			g_string_append(s, " + ");
		g_string_append(s, (name && g_str_has_prefix(name, "KEY_")) ? name + 4
					   : (name ? name : "?"));
	}
	adw_action_row_set_subtitle(ADW_ACTION_ROW(ctx->chord_row), s->str);
	g_string_free(s, TRUE);
	return GDK_EVENT_STOP;
}

static void
on_chord_key_released(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
		      GdkModifierType state, gpointer user_data)
{
	(void)ctrl;
	(void)keyval;
	(void)keycode;
	(void)state;
	WinCtx *ctx = user_data;
	if (!ctx->chord_capturing || ctx->chord_captured->len == 0)
		return;
	// First release finalises: the accumulated held keys are the chord.
	GString *s = g_string_new(NULL);
	for (guint i = 0; i < ctx->chord_captured->len; i++) {
		if (s->len)
			g_string_append_c(s, '+');
		g_string_append_printf(s, "%u", g_array_index(ctx->chord_captured, guint, i));
	}
	g_free(ctx->settings->panic_chord);
	ctx->settings->panic_chord = g_string_free(s, FALSE);
	ctx->chord_capturing = FALSE;
	g_array_set_size(ctx->chord_captured, 0);
	keysclicks_settings_emit_changed(ctx->settings); // main.c re-parses the chord
	refresh_chord_row(ctx);
}

static void
on_chord_set_clicked(GtkButton *button, gpointer user_data)
{
	WinCtx *ctx = user_data;
	ctx->chord_capturing = TRUE;
	g_array_set_size(ctx->chord_captured, 0);
	adw_action_row_set_subtitle(
		ADW_ACTION_ROW(ctx->chord_row),
		_("Hold the shortcut, then release to save. Esc cancels."));
	gtk_button_set_label(button, _("Press keys…"));
	gtk_widget_add_css_class(GTK_WIDGET(button), "suggested-action"); // green: armed
	gtk_widget_grab_focus(GTK_WIDGET(button));
}

static void
win_ctx_free(gpointer data)
{
	WinCtx *ctx = data;
	// Drop the privacy-sync listener first: settings outlive the window, so a
	// stale listener would call into freed ctx on the next emit.
	keysclicks_settings_remove_listener(ctx->settings, on_settings_privacy_sync, ctx);
	if (ctx->chord_captured != NULL)
		g_array_free(ctx->chord_captured, TRUE);
	g_free(ctx);
}

// Click-to-defocus: clicking empty page space grabs focus to the page, releasing any
// focused number entry (which commits its value on focus-leave) — so you are never
// stuck typing into a number field you can't click out of.
static void
on_page_defocus_click(GtkGestureClick *gesture, int n_press, double x, double y,
		      gpointer user_data)
{
	(void)gesture;
	(void)n_press;
	(void)x;
	(void)y;
	gtk_widget_grab_focus(GTK_WIDGET(user_data));
}

// Build (or rebuild) the whole window content in the current UI language. Called
// once at creation and again whenever the language changes, so every string is
// re-run through _(). Reassigns the per-widget pointers in ctx.
static void
populate_content(WinCtx *ctx)
{
	ctx->rebuilding = TRUE;

	GtkWidget *window = ctx->window;
	KeysClicksSettings *settings = ctx->settings;

	gtk_window_set_title(GTK_WINDOW(window),
			     _("On-Screen Keyboard & Mouse-Click Visualizer — Always-On-Top Overlay for Linux/Wayland (COSMIC, sway, Hyprland, KDE Plasma, wlroots)"));

	// A plain toplevel with an AdwHeaderBar exposes the full window controls
	// (minimise, maximise, close) on the right and stays resizable/maximisable.
	GtkWidget *header = adw_header_bar_new();
	adw_header_bar_set_decoration_layout(ADW_HEADER_BAR(header),
					     ":minimize,maximize,close");

	// Show the running version in the header (subtitle) so it is always obvious
	// which build is on screen. KEYSCLICKS_BUILD auto-increments per commit.
	char *version = g_strdup_printf("v%s · build %s", KEYSCLICKS_VERSION,
					KEYSCLICKS_BUILD);
	GtkWidget *title_widget =
		adw_window_title_new(_("On-Screen Keyboard & Mouse-Click Visualizer — Always-On-Top Overlay for Linux/Wayland (COSMIC, sway, Hyprland, KDE Plasma, wlroots)"), version);
	adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), title_widget);
	g_free(version);

	// Privacy master toggle, prominent on the left of the header.
	GtkWidget *privacy_toggle = gtk_toggle_button_new_with_label(_("Hide keys"));
	gtk_widget_set_tooltip_text(
		privacy_toggle,
		_("Hide keystrokes (privacy): show \xe2\x80\xa2 instead of keys"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(privacy_toggle),
				     settings->privacy_hide_keys);
	g_signal_connect(privacy_toggle, "toggled", G_CALLBACK(on_privacy_toggle), ctx);
	adw_header_bar_pack_start(ADW_HEADER_BAR(header), privacy_toggle);
	ctx->privacy_toggle = privacy_toggle;

	// "Report a bug" button (Ionicons bug-outline), top-right.
	adw_header_bar_pack_end(ADW_HEADER_BAR(header),
				keysclicks_feedback_button_new(settings));

	// COSMIC-only pin/unpin toggle, next to the window controls.
	if (keysclicks_dock_available()) {
		GtkWidget *pin_button = gtk_button_new();
		update_pin_button(GTK_BUTTON(pin_button));
		g_signal_connect(pin_button, "clicked", G_CALLBACK(on_pin_clicked), NULL);
		adw_header_bar_pack_end(ADW_HEADER_BAR(header), pin_button);
	}

	AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());

	// Click-to-defocus: focusable page + a click gesture that grabs focus to it, so a
	// click on empty page space releases a focused number entry (commits it on leave).
	gtk_widget_set_focusable(GTK_WIDGET(page), TRUE);
	GtkGesture *page_defocus = gtk_gesture_click_new();
	g_signal_connect(page_defocus, "pressed", G_CALLBACK(on_page_defocus_click), page);
	gtk_widget_add_controller(GTK_WIDGET(page), GTK_EVENT_CONTROLLER(page_defocus));

	GtkWidget *toolbar = adw_toolbar_view_new();
	adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
	adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), GTK_WIDGET(page));
	adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), toolbar);

	// Language --------------------------------------------------------------
	AdwPreferencesGroup *g_lang = add_group(page, _("Language"));
	add_language_combo(g_lang, ctx);
	add_keyboard_layout_combo(g_lang, ctx); // overlay display layout, right below

	// Privacy ---------------------------------------------------------------
	AdwPreferencesGroup *g_privacy = add_group(page, _("Privacy"));
	GtkWidget *privacy_switch = adw_switch_row_new();
	g_object_set(privacy_switch, "title", _("Hide keystrokes (privacy)"), NULL);
	adw_switch_row_set_active(ADW_SWITCH_ROW(privacy_switch),
				  settings->privacy_hide_keys);
	g_signal_connect(privacy_switch, "notify::active",
			 G_CALLBACK(on_privacy_switch), ctx);
	adw_preferences_group_add(g_privacy, privacy_switch);
	ctx->privacy_switch = privacy_switch;

	GtkWidget *provider_row = adw_action_row_new();
	g_object_set(provider_row, "title", _("Automatic password hiding"), NULL);
	// Flag this as a PRO, still-in-development capability with a small green badge.
	GtkWidget *pro_badge = gtk_label_new("PRO");
	gtk_widget_add_css_class(pro_badge, "pro-badge");
	gtk_widget_set_valign(pro_badge, GTK_ALIGN_CENTER);
	adw_action_row_add_prefix(ADW_ACTION_ROW(provider_row), pro_badge);
	char *provider_status =
		keysclicks_privacy_loaded(ctx->privacy)
			? g_strdup(_("On (PRO) — passwords are hidden automatically in password "
				     "fields. This module is still in development."))
			: g_strdup(_("PRO — in development. Automatic hiding ships with the PRO "
				     "add-on; the manual \"Hide keys\" toggle/shortcut works now."));
	adw_action_row_set_subtitle(ADW_ACTION_ROW(provider_row), provider_status);
	adw_action_row_set_subtitle_lines(ADW_ACTION_ROW(provider_row), 0);
	g_free(provider_status);
	adw_preferences_group_add(g_privacy, provider_row);

	// Interactive "Hide keys" shortcut: click Set and hold a chord.
	ctx->chord_row = adw_action_row_new();
	g_object_set(ctx->chord_row, "title", _("Hide-keys shortcut"), NULL);
	adw_action_row_set_subtitle_lines(ADW_ACTION_ROW(ctx->chord_row), 0);
	ctx->chord_button = gtk_button_new_with_label(_("Set"));
	gtk_widget_set_valign(ctx->chord_button, GTK_ALIGN_CENTER);
	gtk_widget_set_tooltip_text(ctx->chord_button,
				    _("Click, then hold a key combination to assign it"));
	g_signal_connect(ctx->chord_button, "clicked", G_CALLBACK(on_chord_set_clicked), ctx);
	adw_action_row_add_suffix(ADW_ACTION_ROW(ctx->chord_row), ctx->chord_button);
	adw_preferences_group_add(g_privacy, ctx->chord_row);
	ctx->chord_capturing = FALSE;
	refresh_chord_row(ctx);

	// Direct link to the OS keyboard settings.
	GtkWidget *os_row = adw_action_row_new();
	g_object_set(os_row, "title", _("System keyboard shortcuts"), NULL);
	adw_action_row_set_subtitle(
		ADW_ACTION_ROW(os_row),
		_("Global shortcuts are set in the OS — open COSMIC → Keyboard → Shortcuts"));
	adw_action_row_set_subtitle_lines(ADW_ACTION_ROW(os_row), 0);
	GtkWidget *os_button = gtk_button_new_with_label(_("Open settings"));
	gtk_widget_set_valign(os_button, GTK_ALIGN_CENTER);
	g_signal_connect(os_button, "clicked", G_CALLBACK(on_open_os_shortcuts), NULL);
	adw_action_row_add_suffix(ADW_ACTION_ROW(os_row), os_button);
	adw_preferences_group_add(g_privacy, os_row);

	// Keys & clicks display -------------------------------------------------
	AdwPreferencesGroup *g_display = add_group(page, _("Keys and clicks display"));
	const char *modes[] = { _("Composed"), _("Raw"), _("Compact"), NULL };
	add_combo(g_display, ctx, _("Label style"), F_MODE, modes, settings->mode);
	add_spin(g_display, ctx, _("Font size"), F_FONT_SIZE, 10, 48, 1, settings->font_size);
	add_switch(g_display, ctx, _("Show keyboard"), F_SHOW_KEYBOARD,
		   settings->show_keyboard);
	add_switch(g_display, ctx, _("Show mouse"), F_SHOW_MOUSE, settings->show_mouse);
	add_switch(g_display, ctx, _("Show Shift separately"), F_SHOW_SHIFT,
		   settings->show_shift_separately);
	add_switch(g_display, ctx, _("Draw border"), F_DRAW_BORDER, settings->draw_border);

	// Position & behaviour --------------------------------------------------
	AdwPreferencesGroup *g_pos = add_group(page, _("Position and behaviour"));
	const char *edges[] = { _("Top"), _("Bottom"), NULL };
	add_combo(g_pos, ctx, _("Screen edge"), F_EDGE, edges, settings->edge);
	// Margins are PERCENT (zoom/resolution independent). Horizontal is bidirectional.
	add_slider(g_pos, ctx, _("Horizontal margin (%)"), F_MARGIN_X, -100, 100, 1,
		   settings->margin_x);
	add_slider(g_pos, ctx, _("Vertical margin (%)"), F_MARGIN_Y, 0, 100, 1,
		   settings->margin_y);
	add_monitor_combo(g_pos, ctx);
	add_switch(g_pos, ctx, _("Keep visible (no timeout)"), F_KEEP,
		   settings->keep_forever);
	ctx->timeout_row =
		add_spin(g_pos, ctx, _("Timeout (ms)"), F_TIMEOUT, 200, 10000, 100,
			 settings->timeout_ms);
	gtk_widget_set_sensitive(ctx->timeout_row, !settings->keep_forever);
	add_spin(g_pos, ctx, _("Bar opacity (%)"), F_BAR_OPACITY, 10, 100, 5,
		 settings->bar_opacity * 100.0);

	ctx->rebuilding = FALSE;
}

GtkWidget *
keysclicks_window_new(GtkApplication *app, KeysClicksSettings *settings,
		      KeysClicksPrivacy *privacy)
{
	install_accent_style();

	WinCtx *ctx = g_new0(WinCtx, 1);
	ctx->app = app;
	ctx->settings = settings;
	ctx->privacy = privacy;
	ctx->chord_captured = g_array_new(FALSE, FALSE, sizeof(guint));

	GtkWidget *window = adw_application_window_new(app);
	ctx->window = window;
	gtk_widget_add_css_class(window, "keysclicks-settings"); // dark-green frame
	// Wide enough that the full title and the version subtitle fit in the header
	// without truncation, alongside the "Hide keys" button and the window controls.
	gtk_window_set_default_size(GTK_WINDOW(window), 820, 680);
	gtk_widget_set_size_request(window, 700, -1); // don't shrink below fitting
	gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
	g_object_set_data_full(G_OBJECT(window), "ctx", ctx, win_ctx_free);

	// Key controller for capturing the "Hide keys" shortcut chord (window-level,
	// added once — survives content rebuilds on language change).
	GtkEventController *keyctrl = gtk_event_controller_key_new();
	g_signal_connect(keyctrl, "key-pressed", G_CALLBACK(on_chord_key_pressed), ctx);
	g_signal_connect(keyctrl, "key-released", G_CALLBACK(on_chord_key_released), ctx);
	gtk_widget_add_controller(window, keyctrl);

	// Keep both privacy controls in sync with external privacy_hide_keys changes
	// (the panic-chord hotkey). Added once; removed in win_ctx_free.
	keysclicks_settings_add_listener(settings, on_settings_privacy_sync, ctx);

	populate_content(ctx);

	return window;
}
