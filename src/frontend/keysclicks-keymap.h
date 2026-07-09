// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger

#pragma once

#include <glib.h>
#include <stdint.h>

G_BEGIN_DECLS

// Thin wrapper around xkbcommon that turns raw evdev keycodes (as reported by
// the backend) into human-readable labels honouring the active keyboard layout
// and modifier state.
typedef struct _KeysClicksKeymap KeysClicksKeymap;

// Creates a keymap from the system default rules/model/layout (or the
// XKB_DEFAULT_* environment variables). Returns NULL on failure.
KeysClicksKeymap *keysclicks_keymap_new(void);

void keysclicks_keymap_free(KeysClicksKeymap *self);

// Computes a display label for the given evdev keycode using the CURRENT
// modifier state. For a key press, call this BEFORE keysclicks_keymap_feed() so
// the already-held modifiers (e.g. Shift) are reflected. Never returns NULL;
// the result must be freed with g_free().
char *keysclicks_keymap_label(KeysClicksKeymap *self, uint32_t evdev_code);

// Like keysclicks_keymap_label(), but for a NON-modifier key pressed while
// Ctrl/Alt/Super are held it prefixes those modifiers so the whole chord shows
// as one label, e.g. "Ctrl+Shift+T", instead of just the last key. Shift is
// added to the prefix only when another modifier is also held (so a plain
// capital stays "A", not "Shift+A"). A modifier key pressed on its own returns
// just its own name. Call BEFORE keysclicks_keymap_feed(). Free with g_free().
char *keysclicks_keymap_combo_label(KeysClicksKeymap *self, uint32_t evdev_code);

// Same chord as keysclicks_keymap_combo_label(), but returned as Pango markup
// with the "+" separators drawn smaller and dimmer than the keys, so a chord
// that ends in the "+" key does not show a confusing "++". The key labels are
// markup-escaped. Free with g_free(). (Overlay chips are rendered as markup.)
char *keysclicks_keymap_combo_markup(KeysClicksKeymap *self, uint32_t evdev_code);

// Updates the internal modifier/lock state. pressed = TRUE for a key down,
// FALSE for a key up.
void keysclicks_keymap_feed(KeysClicksKeymap *self, uint32_t evdev_code,
			    gboolean pressed);

// Seed the NumLock/CapsLock lock state (e.g. read from the system LEDs at startup)
// so numpad keys render as digits when NumLock is on, matching the real keyboard.
void keysclicks_keymap_set_locks(KeysClicksKeymap *self, gboolean num_lock,
				 gboolean caps_lock);

// Switch the keyboard layout the overlay uses to turn keycodes into symbols. Same
// idea as the compositor swapping layouts, but for DISPLAY only: `layout`/`variant`
// are xkb RMLVO names (e.g. "ru", "in"/"tel"); NULL or "" for either falls back to
// the system default for that field. Rebuilds the internal xkb keymap + state and
// re-applies the remembered NumLock/CapsLock. Returns FALSE (keeping the current
// layout) if the requested names do not compile. Held modifier state is reset.
gboolean keysclicks_keymap_set_layout(KeysClicksKeymap *self, const char *layout,
				      const char *variant);

G_END_DECLS
