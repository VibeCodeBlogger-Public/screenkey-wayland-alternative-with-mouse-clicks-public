// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger

#pragma once

#include <glib.h>

G_BEGIN_DECLS

// The application id, matching the installed .desktop file and the window's
// Wayland app_id, so the COSMIC dock associates a pin with our launcher.
#define KEYSCLICKS_APP_ID "io.github.vibecodeblogger.KeysAndClicksVisualizer"

// Pinning is implemented by editing COSMIC's dock favourites file directly:
//   ~/.config/cosmic/com.system76.CosmicAppList/v1/favorites
// which is a RON array of app-id strings. This is an unofficial config hack,
// NOT a public API, and may break with future COSMIC releases; it therefore
// only activates when that file exists (i.e. on a COSMIC session).

// TRUE when the COSMIC dock favourites file exists (this is a COSMIC session).
gboolean keysclicks_dock_available(void);

// TRUE when our app id is currently in the favourites list.
gboolean keysclicks_dock_is_pinned(void);

// Add (pinned = TRUE) or remove (pinned = FALSE) our app id from the favourites
// list. A backup is written to favorites.bak first. No-op if already in the
// requested state. Returns FALSE and sets error on write failure.
gboolean keysclicks_dock_set_pinned(gboolean pinned, GError **error);

G_END_DECLS
