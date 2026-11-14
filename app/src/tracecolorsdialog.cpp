// SPDX-License-Identifier: GPL-3.0-or-later
#include "tracecolorsdialog.hpp"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

TraceColorsDialog::TraceColorsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Trace Colors"));
    auto* form = new QFormLayout;
    const std::array names{tr("CH1"), tr("CH2"), tr("Math"), tr("R1"), tr("R2"), tr("X-Y")};
    for (int t = 0; t < display_style::kTraces; ++t) {
        auto* button = new QToolButton(this);
        button->setIconSize({48, 14});
        buttons_[static_cast<std::size_t>(t)] = button;
        connect(button, &QToolButton::clicked, this, [this, t] { choose(t); });
        form->addRow(names[static_cast<std::size_t>(t)], button);
        setColor(t, display_style::chosenColor(t));
    }

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this] {
        for (int t = 0; t < display_style::kTraces; ++t)
            setColor(t, display_style::defaultColor(t));
    });

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void TraceColorsDialog::setColor(int trace, const QColor& color)
{
    colors_[static_cast<std::size_t>(trace)] = color;
    QPixmap pixmap(48, 14);
    pixmap.fill(color);
    buttons_[static_cast<std::size_t>(trace)]->setIcon(QIcon(pixmap));
}

void TraceColorsDialog::choose(int trace)
{
    const QColor color = QColorDialog::getColor(this->color(trace), this, tr("Trace Color"));
    if (color.isValid())
        setColor(trace, color);
}

void TraceColorsDialog::apply() const
{
    for (int t = 0; t < display_style::kTraces; ++t)
        display_style::setChosenColor(t, color(t));
}
