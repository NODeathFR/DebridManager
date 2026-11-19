#!/usr/bin/env bash
# =============================================================================
#  Debrid Manager — fabrication de la version Linux (.tar.xz portable + AppImage)
#
#  ./paquet-linux.sh             Fabrique l'AppImage dans un conteneur Debian 12
#                                (podman ou docker). Recommandé : l'AppImage
#                                marche alors sur Debian 12+, Ubuntu 22.04+,
#                                Fedora, Arch… quelle que soit cette machine.
#  ./paquet-linux.sh --local     Fabrique avec le Qt de cette machine (≥ 6.4).
#                                L'AppImage ne marchera que sur les distributions
#                                aussi récentes ou plus récentes que celle-ci.
#  ./paquet-linux.sh --verifier  Affiche seulement les outils trouvés.
#
#  Résultat, à part de la release Windows, dans dist/linux/ :
#    DebridManager-<version>-linux-x86_64.tar.xz   dossier portable archivé
#    DebridManager-<version>-linux-x86_64/          le même dossier, décompressé
#    DebridManager-<version>-x86_64.AppImage        le même programme en un fichier
#  data/ et downloads/ d'une copie qu'on y aurait lancée ne sont jamais touchés.
#  Dossiers générés (jamais dans le zip du projet) : build-linux/, dist/,
#  build-linux.log.
# =============================================================================
set -euo pipefail

RACINE="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
cd "$RACINE"

# Numéro de version : lu dans CMakeLists.txt (écrit nulle part ailleurs)
VERSION="$(sed -n 's/^project(DebridManager VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ -n "$VERSION" ] || { echo "Numéro de version introuvable dans CMakeLists.txt" >&2; exit 1; }
NOM="DebridManager-$VERSION-x86_64.AppImage"
NOM_DOSSIER="DebridManager-$VERSION-linux-x86_64"
NOM_TAR="$NOM_DOSSIER.tar.xz"
SORTIE="$RACINE/dist/linux"
IMAGE_DEBIAN="docker.io/library/debian:12"
URL_LINUXDEPLOY="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
URL_PLUGIN_QT="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
JOURNAL="$RACINE/build-linux.log"

titre()  { printf '\n\033[1;34m== %s\033[0m\n' "$*"; }
ok()     { printf '   \033[32m✔\033[0m %s\n' "$*"; }
manque() { printf '   \033[33m✖\033[0m %s\n' "$*"; }
stop()   { printf '\n\033[1;31mArrêt : %s\033[0m\n' "$*" >&2; exit 1; }

# --- Moteur de conteneurs : podman (préféré, sans droits admin) ou docker ---
moteur() {
    if command -v podman >/dev/null 2>&1; then echo podman; return; fi
    if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then echo docker; return; fi
    echo ""
}

# --- Qt de cette machine (mode --local) ---
trouver_qmake() {
    for q in "${QMAKE:-}" qmake6 /usr/lib/qt6/bin/qmake /usr/lib64/qt6/bin/qmake /usr/lib/x86_64-linux-gnu/qt6/bin/qmake qmake-qt6; do
        [ -n "$q" ] || continue
        if command -v "$q" >/dev/null 2>&1 && "$q" -query QT_VERSION 2>/dev/null | grep -q '^6\.'; then
            command -v "$q"; return
        fi
    done
    echo ""
}

telecharger() {   # telecharger URL FICHIER
    if command -v curl >/dev/null 2>&1; then curl -fsSL --retry 3 -o "$2" "$1"
    elif command -v wget >/dev/null 2>&1; then wget -q -O "$2" "$1"
    else stop "ni curl ni wget : impossible de télécharger linuxdeploy"; fi
    chmod +x "$2"
}

# --- Fabrication proprement dite (dans le conteneur, ou en --local) ---
# construire DOSSIER_DE_TRAVAIL
construire() {
    local travail="$1"
    local qmake; qmake="$(trouver_qmake)"
    [ -n "$qmake" ] || stop "Qt 6 introuvable (qmake6). Installez les paquets de développement Qt 6, ou utilisez le mode conteneur."
    local qtver; qtver="$("$qmake" -query QT_VERSION)"
    local qtplugins; qtplugins="$("$qmake" -query QT_INSTALL_PLUGINS)"
    echo "   Qt $qtver ($qmake)"
    case "$qtver" in 6.[0-3].*) stop "Qt $qtver trop ancien : il faut Qt 6.4 ou plus récent." ;; esac

    titre "Compilation (Release)"
    cmake -S "$RACINE" -B "$travail/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_PREFIX_PATH="$("$qmake" -query QT_INSTALL_PREFIX)" >>"$JOURNAL" 2>&1 \
        || stop "configuration CMake impossible (voir build-linux.log)"
    cmake --build "$travail/build" --target DebridManager -j"$(nproc)" >>"$JOURNAL" 2>&1 \
        || stop "compilation impossible (voir build-linux.log)"
    ok "DebridManager compilé"

    titre "Outils linuxdeploy"
    local outils="$travail/outils"
    mkdir -p "$outils"
    [ -x "$outils/linuxdeploy-x86_64.AppImage" ] || telecharger "$URL_LINUXDEPLOY" "$outils/linuxdeploy-x86_64.AppImage"
    [ -x "$outils/linuxdeploy-plugin-qt-x86_64.AppImage" ] || telecharger "$URL_PLUGIN_QT" "$outils/linuxdeploy-plugin-qt-x86_64.AppImage"
    ok "linuxdeploy + extension Qt"

    titre "Assemblage de l'AppImage"
    local appdir="$travail/AppDir"
    rm -rf "$appdir" "$travail/sortie"
    mkdir -p "$travail/sortie"
    cp "$RACINE/resources/app.png" "$travail/debridmanager.png"
    # Modules Qt : Wayland (KDE / GNOME récents) en plus de X11 ; TLS (HTTPS
    # par l'OpenSSL du système, qui n'est pas mis dans le paquet : il reste à
    # jour avec la distribution) ; SVG (icônes).
    local wayland=""
    for p in libqwayland-egl.so libqwayland-generic.so; do
        [ -f "$qtplugins/platforms/$p" ] && wayland="${wayland:+$wayland;}$p"
    done
    [ -n "$wayland" ] && ok "Wayland : $wayland" || manque "Wayland absent (paquet qt6-wayland) : X11 seulement"
    (
        cd "$travail/sortie"
        export APPIMAGE_EXTRACT_AND_RUN=1          # pas besoin de FUSE pour les outils
        export QMAKE="$qmake"
        export EXTRA_PLATFORM_PLUGINS="$wayland"
        export EXTRA_QT_MODULES="waylandclient"
        export LDAI_OUTPUT="$NOM"
        export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
        export NO_STRIP=0
        local exclure=(--exclude-library 'libssl.so*' --exclude-library 'libcrypto.so*')
        # 1. Programme + Qt (l'extension Qt ignore les exclusions : on retire
        #    OpenSSL juste après), 2. fabrication du fichier AppImage.
        "$outils/linuxdeploy-x86_64.AppImage" --appdir "$appdir" \
            -e "$travail/build/DebridManager" \
            -d "$RACINE/resources/linux/debridmanager.desktop" \
            -i "$travail/debridmanager.png" \
            "${exclure[@]}" --plugin qt
        rm -f "$appdir"/usr/lib/libssl.so* "$appdir"/usr/lib/libcrypto.so*
        "$outils/linuxdeploy-x86_64.AppImage" --appdir "$appdir" "${exclure[@]}" --output appimage
    ) >>"$JOURNAL" 2>&1 || stop "linuxdeploy a échoué (voir build-linux.log)"
    # OpenSSL jamais copié : celui du système (à jour) est toujours utilisé ;
    # une copie plus ancienne casserait le HTTPS sur les distributions récentes.
    if ls "$appdir/usr/lib/" | grep -qE '^lib(ssl|crypto)\.so'; then
        stop "OpenSSL a été copié dans l'AppImage (à exclure)"
    fi
    # Le module TLS d'OpenSSL doit être présent (sinon : aucun HTTPS)
    [ -f "$appdir/usr/plugins/tls/libqopensslbackend.so" ] \
        || stop "module TLS OpenSSL absent de l'AppImage (paquet Qt incomplet ?)"
    [ -f "$travail/sortie/$NOM" ] || stop "AppImage non produite (voir build-linux.log)"

    titre "Dossier portable + archive .tar.xz"
    # Même contenu que l'AppImage, en dossier : DebridManager (à lancer),
    # lib/, plugins/, translations/. data/ et downloads/ seront créés à côté
    # de DebridManager au premier lancement.
    local patchelf="$outils/linuxdeploy-extrait/usr/bin/patchelf"
    if [ ! -x "$patchelf" ]; then
        ( cd "$outils" && rm -rf squashfs-root linuxdeploy-extrait \
          && ./linuxdeploy-x86_64.AppImage --appimage-extract >/dev/null 2>&1 \
          && mv squashfs-root linuxdeploy-extrait ) || stop "extraction de linuxdeploy (patchelf) impossible"
    fi
    local dossier="$travail/sortie/$NOM_DOSSIER"
    rm -rf "$dossier"
    mkdir -p "$dossier"
    cp "$appdir/usr/bin/DebridManager" "$dossier/DebridManager"
    "$patchelf" --set-rpath '$ORIGIN/lib' "$dossier/DebridManager"
    cp -a "$appdir/usr/lib" "$dossier/lib"
    cp -a "$appdir/usr/plugins" "$dossier/plugins"
    [ -d "$appdir/usr/translations" ] && cp -a "$appdir/usr/translations" "$dossier/translations"
    printf '[Paths]\nPrefix = .\nPlugins = plugins\nTranslations = translations\n' > "$dossier/qt.conf"
    cp "$RACINE/resources/app.png" "$dossier/debridmanager.png"
    ( cd "$travail/sortie" && tar -cJf "$NOM_TAR" "$NOM_DOSSIER" ) || stop "création de l'archive impossible"

    # Release Linux rangée à part de celle de Windows : dist/linux/
    mkdir -p "$SORTIE"
    cp -f "$travail/sortie/$NOM" "$SORTIE/$NOM"
    chmod +x "$SORTIE/$NOM"
    cp -f "$travail/sortie/$NOM_TAR" "$SORTIE/$NOM_TAR"
    # Dossier portable : remplacé, mais data/ et downloads/ d'une copie lancée
    # sur place sont conservés.
    if [ -d "$SORTIE/$NOM_DOSSIER" ]; then
        find "$SORTIE/$NOM_DOSSIER" -mindepth 1 -maxdepth 1 ! -name data ! -name downloads -exec rm -rf {} +
    fi
    mkdir -p "$SORTIE/$NOM_DOSSIER"
    cp -a "$dossier/." "$SORTIE/$NOM_DOSSIER/"
    # Ne garder que les 3 derniers paquets ; un ancien dossier portable avec
    # data/ ou downloads/ (lancé sur place) est gardé.
    ls -1t "$SORTIE"/DebridManager-*-linux-x86_64.tar.xz 2>/dev/null | tail -n +4 | while read -r ancien; do
        rm -f "$ancien"
        base="${ancien%.tar.xz}"
        version="${base##*/DebridManager-}"; version="${version%-linux-x86_64}"
        rm -f "$SORTIE/DebridManager-$version-x86_64.AppImage"
        if [ -d "$base" ] && [ ! -d "$base/data" ] && [ ! -d "$base/downloads" ]; then
            rm -rf "$base"
        fi
    done
    ok "dist/linux/$NOM_TAR ($(du -h "$SORTIE/$NOM_TAR" | cut -f1))"
    ok "dist/linux/$NOM_DOSSIER/"
    ok "dist/linux/$NOM ($(du -h "$SORTIE/$NOM" | cut -f1))"
}

# ============================== Modes ==============================
MODE="conteneur"
case "${1:-}" in
    "")                MODE="conteneur" ;;
    --local)           MODE="local" ;;
    --verifier)        MODE="verifier" ;;
    --dans-conteneur)  MODE="dans-conteneur" ;;
    -h|--aide|--help)  sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *)                 stop "option inconnue : $1 (voir ./paquet-linux.sh --aide)" ;;
esac

if [ "$MODE" = "verifier" ]; then
    titre "Debrid Manager $VERSION — outils trouvés"
    m="$(moteur)"
    [ -n "$m" ] && ok "conteneurs : $m (mode recommandé possible)" \
                || manque "ni podman ni docker utilisable (mode recommandé impossible ; voir --aide)"
    q="$(trouver_qmake)"
    [ -n "$q" ] && ok "Qt $("$q" -query QT_VERSION) : $q (mode --local possible)" || manque "Qt 6 (mode --local)"
    for t in cmake ninja g++ curl; do
        command -v "$t" >/dev/null 2>&1 && ok "$t" || manque "$t (mode --local)"
    done
    exit 0
fi

if [ "$MODE" = "dans-conteneur" ]; then
    # Exécuté DANS le conteneur Debian 12, en administrateur du conteneur.
    : > "$JOURNAL.conteneur"
    JOURNAL="$JOURNAL.conteneur"
    titre "Installation des outils dans le conteneur (quelques minutes)"
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq >>"$JOURNAL" 2>&1 || stop "apt-get update a échoué (réseau ?)"
    apt-get install -y -qq --no-install-recommends \
        build-essential cmake ninja-build ca-certificates curl file xz-utils \
        qt6-base-dev qt6-base-dev-tools qt6-svg-dev qt6-wayland libgl-dev >>"$JOURNAL" 2>&1 \
        || stop "installation des paquets impossible (voir build-linux.log.conteneur)"
    ok "outils installés"
    construire /tmp/dm
    # docker (administrateur réel) : rendre le fichier à l'utilisateur
    if [ -n "${HOTE_UID:-}" ]; then
        chown -R "$HOTE_UID:$HOTE_GID" "$RACINE/dist/linux" 2>/dev/null || true
        chown "$HOTE_UID:$HOTE_GID" "$RACINE/dist" "$JOURNAL" 2>/dev/null || true
    fi
    exit 0
fi

: > "$JOURNAL"
titre "Debrid Manager $VERSION — fabrication de l'AppImage"

if [ "$MODE" = "local" ]; then
    for t in cmake ninja g++; do
        command -v "$t" >/dev/null 2>&1 || stop "$t introuvable (installez cmake, ninja-build / ninja et g++)"
    done
    construire "$RACINE/build-linux"
    printf '\n   Attention : AppImage faite avec le Qt de cette machine (glibc %s) ;\n' "$(ldd --version | head -1 | grep -o '[0-9.]*$')"
    printf '   elle ne marchera pas sur une distribution plus ancienne.\n'
else
    m="$(moteur)"
    if [ -z "$m" ]; then
        cat >&2 <<'FIN'

Ni podman ni docker n'est utilisable. Installez podman (une seule commande) :
   Fedora          : sudo dnf install podman
   Arch / Manjaro  : sudo pacman -S podman
   Debian / Ubuntu : sudo apt install podman
puis relancez ./paquet-linux.sh — ou utilisez ./paquet-linux.sh --local.
FIN
        exit 1
    fi
    ok "conteneur $IMAGE_DEBIAN avec $m"
    extra=()
    if [ "$m" = "docker" ]; then
        extra+=(-e "HOTE_UID=$(id -u)" -e "HOTE_GID=$(id -g)")
    fi
    # :z = autorise le conteneur à lire/écrire le dossier du projet (SELinux, Fedora)
    "$m" run --rm "${extra[@]}" -v "$RACINE:/src:z" "$IMAGE_DEBIAN" \
        bash /src/paquet-linux.sh --dans-conteneur \
        || stop "la fabrication dans le conteneur a échoué (voir build-linux.log.conteneur)"
fi

printf '\n\033[1;32mTerminé : dist/linux/\033[0m\n'
printf '  %s : dossier portable à décompresser où vous voulez (tar -xJf %s),\n' "$NOM_TAR" "$NOM_TAR"
printf '      puis lancer DebridManager dans ce dossier ;\n'
printf '  %s : le même programme en un seul fichier.\n' "$NOM"
printf 'Dans les deux cas, data/ et downloads/ sont créés à côté du programme.\n'
