// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/benchmarks_page.hpp>

#include <perfmonplus/core/version.hpp>
#include <perfmonplus/gui/async.hpp>
#include <perfmonplus/linux/system_info.hpp>

#include <QHBoxLayout>
#include <QMessageBox>
#include <QPointer>
#include <QVBoxLayout>

#include <chrono>
#include <thread>

namespace pmp::gui {
namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

class RealRunner final : public BenchRunner {
public:
    explicit RealRunner(const linux::BenchParams& p) : params_(p), bench_(p) {
        linux::Benchmark::removeStaleTempFiles(bench_dir());
    }
    std::string preflight(linux::BenchId id) override { return linux::Benchmark::preflight(id, params_); }
    std::uint64_t estimate(linux::BenchId id) override { return linux::Benchmark::estimatedSeconds(id, params_); }
    linux::BenchResult run(linux::BenchId id, const std::atomic<bool>& cancel, const std::function<void(double)>& progress) override {
        return bench_.run(id, cancel, progress);
    }
    void cleanup() override { bench_.cleanup(); }
    std::string tempDir() override { return bench_dir().string(); }
    std::uint64_t diskBytes() override { return params_.diskBytes; }

private:
    std::filesystem::path bench_dir() const { return params_.tempDir.empty() ? linux::defaultBenchDir() : params_.tempDir; }
    linux::BenchParams params_;
    linux::Benchmark bench_;
};

constexpr linux::BenchId kAll[] = {linux::BenchId::CpuSingle, linux::BenchId::CpuMulti, linux::BenchId::MemBandwidth, linux::BenchId::MemLatency,
                                   linux::BenchId::DiskSeqWrite, linux::BenchId::DiskSeqRead, linux::BenchId::DiskRandRead};

bool usesDisk(linux::BenchId id) {
    return id == linux::BenchId::DiskSeqWrite || id == linux::BenchId::DiskSeqRead || id == linux::BenchId::DiskRandRead;
}

}  // namespace

std::unique_ptr<BenchRunner> makeBenchRunner(const linux::BenchParams& params) { return std::make_unique<RealRunner>(params); }

BenchmarksPage::BenchmarksPage(RunnerFactory factory, linux::BenchStore store, QWidget* parent)
    : QWidget(parent), factory_(std::move(factory)), store_(std::move(store)) {
    hooks_.confirmStart = [this](const QString& title, const QString& text) {
        QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton, this);
        box.setTextFormat(Qt::PlainText);
        auto* go = box.addButton("Start", QMessageBox::AcceptRole);
        auto* no = box.addButton("Cancel", QMessageBox::RejectRole);
        box.setDefaultButton(no);
        box.exec();
        return box.clickedButton() == go;
    };
    context_.appVersion = std::string(versionString());

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 4, 14, 10);
    auto* top = new QHBoxLayout;
    auto* picks = new QVBoxLayout;
    for (const auto id : kAll) {
        auto* c = new QCheckBox(q(linux::benchName(id)), this);
        c->setChecked(true);
        checks_.push_back(c);
        picks->addWidget(c);
    }
    gpu_ = new QCheckBox("GPU compute", this);
    gpu_->setEnabled(false);
    gpu_->setToolTip("Not available in this build: there is no portable GPU compute interface to run unprivileged.");
    gpu_->setText("GPU compute — not available in this build");
    picks->addWidget(gpu_);
    top->addLayout(picks);
    auto* side = new QVBoxLayout;
    start_ = new QPushButton("Run benchmarks…", this);
    cancel_ = new QPushButton("Cancel", this);
    cancel_->hide();
    bar_ = new QProgressBar(this);
    bar_->setRange(0, 1000);
    bar_->setTextVisible(false);
    current_ = new QLabel(this);
    current_->setTextFormat(Qt::PlainText);
    notice_ = new QLabel(this);
    notice_->setWordWrap(true);
    notice_->setTextFormat(Qt::PlainText);
    status_ = new QLabel(this);
    status_->setObjectName("dim");
    status_->setTextFormat(Qt::PlainText);
    side->addWidget(start_);
    side->addWidget(cancel_);
    side->addWidget(bar_);
    side->addWidget(current_);
    side->addWidget(notice_);
    side->addWidget(status_);
    side->addStretch(1);
    top->addLayout(side, 1);
    col->addLayout(top);

    cardHost_ = new QWidget(this);
    cardGrid_ = new QGridLayout(cardHost_);
    cardGrid_->setContentsMargins(0, 0, 0, 0);
    col->addWidget(cardHost_);
    auto* hl = new QLabel("History", this);
    hl->setObjectName("dim");
    col->addWidget(hl);
    history_ = new QListWidget(this);
    history_->setAccessibleName("Benchmark history");
    col->addWidget(history_, 1);

    connect(start_, &QPushButton::clicked, this, &BenchmarksPage::start);
    connect(cancel_, &QPushButton::clicked, this, &BenchmarksPage::cancel);
    for (auto* c : checks_) connect(c, &QCheckBox::toggled, this, [this] { updateButtons(); });
    reloadHistory();
    if (!linux::kOptimizedBuild) {
        const QString debug = "This is a debug build: its scores are much lower than a normal build's and are only compared with other debug-build runs.";
        notice_->setText(notice_->text().isEmpty() ? debug : notice_->text() + "\n" + debug);  // keep a "history was damaged" warning
    }
    updateButtons();
}

BenchmarksPage::~BenchmarksPage() {
    cancelFlag_ = true;
    if (worker_.joinable()) worker_.join();
    if (runner_) runner_->cleanup();  // a window closed mid-run leaves no test file behind
}

void BenchmarksPage::setHooks(Hooks h) {
    if (h.confirmStart) hooks_.confirmStart = std::move(h.confirmStart);
}

void BenchmarksPage::setRunBlocked(bool blocked, const QString& reason) {
    blocked_ = blocked;
    blockReason_ = reason;
    updateButtons();
}

void BenchmarksPage::update(const Frame& frame, const HistoryStore&, MonoTime) { source_ = frame.energy.source; }

void BenchmarksPage::setSystemInfo(const linux::SystemInfo& info) {
    context_.cpuModel = info.cpu.model;
    context_.kernel = info.kernel;
    context_.logicalCpus = static_cast<int>(std::thread::hardware_concurrency());
    if (info.memTotal.isMeasured()) context_.memoryBytes = static_cast<std::uint64_t>(*info.memTotal.value);
}

void BenchmarksPage::updateButtons() {
    bool any = false;
    for (auto* c : checks_) any = any || c->isChecked();
    start_->setEnabled(!running_ && any && !blocked_);
    start_->setToolTip(blocked_ ? blockReason_ : QString());
    for (auto* c : checks_) c->setEnabled(!running_);
    cancel_->setVisible(running_);
}

QFrame* BenchmarksPage::cardFrame(int i) const {
    auto* item = cardGrid_->itemAt(i);
    return item ? qobject_cast<QFrame*>(item->widget()) : nullptr;
}

QString BenchmarksPage::warning(const std::vector<linux::BenchId>& ids, BenchRunner& runner) const {
    std::uint64_t seconds = 0;
    bool disk = false;
    for (const auto id : ids) {
        seconds += runner.estimate(id);
        disk = disk || usesDisk(id);
    }
    QString t = QString("Running the benchmarks will use all CPU cores and may make your computer warm and slow for a short time.\n\n"
                        "This takes about %1 seconds.")
                    .arg(seconds);
    if (disk) {
        const double gb = static_cast<double>(runner.diskBytes()) / 1e9;
        t += QString("\n\nThe disk tests write a %1 GB test file in %2 (removed when they finish) and need that much free space plus a margin.")
                 .arg(gb, 0, 'f', 1)
                 .arg(q(runner.tempDir()));
    }
    if (source_ == PowerSource::Battery) t += "\n\nYou are on battery power.";
    t += "\n\nPerfMonPlus keeps sampling while a test runs, so you can watch the load on the Performance page.";
    return t;
}

void BenchmarksPage::start() {
    if (running_ || blocked_) return;
    std::vector<linux::BenchId> ids;
    for (const auto id : kAll) {
        if (checkFor(id)->isChecked()) ids.push_back(id);
    }
    if (ids.empty()) return;
    runner_ = factory_();
    notice_->clear();
    for (const auto id : ids) {  // a test that must not start stops the whole run, with the reason
        const auto why = runner_->preflight(id);
        if (!why.empty()) {
            notice_->setText(q(linux::benchName(id)) + ": " + q(why));
            runner_.reset();
            return;
        }
    }
    lastWarning_ = warning(ids, *runner_);
    if (!hooks_.confirmStart("Run benchmarks?", lastWarning_)) {
        runner_.reset();
        return;
    }
    if (worker_.joinable()) worker_.join();
    running_ = true;
    cancelFlag_ = false;
    bar_->setValue(0);
    status_->setText("Running…");
    updateButtons();
    emit runningChanged(true);
    QPointer<BenchmarksPage> guard(this);
    worker_ = std::thread([this, guard, ids] {
        std::vector<linux::BenchResult> done;
        bool cancelled = false;
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const auto id = ids[i];
            QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, id, i, n = ids.size()] {
                if (guard) guard->current_->setText(QString("%1 (%2 of %3)").arg(q(linux::benchName(id))).arg(i + 1).arg(n));
            }, Qt::QueuedConnection);
            auto r = runner_->run(id, cancelFlag_, [guard, i, n = ids.size()](double f) {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, f, i, n] {  // throttled by the engine's own cadence
                    if (guard) guard->bar_->setValue(static_cast<int>((static_cast<double>(i) + std::clamp(f, 0.0, 1.0)) / static_cast<double>(n) * 1000.0));
                }, Qt::QueuedConnection);
            });
            const bool c = r.cancelled;
            done.push_back(std::move(r));
            if (c) {
                cancelled = true;
                break;
            }
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, done = std::move(done), cancelled]() mutable {
            if (guard) guard->finish(std::move(done), cancelled);
        }, Qt::QueuedConnection);
    });
}

void BenchmarksPage::cancel() {
    if (running_) cancelFlag_ = true;
}

void BenchmarksPage::finish(std::vector<linux::BenchResult> results, bool cancelled) {
    if (worker_.joinable()) worker_.join();
    running_ = false;
    if (runner_) runner_->cleanup();
    current_->clear();
    bar_->setValue(cancelled ? bar_->value() : 1000);
    // Finished tests are kept; the one that was cancelled is not recorded.
    linux::BenchRecord rec = context_;
    rec.wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (auto& r : results) {
        if (!r.cancelled) rec.results.push_back(std::move(r));
    }
    if (!rec.results.empty()) {
        std::string err;
        if (store_.append(rec, &err)) {
            status_->setText(cancelled ? "Cancelled: the tests that finished were kept." : "Done.");
        } else {
            status_->setText("Done, but the result could not be saved: " + q(err));
        }
        reloadHistory();
        showCards(rec);
    } else {
        status_->setText(cancelled ? "Cancelled." : "No test produced a result.");
    }
    updateButtons();
    emit runningChanged(false);
    emit runFinished();
}

void BenchmarksPage::reloadHistory() {
    std::string warning;
    records_ = store_.load(&warning);
    storeWarning_ = q(warning);
    if (!warning.empty()) notice_->setText(storeWarning_);
    history_->clear();
    for (auto it = records_.rbegin(); it != records_.rend(); ++it) history_->addItem(q(vm::historyLine(*it)));
}

void BenchmarksPage::showCards(const linux::BenchRecord& latest) {
    cards_ = vm::buildCards(latest, records_);
    while (auto* item = cardGrid_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    int i = 0;
    for (const auto& c : cards_) {
        auto* f = new QFrame(cardHost_);
        f->setObjectName("card");
        auto* l = new QVBoxLayout(f);
        auto* name = new QLabel(q(c.name), f);
        name->setObjectName("dim");
        auto* score = new QLabel(q(c.score), f);
        QFont big = score->font();
        big.setPointSize(big.pointSize() + 8);
        score->setFont(big);
        auto* raw = new QLabel(q(c.raw) + " · " + q(c.date), f);
        auto* cmp = new QLabel(q(c.comparison), f);
        cmp->setObjectName("dim");
        for (auto* x : {name, score, raw, cmp}) {
            x->setTextFormat(Qt::PlainText);
            l->addWidget(x);
        }
        f->setAccessibleName(q(c.name) + " score " + q(c.score));
        cardGrid_->addWidget(f, i / 4, i % 4);
        ++i;
    }
}

}  // namespace pmp::gui
