#include "net/RealDebridApi.h"

#include "core/Utils.h"

#include <QJsonArray>
#include <QJsonObject>

// Adresse de l'API (remplaçable par DM_TEST_RD_BASE pour les tests hors-ligne).
static QString apiBase()
{
    static const QString base = qEnvironmentVariable("DM_TEST_RD_BASE", QStringLiteral("https://api.real-debrid.com/rest/1.0"));
    return base;
}

QNetworkRequest RealDebridApi::req(const QString& path, int timeoutMs) const
{
    QNetworkRequest r = Http::makeRequest(QUrl(apiBase() + path), timeoutMs);
    r.setRawHeader("Authorization", "Bearer " + m_key.toUtf8());
    return r;
}

QString RealDebridApi::errorFrom(const Http::Response& r)
{
    if (!r.netError.isEmpty())
        return QStringLiteral("Erreur réseau : %1").arg(r.netError);

    // Traduction des codes d'erreur RealDebrid les plus fréquents
    static const QHash<int, QString> fr = {
        {-1, QStringLiteral("Erreur interne RealDebrid")},
        {5, QStringLiteral("Trop de requêtes, ralentissez")},
        {7, QStringLiteral("Ressource introuvable")},
        {8, QStringLiteral("Clé API invalide ou expirée")},
        {9, QStringLiteral("Accès refusé (compte premium requis ?)")},
        {14, QStringLiteral("Compte verrouillé")},
        {16, QStringLiteral("Hébergeur non supporté")},
        {17, QStringLiteral("Hébergeur en maintenance")},
        {18, QStringLiteral("Limite de l'hébergeur atteinte")},
        {19, QStringLiteral("Hébergeur temporairement indisponible")},
        {20, QStringLiteral("Hébergeur réservé aux comptes premium")},
        {21, QStringLiteral("Trop de téléchargements actifs")},
        {22, QStringLiteral("Adresse IP non autorisée (VPN ?)")},
        {23, QStringLiteral("Quota de trafic épuisé")},
        {24, QStringLiteral("Fichier indisponible (supprimé de l'hébergeur ?)")},
        {25, QStringLiteral("Service indisponible")},
        {29, QStringLiteral("Torrent trop volumineux")},
        {30, QStringLiteral("Fichier torrent invalide")},
        {33, QStringLiteral("Torrent déjà actif")},
        {34, QStringLiteral("Trop de requêtes, ralentissez")},
        {35, QStringLiteral("Fichier bloqué (contenu signalé)")},
        {36, QStringLiteral("Limite d'utilisation équitable atteinte")},
    };
    const QJsonObject o = r.json.object();
    const int code = o.value(QStringLiteral("error_code")).toInt(-999);
    if (fr.contains(code))
        return QStringLiteral("RealDebrid : %1").arg(fr.value(code));
    const QString msg = o.value(QStringLiteral("error")).toString();
    if (!msg.isEmpty())
        return QStringLiteral("RealDebrid : %1 [%2]").arg(msg).arg(code);
    return QStringLiteral("RealDebrid : erreur HTTP %1").arg(r.status);
}

bool RealDebridApi::isTransient(const Http::Response& r)
{
    if (!r.netError.isEmpty())
        return true;
    // Le code d'erreur RealDebrid prime sur le code HTTP (un fichier supprimé
    // peut arriver avec un HTTP 503). Passagers : erreur interne, trop de
    // requêtes, hébergeur momentanément indisponible, trop de téléchargements
    // actifs, service indisponible.
    const QJsonValue code = r.json.object().value(QStringLiteral("error_code"));
    if (code.isDouble()) {
        static const QList<int> codes = {-1, 5, 19, 21, 25, 34};
        return codes.contains(code.toInt());
    }
    return r.status >= 500 || r.status == 429 || r.status == 408;
}

TorrentState RealDebridApi::mapStatus(const QString& s)
{
    if (s == QLatin1String("magnet_conversion")) return TorrentState::Converting;
    if (s == QLatin1String("waiting_files_selection")) return TorrentState::WaitingSelection;
    if (s == QLatin1String("queued")) return TorrentState::Queued;
    if (s == QLatin1String("downloaded")) return TorrentState::Ready;
    if (s == QLatin1String("magnet_error") || s == QLatin1String("error") || s == QLatin1String("virus")
        || s == QLatin1String("dead"))
        return TorrentState::Error;
    return TorrentState::Downloading;   // downloading, compressing, uploading
}

QString RealDebridApi::statusLabel(const QString& s)
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("magnet_error"), QStringLiteral("Erreur du magnet")},
        {QStringLiteral("magnet_conversion"), QStringLiteral("Conversion du magnet")},
        {QStringLiteral("waiting_files_selection"), QStringLiteral("Sélection en attente")},
        {QStringLiteral("queued"), QStringLiteral("En file")},
        {QStringLiteral("downloading"), QStringLiteral("Téléchargement serveur")},
        {QStringLiteral("downloaded"), QStringLiteral("Prêt")},
        {QStringLiteral("error"), QStringLiteral("Erreur")},
        {QStringLiteral("virus"), QStringLiteral("Virus détecté")},
        {QStringLiteral("compressing"), QStringLiteral("Compression")},
        {QStringLiteral("uploading"), QStringLiteral("Envoi")},
        {QStringLiteral("dead"), QStringLiteral("Mort (plus de sources)")},
    };
    return labels.value(s, s);
}

void RealDebridApi::accountInfo(Cb<AccountInfo> cb)
{
    Http::send(req(QStringLiteral("/user"), 15000), "GET", {}, this, [cb](const Http::Response& r) {
        AccountInfo info;
        info.service = Service::RealDebrid;
        if (r.status != 200) {
            info.error = errorFrom(r);
            cb(info, info.error);
            return;
        }
        const QJsonObject o = r.json.object();
        info.username = o.value(QStringLiteral("username")).toString();
        info.premium = o.value(QStringLiteral("type")).toString() == QLatin1String("premium");
        info.daysLeft = int(o.value(QStringLiteral("premium")).toDouble() / 86400.0);
        cb(info, {});
    });
}

void RealDebridApi::unrestrict(const QString& link, Cb<DirectLink> cb)
{
    const QByteArray body = Http::formEncode({{QStringLiteral("link"), link}});
    Http::send(req(QStringLiteral("/unrestrict/link")), "POST", body, this, [this, cb, link](const Http::Response& r) {
        m_lastTransient = r.status != 200 && isTransient(r);
        if (r.status != 200) {
            cb({}, errorFrom(r));
            return;
        }
        const QJsonObject o = r.json.object();
        DirectLink d;
        d.filename = Utils::sanitizeFileName(o.value(QStringLiteral("filename")).toString());
        d.size = qint64(o.value(QStringLiteral("filesize")).toDouble());
        d.url = o.value(QStringLiteral("download")).toString();
        d.sourceLink = link;
        d.service = Service::RealDebrid;
        if (d.url.isEmpty()) {
            cb({}, QStringLiteral("RealDebrid : aucun lien direct renvoyé"));
            return;
        }
        cb(d, {});
    });
}

void RealDebridApi::addMagnet(const QString& magnet, Cb<QString> cb)
{
    const QByteArray body = Http::formEncode({{QStringLiteral("magnet"), magnet}});
    Http::send(req(QStringLiteral("/torrents/addMagnet")), "POST", body, this, [cb](const Http::Response& r) {
        if (r.status != 200 && r.status != 201) {
            cb({}, errorFrom(r));
            return;
        }
        cb(r.json.object().value(QStringLiteral("id")).toString(), {});
    });
}

void RealDebridApi::addTorrentFile(const QString&, const QByteArray& data, Cb<QString> cb)
{
    QNetworkRequest rq = req(QStringLiteral("/torrents/addTorrent"), 60000);
    rq.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-bittorrent"));
    Http::send(rq, "PUT", data, this, [cb](const Http::Response& r) {
        if (r.status != 200 && r.status != 201) {
            cb({}, errorFrom(r));
            return;
        }
        cb(r.json.object().value(QStringLiteral("id")).toString(), {});
    });
}

void RealDebridApi::torrentInfo(const QString& id, Cb<TorrentInfo> cb)
{
    Http::send(req(QStringLiteral("/torrents/info/") + id), "GET", {}, this, [this, cb, id](const Http::Response& r) {
        m_lastTransient = r.status != 200 && isTransient(r);
        if (r.status != 200) {
            cb({}, errorFrom(r));
            return;
        }
        const QJsonObject o = r.json.object();
        TorrentInfo t;
        t.id = id;
        t.name = o.value(QStringLiteral("filename")).toString();
        const QString status = o.value(QStringLiteral("status")).toString();
        t.state = mapStatus(status);
        t.statusText = statusLabel(status);
        t.progress = int(o.value(QStringLiteral("progress")).toDouble());
        t.size = qint64(o.value(QStringLiteral("bytes")).toDouble());

        const QJsonArray files = o.value(QStringLiteral("files")).toArray();
        const QJsonArray links = o.value(QStringLiteral("links")).toArray();
        for (const QJsonValue& v : links)
            t.rawLinks << v.toString();

        int selectedCount = 0;
        for (const QJsonValue& v : files) {
            const QJsonObject f = v.toObject();
            TorrentFile tf;
            tf.id = QString::number(f.value(QStringLiteral("id")).toInt());
            QString path = f.value(QStringLiteral("path")).toString();
            while (path.startsWith(QLatin1Char('/')))
                path.remove(0, 1);
            tf.path = path;
            tf.size = qint64(f.value(QStringLiteral("bytes")).toDouble());
            tf.selected = f.value(QStringLiteral("selected")).toInt() == 1;
            if (tf.selected)
                ++selectedCount;
            t.files << tf;
        }
        t.filesListed = !t.files.isEmpty();

        // RealDebrid renvoie 'links' dans l'ordre des fichiers sélectionnés.
        // Si le compte ne correspond pas (archive générée par RD), on ne
        // peut pas faire la correspondance fichier par fichier.
        if (t.state == TorrentState::Ready) {
            t.linksMatchFiles = (selectedCount == t.rawLinks.size());
            if (t.linksMatchFiles) {
                int li = 0;
                for (TorrentFile& tf : t.files)
                    if (tf.selected)
                        tf.link = t.rawLinks.value(li++);
            }
        }
        cb(t, {});
    });
}

void RealDebridApi::selectFiles(const QString& id, const QStringList& fileIds, VoidCb cb)
{
    const QByteArray body = Http::formEncode({{QStringLiteral("files"), fileIds.join(QLatin1Char(','))}});
    Http::send(req(QStringLiteral("/torrents/selectFiles/") + id), "POST", body, this, [cb](const Http::Response& r) {
        // 202 = action déjà faite : pas une erreur pour nous
        if (r.status == 200 || r.status == 202 || r.status == 204)
            cb({});
        else
            cb(errorFrom(r));
    });
}

void RealDebridApi::listTorrents(Cb<QList<LibraryEntry>> cb)
{
    Http::send(req(QStringLiteral("/torrents?limit=5000")), "GET", {}, this, [cb](const Http::Response& r) {
        if (r.status == 204) {   // aucun torrent
            cb({}, {});
            return;
        }
        if (r.status != 200) {
            cb({}, errorFrom(r));
            return;
        }
        QList<LibraryEntry> out;
        for (const QJsonValue& v : r.json.array()) {
            const QJsonObject o = v.toObject();
            LibraryEntry e;
            e.id = o.value(QStringLiteral("id")).toString();
            e.service = Service::RealDebrid;
            e.name = o.value(QStringLiteral("filename")).toString();
            const QString status = o.value(QStringLiteral("status")).toString();
            e.state = mapStatus(status);
            e.statusText = statusLabel(status);
            e.progress = int(o.value(QStringLiteral("progress")).toDouble());
            e.size = qint64(o.value(QStringLiteral("bytes")).toDouble());
            e.added = QDateTime::fromString(o.value(QStringLiteral("added")).toString(), Qt::ISODateWithMs).toLocalTime();
            out << e;
        }
        cb(out, {});
    });
}

void RealDebridApi::deleteTorrent(const QString& id, VoidCb cb)
{
    Http::send(req(QStringLiteral("/torrents/delete/") + id), "DELETE", {}, this, [cb](const Http::Response& r) {
        if (r.status == 200 || r.status == 204)
            cb({});
        else
            cb(errorFrom(r));
    });
}
