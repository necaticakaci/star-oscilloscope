// SPDX-License-Identifier: GPL-3.0-or-later
#include "columnsdialog.hpp"

#include <utility>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr int kFixedRole = Qt::UserRole + 1;

}  // namespace

ColumnsDialog::ColumnsDialog(const std::vector<Column>& columns, std::vector<Column> defaults, QWidget* parent)
    : QDialog(parent), defaults_(std::move(defaults))
{
    setWindowTitle(tr("Table Columns"));
    auto* layout = new QVBoxLayout(this);
    auto* note = new QLabel(tr("Checked columns are shown, from left to right in this order. "
                               "The column headings of the table can be dragged too."),
                            this);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* row = new QHBoxLayout;
    list_ = new QListWidget(this);
    list_->setObjectName("columnList");
    list_->setDragDropMode(QAbstractItemView::InternalMove);
    list_->setDefaultDropAction(Qt::MoveAction);
    row->addWidget(list_, 1);
    auto* side = new QVBoxLayout;
    up_ = new QPushButton(tr("Move &Up"), this);
    up_->setObjectName("moveUp");
    down_ = new QPushButton(tr("Move &Down"), this);
    down_->setObjectName("moveDown");
    side->addWidget(up_);
    side->addWidget(down_);
    side->addStretch(1);
    row->addLayout(side);
    layout->addLayout(row);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    layout->addWidget(buttons);

    connect(up_, &QPushButton::clicked, this, [this] { move(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { move(1); });
    connect(list_, &QListWidget::currentRowChanged, this, &ColumnsDialog::updateButtons);
    connect(list_->model(), &QAbstractItemModel::rowsMoved, this, &ColumnsDialog::updateButtons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            [this] { showColumns(defaults_); });
    showColumns(columns);
}

void ColumnsDialog::showColumns(const std::vector<Column>& columns)
{
    list_->clear();
    for (const Column& c : columns) {
        auto* item = new QListWidgetItem(c.title, list_);
        item->setData(kIdRole, c.id);
        item->setData(kFixedRole, c.fixed);
        // Not a drop target: a dragged column goes between two others, never onto one.
        Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled;
        if (!c.fixed)
            flags |= Qt::ItemIsUserCheckable;
        item->setFlags(flags);
        item->setCheckState(c.shown || c.fixed ? Qt::Checked : Qt::Unchecked);
    }
    list_->setCurrentRow(0);
    updateButtons();
}

std::vector<ColumnsDialog::Column> ColumnsDialog::columns() const
{
    std::vector<Column> out;
    for (int i = 0; i < list_->count(); ++i) {
        const QListWidgetItem* item = list_->item(i);
        const bool fixed = item->data(kFixedRole).toBool();
        out.push_back({item->data(kIdRole).toInt(), item->text(), fixed || item->checkState() == Qt::Checked, fixed});
    }
    return out;
}

void ColumnsDialog::move(int step)
{
    const int row = list_->currentRow();
    const int to = row + step;
    if (row < 0 || to < 0 || to >= list_->count())
        return;
    QListWidgetItem* item = list_->takeItem(row);
    list_->insertItem(to, item);
    list_->setCurrentRow(to);
}

void ColumnsDialog::updateButtons()
{
    const int row = list_->currentRow();
    up_->setEnabled(row > 0);
    down_->setEnabled(row >= 0 && row < list_->count() - 1);
}
