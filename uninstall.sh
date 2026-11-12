#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
# SPDX-License-Identifier: GPL-3.0-or-later
# Removes The Ranch, first putting every corral's icons back on the desktop.
# Your settings (~/.config/theranch) are kept unless you pass --purge.
set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local}"
BIN="$PREFIX/bin/theranch"

if [[ -x "$BIN" ]]; then
    # Works whether or not The Ranch is running: a running instance quits (and
    # parks its files as it does); otherwise this process parks them itself.
    "$BIN" --restore-desktop || true
    for _ in $(seq 1 50); do pgrep -x theranch >/dev/null || break; sleep 0.1; done
fi

# Empty corral folders only (rmdir never deletes anything with files in it).
find "$HOME/Desktop/.ranch" -mindepth 1 -maxdepth 1 -type d -empty -delete 2>/dev/null || true
rmdir "$HOME/Desktop/.ranch" 2>/dev/null || true

rm -f "$BIN" \
      "$PREFIX/share/applications/org.theranch.desktop" \
      "$HOME/.config/autostart/org.theranch.desktop"

if [[ "${1:-}" == "--purge" ]]; then
    rm -rf "$HOME/.config/theranch" "$HOME/.local/state/theranch" "$HOME/.local/share/theranch"
fi

if [[ -d "$HOME/Desktop/.ranch" ]]; then
    echo "Note: ~/Desktop/.ranch still contains files; check it in Dolphin (Ctrl+H shows hidden folders)."
fi
echo "The Ranch has been removed."
