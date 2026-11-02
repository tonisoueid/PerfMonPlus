// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/command.hpp>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

extern char** environ;

namespace pmp::linux {
namespace {

struct Pipe {
    int fds[2] = {-1, -1};
    ~Pipe() {
        for (int fd : fds) {
            if (fd >= 0) ::close(fd);
        }
    }
    int readEnd() const { return fds[0]; }
    int writeEnd() const { return fds[1]; }
    void closeWrite() {
        if (fds[1] >= 0) ::close(fds[1]);
        fds[1] = -1;
    }
};

}  // namespace

CommandResult runCommand(const std::vector<std::string>& argv, std::chrono::milliseconds timeout) {
    CommandResult result;
    if (argv.empty()) {
        result.exitCode = 127;
        result.err = "empty command";
        return result;
    }

    Pipe out, err;
    if (::pipe2(out.fds, O_CLOEXEC) != 0 || ::pipe2(err.fds, O_CLOEXEC) != 0) {
        result.exitCode = 127;
        result.err = std::strerror(errno);
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, out.writeEnd(), STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, err.writeEnd(), STDERR_FILENO);

    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    pid_t pid = 0;
    const int rc = ::posix_spawnp(&pid, argv[0].c_str(), &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) {
        result.exitCode = 127;
        result.err = argv[0] + ": " + std::strerror(rc);
        return result;
    }
    out.closeWrite();
    err.closeWrite();

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    pollfd fds[2] = {{out.readEnd(), POLLIN, 0}, {err.readEnd(), POLLIN, 0}};
    bool open[2] = {true, true};
    char buf[4096];
    while (open[0] || open[1]) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            result.timedOut = true;
            break;
        }
        for (int i = 0; i < 2; ++i) fds[i].fd = open[i] ? (i == 0 ? out.readEnd() : err.readEnd()) : -1;
        const int n = ::poll(fds, 2, static_cast<int>(left.count()));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            result.timedOut = n == 0;
            break;
        }
        for (int i = 0; i < 2; ++i) {
            if (!open[i] || !(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            const ssize_t got = ::read(fds[i].fd, buf, sizeof buf);
            if (got > 0) (i == 0 ? result.out : result.err).append(buf, static_cast<std::size_t>(got));
            else open[i] = false;
        }
    }

    if (result.timedOut) ::kill(pid, SIGKILL);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (WIFEXITED(status)) result.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) result.exitCode = 128 + WTERMSIG(status);
    return result;
}

std::chrono::milliseconds commandTimeout(const std::vector<std::string>& argv) {
    if (argv.empty() || (argv[0] != "systemctl" && argv[0] != "loginctl")) return kQueryTimeout;
    static const char* const verbs[] = {"start", "stop", "restart", "enable", "disable", "lock-session", "terminate-session"};
    for (std::size_t i = 1; i < argv.size() && argv[i] != "--"; ++i) {
        for (const char* verb : verbs) {
            if (argv[i] == verb) return kActionTimeout;
        }
    }
    return kQueryTimeout;
}

CommandResult runDefault(const std::vector<std::string>& argv) { return runCommand(argv, commandTimeout(argv)); }

std::string describeFailure(const CommandResult& r) {
    if (r.timedOut) return "timed out waiting for authorisation or for the command to finish";
    std::string msg = r.err;
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r' || msg.back() == ' ')) msg.pop_back();
    return msg.empty() ? "the command failed" : msg;
}

}  // namespace pmp::linux
