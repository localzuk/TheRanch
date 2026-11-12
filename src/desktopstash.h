// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// DesktopStash: keeps the desktop complete whenever The Ranch isn't running.
//
// While running, a corral's files live in ~/Desktop/.ranch/<id>/, hidden from the
// Plasma desktop. That's only acceptable while we're there to show them, so:
//
//   park()    moves every corral's files back onto ~/Desktop and records where each
//             one came from in parked.json. Runs on every way we stop: quit, logout
//             (SIGTERM), and, via the guard process, crashes and SIGKILL.
//   collect() reverses park() at start-up, moving each recorded file back into its
//             corral.
//
// A file is only collected if it's *the same file* we parked, identified by its
// inode number, which both rename(2) and reboots preserve. So a different file that
// happens to have the same name (say you saved a new "notes.txt" while we were
// closed) stays on the desktop, and a parked file you renamed is still recognised.
// Records that can't be matched are kept (for 30 days), never silently dropped, and
// the last record acted on is backed up as parked.json.prev. Name clashes when
// parking get a "(1)" style name; the original name is restored on collect.
//
// The "collected" marker file means "files are currently inside corrals". The guard
// parks only if the marker exists, so a clean quit (which already parked and
// removed the marker) is left alone. A QLockFile serialises park/collect between the
// app, the guard and `theranch --restore-desktop`.
//
// Everything here is plain filesystem work: no QML, no D-Bus, no Plasma involvement.
// That's deliberate, because it has to work during logout when plasmashell may
// already be gone.

#pragma once

#include <QStringList>

namespace DesktopStash
{
// Moves all corral contents to ~/Desktop. Safe to call when nothing is collected.
// Returns the number of entries moved.
int park();

// Moves parked files back into their corrals. Only corrals in `knownIds` are
// collected into; files for unknown (deleted) corrals stay on the desktop.
// Writes the "collected" marker. Returns the number of entries moved.
int collect(const QStringList &knownIds);

// Runs in a separate, detached process (`theranch --guard <pid>`): blocks until
// `pid` exits, then parks if the app didn't do it itself. Returns an exit code.
int runGuard(qint64 pid);

// ~/.local/state/theranch
QString stateDir();
}
