#!/usr/bin/env bash
# public/scripts/build-deb.sh
# Собирает устанавливаемый двойным кликом .deb из meson-проекта:
#   meson (--prefix=/usr) → install в staging → DEBIAN/control (deps из ldd) → dpkg-deb.
# Работает и локально, и в release-CI (ubuntu-latest). Не требует debhelper.
# Использование:  scripts/build-deb.sh [VERSION]   (VERSION по умолчанию из meson.build)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"   # корень public/
cd "$HERE"

VERSION="${1:-$(sed -n "s/^[[:space:]]*version:[[:space:]]*'\([^']*\)'.*/\1/p" meson.build | head -1)}"
: "${VERSION:?не удалось определить версию}"
ARCH="$(dpkg --print-architecture)"
BUILD="$HERE/build-deb"
STAGE="$HERE/deb-stage"
OUT="$HERE/keysclicks_${VERSION}_${ARCH}.deb"

echo "==> сборка keysclicks $VERSION ($ARCH)"
rm -rf "$BUILD" "$STAGE" "$OUT"
meson setup "$BUILD" "$HERE" --prefix=/usr --buildtype=release >/dev/null
meson compile -C "$BUILD"
DESTDIR="$STAGE" meson install -C "$BUILD" >/dev/null

# Рантайм-зависимости: dpkg-shlibdeps даёт ЧИСТЫЕ прямые зависимости с версиями
# (по DT_NEEDED бинарей), а не всю транзитивную простыню ldd.
echo "==> вычисляю зависимости (dpkg-shlibdeps)"
SHLIB="$(mktemp -d)"
mkdir -p "$SHLIB/debian"
printf 'Source: keysclicks\nPackage: keysclicks\nArchitecture: any\n' > "$SHLIB/debian/control"
DEPENDS="$(
  cd "$SHLIB" && dpkg-shlibdeps -O --ignore-missing-info \
    "$STAGE/usr/bin/keysclicks" "$STAGE/usr/libexec/keysclicks-input" 2>/dev/null \
    | sed -n 's/^shlibs:Depends=//p' || true
)"
rm -rf "$SHLIB"
[ -n "$DEPENDS" ] || { echo "ОШИБКА: dpkg-shlibdeps не дал зависимостей" >&2; exit 1; }
# gtk4-layer-shell на dev-машинах часто собран из исходников (без пакета) → shlibdeps его
# пропустит (--ignore-missing-info); на apt-системе (CI/юзер) пакет есть. Гарантируем ключевую зависимость.
case ",${DEPENDS// /}," in *,libgtk4-layer-shell0,*|*libgtk4-layer-shell0*) ;; *) DEPENDS="$DEPENDS, libgtk4-layer-shell0" ;; esac
# pkexec — бинарь спавнит бэкенд через него (не слинкованная либа); провайдер этой системы
pkexec_pkg="$(dpkg -S "$(command -v pkexec 2>/dev/null)" 2>/dev/null | head -1 | cut -d: -f1 || true)"
[ -n "$pkexec_pkg" ] && DEPENDS="$DEPENDS, $pkexec_pkg"
echo "    Depends: $DEPENDS"

INSTALLED_KB="$(du -sk "$STAGE/usr" | cut -f1)"
mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: keysclicks
Version: $VERSION
Architecture: $ARCH
Maintainer: VibeCodeBlogger <289294152+VibeCodeBlogger@users.noreply.github.com>
Installed-Size: $INSTALLED_KB
Depends: $DEPENDS
Section: utils
Priority: optional
Homepage: https://github.com/VibeCodeBlogger-Public/screenkey-wayland-alternative-with-mouse-clicks-public
Description: On-screen keyboard & mouse-click overlay for Wayland
 Always-on-top overlay that shows the keys you press and the mouse buttons you
 click, for screencasts, tutorials and streams. Built natively for Wayland with
 GTK4 and gtk4-layer-shell (COSMIC, sway, Hyprland, KDE Plasma, wlroots); reads
 input via libinput and translates keys with xkbcommon. A screenkey / KeyCastr
 alternative for Linux/Wayland.
EOF

echo "==> dpkg-deb --build"
dpkg-deb --root-owner-group --build "$STAGE" "$OUT" >/dev/null
echo "==> готово: $OUT"
dpkg-deb --info "$OUT" | sed 's/^/    /'
