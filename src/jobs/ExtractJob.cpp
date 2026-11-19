#include "jobs/ExtractJob.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

ExtractJob::ExtractJob(const QString& sevenZip, const QString& archive, const QString& destDir,
                       const QString& password, QObject* parent)
    : QObject(parent), m_exe(sevenZip), m_archive(archive), m_dest(destDir), m_password(password)
{
    connect(&m_proc, &QProcess::readyReadStandardOutput, this, &ExtractJob::onStdout);
    connect(&m_proc, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += m_proc.readAllStandardError();
        if (m_stderr.size() > 64 * 1024)
            m_stderr = m_stderr.right(64 * 1024);
    });
    connect(&m_proc, &QProcess::finished, this, &ExtractJob::onFinished);
    connect(&m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit failed(Error::Other, QStringLiteral("Impossible de lancer 7-Zip (%1)").arg(QDir::toNativeSeparators(m_exe)));
    });
}

ExtractJob::~ExtractJob()
{
    if (m_proc.state() != QProcess::NotRunning) {
        m_canceled = true;
        m_proc.kill();
        m_proc.waitForFinished(2000);
    }
}

void ExtractJob::start()
{
    QDir().mkpath(m_dest);
    const QStringList args = {
        QStringLiteral("x"),                       // extraire avec les dossiers
        QDir::toNativeSeparators(m_archive),
        QStringLiteral("-o") + QDir::toNativeSeparators(m_dest),
        // -p toujours présent : sans mot de passe, 7-Zip signale l'erreur au
        // lieu d'attendre une saisie au clavier.
        QStringLiteral("-p") + m_password,
        QStringLiteral("-aou"),                    // fichier existant : nouveau renommé, jamais écrasé
        QStringLiteral("-y"),
        QStringLiteral("-bso0"),                   // pas de liste des fichiers
        QStringLiteral("-bsp1"),                   // avancement sur la sortie standard
        QStringLiteral("-bse2"),                   // erreurs sur la sortie d'erreur
        QStringLiteral("-sccUTF-8"),
    };
    m_proc.start(m_exe, args);
    m_proc.closeWriteChannel();
}

void ExtractJob::cancel()
{
    if (m_proc.state() == QProcess::NotRunning)
        return;
    m_canceled = true;
    m_proc.kill();
}

void ExtractJob::onStdout()
{
    // 7-Zip réécrit « 45% 3 - fichier » sur place (retours arrière) : on
    // prend le dernier pourcentage lu.
    m_stdoutTail += m_proc.readAllStandardOutput();
    if (m_stdoutTail.size() > 4096)
        m_stdoutTail = m_stdoutTail.right(4096);
    static const QRegularExpression re(QStringLiteral("(\\d{1,3})%"));
    auto it = re.globalMatch(QString::fromLatin1(m_stdoutTail));
    int last = -1;
    while (it.hasNext())
        last = it.next().captured(1).toInt();
    m_stdoutTail = m_stdoutTail.right(16);
    if (last >= 0 && last <= 100 && last != m_lastPercent) {
        m_lastPercent = last;
        emit progress(last);
    }
}

void ExtractJob::onFinished(int exitCode, QProcess::ExitStatus status)
{
    m_stderr += m_proc.readAllStandardError();
    if (m_canceled) {
        emit failed(Error::Canceled, QStringLiteral("Extraction arrêtée"));
        return;
    }
    if (status == QProcess::NormalExit && (exitCode == 0 || exitCode == 1)) {
        emit progress(100);
        emit finished(exitCode == 1);   // 1 = avertissements sans gravité
        return;
    }
    const QString err = QString::fromUtf8(m_stderr);
    const Error e = classify(err);
    // Détail brut : dernières lignes non vides de 7-Zip
    QStringList lines;
    for (const QString& l : err.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts))
        if (!l.trimmed().isEmpty())
            lines << l.trimmed();
    QString detail = lines.mid(qMax(0, int(lines.size()) - 3)).join(QStringLiteral(" — "));
    if (detail.isEmpty())
        detail = QStringLiteral("code %1").arg(exitCode);
    emit failed(e, message(e, detail));
}

ExtractJob::Error ExtractJob::classify(const QString& text)
{
    if (text.contains(QLatin1String("Wrong password"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("encrypted"), Qt::CaseInsensitive))
        return Error::Password;
    if (text.contains(QLatin1String("Missing volume"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("Unexpected end of archive"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("Unavailable data"), Qt::CaseInsensitive))
        return Error::Missing;
    if (text.contains(QLatin1String("not enough space"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("No space left"), Qt::CaseInsensitive))
        return Error::DiskFull;
    if (text.contains(QLatin1String("CRC Failed"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("Data Error"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("Headers Error"), Qt::CaseInsensitive))
        return Error::Corrupt;
    if (text.contains(QLatin1String("Cannot open the file as"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("Can not open the file as"), Qt::CaseInsensitive))
        return Error::NotArchive;
    return Error::Other;
}

QString ExtractJob::message(Error e, const QString& detail)
{
    switch (e) {
    case Error::Password:   return QStringLiteral("mot de passe manquant ou incorrect");
    case Error::Missing:    return QStringLiteral("archive incomplète : une partie manque ou est tronquée");
    case Error::Corrupt:    return QStringLiteral("données abîmées (contrôle CRC) — retéléchargez la partie en cause");
    case Error::NotArchive: return QStringLiteral("fichier non reconnu comme archive (incomplet ou abîmé ?)");
    case Error::DiskFull:   return QStringLiteral("espace disque insuffisant");
    case Error::Canceled:   return QStringLiteral("extraction arrêtée");
    default:                return QStringLiteral("7-Zip : %1").arg(detail);
    }
}
