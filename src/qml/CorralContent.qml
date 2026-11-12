// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Effects
import org.kde.kirigami as Kirigami
import org.theranch.core

// What's *inside* a corral, shared by free corrals (CorralFrame) and drawers
// (DrawerFrame): the title bar (or a hover grip when the title is hidden), the
// paginated icons, and the corral's menu and dialogs.
//
// It doesn't know how it's being moved. Pressing and dragging the title bar just
// emits movePressed/moveDragged/moveReleased, and the frame decides what that means
// (move a free corral, or slide/undock a drawer).
Item {
    id: content

    required property var host
    required property string corralId
    required property string title
    required property color bgColor
    required property bool showTitle
    required property bool showBackground
    required property bool locked
    required property string dock
    required property int iconSize
    required property url folderUrl
    property bool frameHovered: false // drives the fade-in of the ⋯ button, grip and page arrows
    property var corralModel // the Corrals model entry (for the Background dialog)

    readonly property bool dropHovered: pager.dropHovered
    // Frames watch this: drawers stay open and corrals come to the front while true.
    readonly property bool menuOpen: (menuObj !== null && menuObj.opened)
                                     || (deleteObj !== null && deleteObj.opened)
                                     || (backgroundObj !== null && backgroundObj.opened)

    // Menus and dialogs are created when first needed and destroyed once closed:
    // there can be many corrals, each with a full Background dialog, and almost all
    // of the time none of them is open.
    property var menuObj: null
    property var deleteObj: null
    property var backgroundObj: null

    function popupFor(name, component) {
        if (!content[name]) {
            content[name] = component.createObject(content);
        }
        return content[name];
    }
    // If this content is unloaded while a popup is open (e.g. the menu's own "Dock"
    // or "Drawer Stacking" moves the corral to another window), the popup is
    // destroyed without ever emitting closed(). Tell the window, or it would keep its
    // whole-window input region and swallow every click.
    Component.onDestruction: {
        for (const obj of [menuObj, deleteObj, backgroundObj]) {
            if (obj && obj.opened) host.popupClosed();
        }
    }

    // Deferred (Qt.callLater) because it's called from the popup's own closed signal.
    function releasePopup(name) {
        const obj = content[name];
        if (obj && !obj.opened) {
            content[name] = null;
            obj.destroy();
        }
    }
    // Whole pixels only: anything below the title bar inherits its height, and a
    // fractional offset makes every icon and label render slightly soft.
    readonly property real headerHeight: showTitle ? Math.round(Kirigami.Units.gridUnit * 1.8) : 0
    readonly property int gripHeight: 10

    // Move gestures, in window (scene) coordinates.
    signal movePressed(point scenePos)
    signal moveDragged(point scenePos)
    signal moveReleased(point scenePos)
    signal moveClicked()
    signal moveCanceled()

    // Inline rename in the title bar (double-click, menu, or right after drawing).
    // Renaming a corral whose title is hidden turns the title back on.
    function startRename() {
        if (!showTitle) {
            Corrals.setValue(corralId, "showTitle", true);
        }
        renameField.text = title;
        renameField.visible = true;
        renameField.forceActiveFocus();
        renameField.selectAll();
    }

    function openMenu(item, x, y) {
        popupFor("menuObj", menuComponent).popup(item, x, y);
    }

    function openBackgroundDialog() {
        popupFor("backgroundObj", backgroundComponent).open();
    }

    function askDelete() {
        popupFor("deleteObj", deleteComponent).open();
    }

    // Shared move gesture used by the title bar and the grip. (Inline components
    // can't see this file's ids, hence the explicit `owner`.)
    // Nothing is reported as a drag until the pointer passes the system drag
    // threshold, so a plain click or double-click on the title doesn't nudge the
    // corral. Positions are reported in window coordinates (mapToItem(null, …)).
    component MoveArea: MouseArea {
        required property Item owner
        property point pressScene
        property bool moved: false
        preventStealing: true
        cursorShape: owner.locked ? Qt.ArrowCursor : (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: mouse => {
            if (mouse.button === Qt.RightButton) {
                owner.openMenu(this, mouse.x, mouse.y);
                return;
            }
            pressScene = mapToItem(null, mouse.x, mouse.y);
            moved = false;
            owner.movePressed(pressScene);
        }
        onPositionChanged: mouse => {
            if (!(pressedButtons & Qt.LeftButton)) return;
            const p = mapToItem(null, mouse.x, mouse.y);
            if (!moved && Math.hypot(p.x - pressScene.x, p.y - pressScene.y) < Qt.styleHints.startDragDistance) return;
            moved = true;
            owner.moveDragged(p);
        }
        onReleased: mouse => {
            if (mouse.button !== Qt.LeftButton) return;
            const p = mapToItem(null, mouse.x, mouse.y);
            owner.moveReleased(moved ? p : pressScene);
            if (!moved) owner.moveClicked();
        }
        onCanceled: owner.moveCanceled()
    }

    // ----------------------------------------------------------- title bar
    Item {
        id: header
        visible: content.showTitle
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: content.headerHeight

        MoveArea {
            owner: content
            anchors.fill: parent
            onDoubleClicked: content.startRename()
        }

        QQC2.Label {
            id: titleLabel
            anchors {
                left: parent.left; right: menuButton.left
                leftMargin: Kirigami.Units.largeSpacing
            }
            y: Math.round((parent.height - height) / 2) // centred, on a whole pixel
            visible: !renameField.visible
            text: content.title
            elide: Text.ElideRight
            font.bold: true
            // White with a soft shadow, like Plasma's desktop labels: readable on any
            // background colour or image the user picks.
            color: "white"
            // The shadow needs an offscreen buffer; don't keep one for hidden titles
            // (e.g. inside a closed drawer). `visible` is the effective visibility.
            layer.enabled: visible
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "black"
                shadowBlur: 0.3
                shadowVerticalOffset: 1
                shadowHorizontalOffset: 0
            }
        }

        QQC2.TextField {
            id: renameField
            visible: false // shown by startRename(); Enter, Esc or clicking away ends it
            anchors {
                left: parent.left; right: menuButton.left; verticalCenter: parent.verticalCenter
                leftMargin: Kirigami.Units.smallSpacing
            }
            onAccepted: {
                Corrals.setValue(content.corralId, "title", text.trim());
                visible = false;
            }
            Keys.onEscapePressed: visible = false
            onActiveFocusChanged: if (!activeFocus && visible) accepted()
        }

        QQC2.ToolButton {
            id: menuButton
            anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: Kirigami.Units.smallSpacing }
            icon.name: "overflow-menu"
            icon.color: "white"
            display: QQC2.AbstractButton.IconOnly
            text: i18n("Corral options")
            // Only visible on hover, to keep idle corrals clean (still clickable).
            opacity: content.frameHovered || content.menuOpen ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Kirigami.Units.shortDuration } }
            onClicked: content.openMenu(menuButton, 0, menuButton.height)
            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }

        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: Kirigami.Units.smallSpacing }
            height: 1
            color: Qt.rgba(1, 1, 1, 0.2)
            visible: content.showBackground
        }
    }

    // -------------------------------------- grip when the title is hidden
    // Without a title bar there'd be nothing to grab, so a thin strip along the top
    // acts as the move handle, with a small pill that fades in on hover.
    MoveArea {
        id: grip
        owner: content
        visible: !content.showTitle
        z: 2
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: content.gripHeight

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.3, Kirigami.Units.gridUnit * 4)
            height: 4
            radius: 2
            color: Qt.rgba(1, 1, 1, 0.7)
            opacity: content.frameHovered || grip.pressed ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Kirigami.Units.shortDuration } }
        }
    }

    // --------------------------------------------------------------- icons
    IconPager {
        id: pager
        anchors {
            left: parent.left; right: parent.right; bottom: parent.bottom
            top: content.showTitle ? header.bottom : parent.top
            topMargin: content.showTitle ? 0 : content.gripHeight
            margins: Kirigami.Units.smallSpacing
        }
        host: content.host
        folderUrl: content.folderUrl
        iconSize: content.iconSize
        hovered: content.frameHovered
        onEmptyAreaMenuRequested: (item, x, y) => content.openMenu(item, x, y)
    }

    Component {
        id: menuComponent
        CorralMenu {
            corral: content
            host: content.host
            onClosed: Qt.callLater(content.releasePopup, "menuObj")
        }
    }

    // Menus and dialogs are in-window popups (popupType Item), so each tells the
    // host window when it opens/closes and the window widens its input region.
    Component {
        id: backgroundComponent
        BackgroundDialog {
            corral: content
            corralModel: content.corralModel
            host: content.host
            onClosed: Qt.callLater(content.releasePopup, "backgroundObj")
        }
    }

    Component {
        id: deleteComponent
        QQC2.Dialog {
            id: deleteDialog
            parent: QQC2.Overlay.overlay
            anchors.centerIn: parent
            width: Kirigami.Units.gridUnit * 22
            popupType: QQC2.Popup.Item
            modal: true
            title: i18n("Delete “%1”?", content.title)
            standardButtons: QQC2.Dialog.Yes | QQC2.Dialog.Cancel
            onOpened: content.host.popupOpened()
            onClosed: {
                content.host.popupClosed();
                Qt.callLater(content.releasePopup, "deleteObj");
            }
            onAccepted: Corrals.remove(content.corralId)

            QQC2.Label {
                width: deleteDialog.availableWidth
                wrapMode: Text.Wrap
                text: i18np("Its icon will be moved back onto the desktop. No files are deleted.",
                            "Its %1 icons will be moved back onto the desktop. No files are deleted.",
                            pager.count)
            }
        }
    }
}
