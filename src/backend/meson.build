# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 VibeCodeBlogger

libinput_dep = dependency('libinput')
libudev_dep = dependency('libudev')

# Installed into libexecdir; it is only ever launched by the GUI through pkexec,
# not run directly by the user.
executable(
  'keysclicks-input',
  'keysclicks-input.c',
  dependencies: [libinput_dep, libudev_dep],
  install: true,
  install_dir: get_option('libexecdir'),
)
