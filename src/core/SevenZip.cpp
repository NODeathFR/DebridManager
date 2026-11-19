#include "core/SevenZip.h"

#include "core/Config.h"

#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace SevenZip {

namespace {
bool isExe(const QString& p) { return !p.isEmpty() && QFileInfo(p).isFile() && QFileInfo(p).isExecutable(); }
}

QString autoDetect()
{
#if defined(Q_OS_WIN)
    // 1. Registre (installation normale de 7-Zip, 64 puis 32 bits, machine puis compte)
    const QString exeName = QStringLiteral("7z.exe");
    const QStringList roots = {QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\7-Zip"),
                               QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\7-Zip")};
    for (const QString& root : roots) {
        for (QSettings::Format fmt : {QSettings::Registry64Format, QSettings::Registry32Format}) {
            QSettings reg(root, fmt);
            for (const char* key : {"Path64", "Path"}) {
                const QString dir = reg.value(QLatin1String(key)).toString();
                if (!dir.isEmpty() && isExe(QDir(dir).filePath(exeName)))
                    return QDir::cleanPath(QDir(dir).filePath(exeName));
            }
        }
    }
    // 2. Dossiers d'installation habituels
    for (const char* var : {"ProgramW6432", "ProgramFiles", "ProgramFiles(x86)"}) {
        const QString base = qEnvironmentVariable(var);
        if (base.isEmpty())
            continue;
        const QString p = QDir(base).filePath(QStringLiteral("7-Zip/7z.exe"));
        if (isExe(p))
            return QDir::cleanPath(p);
    }
#endif
    // 3. Dans le PATH. Sous Linux/macOS, 7zz (7-Zip officiel, téléchargé sur
    // 7-zip.org) passe en premier : il sait toujours extraire les RAR, ce qui
    // n'est pas le cas du 7z de certaines distributions (Debian, Fedora…).
#if defined(Q_OS_WIN)
    const QStringList names = {QStringLiteral("7z"), QStringLiteral("7zz"), QStringLiteral("7za")};
#else
    const QStringList names = {QStringLiteral("7zz"), QStringLiteral("7z"), QStringLiteral("7za")};
#endif
    for (const QString& name : names) {
        const QString p = QStandardPaths::findExecutable(name);
        if (!p.isEmpty())
            return p;
    }
    return {};
}

Probe probe(const QString& exe)
{
    Probe out;
    if (!isExe(exe))
        return out;
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(exe, {QStringLiteral("i")});   // « i » : informations, sans rien extraire
    if (!p.waitForStarted(3000))
        return {};
    p.closeWriteChannel();
    if (!p.waitForFinished(5000)) {
        p.kill();
        p.waitForFinished(1000);
    }
    const QString text = QString::fromLocal8Bit(p.readAll().left(256 * 1024));
    // « 7-Zip 24.09 (x64) », « 7-Zip (a) 24.09 », « p7zip … 7-Zip [64] 16.02 »
    static const QRegularExpression re(QStringLiteral("7-Zip(?:\\s*\\([a-z]+\\))?(?:\\s*\\[\\d+\\])?\\s+(\\d+\\.\\d+)"));
    const auto m = re.match(text);
    if (m.hasMatch())
        out.version = m.captured(1);
    // Décodeurs RAR : lignes « … 40305 Rar5 » de la partie « Codecs: ». Les
    // 7-Zip de Debian / Ubuntu / Fedora en sont privés (paquet séparé, ou rien).
    const int codecs = text.indexOf(QLatin1String("Codecs:"));
    if (codecs >= 0) {
        static const QRegularExpression rar(QStringLiteral("\\b4030[1-5]\\b|\\bRar[1-5]\\b"));
        out.rar = rar.match(text.mid(codecs)).hasMatch();
    }
    return out;
}

QString versionOf(const QString& exe) { return probe(exe).version; }

bool supportsRar(const QString& exe)
{
    // Mémorisé par programme (chemin + date de modification : une mise à jour
    // de 7-Zip est prise en compte sans relancer Debrid Manager).
    static QHash<QString, bool> cache;
    const QFileInfo fi(exe);
    const QString key = fi.absoluteFilePath() + QLatin1Char('|') + QString::number(fi.lastModified().toMSecsSinceEpoch());
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();
    const bool rar = probe(exe).rar;
    cache.insert(key, rar);
    return rar;
}

QString rarHelp()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("installez la version officielle de 7-Zip (7-zip.org)");
#else
    return QStringLiteral("installez le paquet « 7zip-rar » (Debian, Ubuntu) ou le 7-Zip officiel "
                          "(fichier 7zz de 7-zip.org, à choisir dans Paramètres → Extraction)");
#endif
}

QString executable()
{
    const QString manual = Config::instance().get(QStringLiteral("seven_zip_path")).toString();
    return !manual.isEmpty() && isExe(manual) ? manual : autoDetect();
}

Info current()
{
    Info info;
    const QString manual = Config::instance().get(QStringLiteral("seven_zip_path")).toString();
    info.custom = !manual.isEmpty() && isExe(manual);
    info.path = info.custom ? manual : autoDetect();
    if (info.found())
        info.version = versionOf(info.path);
    return info;
}

QString downloadUrl() { return QStringLiteral("https://www.7-zip.org/"); }

} // namespace SevenZip
