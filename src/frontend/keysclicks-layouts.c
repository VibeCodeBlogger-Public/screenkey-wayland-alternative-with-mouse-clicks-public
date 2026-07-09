// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger

#include "keysclicks-layouts.h"

#include <string.h>
#include <xkbcommon/xkbregistry.h>

// Sort order: the plain "us" layout is always first (matches the reference tool and is
// the safe default), then everything else by its localized description.
static gint
cmp_layout(gconstpointer a, gconstpointer b)
{
	const KeysClicksLayout *la = a;
	const KeysClicksLayout *lb = b;
	gboolean us_a = g_strcmp0(la->layout, "us") == 0 && la->variant == NULL;
	gboolean us_b = g_strcmp0(lb->layout, "us") == 0 && lb->variant == NULL;
	if (us_a != us_b)
		return us_a ? -1 : 1;
	return g_utf8_collate(la->name != NULL ? la->name : "",
			      lb->name != NULL ? lb->name : "");
}

// Enumerate every xkb layout+variant the registry knows (evdev ruleset) into a heap
// array of KeysClicksLayout. Owned strings; the array lives for the process (cached).
static GArray *
build_layouts(void)
{
	GArray *arr = g_array_new(FALSE, FALSE, sizeof(KeysClicksLayout));

	struct rxkb_context *ctx = rxkb_context_new(RXKB_CONTEXT_NO_FLAGS);
	if (ctx == NULL)
		return arr;
	if (!rxkb_context_parse_default_ruleset(ctx)) {
		rxkb_context_unref(ctx);
		return arr;
	}

	for (struct rxkb_layout *l = rxkb_layout_first(ctx); l != NULL;
	     l = rxkb_layout_next(l)) {
		const char *name = rxkb_layout_get_name(l); // xkb layout, e.g. "us"
		if (name == NULL)
			continue;
		const char *variant = rxkb_layout_get_variant(l);     // NULL for the base
		const char *desc = rxkb_layout_get_description(l);     // "English (US)"
		KeysClicksLayout entry = {
			.layout = g_strdup(name),
			.variant = variant != NULL ? g_strdup(variant) : NULL,
			.name = g_strdup(desc != NULL ? desc : name),
		};
		g_array_append_val(arr, entry);
	}
	rxkb_context_unref(ctx);

	g_array_sort(arr, cmp_layout);
	return arr;
}

const KeysClicksLayout *
keysclicks_layouts(guint *n_out)
{
	static GArray *cache; // built once, kept for the whole process
	if (cache == NULL)
		cache = build_layouts();
	if (n_out != NULL)
		*n_out = cache->len;
	return (const KeysClicksLayout *)cache->data;
}

int
keysclicks_layouts_index_of(const char *layout, const char *variant)
{
	if (layout == NULL || *layout == '\0')
		return -1; // system default — the picker maps this to its first row

	guint n = 0;
	const KeysClicksLayout *list = keysclicks_layouts(&n);
	const char *want_variant = (variant != NULL && *variant != '\0') ? variant : NULL;
	for (guint i = 0; i < n; i++) {
		if (g_strcmp0(list[i].layout, layout) == 0 &&
		    g_strcmp0(list[i].variant, want_variant) == 0)
			return (int)i;
	}
	return -1;
}
