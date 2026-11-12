// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

// The glow along a screen edge that says "release here to dock as a drawer".
// Shown by whichever window the drag is happening in: the pasture (moving a free
// corral) or a drawer window (tearing a drawer off and dropping it on an edge).
Rectangle {
    id: glow

    required property string edge // "", "left", "right", "top", "bottom"
    readonly property bool vertical: edge === "left" || edge === "right"

    visible: edge !== ""
    z: 1000 // above every corral/drawer in the window
    width: vertical ? Kirigami.Units.gridUnit : parent.width
    height: vertical ? parent.height : Kirigami.Units.gridUnit
    x: edge === "right" ? parent.width - width : 0
    y: edge === "bottom" ? parent.height - height : 0
    gradient: Gradient {
        orientation: glow.vertical ? Gradient.Horizontal : Gradient.Vertical
        GradientStop {
            position: 0
            color: glow.edge === "left" || glow.edge === "top" ? Kirigami.Theme.highlightColor : "transparent"
        }
        GradientStop {
            position: 1
            color: glow.edge === "right" || glow.edge === "bottom" ? Kirigami.Theme.highlightColor : "transparent"
        }
    }
}
