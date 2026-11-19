#include "jobs/DownloadTask.h"

#include "jobs/TransferWorker.h"

#include <QCoreApplication>
#include <QThread>

namespace {

QThread* g_engine = nullptr;

void stopEngine()
{
    if (!g_engine)
        return;
    g_engine->quit();
    g_engine->wait();
    delete g_engine;
    g_engine = nullptr;
}

// Fil « moteur » unique, créé au premier téléchargement, arrêté proprement
// à la destruction de l'application.
QThread* engineThread()
{
    if (!g_engine) {
        g_engine = new QThread;
        g_engine->setObjectName(QStringLiteral("Moteur de téléchargement"));
        g_engine->start();
        qAddPostRoutine(stopEngine);
    }
    return g_engine;
}

} // namespace

DownloadTask::DownloadTask(const QUrl& url, const QString& destPath, QObject* parent)
    : QObject(parent), m_worker(new TransferWorker(url, destPath))
{
    m_worker->moveToThread(engineThread());
    // Connexions entre fils : les signaux sont mis en file et livrés dans
    // l'ordre d'émission, dans le fil de l'interface.
    connect(m_worker, &TransferWorker::progress, this, &DownloadTask::progress);
    connect(m_worker, &TransferWorker::finished, this, &DownloadTask::finished);
    connect(m_worker, &TransferWorker::failed, this, &DownloadTask::failed);
    connect(m_worker, &TransferWorker::paused, this, &DownloadTask::paused);
    connect(m_worker, &TransferWorker::canceled, this, &DownloadTask::canceled);
}

DownloadTask::~DownloadTask()
{
    m_worker->disconnect(this);
    TransferWorker* w = m_worker;
    if (g_engine && g_engine->isRunning()) {
        // Bloquant (très court) : le .part est fermé quand on rend la main,
        // un nouveau téléchargement du même fichier peut donc le rouvrir.
        QMetaObject::invokeMethod(w, [w] {
            w->shutdownNow();
            w->deleteLater();
        }, Qt::BlockingQueuedConnection);
    } else {
        w->shutdownNow();
        delete w;
    }
}

void DownloadTask::start()
{
    QMetaObject::invokeMethod(m_worker, &TransferWorker::start, Qt::QueuedConnection);
}

void DownloadTask::pause(bool immediate)
{
    TransferWorker* w = m_worker;
    QMetaObject::invokeMethod(w, [w, immediate] { w->pause(immediate); },
                              immediate ? Qt::BlockingQueuedConnection : Qt::QueuedConnection);
}

void DownloadTask::cancel()
{
    QMetaObject::invokeMethod(m_worker, &TransferWorker::cancel, Qt::QueuedConnection);
}

void DownloadTask::setSpeedLimit(qint64 bytesPerSecond)
{
    TransferWorker::setGlobalSpeedLimit(bytesPerSecond);
}
