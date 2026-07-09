// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger

#pragma once

#include <glib.h>

G_BEGIN_DECLS

// Loads and owns an optional external privacy provider plugin (a .so exporting
// kc_privacy_provider_get). Loading is fail-open: any problem is logged to
// stderr and the app continues with no provider (manual toggle still works).
typedef struct _KeysClicksPrivacy KeysClicksPrivacy;

// Searches for and loads the plugin (see keysclicks-privacy.c for the order).
// Never fails: returns a valid object even when no plugin is present.
KeysClicksPrivacy *keysclicks_privacy_new(void);

void keysclicks_privacy_free(KeysClicksPrivacy *self);

// TRUE if a provider plugin was successfully loaded.
gboolean keysclicks_privacy_loaded(KeysClicksPrivacy *self);

// The loaded provider's name, or NULL if none.
const char *keysclicks_privacy_name(KeysClicksPrivacy *self);

// Non-blocking query of the provider's current masking decision. Returns FALSE
// when no provider is loaded.
gboolean keysclicks_privacy_should_mask(KeysClicksPrivacy *self);

G_END_DECLS
