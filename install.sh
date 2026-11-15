#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds The Ranch into ~/.local and enables it at login.
set -euo pipefail

cd "$(dirname "$0")"
PREFIX="${PREFIX:-$HOME/.local}"

if ! command -v cmake >/dev/null; then
    echo "Missing build tool: cmake"
    echo "On Arch/CachyOS:  sudo pacman -S --needed cmake extra-cmake-modules"
    exit 1
fi

# CMake itself reports anything else that's missing (ECM, Qt, KDE Frameworks,
# LayerShellQt), wherever this distribution keeps them.
if ! cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="$PREFIX"; then
    echo
    echo "Configuring failed; see the messages above for what's missing."
    echo "On Arch/CachyOS the build needs at least:  sudo pacman -S --needed cmake extra-cmake-modules"
    exit 1
fi
cmake --build build -j"$(nproc)"
cmake --install build

# A path as one argument of a .desktop Exec= line: quoted, with the characters the
# Desktop Entry spec reserves inside quotes escaped, backslashes then doubled once
# more (every string value is unescaped once before Exec is parsed), and "%" doubled.
exec_quote() {
    local s=$1
    s=${s//\\/\\\\}
    s=${s//\"/\\\"}
    s=${s//\`/\\\`}
    s=${s//\$/\\\$}
    s=${s//\\/\\\\}
    s=${s//%/%%}
    printf '"%s"' "$s"
}

# Prints org.theranch.desktop with absolute Exec paths: ~/.local/bin isn't always on
# PATH for the session/portal. (Plain string replacement, so no character in $PREFIX
# can be mistaken for part of a pattern.)
desktop_entry() {
    local bin line
    bin=$(exec_quote "$PREFIX/bin/theranch")
    while IFS= read -r line || [[ -n $line ]]; do
        if [[ $line == Exec=theranch* ]]; then
            line="Exec=$bin${line#Exec=theranch}"
        fi
        printf '%s\n' "$line"
    done < org.theranch.desktop
}

desktop_entry > "$PREFIX/share/applications/org.theranch.desktop"
mkdir -p "$HOME/.config/autostart"
desktop_entry > "$HOME/.config/autostart/org.theranch.desktop"

echo
echo "Installed to $PREFIX/bin/theranch (starts automatically at login)."
echo "Start it now with:  \"$PREFIX/bin/theranch\" --new-corral"
