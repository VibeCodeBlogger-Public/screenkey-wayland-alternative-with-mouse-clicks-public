#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 VibeCodeBlogger
#
# Build the On-Screen Keys & Clicks Visualizer and install it system-wide
# (prefix /usr) so that:
#   - the privileged input backend lives in /usr/libexec/keysclicks-input, and
#   - its polkit policy is installed, letting the GUI launch it via pkexec.
#
# The build runs as your user; only the final "install" step is elevated.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

# Absolute build directory. pkexec runs the install step with a clean
# environment and a reset working directory, so a relative "-C build" would fail
# with "Install data not found". The absolute path is expanded here, before
# pkexec, so it stays correct regardless of pkexec's working directory.
build_dir="$repo_root/build"

cat <<'EOF'
On-Screen Keys & Clicks Visualizer — install
--------------------------------------------
Build dependencies (Debian / Pop!_OS):
  sudo apt install meson ninja-build gcc \
    libgtk-4-dev libadwaita-1-dev libgtk4-layer-shell-dev libinput-dev libudev-dev \
    libxkbcommon-dev libjson-glib-dev libevdev-dev

EOF

echo "==> Configuring (prefix=/usr) ..."
meson setup "$build_dir" --prefix=/usr --buildtype=release "$@"

echo "==> Building ..."
meson compile -C "$build_dir"

echo "==> Installing system-wide via pkexec (you will be prompted) ..."
# --no-rebuild: everything is already compiled above as your user; do not let
# the elevated step rebuild and leave root-owned files in the build tree.
pkexec meson install -C "$build_dir" --no-rebuild

echo
echo "Done. Launch the overlay with:  keysclicks"
