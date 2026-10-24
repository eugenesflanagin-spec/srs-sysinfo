# SRS - System Report Search

**A clean-room implementation of SIV64X (System Information Viewer) for Linux.**

> **Credit:** SIV - System Information Viewer - is the work of **Ray Hinchliffe, RH Software**
> (<https://rh-software.com>). SIV has been a remarkably thorough hardware and system viewer for
> Windows for decades, and SRS exists because of it. All credit for SIV's design, its item set and
> its presentation belongs to Ray Hinchliffe.
>
> **Clean room:** SRS contains **no SIV code, binaries or data files**, and SIV was **not
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
| `srs-gui` | GTK3 GUI laid out like SIV's main window and panels; with arguments it is the CLI |
| `sysreport-search` | CLI: `-save` reports in SIV's text format, device-ID lookup, report search, burner, benchmarks |
| `srs-root` | optional helper (one `pkexec` prompt) that caches root-only data: dmidecode, SPD, SMART, ACPI tables, MSRs, PCI config space |
| `srs-sysreport` | writes a full text report folder (`~/sysreports/...`) that `srs-gui`'s Find searches |

## Using it

- **Main window** = SIV's main panel with its button row: OK, Copy, **Linux** (SIV's "Windows"),
  Machine, Sensors, USB Bus, Processors, Volumes, Adapters, PCI Bus, About, Help, plus **Menu**.
- Each panel has SIV's own sub-buttons (Processors: Features, Hypervisor, Groups, CPU-n, Cache-n;
  Sensors: Zero, Status, Cooling, Power, Hide; Adapters: Counters, ARP, Routing, TCP V4/V6, Wi-Fi,
  Bluetooth, DNS, Ping; PCI Bus: Properties, PnP Dev, PCI Dev, UAA Dev, ALL Dev; ...).
- **Menu** (SIV's tray menu) lists every item by group, plus the tools, Save Report, Save All Items,
  Run as root and Exit. Right-click any panel for the same menu.
- **Tools:**
  - **CPU Burnin / CPU Stress** - per-CPU worker threads (Same CPU: Whetstone rating per CPU;
    Swap CPU: affinity test), affinity/APIC check, burn/cool cycles, current / average / rolling /
    maximum CPU load, clock, core temperature, board (Super-I/O) temperature and GPU load,
    temperature and clock, plots, Pass/Fail. Asks before starting, stops at a temperature limit
    (default 90 C) and a time cap.
  - **Benchmark** - Dhrystone 2.1 (DMIPS) and Whetstone (MWIPS) in SIV's modes (all CPUs
    concurrently or one at a time, D+D/W+W and D+W/W+D per core, physical / logical threads).
  - **IPI Delay** - core-to-core latency matrix with a heat map. **Latency** - memory latency by
    block size with a graph.
  - **Device ID Lookup** (Tree / Scan / Info modes over hwdata `pci.ids` / `usb.ids` / systemd-hwdb)
    and **Find** (regex over a `srs-sysreport` folder, with highlighting and F3 navigation).
- Live sections (sensors, clocks, counters, GPU, disk performance) refresh on a timer.
- Hot-keys: Ctrl+F Find, Ctrl+L Lookup, Ctrl+D ALL Dev, Ctrl+M Menu, Ctrl+S Save, Ctrl+H Highlight,
  F3 / Shift+F3, F5 Refresh, F1 Help, Esc closes a panel.

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
own rate; the main window every 2 s), 0.5, 1, 2, 3, 5 or 10 seconds, or *Paused* (press F5 to
refresh a panel by hand). Applies at once to every open window and is saved as `Update Interval=`
(milliseconds; 0 = default, -1 = paused) in `~/.config/srs/SRSDEF.ini`. CPU Burnin / Stress always
updates its status every second.

## Dark mode

**Menu -> Options -> Theme**: *Light (SIV classic)* (default), *Dark*, or *Follow desktop*. Dark
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

## Command line

    srs-gui -save                          SIV's default item set to the console
    srs-gui -save=FILE                     ... to a file
    srs-gui '-save=[cpuid][sensors]=FILE'  chosen items (SIV syntax); [all] = every item
    srs-gui --list                         every item: id, category, bucket, group, title
    srs-gui id 1002:7550 | find PATTERN | devices
    srs-gui burn --threads N --seconds S [--mode same|swap] [--limit C]
    srs-gui bench ac|as|dd|dw|lc|lp|pc|pp [--seconds S]     srs-gui ipi     srs-gui latency [--max MB]

## Safety

Read-only by design: nothing is written to sysfs, devices or firmware. No i2c/SMBus/DDC access,
no hidraw, no SMU commands, no network traffic apart from `ping` to the default gateway in the
Ping item. Burner and benchmarks never start on their own (not from `-save` either). Root-only
data comes only from `srs-root`'s cache in `~/.cache/srs/root`.

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
| `gui.c`, `plat_win32.c`, `srs.rc` | optional Windows build (`SRS64X.exe` / `SRS32X.exe`, search features only) |

## Licence

SRS is free software under the **GNU General Public License v2.0 or later** ([LICENSE](LICENSE)).
Device names come from the hwdata project's `pci.ids` / `usb.ids` / `pnp.ids`, which are not
part of SRS.
