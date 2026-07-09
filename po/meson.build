# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 VibeCodeBlogger
#
# gettext translation catalogs. `po/LINGUAS` lists the 31 supported language
# codes (in the canonical order shared with the reference project) and
# `po/POTFILES` lists the sources scanned for translatable `_()` strings.
#
# Targets this creates:
#   ninja keysclicks-pot        -> regenerate po/keysclicks.pot from POTFILES
#   ninja keysclicks-update-po  -> merge the .pot into every po/<code>.po
#   ninja install               -> compile each po/<code>.po and install
#                                  <localedir>/<code>/LC_MESSAGES/keysclicks.mo
#
# The endonym table in src/frontend/keysclicks-languages.c is intentionally NOT
# in POTFILES: native language names are shown verbatim, never translated.
i18n.gettext(
  gettext_package,
  preset: 'glib',
  args: [
    '--from-code=UTF-8',
    '--package-name=' + gettext_package,
    '--copyright-holder=VibeCodeBlogger',
    '--msgid-bugs-address=https://github.com/VibeCodeBlogger-Public/screenkey-wayland-alternative-with-mouse-clicks-public',
  ],
)
