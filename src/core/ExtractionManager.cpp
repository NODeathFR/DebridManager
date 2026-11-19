#include "core/ExtractionManager.h"

#include "core/Archives.h"
#include "core/Config.h"
#include "core/SevenZip.h"
#include "core/Utils.h"
#include "jobs/ExtractJob.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <cmath>

ExtractionManager::ExtractionManager(DownloadModel* model, QObject* parent)
    : QObject(parent), m_model(model)
{
    m_etaTimer.setInterval(1000);
    connect(&m_etaTimer, &QTimer::timeout, this, &ExtractionManager::refreshEta);
}

ExtractionManager::~ExtractionManager() { shutdown(); }

QString ExtractionManager::fileNameOf(const DownloadItem& it)
{
    const QString& src = !it.destPath.isEmpty() ? it.destPath : (!it.relPath.isEmpty() ? it.relPath : it.name);
    return QFileInfo(src).fileName();
}

QStringList ExtractionManager::idsWithKey(const QString& key) const
{
    QStringList ids;
    for (const DownloadItem& it : m_model->items())
        if (it.kind == ItemKind::File && Archives::identify(fileNameOf(it)).groupKey == key)
            ids << it.id;
    return ids;
}

ExtractionManager::Group ExtractionManager::groupOf(const QString& itemId) const
{
    Group g;
    const DownloadItem* self = m_model->find(itemId);
    if (!self || self->kind != ItemKind::File)
        return g;
    const Archives::Part part = Archives::identify(fileNameOf(*self));
    if (!part.isArchive)
        return g;
    g.valid = true;
    g.key = part.groupKey;
    g.baseName = part.baseName;

    struct Member { const DownloadItem* it; int number; };
    QList<Member> members;
    for (const DownloadItem& it : m_model->items()) {
        if (it.kind != ItemKind::File)
            continue;
        const Archives::Part p = Archives::identify(fileNameOf(it));
        if (p.groupKey == g.key)
            members << Member{&it, p.number};
    }
    std::sort(members.begin(), members.end(), [](const Member& a, const Member& b) { return a.number < b.number; });

    QStringList missingFiles;
    bool firstPresent = false, gap = false;
    int expected = part.firstNumber;
    for (const Member& m : members) {
        g.ids << m.it->id;
        if (g.state == ExtractState::None)
            g.state = m.it->extract;
        if (m.number == part.firstNumber && !firstPresent) {
            firstPresent = true;
            g.firstId = m.it->id;
            g.firstPath = m.it->destPath;
        }
        if (m.number == expected)
            ++expected;
        else if (m.number > expected)
            gap = true;
        switch (m.it->state) {
        case ItemState::Completed:
            if (m.it->destPath.isEmpty() || !QFileInfo::exists(m.it->destPath))
                missingFiles << fileNameOf(*m.it);
            break;
        case ItemState::Failed:
        case ItemState::Canceled:
            ++g.broken;
            break;
        default:
            ++g.unfinished;
        }
    }

    if (g.unfinished)
        g.blocker = g.unfinished == 1 ? QStringLiteral("1 partie encore en téléchargement")
                                      : QStringLiteral("%1 parties encore en téléchargement").arg(g.unfinished);
    else if (g.broken)
        g.blocker = g.broken == 1 ? QStringLiteral("1 partie en échec ou annulée")
                                  : QStringLiteral("%1 parties en échec ou annulées").arg(g.broken);
    else if (!firstPresent)
        g.blocker = QStringLiteral("première partie absente de la file");
    else if (gap)
        g.blocker = QStringLiteral("il manque une partie dans la file");
    else if (!missingFiles.isEmpty())
        g.blocker = missingFiles.size() == 1 ? QStringLiteral("fichier introuvable : %1").arg(missingFiles.first())
                                             : QStringLiteral("%1 fichiers introuvables (déplacés ou supprimés)").arg(missingFiles.size());
    return g;
}

QList<ExtractionManager::Group> ExtractionManager::groupsOf(const QStringList& itemIds) const
{
    QList<Group> out;
    QSet<QString> seen;
    for (const QString& id : itemIds) {
        const Group g = groupOf(id);
        if (g.valid && !seen.contains(g.key)) {
            seen.insert(g.key);
            out << g;
        }
    }
    return out;
}

QString ExtractionManager::defaultDestination(const Group& g)
{
    const QDir dir = QFileInfo(g.firstPath).absoluteDir();
    QString name = Utils::sanitizeFileName(g.baseName);
    if (name.isEmpty())
        name = QStringLiteral("Archive");
    QString candidate = dir.filePath(name);
    for (int i = 2; QFileInfo::exists(candidate) && i < 10000; ++i)
        candidate = dir.filePath(QStringLiteral("%1 (%2)").arg(name).arg(i));
    return candidate;
}

void ExtractionManager::setGroupState(const QStringList& ids, ExtractState st, const QString& status, int percent,
                                      const QString& dir)
{
    for (const QString& id : ids) {
        DownloadItem* it = m_model->find(id);
        if (!it)
            continue;
        it->extract = st;
        it->extractStatus = status;
        it->extractPercent = percent;
        it->extractEta = -1;
        if (!dir.isEmpty())
            it->extractDir = dir;
        m_model->touch(id);
    }
    emit changed();
}

// ---------------------------------------------------------------- Demandes

void ExtractionManager::extract(const QStringList& itemIds, const QString& destDir)
{
    const QList<Group> groups = groupsOf(itemIds);
    for (const Group& g : groups)
        if (g.ready() && !(g.state == ExtractState::Queued || g.state == ExtractState::Running
                           || g.state == ExtractState::NeedPassword))
            enqueue(g, groups.size() == 1 ? destDir : QString(), QString(), false);
}

void ExtractionManager::extractWhenReady(const QStringList& itemIds)
{
    for (const Group& g : groupsOf(itemIds))
        if (g.canWait() && g.state != ExtractState::Pending)
            setGroupState(g.ids, ExtractState::Pending, QStringLiteral("Extraction prévue (parties en cours)"));
    checkPending();
}

void ExtractionManager::cancelWhenReady(const QStringList& itemIds)
{
    for (const Group& g : groupsOf(itemIds))
        if (g.state == ExtractState::Pending)
            setGroupState(g.ids, ExtractState::None, QString());
}

void ExtractionManager::stop(const QStringList& itemIds)
{
    for (const Group& g : groupsOf(itemIds)) {
        if (m_job && m_current.key == g.key) {
            m_job->cancel();   // suite dans le gestionnaire d'échec (Canceled)
            continue;
        }
        bool found = m_waitingPassword.remove(g.key) > 0;
        for (int i = m_queue.size() - 1; i >= 0; --i)
            if (m_queue.at(i).key == g.key) {
                m_queue.removeAt(i);
                found = true;
            }
        if (found || g.state == ExtractState::Pending)
            setGroupState(g.ids, ExtractState::Canceled, QStringLiteral("Extraction annulée"));
    }
}

void ExtractionManager::providePassword(const QString& key, const QString& password)
{
    if (!m_waitingPassword.contains(key))
        return;   // arrêtée entre-temps
    Request req = m_waitingPassword.take(key);
    const QStringList ids = idsWithKey(key);
    if (password.isEmpty()) {
        setGroupState(ids, ExtractState::Canceled, QStringLiteral("Extraction annulée (mot de passe non fourni)"));
        return;
    }
    if (ids.isEmpty())
        return;
    enqueue(groupOf(ids.first()), req.destDir, password, true);
}

bool ExtractionManager::namesStillUnknown() const
{
    for (const DownloadItem& it : m_model->items())
        if (it.kind == ItemKind::File && it.relPath.isEmpty()
            && (it.state == ItemState::Resolving || it.state == ItemState::WaitingRetry))
            return true;
    return false;
}

void ExtractionManager::checkPending()
{
    if (m_shuttingDown)
        return;
    QSet<QString> keys;
    QStringList firstIds;
    for (const DownloadItem& it : m_model->items()) {
        if (it.kind != ItemKind::File || it.extract != ExtractState::Pending)
            continue;
        const QString key = Archives::identify(fileNameOf(it)).groupKey;
        if (!key.isEmpty() && !keys.contains(key)) {
            keys.insert(key);
            firstIds << it.id;
        }
    }
    // Un lien encore en cours de débridage pourrait être une partie de plus :
    // on attend de connaître tous les noms.
    if (firstIds.isEmpty() || namesStillUnknown())
        return;
    for (const QString& id : firstIds) {
        const Group g = groupOf(id);
        if (g.ready())
            enqueue(g, QString(), QString(), false);
    }
}

// ---------------------------------------------------------------- File

void ExtractionManager::enqueue(const Group& g, const QString& destDir, const QString& password, bool front)
{
    Request r{g.key, destDir, password};
    if (front)
        m_queue.prepend(r);
    else
        m_queue.append(r);
    setGroupState(g.ids, ExtractState::Queued, QStringLiteral("Extraction en attente"), 0);
    startNext();
}

void ExtractionManager::startNext()
{
    while (!m_job && !m_shuttingDown && !m_queue.isEmpty()) {
        const Request req = m_queue.takeFirst();
        const QStringList ids = idsWithKey(req.key);
        if (ids.isEmpty())
            continue;   // retirée de la file entre-temps
        const Group g = groupOf(ids.first());
        if (!g.ready()) {
            setGroupState(g.ids, ExtractState::Failed, QStringLiteral("Extraction impossible : %1").arg(g.blocker));
            continue;
        }
        const QString exe = SevenZip::executable();
        if (exe.isEmpty()) {
            setGroupState(g.ids, ExtractState::Failed,
                          QStringLiteral("Extraction impossible : 7-Zip introuvable (voir Paramètres → Extraction)"));
            emit notify(QStringLiteral("Extraction"), QStringLiteral("7-Zip est introuvable : installez-le ou indiquez "
                                                                    "son emplacement dans les paramètres."));
            continue;
        }
        // 7-Zip de Debian / Ubuntu / Fedora : pas de RAR sans paquet supplémentaire.
        if (g.firstPath.endsWith(QLatin1String(".rar"), Qt::CaseInsensitive) && !SevenZip::supportsRar(exe)) {
            setGroupState(g.ids, ExtractState::Failed,
                          QStringLiteral("Extraction impossible : ce 7-Zip ne sait pas lire les RAR — %1").arg(SevenZip::rarHelp()));
            emit notify(QStringLiteral("Extraction impossible"),
                        QStringLiteral("Ce 7-Zip ne sait pas lire les RAR : voir Paramètres → Extraction."));
            continue;
        }
        m_current = req;
        m_currentDest = req.destDir.isEmpty() ? defaultDestination(g) : req.destDir;
        m_currentCreatedDest = !QFileInfo::exists(m_currentDest);
        const QString key = req.key;
        const QString baseName = g.baseName;

        auto* job = new ExtractJob(exe, g.firstPath, m_currentDest, req.password, this);
        m_job = job;
        setGroupState(g.ids, ExtractState::Running, QStringLiteral("Extraction en cours…"), 0);
        m_clock.start();
        m_estimatedTotalMs = -1;
        m_etaTimer.start();

        connect(job, &ExtractJob::progress, this, [this, key](int percent) { onExtractProgress(key, percent); });
        connect(job, &ExtractJob::finished, this, [this, job, key, baseName](bool warnings) {
            m_etaTimer.stop();
            job->deleteLater();
            m_job = nullptr;
            const QStringList ids = idsWithKey(key);
            QString status = warnings ? QStringLiteral("Extrait (avec avertissements de 7-Zip)") : QStringLiteral("Extrait");
            if (Config::instance().get(QStringLiteral("extract_trash_archives"), false).toBool()) {
                int failed = 0;
                for (const QString& id : ids)
                    if (const DownloadItem* it = m_model->find(id); it && QFileInfo::exists(it->destPath))
                        failed += !QFile::moveToTrash(it->destPath);
                status += failed ? QStringLiteral(" — %1 archive(s) non mise(s) à la corbeille").arg(failed)
                                 : QStringLiteral(" — archives mises à la corbeille");
            }
            setGroupState(ids, ExtractState::Done, status, 100, m_currentDest);
            emit notify(QStringLiteral("Extraction terminée"), baseName);
            startNext();
        });
        connect(job, &ExtractJob::failed, this, [this, job, key, baseName](ExtractJob::Error error, const QString& message) {
            m_etaTimer.stop();
            job->deleteLater();
            m_job = nullptr;
            // Ne supprime que le dossier créé pour cette extraction (contenu partiel)
            if (m_currentCreatedDest)
                QDir(m_currentDest).removeRecursively();
            const QStringList ids = idsWithKey(key);
            if (error == ExtractJob::Error::Password) {
                m_waitingPassword.insert(key, m_current);
                setGroupState(ids, ExtractState::NeedPassword, QStringLiteral("Mot de passe demandé"));
                emit passwordRequested(key, baseName, !m_current.password.isEmpty());
            } else if (error == ExtractJob::Error::Canceled) {
                setGroupState(ids, ExtractState::Canceled, QStringLiteral("Extraction annulée"));
            } else {
                setGroupState(ids, ExtractState::Failed, QStringLiteral("Extraction échouée : %1").arg(message));
                emit notify(QStringLiteral("Extraction échouée"), QStringLiteral("%1 — %2").arg(baseName, message));
            }
            startNext();
        });
        job->start();
    }
}

void ExtractionManager::onExtractProgress(const QString& key, int percent)
{
    // Estimation : durée écoulée × 100 / pourcentage, lissée pour éviter les
    // sauts (7-Zip n'annonce que des pourcentages entiers). Rien d'affiché
    // pendant les toutes premières secondes, trop peu fiables.
    const qint64 elapsed = m_clock.isValid() ? m_clock.elapsed() : 0;
    if (percent >= 1 && percent < 100 && elapsed >= 2000) {
        const double estimate = double(elapsed) * 100.0 / percent;
        m_estimatedTotalMs = m_estimatedTotalMs < 0 ? estimate : 0.7 * m_estimatedTotalMs + 0.3 * estimate;
    }
    for (const QString& id : idsWithKey(key))
        if (DownloadItem* it = m_model->find(id); it && it->extract == ExtractState::Running) {
            it->extractPercent = percent;
            m_model->touch(id);
        }
    refreshEta();
}

void ExtractionManager::refreshEta()
{
    if (!m_job || m_estimatedTotalMs < 0)
        return;
    const qint64 left = qint64(std::ceil((m_estimatedTotalMs - double(m_clock.elapsed())) / 1000.0));
    const int eta = int(qBound(qint64(1), left, qint64(999999)));   // jamais « 0 s » tant que 7-Zip travaille
    for (const QString& id : idsWithKey(m_current.key))
        if (DownloadItem* it = m_model->find(id); it && it->extract == ExtractState::Running) {
            it->extractEta = eta;
            m_model->touch(id);
        }
}

void ExtractionManager::shutdown()
{
    m_etaTimer.stop();
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    m_queue.clear();
    if (m_job) {
        ExtractJob* job = m_job;
        m_job = nullptr;
        job->disconnect(this);
        delete job;   // arrête 7-Zip
        if (m_currentCreatedDest)
            QDir(m_currentDest).removeRecursively();
    }
}
