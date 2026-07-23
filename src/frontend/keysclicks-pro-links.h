// public/src/frontend/keysclicks-pro-links.h
// Privacy Pro in-app links + edition label. Pure (no GTK/GLib) so the settings
// window can unit-test the store/help URLs and the Free/Pro predicate headless.
#ifndef KEYSCLICKS_PRO_LINKS_H
#define KEYSCLICKS_PRO_LINKS_H

#include <stdbool.h>

// The Privacy Pro store page (Gumroad). The "Automatic password hiding" row and
// the "Get PRO" button open this. Keep it in lockstep with the site/README.
#define KEYSCLICKS_PRO_STORE_URL "https://vibecodeblogger.gumroad.com/l/xhqau" /* i18n-ignore */

// How to make browsers expose their password fields to the plugin (the site's
// PRO section documents --force-renderer-accessibility / GNOME_ACCESSIBILITY=1).
#define KEYSCLICKS_PRO_BROWSER_HELP_URL \
	"https://vibecodeblogger-public.github.io/screenkey-wayland-alternative-with-mouse-clicks-public/#pro" /* i18n-ignore */

// Edition tag shown after the version in the header: "Pro" once the paid
// automatic-hiding provider is loaded, otherwise "Free". A short brand-ish tag,
// intentionally left untranslated (identifier-like).
static inline const char *keysclicks_pro_edition(bool provider_loaded)
{
	return provider_loaded ? "Pro" : "Free"; /* i18n-ignore */
}

#endif // KEYSCLICKS_PRO_LINKS_H
