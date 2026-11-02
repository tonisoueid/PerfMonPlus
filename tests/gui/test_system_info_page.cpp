// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/system_info_page.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QThread>
#include <QTimer>

#include <atomic>

using namespace pmp;
using namespace pmp::gui;

namespace {

linux::SystemInfo sample() {
    linux::SystemInfo s;
    s.hostname = "framework";
    s.osName = "Ubuntu 26.04.1 LTS";
    s.serial = "ABC123";
    linux::NetAdapter a;
    a.name = "eth0";
    a.mac = "00:11:22:33:44:55";
    s.adapters = {a};
    return s;
}

}  // namespace

TEST_CASE("the page lists its categories", "[gui][system]") {
    SystemInfoPage page;
    page.setInfo(sample());
    REQUIRE(page.categories() == QStringList({"Overview", "Processor", "Memory", "Graphics", "Network adapters", "Displays", "Sensors"}));
}

TEST_CASE("selecting a category shows its rows", "[gui][system]") {
    SystemInfoPage page;
    page.setInfo(sample());
    page.selectCategory("Overview");
    bool found = false;
    for (int r = 0; r < page.table()->rowCount(); ++r) found = found || page.table()->item(r, 1)->text() == "Ubuntu 26.04.1 LTS";
    REQUIRE(found);
}

TEST_CASE("the exports obey the identifiers checkbox", "[gui][system]") {
    SystemInfoPage page;
    page.setInfo(sample());
    REQUIRE_FALSE(page.includeIdentifiersCheck()->isChecked());
    REQUIRE_FALSE(page.exportText().contains("ABC123"));
    REQUIRE_FALSE(page.exportJson().contains("00:11:22:33:44:55"));
    page.includeIdentifiersCheck()->setChecked(true);
    REQUIRE(page.exportText().contains("ABC123"));
    REQUIRE(page.exportJson().contains("00:11:22:33:44:55"));
    REQUIRE_FALSE(QJsonDocument::fromJson(page.exportJson().toUtf8()).isNull());
}

TEST_CASE("the page never animates", "[gui][system]") {
    SystemInfoPage page;
    REQUIRE_FALSE(page.animates());
}

TEST_CASE("runAsync does its work off the UI thread and delivers on it", "[gui][system]") {
    QObject context;
    QThread* workThread = nullptr;
    QThread* doneThread = nullptr;
    int result = 0;
    QEventLoop loop;
    runAsync<int>(
        &context, [&] { workThread = QThread::currentThread(); return 42; },
        [&](int v) {
            doneThread = QThread::currentThread();
            result = v;
            loop.quit();
        });
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    REQUIRE(result == 42);
    REQUIRE(workThread != QCoreApplication::instance()->thread());
    REQUIRE(doneThread == QCoreApplication::instance()->thread());
}

TEST_CASE("runAsync results are dropped when the context is gone", "[gui][system]") {
    std::atomic<bool> delivered{false};
    std::atomic<bool> started{false};
    {
        QObject context;
        runAsync<int>(
            &context, [&] { started = true; QThread::msleep(100); return 1; }, [&](int) { delivered = true; });
        while (!started) QThread::msleep(1);
    }
    QEventLoop loop;
    QTimer::singleShot(400, &loop, &QEventLoop::quit);
    loop.exec();
    REQUIRE_FALSE(delivered);
}

TEST_CASE("an export that cannot be written says so", "[gui][system][minor]") {
    SystemInfoPage page;
    page.setInfo(sample());
    REQUIRE_FALSE(page.saveTo("/proc/pmp-no-such-dir/export.txt", false));
    REQUIRE(page.exportStatus().startsWith("Could not save /proc/pmp-no-such-dir/export.txt: "));
    QTemporaryDir dir;
    REQUIRE(page.saveTo(dir.filePath("ok.json"), true));
    REQUIRE(page.exportStatus() == "Saved " + dir.filePath("ok.json"));
    QFile f(dir.filePath("ok.json"));
    REQUIRE(f.open(QIODevice::ReadOnly));
    REQUIRE(f.readAll().contains("Overview"));
}

TEST_CASE("the Sensors section follows every provider, not only the first", "[gui][system][minor]") {
    SystemInfoPage page;
    page.setInfo(sample());
    auto frame = [](HealthState second, const char* err) {
        Frame f;
        ProviderHealth a, b;
        a.id = "cpu";
        b.id = "gpu";
        b.state = second;
        b.lastError = err;
        f.providerHealth = {a, b};
        return f;
    };
    HistoryStore h;
    page.update(frame(HealthState::Healthy, ""), h, MonoTime{});
    page.selectCategory("Sensors");
    REQUIRE(page.exportText().contains("gpu"));
    REQUIRE_FALSE(page.exportText().contains("Failed"));
    page.update(frame(HealthState::Failed, "boom"), h, MonoTime{});  // only the second provider changed
    REQUIRE(page.exportText().contains("Failed: boom"));
}
