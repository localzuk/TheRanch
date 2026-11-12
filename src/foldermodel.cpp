// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// See foldermodel.h for what this model is and why it exists.

#include "foldermodel.h"

#include <KCoreDirLister>
#include <KDesktopFile>

#include <algorithm>

FolderModel::FolderModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_lister(new KCoreDirLister(this))
{
    m_collator.setNumericMode(true);
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);

    m_lister->setAutoUpdate(true);          // watch the folder and keep us in sync
    m_lister->setShowHiddenFiles(false);    // dotfiles stay hidden, as on the desktop
    m_lister->setAutoErrorHandlingEnabled(false); // no error popups from a background helper

    connect(m_lister, &KCoreDirLister::newItems, this, &FolderModel::onNewItems);
    connect(m_lister, &KCoreDirLister::itemsDeleted, this, &FolderModel::onItemsDeleted);
    connect(m_lister, &KCoreDirLister::refreshItems, this, &FolderModel::onRefreshItems);
    connect(m_lister, qOverload<>(&KCoreDirLister::clear), this, &FolderModel::onClear);
}

void FolderModel::setUrl(const QUrl &url)
{
    if (m_url == url) {
        return;
    }
    m_url = url;
    onClear();
    if (url.isValid()) {
        m_lister->openUrl(url);
    }
    Q_EMIT urlChanged();
}

int FolderModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant FolderModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid)) {
        return {};
    }
    const Entry &e = m_items.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return e.name;
    case IconNameRole:
        // For .desktop launchers KFileItem reads their Icon= key.
        return e.item.iconName();
    case UrlRole:
        return e.item.url();
    case IsDirRole:
        return e.item.isDir();
    case MimeTypeRole:
        return e.item.mimetype();
    }
    return {};
}

QHash<int, QByteArray> FolderModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {IconNameRole, "iconName"},
        {UrlRole, "url"},
        {IsDirRole, "isDir"},
        {MimeTypeRole, "mimeType"},
    };
}

// The label under an icon. Launchers use their Name= (e.g. "Stardew Valley", not
// "Stardew Valley.desktop"), matching the Plasma desktop; everything else uses the
// file name.
QString FolderModel::displayName(const KFileItem &item)
{
    if (item.isDesktopFile() && item.isLocalFile()) {
        const KDesktopFile df(item.localPath());
        const QString name = df.readName();
        if (!name.isEmpty()) {
            return name;
        }
    }
    return item.text();
}

// Folders first, then natural name order: the same as Dolphin and the desktop.
bool FolderModel::lessThan(const Entry &a, const Entry &b) const
{
    if (a.item.isDir() != b.item.isDir()) {
        return a.item.isDir();
    }
    return m_collator.compare(a.name, b.name) < 0;
}

// upper_bound keeps equal-named items in arrival order, which keeps things stable.
int FolderModel::insertPosition(const Entry &entry) const
{
    const auto it = std::upper_bound(m_items.cbegin(), m_items.cend(), entry, [this](const Entry &a, const Entry &b) {
        return lessThan(a, b);
    });
    return int(it - m_items.cbegin());
}

bool FolderModel::isListedFolder(const KFileItem &item) const
{
    return item.url().adjusted(QUrl::StripTrailingSlash) == m_url.adjusted(QUrl::StripTrailingSlash);
}

// Linear scan: corrals hold tens of items, not thousands, so an index isn't worth it.
int FolderModel::indexOf(const QUrl &url) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).item.url() == url) {
            return i;
        }
    }
    return -1;
}

// One row signal per item (rather than a reset) so existing icon delegates, their
// selection and hover state survive a file being added.
void FolderModel::onNewItems(const KFileItemList &items)
{
    for (const KFileItem &item : items) {
        if (isListedFolder(item) || indexOf(item.url()) >= 0) {
            continue; // never show the corral's own folder ("."), never duplicate
        }
        const Entry e{item, displayName(item)};
        const int pos = insertPosition(e);
        beginInsertRows({}, pos, pos);
        m_items.insert(pos, e);
        endInsertRows();
    }
    Q_EMIT countChanged();
}

void FolderModel::onItemsDeleted(const KFileItemList &items)
{
    for (const KFileItem &item : items) {
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items.at(i).item.url() == item.url()) {
                beginRemoveRows({}, i, i);
                m_items.removeAt(i);
                endRemoveRows();
                break;
            }
        }
    }
    Q_EMIT countChanged();
}

void FolderModel::onRefreshItems(const QList<QPair<KFileItem, KFileItem>> &items)
{
    // Careful: KCoreDirLister also reports the listed folder *itself* here (as ".")
    // whenever something broadcasts a change for it, e.g. the Plasma desktop after
    // you drag an icon out of a corral. Treating every pair as remove + insert would
    // add that "." as an icon (a bug we hit), so only touch items we already hold.
    // Renames can change sort position; a remove + sorted insert keeps that correct.
    KFileItemList removed;
    KFileItemList added;
    for (const auto &pair : items) {
        if (indexOf(pair.first.url()) < 0) {
            continue;
        }
        removed << pair.first;
        added << pair.second;
    }
    if (removed.isEmpty()) {
        return;
    }
    onItemsDeleted(removed);
    onNewItems(added);
}

void FolderModel::onClear()
{
    if (m_items.isEmpty()) {
        return;
    }
    beginResetModel();
    m_items.clear();
    endResetModel();
    Q_EMIT countChanged();
}
