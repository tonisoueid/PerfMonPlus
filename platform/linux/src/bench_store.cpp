// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/bench_store.hpp>

#include <perfmonplus/linux/proc_fs.hpp>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace pmp::linux {
namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr int kFileVersion = 1;

BenchId idFromKey(const std::string& key, bool* ok) {
    for (int i = 0; i < kBenchCount; ++i) {
        if (benchKey(static_cast<BenchId>(i)) == key) {
            *ok = true;
            return static_cast<BenchId>(i);
        }
    }
    *ok = false;
    return BenchId::CpuSingle;
}

template <class T>
T num(const json& j, const char* key, T fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<T>() : fallback;
}

std::string str(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? sanitizeText(it->get<std::string>(), 256) : std::string();
}

}  // namespace

fs::path BenchStore::defaultFile() {
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return fs::path(x) / "perfmonplus/benchmarks.json";
    if (const char* h = std::getenv("HOME"); h && *h) return fs::path(h) / ".local/share/perfmonplus/benchmarks.json";
    return "benchmarks.json";
}

std::vector<BenchRecord> BenchStore::load(std::string* warning) const {
    std::error_code ec;
    if (!fs::exists(file_, ec)) return {};
    std::ifstream in(file_);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    const auto doc = json::parse(text, nullptr, false);
    std::vector<BenchRecord> out;
    bool good = doc.is_object() && doc.contains("runs") && doc["runs"].is_array();
    if (good) {
        for (const auto& r : doc["runs"]) {
            if (!r.is_object() || !r.contains("results") || !r["results"].is_array()) {
                good = false;
                break;
            }
            BenchRecord rec;
            rec.benchVersion = num<int>(r, "benchVersion", 0);
            rec.optimized = r.value("optimized", true);  // files from before this field were all written by release builds
            rec.appVersion = str(r, "appVersion");
            rec.cpuModel = str(r, "cpuModel");
            rec.kernel = str(r, "kernel");
            rec.logicalCpus = num<int>(r, "logicalCpus", 0);
            rec.memoryBytes = num<std::uint64_t>(r, "memoryBytes", 0);
            rec.wallMs = num<std::int64_t>(r, "wallMs", 0);
            for (const auto& x : r["results"]) {
                if (!x.is_object()) continue;
                bool known = false;
                BenchResult br;
                br.id = idFromKey(str(x, "id"), &known);
                if (!known) continue;  // a test this build does not know: ignored, not an error
                br.raw = num<double>(x, "raw", 0);
                br.rawUnit = rawUnitOf(br.id);
                br.score = num<double>(x, "score", 0);
                br.seconds = num<double>(x, "seconds", 0);
                br.checksum = std::strtoull(str(x, "checksum").c_str(), nullptr, 10);
                br.cancelled = x.value("cancelled", false);
                br.note = str(x, "note");
                rec.results.push_back(std::move(br));
            }
            out.push_back(std::move(rec));
        }
    }
    if (!good) {
        auto aside = file_;
        aside += ".bad";
        std::error_code re;
        fs::rename(file_, aside, re);
        if (warning) {
            *warning = "The benchmark history " + file_.string() + " was damaged" +
                       (re ? "." : " and has been set aside as " + aside.string() + ".") + " Starting a new one.";
        }
        return {};
    }
    return out;
}

bool BenchStore::append(const BenchRecord& record, std::string* error) const {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    auto runs = load();
    runs.push_back(record);
    if (runs.size() > kKeep) runs.erase(runs.begin(), runs.begin() + static_cast<long>(runs.size() - kKeep));
    json doc = {{"version", kFileVersion}, {"runs", json::array()}};
    for (const auto& r : runs) {
        json results = json::array();
        for (const auto& b : r.results) {
            results.push_back({{"id", benchKey(b.id)},
                               {"raw", b.raw},
                               {"score", b.score},
                               {"seconds", b.seconds},
                               {"checksum", std::to_string(b.checksum)},  // a string: JSON numbers lose 64-bit precision
                               {"cancelled", b.cancelled},
                               {"note", b.note}});
        }
        doc["runs"].push_back({{"benchVersion", r.benchVersion},
                               {"optimized", r.optimized},
                               {"appVersion", r.appVersion},
                               {"cpuModel", r.cpuModel},
                               {"kernel", r.kernel},
                               {"logicalCpus", r.logicalCpus},
                               {"memoryBytes", r.memoryBytes},
                               {"wallMs", r.wallMs},
                               {"results", results}});
    }
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    auto tmp = file_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return fail("Could not write " + file_.string());
        out << doc.dump(1);
        if (!out) {
            fs::remove(tmp, ec);
            return fail("Could not write " + file_.string());
        }
    }
    fs::rename(tmp, file_, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return fail("Could not save " + file_.string() + ": " + ec.message());
    }
    return true;
}

}  // namespace pmp::linux
