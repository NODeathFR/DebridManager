#include "ui/DownloadFilterProxy.h"

#include "core/DownloadModel.h"

DownloadFilterProxy::DownloadFilterProxy(DownloadModel* source, QObject* parent)
    : QSortFilterProxyModel(parent), m_model(source)
{
    setSourceModel(source);
    setDynamicSortFilter(true);   // un changement d'état refiltre la ligne
    // Un torrent ajouté ou retiré change l'affichage de ses fichiers
    auto refresh = [this] { changeFilter([] {}); };
    connect(source, &QAbstractItemModel::rowsInserted, this, refresh);
    connect(source, &QAbstractItemModel::rowsRemoved, this, refresh);
}

bool DownloadFilterProxy::matches(const DownloadItem& it, Filter f)
{
    const bool extracting = it.extract == ExtractState::Queued || it.extract == ExtractState::Running;
    switch (f) {
    case Filter::All:
        return true;
    case Filter::Active:
        return it.state == ItemState::Downloading || it.state == ItemState::Resolving
            || it.state == ItemState::TorrentProcessing || extracting;
    case Filter::Waiting:
        return it.state == ItemState::Queued || it.state == ItemState::WaitingRetry
            || it.state == ItemState::WaitingSelection || it.state == ItemState::Paused
            || it.extract == ExtractState::NeedPassword || it.extract == ExtractState::Pending;
    case Filter::Done:
        return it.state == ItemState::Completed && it.kind == ItemKind::File && !extracting;
    case Filter::Failed:
        return it.state == ItemState::Failed || it.state == ItemState::Canceled || it.extract == ExtractState::Failed;
    case Filter::Torrents:
        return it.kind == ItemKind::Torrent || !it.parentId.isEmpty();
    }
    return true;
}

bool DownloadFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex&) const
{
    if (sourceRow < 0 || sourceRow >= m_model->items().size())
        return false;
    const DownloadItem& it = m_model->items().at(sourceRow);
    if (!matches(it, m_filter))
        return false;
    if (!m_search.isEmpty() && !it.name.contains(m_search, Qt::CaseInsensitive))
        return false;
    if (grouped() && !it.parentId.isEmpty() && m_collapsed.contains(it.parentId) && m_model->rowOf(it.parentId) >= 0)
        return false;
    return true;
}

QVariant DownloadFilterProxy::data(const QModelIndex& index, int role) const
{
    if (role == DownloadModel::IndentRole || role == DownloadModel::ExpanderRole) {
        if (!grouped() || index.column() != DownloadModel::ColName)
            return 0;
        const int row = mapToSource(index).row();
        if (row < 0 || row >= m_model->items().size())
            return 0;
        const DownloadItem& it = m_model->items().at(row);
        if (role == DownloadModel::IndentRole)
            return !it.parentId.isEmpty() && m_model->rowOf(it.parentId) >= 0 ? 1 : 0;
        // 0 : rien à replier, 1 : déplié, 2 : replié
        if (it.kind != ItemKind::Torrent || m_model->childCount(it.id) == 0)
            return 0;
        return m_collapsed.contains(it.id) ? 2 : 1;
    }
    return QSortFilterProxyModel::data(index, role);
}

void DownloadFilterProxy::setFilter(Filter f)
{
    if (f == m_filter)
        return;
    changeFilter([&] { m_filter = f; });
}

void DownloadFilterProxy::setSearch(const QString& text)
{
    const QString t = text.trimmed();
    if (t == m_search)
        return;
    changeFilter([&] { m_search = t; });
}

void DownloadFilterProxy::toggleExpanded(const QString& torrentId)
{
    changeFilter([&] {
        if (!m_collapsed.remove(torrentId))
            m_collapsed.insert(torrentId);
    });
}

void DownloadFilterProxy::changeFilter(const std::function<void()>& change)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    change();
    endFilterChange(Direction::Rows);
#else
    change();
    invalidateFilter();
#endif
}
