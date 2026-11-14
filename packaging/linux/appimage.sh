#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Makes the AppImage from a release build (docs/building.md §1.3):
#
#   packaging/linux/appimage.sh BUILD_DIR OUTPUT_DIR
#
# Needs linuxdeploy and its Qt plugin (github.com/linuxdeploy) in LINUXDEPLOY_DIR, ~/tools/linuxdeploy if not given;
# the versions in use are in docs/building.md. The AppImage carries Qt and the other libraries, all but those of the
# base system (glibc, the C++ library, X11, Wayland, OpenGL), so it runs on distributions at least as new as the one it
# is built on. It cannot install the udev rule: the README gives the user the command.
set -euo pipefail

build=$(realpath "$1")
out=$(realpath "$2")
tools=${LINUXDEPLOY_DIR:-$HOME/tools/linuxdeploy}
version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build/CMakeCache.txt")
[ -n "$version" ] || { echo "no project version in $build/CMakeCache.txt" >&2; exit 1; }

# The image is put together on the Linux file system: a Windows drive under WSL loses the files' modes.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
app="$work/AppDir"
DESTDIR="$app" cmake --install "$build" --prefix /usr --strip >/dev/null
rm -rf "$app/usr/lib/udev"  # of no use inside the image
# The licence in full: the copyright file points to Debian's copy, which other systems lack. linuxdeploy adds the
# copyright files of the libraries it collects under usr/share/doc.
cp "$(dirname "$0")/../../LICENSE" "$app/usr/share/doc/star-oscilloscope/LICENSE"

# Qt's Wayland plugin besides X11, as the .deb recommends it. The Qt plugin of linuxdeploy leaves out the shell
# integration that opens its windows (xdg-shell) and the window frame drawn where the desktop draws none (bradient,
# GNOME): without them a Wayland session showed no window. Qt's own translations come with the Qt plugin
# (usr/translations, where its qt.conf points Qt).
export QMAKE=${QMAKE:-$(command -v qmake6 || command -v qmake)}
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so"
qt_plugins=$("$QMAKE" -query QT_INSTALL_PLUGINS)
wayland=(wayland-shell-integration/libxdg-shell.so wayland-decoration-client/libbradient.so)
for plugin in "${wayland[@]}"; do
    mkdir -p "$app/usr/plugins/${plugin%/*}"
    cp "$qt_plugins/$plugin" "$app/usr/plugins/$plugin"
done
# libusb too: linuxdeploy leaves it to the system (its exclude list), and without it the program would not start.
libusb=$(ldd "$app/usr/bin/star-oscilloscope" | awk '/libusb-1\.0/ { print $3 }')
mkdir -p "$app/usr/lib"
cp -L "$libusb" "$app/usr/lib/libusb-1.0.so.0"
libusb_package=$(dpkg-query -S "$(realpath "$libusb")" | cut -d: -f1)
mkdir -p "$app/usr/share/doc/$libusb_package"
cp "/usr/share/doc/$libusb_package/copyright" "$app/usr/share/doc/$libusb_package/"

export LINUXDEPLOY_OUTPUT_VERSION="$version"
export OUTPUT="$out/star-oscilloscope-$version-x86_64.AppImage"
export APPIMAGE_EXTRACT_AND_RUN=1  # the tools are AppImages too; containers have no FUSE
export PATH="$tools:$PATH"         # where linuxdeploy finds its Qt plugin
"$tools/linuxdeploy-x86_64.AppImage" --appdir "$app" \
    --desktop-file "$app/usr/share/applications/star-oscilloscope.desktop" \
    --icon-file "$app/usr/share/icons/hicolor/256x256/apps/star-oscilloscope.png" \
    --deploy-deps-only "$app/usr/plugins/wayland-shell-integration" \
    --deploy-deps-only "$app/usr/plugins/wayland-decoration-client" \
    --deploy-deps-only "$app/usr/lib/libusb-1.0.so.0" \
    --plugin qt --output appimage
echo "$OUTPUT"
