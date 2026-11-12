// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Effects
import QtQuick.Window
import org.kde.kirigami as Kirigami

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
            // Crop and Fit can be decoded at (about) the size they're shown at: a 4K
            // photo in a 300 px corral then costs ~0.3 MB instead of ~33 MB. Stretch,
            // Tile and Centre need the image's natural size, so they're left alone.
            readonly property bool decodeAtDisplaySize: bg.corral.bgFill === "crop" || bg.corral.bgFill === "fit"
            anchors.fill: parent
            // Only loaded while actually visible (not in hidden copies of this corral,
            // or while its drawer is closed).
            source: bg.imageMode && bg.visible ? bg.corral.bgImage : ""
            fillMode: bg.fillModeFor(bg.corral.bgFill)
            horizontalAlignment: Image.AlignHCenter
            verticalAlignment: Image.AlignVCenter
            opacity: bg.corral.bgImageOpacity
            sourceSize: decodeAtDisplaySize
                        ? Qt.size(Math.ceil(width * Screen.devicePixelRatio), Math.ceil(height * Screen.devicePixelRatio))
                        : undefined
            asynchronous: true
            smooth: true
            // Mipmaps (+33% memory) only help when shrinking a large image on screen,
            // which decoding at display size already avoids.
            mipmap: !decodeAtDisplaySize
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
