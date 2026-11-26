// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.theranch.core

// The quick-open popup: one launcher listing the icons of *every* corral, in a
// titled section per corral. Shown by the Quick Open global shortcut, the tray menu,
// or `theranch --quick-open`.
//
// It's a window of its own on the OVERLAY layer, created only while open, and a
// separate view of the corrals' folders: a filter box and a scrolling list of
// sections in the Plasma theme's colours, centred on the active screen over a dimmed
// backdrop. Corrals and drawers themselves are left exactly where they are, on their
// own layers. That is a design decision: the popup works like an application
// launcher with categories, not by bringing the desktop's corrals forward.
//
// Type to filter (across all corrals), arrows to move (across sections too), Enter
// to open, Esc or a click outside to close. Opening anything closes the popup.
Window {
    id: win

    // ------------------------------------------------------------- metrics
    readonly property int iconSize: Kirigami.Units.iconSizes.large
    // Whole-pixel cells so icons stay sharp.
    readonly property int cellWidth: Math.ceil(Math.max(iconSize * 1.9, iconSize + Kirigami.Units.gridUnit * 2.5))
    readonly property int cellHeight: Math.ceil(iconSize + Kirigami.Units.smallSpacing * 3 + labelMetrics.height * 2)
    // One column count for every section, so the grids line up under each other.
    readonly property int cols: Math.max(4, Math.min(8, Math.floor(width * 0.6 / cellWidth)))

    // ----------------------------------------------------------- selection
    // The highlighted icon, as (section index, index within that section's filtered
    // list). curSection -1 = nothing to select (no icons, or nothing matches).
    property int curSection: -1
    property int curIndex: 0

    function section(s) { return sections.itemAt(s); }
    function sectionCount(s) { const it = section(s); return it ? it.count : 0; }

    // Next/previous section (from s, exclusive) that has visible icons; -1 if none.
    function nextSection(s, step) {
        for (let t = s + step; t >= 0 && t < sections.count; t += step) {
            if (sectionCount(t) > 0) return t;
        }
        return -1;
    }

    function select(s, i) {
        curSection = s;
        curIndex = i;
        ensureVisible();
    }

    // Back to the first icon of the first section with matches (after filtering).
    function resetSelection() {
        select(nextSection(-1, 1), 0);
        flick.contentY = 0;
    }

    // Keeps the selection on a real icon after the lists change underneath it
    // (KIO lists asynchronously, and files can change while we're open).
    function revalidate() {
        if (curSection < 0 || sectionCount(curSection) === 0) {
            resetSelection();
        } else if (curIndex >= sectionCount(curSection)) {
            curIndex = sectionCount(curSection) - 1;
        }
    }

    // dx: -1/+1 for left/right; dy: -1/+1 for up/down. Moving past either end of a
    // section continues into the neighbouring section (keeping the column for up/down).
    function move(dx, dy) {
        if (curSection < 0) { resetSelection(); return; }
        const n = sectionCount(curSection);
        const i = Math.min(curIndex, n - 1);
        if (dx !== 0) {
            if (i + dx >= 0 && i + dx < n) { select(curSection, i + dx); return; }
            const t = nextSection(curSection, dx);
            if (t >= 0) select(t, dx > 0 ? 0 : sectionCount(t) - 1);
            return;
        }
        const col = i % cols;
        if (dy > 0) {
            if (i + cols < n) { select(curSection, i + cols); return; }
            // Not yet on the section's last row: drop to its last icon.
            if (Math.floor(i / cols) < Math.floor((n - 1) / cols)) { select(curSection, n - 1); return; }
            // On the last row: into the next section, same column if it has one.
            const t = nextSection(curSection, 1);
            if (t >= 0) select(t, Math.min(col, sectionCount(t) - 1));
        } else {
            if (i - cols >= 0) { select(curSection, i - cols); return; }
            const t = nextSection(curSection, -1);
            if (t >= 0) {
                const m = sectionCount(t);
                select(t, Math.min(Math.floor((m - 1) / cols) * cols + col, m - 1));
            }
        }
    }

    // Scrolls so the highlighted icon (and, on a section's first row, its title)
    // is in view.
    function ensureVisible() {
        const sec = section(curSection);
        const cell = sec ? sec.cellAt(curIndex) : null;
        if (!cell) return;
        const top = curIndex < cols ? sec.mapToItem(column, 0, 0).y : cell.mapToItem(column, 0, 0).y;
        const bottom = cell.mapToItem(column, 0, cell.height).y;
        if (top < flick.contentY) flick.contentY = top;
        else if (bottom > flick.contentY + flick.height) flick.contentY = bottom - flick.height;
    }

    function openCurrent() {
        const sec = section(curSection);
        if (sec) openUrl(sec.urlAt(curIndex));
    }
    function openUrl(url) {
        if (url && url.toString() !== "") {
            Ranch.open(url);
            Ranch.closeQuickOpen();
        }
    }

    // -------------------------------------------------------------- window
    color: "transparent"
    title: i18n("The Ranch Quick Open")

    Component.onCompleted: {
        Ranch.configureQuickOpenLayer(win);
        // First guess only; the compositor sizes an all-edges-anchored surface.
        width = Screen.width;
        height = Screen.height;
        visible = true;
        filter.forceActiveFocus();
    }

    // Dimmed backdrop: clicking anywhere outside the panel closes.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.35)
        opacity: 0
        Component.onCompleted: opacity = 1
        Behavior on opacity { NumberAnimation { duration: Kirigami.Units.shortDuration } }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onPressed: Ranch.closeQuickOpen()
        }
    }

    Kirigami.ShadowedRectangle {
        id: panel

        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false

        width: layout.implicitWidth + Kirigami.Units.largeSpacing * 2
        height: layout.implicitHeight + Kirigami.Units.largeSpacing * 2
        x: Math.round((win.width - width) / 2)
        y: Math.round((win.height - height) / 2)

        color: Kirigami.Theme.backgroundColor
        radius: Kirigami.Units.cornerRadius * 2
        border.width: 1
        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
        shadow.size: Kirigami.Units.gridUnit
        shadow.color: Qt.rgba(0, 0, 0, 0.4)
        shadow.yOffset: 2

        scale: 0.96
        Component.onCompleted: scale = 1
        Behavior on scale { NumberAnimation { duration: Kirigami.Units.shortDuration; easing.type: Easing.OutCubic } }

        // Swallow clicks on the panel so they don't reach the backdrop.
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            id: layout
            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: "org.theranch-symbolic"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                }
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 4
                    text: i18n("Quick Open")
                }
                QQC2.ToolButton {
                    icon.name: "window-close"
                    display: QQC2.AbstractButton.IconOnly
                    text: i18n("Close")
                    onClicked: Ranch.closeQuickOpen()
                }
            }

            Kirigami.SearchField {
                id: filter
                Layout.fillWidth: true
                placeholderText: i18n("Type to filter…")
                onTextChanged: Qt.callLater(win.resetSelection) // after every section refilters
                // Arrows move through the icons; the text cursor keeps Home/End.
                Keys.onPressed: event => {
                    switch (event.key) {
                    case Qt.Key_Left:
                    case Qt.Key_Backtab: win.move(-1, 0); break;
                    case Qt.Key_Right:
                    case Qt.Key_Tab: win.move(1, 0); break;
                    case Qt.Key_Up: win.move(0, -1); break;
                    case Qt.Key_Down: win.move(0, 1); break;
                    case Qt.Key_PageDown: for (let k = 0; k < 3; ++k) win.move(0, 1); break;
                    case Qt.Key_PageUp: for (let k = 0; k < 3; ++k) win.move(0, -1); break;
                    case Qt.Key_Return:
                    case Qt.Key_Enter: win.openCurrent(); break;
                    case Qt.Key_Escape:
                        if (text !== "") text = ""; else Ranch.closeQuickOpen();
                        break;
                    default: return;
                    }
                    event.accepted = true;
                }
            }

            Flickable {
                id: flick
                Layout.preferredWidth: column.width + scrollBar.width
                // Grows with the content up to ~65% of the screen, then scrolls.
                Layout.preferredHeight: Math.max(win.cellHeight, Math.min(column.implicitHeight, Math.round(win.height * 0.65)))
                contentWidth: column.width
                contentHeight: column.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                QQC2.ScrollBar.vertical: QQC2.ScrollBar { id: scrollBar }

                Column {
                    id: column
                    width: win.cols * win.cellWidth
                    spacing: Kirigami.Units.largeSpacing

                    // One section per corral, in the tray menu's order. Hidden corrals
                    // are included: quick open is a way to reach them too.
                    Repeater {
                        id: sections
                        model: Corrals

                        delegate: Column {
                            id: sec
                            required property int index
                            required property string title
                            required property url folderUrl

                            readonly property int count: filtered.count
                            function cellAt(i) { return cells.itemAt(i); }
                            function urlAt(i) { return filtered.urlAt(i); }

                            // Sections with nothing to show (empty, or no matches) collapse.
                            visible: count > 0
                            width: column.width
                            spacing: Kirigami.Units.smallSpacing
                            onCountChanged: Qt.callLater(win.revalidate)

                            RowLayout {
                                width: parent.width
                                spacing: Kirigami.Units.largeSpacing
                                Kirigami.Heading {
                                    level: 5
                                    text: sec.title !== "" ? sec.title : i18n("(untitled)")
                                    textFormat: Text.PlainText // names are data, never markup
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: Math.round(sec.width * 0.7)
                                }
                                Kirigami.Separator {
                                    Layout.fillWidth: true
                                }
                                QQC2.Label {
                                    text: filter.text === "" ? sec.count
                                                             : i18nc("filter matches of total icons", "%1 of %2", sec.count, folder.count)
                                    opacity: 0.6
                                }
                            }

                            Grid {
                                columns: win.cols

                                Repeater {
                                    id: cells
                                    model: NameFilterModel {
                                        id: filtered
                                        filterText: filter.text
                                        sourceModel: FolderModel {
                                            id: folder
                                            url: sec.folderUrl
                                        }
                                    }

                                    delegate: QQC2.ItemDelegate {
                                        id: cell
                                        required property int index
                                        required property string name
                                        required property string iconName
                                        required property url url

                                        width: win.cellWidth
                                        height: win.cellHeight
                                        highlighted: win.curSection === sec.index && win.curIndex === index
                                        hoverEnabled: true
                                        onHoveredChanged: if (hovered) { win.curSection = sec.index; win.curIndex = index; }
                                        // Launcher-style: one click opens, whatever the double-click setting.
                                        onClicked: win.openUrl(url)

                                        QQC2.ToolTip.text: name.indexOf("<") < 0 ? name : name.replace(/&/g, "&amp;").replace(/</g, "&lt;") // see IconDelegate
                                        QQC2.ToolTip.visible: hovered
                                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                                        contentItem: Item {
                                            Kirigami.Icon {
                                                id: icon
                                                x: Math.round((parent.width - width) / 2)
                                                y: Kirigami.Units.smallSpacing
                                                width: win.iconSize
                                                height: win.iconSize
                                                source: cell.iconName
                                                fallback: "unknown"
                                                active: cell.hovered
                                            }
                                            QQC2.Label {
                                                anchors {
                                                    top: icon.bottom; topMargin: Kirigami.Units.smallSpacing
                                                    left: parent.left; right: parent.right
                                                }
                                                text: cell.name
                                                textFormat: Text.PlainText // names are data, never markup
                                                horizontalAlignment: Text.AlignHCenter
                                                wrapMode: Text.Wrap
                                                maximumLineCount: 2
                                                elide: Text.ElideRight
                                                color: cell.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                QQC2.Label {
                    anchors.centerIn: parent
                    visible: win.curSection < 0
                    text: sections.count === 0 ? i18n("No corrals yet")
                                               : (filter.text === "" ? i18n("Your corrals are empty") : i18n("No matches"))
                    opacity: 0.7
                }
            }
        }
    }

    // Invisible: measures one line of label text.
    QQC2.Label {
        id: labelMetrics
        visible: false
        text: "Mg"
    }
}
