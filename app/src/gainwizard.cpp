// SPDX-License-Identifier: GPL-3.0-or-later
#include "gainwizard.hpp"

#include <algorithm>
#include <cmath>

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWizardPage>

#include "sispinbox.hpp"

namespace {

constexpr double kMeterTolerance = 0.3;   // how far a multimeter reading may be from what the inputs read
constexpr double kMeterSlack = 0.05;      // volts: near zero, a percentage says little
constexpr double kCheckWarning = 0.01;    // the check is expected within a few tenths of a percent
constexpr double kSpreadWarning = 0.002;  // between repeats, usually 0.02 %
constexpr double kMinGain = 0.5, kMaxGain = 2;  // as the calibration table accepts

QLabel* wrapped(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    return label;
}

// A message of the measurement as it goes on a page, in bold.
QString asSentence(QString message)
{
    if (!message.isEmpty())
        message[0] = message[0].toUpper();
    if (!message.endsWith('?') && !message.endsWith('.'))
        message += '.';
    return QStringLiteral("<b>%1</b>").arg(message.toHtmlEscaped());
}

// A page that waits for a measurement: its failure shows on the page, with a button to try again.
class StepPage : public QWizardPage {
    Q_DECLARE_TR_FUNCTIONS(GainWizard)  // the pages share the wizard's texts

public:
    using QWizardPage::QWizardPage;
    virtual void failed(const QString& message) = 0;

protected:
    GainWizard* gainWizard() const { return static_cast<GainWizard*>(wizard()); }
};

class IntroPage : public QWizardPage {
    Q_DECLARE_TR_FUNCTIONS(GainWizard)  // the pages share the wizard's texts

public:
    explicit IntroPage(QWidget* parent) : QWizardPage(parent)
    {
        setTitle(tr("Gain calibration"));
        setSubTitle(tr("Finds the gain of every stage of both channels, in about a minute."));
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(wrapped(tr("The generator gives the signals, up to about ±3 V on DDS OUT, and the gain relays "
                                     "will click many times. Disconnect everything else from DDS OUT."),
                                  this));
        layout->addSpacing(8);
        layout->addWidget(wrapped(tr("A multimeter makes the readings correct in volts:"), this));
        with_ = new QRadioButton(tr("&With a multimeter (recommended)"), this);
        with_->setObjectName("withMultimeter");
        with_->setChecked(true);
        without_ = new QRadioButton(tr("With&out a multimeter: the stages and the channels only agree with each other"), this);
        without_->setObjectName("withoutMultimeter");
        layout->addWidget(with_);
        layout->addWidget(without_);
        layout->addStretch(1);
    }

    bool withMultimeter() const { return with_->isChecked(); }

private:
    QRadioButton* with_ = nullptr;
    QRadioButton* without_ = nullptr;
};

class ConnectPage : public StepPage {
    Q_DECLARE_TR_FUNCTIONS(GainWizard)  // the pages share the wizard's texts

public:
    explicit ConnectPage(QWidget* parent) : StepPage(parent)
    {
        setTitle(tr("Connect the inputs"));
        setSubTitle(tr("Everything measures one point: the generator's output."));
        auto* layout = new QVBoxLayout(this);
        steps_ = wrapped({}, this);
        layout->addWidget(steps_);
        check_ = new QPushButton(tr("&Check the Connection"), this);
        check_->setObjectName("checkConnection");
        auto* row = new QHBoxLayout;
        row->addWidget(check_);
        row->addStretch(1);
        layout->addLayout(row);
        status_ = wrapped({}, this);
        status_->setObjectName("connectStatus");
        layout->addWidget(status_);
        layout->addStretch(1);
        connect(check_, &QPushButton::clicked, this, [this] {
            check_->setEnabled(false);
            status_->setText(tr("Checking…"));
            emit gainWizard()->checkBenchRequested();
        });
    }

    void initializePage() override
    {
        checked_ = false;
        check_->setEnabled(true);
        status_->clear();
        QString text = tr("1. Set both probes to <b>×1</b>.<br>2. Connect both probe tips to <b>DDS OUT</b> and both ground clips to its ground.");
        if (gainWizard()->withMultimeter())
            text += tr("<br>3. Connect the multimeter to the same two points, on <b>DC volts</b>.");
        steps_->setText(text);
    }

    bool isComplete() const override { return checked_; }
    int nextId() const override { return gainWizard()->withMultimeter() ? GainWizard::ReferencePlusId : GainWizard::TransferPageId; }

    void passed()
    {
        checked_ = true;
        status_->setText(tr("Both channels read the generator."));
        emit completeChanged();
        wizard()->next();
    }

    void failed(const QString& message) override
    {
        check_->setEnabled(true);
        status_->setText(asSentence(message));
    }

private:
    QLabel* steps_ = nullptr;
    QPushButton* check_ = nullptr;
    QLabel* status_ = nullptr;
    bool checked_ = false;
};

// The generator stands at a level; the inputs read it, and the user types what the multimeter says.
class LevelPage : public StepPage {
    Q_DECLARE_TR_FUNCTIONS(GainWizard)  // the pages share the wizard's texts

public:
    LevelPage(int pageId, int which, bool optional, QWidget* parent) : StepPage(parent), pageId_(pageId), which_(which), optional_(optional)
    {
        auto* layout = new QVBoxLayout(this);
        status_ = wrapped({}, this);
        status_->setObjectName("levelStatus");
        layout->addWidget(status_);
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("&Multimeter reading:"), this));
        meter_ = new QLineEdit(this);
        meter_->setObjectName("meterReading");
        meter_->setMaximumWidth(120);
        static_cast<QLabel*>(row->itemAt(0)->widget())->setBuddy(meter_);
        row->addWidget(meter_);
        row->addWidget(new QLabel(tr("V"), this));
        row->addStretch(1);
        layout->addLayout(row);
        hint_ = wrapped({}, this);
        hint_->setObjectName("levelHint");
        layout->addWidget(hint_);
        retry_ = new QPushButton(tr("&Try Again"), this);
        retry_->setObjectName("tryAgain");
        retry_->hide();
        auto* retryRow = new QHBoxLayout;
        retryRow->addWidget(retry_);
        retryRow->addStretch(1);
        layout->addLayout(retryRow);
        layout->addStretch(1);
        connect(meter_, &QLineEdit::textChanged, this, [this] { showHint(); emit completeChanged(); });
        connect(retry_, &QPushButton::clicked, this, &LevelPage::request);
    }

    void initializePage() override
    {
        const auto& stages = gainWizard()->current().stages;
        const int stage = pageId_ == GainWizard::ReferencePlusId || pageId_ == GainWizard::ReferenceMinusId ? starosc::reference_stage(stages)
                                                                                                            : starosc::check_stage(stages);
        plan_ = starosc::level_plan(stages, stage);
        const bool check = pageId_ == GainWizard::CheckPlusId || pageId_ == GainWizard::CheckMinusId;
        setTitle(check ? tr("Check against the multimeter") : tr("Reference level"));
        setSubTitle(check ? tr("Optional: another stage against the multimeter. Leave the box empty to skip this.")
                          : tr("The inputs read a level, and the multimeter says what it really is."));
        meter_->clear();
        request();
    }

    bool isComplete() const override
    {
        if (!measured_)
            return false;
        if (optional_ && meter_->text().trimmed().isEmpty())
            return true;
        return problem().isEmpty();
    }

    bool validatePage() override
    {
        GainWizard::LevelReading& reading = gainWizard()->reading(pageId_);
        reading.read = read_;
        reading.have = !meter_->text().trimmed().isEmpty();
        if (reading.have)
            reading.meter = *SiSpinBox::parse(meter_->text(), QStringLiteral("V"));
        return true;
    }

    int nextId() const override
    {
        if (pageId_ == GainWizard::CheckPlusId && meter_->text().trimmed().isEmpty())
            return GainWizard::ResultPageId;
        return QWizardPage::nextId();
    }

    void measured(double ch1, double ch2)
    {
        read_ = {ch1, ch2};
        measured_ = true;
        status_->setText(tr("The generator stands at about <b>%1 V</b>, with a sine on it. Let the multimeter settle and type what it "
                            "shows, with all its digits. If the last digit moves, take the middle.")
                             .arg(expected(), 0, 'f', 2));
        meter_->setEnabled(true);
        meter_->setFocus();
        showHint();
        emit completeChanged();
    }

    void failed(const QString& message) override
    {
        status_->setText(asSentence(message));
        retry_->show();
    }

private:
    double expected() const { return (read_[0] + read_[1]) / 2; }

    void request()
    {
        measured_ = false;
        retry_->hide();
        meter_->setEnabled(false);
        status_->setText(tr("Setting the generator…"));
        hint_->clear();
        emit completeChanged();
        emit gainWizard()->levelRequested(plan_.stage, plan_.volts[static_cast<std::size_t>(which_)], plan_.sine_vpp);
    }

    // Why the text is no reading of this level; empty when it may be one.
    QString problem() const
    {
        const auto value = SiSpinBox::parse(meter_->text(), QStringLiteral("V"));
        if (!value)
            return tr("Type the reading as a number, for example %1.").arg(expected(), 0, 'f', 3);
        if ((*value < 0) != (expected() < 0))
            return expected() < 0 ? tr("This level is negative: type the minus sign too.") : tr("This level is positive.");
        if (std::abs(*value - expected()) > kMeterTolerance * std::abs(expected()) + kMeterSlack)
            return tr("This is far from what the inputs read, about %1 V. Is the multimeter on DC volts, at the same point?").arg(expected(), 0, 'f', 2);
        return {};
    }

    void showHint()
    {
        const bool empty = meter_->text().trimmed().isEmpty();
        hint_->setText(measured_ && !empty ? problem() : QString());
    }

    int pageId_;
    int which_;
    bool optional_;
    starosc::LevelPlan plan_;
    std::array<double, starosc::kChannels> read_{};
    bool measured_ = false;
    QLabel* status_ = nullptr;
    QLineEdit* meter_ = nullptr;
    QLabel* hint_ = nullptr;
    QPushButton* retry_ = nullptr;
};

class TransferPage : public StepPage {
    Q_DECLARE_TR_FUNCTIONS(GainWizard)  // the pages share the wizard's texts

public:
    explicit TransferPage(QWidget* parent) : StepPage(parent)
    {
        setTitle(tr("Measuring the stages"));
        setSubTitle(tr("Each stage is compared with its neighbour. You will hear the gain relays."));
        setCommitPage(true);
        auto* layout = new QVBoxLayout(this);
        progress_ = new QProgressBar(this);
        progress_->setObjectName("transferProgress");
        layout->addWidget(progress_);
        status_ = wrapped({}, this);
        status_->setObjectName("transferStatus");
        layout->addWidget(status_);
        retry_ = new QPushButton(tr("&Try Again"), this);
        retry_->setObjectName("tryAgain");
        retry_->hide();
        auto* row = new QHBoxLayout;
        row->addWidget(retry_);
        row->addStretch(1);
        layout->addLayout(row);
        layout->addStretch(1);
        connect(retry_, &QPushButton::clicked, this, &TransferPage::start);
    }

    void initializePage() override { start(); }
    bool isComplete() const override { return done_; }

    int nextId() const override
    {
        return gainWizard()->withMultimeter() ? GainWizard::CheckPlusId : GainWizard::ResultPageId;
    }

    void progress(int done, int total)
    {
        progress_->setRange(0, total);
        progress_->setValue(done);
    }

    void finished()
    {
        done_ = true;
        status_->setText(tr("Done."));
        emit completeChanged();
        wizard()->next();
    }

    void failed(const QString& message) override
    {
        status_->setText(asSentence(message));
        retry_->show();
    }

private:
    void start()
    {
        done_ = false;
        retry_->hide();
        progress_->setRange(0, 0);
        status_->clear();
        emit completeChanged();
        emit gainWizard()->transferRequested();
    }

    QProgressBar* progress_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* retry_ = nullptr;
    bool done_ = false;
};

class ResultPage : public QWizardPage {
    Q_DECLARE_TR_FUNCTIONS(GainWizard)  // the pages share the wizard's texts

public:
    explicit ResultPage(QWidget* parent) : QWizardPage(parent)
    {
        setTitle(tr("Result"));
        setSubTitle(tr("Finish takes these gains into the calibration table; OK there stores them."));
        auto* layout = new QVBoxLayout(this);
        table_ = new QTableWidget(0, 5, this);
        table_->setObjectName("gainResult");
        table_->setHorizontalHeaderLabels({tr("Stage (CH1 span)"), tr("CH1 before"), tr("CH1 now"), tr("CH2 before"), tr("CH2 now")});
        table_->verticalHeader()->hide();
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setSelectionMode(QAbstractItemView::NoSelection);
        layout->addWidget(table_, 1);
        note_ = wrapped({}, this);
        note_->setObjectName("gainNote");
        layout->addWidget(note_);
    }

    void initializePage() override
    {
        auto* w = static_cast<GainWizard*>(wizard());
        w->computeResult();
        const auto& before = w->current().stages;
        const auto& now = w->result().stages;
        table_->setRowCount(static_cast<int>(now.size()));
        valid_ = true;
        for (int row = 0; row < static_cast<int>(now.size()); ++row) {
            const auto r = static_cast<std::size_t>(row);
            const auto& span = now[r].factory_span[0];
            const auto cell = [&](int column, const QString& text) {
                auto* item = new QTableWidgetItem(text);
                item->setTextAlignment(column == 0 ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignRight | Qt::AlignVCenter);
                table_->setItem(row, column, item);
            };
            cell(0, tr("%1 … %2 mV").arg(span.min_v * 1000, 0, 'f', 0).arg(span.max_v * 1000, 0, 'f', 0));
            for (std::size_t ch = 0; ch < starosc::kChannels; ++ch) {
                cell(1 + 2 * static_cast<int>(ch), QString::number(before[r].channel[ch].gain, 'f', 4));
                cell(2 + 2 * static_cast<int>(ch), QString::number(now[r].channel[ch].gain, 'f', 4));
                valid_ = valid_ && now[r].channel[ch].gain > kMinGain && now[r].channel[ch].gain < kMaxGain;
            }
        }

        QStringList notes;
        if (!valid_)
            notes << tr("<b>Some gains are out of the possible range, so they cannot be used. Check the connections and the readings, and go through the wizard again.</b>");
        std::array<double, starosc::kChannels> error{};
        if (w->checkError(error)) {
            const double worst = std::max(std::abs(error[0]), std::abs(error[1]));
            notes << tr("Checked against the multimeter on another stage: CH1 %1 %, CH2 %2 %.")
                         .arg(error[0] * 100, 0, 'f', 2)
                         .arg(error[1] * 100, 0, 'f', 2);
            if (worst > kCheckWarning)
                notes << tr("<b>That is more than the usual few tenths of a percent. Check the multimeter's connection and go through the wizard again.</b>");
        }
        if (!w->withMultimeter())
            notes << tr("Without a multimeter the stages and the channels agree with each other, but the readings in volts are no more correct than the reference stage was.");
        if (w->transfer().worst_spread > kSpreadWarning)
            notes << tr("The readings moved during the measurement, by %1 %. Is the connection firm?").arg(w->transfer().worst_spread * 100, 0, 'f', 2);
        if (w->current().zero_time.empty())
            notes << tr("The zero has not been calibrated yet: use Measure Zero in the calibration window, with the probes shorted.");
        note_->setText(notes.join("<br>"));
        emit completeChanged();
    }

    bool isComplete() const override { return valid_; }

private:
    QTableWidget* table_ = nullptr;
    QLabel* note_ = nullptr;
    bool valid_ = false;
};

}  // namespace

GainWizard::GainWizard(const starosc::UserCalibration& current, QWidget* parent) : QWizard(parent), current_(current), result_(current)
{
    setWindowTitle(tr("Gain Calibration"));
    setWizardStyle(QWizard::ClassicStyle);
    setOption(QWizard::NoBackButtonOnStartPage);
    setOption(QWizard::NoBackButtonOnLastPage);
    setButtonText(QWizard::CommitButton, buttonText(QWizard::NextButton));
    setPage(IntroPageId, new IntroPage(this));
    setPage(ConnectPageId, new ConnectPage(this));
    setPage(ReferencePlusId, new LevelPage(ReferencePlusId, 0, false, this));
    setPage(ReferenceMinusId, new LevelPage(ReferenceMinusId, 1, false, this));
    setPage(TransferPageId, new TransferPage(this));
    setPage(CheckPlusId, new LevelPage(CheckPlusId, 0, true, this));
    setPage(CheckMinusId, new LevelPage(CheckMinusId, 1, false, this));
    setPage(ResultPageId, new ResultPage(this));
    resize(640, 460);
}

bool GainWizard::withMultimeter() const { return static_cast<IntroPage*>(page(IntroPageId))->withMultimeter(); }

GainWizard::LevelReading& GainWizard::reading(int pageId)
{
    switch (pageId) {
    case ReferencePlusId: return readings_[0];
    case ReferenceMinusId: return readings_[1];
    case CheckPlusId: return readings_[2];
    default: return readings_[3];
    }
}

void GainWizard::computeResult()
{
    const int reference = starosc::reference_stage(current_.stages);
    std::array<double, starosc::kChannels> gain{};
    if (withMultimeter()) {
        for (std::size_t ch = 0; ch < starosc::kChannels; ++ch)
            gain[ch] = starosc::gain_from_levels({readings_[0].read[ch], readings_[1].read[ch]}, {readings_[0].meter, readings_[1].meter});
    } else {
        gain[1] = current_.stages[static_cast<std::size_t>(reference)].channel[1].gain;
        gain[0] = gain[1] * transfer_.channel_ratio;
    }
    result_ = current_;
    starosc::apply_gains(result_, transfer_, gain);
    result_.gain_time = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
}

bool GainWizard::checkError(std::array<double, starosc::kChannels>& error) const
{
    if (!withMultimeter() || !readings_[2].have || !readings_[3].have)
        return false;
    const auto stage = static_cast<std::size_t>(starosc::check_stage(current_.stages));
    for (std::size_t ch = 0; ch < starosc::kChannels; ++ch) {
        const double measured = starosc::gain_from_levels({readings_[2].read[ch], readings_[3].read[ch]}, {readings_[2].meter, readings_[3].meter});
        error[ch] = result_.stages[stage].channel[ch].gain / measured - 1;
    }
    return true;
}

void GainWizard::benchChecked()
{
    if (auto* p = dynamic_cast<ConnectPage*>(currentPage()))
        p->passed();
}

void GainWizard::levelMeasured(double ch1, double ch2)
{
    if (auto* p = dynamic_cast<LevelPage*>(currentPage()))
        p->measured(ch1, ch2);
}

void GainWizard::transferProgress(int done, int total)
{
    if (auto* p = dynamic_cast<TransferPage*>(currentPage()))
        p->progress(done, total);
}

void GainWizard::transferred(const starosc::GainTransfer& result)
{
    transfer_ = result;
    if (auto* p = dynamic_cast<TransferPage*>(currentPage()))
        p->finished();
}

void GainWizard::stepFailed(const QString& message)
{
    if (auto* p = dynamic_cast<StepPage*>(currentPage()))
        p->failed(message);
}
