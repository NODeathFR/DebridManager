#pragma once
// Filtre de la file affichée : pastilles (Tous, En cours…), recherche par
// nom, et groupes de torrent repliables (fichiers décalés sous leur torrent).
// L'ordre des lignes reste celui du modèle (= priorité de la file).

#include <QSet>
#include <QSortFilterProxyModel>
#include <functional>

class DownloadModel;
struct DownloadItem;

class DownloadFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    enum class Filter { All, Active, Waiting, Done, Failed, Torrents };

    explicit DownloadFilterProxy(DownloadModel* source, QObject* parent = nullptr);

    void setFilter(Filter f);
    Filter filter() const { return m_filter; }
    void setSearch(const QString& text);
    void toggleExpanded(const QString& torrentId);

    static bool matches(const DownloadItem& it, Filter f);

    QVariant data(const QModelIndex& index, int role) const override;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    void changeFilter(const std::function<void()>& change);   // modifie les critères puis refiltre
    bool grouped() const { return m_filter == Filter::All && m_search.isEmpty(); }

    DownloadModel* m_model;
    Filter m_filter = Filter::All;
    QString m_search;
    QSet<QString> m_collapsed;
};
