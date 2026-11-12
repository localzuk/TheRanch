// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Root of the UI. Creates two transparent layer surfaces per connected screen:
//   PastureWindow: BOTTOM layer, above the Plasma desktop and below windows.
//                  Free corrals live here.
//   DrawerWindow:  docked corrals (drawers). Two per screen: one on the TOP layer
//                  (above application windows, so drawers slide out over whatever
//                  you're doing), and one on the BOTTOM layer for drawers the user
//                  wants below windows, like the desktop. Each drawer picks one via
//                  its own drawerLayer setting.
// Both list *every* corral; each delegate decides whether it belongs on that
// screen and layer (see `active` in CorralFrame/DrawerFrame). Moving a corral
// between them is just a change to its `dock` or `screen` field.
//
// The Instantiator follows screen hot-plugging: windows come and go with monitors.
QtObject {
    id: root

    property Instantiator perScreen: Instantiator {
        model: Qt.application.screens

        delegate: QtObject {
            required property var modelData

            property PastureWindow pasture: PastureWindow {
                targetScreen: modelData
            }
            property DrawerWindow drawersAbove: DrawerWindow {
                targetScreen: modelData
                topLayer: true
            }
            property DrawerWindow drawersBelow: DrawerWindow {
                targetScreen: modelData
                topLayer: false
            }
        }
    }
}
