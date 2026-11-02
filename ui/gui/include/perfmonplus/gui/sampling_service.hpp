// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/sampler.hpp>

#include <QMetaType>
#include <QObject>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pmp::linux {
class ProcessProvider;
}

namespace pmp::gui {

struct FrameUpdate {
    std::shared_ptr<const Frame> frame;
    bool gapBefore = false;
};

// Runs the P0 Sampler on its own thread and delivers each frame on the UI
// thread. The UI thread never blocks on sampling.
class SamplingService : public QObject {
    Q_OBJECT
public:
    // `providers` empty -> the live Linux providers (this process excluded).
    explicit SamplingService(std::chrono::milliseconds interval = std::chrono::milliseconds(1000),
                             std::vector<std::unique_ptr<Provider>> providers = {}, QObject* parent = nullptr);
    ~SamplingService() override;

    void start();
    void setInterval(std::chrono::milliseconds interval);
    // Both are thread-safe and take effect before the next sample.
    void setProviderEnabled(const std::string& id, bool enabled);
    void setPaused(bool paused);
    // Command lines are private: collected only while the Command line column or details pane needs them.
    void setCollectCommandLines(bool on);

signals:
    void frameReady(pmp::gui::FrameUpdate update);

private:
    void run();

    linux::ProcessProvider* processProvider_ = nullptr;  // owned by sampler_; null for injected providers
    Sampler sampler_;
    std::atomic<long long> intervalMs_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> paused_{false};
    std::vector<std::pair<std::string, bool>> pendingEnable_;  // guarded by mutex_
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
};

}  // namespace pmp::gui

Q_DECLARE_METATYPE(pmp::gui::FrameUpdate)
