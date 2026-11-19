#pragma once
// Saisie de plusieurs liens / magnets d'un coup (un par ligne ou en vrac).

#include "core/Types.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

class AddLinksDialog : public QDialog
{
    Q_OBJECT
public:
    AddLinksDialog(Service current, QWidget* parent = nullptr);
    QStringList links() const;
    Service service() const;

private:
    void refreshCount();

    QPlainTextEdit* m_edit = nullptr;
    QComboBox* m_service = nullptr;
    QLabel* m_count = nullptr;
    QPushButton* m_ok = nullptr;
};

// Remplit une liste déroulante avec les services (avec icônes)
void fillServiceCombo(QComboBox* combo, Service current, bool includeDirect = true);
