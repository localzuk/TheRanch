#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds The Ranch into ~/.local and enables it at login.
set -euo pipefail

cd "$(dirname "$0")"
PREFIX="${PREFIX:-$HOME/.local}"

missing=()
command -v cmake >/dev/null || missing+=(cmake)
[[ -e /usr/share/ECM/cmake/ECMConfig.cmake ]] || missing+=(extra-cmake-modules)
if ((${#missing[@]})); then
    echo "Missing build tools: ${missing[*]}"
    echo "On Arch/CachyOS:  sudo pacman -S --needed ${missing[*]}"
    exit 1
fi

cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build build -j"$(nproc)"
cmake --install build

# Absolute Exec paths: ~/.local/bin isn't always on PATH for the session/portal.
APPFILE="$PREFIX/share/applications/org.theranch.desktop"
sed -i "s|^Exec=theranch|Exec=$PREFIX/bin/theranch|" "$APPFILE"

mkdir -p "$HOME/.config/autostart"
sed "s|^Exec=theranch|Exec=$PREFIX/bin/theranch|" org.theranch.desktop > "$HOME/.config/autostart/org.theranch.desktop"

echo
echo "Installed to $PREFIX/bin/theranch (starts automatically at login)."
echo "Start it now with:  $PREFIX/bin/theranch --new-corral"
