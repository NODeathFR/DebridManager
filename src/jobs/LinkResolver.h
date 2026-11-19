#pragma once
// Transforme un lien d'hébergeur en lien direct (ou valide un lien déjà
// direct). En mode Auto : détection de l'hébergeur puis essai RealDebrid
// et/ou AllDebrid avec bascule automatique sur l'autre service en cas
// d'échec.

#include "core/Types.h"

#include <QObject>

class LinkResolver : public QObject
{
    Q_OBJECT
public:
    LinkResolver(const QString& link, Service requested, QObject* parent = nullptr);
    void start();

signals:
    void status(const QString& text);
    void resolved(const DirectLink& link);
    // retryable : au moins un service a répondu par une erreur passagère
    // (réseau, serveur indisponible…), un nouvel essai plus tard a un sens.
    void failed(const QString& message, bool retryable);

private:
    void tryNext();
    void resolveDirect();

    QString m_link;
    Service m_requested;
    QList<Service> m_candidates;
    QStringList m_errors;
    bool m_transient = false;
};
