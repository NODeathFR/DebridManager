#pragma once
// Panneau « Détails » sous la file : informations complètes sur l'élément
// sélectionné (lien, destination, progression, erreur…) et actions rapides.

#include "ui/Widgets.h"

class DownloadManager;
class ElidedLabel;
class QLabel;
class QPushButton;

class DetailsPanel : public Card
{
    Q_OBJECT
public:
    DetailsPanel(DownloadManager* manager, QWidget* parent = nullptr);

    // Ids sélectionnés (seul le premier est détaillé quand il n'y en a qu'un)
    void setSelection(const QStringList& ids);
    void refresh();
    const QString& currentId() const { return m_id; }

signals:
    void revealRequested(const QString& path);

private:
    ElidedLabel* addField(int row, int col, const QString& label);

    DownloadManager* m_manager;
    QString m_id;
    int m_count = 0;
    QLabel* m_title = nullptr;
    ElidedLabel* m_name = nullptr;
    QWidget* m_body = nullptr;
    QLabel* m_hint = nullptr;
    ElidedLabel* m_source = nullptr;
    ElidedLabel* m_dest = nullptr;
    ElidedLabel* m_progress = nullptr;
    ElidedLabel* m_speed = nullptr;
    ElidedLabel* m_origin = nullptr;
    ElidedLabel* m_added = nullptr;
    QLabel* m_error = nullptr;
    QPushButton* m_open = nullptr;
    QPushButton* m_reveal = nullptr;
    QPushButton* m_copy = nullptr;
    QPushButton* m_top = nullptr;
};
