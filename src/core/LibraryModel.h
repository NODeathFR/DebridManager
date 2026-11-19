#pragma once
// Bibliothèque : torrents déjà présents sur les comptes RD/AD.

#include "core/Types.h"

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QVector>

class LibraryModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColName, ColService, ColStatus, ColProgress, ColSize, ColAdded, ColumnCount };
    enum Role { SortRole = Qt::UserRole + 1, PercentRole, StateRole };

    explicit LibraryModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

    void setEntries(const QVector<LibraryEntry>& entries);
    void removeEntry(Service s, const QString& id);
    const LibraryEntry& entry(int row) const { return m_entries.at(row); }

private:
    QVector<LibraryEntry> m_entries;
};

class LibraryFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setText(const QString& t);
    void setService(Service s);   // Auto = tous

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

private:
    QString m_text;
    Service m_service = Service::Auto;
};
