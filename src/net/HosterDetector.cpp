#include "net/HosterDetector.h"

#include "core/Utils.h"
#include "net/AllDebridApi.h"
#include "net/Http.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QUrlQuery>

namespace {

struct Cache {
    QSet<QString> domains;
    qint64 fetchedAt = 0;
    bool valid = false;
};

constexpr qint64 kTtlMs = 6LL * 3600 * 1000;

QHash<int, Cache>& caches()
{
    static QHash<int, Cache> c;
    return c;
}

QHash<int, QList<std::function<void()>>>& pending()
{
    static QHash<int, QList<std::function<void()>>> p;
    return p;
}

QString normalize(QString d)
{
    // NB : l'ancienne version Python utilisait lstrip("www."), qui retire
    // tous les caractères 'w' et '.' en tête ("webshare.cz" -> "ebshare.cz").
    d = d.trimmed().toLower();
    if (d.startsWith(QLatin1String("www.")))
        d.remove(0, 4);
    return d;
}

void addAll(QSet<QString>& set, const QJsonArray& arr)
{
    for (const QJsonValue& v : arr)
        set.insert(normalize(v.toString()));
}

// Un domaine correspond s'il est dans la liste ou si un de ses domaines
// parents l'est ("dl3.1fichier.com" -> "1fichier.com").
bool matches(const QSet<QString>& set, QString domain)
{
    while (!domain.isEmpty()) {
        if (set.contains(domain))
            return true;
        const int dot = domain.indexOf(QLatin1Char('.'));
        if (dot < 0 || domain.indexOf(QLatin1Char('.'), dot + 1) < 0)
            break;   // on s'arrête au domaine de second niveau
        domain = domain.mid(dot + 1);
    }
    return false;
}

void ensure(int which, const QString& adKey, std::function<void()> done)
{
    Cache& c = caches()[which];
    if (c.valid && QDateTime::currentMSecsSinceEpoch() - c.fetchedAt < kTtlMs) {
        done();
        return;
    }
    auto& waiters = pending()[which];
    waiters << std::move(done);
    if (waiters.size() > 1)
        return;   // une requête est déjà en vol

    auto finish = [which](QSet<QString> set, bool ok) {
        Cache& cache = caches()[which];
        if (ok) {
            cache.domains = std::move(set);
            cache.fetchedAt = QDateTime::currentMSecsSinceEpoch();
            cache.valid = true;
        }
        const auto list = pending().take(which);
        for (const auto& fn : list)
            fn();
    };

    if (which == int(HosterDetector::RealDebrid)) {
        // Adresses remplaçables pour les tests hors-ligne (DM_TEST_RD_BASE / DM_TEST_AD_BASE)
        const QString rdBase = qEnvironmentVariable("DM_TEST_RD_BASE", QStringLiteral("https://api.real-debrid.com/rest/1.0"));
        QNetworkRequest rq = Http::makeRequest(QUrl(rdBase + QStringLiteral("/hosts/domains")), 15000);
        Http::send(rq, "GET", {}, nullptr, [finish](const Http::Response& r) {
            QSet<QString> set;
            const bool ok = r.status == 200 && r.json.isArray();
            if (ok)
                addAll(set, r.json.array());
            finish(set, ok);
        });
    } else {
        QUrl url(qEnvironmentVariable("DM_TEST_AD_BASE", QStringLiteral("https://api.alldebrid.com/v4")) + QStringLiteral("/hosts/domains"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("agent"), QString::fromLatin1(AllDebridApi::kAgent));
        url.setQuery(q);
        QNetworkRequest rq = Http::makeRequest(url, 15000);
        if (!adKey.isEmpty())
            rq.setRawHeader("Authorization", "Bearer " + adKey.toUtf8());
        Http::send(rq, "GET", {}, nullptr, [finish](const Http::Response& r) {
            QSet<QString> set;
            const QJsonObject o = r.json.object();
            const bool ok = o.value(QStringLiteral("status")).toString() == QLatin1String("success");
            if (ok) {
                const QJsonObject d = o.value(QStringLiteral("data")).toObject();
                addAll(set, d.value(QStringLiteral("hosts")).toArray());
                addAll(set, d.value(QStringLiteral("streams")).toArray());
                addAll(set, d.value(QStringLiteral("redirectors")).toArray());
            }
            finish(set, ok);
        });
    }
}

} // namespace

void HosterDetector::detect(const QString& link, const QString& adKey, QObject* context, std::function<void(Result)> cb)
{
    const QString domain = Utils::domainOf(link);
    QPointer<QObject> guard(context);
    auto remaining = std::make_shared<int>(2);

    auto evaluate = [=] {
        if (--*remaining > 0 || !guard)
            return;
        const Cache& rd = caches()[RealDebrid];
        const Cache& ad = caches()[AllDebrid];
        const bool inRd = rd.valid && matches(rd.domains, domain);
        const bool inAd = ad.valid && matches(ad.domains, domain);
        if (inRd && inAd)
            cb(Both);
        else if (inRd)
            cb(RealDebrid);
        else if (inAd)
            cb(AllDebrid);
        else if (rd.valid && ad.valid)
            cb(Direct);
        else
            cb(Unknown);
    };
    ensure(RealDebrid, adKey, evaluate);
    ensure(AllDebrid, adKey, evaluate);
}
