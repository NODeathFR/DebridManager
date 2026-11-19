#include "ui/DetailsPanel.h"

#include "core/Config.h"
#include "core/DownloadManager.h"
#include "core/Utils.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

DetailsPanel::DetailsPanel(DownloadManager* manager, QWidget* parent)
    : Card(parent, "DetailsCard"), m_manager(manager)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 12);
    root->setSpacing(10);

    // Bandeau de titre
    auto* head = new QFrame(this);
    head->setObjectName(QStringLiteral("DetailsHead"));
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(12, 6, 12, 6);
    hl->setSpacing(8);
    auto* icon = new QLabel(head);
    icon->setPixmap(Icons::pixmap(QStringLiteral("list"), 16, devicePixelRatioF()));
    m_title = new QLabel(QStringLiteral("Détails"), head);
    m_title->setObjectName(QStringLiteral("DetailsTitle"));
    m_name = new ElidedLabel(head);
    m_name->setColor(QColor(0x9a, 0xa3, 0xb2));
    hl->addWidget(icon);
    hl->addWidget(m_title);
    hl->addSpacing(12);
    hl->addWidget(m_name, 1);
    root->addWidget(head);

    // Champs sur deux colonnes
    m_body = new QWidget(this);
    auto* grid = new QGridLayout(m_body);
    grid->setContentsMargins(16, 0, 16, 0);
    grid->setHorizontalSpacing(28);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    m_source = addField(0, 0, QStringLiteral("Lien d'origine"));
    m_dest = addField(0, 1, QStringLiteral("Destination"));
    m_progress = addField(2, 0, QStringLiteral("Progression"));
    m_speed = addField(2, 1, QStringLiteral("Vitesse"));
    m_origin = addField(4, 0, QStringLiteral("Service"));
    m_added = addField(4, 1, QStringLiteral("Ajouté"));
    root->addWidget(m_body);

    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("DetailsError"));
    m_error->setWordWrap(true);
    m_error->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_error->setContentsMargins(16, 0, 16, 0);
    m_error->hide();
    root->addWidget(m_error);

    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(16, 0, 16, 0);
    buttons->setSpacing(6);
    m_open = makeButton(QStringLiteral("Ouvrir le fichier"), QStringLiteral("file"));
    m_reveal = makeButton(QStringLiteral("Afficher dans le dossier"), QStringLiteral("folder-open"));
    m_copy = makeButton(QStringLiteral("Copier le lien"), QStringLiteral("copy"));
    m_top = makeButton(QStringLiteral("Passer en tête de file"), QStringLiteral("arrow-top"));
    for (QPushButton* b : {m_open, m_reveal, m_copy, m_top})
        buttons->addWidget(b);
    buttons->addStretch();
    root->addLayout(buttons);

    connect(m_open, &QPushButton::clicked, this, [this] {
        if (const DownloadItem* it = m_manager->model()->find(m_id))
            QDesktopServices::openUrl(QUrl::fromLocalFile(it->destPath));
    });
    connect(m_reveal, &QPushButton::clicked, this, [this] {
        if (const DownloadItem* it = m_manager->model()->find(m_id))
            emit revealRequested(it->extract == ExtractState::Done && QFileInfo(it->extractDir).isDir() ? it->extractDir
                                                                                                         : it->destPath);
    });
    connect(m_copy, &QPushButton::clicked, this, [this] {
        if (const DownloadItem* it = m_manager->model()->find(m_id))
            QGuiApplication::clipboard()->setText(it->sourceLink.isEmpty() ? it->directUrl : it->sourceLink);
    });
    connect(m_top, &QPushButton::clicked, this, [this] { m_manager->moveToTop({m_id}); });

    setSelection({});
}

ElidedLabel* DetailsPanel::addField(int row, int col, const QString& label)
{
    auto* grid = static_cast<QGridLayout*>(m_body->layout());
    auto* key = new QLabel(label, m_body);
    key->setProperty("dim", true);
    auto* value = new ElidedLabel(m_body);
    grid->addWidget(key, row, col);
    grid->addWidget(value, row + 1, col);
    return value;
}

void DetailsPanel::setSelection(const QStringList& ids)
{
    m_count = ids.size();
    m_id = ids.size() == 1 ? ids.first() : QString();
    refresh();
}

void DetailsPanel::refresh()
{
    const DownloadItem* it = m_id.isEmpty() ? nullptr : m_manager->model()->find(m_id);
    if (!it) {
        m_name->setText(m_count > 1 ? QStringLiteral("%1 éléments sélectionnés").arg(m_count)
                                    : QStringLiteral("Sélectionnez un téléchargement pour voir ses détails"));
        for (ElidedLabel* l : {m_source, m_dest, m_progress, m_speed, m_origin, m_added})
            l->setText(QStringLiteral("—"));
        m_progress->setColor(Theme::C::text);
        m_error->hide();
        for (QPushButton* b : {m_open, m_reveal, m_copy, m_top})
            b->setEnabled(false);
        return;
    }

    m_name->setText(it->name);
    if (it->sourceLink.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive))
        m_source->setText(it->sourceLink.left(80) + QStringLiteral("…"));
    else if (!it->sourceLink.isEmpty())
        m_source->setText(it->sourceLink);
    else if (!it->torrentFileName.isEmpty())
        m_source->setText(QStringLiteral("Fichier %1").arg(it->torrentFileName));
    else
        m_source->setText(QStringLiteral("—"));

    m_dest->setText(it->destPath.isEmpty() ? (it->kind == ItemKind::Torrent ? QStringLiteral("—")
                                                                              : QStringLiteral("Choisie au lancement du téléchargement"))
                                           : QDir::toNativeSeparators(it->destPath));

    const int pct = it->percent();
    QString progress;
    if (it->kind == ItemKind::File && it->size > 0)
        progress = QStringLiteral("%1 sur %2 (%3 %)").arg(Utils::humanSize(it->received), Utils::humanSize(it->size)).arg(qMax(0, pct));
    else if (pct >= 0)
        progress = QStringLiteral("%1 %").arg(pct);
    const QString status = it->state == ItemState::Completed && it->extract != ExtractState::None ? it->extractStatus : it->status;
    // En téléchargement, le statut ne fait que répéter la progression
    if (progress.isEmpty())
        m_progress->setText(status);
    else if (it->state == ItemState::Downloading || status.isEmpty())
        m_progress->setText(progress);
    else
        m_progress->setText(QStringLiteral("%1 · %2").arg(progress, status));
    m_progress->setColor(Theme::stateColor(int(it->state)));

    if (it->state == ItemState::Downloading && it->speed > 1) {
        QString s = Utils::humanSpeed(it->speed);
        if (it->size > it->received)
            s += QStringLiteral(" · reste %1").arg(Utils::humanDuration(qint64(double(it->size - it->received) / it->speed)));
        m_speed->setText(s);
    } else {
        m_speed->setText(QStringLiteral("—"));
    }

    QString origin = serviceLabel(it->service);
    if (!it->parentId.isEmpty())
        if (const DownloadItem* parent = m_manager->model()->find(it->parentId))
            origin += QStringLiteral(" · torrent « %1 »").arg(parent->name);
    m_origin->setText(origin);

    QString added = it->added.isValid() ? QLocale().toString(it->added, QStringLiteral("dd/MM/yyyy à HH:mm")) : QStringLiteral("—");
    if (it->attempts > 0)
        added += QStringLiteral(" · essai %1 sur %2").arg(it->attempts).arg(Config::instance().autoRetries());
    m_added->setText(added);

    m_error->setText(it->error);
    m_error->setVisible(!it->error.isEmpty());

    const bool done = it->state == ItemState::Completed && it->kind == ItemKind::File;
    m_open->setEnabled(done && QFileInfo::exists(it->destPath));
    m_reveal->setEnabled(!it->destPath.isEmpty());
    m_copy->setEnabled(!it->sourceLink.isEmpty() || !it->directUrl.isEmpty());
    m_top->setEnabled(!it->isFinished());
}
