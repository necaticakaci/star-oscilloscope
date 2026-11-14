// SPDX-License-Identifier: GPL-3.0-or-later
#include "hoverhelp.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCursor>
#include <QDockWidget>
#include <QEvent>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPointer>
#include <QStatusBar>
#include <QTabBar>

namespace hover_help {
namespace {

constexpr auto kLineName = "hoverHelp";

// A status line shows no line breaks.
QString oneLine(const QString& text) { return QString(text).replace(QLatin1Char('\n'), QLatin1Char(' ')).simplified(); }

// The main window that `widget` belongs to, also from a floating dock; none from a dialog.
QMainWindow* mainWindowOf(QWidget* widget)
{
    QWidget* top = widget->window();
    if (!qobject_cast<QMainWindow*>(top) && !qobject_cast<QDockWidget*>(top))
        return nullptr;
    for (QWidget* w = widget; w; w = w->parentWidget())
        if (auto* main = qobject_cast<QMainWindow*>(w))
            return main;
    return nullptr;
}

void show(QWidget* source, const QString& text)
{
    if (QMainWindow* main = mainWindowOf(source)) {
        if (text.isEmpty())
            main->statusBar()->clearMessage();
        else
            main->statusBar()->showMessage(text);
    } else if (auto* line = source->window()->findChild<QLabel*>(kLineName)) {
        line->setText(text);
    }
}

// The help of what is at `at` in the viewport of `view`: an item, or a heading.
QString itemHelp(const QAbstractItemView* view, QPoint at)
{
    if (const auto* header = qobject_cast<const QHeaderView*>(view)) {
        const int section = header->logicalIndexAt(at);
        if (section < 0 || !header->model())
            return {};
        return header->model()->headerData(section, header->orientation(), Qt::StatusTipRole).toString();
    }
    return view->indexAt(at).data(Qt::StatusTipRole).toString();
}

class Filter : public QObject {
public:
    using QObject::QObject;

    void say(QWidget* source, const QString& text)
    {
        const QString line = oneLine(text);
        if (line.isEmpty()) {
            // Only the widget whose help is shown takes it away.
            if (!source_ || source != source_)
                return;
            show(source, {});
            source_ = nullptr;
            said_.clear();
            return;
        }
        if (source == source_ && line == said_)
            return;
        show(source, line);
        source_ = source;
        said_ = line;
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        const QEvent::Type type = event->type();
        if (type == QEvent::ToolTip)
            return true;  // never a tooltip
        if ((type != QEvent::Enter && type != QEvent::Leave && type != QEvent::MouseMove) || !watched->isWidgetType())
            return false;
        auto* widget = static_cast<QWidget*>(watched);
        if (type == QEvent::Leave) {
            say(widget, {});
            return false;
        }
        const QPoint at = type == QEvent::MouseMove ? static_cast<QMouseEvent*>(event)->position().toPoint()
                                                    : widget->mapFromGlobal(QCursor::pos());
        if (auto* view = qobject_cast<QAbstractItemView*>(widget->parentWidget()); view && view->viewport() == widget) {
            if (type == QEvent::Enter)
                widget->setMouseTracking(true);  // the help follows the item under the pointer
            say(widget, itemHelp(view, at));
        } else if (auto* tabs = qobject_cast<QTabBar*>(widget)) {
            if (type == QEvent::Enter)
                tabs->setMouseTracking(true);
            const int tab = tabs->tabAt(at);
            say(widget, tab >= 0 ? tabs->tabToolTip(tab) : QString());
        } else if (type == QEvent::Enter && !widget->statusTip().isEmpty()) {
            say(widget, widget->statusTip());
        }
        return false;
    }

private:
    QPointer<QWidget> source_;
    QString said_;
};

Filter* filter()
{
    static Filter* const instance = [] {
        auto* f = new Filter(qApp);
        qApp->installEventFilter(f);
        return f;
    }();
    return instance;
}

}  // namespace

void install() { filter(); }

QLabel* line(QWidget* dialog, int lines)
{
    install();
    auto* label = new QLabel(dialog);
    label->setObjectName(kLineName);
    label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    label->setMargin(2);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setFixedHeight(lines * label->fontMetrics().lineSpacing() + 2 * (label->margin() + label->frameWidth()) + 2);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    return label;
}

void say(QWidget* source, const QString& text) { filter()->say(source, text); }

}  // namespace hover_help
