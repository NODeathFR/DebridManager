#include "core/LibraryModel.h"

#include "core/Utils.h"
#include <QLocale>
#include "ui/Icons.h"
#include "ui/Theme.h"

LibraryModel::LibraryModel(QObject* parent) : QAbstractTableModel(parent) {}

int LibraryModel::rowCount(const QModelIndex& p) const { return p.isValid() ? 0 : m_entries.size(); }
int LibraryModel::columnCount(const QModelIndex& p) const { return p.isValid() ? 0 : ColumnCount; }

QVariant LibraryModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    static const char* labels[] = {"Nom", "Service", "Statut", "Serveur", "Taille", "Ajouté le"};
    return section >= 0 && section < ColumnCount ? QString::fromUtf8(labels[section]) : QVariant();
}

static int stateToItemColor(TorrentState s)
{
    // Réutilise les couleurs d'état de la file (voir Theme::stateColor)
    switch (s) {
    case TorrentState::Ready:     return 7;   // Completed
    case TorrentState::Error:     return 8;   // Failed
    case TorrentState::Downloading:
    case TorrentState::Converting: return 4;  // Downloading
    default:                       return 3;  // Queued
    }
}

QVariant LibraryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const LibraryEntry& e = m_entries.at(index.row());

    if (role == PercentRole)
        return e.progress;
    if (role == StateRole)
        return stateToItemColor(e.state);

    switch (index.column()) {
    case ColName:
        if (role == Qt::DisplayRole || role == Qt::ToolTipRole)
            return e.name;
        if (role == Qt::DecorationRole)
            return Icons::icon(QStringLiteral("magnet"));
        if (role == SortRole)
            return e.name.toLower();
        break;
    case ColService:
        if (role == Qt::DisplayRole || role == SortRole)
            return serviceLabel(e.service);
        if (role == Qt::ForegroundRole)
            return Theme::serviceColor(e.service);
        break;
    case ColStatus:
        if (role == Qt::DisplayRole || role == SortRole)
            return e.statusText;
        if (role == Qt::ForegroundRole)
            return Theme::stateColor(stateToItemColor(e.state));
        break;
    case ColProgress:
        if (role == Qt::DisplayRole)
            return QStringLiteral("%1 %").arg(e.progress);
        if (role == SortRole)
            return e.progress;
        break;
    case ColSize:
        if (role == Qt::DisplayRole)
            return Utils::humanSize(e.size);
        if (role == SortRole)
            return e.size;
        if (role == Qt::TextAlignmentRole)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        break;
    case ColAdded:
        if (role == Qt::DisplayRole)
            return QLocale().toString(e.added, QStringLiteral("dd/MM/yyyy HH:mm"));
        if (role == SortRole)
            return e.added;
        break;
    }
    return {};
}

void LibraryModel::setEntries(const QVector<LibraryEntry>& entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

void LibraryModel::removeEntry(Service s, const QString& id)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).service == s && m_entries.at(i).id == id) {
            beginRemoveRows({}, i, i);
            m_entries.removeAt(i);
            endRemoveRows();
            return;
        }
    }
}

void LibraryFilterProxy::setText(const QString& t)
{
    m_text = t.trimmed();
    invalidateFilter();
}

void LibraryFilterProxy::setService(Service s)
{
    m_service = s;
    invalidateFilter();
}

bool LibraryFilterProxy::filterAcceptsRow(int row, const QModelIndex&) const
{
    auto* m = static_cast<LibraryModel*>(sourceModel());
    const LibraryEntry& e = m->entry(row);
    if (m_service != Service::Auto && e.service != m_service)
        return false;
    if (m_text.isEmpty())
        return true;
    // Tous les mots doivent apparaître (ordre libre) : "s01 1080p"
    const QStringList words = m_text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& w : words)
        if (!e.name.contains(w, Qt::CaseInsensitive))
            return false;
    return true;
}
