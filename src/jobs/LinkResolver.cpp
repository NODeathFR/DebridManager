#include "jobs/LinkResolver.h"

#include "core/Config.h"
#include "core/Utils.h"
#include "net/DebridApi.h"
#include "net/HosterDetector.h"
#include "net/Http.h"

#include <QRegularExpression>

LinkResolver::LinkResolver(const QString& link, Service requested, QObject* parent)
    : QObject(parent), m_link(link), m_requested(requested)
{
}

void LinkResolver::start()
{
    const Config& cfg = Config::instance();
    const bool hasRd = !cfg.apiKey(Service::RealDebrid).isEmpty();
    const bool hasAd = !cfg.apiKey(Service::AllDebrid).isEmpty();
    // Par valeur : la détection de l'hébergeur peut répondre plus tard (liste
    // des hébergeurs pas encore téléchargée, au premier lien après le
    // démarrage) ; des références vers ces variables locales ne seraient alors
    // plus valables et donnaient « Aucun service disponible ».
    auto withKeys = [hasRd, hasAd](QList<Service> order) {
        QList<Service> out;
        for (Service s : order)
            if ((s == Service::RealDebrid && hasRd) || (s == Service::AllDebrid && hasAd))
                out << s;
        return out;
    };

    if (m_requested == Service::Direct) {
        resolveDirect();
        return;
    }
    if (m_requested == Service::RealDebrid || m_requested == Service::AllDebrid) {
        m_candidates = withKeys({m_requested});
        if (m_candidates.isEmpty()) {
            emit failed(QStringLiteral("Aucune clé API %1 configurée").arg(serviceLabel(m_requested)), false);
            return;
        }
        tryNext();
        return;
    }

    // Auto
    if (!hasRd && !hasAd) {
        resolveDirect();   // sans clé, on ne peut que télécharger en direct
        return;
    }
    emit status(QStringLiteral("Détection de l'hébergeur…"));
    HosterDetector::detect(m_link, cfg.apiKey(Service::AllDebrid), this,
                           [this, withKeys](HosterDetector::Result r) {
        switch (r) {
        case HosterDetector::Direct:
            resolveDirect();
            return;
        case HosterDetector::AllDebrid:
            m_candidates = withKeys({Service::AllDebrid, Service::RealDebrid});
            break;
        default:   // RealDebrid, Both, Unknown
            m_candidates = withKeys({Service::RealDebrid, Service::AllDebrid});
            break;
        }
        tryNext();
    });
}

void LinkResolver::tryNext()
{
    if (m_candidates.isEmpty()) {
        emit failed(m_errors.isEmpty() ? QStringLiteral("Aucun service disponible") : m_errors.join(QStringLiteral(" | ")),
                    m_transient);
        return;
    }
    const Service s = m_candidates.takeFirst();
    DebridApi* api = DebridApi::create(s, Config::instance().apiKey(s), this);
    emit status(QStringLiteral("Débridage via %1…").arg(serviceLabel(s)));
    api->unrestrict(m_link, [this, api](const DirectLink& d, const QString& err) {
        api->deleteLater();
        if (err.isEmpty()) {
            emit resolved(d);
            return;
        }
        m_errors << err;
        m_transient |= api->lastErrorTransient();
        if (!m_candidates.isEmpty())
            emit status(QStringLiteral("%1 — essai suivant…").arg(err));
        tryNext();
    });
}

void LinkResolver::resolveDirect()
{
    emit status(QStringLiteral("Vérification du lien direct…"));
    QNetworkRequest rq = Http::makeRequest(QUrl(m_link), 15000);
    Http::send(rq, "HEAD", {}, this, [this](const Http::Response& r) {
        DirectLink d;
        d.url = m_link;
        d.sourceLink = m_link;
        d.service = Service::Direct;
        d.filename = Utils::fileNameFromUrl(r.finalUrl.isValid() ? r.finalUrl.toString() : m_link);

        // Certains serveurs refusent HEAD (403/405) mais acceptent GET : on
        // n'échoue que si le fichier est clairement absent.
        if (r.status == 404 || r.status == 410) {
            emit failed(QStringLiteral("Lien direct inaccessible (HTTP %1)").arg(r.status), false);
            return;
        }
        // Content-Disposition : filename*=UTF-8''... prioritaire sur filename="..."
        const QString cd = QString::fromUtf8(r.headers.value("content-disposition"));
        static const QRegularExpression reStar(QStringLiteral("filename\\*\\s*=\\s*([^']*)'[^']*'([^;]+)"),
                                               QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression rePlain(QStringLiteral("filename\\s*=\\s*\"?([^\";]+)\"?"),
                                                QRegularExpression::CaseInsensitiveOption);
        auto m = reStar.match(cd);
        if (m.hasMatch()) {
            d.filename = Utils::sanitizeFileName(QUrl::fromPercentEncoding(m.captured(2).trimmed().toUtf8()));
        } else if ((m = rePlain.match(cd)).hasMatch()) {
            d.filename = Utils::sanitizeFileName(m.captured(1).trimmed());
        }
        d.size = r.headers.value("content-length").toLongLong();
        emit resolved(d);
    });
}
