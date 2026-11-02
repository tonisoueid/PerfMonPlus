// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/process_inspector.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>

using namespace pmp;
using pmp::linux::ProcessInspector;

namespace {

std::string statLine(int start) {
    return "42 (x) S 1 42 42 0 -1 0 0 0 0 0 1 1 0 0 20 0 1 0 " + std::to_string(start) + " 1000 10 0";
}

const ProcessKey kKey{42, 5000};

void alive(FakeRoot& fr) { fr.write("proc/42/stat", statLine(5000)); }

std::string elf(unsigned machine, unsigned dataEncoding = 1) {
    std::string h(20, '\0');
    h[0] = 0x7f;
    h[1] = 'E';
    h[2] = 'L';
    h[3] = 'F';
    h[5] = static_cast<char>(dataEncoding);
    if (dataEncoding == 1) {
        h[18] = static_cast<char>(machine & 0xff);
        h[19] = static_cast<char>(machine >> 8);
    } else {
        h[19] = static_cast<char>(machine & 0xff);
        h[18] = static_cast<char>(machine >> 8);
    }
    return h;
}

std::string exeAt(FakeRoot& fr, const std::string& name, const std::string& contents) {
    const auto abs = fr.root().proc.parent_path() / "bin" / name;
    fr.write("bin/" + name, contents);
    fr.symlink(abs.string(), "proc/42/exe");
    return abs.string();
}

}  // namespace

TEST_CASE("command line joins NUL separated arguments", "[inspector]") {
    FakeRoot fr;
    alive(fr);
    fr.write("proc/42/cmdline", std::string("firefox\0-new-window\0", 20));
    const auto i = ProcessInspector(fr.root()).inspect(kKey, false);
    REQUIRE(i);
    REQUIRE(i->cmdline == "firefox -new-window");
    REQUIRE_FALSE(i->cmdlineRestricted);
    fr.write("proc/42/cmdline", "");  // a kernel thread
    const auto k = ProcessInspector(fr.root()).inspect(kKey, false);
    REQUIRE(k->cmdline.empty());
    REQUIRE_FALSE(k->cmdlineRestricted);
}

TEST_CASE("the environment is only read on request", "[inspector]") {
    if (geteuid() == 0) SKIP("root can read everything");
    FakeRoot fr;
    alive(fr);
    fr.write("proc/42/environ", std::string("HOME=/home/dave\0PATH=/usr/bin\0", 31));
    fr.makeUnreadable("proc/42/environ");
    const ProcessInspector inspector(fr.root());
    const auto without = inspector.inspect(kKey, false);
    REQUIRE(without->environment.empty());
    REQUIRE_FALSE(without->environmentRestricted);  // never touched
    const auto with = inspector.inspect(kKey, true);
    REQUIRE(with->environmentRestricted);
}

TEST_CASE("a readable environment is listed one variable per line and bounded", "[inspector]") {
    FakeRoot fr;
    alive(fr);
    fr.write("proc/42/environ", std::string("HOME=/home/dave\0PATH=/usr/bin\0", 31));
    const auto i = ProcessInspector(fr.root()).inspect(kKey, true);
    REQUIRE(i->environment == "HOME=/home/dave\nPATH=/usr/bin");
    REQUIRE_FALSE(i->environmentTruncated);
    fr.write("proc/42/environ", std::string(100 * 1024, 'x'));
    const auto big = ProcessInspector(fr.root()).inspect(kKey, true);
    REQUIRE(big->environmentTruncated);
    REQUIRE(big->environment.size() <= 64 * 1024 + 3);
}

TEST_CASE("other users' processes are restricted", "[inspector]") {
    if (geteuid() == 0) SKIP("root can read everything");
    FakeRoot fr;
    alive(fr);
    fr.write("proc/42/cmdline", "secret");
    fr.makeUnreadable("proc/42/cmdline");
    fr.makeDir("proc/42/fd");
    fr.makeUnreadable("proc/42/fd");
    const auto i = ProcessInspector(fr.root()).inspect(kKey, false);
    REQUIRE(i);
    REQUIRE(i->cmdlineRestricted);
    REQUIRE(i->cmdline.empty());
    REQUIRE_FALSE(i->openFiles.has_value());
    REQUIRE_FALSE(i->sockets.has_value());
}

TEST_CASE("file and socket counts", "[inspector]") {
    FakeRoot fr;
    alive(fr);
    fr.symlink("/dev/null", "proc/42/fd/0");
    fr.symlink("/tmp/x", "proc/42/fd/1");
    fr.symlink("socket:[123]", "proc/42/fd/3");
    const auto i = ProcessInspector(fr.root()).inspect(kKey, false);
    REQUIRE(i->openFiles == 3);
    REQUIRE(i->sockets == 1);
}

TEST_CASE("architecture comes from the ELF header", "[inspector]") {
    FakeRoot fr;
    alive(fr);
    exeAt(fr, "a", elf(0x3e));
    REQUIRE(ProcessInspector(fr.root()).inspect(kKey, false)->architecture == "x86_64");
    fr.remove("proc/42/exe");
    exeAt(fr, "b", elf(0xb7));
    REQUIRE(ProcessInspector(fr.root()).inspect(kKey, false)->architecture == "arm64");
    fr.remove("proc/42/exe");
    exeAt(fr, "c", elf(0x03));
    REQUIRE(ProcessInspector(fr.root()).inspect(kKey, false)->architecture == "i386");
    fr.remove("proc/42/exe");
    exeAt(fr, "d", elf(0x3e, 2));  // big-endian encoding: x86_64 stored big-endian is still 0x3e
    REQUIRE(ProcessInspector(fr.root()).inspect(kKey, false)->architecture == "x86_64");
    fr.remove("proc/42/exe");
    exeAt(fr, "e", "#!/bin/sh\n");
    REQUIRE(ProcessInspector(fr.root()).inspect(kKey, false)->architecture.empty());
}

TEST_CASE("a recycled pid is not inspected", "[inspector]") {
    FakeRoot fr;
    fr.write("proc/42/stat", statLine(9000));  // a different process now owns PID 42
    REQUIRE_FALSE(ProcessInspector(fr.root()).inspect(kKey, false).has_value());
    fr.remove("proc/42");
    REQUIRE_FALSE(ProcessInspector(fr.root()).inspect(kKey, false).has_value());
}

TEST_CASE("invalid utf-8 and long command lines are made safe", "[inspector]") {
    FakeRoot fr;
    alive(fr);
    static constexpr char kRaw[] = "bad\xff\xfe\0<b>x</b>\0";
    fr.write("proc/42/cmdline", std::string(kRaw, sizeof(kRaw) - 1));
    const auto i = ProcessInspector(fr.root()).inspect(kKey, false);
    REQUIRE(i->cmdline == "bad?? <b>x</b>");
    fr.write("proc/42/cmdline", std::string(20 * 1024, 'a'));
    const auto big = ProcessInspector(fr.root()).inspect(kKey, false);
    REQUIRE(big->cmdline.size() == 8192 + 3);
    REQUIRE(big->cmdline.ends_with("\xE2\x80\xA6"));
}

TEST_CASE("an inspection of a process that was replaced while it ran is thrown away", "[inspector][minor]") {
    FakeRoot fr;
    alive(fr);
    fr.write("proc/42/cmdline", std::string("old\0", 4));
    ProcessInspector insp(fr.root());
    REQUIRE(insp.inspect(kKey, false).has_value());
    insp.beforeFinalCheck = [&] { fr.write("proc/42/stat", statLine(9000)); };  // PID 42 becomes a different process mid-read
    REQUIRE_FALSE(insp.inspect(kKey, false).has_value());
}
