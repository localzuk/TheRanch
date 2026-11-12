// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ranch — app-wide services, exposed to QML as the `Ranch` singleton.
//
// This is everything QML can't (or shouldn't) do itself:
//  * turning windows into wlr-layer-shell surfaces and shaping their input region;
//  * file operations through KIO (open, move, trash, native drag, the KDE file menu);
//  * the notification-area icon (KStatusNotifierItem) and its menu;
//  * global shortcuts (KGlobalAccel);
//  * app-wide UI state shared by every window: draw mode, hide-all, lock-all, and
//    whether a native menu or drag is in progress (drawers stay open while it is).

#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

class GroupStore;
class KAboutApplicationDialog;
class KStatusNotifierItem;
class QAction;
class QQuickItem;
class QQuickWindow;
class QScreen;

class Ranch : public QObject
{
    Q_OBJECT
    // True while the user is drawing a new corral (every pasture window shows the overlay).
    Q_PROPERTY(bool drawMode READ drawMode WRITE setDrawMode NOTIFY drawModeChanged)
    // "Hide All": hides every corral without touching their individual `hidden` flags,
    // so "Show All" brings back exactly what was visible before.
    Q_PROPERTY(bool allHidden READ allHidden WRITE setAllHidden NOTIFY allHiddenChanged)
    Q_PROPERTY(bool allLocked READ allLocked WRITE setAllLocked NOTIFY allLockedChanged)
    // A QMenu (file context menu) is open. Drawers read this to avoid sliding shut
    // underneath it, since the menu is a separate surface and steals hover.
    Q_PROPERTY(bool nativeMenuOpen READ nativeMenuOpen NOTIFY nativeMenuOpenChanged)
    // A drag started by us is in progress (same reason as nativeMenuOpen).
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged)
    Q_PROPERTY(QUrl desktopUrl READ desktopUrl CONSTANT)

public:
    explicit Ranch(GroupStore *store, QObject *parent = nullptr);

    bool drawMode() const { return m_drawMode; }
    void setDrawMode(bool on); // also un-hides everything, so the new corral is visible
    bool allHidden() const { return m_allHidden; }
    void setAllHidden(bool hidden);
    bool allLocked() const { return m_allLocked; }
    void setAllLocked(bool locked);
    bool nativeMenuOpen() const { return m_nativeMenuOpen; }
    bool dragging() const { return m_dragging; }
    QUrl desktopUrl() const;

    // --- surfaces ---------------------------------------------------------
    // Turns a (not yet shown) window into a full-area layer surface on the named screen.
    // top=false → BOTTOM layer (above the desktop, below windows); top=true → TOP layer.
    // (QML's Qt.application.screens hands out QQuickScreenInfo, not QScreen, hence the name.)
    Q_INVOKABLE void configureLayer(QQuickWindow *window, bool top, const QString &screenName);
    // Restricts pointer input to the union of rects ({x,y,width,height} in window coords).
    // Clicks outside fall through to whatever is underneath (the desktop or a window).
    Q_INVOKABLE void setInputRegion(QQuickWindow *window, const QVariantList &rects);
    // Exclusive keyboard while drawing so Esc reaches us; on-demand otherwise.
    Q_INVOKABLE void setExclusiveKeyboard(QQuickWindow *window, bool exclusive);

    // --- files ------------------------------------------------------------
    // Opens a file or runs a .desktop launcher, with KDE's usual prompts.
    Q_INVOKABLE void open(const QUrl &url);
    Q_INVOKABLE void openFolder(const QUrl &url);
    // Moves urls into folder `dest`; returns false when nothing needed moving.
    // Files from outside the desktop are linked, not moved (see moveUrls()).
    Q_INVOKABLE bool moveInto(const QVariantList &urls, const QUrl &dest);
    Q_INVOKABLE void trash(const QVariantList &urls);
    // Shows the native KDE file context menu (Open With, service menus, Properties…)
    // for `urls`, anchored at `pos` inside `item`.
    Q_INVOKABLE void showFileMenu(const QVariantList &urls, QQuickItem *item, QPointF pos);
    // Starts a blocking native drag. Returns the Qt::DropAction performed.
    Q_INVOKABLE int startDrag(const QVariantList &urls, const QString &iconName, int iconSize);

Q_SIGNALS:
    void drawModeChanged();
    void allHiddenChanged();
    void allLockedChanged();
    void nativeMenuOpenChanged();
    void draggingChanged();

private:
    void setupTray();
    void setupShortcuts();
    // Rebuilds the tray menu and tooltip from current state. Always called via
    // m_trayRefresh, never from inside a menu action's own signal (it deletes them).
    void rebuildTrayMenu();
    void showAbout();
    static QString autostartPath(); // ~/.config/autostart/org.theranch.desktop
    static bool autostartEnabled();
    static void setAutostartEnabled(bool enabled);
    void setNativeMenuOpen(bool open);
    bool moveUrls(const QList<QUrl> &urls, const QUrl &dest);
    void trashUrls(const QList<QUrl> &urls);

    GroupStore *m_store;
    KStatusNotifierItem *m_tray = nullptr;
    QPointer<KAboutApplicationDialog> m_about; // null once the dialog is closed (deleted)
    QTimer m_trayRefresh; // zero-delay single shot: coalesces bursts of changes into one rebuild
    bool m_drawMode = false;
    bool m_allHidden = false;
    bool m_allLocked = false;
    bool m_nativeMenuOpen = false;
    bool m_dragging = false;
};
