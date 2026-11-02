// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/update_checker.hpp>

#include <perfmonplus/core/version.hpp>
#include <perfmonplus/vm/version_compare.hpp>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace pmp::gui {
namespace {

constexpr qint64 kMaxManifestBytes = 64 * 1024;
constexpr int kTimeoutMs = 10000;

}  // namespace

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent) {
    net_.setCookieJar(nullptr);  // never remember anything between requests
    connect(&net_, &QNetworkAccessManager::finished, this, &UpdateChecker::finished);
}

bool UpdateChecker::check(const QUrl& manifest) {
    const QString scheme = manifest.scheme();
    if (!manifest.isValid() || (scheme != "https" && scheme != "file")) return false;
    if (manifest.hasQuery() || manifest.hasFragment()) return false;  // nothing of ours rides along on the request
    QNetworkReply* reply = net_.get(buildRequest(manifest));
    // Stop downloading as soon as the manifest is bigger than it may be, instead of buffering all of it first.
    reply->setReadBufferSize(kMaxManifestBytes + 1);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > kMaxManifestBytes) {
            reply->setProperty("tooLarge", true);
            reply->abort();
        }
    });
    return true;
}

QNetworkRequest UpdateChecker::buildRequest(const QUrl& manifest) {
    QNetworkRequest request(manifest);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, "PerfMonPlus");
    // Qt would add the system's language here, which tells the server something about the person: send the same
    // constant everyone sends.
    request.setRawHeader("Accept-Language", "en");
    return request;
}

void UpdateChecker::finished(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->property("tooLarge").toBool()) {
        emit failed("The update manifest is too large.");
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        emit failed(reply->errorString());
        return;
    }
    const QByteArray body = reply->read(kMaxManifestBytes + 1);
    if (body.size() > kMaxManifestBytes) {
        emit failed("The update manifest is too large.");
        return;
    }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(body, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        emit failed("The update manifest is not valid JSON.");
        return;
    }
    const QString version = doc.object().value("version").toString();
    const QUrl page(doc.object().value("url").toString());
    if (version.isEmpty()) {
        emit failed("The update manifest has no version.");
        return;
    }
    const std::string_view current = versionString();
    if (vm::compareVersions(version.toStdString(), current) <= 0) {
        emit upToDate();
        return;
    }
    if (!page.isValid() || page.scheme() != "https") {  // the banner opens this in a browser
        emit failed("The update manifest's download page is not an https address.");
        return;
    }
    emit updateAvailable(version, page);
}

}  // namespace pmp::gui
