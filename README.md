# PerfMonPlus

A native, cross-platform system monitor in the spirit of a modern Task Manager:
one glance shows which process and which subsystem is responsible for a slowdown.
Linux first, native Qt 6 UI.

**Status: phase P4 (Tools) on top of P2 (Processes) and P3 (monitoring completeness).** The `perfmonplus` app has the shell (sidebar, status bar), a live
Summary page, and a Performance page with CPU, Memory, Disks, Network, GPU, NPU, Energy and Thermals detail (a panel
appears only for hardware that exists), in Dark, Light and Green-phosphor themes. It also has **System Info**,
**Startup apps**, **Users** and **Services** pages, a **Settings** dialog, an F12 screensaver and an opt-in update
check. The **Processes** page lists every process with four views, a filter, sortable columns, a details pane and
identity-safe actions. The **Tools** pages are all there: **Power & Freq**, **Connections**, **Installed Apps**
(with a Kernel modules tab), **Disk Space** and **Benchmarks**, plus the **Flight Recorder** (record, save and replay
`.pmptrace` files) and screen video recording. Everything is free; there is no paid edition. The `perfmonplus-probe`
CLI from phase P0 is still available.

Design principle: a measuring instrument never makes up an answer. Every reading
carries an explicit availability state, so a measured zero (`Zero KB/s`) is never
confused with a missing sensor (`Unavailable`), and stopped sampling leaves a gap
instead of a smooth line.

- Recording file format: [`docs/trace-format.md`](docs/trace-format.md)

## Requirements

GCC >= 12 (C++20), CMake >= 3.24, Ninja, Catch2 v3, nlohmann-json, zlib (`zlib1g-dev`), Qt 6.4+ (Widgets).
Optional at run time: `ffmpeg` (video recording), `gio` (Move to Trash), `xdg-open` (Reveal), `flatpak`/`snap`/`rpm`
(their apps appear on Installed Apps when present).

```bash
sudo apt install -y cmake ninja-build g++ catch2 nlohmann-json3-dev zlib1g-dev qt6-base-dev
```

No `sudo`? Unpack the Qt `-dev` packages into a private prefix and pass it to CMake with
`-DCMAKE_PREFIX_PATH=<prefix>/usr` (the runtime libraries must already be installed).

## Build

```bash
# Debug: development and tests
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Release: real use and the timing check
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel

# No Qt on this machine? The core, Linux providers, trace format, view models and the probe still build and test
cmake -S . -B build-core -G Ninja -DPMP_BUILD_GUI=OFF
cmake --build build-core && ctest --test-dir build-core -LE live
```

A Debug build is for development: it is several times slower, so the Benchmarks page says its scores are not
comparable with Release ones (and never mixes them in the history comparison).

## Run

### The GUI

```bash
./build/apps/perfmonplus/perfmonplus                      # open the window
./build/apps/perfmonplus/perfmonplus --low-effects        # no glow, 30 Hz cap: cheapest mode
./build/apps/perfmonplus/perfmonplus --theme green        # dark | light | green for this run
./build/apps/perfmonplus/perfmonplus --page processes     # open on a page for this run (the file name used by --screenshot-dir)
./build/apps/perfmonplus/perfmonplus --open-recording trace.pmptrace [--play]   # replay a recording (see below)
./build/apps/perfmonplus/perfmonplus --run-seconds 10     # headless-friendly: quit after 10 s, print its own CPU use
./build/apps/perfmonplus/perfmonplus --screenshot-dir out # save a PNG of every page, then quit (--size 900x575 to choose the size)
./build/apps/perfmonplus/perfmonplus --version
```

Ctrl+1..9 jump to the first nine pages. The **Colors** button (bottom of the sidebar) switches theme and
adjusts saturation, glow (bloom) and frame rate live; choices are remembered between runs. In a window narrower than
1060 px the sidebar steps aside by itself and returns when the window is wide again (your own show/hide choice is kept).
Stretches with no data (a suspend, a stopped source) are shaded on every graph instead of being left looking like a dip.
On a machine without a display, prefix with `QT_QPA_PLATFORM=offscreen`.

### Processes

- **Views** (the combo above the table): *Process View* (Applications, Background and System groups; an application
  is one systemd `app.slice` unit, so Firefox's many processes share a row), *Tree View*, *Flat List*, *By User*.
- **Filter** by name, user or PID (case-insensitive, matched literally). A match keeps its parents visible.
- **Columns**: right-click a header to show or hide optional columns (GPU, disk, threads, private memory, page
  faults, priority, start and CPU time, command line, path, kind, a relative energy estimate...). Click a header to
  sort and again to reverse; the selected row stays selected and in view while rows move. Cells with a number show
  an inline bar. Fields you may not read (another user's disk I/O or executable) say *Restricted*, never `0`.
- **Tombstones**: a process that just exited stays as a red row for 5–60 s (Settings > Sampling; Off disables it),
  so short-lived processes are visible. *Pin this row* keeps one.
- **Details pane** under the table: path, identity, lifetime, processor, memory and disk, mini CPU and memory graphs,
  open files and sockets. The command line is hidden until you click *Show command line*; the environment is never
  read unless you tick *Show environment in process details* in Settings > Privacy. Enter shows or hides the pane.
- **Actions** (toolbar, context menu, `Delete` = End task with a confirmation): End task (then a *Force quit* offer
  if it ignores the request), End process tree, Suspend, Resume, Set priority, Set affinity, Open file location,
  Copy (row, PID, path), Search online. Every one re-checks the process's identity first: if the PID now belongs to
  another process nothing is sent and you are told so. PerfMonPlus never runs as root and does not elevate, so
  another user's processes can't be signalled (the message says why). Kernel threads can't be ended.
- **Graph hover and culprits**: hover any Summary or Performance graph for a cursor and a tooltip with the exact
  value (linked graphs share the cursor). Drag across the CPU or memory graph to list the processes that used the
  resource then — including ones that have exited — and click *Go to process*. Settings > Sampling can turn the
  per-process history off.

Not offered on Linux: application icons, *Bring to front* (Wayland has no generic way), per-process network and NPU
use, and exit codes of other processes.

### Pages, Settings and the screensaver

- **System Info**: CPU facts, firmware, adapters, displays and sensors. Serial numbers and MAC addresses stay hidden,
  in the page and in Copy / Export, unless you tick *Include identifiers*.
- **Startup apps**: XDG autostart entries and systemd user services. Disabling a system entry writes a per-user
  override; nothing is edited outside your own config directory.
- **Users**: one row per logged-in user with their CPU, memory and disk; Lock and Sign off use `loginctl`.
- **Services**: systemd units with Start / Stop / Restart / Logs through `systemctl`, which asks polkit for
  permission itself. PerfMonPlus never runs as root and installs no polkit or udev files.
- **Settings** (bottom of the sidebar): launch at login, update interval (or pause), graph history window, number of
  top processes, frame rate, bloom, low effects, binary or decimal units, °C or °F, the update check, and a table of
  providers where each can be switched off (its numbers then read *Disabled*).
- **F12** opens a full-screen tunnel of rings that move faster and glow brighter as CPU, memory, GPU and disk get
  busier. Any key or a click closes it.

### Flight Recorder

*Record a slowdown, save it, and scrub back through it after the spike is gone.*

- The red button in the footer (or **Ctrl+R**) starts a recording; the timecode counts up (`HH:MM:SS.ff`) and the size
  estimate grows next to it. Stop it, then **File ▸ Save Recording…** (**Ctrl+S**). A recording stops by itself at the
  limit set in Settings (30 minutes by default) and says so.
- **Rolling buffer** (Settings ▸ General): keeps the last N minutes in memory all the time. The button then reads
  *Save what just happened*: it writes the last N minutes to a file without stopping.
- **File ▸ Open Recording…** (**Ctrl+O**), *Open Recent*, or drop a file on the window. The app enters **Replay**: a
  banner says which file, when and on what system it was recorded, with **Return to Live**. Summary, Performance and
  Power & Freq draw from the recording with the same widgets as live; the other pages say *Not recorded*.
  Transport: **Space** play/pause, **←/→** one sample, **Shift+←/→** ten seconds, **Home/End**, speed 0.25×–8×, and a
  scrub bar. Gaps (sleep, a provider that failed) stay gaps. "Which process caused this?" works on the recorded top
  processes.
- A recording holds the seven panels' numbers plus the busiest processes by CPU and by memory. It holds **no** user
  names, host name, paths, command lines, serial numbers or addresses. A truncated or damaged file replays what is
  readable and says what was skipped. The format is documented in [`docs/trace-format.md`](docs/trace-format.md); a
  recording made on one operating system plays on any other.
- **Screen video** (**File ▸ Record Video…**, **Ctrl+Shift+R**) records PerfMonPlus's own window through `ffmpeg`
  (VP9 in WebM, or H.264 in MP4). Without ffmpeg it tells you what to install. It costs CPU while it runs, and a
  blinking `● VIDEO` shows in the footer.

### Tools

- **Power & Freq**: a frequency graph per core (coloured by core class), governor, energy preference and driver,
  throttle events, power per domain (package, cores, uncore, memory, platform; `Restricted` with the usual
  explanation when the kernel hides RAPL from non-root users) and the battery's health, cycles, voltage and
  temperature.
- **Connections**: every TCP/UDP socket with its process, state and addresses. **Listening / Exposed** lists what this
  computer accepts connections on, marking wildcard binds as reachable from other machines (a firewall may still
  block them). Other users' sockets show *Restricted*, never a guess. Per-connection traffic is *Unavailable* (it
  needs a privileged helper). Host names are looked up **only** if you turn that on in Settings ▸ Privacy.
- **Installed Apps**: apt, rpm, Flatpak, Snap and AppImage apps with their size on disk, a size bar by source, and
  *Reveal* (for apt, the folder of the package's files). **Uninstall** never runs as administrator: a per-user Flatpak is removed after you confirm; for
  everything else it shows the exact command to run yourself. The *Kernel modules* tab lists `/proc/modules`.
- **Disk Space**: choose a volume or a folder and scan it in the background (idle priority, cancellable, other file
  systems skipped, nothing followed through symbolic links). Results: a folder-size map (click to select,
  double-click to open a folder, Backspace to go up), the largest files, and a breakdown by file type. **Move to
  Trash** uses `gio trash` after a confirmation that defaults to Cancel; nothing is ever deleted permanently.
- **Benchmarks**: CPU single- and multi-thread, memory bandwidth and latency, disk sequential write/read and random
  read. A warning comes first (time, heat, the test file and free space, battery). Results stay on this machine, with
  your earlier runs for comparison; scores are only compared between the same CPU and test version. A GPU test is not
  included. Disk tests write one temporary file under `~/.cache/perfmonplus`, removed when they finish or are cancelled.

### Update check (off until you configure it)

Nothing is requested unless you enter an *Update manifest address* in Settings > Privacy. The check is one plain GET
of an `https://` (or `file://`) address with no query string, cookies, body or telemetry. The manifest is JSON:

```json
{"version": "0.2.0", "url": "https://example.org/perfmonplus/releases/0.2.0"}
```

If `version` is newer than the running one, a banner offers *Open download page* (the page address must be https).

### Reading CPU package power (optional)

Since Linux 5.10 the RAPL energy counter is readable by root only, because it is a side channel. PerfMonPlus then
shows *Restricted* and falls back to the battery's discharge rate. If you accept the trade-off you can give one group
read access yourself; PerfMonPlus never does this for you and never needs to run as root:

```bash
sudo groupadd --system perfmonplus
sudo usermod -aG perfmonplus "$USER"          # log out and in again
echo 'SUBSYSTEM=="powercap", ACTION=="add|change", RUN+="/bin/chgrp perfmonplus /sys%p/energy_uj", RUN+="/bin/chmod g+r /sys%p/energy_uj"' \
  | sudo tee /etc/udev/rules.d/90-perfmonplus-rapl.rules
sudo udevadm control --reload && sudo udevadm trigger --subsystem-match=powercap
```

### The probe

`perfmonplus-probe` prints one JSON object per sample (JSON Lines) on stdout.

```bash
./build/apps/probe/perfmonplus-probe                          # one line per second, Ctrl+C to stop
./build/apps/probe/perfmonplus-probe --count 3 --interval-ms 500
./build/apps/probe/perfmonplus-probe --count 1 --top 5        # 5 busiest processes
./build/apps/probe/perfmonplus-probe --include-self           # include the probe itself
./build/apps/probe/perfmonplus-probe --version
```

Options: `--interval-ms N` (default 1000), `--count N` (0 = forever), `--top N`
(default 12), `--include-self`, `--proc-root DIR`, `--sys-root DIR`.

Pretty-print the last sample:

```bash
./build/apps/probe/perfmonplus-probe --count 3 --interval-ms 500 | tail -1 | python3 -m json.tool | less
```

Notes:
- The first sample's rates (CPU %, disk and network throughput) are
  `"availability": "pending"`, because a rate needs two samples.
- Unavailable readings have `"value": null`; a measured zero has `"value": 0`.

## Test

The GUI tests run headless (they default to `QT_QPA_PLATFORM=offscreen`).

```bash
ctest --test-dir build -LE live --output-on-failure   # unit + GUI tests, fake /proc trees (~5 s)
ctest --test-dir build -L live --output-on-failure    # live tests: real /proc, forks a child, spins all cores ~1.5 s
ctest --test-dir build-rel --output-on-failure        # Release run: also the sampler timing budget and the GUI CPU budget (~1 min)
./build/tests/pmp_tests "[format]"                    # one area by tag: [cpu] [disk] [net] [process] [sampler] [json] [theme] [geometry] ...
./build/tests/pmp_tests "[trace]"                    # the recording format: [container] [codec] [recorder] [player] [golden]
./build/tests/pmp_gui_tests "[summary]"               # GUI tags: [gui] [widgets] [shell] [summary] [performance] [colors] [clock] [settings] [update] [screensaver] [processes] [details] [actions-ui] [hover] [culprits] [links] [recording] [replay] [connections] [apps] [disk] [bench-page] [video]
```

The timing and CPU budget tests (sampler cost per process; the app staying under 2% of the whole machine, or
0.5% in low-effects mode) only run in optimized builds; in Debug they are reported as skipped.

## Layout

```
core/            pmp_core: readings, formatting, history ring/store, per-process history, gap rule, Sampler (no OS/Qt code)
trace/           pmp_trace: the .pmptrace container, recorder and player (no OS/Qt code; needs only zlib)
platform/linux/  pmp_linux: CPU, memory, disk, network, process, GPU, NPU, energy, thermal providers; services, sessions,
                 startup and system-info collectors; process inspector and identity-checked process actions; connections,
                 installed-software, disk-scan and benchmark engines
ui/viewmodel/    pmp_viewmodel: themes, graph geometry, meter maths, page text (no Qt; unit-tested)
ui/gui/          pmp_gui: Qt 6 widgets, pages, sampling thread, animation clock
apps/perfmonplus the GUI application
apps/probe/      perfmonplus-probe CLI and JSON serialisation
tests/           unit tests (fake /proc via FakeRoot) and live tests
docs/            functional spec and implementation plans
```

## License

GPL-3.0-or-later. See [`LICENSE`](LICENSE).
