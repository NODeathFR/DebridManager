#include "jobs/TorrentJob.h"

#include "core/Config.h"
#include "core/Utils.h"
#include "net/DebridApi.h"

#include <QTimer>

#include <algorithm>

TorrentJob::TorrentJob(const Source& src, Service requested, QObject* parent)
    : QObject(parent), m_src(src), m_requested(requested)
{
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &TorrentJob::poll);
}

void TorrentJob::start()
{
    const Config& cfg = Config::instance();
    Service s = m_requested;
    if (!m_src.existingId.isEmpty()
        && (m_src.existingService == Service::RealDebrid || m_src.existingService == Service::AllDebrid))
        s = m_src.existingService;   // reprise : le torrent est sur ce compte-là
    else if (s != Service::RealDebrid && s != Service::AllDebrid)
        s = !cfg.apiKey(Service::RealDebrid).isEmpty() ? Service::RealDebrid : Service::AllDebrid;
    startWith(s);
}

void TorrentJob::startWith(Service s)
{
    const QString key = Config::instance().apiKey(s);
    if (key.isEmpty()) {
        fail(QStringLiteral("Aucune clé API %1 configurée").arg(serviceLabel(s)));
        return;
    }
    if (m_api)
        m_api->deleteLater();
    m_service = s;
    m_api = DebridApi::create(s, key, this);
    emit serviceChanged(s);

    if (!m_src.existingId.isEmpty()) {
        m_torrentId = m_src.existingId;
        m_phase = Phase::Listing;
        emit status(QStringLiteral("Récupération de la liste des fichiers…"), -1);
        poll();
        return;
    }

    m_phase = Phase::Adding;
    emit status(QStringLiteral("Envoi du torrent à %1…").arg(serviceLabel(s)), -1);
    auto onAdded = [this](const QString& id, const QString& err) {
        if (!err.isEmpty() || id.isEmpty()) {
            fail(err.isEmpty() ? QStringLiteral("Identifiant de torrent manquant") : err);
            return;
        }
        m_torrentId = id;
        m_phase = Phase::Listing;
        poll();
    };
    if (!m_src.magnet.isEmpty())
        m_api->addMagnet(m_src.magnet, onAdded);
    else
        m_api->addTorrentFile(m_src.fileName, m_src.fileData, onAdded);
}

void TorrentJob::poll()
{
    if (!m_api || m_phase == Phase::Done)
        return;
    DebridApi* api = m_api;
    m_api->torrentInfo(m_torrentId, [this, api](const TorrentInfo& info, const QString& err) {
        if (!err.isEmpty()) {
            // Coupure réseau ou serveur momentanément indisponible pendant un
            // suivi qui peut durer des heures : on patiente (≈ 15 min au plus).
            if (api->lastErrorTransient() && m_pollErrors < 30) {
                ++m_pollErrors;
                const int delay = qMin(30, 5 * m_pollErrors);
                emit status(QStringLiteral("Serveur injoignable (%1) — nouvel essai dans %2 s").arg(err).arg(delay), -1);
                m_pollTimer.start(delay * 1000);
                return;
            }
            fail(err);
            return;
        }
        m_pollErrors = 0;
        handleInfo(info);
    });
}

void TorrentJob::handleInfo(const TorrentInfo& info)
{
    m_last = info;
    if (info.state == TorrentState::Error) {
        fail(QStringLiteral("Torrent en erreur : %1").arg(info.statusText));
        return;
    }

    if (m_phase == Phase::Listing) {
        // RD : fichiers connus dès la fin de la conversion du magnet.
        // AD : fichiers connus seulement une fois le torrent prêt.
        if (info.filesListed && !info.files.isEmpty() && info.state != TorrentState::Converting) {
            m_files = info.files;
            std::sort(m_files.begin(), m_files.end(),
                      [](const TorrentFile& a, const TorrentFile& b) { return Utils::naturalLess(a.path, b.path); });
            m_phase = Phase::WaitingUser;
            emit filesReady(m_files, info.name);
            return;
        }
        if (info.state == TorrentState::Ready && info.files.isEmpty()) {
            fail(QStringLiteral("Torrent prêt mais aucun fichier renvoyé"));
            return;
        }
        const bool showPct = info.state == TorrentState::Downloading && info.progress > 0;
        emit status(QStringLiteral("Serveur : %1%2").arg(info.statusText,
                        showPct ? QStringLiteral(" (%1 %)").arg(info.progress) : QString()),
                    showPct ? info.progress : -1);
        m_pollTimer.start(info.state == TorrentState::Converting ? 2000 : 3000);
        return;
    }

    if (m_phase == Phase::ServerDownload) {
        if (info.state == TorrentState::Ready) {
            collectLinks();
            return;
        }
        emit status(QStringLiteral("Serveur : %1 (%2 %)").arg(info.statusText).arg(info.progress), info.progress);
        m_pollTimer.start(3000);
    }
}

void TorrentJob::selectFiles(const QStringList& fileIds)
{
    if (m_phase != Phase::WaitingUser)
        return;
    m_selected = fileIds;
    // On ne sélectionne côté RD que si ça n'a jamais été fait : un torrent
    // déjà traité (bibliothèque) refuse une seconde sélection.
    if (m_service == Service::RealDebrid && m_last.state == TorrentState::WaitingSelection) {
        m_phase = Phase::Selecting;
        emit status(QStringLiteral("Envoi de la sélection (%1 fichier(s))…").arg(fileIds.size()), -1);
        m_api->selectFiles(m_torrentId, fileIds, [this](const QString& err) {
            if (!err.isEmpty()) {
                fail(err);
                return;
            }
            m_phase = Phase::ServerDownload;
            m_pollTimer.start(1000);
        });
        return;
    }
    m_phase = Phase::ServerDownload;
    handleInfo(m_last);
}

void TorrentJob::collectLinks()
{
    m_toUnrestrict.clear();
    if (!m_last.linksMatchFiles) {
        // Archive générée par RealDebrid : on débride les liens tels quels.
        for (const QString& l : m_last.rawLinks) {
            TorrentFile f;
            f.link = l;
            m_toUnrestrict << f;
        }
    } else {
        QHash<QString, TorrentFile> byId;
        for (const TorrentFile& f : m_last.files)
            byId.insert(f.id, f);
        for (const QString& id : m_selected) {
            const TorrentFile f = byId.value(id);
            if (f.link.isEmpty()) {
                const QString name = f.path.isEmpty() ? id : f.path;
                m_errors << QStringLiteral("Lien introuvable pour « %1 » (fichier non sélectionné à l'origine sur ce torrent)").arg(name);
                continue;
            }
            m_toUnrestrict << f;
        }
    }
    if (m_toUnrestrict.isEmpty()) {
        fail(m_errors.isEmpty() ? QStringLiteral("Torrent terminé mais aucun lien renvoyé") : m_errors.join(QStringLiteral(" | ")));
        return;
    }
    m_phase = Phase::Unrestricting;
    m_unrestrictTotal = m_toUnrestrict.size();
    unrestrictNext();
}

void TorrentJob::unrestrictNext()
{
    if (m_toUnrestrict.isEmpty()) {
        m_phase = Phase::Done;
        if (m_results.isEmpty())
            emit failed(QStringLiteral("Impossible de débrider les fichiers : %1").arg(m_errors.join(QStringLiteral(" | "))));
        else
            emit resolved(m_results, m_errors);
        return;
    }
    const TorrentFile f = m_toUnrestrict.takeFirst();
    const int done = m_unrestrictTotal - m_toUnrestrict.size();
    emit status(QStringLiteral("Débridage des fichiers %1/%2…").arg(done).arg(m_unrestrictTotal),
                done * 100 / qMax(1, m_unrestrictTotal));
    DebridApi* api = m_api;
    m_api->unrestrict(f.link, [this, api, f](const DirectLink& d, const QString& err) {
        if (!err.isEmpty() && api->lastErrorTransient() && m_unrestrictRetries < 3) {
            ++m_unrestrictRetries;   // erreur passagère : même fichier un peu plus tard
            m_toUnrestrict.prepend(f);
            emit status(QStringLiteral("%1 — nouvel essai dans %2 s").arg(err).arg(5 * m_unrestrictRetries), -1);
            QTimer::singleShot(5000 * m_unrestrictRetries, this, &TorrentJob::unrestrictNext);
            return;
        }
        m_unrestrictRetries = 0;
        if (!err.isEmpty()) {
            m_errors << err;
        } else {
            DirectLink out = d;
            if (!f.path.isEmpty())
                out.filename = f.path;      // conserve l'arborescence du torrent
            if (out.size <= 0)
                out.size = f.size;
            out.sourceLink = f.link;
            out.service = m_service;
            m_results << out;
        }
        unrestrictNext();
    });
}

void TorrentJob::fail(const QString& message)
{
    m_pollTimer.stop();
    // Reprise d'un torrent déjà envoyé, mais qui n'est plus utilisable sur le
    // compte (supprimé, en erreur) : on renvoie le magnet / fichier d'origine.
    const bool canReAdd = !m_triedReAdd && m_phase == Phase::Listing && !m_src.existingId.isEmpty()
        && (!m_src.magnet.isEmpty() || !m_src.fileData.isEmpty());
    if (canReAdd) {
        m_triedReAdd = true;
        m_src.existingId.clear();
        m_torrentId.clear();
        emit status(QStringLiteral("Torrent inutilisable sur le compte (%1), nouvel envoi…").arg(message), -1);
        startWith(m_service);
        return;
    }
    const bool canFallback = m_requested == Service::Auto && !m_triedFallback && m_service == Service::RealDebrid
        && m_src.existingId.isEmpty() && (m_phase == Phase::Adding || m_phase == Phase::Listing)
        && !Config::instance().apiKey(Service::AllDebrid).isEmpty();
    if (canFallback) {
        m_triedFallback = true;
        m_errors << message;
        emit status(QStringLiteral("RealDebrid a échoué (%1), essai AllDebrid…").arg(message), -1);
        startWith(Service::AllDebrid);
        return;
    }
    m_phase = Phase::Done;
    emit failed(m_errors.isEmpty() || m_triedFallback == false ? message
                                                               : m_errors.join(QStringLiteral(" | ")) + QStringLiteral(" | ") + message);
}
