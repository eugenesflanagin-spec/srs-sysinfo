# SRS - System Report Search

I wanted a Linux version of SIV (System Information Viewer) for myself, so I built one and set out to
improve on it along the way. SRS is free and open source for the Linux and open-source community.
(SRS is an independent project, not affiliated with SIV or its author.)

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

| Program | What |
|---|---|
| `srs-gui` | GTK3 GUI: a main window with an Overview and one page per category, plus a classic view laid out like SIV's main window and panels; with arguments it is the CLI |
| `sysreport-search` | CLI: `-save` reports in SIV's text format, device-ID lookup, report search, burner, benchmarks |
| `srs-root` | optional helper (one `pkexec` prompt) that caches root-only data: dmidecode, SPD, SMART, ACPI tables, MSRs, PCI config space |
| `srs-sysreport` | writes a full text report folder (`~/sysreports/...`) that `srs-gui`'s Find searches |

## Using it

![SRS 2.0 main window: the Overview page](docs/images/overview-light.png)

- **Main window** (new in 2.0): a sidebar of pages - **Overview**, Processor, Memory,
  Motherboard & BIOS, Graphics & audio, Storage, Network, Sensors, USB & PCI devices, Operating
  system, Benchmarks & stress, Tools.
  - **Overview** answers "Is my PC OK?": status lights for CPU and graphics temperature, drive
    health (SMART) and temperature, hardware errors (machine checks, ECC errors, faulty memory
    pages), free space on the system disk, memory in use and sensor alarms, each in plain words
    with a **Details** button. Cards below show the key numbers for the processor, graphics,
    memory, storage, operating system and motherboard.
  - **Each page** starts with a short explanation and the key facts, then the full panels as tabs
    (same text, colours and live updates as the classic panels) with Refresh, Copy, Guide (F1),
    a Live switch and **Open in window**. **All N items** lists every item of the page, the
    expert ones too.
  - **Search** in the title bar (Ctrl+F) finds any item by name or id (`processor:3` works);
    Enter searches the text of the current tab, F3 for the next match.
  - **Menu**: Save Report, Save All Items, Copy This Tab, Options, Run as root, Find in System
    Report, Device ID Lookup, Classic View, the full Classic Menu, User Guide, About SRS, Quit.
  - The tools below open from the **Benchmarks & stress** and **Tools** pages too.
  - **Power & cost** (new in 2.1): an estimate of the whole PC's power at the wall with its likely
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
  - **Device ID Lookup** (Tree / Scan / Info modes over hwdata `pci.ids` / `usb.ids` / systemd-hwdb)
    and **Find** (regex over a `srs-sysreport` folder, with highlighting and F3 navigation).
- Live sections (sensors, clocks, counters, GPU, disk performance) refresh on a timer.
- Hot-keys (classic view): Ctrl+F Find, Ctrl+L Lookup, Ctrl+D ALL Dev, Ctrl+M Menu, Ctrl+S Save, Ctrl+H Highlight,
  F3 / Shift+F3, F5 Refresh, F1 Help, Esc closes a panel. In the new main window Ctrl+F goes to
  the search box (Ctrl+Shift+F opens Find), Alt+1 ... Alt+0, Alt+- and Alt+= jump to a page and
  Ctrl+W closes an extra tab.

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
    srs-gui bench ac|as|dd|dw|ec|ep|lc|lp|pc|pp [--seconds S]
    srs-gui ipi [--cpus N] [--rounds R]    srs-gui latency [--max MB]

`sysreport-search` takes the same commands. `--help` lists them all; the full reference is in
the user guide (`/usr/share/doc/srs-sysinfo/SRS-User-Guide.pdf`, menu entry **SRS User Guide
(PDF)**, or the **Guide** button / F1 on any tab or panel), sources in [docs/](docs/).

## Safety

Read-only by design: nothing is written to sysfs, devices or firmware. No i2c/SMBus/DDC access,
no hidraw, no SMU commands, no network traffic apart from `ping` to the default gateway in the
Ping item. Burner and benchmarks never start on their own (not from `-save` either). Root-only
data comes only from `srs-root`'s cache in `~/.cache/srs/root`.

## Install

Every [release](https://github.com/eugenesflanagin-spec/srs-sysinfo/releases) has packages for
Arch, Debian 13, Ubuntu 24.04, Fedora 44 and openSUSE Tumbleweed, a generic tarball, and an
**AppImage** for any other distribution. On Arch: `yay -S srs-sysinfo` (AUR).

**AppImage** (one file, nothing to install, runs on distributions from about 2022 on, glibc 2.35+):

    chmod +x srs-sysinfo-2.0.0-x86_64.AppImage
    ./srs-sysinfo-2.0.0-x86_64.AppImage                  # the window
    ./srs-sysinfo-2.0.0-x86_64.AppImage --version        # with arguments it is sysreport-search
    ./srs-sysinfo-2.0.0-x86_64.AppImage srs-sysreport    # the full text report

It can update itself: AppImageUpdate (or a manager that uses it, such as AppImageLauncher) fetches only the changed
parts of a new release. **Run as root** works from the AppImage too (the helper script is copied
to a private folder first, because the administrator account cannot look inside a running
AppImage). The boot-time refresh (**Refresh root data at boot**) needs the distribution package.

## Build

    make native                 # srs-gui + sysreport-search for this machine
    make install-system DESTDIR=... SYSPREFIX=/usr
    make install                # per-user install into ~/.local
    make all                    # also i686/aarch64 CLIs and the Windows images (needs mingw-w64)

Dependencies: gtk3, glibc, hwdata, systemd (hwdb). Optional: libayatana-appindicator (tray icon), dmidecode, i2c-tools (decode-dimms),
smartmontools, nvme-cli, pciutils, polkit (srs-root), vulkan-tools, clinfo, mesa-utils, iw,
bluez-utils, wireplumber, inxi, lshw, hwinfo, lm_sensors.

## Source map

| File | Content |
|---|---|
| `sec.h`, `sec_core.c` | section API, registry, text backend, `-save`, data helpers |
| `sec_cpu.c` ... `sec_siv.c` | one file per group (cpu, machine, sensors, storage, network, os, gpu, bus, tools, siv) |
| `sec_stubs.c` | generated from `CHECKLIST.tsv` by `gen_stubs.py`: every SIV item is registered |
| `burner.c`, `bench.c` | CPU Burnin/Stress engine; Dhrystone, Whetstone, IPI, memory latency |
| `gui_gtk.c` | the GTK GUI; `core.c` / `cli.c` the lookup / find engine and CLI |
| `power*.c`, `tools/gen_power_rates.py`, `data/` | the Power & cost page: `power_model.c` (estimate, counters, prices, alarms; `make test-power`), `power_src.c` (sysfs sources), `power_db.c` (SQLite history), `power_rates.c` + `power_rates_data.inc` (offline price / CO2 tables, made by the script) |
| `gui.c`, `plat_win32.c`, `srs.rc` | optional Windows build (`SRS64X.exe` / `SRS32X.exe`, search features only) |
| `guide.c`, `guide_map.inc` | Guide button / F1 / `guide`: item -> guide section (map generated by `docs/gen_guide_map.py`) |
| `sync_checklist.py` | copies each item's bucket, root flag and source from the code into `CHECKLIST.tsv` |
| `docs/` | user guide, CLI reference, glossary, section reference (Markdown), prebuilt `SRS-User-Guide.pdf` + `html/`; `build_srs_pdf.py` rebuilds them (pandoc + WeasyPrint) |

## Licence

SRS is free software under the **GNU General Public License v2.0 or later** ([LICENSE](LICENSE)).
Device names come from the hwdata project's `pci.ids` / `usb.ids` / `pnp.ids`, which are not
part of SRS.

The Power & cost page's built-in price and CO2 tables come from public data: U.S. Energy
Information Administration (Electric Power Monthly; public domain), NREL utility rates by ZIP code
(CC BY 4.0), Eurostat (`nrg_pc_204`; reuse with acknowledgement), Ofgem (energy price cap) and
Ember (yearly electricity data; CC BY 4.0). These organisations are not responsible for SRS's use
of their data. Details and dates: [CREDITS.md](CREDITS.md) and the user guide, section 6.7.
