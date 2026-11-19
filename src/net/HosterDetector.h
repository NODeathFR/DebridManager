#pragma once
// Détermine si un lien est un lien d'hébergeur (à débrider) ou un lien
// direct, en comparant son domaine aux listes officielles RD/AD.
// Listes mises en cache 6 h ; requêtes simultanées dédoublonnées (coller
// 50 liens d'un coup ne déclenche qu'un seul téléchargement de la liste).

#include <QObject>
#include <QString>
#include <functional>

class HosterDetector
{
public:
    enum Result { RealDebrid, AllDebrid, Both, Direct, Unknown };
    static void detect(const QString& link, const QString& adKey, QObject* context, std::function<void(Result)> cb);
};
