// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/sessions.hpp>

#include <nlohmann/json.hpp>

namespace pmp::linux {
namespace {

std::string str(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

}  // namespace

SessionsCollector::SessionsCollector(CommandRunner run) : run_(std::move(run)) {}

std::vector<SessionInfo> SessionsCollector::collect() const {
    const auto r = run_({"loginctl", "--json=short", "list-sessions"});
    if (r.exitCode != 0) throw std::runtime_error("loginctl: " + (r.err.empty() ? std::string("failed") : r.err));
    const auto j = nlohmann::json::parse(r.out, nullptr, false);
    if (!j.is_array()) throw std::runtime_error("loginctl: unexpected output");
    std::vector<SessionInfo> out;
    for (const auto& s : j) {
        SessionInfo i;
        i.id = str(s, "session");
        i.user = str(s, "user");
        i.seat = str(s, "seat");
        i.tty = str(s, "tty");
        i.sessionClass = str(s, "class");
        i.uid = s.value("uid", 0u);
        i.leader = s.value("leader", 0);
        i.idle = s.value("idle", false);
        out.push_back(std::move(i));
    }
    return out;
}

CommandResult SessionsCollector::lock(const std::string& id) const { return run_({"loginctl", "lock-session", "--", id}); }

CommandResult SessionsCollector::terminate(const std::string& id) const {
    return run_({"loginctl", "terminate-session", "--", id});
}

}  // namespace pmp::linux
