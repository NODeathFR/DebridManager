#pragma once
// Configuration portable : config.json et session.json dans data\ à côté de
// l'exécutable (si le dossier est inscriptible), sinon dans le dossier de
// configuration utilisateur. Les anciens fichiers à côté de l'exe sont
// déplacés automatiquement dans data\.
// Compatible avec le config.json de l'ancienne version Python (mêmes clés).

#include "core/Types.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

class Config
{
public:
    static Config& instance();

    QString dataDir() const { return m_dir; }       // dossier config + session (data\ en portable)
    QString configPath() const;
    QString sessionPath() const;
    bool isPortable() const { return m_portable; }
    // Non vide si config.json était illisible au démarrage (copie mise de côté).
    QString loadProblem() const { return m_loadProblem; }

    QJsonValue get(const QString& key, const QJsonValue& def = {}) const;
    void set(const QString& key, const QJsonValue& value);   // sauvegarde immédiate

    // Accès typés
    QString apiKey(Service s) const;
    void setApiKey(Service s, const QString& key);
    bool hasAnyKey() const;

    QString downloadDir() const;
    void setDownloadDir(const QString& dir);
    // Dossier séparé pour les vidéos (et leurs sous-titres) ; vide = même
    // dossier que les autres fichiers.
    QString videoDir() const;
    void setVideoDir(const QString& dir);
    // Dossier où ranger ce fichier (d'après son extension).
    QString destinationDirFor(const QString& fileName) const;

    Service defaultService() const;
    int maxConcurrent() const;
    int autoRetries() const;
    qint64 speedLimit() const;   // octets/s, 0 = illimitée (clé speed_limit)
    bool keepTorrentTree() const;
    bool clipboardMonitor() const;
    bool notifications() const;
    bool autoResume() const;
    bool removeTorrentRowWhenDone() const;

    static QString sanitizeKey(const QString& key);

private:
    Config();
    void load();
    void save() const;

    QString m_dir;
    QString m_baseDir;   // base du dossier de téléchargement par défaut
    bool m_portable = true;
    QJsonObject m_data;
    QString m_loadProblem;
};
