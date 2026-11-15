// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Effects
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.theranch.core

// Paints a corral's background: a colour, or an image with an optional colour
// overlay. Used by free corrals, drawers and the Background dialog's preview, so
// all three always look the same. Purely presentational: reads the model, owns
// no state.
Item {
    id: bg

    required property var corral // a Corrals model entry (bgMode, bgColor, bgImage, …)
    property real radius: Kirigami.Units.cornerRadius

    readonly property bool imageMode: corral.bgMode === "image" && corral.bgImage.toString() !== ""
    readonly property int imageStatus: img.status

    function fillModeFor(name) {
        switch (name) {
        case "fit": return Image.PreserveAspectFit;
        case "stretch": return Image.Stretch;
        case "tile": return Image.Tile;
        case "center": return Image.Pad;
        default: return Image.PreserveAspectCrop;
        }
    }

    // Colour mode (alpha in the colour is the transparency).
    Rectangle {
        anchors.fill: parent
        radius: bg.radius
        color: bg.corral.bgColor
        visible: !bg.imageMode
    }

    // Image mode: image (with its own transparency) + optional colour wash,
    // clipped to the rounded corners.
    // Why a MultiEffect mask instead of Kirigami.ShadowedImage: ShadowedImage draws
    // the raw texture stretched to its size and ignores Image.fillMode, which would
    // break the Tile/Fit/Centre options. A plain Image respects fillMode; the mask
    // just rounds the corners. The layer is only enabled in image mode (it costs an
    // offscreen texture).
    Item {
        id: imageLayer
        anchors.fill: parent
        visible: bg.imageMode
        layer.enabled: visible
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: roundMask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }

        Image {
            id: img
            // Never hold more pixels than can be shown: a 4K photo in a 300 px corral
            // then costs ~0.3 MB instead of ~33 MB. How depends on the fit mode:
            //   crop, fit      decoded at the size it's shown at (Qt works out the
            //                  right size for cropping/fitting from the box we give);
            //   stretch        decoded just big enough to cover the box. sourceSize
            //                  keeps the aspect ratio, so the box itself would leave
            //                  one axis short and soft;
            //   tile, center   drawn 1:1, so the size can't change, but only the part
            //                  behind the corral is ever visible: decode just that.
            // The last two need the file's own size, read from its header in C++.
            readonly property string fill: bg.corral.bgFill
            readonly property bool boxed: fill === "crop" || fill === "fit"
            readonly property bool oneToOne: fill === "tile" || fill === "center"
            readonly property size natural: bg.imageMode && bg.visible && !boxed ? Corrals.imageSize(bg.corral.bgImage)
                                                                               : Qt.size(0, 0)
            readonly property bool naturalKnown: natural.width > 0 && natural.height > 0

            anchors.fill: parent
            // Only loaded while actually visible (not in hidden copies of this corral,
            // or while its drawer is closed), and once we know how big we are.
            source: bg.imageMode && bg.visible && settled.width > 0 ? bg.corral.bgImage : ""
            fillMode: bg.fillModeFor(fill)
            horizontalAlignment: Image.AlignHCenter
            verticalAlignment: Image.AlignVCenter
            opacity: bg.corral.bgImageOpacity

            // Our size in logical pixels, but only once it has stopped changing: bound
            // directly, every pixel of a resize drag would re-read and re-decode the
            // file. Meanwhile the current texture is simply scaled.
            property size settled: Qt.size(0, 0)
            function settle() {
                if (width > 0 && height > 0) settled = Qt.size(Math.ceil(width), Math.ceil(height));
            }
            function sizeChanged() {
                if (settled.width > 0) settleTimer.restart(); else settle(); // first time: no wait
            }
            onWidthChanged: sizeChanged()
            onHeightChanged: sizeChanged()
            Component.onCompleted: settle()
            Timer {
                id: settleTimer
                interval: 200
                onTriggered: img.settle()
            }

            sourceSize: {
                const dpr = Screen.devicePixelRatio;
                const w = Math.ceil(settled.width * dpr), h = Math.ceil(settled.height * dpr);
                if (boxed) return Qt.size(w, h);
                if (fill === "stretch" && naturalKnown) {
                    const scale = Math.max(w / natural.width, h / natural.height);
                    if (scale < 1) return Qt.size(Math.ceil(natural.width * scale), Math.ceil(natural.height * scale));
                }
                return undefined; // the file's own size
            }
            // Tile and Centre show the image centred at its own size, so whatever lies
            // beyond the corral on an axis is cut off: leave exactly that out.
            sourceClipRect: {
                if (!oneToOne || !naturalKnown || (natural.width <= settled.width && natural.height <= settled.height)) {
                    return undefined;
                }
                const w = Math.min(natural.width, settled.width), h = Math.min(natural.height, settled.height);
                return Qt.rect(Math.floor((natural.width - w) / 2), Math.floor((natural.height - h) / 2), w, h);
            }
            asynchronous: true
            smooth: true
            // No mipmaps (+33% memory): they only help when a large texture is drawn
            // much smaller, which none of the modes above leaves us doing.
            mipmap: false
            clip: true
        }

        Rectangle {
            anchors.fill: parent
            visible: bg.corral.overlayEnabled
            color: bg.corral.overlayColor
            opacity: bg.corral.overlayOpacity
        }
    }

    // The mask's shape (never drawn itself; MultiEffect samples its layer).
    Rectangle {
        id: roundMask
        anchors.fill: parent
        radius: bg.radius
        visible: false
        layer.enabled: true
    }
}
