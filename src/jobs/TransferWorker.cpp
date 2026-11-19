#include "jobs/TransferWorker.h"

#include "core/Utils.h"
#include "net/Http.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>

namespace {
// Gestionnaire réseau propre au fil moteur (un QNetworkAccessManager ne
// s'utilise que dans le fil où il a été créé).
QNetworkAccessManager* engineNam()
{
    static QNetworkAccessManager* nam = nullptr;
    if (!nam) {
        nam = new QNetworkAccessManager;
        nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
        QObject::connect(QThread::currentThread(), &QThread::finished, nam, &QObject::deleteLater);
    }
    return nam;
}
// ---- Limiteur de vitesse global ----
// La limite est réglée depuis l'interface (atomique). Le distributeur vit
// dans le fil moteur : toutes les 10 ms, il partage le débit autorisé à
// parts égales entre les téléchargements qui ont des données en attente
// (ce qu'un téléchargement n'utilise pas est redistribué aux autres).
std::atomic<qint64> g_limit{0};   // octets/s, 0 = illimité

class Throttle : public QObject
{
public:
    static Throttle* instance()
    {
        static Throttle* t = nullptr;
        if (!t) {
            t = new Throttle;
            QObject::connect(QThread::currentThread(), &QThread::finished, t, &QObject::deleteLater);
        }
        return t;
    }

    void request(TransferWorker* w)
    {
        if (!m_waiting.contains(w))
            m_waiting.append(w);
        if (!m_timer.isActive())
            m_timer.start();   // le temps écoulé depuis (borné à 100 ms) compte
    }

private:
    Throttle()
    {
        m_timer.setInterval(10);
        m_timer.setTimerType(Qt::PreciseTimer);
        connect(&m_timer, &QTimer::timeout, this, &Throttle::tick);
        m_clock.start();
    }

    void tick()
    {
        const qint64 limit = g_limit.load(std::memory_order_relaxed);
        // En nanosecondes : des millisecondes arrondies feraient perdre ~5 %.
        const qint64 now = m_clock.nsecsElapsed();
        const qint64 dtNs = now - m_lastNs;
        m_lastNs = now;
        QList<QPointer<TransferWorker>> list;
        for (const auto& w : std::as_const(m_waiting))
            if (w)
                list.append(w);
        m_waiting.clear();
        if (limit <= 0) {   // limite levée : chacun lit tout ce qu'il a
            m_budget = 0;
            for (const auto& w : list)
                if (w)
                    w->drain(-1);
            m_timer.stop();
            return;
        }
        m_budget = std::min(double(limit) * 0.1, m_budget + double(limit) * double(dtNs) / 1e9);
        // Partage équitable, en plusieurs passes pour redistribuer les restes.
        for (int pass = 0; pass < 4 && !list.isEmpty() && m_budget >= 1; ++pass) {
            const qint64 share = std::max<qint64>(1, qint64(m_budget / list.size()));
            QList<QPointer<TransferWorker>> still;
            for (const auto& w : list) {
                if (!w)
                    continue;
                m_budget -= double(w->drain(share));
                if (w && w->hasPending())
                    still.append(w);
            }
            list = still;
            if (m_budget < double(share))
                break;
        }
        for (const auto& w : list)
            if (w && w->hasPending())
                m_waiting.append(w);
        if (m_waiting.isEmpty())
            m_timer.stop();
    }

    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_lastNs = 0;
    double m_budget = 0;
    QList<QPointer<TransferWorker>> m_waiting;
};

constexpr qint64 kSpeedWindowMs = 3000;
constexpr qint64 kEmitIntervalMs = 250;
constexpr qint64 kReadBuffer = 8 * 1024 * 1024;
}

TransferWorker::TransferWorker(const QUrl& url, const QString& destPath)
    : m_url(url), m_dest(destPath), m_tmp(destPath + QStringLiteral(".part"))
{
}

void TransferWorker::setGlobalSpeedLimit(qint64 bytesPerSecond)
{
    g_limit.store(std::max<qint64>(0, bytesPerSecond), std::memory_order_relaxed);
}

TransferWorker::~TransferWorker()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
    }
    if (m_file.isOpen())
        m_file.close();
}

void TransferWorker::start()
{
    QDir().mkpath(QFileInfo(m_dest).absolutePath());
    m_offset = QFileInfo::exists(m_tmp) ? QFileInfo(m_tmp).size() : 0;

    QNetworkRequest req = Http::makeRequest(m_url, 0);
    // Délai d'inactivité : abandon si aucun octet pendant 60 s
    req.setTransferTimeout(60000);
    // Pas de compression : la taille reçue doit correspondre au fichier
    req.setRawHeader("Accept-Encoding", "identity");
    if (m_offset > 0)
        req.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(m_offset) + '-');

    m_clock.start();
    m_reply = engineNam()->get(req);
    m_reply->setReadBufferSize(kReadBuffer);
    connect(m_reply, &QNetworkReply::readyRead, this, &TransferWorker::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &TransferWorker::onFinished);
}

bool TransferWorker::prepareFile()
{
    if (m_prepared)
        return true;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const qint64 contentLength = m_reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    QIODevice::OpenMode mode = QIODevice::WriteOnly;

    if (status == 206) {
        // Content-Range: bytes début-fin/total
        static const QRegularExpression re(QStringLiteral("bytes\\s+(\\d+)-(\\d+)/(\\d+|\\*)"));
        const auto m = re.match(QString::fromLatin1(m_reply->rawHeader("Content-Range")));
        const qint64 start = m.hasMatch() ? m.captured(1).toLongLong() : 0;
        if (m.hasMatch() && m.captured(3) != QLatin1String("*"))
            m_total = m.captured(3).toLongLong();
        else
            m_total = start + contentLength;
        if (start != m_offset) {
            // Le serveur ne reprend pas où on l'attend : on repart de zéro.
            QFile::remove(m_tmp);
            stopWithError(QStringLiteral("Reprise incohérente, nouvel essai depuis le début"), 0, true);
            return false;
        }
        mode |= QIODevice::Append;
    } else if (status == 200) {
        m_offset = 0;           // le serveur ignore le Range : on repart de zéro
        m_total = contentLength;
        mode |= QIODevice::Truncate;
    } else if (status == 416 && m_offset > 0) {
        // « Plage demandée hors du fichier » : si le .part a déjà la taille
        // totale, le téléchargement était fini mais pas encore renommé
        // (programme arrêté pile à ce moment) → il ne reste qu'à renommer.
        static const QRegularExpression re(QStringLiteral("/\\s*(\\d+)\\s*$"));
        const auto m = re.match(QString::fromLatin1(m_reply->rawHeader("Content-Range")));
        if (m.hasMatch() && m.captured(1).toLongLong() == m_offset) {
            m_total = m_offset;
            m_received = m_offset;
            m_alreadyComplete = true;
            m_prepared = true;
            return true;
        }
        // Le .part est plus long que le fichier du serveur : on repart de zéro.
        QFile::remove(m_tmp);
        stopWithError(QStringLiteral("Fichier partiel incohérent, nouvel essai depuis le début"), 0, true);
        return false;
    } else {
        const bool retry = status >= 500 || status == 429 || status == 408;
        stopWithError(QStringLiteral("Erreur HTTP %1").arg(status), status, retry);
        return false;
    }

    // Espace disque suffisant ?
    const QStorageInfo storage(QFileInfo(m_dest).absolutePath());
    const qint64 remaining = m_total - m_offset;
    if (storage.isValid() && m_total > 0 && storage.bytesAvailable() < remaining) {
        stopWithError(QStringLiteral("Espace disque insuffisant (%1 requis, %2 libres)")
                          .arg(Utils::humanSize(remaining), Utils::humanSize(storage.bytesAvailable())),
                      0, false);
        return false;
    }

    m_file.setFileName(m_tmp);
    if (!m_file.open(mode)) {
        stopWithError(QStringLiteral("Erreur disque : %1").arg(m_file.errorString()), 0, false);
        return false;
    }
    m_received = m_offset;
    m_samples.clear();
    m_samples.emplace_back(m_clock.elapsed(), m_received);
    m_prepared = true;
    return true;
}

void TransferWorker::writeChunk(const QByteArray& data)
{
    if (data.isEmpty())
        return;
    if (m_file.write(data) != data.size()) {
        stopWithError(QStringLiteral("Erreur d'écriture : %1").arg(m_file.errorString()), 0, false);
        return;
    }
    m_received += data.size();

    const qint64 now = m_clock.elapsed();
    m_samples.emplace_back(now, m_received);
    while (m_samples.size() > 2 && now - m_samples.front().first > kSpeedWindowMs)
        m_samples.pop_front();

    if (now - m_lastEmit >= kEmitIntervalMs) {
        m_lastEmit = now;
        double speed = 0;
        const qint64 dt = m_samples.back().first - m_samples.front().first;
        if (dt > 0)
            speed = double(m_samples.back().second - m_samples.front().second) * 1000.0 / double(dt);
        emit progress(m_received, m_total, speed);
    }
}

void TransferWorker::onReadyRead()
{
    if (m_stop != Stop::None || !m_reply)
        return;
    if (!prepareFile())
        return;
    if (m_alreadyComplete) {   // corps de la réponse d'erreur : ignoré
        m_reply->readAll();
        return;
    }
    if (g_limit.load(std::memory_order_relaxed) > 0) {
        // Limité : on attend notre part ; le reste patiente dans le tampon de
        // Qt (qui, plein, ralentit la réception TCP elle-même).
        Throttle::instance()->request(this);
        return;
    }
    drain(-1);
}

qint64 TransferWorker::drain(qint64 maxBytes)
{
    if (m_stop != Stop::None || !m_reply || !prepareFile() || m_alreadyComplete)
        return 0;
    const qint64 avail = m_reply->bytesAvailable();
    const qint64 n = maxBytes < 0 ? avail : std::min(avail, maxBytes);
    if (n <= 0)
        return 0;
    writeChunk(n >= avail ? m_reply->readAll() : m_reply->read(n));
    return n;
}

bool TransferWorker::hasPending() const
{
    return m_stop == Stop::None && !m_alreadyComplete && m_reply && m_reply->bytesAvailable() > 0;
}

void TransferWorker::stopWithError(const QString& message, int status, bool retryable)
{
    if (m_stop != Stop::None)
        return;
    m_stop = Stop::Error;
    m_error = message;
    m_errorStatus = status;
    m_errorRetryable = retryable;
    abortLater();
}

void TransferWorker::abortLater()
{
    // Différé : on peut être appelé depuis un signal émis pendant la lecture
    // (readyRead -> progress -> pause()), où un abort immédiat perturbe Qt.
    if (m_reply && m_reply->isRunning())
        QMetaObject::invokeMethod(m_reply, &QNetworkReply::abort, Qt::QueuedConnection);
}

void TransferWorker::onFinished()
{
    QNetworkReply* reply = m_reply;
    if (!reply)
        return;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (m_stop == Stop::None && status == 416)
        prepareFile();   // réponse sans corps : readyRead n'a peut-être jamais eu lieu
    if (m_stop == Stop::None && reply->error() == QNetworkReply::NoError && prepareFile())
        writeChunk(reply->readAll());   // fin du flux encore en tampon

    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }

    switch (m_stop) {
    case Stop::Pause:
        emit paused();
        return;
    case Stop::Cancel:
        QFile::remove(m_tmp);
        emit canceled();
        return;
    case Stop::Error:
        emit failed(m_error, m_errorStatus, m_errorRetryable);
        return;
    case Stop::None:
        break;
    }

    if (m_alreadyComplete) {
        finishFile();
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        if (status >= 400) {
            const bool retry = status >= 500 || status == 429 || status == 408;
            emit failed(QStringLiteral("Erreur HTTP %1").arg(status), status, retry);
        } else {
            const QString msg = reply->error() == QNetworkReply::OperationCanceledError
                ? QStringLiteral("Délai dépassé (aucune donnée reçue pendant 60 s)")
                : reply->errorString();
            emit failed(QStringLiteral("Erreur réseau : %1").arg(msg), 0, true);
        }
        return;
    }

    if (m_total > 0 && m_received < m_total) {
        emit failed(QStringLiteral("Téléchargement incomplet (%1 sur %2)")
                        .arg(Utils::humanSize(m_received), Utils::humanSize(m_total)), 0, true);
        return;
    }
    finishFile();
}

void TransferWorker::finishFile()
{
    QString finalPath = m_dest;
    if (QFileInfo::exists(finalPath))
        finalPath = Utils::uniquePath(finalPath, {});
    if (!QFile::rename(m_tmp, finalPath)) {
        emit failed(QStringLiteral("Impossible de renommer le fichier .part"), 0, false);
        return;
    }
    emit progress(m_received, m_total > 0 ? m_total : m_received, 0);
    emit finished(finalPath);
}

void TransferWorker::pause(bool immediate)
{
    if (m_reply && m_reply->isRunning() && m_stop == Stop::None) {
        m_stop = Stop::Pause;
        if (immediate)
            m_reply->abort();   // fermeture de l'application : synchrone
        else
            abortLater();
    } else if (!m_reply) {
        emit paused();
    }
}

void TransferWorker::cancel()
{
    if (m_reply && m_reply->isRunning() && m_stop == Stop::None) {
        m_stop = Stop::Cancel;
        abortLater();
    } else if (!m_reply) {
        QFile::remove(m_tmp);
        emit canceled();
    }
}

void TransferWorker::shutdownNow()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }
}
