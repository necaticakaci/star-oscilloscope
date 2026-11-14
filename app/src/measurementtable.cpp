// SPDX-License-Identifier: GPL-3.0-or-later
#include "measurementtable.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QMenu>

#include "columnsdialog.hpp"
#include "displaystyle.hpp"
#include "starosc/math.hpp"
#include "starosc/scale.hpp"

namespace {

enum Column { Source, Parameter, Value, Min, Max, Mean, StdDev, Count, ColumnCount };

// The columns' names in the settings file: English in every language, so a change of language keeps the choice.
constexpr std::array<std::string_view, ColumnCount> kColumnKeys{"Source", "Parameter", "Value",   "Min",
                                                                "Max",    "Mean",      "Std dev", "Count"};

}  // namespace

MeasurementTable::MeasurementTable(QWidget* parent) : QTableWidget(0, ColumnCount, parent)
{
    setHorizontalHeaderLabels(
        {tr("Source"), tr("Parameter"), tr("Value"), tr("Min"), tr("Max"), tr("Mean"), tr("Std dev"), tr("Count")});
    verticalHeader()->hide();
    QHeaderView* header = horizontalHeader();
    header->setHighlightSections(false);
    header->setStretchLastSection(false);
    header->setSectionsMovable(true);
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QHeaderView::customContextMenuRequested, this, [this](const QPoint& at) {
        QMenu menu(this);
        addColumnActions(&menu);
        menu.exec(horizontalHeader()->viewport()->mapToGlobal(at));
    });
    for (int c = 0; c < ColumnCount; ++c)
        horizontalHeaderItem(c)->setStatusTip(tr("Drag to move the column; right-click to choose the columns"));
    header->setSectionResizeMode(Source, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(Parameter, QHeaderView::ResizeToContents);
    const QFontMetrics fixed(display_style::fixedFont());
    for (int c = Value; c < Count; ++c) {
        header->setSectionResizeMode(c, QHeaderView::Fixed);
        header->resizeSection(c, fixed.horizontalAdvance(QStringLiteral("-000.00 mV")) + 16);
    }
    header->setSectionResizeMode(Count, QHeaderView::Fixed);
    header->resizeSection(Count, fixed.horizontalAdvance(QStringLiteral("0000000")) + 16);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setAlternatingRowColors(true);
    setWordWrap(false);
    setShowGrid(false);
    verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 4);
}

void MeasurementTable::rebuild(const std::array<bool, kSources>& enabled)
{
    rows_.clear();
    for (int source = 0; source < kSources; ++source) {
        if (!enabled[source])
            continue;
        const auto& list = source == kPairSource ? selection_.pair : selection_.trace[static_cast<std::size_t>(source)];
        for (auto p : list)
            rows_.push_back({source, p, {}, starosc::Measurements::kNone, {}});
    }
    shown_ = enabled;
    refresh_.restart();

    setRowCount(static_cast<int>(rows_.size()));
    for (int r = 0; r < rowCount(); ++r) {
        setCell(r, Source, rowName(rows_[r]));
        setSourceIcon(r);
        setCell(r, Parameter, parameterName(rows_[r].parameter));
        for (int c = Value; c < ColumnCount; ++c)
            setCell(r, c, QString());
    }
}

QString MeasurementTable::rowName(const Row& row) const
{
    if (row.source == kPairSource)
        return tr("CH1→CH2");
    return row.source == starosc::kChannels ? tr("Math") : tr("CH%1").arg(row.source + 1);
}

void MeasurementTable::setSourceIcon(int row)
{
    const int source = rows_[static_cast<std::size_t>(row)].source;
    item(row, Source)->setIcon(source == kPairSource ? QIcon() : display_style::swatch(source));
}

void MeasurementTable::refreshColors()
{
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r)
        setSourceIcon(r);
}

void MeasurementTable::setSelection(const MeasurementSelection& selection)
{
    selection_ = selection;
    rebuild(shown_);
    emit selectionChanged(selection_);
}

void MeasurementTable::removeMeasurement(int row)
{
    const Row& r = rows_[static_cast<std::size_t>(row)];
    auto& list = r.source == kPairSource ? selection_.pair : selection_.trace[static_cast<std::size_t>(r.source)];
    list.erase(std::remove(list.begin(), list.end(), r.parameter), list.end());
    rows_.erase(rows_.begin() + row);
    removeRow(row);
    emit selectionChanged(selection_);
}

void MeasurementTable::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    const int row = rowAt(event->y());
    if (row >= 0 && row < static_cast<int>(rows_.size())) {
        const Row& r = rows_[static_cast<std::size_t>(row)];
        menu.addAction(tr("Remove %1 %2").arg(rowName(r), parameterName(r.parameter)),
                       this, [this, row] { removeMeasurement(row); });
        menu.addSeparator();
    }
    menu.addAction(tr("Select Measurements…"), this, &MeasurementTable::selectRequested);
    menu.addAction(tr("Reset Statistics"), this, &MeasurementTable::resetStatistics);
    menu.addSeparator();
    menu.addAction(tr("Table Columns…"), this, &MeasurementTable::editColumns);
    menu.exec(event->globalPos());
}

QStringList MeasurementTable::defaultColumns()
{
    QStringList names;
    for (std::string_view key : kColumnKeys)
        names << QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size()));
    return names;
}

QStringList MeasurementTable::columns() const
{
    const QStringList keys = defaultColumns();
    const QHeaderView* header = horizontalHeader();
    QStringList names;
    for (int visual = 0; visual < ColumnCount; ++visual)
        if (const int column = header->logicalIndex(visual); !isColumnHidden(column))
            names << keys[column];
    return names;
}

void MeasurementTable::setColumns(const QStringList& names)
{
    const QStringList keys = defaultColumns();
    std::vector<int> order;
    for (const QString& name : names) {
        int column = -1;
        for (int c = 0; c < ColumnCount && column < 0; ++c)
            if (keys[c].compare(name.trimmed(), Qt::CaseInsensitive) == 0)
                column = c;
        if (column >= 0 && std::find(order.begin(), order.end(), column) == order.end())
            order.push_back(column);
    }
    if (std::find(order.begin(), order.end(), static_cast<int>(Parameter)) == order.end())
        order.insert(order.begin() + (!order.empty() && order.front() == Source ? 1 : 0), Parameter);
    std::vector<bool> shown(order.size(), true);
    for (int column = 0; column < ColumnCount; ++column)
        if (std::find(order.begin(), order.end(), column) == order.end()) {
            order.push_back(column);
            shown.push_back(false);
        }
    arrange(order, shown);
}

void MeasurementTable::arrange(const std::vector<int>& order, const std::vector<bool>& shown)
{
    QHeaderView* header = horizontalHeader();
    for (std::size_t visual = 0; visual < order.size(); ++visual) {
        header->moveSection(header->visualIndex(order[visual]), static_cast<int>(visual));
        setColumnHidden(order[visual], !shown[visual] && order[visual] != Parameter);
    }
}

void MeasurementTable::addColumnActions(QMenu* menu)
{
    const QHeaderView* header = horizontalHeader();
    for (int visual = 0; visual < ColumnCount; ++visual) {
        const int column = header->logicalIndex(visual);
        QAction* action = menu->addAction(horizontalHeaderItem(column)->text());
        action->setCheckable(true);
        action->setChecked(!isColumnHidden(column));
        action->setEnabled(column != Parameter);
        connect(action, &QAction::toggled, this, [this, column](bool on) { setColumnHidden(column, !on); });
    }
    menu->addSeparator();
    menu->addAction(tr("Table Columns…"), this, &MeasurementTable::editColumns);
    menu->addAction(tr("Restore Default Columns"), this, [this] { setColumns(defaultColumns()); });
}

void MeasurementTable::editColumns()
{
    std::vector<ColumnsDialog::Column> current;
    std::vector<ColumnsDialog::Column> defaults;
    const QHeaderView* header = horizontalHeader();
    for (int visual = 0; visual < ColumnCount; ++visual) {
        const int column = header->logicalIndex(visual);
        current.push_back({column, horizontalHeaderItem(column)->text(), !isColumnHidden(column), column == Parameter});
    }
    for (int column = 0; column < ColumnCount; ++column)
        defaults.push_back({column, horizontalHeaderItem(column)->text(), true, column == Parameter});
    ColumnsDialog dialog(current, defaults, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    std::vector<int> order;
    std::vector<bool> shown;
    for (const ColumnsDialog::Column& c : dialog.columns()) {
        order.push_back(c.id);
        shown.push_back(c.shown);
    }
    arrange(order, shown);
}

void MeasurementTable::setCell(int row, int column, const QString& text)
{
    QTableWidgetItem* cell = item(row, column);
    if (!cell) {
        cell = new QTableWidgetItem;
        if (column >= Value) {
            cell->setFont(display_style::fixedFont());
            cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }
        setItem(row, column, cell);
    }
    cell->setText(text);
}

void MeasurementTable::setFrame(const starosc::Frame& frame)
{
    std::array<bool, kSources> enabled{};
    for (int ch = 0; ch < starosc::kChannels; ++ch)
        enabled[ch] = !frame.volts[ch].empty();
    enabled[starosc::kChannels] = !frame.math.empty();
    enabled[kPairSource] = enabled[0] && enabled[1];
    if (enabled != shown_)
        rebuild(enabled);

    mathUnit_ = starosc::math_unit(frame.settings.math.operation);
    for (Row& row : rows_) {
        const bool math = row.source == starosc::kChannels;
        row.value = starosc::Measurements::kNone;
        row.note.clear();
        if (row.source == kPairSource) {
            row.value = frame.pair.value(row.parameter);
        } else {
            const starosc::Measurements& m = math ? frame.math_measurements : frame.measurements[row.source];
            row.value = m.value(row.parameter);
            // The edge is faster than the samples can show, so its time only has an upper limit, which is no
            // value for the statistics.
            if (m.unresolved(row.parameter)) {
                row.note = "< " + QString::fromStdString(starosc::format_si(m.resolution_s, "s", 3));
                row.value = starosc::Measurements::kNone;
            }
        }
        row.stats.add(row.value);
    }
    refresh_.request();
}

void MeasurementTable::showValues()
{
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r) {
        const Row& row = rows_[static_cast<std::size_t>(r)];
        const std::string_view unit = row.source == starosc::kChannels ? mathUnit_ : std::string_view("V");
        setCell(r, Value, row.note.isEmpty() ? formatMeasurement(row.parameter, row.value, unit) : row.note);
        item(r, Value)->setStatusTip(row.note.isEmpty() ? QString()
                                                      : tr("The edge takes too few samples to time; choose a faster timebase."));
        setCell(r, Min, formatMeasurement(row.parameter, row.stats.min(), unit));
        setCell(r, Max, formatMeasurement(row.parameter, row.stats.max(), unit));
        setCell(r, Mean, formatMeasurement(row.parameter, row.stats.mean(), unit));
        setCell(r, StdDev, formatMeasurement(row.parameter, row.stats.stddev(), unit));
        setCell(r, Count, QString::number(row.stats.count()));
    }
}

void MeasurementTable::resetStatistics()
{
    for (Row& row : rows_)
        row.stats.reset();
    // At once, also while stopped, when no frame would come to show it.
    refresh_.restart();
    showValues();
}
