// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger
//
// The supported-language table: the single source of truth for which UI
// languages exist, their canonical codes (matching po/<code>.po and the
// reference project), their native names (endonyms, shown verbatim in the
// picker) and text direction. Used by the language switcher and by the startup
// locale resolver so the UI opens in the user's native language.

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
	const char *code;        // canonical code, e.g. "pt-BR" (matches po/ + parity)
	const char *native_name; // endonym, shown in its own script, never translated
	gboolean rtl;            // right-to-left script (ar, fa, ur)
} KeysClicksLanguage;

// The full table (31 entries), in the canonical order shared with the reference
// project. Never NULL; *n_out is set to the entry count.
const KeysClicksLanguage *keysclicks_languages(guint *n_out);

// Resolve a wanted language to a supported code. `want` may be an explicit code
// ("ru", "pt-BR"), a system locale name ("pt_BR.UTF-8", "zh_CN"), or NULL/"" to
// auto-detect from the environment via g_get_language_names(). Matching is
// case-insensitive, tolerates '_' vs '-' and strips ".codeset"/"@modifier";
// an exact code wins, otherwise the first table entry sharing the base subtag
// (e.g. system "es_MX" -> "es-419", "zh_CN" -> "zh-Hans"). Returns a static code
// owned by the table, or NULL when nothing matches (caller keeps English).
const char *keysclicks_language_resolve(const char *want);

// TRUE if `code` exactly names a supported language.
gboolean keysclicks_language_is_supported(const char *code);

// TRUE if `code` is a right-to-left language (ar/fa/ur). Unknown code -> FALSE.
// The GUI mirrors its layout (gtk_widget_set_default_direction) for these.
gboolean keysclicks_language_is_rtl(const char *code);

// Native name for a supported code, or the code itself when unknown.
const char *keysclicks_language_native_name(const char *code);

// Zero-based index of `code` in the table, or -1 when unsupported.
int keysclicks_language_index(const char *code);

G_END_DECLS
