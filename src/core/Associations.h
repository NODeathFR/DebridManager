#pragma once
// Association des fichiers .torrent et des liens magnet avec DebridManager.
// Sans droits administrateur, et entièrement réversible : le programme
// utilisé avant est noté puis remis en place quand on décoche.
//  - Windows : registre du compte (HKCU).
//  - Linux : ~/.local/share/applications/debridmanager.desktop et
//    ~/.config/mimeapps.list (norme freedesktop, KDE comme GNOME).
//
// Pour les tests : DM_ASSOC_INI désigne un fichier .ini qui remplace le
// registre (même arborescence, même sous Linux) ; sous Linux, XDG_DATA_HOME
// et XDG_CONFIG_HOME déplacent les fichiers ; DM_ASSOC_NO_TOOLS évite de
// lancer update-desktop-database / kbuildsycoca.

#include <QString>

namespace Associations {

enum class Kind { Torrent, Magnet };

enum class State {
    Unsupported,   // système non pris en charge (macOS…)
    None,          // pas associé
    Ours,          // associé à cet exécutable
    OldLocation,   // associé à DebridManager, mais à un ancien emplacement
};

bool supported();
State state(Kind k);
// Un autre programme est choisi par défaut. Windows : choix mémorisé que seul
// Windows peut changer. Linux : choisi après coup dans les réglages du bureau
// (enable() remet Debrid Manager par défaut).
bool otherDefault(Kind k);
// Linux : nom du programme par défaut actuel (« qBittorrent »), vide si aucun.
QString defaultProgram(Kind k);
// Vrai sous Windows (et avec le registre simulé des tests).
bool usesRegistry();

bool enable(Kind k);    // false si le registre n'a pas pu être écrit
bool disable(Kind k);

// Au démarrage : réécrit les associations activées qui pointent vers un
// ancien emplacement du programme (dossier portable déplacé). Renvoie le
// nombre d'associations corrigées.
int repairMoved();

// Ouvre l'écran « Applications par défaut » de Windows (Windows seulement).
void openSystemDefaults();

QString exePath();   // chemin natif du programme, .AppImage sous Linux (DM_ASSOC_EXE pour les tests)

} // namespace Associations
