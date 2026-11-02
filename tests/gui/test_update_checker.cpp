// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/version.hpp>
#include <perfmonplus/gui/main_window.hpp>
#include <perfmonplus/gui/update_checker.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <optional>

using namespace pmp;
using namespace pmp::gui;

namespace {

struct Outcome {
    std::optional<QString> version;
    std::optional<QUrl> page;
    bool upToDate = false;
    std::optional<QString> failure;
};

QString writeManifest(QTemporaryDir& dir, const QByteArray& body) {
    const QString path = dir.filePath("manifest.json");
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write(body);
    return path;
}

// Runs one check and waits (up to 2 s) for its outcome.
Outcome run(UpdateChecker& c, const QUrl& url, bool expectStarted = true) {
    Outcome o;
    QEventLoop loop;
    QObject::connect(&c, &UpdateChecker::updateAvailable, &loop, [&](QString v, QUrl p) {
        o.version = v;
        o.page = p;
        loop.quit();
    });
    QObject::connect(&c, &UpdateChecker::upToDate, &loop, [&] {
        o.upToDate = true;
        loop.quit();
    });
    QObject::connect(&c, &UpdateChecker::failed, &loop, [&](QString r) {
        o.failure = r;
        loop.quit();
    });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    const bool started = c.check(url);
    REQUIRE(started == expectStarted);
    if (started) loop.exec();
    return o;
}

QUrl fileUrl(const QString& path) { return QUrl::fromLocalFile(path); }

}  // namespace

TEST_CASE("refuses http and other schemes", "[gui][update]") {
    UpdateChecker c;
    for (const char* url : {"http://x/m.json", "ftp://x/m.json", "javascript:alert(1)", "", "m.json"}) {
        QEventLoop loop;
        bool signalled = false;
        QObject::connect(&c, &UpdateChecker::failed, &loop, [&](QString) { signalled = true; });
        QObject::connect(&c, &UpdateChecker::upToDate, &loop, [&] { signalled = true; });
        QObject::connect(&c, &UpdateChecker::updateAvailable, &loop, [&](QString, QUrl) { signalled = true; });
        REQUIRE_FALSE(c.check(QUrl(url)));
        QTimer::singleShot(100, &loop, &QEventLoop::quit);
        loop.exec();
        REQUIRE_FALSE(signalled);
    }
}

TEST_CASE("a manifest address with a query or fragment is refused", "[gui][update]") {
    UpdateChecker c;
    REQUIRE_FALSE(c.check(QUrl("https://example.org/m.json?id=1")));
    REQUIRE_FALSE(c.check(QUrl("https://example.org/m.json#frag")));
}

TEST_CASE("newer manifest from a file url", "[gui][update]") {
    QTemporaryDir dir;
    UpdateChecker c;
    const auto o = run(c, fileUrl(writeManifest(dir, R"({"version":"9.0.0","url":"https://example.invalid/r"})")));
    REQUIRE(o.version == QString("9.0.0"));
    REQUIRE(o.page == QUrl("https://example.invalid/r"));
}

TEST_CASE("same version is up to date", "[gui][update]") {
    QTemporaryDir dir;
    UpdateChecker c;
    const QString current = QString::fromUtf8(versionString().data(), static_cast<int>(versionString().size()));
    const auto o = run(c, fileUrl(writeManifest(dir, QByteArray(R"({"version":")") + current.toUtf8() + R"(","url":"https://example.invalid/r"})")));
    REQUIRE(o.upToDate);
}

TEST_CASE("malformed manifest fails cleanly", "[gui][update]") {
    QTemporaryDir dir;
    UpdateChecker c;
    const auto o = run(c, fileUrl(writeManifest(dir, "this is not json")));
    REQUIRE(o.failure.has_value());
    REQUIRE_FALSE(o.failure->isEmpty());
}

TEST_CASE("a manifest whose page address is not https is rejected", "[gui][update]") {
    QTemporaryDir dir;
    UpdateChecker c;
    const auto o = run(c, fileUrl(writeManifest(dir, R"j({"version":"9.0.0","url":"javascript:alert(1)"})j")));
    REQUIRE_FALSE(o.version.has_value());
    REQUIRE(o.failure.has_value());
}

TEST_CASE("a missing manifest file fails cleanly", "[gui][update]") {
    UpdateChecker c;
    const auto o = run(c, fileUrl("/nonexistent/perfmonplus/manifest.json"));
    REQUIRE(o.failure.has_value());
}

TEST_CASE("no request without a configured url", "[gui][update]") {
    MainWindow quiet;  // default settings: update check on, but no manifest address
    REQUIRE_FALSE(quiet.updateCheckStarted());

    AppSettings off;
    off.checkUpdates = false;
    off.updateManifestUrl = "https://example.org/m.json";
    MainWindow disabled(off);
    REQUIRE_FALSE(disabled.updateCheckStarted());
}

TEST_CASE("an available update shows a dismissible banner", "[gui][update]") {
    MainWindow w;
    w.show();
    REQUIRE(w.updateBannerText().isEmpty());
    w.showUpdateBanner("9.0.0", QUrl("https://example.invalid/r"));
    REQUIRE(w.updateBannerText() == "PerfMonPlus 9.0.0 is available — Open download page");
    w.dismissUpdateBanner();
    REQUIRE(w.updateBannerText().isEmpty());
}

TEST_CASE("the request names nothing about the user, the machine or its language", "[gui][update][minor]") {
    const auto r = UpdateChecker::buildRequest(QUrl("https://example.org/m.json"));
    REQUIRE(r.rawHeader("Accept-Language") == "en");  // not the system locale
    REQUIRE(r.header(QNetworkRequest::UserAgentHeader).toString() == "PerfMonPlus");
    REQUIRE_FALSE(r.hasRawHeader("Cookie"));
    REQUIRE_FALSE(r.hasRawHeader("Authorization"));
    REQUIRE(r.url().query().isEmpty());
}

TEST_CASE("an oversized manifest is refused", "[gui][update][minor]") {
    QTemporaryDir dir;
    UpdateChecker c;
    const auto o = run(c, fileUrl(writeManifest(dir, QByteArray(300 * 1024, 'x'))));
    REQUIRE(o.failure.has_value());
    REQUIRE(o.failure->contains("too large"));
}
