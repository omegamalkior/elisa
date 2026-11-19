#!/bin/sh
# Bundles the installed Elisa with everything it needs into an AppImage that runs on any Linux.
set -eu

ARCH=$(uname -m)
VERSION="26.08.3-album-artist"
export ARCH VERSION
export OUTPATH=./dist
export OUTNAME="Elisa-album-artist-$ARCH.AppImage"
export DESKTOP=/usr/share/applications/org.kde.elisa.desktop
ICON=$(ls /usr/share/icons/hicolor/256x256/apps/elisa.png /usr/share/icons/hicolor/150x150/apps/elisa.png \
           /usr/share/icons/hicolor/128x128/apps/elisa.png 2>/dev/null | head -n 1)
export ICON
export DEPLOY_OPENGL=1
export DEPLOY_PIPEWIRE=1
export OPTIMIZE_LAUNCH=1

quick-sharun \
	/usr/bin/elisa                        \
	/usr/lib/libQt6QuickControls2Basic*   \
	/usr/lib/qt6/plugins/kf6              \
	/usr/lib/qt6/plugins/kiconthemes6/*/* \
	/usr/share/kf6                        \
	/usr/share/color-schemes              \
	/usr/share/icons/breeze               \
	/usr/share/icons/breeze-dark

echo 'PACKAGE_TYPE=appimage' >> ./AppDir/.env

quick-sharun --make-appimage

# quick check that the AppImage starts
quick-sharun --test ./dist/*.AppImage
