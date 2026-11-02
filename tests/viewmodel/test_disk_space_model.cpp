// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/disk_space_model.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using namespace pmp::vm;
using namespace pmp::linux;
using Catch::Approx;

namespace {

FileEntry file(const std::string& path, std::uint64_t bytes, std::int64_t mtime, const std::string& ext) {
    FileEntry f;
    f.path = f.display = path;
    f.bytes = bytes;
    f.mtime = mtime;
    f.ext = ext;
    return f;
}

ScanResult sample() {
    ScanResult r;
    r.largest = {file("/data/movies/film.mkv", 9000, 1'700'000'000, "mkv"), file("/data/a.txt", 100, 1'600'000'000, "txt"),
                 file("/home/u/Photo.JPG", 5000, 1'650'000'000, "jpg"), file("/b.txt", 100, 1'700'000'000, "txt")};
    r.byType = {{"mkv", {1, 9000}}, {"txt", {2, 200}}, {"jpg", {1, 5000}}};
    r.root.files = 1204332;
    r.dirs = 18;
    return r;
}

}  // namespace

TEST_CASE("largest list sorts by each column and is stable on ties", "[disk-vm]") {
    const auto r = sample();
    auto rows = buildLargest(r, "");
    REQUIRE(rows[0].name == "film.mkv");
    REQUIRE(rows[0].folder == "/data/movies");
    REQUIRE(rows[0].type == "mkv");
    REQUIRE(rows[0].modified == "2023-11-14");
    rows = buildLargest(r, "", "size", false);
    REQUIRE(rows[0].sortSize == 100);
    rows = buildLargest(r, "", "name", false);
    REQUIRE(rows[0].name == "a.txt");
    REQUIRE(rows[1].name == "b.txt");
    rows = buildLargest(r, "", "modified", true);
    REQUIRE(rows[0].key == "/data/movies/film.mkv");  // newest; ties by path
    REQUIRE(rows[1].key == "/b.txt");
    rows = buildLargest(r, "", "type", false);
    REQUIRE(rows[0].type == "jpg");
    rows = buildLargest(r, "", "folder", false);
    REQUIRE(rows[0].folder == "/");
    REQUIRE(buildLargest(r, "photo").size() == 1);  // case-insensitive, matches the path
    REQUIRE(buildLargest(r, "nothing").empty());
}

TEST_CASE("type rows sum to 100% with Other", "[disk-vm]") {
    ScanResult r;
    for (int i = 0; i < 30; ++i) r.byType["t" + std::to_string(i)] = {1, static_cast<std::uint64_t>(1000 * (i + 1))};
    const auto rows = buildTypes(r, 12);
    REQUIRE(rows.size() == 13);
    REQUIRE(rows.back().type == "Other");
    double sum = 0;
    for (const auto& t : rows) sum += t.fraction;
    REQUIRE(sum == Approx(1.0));
    REQUIRE(rows[0].type == "t29");  // the largest first
    REQUIRE(buildTypes(sample()).size() == 3);  // no "Other" when everything fits
}

TEST_CASE("summary text and pluralisation", "[disk-vm]") {
    auto r = sample();
    r.inaccessible = 14;
    r.skippedMounts = 2;
    REQUIRE(scanSummary(r) == "Scanned 1,204,332 files in 18 folders · 14 inaccessible · 2 mount points skipped");
    r.root.files = 1;
    r.dirs = 1;
    r.inaccessible = 0;
    r.skippedMounts = 1;
    r.vanished = 3;
    r.cancelled = true;
    REQUIRE(scanSummary(r) == "Scanned 1 file in 1 folder · 1 mount point skipped · 3 changed while scanning · results are partial");
    REQUIRE(explainSkipped(r).find("Mount points skipped") != std::string::npos);
    ScanResult clean;
    REQUIRE(explainSkipped(clean).empty());
}
