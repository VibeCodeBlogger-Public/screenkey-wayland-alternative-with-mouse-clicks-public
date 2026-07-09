// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger
//
// Thin gettext front-end: bind the translation domain at startup and switch the
// active UI language at runtime. Shared by main() (startup + persisted choice)
// and the settings window's language switcher.

#pragma once

#include <glib.h>

G_BEGIN_DECLS

// Pick up the system locale and bind the gettext domain. Call once at startup,
// before the first translated string is used.
void keysclicks_i18n_init(void);

// Switch the active UI language. `preferred` is "" / NULL to follow the system
// locale, or a language code resolved against the supported table. Pins the
// LANGUAGE environment variable (so hyphenated codes like "pt-BR" that are not
// system locales still map to our catalogs) and drops gettext's catalog cache
// so a subsequent _() reflects the change.
void keysclicks_i18n_apply_language(const char *preferred);

G_END_DECLS
