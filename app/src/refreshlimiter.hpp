// SPDX-License-Identifier: GPL-3.0-or-later
// Lets numeric readouts change at most about three times a second, as a bench instrument's do: at the frame rate
// their digits change faster than they can be read. The newest values always show, at the latest when the interval
// ends.
#pragma once

#include <chrono>
#include <functional>
#include <utility>

#include <QTimer>

class RefreshLimiter {
public:
    static constexpr std::chrono::milliseconds kInterval{333};

    explicit RefreshLimiter(std::function<void()> refresh) : refresh_(std::move(refresh))
    {
        timer_.setSingleShot(true);
        timer_.setInterval(kInterval);
        QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] {
            if (pending_)
                run();
        });
    }

    // New values: shown now if the last ones have been up for the interval, else when they have.
    void request()
    {
        if (timer_.isActive())
            pending_ = true;
        else
            run();
    }

    // Forget the interval, as after a change the user made: the next values show at once.
    void restart()
    {
        timer_.stop();
        pending_ = false;
    }

private:
    void run()
    {
        pending_ = false;
        refresh_();
        timer_.start();
    }

    QTimer timer_;
    std::function<void()> refresh_;
    bool pending_ = false;
};
