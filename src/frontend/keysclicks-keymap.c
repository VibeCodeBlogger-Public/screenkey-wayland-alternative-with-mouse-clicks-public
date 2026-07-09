// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger

#include "keysclicks-keymap.h"

#include <xkbcommon/xkbcommon.h>

// evdev keycodes are offset by 8 in the X11/xkb keycode space.
#define KEYSCLICKS_EVDEV_OFFSET 8

struct _KeysClicksKeymap {
	struct xkb_context *context;
	struct xkb_keymap *keymap;
	struct xkb_state *state;
	// Remembered lock state, re-applied whenever the keymap is rebuilt (layout switch)
	// because a fresh xkb_state always starts unlocked.
	gboolean num_lock;
	gboolean caps_lock;
};

KeysClicksKeymap *
keysclicks_keymap_new(void)
{
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (context == NULL)
		return NULL;

	struct xkb_keymap *keymap =
		xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (keymap == NULL) {
		xkb_context_unref(context);
		return NULL;
	}

	struct xkb_state *state = xkb_state_new(keymap);
	if (state == NULL) {
		xkb_keymap_unref(keymap);
		xkb_context_unref(context);
		return NULL;
	}

	KeysClicksKeymap *self = g_new0(KeysClicksKeymap, 1);
	self->context = context;
	self->keymap = keymap;
	self->state = state;
	return self;
}

void
keysclicks_keymap_free(KeysClicksKeymap *self)
{
	if (self == NULL)
		return;
	xkb_state_unref(self->state);
	xkb_keymap_unref(self->keymap);
	xkb_context_unref(self->context);
	g_free(self);
}

void
keysclicks_keymap_feed(KeysClicksKeymap *self, uint32_t evdev_code, gboolean pressed)
{
	if (self == NULL)
		return;
	xkb_state_update_key(self->state, evdev_code + KEYSCLICKS_EVDEV_OFFSET,
			     pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
}

// Apply the remembered NumLock/CapsLock to the CURRENT xkb_state. Split out from
// keysclicks_keymap_set_locks so a layout rebuild can re-seed the same locks.
static void
apply_locks(KeysClicksKeymap *self)
{
	// Seed the locked modifiers so numpad keys resolve to DIGITS when NumLock is on
	// (and letters honour CapsLock). The state otherwise starts unlocked, so with the
	// system's NumLock on the numpad showed navigation keys (KP_Home…) instead of
	// digits. Later NumLock/CapsLock presses keep it in sync via keysclicks_keymap_feed.
	xkb_mod_index_t caps = xkb_keymap_mod_get_index(self->keymap, XKB_MOD_NAME_CAPS);
	xkb_mod_index_t num =
		xkb_keymap_mod_get_index(self->keymap, "Mod2"); // NumLock (i18n-ignore: xkb modifier)
	xkb_mod_mask_t locked = 0;
	if (self->caps_lock && caps != XKB_MOD_INVALID)
		locked |= (xkb_mod_mask_t)1 << caps;
	if (self->num_lock && num != XKB_MOD_INVALID)
		locked |= (xkb_mod_mask_t)1 << num;
	xkb_state_update_mask(self->state, 0, 0, locked, 0, 0, 0);
}

void
keysclicks_keymap_set_locks(KeysClicksKeymap *self, gboolean num_lock, gboolean caps_lock)
{
	if (self == NULL)
		return;
	self->num_lock = num_lock;
	self->caps_lock = caps_lock;
	apply_locks(self);
}

gboolean
keysclicks_keymap_set_layout(KeysClicksKeymap *self, const char *layout, const char *variant)
{
	if (self == NULL)
		return FALSE;

	// Build a keymap for the chosen layout/variant. Empty/NULL fields fall back to
	// the system default (so "System" = the pre-feature behaviour).
	struct xkb_rule_names names = {
		.rules = NULL,
		.model = NULL,
		.layout = (layout != NULL && *layout != '\0') ? layout : NULL,
		.variant = (variant != NULL && *variant != '\0') ? variant : NULL,
		.options = NULL,
	};
	struct xkb_keymap *keymap =
		xkb_keymap_new_from_names(self->context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (keymap == NULL)
		return FALSE; // unknown names — keep the current keymap/state untouched

	struct xkb_state *state = xkb_state_new(keymap);
	if (state == NULL) {
		xkb_keymap_unref(keymap);
		return FALSE;
	}

	xkb_state_unref(self->state);
	xkb_keymap_unref(self->keymap);
	self->keymap = keymap;
	self->state = state;
	apply_locks(self); // a fresh state starts unlocked — restore NumLock/CapsLock
	return TRUE;
}

// Friendly names for keys that either have no printable character or read
// better spelled out. Returns a static string or NULL if the symbol is not
// special-cased.
static const char *
special_label(xkb_keysym_t sym)
{
	switch (sym) {
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		return "Enter";
	case XKB_KEY_BackSpace:
		return "Backspace";
	case XKB_KEY_Tab:
	case XKB_KEY_ISO_Left_Tab:
		return "Tab";
	case XKB_KEY_Escape:
		return "Esc";
	case XKB_KEY_space:
		return "Space";
	case XKB_KEY_Delete:
		return "Del";
	case XKB_KEY_Insert:
		return "Ins";
	case XKB_KEY_Home:
		return "Home";
	case XKB_KEY_End:
		return "End";
	case XKB_KEY_Page_Up:
		return "PgUp";
	case XKB_KEY_Page_Down:
		return "PgDn";
	case XKB_KEY_Up:
		return "\xe2\x86\x91"; // up arrow
	case XKB_KEY_Down:
		return "\xe2\x86\x93"; // down arrow
	case XKB_KEY_Left:
		return "\xe2\x86\x90"; // left arrow
	case XKB_KEY_Right:
		return "\xe2\x86\x92"; // right arrow
	case XKB_KEY_Shift_L:
	case XKB_KEY_Shift_R:
		return "Shift";
	case XKB_KEY_Control_L:
	case XKB_KEY_Control_R:
		return "Ctrl";
	case XKB_KEY_Alt_L:
	case XKB_KEY_Alt_R:
		return "Alt";
	case XKB_KEY_ISO_Level3_Shift:
		return "AltGr";
	case XKB_KEY_Super_L:
	case XKB_KEY_Super_R:
		return "Super";
	case XKB_KEY_Meta_L:
	case XKB_KEY_Meta_R:
		// Physically this is the Alt key on modern keyboards (the Meta keysym just
		// happens to live there in some layouts), so label it "Alt" — that is what
		// the user pressed.
		return "Alt";
	case XKB_KEY_Hyper_L:
	case XKB_KEY_Hyper_R:
		return "Hyper";
	case XKB_KEY_Menu:
		return "Menu";
	case XKB_KEY_Caps_Lock:
		return "CapsLk";
	case XKB_KEY_Num_Lock:
		return "NumLk";
	case XKB_KEY_Scroll_Lock:
		return "ScrLk";
	case XKB_KEY_Print:
		return "PrtSc";
	case XKB_KEY_Pause:
		return "Pause";
	default:
		return NULL;
	}
}

// TRUE if the keysym is itself a modifier/lock key (so a chord label does not
// prefix a modifier onto itself, e.g. Ctrl shows as "Ctrl", not "Ctrl+Ctrl").
static gboolean
is_modifier_sym(xkb_keysym_t sym)
{
	switch (sym) {
	case XKB_KEY_Shift_L:
	case XKB_KEY_Shift_R:
	case XKB_KEY_Control_L:
	case XKB_KEY_Control_R:
	case XKB_KEY_Alt_L:
	case XKB_KEY_Alt_R:
	case XKB_KEY_ISO_Level3_Shift:
	case XKB_KEY_Super_L:
	case XKB_KEY_Super_R:
	case XKB_KEY_Meta_L:
	case XKB_KEY_Meta_R:
	case XKB_KEY_Hyper_L:
	case XKB_KEY_Hyper_R:
	case XKB_KEY_Caps_Lock:
	case XKB_KEY_Num_Lock:
		return TRUE;
	default:
		return FALSE;
	}
}

// Ordered chord parts: the held modifier names (if the key itself is not a
// modifier and at least one of Ctrl/Alt/Super is down) followed by the base key
// label. NULL-terminated; free with g_strfreev(). The base is always last so a
// key that is itself "+" ends up as the final element, never a separator.
static char **
combo_parts(KeysClicksKeymap *self, uint32_t evdev_code)
{
	char *base = keysclicks_keymap_label(self, evdev_code);
	GPtrArray *parts = g_ptr_array_new();

	xkb_keycode_t keycode = evdev_code + KEYSCLICKS_EVDEV_OFFSET;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(self->state, keycode);
	if (!is_modifier_sym(sym)) {
		gboolean ctrl = xkb_state_mod_name_is_active(
					self->state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
		gboolean alt = xkb_state_mod_name_is_active(
				       self->state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0;
		gboolean super = xkb_state_mod_name_is_active(
					 self->state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0;
		gboolean shift = xkb_state_mod_name_is_active(
					 self->state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
		// Shift alone is already in the symbol (a → A), so only fold it in when a
		// "real" modifier is also held (Ctrl/Alt/Super) — a plain capital stays "A".
		if (ctrl || alt || super) {
			if (super)
				g_ptr_array_add(parts, g_strdup("Super"));
			if (ctrl)
				g_ptr_array_add(parts, g_strdup("Ctrl"));
			if (alt)
				g_ptr_array_add(parts, g_strdup("Alt"));
			if (shift)
				g_ptr_array_add(parts, g_strdup("Shift"));
		}
	}
	g_ptr_array_add(parts, base); // transfers ownership; always the last part
	g_ptr_array_add(parts, NULL);
	return (char **)g_ptr_array_free(parts, FALSE);
}

char *
keysclicks_keymap_combo_label(KeysClicksKeymap *self, uint32_t evdev_code)
{
	if (self == NULL)
		return keysclicks_keymap_label(self, evdev_code);
	char **parts = combo_parts(self, evdev_code);
	char *out = g_strjoinv("+", parts);
	g_strfreev(parts);
	return out;
}

char *
keysclicks_keymap_combo_markup(KeysClicksKeymap *self, uint32_t evdev_code)
{
	if (self == NULL) {
		char *base = keysclicks_keymap_label(self, evdev_code);
		char *esc = g_markup_escape_text(base, -1);
		g_free(base);
		return esc;
	}
	char **parts = combo_parts(self, evdev_code);
	GString *s = g_string_new(NULL);
	for (int i = 0; parts[i] != NULL; i++) {
		if (i > 0)
			// Separator "+": rendered smaller and dimmer than the key labels so
			// it never reads as a literal "+" key (e.g. Ctrl+Alt+Shift and the
			// "+" key no longer show a confusing "++").
			g_string_append(s, "<span size=\"65%\" alpha=\"55%\">+</span>");
		char *esc = g_markup_escape_text(parts[i], -1);
		g_string_append(s, esc);
		g_free(esc);
	}
	g_strfreev(parts);
	return g_string_free(s, FALSE);
}

char *
keysclicks_keymap_label(KeysClicksKeymap *self, uint32_t evdev_code)
{
	if (self == NULL)
		return g_strdup("?");

	xkb_keycode_t keycode = evdev_code + KEYSCLICKS_EVDEV_OFFSET;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(self->state, keycode);

	// Function keys F1..F24.
	if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F24)
		return g_strdup_printf("F%d", (int)(sym - XKB_KEY_F1 + 1));

	const char *special = special_label(sym);
	if (special != NULL)
		return g_strdup(special);

	// Printable character in the current state (respects Shift/CapsLock/etc.).
	char buffer[64];
	int len = xkb_state_key_get_utf8(self->state, keycode, buffer, sizeof buffer);
	if (len > 0) {
		guchar first = (guchar)buffer[0];
		if (first >= 0x20 && first != 0x7f) // reject control characters
			return g_strdup(buffer);
	}

	// Last resort: the symbolic name of the keysym (e.g. "XF86AudioPlay").
	if (xkb_keysym_get_name(sym, buffer, sizeof buffer) > 0)
		return g_strdup(buffer);

	return g_strdup("?");
}
