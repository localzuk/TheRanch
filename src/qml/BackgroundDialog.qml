// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtCore
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.theranch.core

// Edit a corral's background. Changes apply live to the corral; Cancel restores
// what was there when the dialog opened.
//
// Design: there's no "draft" copy of the settings. Every control writes straight to
// the store (so the real corral is the preview), and we snapshot the relevant keys
// on open so Cancel can write them back. Controls display the model's values
// through bindings. When a user interaction would break such a binding (QML
// replaces a binding on assignment), the handler re-establishes it with
// Qt.binding(), so Cancel and re-opening show the right state.
QQC2.Dialog {
    id: dialog

    required property var corral      // CorralContent (for its live size)
    required property var corralModel // Corrals model entry
    required property var host

    readonly property string corralId: corralModel.corralId
    readonly property bool imageMode: corralModel.bgMode === "image"

    // ---------------------------------------------------- ideal image size
    // The ideal image is the corral's size in *physical* pixels (logical size ×
    // display scale), so it stays sharp on HiDPI screens. `corral` is the live
    // CorralContent, so these update if the corral is resized while we're open.
    readonly property real dpr: body.screenDpr > 0 ? body.screenDpr : 1
    readonly property int idealWidth: Math.round(corral.width * dpr)
    readonly property int idealHeight: Math.round(corral.height * dpr)
    readonly property int imageWidth: probe.status === Image.Ready ? probe.implicitWidth : 0
    readonly property int imageHeight: probe.status === Image.Ready ? probe.implicitHeight : 0

    // One-line verdict on how the chosen image will fit this corral, for the
    // current fit mode. Aspect ratios within 3% count as "the same shape". For
    // crop, the visible fraction is min(rImg/rBox, rBox/rImg), so the rest is cut off.
    readonly property var verdict: {
        if (imageWidth <= 0 || idealWidth <= 0) return { text: "", ok: true };
        const fill = corralModel.bgFill;
        const rImg = imageWidth / imageHeight, rBox = idealWidth / idealHeight;
        const shapeOff = Math.abs(rImg - rBox) / rBox > 0.03;
        const small = imageWidth < idealWidth || imageHeight < idealHeight;
        if (fill === "tile")
            return { text: i18n("Repeated at its own size."), ok: true };
        if (fill === "center")
            return small ? { text: i18n("Smaller than the corral, so there'll be empty space around it."), ok: false }
                         : { text: i18n("Larger than the corral, so the edges will be cut off."), ok: false };
        const notes = [];
        if (shapeOff) {
            if (fill === "crop") {
                const cropped = Math.round((1 - Math.min(rImg / rBox, rBox / rImg)) * 100);
                notes.push(i18n("Different shape: about %1% will be cropped.", cropped));
            } else if (fill === "fit") {
                notes.push(i18n("Different shape: empty bands will show."));
            } else {
                notes.push(i18n("Different shape: it will look stretched."));
            }
        }
        if (small) notes.push(i18n("Smaller than ideal: it may look soft."));
        return notes.length ? { text: notes.join(" "), ok: false } : { text: i18n("Good fit."), ok: true };
    }

    // ------------------------------------------------------ open / cancel
    property var snapshot: ({})
    readonly property var keys: ["color", "showBackground", "bgMode", "bgImage", "bgFill",
                                 "bgImageOpacity", "overlayEnabled", "overlayColor", "overlayOpacity"]
    // Store keys and model role names are the same except for `color` (see groupstore.cpp).
    readonly property var roleFor: ({ color: "bgColor" }) // store key → model role, where they differ

    function valueOf(key) {
        const v = corralModel[roleFor[key] || key];
        return (key === "color" || key === "overlayColor") ? v.toString() : v;
    }

    onAboutToShow: {
        const s = {};
        for (const k of keys) s[k] = valueOf(k);
        snapshot = s;
        Corrals.setValue(corralId, "showBackground", true); // so the live preview is visible
    }
    onRejected: {
        for (const k of keys) Corrals.setValue(corralId, k, snapshot[k]);
    }
    onOpened: host.popupOpened()
    onClosed: host.popupClosed()

    function set(key, value) { Corrals.setValue(corralId, key, value); }

    // Colour helpers: keep RGB and alpha (transparency) independent.
    function withAlpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a); }
    function opaque(c) { return Qt.rgba(c.r, c.g, c.b, 1); }

    title: i18n("Background for “%1”", corralModel.title)
    parent: QQC2.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Kirigami.Units.gridUnit * 30, parent ? parent.width - Kirigami.Units.gridUnit * 2 : 600)
    popupType: QQC2.Popup.Item
    modal: true
    // In-window pickers are popups in the same overlay as us; step back while one
    // is open so it's drawn on top instead of underneath.
    z: colorDialog.visible || fileDialog.visible ? -1 : 0
    standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel

    // Not displayed. Loads the chosen file at full size only to read its pixel
    // dimensions (the visible Image in CorralBackground is scaled).
    Image {
        id: probe // reads the chosen image's natural size
        visible: false
        asynchronous: true
        source: dialog.corralModel.bgImage
    }

    // The native KDE file picker. The chosen file is copied into our data dir
    // (GroupStore::importBackground), which also switches the corral to image mode.
    // ------------------------------------------------- colour & file pickers
    // Drawers live on the TOP layer, above every normal window, so a native picker
    // (a normal window) would open *behind* them, invisible. For drawers we use Qt's
    // own pickers drawn inside this surface instead (DontUseNativeDialog plus
    // popupType Item), which stack above the Background dialog. Free corrals are
    // below windows, so they keep the native KDE pickers.
    readonly property bool inWindowPickers: host.aboveWindows === true

    // Which setting the colour picker is choosing: "color" (background) or "overlayColor".
    function pickColor(key, initial) {
        colorDialog.target = key;
        colorDialog.selectedColor = initial;
        colorDialog.open();
    }

    ColorDialog {
        id: colorDialog
        property string target: "color"
        title: target === "color" ? i18n("Background Colour") : i18n("Overlay Colour")
        // No alpha channel here: transparency has its own slider in this dialog.
        options: dialog.inWindowPickers ? ColorDialog.DontUseNativeDialog : 0
        popupType: QQC2.Popup.Item
        onAccepted: {
            if (target === "color") {
                // Keep the current transparency; only the hue changes.
                dialog.set("color", dialog.withAlpha(selectedColor, dialog.corralModel.bgColor.a).toString());
            } else {
                dialog.set("overlayColor", dialog.opaque(selectedColor).toString());
            }
        }
    }

    FileDialog {
        id: fileDialog
        options: dialog.inWindowPickers ? FileDialog.DontUseNativeDialog : 0
        popupType: QQC2.Popup.Item
        title: i18n("Choose a Background Image")
        currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        nameFilters: [i18n("Images (*.png *.jpg *.jpeg *.webp *.avif *.jxl *.bmp *.gif *.svg)"), i18n("All files (*)")]
        onAccepted: {
            if (!Corrals.importBackground(dialog.corralId, selectedFile)) {
                errorLabel.text = i18n("Couldn't read that file.");
            } else {
                errorLabel.text = "";
            }
        }
    }

    contentItem: ColumnLayout {
        id: body
        readonly property real screenDpr: Screen.devicePixelRatio
        spacing: Kirigami.Units.largeSpacing

        // --------------------------------------------------------- preview
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 7

            // Same aspect ratio as the corral, so it shows what a crop will cut.
            Rectangle { // checkerboard-ish backdrop so transparency is visible
                id: previewFrame
                readonly property real aspect: dialog.corral.height > 0 ? dialog.corral.width / dialog.corral.height : 1.5
                anchors.centerIn: parent
                height: parent.height
                width: Math.min(parent.width, height * aspect)
                color: "#5a6270"
                radius: Kirigami.Units.cornerRadius
                clip: true

                Grid {
                    anchors.fill: parent
                    columns: Math.ceil(width / 12)
                    Repeater {
                        model: Math.ceil(previewFrame.width / 12) * Math.ceil(previewFrame.height / 12)
                        Rectangle {
                            required property int index
                            readonly property int cols: Math.ceil(previewFrame.width / 12)
                            width: 12; height: 12
                            color: ((index % cols) + Math.floor(index / cols)) % 2 ? "#6b7380" : "#5a6270"
                        }
                    }
                }

                CorralBackground {
                    anchors.fill: parent
                    corral: dialog.corralModel
                }
                QQC2.Label {
                    anchors { left: parent.left; top: parent.top; margins: Kirigami.Units.smallSpacing }
                    text: dialog.corralModel.title
                    color: "white"
                    font.bold: true
                }
            }
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true

            RowLayout {
                Kirigami.FormData.label: i18n("Background:")
                QQC2.RadioButton {
                    text: i18n("Colour")
                    checked: !dialog.imageMode
                    onToggled: {
                        if (checked) dialog.set("bgMode", "color");
                        checked = Qt.binding(() => !dialog.imageMode);
                    }
                }
                QQC2.RadioButton {
                    text: i18n("Image")
                    checked: dialog.imageMode
                    onToggled: {
                        if (checked) {
                            // No image yet: the mode switches once one is imported.
                            if (dialog.corralModel.bgImage.toString() === "") fileDialog.open();
                            else dialog.set("bgMode", "image");
                        }
                        checked = Qt.binding(() => dialog.imageMode);
                    }
                }
            }

            // ------------------------------------------------- colour mode
            QQC2.Button {
                Kirigami.FormData.label: i18n("Colour:")
                visible: !dialog.imageMode
                implicitWidth: Kirigami.Units.gridUnit * 4
                contentItem: Rectangle {
                    radius: Kirigami.Units.cornerRadius / 2
                    color: dialog.opaque(dialog.corralModel.bgColor)
                    border.color: Kirigami.Theme.textColor
                    border.width: 1
                }
                onClicked: dialog.pickColor("color", dialog.opaque(dialog.corralModel.bgColor))
            }
            PercentSlider {
                Kirigami.FormData.label: i18n("Transparency:")
                visible: !dialog.imageMode
                value: 1 - dialog.corralModel.bgColor.a
                onMoved: v => dialog.set("color", dialog.withAlpha(dialog.corralModel.bgColor, 1 - v).toString())
            }

            // -------------------------------------------------- image mode
            RowLayout {
                Kirigami.FormData.label: i18n("Image:")
                visible: dialog.imageMode
                QQC2.Button {
                    text: dialog.corralModel.bgImage.toString() === "" ? i18n("Choose Image…") : i18n("Change Image…")
                    icon.name: "document-open"
                    onClicked: fileDialog.open()
                }
                QQC2.Label {
                    id: errorLabel
                    color: Kirigami.Theme.negativeTextColor
                    visible: text !== ""
                }
            }

            ColumnLayout {
                Kirigami.FormData.label: i18n("Ideal size:")
                visible: dialog.imageMode
                spacing: 0
                QQC2.Label {
                    text: i18n("%1 × %2 px", dialog.idealWidth, dialog.idealHeight)
                    font.bold: true
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    text: dialog.dpr !== 1
                          ? i18n("This corral's size at your %1× display scale. Updates if you resize the corral.", dialog.dpr)
                          : i18n("This corral's size. Updates if you resize the corral.")
                }
            }

            ColumnLayout {
                Kirigami.FormData.label: i18n("Your image:")
                visible: dialog.imageMode && dialog.imageWidth > 0
                spacing: 0
                QQC2.Label {
                    text: i18n("%1 × %2 px", dialog.imageWidth, dialog.imageHeight)
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: dialog.verdict.text
                    color: dialog.verdict.ok ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.neutralTextColor
                }
            }

            QQC2.ComboBox {
                Kirigami.FormData.label: i18n("Fit:")
                visible: dialog.imageMode
                textRole: "label"
                valueRole: "value"
                model: [
                    { label: i18n("Crop to fill"), value: "crop" },
                    { label: i18n("Fit inside"), value: "fit" },
                    { label: i18n("Stretch"), value: "stretch" },
                    { label: i18n("Tile"), value: "tile" },
                    { label: i18n("Centre"), value: "center" },
                ]
                currentIndex: Math.max(0, indexOfValue(dialog.corralModel.bgFill))
                onActivated: {
                    dialog.set("bgFill", currentValue);
                    currentIndex = Qt.binding(() => Math.max(0, indexOfValue(dialog.corralModel.bgFill)));
                }
            }

            PercentSlider {
                Kirigami.FormData.label: i18n("Transparency:")
                visible: dialog.imageMode
                value: 1 - dialog.corralModel.bgImageOpacity
                onMoved: v => dialog.set("bgImageOpacity", 1 - v)
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Colour overlay:")
                visible: dialog.imageMode
                QQC2.CheckBox {
                    checked: dialog.corralModel.overlayEnabled
                    onToggled: {
                        dialog.set("overlayEnabled", checked);
                        checked = Qt.binding(() => dialog.corralModel.overlayEnabled);
                    }
                }
                QQC2.Button {
                    enabled: dialog.corralModel.overlayEnabled
                    implicitWidth: Kirigami.Units.gridUnit * 4
                    contentItem: Rectangle {
                        radius: Kirigami.Units.cornerRadius / 2
                        color: dialog.corralModel.overlayColor
                        border.color: Kirigami.Theme.textColor
                        border.width: 1
                        opacity: parent.enabled ? 1 : 0.4
                    }
                    onClicked: dialog.pickColor("overlayColor", dialog.corralModel.overlayColor)
                }
            }
            PercentSlider {
                Kirigami.FormData.label: i18n("Overlay strength:")
                visible: dialog.imageMode
                enabled: dialog.corralModel.overlayEnabled
                value: dialog.corralModel.overlayOpacity
                onMoved: v => dialog.set("overlayOpacity", v)
            }
        }
    }

    // Slider 0..1 with a percentage readout. Follows `value` (the saved setting)
    // and reports drags through moved(v) without breaking that binding.
    component PercentSlider: RowLayout {
        id: ps
        property real value: 0
        signal moved(real v)
        onValueChanged: if (!slider.pressed) slider.value = value
        QQC2.Slider {
            id: slider
            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
            from: 0
            to: 1
            // No stepSize: the KDE style would draw 100 tick marks. Round instead.
            Component.onCompleted: value = ps.value
            onMoved: ps.moved(Math.round(value * 100) / 100)
        }
        QQC2.Label {
            text: i18nc("percentage", "%1%", Math.round(slider.value * 100))
            Layout.minimumWidth: Kirigami.Units.gridUnit * 2.5
        }
    }
}
