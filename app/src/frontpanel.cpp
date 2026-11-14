// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontpanel.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include "displaystyle.hpp"
#include "knob.hpp"
#include "probebox.hpp"
#include "starosc/math.hpp"
#include "starosc/scale.hpp"
#include "starosc/zoom.hpp"

namespace {

constexpr int kStripHeight = 3;
constexpr int kKeyHeight = 22;
constexpr int kKeyMargin = 10;
constexpr int kProbeChars = 5;
constexpr int kLightSize = 8;
constexpr double kSensitivityStep = 0.1;
constexpr int kSensitivitySteps = 10;
constexpr double kPositionStep = 0.1;
constexpr double kPositionResolution = 0.01;

// Set in full, since a child would otherwise take what is not set from its section's title.
QFont smallFont()
{
    QFont f = QApplication::font();
    if (f.pointSizeF() > 1)
        f.setPointSizeF(f.pointSizeF() - 1);
    f.setBold(false);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    return f;
}

QFont keyFont()
{
    QFont f = smallFont();
    f.setBold(true);
    return f;
}

QFont titleFont()
{
    QFont f = keyFont();
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1);
    return f;
}

// A key of the panel; a channel's has a strip of the trace's colour along its lower edge.
class PanelKey : public QToolButton {
public:
    using QToolButton::QToolButton;
    QSize sizeHint() const override { return {fontMetrics().horizontalAdvance(text()) + kKeyMargin, kKeyHeight}; }
    QSize minimumSizeHint() const override { return sizeHint(); }
    void setStrip(const QColor& color)
    {
        strip_ = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QToolButton::paintEvent(event);
        if (!strip_.isValid() || !isEnabled())
            return;
        QPainter p(this);
        p.fillRect(QRect(3, height() - kStripHeight - 2, width() - 6, kStripHeight), strip_);
    }

private:
    QColor strip_;
};

// Lit or not; the property is there for the tests.
class Light : public QWidget {
public:
    explicit Light(QWidget* parent) : QWidget(parent)
    {
        setFixedSize(kLightSize, kLightSize);
        setProperty("lit", false);
    }
    void setLit(bool lit)
    {
        setProperty("lit", lit);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), property("lit").toBool() ? QColor(0x38, 0xd0, 0x38) : palette().color(QPalette::Dark));
        p.setPen(palette().color(QPalette::Shadow));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
};

PanelKey* makeKey(const QString& text, QWidget* parent, const QString& name = {}, const QString& tip = {})
{
    auto* key = new PanelKey(parent);
    key->setText(text);
    key->setFont(keyFont());
    key->setCheckable(true);
    key->setFixedHeight(kKeyHeight);
    key->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    key->setObjectName(name);
    key->setStatusTip(tip);
    return key;
}

Knob* makeKnob(const QString& caption, Knob::Size size, int chars, const QString& unit, QWidget* parent,
               const QString& name, const QString& tip)
{
    auto* knob = new Knob(caption, size, chars, unit, parent);
    knob->setFont(smallFont());
    knob->setObjectName(name);
    knob->setStatusTip(tip);
    return knob;
}

QLabel* makeLegend(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setFont(smallFont());
    label->setAlignment(Qt::AlignCenter);
    return label;
}

QGroupBox* makeSection(const QString& title, QWidget* parent)
{
    auto* box = new QGroupBox(title, parent);
    box->setFont(titleFont());
    return box;
}

QComboBox* makeCombo(const QStringList& items, QWidget* parent)
{
    auto* combo = new QComboBox(parent);
    combo->setFont(smallFont());
    combo->addItems(items);
    return combo;
}

struct KeyChoice {
    QString text;
    int id;
    QString tip;
};

// Keys of which one is down, under a legend. The group reports clicks, so showing a setting never counts as one.
QWidget* makeKeyRow(const QString& legend, const std::vector<KeyChoice>& keys, const QString& namePrefix,
                    QButtonGroup*& group, QWidget* parent)
{
    auto* box = new QWidget(parent);
    auto* column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(1);
    column->addWidget(makeLegend(legend, box));
    auto* row = new QHBoxLayout;
    row->setSpacing(1);
    group = new QButtonGroup(box);
    group->setExclusive(true);
    for (const KeyChoice& choice : keys) {
        PanelKey* key = makeKey(choice.text, box, namePrefix + QString::number(choice.id), choice.tip);
        group->addButton(key, choice.id);
        row->addWidget(key);
    }
    column->addLayout(row);
    return box;
}

QString si(double value, const QString& unit, bool trim = true)
{
    return QString::fromStdString(starosc::format_si(value, unit.toStdString(), 3, trim));
}

QString signedDivisions(double value)
{
    return QString::asprintf("%+.2f", std::abs(value) < kPositionResolution / 2 ? 0.0 : value);
}

// In the order of turning clockwise: the scope's convention, the same as a wheel notch up on the display.
void showScaleSteps(Knob* knob, std::vector<double> values, const QString& unit)
{
    std::ranges::reverse(values);
    QStringList labels;
    for (double v : values)
        labels << si(v, unit);
    knob->setSteps(values, labels);
    knob->setUnit(unit);
}

}  // namespace

template <typename Change>
void FrontPanel::edit(Change change)
{
    const starosc::ScopeSettings before = settings_;
    change(settings_);
    if (settings_ == before)
        return;
    showSettings();
    emit settingsChanged(settings_);
}

FrontPanel::FrontPanel(QWidget* parent) : ScopePanel(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addWidget(createVertical());
    layout->addWidget(createMath());
    layout->addWidget(createReferences());
    layout->addWidget(createHorizontal());
    layout->addWidget(createTrigger());
    layout->addStretch(1);
    for (Knob* knob : findChildren<Knob*>()) {
        QString tip = knob->statusTip();
        if (!tip.isEmpty() && !tip.endsWith('.'))
            tip += '.';
        knob->setStatusTip(tip + " " + tr("Turn with the wheel or by dragging around it. Double-click to type a value."));
    }
    refreshColors();
    showSettings();
}

QGroupBox* FrontPanel::createVertical()
{
    QGroupBox* box = makeSection(tr("VERTICAL"), this);
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(6, 4, 6, 6);
    row->setSpacing(10);
    for (int ch = 0; ch < starosc::kChannels; ++ch)
        row->addWidget(createChannel(ch, box));
    return box;
}

QWidget* FrontPanel::createChannel(int channel, QWidget* parent)
{
    ChannelControls& w = channels_[static_cast<std::size_t>(channel)];
    const QString name = QString("ch%1").arg(channel + 1);
    auto* box = new QWidget(parent);
    auto* column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(3);

    w.key = makeKey(tr("CH%1").arg(channel + 1), box, name + "Key", tr("Show channel %1").arg(channel + 1));
    connect(w.key, &QToolButton::clicked, this,
            [this, channel](bool on) { edit([&](auto& s) { s.channel[channel].enabled = on; }); });
    column->addWidget(w.key);

    w.position = makeKnob(tr("POS"), Knob::Size::Small, 5, QStringLiteral("div"), box, name + "Position",
                          tr("Position of the trace, in divisions"));
    w.position->setRange(-starosc::kDivisionsY / 2.0, starosc::kDivisionsY / 2.0, kPositionStep, kPositionResolution);
    w.position->setFormat(signedDivisions);
    connect(w.position, &Knob::edited, this,
            [this, channel](double v) { edit([&](auto& s) { s.channel[channel].position_div = v; }); });

    w.scale = makeKnob(tr("VOLTS/DIV"), Knob::Size::Large, 7, QStringLiteral("V"), box, name + "Scale",
                       tr("Vertical scale at the probe tip"));
    connect(w.scale, &Knob::edited, this,
            [this, channel](double v) { edit([&](auto& s) { s.channel[channel].volts_per_div = v; }); });

    auto* knobs = new QHBoxLayout;
    knobs->setSpacing(4);
    knobs->addStretch(1);
    knobs->addWidget(w.position, 0, Qt::AlignBottom);
    knobs->addWidget(w.scale, 0, Qt::AlignBottom);
    knobs->addStretch(1);
    column->addLayout(knobs);

    auto* lower = new QHBoxLayout;
    lower->setSpacing(1);
    w.coupling = new QButtonGroup(box);
    const int dc = static_cast<int>(starosc::Coupling::DC);
    const int ac = static_cast<int>(starosc::Coupling::AC);
    for (const auto& [text, id, tip] : {std::tuple{tr("DC"), dc, tr("DC coupling")}, std::tuple{tr("AC"), ac, tr("AC coupling")}}) {
        PanelKey* key = makeKey(text, box, name + (id == dc ? "Dc" : "Ac"), tip);
        w.coupling->addButton(key, id);
        lower->addWidget(key);
    }
    connect(w.coupling, &QButtonGroup::idClicked, this, [this, channel](int id) {
        edit([&](auto& s) { s.channel[channel].coupling = static_cast<starosc::Coupling>(id); });
    });
    lower->addSpacing(3);
    w.probe = new ProbeBox(box);
    w.probe->setFont(smallFont());
    w.probe->setMinimumContentsLength(kProbeChars);
    w.probe->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    w.probe->setObjectName(QString("frontProbe%1").arg(channel + 1));
    connect(w.probe, &ProbeBox::probeChosen, this, [this, channel](double probe) { changeProbe(channel, probe); });
    lower->addWidget(w.probe, 1);
    column->addLayout(lower);
    return box;
}

QGroupBox* FrontPanel::createMath()
{
    QGroupBox* box = makeSection(tr("MATH"), this);
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(6, 4, 6, 6);
    row->setSpacing(6);

    auto* left = new QVBoxLayout;
    left->setSpacing(3);
    mathKey_ = makeKey(tr("MATH"), box, "mathKey", tr("Show the math trace"));
    connect(mathKey_, &QToolButton::clicked, this, [this](bool on) { edit([&](auto& s) { s.math.enabled = on; }); });
    left->addWidget(mathKey_);

    mathA_ = makeCombo({tr("CH1"), tr("CH2")}, box);
    mathA_->setObjectName("mathA");
    mathB_ = makeCombo({tr("CH1"), tr("CH2")}, box);
    mathB_->setObjectName("mathB");
    mathOperation_ = makeCombo({}, box);
    mathOperation_->setObjectName("mathOperation");
    for (int i = 0; i < starosc::kMathOperations; ++i)
        mathOperation_->addItem(QString::fromUtf8(starosc::math_symbol(static_cast<starosc::MathOperation>(i)).data()));
    auto* function = new QHBoxLayout;
    function->setSpacing(2);
    function->addWidget(mathA_, 1);
    function->addWidget(mathOperation_);
    function->addWidget(mathB_, 1);
    left->addLayout(function);
    left->addStretch(1);
    row->addLayout(left, 1);

    connect(mathA_, &QComboBox::activated, this, [this](int i) { edit([&](auto& s) { s.math.a = i; }); });
    connect(mathB_, &QComboBox::activated, this, [this](int i) { edit([&](auto& s) { s.math.b = i; }); });
    connect(mathOperation_, &QComboBox::activated, this, [this](int i) {
        // The unit changes with the operation; the scale stays as it is.
        edit([&](auto& s) { s.math.operation = static_cast<starosc::MathOperation>(i); });
    });

    mathPosition_ = makeKnob(tr("POS"), Knob::Size::Small, 5, QStringLiteral("div"), box, "mathPosition",
                             tr("Position of the math trace, in divisions"));
    mathPosition_->setRange(-starosc::kDivisionsY / 2.0, starosc::kDivisionsY / 2.0, kPositionStep, kPositionResolution);
    mathPosition_->setFormat(signedDivisions);
    connect(mathPosition_, &Knob::edited, this, [this](double v) { edit([&](auto& s) { s.math.position_div = v; }); });

    mathScale_ = makeKnob(tr("SCALE"), Knob::Size::Small, 8, {}, box, "mathScale", tr("Scale of the math trace"));
    connect(mathScale_, &Knob::edited, this, [this](double v) { edit([&](auto& s) { s.math.units_per_div = v; }); });

    row->addWidget(mathPosition_, 0, Qt::AlignBottom);
    row->addWidget(mathScale_, 0, Qt::AlignBottom);
    return box;
}

QGroupBox* FrontPanel::createReferences()
{
    QGroupBox* box = makeSection(tr("REFERENCE"), this);
    auto* grid = new QGridLayout(box);
    grid->setContentsMargins(6, 4, 6, 6);
    grid->setHorizontalSpacing(3);
    grid->setVerticalSpacing(3);
    grid->setColumnStretch(4, 1);
    for (int r = 0; r < static_cast<int>(references_.size()); ++r) {
        ReferenceControls& w = references_[static_cast<std::size_t>(r)];
        w.key = makeKey(tr("R%1").arg(r + 1), box, QString("ref%1Key").arg(r + 1), tr("Show the stored waveform"));
        connect(w.key, &QToolButton::clicked, this, [this, r](bool on) { emit referenceShown(r, on); });

        w.source = makeCombo({tr("CH1"), tr("CH2"), tr("Math")}, box);
        w.source->setObjectName(QString("ref%1Source").arg(r + 1));
        w.source->setStatusTip(tr("Trace to store"));

        PanelKey* store = makeKey(tr("STORE"), box, QString("ref%1Store").arg(r + 1),
                                  tr("Store the trace as it is on the screen now"));
        store->setCheckable(false);
        store->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        connect(store, &QToolButton::clicked, this,
                [this, r] { emit referenceStoreRequested(r, references_[static_cast<std::size_t>(r)].source->currentIndex()); });

        auto* more = new QToolButton(box);
        more->setObjectName(QString("ref%1More").arg(r + 1));
        more->setFont(keyFont());
        more->setText(tr("…"));
        more->setFixedHeight(kKeyHeight);
        more->setPopupMode(QToolButton::InstantPopup);
        auto* menu = new QMenu(more);
        menu->addAction(tr("Save As…"), this, [this, r] { emit referenceSaveRequested(r); });
        menu->addAction(tr("Load…"), this, [this, r] { emit referenceLoadRequested(r); });
        menu->addAction(tr("Clear"), this, [this, r] { emit referenceClearRequested(r); });
        more->setMenu(menu);

        w.description = new QLabel(box);
        w.description->setFont(smallFont());
        w.description->setEnabled(false);
        // Long descriptions are cut, and the panel keeps its width; the tip has it all.
        w.description->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

        grid->addWidget(w.key, r, 0);
        grid->addWidget(w.source, r, 1);
        grid->addWidget(store, r, 2);
        grid->addWidget(more, r, 3);
        grid->addWidget(w.description, r, 4);
        setReferenceState(r, {}, false);
    }
    grid->setColumnMinimumWidth(0, 30);
    return box;
}

QGroupBox* FrontPanel::createHorizontal()
{
    QGroupBox* box = makeSection(tr("HORIZONTAL"), this);
    auto* column = new QVBoxLayout(box);
    column->setContentsMargins(6, 4, 6, 6);
    column->setSpacing(3);

    timeScale_ = makeKnob(tr("TIME/DIV"), Knob::Size::Large, 7, QStringLiteral("s"), box, "timeScale", tr("Main timebase"));
    showScaleSteps(timeScale_, starosc::seconds_per_div_steps(), QStringLiteral("s"));
    connect(timeScale_, &Knob::edited, this, [this](double v) { edit([&](auto& s) { s.timebase.seconds_per_div = v; }); });

    zoom_ = makeKnob(tr("ZOOM"), Knob::Size::Small, 7, QStringLiteral("s"), box, "zoomScale",
                     tr("Show a part of the record with a faster timebase. The capture and the measurements stay on "
                        "the main timebase."));
    connect(zoom_, &Knob::edited, this, [this](double v) { edit([&](auto& s) { s.zoom.seconds_per_div = v; }); });

    zoomPosition_ = makeKnob(tr("ZOOM POS"), Knob::Size::Small, 7, QStringLiteral("%"), box, "zoomPosition",
                             tr("Centre of the zoomed part, in percent of the record"));
    zoomPosition_->setFormat([](double v) { return QString::asprintf("%.1f %%", v); });
    connect(zoomPosition_, &Knob::edited, this, [this](double v) { edit([&](auto& s) { s.zoom.center = v / 100; }); });

    auto* knobs = new QHBoxLayout;
    knobs->setSpacing(6);
    knobs->addWidget(timeScale_, 0, Qt::AlignBottom);
    knobs->addWidget(zoom_, 0, Qt::AlignBottom);
    knobs->addWidget(zoomPosition_, 0, Qt::AlignBottom);
    acquisitionInfo_ = new QLabel(box);
    acquisitionInfo_->setObjectName("frontAcquisitionInfo");
    acquisitionInfo_->setFont(smallFont());
    acquisitionInfo_->setAlignment(Qt::AlignCenter);
    acquisitionInfo_->setWordWrap(true);
    acquisitionInfo_->setStatusTip(tr("Sample rate and points on the screen"));
    knobs->addWidget(acquisitionInfo_, 1);
    column->addLayout(knobs);

    auto* keys = new QHBoxLayout;
    keys->setSpacing(6);
    const int small = static_cast<int>(starosc::MemoryDepth::Small);
    const int medium = static_cast<int>(starosc::MemoryDepth::Medium);
    const int large = static_cast<int>(starosc::MemoryDepth::Large);
    keys->addWidget(makeKeyRow(tr("MEMORY"),
                               {{tr("SMALL"), small, tr("Small memory: fast update")},
                                {tr("MED"), medium, tr("Medium memory")},
                                {tr("LARGE"), large, tr("Large memory: high sample rate")}},
                               "memory", memory_, box),
                    3);
    connect(memory_, &QButtonGroup::idClicked, this,
            [this](int id) { edit([&](auto& s) { s.timebase.memory = static_cast<starosc::MemoryDepth>(id); }); });
    keys->addWidget(makeKeyRow(tr("DISPLAY"),
                               {{tr("Y-T"), static_cast<int>(starosc::DisplayMode::YT), tr("Voltage against time")},
                                {tr("X-Y"), static_cast<int>(starosc::DisplayMode::XY), tr("CH1 across, CH2 up")}},
                               "display", display_, box),
                    2);
    connect(display_, &QButtonGroup::idClicked, this,
            [this](int id) { edit([&](auto& s) { s.display = static_cast<starosc::DisplayMode>(id); }); });
    column->addLayout(keys);
    return box;
}

QGroupBox* FrontPanel::createTrigger()
{
    QGroupBox* box = makeSection(tr("TRIGGER"), this);
    auto* column = new QVBoxLayout(box);
    column->setContentsMargins(6, 4, 6, 6);
    column->setSpacing(3);

    triggerLevel_ = makeKnob(tr("LEVEL"), Knob::Size::Large, 7, QStringLiteral("V"), box, "triggerLevel",
                             tr("Trigger level at the probe tip"));
    triggerLevel_->setFormat([](double v) { return si(v, QStringLiteral("V"), false); });
    connect(triggerLevel_, &Knob::edited, this, [this](double v) { edit([&](auto& s) { s.trigger.level_v = v; }); });

    sensitivity_ = makeKnob(tr("SENS"), Knob::Size::Small, 7, QStringLiteral("div"), box, "triggerSensitivity",
                            tr("Hysteresis: how far the signal must swing back before the trigger re-arms"));
    std::vector<double> sensitivities;
    QStringList sensitivityLabels;
    for (int i = 1; i <= kSensitivitySteps; ++i) {
        sensitivities.push_back(i * kSensitivityStep);
        sensitivityLabels << QString::asprintf("%.1f div", i * kSensitivityStep);
    }
    sensitivity_->setSteps(sensitivities, sensitivityLabels);
    connect(sensitivity_, &Knob::edited, this, [this](double v) { edit([&](auto& s) { s.trigger.hysteresis_div = v; }); });

    preTrigger_ = makeKnob(tr("PRE-TRIG"), Knob::Size::Small, 5, QStringLiteral("%"), box, "preTrigger",
                           tr("Part of the screen before the trigger point"));
    preTrigger_->setRange(5, 95, 5, 1);
    preTrigger_->setFormat([](double v) { return QString::asprintf("%.0f %%", v); });
    connect(preTrigger_, &Knob::edited, this, [this](double v) { edit([&](auto& s) { s.trigger.pre_trigger = v / 100; }); });

    auto* light = new QVBoxLayout;
    light->setSpacing(2);
    triggeredLight_ = new Light(box);
    triggeredLight_->setObjectName("triggeredLight");
    triggeredLight_->setStatusTip(tr("The frame on the screen was triggered"));
    light->addStretch(1);
    light->addWidget(triggeredLight_, 0, Qt::AlignHCenter);
    light->addWidget(makeLegend(tr("TRIG'D"), box));
    light->addStretch(1);

    auto* knobs = new QHBoxLayout;
    knobs->setSpacing(6);
    knobs->addStretch(1);
    knobs->addWidget(triggerLevel_, 0, Qt::AlignBottom);
    knobs->addWidget(sensitivity_, 0, Qt::AlignBottom);
    knobs->addWidget(preTrigger_, 0, Qt::AlignBottom);
    knobs->addLayout(light);
    knobs->addStretch(1);
    column->addLayout(knobs);

    auto* keys = new QHBoxLayout;
    keys->setSpacing(6);
    keys->addWidget(makeKeyRow(tr("SOURCE"), {{tr("CH1"), 0, tr("Trigger on channel 1")}, {tr("CH2"), 1, tr("Trigger on channel 2")}},
                               "triggerSource", triggerSource_, box),
                    2);
    connect(triggerSource_, &QButtonGroup::idClicked, this, [this](int id) { edit([&](auto& s) { s.trigger.source = id; }); });
    keys->addWidget(makeKeyRow(tr("EDGE"),
                               {{QStringLiteral("↑"), static_cast<int>(starosc::TriggerEdge::Rising), tr("Rising edge")},
                                {QStringLiteral("↓"), static_cast<int>(starosc::TriggerEdge::Falling), tr("Falling edge")},
                                {QStringLiteral("↑↓"), static_cast<int>(starosc::TriggerEdge::Either), tr("Either edge")}},
                               "triggerEdge", triggerEdge_, box),
                    2);
    connect(triggerEdge_, &QButtonGroup::idClicked, this,
            [this](int id) { edit([&](auto& s) { s.trigger.edge = static_cast<starosc::TriggerEdge>(id); }); });
    keys->addWidget(makeKeyRow(tr("MODE"),
                               {{tr("AUTO"), static_cast<int>(starosc::TriggerMode::Auto), tr("Show frames without a trigger too")},
                                {tr("NORM"), static_cast<int>(starosc::TriggerMode::Normal), tr("Show triggered frames only")},
                                {tr("SINGLE"), static_cast<int>(starosc::TriggerMode::Single), tr("Stop after one triggered frame")}},
                               "triggerMode", triggerMode_, box),
                    3);
    connect(triggerMode_, &QButtonGroup::idClicked, this,
            [this](int id) { edit([&](auto& s) { s.trigger.mode = static_cast<starosc::TriggerMode>(id); }); });
    column->addLayout(keys);
    return box;
}

// The rate and the points on lines of their own: the column is narrow, and the text breaks at the same place in every
// language.
void FrontPanel::setAcquisitionInfo(const QString& text)
{
    acquisitionInfo_->setText(QString(text).replace(QStringLiteral(" · "), QStringLiteral("\n")));
}

void FrontPanel::setReferenceState(int ref, const QString& description, bool shown)
{
    ReferenceControls& w = references_[static_cast<std::size_t>(ref)];
    w.key->setEnabled(!description.isEmpty());
    w.key->setChecked(shown && !description.isEmpty());
    w.description->setText(description.isEmpty() ? tr("empty") : description);
    w.description->setStatusTip(description);
}

void FrontPanel::refreshColors()
{
    for (int ch = 0; ch < starosc::kChannels; ++ch)
        static_cast<PanelKey*>(channels_[static_cast<std::size_t>(ch)].key)->setStrip(display_style::chosenColor(display_style::Ch1 + ch));
    static_cast<PanelKey*>(mathKey_)->setStrip(display_style::chosenColor(display_style::Math));
    for (int r = 0; r < static_cast<int>(references_.size()); ++r)
        static_cast<PanelKey*>(references_[static_cast<std::size_t>(r)].key)->setStrip(display_style::chosenColor(display_style::Ref1 + r));
}

void FrontPanel::setTriggered(bool triggered) { static_cast<Light*>(triggeredLight_)->setLit(triggered); }

void FrontPanel::showSettings()
{
    for (int ch = 0; ch < starosc::kChannels; ++ch) {
        const auto& cs = settings_.channel[static_cast<std::size_t>(ch)];
        const ChannelControls& w = channels_[static_cast<std::size_t>(ch)];
        w.key->setChecked(cs.enabled);
        showScaleSteps(w.scale, starosc::volts_per_div_steps(cs.probe), QStringLiteral("V"));
        w.scale->setValue(cs.volts_per_div);
        w.position->setValue(cs.position_div);
        w.coupling->button(static_cast<int>(cs.coupling))->setChecked(true);
        w.probe->showProbe(cs);
        for (QWidget* control : std::initializer_list<QWidget*>{w.scale, w.position, w.probe})
            control->setEnabled(cs.enabled);
        for (QAbstractButton* key : w.coupling->buttons())
            key->setEnabled(cs.enabled);
    }

    timeScale_->setValue(settings_.timebase.seconds_per_div);
    memory_->button(static_cast<int>(settings_.timebase.memory))->setChecked(true);
    display_->button(static_cast<int>(settings_.display))->setChecked(true);

    std::vector<double> zooms = starosc::zoom_steps(settings_.timebase.seconds_per_div, sampleRate_);
    QStringList zoomLabels{tr("Off")};
    for (double z : zooms)
        zoomLabels << si(z, QStringLiteral("s"));
    zooms.insert(zooms.begin(), 0.0);
    zoom_->setSteps(zooms, zoomLabels);
    zoom_->setValue(settings_.zoom.on() ? settings_.zoom.seconds_per_div : 0);

    const bool yt = settings_.display == starosc::DisplayMode::YT;
    const starosc::VisibleRange range = starosc::visible_range(settings_.timebase, settings_.zoom);
    zoomPosition_->setRange(range.span / 2 * 100, (1 - range.span / 2) * 100, std::max(0.001, range.span * 10), 0.001);
    zoomPosition_->setValue((range.begin + range.span / 2) * 100);
    zoom_->setEnabled(yt);
    zoomPosition_->setEnabled(yt && settings_.zoom.on());

    const auto& t = settings_.trigger;
    triggerSource_->button(t.source)->setChecked(true);
    triggerEdge_->button(static_cast<int>(t.edge))->setChecked(true);
    triggerMode_->button(static_cast<int>(t.mode))->setChecked(true);
    const auto& src = settings_.channel[static_cast<std::size_t>(t.source)];
    const double half = starosc::kDivisionsY / 2.0;
    triggerLevel_->setRange((-half - src.position_div) * src.volts_per_div, (half - src.position_div) * src.volts_per_div,
                            src.volts_per_div / 10, src.volts_per_div / 100);
    triggerLevel_->setValue(t.level_v);
    sensitivity_->setValue(t.hysteresis_div);
    preTrigger_->setValue(t.pre_trigger * 100);

    const auto& m = settings_.math;
    mathKey_->setChecked(m.enabled);
    mathA_->setCurrentIndex(m.a);
    mathOperation_->setCurrentIndex(static_cast<int>(m.operation));
    mathB_->setCurrentIndex(m.b);
    showScaleSteps(mathScale_, starosc::units_per_div_steps(), QString::fromUtf8(starosc::math_unit(m.operation).data()));
    mathScale_->setValue(m.units_per_div);
    mathPosition_->setValue(m.position_div);
    for (QWidget* control : std::initializer_list<QWidget*>{mathA_, mathOperation_, mathB_, mathScale_, mathPosition_})
        control->setEnabled(m.enabled);
}
