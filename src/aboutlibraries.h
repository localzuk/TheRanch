// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The About dialog's "Libraries" tab: credits every third-party library The Ranch
// links against or loads at run time, with its copyright, its licence terms, a
// button per licence that opens the full text in its own window, and a button to
// the project's website. (KDE's stock "Components" tab lists only Qt and KDE
// Frameworks as a whole, and shows no licences.)

#pragma once

class QWidget;

namespace AboutLibraries
{
// Builds the page. License windows it opens are children of `parent`'s window, so
// they close with the About dialog.
QWidget *createPage(QWidget *parent);
}
