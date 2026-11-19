#pragma once
// Interface commune RealDebrid / AllDebrid. Toutes les méthodes sont
// asynchrones : le callback reçoit le résultat et un message d'erreur
// (vide en cas de succès).

#include "core/Types.h"

#include <QObject>
#include <functional>

class DebridApi : public QObject
{
    Q_OBJECT
public:
    template <class T>
    using Cb = std::function<void(const T& result, const QString& error)>;
    using VoidCb = std::function<void(const QString& error)>;

    DebridApi(const QString& apiKey, QObject* parent) : QObject(parent), m_key(apiKey) {}
    ~DebridApi() override = default;

    virtual Service service() const = 0;

    virtual void accountInfo(Cb<AccountInfo> cb) = 0;
    virtual void unrestrict(const QString& link, Cb<DirectLink> cb) = 0;
    virtual void addMagnet(const QString& magnet, Cb<QString> cb) = 0;               // -> id
    virtual void addTorrentFile(const QString& fileName, const QByteArray& data, Cb<QString> cb) = 0;
    virtual void torrentInfo(const QString& id, Cb<TorrentInfo> cb) = 0;
    virtual void selectFiles(const QString& id, const QStringList& fileIds, VoidCb cb) = 0;
    virtual void listTorrents(Cb<QList<LibraryEntry>> cb) = 0;
    virtual void deleteTorrent(const QString& id, VoidCb cb) = 0;

    // Fabrique : renvoie nullptr pour Auto/Direct.
    static DebridApi* create(Service s, const QString& apiKey, QObject* parent);

    // Après une erreur de unrestrict() ou torrentInfo() : vrai si elle est
    // passagère (réseau coupé, serveur surchargé ou momentanément
    // indisponible, trop de requêtes) et qu'un nouvel essai a donc un sens.
    bool lastErrorTransient() const { return m_lastTransient; }

protected:
    QString m_key;
    bool m_lastTransient = false;
};
