#include "ui/AddLinksDialog.h"

#include "core/Utils.h"
#include "ui/Icons.h"
#include "ui/Widgets.h"

#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

void fillServiceCombo(QComboBox* combo, Service current, bool includeDirect)
{
    combo->clear();
    combo->setIconSize(QSize(18, 18));
    combo->addItem(Icons::icon(QStringLiteral("auto")), QStringLiteral("Auto (détection + bascule RD ⇄ AD)"), int(Service::Auto));
    combo->addItem(Icons::icon(QStringLiteral("server")), QStringLiteral("RealDebrid"), int(Service::RealDebrid));
    combo->addItem(Icons::icon(QStringLiteral("server")), QStringLiteral("AllDebrid"), int(Service::AllDebrid));
    if (includeDirect)
        combo->addItem(Icons::icon(QStringLiteral("link")), QStringLiteral("Lien direct (sans débridage)"), int(Service::Direct));
    const int idx = combo->findData(int(current));
    combo->setCurrentIndex(idx < 0 ? 0 : idx);
}

AddLinksDialog::AddLinksDialog(Service current, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Ajouter des liens"));
    resize(680, 420);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    auto* intro = new QLabel(QStringLiteral("Collez un ou plusieurs liens d'hébergeurs, liens directs ou magnets. "
                                            "Ils peuvent être mélangés à du texte : seuls les liens sont retenus."), this);
    intro->setWordWrap(true);
    intro->setProperty("dim", true);
    root->addWidget(intro);

    m_edit = new QPlainTextEdit(this);
    m_edit->setPlaceholderText(QStringLiteral("https://1fichier.com/?abc…\nmagnet:?xt=urn:btih:…"));
    m_edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    root->addWidget(m_edit, 1);

    const QStringList fromClipboard = Utils::extractLinks(QGuiApplication::clipboard()->text());
    if (!fromClipboard.isEmpty())
        m_edit->setPlainText(fromClipboard.join(QLatin1Char('\n')));

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("Service :"), this));
    m_service = new QComboBox(this);
    fillServiceCombo(m_service, current);
    row->addWidget(m_service);
    m_count = new QLabel(this);
    row->addWidget(m_count, 1, Qt::AlignRight);
    root->addLayout(row);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = makeButton(QStringLiteral("Annuler"), QStringLiteral("cancel"));
    m_ok = makeButton(QStringLiteral("Ajouter à la file"), QStringLiteral("plus"), {}, "accent");
    m_ok->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(m_ok);
    root->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_edit, &QPlainTextEdit::textChanged, this, &AddLinksDialog::refreshCount);
    refreshCount();
}

void AddLinksDialog::refreshCount()
{
    const int n = links().size();
    m_count->setText(n ? QStringLiteral("<b>%1</b> lien(s) détecté(s)").arg(n) : QStringLiteral("Aucun lien détecté"));
    m_ok->setEnabled(n > 0);
}

QStringList AddLinksDialog::links() const { return Utils::extractLinks(m_edit->toPlainText()); }
Service AddLinksDialog::service() const { return Service(m_service->currentData().toInt()); }
