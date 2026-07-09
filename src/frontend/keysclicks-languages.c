// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger

#include "keysclicks-languages.h"

#include <string.h>

// The 31 supported languages, in the canonical order shared with the reference
// project. Native names are endonyms (shown in their own script) and are never
// translated, so this file is intentionally excluded from the gettext scan
// (po/POTFILES) and from the i18n hardcode linter.
// Fields: code, native name (endonym), RTL, xkb layout, xkb variant. The xkb
// layout/variant drive the overlay's keyboard-layout picker (a display-only choice).
// For languages whose script is entered via an IME or is plain Latin (CJK, Amharic,
// Haitian, Swahili…) the xkb layout is only an approximation — the picker keeps the
// entry (order preserved) but the rendered symbols may match the base layout.
static const KeysClicksLanguage LANGUAGES[] = {
	{ "en", "English", FALSE, "us", NULL },
	{ "es-419", "Español", FALSE, "latam", NULL },
	{ "zh-Hans", "简体中文", FALSE, "cn", NULL },
	{ "fil", "Filipino", FALSE, "ph", NULL },
	{ "vi", "Tiếng Việt", FALSE, "vn", NULL },
	{ "ru", "Русский", FALSE, "ru", NULL },
	{ "ar", "العربية", TRUE, "ara", NULL },
	{ "ko", "한국어", FALSE, "kr", NULL },
	{ "hi", "हिन्दी", FALSE, "in", NULL },
	{ "ht", "Kreyòl Ayisyen", FALSE, "us", NULL },
	{ "pt-BR", "Português (Brasil)", FALSE, "br", NULL },
	{ "fr", "Français", FALSE, "fr", NULL },
	{ "fa", "فارسی", TRUE, "ir", NULL },
	{ "ja", "日本語", FALSE, "jp", NULL },
	{ "am", "አማርኛ", FALSE, "et", NULL },
	{ "te", "తెలుగు", FALSE, "in", "tel" },
	{ "ur", "اردو", TRUE, "pk", NULL },
	{ "pl", "Polski", FALSE, "pl", NULL },
	{ "bn", "বাংলা", FALSE, "bd", NULL },
	{ "gu", "ગુજરાતી", FALSE, "in", "guj" },
	{ "pa", "ਪੰਜਾਬੀ", FALSE, "in", "guru" },
	{ "sw", "Kiswahili", FALSE, "us", NULL },
	{ "uk", "Українська", FALSE, "ua", NULL },
	{ "hy", "Հայերեն", FALSE, "am", NULL },
	{ "ro", "Română", FALSE, "ro", NULL },
	{ "uz", "Oʻzbekcha", FALSE, "uz", NULL },
	{ "ka", "ქართული", FALSE, "ge", NULL },
	{ "az", "Azərbaycan", FALSE, "az", NULL },
	{ "be", "Беларуская", FALSE, "by", NULL },
	{ "kk", "Қазақша", FALSE, "kz", NULL },
	{ "zh-Hant", "繁體中文", FALSE, "tw", NULL },
};

const KeysClicksLanguage *
keysclicks_languages(guint *n_out)
{
	if (n_out != NULL)
		*n_out = G_N_ELEMENTS(LANGUAGES);
	return LANGUAGES;
}

int
keysclicks_language_index(const char *code)
{
	if (code == NULL)
		return -1;
	for (guint i = 0; i < G_N_ELEMENTS(LANGUAGES); i++)
		if (g_ascii_strcasecmp(LANGUAGES[i].code, code) == 0)
			return (int)i;
	return -1;
}

gboolean
keysclicks_language_is_supported(const char *code)
{
	return keysclicks_language_index(code) >= 0;
}

const char *
keysclicks_language_xkb_layout(const char *code, const char **variant_out)
{
	int idx = keysclicks_language_index(code);
	if (idx < 0) {
		if (variant_out != NULL)
			*variant_out = NULL;
		return NULL;
	}
	if (variant_out != NULL)
		*variant_out = LANGUAGES[idx].xkb_variant;
	return LANGUAGES[idx].xkb_layout;
}

gboolean
keysclicks_language_is_rtl(const char *code)
{
	int idx = keysclicks_language_index(code);
	return idx >= 0 ? LANGUAGES[idx].rtl : FALSE;
}

const char *
keysclicks_language_native_name(const char *code)
{
	int idx = keysclicks_language_index(code);
	return idx >= 0 ? LANGUAGES[idx].native_name : code;
}

// Match a single locale name (already an explicit code or a system name) against
// the table. Returns a static table code or NULL.
static const char *
resolve_one(const char *raw)
{
	if (raw == NULL || *raw == '\0')
		return NULL;

	// Normalise: lowercase, drop ".codeset"/"@modifier", '_' -> '-'.
	gchar *n = g_ascii_strdown(raw, -1);
	gchar *cut = strpbrk(n, ".@");
	if (cut != NULL)
		*cut = '\0';
	for (gchar *c = n; *c != '\0'; c++)
		if (*c == '_')
			*c = '-';

	const char *hit = NULL;

	// 1) Exact code match ("ru", "pt-br" -> "pt-BR").
	for (guint i = 0; i < G_N_ELEMENTS(LANGUAGES) && hit == NULL; i++)
		if (g_ascii_strcasecmp(LANGUAGES[i].code, n) == 0)
			hit = LANGUAGES[i].code;

	// 2) Base-subtag match: system "es-mx" -> "es-419", "zh-cn" -> "zh-Hans".
	//    Compare up to the first '-' of the request against each code's base.
	if (hit == NULL) {
		gchar *dash = strchr(n, '-');
		gsize baselen = dash != NULL ? (gsize)(dash - n) : strlen(n);
		for (guint i = 0; i < G_N_ELEMENTS(LANGUAGES) && hit == NULL; i++) {
			const char *code = LANGUAGES[i].code;
			if (g_ascii_strncasecmp(code, n, baselen) == 0 &&
			    (code[baselen] == '\0' || code[baselen] == '-'))
				hit = code;
		}
	}

	g_free(n);
	return hit;
}

const char *
keysclicks_language_resolve(const char *want)
{
	if (want != NULL && *want != '\0')
		return resolve_one(want);

	// Auto-detect: g_get_language_names() returns the environment's preference
	// list (from LANGUAGE/LC_ALL/LANG), most-specific first, ending in "C".
	const char *const *names = g_get_language_names();
	for (guint i = 0; names != NULL && names[i] != NULL; i++) {
		const char *hit = resolve_one(names[i]);
		if (hit != NULL)
			return hit;
	}
	return NULL;
}
