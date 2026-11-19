#include "core/DownloadManager.h"

#include "core/Config.h"
#include "core/ExtractionManager.h"
#include "core/Utils.h"
#include "jobs/DownloadTask.h"
#include "jobs/LinkResolver.h"
#include "jobs/TorrentJob.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUrlQuery>
#include <QUuid>

static QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

DownloadManager::DownloadManager(QObject* parent)
    : QObject(parent), m_model(new DownloadModel(this)), m_extract(new ExtractionManager(m_model, this))
{
    connect(m_extract, &ExtractionManager::notify, this, &DownloadManager::notify);
    connect(m_extract, &ExtractionManager::changed, this, &DownloadManager::requestSave);
    m_maxConcurrent = Config::instance().maxConcurrent();
    DownloadTask::setSpeedLimit(Config::instance().speedLimit());

    m_statsTimer.setInterval(500);
    connect(&m_statsTimer, &QTimer::timeout, this, &DownloadManager::emitStats);
    m_statsTimer.start();

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(2000);
    connect(&m_saveTimer, &QTimer::timeout, this, &DownloadManager::saveSession);
}

DownloadManager::~DownloadManager() = default;

// ------------------------------------------------------------------ Ajouts

int DownloadManager::addLinks(const QStringList& links, Service service)
{
    QSet<QString> existing;
    for (const DownloadItem& it : m_model->items())
        if (!it.isFinished())
            existing.insert(it.sourceLink);

    int added = 0;
    for (const QString& link : links) {
        if (existing.contains(link))
            continue;   // déjà dans la file : on ignore le doublon
        existing.insert(link);
        ++added;

        if (link.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive)) {
            QString name = QUrlQuery(QUrl(link).query()).queryItemValue(QStringLiteral("dn"), QUrl::FullyDecoded);
            name.replace(QLatin1Char('+'), QLatin1Char(' '));
            const QString id = addTorrentItem(name.isEmpty() ? QStringLiteral("Torrent (magnet)") : name,
                                              service == Service::Direct ? Service::Auto : service);
            m_model->find(id)->sourceLink = link;
            startTorrent(id);
            continue;
        }

        DownloadItem it;
        it.id = newId();
        it.kind = ItemKind::File;
        it.state = ItemState::Resolving;
        it.requested = service;
        it.service = service;
        it.name = link;
        it.sourceLink = link;
        it.status = QStringLiteral("Résolution…");
        it.added = QDateTime::currentDateTime();
        m_model->append(it);
        startResolve(it.id, service);
    }
    schedule();
    return added;
}

QString DownloadManager::addTorrentItem(const QString& displayName, Service service)
{
    DownloadItem it;
    it.id = newId();
    it.kind = ItemKind::Torrent;
    it.state = ItemState::TorrentProcessing;
    it.requested = service;
    it.service = service;
    it.name = displayName;
    it.status = QStringLiteral("Préparation du torrent…");
    it.added = QDateTime::currentDateTime();
    m_model->append(it);
    return it.id;
}

void DownloadManager::addTorrentFile(const QString& path, Service service)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 20 * 1024 * 1024) {
        emit notify(QStringLiteral("Fichier torrent"), QStringLiteral("Impossible de lire %1").arg(QFileInfo(path).fileName()));
        return;
    }
    const QString fileName = QFileInfo(path).fileName();
    const QString id = addTorrentItem(QFileInfo(path).completeBaseName(), service == Service::Direct ? Service::Auto : service);
    DownloadItem* it = m_model->find(id);
    it->torrentFileName = fileName;
    it->torrentData = f.readAll();
    startTorrent(id);
    schedule();
}

void DownloadManager::addLibraryTorrent(Service service, const QString& torrentId, const QString& name)
{
    const QString id = addTorrentItem(name, service);
    m_model->find(id)->torrentId = torrentId;
    startTorrent(id);
    schedule();
}

// ---------------------------------------------------------------- Torrents

void DownloadManager::startTorrent(const QString& id)
{
    DownloadItem* it = m_model->find(id);
    if (!it)
        return;
    TorrentJob::Source src;
    if (!it->sourceLink.isEmpty())
        src.magnet = it->sourceLink;
    else if (!it->torrentData.isEmpty()) {
        src.fileName = it->torrentFileName;
        src.fileData = it->torrentData;
    }
    // Torrent de la bibliothèque, ou relance d'un torrent déjà envoyé : on
    // reprend celui du compte au lieu d'en créer un second.
    if (!it->torrentId.isEmpty()) {
        src.existingId = it->torrentId;
        src.existingService = it->service;
    }

    it->error.clear();
    it->serverProgress = -1;
    setStatus(id, ItemState::TorrentProcessing, QStringLiteral("Préparation du torrent…"));

    auto* job = new TorrentJob(src, it->requested, this);
    m_torrents.insert(id, job);

    connect(job, &TorrentJob::status, this, [this, id](const QString& text, int progress) {
        if (DownloadItem* i = m_model->find(id)) {
            i->status = text;
            i->serverProgress = progress;
            m_model->touch(id);
        }
    });
    connect(job, &TorrentJob::serviceChanged, this, [this, id](Service s) {
        if (DownloadItem* i = m_model->find(id)) {
            i->service = s;
            m_model->touch(id);
        }
    });
    connect(job, &TorrentJob::filesReady, this, [this, id, job](const QList<TorrentFile>& files, const QString& name) {
        DownloadItem* i = m_model->find(id);
        if (!i)
            return;
        if (!name.isEmpty())
            i->name = name;
        i->torrentId = job->torrentId();
        if (files.size() == 1) {   // rien à choisir
            job->selectFiles({files.first().id});
            return;
        }
        setStatus(id, ItemState::WaitingSelection,
                  QStringLiteral("%1 fichiers — en attente de votre sélection").arg(files.size()));
        emit torrentSelectionRequested(id, i->name, files);
    });
    connect(job, &TorrentJob::resolved, this, [this, id, job](const QList<DirectLink>& links, const QStringList& errors) {
        m_torrents.remove(id);
        job->deleteLater();
        DownloadItem* i = m_model->find(id);
        if (!i)
            return;
        const bool removeRow = errors.isEmpty() && Config::instance().removeTorrentRowWhenDone();
        QList<DownloadItem> files;
        for (const DirectLink& d : links) {
            DownloadItem f;
            f.id = newId();
            if (!removeRow)
                f.parentId = id;   // affiché replié/déplié sous la ligne du torrent
            f.kind = ItemKind::File;
            f.state = ItemState::Queued;
            f.requested = i->service;
            f.service = d.service;
            // Un fichier seul ne doit jamais être rangé dans un dossier à son nom
            f.relPath = links.size() == 1 ? QFileInfo(d.filename).fileName() : d.filename;
            f.name = QFileInfo(d.filename).fileName();
            f.sourceLink = d.sourceLink;
            f.directUrl = d.url;
            f.size = d.size;
            f.status = QStringLiteral("En attente");
            f.added = QDateTime::currentDateTime();
            files << f;
        }
        QString summary = QStringLiteral("%1 fichier(s) ajouté(s) à la file").arg(files.size());
        if (!errors.isEmpty()) {
            summary += QStringLiteral(" — %1 erreur(s), voir l'info-bulle").arg(errors.size());
            i->error = errors.join(QLatin1Char('\n'));
        }
        i->serverProgress = 100;
        setStatus(id, ItemState::Completed, summary);
        m_model->insertAt(m_model->rowOf(id) + 1, files);   // regroupés sous le torrent
        if (removeRow)
            m_model->remove({id});
        schedule();
    });
    connect(job, &TorrentJob::failed, this, [this, id, job](const QString& message) {
        m_torrents.remove(id);
        job->deleteLater();
        if (DownloadItem* i = m_model->find(id)) {
            if (!job->torrentId().isEmpty()) {   // pour « Réessayer » : reprendre ce torrent
                i->torrentId = job->torrentId();
                i->service = job->service();
            }
            i->error = message;
            setStatus(id, ItemState::Failed, QStringLiteral("Échec : %1").arg(message));
        }
        schedule();
    });
    job->start();
}

void DownloadManager::submitTorrentSelection(const QString& itemId, const QStringList& fileIds)
{
    TorrentJob* job = m_torrents.value(itemId);
    if (!job)
        return;
    if (fileIds.isEmpty()) {
        m_torrents.remove(itemId);
        job->deleteLater();
        setStatus(itemId, ItemState::Canceled, QStringLiteral("Sélection annulée"));
        schedule();
        return;
    }
    setStatus(itemId, ItemState::TorrentProcessing, QStringLiteral("Sélection envoyée (%1 fichier(s))…").arg(fileIds.size()));
    job->selectFiles(fileIds);
}

// ---------------------------------------------------------------- Liens

void DownloadManager::startResolve(const QString& id, Service forced)
{
    DownloadItem* it = m_model->find(id);
    if (!it)
        return;
    auto* r = new LinkResolver(it->sourceLink, forced, this);
    m_resolvers.insert(id, r);
    connect(r, &LinkResolver::status, this, [this, id](const QString& text) {
        if (DownloadItem* i = m_model->find(id)) {
            i->status = text;
            m_model->touch(id);
        }
    });
    connect(r, &LinkResolver::resolved, this, [this, id, r](const DirectLink& d) {
        m_resolvers.remove(id);
        r->deleteLater();
        DownloadItem* i = m_model->find(id);
        if (!i)
            return;
        i->directUrl = d.url;
        i->service = d.service;
        if (i->relPath.isEmpty()) {
            i->relPath = d.filename;
            i->name = d.filename;
        }
        if (d.size > 0)
            i->size = d.size;
        i->error.clear();
        i->attempts = 0;   // les relances du débridage ne comptent pas pour le téléchargement
        setStatus(id, ItemState::Queued, QStringLiteral("En attente"));
        schedule();
    });
    connect(r, &LinkResolver::failed, this, [this, id, r](const QString& message, bool retryable) {
        m_resolvers.remove(id);
        r->deleteLater();
        if (DownloadItem* i = m_model->find(id)) {
            i->error = message;
            // Erreur passagère (réseau, hébergeur momentanément indisponible…) :
            // nouveau débridage programmé, comme pour un téléchargement.
            if (retryable) {
                i->directUrl.clear();   // un éventuel ancien lien direct a expiré
                if (scheduleRetry(id, message)) {
                    schedule();
                    return;
                }
            }
            setStatus(id, ItemState::Failed, QStringLiteral("Échec : %1").arg(message));
        }
        schedule();
    });
    r->start();
}

// ---------------------------------------------------------------- File

int DownloadManager::runningDownloads() const
{
    int n = 0;
    for (const auto& t : m_tasks)
        if (t)
            ++n;
    return n;
}

void DownloadManager::schedule()
{
    if (m_shuttingDown)
        return;
    int running = runningDownloads();
    QStringList toLaunch;
    for (const DownloadItem& it : m_model->items()) {
        if (running >= m_maxConcurrent)
            break;
        if (it.kind == ItemKind::File && it.state == ItemState::Queued && !it.directUrl.isEmpty()
            && !m_tasks.contains(it.id)) {
            toLaunch << it.id;
            ++running;
        }
    }
    for (const QString& id : toLaunch)
        launch(id);

    bool busy = false;
    int completed = 0, failed = 0;
    for (const DownloadItem& it : m_model->items()) {
        busy |= it.isBusy();
        if (it.kind == ItemKind::File) {
            completed += it.state == ItemState::Completed;
            failed += it.state == ItemState::Failed;
        }
    }
    if (m_wasBusy && !busy) {
        QString msg = QStringLiteral("%1 fichier(s) terminé(s)").arg(completed);
        if (failed)
            msg += QStringLiteral(", %1 en échec").arg(failed);
        emit notify(QStringLiteral("File de téléchargement terminée"), msg);
    }
    m_wasBusy = busy;
    m_extract->checkPending();   // archives « à extraire quand tout sera terminé »
    emitStats();
    requestSave();
}

void DownloadManager::launch(const QString& id)
{
    DownloadItem* it = m_model->find(id);
    if (!it)
        return;

    if (it->destPath.isEmpty()) {
        QSet<QString> reserved;
        for (const DownloadItem& o : m_model->items())
            if (o.id != id && !o.destPath.isEmpty() && o.state != ItemState::Canceled)
                reserved.insert(o.destPath);
        const QString wanted = it->relPath.isEmpty() ? it->name : it->relPath;
        const QString rel = Config::instance().keepTorrentTree()
            ? Utils::sanitizeRelativePath(wanted)
            : Utils::sanitizeFileName(QFileInfo(wanted).fileName());
        // Vidéos (et sous-titres) dans leur propre dossier si l'option est réglée
        const QString dir = Config::instance().destinationDirFor(QFileInfo(wanted).fileName());
        it->destPath = Utils::uniquePath(QDir(dir).filePath(rel), reserved);
    }

    const bool resuming = QFileInfo::exists(it->destPath + QStringLiteral(".part"));
    auto* task = new DownloadTask(QUrl(it->directUrl), it->destPath, this);
    m_tasks.insert(id, task);
    it->speed = 0;
    setStatus(id, ItemState::Downloading, resuming ? QStringLiteral("Reprise…") : QStringLiteral("Connexion…"));

    connect(task, &DownloadTask::progress, this, [this, id](qint64 received, qint64 total, double speed) {
        DownloadItem* i = m_model->find(id);
        if (!i)
            return;
        i->received = received;
        if (total > 0)
            i->size = total;
        i->speed = speed;
        i->status = i->size > 0
            ? QStringLiteral("%1 / %2").arg(Utils::humanSize(received), Utils::humanSize(i->size))
            : QStringLiteral("%1 reçus").arg(Utils::humanSize(received));
        m_model->touch(id);
    });
    connect(task, &DownloadTask::finished, this, [this, id, task](const QString& path) {
        m_tasks.remove(id);
        task->deleteLater();
        if (DownloadItem* i = m_model->find(id)) {
            i->destPath = path;
            i->size = qMax(i->size, i->received);
            i->received = i->size;
            i->speed = 0;
            i->attempts = 0;
            i->error.clear();
            setStatus(id, ItemState::Completed, QStringLiteral("Terminé"));
        }
        schedule();
    });
    connect(task, &DownloadTask::failed, this, [this, id, task](const QString& msg, int http, bool retryable) {
        m_tasks.remove(id);
        task->deleteLater();
        handleFailure(id, msg, http, retryable);
    });
    connect(task, &DownloadTask::paused, this, [this, id, task] {
        m_tasks.remove(id);
        task->deleteLater();
        if (DownloadItem* i = m_model->find(id)) {
            i->speed = 0;
            setStatus(id, ItemState::Paused, QStringLiteral("En pause"));
        }
        schedule();
    });
    connect(task, &DownloadTask::canceled, this, [this, id, task] {
        m_tasks.remove(id);
        task->deleteLater();
        if (DownloadItem* i = m_model->find(id)) {
            i->speed = 0;
            i->received = 0;
            setStatus(id, ItemState::Canceled, QStringLiteral("Annulé"));
        }
        if (m_removeAfterCancel.remove(id))
            m_model->remove({id});
        schedule();
    });
    task->start();
}

void DownloadManager::handleFailure(const QString& id, const QString& message, int httpStatus, bool retryable)
{
    DownloadItem* it = m_model->find(id);
    if (!it)
        return;
    it->speed = 0;
    it->error = message;

    // Lien direct expiré / refusé : on redemande un lien frais au service.
    const bool expired = httpStatus == 401 || httpStatus == 403 || httpStatus == 404 || httpStatus == 410;
    if (expired && !it->reresolved && it->service != Service::Direct && !it->sourceLink.isEmpty()) {
        it->reresolved = true;
        setStatus(id, ItemState::Resolving, QStringLiteral("Lien expiré, nouveau débridage…"));
        startResolve(id, it->service);
        schedule();
        return;
    }

    if (retryable && scheduleRetry(id, message)) {
        schedule();
        return;
    }

    setStatus(id, ItemState::Failed, QStringLiteral("Échec : %1").arg(message));
    schedule();
}

bool DownloadManager::scheduleRetry(const QString& id, const QString& message)
{
    DownloadItem* it = m_model->find(id);
    const int maxRetries = Config::instance().autoRetries();
    if (!it || it->attempts >= maxRetries)
        return false;
    it->attempts++;
    const int delay = qMin(60, 5 << (it->attempts - 1));   // 5, 10, 20, 40, 60 s
    setStatus(id, ItemState::WaitingRetry,
              QStringLiteral("%1 — nouvel essai dans %2 s (%3/%4)").arg(message).arg(delay).arg(it->attempts).arg(maxRetries));
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    m_retryTimers.insert(id, timer);
    connect(timer, &QTimer::timeout, this, [this, id, timer] {
        m_retryTimers.remove(id);
        timer->deleteLater();
        DownloadItem* i = m_model->find(id);
        if (!i || i->state != ItemState::WaitingRetry)
            return;
        if (i->directUrl.isEmpty()) {   // c'est le débridage qui avait échoué
            setStatus(id, ItemState::Resolving, QStringLiteral("Résolution…"));
            startResolve(id, i->requested);
        } else {
            setStatus(id, ItemState::Queued, QStringLiteral("En attente"));
        }
        schedule();
    });
    timer->start(delay * 1000);
    return true;
}

void DownloadManager::setStatus(const QString& id, ItemState state, const QString& status)
{
    if (DownloadItem* it = m_model->find(id)) {
        it->state = state;
        it->status = status;
        m_model->touch(id);
    }
}

void DownloadManager::dropWorkers(const QString& id)
{
    if (auto r = m_resolvers.take(id))
        r->deleteLater();
    if (auto t = m_torrents.take(id))
        t->deleteLater();
    if (auto timer = m_retryTimers.take(id)) {
        timer->stop();
        timer->deleteLater();
    }
}

// ---------------------------------------------------------------- Actions

void DownloadManager::pause(const QStringList& ids)
{
    for (const QString& id : ids) {
        DownloadItem* it = m_model->find(id);
        if (!it || it->kind != ItemKind::File)
            continue;
        if (auto task = m_tasks.value(id)) {
            task->pause();
        } else if (it->state == ItemState::Queued || it->state == ItemState::WaitingRetry) {
            dropWorkers(id);
            setStatus(id, ItemState::Paused, QStringLiteral("En pause"));
        }
    }
    schedule();
}

void DownloadManager::resume(const QStringList& ids)
{
    for (const QString& id : ids) {
        DownloadItem* it = m_model->find(id);
        if (!it)
            continue;
        const bool restartable = it->state == ItemState::Paused || it->state == ItemState::Failed
            || it->state == ItemState::Canceled;
        if (!restartable)
            continue;
        it->attempts = 0;
        it->reresolved = false;
        it->error.clear();
        if (it->kind == ItemKind::Torrent) {
            if (it->state != ItemState::Paused)
                startTorrent(id);
        } else if (it->directUrl.isEmpty()) {
            setStatus(id, ItemState::Resolving, QStringLiteral("Résolution…"));
            startResolve(id, it->requested);
        } else {
            setStatus(id, ItemState::Queued, QStringLiteral("En attente"));
        }
    }
    schedule();
}

void DownloadManager::cancel(const QStringList& ids, bool removeAfter)
{
    QStringList toRemove;
    for (const QString& id : ids) {
        DownloadItem* it = m_model->find(id);
        if (!it)
            continue;
        if (auto task = m_tasks.value(id)) {
            if (removeAfter)
                m_removeAfterCancel.insert(id);
            task->cancel();   // suite dans le gestionnaire "canceled"
            continue;
        }
        dropWorkers(id);
        if (it->state != ItemState::Completed) {
            if (!it->destPath.isEmpty())
                QFile::remove(it->destPath + QStringLiteral(".part"));
            it->received = 0;
            if (it->state != ItemState::Canceled)
                setStatus(id, ItemState::Canceled, QStringLiteral("Annulé"));
        }
        if (removeAfter)
            toRemove << id;
    }
    m_model->remove(toRemove);
    schedule();
}

void DownloadManager::remove(const QStringList& ids) { cancel(ids, true); }

void DownloadManager::moveToTop(const QStringList& ids)
{
    m_model->moveToEdge(ids, true);
    schedule();
}

void DownloadManager::moveToBottom(const QStringList& ids)
{
    m_model->moveToEdge(ids, false);
    schedule();
}

void DownloadManager::pauseAll()
{
    QStringList ids;
    for (const DownloadItem& it : m_model->items())
        ids << it.id;
    pause(ids);
}

void DownloadManager::resumeAll()
{
    QStringList ids;
    for (const DownloadItem& it : m_model->items())
        if (it.state == ItemState::Paused)
            ids << it.id;
    resume(ids);
}

void DownloadManager::retryFailed()
{
    QStringList ids;
    for (const DownloadItem& it : m_model->items())
        if (it.state == ItemState::Failed)
            ids << it.id;
    resume(ids);
}

void DownloadManager::cancelAll()
{
    QStringList ids;
    for (const DownloadItem& it : m_model->items())
        if (it.isBusy() || it.state == ItemState::Paused)
            ids << it.id;
    cancel(ids, false);
}

void DownloadManager::clearFinished()
{
    removeFinished();
    schedule();
}

void DownloadManager::removeFinished()
{
    QStringList ids;
    for (const DownloadItem& it : m_model->items()) {
        if (!it.isFinished())
            continue;
        // Une archive en cours d'extraction ou prévue reste dans la liste
        if (it.extractBusy() || it.extract == ExtractState::Pending)
            continue;
        if (it.state == ItemState::Failed && !it.destPath.isEmpty())
            QFile::remove(it.destPath + QStringLiteral(".part"));
        ids << it.id;
    }
    m_model->remove(ids);
}

void DownloadManager::setMaxConcurrent(int n)
{
    m_maxConcurrent = qBound(1, n, 10);
    Config::instance().set(QStringLiteral("max_concurrent_downloads"), m_maxConcurrent);
    schedule();
}

// ---------------------------------------------------------------- Stats

void DownloadManager::emitStats()
{
    double speed = 0;
    int downloading = 0, waiting = 0, completed = 0, failed = 0;
    for (const DownloadItem& it : m_model->items()) {
        switch (it.state) {
        case ItemState::Downloading:
            ++downloading;
            speed += it.speed;
            break;
        case ItemState::Completed:
            if (it.kind == ItemKind::File)
                ++completed;
            break;
        case ItemState::Failed:
            ++failed;
            break;
        default:
            if (it.isBusy())
                ++waiting;
        }
    }
    emit statsChanged(speed, downloading, waiting, completed, failed);
}

// ---------------------------------------------------------------- Session

void DownloadManager::requestSave()
{
    if (!m_shuttingDown)
        m_saveTimer.start();
}

void DownloadManager::saveSession()
{
    const bool autoResume = Config::instance().autoResume();
    QJsonArray arr;
    for (const DownloadItem& it : m_model->items()) {
        if (it.kind != ItemKind::File || it.state == ItemState::Canceled)
            continue;
        DownloadItem copy = it;
        switch (copy.state) {
        case ItemState::Downloading:
        case ItemState::Queued:
        case ItemState::WaitingRetry:
            copy.state = autoResume ? ItemState::Queued : ItemState::Paused;
            break;
        case ItemState::Resolving:
            copy.state = ItemState::Failed;
            copy.error = QStringLiteral("Interrompu à la fermeture");
            break;
        default:
            break;
        }
        arr.append(copy.toJson());
    }
    QSaveFile f(Config::instance().sessionPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void DownloadManager::loadSession()
{
    QFile f(Config::instance().sessionPath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    QList<DownloadItem> items;
    for (const QJsonValue& v : arr) {
        DownloadItem it = DownloadItem::fromJson(v.toObject());
        if (it.id.isEmpty())
            continue;
        if (it.state == ItemState::Queued && it.directUrl.isEmpty())
            it.state = ItemState::Failed;
        const QString part = it.destPath + QStringLiteral(".part");
        if (it.state != ItemState::Completed)
            it.received = QFileInfo::exists(part) ? QFileInfo(part).size() : 0;
        if (it.extractBusy()) {   // extraction interrompue par la fermeture du programme
            it.extract = ExtractState::Failed;
            it.extractStatus = QStringLiteral("Extraction interrompue (programme fermé) — à relancer");
        }
        switch (it.state) {
        case ItemState::Completed:
            it.status = QFileInfo::exists(it.destPath) ? QStringLiteral("Terminé") : QStringLiteral("Terminé (fichier déplacé ou supprimé)");
            break;
        case ItemState::Paused:
            it.status = QStringLiteral("En pause");
            break;
        case ItemState::Failed:
            it.status = QStringLiteral("Échec : %1").arg(it.error);
            break;
        default:
            it.state = ItemState::Queued;
            it.status = QStringLiteral("En attente");
        }
        items << it;
    }
    m_model->insertAt(0, items);
    schedule();
}

void DownloadManager::setSpeedLimit(qint64 bytesPerSecond)
{
    bytesPerSecond = qMax<qint64>(0, bytesPerSecond);
    Config::instance().set(QStringLiteral("speed_limit"), double(bytesPerSecond));
    DownloadTask::setSpeedLimit(bytesPerSecond);
}

void DownloadManager::shutdown()
{
    if (m_shuttingDown)
        return;   // déjà fait (fermeture puis fin du programme)
    m_shuttingDown = true;
    // Option « nettoyer la liste à la fermeture » : comme le bouton Nettoyer,
    // avant l'arrêt des extractions (une extraction en cours reste dans la
    // liste, pour être relancée au prochain démarrage).
    if (Config::instance().get(QStringLiteral("clear_list_on_exit"), false).toBool())
        removeFinished();
    m_extract->shutdown();
    m_saveTimer.stop();
    QStringList running;
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it)
        if (it.value())
            running << it.key();
    for (const QString& id : running)
        if (auto task = m_tasks.value(id))
            task->pause(true);
    // Les téléchargements interrompus par la fermeture seront repris au
    // prochain démarrage (si l'option est active).
    for (const QString& id : running)
        if (DownloadItem* it = m_model->find(id))
            it->state = ItemState::Queued;
    saveSession();
}
