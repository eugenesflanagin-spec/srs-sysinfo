# SRS - System Report Search

I wanted a Linux version of SIV (System Information Viewer) for myself, so I built one and set out to
improve on it along the way. SRS is free and open source for the Linux and open-source community.
(SRS is an independent project, not affiliated with SIV or its author.)

SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under the GNU General Public License v2.0 or
later (GPL-2.0-or-later).

> **Credit:** SIV - System Information Viewer - is the work of **Ray Hinchliffe, RH Software**
> (<https://rh-software.com>). SIV has been a remarkably thorough hardware and system viewer for
> Windows for decades, and SRS exists because of it. All credit for SIV's design, its item set and
> its presentation belongs to Ray Hinchliffe.
>
> **No SIV code:** SRS contains **no SIV code, binaries or data files**, and SIV was **not
> disassembled or decompiled**. The behaviour was specified only from SIV's observable output -
> its `-save` text dump, its panels and its printable strings - and then implemented
> independently in C on Linux data sources (sysfs, procfs, CPUID, hwmon, udev, hwdata).
> See [EVIDENCE.md](EVIDENCE.md) for how the specification was gathered.
>
> **Not affiliated:** SRS is an independent project. It is not affiliated with, endorsed by or
> supported by RH Software. Please do not send SRS questions or bug reports to RH Software.
> "SIV" and "System Information Viewer" are names of RH Software's product and are used here only
> to describe what SRS is modelled on.

## What it is

Every one of SIV64X 5.87's **538 report items** has a counterpart in SRS
([CHECKLIST.tsv](CHECKLIST.tsv)) - **364** backed by real Linux data, **136** that SIV would show
as "not present" on a typical PC (Itanium, Xeon PIRs, RAID cards, IPMI, ...) and **38**
Windows-only items, most of which show their Linux equivalent instead (services -> systemd,
drivers -> kernel modules, registry find -> `/etc` and `~/.config`, WER -> coredumpctl, ...).
SRS adds 20 items of its own (its benchmarks and their history, CPU Burnin and Stress, Find, ID Lookup and a few more), so
`sysreport-search --list` shows **558** in all.

| Program | What |
|---|---|
| `srs-gui` | GTK3 GUI: a main window with an Overview and one page per category, plus a classic view laid out like SIV's main window and panels; with arguments it is the CLI |
| `sysreport-search` | CLI: `-save` reports in SIV's text format, device-ID lookup, report search, burner, benchmarks |
| `srs-root` | optional helper (one `pkexec` prompt) that caches root-only data: dmidecode, SPD, SMART, ACPI tables, MSRs, PCI config space |
| `srs-sysreport` | writes a full text report folder (`~/sysreports/...`) that `srs-gui`'s Find searches |

## Using it

![SRS 2.0 main window: the Overview page](docs/images/overview-light.png)

- **Main window** (new in 2.0): a sidebar of pages - **Overview**, Processor, Memory,
  Motherboard & BIOS, Graphics & audio, Storage, Network, Sensors, **Power & cost** (2.2), USB &
  PCI devices, Operating system, Benchmarks & stress, Tools.
  - **Overview** answers "Is my PC OK?": status lights for CPU and graphics temperature, drive
    health (SMART) and temperature, hardware errors (machine checks, ECC errors, faulty memory
    pages), free space on the system disk, memory in use and sensor alarms, each in plain words
    with a **Details** button. Cards below show the key numbers for the processor, graphics,
    memory, storage, operating system and motherboard.
  - **Each page** starts with a short explanation and the key facts, then the full panels as tabs
    (same text, colours and live updates as the classic panels) with Refresh, Copy, Guide (F1),
    a Live switch and **Open in window**. **All N items** is a searchable jump list of every item
    of the page, the expert ones too.
  - **Tabs your way** (2.2): add (**+**, Ctrl+T), close, replace (right-click > Replace with...),
    drag and keep tabs; SRS remembers each page's tabs and their order. Per page in Options: the
    standard tabs, the ones you tick, or every item; or every item as a tab on all pages. Tab bar:
    one scrolling row, wrapped rows or a side list. **Lineups** save a whole arrangement: Basic,
    Enthusiast, Expert (default), and your own (save, export, import). Windows-only items and
    hardware this PC does not have are hidden; **Options -> Check now** (or the opt-in **Check
    daily**) also hides items that come out empty here, never running the network items;
    **Show hidden items** lists them.
  - **Sortable tables** (2.2): click a column heading (first click ascending ▲, second descending ▼);
    numbers sort by value with their units (MB/GB, MHz, °C, RPM, %), dates by date. **Reset view**
    puts the item's order back; Copy and Save Report always keep it.
  - **Startup manager** (2.2, Operating system -> Startup programs; the tab opens on the read-only
    **Report**, **Manage** is next to it): turn any autostart
    entry, user service or (after a warning, with pkexec) system service off and on again without
    deleting anything; Undo, Restore default, and **Add to startup** for a program or a command with
    an optional delay. Desktop essentials (Plasma, PipeWire, KWallet, portals, polkit agent) ask first.
  - **Badge** in the title bar: **Full access** when SRS runs as root or root data was collected
    (hover for when), else **Limited (user)** with a **Run as root...** button.
  - **CPU Clocks** (2.2) adds **Stock and in effect now**: the base clock measured (BCLK / eCLK),
    core base / boost / now at stock and in effect, and FCLK / UCLK / MCLK from the SMU (ryzen_smu).
  - **Search** in the title bar (Ctrl+F) finds any item by name or id (`processor:3` works);
    Enter searches the text of the current tab, F3 for the next match.
  - **Menu**: Save Report, Save All Items, Copy This Tab, Lineup, Options, Run as root, Find in System
    Report, Device ID Lookup, Classic View, the full Classic Menu, User Guide, About SRS, Quit.
  - The tools below open from the **Benchmarks & stress** and **Tools** pages too.
  - **Power & cost** (new in 2.2, under Sensors, Alt+9): an estimate of the whole PC's power at the wall with its likely
    range (the CPU and graphics card measured, the rest from an editable parts list and the power
    supply's efficiency), the cost at your price (flat, tiered or time-of-use, any currency, or an
    offline area average), recording off / for a set time / always into a local SQLite history,
    daily/weekly/monthly graphs, gaming and other busy sessions, CSV export, alarms as desktop
    notifications and a CO2 estimate. **Estimates only: not a meter reading and not a bill.**
    Nothing on the page goes online.
- **Classic view** (Menu -> Classic View, the **Classic view** button under the sidebar,
  `srs-gui --gui-classic`, or **Options -> Start in classic view** to make it the default) is
  SRS's SIV-style interface, unchanged from 1.1:
  - **Classic main window** = SIV's main panel with its button row: OK, Copy, **Linux** (SIV's
    "Windows"; opens General Info), Machine, Sensors, USB Bus, Processors, Volumes, Adapters,
    PCI Bus, About, Help, plus **Menu**.
  - Each panel has SIV's own sub-buttons (Processors: Features, Hypervisor, Groups, CPU-n, Cache-n;
    Sensors: Zero, Status, Cooling, Power, Hide; Adapters: Counters, ARP, Routing, TCP V4/V6, Wi-Fi,
    Bluetooth, DNS, Ping; PCI Bus: Properties, PnP Dev, PCI Dev, UAA Dev, ALL Dev; ...).
  - **Menu** (SIV's tray menu) lists every item by group, plus the tools, Save Report, Save All
    Items, Run as root and Exit. Right-click any panel for the same menu.
- **Tools:**
  - **CPU Burnin / CPU Stress** - per-CPU worker threads (Same CPU: Whetstone rating per CPU;
    Swap CPU: affinity test), affinity/APIC check, burn/cool cycles, current / average / rolling /
    maximum CPU load, clock, core temperature, board (Super-I/O) temperature and GPU load,
    temperature and clock, plots, Pass/Fail. Asks before starting, stops at a temperature limit
    (default 90 C) and a time cap. Each finished run's summary is saved to
    `~/.cache/srs/burnin-YYYYMMDD-HHMMSS.txt` for comparing runs.
  - **Benchmark** - Dhrystone 2.1 (DMIPS) and Whetstone (MWIPS) in SIV's modes (all CPUs
    concurrently or one at a time, D+D/W+W and D+W/W+D per core, physical / logical threads).
  - **IPI Delay** - core-to-core latency matrix with a heat map. **Latency** - memory latency by
    block size with a graph.
  - **Cache & Memory** - Read / Write / Copy bandwidth and latency of the RAM and the L1, L2, L3
    caches, with the measured base clock and the Infinity Fabric clock (engine from the author's
    Linux System Audit 0.2.0, now part of SRS; x86-64).
  - **Storage** - sequential 1 MiB, 4K random (high queue depth) and 4K Q1T1 read / write speed,
    IOPS and response times of the drive under a folder you choose, on a temporary test file only
    (unlinked at once, O_DIRECT, Linux AIO); RAM / network folders refused, USB drives opt-in.
  - **OS Latency** - how quickly the operating system answers: system calls, context switches,
    thread / program start, wake-ups, timer lateness (with the CPU scheduler in use), page faults
    and small-file operations, as median / 95% / 99% / worst; one or two CPUs, about 6 s.
  - **CPU Compute** - matrix multiply (SGEMM / DGEMM), peak multiply-add, FFT, N-body, Mandelbrot
    and a ray tracer in GFLOPS / Mpixels/s on 1 and all threads, with the best vector code the CPU
    has (SSE2, AVX2, AVX-512); every answer checked, a wrong one shown as WRONG.
  - **Everyday Work** - compression with the system's zlib / zstd / lz4 / xz (optional, loaded at
    run time), sorting, CRC32 / CRC32C / xxHash64 / FNV-1a, text search and JSON / UTF-8 checks and
    a bytecode interpreter, 1 and all threads, every result checked, generated data only.
  - **Network** - loopback TCP / UDP throughput, packet loss and round trip time (nothing leaves the
    PC), and an optional internet speed test against Cloudflare's public speed test (DNS, connect,
    HTTPS, download / upload with 1 and N streams) that asks separately and states its data use;
    libcurl optional; no addresses or network names in the results.
  - **GPU Benchmark** - through Vulkan: FP32 / FP16 / FP64 / INT32 multiply-add (GFLOPS / GIOPS),
    video memory read / write / copy and upload / download (GB/s), dispatch latency (µs); every
    result checked; Vulkan loaded at run time (optional); about 15 s at full GPU load.
  - **GPU Burnin** - repeated large matrix multiplications at full GPU load through Vulkan, ROCm /
    HIP, CUDA (or ZLUDA) or OpenCL, compute errors counted (0 = PASS), live temperature / power /
    clock / fan; stops by itself at the temperature limit (default 100 °C) or on a driver error;
    never changes clocks, voltages or fans.
  - **GPU Compute: Vulkan vs HIP vs CUDA vs OpenCL** - the same matrix multiply (own kernel and
    hipBLAS / cuBLAS / CLBlast), multiply-add, copy, upload / download and launch tests through every
    installed back end on the same GPU, side by side, fastest marked, % of Vulkan; "CUDA (ZLUDA)"
    and one column per OpenCL driver.
  - **SRS Pi** - pi to 128 thousand ... 32 million digits (Chudnovsky, or Gauss-Legendre / AGM like
    SuperPi), one or more threads, every digit checked against built-in SHA-256 checksums.
  - **Crypto** - AES-128-CTR and SHA-256 in MB/s, with AES-NI / SHA-NI and in plain C, one thread
    and all threads; FIPS test vectors checked first. **Prime Sieve** - counts the primes up to
    10^8 ... 10^11, checked against the known counts. Every run of the benchmarks above (all but the
    classic Benchmark, IPI Delay and Latency, which keep their own last result) is kept in a
    **Benchmark history** (`~/.cache/srs/bench-history.tsv`) with a build tag, for fair compares.
  - Every benchmark asks before it starts (the command line too; `--yes` skips the question). **Tested on one PC** (Ryzen 7 7800X3D, Radeon RX 9070 XT,
    wired network): parts that need other hardware may have a bug or two - please report them on
    [GitHub Issues](https://github.com/eugenesflanagin-spec/srs-sysinfo/issues) with your hardware
    and the report SRS made. Details in the [CHANGELOG](CHANGELOG.md) (Known limits).
  - **GPU features and the hardware I can test them on.** I build and test SRS on a single machine:
    a Ryzen 7 7800X3D with a Radeon RX 9070 XT. I can't try what I don't own, so GPU features for
    other hardware, such as NVIDIA CUDA (and ZLUDA), Intel graphics, or any ROCm/HIP and OpenCL
    setup other than mine, may have a bug or two I haven't met yet. If you find one, please open an
    issue on GitHub with your GPU and the report SRS made, and I'll fix it. So far the Vulkan tests,
    GPU Burnin and the GPU Compute comparison ran on lavapipe (the Mesa software renderer) only;
    for ROCm/HIP, CUDA and OpenCL I checked that the libraries load and that every function SRS
    needs is found, but did not run HIP, CUDA or OpenCL work on a GPU. GPU Burnin ends by itself on a
    driver error, a GPU that stops answering, a piece of work that takes far too long, or a
    temperature at the limit (default 100 °C); it never changes clocks, voltages or fans.
  - **Device ID Lookup** (Tree / Scan / Info modes over hwdata `pci.ids` / `usb.ids` / systemd-hwdb)
    and **Find** (regex over a `srs-sysreport` folder, with highlighting and F3 navigation).
- Live sections (sensors, clocks, counters, GPU, disk performance) refresh on a timer.
- Hot-keys (classic view): Ctrl+F Find, Ctrl+L Lookup, Ctrl+D ALL Dev, Ctrl+M Menu, Ctrl+S Save, Ctrl+H Highlight,
  F3 / Shift+F3, F5 Refresh, F1 Help, Esc closes a panel. In the new main window Ctrl+F goes to
  the search box (Ctrl+Shift+F opens Find), Alt+1 ... Alt+0, Alt+-, Alt+= and Alt+Backspace jump to a page,
  Ctrl+T adds a tab, Ctrl+W closes the tab in view and Ctrl+PageUp / Ctrl+PageDown step through the tabs.

## System tray

Like SIV's Icon Tray, SRS shows a tray icon (needs `libayatana-appindicator`; KDE Plasma, XFCE,
GNOME with the AppIndicator extension) with a live CPU / GPU temperature and load tooltip and a
menu (Show / Hide to Tray, Sensors, Processors, CPU Burnin, CPU Stress, Benchmark, Find, the SRS
Menu, Exit; middle-click toggles). **Menu -> Options** sets: tray icon, Close button hides to
tray, Minimize hides to tray (X11/XWayland), Start hidden in tray, and Start with the desktop
(autostart into the tray). `srs-gui --gui-tray` starts hidden. See [CHANGELOG.md](CHANGELOG.md).

## Live updates

Live sections refresh smoothly: each refresh is drawn off screen and only the lines whose values
changed are copied to the window, so the text no longer blinks, the scroll position stays put, and
an unchanged panel isn't redrawn at all. With the Sensors panel open this halved SRS's CPU use
(about 4% to 2% on a 7800X3D).

**Menu -> Options -> Update interval** sets how often live panels refresh: *Default* (each panel's
own rate; the classic main window every 2 s; the Overview and the key-fact tiles every 3 s), 0.5, 1, 2, 3, 5 or 10 seconds, or *Paused* (press F5 to
refresh a panel by hand). Applies at once to every open window and is saved as `Update Interval=`
(milliseconds; 0 = default, -1 = paused) in `~/.config/srs/SRSDEF.ini`. CPU Burnin / Stress always
updates its status every second.

## Dark mode

**Menu -> Options -> Theme**: *Light (SIV classic)*, *Dark*, or *Follow desktop*. Until you pick
one, the new main window follows the desktop and the classic view starts light. Dark
mode uses a dark panel with SIV's colour palette brightened so it stays readable, dark buttons and
graphs. *Follow desktop* reads the desktop colour scheme from xdg-desktop-portal
(`org.freedesktop.appearance color-scheme`, set by KDE Plasma / GNOME) and switches live when you
change it; without a portal it falls back to `GTK_THEME` or `~/.config/gtk-3.0/settings.ini`.
Changes apply at once to every open window and are saved as `Theme=` in `~/.config/srs/SRSDEF.ini`.

## Root-only data

SMBIOS serials/UUID, DIMM SPD, SMART, ACPI tables, MSRs and full PCI config space need root.
Either use **Menu -> Run as root** (one `pkexec` prompt; the results are cached in
`~/.cache/srs/root` and every panel shows them from then on), or run the CLI with `sudo`
(`sudo sysreport-search -save`): as root it uses the invoking user's cache, or collects one
first if none exists. SMART skips USB-attached disks unless `SRS_SMART_USB=1` is set.

**Menu -> Options -> Refresh root data at boot** keeps that cache fresh without any prompt: it
enables the packaged `srs-root@<you>.service` (one `pkexec` password prompt when you tick it),
which runs the same read-only `srs-root` collector as root once at every boot, at the lowest CPU
and disk priority, and hands the files to you. SRS itself still runs as your user. Untick to
disable it. By hand: `sudo systemctl enable --now srs-root@$USER.service` (or `disable`).

## Command line

    srs-gui -save                          SIV's default item set to the console
    srs-gui -save=FILE                     ... to a file
    srs-gui '-save=[cpuid][sensors]=FILE'  chosen items (SIV syntax); [all] = every item
    srs-gui '-save=[all]=FILE' --redact    every item, serials / UUIDs / MACs / host name masked
    srs-gui --list                         every item: id, category, bucket, group, title
    srs-gui guide ITEM [--print]           open the user guide at ITEM's section
    srs-gui id 1002:7550 | find PATTERN | devices
    srs-gui burn --threads N --seconds S [--mode same|swap] [--limit C] [--cool S] [--cycles N]
    srs-gui bench ac|as|dd|dw|ec|ep|lc|lp|pc|pp [--seconds S] [--yes]
    srs-gui ipi [--cpus N] [--rounds R] [--yes]    srs-gui latency [--max MB] [--yes]
    srs-gui pi [--digits 1m] [--threads N] [--yes]    srs-gui crypto [--yes]    srs-gui prime [--limit 1e9] [--yes]
    srs-gui os-latency [--samples N] [--tick-seconds S] [--only GROUPS] [--yes]
    srs-gui net [--local] [--internet] [--seconds S] [--streams N] [--yes] [--yes-internet]
    srs-gui gpu [--device N] [--seconds S] [--only TESTS] [--list] [--yes]
    srs-gui gpu-burn [--seconds N] [--backend vulkan|hip|cuda|opencl|auto] [--device N|all] [--limit C] [--yes]
    srs-gui gpu-compare [--backends vulkan,hip,cuda,opencl] [--seconds S] [--device N] [--yes]
    srs-gui work [--threads N] [--seconds S] [--only GROUPS] [--libs zlib,zstd,lz4,xz] [--single] [--yes]
    srs-gui compute [--threads N] [--seconds S] [--tests LIST] [--code auto|c|sse2|avx2|avx512] [--single] [--yes]

`sysreport-search` takes the same commands. `--help` lists them all; the full reference is in
the user guide (`/usr/share/doc/srs-sysinfo/SRS-User-Guide.pdf`, menu entry **SRS User Guide
(PDF)**, or the **Guide** button / F1 on any tab or panel), sources in [docs/](docs/).

## Safety

Read-only by design: nothing is written to sysfs, devices or firmware. No i2c/SMBus/DDC access,
no hidraw, no SMU commands, no network traffic apart from `ping` to the default gateway in the
Ping item and the Network benchmark's internet speed test, which runs only after you say yes to
its own question. Burner and benchmarks never start on their own (not from `-save` either), and
every benchmark asks before it starts. Root-only
data comes only from `srs-root`'s cache in `~/.cache/srs/root`. The one thing that changes your
system is the Startup manager, and only when you click a switch or a button there: it writes
autostart files in `~/.config/autostart` (never deletes; old versions go to
`~/.config/srs/startup-backup`) and runs `systemctl --user` or, after a warning, `pkexec systemctl`
for system services. `SRS_STARTUP_DRY_RUN=1` shows those commands instead of running them.

## Install

Every [release](https://github.com/eugenesflanagin-spec/srs-sysinfo/releases) has packages for
Arch, Debian 13, Ubuntu 24.04, Fedora 44 and openSUSE Tumbleweed, a generic tarball, and an
**AppImage** for any other distribution. On Arch: `yay -S srs-sysinfo` (AUR).

**AppImage** (one file, nothing to install, runs on distributions from about 2022 on, glibc 2.35+):

    chmod +x srs-sysinfo-2.2.0-x86_64.AppImage
    ./srs-sysinfo-2.2.0-x86_64.AppImage                  # the window
    ./srs-sysinfo-2.2.0-x86_64.AppImage --version        # with arguments it is sysreport-search
    ./srs-sysinfo-2.2.0-x86_64.AppImage srs-sysreport    # the full text report

It can update itself: AppImageUpdate (or a manager that uses it, such as AppImageLauncher) fetches only the changed
parts of a new release. **Run as root** works from the AppImage too (the helper script is copied
to a private folder first, because the administrator account cannot look inside a running
AppImage). The boot-time refresh (**Refresh root data at boot**) needs the distribution package.

## Build

    make native                 # srs-gui + sysreport-search for this machine
    make install-system DESTDIR=... SYSPREFIX=/usr
    make install                # per-user install into ~/.local
    make all                    # also i686/aarch64 CLIs and the Windows images (needs mingw-w64)
    make test-power             # unit tests of the Power & cost arithmetic and history database

Dependencies: gtk3, sqlite (srs-gui only: the Power & cost history), glibc, hwdata, systemd (hwdb). Optional: libayatana-appindicator (tray icon), dmidecode, i2c-tools (decode-dimms),
smartmontools, nvme-cli, pciutils, polkit (srs-root), vulkan-tools, clinfo, mesa-utils, iw,
bluez-utils, wireplumber, inxi, lshw, hwinfo, lm_sensors. Loaded at run time when present (a missing one only
skips its rows): zlib, zstd, lz4, xz (Everyday Work), libcurl (the internet speed test), the Vulkan loader
(vulkan-icd-loader) and a Vulkan driver (GPU benchmarks), ROCm / HIP (rocm-hip-runtime, hipblas), CUDA, an
OpenCL loader and driver, and CLBlast (the other GPU back ends). `make check-mnemonics` checks that no menu or
window page has two entries with the same Alt+letter.

## Source map

| File | Content |
|---|---|
| `sec.h`, `sec_core.c` | section API, registry, text backend, `-save`, data helpers |
| `sec_cpu.c` ... `sec_siv.c` | one file per group (cpu, machine, sensors, storage, network, os, gpu, bus, tools, siv) |
| `sec_stubs.c` | generated from `CHECKLIST.tsv` by `gen_stubs.py`: every SIV item is registered |
| `burner.c`, `bench.c` | CPU Burnin/Stress engine; Dhrystone, Whetstone, IPI, memory latency, run history |
| `bench_storage.c` | Storage benchmark (temporary unlinked test file, O_DIRECT, Linux AIO) |
| `bench_cachemem.c` | Cache & Memory benchmark (engine from Linux System Audit 0.2.0) |
| `bench_compute.c` | CPU Compute benchmark (SGEMM / DGEMM, multiply-add, FFT, N-body, Mandelbrot, ray tracer; SSE2 / AVX2 / AVX-512) |
| `bench_net.c` | Network benchmark (loopback sockets; internet test through libcurl loaded with dlopen) |
| `bench_gpu.c` | GPU Benchmark (Vulkan compute: multiply-add, video memory, transfers, dispatch latency) |
| `gpu_vk.c`, `bench_gpu_spv.inc`, `shaders/gpu/` | the Vulkan layer of the GPU benchmarks (loader opened with dlopen, own copy of the few Vulkan types it needs); the GLSL compute shaders and their pre-compiled SPIR-V (`shaders/gpu/build.sh`, `make gpu-shaders`) |
| `bench_gpuburn.c`, `gpu_backend.c`, `gpuburn_vk.c`, `gpuburn_rt.c`, `gpuburn_cl.c`, `gpu_kernels.h` | GPU Burnin and the compute back ends: Vulkan, ROCm / HIP and CUDA (`gpuburn_rt.c`), OpenCL, each loaded with dlopen; GPU sensors from hwmon |
| `bench_gpucompare.c` | GPU Compute comparison: Vulkan vs HIP vs CUDA vs OpenCL on the same GPU |
| `bench_work.c` | Everyday Work benchmark (compression via dlopen, sorting, checksums, text, interpreter) |
| `bench_os.c` | OS Latency benchmark (system calls, context switches, wake-ups, timers, page faults, file metadata) |
| `bench_pi.c`, `bench_crypto.c`, `bench_prime.c` | SRS Pi (big numbers, NTT, Chudnovsky / AGM), AES-128 / SHA-256, prime sieve |
| `tests/bench-test.sh` | correctness tests for SRS Pi, Crypto and Prime Sieve (short runs) |
| `tests/bench-compute-test.sh` | CPU Compute self-test on every code path, spoilt-result detection, Stop, CLI |
| `tests/bench-net-test.sh` | Network loopback test, privacy checks, Stop, refusals, strace (loopback only), CLI |
| `tests/bench-gpu-test.sh` | GPU benchmarks on lavapipe only (software Vulkan; refuses any real GPU), ASan + UBSan, Vulkan layouts, shaders, Stop, CLI |
| `tests/bench-work-test.sh` | Everyday Work known answers, round trips, "not installed" path, Stop, CLI (ASan + UBSan) |
| `tests/bench-os-test.sh` | OS Latency engine and CLI test (tiny runs, throw-away HOME; `make test-bench` runs all bench tests) |
| `gui_gtk.c` | the GTK GUI; `core.c` / `cli.c` the lookup / find engine and CLI |
| `power*.c`, `tools/gen_power_rates.py`, `data/` | the Power & cost page: `power_model.c` (estimate, counters, prices, alarms; `make test-power`), `power_src.c` (sysfs sources), `power_db.c` (SQLite history), `power_rates.c` + `power_rates_data.inc` (offline price / CO2 tables, made by the script) |
| `gui_tabs.inc`, `tblsort.c`, `gui_startup.c` | main window: tab lineups and the item jump list; unit-aware table sorting; the Startup manager |
| `gui.c`, `plat_win32.c`, `srs.rc` | optional Windows build (`SRS64X.exe` / `SRS32X.exe`, search features only) |
| `guide.c`, `guide_map.inc` | Guide button / F1 / `guide`: item -> guide section (map generated by `docs/gen_guide_map.py`) |
| `sync_checklist.py` | copies each item's bucket, root flag and source from the code into `CHECKLIST.tsv` |
| `docs/` | user guide, CLI reference, glossary, section reference (Markdown), prebuilt `SRS-User-Guide.pdf` + `html/`; `build_srs_pdf.py` rebuilds them (pandoc + WeasyPrint) |

## Licence

SRS is free software under the **GNU General Public License v2.0 or later** ([LICENSE](LICENSE)).
Device names come from the hwdata project's `pci.ids` / `usb.ids` / `pnp.ids`, which are not
part of SRS.

The new benchmarks are SRS's own code, written from published sources: the Chudnovsky series
(D. V. and G. V. Chudnovsky, 1988), the Gauss-Legendre / Brent-Salamin AGM (1976), FIPS 197 (AES),
FIPS 180-4 (SHA-256) and the sieve of Eratosthenes; CPU Compute uses the register-blocked
matrix multiply described by Goto and van de Geijn (2008), the Cooley-Tukey FFT (1965), direct-sum
N-body gravity with Plummer softening, the Mandelbrot escape-time algorithm and Whitted-style ray
tracing (1980); Everyday Work uses the xxHash64 design by Yann Collet (written from its published
specification), CRC32 / CRC32C with slicing-by-8 (Kounavis and Berry), FNV-1a (Fowler, Noll, Vo),
Boyer-Moore-Horspool (1980) and multikey quicksort (Bentley and Sedgewick, 1997), and loads the
system's zlib, zstd, lz4 and xz libraries at run time (not shipped or linked). The Network
benchmark's internet test uses Cloudflare's public speed test endpoints (speed.cloudflare.com,
`/__down` and `/__up`; SRS is not affiliated with Cloudflare) through libcurl, loaded at run time.
The GPU benchmarks open the system's Vulkan loader at run time and use their own copy of the few
Vulkan structures they need, laid out as in the Khronos Vulkan-Headers (Apache-2.0 OR MIT); they
were tested on Mesa's lavapipe software renderer (MIT). GPU Burnin's way of finding compute errors
(every result compared with the first) follows gpu-burn by Ville Timonen (BSD-2-Clause,
<https://github.com/wilicc/gpu-burn>); the code is SRS's own. ROCm / HIP, CUDA, OpenCL and their
matrix libraries (hipBLAS, cuBLAS, CLBlast) are opened at run time when installed and are not
shipped. SRS Pi's reference checksums were computed once
with [mpmath](https://mpmath.org) (BSD licence); mpmath is not part of SRS.

The Power & cost page's built-in price and CO2 tables come from public data: U.S. Energy
Information Administration (Electric Power Monthly; public domain), NREL utility rates by ZIP code
(CC BY 4.0), Eurostat (`nrg_pc_204`; reuse with acknowledgement), Ofgem (energy price cap) and
Ember (yearly electricity data; CC BY 4.0). These organisations are not responsible for SRS's use
of their data. Details and dates: [CREDITS.md](CREDITS.md) and the user guide, section 6.11.
