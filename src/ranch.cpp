// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// See ranch.h. Sections: state setters, layer-shell surfaces, file operations
// (KIO), native menus and drags, then the tray icon, global shortcuts and quick open.

#include "ranch.h"
#include "aboutlibraries.h"
#include "groupstore.h"

#include <KAboutApplicationDialog>
#include <KAboutData>
#include <KActionCollection>
#include <KFileItem>
#include <KFileItemActions>
#include <KFileItemListProperties>
#include <KGlobalAccel>
#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenUrlJob>
#include <KIO/RenameFileDialog>
#include <KLocalizedString>
#include <KPropertiesDialog>
#include <KShortcutsDialog>
#include <KStatusNotifierItem>
#include <KUrlMimeData>
#include <LayerShellQt/Window>

#include <QAction>
#include <QDrag>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QMimeData>
#include <QQuickItem>
#include <QQuickWindow>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTabWidget>

using namespace Qt::StringLiterals;

// QML hands us arrays of url objects or strings; accept both.
static QList<QUrl> toUrls(const QVariantList &list)
{
    QList<QUrl> out;
    for (const QVariant &v : list) {
        const QUrl u = v.typeId() == QMetaType::QUrl ? v.toUrl() : QUrl(v.toString());
        if (u.isValid()) {
            out << u;
        }
    }
    return out;
}

Ranch::Ranch(GroupStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    setupTray();
    setupShortcuts();
}

// ------------------------------------------------------------- quick open
//
// Quick open is a launcher-style popup: a separate window that lists every
// corral's icons, one titled section per corral, centred on the active screen and
// closed as soon as something is opened. It is deliberately *not* the corrals:
// corrals and drawers never change layer or position because of it, and the popup
// has its own look and type-to-filter, like an application launcher.

void Ranch::toggleQuickOpen()
{
    m_quickOpenVisible = !m_quickOpenVisible;
    Q_EMIT quickOpenVisibleChanged();
}

void Ranch::closeQuickOpen()
{
    if (m_quickOpenVisible) {
        m_quickOpenVisible = false;
        Q_EMIT quickOpenVisibleChanged();
    }
}

// The quick-open key, shown as a hint beside the tray menu's Quick Open item.
static QKeySequence firstShortcut(QAction *action)
{
    const QList<QKeySequence> keys = action ? KGlobalAccel::self()->shortcut(action) : QList<QKeySequence>{};
    return keys.isEmpty() ? QKeySequence() : keys.first();
}

QUrl Ranch::desktopUrl() const
{
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
}

void Ranch::setDrawMode(bool on)
{
    if (m_drawMode == on) {
        return;
    }
    if (on && m_allHidden) {
        setAllHidden(false);
    }
    m_drawMode = on;
    Q_EMIT drawModeChanged();
}

void Ranch::setAllHidden(bool hidden)
{
    if (m_allHidden == hidden) {
        return;
    }
    m_allHidden = hidden;
    Q_EMIT allHiddenChanged();
}

void Ranch::setAllLocked(bool locked)
{
    if (m_allLocked == locked) {
        return;
    }
    m_allLocked = locked;
    Q_EMIT allLockedChanged();
}

void Ranch::setNativeMenuOpen(bool open)
{
    if (m_nativeMenuOpen == open) {
        return;
    }
    m_nativeMenuOpen = open;
    Q_EMIT nativeMenuOpenChanged();
}

// ---------------------------------------------------------------- surfaces
//
// Every window The Ranch shows on the desktop is a wlr-layer-shell surface covering
// one screen's available area. Layer-shell is what lets us sit *between* the Plasma
// desktop and normal windows (BOTTOM) or above windows (TOP), which neither a normal
// window nor a Plasma widget can do. Plasma 6.8 is Wayland-only, so this is always
// available.

void Ranch::configureLayer(QQuickWindow *window, bool top, const QString &screenName)
{
    if (!window) {
        return;
    }
    auto *lsw = LayerShellQt::Window::get(window);
    if (!lsw) {
        qWarning() << "LayerShellQt unavailable: is this a Wayland session?";
        return;
    }
    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen->name() == screenName) {
            window->setScreen(screen);
            lsw->setScreen(screen);
            break;
        }
    }
    // Stay on the screen we asked for, not whichever one has focus.
    lsw->setWantsToBeOnActiveScreen(false);
    // The scope is a free-form name; compositors and tools like `kwin --debug` show it.
    lsw->setScope(top ? u"theranch-drawers"_s : u"theranch-pasture"_s);
    lsw->setLayer(top ? LayerShellQt::Window::LayerTop : LayerShellQt::Window::LayerBottom);
    // Anchored to all four edges = the compositor sizes us to the whole available area.
    lsw->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                                  | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
    lsw->setExclusiveZone(0); // respect panels' exclusive zones, claim none ourselves
    // OnDemand: we get keyboard focus only when clicked (renaming, Delete key), and
    // never steal it just by appearing.
    lsw->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
    lsw->setActivateOnShow(false);
}

// On Wayland, QWindow::setMask() sets the surface's *input* region (and only that,
// since our windows are translucent). Outside it, the compositor delivers clicks,
// hover and drops to whatever is underneath: the desktop or another window.
void Ranch::setInputRegion(QQuickWindow *window, const QVariantList &rects)
{
    if (!window) {
        return;
    }
    QRegion region;
    for (const QVariant &v : rects) {
        const QRectF r = v.toRectF();
        if (r.isValid()) {
            region += r.toAlignedRect();
        }
    }
    // An empty mask means "no mask" (whole window takes input), so keep a single
    // pixel in the corner instead when nothing should be interactive.
    if (region.isEmpty()) {
        region = QRegion(0, 0, 1, 1);
    }
    window->setMask(region);
}

void Ranch::configureQuickOpenLayer(QQuickWindow *window)
{
    auto *lsw = window ? LayerShellQt::Window::get(window) : nullptr;
    if (!lsw) {
        return;
    }
    // Whichever screen the user is working on, like KRunner.
    lsw->setWantsToBeOnActiveScreen(true);
    lsw->setScope(u"theranch-quickopen"_s);
    lsw->setLayer(LayerShellQt::Window::LayerOverlay);
    lsw->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                                  | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
    lsw->setExclusiveZone(-1); // cover panels too: the dimmed backdrop closes the popup when clicked
    lsw->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
    lsw->setActivateOnShow(true);
}

void Ranch::setDrawing(QQuickWindow *window, bool drawing)
{
    auto *lsw = window ? LayerShellQt::Window::get(window) : nullptr;
    if (!lsw) {
        return;
    }
    // The layer can change while the surface is mapped (Plasma's panels do the
    // same). Exclusive keyboard is also only guaranteed on the top and overlay
    // layers; on the bottom layer the compositor may ignore it.
    lsw->setLayer(drawing ? LayerShellQt::Window::LayerOverlay : LayerShellQt::Window::LayerBottom);
    lsw->setKeyboardInteractivity(drawing ? LayerShellQt::Window::KeyboardInteractivityExclusive
                                          : LayerShellQt::Window::KeyboardInteractivityOnDemand);
    if (drawing) {
        window->requestActivate();
    }
    window->requestUpdate(); // a frame, so the new layer state is committed promptly
}

// ------------------------------------------------------------------- files
//
// All file operations go through KIO with the default UI delegate, so the user gets
// the same conflict, permission and "untrusted launcher" dialogs as in Dolphin.

void Ranch::open(const QUrl &url)
{
    auto *job = new KIO::OpenUrlJob(url);
    job->setRunExecutables(true); // .desktop launchers on the desktop must start their app
    // Executable scripts and binaries ask "open or execute?" first, as in Dolphin,
    // rather than running on a single click. Launchers are unaffected.
    job->setShowOpenOrExecuteDialog(true);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
    job->start();
}

void Ranch::openFolder(const QUrl &url)
{
    auto *job = new KIO::OpenUrlJob(url, u"inode/directory"_s);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
    job->start();
}

bool Ranch::moveInto(const QVariantList &urls, const QUrl &dest)
{
    return moveUrls(toUrls(urls), dest);
}

// Folders whose contents The Ranch manages: the desktop itself and every corral.
// Compared with symlinks resolved: ~/Desktop may itself be a link, and drags can
// arrive with either spelling of the path.
static QString canonicalDir(const QString &path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath(); // empty if it doesn't exist
    return canonical.isEmpty() ? QDir(path).absolutePath() : canonical;
}

static bool isDesktopTree(const QString &dirPath)
{
    const QString dir = canonicalDir(dirPath);
    const QString desktop = canonicalDir(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    const QString root = canonicalDir(GroupStore::ranchRoot());
    return dir == desktop || QFileInfo(dir).absolutePath() == root;
}

// Drags from the Plasma desktop can arrive with symlinks already resolved to their
// target (e.g. a desktop link to ~/.local/share/applications/foo.desktop). Find the
// desktop/corral symlink that points at `path`, so we move the link, not the target.
static QString findLinkTo(const QString &path)
{
    const QString target = QFileInfo(path).canonicalFilePath();
    if (target.isEmpty()) {
        return {};
    }
    QStringList dirs{QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)};
    const QDir root(GroupStore::ranchRoot());
    for (const QString &d : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        dirs << root.filePath(d);
    }
    for (const QString &dir : std::as_const(dirs)) {
        const auto entries = QDir(dir).entryInfoList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
        for (const QFileInfo &fi : entries) {
            if (fi.isSymLink() && fi.canonicalFilePath() == target) {
                return fi.absoluteFilePath();
            }
        }
    }
    return {};
}

// The one place files get moved into (or out of) corrals. Rules:
//  1. A file already on the desktop or in a corral is moved, so it leaves where it
//     was. That's the whole point of grouping.
//  2. A file from anywhere else is treated with suspicion. If a desktop/corral
//     symlink points at it, that *link* is what the user dragged (see findLinkTo),
//     so we move the link. Otherwise (e.g. a drag from Dolphin) we create a link in
//     the corral and leave the original alone. The Ranch must never pull files out
//     of other places, such as an app's menu entry in ~/.local/share/applications.
//  3. Dropping a file onto the folder it's already in is a no-op.
bool Ranch::moveUrls(const QList<QUrl> &urls, const QUrl &dest)
{
    QList<QUrl> toMove;
    QList<QUrl> toLink;
    for (QUrl u : urls) {
        if (!u.isLocalFile()) {
            // Remote or virtual locations (sftp:, smb:, mtp:, trash:, …) are "elsewhere"
            // too: a move would delete the original there, so link instead.
            toLink << u;
            continue;
        }
        {
            const QString dir = QFileInfo(u.toLocalFile()).absolutePath();
            if (!isDesktopTree(dir)) {
                const QString link = findLinkTo(u.toLocalFile());
                if (!link.isEmpty()) {
                    u = QUrl::fromLocalFile(link); // move the desktop shortcut, not what it points to
                } else {
                    // Never take files away from elsewhere on the system: link them instead.
                    toLink << u;
                    continue;
                }
            }
        }
        if (u.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash) != dest.adjusted(QUrl::StripTrailingSlash) && u != dest) {
            toMove << u;
        }
    }
    if (toMove.isEmpty() && toLink.isEmpty()) {
        return false;
    }
    if (!toMove.isEmpty()) {
        auto *job = KIO::move(toMove, dest);
        job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
    }
    if (!toLink.isEmpty()) {
        auto *job = KIO::link(toLink, dest);
        job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
    }
    return true;
}

void Ranch::trash(const QVariantList &urls)
{
    trashUrls(toUrls(urls));
}

void Ranch::trashUrls(const QList<QUrl> &urls)
{
    if (urls.isEmpty()) {
        return;
    }
    auto *job = KIO::trash(urls);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
}

// Builds the same kind of menu Dolphin/the desktop show for files: Open, Open With,
// our own corral-specific actions, service menus (KFileItemActions), and Properties.
void Ranch::showFileMenu(const QVariantList &urlList, QQuickItem *item, QPointF pos)
{
    const QList<QUrl> urls = toUrls(urlList);
    if (urls.isEmpty() || !item || !item->window()) {
        return;
    }
    KFileItemList items;
    for (const QUrl &u : urls) {
        KFileItem fi(u);
        fi.determineMimeType();
        items << fi;
    }

    auto *menu = new QMenu;
    menu->setAttribute(Qt::WA_DeleteOnClose);

    menu->addAction(QIcon::fromTheme(u"document-open"_s), i18n("Open"), this, [this, urls] {
        for (const QUrl &u : urls) {
            open(u);
        }
    });

    auto *actions = new KFileItemActions(menu);
    actions->setItemListProperties(KFileItemListProperties(items));
    actions->setParentWidget(menu);
    actions->insertOpenWithActionsTo(nullptr, menu, {});
    menu->addSeparator();

    if (items.size() == 1) {
        menu->addAction(QIcon::fromTheme(u"edit-rename"_s), i18n("Rename…"), this, [items] {
            auto *dlg = new KIO::RenameFileDialog(items, nullptr);
            dlg->open();
        });
    }
    menu->addAction(QIcon::fromTheme(u"user-trash"_s), i18n("Move to Trash"), this, [this, urls] {
        trashUrls(urls);
    });
    menu->addAction(QIcon::fromTheme(u"user-desktop"_s), i18n("Remove from Corral"), this, [this, urls] {
        moveUrls(urls, desktopUrl());
    });
    menu->addSeparator();

    actions->addActionsTo(menu, KFileItemActions::MenuActionSource::Services | KFileItemActions::MenuActionSource::Plugins);

    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(u"document-properties"_s), i18n("Properties"), this, [items] {
        auto *dlg = new KPropertiesDialog(items);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });

    // A layer-shell popup needs its parent surface set explicitly; without a transient
    // parent, QtWayland can't create the xdg_popup and the menu never appears.
    // winId() forces the native window to exist so we can set that.
    menu->winId();
    menu->windowHandle()->setTransientParent(item->window());
    connect(menu, &QMenu::aboutToHide, this, [this] {
        setNativeMenuOpen(false);
    });
    // Also when it goes away without ever hiding (it never got shown), or drawers
    // would stay open for good waiting for a menu that isn't there.
    connect(menu, &QObject::destroyed, this, [this] {
        setNativeMenuOpen(false);
    });
    setNativeMenuOpen(true);
    menu->popup(item->mapToGlobal(pos).toPoint());
    if (!menu->isVisible()) {
        menu->deleteLater();
    }
}

// Called from a QML MouseArea once the pointer passes the drag threshold. QDrag::exec()
// runs a nested event loop until the drop, so the drop target (desktop, Dolphin,
// another corral) performs the move while we wait. `dragging` is raised meanwhile
// so drawers stay open under the cursor.
int Ranch::startDrag(const QVariantList &urlList, const QString &iconName, int iconSize)
{
    const QList<QUrl> urls = toUrls(urlList);
    if (urls.isEmpty()) {
        return int(Qt::IgnoreAction);
    }
    auto *mime = new QMimeData;
    // Same list as both "urls" and "most local urls": ours are always local files.
    KUrlMimeData::setUrls(urls, urls, mime);

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pm = QIcon::fromTheme(iconName, QIcon::fromTheme(u"unknown"_s)).pixmap(QSize(iconSize, iconSize), dpr);
    drag->setPixmap(pm);
    drag->setHotSpot(QPoint(iconSize / 2, iconSize / 2));

    m_dragging = true;
    Q_EMIT draggingChanged();
    const Qt::DropAction result = drag->exec(Qt::MoveAction | Qt::CopyAction | Qt::LinkAction, Qt::MoveAction);
    m_dragging = false;
    Q_EMIT draggingChanged();
    return int(result);
}

// ------------------------------------------------------------ tray & keys
//
// The tray icon is a KStatusNotifierItem (shown by Plasma's system tray over D-Bus).
// Its menu is exported via dbusmenu, so it's rebuilt from scratch whenever state
// changes rather than patched in place.

void Ranch::setupTray()
{
    m_tray = new KStatusNotifierItem(u"theranch"_s, this);
    m_tray->setTitle(i18n("The Ranch"));
    m_tray->setIconByName(u"view-grid-symbolic"_s);
    m_tray->setCategory(KStatusNotifierItem::ApplicationStatus);
    m_tray->setStatus(KStatusNotifierItem::Active);
    // We supply our own Quit so the menu layout is entirely ours.
    m_tray->setStandardActionsEnabled(false);

    auto *menu = new QMenu;
    m_tray->setContextMenu(menu);

    // Left-click: show/hide everything. Middle-click: draw a new corral.
    connect(m_tray, &KStatusNotifierItem::activateRequested, this, [this] {
        setAllHidden(!m_allHidden);
    });
    connect(m_tray, &KStatusNotifierItem::secondaryActivateRequested, this, [this] {
        setDrawMode(!m_drawMode); // again to cancel
    });

    // Keep the menu and tooltip current whenever anything they show changes, rather
    // than relying on the tray host asking first (dbusmenu AboutToShow is optional).
    m_trayRefresh.setSingleShot(true);
    m_trayRefresh.setInterval(0);
    connect(&m_trayRefresh, &QTimer::timeout, this, &Ranch::rebuildTrayMenu);
    const auto refresh = [this] {
        m_trayRefresh.start();
    };
    connect(m_store, &QAbstractItemModel::rowsInserted, this, refresh);
    connect(m_store, &QAbstractItemModel::rowsRemoved, this, refresh);
    connect(m_store, &QAbstractItemModel::dataChanged, this, refresh);
    connect(m_store, &QAbstractItemModel::modelReset, this, refresh);
    connect(this, &Ranch::allHiddenChanged, this, refresh);
    connect(this, &Ranch::allLockedChanged, this, refresh);
    connect(this, &Ranch::drawModeChanged, this, refresh);
    // Shortcuts changed in System Settings → Shortcuts.
    connect(KGlobalAccel::self(), &KGlobalAccel::globalShortcutChanged, this, refresh);
    rebuildTrayMenu();
}

QString Ranch::autostartPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u"/autostart/org.theranch.desktop"_s;
}

bool Ranch::autostartEnabled()
{
    QFile f(autostartPath());
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    return !QString::fromUtf8(f.readAll()).contains(u"\nHidden=true"_s);
}

// A path as one argument of a .desktop Exec= line: quoted, with the characters the
// Desktop Entry spec reserves inside quotes escaped, then backslashes doubled once
// more for the string-value escaping every key goes through, and "%" (a field code
// marker) doubled.
static QString desktopExecQuote(const QString &path)
{
    QString out = path;
    out.replace(u'\\', u"\\\\"_s).replace(u'"', u"\\\""_s).replace(u'`', u"\\`"_s).replace(u'$', u"\\$"_s);
    out.replace(u'\\', u"\\\\"_s);
    out.replace(u'%', u"%%"_s);
    return u'"' + out + u'"';
}

void Ranch::setAutostartEnabled(bool enabled)
{
    if (!enabled) {
        QFile::remove(autostartPath());
        return;
    }
    QDir().mkpath(QFileInfo(autostartPath()).absolutePath());
    QSaveFile f(autostartPath());
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot write" << autostartPath();
        return;
    }
    f.write(u"[Desktop Entry]\n"
            "Type=Application\n"
            "Name=The Ranch\n"
            "Comment=Desktop icon corrals\n"
            "Exec=%1\n"
            "Icon=view-grid-symbolic\n"
            "X-KDE-autostart-phase=2\n"_s.arg(desktopExecQuote(QCoreApplication::applicationFilePath()))
                .toUtf8());
    f.commit();
}

void Ranch::rebuildTrayMenu()
{
    const auto &corrals = m_store->corrals();
    int hiddenCount = 0;
    for (const Corral &c : corrals) {
        hiddenCount += c.hidden ? 1 : 0;
    }
    const bool anyVisible = !m_allHidden && hiddenCount < corrals.size();
    const bool anyHidden = m_allHidden || hiddenCount > 0;

    // Tooltip summary
    QString summary;
    if (corrals.isEmpty()) {
        summary = i18n("No corrals yet");
    } else if (m_allHidden) {
        summary = i18np("1 corral, all hidden", "%1 corrals, all hidden", corrals.size());
    } else {
        summary = i18np("1 corral", "%1 corrals", corrals.size());
        if (hiddenCount > 0) {
            summary += i18nc("appended to corral count", ", %1 hidden", hiddenCount);
        }
    }
    m_tray->setToolTip(u"view-grid-symbolic"_s,
                       i18n("The Ranch"),
                       summary + u"<br/>"_s + i18n("Click to show or hide all · Middle-click for a new corral"));
    m_tray->setOverlayIconByName(m_allHidden ? u"view-hidden"_s : QString());

    // Actions are rebuilt from a queued timer, never from inside one of their own
    // signals, so clearing (and deleting) them here is safe.
    QMenu *menu = m_tray->contextMenu();
    menu->clear();

    // While drawing, the same item cancels: the draw overlay sits below application
    // windows, so with the desktop covered there may be nowhere to press Esc.
    if (m_drawMode) {
        menu->addAction(QIcon::fromTheme(u"dialog-cancel"_s), i18n("Cancel New Corral"), this, [this] {
            setDrawMode(false);
        });
    } else {
        menu->addAction(QIcon::fromTheme(u"list-add"_s), i18n("New Corral…"), this, [this] {
            setDrawMode(true);
        });
    }

    menu->addSeparator(); // (tray hosts drop QMenu section titles, so plain separator)
    if (corrals.isEmpty()) {
        menu->addAction(i18n("No corrals yet"))->setEnabled(false);
    }
    for (const Corral &c : corrals) {
        QString label = c.title.isEmpty() ? i18n("(untitled)") : c.title;
        if (!c.dock.isEmpty()) {
            label = i18nc("corral title, docked as drawer", "%1 (drawer)", label);
        }
        // "&" marks a keyboard accelerator in menu text, so "City & Base" would show
        // as "City _ Base"; doubling it shows a literal ampersand.
        label.replace(u'&', u"&&"_s);
        auto *a = menu->addAction(label);
        a->setCheckable(true);
        a->setChecked(!c.hidden && !m_allHidden);
        const QString id = c.id;
        connect(a, &QAction::triggered, this, [this, id](bool visible) {
            if (visible) {
                setAllHidden(false);
            }
            m_store->setValue(id, u"hidden"_s, !visible);
        });
    }

    // Quick open from the tray too, so the feature works without a shortcut.
    if (!corrals.isEmpty() && m_quickOpenAction) {
        auto *quick = menu->addAction(QIcon::fromTheme(u"quickopen"_s), i18n("Quick Open…"), this, &Ranch::toggleQuickOpen);
        quick->setShortcut(firstShortcut(m_quickOpenAction)); // shown as a hint
    }

    menu->addSeparator();
    auto *showAll = menu->addAction(QIcon::fromTheme(u"view-visible"_s), i18n("Show All Corrals"), this, [this] {
        setAllHidden(false);
        for (const Corral &c : m_store->corrals()) {
            if (c.hidden) {
                m_store->setValue(c.id, u"hidden"_s, false);
            }
        }
    });
    showAll->setEnabled(anyHidden);
    auto *hideAll = menu->addAction(QIcon::fromTheme(u"view-hidden"_s), i18n("Hide All Corrals"), this, [this] {
        setAllHidden(true);
    });
    hideAll->setEnabled(anyVisible);

    menu->addSeparator();
    auto *lockAll = menu->addAction(QIcon::fromTheme(u"object-locked"_s), i18n("Lock All Corrals"));
    lockAll->setCheckable(true);
    lockAll->setChecked(m_allLocked);
    connect(lockAll, &QAction::triggered, this, &Ranch::setAllLocked);

    // One-off bulk edits of every drawer's own stacking setting. There is no
    // app-wide default that drawers keep following (each drawer's setting stands on
    // its own), so these are plain actions, enabled only when they'd change something.
    int above = 0, below = 0;
    for (const Corral &c : corrals) {
        if (!c.dock.isEmpty()) {
            (c.drawerLayer == u"bottom"_s ? below : above)++;
        }
    }
    auto *allAbove = menu->addAction(QIcon::fromTheme(u"window-keep-above"_s), i18n("Put All Drawers Above Windows"), this, [this] {
        m_store->setAllDrawerLayers(u"top"_s);
    });
    allAbove->setEnabled(below > 0);
    auto *allBelow = menu->addAction(QIcon::fromTheme(u"window-keep-below"_s), i18n("Put All Drawers Below Windows"), this, [this] {
        m_store->setAllDrawerLayers(u"bottom"_s);
    });
    allBelow->setEnabled(above > 0);

    auto *autostart = menu->addAction(QIcon::fromTheme(u"system-run"_s), i18n("Start at Login"));
    autostart->setCheckable(true);
    autostart->setChecked(autostartEnabled());
    connect(autostart, &QAction::triggered, this, [this](bool on) {
        setAutostartEnabled(on);
        m_trayRefresh.start();
    });

    menu->addAction(QIcon::fromTheme(u"configure-shortcuts"_s), i18n("Configure Shortcuts…"), this, &Ranch::showShortcutsDialog);
    menu->addAction(QIcon::fromTheme(u"folder-open"_s), i18n("Open Corrals Folder"), this, [this] {
        QDir().mkpath(GroupStore::ranchRoot());
        openFolder(QUrl::fromLocalFile(GroupStore::ranchRoot()));
    });

    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(u"help-about"_s), i18n("About The Ranch"), this, &Ranch::showAbout);
    menu->addAction(QIcon::fromTheme(u"application-exit"_s), i18n("Quit"), qApp, &QCoreApplication::quit);
}

void Ranch::setupShortcuts()
{
    // Registered without default keys; users bind them from the tray menu's
    // Configure Shortcuts… or System Settings → Shortcuts.
    auto *toggle = new QAction(i18n("Show/Hide All Corrals"), this);
    toggle->setObjectName(u"toggle-all-corrals"_s);
    KGlobalAccel::self()->setGlobalShortcut(toggle, QList<QKeySequence>{});
    connect(toggle, &QAction::triggered, this, [this] {
        setAllHidden(!m_allHidden);
    });

    auto *draw = new QAction(i18n("Draw New Corral"), this);
    draw->setObjectName(u"draw-new-corral"_s);
    KGlobalAccel::self()->setGlobalShortcut(draw, QList<QKeySequence>{});
    connect(draw, &QAction::triggered, this, [this] {
        setDrawMode(!m_drawMode); // again to cancel
    });

    m_quickOpenAction = new QAction(i18n("Quick Open"), this);
    m_quickOpenAction->setObjectName(u"quick-open"_s);
    KGlobalAccel::self()->setGlobalShortcut(m_quickOpenAction, QList<QKeySequence>{});
    connect(m_quickOpenAction, &QAction::triggered, this, &Ranch::toggleQuickOpen);

    // The same actions, gathered for the Configure Shortcuts dialog.
    m_globalActions = new KActionCollection(this, QCoreApplication::applicationName());
    m_globalActions->addAction(m_quickOpenAction->objectName(), m_quickOpenAction);
    m_globalActions->addAction(toggle->objectName(), toggle);
    m_globalActions->addAction(draw->objectName(), draw);

    // Every action we use is registered above, so anything else stored for our
    // component is stale (e.g. the per-corral "quick-open-<id>" entries of earlier
    // builds) and can be dropped from System Settings.
    KGlobalAccel::cleanComponent(QCoreApplication::applicationName());
    m_trayRefresh.start(); // the tray menu shows the quick-open key loaded just now
}

// A plain top-level window (not a layer surface), so it stacks like any other
// dialog. Opened from the tray, so it never has to compete with a drawer for focus.
void Ranch::showShortcutsDialog()
{
    if (m_shortcutsDialog) {
        m_shortcutsDialog->raise();
        m_shortcutsDialog->activateWindow();
        return;
    }
    // GlobalAction only: these actions have no in-app (local) shortcuts to show.
    m_shortcutsDialog = new KShortcutsDialog(KShortcutsEditor::GlobalAction, KShortcutsEditor::LetterShortcutsDisallowed);
    m_shortcutsDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_shortcutsDialog->setWindowTitle(i18n("Configure Shortcuts — The Ranch"));
    m_shortcutsDialog->addCollection(m_globalActions, i18n("The Ranch"));
    connect(m_shortcutsDialog, &KShortcutsDialog::saved, this, [this] {
        m_trayRefresh.start(); // the Quick Open item shows its key
    });
    m_shortcutsDialog->configure(true); // non-modal: shows and returns
}

// KDE's standard About dialog (About / Components / Author tabs, the licence link and
// the bug-report link), filled from the KAboutData set up in main.cpp, plus our own
// "Libraries" tab crediting every third-party library and its licences (see
// aboutlibraries.h). One at a time: choosing the menu item again just brings the
// open dialog forward.
void Ranch::showAbout()
{
    if (m_about) {
        m_about->raise();
        m_about->activateWindow();
        return;
    }
    m_about = new KAboutApplicationDialog(KAboutData::applicationData());
    m_about->setAttribute(Qt::WA_DeleteOnClose);
    // KAboutApplicationDialog has no API for extra tabs, but is built around a single
    // QTabWidget. Insert after "Components" (index 1), which stays: its platform
    // details and "Copy to Clipboard" are what bug reports need.
    if (auto *tabs = m_about->findChild<QTabWidget *>()) {
        tabs->insertTab(2, AboutLibraries::createPage(tabs), i18nc("@title:tab", "Libraries"));
    } else {
        qWarning("About dialog has no tab widget; the Libraries tab is missing");
    }
    m_about->show();
}
