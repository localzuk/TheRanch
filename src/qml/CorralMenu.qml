// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import org.theranch.core

// The corral's options menu (⋯ button, right-click on the title or empty space,
// or right-click on a drawer tab). Every item writes straight to the store; the
// corral repaints from the model, so there's no local state to keep in sync.
QQC2.Menu {
    id: corralMenu

    required property var corral // CorralContent
    required property var host

    readonly property string corralId: corral.corralId

    // An in-window popup rather than a separate native window: simpler on
    // layer-shell, and the host widens the input region while we're open.
    popupType: QQC2.Popup.Item
    onOpened: host.popupOpened()
    onClosed: host.popupClosed()

    QQC2.MenuItem {
        text: i18n("Rename…")
        icon.name: "edit-rename"
        onTriggered: corralMenu.corral.startRename()
    }
    QQC2.MenuItem {
        text: i18n("Background…")
        icon.name: "preferences-desktop-wallpaper"
        onTriggered: corralMenu.corral.openBackgroundDialog()
    }
    QQC2.MenuItem {
        text: i18n("Show Background")
        checkable: true
        checked: corralMenu.corral.showBackground
        onTriggered: Corrals.setValue(corralMenu.corralId, "showBackground", checked)
    }
    QQC2.MenuItem {
        text: i18n("Show Title")
        checkable: true
        checked: corralMenu.corral.showTitle
        onTriggered: Corrals.setValue(corralMenu.corralId, "showTitle", checked)
    }
    QQC2.MenuItem {
        text: i18n("Lock Position and Size")
        icon.name: "object-locked"
        checkable: true
        checked: corralMenu.corral.locked
        enabled: !Ranch.allLocked // the tray's "Lock All" overrides per-corral locks
        onTriggered: Corrals.setValue(corralMenu.corralId, "locked", checked)
    }

    QQC2.Menu {
        title: i18n("Icon Size")
        popupType: QQC2.Popup.Item

        Repeater {
            model: [
                { label: i18n("Small"), size: 32 },
                { label: i18n("Medium"), size: 48 },
                { label: i18n("Large"), size: 64 },
                { label: i18n("Huge"), size: 96 },
            ]
            delegate: QQC2.MenuItem {
                required property var modelData
                text: modelData.label
                checkable: true
                autoExclusive: true
                checked: corralMenu.corral.iconSize === modelData.size
                onTriggered: Corrals.setValue(corralMenu.corralId, "iconSize", modelData.size)
            }
        }
    }

    QQC2.Menu {
        title: corralMenu.corral.dock === "" ? i18n("Dock as Drawer") : i18n("Drawer Edge")
        popupType: QQC2.Popup.Item

        Repeater {
            model: [
                { label: i18n("Left Edge"), edge: "left" },
                { label: i18n("Right Edge"), edge: "right" },
                { label: i18n("Top Edge"), edge: "top" },
                { label: i18n("Bottom Edge"), edge: "bottom" },
            ]
            delegate: QQC2.MenuItem {
                required property var modelData
                text: modelData.label
                checkable: true
                autoExclusive: true
                checked: corralMenu.corral.dock === modelData.edge
                onTriggered: {
                    // Docking from the menu (rather than by dragging to the edge).
                    if (corralMenu.corral.dock === "") {
                        // Start the drawer where the corral currently sits along that edge.
                        const p = corralMenu.corral.mapToItem(null, 0, 0);
                        const vertical = modelData.edge === "left" || modelData.edge === "right";
                        Corrals.setValue(corralMenu.corralId, "dockOffset", vertical ? p.y : p.x);
                    }
                    Corrals.setValue(corralMenu.corralId, "dock", modelData.edge);
                }
            }
        }
    }

    // Only meaningful for drawers. Each drawer's own setting; nothing global is
    // inherited (the tray's "Put All Drawers…" actions just set every drawer once).
    QQC2.Menu {
        title: i18n("Drawer Stacking")
        popupType: QQC2.Popup.Item
        enabled: corralMenu.corral.dock !== ""

        Repeater {
            model: [
                { label: i18n("Above Windows"), value: "top" },
                { label: i18n("Below Windows"), value: "bottom" },
            ]
            delegate: QQC2.MenuItem {
                required property var modelData
                text: modelData.label
                checkable: true
                autoExclusive: true
                checked: corralMenu.corral.corralModel.drawerLayer === modelData.value
                onTriggered: Corrals.setValue(corralMenu.corralId, "drawerLayer", modelData.value)
            }
        }
    }

    QQC2.MenuItem {
        visible: corralMenu.corral.dock !== ""
        height: visible ? implicitHeight : 0 // invisible menu items still take space otherwise
        text: i18n("Undock to Desktop")
        icon.name: "window-restore"
        // The stored x/y were kept while docked, so it returns to where it was.
        onTriggered: Corrals.setValue(corralMenu.corralId, "dock", "")
    }

    QQC2.MenuSeparator {}

    QQC2.MenuItem {
        text: i18n("Hide Corral")
        icon.name: "view-hidden"
        onTriggered: Corrals.setValue(corralMenu.corralId, "hidden", true) // back via the tray menu
    }
    QQC2.MenuItem {
        text: i18n("Open in File Manager")
        icon.name: "system-file-manager"
        onTriggered: Ranch.openFolder(corralMenu.corral.folderUrl)
    }

    QQC2.MenuSeparator {}

    QQC2.MenuItem {
        text: i18n("Delete Corral…")
        icon.name: "edit-delete"
        onTriggered: corralMenu.corral.askDelete()
    }
}
