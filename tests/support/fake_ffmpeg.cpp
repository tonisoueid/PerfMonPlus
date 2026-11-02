// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

// A stand-in for ffmpeg in the video recorder tests: reads raw video from stdin until it closes, then writes what it
// saw (its arguments and the byte count) to the file named by its last argument. PMP_FAKE_FFMPEG_EXIT makes it fail
// with that exit code after printing PMP_FAKE_FFMPEG_ERR; PMP_FAKE_FFMPEG_EARLY makes it quit before reading anything.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (std::getenv("PMP_FAKE_FFMPEG_EARLY")) {
        std::fprintf(stderr, "%s\n", std::getenv("PMP_FAKE_FFMPEG_ERR") ? std::getenv("PMP_FAKE_FFMPEG_ERR") : "encoder died");
        return 3;
    }
    unsigned long long bytes = 0;
    char buf[65536];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, stdin)) > 0) bytes += n;
    if (argc > 1) {
        std::ofstream out(argv[argc - 1]);
        for (int i = 1; i < argc - 1; ++i) out << argv[i] << '\n';
        out << "BYTES " << bytes << '\n';
    }
    if (const char* code = std::getenv("PMP_FAKE_FFMPEG_EXIT")) {
        std::fprintf(stderr, "%s\n", std::getenv("PMP_FAKE_FFMPEG_ERR") ? std::getenv("PMP_FAKE_FFMPEG_ERR") : "failed");
        return std::atoi(code);
    }
    return 0;
}
