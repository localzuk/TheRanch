// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// See groupstore.h: the corral list, its JSON persistence, and the per-corral
// folders under ~/Desktop/.ranch.

#include "groupstore.h"

#include <KIO/CopyJob>
#include <KIO/JobUiDelegateFactory>
#include <KLocalizedString>

#include <QDateTime>
#include <QDir>
#include <QSet>

#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

using namespace Qt::StringLiterals;

// JSON/setValue key → role. QML role names are listed separately in roleNames()
// because some of them have to differ (e.g. "x" → "areaX").
static const QHash<QString, int> s_keyToRole = {
    {u"title"_s, GroupStore::TitleRole},
    {u"color"_s, GroupStore::ColorRole},
    {u"showTitle"_s, GroupStore::ShowTitleRole},
    {u"showBackground"_s, GroupStore::ShowBackgroundRole},
    {u"hidden"_s, GroupStore::HiddenRole},
    {u"locked"_s, GroupStore::LockedRole},
    {u"screen"_s, GroupStore::ScreenRole},
    {u"x"_s, GroupStore::XRole},
    {u"y"_s, GroupStore::YRole},
    {u"width"_s, GroupStore::WidthRole},
    {u"height"_s, GroupStore::HeightRole},
    {u"dock"_s, GroupStore::DockRole},
    {u"dockOffset"_s, GroupStore::DockOffsetRole},
    {u"iconSize"_s, GroupStore::IconSizeRole},
    {u"bgMode"_s, GroupStore::BgModeRole},
    {u"bgImage"_s, GroupStore::BgImageRole},
    {u"bgFill"_s, GroupStore::BgFillRole},
    {u"bgImageOpacity"_s, GroupStore::BgImageOpacityRole},
    {u"overlayEnabled"_s, GroupStore::OverlayEnabledRole},
    {u"overlayColor"_s, GroupStore::OverlayColorRole},
    {u"overlayOpacity"_s, GroupStore::OverlayOpacityRole},
    {u"drawerLayer"_s, GroupStore::DrawerLayerRole},
};

GroupStore::GroupStore(QObject *parent)
    : QAbstractListModel(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &GroupStore::save);
    load();
    recoverOrphans();
    // Not done on every change, so a cancelled Background dialog can still restore
    // the previous image.
    pruneBackgrounds();
}

GroupStore::~GroupStore()
{
    if (m_saveTimer.isActive()) {
        save();
    }
}

QString GroupStore::ranchRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + u"/.ranch"_s;
}

QString GroupStore::configPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u"/theranch/corrals.json"_s;
}

int GroupStore::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_corrals.size();
}

QVariant GroupStore::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid)) {
        return {};
    }
    const Corral &c = m_corrals.at(index.row());
    switch (role) {
    case IdRole: return c.id;
    case TitleRole: return c.title;
    case ColorRole: return c.color;
    case ShowTitleRole: return c.showTitle;
    case ShowBackgroundRole: return c.showBackground;
    case HiddenRole: return c.hidden;
    case LockedRole: return c.locked;
    case ScreenRole: return c.screen;
    case XRole: return c.x;
    case YRole: return c.y;
    case WidthRole: return c.width;
    case HeightRole: return c.height;
    case DockRole: return c.dock;
    case DockOffsetRole: return c.dockOffset;
    case IconSizeRole: return c.iconSize;
    case FolderUrlRole: return folderUrl(c.id);
    case BgModeRole: return c.bgMode;
    case BgImageRole: return c.bgImage.isEmpty() ? QUrl() : QUrl::fromLocalFile(c.bgImage);
    case BgFillRole: return c.bgFill;
    case BgImageOpacityRole: return c.bgImageOpacity;
    case OverlayEnabledRole: return c.overlayEnabled;
    case OverlayColorRole: return c.overlayColor;
    case OverlayOpacityRole: return c.overlayOpacity;
    case DrawerLayerRole: return c.drawerLayer;
    }
    return {};
}

QHash<int, QByteArray> GroupStore::roleNames() const
{
    // Names avoid clashing with QQuickItem properties (x, y, width, height, color…)
    // so delegates can declare them as required properties.
    return {
        {IdRole, "corralId"},
        {TitleRole, "title"},
        {ColorRole, "bgColor"},
        {ShowTitleRole, "showTitle"},
        {ShowBackgroundRole, "showBackground"},
        {HiddenRole, "hidden"},
        {LockedRole, "locked"},
        {ScreenRole, "screenName"},
        {XRole, "areaX"},
        {YRole, "areaY"},
        {WidthRole, "areaWidth"},
        {HeightRole, "areaHeight"},
        {DockRole, "dock"},
        {DockOffsetRole, "dockOffset"},
        {IconSizeRole, "iconSize"},
        {FolderUrlRole, "folderUrl"},
        {BgModeRole, "bgMode"},
        {BgImageRole, "bgImage"},
        {BgFillRole, "bgFill"},
        {BgImageOpacityRole, "bgImageOpacity"},
        {OverlayEnabledRole, "overlayEnabled"},
        {OverlayColorRole, "overlayColor"},
        {OverlayOpacityRole, "overlayOpacity"},
        {DrawerLayerRole, "drawerLayer"},
    };
}

QUrl GroupStore::folderUrl(const QString &id) const
{
    return QUrl::fromLocalFile(ranchRoot() + u'/' + id);
}

int GroupStore::indexOf(const QString &id) const
{
    for (int i = 0; i < m_corrals.size(); ++i) {
        if (m_corrals.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

QString GroupStore::create(const QString &screen, qreal x, qreal y, qreal w, qreal h)
{
    Corral c;
    // 8 hex chars: short enough to read in a file manager, plenty unique for a desktop.
    c.id = QUuid::createUuid().toString(QUuid::Id128).left(8);
    c.title = i18nc("default title of a new corral", "New Corral");
    c.screen = screen;
    c.x = x;
    c.y = y;
    c.width = w;
    c.height = h;
    QDir().mkpath(ranchRoot() + u'/' + c.id);

    beginInsertRows({}, m_corrals.size(), m_corrals.size());
    m_corrals.append(c);
    endInsertRows();
    Q_EMIT countChanged();
    Q_EMIT created(c.id);
    scheduleSave();
    return c.id;
}

void GroupStore::setValue(const QString &id, const QString &key, const QVariant &value)
{
    const int row = indexOf(id);
    const int role = s_keyToRole.value(key, -1);
    if (row < 0 || role < 0) {
        qWarning() << "GroupStore::setValue: bad id or key" << id << key;
        return;
    }
    // Values arrive from QML in whatever type JS had to hand (strings for colours,
    // url objects or strings for paths), so each setter normalises its input.
    Corral &c = m_corrals[row];
    switch (role) {
    case TitleRole: c.title = value.toString(); break;
    case ColorRole: c.color = value.typeId() == QMetaType::QString ? QColor::fromString(value.toString()) : value.value<QColor>(); break;
    case ShowTitleRole: c.showTitle = value.toBool(); break;
    case ShowBackgroundRole: c.showBackground = value.toBool(); break;
    case HiddenRole: c.hidden = value.toBool(); break;
    case LockedRole: c.locked = value.toBool(); break;
    case ScreenRole: c.screen = value.toString(); break;
    case XRole: c.x = value.toReal(); break;
    case YRole: c.y = value.toReal(); break;
    case WidthRole: c.width = value.toReal(); break;
    case HeightRole: c.height = value.toReal(); break;
    case DockRole: c.dock = value.toString(); break;
    case DockOffsetRole: c.dockOffset = value.toReal(); break;
    case IconSizeRole: c.iconSize = value.toInt(); break;
    case BgModeRole: c.bgMode = value.toString() == u"image"_s ? u"image"_s : u"color"_s; break;
    case BgImageRole: {
        // Only ever point at our own imported copies (or nothing). This also stops
        // pruneBackgrounds() from being asked to manage files we don't own.
        const QString path = value.typeId() == QMetaType::QUrl ? value.toUrl().toLocalFile() : value.toString();
        c.bgImage = path.startsWith(backgroundsDir()) ? path : QString();
        break;
    }
    case BgFillRole: c.bgFill = value.toString(); break;
    // Opacities are clamped; the UI shouldn't produce out-of-range values, but a
    // hand-edited JSON file or a script might.
    case BgImageOpacityRole: c.bgImageOpacity = std::clamp(value.toReal(), 0.0, 1.0); break;
    case OverlayEnabledRole: c.overlayEnabled = value.toBool(); break;
    case OverlayColorRole: c.overlayColor = value.typeId() == QMetaType::QString ? QColor::fromString(value.toString()) : value.value<QColor>(); break;
    case OverlayOpacityRole: c.overlayOpacity = std::clamp(value.toReal(), 0.0, 1.0); break;
    case DrawerLayerRole: {
        const QString v = value.toString();
        c.drawerLayer = v == u"bottom"_s ? v : u"top"_s;
        break;
    }
    }

    const QModelIndex idx = index(row);
    Q_EMIT dataChanged(idx, idx, {role});
    scheduleSave();
}

void GroupStore::setAllDrawerLayers(const QString &layer)
{
    for (const Corral &c : std::as_const(m_corrals)) {
        if (c.drawerLayer != layer) {
            setValue(c.id, u"drawerLayer"_s, layer);
        }
    }
}

void GroupStore::setGeometry(const QString &id, qreal x, qreal y, qreal w, qreal h)
{
    const int row = indexOf(id);
    if (row < 0) {
        return;
    }
    Corral &c = m_corrals[row];
    c.x = x;
    c.y = y;
    c.width = w;
    c.height = h;
    const QModelIndex idx = index(row);
    Q_EMIT dataChanged(idx, idx, {XRole, YRole, WidthRole, HeightRole});
    scheduleSave();
}

void GroupStore::remove(const QString &id)
{
    const int row = indexOf(id);
    if (row < 0) {
        return;
    }

    // Never lose files: move everything back onto the desktop, then drop the empty folder.
    // The move is asynchronous and uses KIO's normal dialogs, so a name clash on the
    // desktop asks the user (rename/skip) rather than overwriting anything.
    const QString dirPath = ranchRoot() + u'/' + id;
    const QDir dir(dirPath);
    QList<QUrl> urls;
    const auto entries = dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries) {
        urls << QUrl::fromLocalFile(fi.absoluteFilePath());
    }
    if (urls.isEmpty()) {
        QDir().rmdir(dirPath);
    } else {
        auto *job = KIO::move(urls, QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)));
        job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
        connect(job, &KJob::result, this, [dirPath](KJob *) {
            QDir().rmdir(dirPath); // only succeeds when empty, so skipped/conflicting files stay put
        });
    }

    beginRemoveRows({}, row, row);
    m_corrals.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
    pruneBackgrounds();
    scheduleSave();
}

QString GroupStore::backgroundsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/theranch/backgrounds"_s;
}

// Called by the Background dialog's file picker. Switches the corral to image mode
// on success.
bool GroupStore::importBackground(const QString &id, const QUrl &image)
{
    const int row = indexOf(id);
    const QString src = image.toLocalFile();
    if (row < 0 || src.isEmpty() || !QFileInfo(src).isReadable()) {
        return false;
    }
    // A copy, so the background survives the original being moved or deleted.
    // A fresh name each time so QML doesn't show a cached old image.
    QDir().mkpath(backgroundsDir());
    const QString suffix = QFileInfo(src).suffix().toLower();
    const QString dst = backgroundsDir() + u'/' + id + u'-' + QString::number(QDateTime::currentMSecsSinceEpoch())
        + (suffix.isEmpty() ? QString() : u'.' + suffix);
    if (!QFile::copy(src, dst)) {
        return false;
    }
    setValue(id, u"bgImage"_s, QUrl::fromLocalFile(dst));
    setValue(id, u"bgMode"_s, u"image"_s);
    return true;
}

// Deletes imported images no corral refers to any more.
// Deliberately NOT run after every change: the Background dialog's Cancel restores the
// previous bgImage, so that file must still exist until the dialog is done. Running
// at start-up and on corral removal is enough to stop the folder growing forever.
void GroupStore::pruneBackgrounds()
{
    QSet<QString> used;
    for (const Corral &c : std::as_const(m_corrals)) {
        if (!c.bgImage.isEmpty()) {
            used.insert(c.bgImage);
        }
    }
    const QDir dir(backgroundsDir());
    const auto files = dir.entryInfoList(QDir::Files);
    for (const QFileInfo &fi : files) {
        if (!used.contains(fi.absoluteFilePath())) {
            QFile::remove(fi.absoluteFilePath());
        }
    }
}

// Restarting the timer on every change means a drag that emits dozens of geometry
// updates per second results in one write, 500 ms after it stops.
void GroupStore::scheduleSave()
{
    m_saveTimer.start();
}

// Missing keys fall back to the Corral defaults, so older config files (from before a
// field existed) load without migration code.
void GroupStore::load()
{
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    // Migration: older versions had an app-wide "drawersOnTop" default that drawers
    // set to "default" followed. Each such drawer now gets that value as its own.
    const bool legacyOnTop = root.value(u"drawersOnTop").toBool(true);
    const QJsonArray arr = root.value(u"corrals").toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Corral c;
        c.id = o.value(u"id").toString();
        if (c.id.isEmpty()) {
            continue;
        }
        c.title = o.value(u"title").toString();
        c.color = QColor::fromString(o.value(u"color").toString(c.color.name(QColor::HexArgb)));
        c.showTitle = o.value(u"showTitle").toBool(true);
        c.showBackground = o.value(u"showBackground").toBool(true);
        c.hidden = o.value(u"hidden").toBool(false);
        c.locked = o.value(u"locked").toBool(false);
        c.screen = o.value(u"screen").toString();
        c.x = o.value(u"x").toDouble(c.x);
        c.y = o.value(u"y").toDouble(c.y);
        c.width = o.value(u"width").toDouble(c.width);
        c.height = o.value(u"height").toDouble(c.height);
        c.dock = o.value(u"dock").toString();
        c.dockOffset = o.value(u"dockOffset").toDouble(c.dockOffset);
        c.iconSize = o.value(u"iconSize").toInt(c.iconSize);
        c.bgMode = o.value(u"bgMode").toString(c.bgMode);
        c.bgImage = o.value(u"bgImage").toString();
        c.bgFill = o.value(u"bgFill").toString(c.bgFill);
        c.bgImageOpacity = o.value(u"bgImageOpacity").toDouble(c.bgImageOpacity);
        c.overlayEnabled = o.value(u"overlayEnabled").toBool(false);
        c.overlayColor = QColor::fromString(o.value(u"overlayColor").toString(c.overlayColor.name(QColor::HexArgb)));
        c.overlayOpacity = o.value(u"overlayOpacity").toDouble(c.overlayOpacity);
        const QString layer = o.value(u"drawerLayer").toString(u"default"_s);
        c.drawerLayer = layer == u"top"_s || layer == u"bottom"_s ? layer : (legacyOnTop ? u"top"_s : u"bottom"_s);
        QDir().mkpath(ranchRoot() + u'/' + c.id); // recreate if someone deleted it
        m_corrals.append(c);
    }
}

void GroupStore::recoverOrphans()
{
    const QDir root(ranchRoot());
    const auto dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    qreal offset = 0;
    for (const QString &name : dirs) {
        if (indexOf(name) >= 0) {
            continue;
        }
        Corral c;
        c.id = name;
        c.title = i18nc("title for a corral rebuilt from an unknown folder", "Recovered");
        c.hidden = true; // don't surprise the user; they can show it from the tray
        c.x += offset;   // cascade, so several recovered corrals don't stack exactly
        c.y += offset;
        offset += 24;
        m_corrals.append(c);
        scheduleSave();
    }
}

void GroupStore::save() const
{
    QJsonArray arr;
    for (const Corral &c : m_corrals) {
        arr.append(QJsonObject{
            {u"id"_s, c.id},
            {u"title"_s, c.title},
            {u"color"_s, c.color.name(QColor::HexArgb)},
            {u"showTitle"_s, c.showTitle},
            {u"showBackground"_s, c.showBackground},
            {u"hidden"_s, c.hidden},
            {u"locked"_s, c.locked},
            {u"screen"_s, c.screen},
            {u"x"_s, c.x},
            {u"y"_s, c.y},
            {u"width"_s, c.width},
            {u"height"_s, c.height},
            {u"dock"_s, c.dock},
            {u"dockOffset"_s, c.dockOffset},
            {u"iconSize"_s, c.iconSize},
            {u"bgMode"_s, c.bgMode},
            {u"bgImage"_s, c.bgImage},
            {u"bgFill"_s, c.bgFill},
            {u"bgImageOpacity"_s, c.bgImageOpacity},
            {u"overlayEnabled"_s, c.overlayEnabled},
            {u"overlayColor"_s, c.overlayColor.name(QColor::HexArgb)},
            {u"overlayOpacity"_s, c.overlayOpacity},
            {u"drawerLayer"_s, c.drawerLayer},
        });
    }
    QDir().mkpath(QFileInfo(configPath()).absolutePath());
    // QSaveFile writes to a temp file and renames on commit(), so a crash or power
    // cut mid-write can't leave a truncated corrals.json behind.
    QSaveFile f(configPath());
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot write" << configPath();
        return;
    }
    f.write(QJsonDocument(QJsonObject{{u"version"_s, 1}, {u"corrals"_s, arr}}).toJson());
    f.commit();
}
