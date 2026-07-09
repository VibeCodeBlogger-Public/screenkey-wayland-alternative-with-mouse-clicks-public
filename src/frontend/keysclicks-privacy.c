// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger
//
// Fail-open loader for the optional external privacy provider plugin. The plugin
// is a closed, separately distributed .so; the base app only depends on the
// frozen ABI in <keysclicks-privacy-provider.h>. If anything goes wrong (no
// file, wrong ABI, bad symbol, failed init) the app logs to stderr and runs with
// no provider — the manual privacy toggle keeps working regardless.

#include "keysclicks-privacy.h"

#include <dlfcn.h>

#include "keysclicks-privacy-provider.h"

struct _KeysClicksPrivacy {
	void *handle;                     // dlopen handle, or NULL
	const KcPrivacyProvider *provider; // borrowed from the plugin, or NULL
	void *ctx;                        // provider instance context
};

typedef const KcPrivacyProvider *(*GetProviderFunc)(void);

// Build the ordered list of candidate plugin paths.
static GPtrArray *
candidate_paths(void)
{
	GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);

	const char *env = g_getenv("KEYSCLICKS_PRIVACY_PLUGIN");
	if (env != NULL && *env != '\0')
		g_ptr_array_add(paths, g_strdup(env));

	g_ptr_array_add(paths, g_build_filename(g_get_home_dir(), ".local", "lib",
						"keysclicks",
						"libkeysclicks_privacy_pro.so", NULL));
	g_ptr_array_add(paths,
			g_strdup("/usr/lib/keysclicks/libkeysclicks_privacy_pro.so"));
	return paths;
}

// Try one candidate path. On success fills self and returns TRUE; otherwise logs
// and returns FALSE (leaving self untouched).
static gboolean
try_load(KeysClicksPrivacy *self, const char *path)
{
	if (!g_file_test(path, G_FILE_TEST_EXISTS))
		return FALSE; // silent: a missing optional plugin is normal

	void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (handle == NULL) {
		g_warning("keysclicks: privacy plugin '%s' failed to load: %s", path,
			  dlerror());
		return FALSE;
	}

	GetProviderFunc get_provider =
		(GetProviderFunc)dlsym(handle, "kc_privacy_provider_get");
	if (get_provider == NULL) {
		g_warning("keysclicks: privacy plugin '%s' has no kc_privacy_provider_get",
			  path);
		dlclose(handle);
		return FALSE;
	}

	const KcPrivacyProvider *provider = get_provider();
	if (provider == NULL) {
		g_warning("keysclicks: privacy plugin '%s' returned no provider", path);
		dlclose(handle);
		return FALSE;
	}
	if (provider->abi_version != KC_PRIVACY_ABI_VERSION) {
		g_warning("keysclicks: privacy plugin '%s' ABI %u != expected %u; ignoring",
			  path, provider->abi_version, KC_PRIVACY_ABI_VERSION);
		dlclose(handle);
		return FALSE;
	}

	void *ctx = NULL;
	if (provider->init != NULL) {
		ctx = provider->init();
		if (ctx == NULL) {
			g_warning("keysclicks: privacy plugin '%s' init() failed", path);
			dlclose(handle);
			return FALSE;
		}
	}

	self->handle = handle;
	self->provider = provider;
	self->ctx = ctx;
	g_message("keysclicks: loaded privacy provider '%s' from %s",
		  provider->name != NULL ? provider->name : "(unnamed)", path);
	return TRUE;
}

KeysClicksPrivacy *
keysclicks_privacy_new(void)
{
	KeysClicksPrivacy *self = g_new0(KeysClicksPrivacy, 1);

	GPtrArray *paths = candidate_paths();
	for (guint i = 0; i < paths->len; i++) {
		if (try_load(self, g_ptr_array_index(paths, i)))
			break;
	}
	g_ptr_array_free(paths, TRUE);

	if (self->provider == NULL)
		g_message("keysclicks: no privacy provider plugin loaded (manual toggle "
			  "only)");
	return self;
}

void
keysclicks_privacy_free(KeysClicksPrivacy *self)
{
	if (self == NULL)
		return;
	if (self->provider != NULL && self->provider->shutdown != NULL)
		self->provider->shutdown(self->ctx);
	if (self->handle != NULL)
		dlclose(self->handle);
	g_free(self);
}

gboolean
keysclicks_privacy_loaded(KeysClicksPrivacy *self)
{
	return self != NULL && self->provider != NULL;
}

const char *
keysclicks_privacy_name(KeysClicksPrivacy *self)
{
	if (self == NULL || self->provider == NULL)
		return NULL;
	return self->provider->name;
}

gboolean
keysclicks_privacy_should_mask(KeysClicksPrivacy *self)
{
	if (self == NULL || self->provider == NULL ||
	    self->provider->should_mask_keys == NULL)
		return FALSE;
	return self->provider->should_mask_keys(self->ctx) ? TRUE : FALSE;
}
