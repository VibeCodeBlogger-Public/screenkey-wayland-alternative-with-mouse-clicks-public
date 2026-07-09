// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger
//
// COSMIC dock pinning. COSMIC stores the pinned launchers of its dock in a RON
// array of app-id strings at
//   ~/.config/cosmic/com.system76.CosmicAppList/v1/favorites
// e.g.
//   [
//       "firefox",
//       "com.system76.CosmicFiles",
//   ]
// COSMIC watches this file (cosmic-config / inotify) and updates the dock on its
// own. Editing it directly is an UNOFFICIAL hack — not a public API — and could
// break with a future COSMIC update, so every entry point here is guarded by
// keysclicks_dock_available().

#include "keysclicks-dock.h"

#include <gio/gio.h>

static char *
favorites_path(void)
{
	return g_build_filename(g_get_user_config_dir(), "cosmic",
				"com.system76.CosmicAppList", "v1", "favorites", NULL);
}

gboolean
keysclicks_dock_available(void)
{
	char *path = favorites_path();
	gboolean ok = g_file_test(path, G_FILE_TEST_EXISTS);
	g_free(path);
	return ok;
}

// Extract every quoted string from the RON file into an owned GPtrArray. The
// favourites file only contains the array of app-id strings, so scanning for
// quoted tokens is sufficient (and tolerant of the trailing comma and layout).
static GPtrArray *
read_favorites(void)
{
	GPtrArray *list = g_ptr_array_new_with_free_func(g_free);
	char *path = favorites_path();
	char *contents = NULL;
	if (g_file_get_contents(path, &contents, NULL, NULL)) {
		for (const char *p = contents; *p != '\0';) {
			if (*p != '"') {
				p++;
				continue;
			}
			p++; // opening quote
			GString *token = g_string_new(NULL);
			while (*p != '\0' && *p != '"') {
				if (*p == '\\' && p[1] != '\0') {
					g_string_append_c(token, p[1]);
					p += 2;
				} else {
					g_string_append_c(token, *p);
					p++;
				}
			}
			if (*p == '"')
				p++; // closing quote
			g_ptr_array_add(list, g_string_free(token, FALSE));
		}
		g_free(contents);
	}
	g_free(path);
	return list;
}

gboolean
keysclicks_dock_is_pinned(void)
{
	GPtrArray *list = read_favorites();
	gboolean found = FALSE;
	for (guint i = 0; i < list->len; i++) {
		if (g_strcmp0(g_ptr_array_index(list, i), KEYSCLICKS_APP_ID) == 0) {
			found = TRUE;
			break;
		}
	}
	g_ptr_array_free(list, TRUE);
	return found;
}

static void
backup_favorites(void)
{
	char *path = favorites_path();
	char *contents = NULL;
	gsize length = 0;
	if (g_file_get_contents(path, &contents, &length, NULL)) {
		char *backup = g_strconcat(path, ".bak", NULL);
		g_file_set_contents(backup, contents, length, NULL);
		g_free(backup);
		g_free(contents);
	}
	g_free(path);
}

static gboolean
write_favorites(GPtrArray *list, GError **error)
{
	GString *out = g_string_new("[\n");
	for (guint i = 0; i < list->len; i++) {
		char *escaped = g_strescape(g_ptr_array_index(list, i), NULL);
		g_string_append_printf(out, "    \"%s\",\n", escaped);
		g_free(escaped);
	}
	g_string_append(out, "]\n");

	char *path = favorites_path();
	gboolean ok = g_file_set_contents(path, out->str, out->len, error);
	g_free(path);
	g_string_free(out, TRUE);
	return ok;
}

gboolean
keysclicks_dock_set_pinned(gboolean pinned, GError **error)
{
	GPtrArray *list = read_favorites();

	gboolean present = FALSE;
	for (guint i = 0; i < list->len; i++) {
		if (g_strcmp0(g_ptr_array_index(list, i), KEYSCLICKS_APP_ID) == 0) {
			present = TRUE;
			break;
		}
	}

	gboolean changed = FALSE;
	if (pinned && !present) {
		g_ptr_array_add(list, g_strdup(KEYSCLICKS_APP_ID));
		changed = TRUE;
	} else if (!pinned && present) {
		// Remove every occurrence (guards against pre-existing duplicates).
		for (guint i = list->len; i > 0; i--) {
			if (g_strcmp0(g_ptr_array_index(list, i - 1), KEYSCLICKS_APP_ID) == 0)
				g_ptr_array_remove_index(list, i - 1);
		}
		changed = TRUE;
	}

	gboolean ok = TRUE;
	if (changed) {
		backup_favorites();
		ok = write_favorites(list, error);
	}
	g_ptr_array_free(list, TRUE);
	return ok;
}
