#pragma once
// Une extraction : lance 7-Zip dans un processus séparé (la fenêtre reste
// fluide), lit son avancement et traduit ses erreurs.

#include <QObject>
#include <QProcess>

class ExtractJob : public QObject
{
    Q_OBJECT
public:
    enum class Error { None, Password, Missing, Corrupt, NotArchive, DiskFull, Canceled, Other };

    ExtractJob(const QString& sevenZip, const QString& archive, const QString& destDir,
               const QString& password, QObject* parent = nullptr);
    ~ExtractJob() override;

    void start();
    void cancel();

    // Analyse du texte d'erreur de 7-Zip (public pour les tests)
    static Error classify(const QString& stderrText);
    static QString message(Error e, const QString& detail);

signals:
    void progress(int percent);
    void finished(bool withWarnings);
    void failed(ExtractJob::Error error, const QString& message);

private:
    void onStdout();
    void onFinished(int exitCode, QProcess::ExitStatus status);

    QProcess m_proc;
    QString m_exe, m_archive, m_dest, m_password;
    QByteArray m_stderr;
    QByteArray m_stdoutTail;
    int m_lastPercent = -1;
    bool m_canceled = false;
};
