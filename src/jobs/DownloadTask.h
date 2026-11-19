#pragma once
// Téléchargement HTTP d'un fichier vers <dest>.part puis renommage final
// (reprise par Range, vitesse lissée, espace disque, intégrité).
//
// Façade utilisée par le gestionnaire de file : le travail réel (réception
// réseau + écriture disque) est fait par un TransferWorker dans un fil
// d'exécution séparé, partagé par tous les téléchargements, pour que la
// fenêtre reste fluide même à plusieurs Gbit/s. Les signaux arrivent dans le
// fil de l'interface (progression au plus 4 fois par seconde).

#include <QObject>
#include <QString>
#include <QUrl>

class TransferWorker;

class DownloadTask : public QObject
{
    Q_OBJECT
public:
    DownloadTask(const QUrl& url, const QString& destPath, QObject* parent = nullptr);
    ~DownloadTask() override;   // attend que le fichier soit fermé

    void start();
    void pause(bool immediate = false);   // garde le .part ; immediate = attend l'arrêt (fermeture)
    void cancel();                        // supprime le .part

    // Vitesse maximale totale, tous téléchargements confondus (0 = illimitée).
    // Effet immédiat, y compris sur les téléchargements en cours.
    static void setSpeedLimit(qint64 bytesPerSecond);

signals:
    void progress(qint64 received, qint64 total, double speed);
    void finished(const QString& finalPath);
    void failed(const QString& message, int httpStatus, bool retryable);
    void paused();
    void canceled();

private:
    TransferWorker* m_worker = nullptr;
};
