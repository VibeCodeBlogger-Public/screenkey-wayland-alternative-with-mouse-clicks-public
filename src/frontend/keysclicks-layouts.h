// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger
//
// The overlay's keyboard-layout catalogue: the FULL set of xkb layouts the system
// knows about (via libxkbregistry) — deliberately richer than the UI-language list,
// the same source your OS keyboard settings draw from. Used by the settings window's
// "Keyboard layout" picker; the choice is display-only (which layout turns keycodes
// into symbols in the overlay).

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
	const char *layout;  // xkb layout name, e.g. "us", "ru", "in"
	const char *variant; // xkb variant, e.g. "dvorak", "tel"; NULL for the base layout
	const char *name;    // human description for the picker, e.g. "Russian", "English (Dvorak)"
} KeysClicksLayout;

// The full layout list, sorted "us" first then by description. Built once from the xkb
// registry and cached for the process. Never NULL; *n_out is set to the count (0 only
// if the xkb registry could not be read).
const KeysClicksLayout *keysclicks_layouts(guint *n_out);

// Zero-based index of (layout, variant) in the list, or -1 when not found. A NULL/empty
// layout means "system default" and returns -1 (the picker maps that to its first row).
int keysclicks_layouts_index_of(const char *layout, const char *variant);

G_END_DECLS
