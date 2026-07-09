// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger

#include "keysclicks-overlay.h"

#include <glib/gi18n.h>
#include <gtk4-layer-shell.h>

#define KEYSCLICKS_MAX_CHIPS 14 // most chips visible at once

// Per-overlay state, attached to the window.
typedef struct {
	GtkWidget *window;
	GtkWidget *bar;
	KeysClicksSettings *settings;
	GtkCssProvider *provider; // reloaded when appearance settings change

	// Margin-drag preview: last applied margins (to detect a margin change) plus a
	// transient placeholder chip that shows the bar's footprint while dragging.
	int last_margin_x;
	int last_margin_y;
	GtkWidget *preview_chip;
	guint preview_timeout;

	gboolean last_keep; // last keep_forever, to detect turning it OFF
} BarOverlay;

// Remove every chip currently on the bar (except the transient margin preview).
// Chips pushed while "keep forever" was on carry no expiry timeout, so turning it
// off would otherwise leave them stuck on screen forever.
static gboolean flush_empty_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data);
static void
clear_all_chips(BarOverlay *state)
{
	GtkWidget *child = gtk_widget_get_first_child(state->bar);
	while (child != NULL) {
		GtkWidget *next = gtk_widget_get_next_sibling(child);
		if (child != state->preview_chip)
			gtk_box_remove(GTK_BOX(state->bar), child);
		child = next;
	}
	// Force the now-empty bar to commit on COSMIC (idle transparent surfaces keep
	// their last frame otherwise).
	GtkRoot *root = gtk_widget_get_root(state->bar);
	if (root != NULL) {
		int *frames = g_new(int, 1);
		*frames = 4;
		gtk_widget_add_tick_callback(GTK_WIDGET(root), flush_empty_tick, frames, NULL);
	}
}

// Convert a margin expressed as a signed PERCENT (-100..100) into pixels of the
// given extent. Percent keeps margins independent of resolution/zoom: the pixel
// offset is recomputed from the live monitor size on every apply, so changing a
// monitor's scale no longer throws the bar off. Clamped to [-100,100] so the bar
// can never travel more than one full extent. Pure — unit-tested.
int
keysclicks_overlay_percent_to_px(int percent, int extent)
{
	if (percent > 100)
		percent = 100;
	if (percent < -100)
		percent = -100;
	return percent * extent / 100;
}

static BarOverlay *
overlay_state(GtkWidget *window)
{
	return g_object_get_data(G_OBJECT(window), "keysclicks-state");
}

// Make the whole surface click-through by giving it an empty input region, so
// clicks fall through to the windows underneath. Re-applied on realize and
// after every chip change, because GTK can reset it on surface changes.
static void
apply_click_through(GtkWidget *window)
{
	GtkNative *native = gtk_widget_get_native(window);
	if (native == NULL)
		return;
	GdkSurface *surface = gtk_native_get_surface(native);
	if (surface == NULL)
		return;

	cairo_region_t *empty = cairo_region_create();
	gdk_surface_set_input_region(surface, empty);
	cairo_region_destroy(empty);
}

static void
on_realize(GtkWidget *window, gpointer user_data)
{
	(void)user_data;
	apply_click_through(window);
}

// Drives a few frames after the bar goes empty. When the LAST chip is removed the
// bar becomes fully transparent, and on some compositors (COSMIC) a plain redraw of
// an idle layer surface is never committed — the compositor keeps showing the
// previous frame, so the final chip looks permanently "stuck". A tick callback wakes
// the frame clock and forces real render+commit cycles (the same mechanism the click
// flash uses), which flush the cleared content. It removes itself after a few frames,
// so it stays event-driven (no idle CPU) and never unmaps the surface (unmapping is
// unsafe here — it races the layer-shell handshake and kills the client).
static gboolean
flush_empty_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
	(void)clock;
	int *frames = data;
	gtk_widget_queue_draw(widget);
	if (--(*frames) <= 0) {
		g_free(frames);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static gboolean
on_chip_expired(gpointer data)
{
	GtkWidget *chip = data;
	GtkWidget *parent = gtk_widget_get_parent(chip);
	if (parent != NULL) {
		gtk_box_remove(GTK_BOX(parent), chip);
		if (gtk_widget_get_first_child(parent) == NULL) {
			GtkRoot *root = gtk_widget_get_root(parent);
			if (root != NULL) {
				int *frames = g_new(int, 1);
				*frames = 4; // a few frames is plenty to commit the cleared bar
				gtk_widget_add_tick_callback(GTK_WIDGET(root),
							     flush_empty_tick, frames, NULL);
			}
		}
	}
	g_object_unref(chip);
	return G_SOURCE_REMOVE;
}

static GdkMonitor *
find_monitor(const char *connector)
{
	if (connector == NULL || *connector == '\0')
		return NULL;
	GListModel *monitors = gdk_display_get_monitors(gdk_display_get_default());
	guint n = g_list_model_get_n_items(monitors);
	for (guint i = 0; i < n; i++) {
		GdkMonitor *monitor = g_list_model_get_item(monitors, i);
		gboolean match = g_strcmp0(gdk_monitor_get_connector(monitor), connector) == 0;
		g_object_unref(monitor); // model keeps its own reference
		if (match)
			return monitor;
	}
	return NULL;
}

static void
apply_css(BarOverlay *state)
{
	const KeysClicksSettings *s = state->settings;
	const char *border = s->draw_border ? "1px solid rgba(255, 255, 255, 0.14)"
					    : "none";
	char *css = g_strdup_printf(
		".keysclicks-window { background: transparent; }\n"
		// Fixed min-height (one chip row) so the surface height does not change
		// between the empty bar and the first chip — see apply_layer() on why a
		// layer-surface resize must be avoided on COSMIC.
		".keysclicks-bar { padding: 4px; min-height: %dpx; background: rgba(18,18,22,0.08); border-radius: 14px; }\n"
		".keysclicks-chip {\n"
		"  background-color: rgba(18, 18, 22, 0.86);\n"
		"  color: #ffffff;\n"
		"  font-size: %dpx;\n"
		"  font-weight: 600;\n"
		"  padding: 8px 14px;\n"
		"  margin: 4px;\n"
		"  border-radius: 10px;\n"
		"  border: %s;\n"
		"}\n"
		// Transient placeholder shown while dragging the margin sliders so the
		// user sees where the overlay lands. Dark-green (green-700) to match the
		// approved accent, dashed so it reads as a preview, not a real chip.
		".keysclicks-preview {\n"
		"  background-color: rgba(21, 128, 61, 0.9);\n"
		"  border: 2px dashed #ffffff;\n"
		"}\n",
		s->font_size + 34, s->font_size, border);
	gtk_css_provider_load_from_string(state->provider, css);
	g_free(css);
}

static void
apply_layer(GtkWidget *window, const KeysClicksSettings *s)
{
	if (!gtk_layer_is_supported())
		return;
	GtkWindow *w = GTK_WINDOW(window);

	// Anchor BOTH left and right so the surface spans the full monitor width and
	// its size is fixed by the compositor. Crucial: a content-driven resize of a
	// layer surface (which is what happened when the bar was only bottom-anchored
	// and grew/shrank as chips were added and expired) races the layer-shell
	// configure/ack handshake on some compositors — notably COSMIC — and gets the
	// client killed with a Wayland protocol error ("Broken pipe"). A fixed-size
	// surface never resizes, so that race cannot happen. Horizontal placement of
	// the chips is done by the bar's halign (see keysclicks_overlay_apply_settings)
	// within this full-width surface.
	gtk_layer_set_anchor(w, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_anchor(w, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	// Full-width surface with NO horizontal layer margin — the horizontal margin is
	// applied to the bar widget instead (keysclicks_overlay_apply_settings), so it
	// works for every alignment, including centred.
	gtk_layer_set_margin(w, GTK_LAYER_SHELL_EDGE_LEFT, 0);
	gtk_layer_set_margin(w, GTK_LAYER_SHELL_EDGE_RIGHT, 0);

	// Top anchors the bar to the TOP edge, Bottom to the BOTTOM (direct mapping,
	// per the owner's request to swap the previous mirrored wiring back).
	gboolean top = s->edge == KEYSCLICKS_EDGE_TOP;
	gtk_layer_set_anchor(w, GTK_LAYER_SHELL_EDGE_TOP, top);
	gtk_layer_set_anchor(w, GTK_LAYER_SHELL_EDGE_BOTTOM, !top);

	// Vertical margin is a PERCENT of the monitor height (zoom independent).
	GdkMonitor *bar_mon = find_monitor(s->monitor_connector);
	int mon_h = 0;
	if (bar_mon != NULL) {
		GdkRectangle geo;
		gdk_monitor_get_geometry(bar_mon, &geo);
		mon_h = geo.height;
	} else { // auto placement: fall back to the tallest monitor
		GListModel *mons = gdk_display_get_monitors(gdk_display_get_default());
		for (guint i = 0; i < g_list_model_get_n_items(mons); i++) {
			GdkMonitor *mm = g_list_model_get_item(mons, i);
			GdkRectangle geo;
			gdk_monitor_get_geometry(mm, &geo);
			if (geo.height > mon_h)
				mon_h = geo.height;
			g_object_unref(mm);
		}
	}
	int margin_y_px = keysclicks_overlay_percent_to_px(s->margin_y, mon_h);
	gtk_layer_set_margin(w, top ? GTK_LAYER_SHELL_EDGE_TOP : GTK_LAYER_SHELL_EDGE_BOTTOM,
			     margin_y_px);

	// The monitor is pinned once at creation (keysclicks_overlay_new), never here:
	// changing a mapped layer surface's output crashes COSMIC. A monitor change is
	// instead handled by the settings window (restart prompt) — see on_combo.
}

// Logical width used to clamp the horizontal margin: the selected monitor if one
// is chosen, otherwise the WIDEST monitor (the compositor usually centres an
// auto-placed bar on the primary, and using the widest avoids capping the range
// to a narrow secondary — e.g. a portrait side monitor). 0 when none.
static int
target_monitor_width(const KeysClicksSettings *s)
{
	GdkMonitor *m = find_monitor(s->monitor_connector); // borrowed (model holds a ref)
	if (m != NULL) {
		GdkRectangle geo;
		gdk_monitor_get_geometry(m, &geo);
		return geo.width;
	}
	GListModel *mons = gdk_display_get_monitors(gdk_display_get_default());
	guint n = g_list_model_get_n_items(mons);
	int widest = 0;
	for (guint i = 0; i < n; i++) {
		GdkMonitor *mm = g_list_model_get_item(mons, i); // owns a ref
		GdkRectangle geo;
		gdk_monitor_get_geometry(mm, &geo);
		if (geo.width > widest)
			widest = geo.width;
		g_object_unref(mm);
	}
	return widest;
}

#define KEYSCLICKS_MARGIN_PREVIEW_MS 1400

static gboolean
clear_margin_preview(gpointer data)
{
	BarOverlay *state = data;
	state->preview_timeout = 0;
	if (state->preview_chip != NULL) {
		GtkWidget *parent = gtk_widget_get_parent(state->preview_chip);
		if (parent != NULL)
			gtk_box_remove(GTK_BOX(parent), state->preview_chip);
		state->preview_chip = NULL;
		// Force a commit of the now-cleared bar (COSMIC keeps the last frame of an
		// idle transparent surface — same reason as flush_empty_tick on chip expiry).
		GtkRoot *root = gtk_widget_get_root(state->bar);
		if (root != NULL) {
			int *frames = g_new(int, 1);
			*frames = 4;
			gtk_widget_add_tick_callback(GTK_WIDGET(root), flush_empty_tick,
						     frames, NULL);
		}
	}
	return G_SOURCE_REMOVE;
}

// Show a transient placeholder in the bar so the user sees WHERE the overlay will
// appear while dragging the margin sliders. Reuses the bar surface (no new layer
// surface), so it stays safe on COSMIC. Auto-clears after a short delay; repeated
// calls (a live drag) just extend it.
static void
show_margin_preview(BarOverlay *state)
{
	gtk_widget_set_visible(state->window, TRUE);
	if (state->preview_chip == NULL) {
		state->preview_chip = gtk_label_new(NULL);
		gtk_label_set_markup(GTK_LABEL(state->preview_chip),
				     "\xe2\x86\x94 overlay position"); // ↔
		gtk_widget_add_css_class(state->preview_chip, "keysclicks-chip");
		gtk_widget_add_css_class(state->preview_chip, "keysclicks-preview");
		gtk_box_append(GTK_BOX(state->bar), state->preview_chip);
		apply_click_through(state->window);
	}
	if (state->preview_timeout != 0)
		g_source_remove(state->preview_timeout);
	state->preview_timeout =
		g_timeout_add(KEYSCLICKS_MARGIN_PREVIEW_MS, clear_margin_preview, state);
}

void
keysclicks_overlay_apply_settings(GtkWidget *overlay)
{
	BarOverlay *state = overlay_state(overlay);
	if (state == NULL)
		return;
	const KeysClicksSettings *s = state->settings;

	apply_css(state);
	gtk_widget_set_opacity(state->bar, s->bar_opacity);

	// Direct Top/Bottom mapping (matches apply_layer): Top pins the bar to the top.
	gtk_widget_set_valign(state->bar, s->edge == KEYSCLICKS_EDGE_TOP ? GTK_ALIGN_START
									: GTK_ALIGN_END);

	// Horizontal margin is a signed PERCENT position over the WHOLE range
	// (-100 = flush left, 0 = centre, +100 = flush right), recomputed to pixels each
	// apply (resolution/zoom independent — percent of the LOGICAL monitor width, which
	// GDK already reports scaled, so 150%->100% keeps the same fraction).
	//
	// Anchor the bar to whichever side it is heading toward and pull it in with a
	// margin: at the extreme the margin is exactly 0, so the bar sits FLUSH against
	// that edge regardless of any width estimate. The measured bar width only scales
	// the interior so that 0 lands dead centre (right edge = (mon_w + bar_w)/2).
	int mon_w = target_monitor_width(s);
	int bar_w = 0;
	gtk_widget_measure(state->bar, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &bar_w, NULL,
			   NULL);
	int travel = mon_w - bar_w;
	if (travel < 0)
		travel = 0;
	if (s->margin_x >= 0) {
		// Heading right (or centred): pin the RIGHT edge. +100 -> margin_end 0 (flush
		// right); 0 -> travel/2 (centred).
		gtk_widget_set_halign(state->bar, GTK_ALIGN_END);
		gtk_widget_set_margin_end(
			state->bar,
			keysclicks_overlay_percent_to_px(100 - s->margin_x, travel / 2));
		gtk_widget_set_margin_start(state->bar, 0);
	} else {
		// Heading left: pin the LEFT edge. -100 -> margin_start 0 (flush left).
		gtk_widget_set_halign(state->bar, GTK_ALIGN_START);
		gtk_widget_set_margin_start(
			state->bar,
			keysclicks_overlay_percent_to_px(100 + s->margin_x, travel / 2));
		gtk_widget_set_margin_end(state->bar, 0);
	}

	apply_layer(state->window, s);
	gtk_widget_set_visible(state->window, s->overlay_visible);

	// If a margin changed (the user is dragging a margin slider), flash a preview so
	// they can see where the overlay lands. Never on the first apply (last_* is
	// seeded to the initial settings in keysclicks_overlay_new).
	if (s->overlay_visible &&
	    (state->last_margin_x != s->margin_x || state->last_margin_y != s->margin_y))
		show_margin_preview(state);
	state->last_margin_x = s->margin_x;
	state->last_margin_y = s->margin_y;

	// Turning "keep visible" OFF must clear chips already frozen on screen (they were
	// pushed with no expiry timeout, so nothing would ever remove them).
	if (state->last_keep && !s->keep_forever)
		clear_all_chips(state);
	state->last_keep = s->keep_forever;
}

static void
bar_overlay_free(gpointer data)
{
	BarOverlay *state = data;
	if (state->preview_timeout != 0)
		g_source_remove(state->preview_timeout);
	g_clear_object(&state->provider);
	g_free(state);
}

GtkWidget *
keysclicks_overlay_new(GtkApplication *app, KeysClicksSettings *settings)
{
	GtkWidget *window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), _("On-Screen Keys & Clicks Visualizer"));
	gtk_widget_add_css_class(window, "keysclicks-window");

	if (gtk_layer_is_supported()) {
		gtk_layer_init_for_window(GTK_WINDOW(window));
		gtk_layer_set_namespace(GTK_WINDOW(window), "keysclicks-overlay");
		// OVERLAY layer keeps the bar above normal windows (always-on-top).
		gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_OVERLAY);
		// Never take keyboard focus, or global shortcuts would break.
		gtk_layer_set_keyboard_mode(GTK_WINDOW(window),
					    GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
		// Pin the output ONCE (a later change crashes COSMIC — the settings window
		// prompts for a restart instead).
		gtk_layer_set_monitor(GTK_WINDOW(window),
				      find_monitor(settings->monitor_connector));
	} else {
		g_warning("keysclicks: gtk4-layer-shell is not supported by this "
			  "compositor; the overlay will fall back to an ordinary window "
			  "and may not stay on top");
	}

	GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class(bar, "keysclicks-bar");
	gtk_window_set_child(GTK_WINDOW(window), bar);

	BarOverlay *state = g_new0(BarOverlay, 1);
	state->window = window;
	state->bar = bar;
	state->settings = settings;
	// Seed so the first apply_settings does not mistake startup for a margin drag.
	state->last_margin_x = settings->margin_x;
	state->last_margin_y = settings->margin_y;
	state->last_keep = settings->keep_forever;
	state->provider = gtk_css_provider_new();
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
						   GTK_STYLE_PROVIDER(state->provider),
						   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_set_data_full(G_OBJECT(window), "keysclicks-state", state,
			       bar_overlay_free);

	g_signal_connect(window, "realize", G_CALLBACK(on_realize), NULL);

	keysclicks_overlay_apply_settings(window);
	return window;
}

void
keysclicks_overlay_push(GtkWidget *overlay, const char *text)
{
	BarOverlay *state = overlay_state(overlay);
	if (state == NULL)
		return;
	GtkWidget *bar = state->bar;

	// Chips are rendered as Pango markup so a chord can dim its "+" separators
	// (see keysclicks_keymap_combo_markup). Callers pass markup-safe text: the
	// composed labels are escaped by combo_markup; raw evdev names, button labels
	// and the mask bullet contain no markup characters.
	GtkWidget *chip = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(chip), text);
	gtk_widget_add_css_class(chip, "keysclicks-chip");
	gtk_box_append(GTK_BOX(bar), chip);

	// Trim the oldest chips beyond the cap. Their own expiry timeout still
	// holds a reference, so removing them here does not free them early.
	int count = 0;
	for (GtkWidget *child = gtk_widget_get_first_child(bar); child != NULL;
	     child = gtk_widget_get_next_sibling(child))
		count++;
	while (count > KEYSCLICKS_MAX_CHIPS) {
		GtkWidget *oldest = gtk_widget_get_first_child(bar);
		gtk_box_remove(GTK_BOX(bar), oldest);
		count--;
	}

	// Auto-expire unless the user asked to keep chips forever.
	if (!state->settings->keep_forever) {
		g_object_ref(chip);
		g_timeout_add(state->settings->timeout_ms, on_chip_expired, chip);
	}

	apply_click_through(overlay);
}
