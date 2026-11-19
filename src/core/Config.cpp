#include "core/Config.h"

#include "core/Utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

bool dirIsWritable(const QString& dir)
{
    // QFileInfo::isWritable() est peu fiable sous Windows (UAC, Program
    // Files) : on teste réellement la création d'un fichier.
    QFile probe(QDir(dir).filePath(QStringLiteral(".write_test")));
    if (!probe.open(QIODevice::WriteOnly))
        return false;
    probe.close();
    probe.remove();
    return true;
}

QJsonObject defaults(const QString& baseDir)
{
    QJsonObject d;
    d[QStringLiteral("realdebrid_api_key")] = QString();
    d[QStringLiteral("alldebrid_api_key")] = QString();
    d[QStringLiteral("download_dir")] = QDir(baseDir).filePath(QStringLiteral("downloads"));
    d[QStringLiteral("default_host")] = QStringLiteral("auto");
    d[QStringLiteral("max_concurrent_downloads")] = 3;
    d[QStringLiteral("auto_retries")] = 3;
    // Nouvelle clé (v2.0.1) : par défaut, les fichiers arrivent directement
    // dans le dossier de téléchargement, sans sous-dossier.
    d[QStringLiteral("create_torrent_folders")] = false;
    d[QStringLiteral("clipboard_monitor")] = false;
    d[QStringLiteral("notifications")] = true;
    d[QStringLiteral("auto_resume")] = true;
    d[QStringLiteral("remove_torrent_row_when_done")] = false;
    return d;
}

// Linux : config.json contient les clés API en clair → lisible par le seul
// propriétaire (droits 600), tout comme le dossier data/ (700). Sans effet
// sous Windows (les droits y sont gérés autrement, par compte).
void restrictToOwner(const QString& path, bool isDir)
{
#if defined(Q_OS_UNIX)
    QFileDevice::Permissions p = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    if (isDir)
        p |= QFileDevice::ExeOwner;
    // QFile::permissions() ajoute les droits « User » (= Owner) : on compare sur Owner/Group/Other.
    const QFileDevice::Permissions mask = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
        | QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    if (QFileInfo::exists(path) && (QFile::permissions(path) & mask) != p)
        QFile::setPermissions(path, p);
#else
    Q_UNUSED(path);
    Q_UNUSED(isDir);
#endif
}

} // namespace

Config& Config::instance()
{
    static Config cfg;
    return cfg;
}

Config::Config()
{
    // Sous Linux, dossier du fichier .AppImage (le programme lui-même tourne
    // depuis un dossier temporaire en lecture seule).
    const QString appDir = Utils::appDirPath();
    if (dirIsWritable(appDir)) {
        // Portable : config.json et session.json dans data\ à côté de l'exe
        // (le dossier de téléchargement par défaut reste à côté de l'exe).
        m_dir = QDir(appDir).filePath(QStringLiteral("data"));
        m_baseDir = appDir;
        m_portable = true;
        QDir().mkpath(m_dir);
        // Versions ≤ 0.8.2 : fichiers directement à côté de l'exe → déplacés.
        for (const QString& name : {QStringLiteral("config.json"), QStringLiteral("session.json")}) {
            const QString oldPath = QDir(appDir).filePath(name);
            const QString newPath = QDir(m_dir).filePath(name);
            if (QFile::exists(oldPath) && !QFile::exists(newPath))
                QFile::rename(oldPath, newPath);
        }
    } else {
        m_dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        m_baseDir = m_dir;
        QDir().mkpath(m_dir);
        m_portable = false;
    }
    restrictToOwner(m_dir, true);
    load();
    restrictToOwner(configPath(), false);   // aussi pour un fichier venu d'une ancienne version
}

QString Config::configPath() const { return QDir(m_dir).filePath(QStringLiteral("config.json")); }
QString Config::sessionPath() const { return QDir(m_dir).filePath(QStringLiteral("session.json")); }

void Config::load()
{
    m_data = defaults(m_baseDir);
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) {
        save();
        return;
    }
    const QByteArray raw = f.readAll();
    f.close();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (doc.isObject()) {
        const QJsonObject o = doc.object();
        for (auto it = o.begin(); it != o.end(); ++it)
            m_data[it.key()] = it.value();
        return;
    }
    if (raw.trimmed().isEmpty())
        return;
    // Fichier abîmé (souvent : modifié à la main avec une faute) : il serait
    // remplacé par les réglages par défaut à la première modification, clés
    // API comprises. On le met de côté pour pouvoir le corriger.
    QString backup = configPath() + QStringLiteral(".illisible");
    for (int i = 2; QFile::exists(backup); ++i)
        backup = configPath() + QStringLiteral(".illisible-%1").arg(i);
    QFile::copy(configPath(), backup);
    m_loadProblem = QStringLiteral("Le fichier de réglages était illisible (%1, position %2).\n"
                                   "Les réglages par défaut sont utilisés ; l'ancien fichier a été gardé sous :\n%3")
                        .arg(err.errorString()).arg(err.offset).arg(QDir::toNativeSeparators(backup));
}

void Config::save() const
{
    // QSaveFile : écriture atomique, jamais de config.json à moitié écrit
    // si l'application est tuée pendant la sauvegarde.
    QSaveFile f(configPath());
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(m_data).toJson(QJsonDocument::Indented));
    // Les enregistrements suivants reprennent les droits du fichier existant.
    if (f.commit())
        restrictToOwner(configPath(), false);
}

QJsonValue Config::get(const QString& key, const QJsonValue& def) const
{
    const QJsonValue v = m_data.value(key);
    return v.isUndefined() || v.isNull() ? def : v;
}

void Config::set(const QString& key, const QJsonValue& value)
{
    if (m_data.value(key) == value)
        return;
    m_data[key] = value;
    save();
}

QString Config::sanitizeKey(const QString& key)
{
    // Caractères invisibles parfois copiés avec une clé depuis un navigateur
    static const QRegularExpression invisible(
        QStringLiteral("[\\x{00A0}\\x{200B}\\x{200C}\\x{200D}\\x{2060}\\x{FEFF}\\x{2028}\\x{2029}\\r\\n\\t ]"));
    QString k = key;
    k.remove(invisible);
    return k.trimmed();
}

static QString keyName(Service s)
{
    return s == Service::AllDebrid ? QStringLiteral("alldebrid_api_key") : QStringLiteral("realdebrid_api_key");
}

QString Config::apiKey(Service s) const
{
    if (s != Service::RealDebrid && s != Service::AllDebrid)
        return {};
    return sanitizeKey(get(keyName(s)).toString());
}

void Config::setApiKey(Service s, const QString& key)
{
    if (s == Service::RealDebrid || s == Service::AllDebrid)
        set(keyName(s), sanitizeKey(key));
}

bool Config::hasAnyKey() const
{
    return !apiKey(Service::RealDebrid).isEmpty() || !apiKey(Service::AllDebrid).isEmpty();
}

QString Config::downloadDir() const
{
    QString d = get(QStringLiteral("download_dir")).toString();
    if (d.isEmpty())
        d = QDir(m_dir).filePath(QStringLiteral("downloads"));
    return QDir::cleanPath(d);
}

void Config::setDownloadDir(const QString& dir) { set(QStringLiteral("download_dir"), QDir::cleanPath(dir)); }

QString Config::videoDir() const
{
    const QString d = get(QStringLiteral("video_download_dir")).toString().trimmed();
    return d.isEmpty() ? QString() : QDir::cleanPath(d);
}

void Config::setVideoDir(const QString& dir)
{
    set(QStringLiteral("video_download_dir"), dir.trimmed().isEmpty() ? QString() : QDir::cleanPath(dir.trimmed()));
}

QString Config::destinationDirFor(const QString& fileName) const
{
    const QString video = videoDir();
    if (!video.isEmpty() && (Utils::isVideoFile(fileName) || Utils::isSubtitleFile(fileName)))
        return video;
    return downloadDir();
}

Service Config::defaultService() const
{
    return serviceFromId(get(QStringLiteral("default_host")).toString(), Service::Auto);
}

int Config::maxConcurrent() const { return qBound(1, get(QStringLiteral("max_concurrent_downloads")).toInt(3), 10); }
qint64 Config::speedLimit() const { return qMax<qint64>(0, qint64(get(QStringLiteral("speed_limit")).toDouble(0))); }
int Config::autoRetries() const { return qBound(0, get(QStringLiteral("auto_retries")).toInt(3), 10); }
bool Config::keepTorrentTree() const { return get(QStringLiteral("create_torrent_folders")).toBool(false); }
bool Config::clipboardMonitor() const { return get(QStringLiteral("clipboard_monitor")).toBool(false); }
bool Config::notifications() const { return get(QStringLiteral("notifications")).toBool(true); }
bool Config::autoResume() const { return get(QStringLiteral("auto_resume")).toBool(true); }
bool Config::removeTorrentRowWhenDone() const { return get(QStringLiteral("remove_torrent_row_when_done")).toBool(false); }
