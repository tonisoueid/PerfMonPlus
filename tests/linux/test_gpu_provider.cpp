// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/gpu_provider.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <chrono>

using namespace pmp;
using namespace std::chrono_literals;
using Catch::Approx;
using pmp::linux::GpuProvider;

namespace {

std::string fdinfo(unsigned client, unsigned long long renderNs) {
    return "pos:\t0\ndrm-driver:\ti915\ndrm-client-id:\t" + std::to_string(client) +
           "\ndrm-pdev:\t0000:00:02.0\ndrm-engine-render:\t" + std::to_string(renderNs) +
           " ns\ndrm-engine-copy:\t0 ns\ndrm-resident-system0:\t394480 KiB\n";
}

void device(FakeRoot& fr) {
    fr.write("pci.ids", "8086  Intel Corporation\n\t9a49  TigerLake-LP GT2 [Iris Xe Graphics]\n");
    fr.write("sys/devices/pci0000:00/0000:00:02.0/vendor", "0x8086\n");
    fr.write("sys/devices/pci0000:00/0000:00:02.0/device", "0x9a49\n");
    fr.symlink("../../../bus/pci/drivers/i915", "sys/devices/pci0000:00/0000:00:02.0/driver");
    fr.symlink("../../../devices/pci0000:00/0000:00:02.0", "sys/class/drm/card1/device");
    fr.write("sys/class/drm/card1/gt_act_freq_mhz", "1300\n");
    fr.makeDir("sys/class/drm/card1-eDP-1");
    fr.write("sys/class/drm/version", "drm 1.1\n");
}

void client(FakeRoot& fr, int pid, int fd, unsigned clientId, unsigned long long ns) {
    const auto p = "proc/" + std::to_string(pid);
    fr.symlink("/dev/dri/renderD128", p + "/fd/" + std::to_string(fd));
    fr.write(p + "/fdinfo/" + std::to_string(fd), fdinfo(clientId, ns));
}

Frame frameWithProcesses(std::initializer_list<int> pids) {
    Frame f;
    for (int pid : pids) {
        ProcessRecord r;
        r.key = {pid, static_cast<std::uint64_t>(pid)};
        r.name = "p" + std::to_string(pid);
        f.processes.processes.push_back(r);
    }
    f.processes.count = Metric::measured(static_cast<double>(f.processes.processes.size()), Unit::Count);
    return f;
}

Frame sampleAt(GpuProvider& p, Frame f, int gen, MonoTime t) {
    p.sample(f, SampleContext{static_cast<std::uint64_t>(gen), t});
    return f;
}

const ProcessRecord& proc(const Frame& f, int pid) {
    for (const auto& r : f.processes.processes)
        if (r.key.pid == pid) return r;
    FAIL("no process " << pid);
    return f.processes.processes.front();
}

GpuProvider make(FakeRoot& fr) { return GpuProvider(fr.root(), fr.root().proc.parent_path() / "pci.ids", 1); }

const MonoTime t0{};

}  // namespace

TEST_CASE("gpu device identity", "[gpu]") {
    FakeRoot fr;
    device(fr);
    client(fr, 100, 5, 27, 1000000000);
    auto p = make(fr);
    const auto f = sampleAt(p, frameWithProcesses({100, 200}), 1, t0);
    REQUIRE(f.gpus.size() == 1);
    REQUIRE(f.gpus[0].name == "TigerLake-LP GT2 [Iris Xe Graphics]");
    REQUIRE(f.gpus[0].driver == "i915");
    REQUIRE(f.gpus[0].pciSlot == "0000:00:02.0");
    REQUIRE(*f.gpus[0].freqHz.value == Approx(1.3e9));
    REQUIRE(f.gpus[0].util.availability == Availability::Pending);
}

TEST_CASE("utilisation comes from engine time deltas, per device and per process", "[gpu]") {
    FakeRoot fr;
    device(fr);
    fr.symlink("/dev/null", "proc/200/fd/3");
    client(fr, 100, 5, 27, 1000000000);
    auto p = make(fr);
    sampleAt(p, frameWithProcesses({100, 200}), 1, t0);
    fr.write("proc/100/fdinfo/5", fdinfo(27, 1250000000));
    const auto f = sampleAt(p, frameWithProcesses({100, 200}), 2, t0 + 1s);
    REQUIRE(*f.gpus[0].util.value == Approx(25.0));
    REQUIRE(f.gpus[0].engines.size() == 2);
    REQUIRE(f.gpus[0].engines[0].name == "render");
    REQUIRE(*f.gpus[0].engines[0].util.value == Approx(25.0));
    REQUIRE(*proc(f, 100).gpu.value == Approx(25.0));
    REQUIRE(proc(f, 200).gpu.isMeasured());
    REQUIRE(*proc(f, 200).gpu.value == 0.0);
    REQUIRE(*f.gpus[0].memUsed.value == 394480.0 * 1024);
}

TEST_CASE("duplicate clients count once", "[gpu]") {
    FakeRoot fr;
    device(fr);
    client(fr, 100, 5, 27, 1000000000);
    client(fr, 100, 6, 27, 1000000000);  // dup()'d fd
    client(fr, 101, 5, 27, 1000000000);  // forked child sharing the client
    auto p = make(fr);
    sampleAt(p, frameWithProcesses({100, 101}), 1, t0);
    for (auto [pid, fd] : {std::pair{100, 5}, {100, 6}, {101, 5}}) {
        fr.write("proc/" + std::to_string(pid) + "/fdinfo/" + std::to_string(fd), fdinfo(27, 1250000000));
    }
    const auto f = sampleAt(p, frameWithProcesses({100, 101}), 2, t0 + 1s);
    REQUIRE(*f.gpus[0].util.value == Approx(25.0));
    REQUIRE(*f.gpus[0].memUsed.value == 394480.0 * 1024);
}

TEST_CASE("unreadable clients mark the total partial", "[gpu]") {
    if (geteuid() == 0) SKIP("root can read everything");
    FakeRoot fr;
    device(fr);
    client(fr, 100, 5, 27, 1000000000);
    fr.symlink("/dev/null", "proc/300/fd/3");
    fr.makeUnreadable("proc/300/fd");
    auto p = make(fr);
    const auto f = sampleAt(p, frameWithProcesses({100, 300}), 1, t0);
    REQUIRE(f.gpus[0].visibleClientsOnly);
}

TEST_CASE("a machine with no gpu", "[gpu]") {
    FakeRoot fr;
    fr.makeDir("proc/100");
    auto p = make(fr);
    const auto f = sampleAt(p, frameWithProcesses({100}), 1, t0);
    REQUIRE(f.gpus.empty());
    REQUIRE(proc(f, 100).gpu.availability == Availability::Unsupported);
}

TEST_CASE("a gpu is not 0% when the process table is unavailable", "[gpu]") {
    FakeRoot fr;
    device(fr);
    client(fr, 100, 5, 27, 1000000000);
    auto p = make(fr);
    sampleAt(p, frameWithProcesses({100}), 1, t0);
    Frame none;  // the process provider is disabled or failed: nothing to inspect
    none.processes.count = Metric::notMeasured(Availability::Disabled, Unit::Count);
    const auto f = sampleAt(p, none, 2, t0 + 1s);
    REQUIRE(f.gpus.size() == 1);
    REQUIRE_FALSE(f.gpus[0].util.isMeasured());
    REQUIRE_FALSE(f.gpus[0].memUsed.isMeasured());
}

TEST_CASE("a driver that publishes no engine counters is not reported as 0%", "[gpu]") {
    FakeRoot fr;
    device(fr);  // card exists, but no process has a DRM fd with engine keys
    auto p = make(fr);
    sampleAt(p, frameWithProcesses({100}), 1, t0);
    const auto f = sampleAt(p, frameWithProcesses({100}), 2, t0 + 1s);
    REQUIRE(f.gpus.size() == 1);
    REQUIRE_FALSE(f.gpus[0].util.isMeasured());
    REQUIRE(f.gpus[0].util.availability == Availability::Unsupported);
}

TEST_CASE("a client first seen this sample is Pending, not 0%", "[gpu][minor]") {
    FakeRoot fr;
    device(fr);
    client(fr, 100, 5, 27, 1000000000);
    auto p = make(fr);
    sampleAt(p, frameWithProcesses({100, 300}), 1, t0);
    fr.write("proc/100/fdinfo/5", fdinfo(27, 1250000000));
    client(fr, 300, 7, 99, 5000000000ULL);  // a process opened the GPU between two samples: no earlier counter to subtract
    const auto f = sampleAt(p, frameWithProcesses({100, 300}), 2, t0 + 1s);
    REQUIRE(*proc(f, 100).gpu.value == Approx(25.0));
    REQUIRE(proc(f, 300).gpu.availability == Availability::Pending);
    REQUIRE_FALSE(proc(f, 300).gpu.value.has_value());
    // and one sample later it has a delta
    fr.write("proc/300/fdinfo/7", fdinfo(99, 5500000000ULL));
    const auto g = sampleAt(p, frameWithProcesses({100, 300}), 3, t0 + 2s);
    REQUIRE(*proc(g, 300).gpu.value == Approx(50.0));
}

TEST_CASE("amdgpu's device figure comes from sysfs, so it is not marked partial", "[gpu][minor]") {
    if (geteuid() == 0) SKIP("root can read everything");
    FakeRoot fr;
    device(fr);
    fr.remove("sys/devices/pci0000:00/0000:00:02.0/driver");
    fr.symlink("../../../bus/pci/drivers/amdgpu", "sys/devices/pci0000:00/0000:00:02.0/driver");
    fr.write("sys/devices/pci0000:00/0000:00:02.0/gpu_busy_percent", "37\n");
    client(fr, 100, 5, 27, 1000000000);
    fr.symlink("/dev/null", "proc/300/fd/3");
    fr.makeUnreadable("proc/300/fd");
    auto p = make(fr);
    const auto f = sampleAt(p, frameWithProcesses({100, 300}), 1, t0);
    REQUIRE(*f.gpus[0].util.value == 37.0);
    REQUIRE_FALSE(f.gpus[0].visibleClientsOnly);  // the device-wide number does not depend on seeing every client
}
