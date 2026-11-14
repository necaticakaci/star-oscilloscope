// SPDX-License-Identifier: GPL-3.0-or-later
#include "probebox.hpp"

#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

#include <QLineEdit>

namespace {

constexpr double kPresets[] = {1.0, 10.0, 100.0};

QString label(double probe) { return QStringLiteral("×") + QString::number(probe, 'g', 6); }

// "10.15", "10,15" or "×10.15".
std::optional<double> parse(QString text)
{
    text.remove(QChar(0x00d7));
    text.remove(QLatin1Char('x'), Qt::CaseInsensitive);
    text.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double value = text.trimmed().toDouble(&ok);
    if (!ok || value < starosc::kMinProbe || value > starosc::kMaxProbe)
        return std::nullopt;
    return value;
}

}  // namespace

bool ProbeBox::isPreset(double probe) { return std::ranges::find(kPresets, probe) != std::end(kPresets); }

ProbeBox::ProbeBox(QWidget* parent) : QComboBox(parent)
{
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
    setCompleter(nullptr);
    setStatusTip(tr("Attenuation of the probe. Type a measured value for more accuracy, for example 10.15."));
    connect(this, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!updating_ && index >= 0)
            emit probeChosen(itemData(index).toDouble());
    });
    connect(lineEdit(), &QLineEdit::editingFinished, this, &ProbeBox::readText);
    showProbe(shown_);
}

void ProbeBox::readText()
{
    if (updating_)
        return;
    if (const auto probe = parse(currentText()))
        emit probeChosen(*probe);
    showProbe(shown_);  // back to the setting when the text was no factor, and in its usual form when it was
}

void ProbeBox::showProbe(const starosc::ChannelSettings& channel)
{
    shown_ = channel;
    const bool wasUpdating = std::exchange(updating_, true);
    std::vector<double> items(std::begin(kPresets), std::end(kPresets));
    if (channel.custom_probe > 0 && !isPreset(channel.custom_probe))
        items.push_back(channel.custom_probe);
    if (!isPreset(channel.probe) && channel.probe != channel.custom_probe)
        items.push_back(channel.probe);
    std::ranges::sort(items);
    bool same = count() == static_cast<int>(items.size());
    for (int i = 0; same && i < count(); ++i)
        same = itemData(i).toDouble() == items[static_cast<std::size_t>(i)];
    if (!same) {
        clear();
        for (double item : items)
            addItem(label(item), item);
    }
    setCurrentIndex(findData(channel.probe));
    setEditText(label(channel.probe));
    updating_ = wasUpdating;
}
