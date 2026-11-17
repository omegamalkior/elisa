#!/bin/sh
# Installs everything needed to build Elisa and bundle it as a portable AppImage (Arch Linux container).
set -eu

echo "Installing build tools and Elisa's dependencies..."
pacman -Syu --noconfirm --needed \
	base-devel cmake ninja git extra-cmake-modules kdoctools qt6-tools \
	elisa breeze breeze-icons kirigami-addons qqc2-desktop-style qt6-multimedia-ffmpeg \
	kimageformats qt6-imageformats fuse3 pipewire-audio qt6ct

echo "Building the patched Elisa from this branch..."
cmake -S . -B build -G Ninja \
	-DCMAKE_INSTALL_PREFIX=/usr \
	-DCMAKE_BUILD_TYPE=Release \
	-DBUILD_TESTING=OFF \
	-DCMAKE_DISABLE_FIND_PACKAGE_LIBVLC=ON   # use Qt Multimedia (bundled cleanly) instead of VLC
cmake --build build
cmake --install build   # replaces Arch's Elisa with ours

echo "Installing debloated packages (smaller AppImage)..."
get-debloated-pkgs --add-common --prefer-nano
