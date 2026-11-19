#include "core/Archives.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace Archives {

namespace {
// Archives d'un seul morceau reconnues (7-Zip sait en lire bien d'autres,
// mais on ne propose l'extraction que pour les formats courants).
const QStringList kSingle = {
    QStringLiteral("7z"), QStringLiteral("zip"), QStringLiteral("rar"), QStringLiteral("tar"),
    QStringLiteral("gz"), QStringLiteral("tgz"), QStringLiteral("bz2"), QStringLiteral("tbz2"),
    QStringLiteral("xz"), QStringLiteral("txz"), QStringLiteral("zst"), QStringLiteral("cab"),
};

Part make(const QString& base, const QString& kind, int number, int first)
{
    Part p;
    p.isArchive = true;
    p.baseName = base;
    p.groupKey = base.toLower() + QLatin1Char('|') + kind;
    p.number = number;
    p.firstNumber = first;
    return p;
}
} // namespace

Part identify(const QString& fileName)
{
    const QString name = QFileInfo(fileName).fileName();
    QRegularExpressionMatch m;

    // nom.part01.rar
    static const QRegularExpression partRar(QStringLiteral("^(.+)\\.part(\\d{1,4})\\.rar$"),
                                            QRegularExpression::CaseInsensitiveOption);
    if ((m = partRar.match(name)).hasMatch())
        return make(m.captured(1), QStringLiteral("partrar"), m.captured(2).toInt(), 1);

    // nom.r00, nom.r01… (la partie .rar vient avant .r00)
    static const QRegularExpression oldRar(QStringLiteral("^(.+)\\.r(\\d{2,3})$"),
                                           QRegularExpression::CaseInsensitiveOption);
    if ((m = oldRar.match(name)).hasMatch())
        return make(m.captured(1), QStringLiteral("rar"), m.captured(2).toInt() + 1, 0);

    // nom.z01, nom.z02… (la .zip est ouverte en premier par 7-Zip)
    static const QRegularExpression zipPart(QStringLiteral("^(.+)\\.z(\\d{2,3})$"),
                                            QRegularExpression::CaseInsensitiveOption);
    if ((m = zipPart.match(name)).hasMatch())
        return make(m.captured(1), QStringLiteral("zip"), m.captured(2).toInt(), 0);

    // nom.7z.001, nom.mkv.001…
    static const QRegularExpression numbered(QStringLiteral("^(.+)\\.([A-Za-z0-9]{1,5})\\.(\\d{3})$"));
    if ((m = numbered.match(name)).hasMatch()) {
        const QString ext = m.captured(2).toLower();
        const QString base = kSingle.contains(ext) ? m.captured(1) : m.captured(1) + QLatin1Char('.') + m.captured(2);
        return make(base, QStringLiteral("split.") + ext, m.captured(3).toInt(), 1);
    }

    const QFileInfo fi(name);
    const QString ext = fi.suffix().toLower();
    if (!kSingle.contains(ext))
        return {};
    QString base = fi.completeBaseName();
    // .tar.gz, .tar.xz… : le nom de base ne garde pas « .tar »
    if (base.endsWith(QLatin1String(".tar"), Qt::CaseInsensitive))
        base.chop(4);
    if (ext == QLatin1String("rar"))
        return make(base, QStringLiteral("rar"), 0, 0);
    if (ext == QLatin1String("zip"))
        return make(base, QStringLiteral("zip"), 0, 0);
    return make(base, ext, 0, 0);
}

} // namespace Archives
