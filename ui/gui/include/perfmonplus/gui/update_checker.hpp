// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QUrl>

namespace pmp::gui {

// Opt-in update check (UPD-01..03): one plain GET of a small JSON manifest, only from an address the user
// configured, only over https:// (or file://). No query string, no cookies, no body, no telemetry; the manifest is
// {"version": "...", "url": "https://..."} and the page address must itself be https.
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);

    // Returns false, sending nothing and emitting nothing, unless `manifest` is an acceptable address.
    bool check(const QUrl& manifest);
    // The request that goes out: nothing in it names the user, the machine or its language.
    static QNetworkRequest buildRequest(const QUrl& manifest);

signals:
    void updateAvailable(QString version, QUrl page);
    void upToDate();
    void failed(QString reason);

private:
    void finished(QNetworkReply* reply);

    QNetworkAccessManager net_;
};

}  // namespace pmp::gui
