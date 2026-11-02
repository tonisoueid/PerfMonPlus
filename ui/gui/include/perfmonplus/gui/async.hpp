// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <utility>

namespace pmp::gui {

// Runs `work` on a background thread and calls `done` with its result on `context`'s thread. If `context` is
// destroyed first the result is dropped: the worker never touches `context` (it may already be gone); it posts to
// the application object, and the guard is checked on the UI thread where destruction cannot race.
// How many workers are still running. The app waits for them (briefly) on its way out, so none is left touching an
// application object that no longer exists.
inline std::atomic<int>& asyncInFlight() {
    static std::atomic<int> n{0};
    return n;
}

// True when every worker has finished within `timeout`.
inline bool waitForAsyncWorkers(std::chrono::milliseconds timeout) {
    const auto until = std::chrono::steady_clock::now() + timeout;
    while (asyncInFlight() > 0) {
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

template <class T>
void runAsync(QObject* context, std::function<T()> work, std::function<void(T)> done) {
    QPointer<QObject> guard(context);
    ++asyncInFlight();
    std::thread([guard, work = std::move(work), done = std::move(done)]() mutable {
        T value = work();
        if (auto* app = QCoreApplication::instance()) {  // the application may already be gone: then nobody wants the answer
            QMetaObject::invokeMethod(
                app,
                [guard, done = std::move(done), value = std::move(value)]() mutable {
                    if (guard) done(std::move(value));
                },
                Qt::QueuedConnection);
        }
        --asyncInFlight();
    }).detach();
}

}  // namespace pmp::gui
