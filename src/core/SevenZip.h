#pragma once
// Localisation du programme 7-Zip utilisé pour l'extraction.
// Il n'est jamais intégré à DebridManager : on utilise le 7-Zip installé sur
// le PC (mis à jour normalement par l'utilisateur) ou un 7z.exe (7zz sous
// Linux) choisi à la main (clé de configuration seven_zip_path).

#include <QString>

namespace SevenZip {

struct Info {
    QString path;       // chemin de 7z.exe (vide si introuvable)
    QString version;    // « 24.09 » (vide si inconnue)
    bool custom = false;   // choisi à la main dans les paramètres
    bool found() const { return !path.isEmpty(); }
};

// Résultat de « 7z i » : version, et prise en charge des RAR (vrai si on ne
// sait pas : seuls les 7-Zip qui listent leurs décodeurs sans RAR sont signalés).
struct Probe {
    QString version;
    bool rar = true;
};

QString autoDetect();              // 7-Zip installé (registre, Program Files, PATH)
Probe probe(const QString& exe);   // lance « 7z i » (≤ 5 s)
bool supportsRar(const QString& exe);   // idem, mémorisé
QString rarHelp();                 // que faire si les RAR ne sont pas pris en charge
QString versionOf(const QString& exe);   // lance « 7z » pour lire sa version (≤ 5 s)
QString executable();              // réglage manuel s'il existe, sinon détection (rapide)
Info current();                    // idem + version (lance 7-Zip)
QString downloadUrl();

} // namespace SevenZip
