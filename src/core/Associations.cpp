#include "core/Associations.h"

#include "core/Utils.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#include <memory>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shlobj.h>
#endif

namespace Associations {
namespace {

// Racine : HKEY_CURRENT_USER\Software (ou un .ini pour les tests).
const QString kProgTorrent = QStringLiteral("DebridManager.torrent");
const QString kProgMagnet  = QStringLiteral("DebridManager.magnet");
const QString kAppKey      = QStringLiteral("DebridManager");          // Software\DebridManager
const QString kCaps        = kAppKey + QStringLiteral("/Capabilities");
const QString kBackup      = kAppKey + QStringLiteral("/Sauvegarde");
const QString kMarker      = QStringLiteral("DebridManager");          // valeur témoin dans Classes\magnet
const QString kDef         = QStringLiteral("Default");                // valeur par défaut d'une clé

std::unique_ptr<QSettings> openRoot()
{
    const QString ini = qEnvironmentVariable("DM_ASSOC_INI");
    if (!ini.isEmpty())
        return std::make_unique<QSettings>(ini, QSettings::IniFormat);
#ifdef Q_OS_WIN
    return std::make_unique<QSettings>(QStringLiteral("HKEY_CURRENT_USER\\Software"), QSettings::NativeFormat);
#else
    return nullptr;
#endif
}

QString command() { return QStringLiteral("\"%1\" \"%2\"").arg(exePath(), QStringLiteral("%1")); }
QString iconRef() { return QStringLiteral("\"%1\",0").arg(exePath()); }

QString progId(Kind k) { return k == Kind::Torrent ? kProgTorrent : kProgMagnet; }
QString progKey(Kind k) { return QStringLiteral("Classes/") + progId(k); }
QString backupKey(Kind k) { return kBackup + (k == Kind::Torrent ? QStringLiteral("/torrent") : QStringLiteral("/magnet")); }

void writeProgId(QSettings& s, Kind k)
{
    const QString key = progKey(k);
    s.setValue(key + QStringLiteral("/") + kDef,
               k == Kind::Torrent ? QStringLiteral("Fichier torrent") : QStringLiteral("URL:Lien magnet"));
    if (k == Kind::Magnet)
        s.setValue(key + QStringLiteral("/URL Protocol"), QString());
    s.setValue(key + QStringLiteral("/DefaultIcon/") + kDef, iconRef());
    s.setValue(key + QStringLiteral("/shell/open/command/") + kDef, command());
}

// Déclaration de DebridManager dans la liste « Applications par défaut ».
void writeCapabilities(QSettings& s, Kind k)
{
    s.setValue(kCaps + QStringLiteral("/ApplicationName"), QStringLiteral("Debrid Manager"));
    s.setValue(kCaps + QStringLiteral("/ApplicationDescription"),
               QStringLiteral("Client RealDebrid / AllDebrid : liens, magnets et fichiers .torrent"));
    s.setValue(kCaps + QStringLiteral("/ApplicationIcon"), iconRef());
    if (k == Kind::Torrent)
        s.setValue(kCaps + QStringLiteral("/FileAssociations/.torrent"), kProgTorrent);
    else
        s.setValue(kCaps + QStringLiteral("/URLAssociations/magnet"), kProgMagnet);
    s.setValue(QStringLiteral("RegisteredApplications/") + kAppKey,
               QStringLiteral("Software\\DebridManager\\Capabilities"));
}

// Valeurs de Classes\magnet gérées (et sauvegardées) par DebridManager.
const QStringList& magnetValues()
{
    static const QStringList v{kDef, QStringLiteral("URL Protocol"), QStringLiteral("DefaultIcon/") + kDef,
                               QStringLiteral("shell/open/command/") + kDef};
    return v;
}

void backupOnce(QSettings& s, Kind k)
{
    const QString b = backupKey(k);
    if (s.contains(b + QStringLiteral("/fait")))
        return;   // déjà sauvegardé : ne jamais écraser par nos propres valeurs
    if (k == Kind::Torrent) {
        const QString cur = s.value(QStringLiteral("Classes/.torrent/") + kDef).toString();
        const bool foreign = !cur.isEmpty() && cur != kProgTorrent;
        s.setValue(b + QStringLiteral("/existait"), foreign);
        if (foreign)
            s.setValue(b + QStringLiteral("/") + kDef, cur);
    } else {
        const bool foreign = s.contains(QStringLiteral("Classes/magnet/shell/open/command/") + kDef)
                             && !s.contains(QStringLiteral("Classes/magnet/") + kMarker);
        s.setValue(b + QStringLiteral("/existait"), foreign);
        if (foreign) {
            int i = 0;
            for (const QString& name : magnetValues()) {
                const QString src = QStringLiteral("Classes/magnet/") + name;
                if (s.contains(src))
                    s.setValue(b + QStringLiteral("/v%1").arg(i), s.value(src));
                ++i;
            }
        }
    }
    s.setValue(b + QStringLiteral("/fait"), true);
}

void cleanupIfUnused(QSettings& s)
{
    const bool anyLeft = s.contains(progKey(Kind::Torrent) + QStringLiteral("/shell/open/command/") + kDef)
                         || s.contains(progKey(Kind::Magnet) + QStringLiteral("/shell/open/command/") + kDef);
    if (anyLeft)
        return;
    s.remove(kAppKey);
    s.remove(QStringLiteral("RegisteredApplications/") + kAppKey);
}

void notifyShell()
{
#ifdef Q_OS_WIN
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#endif
}

bool finish(QSettings& s)
{
    s.sync();
    notifyShell();
    return s.status() == QSettings::NoError;
}

} // namespace

// ===================== Windows : registre du compte (HKCU) =====================

static State regState(Kind k)
{
    auto s = openRoot();
    if (!s)
        return State::Unsupported;
    const QString cmd = s->value(progKey(k) + QStringLiteral("/shell/open/command/") + kDef).toString();
    if (cmd.isEmpty())
        return State::None;
    return cmd.compare(command(), Qt::CaseInsensitive) == 0 ? State::Ours : State::OldLocation;
}

static bool regOtherDefault(Kind k)
{
    auto s = openRoot();
    if (!s)
        return false;
    const QString key = k == Kind::Torrent
        ? QStringLiteral("Microsoft/Windows/CurrentVersion/Explorer/FileExts/.torrent/UserChoice/ProgId")
        : QStringLiteral("Microsoft/Windows/Shell/Associations/UrlAssociations/magnet/UserChoice/ProgId");
    const QString choice = s->value(key).toString();
    if (choice.isEmpty() || choice.contains(QLatin1String("DebridManager"), Qt::CaseInsensitive))
        return false;
    // « magnet » désigne Classes\magnet : c'est nous si notre témoin y est.
    if (k == Kind::Magnet && choice.compare(QLatin1String("magnet"), Qt::CaseInsensitive) == 0)
        return !s->contains(QStringLiteral("Classes/magnet/") + kMarker);
    return true;
}

static bool regEnable(Kind k)
{
    auto s = openRoot();
    if (!s)
        return false;
    backupOnce(*s, k);
    writeProgId(*s, k);
    if (k == Kind::Torrent) {
        s->setValue(QStringLiteral("Classes/.torrent/") + kDef, kProgTorrent);
        s->setValue(QStringLiteral("Classes/.torrent/OpenWithProgids/") + kProgTorrent, QString());
    } else {
        // Classes\magnet : utilisé directement quand aucun programme par défaut
        // n'a été choisi dans Windows.
        s->setValue(QStringLiteral("Classes/magnet/") + kDef, QStringLiteral("URL:Lien magnet"));
        s->setValue(QStringLiteral("Classes/magnet/URL Protocol"), QString());
        s->setValue(QStringLiteral("Classes/magnet/DefaultIcon/") + kDef, iconRef());
        s->setValue(QStringLiteral("Classes/magnet/shell/open/command/") + kDef, command());
        s->setValue(QStringLiteral("Classes/magnet/") + kMarker, QStringLiteral("1"));
    }
    writeCapabilities(*s, k);
    return finish(*s);
}

static bool regDisable(Kind k)
{
    auto s = openRoot();
    if (!s)
        return false;
    const QString b = backupKey(k);
    const bool hadForeign = s->value(b + QStringLiteral("/existait")).toBool();
    s->remove(progKey(k));
    if (k == Kind::Torrent) {
        s->remove(QStringLiteral("Classes/.torrent/OpenWithProgids/") + kProgTorrent);
        const QString defKey = QStringLiteral("Classes/.torrent/") + kDef;
        if (s->value(defKey).toString() == kProgTorrent) {
            if (hadForeign)
                s->setValue(defKey, s->value(b + QStringLiteral("/") + kDef));
            else
                s->remove(defKey);
        }
        s->remove(kCaps + QStringLiteral("/FileAssociations/.torrent"));
    } else {
        if (s->contains(QStringLiteral("Classes/magnet/") + kMarker)) {
            if (hadForeign) {
                s->remove(QStringLiteral("Classes/magnet/") + kMarker);
                int i = 0;
                for (const QString& name : magnetValues()) {
                    const QString saved = b + QStringLiteral("/v%1").arg(i++);
                    if (s->contains(saved))
                        s->setValue(QStringLiteral("Classes/magnet/") + name, s->value(saved));
                    else
                        s->remove(QStringLiteral("Classes/magnet/") + name);
                }
            } else {
                s->remove(QStringLiteral("Classes/magnet"));
            }
        }
        s->remove(kCaps + QStringLiteral("/URLAssociations/magnet"));
    }
    s->remove(b);
    cleanupIfUnused(*s);
    return finish(*s);
}


// ============ Linux : raccourci .desktop + programmes par défaut (XDG) ============
//
// Tout se passe dans le dossier personnel, sans droits administrateur :
//   ~/.local/share/applications/debridmanager.desktop   (type MIME pris en charge)
//   ~/.local/share/icons/hicolor/scalable/apps/debridmanager.svg
//   ~/.config/mimeapps.list   [Default Applications]   (programme par défaut)
// Le programme par défaut précédent est noté dans le .desktop (clés
// X-DebridManager-Avant-…) et remis en place quand on décoche.
// KDE et GNOME lisent tous deux ces fichiers (norme freedesktop).

namespace {

const QString kDesktopId = QStringLiteral("debridmanager.desktop");
const QString kIconName  = QStringLiteral("debridmanager");
const QString kDefaultGroup = QStringLiteral("Default Applications");
const QString kAddedGroup   = QStringLiteral("Added Associations");

QString mimeOf(Kind k)
{
    return k == Kind::Torrent ? QStringLiteral("application/x-bittorrent") : QStringLiteral("x-scheme-handler/magnet");
}
QString backupName(Kind k)
{
    return k == Kind::Torrent ? QStringLiteral("X-DebridManager-Avant-torrent") : QStringLiteral("X-DebridManager-Avant-magnet");
}

QString dataHome()
{
    const QString d = qEnvironmentVariable("XDG_DATA_HOME");
    return d.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/share") : d;
}
QString configHome()
{
    const QString d = qEnvironmentVariable("XDG_CONFIG_HOME");
    return d.isEmpty() ? QDir::homePath() + QStringLiteral("/.config") : d;
}
QString appsDir() { return dataHome() + QStringLiteral("/applications"); }
QString desktopPath() { return appsDir() + QLatin1Char('/') + kDesktopId; }
QString iconPath() { return dataHome() + QStringLiteral("/icons/hicolor/scalable/apps/") + kIconName + QStringLiteral(".svg"); }
QString mimeappsPath() { return configHome() + QStringLiteral("/mimeapps.list"); }

// Fichiers propres à un bureau (« kde-mimeapps.list »…), prioritaires sur
// mimeapps.list d'après la norme. On ne les modifie que s'ils existent.
QStringList desktopMimeapps()
{
    QStringList out;
    for (const QString& d : qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        const QString p = configHome() + QLatin1Char('/') + d.toLower() + QStringLiteral("-mimeapps.list");
        if (QFileInfo::exists(p) && !out.contains(p))
            out << p;
    }
    return out;
}

// --- Petit éditeur de fichiers « clé=valeur » par groupes, qui conserve tout
// le reste à l'identique (QSettings réécrirait et encoderait les clés). ---
QStringList readLines(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
}

bool writeLines(const QString& path, QStringList lines)
{
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    f.write((lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    return f.commit();
}

QString iniValue(const QStringList& lines, const QString& group, const QString& key)
{
    QString current;
    for (const QString& raw : lines) {
        const QString l = raw.trimmed();
        if (l.startsWith(QLatin1Char('[')) && l.endsWith(QLatin1Char(']'))) {
            current = l.mid(1, l.size() - 2);
            continue;
        }
        const int eq = l.indexOf(QLatin1Char('='));
        if (current == group && eq > 0 && l.left(eq).trimmed() == key)
            return l.mid(eq + 1).trimmed();
    }
    return {};
}

// value vide = supprimer la clé.
void setIniValue(QStringList& lines, const QString& group, const QString& key, const QString& value)
{
    QString current;
    int groupLine = -1, groupEnd = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QString l = lines.at(i).trimmed();
        if (l.startsWith(QLatin1Char('[')) && l.endsWith(QLatin1Char(']'))) {
            if (current == group && groupEnd < 0)
                groupEnd = i;
            current = l.mid(1, l.size() - 2);
            if (current == group && groupLine < 0)
                groupLine = i;
            continue;
        }
        const int eq = l.indexOf(QLatin1Char('='));
        if (current == group && eq > 0 && l.left(eq).trimmed() == key) {
            if (!value.isEmpty()) {
                lines[i] = key + QLatin1Char('=') + value;
                return;
            }
            lines.removeAt(i);
            // Groupe devenu vide : on retire aussi son en-tête (et la ligne vide qui le précède)
            int end = groupLine + 1;
            while (end < lines.size() && !lines.at(end).trimmed().startsWith(QLatin1Char('[')))
                if (!lines.at(end++).trimmed().isEmpty())
                    return;
            lines.erase(lines.begin() + groupLine, lines.begin() + end);
            if (groupLine > 0 && groupLine <= lines.size() && lines.at(groupLine - 1).trimmed().isEmpty())
                lines.removeAt(groupLine - 1);
            return;
        }
    }
    if (value.isEmpty())
        return;
    if (groupLine < 0) {
        if (!lines.isEmpty() && !lines.last().trimmed().isEmpty())
            lines << QString();
        lines << QLatin1Char('[') + group + QLatin1Char(']') << key + QLatin1Char('=') + value;
        return;
    }
    // Ajout à la fin du groupe (avant les lignes vides qui le séparent du suivant)
    int at = groupEnd < 0 ? lines.size() : groupEnd;
    while (at - 1 > groupLine && lines.at(at - 1).trimmed().isEmpty())
        --at;
    lines.insert(at, key + QLatin1Char('=') + value);
}

QStringList splitList(const QString& v) { return v.split(QLatin1Char(';'), Qt::SkipEmptyParts); }
QString joinList(const QStringList& l) { return l.isEmpty() ? QString() : l.join(QLatin1Char(';')) + QLatin1Char(';'); }

// Programme par défaut actuel pour ce type (le fichier le plus prioritaire gagne).
QString currentDefault(const QString& mime)
{
    for (const QString& p : desktopMimeapps() + QStringList{mimeappsPath()}) {
        const QString v = iniValue(readLines(p), kDefaultGroup, mime);
        if (!v.isEmpty())
            return splitList(v).value(0);
    }
    return {};
}

// Ligne Exec= du .desktop, selon la norme Desktop Entry. Guillemets
// seulement si le chemin en a besoin (espaces…) : l'outil xdg-open, dans son
// mode générique (hors KDE / GNOME), ne sait pas lire un chemin entre guillemets.
QString execLine()
{
    QString path = exePath();
    static const QRegularExpression reserved(QStringLiteral("[\\s\"'\\\\><~|&;$*?#()`]"));
    if (reserved.match(path).hasMatch()) {
        for (const char* c : {"\\", "\"", "`", "$"})
            path.replace(QLatin1String(c), QLatin1Char('\\') + QLatin1String(c));
        path = QLatin1Char('"') + path + QLatin1Char('"');
    }
    path.replace(QLatin1Char('%'), QStringLiteral("%%"));
    QString exec = path + QStringLiteral(" %U");
    exec.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));   // échappement des valeurs de .desktop
    return exec;
}

QStringList desktopMimes(const QStringList& lines)
{
    return splitList(iniValue(lines, QStringLiteral("Desktop Entry"), QStringLiteral("MimeType")));
}

bool writeDesktop(const QStringList& mimes, const QStringList& previous)
{
    QStringList l;
    l << QStringLiteral("[Desktop Entry]")
      << QStringLiteral("Type=Application")
      << QStringLiteral("Name=Debrid Manager")
      << QStringLiteral("Comment=Client RealDebrid / AllDebrid : liens, magnets et fichiers .torrent")
      << QStringLiteral("Exec=") + execLine()
      << QStringLiteral("Icon=") + kIconName
      << QStringLiteral("Terminal=false")
      << QStringLiteral("Categories=Network;FileTransfer;")
      << QStringLiteral("MimeType=") + joinList(mimes)
      << QStringLiteral("NoDisplay=true")   // pas d'entrée dans le menu : seulement « Ouvrir avec »
      << QStringLiteral("StartupWMClass=DebridManager");
    l << previous;
    return writeLines(desktopPath(), l);
}

QStringList backupLines(const QStringList& lines)
{
    QStringList out;
    for (Kind k : {Kind::Torrent, Kind::Magnet}) {
        const QString v = iniValue(lines, QStringLiteral("Desktop Entry"), backupName(k));
        if (!v.isEmpty())
            out << backupName(k) + QLatin1Char('=') + v;
    }
    return out;
}

void installIcon()
{
    if (QFileInfo::exists(iconPath()))
        return;
    QDir().mkpath(QFileInfo(iconPath()).absolutePath());
    QFile::copy(QStringLiteral(":/icons/app.svg"), iconPath());
    QFile::setPermissions(iconPath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup
                                          | QFileDevice::ReadOther);
}

// Prévient le bureau (cache des types MIME, base KDE). Sans attendre, et sans
// erreur si l'outil n'existe pas.
void refreshDesktop()
{
    if (!qEnvironmentVariable("DM_ASSOC_NO_TOOLS").isEmpty())
        return;
    const QString udd = QStandardPaths::findExecutable(QStringLiteral("update-desktop-database"));
    if (!udd.isEmpty())
        QProcess::startDetached(udd, {QStringLiteral("-q"), appsDir()});
    for (const char* tool : {"kbuildsycoca6", "kbuildsycoca5"}) {
        const QString t = QStandardPaths::findExecutable(QLatin1String(tool));
        if (!t.isEmpty()) {
            QProcess::startDetached(t, {});
            break;
        }
    }
}

// « Ouvrir avec » : Debrid Manager proposé en premier (add) ou retiré.
void setAdded(QStringList& lines, const QString& mime, bool add)
{
    QStringList added = splitList(iniValue(lines, kAddedGroup, mime));
    added.removeAll(kDesktopId);
    if (add)
        added.prepend(kDesktopId);
    setIniValue(lines, kAddedGroup, mime, joinList(added));
}

// Debrid Manager devient le programme par défaut : dans mimeapps.list, et
// dans les fichiers propres au bureau qui fixent déjà ce type (prioritaires).
bool makeDefault(const QString& mime)
{
    bool ok = true;
    for (const QString& p : desktopMimeapps()) {
        QStringList lines = readLines(p);
        if (iniValue(lines, kDefaultGroup, mime).isEmpty())
            continue;
        setIniValue(lines, kDefaultGroup, mime, kDesktopId);
        ok &= writeLines(p, lines);
    }
    QStringList lines = readLines(mimeappsPath());
    setIniValue(lines, kDefaultGroup, mime, kDesktopId);
    setAdded(lines, mime, true);
    return ok && writeLines(mimeappsPath(), lines);
}

// Partout où Debrid Manager est par défaut : remet le programme d'avant
// (ou retire la ligne s'il n'y en avait pas). Un choix fait ailleurs depuis
// n'est jamais touché. Retire aussi Debrid Manager de « Ouvrir avec ».
bool restoreDefault(const QString& mime, const QString& before)
{
    bool ok = true;
    for (const QString& p : desktopMimeapps() + QStringList{mimeappsPath()}) {
        QStringList lines = readLines(p);
        const QStringList original = lines;
        if (splitList(iniValue(lines, kDefaultGroup, mime)).value(0) == kDesktopId)
            setIniValue(lines, kDefaultGroup, mime, before);
        setAdded(lines, mime, false);
        if (lines != original)
            ok &= writeLines(p, lines);
    }
    return ok;
}

State xdgState(Kind k)
{
    const QStringList lines = readLines(desktopPath());
    if (!desktopMimes(lines).contains(mimeOf(k)))
        return State::None;
    const QString exec = iniValue(lines, QStringLiteral("Desktop Entry"), QStringLiteral("Exec"));
    return exec == execLine() ? State::Ours : State::OldLocation;
}

bool xdgOtherDefault(Kind k)
{
    if (xdgState(k) == State::None)
        return false;
    return currentDefault(mimeOf(k)) != kDesktopId;
}

bool xdgEnable(Kind k)
{
    const QStringList lines = readLines(desktopPath());
    QStringList mimes = desktopMimes(lines);
    QStringList previous = backupLines(lines);
    const QString mime = mimeOf(k);
    // Programme par défaut d'avant, noté une seule fois (jamais nous-mêmes)
    const QString before = currentDefault(mime);
    if (!before.isEmpty() && before != kDesktopId
        && iniValue(lines, QStringLiteral("Desktop Entry"), backupName(k)).isEmpty())
        previous << backupName(k) + QLatin1Char('=') + before;
    if (!mimes.contains(mime))
        mimes << mime;
    installIcon();
    bool ok = writeDesktop(mimes, previous);
    ok &= makeDefault(mime);
    refreshDesktop();
    return ok;
}

bool xdgDisable(Kind k)
{
    const QStringList lines = readLines(desktopPath());
    const QString mime = mimeOf(k);
    const QString before = iniValue(lines, QStringLiteral("Desktop Entry"), backupName(k));
    bool ok = restoreDefault(mime, before);
    QStringList mimes = desktopMimes(lines);
    mimes.removeAll(mime);
    if (mimes.isEmpty()) {
        QFile::remove(desktopPath());
        QFile::remove(iconPath());
    } else {
        QStringList previous;
        for (const QString& b : backupLines(lines))
            if (!b.startsWith(backupName(k) + QLatin1Char('=')))
                previous << b;
        ok &= writeDesktop(mimes, previous);
    }
    refreshDesktop();
    return ok;
}

// Programme déplacé : seule la ligne Exec change, les choix par défaut restent.
bool xdgRepair()
{
    const QStringList lines = readLines(desktopPath());
    const QStringList mimes = desktopMimes(lines);
    if (mimes.isEmpty() || iniValue(lines, QStringLiteral("Desktop Entry"), QStringLiteral("Exec")) == execLine())
        return false;
    installIcon();
    const bool ok = writeDesktop(mimes, backupLines(lines));
    refreshDesktop();
    return ok;
}

bool useRegistry()
{
#ifdef Q_OS_WIN
    return true;
#else
    return !qEnvironmentVariable("DM_ASSOC_INI").isEmpty();   // tests : registre simulé
#endif
}

} // namespace

// =============================== Interface publique ===============================

QString exePath()
{
    const QString forced = qEnvironmentVariable("DM_ASSOC_EXE");
    return QDir::toNativeSeparators(forced.isEmpty() ? Utils::appFilePath() : forced);
}

bool supported()
{
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    return true;
#else
    return useRegistry();
#endif
}

bool usesRegistry() { return useRegistry(); }

State state(Kind k) { return useRegistry() ? regState(k) : xdgState(k); }
bool otherDefault(Kind k) { return useRegistry() ? regOtherDefault(k) : xdgOtherDefault(k); }
bool enable(Kind k) { return useRegistry() ? regEnable(k) : xdgEnable(k); }
bool disable(Kind k) { return useRegistry() ? regDisable(k) : xdgDisable(k); }

QString defaultProgram(Kind k)
{
    if (useRegistry())
        return {};
    QString id = currentDefault(mimeOf(k));
    // Nom lisible : « Name= » du .desktop s'il est trouvé, sinon son identifiant
    QStringList dirs{appsDir()};
    for (const QString& d : qEnvironmentVariable("XDG_DATA_DIRS", QStringLiteral("/usr/local/share:/usr/share"))
                                .split(QLatin1Char(':'), Qt::SkipEmptyParts))
        dirs << d + QStringLiteral("/applications");
    for (const QString& d : dirs) {
        const QString name = iniValue(readLines(d + QLatin1Char('/') + id), QStringLiteral("Desktop Entry"),
                                      QStringLiteral("Name"));
        if (!name.isEmpty())
            return name;
    }
    if (id.endsWith(QLatin1String(".desktop")))
        id.chop(8);
    return id;
}

int repairMoved()
{
    int fixed = 0;
    if (!useRegistry())
        return xdgRepair() ? 1 : 0;
    for (Kind k : {Kind::Torrent, Kind::Magnet})
        if (state(k) == State::OldLocation && enable(k))
            ++fixed;
    return fixed;
}

void openSystemDefaults()
{
    // Windows 11 ouvre directement la fiche de DebridManager ; Windows 10
    // ignore le paramètre et ouvre la liste générale.
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:defaultapps?registeredAppUser=DebridManager")));
}

} // namespace Associations
