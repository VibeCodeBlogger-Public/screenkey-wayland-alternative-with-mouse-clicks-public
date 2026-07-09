// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger

#pragma once

#include <gio/gio.h>
#include <glib.h>

G_BEGIN_DECLS

// Which kind of input produced an event.
typedef enum {
	KEYSCLICKS_EVENT_KEY,    // keyboard key; code is an evdev keycode
	KEYSCLICKS_EVENT_BUTTON, // pointer button; code is an evdev button code
	KEYSCLICKS_EVENT_MOTION, // relative pointer motion; use dx/dy
} KeysClicksEventKind;

// Called on the GLib main context for every parsed event from the backend.
// For KEY/BUTTON events, code/pressed are set and dx/dy are 0. For MOTION
// events, dx/dy hold the relative delta and code/pressed are unused.
typedef void (*KeysClicksEventFunc)(KeysClicksEventKind kind, guint32 code,
				    gboolean pressed, double dx, double dy,
				    gpointer user_data);

// Owns the privileged keysclicks-input subprocess (spawned via pkexec) and
// streams its JSON output back to the caller as decoded events.
typedef struct _KeysClicksBackend KeysClicksBackend;

KeysClicksBackend *keysclicks_backend_new(KeysClicksEventFunc callback,
					  gpointer user_data);

// Launches "pkexec <backend>" and begins reading events asynchronously.
// Returns FALSE and sets error if the subprocess could not be spawned.
gboolean keysclicks_backend_start(KeysClicksBackend *self, GError **error);

// Stops reading and terminates the subprocess. Safe to call more than once.
void keysclicks_backend_stop(KeysClicksBackend *self);

// Parse the pointer "speed:" value out of a COSMIC input-config (RON) string.
// Returns a newly-allocated C-locale number string ("-0.81"), or NULL if there
// is no parseable speed. Exposed for unit testing.
char *keysclicks_backend_parse_accel_speed(const char *content);

void keysclicks_backend_free(KeysClicksBackend *self);

G_END_DECLS
