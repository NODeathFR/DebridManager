#pragma once
// Types de données partagés entre la couche réseau, la logique et l'interface.

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

enum class Service { Auto, RealDebrid, AllDebrid, Direct };

QString serviceId(Service s);                                   // "realdebrid"...
Service serviceFromId(const QString& id, Service fallback = Service::Auto);
QString serviceLabel(Service s);                                // "RealDebrid"...

// Lien direct prêt à être téléchargé.
struct DirectLink {
    QString filename;      // peut contenir des sous-dossiers relatifs ("Saison 1/Ep01.mkv")
    qint64 size = 0;
    QString url;           // lien direct
    QString sourceLink;    // lien d'hébergeur d'origine (re-débridage si le direct expire)
    Service service = Service::Direct;
};

// Fichier contenu dans un torrent côté serveur.
struct TorrentFile {
    QString id;            // RD : id numérique ; AD : lien intermédiaire
    QString path;          // chemin relatif dans le torrent
    qint64 size = 0;
    bool selected = false; // déjà sélectionné côté serveur
    QString link;          // lien d'hébergeur à débrider (une fois prêt)
};

enum class TorrentState { Converting, WaitingSelection, Queued, Downloading, Ready, Error };

struct TorrentInfo {
    QString id;
    QString name;
    TorrentState state = TorrentState::Queued;
    QString statusText;
    int progress = 0;
    qint64 size = 0;
    QList<TorrentFile> files;
    bool filesListed = false;
    // RD : quand le nombre de liens ne correspond pas aux fichiers sélectionnés
    // (archive RAR générée côté serveur), on garde les liens bruts.
    QStringList rawLinks;
    bool linksMatchFiles = true;
};

struct LibraryEntry {
    QString id;
    Service service = Service::RealDebrid;
    QString name;
    QString statusText;
    TorrentState state = TorrentState::Queued;
    int progress = 0;
    qint64 size = 0;
    QDateTime added;
};

struct AccountInfo {
    Service service = Service::RealDebrid;
    QString error;
    QString username;
    bool premium = false;
    int daysLeft = 0;
};

Q_DECLARE_METATYPE(DirectLink)
Q_DECLARE_METATYPE(TorrentFile)
