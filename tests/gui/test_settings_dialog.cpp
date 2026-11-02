// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/format.hpp>
#include <perfmonplus/core/version.hpp>
#include <perfmonplus/gui/main_window.hpp>
#include <perfmonplus/gui/settings_dialog.hpp>
#include <perfmonplus/gui/processes_page.hpp>
#include <perfmonplus/gui/summary_page.hpp>

#include "support/frame_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <unistd.h>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <memory>

using namespace pmp;
using namespace pmp::gui;
using namespace pmp::testing;
using namespace std::chrono_literals;

namespace {

class TickProvider final : public Provider {
public:
    explicit TickProvider(std::shared_ptr<std::atomic<int>> calls) : calls_(std::move(calls)) {}
    std::string_view id() const override { return "tick"; }
    void sample(Frame& out, const SampleContext&) override {
        ++*calls_;
        out.cpu.total = Metric::measured(10.0, Unit::Percent);
    }
    void fillUnavailable(Frame& out, Availability why) const override {
        out.cpu.total = Metric::notMeasured(why, Unit::Percent);
    }

private:
    std::shared_ptr<std::atomic<int>> calls_;
};

// Spin the event loop for `ms`, counting delivered frames.
int framesWithin(SamplingService& s, int ms) {
    int n = 0;
    QEventLoop loop;
    QObject::connect(&s, &SamplingService::frameReady, &loop, [&](FrameUpdate) { ++n; });
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
    return n;
}

struct FormatGuard {  // format options are process-wide; never leak them into other tests
    ~FormatGuard() { setFormatOptions({}); }
};

std::vector<ProviderHealth> twoProviders() {
    ProviderHealth a, b;
    a.id = "cpu";
    b.id = "gpu";
    return {a, b};
}

}  // namespace

TEST_CASE("bad saved values snap to defaults", "[gui][settings]") {
    QTemporaryDir dir;
    QSettings store(dir.filePath("s.ini"), QSettings::IniFormat);
    store.setValue("sampleIntervalMs", 750);
    store.setValue("topProcessCount", 99);
    store.setValue("disabledProviders", QStringList{"gpu", "", "gpu"});
    const auto s = AppSettings::load(store);
    REQUIRE(s.sampleIntervalMs == 1000);
    REQUIRE(s.topProcessCount == 12);
    REQUIRE(s.disabledProviders == QStringList{"gpu"});
}

TEST_CASE("the new settings survive a save and load", "[gui][settings]") {
    QTemporaryDir dir;
    QSettings store(dir.filePath("s.ini"), QSettings::IniFormat);
    AppSettings s;
    s.sampleIntervalMs = 2000;
    s.topProcessCount = 20;
    s.decimalUnits = true;
    s.fahrenheit = true;
    s.checkUpdates = false;
    s.updateManifestUrl = "https://example.org/perfmonplus.json";
    s.launchAtLogin = true;
    s.disabledProviders = {"gpu", "npu"};
    s.save(store);
    store.sync();
    REQUIRE(AppSettings::load(store) == s);
}

TEST_CASE("paused stops sampling", "[gui][settings]") {
    auto calls = std::make_shared<std::atomic<int>>(0);
    std::vector<std::unique_ptr<Provider>> ps;
    ps.push_back(std::make_unique<TickProvider>(calls));
    SamplingService svc(20ms, std::move(ps));
    svc.start();
    REQUIRE(framesWithin(svc, 400) >= 2);

    svc.setPaused(true);
    framesWithin(svc, 150);  // let a frame already in flight arrive
    const int before = calls->load();
    REQUIRE(framesWithin(svc, 300) == 0);
    REQUIRE(calls->load() == before);

    svc.setPaused(false);
    REQUIRE(framesWithin(svc, 600) >= 1);
}

TEST_CASE("disabling a provider through the service stops its sampling", "[gui][settings]") {
    auto calls = std::make_shared<std::atomic<int>>(0);
    std::vector<std::unique_ptr<Provider>> ps;
    ps.push_back(std::make_unique<TickProvider>(calls));
    SamplingService svc(20ms, std::move(ps));
    svc.start();
    REQUIRE(framesWithin(svc, 300) >= 2);
    svc.setProviderEnabled("tick", false);
    framesWithin(svc, 150);
    const int before = calls->load();
    framesWithin(svc, 300);
    REQUIRE(calls->load() == before);
    svc.setProviderEnabled("tick", true);
    framesWithin(svc, 300);
    REQUIRE(calls->load() > before);
}

TEST_CASE("status bar does not count disabled as degraded", "[gui][shell]") {
    MainWindow w;
    Frame f = populatedFrame(4, 3);
    ProviderHealth ok, off;
    ok.id = "cpu";
    off.id = "gpu";
    off.state = HealthState::Disabled;
    f.providerHealth = {ok, off};
    w.applyFrame({std::make_shared<const Frame>(f), false});
    REQUIRE(w.statusText().startsWith("Native providers healthy"));
    REQUIRE(w.statusText().contains("1 disabled"));
}

TEST_CASE("fahrenheit applies live", "[gui][settings]") {
    FormatGuard guard;
    MainWindow w;
    Frame f = populatedFrame(4, 3);
    withHardware(f);
    w.applyFrame({std::make_shared<const Frame>(f), false});
    auto* summary = qobject_cast<SummaryPage*>(w.pageWidget(Page::Summary));
    REQUIRE(summary);
    const auto showsUnit = [&](const QString& unit) {
        for (auto* tile : summary->tiles()) {
            if (tile->valueText().endsWith(unit)) return true;
        }
        return false;
    };
    REQUIRE(showsUnit("°C"));

    auto s = w.settings();
    s.fahrenheit = true;
    w.applySettings(s);
    REQUIRE(showsUnit("°F"));
    REQUIRE_FALSE(showsUnit("°C"));
}

TEST_CASE("decimal units apply live", "[gui][settings]") {
    FormatGuard guard;
    MainWindow w;
    auto s = w.settings();
    s.decimalUnits = true;
    w.applySettings(s);
    REQUIRE_FALSE(formatOptions().binaryBytes);
}

TEST_CASE("launch at login writes the autostart file", "[gui][settings]") {
    QTemporaryDir dir;
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    MainWindow w;
    auto s = w.settings();
    s.launchAtLogin = true;
    w.applySettings(s);
    const QString file = dir.filePath("autostart/perfmonplus.desktop");
    REQUIRE(QFile::exists(file));
    s.launchAtLogin = false;
    w.applySettings(s);
    REQUIRE_FALSE(QFile::exists(file));
    qunsetenv("XDG_CONFIG_HOME");
}

TEST_CASE("settings dialog returns the settings it was given", "[gui][settings]") {
    AppSettings s;
    s.sampleIntervalMs = 2000;
    s.topProcessCount = 20;
    s.historySeconds = 300;
    s.decimalUnits = true;
    s.fahrenheit = true;
    s.checkUpdates = false;
    s.updateManifestUrl = "https://example.org/perfmonplus.json";
    s.launchAtLogin = true;
    s.frameRate = 30;
    s.bloom = false;
    s.lowEffects = true;
    s.disabledProviders = {"gpu"};
    SettingsDialog dlg(s, twoProviders());
    REQUIRE(dlg.result() == s);
}

TEST_CASE("unticking a provider disables it", "[gui][settings]") {
    SettingsDialog dlg(AppSettings{}, twoProviders());
    auto* table = dlg.providerTable();
    REQUIRE(table->rowCount() == 2);
    table->item(1, 0)->setCheckState(Qt::Unchecked);
    REQUIRE(dlg.result().disabledProviders == QStringList{"gpu"});
}

TEST_CASE("about tab shows version and licence", "[gui][settings]") {
    SettingsDialog dlg(AppSettings{}, twoProviders());
    auto* tabs = dlg.tabs();
    QStringList names;
    for (int i = 0; i < tabs->count(); ++i) names << tabs->tabText(i);
    REQUIRE(names == QStringList{"General", "Sampling", "Display", "Privacy", "Advanced", "About"});
    QString text;
    for (auto* label : tabs->widget(tabs->count() - 1)->findChildren<QLabel*>()) text += label->text() + "\n";
    REQUIRE(text.contains("perfmonplus " + QString::fromUtf8(versionString().data(), static_cast<int>(versionString().size()))));
    REQUIRE(text.contains("GPL-3.0-or-later"));
}

TEST_CASE("check now sends the address typed in the dialog", "[gui][settings]") {
    SettingsDialog dlg(AppSettings{}, twoProviders());
    QString seen;
    QObject::connect(&dlg, &SettingsDialog::checkNowRequested, &dlg, [&](const QString& url) { seen = url; });
    dlg.manifestUrlEdit()->setText("https://example.org/m.json");
    dlg.checkNowButton()->click();
    REQUIRE(seen == "https://example.org/m.json");
    dlg.setUpdateStatus("Up to date");
    REQUIRE(dlg.updateStatusText() == "Up to date");
}

TEST_CASE("choices made in the settings dialog are kept for the next run", "[gui][settings]") {
    QTemporaryDir dir;
    QSettings store(dir.filePath("s.ini"), QSettings::IniFormat);
    MainWindow w;
    std::optional<AppSettings> emitted;
    QObject::connect(&w, &MainWindow::settingsChanged, &w, [&](AppSettings s) { emitted = s; });
    auto chosen = w.settings();
    chosen.sampleIntervalMs = 2000;
    chosen.topProcessCount = 20;
    chosen.historySeconds = 300;
    chosen.decimalUnits = true;
    chosen.fahrenheit = true;
    chosen.updateManifestUrl = "https://example.org/m.json";
    chosen.disabledProviders = {"gpu"};
    w.applySettings(chosen);
    REQUIRE(emitted.has_value());

    AppSettings persisted;           // what main() holds: the loaded settings
    persisted.geometry = "keep me";  // window state is not a user choice made in the dialog
    persisted.takeUserChoices(*emitted);
    persisted.save(store);
    store.sync();
    const auto reloaded = AppSettings::load(store);
    REQUIRE(reloaded.sampleIntervalMs == 2000);
    REQUIRE(reloaded.topProcessCount == 20);
    REQUIRE(reloaded.historySeconds == 300);
    REQUIRE(reloaded.decimalUnits);
    REQUIRE(reloaded.fahrenheit);
    REQUIRE(reloaded.updateManifestUrl == "https://example.org/m.json");
    REQUIRE(reloaded.disabledProviders == QStringList{"gpu"});
    REQUIRE(reloaded.geometry == QByteArray("keep me"));
}

TEST_CASE("the saved interval is the one sampling starts with", "[gui][settings]") {
    AppSettings s;
    s.sampleIntervalMs = 2000;
    REQUIRE(s.startupInterval() == 2000ms);
    s.sampleIntervalMs = 0;  // paused: the service still needs a sane interval for when it resumes
    REQUIRE(s.startupInterval() == 1000ms);
}

TEST_CASE("the dialog offers the five tombstone choices and returns the chosen one", "[gui][settings][processes]") {
    SettingsDialog dlg(AppSettings{}, twoProviders());
    auto* combo = dlg.tombstoneCombo();
    std::vector<int> values;
    for (int i = 0; i < combo->count(); ++i) values.push_back(combo->itemData(i).toInt());
    REQUIRE(values == std::vector<int>{0, 5, 10, 30, 60});
    REQUIRE(combo->currentData().toInt() == 10);
    combo->setCurrentIndex(4);
    REQUIRE(dlg.result().tombstoneSeconds == 60);
}

TEST_CASE("the process options default sensibly and round trip through the dialog", "[gui][settings][processes]") {
    AppSettings defaults;
    REQUIRE_FALSE(defaults.showEnvironment);   // environments often hold secrets: opt in
    REQUIRE(defaults.processHistoryEnabled);
    SettingsDialog dlg(defaults, twoProviders());
    REQUIRE_FALSE(dlg.environmentBox()->isChecked());
    REQUIRE(dlg.processHistoryBox()->isChecked());
    dlg.environmentBox()->setChecked(true);
    dlg.processHistoryBox()->setChecked(false);
    const auto r = dlg.result();
    REQUIRE(r.showEnvironment);
    REQUIRE_FALSE(r.processHistoryEnabled);

    QTemporaryDir dir;
    QSettings store(dir.filePath("s.ini"), QSettings::IniFormat);
    r.save(store);
    store.sync();
    REQUIRE(AppSettings::load(store) == r);
}

TEST_CASE("tombstone retention applies live", "[gui][settings][processes]") {
    MainWindow w;
    w.show();
    w.showPage(Page::Processes);
    Frame f;
    f.generation = 1;
    f.mono = MonoTime{} + 100s;
    f.wall = std::chrono::system_clock::time_point{} + 1700000000s;
    f.memory.total = Metric::measured(10000, Unit::Bytes);
    ProcessRecord live;
    live.key = {1, 10};
    live.name = "live";
    live.user = "dave";
    live.cpu = Metric::measured(1, Unit::Percent);
    ProcessRecord gone = live;
    gone.key = {2, 20};
    gone.name = "gone";
    gone.exitedAt = MonoTime{} + 97s;
    f.processes.processes = {live, gone};
    w.applyFrame({std::make_shared<const Frame>(f), false});
    auto* page = qobject_cast<ProcessesPage*>(w.pageWidget(Page::Processes));
    REQUIRE(page);
    page->setView(vm::ProcessView::Flat);
    REQUIRE(page->model()->rowOfId("p:2:20") >= 0);
    auto s = w.settings();
    s.tombstoneSeconds = 0;
    w.applySettings(s);
    REQUIRE(page->model()->rowOfId("p:2:20") < 0);
    s.tombstoneSeconds = 10;
    w.applySettings(s);
    REQUIRE(page->model()->rowOfId("p:2:20") >= 0);
}

TEST_CASE("turning process history off stops tracking and clears it", "[gui][settings][processes]") {
    MainWindow w;
    Frame f;
    f.generation = 1;
    f.mono = MonoTime{} + 10s;
    ProcessRecord p;
    p.key = {1, 10};
    p.name = "a";
    p.cpu = Metric::measured(1, Unit::Percent);
    f.processes.processes = {p};
    w.applyFrame({std::make_shared<const Frame>(f), false});
    REQUIRE(w.processHistory().tracked() == 1);
    auto s = w.settings();
    s.processHistoryEnabled = false;
    w.applySettings(s);
    REQUIRE(w.processHistory().tracked() == 0);
    f.generation = 2;
    f.mono = MonoTime{} + 11s;
    w.applyFrame({std::make_shared<const Frame>(f), false});
    REQUIRE(w.processHistory().tracked() == 0);
}

TEST_CASE("a command line column restored from settings starts collecting", "[gui][settings][processes]") {
    AppSettings s;
    s.processColumns = {"name", "pid", "cmdline"};
    s.processView = 2;  // flat
    s.lastPage = static_cast<int>(Page::Processes);
    MainWindow w(s, std::make_unique<SamplingService>(30ms));
    w.resize(1200, 800);
    w.show();
    auto* page = qobject_cast<ProcessesPage*>(w.pageWidget(Page::Processes));
    REQUIRE(page);
    REQUIRE(page->wantsCommandLines());
    int column = -1;
    for (int c = 0; c < static_cast<int>(page->columns().size()); ++c) {
        if (page->columns()[static_cast<std::size_t>(c)] == vm::ProcessColumn::CommandLine) column = c;
    }
    REQUIRE(column >= 0);
    bool filled = false;
    QEventLoop loop;
    QTimer poll;
    QTimer giveUp;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        for (int r = 0; r < page->model()->rowCount() && !filled; ++r) {
            const auto text = page->model()->index(r, column).data().toString();
            if (!text.isEmpty() && text != "—") filled = true;
        }
        if (filled) loop.quit();
    });
    QObject::connect(&giveUp, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(100);
    giveUp.start(6000);
    loop.exec();
    REQUIRE(filled);  // at least one process has a real command line, so the provider was told to collect them
}

TEST_CASE("recording settings validate", "[gui][settings][recording]") {
    QTemporaryDir dir;
    QSettings store(dir.filePath("s.ini"), QSettings::IniFormat);
    store.setValue("recordingRingMinutes", 0);
    store.setValue("recordingMaxMinutes", 2);
    store.setValue("recordingTopProcesses", 500);
    AppSettings s = AppSettings::load(store);
    REQUIRE(s.recordingRingMinutes == 1);
    REQUIRE(s.recordingMaxMinutes == 5);
    REQUIRE(s.recordingTopProcesses == 20);  // out of range: back to the default
    store.setValue("recordingRingMinutes", 999);
    store.setValue("recordingMaxMinutes", 9999);
    s = AppSettings::load(store);
    REQUIRE(s.recordingRingMinutes == 60);
    REQUIRE(s.recordingMaxMinutes == 120);
}

TEST_CASE("recent recordings keep 8, newest first, and drop missing files", "[gui][settings][recording]") {
    QTemporaryDir dir;
    QSettings store(dir.filePath("s.ini"), QSettings::IniFormat);
    QStringList paths;
    for (int i = 0; i < 10; ++i) {
        const QString p = dir.filePath(QString("r%1.pmptrace").arg(i));
        QFile f(p);
        REQUIRE(f.open(QIODevice::WriteOnly));
        paths << p;
    }
    store.setValue("recentRecordings", paths + QStringList{dir.filePath("gone.pmptrace"), paths[0]});
    const auto s = AppSettings::load(store);
    REQUIRE(s.recentRecordings.size() == 8);
    REQUIRE(s.recentRecordings.first() == paths.first());
    REQUIRE_FALSE(s.recentRecordings.contains(dir.filePath("gone.pmptrace")));
    auto copy = s.recentRecordings;
    REQUIRE(copy.removeDuplicates() == 0);
    AppSettings t;
    t.addRecent("/a");
    t.addRecent("/b");
    t.addRecent("/a");
    REQUIRE(t.recentRecordings == QStringList({"/a", "/b"}));
}

TEST_CASE("recording options survive a save and load", "[gui][settings][recording]") {
    QTemporaryDir dir;
    QSettings store(dir.filePath("s.ini"), QSettings::IniFormat);
    AppSettings s;
    s.recordingRing = true;
    s.recordingRingMinutes = 15;
    s.recordingMaxMinutes = 45;
    s.recordingTopProcesses = 30;
    s.resolveHostNames = true;
    s.save(store);
    store.sync();
    REQUIRE(AppSettings::load(store) == s);
}

TEST_CASE("settings dialog edits recording options", "[gui][settings][recording]") {
    AppSettings s;
    SettingsDialog dlg(s, {});
    REQUIRE(dlg.recordingRingBox()->isChecked() == false);
    dlg.recordingRingBox()->setChecked(true);
    dlg.recordingRingMinutesSpin()->setValue(20);
    dlg.recordingMaxMinutesSpin()->setValue(60);
    dlg.recordingTopSpin()->setValue(25);
    const auto r = dlg.result();
    REQUIRE(r.recordingRing);
    REQUIRE(r.recordingRingMinutes == 20);
    REQUIRE(r.recordingMaxMinutes == 60);
    REQUIRE(r.recordingTopProcesses == 25);
    REQUIRE(dlg.resolveHostsBox()->isChecked() == false);  // off unless the user asks
    dlg.resolveHostsBox()->setChecked(true);
    REQUIRE(dlg.result().resolveHostNames);
    REQUIRE(dlg.recordingRingMinutesSpin()->isEnabled());
    dlg.recordingRingBox()->setChecked(false);
    REQUIRE_FALSE(dlg.recordingRingMinutesSpin()->isEnabled());
}

TEST_CASE("switching per-process history off tells the pages, so they can say so", "[gui][settings][minor]") {
    MainWindow w;
    auto* summary = dynamic_cast<SummaryPage*>(w.pageWidget(Page::Summary));
    REQUIRE(summary->culprits()->history() != nullptr);
    AppSettings s = w.settings();
    s.processHistoryEnabled = false;
    w.applySettings(s);
    REQUIRE(summary->culprits()->history() == nullptr);  // the popover then explains how to turn it back on
    s.processHistoryEnabled = true;
    w.applySettings(s);
    REQUIRE(summary->culprits()->history() != nullptr);
    AppSettings off;
    off.processHistoryEnabled = false;
    MainWindow w2(off);
    REQUIRE(dynamic_cast<SummaryPage*>(w2.pageWidget(Page::Summary))->culprits()->history() == nullptr);  // also from the start
}

namespace {
class ResetCountingProvider final : public Provider {
public:
    explicit ResetCountingProvider(std::shared_ptr<std::atomic<int>> resets) : resets_(std::move(resets)) {}
    std::string_view id() const override { return "rate"; }
    void sample(Frame& out, const SampleContext&) override { out.cpu.total = Metric::measured(1.0, Unit::Percent); }
    void fillUnavailable(Frame& out, Availability why) const override { out.cpu.total = Metric::notMeasured(why, Unit::Percent); }
    void reset() override { ++*resets_; }

private:
    std::shared_ptr<std::atomic<int>> resets_;
};
}  // namespace

TEST_CASE("resuming from a pause starts every rate over", "[gui][settings][minor]") {
    auto resets = std::make_shared<std::atomic<int>>(0);
    std::vector<std::unique_ptr<Provider>> ps;
    ps.push_back(std::make_unique<ResetCountingProvider>(resets));
    SamplingService svc(20ms, std::move(ps));
    svc.start();
    REQUIRE(framesWithin(svc, 400) >= 2);
    const int before = resets->load();
    svc.setPaused(true);
    framesWithin(svc, 150);
    svc.setPaused(false);
    REQUIRE(framesWithin(svc, 600) >= 1);
    REQUIRE(resets->load() == before + 1);  // once, for the pause (no gap was detected, the interval is steady)
}

TEST_CASE("a paused sampler sleeps until it is woken, not on a 50 ms poll", "[gui][settings][minor]") {
    auto calls = std::make_shared<std::atomic<int>>(0);
    std::vector<std::unique_ptr<Provider>> ps;
    ps.push_back(std::make_unique<TickProvider>(calls));
    SamplingService svc(20ms, std::move(ps));
    svc.start();
    svc.setPaused(true);
    framesWithin(svc, 150);
    const int paused = calls->load();
    svc.setPaused(false);  // the wake-up is immediate
    REQUIRE(framesWithin(svc, 200) >= 2);  // sampling is back within 200 ms: no lost wake-up
    REQUIRE(calls->load() > paused);
}

TEST_CASE("a failed start-at-login write is reported and the setting is not left lying", "[gui][settings][minor]") {
    if (::geteuid() == 0) SKIP("root can write anywhere");
    QTemporaryDir dir;
    QDir(dir.path()).mkpath("config");
    QFile::setPermissions(dir.filePath("config"), QFileDevice::ReadOwner | QFileDevice::ExeOwner);  // read-only
    const QByteArray oldConfig = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", dir.filePath("config").toUtf8());
    MainWindow w;
    AppSettings s = w.settings();
    s.launchAtLogin = true;
    w.applySettings(s);
    const bool kept = w.settings().launchAtLogin;
    const QString notice = w.noticeText();
    QFile::setPermissions(dir.filePath("config"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    if (oldConfig.isEmpty()) qunsetenv("XDG_CONFIG_HOME");
    else qputenv("XDG_CONFIG_HOME", oldConfig);
    REQUIRE_FALSE(kept);
    REQUIRE(notice.startsWith("Could not change the start-at-login setting"));
}
