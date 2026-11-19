#pragma once
#include "net/DebridApi.h"
#include "net/Http.h"

class RealDebridApi : public DebridApi
{
    Q_OBJECT
public:
    RealDebridApi(const QString& key, QObject* parent) : DebridApi(key, parent) {}

    Service service() const override { return Service::RealDebrid; }
    void accountInfo(Cb<AccountInfo> cb) override;
    void unrestrict(const QString& link, Cb<DirectLink> cb) override;
    void addMagnet(const QString& magnet, Cb<QString> cb) override;
    void addTorrentFile(const QString& fileName, const QByteArray& data, Cb<QString> cb) override;
    void torrentInfo(const QString& id, Cb<TorrentInfo> cb) override;
    void selectFiles(const QString& id, const QStringList& fileIds, VoidCb cb) override;
    void listTorrents(Cb<QList<LibraryEntry>> cb) override;
    void deleteTorrent(const QString& id, VoidCb cb) override;

    static TorrentState mapStatus(const QString& status);
    static QString statusLabel(const QString& status);

private:
    QNetworkRequest req(const QString& path, int timeoutMs = 30000) const;
    static QString errorFrom(const Http::Response& r);
    static bool isTransient(const Http::Response& r);
};
