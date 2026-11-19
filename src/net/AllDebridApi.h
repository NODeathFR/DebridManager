#pragma once
#include "net/DebridApi.h"
#include "net/Http.h"

class QJsonArray;

class AllDebridApi : public DebridApi
{
    Q_OBJECT
public:
    AllDebridApi(const QString& key, QObject* parent) : DebridApi(key, parent) {}

    Service service() const override { return Service::AllDebrid; }
    void accountInfo(Cb<AccountInfo> cb) override;
    void unrestrict(const QString& link, Cb<DirectLink> cb) override;
    void addMagnet(const QString& magnet, Cb<QString> cb) override;
    void addTorrentFile(const QString& fileName, const QByteArray& data, Cb<QString> cb) override;
    void torrentInfo(const QString& id, Cb<TorrentInfo> cb) override;
    void selectFiles(const QString& id, const QStringList& fileIds, VoidCb cb) override;
    void listTorrents(Cb<QList<LibraryEntry>> cb) override;
    void deleteTorrent(const QString& id, VoidCb cb) override;

    static constexpr const char* kAgent = "DebridManager";
    static TorrentState mapStatus(int code);
    static QString statusLabel(int code, const QString& fallback);

private:
    QNetworkRequest req(const QString& path, const QString& base = {}, int timeoutMs = 30000) const;
    static QString errorFrom(const Http::Response& r);
    static bool ok(const Http::Response& r);
    static bool isTransient(const Http::Response& r);
    void pollDelayed(int delayedId, const QString& link, DirectLink partial, int attempt, Cb<DirectLink> cb);
    static void flatten(const QJsonArray& nodes, const QString& prefix, QList<TorrentFile>& out);
};
