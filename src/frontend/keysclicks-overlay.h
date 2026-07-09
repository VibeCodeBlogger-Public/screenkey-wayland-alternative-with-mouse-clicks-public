// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger

#pragma once

#include <gtk/gtk.h>

#include "keysclicks-settings.h"

G_BEGIN_DECLS

// Creates the always-on-top overlay bar as a gtk4-layer-shell surface. It reads
// its appearance and placement from the shared settings record. The window
// never grabs the keyboard and is click-through. Returns the window (owned by
// the application).
GtkWidget *keysclicks_overlay_new(GtkApplication *app, KeysClicksSettings *settings);

// Appends a new labelled chip to the bar. The chip auto-expires after the
// configured timeout (unless "keep" is enabled), and the bar keeps only the
// most recent few chips.
void keysclicks_overlay_push(GtkWidget *overlay, const char *text);

// Re-apply the current settings (visibility, font size, border, opacity,
// alignment, screen edge, margins and monitor) to a live overlay.
void keysclicks_overlay_apply_settings(GtkWidget *overlay);

// Convert a signed percent (-100..100, clamped) to pixels of the given extent —
// how the resolution/zoom-independent margins are turned into positions. Exposed
// for unit testing.
int keysclicks_overlay_percent_to_px(int percent, int extent);

G_END_DECLS
