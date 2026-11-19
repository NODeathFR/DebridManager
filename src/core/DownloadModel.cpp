#include "core/DownloadModel.h"

#include "core/Utils.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QJsonValue>

// ---------------------------------------------------------------- Item

bool DownloadItem::isBusy() const
{
    switch (state) {
    case ItemState::Resolving:
    case ItemState::TorrentProcessing:
    case ItemState::WaitingSelection:
    case ItemState::Queued:
    case ItemState::Downloading:
    case ItemState::WaitingRetry:
        return true;
    default:
        return false;
    }
}

bool DownloadItem::isFinished() const
{
    return state == ItemState::Completed || state == ItemState::Failed || state == ItemState::Canceled;
}

int DownloadItem::percent() const
{
    if (kind == ItemKind::Torrent) {
        if (state == ItemState::Completed)
            return 100;
        return serverProgress;
    }
    if (state == ItemState::Completed)
        return 100;
    if (size <= 0)
        return -1;
    return int(qBound(qint64(0), received * 100 / size, qint64(100)));
}

static QString stateId(ItemState s) { return QString::number(int(s)); }

QJsonObject DownloadItem::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("state")] = stateId(state);
    o[QStringLiteral("requested")] = serviceId(requested);
    o[QStringLiteral("service")] = serviceId(service);
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("relPath")] = relPath;
    o[QStringLiteral("sourceLink")] = sourceLink;
    o[QStringLiteral("directUrl")] = directUrl;
    o[QStringLiteral("destPath")] = destPath;
    o[QStringLiteral("size")] = double(size);
    o[QStringLiteral("received")] = double(received);
    o[QStringLiteral("error")] = error;
    o[QStringLiteral("added")] = added.toString(Qt::ISODate);
    if (extract != ExtractState::None) {
        o[QStringLiteral("extract")] = int(extract);
        o[QStringLiteral("extractStatus")] = extractStatus;
        o[QStringLiteral("extractDir")] = extractDir;
    }
    return o;
}

DownloadItem DownloadItem::fromJson(const QJsonObject& o)
{
    DownloadItem it;
    it.id = o.value(QStringLiteral("id")).toString();
    it.kind = ItemKind::File;
    it.state = ItemState(o.value(QStringLiteral("state")).toString().toInt());
    it.requested = serviceFromId(o.value(QStringLiteral("requested")).toString());
    it.service = serviceFromId(o.value(QStringLiteral("service")).toString());
    it.name = o.value(QStringLiteral("name")).toString();
    it.relPath = o.value(QStringLiteral("relPath")).toString();
    it.sourceLink = o.value(QStringLiteral("sourceLink")).toString();
    it.directUrl = o.value(QStringLiteral("directUrl")).toString();
    it.destPath = o.value(QStringLiteral("destPath")).toString();
    it.size = qint64(o.value(QStringLiteral("size")).toDouble());
    it.received = qint64(o.value(QStringLiteral("received")).toDouble());
    it.error = o.value(QStringLiteral("error")).toString();
    it.added = QDateTime::fromString(o.value(QStringLiteral("added")).toString(), Qt::ISODate);
    it.extract = ExtractState(qBound(0, o.value(QStringLiteral("extract")).toInt(), int(ExtractState::Canceled)));
    it.extractStatus = o.value(QStringLiteral("extractStatus")).toString();
    it.extractDir = o.value(QStringLiteral("extractDir")).toString();
    return it;
}

// ---------------------------------------------------------------- Modèle

DownloadModel::DownloadModel(QObject* parent) : QAbstractTableModel(parent) {}

int DownloadModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_items.size(); }
int DownloadModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : ColumnCount; }

Qt::ItemFlags DownloadModel::flags(const QModelIndex& index) const
{
    return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}

QVariant DownloadModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColName:     return QStringLiteral("Nom");
    case ColService:  return QStringLiteral("Service");
    case ColStatus:   return QStringLiteral("Statut");
    case ColProgress: return QStringLiteral("Progression");
    case ColSpeed:    return QStringLiteral("Vitesse");
    case ColEta:      return QStringLiteral("Restant");
    case ColSize:     return QStringLiteral("Taille");
    }
    return {};
}

static QString stateIcon(const DownloadItem& it)
{
    switch (it.state) {
    case ItemState::Completed:        return it.kind == ItemKind::Torrent ? QStringLiteral("magnet") : QStringLiteral("check");
    case ItemState::Failed:           return QStringLiteral("error");
    case ItemState::Canceled:         return QStringLiteral("stop");
    case ItemState::Paused:           return QStringLiteral("pause");
    case ItemState::Downloading:      return QStringLiteral("download");
    case ItemState::WaitingRetry:     return QStringLiteral("retry");
    case ItemState::WaitingSelection: return QStringLiteral("list");
    case ItemState::TorrentProcessing:return QStringLiteral("magnet");
    case ItemState::Resolving:        return QStringLiteral("link");
    case ItemState::Queued:           return QStringLiteral("clock");
    }
    return {};
}

QVariant DownloadModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const DownloadItem& it = m_items.at(index.row());

    const bool extracting = it.state == ItemState::Completed
        && (it.extract == ExtractState::Running || it.extract == ExtractState::Queued);
    if (role == StateRole)
        return extracting ? ExtractingState : int(it.state);
    if (role == PercentRole)
        return extracting ? qMax(0, it.extractPercent) : it.percent();
    const bool showExtract = it.state == ItemState::Completed && it.extract != ExtractState::None;
    if (role == IdRole)
        return it.id;
    if (role == ParentRole)
        return it.parentId;
    if (role == IsTorrentRole)
        return it.kind == ItemKind::Torrent;

    switch (index.column()) {
    case ColName:
        if (role == Qt::DisplayRole)
            return it.name;
        if (role == Qt::DecorationRole)
            return Icons::icon(it.kind == ItemKind::Torrent ? QStringLiteral("magnet") : Icons::forFile(it.name));
        if (role == Qt::FontRole && it.kind == ItemKind::Torrent) {
            QFont f;
            f.setBold(true);
            return f;
        }
        if (role == Qt::ToolTipRole) {
            QString tip = QStringLiteral("<b>%1</b>").arg(it.name.toHtmlEscaped());
            if (!it.destPath.isEmpty())
                tip += QStringLiteral("<br>%1").arg(QDir::toNativeSeparators(it.destPath).toHtmlEscaped());
            if (!it.sourceLink.isEmpty() && !it.sourceLink.startsWith(QLatin1String("magnet:")))
                tip += QStringLiteral("<br><i>%1</i>").arg(it.sourceLink.left(160).toHtmlEscaped());
            if (!it.error.isEmpty())
                tip += QStringLiteral("<br><span style='color:#ff8a80'>%1</span>").arg(it.error.toHtmlEscaped());
            if (it.extract == ExtractState::Done && !it.extractDir.isEmpty())
                tip += QStringLiteral("<br>Extrait dans : %1").arg(QDir::toNativeSeparators(it.extractDir).toHtmlEscaped());
            return tip;
        }
        break;
    case ColService:
        if (role == Qt::DisplayRole)
            return serviceLabel(it.service);
        if (role == Qt::ForegroundRole)
            return Theme::serviceColor(it.service);
        break;
    case ColStatus:
        if (role == Qt::DisplayRole)
            return showExtract ? it.extractStatus : it.status;
        if (role == Qt::DecorationRole)
            return Icons::icon(showExtract && it.extract != ExtractState::Pending ? QStringLiteral("archive") : stateIcon(it));
        if (role == Qt::ForegroundRole) {
            if (!showExtract)
                return Theme::stateColor(int(it.state));
            switch (it.extract) {
            case ExtractState::Done:         return Theme::stateColor(int(ItemState::Completed));
            case ExtractState::Failed:       return Theme::stateColor(int(ItemState::Failed));
            case ExtractState::Canceled:     return Theme::stateColor(int(ItemState::Canceled));
            case ExtractState::Pending:
            case ExtractState::NeedPassword: return Theme::stateColor(int(ItemState::WaitingSelection));
            default:                         return Theme::stateColor(ExtractingState);
            }
        }
        if (role == Qt::ToolTipRole) {
            if (showExtract)
                return it.extractStatus;
            if (!it.error.isEmpty())
                return it.error;
        }
        break;
    case ColProgress:
        if (role == Qt::DisplayRole) {
            const int p = extracting ? qMax(0, it.extractPercent) : it.percent();
            return p < 0 ? QString() : QStringLiteral("%1 %").arg(p);
        }
        break;
    case ColSpeed:
        if (role == Qt::DisplayRole && it.state == ItemState::Downloading)
            return Utils::humanSpeed(it.speed);
        if (role == Qt::TextAlignmentRole)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        break;
    case ColEta:
        if (role == Qt::DisplayRole && it.state == ItemState::Downloading && it.speed > 1 && it.size > it.received)
            return Utils::humanDuration(qint64(double(it.size - it.received) / it.speed));
        if (role == Qt::DisplayRole && it.state == ItemState::Completed && it.extract == ExtractState::Running
            && it.extractEta >= 0)
            return Utils::humanDuration(it.extractEta);
        if (role == Qt::ForegroundRole && it.state == ItemState::Completed && it.extract == ExtractState::Running)
            return Theme::stateColor(ExtractingState);
        if (role == Qt::ToolTipRole && it.state == ItemState::Completed && it.extract == ExtractState::Running
            && it.extractEta >= 0)
            return QStringLiteral("Temps restant estimé pour l'extraction");
        if (role == Qt::TextAlignmentRole)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        break;
    case ColSize:
        if (role == Qt::DisplayRole)
            return it.size > 0 ? Utils::humanSize(it.size) : QString();
        if (role == Qt::TextAlignmentRole)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        break;
    }
    return {};
}

DownloadItem* DownloadModel::find(const QString& id)
{
    const int row = m_index.value(id, -1);
    return row < 0 ? nullptr : &m_items[row];
}

int DownloadModel::rowOf(const QString& id) const { return m_index.value(id, -1); }

QString DownloadModel::idAt(int row) const
{
    return row >= 0 && row < m_items.size() ? m_items.at(row).id : QString();
}

void DownloadModel::append(const DownloadItem& item)
{
    insertAt(m_items.size(), {item});
}

void DownloadModel::insertAt(int row, const QList<DownloadItem>& items)
{
    if (items.isEmpty())
        return;
    row = qBound(0, row, int(m_items.size()));
    beginInsertRows({}, row, row + items.size() - 1);
    for (int i = 0; i < items.size(); ++i)
        m_items.insert(row + i, items.at(i));
    rebuildIndex();
    endInsertRows();
}

void DownloadModel::remove(const QStringList& ids)
{
    QList<int> rows;
    for (const QString& id : ids) {
        const int r = rowOf(id);
        if (r >= 0)
            rows << r;
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : rows) {
        beginRemoveRows({}, r, r);
        m_items.removeAt(r);
        endRemoveRows();
    }
    rebuildIndex();
}

void DownloadModel::moveToEdge(const QStringList& ids, bool top)
{
    if (ids.isEmpty())
        return;
    const QSet<QString> set(ids.begin(), ids.end());
    QVector<DownloadItem> moved, rest;
    for (const DownloadItem& it : m_items)
        (set.contains(it.id) ? moved : rest).append(it);
    beginResetModel();
    m_items = top ? moved + rest : rest + moved;
    rebuildIndex();
    endResetModel();
}

void DownloadModel::touch(const QString& id)
{
    const int r = rowOf(id);
    if (r >= 0)
        emit dataChanged(index(r, 0), index(r, ColumnCount - 1));
}

void DownloadModel::rebuildIndex()
{
    m_index.clear();
    m_children.clear();
    for (int i = 0; i < m_items.size(); ++i) {
        m_index.insert(m_items.at(i).id, i);
        if (!m_items.at(i).parentId.isEmpty())
            m_children[m_items.at(i).parentId]++;
    }
}
