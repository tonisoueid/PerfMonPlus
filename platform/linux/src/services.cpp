// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/services.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace pmp::linux {
namespace {

std::vector<std::string> systemctl(ServiceScope scope, std::initializer_list<std::string> rest) {
    std::vector<std::string> argv{"systemctl"};
    if (scope == ServiceScope::User) argv.push_back("--user");
    argv.insert(argv.end(), rest.begin(), rest.end());
    return argv;
}

std::string str(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool isService(const std::string& unit) {
    return unit.size() > 8 && unit.compare(unit.size() - 8, 8, ".service") == 0 && unit.find("@.service") == std::string::npos;
}

nlohmann::json parseList(const CommandResult& r, const char* what) {
    if (r.exitCode != 0) throw std::runtime_error(std::string(what) + ": " + (r.err.empty() ? "failed" : r.err));
    const auto j = nlohmann::json::parse(r.out, nullptr, false);
    if (!j.is_array()) throw std::runtime_error(std::string(what) + ": unexpected output");
    return j;
}

}  // namespace

ServicesCollector::ServicesCollector(CommandRunner run) : run_(std::move(run)) {}

std::vector<ServiceInfo> ServicesCollector::collect(ServiceScope scope) const {
    const auto units = parseList(run_(systemctl(scope, {"list-units", "--type=service", "--all", "--output=json", "--no-pager"})),
                                 "systemctl list-units");
    const auto files = parseList(run_(systemctl(scope, {"list-unit-files", "--type=service", "--output=json", "--no-pager"})),
                                 "systemctl list-unit-files");

    std::map<std::string, std::string> startType;
    for (const auto& f : files) startType[str(f, "unit_file")] = str(f, "state");

    std::vector<ServiceInfo> out;
    std::set<std::string> seen;
    for (const auto& u : units) {
        const auto name = str(u, "unit");
        if (!isService(name) || !seen.insert(name).second) continue;
        ServiceInfo s;
        s.unit = name;
        s.description = str(u, "description");
        s.active = str(u, "active");
        s.sub = str(u, "sub");
        s.load = str(u, "load");
        s.startType = startType.count(name) ? startType[name] : "";
        s.scope = scope;
        out.push_back(std::move(s));
    }
    for (const auto& [name, state] : startType) {  // installed but not loaded
        if (!isService(name) || !seen.insert(name).second) continue;
        ServiceInfo s;
        s.unit = name;
        s.active = "inactive";
        s.sub = "dead";
        s.load = "not-loaded";
        s.startType = state;
        s.scope = scope;
        out.push_back(std::move(s));
    }

    // Main PIDs for the active units, in one call.
    std::vector<std::string> show = systemctl(scope, {"show", "--property=Id,MainPID", "--no-pager", "--"});
    const auto base = show.size();  // 5 arguments in system scope, 6 with --user
    for (const auto& s : out) {
        if (s.active == "active") show.push_back(s.unit);
    }
    if (show.size() > base) {
        const auto r = run_(show);
        std::map<std::string, int> pids;
        std::istringstream in(r.out);
        std::string line, id;
        while (std::getline(in, line)) {
            if (line.rfind("Id=", 0) == 0) id = line.substr(3);
            else if (line.rfind("MainPID=", 0) == 0 && !id.empty()) {
                try {
                    pids[id] = std::stoi(line.substr(8));
                } catch (...) {
                }
            }
        }
        for (auto& s : out) {
            if (pids.count(s.unit)) s.mainPid = pids[s.unit];
        }
    }
    return out;
}

CommandResult ServicesCollector::act(ServiceScope scope, const std::string& action, const std::string& unit) const {
    if (action != "start" && action != "stop" && action != "restart") {
        throw std::invalid_argument("unsupported service action: " + action);
    }
    return run_(systemctl(scope, {action, "--", unit}));
}

std::string ServicesCollector::logs(ServiceScope scope, const std::string& unit, int lines) const {
    std::vector<std::string> argv{"journalctl"};
    if (scope == ServiceScope::User) argv.push_back("--user");
    argv.insert(argv.end(), {"--unit=" + unit, "-n", std::to_string(lines), "--no-pager", "-o", "short-iso"});
    const auto r = run_(argv);
    if (r.exitCode != 0) return r.err;
    // journalctl exits 0 but explains on stderr that it is showing only part of the journal: keep that notice.
    return r.err.empty() ? r.out : r.out + "\n" + r.err;
}

}  // namespace pmp::linux
