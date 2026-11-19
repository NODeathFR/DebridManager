#include "core/Utils.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

namespace Utils {

QString humanSize(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("—");
    static const char* units[] = {"o", "Ko", "Mo", "Go", "To", "Po"};
    double v = double(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 5) {
        v /= 1024.0;
        ++u;
    }
    return QStringLiteral("%1 %2").arg(QString::number(v, 'f', u == 0 ? 0 : (v < 10 ? 2 : 1)),
                                       QString::fromLatin1(units[u]));
}

QString humanSpeed(double bps)
{
    if (bps <= 1.0)
        return {};
    return humanSize(qint64(bps)) + QStringLiteral("/s");
}

QString humanDuration(qint64 s)
{
    if (s < 0)
        return {};
    if (s < 60)
        return QStringLiteral("%1 s").arg(s);
    if (s < 3600)
        return QStringLiteral("%1 min %2 s").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
    if (s < 86400)
        return QStringLiteral("%1 h %2 min").arg(s / 3600).arg((s % 3600) / 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1 j %2 h").arg(s / 86400).arg((s % 86400) / 3600);
}

QString sanitizeFileName(const QString& input)
{
    static const QRegularExpression forbidden(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1F]"));
    QString name = input;
    name.replace(forbidden, QStringLiteral("_"));
    name = name.trimmed();
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
        name.chop(1);
    if (name.isEmpty())
        return QStringLiteral("fichier");

    static const QStringList reserved = {
        QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
        QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
        QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"),
        QStringLiteral("LPT8"), QStringLiteral("LPT9")};
    const QString base = name.section(QLatin1Char('.'), 0, 0).toUpper();
    if (reserved.contains(base))
        name.prepend(QLatin1Char('_'));

    // Limite raisonnable (NTFS : 255) en conservant l'extension.
    if (name.size() > 200) {
        const QString ext = QFileInfo(name).suffix();
        name = ext.isEmpty() ? name.left(200) : name.left(200 - ext.size() - 1) + QLatin1Char('.') + ext;
    }
    return name;
}

QString sanitizeRelativePath(const QString& path)
{
    QStringList parts;
    const auto raw = QString(path).replace(QLatin1Char('\\'), QLatin1Char('/'))
                         .split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& p : raw) {
        if (p == QLatin1String(".") || p == QLatin1String(".."))
            continue;
        parts << sanitizeFileName(p);
    }
    if (parts.isEmpty())
        return QStringLiteral("fichier");
    return parts.join(QLatin1Char('/'));
}

bool naturalLess(const QString& a, const QString& b)
{
    // Implémentation maison : QCollator::setNumericMode n'est pas supporté
    // par tous les back-ends (ex. locale "C" sans ICU).
    int i = 0, j = 0;
    const int na = a.size(), nb = b.size();
    while (i < na && j < nb) {
        const QChar ca = a.at(i), cb = b.at(j);
        if (ca.isDigit() && cb.isDigit()) {
            int si = i, sj = j;
            while (si < na && a.at(si) == QLatin1Char('0')) ++si;
            while (sj < nb && b.at(sj) == QLatin1Char('0')) ++sj;
            int ei = si, ej = sj;
            while (ei < na && a.at(ei).isDigit()) ++ei;
            while (ej < nb && b.at(ej).isDigit()) ++ej;
            const int la = ei - si, lb = ej - sj;
            if (la != lb)
                return la < lb;
            const int cmp = QStringView(a).mid(si, la).compare(QStringView(b).mid(sj, lb));
            if (cmp != 0)
                return cmp < 0;
            i = ei;
            j = ej;
            continue;
        }
        const QChar la = ca.toLower(), lb = cb.toLower();
        if (la != lb)
            return QString::localeAwareCompare(QString(la), QString(lb)) < 0;
        ++i;
        ++j;
    }
    return (na - i) < (nb - j);
}

QStringList extractLinks(const QString& text)
{
    static const QRegularExpression re(
        QStringLiteral("(https?://[^\\s\"'<>]+|magnet:\\?[^\\s\"'<>]+)"),
        QRegularExpression::CaseInsensitiveOption);
    QStringList out;
    QSet<QString> seen;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        QString link = it.next().captured(1);
        // Ponctuation de fin de phrase collée au lien
        while (!link.isEmpty() && QStringLiteral(".,;)]}").contains(link.back()))
            link.chop(1);
        if (!link.isEmpty() && !seen.contains(link)) {
            seen.insert(link);
            out << link;
        }
    }
    return out;
}

QString uniquePath(const QString& path, const QSet<QString>& reserved)
{
    auto taken = [&](const QString& p) {
        return QFileInfo::exists(p) || QFileInfo::exists(p + QStringLiteral(".part")) || reserved.contains(p);
    };
    if (!taken(path))
        return path;
    const QFileInfo fi(path);
    const QString dir = fi.absolutePath();
    const QString base = fi.completeBaseName();
    const QString ext = fi.suffix().isEmpty() ? QString() : QLatin1Char('.') + fi.suffix();
    for (int i = 1; i < 10000; ++i) {
        const QString candidate = QStringLiteral("%1/%2 (%3)%4").arg(dir, base).arg(i).arg(ext);
        if (!taken(candidate))
            return candidate;
    }
    return path;
}

QString domainOf(const QString& url)
{
    QString host = QUrl(url).host().toLower();
    if (host.startsWith(QLatin1String("www.")))
        host.remove(0, 4);
    return host;
}

QString fileNameFromUrl(const QString& url)
{
    const QString name = QUrl::fromPercentEncoding(QUrl(url).path().section(QLatin1Char('/'), -1).toUtf8());
    return name.isEmpty() ? QStringLiteral("fichier") : sanitizeFileName(name);
}

bool isVideoFile(const QString& name)
{
    static const QStringList ext = {
        QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("avi"), QStringLiteral("m4v"),
        QStringLiteral("mov"), QStringLiteral("wmv"), QStringLiteral("ts"), QStringLiteral("m2ts"),
        QStringLiteral("webm"), QStringLiteral("flv"), QStringLiteral("mpg"), QStringLiteral("mpeg")};
    return ext.contains(QFileInfo(name).suffix().toLower());
}

bool isSubtitleFile(const QString& name)
{
    static const QStringList ext = {QStringLiteral("srt"), QStringLiteral("ass"), QStringLiteral("ssa"),
                                    QStringLiteral("sub"), QStringLiteral("idx"), QStringLiteral("vtt"),
                                    QStringLiteral("sup")};
    return ext.contains(QFileInfo(name).suffix().toLower());
}

} // namespace Utils

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(DM_HAVE_DBUS)
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusUnixFileDescriptor>
#endif

#include <QCoreApplication>

QString Utils::appFilePath()
{
#if defined(Q_OS_LINUX)
    // Variable posée par le lanceur de l'AppImage : chemin du fichier .AppImage.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty() && QFileInfo(appImage).isFile())
        return QFileInfo(appImage).absoluteFilePath();
#endif
    return QCoreApplication::applicationFilePath();
}

QString Utils::appDirPath() { return QFileInfo(appFilePath()).absolutePath(); }

#if defined(DM_HAVE_DBUS) && !defined(Q_OS_WIN)
namespace {

// Blocage en cours (un seul à la fois).
struct Inhibition {
    QString method;               // vide = aucun
    uint cookie = 0;              // KDE / GNOME
    QDBusUnixFileDescriptor fd;   // logind : le blocage dure tant que ce descripteur est ouvert
};
Inhibition g_inhibit;

const QString kReason = QStringLiteral("Téléchargements ou extraction en cours");

QDBusMessage callBus(QDBusConnection bus, const QString& service, const QString& path, const QString& iface,
                     const QString& method, const QVariantList& args)
{
    QDBusMessage m = QDBusMessage::createMethodCall(service, path, iface, method);
    m.setArguments(args);
    return bus.call(m, QDBus::Block, 2000);   // jamais plus de 2 s d'attente
}

bool replyOk(const QDBusMessage& r) { return r.type() == QDBusMessage::ReplyMessage && !r.arguments().isEmpty(); }

} // namespace
#endif

void Utils::keepSystemAwake(bool awake)
{
#if defined(Q_OS_WIN)
    // ES_SYSTEM_REQUIRED : pas de mise en veille automatique ; l'écran peut
    // s'éteindre normalement. Une mise en veille demandée par l'utilisateur
    // (menu Démarrer, capot fermé) reste possible.
    SetThreadExecutionState(awake ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS);
#elif defined(DM_HAVE_DBUS)
    if (awake == !g_inhibit.method.isEmpty())
        return;
    QDBusConnection session = QDBusConnection::sessionBus();
    if (!awake) {
        if (g_inhibit.method == QLatin1String("kde"))
            callBus(session, QStringLiteral("org.freedesktop.PowerManagement"),
                    QStringLiteral("/org/freedesktop/PowerManagement/Inhibit"),
                    QStringLiteral("org.freedesktop.PowerManagement.Inhibit"), QStringLiteral("UnInhibit"),
                    {g_inhibit.cookie});
        else if (g_inhibit.method == QLatin1String("gnome"))
            callBus(session, QStringLiteral("org.gnome.SessionManager"), QStringLiteral("/org/gnome/SessionManager"),
                    QStringLiteral("org.gnome.SessionManager"), QStringLiteral("Uninhibit"), {g_inhibit.cookie});
        g_inhibit = Inhibition();   // logind : fermer le descripteur suffit
        return;
    }
    // Comme sous Windows : seule la mise en veille AUTOMATIQUE est bloquée,
    // l'écran peut s'éteindre et l'utilisateur peut toujours mettre en veille.
    if (session.isConnected()) {
        // 1. KDE Plasma, Xfce… (norme freedesktop de gestion d'énergie)
        QDBusMessage r = callBus(session, QStringLiteral("org.freedesktop.PowerManagement"),
                                 QStringLiteral("/org/freedesktop/PowerManagement/Inhibit"),
                                 QStringLiteral("org.freedesktop.PowerManagement.Inhibit"), QStringLiteral("Inhibit"),
                                 {QStringLiteral("Debrid Manager"), kReason});
        if (replyOk(r)) {
            g_inhibit.method = QStringLiteral("kde");
            g_inhibit.cookie = r.arguments().first().toUInt();
            return;
        }
        // 2. GNOME, MATE, Cinnamon : drapeau 4 = « mise en veille »
        r = callBus(session, QStringLiteral("org.gnome.SessionManager"), QStringLiteral("/org/gnome/SessionManager"),
                    QStringLiteral("org.gnome.SessionManager"), QStringLiteral("Inhibit"),
                    {QStringLiteral("DebridManager"), uint(0), kReason, uint(4)});
        if (replyOk(r)) {
            g_inhibit.method = QStringLiteral("gnome");
            g_inhibit.cookie = r.arguments().first().toUInt();
            return;
        }
    }
    // 3. Sinon logind (systemd) : bloque toute mise en veille tant que c'est actif.
    QDBusConnection system = QDBusConnection::systemBus();
    if (system.isConnected()) {
        const QDBusMessage r = callBus(system, QStringLiteral("org.freedesktop.login1"),
                                       QStringLiteral("/org/freedesktop/login1"),
                                       QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("Inhibit"),
                                       {QStringLiteral("sleep"), QStringLiteral("Debrid Manager"), kReason,
                                        QStringLiteral("block")});
        if (replyOk(r)) {
            const auto fd = qvariant_cast<QDBusUnixFileDescriptor>(r.arguments().first());
            if (fd.isValid()) {
                g_inhibit.method = QStringLiteral("logind");
                g_inhibit.fd = fd;
            }
        }
    }
#else
    Q_UNUSED(awake);
#endif
}

QString Utils::keepAwakeMethod()
{
#if defined(DM_HAVE_DBUS) && !defined(Q_OS_WIN)
    return g_inhibit.method;
#else
    return {};
#endif
}
