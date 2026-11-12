// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Effects
import org.kde.kirigami as Kirigami
import org.theranch.core

// One icon inside a corral: icon + two-line label, plus the mouse behaviour you'd
// expect from the Plasma desktop (select, open, drag out, right-click menu).
// Roles (name, iconName, url) come from FolderModel via the page's PageModel.
Item {
    id: del

    required property string name
    required property string iconName
    required property url url
    required property var owner // the IconPager: selection, iconSize

    readonly property bool selected: owner.isSelected(url)

    width: GridView.view.cellWidth
    height: GridView.view.cellHeight

    Rectangle {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing / 2
        radius: Kirigami.Units.cornerRadius
        color: Kirigami.Theme.highlightColor
        opacity: del.selected ? 0.5 : (mouse.containsMouse ? 0.25 : 0)
        Behavior on opacity { NumberAnimation { duration: Kirigami.Units.shortDuration } }
    }

    Kirigami.Icon {
        id: icon
        // Centred by hand and rounded: anchors.horizontalCenter would put the icon on a
        // half pixel whenever (cell width - icon size) is odd, blurring it.
        x: Math.round((parent.width - width) / 2)
        y: Kirigami.Units.smallSpacing
        width: del.owner.iconSize
        height: del.owner.iconSize
        source: del.iconName
        fallback: "unknown"
        active: mouse.containsMouse
    }

    QQC2.Label {
        anchors {
            top: icon.bottom; topMargin: Kirigami.Units.smallSpacing
            left: parent.left; right: parent.right
            leftMargin: Kirigami.Units.smallSpacing; rightMargin: Kirigami.Units.smallSpacing
        }
        text: del.name
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        color: "white"
        // Each shadowed label needs its own offscreen buffer, so only while it's
        // actually on screen (not in a closed drawer). `visible` is the effective one.
        layer.enabled: visible
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: "black"
            shadowBlur: 0.4
            shadowVerticalOffset: 1
            shadowHorizontalOffset: 0
        }
    }

    QQC2.ToolTip.text: del.name
    QQC2.ToolTip.visible: mouse.containsMouse && !mouse.pressed
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    MouseArea {
        id: mouse
        property point pressPos
        property bool dragStarted: false

        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        // Selection happens on press (not click), like file managers: pressing an
        // unselected icon selects it so a drag that follows carries the right files.
        onPressed: m => {
            pressPos = Qt.point(m.x, m.y);
            dragStarted = false;
            if (m.button === Qt.RightButton) {
                if (!del.selected) del.owner.select(del.url);
                Ranch.showFileMenu(del.owner.selectedUrls(), del, Qt.point(m.x, m.y));
                return;
            }
            if (m.modifiers & Qt.ControlModifier) {
                del.owner.toggle(del.url);
            } else if (!del.selected) {
                del.owner.select(del.url);
            }
        }

        onPositionChanged: m => {
            if (dragStarted || !(pressedButtons & Qt.LeftButton)) return;
            if (Math.hypot(m.x - pressPos.x, m.y - pressPos.y) < Qt.styleHints.startDragDistance) return;
            dragStarted = true;
            // Blocks until the drop; the file is moved by whoever accepts it.
            Ranch.startDrag(del.owner.selectedUrls(), del.iconName, del.owner.iconSize);
            // The native drag swallowed our release; reset the press state.
            enabled = false;
            enabled = true;
        }

        // Open on single- or double-click according to the KDE setting
        // (System Settings → General Behaviour), which the platform theme exposes as
        // singleClickActivation. Ctrl/Shift-click only changes the selection.
        onClicked: m => {
            if (m.button === Qt.LeftButton && !dragStarted && Qt.styleHints.singleClickActivation
                    && !(m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) {
                Ranch.open(del.url);
            }
        }
        onDoubleClicked: m => {
            if (m.button === Qt.LeftButton && !Qt.styleHints.singleClickActivation) {
                Ranch.open(del.url);
            }
        }
    }
}
