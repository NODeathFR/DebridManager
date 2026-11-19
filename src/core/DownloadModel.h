#pragma once
// Modèle de la file de téléchargement (vue en tableau). L'ordre des lignes
// est aussi l'ordre de priorité de la file d'attente.

#include "core/Types.h"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QVector>

enum class ItemKind { File, Torrent };

enum class ItemState {
    Resolving,          // débridage du lien en cours
    TorrentProcessing,  // torrent en traitement côté serveur
    WaitingSelection,   // attend le choix des fichiers par l'utilisateur
    Queued,             // prêt, en attente d'une place
    Downloading,
    Paused,
    WaitingRetry,       // échec temporaire, nouvel essai programmé
    Completed,
    Failed,
    Canceled
};

// Extraction d'une archive (indépendante de l'état du téléchargement).
// Toutes les parties d'une même archive portent le même état.
enum class ExtractState {
    None,
    Pending,        // à extraire dès que toutes les parties seront terminées
    Queued,         // en attente de son tour
    Running,
    NeedPassword,   // mot de passe demandé à l'utilisateur
    Done,
    Failed,
    Canceled
};

struct DownloadItem {
    QString id;
    ItemKind kind = ItemKind::File;
    ItemState state = ItemState::Queued;
    Service requested = Service::Auto;
    Service service = Service::Auto;

    QString name;          // nom affiché
    QString relPath;       // chemin relatif voulu (peut contenir des dossiers)
    QString sourceLink;    // lien d'hébergeur / magnet / lien direct d'origine
    QString directUrl;     // lien direct à télécharger
    QString destPath;      // chemin final choisi (réservé dès le 1er lancement)

    qint64 size = 0;
    qint64 received = 0;
    double speed = 0;
    int serverProgress = -1;   // progression côté serveur (torrents)
    QString status;
    QString error;
    int attempts = 0;
    bool reresolved = false;
    QDateTime added;

    // Torrents
    QString parentId;      // ligne du torrent dont ce fichier est issu (non enregistré)
    QString torrentId;
    QString torrentFileName;
    QByteArray torrentData;

    // Extraction
    ExtractState extract = ExtractState::None;
    int extractPercent = -1;
    int extractEta = -1;   // secondes restantes estimées (-1 = inconnu), non enregistré
    QString extractStatus;
    QString extractDir;    // dossier où l'archive a été (ou est) extraite
    bool extractBusy() const { return extract == ExtractState::Queued || extract == ExtractState::Running
                                      || extract == ExtractState::NeedPassword; }

    bool isBusy() const;       // occupe un "travail" (réseau en cours ou en file)
    bool isFinished() const;   // terminé, échoué ou annulé
    int percent() const;       // -1 si inconnu

    QJsonObject toJson() const;
    static DownloadItem fromJson(const QJsonObject& o);
};

class DownloadModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColName, ColService, ColStatus, ColProgress, ColSpeed, ColEta, ColSize, ColumnCount };
    // ParentRole : id du torrent d'origine ; IndentRole / ExpanderRole sont
    // fournis par le filtre de la vue (DownloadFilterProxy).
    enum Role { StateRole = Qt::UserRole + 1, PercentRole, IdRole, ParentRole, IsTorrentRole, IndentRole, ExpanderRole };
    static constexpr int ExtractingState = 10;   // valeur de StateRole pendant une extraction

    explicit DownloadModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    const QVector<DownloadItem>& items() const { return m_items; }
    DownloadItem* find(const QString& id);       // pointeur valide jusqu'à la prochaine insertion/suppression
    int rowOf(const QString& id) const;
    QString idAt(int row) const;
    int childCount(const QString& id) const { return m_children.value(id); }   // fichiers issus de ce torrent

    void append(const DownloadItem& item);
    void insertAt(int row, const QList<DownloadItem>& items);
    void remove(const QStringList& ids);
    void moveToEdge(const QStringList& ids, bool top);
    void touch(const QString& id);               // rafraîchit la ligne

private:
    void rebuildIndex();

    QVector<DownloadItem> m_items;
    QHash<QString, int> m_index;
    QHash<QString, int> m_children;
};
