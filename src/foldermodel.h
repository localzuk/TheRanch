// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// FolderModel — a flat, live, sorted listing of one folder (a corral's contents).
//
// Backed by KIO's KCoreDirLister, so it updates on its own as files are added,
// removed or renamed by anyone: The Ranch, Dolphin, the Plasma desktop. Sort order
// matches Dolphin: folders first, then natural, case-insensitive names.
//
// We use our own model rather than KDirModel because we need .desktop launchers to
// show their Name= (like the Plasma desktop does) and a flat list we can page.

#pragma once

#include <KFileItem>
#include <QAbstractListModel>
#include <QCollator>
#include <QUrl>

class KCoreDirLister;

class FolderModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QUrl url READ url WRITE setUrl NOTIFY urlChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    // QML role names: name, iconName, url, isDir, mimeType.
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IconNameRole,
        UrlRole,
        IsDirRole,
        MimeTypeRole,
    };

    explicit FolderModel(QObject *parent = nullptr);

    QUrl url() const { return m_url; }
    // Clears the model and starts listing `url` (asynchronously; rows arrive via KIO).
    void setUrl(const QUrl &url);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void urlChanged();
    void countChanged();

private:
    struct Entry {
        KFileItem item;
        QString name; // cached display name (.desktop files show Name=); reading it hits the disk
    };

    bool lessThan(const Entry &a, const Entry &b) const;
    // True for the KFileItem of the listed folder itself ("."), which KIO reports
    // alongside its contents in some signals and which must never become an icon.
    bool isListedFolder(const KFileItem &item) const;
    int indexOf(const QUrl &url) const;
    // Binary search for where `entry` belongs in the sorted list.
    int insertPosition(const Entry &entry) const;

    // KCoreDirLister signal handlers. Each keeps m_items sorted and emits the
    // fine-grained row signals so QML delegates aren't rebuilt unnecessarily.
    void onNewItems(const KFileItemList &items);
    void onItemsDeleted(const KFileItemList &items);
    void onRefreshItems(const QList<QPair<KFileItem, KFileItem>> &items);
    void onClear();

    static QString displayName(const KFileItem &item);

    QUrl m_url;
    KCoreDirLister *m_lister;
    QList<Entry> m_items; // always sorted by lessThan()
    QCollator m_collator; // natural ("file 2" < "file 10"), case-insensitive
};
