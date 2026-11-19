#include "net/Http.h"

#include <QCoreApplication>
#include <QHttpMultiPart>
#include <QNetworkAccessManager>

namespace Http {

QNetworkAccessManager* nam()
{
    static QNetworkAccessManager* instance = [] {
        auto* m = new QNetworkAccessManager(QCoreApplication::instance());
        m->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
        return m;
    }();
    return instance;
}

QNetworkRequest makeRequest(const QUrl& url, int timeoutMs)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("DebridManager/%1").arg(QStringLiteral(APP_VERSION)));
    if (timeoutMs > 0)
        req.setTransferTimeout(timeoutMs);
    return req;
}

static void attach(QNetworkReply* reply, QObject* context, Callback cb)
{
    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    QObject::connect(reply, &QNetworkReply::finished, context ? context : reply, [reply, cb = std::move(cb)] {
        Response r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.body = reply->readAll();
        r.finalUrl = reply->url();
        if (r.status == 0 && reply->error() != QNetworkReply::NoError)
            r.netError = reply->errorString();
        for (const auto& p : reply->rawHeaderPairs())
            r.headers.insert(p.first.toLower(), p.second);
        if (!r.body.isEmpty())
            r.json = QJsonDocument::fromJson(r.body);
        cb(r);
    });
}

void send(QNetworkRequest req, const QByteArray& verb, const QByteArray& body, QObject* context, Callback cb)
{
    if (!body.isEmpty() && !req.header(QNetworkRequest::ContentTypeHeader).isValid())
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply* reply = verb == "HEAD" ? nam()->head(req)
                         : verb == "GET" ? nam()->get(req)
                                         : nam()->sendCustomRequest(req, verb, body);
    attach(reply, context, std::move(cb));
}

void sendMultipart(QNetworkRequest req, QHttpMultiPart* multipart, QObject* context, Callback cb)
{
    QNetworkReply* reply = nam()->post(req, multipart);
    multipart->setParent(reply);
    attach(reply, context, std::move(cb));
}

QByteArray formEncode(const Fields& fields)
{
    QByteArray out;
    for (const auto& f : fields) {
        if (!out.isEmpty())
            out += '&';
        out += QUrl::toPercentEncoding(f.first) + '=' + QUrl::toPercentEncoding(f.second);
    }
    return out;
}

} // namespace Http
