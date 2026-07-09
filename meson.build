project(
  'keysclicks',
  'c',
  version: '0.1.0',
  license: 'Apache-2.0',
  meson_version: '>= 0.60.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

# Absolute path where the privileged input backend is installed. It is compiled
# into the GUI (so it knows what to launch through pkexec) and substituted into
# the polkit policy (so pkexec authorizes exactly that binary).
prefix = get_option('prefix')
backend_path = prefix / get_option('libexecdir') / 'keysclicks-input'

conf_data = configuration_data()
conf_data.set('BACKEND_PATH', backend_path)

# Internationalization (gettext). The UI opens in the user's native language via
# setlocale(LC_ALL, "") in main.c; catalogs (.mo) install under KEYSCLICKS_LOCALEDIR.
# See po/ and docs/dev/i18n.md.
i18n = import('i18n')
gettext_package = 'keysclicks'
localedir = prefix / get_option('localedir')
add_project_arguments(
  '-DGETTEXT_PACKAGE="' + gettext_package + '"',
  '-DKEYSCLICKS_LOCALEDIR="' + localedir + '"',
  language: 'c',
)

# Public plugin ABI header for external privacy providers, installed to
# ${includedir}/keysclicks/ so a separate module can build against it.
inc_public = include_directories('include')
install_headers('include/keysclicks-privacy-provider.h', subdir: 'keysclicks')

subdir('po')            # translation catalogs (gettext) — 31 languages
subdir('src/backend')   # keysclicks-input: libinput -> JSON events
subdir('src/frontend')  # keysclicks: GTK4 + gtk4-layer-shell overlay
subdir('data')          # polkit policy + desktop entry

# Unit tests are NOT part of the published product. They live outside this folder,
# in the private kitchen (repo-root tests/), and are built by the root meson project
# against these sources. Publication copies this folder as-is, with no filter (§6).
