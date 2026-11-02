# AGENTS.md

Guidance for AI coding agents working in this repository. Human-oriented
intro, requirements and build notes live in [`README.md`](README.md).

## Project

PerfMonPlus — a native, Linux-first system monitor (in the spirit of a modern
Task Manager) in C++20 with a Qt 6 Widgets UI. One glance shows which process
or subsystem is responsible for a slowdown. Licence: GPL-3.0-or-later; every
source file carries `SPDX-License-Identifier: GPL-3.0-or-later`.

**Core design rule: a measuring instrument never makes up an answer.** Every
reading carries an explicit availability state — a measured zero (`Zero KB/s`)
is never confused with a missing sensor (`Unavailable`), and stopped sampling
leaves a gap in graphs instead of a smooth line. Every change you make must
preserve this.

## Layout

| Path | Contents |
|---|---|
| `core/` | Qt-free core: `Provider`/`Sampler` abstractions, history store, byte/rate formatting |
| `platform/linux/` | Linux collectors: `/proc`, sysfs, systemd, DRM fdinfo, udev … |
| `trace/` | Flight Recorder: `.pmptrace` container, recorder, player (format spec: [`docs/trace-format.md`](docs/trace-format.md)) |
| `ui/viewmodel/` | Qt-free models between collectors and widgets |
| `ui/gui/` | Qt Widgets application: pages, custom widgets, controllers |
| `apps/perfmonplus/` | The GUI executable |
| `apps/probe/` | `perfmonplus-probe` CLI — streams one JSON snapshot per sample to stdout |
| `tests/` | Catch2 v3 suites: `core/`, `linux/`, `gui/`, `viewmodel/`, `live/`, `probe/`, shared `support/` and `data/` |

Dependencies flow downward only: `apps → ui/gui → ui/viewmodel → platform → core`;
`trace` sits beside `core`. Nothing in `core/`, `platform/`, `trace/` or
`ui/viewmodel/` may include Qt.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   # development + tests
cmake --build build

cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release   # real use + timing gates
cmake --build build-rel
```

- `-DPMP_BUILD_GUI=OFF` builds and tests the Qt-free layers on a machine
  without Qt.
- Toolchain: GCC ≥ 12 (C++20), CMake ≥ 3.24, Ninja, Catch2 v3, nlohmann-json,
  zlib, Qt 6.4+ (Widgets, Network). Optional at run time: `ffmpeg` (video
  recording), `gio`, `xdg-open`, `flatpak`/`snap`/`rpm`.
- `build*/` is gitignored; never commit build output.

## Test

```bash
ctest --test-dir build --output-on-failure -j8      # full suite
ctest --test-dir build -R "<regex of test name>"    # one test
```

Conventions:

- Tests are **named as sentences** stating the required behaviour
  ("rates restart after pause/suspend/re-enable", "no active units means no
  main-pid query, in either scope"). Match the style; a fix is not done until
  a test states it.
- Labels: `[gui]` (auto-run with `QT_QPA_PLATFORM=offscreen` — headless-safe),
  `[live]` (read the real machine; results depend on hardware),
  `[probe]`, plus per-area tags like `[services]`, `[minor]`.
- CPU-budget gates ("stays under 2% total cpu") run against the **Release**
  build and skip where they cannot measure fairly.
- Long fuzz/live tests can starve under extreme `-j`; if one fails, re-run it
  in isolation before believing it.

## Run

```bash
build/apps/perfmonplus/perfmonplus            # GUI (use QT_QPA_PLATFORM=offscreen headless)
build/apps/probe/perfmonplus-probe            # JSON snapshot stream on stdout
```

Useful GUI flags: `--run-seconds N` (auto-quit — use in smoke tests),
`--screenshot-dir DIR` (PNG per page — visual checks without a display),
`--size WxH`, `--theme NAME`, `--page NAME`, `--low-effects`,
`--open-recording FILE.pmptrace [--play]`, `--record [--record-video FILE]`,
`--scan DIR` (Disk Space page).

## Change conventions

- Every reading gets an availability state; never synthesise plausible values.
- Untrusted text (process names, unit descriptions, file names) is sanitised
  before it reaches widgets — keep it plain text, never rich text or mnemonics.
- New source files: SPDX header, match surrounding naming and comment density.
- `git` style: conventional commits (`feat(gui): …`, `fix(linux): …`,
  `docs: …`, `build: …`), work on a branch, merge to `main` when green.
