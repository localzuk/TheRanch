// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// See pagemodel.h: one PageModel per page of a corral, all over the same FolderModel.

#include "pagemodel.h"

#include <QTimer>

PageModel::PageModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

void PageModel::setPage(int page)
{
    if (m_page == page) {
        return;
    }
    // begin/endFilterChange is the Qt ≥ 6.10 replacement for invalidateFilter().
    beginFilterChange();
    m_page = page;
    endFilterChange(Direction::Rows);
    Q_EMIT pageChanged();
}

void PageModel::setPageSize(int size)
{
    size = std::max(1, size);
    if (m_pageSize == size) {
        return;
    }
    beginFilterChange();
    m_pageSize = size;
    endFilterChange(Direction::Rows);
    Q_EMIT pageSizeChanged();
}

void PageModel::setSourceModel(QAbstractItemModel *model)
{
    for (const auto &c : std::as_const(m_sourceConnections)) {
        disconnect(c);
    }
    m_sourceConnections.clear();

    QSortFilterProxyModel::setSourceModel(model);

    if (model) {
        // Inserting/removing a row shifts every later row across page boundaries,
        // which QSortFilterProxyModel can't know from filterAcceptsRow alone.
        m_sourceConnections << connect(model, &QAbstractItemModel::rowsInserted, this, &PageModel::refilterLater)
                            << connect(model, &QAbstractItemModel::rowsRemoved, this, &PageModel::refilterLater)
                            << connect(model, &QAbstractItemModel::rowsMoved, this, &PageModel::refilterLater)
                            << connect(model, &QAbstractItemModel::layoutChanged, this, &PageModel::refilterLater)
                            << connect(model, &QAbstractItemModel::modelReset, this, &PageModel::refilterLater);
    }
}

void PageModel::refilterLater()
{
    if (m_refilterQueued) {
        return;
    }
    m_refilterQueued = true;
    // Zero-delay timer = "after the current batch of model signals has been delivered".
    QTimer::singleShot(0, this, [this] {
        m_refilterQueued = false;
        beginFilterChange();
        endFilterChange(Direction::Rows);
    });
}

bool PageModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    Q_UNUSED(sourceParent)
    const int first = m_page * m_pageSize;
    return sourceRow >= first && sourceRow < first + m_pageSize;
}
