// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Effects
import org.kde.kirigami as Kirigami
import org.theranch.core

// A corral docked to a screen edge, living in a DrawerWindow (above or below windows).
// It's made of two parts:
//   tab   - a small labelled handle that always sits on the edge. Hover or click
//           it to open; drag it along the edge to reposition, or away to undock.
//   panel - the corral itself (CorralContent), which slides out from the edge.
//
// This item fills the whole window but draws only those two parts; `inputRects`
// tells the window which bits should be clickable at the moment.
//
// Open/close is driven by several independent reasons (pinned, hovered, a file
// being dragged over it, a menu open, ...). See the "open state" section.
Item {
    id: drawer

    // model roles (areaWidth/Height are the size the user drew the corral)
    required property string corralId
    required property string title
    required property color bgColor
    required property bool showTitle
    required property bool showBackground
    required property bool hidden
    required property bool locked
    required property string screenName
    required property real areaWidth
    required property real areaHeight
    required property string dock
    required property real dockOffset
    required property string drawerLayer // "top" or "bottom"
    required property int iconSize
    required property url folderUrl
    required property var model // whole entry, for the background settings

    required property var host

    // Above or below windows: this drawer's own setting.
    readonly property bool onTop: drawerLayer !== "bottom"
    // Shown in this window only if docked, visible, on our screen, and on our layer;
    // every drawer exists in both drawer windows, and exactly one of them shows it.
    readonly property bool active: !Ranch.allHidden && !hidden && dock !== "" && host.ownsScreen(screenName)
                                   && onTop === host.aboveWindows
    readonly property bool effectiveLocked: locked || Ranch.allLocked

    // The CorralContent, or null while this drawer isn't shown here (see the Loader).
    readonly property var content: contentLoader.item
    readonly property bool contentDropHovered: content !== null && content.dropHovered
    readonly property bool contentMenuOpen: content !== null && content.menuOpen

    // ----------------------------------------------------------- geometry
    readonly property bool vertical: dock === "left" || dock === "right" // the edge is vertical
    // depth = how far it slides out; length = extent along the edge. Both come from the drawn size.
    // Whole pixels, so the panel's contents render crisply (see the panel's x/y too).
    // While a resize handle is dragged these follow liveDepth/liveLength instead.
    readonly property real depth: Math.round(resizing ? liveDepth
                                              : vertical ? Math.min(areaWidth, width * 0.9) : Math.min(areaHeight, height * 0.9))
    readonly property real length: Math.round(resizing ? liveLength
                                               : vertical ? Math.min(areaHeight, height) : Math.min(areaWidth, width))
    readonly property real edgeLength: vertical ? height : width

    // Position along the edge. While the tab is being dragged we follow liveOffset
    // and only write dockOffset to the store on release. Clamped so the drawer never
    // hangs off the end of the edge.
    property bool draggingTab: false
    property real liveOffset: 0
    readonly property real along: Math.round(Math.max(0, Math.min(draggingTab || resizing ? liveOffset : dockOffset, edgeLength - length)))

    readonly property real tabThickness: Math.round(Kirigami.Units.gridUnit * 1.5)
    readonly property real tabLength: Math.round(Math.min(length, Math.max(Kirigami.Units.gridUnit * 4,
                                                                tabLabel.implicitWidth + Kirigami.Units.gridUnit * 2.5)))
    // The tab tucks this far under the panel, just enough that no hairline gap shows
    // between them. Kept tiny because the panel may be see-through.
    readonly property real overlap: 1

    // -------------------------------------------------------- open state
    // Each flag is a separate reason to be open; `open` is true if any applies.
    //   pinned    - the user clicked the tab; stays open until clicked again.
    //   hoverOpen - the pointer rested on the tab/panel (openTimer delay).
    //   dragOpen  - a file is being dragged over it, so it can be dropped inside.
    //   keepOpen  - something we started (menu, drag, rename, tab drag) is still in
    //               progress. Native menus and drags take the pointer away, so
    //               without this the drawer would slide shut under them.
    property bool pinned: false
    property bool hoverOpen: false
    property bool dragOpen: false
    property real progress: 0 // 0 = closed, 1 = fully out
    readonly property bool hovered: tabHover.hovered || panelHover.hovered
    readonly property bool dragInside: tabDrop.containsDrag || contentDropHovered
    // Stay open while something we started is still in flight.
    readonly property bool keepOpen: progress > 0
                                     && (Ranch.nativeMenuOpen || Ranch.dragging || contentMenuOpen || draggingTab || tearing || resizing)
    readonly property bool open: active && (pinned || hoverOpen || dragOpen || keepOpen)

    // Set imperatively (not bound) so `keepOpen` can read progress without a binding loop.
    onOpenChanged: {
        progress = open ? 1 : 0;
        if (open) raise();
    }

    // Bring this drawer (panel + tab) above every other drawer on this screen, so
    // neighbouring tabs can't cover its title bar or options button.
    function raise() {
        if (z !== host.topZ || z === 0) z = host.nextZ();
    }

    Connections {
        target: drawer.content
        function onMenuOpenChanged() { if (drawer.content.menuOpen) drawer.raise(); }
    }
    Behavior on progress { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

    // Hover intent: a short delay before opening (so sweeping the mouse past the
    // edge doesn't pop drawers open) and a longer grace period before closing.
    onHoveredChanged: {
        if (hovered) {
            raise();
            closeTimer.stop();
            if (!open) openTimer.restart();
        } else {
            openTimer.stop();
            closeTimer.restart();
        }
    }
    onDragInsideChanged: {
        if (dragInside) {
            raise();
            closeTimer.stop();
            dragOpen = true;
        } else {
            closeTimer.restart();
        }
    }

    Timer {
        id: openTimer
        interval: 250
        onTriggered: drawer.hoverOpen = true
    }
    Timer {
        id: closeTimer
        interval: 600
        onTriggered: {
            if (!drawer.hovered && !drawer.dragInside) {
                drawer.hoverOpen = false;
                drawer.dragOpen = false;
            }
        }
    }

    // --------------------------------------------------------- input mask
    // Closed: just the tab is clickable. Open (or mid-animation): tab + panel.
    // Recomputed as the panel slides, so the clickable area tracks the animation.
    readonly property var inputRects: {
        if (tearing) return [Qt.rect(panel.x, panel.y, panel.width, panel.height)];
        const t = Qt.rect(tab.x, tab.y, tab.width, tab.height);
        return progress > 0 ? [Qt.rect(panel.x, panel.y, panel.width, panel.height), t] : [t];
    }
    onInputRectsChanged: host.requestMaskUpdate()
    onActiveChanged: {
        // Undocked/hidden: forget open state so it doesn't reappear already open.
        if (!active) { pinned = false; hoverOpen = false; dragOpen = false; }
        host.requestMaskUpdate();
    }

    // ------------------------------------------------------------ resizing
    // Open drawers can be resized in place, without undocking: the inner edge (away
    // from the screen edge) sets the depth, the two ends set the length along the
    // edge, and the inner corners do both. As with free corrals, the drag drives
    // local "live" values and the result is written to the store once, on release.
    // For left/right drawers depth is the corral's width and length its height;
    // for top/bottom drawers it's the other way round.
    property bool resizing: false
    property real liveDepth
    property real liveLength
    property point resizeStart
    property real startDepth
    property real startLength
    property real startOffset
    readonly property real minDepth: Math.max(120, iconSize * 2)
    readonly property real minLength: Math.max(90, iconSize + Kirigami.Units.gridUnit * 4)

    function beginResize(p) {
        raise();
        resizeStart = p;
        startDepth = depth; startLength = length; startOffset = along;
        liveDepth = depth; liveLength = length; liveOffset = along;
        resizing = true;
    }

    // byDepth: the inner edge moves; atStart/atEnd: the near/far end along the edge.
    function resizeTo(p, byDepth, atStart, atEnd) {
        const dAlong = vertical ? p.y - resizeStart.y : p.x - resizeStart.x;
        const dPerp = vertical ? p.x - resizeStart.x : p.y - resizeStart.y;
        if (byDepth) {
            // Away from the screen edge = deeper. That's +x/+y for left/top drawers.
            const grow = (dock === "left" || dock === "top") ? dPerp : -dPerp;
            const maxDepth = Math.round((vertical ? width : height) * 0.9);
            liveDepth = Math.max(minDepth, Math.min(startDepth + grow, maxDepth));
        }
        if (atEnd) {
            liveLength = Math.max(minLength, Math.min(startLength + dAlong, edgeLength - startOffset));
        }
        if (atStart) {
            const off = Math.max(0, Math.min(startOffset + dAlong, startOffset + startLength - minLength));
            liveOffset = off;
            liveLength = startLength + (startOffset - off);
        }
    }

    function endResize() {
        if (!resizing) return;
        const w = vertical ? liveDepth : liveLength;
        const h = vertical ? liveLength : liveDepth;
        Corrals.setGeometry(corralId, model.areaX, model.areaY, Math.round(w), Math.round(h));
        Corrals.setValue(corralId, "dockOffset", Math.round(liveOffset));
        resizing = false;
    }

    // ------------------------------------------------- tab drag / tear off
    // Shared by the tab's MouseArea and the panel's title bar (via CorralContent).
    // Movement along the edge repositions the drawer; pulling away from the edge past
    // a threshold *tears it off*. Positions are in window coordinates.
    //
    // Tearing off stays inside this window for the whole gesture: the panel itself
    // becomes the free corral, at its drawn size, and follows the pointer. Only on
    // release do we write the result (undocked, or re-docked to whichever edge it
    // was dropped on). Undocking mid-drag would hand the corral to the pasture window,
    // a different surface, and the pointer grab (and with it the drag) would end on
    // the spot, forcing a second drag.
    property point pressScene
    property real pressOffset
    property bool tearing: false
    property rect tearRect      // where the torn-off corral is, while dragging
    property point tearGrab     // pointer position within the corral, kept constant
    readonly property int edgeTrigger: 4

    // Which screen edge (if any) the pointer is pushing against.
    function edgeAt(p) {
        if (p.x <= edgeTrigger) return "left";
        if (p.x >= width - 1 - edgeTrigger) return "right";
        if (p.y <= edgeTrigger) return "top";
        if (p.y >= height - 1 - edgeTrigger) return "bottom";
        return "";
    }

    function gesturePressed(p) {
        raise();
        pressScene = p;
        pressOffset = along;
        liveOffset = along;
    }

    function gestureDragged(p) {
        if (effectiveLocked) return;
        if (tearing) {
            moveTear(p);
            return;
        }
        const dx = p.x - pressScene.x, dy = p.y - pressScene.y;
        // How far the pointer has moved away from the docked edge (into the screen).
        const away = dock === "left" ? dx : dock === "right" ? -dx : dock === "top" ? dy : -dy;
        if (away > Kirigami.Units.gridUnit * 2) {
            beginTear(p);
            return;
        }
        draggingTab = true;
        liveOffset = pressOffset + (vertical ? dy : dx);
    }

    function gestureReleased(p, wasClick) {
        if (tearing) {
            finishTear(p);
        } else if (draggingTab) {
            Corrals.setValue(corralId, "dockOffset", along);
            draggingTab = false;
        } else if (wasClick) {
            // Click the tab: pin open, or unpin and close.
            if (pinned) { pinned = false; hoverOpen = false; dragOpen = false; }
            else pinned = true;
        }
    }

    function beginTear(p) {
        const w = Math.min(areaWidth, width), h = Math.min(areaHeight, height);
        const inPanel = progress > 0 && pressScene.x >= panel.x && pressScene.x < panel.x + panel.width
                        && pressScene.y >= panel.y && pressScene.y < panel.y + panel.height;
        // Grabbed the open drawer's title bar: keep hold of the same spot. Grabbed
        // the tab: hold the corral by the middle of its title bar.
        tearGrab = inPanel ? Qt.point(Math.min(pressScene.x - panel.x, w - 1), Math.min(pressScene.y - panel.y, h - 1))
                           : Qt.point(w / 2, Kirigami.Units.gridUnit);
        draggingTab = false;
        tearing = true;
        raise();
        moveTear(p);
    }

    function moveTear(p) {
        const w = Math.min(areaWidth, width), h = Math.min(areaHeight, height);
        tearRect = Qt.rect(Math.round(Math.max(0, Math.min(p.x - tearGrab.x, width - w))),
                           Math.round(Math.max(0, Math.min(p.y - tearGrab.y, height - h))), w, h);
        host.dockHint = edgeAt(p);
    }

    // The gesture was taken away from us (e.g. the compositor grabbed the pointer):
    // leave the drawer where it was.
    function cancelGesture() {
        tearing = false;
        draggingTab = false;
        host.dockHint = "";
    }

    // Released: dock to the edge it was dropped on (same edge or another), or become
    // a free corral exactly where it was dropped.
    function finishTear(p) {
        const edge = edgeAt(p);
        const r = tearRect;
        const id = corralId;
        host.dockHint = "";
        tearing = false;
        pinned = false; hoverOpen = false; dragOpen = false;
        // Deferred: undocking unloads this drawer's contents, which may include the
        // MouseArea whose release handler we're running in right now.
        Qt.callLater(() => {
            Corrals.setGeometry(id, r.x, r.y, r.width, r.height);
            if (edge !== "") {
                Corrals.setValue(id, "dockOffset", (edge === "left" || edge === "right") ? r.y : r.x);
            }
            Corrals.setValue(id, "dock", edge);
        });
    }

    anchors.fill: parent
    visible: active

    // --------------------------------------------------------------- panel
    // Slides by moving its position: progress 0 puts it entirely past the screen
    // edge, progress 1 flush against it. pw/ph are its width/height for this edge.
    Item {
        id: panel
        readonly property real pw: drawer.vertical ? drawer.depth : drawer.length
        readonly property real ph: drawer.vertical ? drawer.length : drawer.depth

        // While being torn off it's simply the free corral, following the pointer.
        width: drawer.tearing ? drawer.tearRect.width : pw
        height: drawer.tearing ? drawer.tearRect.height : ph
        // Rounded every frame: mid-slide that's invisible, and at rest it guarantees
        // the open drawer's icons and text sit exactly on the pixel grid.
        x: drawer.tearing ? drawer.tearRect.x : Math.round(drawer.dock === "left" ? -pw * (1 - drawer.progress)
                    : drawer.dock === "right" ? drawer.width - pw * drawer.progress
                    : drawer.along)
        y: drawer.tearing ? drawer.tearRect.y : Math.round(drawer.dock === "top" ? -ph * (1 - drawer.progress)
                    : drawer.dock === "bottom" ? drawer.height - ph * drawer.progress
                    : drawer.along)
        visible: drawer.progress > 0 || drawer.tearing

        HoverHandler {
            id: panelHover
        }

        Rectangle {
            id: panelBg
            anchors.fill: parent
            radius: Kirigami.Units.cornerRadius
            // The user's colour and transparency, exactly as chosen. (Image backgrounds
            // are painted by CorralBackground on top; only "Show Background" off falls
            // back to a dark panel, so a drawer over windows is never invisible.)
            readonly property bool imageBg: drawer.showBackground && drawer.model.bgMode === "image"
            color: !drawer.showBackground ? Qt.rgba(0.12, 0.13, 0.15, 0.92)
                 : imageBg ? "transparent"
                 : drawer.bgColor
            border.width: 1
            border.color: drawer.contentDropHovered ? Kirigami.Theme.highlightColor : Qt.rgba(1, 1, 1, 0.2)
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: Qt.rgba(0, 0, 0, 0.5)
                // A shadow under a see-through panel would show through and darken it.
                shadowOpacity: panelBg.imageBg ? 0 : panelBg.color.a
                shadowBlur: 0.6
            }
        }

        // Image backgrounds (colour backgrounds are just panelBg's colour above).
        CorralBackground {
            anchors.fill: parent
            corral: drawer.model
            visible: panelBg.imageBg
        }

        // Every window has a delegate for every corral, but only one window shows each
        // corral, so the contents (folder listing, icons, menus) are only built where
        // this corral is actually active. Elsewhere it's a nearly free empty Item.
        Loader {
            id: contentLoader
            anchors.fill: parent
            active: drawer.active
            sourceComponent: Component {
                CorralContent {
                    corralModel: drawer.model
                    host: drawer.host
                    corralId: drawer.corralId
                    title: drawer.title
                    bgColor: drawer.bgColor
                    showTitle: drawer.showTitle
                    showBackground: true // drawers always have a panel behind the content
                    locked: drawer.effectiveLocked
                    dock: drawer.dock
                    iconSize: drawer.iconSize
                    folderUrl: drawer.folderUrl
                    frameHovered: panelHover.hovered

                    onMovePressed: p => drawer.gesturePressed(p)
                    onMoveDragged: p => drawer.gestureDragged(p)
                    onMoveReleased: p => drawer.gestureReleased(p, false)
            onMoveCanceled: drawer.cancelGesture()
                }
            }
        }

        // Resize handles, inside the panel's edges (the tab is outside, so they never
        // compete with it). Only on a fully open, unlocked drawer.
        Repeater {
            model: drawer.effectiveLocked || drawer.tearing || drawer.progress < 1 ? [] : [
                // depth, start, end
                [1, 0, 0], [0, 1, 0], [0, 0, 1], [1, 1, 0], [1, 0, 1],
            ]
            delegate: MouseArea {
                required property var modelData
                readonly property bool byDepth: modelData[0] === 1
                readonly property bool atStart: modelData[1] === 1
                readonly property bool atEnd: modelData[2] === 1
                readonly property bool corner: byDepth && (atStart || atEnd)
                readonly property int hs: 6
                readonly property int cornerLen: hs * 3
                readonly property real pw: panel.width
                readonly property real ph: panel.height
                // Panel coordinates of the inner edge, on the axis across the edge.
                readonly property real innerPos: (drawer.dock === "left" || drawer.dock === "top")
                                                 ? (drawer.vertical ? pw : ph) - hs : 0
                // Long-axis extent: corners are short, plain edges stop short of them.
                readonly property real alongPos: atStart ? 0 : atEnd ? (drawer.vertical ? ph : pw) - (corner ? cornerLen : hs) : cornerLen
                readonly property real alongLen: corner ? cornerLen : (atStart || atEnd) ? hs : (drawer.vertical ? ph : pw) - 2 * cornerLen
                readonly property real acrossPos: byDepth ? (corner && (drawer.dock === "left" || drawer.dock === "top") ? innerPos - cornerLen + hs : innerPos) : 0
                readonly property real acrossLen: byDepth ? (corner ? cornerLen : hs) : (drawer.vertical ? pw : ph)

                x: drawer.vertical ? acrossPos : alongPos
                y: drawer.vertical ? alongPos : acrossPos
                width: drawer.vertical ? acrossLen : alongLen
                height: drawer.vertical ? alongLen : acrossLen
                z: corner ? 11 : 10
                preventStealing: true
                cursorShape: {
                    if (!corner) return (byDepth === drawer.vertical) ? Qt.SizeHorCursor : Qt.SizeVerCursor;
                    // Which screen corner this handle sits at decides the diagonal.
                    const right = drawer.vertical ? x > pw / 2 : atEnd;
                    const bottom = drawer.vertical ? atEnd : y > ph / 2;
                    return right === bottom ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor;
                }

                onPressed: mouse => drawer.beginResize(mapToItem(null, mouse.x, mouse.y))
                onPositionChanged: mouse => drawer.resizeTo(mapToItem(null, mouse.x, mouse.y), byDepth, atStart, atEnd)
                onReleased: drawer.endResize()
                onCanceled: drawer.endResize()
            }
        }

    }

    // ----------------------------------------------------------------- tab
    Rectangle {
        id: tab
        // Faded rather than hidden: an invisible item loses the mouse grab, which
        // would end the very drag that's tearing the drawer off.
        opacity: drawer.tearing ? 0 : 1
        // A divider tab in a lever-arch file: it sits *behind* the drawer, so the
        // drawer's edge (and its shadow) cuts across the tab's base, and only its two
        // outer corners are rounded. The base is square and flush with the edge.
        z: -1
        // Centred along the drawer's length; glued to the panel's outer side, so it
        // rides out with the panel as it opens.
        readonly property real centred: Math.round(drawer.along + (drawer.length - drawer.tabLength) / 2)

        width: (drawer.vertical ? drawer.tabThickness : drawer.tabLength) + (drawer.vertical ? drawer.overlap : 0)
        height: (drawer.vertical ? drawer.tabLength : drawer.tabThickness) + (drawer.vertical ? 0 : drawer.overlap)
        x: drawer.dock === "left" ? panel.x + panel.width - drawer.overlap
         : drawer.dock === "right" ? panel.x - drawer.tabThickness
         : centred
        y: drawer.dock === "top" ? panel.y + panel.height - drawer.overlap
         : drawer.dock === "bottom" ? panel.y - drawer.tabThickness
         : centred
        readonly property real outerRadius: Math.round(Kirigami.Units.cornerRadius * 1.5)
        topLeftRadius: drawer.dock === "right" || drawer.dock === "bottom" ? outerRadius : 0
        topRightRadius: drawer.dock === "left" || drawer.dock === "bottom" ? outerRadius : 0
        bottomLeftRadius: drawer.dock === "right" || drawer.dock === "top" ? outerRadius : 0
        bottomRightRadius: drawer.dock === "left" || drawer.dock === "top" ? outerRadius : 0
        // The tab stays solid enough to find and grab even when the panel is very
        // transparent: same hue as the panel, at least 85% opaque.
        readonly property color baseColor: panelBg.imageBg || !drawer.showBackground
                                           ? Qt.rgba(0.12, 0.13, 0.15, 0.92)
                                           : Qt.rgba(drawer.bgColor.r, drawer.bgColor.g, drawer.bgColor.b, Math.max(drawer.bgColor.a, 0.85))
        color: tabHover.hovered || drawer.open ? Qt.lighter(baseColor, 1.25) : baseColor
        border.width: 1
        border.color: tabDrop.containsDrag ? Kirigami.Theme.highlightColor : Qt.rgba(1, 1, 1, 0.25)

        HoverHandler {
            id: tabHover
            cursorShape: drawer.draggingTab ? Qt.ClosedHandCursor : Qt.PointingHandCursor
        }

        Row {
            anchors.centerIn: parent
            // Readable along the edge: bottom-to-top on the left, top-to-bottom on the right.
            rotation: drawer.dock === "left" ? -90 : drawer.dock === "right" ? 90 : 0
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                visible: drawer.pinned
                width: Kirigami.Units.iconSizes.small
                height: width
                source: "pin"
                color: "white"
                anchors.verticalCenter: parent.verticalCenter
            }
            QQC2.Label {
                id: tabLabel
                text: drawer.title || i18n("Corral")
                color: "white"
                font.bold: true
                elide: Text.ElideRight
                width: Math.min(implicitWidth, drawer.length - Kirigami.Units.gridUnit * 2.5)
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Left: click to pin/unpin, drag along the edge to reposition, drag away to
        // undock (see gesture* above). Right: the corral's options menu.
        MouseArea {
            property bool moved: false
            anchors.fill: parent
            preventStealing: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: mouse => {
                if (mouse.button === Qt.RightButton) {
                    if (drawer.content) drawer.content.openMenu(this, mouse.x, mouse.y);
                    return;
                }
                moved = false;
                drawer.gesturePressed(mapToItem(null, mouse.x, mouse.y));
            }
            onPositionChanged: mouse => {
                if (!(pressedButtons & Qt.LeftButton)) return;
                const p = mapToItem(null, mouse.x, mouse.y);
                if (!moved && Math.hypot(p.x - drawer.pressScene.x, p.y - drawer.pressScene.y) < Qt.styleHints.startDragDistance) return;
                moved = true;
                drawer.gestureDragged(p);
            }
            onReleased: mouse => {
                if (mouse.button === Qt.LeftButton) {
                    drawer.gestureReleased(mapToItem(null, mouse.x, mouse.y), !moved);
                }
            }
            onCanceled: drawer.cancelGesture()
        }

        // Dragging a file onto the closed tab opens the drawer so it can be dropped inside.
        DropArea {
            id: tabDrop
            anchors.fill: parent
            keys: ["text/uri-list"]
        }
    }
}
