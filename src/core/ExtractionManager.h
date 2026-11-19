#pragma once
// Extraction des archives de la file, à la demande de l'utilisateur (jamais
// automatique sans demande). Une seule extraction à la fois, par 7-Zip.
// Les parties d'une même archive sont retrouvées dans la file d'après leur nom.

#include "core/DownloadModel.h"

#include <QObject>
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>

class ExtractJob;

class ExtractionManager : public QObject
{
    Q_OBJECT
public:
    // Une archive de la file et l'état de ses parties.
    struct Group {
        bool valid = false;        // l'élément est une archive reconnue
        QString key;
        QString baseName;          // « Film.2024 »
        QStringList ids;           // toutes les parties présentes dans la file
        QString firstId;           // partie à ouvrir avec 7-Zip
        QString firstPath;
        int unfinished = 0;        // parties encore en téléchargement
        int broken = 0;            // parties en échec ou annulées
        QString blocker;           // raison qui empêche d'extraire maintenant (vide = prêt)
        ExtractState state = ExtractState::None;
        bool ready() const { return valid && blocker.isEmpty(); }
        bool canWait() const { return valid && (unfinished > 0 || broken > 0); }   // « quand tout sera terminé »
    };

    explicit ExtractionManager(DownloadModel* model, QObject* parent = nullptr);
    ~ExtractionManager() override;

    Group groupOf(const QString& itemId) const;
    QList<Group> groupsOf(const QStringList& itemIds) const;   // sans doublon

    void extract(const QStringList& itemIds, const QString& destDir = {});   // destDir : seulement pour une archive
    void extractWhenReady(const QStringList& itemIds);
    void cancelWhenReady(const QStringList& itemIds);
    void stop(const QStringList& itemIds);
    void providePassword(const QString& key, const QString& password);   // vide = abandon
    void checkPending();     // à appeler quand la file change
    void shutdown();

    static QString defaultDestination(const Group& g);
    static QString fileNameOf(const DownloadItem& it);

signals:
    void passwordRequested(const QString& key, const QString& archiveName, bool wrongPassword);
    void notify(const QString& title, const QString& message);
    void changed();          // un état a changé (sauvegarde de la session)

private:
    struct Request {
        QString key;
        QString destDir;       // choisi par l'utilisateur (vide = dossier au nom de l'archive)
        QString password;
    };
    void enqueue(const Group& g, const QString& destDir, const QString& password, bool front);
    void startNext();
    void setGroupState(const QStringList& ids, ExtractState st, const QString& status, int percent = -1,
                       const QString& dir = QString());
    QStringList idsWithKey(const QString& key) const;
    bool namesStillUnknown() const;
    void onExtractProgress(const QString& key, int percent);
    void refreshEta();

    DownloadModel* m_model;
    QList<Request> m_queue;
    QHash<QString, Request> m_waitingPassword;   // clé -> demande en attente de mot de passe
    QPointer<ExtractJob> m_job;
    Request m_current;
    QString m_currentDest;
    bool m_currentCreatedDest = false;
    bool m_shuttingDown = false;
    // Temps restant de l'extraction en cours, déduit du rythme d'avancement
    QElapsedTimer m_clock;
    double m_estimatedTotalMs = -1;   // durée totale estimée (lissée)
    QTimer m_etaTimer;                // décompte entre deux annonces de 7-Zip
};
