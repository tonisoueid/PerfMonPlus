// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/sampling_service.hpp>

#include <perfmonplus/core/gap.hpp>
#include <perfmonplus/linux/cpu_provider.hpp>
#include <perfmonplus/linux/disk_provider.hpp>
#include <perfmonplus/linux/energy_provider.hpp>
#include <perfmonplus/linux/gpu_provider.hpp>
#include <perfmonplus/linux/memory_provider.hpp>
#include <perfmonplus/linux/net_provider.hpp>
#include <perfmonplus/linux/npu_provider.hpp>
#include <perfmonplus/linux/process_provider.hpp>
#include <perfmonplus/linux/thermal_provider.hpp>

#include <time.h>

#include <algorithm>
#include <unistd.h>

namespace pmp::gui {
namespace {

std::vector<std::unique_ptr<Provider>> liveProviders(linux::ProcessProvider*& processes) {
    const auto root = linux::ProcRoot::live();
    linux::ProcessOptions po;
    po.selfPid = getpid();
    std::vector<std::unique_ptr<Provider>> ps;
    ps.push_back(std::make_unique<linux::CpuProvider>(root));
    ps.push_back(std::make_unique<linux::MemoryProvider>(root));
    ps.push_back(std::make_unique<linux::DiskProvider>(root));
    ps.push_back(std::make_unique<linux::NetProvider>(root));
    auto processProvider = std::make_unique<linux::ProcessProvider>(root, std::move(po));
    processes = processProvider.get();
    ps.push_back(std::move(processProvider));
    // After the process provider: GpuProvider annotates the live process records.
    ps.push_back(std::make_unique<linux::GpuProvider>(root));
    ps.push_back(std::make_unique<linux::NpuProvider>(root));
    ps.push_back(std::make_unique<linux::EnergyProvider>(root));
    ps.push_back(std::make_unique<linux::ThermalProvider>(root));
    return ps;
}

constexpr long long kMinIntervalMs = 5;  // a zero or negative interval must not spin the sampler

std::chrono::nanoseconds bootTime() {
    timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);
}

}  // namespace

SamplingService::SamplingService(std::chrono::milliseconds interval, std::vector<std::unique_ptr<Provider>> providers,
                                 QObject* parent)
    : QObject(parent),
      sampler_(providers.empty() ? liveProviders(processProvider_) : std::move(providers)),
      intervalMs_(std::max<long long>(kMinIntervalMs, interval.count())) {
    qRegisterMetaType<pmp::gui::FrameUpdate>();
}

SamplingService::~SamplingService() {
    {
        std::lock_guard lock(mutex_);  // set under the lock so the worker cannot miss the wake-up
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void SamplingService::start() {
    if (thread_.joinable()) return;
    thread_ = std::thread([this] { run(); });
}

void SamplingService::setInterval(std::chrono::milliseconds interval) {
    {
        std::lock_guard lock(mutex_);
        intervalMs_ = std::max<long long>(kMinIntervalMs, interval.count());
    }
    wake_.notify_all();  // re-evaluate the schedule immediately
}

void SamplingService::setProviderEnabled(const std::string& id, bool enabled) {
    {
        std::lock_guard lock(mutex_);
        pendingEnable_.emplace_back(id, enabled);
    }
    wake_.notify_all();
}

void SamplingService::setCollectCommandLines(bool on) {
    if (processProvider_) processProvider_->setCollectCommandLine(on);
}

void SamplingService::setPaused(bool paused) {
    {
        std::lock_guard lock(mutex_);  // under the lock: the worker re-checks it before it sleeps, so it cannot miss this
        paused_ = paused;
    }
    wake_.notify_all();
}

void SamplingService::run() {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    MonoTime prevMono{};
    std::chrono::nanoseconds prevBoot{};
    bool first = true;
    bool wasPaused = false;
    auto next = last;

    while (!stop_) {
        {
            std::unique_lock lock(mutex_);
            long long seenInterval = intervalMs_;
            while (!stop_ && clock::now() < next) {
                if (wake_.wait_until(lock, next) == std::cv_status::no_timeout && intervalMs_ != seenInterval) {
                    seenInterval = intervalMs_;
                    next = last + std::chrono::milliseconds(seenInterval);  // apply a new interval right away
                }
            }
        }
        if (stop_) break;
        if (paused_) {  // nothing is sampled or published; wait until resumed, the stop request or a new setting wakes us
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !paused_; });
            next = clock::now();
            wasPaused = true;
            continue;
        }
        {
            std::lock_guard lock(mutex_);
            for (const auto& [id, enabled] : pendingEnable_) sampler_.setEnabled(id, enabled);
            pendingEnable_.clear();
        }

        const auto interval = std::chrono::milliseconds(intervalMs_.load());
        const auto mono = clock::now();
        const auto boot = bootTime();
        const bool gap = !first && shouldInsertGap(interval, mono - prevMono, boot - prevBoot);
        // After a pause or a suspend the first delta would span the whole stretch: start every rate over (it reads Pending).
        if (gap || wasPaused) sampler_.resetAll();
        wasPaused = false;
        auto frame = std::make_shared<const Frame>(sampler_.sample(mono, boot, std::chrono::system_clock::now()));
        prevMono = mono;
        prevBoot = boot;
        first = false;
        last = mono;

        FrameUpdate update{std::move(frame), gap};
        QMetaObject::invokeMethod(this, [this, update]() mutable { emit frameReady(std::move(update)); },
                                  Qt::QueuedConnection);

        next += interval;
        if (next < clock::now()) next = clock::now();  // never burst to catch up after a stall or suspend
    }
}

}  // namespace pmp::gui
