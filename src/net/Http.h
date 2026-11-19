#pragma once
// Petite surcouche asynchrone autour de QNetworkAccessManager pour les appels
// aux API (RealDebrid, AllDebrid…), dans la boucle d'événements de l'interface.
// Les téléchargements de fichiers ont leur propre fil : voir DownloadTask.

#include <QByteArray>
#include <QHash>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPair>
#include <QString>
#include <functional>

class QHttpMultiPart;
class QNetworkAccessManager;
class QObject;

namespace Http {

struct Response {
    int status = 0;                       // code HTTP (0 = pas de réponse)
    QByteArray body;
    QJsonDocument json;
    QString netError;                     // non vide si erreur réseau (pas de réponse HTTP)
    QHash<QByteArray, QByteArray> headers;  // en minuscules
    QUrl finalUrl;
};

using Callback = std::function<void(const Response&)>;
using Fields = QList<QPair<QString, QString>>;

QNetworkAccessManager* nam();

QNetworkRequest makeRequest(const QUrl& url, int timeoutMs = 30000);

// Le callback est appelé dans le thread principal, uniquement si `context`
// existe encore (sinon la réponse est simplement ignorée).
void send(QNetworkRequest req, const QByteArray& verb, const QByteArray& body,
          QObject* context, Callback cb);
void sendMultipart(QNetworkRequest req, QHttpMultiPart* multipart, QObject* context, Callback cb);

// Encodage application/x-www-form-urlencoded strict (les '+' et '&' des
// magnets sont correctement échappés, contrairement à QUrlQuery).
QByteArray formEncode(const Fields& fields);

} // namespace Http
