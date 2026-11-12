// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.theranch.core

// The pasture: one per screen, a BOTTOM-layer surface covering the screen's
// available area (above the Plasma desktop, below application windows). It holds
// the free (undocked) corrals and the "draw a new corral" overlay.
//
// Only the corrals' rectangles accept input (see maskTimer); clicks anywhere else
// fall through to the Plasma desktop as if we weren't here.
Window {
    id: win

    required property var targetScreen

    // --- host API --------------------------------------------------------
    // CorralFrame/DrawerFrame/CorralContent receive this window as `host` and call
    // these, so the frames don't need to know which layer they're on. Both window
    // types implement the same API.
    //
    // popupCount:  open in-window popups (menus, dialogs). While > 0 the input
    //              region covers the whole window, so clicking outside a menu
    //              reaches the menu to close it rather than falling through.
    property int popupCount: 0
    // Whether this surface sits above normal windows. Native dialogs (separate
    // windows) can't appear above such a surface, so in-window ones are used here.
    readonly property bool aboveWindows: false // BOTTOM layer: below normal windows
    property string dockHint: "" // edge glowing while a corral is dragged into it

    // Stacking: whichever corral/drawer was used most recently is drawn on top.
    property int topZ: 0
    function nextZ() { return ++topZ; }

    function popupOpened() { popupCount++; requestMaskUpdate(); }
    function popupClosed() { popupCount = Math.max(0, popupCount - 1); requestMaskUpdate(); }
    function requestMaskUpdate() { maskTimer.restart(); }

    // Does a corral whose `screen` is `name` belong on this window's screen?
    function ownsScreen(name) {
        const screens = Qt.application.screens;
        for (let i = 0; i < screens.length; ++i) {
            if (screens[i].name === name) {
                return name === targetScreen.name;
            }
        }
        // Corral's screen isn't connected: show it on the first (primary) screen.
        return screens.length > 0 && screens[0].name === targetScreen.name;
    }

    // Keeps a stored rect visible on this screen (e.g. after a resolution change)
    // without modifying the stored geometry itself.
    function clampRect(r) {
        const w = Math.min(r.width, width);
        const h = Math.min(r.height, height);
        return Qt.rect(Math.max(0, Math.min(r.x, width - w)), Math.max(0, Math.min(r.y, height - h)), w, h);
    }

    // Other visible corrals' rectangles, for edge magnetism.
    function peerRects(exceptId) {
        const out = [];
        for (let i = 0; i < corrals.count; ++i) {
            const it = corrals.itemAt(i);
            if (it && it.active && it.corralId !== exceptId) {
                out.push(Qt.rect(it.x, it.y, it.width, it.height));
            }
        }
        return out;
    }

    // ----------------------------------------------------------------------
    screen: targetScreen
    color: "transparent"

    // A full-screen surface holds full-screen GPU buffers even when it's entirely
    // transparent (several MB each at 2560×1440), so a window with nothing to show
    // hides itself and gives them back. Example: with every corral docked above
    // windows, the pasture and the below-windows drawer layer both stay hidden.
    property bool ready: false   // set once the layer surface is configured
    property int activeCount: 0  // corrals/drawers shown here; updated by maskTimer
    visible: ready && (activeCount > 0 || popupCount > 0 || Ranch.drawMode)
    onVisibleChanged: if (!visible) releaseResources() // drop scene-graph caches too
    title: "The Ranch"

    Component.onCompleted: {
        Ranch.configureLayer(win, false, targetScreen.name);
        // The compositor sizes an all-edges-anchored layer surface itself; this is
        // just a sensible first guess (and what non-Wayland test runs use).
        width = targetScreen.width;
        height = targetScreen.height;
        ready = true;
        requestMaskUpdate();
    }

    onWidthChanged: requestMaskUpdate()
    onHeightChanged: requestMaskUpdate()

    Connections {
        target: Ranch
        function onDrawModeChanged() {
            Ranch.setExclusiveKeyboard(win, Ranch.drawMode);
            if (Ranch.drawMode) {
                drawLayer.forceActiveFocus();
            }
            win.requestMaskUpdate();
        }
        function onAllHiddenChanged() { win.requestMaskUpdate(); }
    }

    // Recomputes the input region. Zero-interval and restarted on every change, so a
    // burst of geometry updates (dragging) costs one setMask per frame, not dozens.
    Timer {
        id: maskTimer
        interval: 0
        onTriggered: {
            if (Ranch.drawMode || win.popupCount > 0) {
                win.activeCount = Math.max(win.activeCount, 1); // stay shown; recounted afterwards
                Ranch.setInputRegion(win, [Qt.rect(0, 0, win.width, win.height)]);
                return;
            }
            const rects = [];
            const m = 6; // resize handles straddle the frame edge
            for (let i = 0; i < corrals.count; ++i) {
                const it = corrals.itemAt(i);
                if (it && it.active) {
                    rects.push(Qt.rect(it.x - m, it.y - m, it.width + 2 * m, it.height + 2 * m));
                }
            }
            win.activeCount = rects.length;
            Ranch.setInputRegion(win, rects);
        }
    }

    // One CorralFrame per corral in the store; frames that don't belong here
    // (docked, hidden, other screen) are simply invisible and outside the mask.
    Repeater {
        id: corrals
        model: Corrals
        delegate: CorralFrame {
            host: win
        }
    }

    // Glow on the edge a dragged corral will dock to.
    DockGlow {
        edge: win.dockHint
    }

    // --- draw-a-new-corral mode -----------------------------------------
    Item {
        id: drawLayer
        anchors.fill: parent
        visible: Ranch.drawMode
        focus: Ranch.drawMode

        Keys.onEscapePressed: Ranch.drawMode = false

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.25)
        }

        QQC2.Label {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Kirigami.Units.gridUnit * 2
            text: i18n("Drag to draw a new corral — Esc or right-click to cancel")
            color: "white"
            font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.3
        }

        Rectangle {
            id: band
            visible: drawArea.drawing
            radius: Kirigami.Units.cornerRadius
            color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.25)
            border.color: Kirigami.Theme.highlightColor
            border.width: 2

            QQC2.Label {
                anchors.centerIn: parent
                text: Math.round(band.width) + " × " + Math.round(band.height)
                color: band.width >= 120 && band.height >= 90 ? "white" : Kirigami.Theme.negativeTextColor
            }
        }

        MouseArea {
            id: drawArea
            property point start
            property bool drawing: false

            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.CrossCursor

            onPressed: mouse => {
                if (mouse.button === Qt.RightButton) {
                    Ranch.drawMode = false;
                    return;
                }
                start = Qt.point(mouse.x, mouse.y);
                band.x = mouse.x; band.y = mouse.y; band.width = 0; band.height = 0;
                drawing = true;
            }
            onPositionChanged: mouse => {
                if (!drawing) {
                    return;
                }
                band.x = Math.min(start.x, mouse.x);
                band.y = Math.min(start.y, mouse.y);
                band.width = Math.abs(mouse.x - start.x);
                band.height = Math.abs(mouse.y - start.y);
            }
            onReleased: mouse => {
                if (!drawing) {
                    return;
                }
                drawing = false;
                if (band.width >= 120 && band.height >= 90) {
                    Corrals.create(win.targetScreen.name, band.x, band.y, band.width, band.height);
                    Ranch.drawMode = false;
                }
            }
        }
    }
}
