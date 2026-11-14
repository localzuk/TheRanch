// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "namefiltermodel.h"
#include "foldermodel.h"

NameFilterModel::NameFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    const auto changed = [this] {
        Q_EMIT countChanged();
    };
    connect(this, &QAbstractItemModel::rowsInserted, this, changed);
    connect(this, &QAbstractItemModel::rowsRemoved, this, changed);
    connect(this, &QAbstractItemModel::modelReset, this, changed);
    connect(this, &QAbstractItemModel::layoutChanged, this, changed);
}

void NameFilterModel::setFilterText(const QString &text)
{
    if (m_filterText == text) {
        return;
    }
    beginFilterChange();
    m_filterText = text;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    Q_EMIT filterTextChanged();
    Q_EMIT countChanged();
}

QUrl NameFilterModel::urlAt(int row) const
{
    if (row < 0 || row >= rowCount()) {
        return {};
    }
    return index(row, 0).data(FolderModel::UrlRole).toUrl();
}

bool NameFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (m_filterText.isEmpty()) {
        return true;
    }
    const QString name = sourceModel()->index(sourceRow, 0, sourceParent).data(FolderModel::NameRole).toString();
    return name.contains(m_filterText, Qt::CaseInsensitive);
}
