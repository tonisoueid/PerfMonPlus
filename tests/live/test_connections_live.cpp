// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/connections.hpp>

#include <catch2/catch_test_macros.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace pmp;
using namespace pmp::linux;

TEST_CASE("collect finds our own listening socket", "[live][connections]") {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    REQUIRE(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0);
    REQUIRE(::listen(fd, 1) == 0);
    socklen_t len = sizeof addr;
    REQUIRE(::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
    const auto port = ntohs(addr.sin_port);

    ConnectionsCollector c(ProcRoot::live());
    const auto list = c.collect();
    bool found = false;
    for (const auto& row : list.rows) {
        if (row.proto == Proto::Tcp4 && row.localPort == port && row.state == SockState::Listen) {
            found = true;
            REQUIRE(isLoopback(row.local));
            REQUIRE_FALSE(isWildcard(row.local));
            REQUIRE(row.owner.has_value());
            REQUIRE(row.owner->pid == ::getpid());
            REQUIRE(row.ownerAvailability == Availability::Measured);
        }
    }
    ::close(fd);
    REQUIRE(found);
}
