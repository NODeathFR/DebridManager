#include "net/AllDebridApi.h"

#include "core/Utils.h"

#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QUrlQuery>

static const QString kBaseV4 = QStringLiteral("https://api.alldebrid.com/v4");
static const QString kBaseV41 = QStringLiteral("https://api.alldebrid.com/v4.1");

QNetworkRequest AllDebridApi::req(const QString& path, const QString& base, int timeoutMs) const
{
    QUrl url((base.isEmpty() ? kBaseV4 : base) + path);
    QUrlQuery q(url);
    q.addQueryItem(QStringLiteral("agent"), QString::fromLatin1(kAgent));
    url.setQuery(q);
    QNetworkRequest r = Http::makeRequest(url, timeoutMs);
    r.setRawHeader("Authorization", "Bearer " + m_key.toUtf8());
    return r;
}

bool AllDebridApi::ok(const Http::Response& r)
{
    return r.netError.isEmpty() && r.json.object().value(QStringLiteral("status")).toString() == QLatin1String("success");
}

QString AllDebridApi::errorFrom(const Http::Response& r)
{
    if (!r.netError.isEmpty())
        return QStringLiteral("Erreur réseau : %1").arg(r.netError);
    static const QHash<QString, QString> fr = {
        {QStringLiteral("AUTH_MISSING_APIKEY"), QStringLiteral("Clé API manquante")},
        {QStringLiteral("AUTH_BAD_APIKEY"), QStringLiteral("Clé API invalide")},
        {QStringLiteral("AUTH_BLOCKED"), QStringLiteral("Clé bloquée (vérifiez vos e-mails AllDebrid / géolocalisation)")},
        {QStringLiteral("AUTH_USER_BANNED"), QStringLiteral("Compte banni")},
        {QStringLiteral("LINK_HOST_NOT_SUPPORTED"), QStringLiteral("Hébergeur non supporté")},
        {QStringLiteral("LINK_DOWN"), QStringLiteral("Fichier indisponible (lien mort)")},
        {QStringLiteral("LINK_PASS_PROTECTED"), QStringLiteral("Lien protégé par mot de passe")},
        {QStringLiteral("LINK_HOST_UNAVAILABLE"), QStringLiteral("Hébergeur temporairement indisponible")},
        {QStringLiteral("LINK_TOO_MANY_DOWNLOADS"), QStringLiteral("Trop de téléchargements simultanés sur cet hébergeur")},
        {QStringLiteral("LINK_HOST_FULL"), QStringLiteral("Serveurs de l'hébergeur saturés")},
        {QStringLiteral("LINK_HOST_LIMIT_REACHED"), QStringLiteral("Limite quotidienne de l'hébergeur atteinte")},
        {QStringLiteral("LINK_ERROR"), QStringLiteral("Impossible de débrider ce lien")},
        {QStringLiteral("MUST_BE_PREMIUM"), QStringLiteral("Compte premium requis")},
        {QStringLiteral("FREE_TRIAL_LIMIT_REACHED"), QStringLiteral("Limite de l'essai gratuit atteinte")},
        {QStringLiteral("MAGNET_INVALID_URI"), QStringLiteral("Magnet invalide")},
        {QStringLiteral("MAGNET_TOO_MANY_ACTIVE"), QStringLiteral("Trop de torrents actifs sur le compte")},
        {QStringLiteral("MAGNET_INVALID_FILE"), QStringLiteral("Fichier torrent invalide")},
        {QStringLiteral("NO_SERVER"), QStringLiteral("Serveurs AllDebrid non autorisés (VPN/serveur dédié ?)")},
    };
    const QJsonObject err = r.json.object().value(QStringLiteral("error")).toObject();
    const QString code = err.value(QStringLiteral("code")).toString();
    if (fr.contains(code))
        return QStringLiteral("AllDebrid : %1").arg(fr.value(code));
    const QString msg = err.value(QStringLiteral("message")).toString();
    if (!msg.isEmpty())
        return QStringLiteral("AllDebrid : %1 [%2]").arg(msg, code);
    return QStringLiteral("AllDebrid : réponse invalide (HTTP %1)").arg(r.status);
}

bool AllDebridApi::isTransient(const Http::Response& r)
{
    if (!r.netError.isEmpty())
        return true;
    // Le code d'erreur AllDebrid prime sur le code HTTP.
    const QString code = r.json.object().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
    if (!code.isEmpty()) {
        static const QStringList codes = {QStringLiteral("LINK_HOST_UNAVAILABLE"), QStringLiteral("LINK_TOO_MANY_DOWNLOADS"),
                                          QStringLiteral("LINK_HOST_FULL")};
        return codes.contains(code);
    }
    return r.status >= 500 || r.status == 429 || r.status == 408;
}

TorrentState AllDebridApi::mapStatus(int code)
{
    if (code == 4) return TorrentState::Ready;
    if (code >= 5) return TorrentState::Error;
    if (code == 0) return TorrentState::Queued;
    return TorrentState::Downloading;
}

QString AllDebridApi::statusLabel(int code, const QString& fallback)
{
    static const QHash<int, QString> labels = {
        {0, QStringLiteral("En file")}, {1, QStringLiteral("Téléchargement serveur")},
        {2, QStringLiteral("Compression / déplacement")}, {3, QStringLiteral("Envoi")},
        {4, QStringLiteral("Prêt")}, {5, QStringLiteral("Échec d'envoi")},
        {6, QStringLiteral("Erreur interne (décompression)")}, {7, QStringLiteral("Non téléchargé en 20 min")},
        {8, QStringLiteral("Fichier trop volumineux")}, {9, QStringLiteral("Erreur interne")},
        {10, QStringLiteral("Téléchargement trop long (> 72 h)")}, {11, QStringLiteral("Supprimé côté hébergeur")},
        {14, QStringLiteral("Erreur de tracker")}, {15, QStringLiteral("Aucune source (pas de pairs)")},
    };
    return labels.value(code, fallback.isEmpty() ? QStringLiteral("Statut %1").arg(code) : fallback);
}

void AllDebridApi::accountInfo(Cb<AccountInfo> cb)
{
    Http::send(req(QStringLiteral("/user"), {}, 15000), "GET", {}, this, [cb](const Http::Response& r) {
        AccountInfo info;
        info.service = Service::AllDebrid;
        if (!ok(r)) {
            info.error = errorFrom(r);
            cb(info, info.error);
            return;
        }
        const QJsonObject u = r.json.object().value(QStringLiteral("data")).toObject().value(QStringLiteral("user")).toObject();
        info.username = u.value(QStringLiteral("username")).toString();
        info.premium = u.value(QStringLiteral("isPremium")).toBool();
        const double until = u.value(QStringLiteral("premiumUntil")).toDouble();
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        info.daysLeft = info.premium && until > now ? int((until - now) / 86400) : 0;
        cb(info, {});
    });
}

void AllDebridApi::unrestrict(const QString& link, Cb<DirectLink> cb)
{
    const QByteArray body = Http::formEncode({{QStringLiteral("link"), link}});
    Http::send(req(QStringLiteral("/link/unlock")), "POST", body, this, [this, cb, link](const Http::Response& r) {
        m_lastTransient = !ok(r) && isTransient(r);
        if (!ok(r)) {
            cb({}, errorFrom(r));
            return;
        }
        const QJsonObject d = r.json.object().value(QStringLiteral("data")).toObject();
        DirectLink out;
        out.filename = Utils::sanitizeFileName(d.value(QStringLiteral("filename")).toString());
        out.size = qint64(d.value(QStringLiteral("filesize")).toDouble());
        out.url = d.value(QStringLiteral("link")).toString();
        out.sourceLink = link;
        out.service = Service::AllDebrid;
        // Certains hébergeurs renvoient un lien "différé" à interroger
        // jusqu'à ce que le fichier soit prêt côté AllDebrid.
        const int delayed = d.value(QStringLiteral("delayed")).toInt(0);
        if (out.url.isEmpty() && delayed > 0) {
            pollDelayed(delayed, link, out, 0, cb);
            return;
        }
        if (out.url.isEmpty()) {
            cb({}, QStringLiteral("AllDebrid : aucun lien direct renvoyé"));
            return;
        }
        cb(out, {});
    });
}

void AllDebridApi::pollDelayed(int delayedId, const QString& link, DirectLink partial, int attempt, Cb<DirectLink> cb)
{
    if (attempt > 120) {   // ~10 minutes
        m_lastTransient = false;
        cb({}, QStringLiteral("AllDebrid : délai dépassé pour le lien différé"));
        return;
    }
    QTimer::singleShot(attempt == 0 ? 2000 : 5000, this, [=] {
        const QByteArray body = Http::formEncode({{QStringLiteral("id"), QString::number(delayedId)}});
        Http::send(req(QStringLiteral("/link/delayed")), "POST", body, this, [=](const Http::Response& r) {
            m_lastTransient = !ok(r) && isTransient(r);
            if (!ok(r)) {
                cb({}, errorFrom(r));
                return;
            }
            const QJsonObject d = r.json.object().value(QStringLiteral("data")).toObject();
            const int status = d.value(QStringLiteral("status")).toInt();
            if (status == 2) {
                DirectLink out = partial;
                out.url = d.value(QStringLiteral("link")).toString();
                cb(out, out.url.isEmpty() ? QStringLiteral("AllDebrid : lien différé vide") : QString());
            } else if (status == 3) {
                cb({}, QStringLiteral("AllDebrid : échec de la préparation du lien différé"));
            } else {
                pollDelayed(delayedId, link, partial, attempt + 1, cb);
            }
        });
    });
}

static QString firstEntryId(const QJsonArray& arr, QString* error)
{
    if (arr.isEmpty()) {
        *error = QStringLiteral("AllDebrid : aucune réponse");
        return {};
    }
    const QJsonObject e = arr.first().toObject();
    if (e.contains(QStringLiteral("error"))) {
        const QJsonObject err = e.value(QStringLiteral("error")).toObject();
        *error = QStringLiteral("AllDebrid : %1 [%2]").arg(err.value(QStringLiteral("message")).toString(),
                                                         err.value(QStringLiteral("code")).toString());
        return {};
    }
    return QString::number(e.value(QStringLiteral("id")).toVariant().toLongLong());
}

void AllDebridApi::addMagnet(const QString& magnet, Cb<QString> cb)
{
    const QByteArray body = Http::formEncode({{QStringLiteral("magnets[]"), magnet}});
    Http::send(req(QStringLiteral("/magnet/upload")), "POST", body, this, [cb](const Http::Response& r) {
        if (!ok(r)) {
            cb({}, errorFrom(r));
            return;
        }
        QString err;
        const QString id = firstEntryId(r.json.object().value(QStringLiteral("data")).toObject()
                                            .value(QStringLiteral("magnets")).toArray(), &err);
        cb(id, err);
    });
}

void AllDebridApi::addTorrentFile(const QString& fileName, const QByteArray& data, Cb<QString> cb)
{
    auto* mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-bittorrent"));
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"files[]\"; filename=\"%1\"").arg(QString(fileName).replace(QLatin1Char('"'), QLatin1Char('_'))));
    part.setBody(data);
    mp->append(part);
    Http::sendMultipart(req(QStringLiteral("/magnet/upload/file"), {}, 60000), mp, this, [cb](const Http::Response& r) {
        if (!ok(r)) {
            cb({}, errorFrom(r));
            return;
        }
        QString err;
        const QString id = firstEntryId(r.json.object().value(QStringLiteral("data")).toObject()
                                            .value(QStringLiteral("files")).toArray(), &err);
        cb(id, err);
    });
}

void AllDebridApi::flatten(const QJsonArray& nodes, const QString& prefix, QList<TorrentFile>& out)
{
    for (const QJsonValue& v : nodes) {
        const QJsonObject n = v.toObject();
        const QString name = n.value(QStringLiteral("n")).toString(QStringLiteral("fichier"));
        if (n.contains(QStringLiteral("e"))) {
            flatten(n.value(QStringLiteral("e")).toArray(), prefix + name + QLatin1Char('/'), out);
        } else if (n.contains(QStringLiteral("l"))) {
            TorrentFile f;
            f.link = n.value(QStringLiteral("l")).toString();
            f.id = f.link;
            f.path = prefix + name;
            f.size = qint64(n.value(QStringLiteral("s")).toDouble());
            f.selected = true;
            out << f;
        }
    }
}

void AllDebridApi::torrentInfo(const QString& id, Cb<TorrentInfo> cb)
{
    const QByteArray body = Http::formEncode({{QStringLiteral("id"), id}});
    Http::send(req(QStringLiteral("/magnet/status"), kBaseV41), "POST", body, this, [this, cb, id](const Http::Response& r) {
        m_lastTransient = !ok(r) && isTransient(r);
        if (!ok(r)) {
            cb({}, errorFrom(r));
            return;
        }
        const QJsonValue mv = r.json.object().value(QStringLiteral("data")).toObject().value(QStringLiteral("magnets"));
        QJsonObject m;
        if (mv.isArray()) {
            const QJsonArray a = mv.toArray();
            if (!a.isEmpty())
                m = a.first().toObject();
        } else {
            m = mv.toObject();
        }
        if (m.isEmpty()) {
            cb({}, QStringLiteral("AllDebrid : torrent introuvable"));
            return;
        }
        TorrentInfo t;
        t.id = id;
        t.name = m.value(QStringLiteral("filename")).toString();
        const int code = m.value(QStringLiteral("statusCode")).toInt();
        t.state = mapStatus(code);
        t.statusText = statusLabel(code, m.value(QStringLiteral("status")).toString());
        t.size = qint64(m.value(QStringLiteral("size")).toDouble());
        const double downloaded = m.value(QStringLiteral("downloaded")).toDouble();
        t.progress = t.size > 0 ? int(downloaded * 100.0 / double(t.size)) : 0;
        if (t.state != TorrentState::Ready) {
            cb(t, {});
            return;
        }
        // Prêt : la liste des fichiers (avec liens) est sur un endpoint séparé
        const QByteArray fbody = Http::formEncode({{QStringLiteral("id[]"), id}});
        Http::send(req(QStringLiteral("/magnet/files")), "POST", fbody, this, [this, cb, t](const Http::Response& fr) mutable {
            m_lastTransient = !ok(fr) && isTransient(fr);
            if (!ok(fr)) {
                cb({}, errorFrom(fr));
                return;
            }
            const QJsonArray mags = fr.json.object().value(QStringLiteral("data")).toObject()
                                        .value(QStringLiteral("magnets")).toArray();
            if (mags.isEmpty()) {
                cb({}, QStringLiteral("AllDebrid : aucun fichier trouvé pour ce torrent"));
                return;
            }
            flatten(mags.first().toObject().value(QStringLiteral("files")).toArray(), {}, t.files);
            t.filesListed = true;
            cb(t, {});
        });
    });
}

void AllDebridApi::selectFiles(const QString&, const QStringList&, VoidCb cb)
{
    // AllDebrid télécharge tout le torrent côté serveur : rien à sélectionner.
    QTimer::singleShot(0, this, [cb] { cb({}); });
}

void AllDebridApi::listTorrents(Cb<QList<LibraryEntry>> cb)
{
    Http::send(req(QStringLiteral("/magnet/status"), kBaseV41), "POST", {}, this, [cb](const Http::Response& r) {
        if (!ok(r)) {
            cb({}, errorFrom(r));
            return;
        }
        const QJsonValue mv = r.json.object().value(QStringLiteral("data")).toObject().value(QStringLiteral("magnets"));
        QJsonArray arr = mv.isArray() ? mv.toArray() : QJsonArray{mv};
        QList<LibraryEntry> out;
        for (const QJsonValue& v : arr) {
            const QJsonObject m = v.toObject();
            if (m.isEmpty())
                continue;
            LibraryEntry e;
            e.id = QString::number(m.value(QStringLiteral("id")).toVariant().toLongLong());
            e.service = Service::AllDebrid;
            e.name = m.value(QStringLiteral("filename")).toString();
            const int code = m.value(QStringLiteral("statusCode")).toInt();
            e.state = mapStatus(code);
            e.statusText = statusLabel(code, m.value(QStringLiteral("status")).toString());
            e.size = qint64(m.value(QStringLiteral("size")).toDouble());
            const double dl = m.value(QStringLiteral("downloaded")).toDouble();
            e.progress = e.state == TorrentState::Ready ? 100 : (e.size > 0 ? int(dl * 100.0 / double(e.size)) : 0);
            e.added = QDateTime::fromSecsSinceEpoch(qint64(m.value(QStringLiteral("uploadDate")).toDouble()));
            out << e;
        }
        cb(out, {});
    });
}

void AllDebridApi::deleteTorrent(const QString& id, VoidCb cb)
{
    const QByteArray body = Http::formEncode({{QStringLiteral("id"), id}});
    Http::send(req(QStringLiteral("/magnet/delete")), "POST", body, this, [cb](const Http::Response& r) {
        cb(ok(r) ? QString() : errorFrom(r));
    });
}
