#pragma once
#include <QSet>
#include <QString>
#include <QStringList>

namespace Utils {

QString humanSize(qint64 bytes);
QString humanSpeed(double bytesPerSecond);
QString humanDuration(qint64 seconds);

// Nom de fichier valide sous Windows/Linux/macOS (caractères interdits,
// noms réservés CON/NUL..., points/espaces finaux, longueur).
QString sanitizeFileName(const QString& name);
// Idem pour un chemin relatif "Dossier/Sous-dossier/fichier" (chaque
// composant est nettoyé, ".." est neutralisé).
QString sanitizeRelativePath(const QString& path);

// Tri "naturel" : "Partie 2" avant "Partie 10".
bool naturalLess(const QString& a, const QString& b);

// Extrait tous les liens http(s) et magnet d'un texte quelconque (même au
// milieu d'une phrase), sans doublons, dans l'ordre d'apparition.
QStringList extractLinks(const QString& text);

// Chemin libre : ajoute " (1)", " (2)"... si le fichier existe déjà ou si
// le chemin est déjà réservé par un autre téléchargement.
QString uniquePath(const QString& path, const QSet<QString>& reserved);

// Domaine d'une URL, en minuscules, sans "www.".
QString domainOf(const QString& url);

// Nom de fichier déduit d'une URL.
QString fileNameFromUrl(const QString& url);

bool isVideoFile(const QString& name);
// Sous-titres (.srt, .ass…) : rangés avec les vidéos.
bool isSubtitleFile(const QString& name);

// Empêche (true) ou autorise de nouveau (false) la mise en veille automatique
// du système (l'écran peut s'éteindre). Windows : SetThreadExecutionState.
// Linux : par D-Bus, gestion d'énergie du bureau (KDE, Xfce…), sinon GNOME,
// sinon logind. À appeler toujours depuis le même fil (celui de l'interface).
void keepSystemAwake(bool awake);
// Moyen utilisé par keepSystemAwake (« logind »…), vide si rien n'est actif.
QString keepAwakeMethod();

// Fichier du programme tel que l'utilisateur le voit : sous Linux, le
// fichier .AppImage (et non l'exécutable décompressé dans un dossier
// temporaire en lecture seule) ; ailleurs, l'exécutable.
QString appFilePath();
// Dossier qui contient ce fichier (data/ et downloads/ y sont créés).
QString appDirPath();

} // namespace Utils
