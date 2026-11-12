// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.theranch.core

// A drawer layer: a surface covering the screen's available area where corrals
// docked to an edge live as drawers. There are two per screen (see Main.qml):
// `topLayer: true` puts it on the TOP layer so drawers slide out *over*
// application windows; `topLayer: false` puts it on the BOTTOM layer, for drawers
// the user prefers below windows, like the desktop.
//
// On the TOP layer, being above every window makes the input region critical: only the drawer tabs,
// plus the panels of drawers that are currently open, may accept input. Otherwise
// this transparent full-screen surface would swallow every click on the desktop.
Window {
    id: win

    required property var targetScreen
    required property bool topLayer

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
    readonly property bool aboveWindows: topLayer
    property string dockHint: "" // edge glowing while a drawer is torn off and dragged

    // Stacking: whichever corral/drawer was used most recently is drawn on top.
    property int topZ: 0
    function nextZ() { return ++topZ; }

    function popupOpened() { popupCount++; requestMaskUpdate(); }
    function popupClosed() { popupCount = Math.max(0, popupCount - 1); requestMaskUpdate(); }
    function requestMaskUpdate() { maskTimer.restart(); }

    function ownsScreen(name) {
        const screens = Qt.application.screens;
        for (let i = 0; i < screens.length; ++i) {
            if (screens[i].name === name) {
                return name === targetScreen.name;
            }
        }
        return screens.length > 0 && screens[0].name === targetScreen.name;
    }

    function clampRect(r) {
        const w = Math.min(r.width, width);
        const h = Math.min(r.height, height);
        return Qt.rect(Math.max(0, Math.min(r.x, width - w)), Math.max(0, Math.min(r.y, height - h)), w, h);
    }

    function peerRects(exceptId) { return []; } // no edge magnetism between drawers

    // ----------------------------------------------------------------------
    screen: targetScreen
    color: "transparent"

    // A full-screen surface holds full-screen GPU buffers even when it's entirely
    // transparent (several MB each at 2560×1440), so a window with nothing to show
    // hides itself and gives them back. Example: with every corral docked above
    // windows, the pasture and the below-windows drawer layer both stay hidden.
    property bool ready: false   // set once the layer surface is configured
    property int activeCount: 0  // corrals/drawers shown here; updated by maskTimer
    visible: ready && (activeCount > 0 || popupCount > 0)
    onVisibleChanged: if (!visible) releaseResources() // drop scene-graph caches too
    title: topLayer ? "The Ranch Drawers" : "The Ranch Drawers (below windows)"

    Component.onCompleted: {
        Ranch.configureLayer(win, topLayer, targetScreen.name);
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
        function onAllHiddenChanged() { win.requestMaskUpdate(); }
    }

    // Input region = each active drawer's inputRects (its tab, plus its panel while
    // open or animating). DrawerFrame requests an update on every animation step,
    // so the clickable area follows the slide.
    Timer {
        id: maskTimer
        interval: 0
        onTriggered: {
            if (win.popupCount > 0) {
                Ranch.setInputRegion(win, [Qt.rect(0, 0, win.width, win.height)]);
                return;
            }
            let rects = [];
            let shown = 0;
            for (let i = 0; i < drawers.count; ++i) {
                const it = drawers.itemAt(i);
                if (it && it.active) {
                    rects = rects.concat(it.inputRects);
                    ++shown;
                }
            }
            win.activeCount = shown;
            Ranch.setInputRegion(win, rects);
        }
    }

    Repeater {
        id: drawers
        model: Corrals
        delegate: DrawerFrame {
            host: win
        }
    }

    // Glow on the edge a torn-off drawer will re-dock to if released.
    DockGlow {
        edge: win.dockHint
    }
}
