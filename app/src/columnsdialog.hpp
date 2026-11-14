// SPDX-License-Identifier: GPL-3.0-or-later
// Chooses which columns of a table are shown and in what order: a list with a check box for each column, Move Up and
// Move Down, and dragging within the list.
#pragma once

#include <vector>

#include <QDialog>
#include <QString>

class QListWidget;
class QPushButton;

class ColumnsDialog : public QDialog {
    Q_OBJECT

public:
    struct Column {
        int id = 0;
        QString title;
        bool shown = true;
        bool fixed = false;  // always shown
    };

    // `columns` in their current order; `defaults` what Restore Defaults returns to.
    ColumnsDialog(const std::vector<Column>& columns, std::vector<Column> defaults, QWidget* parent = nullptr);

    // In the chosen order.
    std::vector<Column> columns() const;

private:
    void showColumns(const std::vector<Column>& columns);
    void move(int step);
    void updateButtons();

    std::vector<Column> defaults_;
    QListWidget* list_ = nullptr;
    QPushButton* up_ = nullptr;
    QPushButton* down_ = nullptr;
};
