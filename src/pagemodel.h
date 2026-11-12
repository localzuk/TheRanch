// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PageModel — one page of a corral's icons.
//
// Pagination is done by giving each page of the pager its own PageModel over the
// same FolderModel, showing rows [page * pageSize, (page + 1) * pageSize). Because
// it's a proxy, renames, additions and removals in the folder flow through to the
// right page automatically, with no copying of data into QML.

#pragma once

#include <QSortFilterProxyModel>

class PageModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(int pageSize READ pageSize WRITE setPageSize NOTIFY pageSizeChanged)

public:
    explicit PageModel(QObject *parent = nullptr);

    int page() const { return m_page; }
    void setPage(int page);
    int pageSize() const { return m_pageSize; }
    void setPageSize(int size); // clamped to >= 1

    // Also hooks the source's row signals; see refilterLater().
    void setSourceModel(QAbstractItemModel *model) override;

Q_SIGNALS:
    void pageChanged();
    void pageSizeChanged();

protected:
    // Accepts a row purely by its index, so it depends on the source's ordering.
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    // Inserting or removing one source row shifts every later row across page
    // boundaries, which QSortFilterProxyModel can't infer, so we re-run the filter.
    // Coalesced to once per event-loop pass because KIO often delivers rows in bursts.
    void refilterLater();

    int m_page = 0;
    int m_pageSize = 1;
    bool m_refilterQueued = false;
    QList<QMetaObject::Connection> m_sourceConnections;
};
