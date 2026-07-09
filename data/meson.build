# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 VibeCodeBlogger

# Substitute the installed backend path into the polkit policy so pkexec
# authorizes exactly the binary we install.
configure_file(
  input: 'io.github.vibecodeblogger.KeysAndClicksVisualizer.policy.in',
  output: 'io.github.vibecodeblogger.KeysAndClicksVisualizer.policy',
  configuration: conf_data,
  install: true,
  install_dir: get_option('datadir') / 'polkit-1' / 'actions',
)

install_data(
  'io.github.vibecodeblogger.KeysAndClicksVisualizer.desktop',
  install_dir: get_option('datadir') / 'applications',
)

# Application icon in the hicolor theme. The window/dock icon is resolved by the
# compositor from the GTK application id (Icon= in the .desktop / app_id) against
# the installed icon theme, so we ship every hicolor raster size. The 1024px
# master, the .ico and gen-icons.py under data/icons/ are provenance-only and
# are intentionally NOT installed (Pillow is a dev-time tool, not a build dep).
icon_sizes = ['16', '24', '32', '48', '64', '128', '256', '512']
foreach size : icon_sizes
  install_data(
    'icons' / 'hicolor' / size + 'x' + size / 'apps' / 'io.github.vibecodeblogger.KeysAndClicksVisualizer.png',
    install_dir: get_option('datadir') / 'icons' / 'hicolor' / size + 'x' + size / 'apps',
  )
endforeach

# Refresh the icon-theme cache after install so the new icon is picked up.
gnome = import('gnome')
gnome.post_install(gtk_update_icon_cache: true)
