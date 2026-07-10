# Contributing to the On-Screen Keyboard & Mouse-Click Visualizer — Always-On-Top Overlay for Linux/Wayland (COSMIC, sway, Hyprland, KDE Plasma, wlroots)

Thanks for your interest in improving the On-Screen Keyboard & Mouse-Click Visualizer — Always-On-Top Overlay for Linux/Wayland (COSMIC, sway, Hyprland, KDE Plasma, wlroots),
the always-on-top keyboard and mouse-click overlay for Wayland. Contributions of
all sizes are welcome.

## Building from source

Build dependencies (Debian / Pop!_OS):

```sh
sudo apt install meson ninja-build gcc \
  libgtk-4-dev libadwaita-1-dev libgtk4-layer-shell-dev libinput-dev libudev-dev \
  libxkbcommon-dev libjson-glib-dev libevdev-dev
```

Configure and build:

```sh
meson setup build
meson compile -C build
```

To install system-wide (needed for the privileged input backend and its polkit
policy):

```sh
./scripts/install.sh
```

## How to submit changes

We use a simple branch → pull request flow:

1. Create a feature branch off `main` (for example `feature/short-description`).
2. Make your change. Keep commits focused, with messages in the form
   `type(scope): summary` (English or Russian).
3. Open a pull request against `main` and describe what changed and why.
4. A maintainer reviews and merges (rebase + delete branch).

Please make sure `meson compile -C build` succeeds before opening the PR.

## Source headers

Every source file starts with a two-line SPDX header:

```c
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 VibeCodeBlogger
```

Keep this header when you add new files.

## Good first issues

New contributors are especially welcome to pick up work labelled
**good first issue**. See the "Ideas and good first issues" section in the
[README](README.md) for a starting list, or open an issue to discuss a new one.

## Licensing of contributions

The On-Screen Keyboard & Mouse-Click Visualizer — Always-On-Top Overlay for Linux/Wayland (COSMIC, sway, Hyprland, KDE Plasma, wlroots) is licensed under the **Apache License,
Version 2.0**. Unless you state otherwise, any contribution you intentionally
submit for inclusion in the work is provided under the terms of Apache-2.0
Section 5 ("Submission of Contributions"), without any additional terms or
conditions.
