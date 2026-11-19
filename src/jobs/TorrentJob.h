#pragma once
// Cycle de vie complet d'un torrent côté serveur :
//   ajout (magnet / .torrent) ou reprise d'un torrent de la bibliothèque
//   -> attente de la liste des fichiers -> sélection par l'utilisateur
//   -> attente du téléchargement serveur -> débridage des fichiers choisis.
// Mode Auto : bascule sur AllDebrid si RealDebrid échoue avant la sélection.

#include "core/Types.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

class DebridApi;

class TorrentJob : public QObject
{
    Q_OBJECT
public:
    struct Source {
        QString magnet;
        QString fileName;
        QByteArray fileData;
        QString existingId;            // torrent déjà présent sur le compte
        Service existingService = Service::Auto;   // compte qui le contient (Auto = celui demandé)
    };

    TorrentJob(const Source& src, Service requested, QObject* parent = nullptr);

    void start();
    void selectFiles(const QStringList& fileIds);
    Service service() const { return m_service; }
    QString torrentId() const { return m_torrentId; }

signals:
    void status(const QString& text, int progress);            // progress -1 = indéterminé
    void serviceChanged(Service service);
    void filesReady(const QList<TorrentFile>& files, const QString& torrentName);
    void resolved(const QList<DirectLink>& links, const QStringList& errors);
    void failed(const QString& message);

private:
    enum class Phase { Adding, Listing, WaitingUser, Selecting, ServerDownload, Unrestricting, Done };

    void startWith(Service s);
    void poll();
    void handleInfo(const TorrentInfo& info);
    void fail(const QString& message);
    void collectLinks();
    void unrestrictNext();

    Source m_src;
    Service m_requested;
    Service m_service = Service::RealDebrid;
    QPointer<DebridApi> m_api;
    QString m_torrentId;
    Phase m_phase = Phase::Adding;
    QTimer m_pollTimer;
    TorrentInfo m_last;
    QList<TorrentFile> m_files;
    QStringList m_selected;
    QList<TorrentFile> m_toUnrestrict;
    QList<DirectLink> m_results;
    QStringList m_errors;
    bool m_triedFallback = false;
    bool m_triedReAdd = false;     // torrent repris introuvable : renvoyé une fois
    int m_pollErrors = 0;          // erreurs passagères consécutives pendant le suivi
    int m_unrestrictRetries = 0;   // essais pour le fichier en cours de débridage
    int m_unrestrictTotal = 0;
};
