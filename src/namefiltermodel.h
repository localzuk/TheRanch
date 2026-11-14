// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NameFilterModel — a FolderModel narrowed down to the items whose name contains
// some text (case-insensitive). Used by the quick-open popup's type-to-filter box.

#pragma once

#include <QSortFilterProxyModel>
#include <QUrl>

class NameFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit NameFilterModel(QObject *parent = nullptr);

    QString filterText() const { return m_filterText; }
    void setFilterText(const QString &text);
    int count() const { return rowCount(); }

    // The url of row `row`, or an empty url if out of range (for Enter in QML).
    Q_INVOKABLE QUrl urlAt(int row) const;

Q_SIGNALS:
    void filterTextChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_filterText;
};
