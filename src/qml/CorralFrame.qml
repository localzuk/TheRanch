// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami
import org.theranch.core

// A free-standing corral on the pasture (one delegate per corral in the store).
// Owns the corral's *chrome*: background, outline, move/resize gestures, edge
// magnetism, docking-by-dragging and stacking. The title bar and icons are in
// CorralContent, which is shared with drawers.
//
// Geometry model: the store holds the truth (areaX/Y/Width/Height). While the user
// drags or resizes, we drive a local `liveRect` instead and write the result back
// once on release. That keeps every intermediate position out of the store and
// off the disk, and a cancelled gesture can't leave a half-moved corral.
Item {
    id: frame

    // model roles (named to avoid clashing with Item's own x/y/width/height/color)
    required property string corralId
    required property string title
    required property color bgColor
    required property bool showTitle
    required property bool showBackground
    required property bool hidden
    required property bool locked
    required property string screenName
    required property real areaX
    required property real areaY
    required property real areaWidth
    required property real areaHeight
    required property string dock
    required property int iconSize
    required property url folderUrl
    required property var model // whole entry, for the background settings

    required property var host

    readonly property int handleSize: 6    // resize grip thickness, straddling the edge
    readonly property int snapDistance: 12 // magnet range for screen edges and other corrals
    readonly property int edgeTrigger: 4   // pointer this close to a screen edge = dock on release
    readonly property real minWidth: Math.max(120, iconSize * 2)
    readonly property real minHeight: Math.max(90, iconSize + Kirigami.Units.gridUnit * 4)

    // Whether this corral is shown in *this* window: free, not hidden, on our screen.
    readonly property bool active: !Ranch.allHidden && !hidden && dock === "" && host.ownsScreen(screenName)
    readonly property bool effectiveLocked: locked || Ranch.allLocked

    // The CorralContent, or null while this corral isn't shown here (see the Loader).
    readonly property var content: contentLoader.item
    readonly property bool contentDropHovered: content !== null && content.dropHovered

    // While dragging/resizing we drive a local rect and commit it on release.
    property bool interacting: false
    property rect liveRect
    property rect startRect
    property point startScene
    readonly property rect storedRect: host.clampRect(Qt.rect(areaX, areaY, areaWidth, areaHeight))
    readonly property rect shownRect: interacting ? liveRect : storedRect

    visible: active
    // Rounded so the corral, and everything inside it, sits on whole pixels.
    x: Math.round(shownRect.x)
    y: Math.round(shownRect.y)
    width: Math.round(shownRect.width)
    height: Math.round(shownRect.height)

    // Our rectangle is part of the window's input region, so it must follow us.
    onXChanged: host.requestMaskUpdate()
    onYChanged: host.requestMaskUpdate()
    onWidthChanged: host.requestMaskUpdate()
    onHeightChanged: host.requestMaskUpdate()
    onActiveChanged: host.requestMaskUpdate()

    // A corral the user just drew opens with its title ready to type into.
    Connections {
        target: Corrals
        function onCreated(id) {
            if (id === frame.corralId) {
                if (frame.content) frame.content.startRename();
            }
        }
    }

    // ---------------------------------------------------------------- logic
    // All gesture maths uses window ("scene") coordinates, which don't change as the
    // corral moves under the pointer. Item-local coordinates would feed back on
    // themselves.

    // Which screen edge (if any) the pointer is pushing against. With an implicit
    // pointer grab we keep receiving positions even past the window's edge.
    function edgeAt(p) {
        if (p.x <= edgeTrigger) return "left";
        if (p.x >= host.width - 1 - edgeTrigger) return "right";
        if (p.y <= edgeTrigger) return "top";
        if (p.y >= host.height - 1 - edgeTrigger) return "bottom";
        return "";
    }

    // Snap a coordinate `v` (start edge) of an extent `len` to any target line.
    function magnet(v, len, lines) {
        let best = v, bestDist = snapDistance + 1;
        for (const line of lines) {
            for (const cand of [line, line - len]) { // align our start or our end
                const d = Math.abs(v - cand);
                if (d < bestDist) { best = cand; bestDist = d; }
            }
        }
        return best;
    }

    // Candidate snap lines: the screen edges plus every other visible corral's edges.
    function linesX() {
        const l = [0, host.width];
        for (const r of host.peerRects(corralId)) l.push(r.x, r.x + r.width);
        return l;
    }
    function linesY() {
        const l = [0, host.height];
        for (const r of host.peerRects(corralId)) l.push(r.y, r.y + r.height);
        return l;
    }

    // Overlapping corrals: the one being used comes to the front. z values only
    // ever grow (host.nextZ()); the check avoids burning numbers on repeat calls.
    function raise() {
        if (z !== host.topZ || z === 0) z = host.nextZ();
    }

    Connections {
        target: frame.content
        function onMenuOpenChanged() { if (frame.content.menuOpen) frame.raise(); }
        function onDropHoveredChanged() { if (frame.content.dropHovered) frame.raise(); }
    }

    function beginInteraction(scenePos) {
        raise();
        startRect = storedRect;
        liveRect = storedRect;
        startScene = scenePos;
        interacting = true;
    }

    function moveTo(scenePos) {
        const w = startRect.width, h = startRect.height;
        let nx = magnet(startRect.x + scenePos.x - startScene.x, w, linesX());
        let ny = magnet(startRect.y + scenePos.y - startScene.y, h, linesY());
        nx = Math.max(0, Math.min(nx, host.width - w));
        ny = Math.max(0, Math.min(ny, host.height - h));
        liveRect = Qt.rect(nx, ny, w, h);
        host.dockHint = edgeAt(scenePos);
    }

    // Released on a screen edge → dock there as a drawer, remembering where along
    // the edge it was. The geometry is saved too, so undocking later puts it back.
    function endMove(scenePos) {
        const edge = edgeAt(scenePos);
        host.dockHint = "";
        if (edge !== "") {
            Corrals.setGeometry(corralId, liveRect.x, liveRect.y, liveRect.width, liveRect.height);
            Corrals.setValue(corralId, "dockOffset", (edge === "left" || edge === "right") ? liveRect.y : liveRect.x);
            Corrals.setValue(corralId, "dock", edge);
        } else {
            Corrals.setGeometry(corralId, liveRect.x, liveRect.y, liveRect.width, liveRect.height);
        }
        interacting = false;
    }

    // l/t/r/b say which edges this handle moves. Each moving edge snaps, then is
    // clamped to the screen and to the minimum size.
    function resizeTo(scenePos, l, t, r, b) {
        const dx = scenePos.x - startScene.x, dy = scenePos.y - startScene.y;
        let x1 = startRect.x, y1 = startRect.y;
        let x2 = startRect.x + startRect.width, y2 = startRect.y + startRect.height;
        if (l) x1 = Math.max(0, Math.min(magnet(x1 + dx, 0, linesX()), x2 - minWidth));
        if (r) x2 = Math.min(host.width, Math.max(magnet(x2 + dx, 0, linesX()), x1 + minWidth));
        if (t) y1 = Math.max(0, Math.min(magnet(y1 + dy, 0, linesY()), y2 - minHeight));
        if (b) y2 = Math.min(host.height, Math.max(magnet(y2 + dy, 0, linesY()), y1 + minHeight));
        liveRect = Qt.rect(x1, y1, x2 - x1, y2 - y1);
    }

    function endResize() {
        Corrals.setGeometry(corralId, liveRect.x, liveRect.y, liveRect.width, liveRect.height);
        interacting = false;
    }

    // --------------------------------------------------------------- visuals
    HoverHandler {
        id: hover
    }

    // Colour or image, see CorralBackground. Fades rather than vanishing when
    // "Show Background" is toggled.
    CorralBackground {
        id: background
        anchors.fill: parent
        corral: frame.model
        opacity: frame.showBackground ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Kirigami.Units.shortDuration } }
    }

    // Outline: always subtle with a background; without one it only appears on
    // hover/interaction so the corral can still be found and grabbed.
    Rectangle {
        anchors.fill: parent
        radius: Kirigami.Units.cornerRadius
        color: frame.showBackground ? "transparent" : Qt.rgba(1, 1, 1, 0.06)
        border.width: 1
        border.color: frame.interacting || frame.contentDropHovered ? Kirigami.Theme.highlightColor : Qt.rgba(1, 1, 1, 0.35)
        opacity: frame.showBackground || hover.hovered || frame.interacting || frame.contentDropHovered ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Kirigami.Units.shortDuration } }
    }

    // Every window has a delegate for every corral, but only one window shows each
    // corral, so the contents (folder listing, icons, menus) are only built where
    // this corral is actually active. Elsewhere it's a nearly free empty Item.
    Loader {
        id: contentLoader
        anchors.fill: parent
        active: frame.active
        sourceComponent: Component {
            CorralContent {
                host: frame.host
                corralId: frame.corralId
                title: frame.title
                bgColor: frame.bgColor
                showTitle: frame.showTitle
                showBackground: frame.showBackground
                locked: frame.effectiveLocked
                dock: frame.dock
                iconSize: frame.iconSize
                folderUrl: frame.folderUrl
                frameHovered: hover.hovered
                corralModel: frame.model

                // The title bar / grip lives in CorralContent; it reports the gesture here.
                onMovePressed: p => { if (!frame.effectiveLocked) frame.beginInteraction(p); }
                onMoveDragged: p => { if (frame.interacting) frame.moveTo(p); }
                onMoveReleased: p => { if (frame.interacting) frame.endMove(p); }
                onMoveCanceled: { frame.interacting = false; frame.host.dockHint = ""; }
            }
        }
    }


    // ------------------------------------------------------- resize handles
    // Eight invisible MouseAreas: four edges and four corners. Each straddles the
    // frame edge by handleSize (which is why the window's input region is grown by
    // the same amount). Corners are longer and on top, so they win where they overlap
    // an edge. Locking removes them entirely.
    Repeater {
        model: frame.effectiveLocked ? [] : [
            // l, t, r, b, cursor
            [1, 0, 0, 0, Qt.SizeHorCursor], [0, 0, 1, 0, Qt.SizeHorCursor],
            [0, 1, 0, 0, Qt.SizeVerCursor], [0, 0, 0, 1, Qt.SizeVerCursor],
            [1, 1, 0, 0, Qt.SizeFDiagCursor], [0, 0, 1, 1, Qt.SizeFDiagCursor],
            [0, 1, 1, 0, Qt.SizeBDiagCursor], [1, 0, 0, 1, Qt.SizeBDiagCursor],
        ]

        delegate: MouseArea {
            required property var modelData
            readonly property bool l: modelData[0] === 1
            readonly property bool t: modelData[1] === 1
            readonly property bool r: modelData[2] === 1
            readonly property bool b: modelData[3] === 1
            readonly property bool corner: (l || r) && (t || b)
            readonly property int hs: frame.handleSize
            readonly property int cornerLen: hs * 3

            x: l ? -hs : (r ? frame.width - (corner ? cornerLen : hs) : cornerLen)
            y: t ? -hs : (b ? frame.height - (corner ? cornerLen : hs) : cornerLen)
            width: (l || r) ? (corner ? cornerLen + hs : 2 * hs) : frame.width - 2 * cornerLen
            height: (t || b) ? (corner ? cornerLen + hs : 2 * hs) : frame.height - 2 * cornerLen
            z: corner ? 3 : 2
            cursorShape: modelData[4]
            preventStealing: true

            onPressed: mouse => frame.beginInteraction(mapToItem(null, mouse.x, mouse.y))
            onPositionChanged: mouse => frame.resizeTo(mapToItem(null, mouse.x, mouse.y), l, t, r, b)
            onReleased: frame.endResize()
            onCanceled: frame.endResize() // e.g. the compositor took the grab: keep what we have
        }
    }
}
