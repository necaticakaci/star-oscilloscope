// SPDX-License-Identifier: GPL-3.0-or-later
// Automatic measurements of the displayed waveforms, with running statistics. Every frame counts in the statistics;
// the numbers on the screen change a few times a second. The user chooses the columns and their order: by dragging
// the headings, from the headings' context menu, or in the Table Columns dialog.
#pragma once

#include <array>
#include <string_view>
#include <vector>

#include <QStringList>
#include <QTableWidget>

class QMenu;

#include "measurementselection.hpp"
#include "refreshlimiter.hpp"
#include "starosc/engine.hpp"
#include "starosc/measure.hpp"

class MeasurementTable : public QTableWidget {
    Q_OBJECT

public:
    explicit MeasurementTable(QWidget* parent = nullptr);

    void setFrame(const starosc::Frame& frame);
    void setSelection(const MeasurementSelection& selection);
    const MeasurementSelection& selection() const { return selection_; }
    // After a trace colour changed.
    void refreshColors();

    // The shown columns from left to right, by the names the settings file keeps (English); hidden ones left out.
    QStringList columns() const;
    // Shows these columns in this order and hides the others. Unknown names are skipped; Parameter is always shown.
    void setColumns(const QStringList& names);
    static QStringList defaultColumns();
    // A check box for each column, then Table Columns… and Restore Default Columns.
    void addColumnActions(QMenu* menu);

signals:
    void selectRequested();  // from the context menu
    void selectionChanged(const MeasurementSelection& selection);

public slots:
    void resetStatistics();
    void editColumns();  // the Table Columns dialog

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    // Sources of the rows: the traces of MeasurementSelection, then CH1 to CH2.
    static constexpr int kPairSource = MeasurementSelection::kTraces;
    static constexpr int kSources = kPairSource + 1;

    struct Row {
        int source;
        starosc::Parameter parameter;
        starosc::Statistics stats;
        double value = starosc::Measurements::kNone;  // of the last frame
        QString note;                                  // shown instead of the value
    };

    void rebuild(const std::array<bool, kSources>& enabled);
    // Columns by their index, left to right, each shown or hidden.
    void arrange(const std::vector<int>& order, const std::vector<bool>& shown);
    void removeMeasurement(int row);
    QString rowName(const Row& row) const;
    void setSourceIcon(int row);
    void setCell(int row, int column, const QString& text);
    void showValues();

    MeasurementSelection selection_ = MeasurementSelection::defaults();
    std::vector<Row> rows_;
    std::array<bool, kSources> shown_{};
    std::string_view mathUnit_ = "V";
    RefreshLimiter refresh_{[this] { showValues(); }};
};
