#pragma once
// Moteur d'un téléchargement HTTP (vit dans le fil « moteur », jamais dans
// celui de l'interface) : fichier <dest>.part puis renommage final, reprise
// par en-tête Range, vitesse lissée sur fenêtre glissante, vérification de
// l'espace disque et de l'intégrité (taille reçue).
// Ne pas utiliser directement : passer par DownloadTask.

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <deque>

class QNetworkReply;

class TransferWorker : public QObject
{
    Q_OBJECT
public:
    TransferWorker(const QUrl& url, const QString& destPath);
    ~TransferWorker() override;

    void start();
    void pause(bool immediate = false);   // garde le .part pour reprendre plus tard
    void cancel();    // supprime le .part
    void shutdownNow();   // arrêt immédiat et silencieux (destruction)

    // Limite globale partagée par tous les téléchargements (0 = illimitée).
    // Appelable depuis n'importe quel fil, effet immédiat.
    static void setGlobalSpeedLimit(qint64 bytesPerSecond);

    // Utilisés par le distributeur du limiteur (fil moteur).
    qint64 drain(qint64 maxBytes);   // écrit au plus maxBytes (-1 = tout) ; renvoie le nombre écrit
    bool hasPending() const;         // des données attendent dans le tampon

signals:
    void progress(qint64 received, qint64 total, double speed);
    void finished(const QString& finalPath);
    void failed(const QString& message, int httpStatus, bool retryable);
    void paused();
    void canceled();

private:
    enum class Stop { None, Pause, Cancel, Error };

    bool prepareFile();
    void onReadyRead();
    void onFinished();
    void writeChunk(const QByteArray& data);
    void abortLater();
    void stopWithError(const QString& message, int status, bool retryable);
    void finishFile();   // renomme le .part et annonce la fin

    QUrl m_url;
    QString m_dest;
    QString m_tmp;
    QFile m_file;
    QPointer<QNetworkReply> m_reply;
    qint64 m_offset = 0;
    qint64 m_received = 0;
    qint64 m_total = 0;
    bool m_prepared = false;
    bool m_alreadyComplete = false;   // .part déjà complet (coupure juste avant le renommage)
    Stop m_stop = Stop::None;
    QString m_error;
    int m_errorStatus = 0;
    bool m_errorRetryable = false;
    QElapsedTimer m_clock;
    qint64 m_lastEmit = 0;
    std::deque<std::pair<qint64, qint64>> m_samples;   // (ms, octets)
};
