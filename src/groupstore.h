// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GroupStore — the list of corrals, their settings, and where they live on disk.
//
// Exposed to QML as the `Corrals` singleton. Every corral is backed by a real
// folder, ~/Desktop/.ranch/<id>/, so "putting an icon in a corral" is just moving
// the file there. Plasma's desktop hides dot-folders, so the icon leaves the main
// desktop as a side effect. Settings are persisted as JSON in
// ~/.config/theranch/corrals.json.

#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QTimer>
#include <QUrl>

// One corral's settings. Field names match the JSON keys and the keys accepted by
// GroupStore::setValue(); the QML role names differ where a name would clash with
// a QQuickItem property (see GroupStore::roleNames()).
struct Corral {
    QString id;    // short random id; also the folder name under ~/Desktop/.ranch
    QString title;
    QColor color = QColor(0x2a, 0x5d, 0x8f, 0x80); // colour-mode background; alpha = 1 - transparency
    bool showTitle = true;
    bool showBackground = true;
    bool hidden = false; // hidden individually (the tray's "Hide All" is separate, see Ranch::allHidden)
    bool locked = false; // no move/resize
    QString screen;      // QScreen::name(); if that screen is gone the corral shows on the primary one

    // Geometry in logical pixels, relative to the screen's *available* area (panels
    // excluded). This is the size the user drew; nothing but resizing changes it.
    qreal x = 40, y = 40, width = 360, height = 240;

    // Drawer docking. Empty = free on the desktop. When docked, width/height above
    // are still the drawer's size, and dockOffset is its position along that edge.
    QString dock; // "", "left", "right", "top", "bottom"
    qreal dockOffset = 120;
    // Drawer stacking, set per drawer: "top" = above windows, "bottom" = below
    // windows, like the desktop. To change many at once, see setAllDrawerLayers().
    QString drawerLayer = QStringLiteral("top");

    int iconSize = 48;

    // Background: "color" (uses `color`, alpha included) or "image".
    QString bgMode = QStringLiteral("color");
    QString bgImage;              // local path of our imported copy (see importBackground)
    QString bgFill = QStringLiteral("crop"); // crop, fit, stretch, tile, center
    qreal bgImageOpacity = 1.0;   // 0..1 (the dialog shows 1 - this as "transparency")
    bool overlayEnabled = false;  // colour wash over the image
    QColor overlayColor = QColor(0, 0, 0);
    qreal overlayOpacity = 0.3;   // 0..1
};

class GroupStore : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        ColorRole,
        ShowTitleRole,
        ShowBackgroundRole,
        HiddenRole,
        LockedRole,
        ScreenRole,
        XRole,
        YRole,
        WidthRole,
        HeightRole,
        DockRole,
        DockOffsetRole,
        IconSizeRole,
        FolderUrlRole, // derived, read-only: file:// URL of the corral's folder
        BgModeRole,
        BgImageRole,
        BgFillRole,
        BgImageOpacityRole,
        OverlayEnabledRole,
        OverlayColorRole,
        OverlayOpacityRole,
        DrawerLayerRole,
    };

    // Loads corrals.json, recovers orphaned folders and prunes unused background images.
    explicit GroupStore(QObject *parent = nullptr);
    // Flushes a pending save, so a quit within the debounce window loses nothing.
    ~GroupStore() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Creates a corral and its backing folder; returns the new id.
    // Emits created(id) after the row is inserted (QML uses it to start renaming).
    Q_INVOKABLE QString create(const QString &screen, qreal x, qreal y, qreal w, qreal h);
    // Sets one field by its JSON key (e.g. "title", "x", "dock", "bgImageOpacity").
    // Unknown ids/keys are ignored with a warning. Saves are debounced.
    Q_INVOKABLE void setValue(const QString &id, const QString &key, const QVariant &value);
    // Sets geometry in one go (avoids four separate change notifications while dragging).
    Q_INVOKABLE void setGeometry(const QString &id, qreal x, qreal y, qreal w, qreal h);
    // Removes a corral. Its files are moved back to ~/Desktop first: deleting a
    // corral never deletes files.
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE QUrl folderUrl(const QString &id) const;
    // Copies an image into the app's data dir and makes it the corral's background.
    // Returns false if the file couldn't be read.
    Q_INVOKABLE bool importBackground(const QString &id, const QUrl &image);

    // One-off bulk edit: writes `layer` ("top"/"bottom") into every corral's own
    // setting. Nothing keeps following it afterwards.
    Q_INVOKABLE void setAllDrawerLayers(const QString &layer);

    // Read-only access for C++ (the tray menu).
    const QList<Corral> &corrals() const { return m_corrals; }

    static QString ranchRoot(); // ~/Desktop/.ranch
    static QString backgroundsDir(); // ~/.local/share/theranch/backgrounds

Q_SIGNALS:
    void countChanged();
    void created(const QString &id);

private:
    int indexOf(const QString &id) const;
    void load();
    // Any .ranch/<dir> without a record becomes a hidden "Recovered" corral, so
    // files can't be stranded in an invisible folder.
    void recoverOrphans();
    void scheduleSave();
    // Deletes imported background images that no corral refers to.
    void pruneBackgrounds();
    void save() const;
    static QString configPath();

    QList<Corral> m_corrals;
    QTimer m_saveTimer; // debounces writes (dragging emits many changes per second)
};
