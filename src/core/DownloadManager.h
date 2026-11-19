#pragma once
// Chef d'orchestre : file d'attente, limite de parallélisme, relances
// automatiques, re-débridage des liens expirés, torrents, session.

#include "core/DownloadModel.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

class DownloadTask;
class ExtractionManager;
class LinkResolver;
class TorrentJob;

class DownloadManager : public QObject
{
    Q_OBJECT
public:
    explicit DownloadManager(QObject* parent = nullptr);
    ~DownloadManager() override;

    DownloadModel* model() const { return m_model; }
    ExtractionManager* extraction() const { return m_extract; }

    // Ajouts
    int addLinks(const QStringList& links, Service service);   // renvoie le nombre réellement ajouté
    void addTorrentFile(const QString& path, Service service);
    void addLibraryTorrent(Service service, const QString& torrentId, const QString& name);
    void submitTorrentSelection(const QString& itemId, const QStringList& fileIds);   // vide = annulation

    // Actions sur des éléments
    void pause(const QStringList& ids);
    void resume(const QStringList& ids);      // reprend / relance (pause, échec, annulé)
    void cancel(const QStringList& ids, bool removeAfter = false);
    void remove(const QStringList& ids);
    void moveToTop(const QStringList& ids);
    void moveToBottom(const QStringList& ids);

    // Actions globales
    void pauseAll();
    void resumeAll();
    void retryFailed();
    void cancelAll();
    void clearFinished();

    void setMaxConcurrent(int n);
    void setSpeedLimit(qint64 bytesPerSecond);   // enregistre + applique tout de suite (0 = illimitée)
    int runningDownloads() const;

    void loadSession();
    void shutdown();          // met en pause proprement et sauvegarde la session

signals:
    void torrentSelectionRequested(const QString& itemId, const QString& torrentName, const QList<TorrentFile>& files);
    void statsChanged(double totalSpeed, int downloading, int waiting, int completed, int failed);
    void notify(const QString& title, const QString& message);

private:
    void removeFinished();    // éléments terminés / échoués / annulés (sans relancer la file)
    void schedule();
    void launch(const QString& id);
    void startResolve(const QString& id, Service forced);
    void startTorrent(const QString& id);
    void handleFailure(const QString& id, const QString& message, int httpStatus, bool retryable);
    bool scheduleRetry(const QString& id, const QString& message);   // faux si plus d'essai permis
    void setStatus(const QString& id, ItemState state, const QString& status);
    void dropWorkers(const QString& id);
    void emitStats();
    void requestSave();
    void saveSession();
    QString addTorrentItem(const QString& displayName, Service service);

    DownloadModel* m_model;
    ExtractionManager* m_extract;
    QHash<QString, QPointer<DownloadTask>> m_tasks;
    QHash<QString, QPointer<LinkResolver>> m_resolvers;
    QHash<QString, QPointer<TorrentJob>> m_torrents;
    QHash<QString, QPointer<QTimer>> m_retryTimers;
    QSet<QString> m_removeAfterCancel;
    int m_maxConcurrent = 3;
    bool m_shuttingDown = false;
    bool m_wasBusy = false;
    QTimer m_statsTimer;
    QTimer m_saveTimer;
};
