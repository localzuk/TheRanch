// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.theranch.core

// The icon area of a corral. Corrals keep the size the user drew, so instead of
// growing or scrolling, icons are laid out in as many *whole* rows × columns as fit,
// and anything beyond that goes onto further pages. Pages are switched with the
// mouse wheel, hover arrows, page dots, or by hovering a dragged file over an edge.
//
// Data flow: one FolderModel lists the corral's folder; each page is a GridView over
// its own PageModel slice of it (see pagemodel.h). Also handles selection,
// keyboard shortcuts, and dropping files into the corral.
Item {
    id: pager

    required property var host
    required property url folderUrl
    required property int iconSize
    property bool hovered: false

    readonly property alias count: folder.count
    readonly property bool dropHovered: drop.containsDrag

    signal emptyAreaMenuRequested(var item, real x, real y)

    // ------------------------------------------------------------- layout
    // Cells have a minimum width; actual cells stretch to share the width evenly.
    // Height fits the icon plus two lines of label.
    readonly property real cellMinWidth: Math.max(iconSize * 1.8, iconSize + Kirigami.Units.gridUnit * 2)
    // Rounded up to whole pixels so every row starts on a pixel boundary (a fractional
    // row height would leave lower rows' icons and labels slightly blurred).
    readonly property real cellHeight: Math.ceil(iconSize + Kirigami.Units.smallSpacing * 2 + labelMetrics.height * 2)
    readonly property real dotsHeight: Kirigami.Units.gridUnit * 0.75
    readonly property int cols: Math.max(1, Math.floor(width / cellMinWidth))
    readonly property int rowsNoDots: Math.max(1, Math.floor(height / cellHeight))
    // Page dots need room, but whether we need dots depends on how many rows fit.
    // Deciding from the dot-less row count first avoids a binding loop.
    readonly property bool paginated: folder.count > cols * rowsNoDots
    readonly property int rows: paginated ? Math.max(1, Math.floor((height - dotsHeight) / cellHeight)) : rowsNoDots
    readonly property int pageSize: cols * rows
    readonly property int pageCount: Math.max(1, Math.ceil(folder.count / pageSize))

    property int currentPage: 0
    property int previousPageSize: pageSize // remembered so a resize can keep your place

    // Keep the first icon of the current page in view when the page size changes.
    onPageSizeChanged: {
        const first = currentPage * previousPageSize;
        previousPageSize = pageSize;
        currentPage = Math.min(Math.floor(first / pageSize), pageCount - 1);
    }
    onPageCountChanged: currentPage = Math.min(currentPage, pageCount - 1)

    function goToPage(p) {
        currentPage = Math.max(0, Math.min(p, pageCount - 1));
    }

    // ---------------------------------------------------------- selection
    // Selected icons, as URL strings. Kept here (not in each delegate) because
    // delegates are recreated when pages change, and selection must survive that.
    // Reassigned (never mutated in place) so bindings on it re-evaluate.
    property var selection: [] // url strings

    function isSelected(url) { return selection.indexOf(url.toString()) >= 0; }
    function select(url) { selection = [url.toString()]; forceActiveFocus(); }
    function toggle(url) {
        const s = selection.slice(), u = url.toString(), i = s.indexOf(u);
        if (i >= 0) s.splice(i, 1); else s.push(u);
        selection = s;
        forceActiveFocus();
    }
    function selectedUrls() { return selection.slice(); }

    activeFocusOnTab: true
    Keys.onDeletePressed: Ranch.trash(selectedUrls())
    Keys.onReturnPressed: selectedUrls().forEach(u => Ranch.open(u))
    // Clicking another corral (or anywhere else) clears this one's selection.
    onActiveFocusChanged: if (!activeFocus) selection = []

    FolderModel {
        id: folder
        url: pager.folderUrl
    }

    // Invisible: only used to measure one line of label text in the current font.
    QQC2.Label {
        id: labelMetrics
        visible: false
        text: "Mg"
    }

    // ------------------------------------------------------- empty space
    // Underneath the icons: a click on empty space clears the selection, and a
    // right-click opens the corral's menu (icons handle their own clicks on top).
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: mouse => {
            pager.selection = [];
            if (mouse.button === Qt.RightButton) {
                pager.emptyAreaMenuRequested(this, mouse.x, mouse.y);
            }
        }
    }

    // --------------------------------------------------------------- pages
    // A horizontal ListView with one delegate per page, snapped one page at a
    // time; currentIndex follows currentPage, giving an animated slide between pages.
    ListView {
        id: pages
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: pager.rows * pager.cellHeight
        orientation: ListView.Horizontal
        interactive: false // icon drags take priority; page with wheel, arrows or dots
        // Only build the page being shown; neighbours are created when you flip to them.
        cacheBuffer: 0
        clip: true
        model: pager.pageCount
        currentIndex: pager.currentPage
        highlightRangeMode: ListView.StrictlyEnforceRange
        highlightMoveDuration: Kirigami.Units.longDuration
        highlightMoveVelocity: -1
        snapMode: ListView.SnapOneItem

        delegate: Item {
            id: pageItem
            required property int index
            width: pages.width
            height: pages.height

            // Cells are a whole number of pixels wide, and the leftover (< one pixel
            // per column) is split evenly either side, so every icon sits on the
            // pixel grid. width / cols would give e.g. 107.67 px cells: soft icons.
            GridView {
                readonly property int cell: Math.floor(pageItem.width / pager.cols)
                x: Math.floor((pageItem.width - cell * pager.cols) / 2)
                width: cell * pager.cols
                height: pageItem.height
                interactive: false
                cellWidth: cell
                cellHeight: pager.cellHeight
                model: PageModel {
                    sourceModel: folder
                    page: pageItem.index
                    pageSize: pager.pageSize
                }
                delegate: IconDelegate {
                    owner: pager
                }
            }
        }
    }

    // ---------------------------------------------------------- page dots
    Row {
        visible: pager.paginated
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom }
        height: pager.dotsHeight
        spacing: Kirigami.Units.smallSpacing

        Repeater {
            model: pager.paginated ? pager.pageCount : 0
            delegate: Rectangle {
                required property int index
                anchors.verticalCenter: parent.verticalCenter
                width: index === pager.currentPage ? 16 : 7
                height: 7
                radius: 3.5
                color: "white"
                opacity: index === pager.currentPage ? 0.95 : 0.45
                Behavior on width { NumberAnimation { duration: Kirigami.Units.shortDuration } }

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    onClicked: pager.goToPage(parent.index)
                }
            }
        }
    }

    // ------------------------------------------------------ page arrows
    Repeater {
        model: [-1, 1]
        delegate: QQC2.RoundButton {
            required property int modelData
            readonly property int step: modelData
            anchors.verticalCenter: pages.verticalCenter
            x: step < 0 ? 0 : pager.width - width
            width: Kirigami.Units.gridUnit * 1.5
            height: width
            icon.name: step < 0 ? "go-previous" : "go-next"
            icon.width: Kirigami.Units.iconSizes.small
            icon.height: Kirigami.Units.iconSizes.small
            visible: pager.paginated && pager.hovered
                     && (step < 0 ? pager.currentPage > 0 : pager.currentPage < pager.pageCount - 1)
            onClicked: pager.goToPage(pager.currentPage + step)
        }
    }

    // ------------------------------------------------------------- wheel
    // Accumulates to one notch (120 units) per page, so touchpads, which send many
    // small deltas, don't flip through every page at once.
    WheelHandler {
        property real accumulated: 0
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            accumulated += (event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x);
            if (accumulated <= -120) { pager.goToPage(pager.currentPage + 1); accumulated = 0; }
            else if (accumulated >= 120) { pager.goToPage(pager.currentPage - 1); accumulated = 0; }
        }
    }

    // -------------------------------------------------------------- drops
    DropArea {
        id: drop
        anchors.fill: parent
        keys: ["text/uri-list"]

        // Hovering a drag over the left/right edge flips pages.
        onPositionChanged: drag => {
            const edge = Kirigami.Units.gridUnit * 1.5;
            const dir = drag.x < edge ? -1 : (drag.x > width - edge ? 1 : 0);
            if (dir !== flipTimer.dir) { flipTimer.dir = dir; flipTimer.restart(); }
        }
        onExited: flipTimer.stop()
        // The actual move/link is decided in C++ (Ranch::moveUrls), including the
        // rules that protect files from outside the desktop.
        onDropped: dropEvent => {
            flipTimer.stop();
            if (dropEvent.hasUrls && Ranch.moveInto(dropEvent.urls, pager.folderUrl)) {
                dropEvent.accept(Qt.MoveAction);
            } else {
                dropEvent.accept(Qt.IgnoreAction);
            }
        }

        // Repeats, so holding a drag at an edge keeps flipping pages.
        Timer {
            id: flipTimer
            property int dir: 0
            interval: 700
            repeat: true
            onTriggered: if (dir !== 0) pager.goToPage(pager.currentPage + dir)
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -Kirigami.Units.smallSpacing
        radius: Kirigami.Units.cornerRadius
        color: "transparent"
        border.color: Kirigami.Theme.highlightColor
        border.width: 2
        visible: drop.containsDrag
    }

    QQC2.Label {
        anchors.centerIn: parent
        visible: folder.count === 0
        width: parent.width - Kirigami.Units.gridUnit * 2
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: i18n("Drop icons here")
        color: Qt.rgba(1, 1, 1, 0.6)
    }
}
