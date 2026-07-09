# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 VibeCodeBlogger

gtk_dep = dependency('gtk4')
adwaita_dep = dependency('libadwaita-1')
layershell_dep = dependency('gtk4-layer-shell-0')
xkb_dep = dependency('xkbcommon')
xkbreg_dep = dependency('xkbregistry') # enumerate all available keyboard layouts
json_dep = dependency('json-glib-1.0')
evdev_dep = dependency('libevdev')
curl_dep = dependency('libcurl') # HTTPS POST for the in-app "Report a bug" feedback
dl_dep = meson.get_compiler('c').find_library('dl', required: false)

# Build-time version stamp: "<commit-count>.<short-hash>[+]". vcs_tag re-runs on
# every build, so the header always shows the version actually running. The count
# auto-increments per commit (our monotonic build number).
version_h = vcs_tag(
  input: 'version.h.in',
  output: 'version.h',
  replace_string: '@VCS_TAG@',
  command: ['sh', '-c',
    'printf "%s.%s%s" ' +
    '"$(git rev-list --count HEAD 2>/dev/null || echo 0)" ' +
    '"$(git rev-parse --short HEAD 2>/dev/null || echo nogit)" ' +
    '"$(git diff --quiet 2>/dev/null || echo +)"'],
  fallback: '0.nogit',
)

executable(
  'keysclicks',
  [
    version_h,
    'main.c',
    'keysclicks-overlay.c',
    'keysclicks-keymap.c',
    'keysclicks-backend.c',
    'keysclicks-settings.c',
    'keysclicks-window.c',
    'keysclicks-dock.c',
    'keysclicks-privacy.c',
    'keysclicks-feedback.c',
    'keysclicks-feedback-payload.c',
    'keysclicks-i18n.c',
    'keysclicks-languages.c',
    'keysclicks-layouts.c',
  ],
  include_directories: inc_public,
  dependencies: [
    gtk_dep,
    adwaita_dep,
    layershell_dep,
    xkb_dep,
    xkbreg_dep,
    json_dep,
    evdev_dep,
    curl_dep,
    dl_dep,
  ],
  # Tell the GUI where the privileged backend is installed + the semver.
  c_args: [
    '-DKEYSCLICKS_BACKEND_PATH="' + backend_path + '"',
    '-DKEYSCLICKS_VERSION="' + meson.project_version() + '"',
  ],
  install: true,
)
