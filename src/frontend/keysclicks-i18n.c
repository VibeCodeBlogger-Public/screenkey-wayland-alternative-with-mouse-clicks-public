// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger

#include "keysclicks-i18n.h"

#include "keysclicks-languages.h"

#include <libintl.h>
#include <locale.h>

void
keysclicks_i18n_init(void)
{
	setlocale(LC_ALL, "");
	bindtextdomain(GETTEXT_PACKAGE, KEYSCLICKS_LOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);
}

void
keysclicks_i18n_apply_language(const char *preferred)
{
	const char *code = keysclicks_language_resolve(preferred);
	if (code != NULL) {
		char *list = g_strconcat(code, ":en", NULL);
		g_setenv("LANGUAGE", list, TRUE);
		g_free(list);
	}
#ifdef __GLIBC__
	// Drop gettext's in-memory catalog cache so a runtime language switch takes
	// effect (glibc-specific; a harmless no-op elsewhere).
	{
		extern int _nl_msg_cat_cntr;
		++_nl_msg_cat_cntr;
	}
#endif
}
