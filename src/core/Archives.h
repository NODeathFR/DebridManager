#pragma once
// Reconnaissance des archives et de leurs parties d'après le nom du fichier.
// Formats en plusieurs parties gérés :
//   nom.part1.rar, nom.part2.rar…        (RAR récent, numéros éventuellement « 01 »)
//   nom.rar, nom.r00, nom.r01…           (RAR ancien : la .rar est la première)
//   nom.7z.001, nom.zip.001, nom.xxx.001  (découpage en morceaux numérotés)
//   nom.zip, nom.z01, nom.z02…           (ZIP découpé : 7-Zip ouvre la .zip)

#include <QString>

namespace Archives {

struct Part {
    bool isArchive = false;
    QString groupKey;   // identique pour toutes les parties d'une même archive (minuscules)
    QString baseName;   // nom de l'archive sans extension ni numéro (« Film.2024 »)
    int number = 0;     // ordre de la partie (la première = firstNumber)
    int firstNumber = 0;
    bool isFirst() const { return number == firstNumber; }
};

// Analyse un nom de fichier (le dossier éventuel est ignoré).
Part identify(const QString& fileName);

} // namespace Archives
