// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/linux/connections.hpp>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace pmp::vm {

// One row of the Connections table (CON-01). All text is plain; the process name and any host name come from
// outside and must be shown as plain text.
struct ConnectionRow {
    std::string key;        // proto|local|remote|inode: identifies the socket across refreshes
    std::string process;    // "name (PID n)", "PID n", "Restricted" or "—": a process is never guessed
    std::string protocol, local, remote, state, rx, tx, host, exposure;
    std::optional<ProcessKey> owner;
    std::optional<linux::IpAddr> remoteAddr;  // set when there is a remote end whose host name could be looked up
    bool listening = false;
    bool exposed = false;   // listening on a wildcard address (CON-02)
};

struct HostLookups {
    std::map<linux::IpAddr, std::string> resolved;
    std::set<linux::IpAddr> failed, pending;
};

struct ConnectionOptions {
    bool listeningOnly = false;  // the "Listening / Exposed" mode (CON-02)
    std::string filter{};        // process, port, address, state or protocol, case-insensitive (CON-04)
    std::optional<linux::Proto> proto{};
    std::optional<linux::SockState> state{};
    std::string sortBy = "process";  // process | protocol | local | remote | state | host
    bool descending = false;
    bool resolveHosts = false;   // reverse DNS is opt-in (NFR-14)
};

// A listening socket is "exposed" only when bound to a wildcard address; loopback listeners never are.
bool isListening(const linux::Connection& c);
bool isExposed(const linux::Connection& c);
std::string exposureText(const linux::Connection& c);
std::string exposureExplanation();  // the note under the "Listening / Exposed" mode

std::vector<ConnectionRow> buildConnections(const std::vector<linux::Connection>& rows, const Frame& frame,
                                            const ConnectionOptions& options, const HostLookups& hosts);

}  // namespace pmp::vm
