# Changelog

SRS is a Linux system information viewer inspired by SIV64X (System Information Viewer), built to improve on it
and shared freely with the Linux and open-source community.
SIV is by Ray Hinchliffe, RH Software (https://rh-software.com); SRS is not affiliated with RH Software.

## 2.0.0 - 2026-10-26

This is SRS 2.0, with a new main window: an Overview that answers "Is my PC OK?", one page per
topic and a search box. The classic SIV-style windows are still there, one click away.

### Added
- **A new main window: easier to find your way, nothing taken away.** One window with a sidebar of
  pages: **Overview**, Processor, Memory, Motherboard & BIOS, Graphics & audio, Storage, Network,
  Sensors, USB & PCI devices, Operating system, Benchmarks & stress, and Tools. It follows the
  desktop's light or dark theme.
  - **Overview** answers "Is my PC OK?" with a status light per check: CPU and graphics
    temperature, drive health (SMART) and drive temperature, hardware errors (machine checks, ECC
    memory errors, faulty memory pages), free space on the system disk, memory in use, sensor alarm
    flags, and CPU throttling on CPUs that count it. Each line says what it means in plain words;
    hover over it for the limits, press **Details** for the readings behind it. Below are cards
    for the processor, graphics, memory, storage, operating system and motherboard with the
    numbers people look for, each with a button to its page.
  - **Each page** starts with a short explanation and the key facts as tiles, then the same panels
    as before as tabs, with the same text, colours and live updates. Every tab has Refresh, Copy,
    Guide (F1), a Live switch where the item updates by itself, and **Open in window** for the
    classic panel. **All N items** lists every item of the page, the expert ones too, and opens
    them as extra tabs (Processor also has each CPU and its caches).
  - **Search** (Ctrl+F) in the title bar: type a name such as "temperature", "SMART" or "BIOS"
    and pick an item from the list; Enter finds the text on the current tab (F3 / Shift+F3 for the
    next / previous match). Item ids work too, with an argument: `processor:3`, `volumes:/`.
  - **Menu**: Save Report, Save All Items, Copy This Tab, Options, Run as root, Find in System
    Report, Device ID Lookup, Classic View, the Classic Menu (every item and tool, as before), User
    Guide, About, Quit.
  - Keys: Alt+1 ... Alt+0, Alt+- and Alt+= jump to a page, F5 reads the page again, Ctrl+W closes
    an extra tab, Ctrl+Shift+F opens Find in System Report. Every button has a plain-English
    tooltip and an underlined Alt key.
- **Classic view** keeps the SIV-style windows exactly as they were: **Menu -> Classic View**, the
  **Classic view** button under the sidebar, `srs-gui --gui-classic`, and the menu entry's
  **Classic View** action. **Options -> Start in classic view** makes it the default (saved as
  `Classic View=1` in `SRSDEF.ini`); `--gui-new` opens the new window anyway.
- **"Is my PC OK?" uses each sensor's own temperature limits**: the CPU's maximum (Intel's own
  report, or AMD's specification for desktop Ryzen models), the graphics card's critical
  hotspot temperature, and each drive's own warning temperature. General limits only where none
  is known.

### Changed
- **About SIV** is now called **About SRS** (ID still `about`).
- **About** now starts with who made SRS, its licence and its home page: "Created by Eugene
  Flanagin", "Licensed under GPL-2.0-or-later", https://github.com/eugenesflanagin-spec/srs-sysinfo.
- The **Windows (OS) panel** is now called **General Info**, in the new window, the classic menu and
  reports. Its ID is still `windows`, so `--gui-open=windows`, `-save` reports and saved links keep working.
- Without a `Theme=` setting the new window follows the desktop's light / dark setting; the classic
  view still starts light. A saved Theme is used as before.
- In the new window the panel text is 9 pt instead of 8 pt (the classic panels keep 8 pt).
- `--gui-open=ITEM`, the tray menu's Sensors / Processors and a double-clicked device ID open the
  item on its page in the new window, and as a panel in the classic view.
- The command line, `-save` reports and their format are unchanged.
- **User guide and Section Reference updated for the new window**: a new tour of the main window
  (section 6) with screenshots of the Overview (light and dark), the Processor and Sensors pages
  and the search box; every tool, troubleshooting row and FAQ answer says where to find it in the
  new window and in the classic view; each group of the Section Reference names its page.

## 1.1.4 - 2026-10-25

### Changed
- **"Redact identifiers" now also covers copied text.** With **Options -> Redact identifiers in saved
  reports and copied text** on, the Copy button and right-click Copy / Ctrl+C mask serial numbers,
  UUIDs, MAC addresses (vendor half kept), boot / machine IDs and the host name, the same way Save
  Report does. Before, only saved reports were masked. The panels still show everything.

## 1.1.3 - 2026-10-25

### Changed
- **User guide: example screenshots and example values use a generic computer name (MYPC).**
  Seven screenshots (main window, Machine, CPUs, PCI Bus, USB Bus, Sensor Status, CPU Benchmarks)
  and two "typical values" examples (Graphics, Linux OS) now show MYPC as the computer name.

## 1.1.2 - 2026-10-25

### Added
- **User guide: "Reading the CPU IPI Delay table - a worked example"** (section 9.2.2). A real
  16 x 16 result from one Ryzen 7 7800X3D (one CCD, 100,000 rounds per pair), step by step: row =
  from, column = to, the `-` diagonal; partner threads (CPU n and n+8) are the fastest pair in each
  row at 23-25 ns, other pairs 27-34 ns through the shared L3, the farthest pairs 33-34 ns; why
  one-CCD CPUs give an even table and two-CCD CPUs typically show a second block around 70-80 ns;
  what to use it for, and that Curve Optimizer / voltage changes do not change it. Linked from the
  CPU IPI Delay item, so Guide (F1) on the IPI Delay window leads there.

## 1.1.1 - 2026-10-25

### Fixed
- **CPU Burnin / CPU Stress: GPU Frequency "Rolling Average" always showed 0 MHz.** The GPU clock was
  never added to the rolling average; the other six rows were not affected.

### Added
- **Burn-in results are saved.** Each finished CPU Burnin or CPU Stress run (Pass, Fail or Stopped,
  also when the window is closed during a run) is saved to `~/.cache/srs/burnin-YYYYMMDD-HHMMSS.txt`:
  the same text as the window's Copy button (status, sensor table, thread table). The status line
  names the file. `sysreport-search burn` saves its summary the same way and prints the path.
  One file per run, so runs can be compared (`diff`, `meld`).
- **User guide: "What hyperthreading (SMT) really does - a worked example"** (section 9.2.1). Real
  benchmark results from one Ryzen 7 7800X3D, step by step: the two threads of a core are equally
  fast, all-core runs score about 5 % lower because of boost, a second thread adds 66-80 %, mixed
  integer + floating-point work shares a core better than identical work, and why one core can score
  a little lower than the rest.

### Changed
- `--version` shows the project's home page.
- The generic Linux tarball is made from the Ubuntu 24.04 package, so it runs on glibc 2.38 and
  newer (1.1.0's needed glibc 2.42).

## 1.1.0 - 2026-10-25

### Added
- **A full user guide and section-by-section reference, also as a PDF.** The guide explains every
  window, option and command, and every one of the 545 items in plain words (what you see, normal
  and unusual values, where Linux gets it). Installed in `/usr/share/doc/srs-sysinfo/`:
  `SRS-User-Guide.pdf`, an HTML copy in `html/`, and the Markdown sources. New menu entry
  **SRS User Guide (PDF)**.
- **Guide links everywhere.** Every panel has a **Guide** button (and **F1**, and "Open guide for
  this item" in its right-click menu) that opens the guide at that item's section: in Okular,
  Evince or Papers at the right page, otherwise the HTML guide in your web browser. **Menu -> User
  Guide...** opens the start. Shift+F1 still opens the Help panel.
  - Command line: `sysreport-search guide ITEM` (`--print` shows the command instead of running it).
- **Redact identifiers in reports you share.** `sysreport-search -save ... --redact`, or **Menu ->
  Options -> Redact identifiers in saved reports** for Save Report / Save All Items. Serial
  numbers, UUIDs, WWNs, GPU and Thunderbolt unique IDs, boot and machine IDs and the host name
  become `XXXX` (same length, so columns stay lined up); MAC addresses keep their vendor half
  (`04:7c:16:XX:XX:XX`). The report header says `(identifiers redacted)`. Normal reports are
  unchanged and complete. IP addresses and Wi-Fi names are not masked.
- `--help` now lists every command: `-save`, `--list`, `guide`, `burn`, `bench`, `ipi`,
  `latency`, and srs-gui's `--gui-open` / `--gui-tray` / `SRS_OPEN`.

### Changed
- **No more notes written for the author's PC.** Several items printed sentences that were only
  true on the PC SRS was developed on. They now check your system:
  - GPU Fans names the fan-control program only if one is really running (LACT, CoolerControl,
    CoreCtrl, fancontrol), instead of always saying "owned by LACT".
  - GPU LEDs looks for LED devices the kernel exposes for your GPU, instead of naming one
    graphics card.
  - Thunderbolt lists the Thunderbolt / USB4 routers and domains the kernel found, and no longer
    names one motherboard's USB4 chip.
  - GPU I2C no longer says the SMU was hung "here".
  - Sensors: Super-I/O inputs are marked "probably not connected" when they read far above the
    board's own SYSTIN / CPUTIN temperatures, instead of from a list for one board model. A real
    probe on a temperature header is no longer greyed out.
  - Memory Latency labels blocks L1 / L2 / L3 / DRAM from your CPU's own cache sizes (they were
    fixed at 32 KB / 1 MB / 96 MB).
  - Benchmark modes `ec` / `ep` find efficiency cores on Intel hybrid CPUs, and `pc` / `pp` leave
    them out (they always said "not a hybrid processor"). The Processors page counts them too.
- `CHECKLIST.tsv` now matches the program: a new `sync_checklist.py` copies each item's root flag
  and data source from the code. DIMMs, Storage spaces, SIO debug, I/O GPIO and Fake MCHBAR no
  longer claim to need root.

### Fixed
- **SPD was empty on DDR5 PCs.** `decode-dimms` cannot read DDR5 SPD hubs and reports 0 DIMMs;
  SPD now shows the SMBIOS memory summary in that case.
- **Double-clicking a USB ID opened a PCI lookup.** USB is now recognised from the item, the
  line, `VID_xxxx PID_xxxx` pairs and HID names (`0003:046D:C52B.0001`); PCI slot addresses are
  no longer taken for IDs.
- AMD SMU / CPU SMU: the SMU firmware version was always `-` (it is in the driver's `version`
  file). Fake PM tables showed the binary table version and size as garbage. AMD PM Table now
  shows the table layout; AMD PM Table 0 keeps the values.
- Groups: with more than 256 groups the current user's groups were not found (showed `(-1)`).
- ACPI tables: the FACS table was read as if it had a standard header (revision 137, OEM `$X`);
  `acpi-tables:FACS` now lists its real fields.
- Device names: 8-character ACPI IDs were looked up as 3-letter PNP vendors (`AMDI0015` showed
  "Amdek", `ACPI000C` "Aspen Tech"); they now use ACPI vendor IDs, and unregistered PNP prefixes
  are shown as such.
- SIV setup listed only 4 of the 12 settings.
- RAID items counted a JMicron USB-NVMe enclosure as a RAID controller; SMART counted skipped
  USB drives as cached; `ntfs:<mountpoint>` was titled "NTFS" for btrfs; Volumes, Drive Geometry
  and Environment had no title line.
- Desktop PCs showed "Battery Present" / an empty "Battery %" because of a mouse or game
  controller battery; peripheral batteries no longer count.
- Network: adapters in state "unknown" without a link no longer count as up. DNS cache no longer
  calls dnsmasq a libvirt helper when `/etc/resolv.conf` uses it as the system cache. mDNS names
  (`\032`) and mount paths (`\040`) are decoded.
- CPU timer shows the real kernel tick rate (`CONFIG_HZ`) next to `USER_HZ`; Processors shows each
  CPU's NUMA node and a "Sub-ID" column header; family / model print as `0x` hex.
- Benchmarks: changing the seconds per measurement now takes effect in the same session; the GUI
  allows the same ranges as the command line (60 s per measurement, latency up to 4096 MB);
  `bench --seconds` error text matches the accepted range.
- `find --report DIR` with a missing folder names that folder; udev "priority" label no longer cut;
  "1 port" singular; file locks show full paths.

## 1.0.8 - 2026-10-24

### Added
- **Menu -> Options -> Refresh root data at boot.** Root-only panels (SMBIOS serials, DIMM SPD,
  SMART, ACPI tables, MSRs, full PCI config) are filled at every boot without a password prompt.
  - New systemd unit `srs-root@.service` (in `/usr/lib/systemd/system`). Enabled per user as
    `srs-root@<user>.service`, it runs the existing read-only `srs-root` collector once per boot
    as root, at Nice 19 and idle I/O priority, and hands `~/.cache/srs/root` to that user.
  - Ticking the option asks for your password once (pkexec) and collects straight away;
    unticking disables the unit. A cancelled prompt leaves the tick as it was.
  - SRS itself keeps running as your user; only the collector runs as root.
- `srs-root --user NAME` (root only) collects for that user; the boot unit uses it.

## 1.0.7 - 2026-10-24

### Fixed
- **No more flicker on live updates** (Sensors, the main window, clocks, CPU Burnin / Stress…).
  Every refresh used to clear the whole text view and rebuild it, so GTK re-laid out and repainted
  everything and the scroll position had to be put back afterwards.
  - Each refresh is now drawn into an off-screen copy that shares the window's styles.
  - Just before GTK paints, only the lines whose text changed are replaced (text and colours);
    unchanged lines are never touched, and an unchanged panel isn't redrawn at all.
  - The scroll position no longer jumps, since nothing above it is rebuilt.
  - Less work per refresh: with the Sensors panel open, SRS went from about 4% to 2% CPU.
  - One-off output (Find, Device ID Lookup, benchmark results) still draws directly.

### Added
- **Menu -> Options -> Update interval**: Default (each panel's own rate), 0.5 / 1 / 2 / 3 / 5 /
  10 seconds, or Paused (F5 refreshes by hand). Applies to every open window at once; saved as
  `Update Interval=` in SRSDEF.ini.

## 1.0.6 - 2026-10-16

### Fixed
- **Memory Latency: small blocks no longer show false spikes.** 1.0.5 timed each block size once,
  for about 2 ms at L1 sizes, so a single interrupt or clock change could make a block read
  several times too slow (e.g. 8 KB = 4.54 ns on a 7800X3D whose L1 is ~1 ns).
  - Each block size is now timed 5 times and the fastest pass is kept. Interruptions only ever
    make a pass slower, so the fastest pass is the true latency.
  - A ~200 ms warm-up runs first, so the CPU is at full clock before the first block.
  - Each block is walked once in full before timing, so the cache and TLB are filled for every
    size, not just the first 10,000 lines.
  - Result on a 7800X3D, 3 runs each: L1 4-32 KB 0.95-1.09 ns (was 1.02-1.27 with outliers);
    64 MB, which still fits in the 96 MB L3, 11.1-11.6 ns (was 13.8-19.2).
- **aarch64 build:** `burner.c` included `<errno.h>` and `<fcntl.h>` only on x86, so the
  GPU Load command code added in 1.0.5 failed to compile on aarch64.
- Help and error text now name `srs-sysreport` instead of the old `sysreport.sh`.

## 1.0.5 - 2026-10-15

### Added
- **CPU Burnin / Stress: GPU Load command diagnostics** - a wrong or misspelled command no longer
  fails silently.
  - Before starting, SRS checks the command's program exists (PATH or full path). If not, an
    error dialog names it and nothing is started.
  - New "GPU Load" status line while running: running (pid), finished (exit 0) after N s - the
    CPU run continues, failed (exit N), not found (exit 127), not executable (exit 126), killed by
    signal N, or stopped with the CPU load. Failures show in red.
  - The command's own output (stdout + stderr) is saved to `~/.cache/srs/gpu-load.log`, so its
    error messages are kept.

## 1.0.4 - 2026-10-15

### Added
- **Dark mode.** Menu -> Options -> Theme (also in the tray's SRS Menu):
  - *Light (SIV classic)* - default, unchanged SIV look.
  - *Dark* - dark panels, light text, SIV's blue / purple / teal / orange / green / red palette
    brightened for a dark background; dark buttons, inputs, menus (Adwaita dark) and graphs
    (Burnin / Stress history, Latency curve).
  - *Follow desktop* - uses the desktop colour scheme from xdg-desktop-portal
    (`org.freedesktop.appearance color-scheme`; KDE Plasma and GNOME set it) and switches live when
    the desktop scheme changes. Falls back to `GTK_THEME` / `~/.config/gtk-3.0/settings.ini`.
- Switching applies immediately to every open window; saved as `Theme=Light|Dark|System` in
  `~/.config/srs/SRSDEF.ini`. Find highlights keep dark text in dark mode so they stay readable.

### Changed
- srs-gui no longer forces `GTK_THEME=Adwaita:light`; the Theme option drives Adwaita light / dark
  for the SRS process itself (a dark `GTK_THEME` such as adw-gtk3-dark no longer half-themes it).

## 1.0.3 - 2026-10-15

### Added
- **System tray icon, like SIV's Icon Tray** (StatusNotifierItem via `libayatana-appindicator`,
  loaded at run time - optional; KDE Plasma, XFCE, GNOME with the AppIndicator extension).
  - Live tooltip: CPU temperature and load, GPU temperature and load (every 2 s).
  - Tray menu: Show SRS / Hide to Tray, Sensors, Processors, CPU Burnin, CPU Stress, Benchmark,
    Find, the full SRS Menu, Exit. Middle-click toggles show / hide.
- **Menu -> Options** (also in the tray's SRS Menu), saved in `~/.config/srs/SRSDEF.ini`:
  - System tray icon (default on)
  - Close button hides to tray (default off)
  - Minimize hides to tray (X11 / XWayland - native Wayland apps are not told when they are minimised)
  - Start hidden in tray
  - Start with the desktop - writes `~/.config/autostart/srs-gui.desktop` running `srs-gui --gui-tray`
  - Hide to Tray
- `srs-gui --gui-tray` starts hidden in the tray.
- Safety: SRS never hides its windows unless a tray icon actually exists, so it cannot get lost.

## 1.0.2 - 2026-10-15

### Changed
- Running as root (`sudo sysreport-search -save`) uses the invoking user's root-data cache
  (`SUDO_USER` / `PKEXEC_UID`), or collects one first with `srs-root` if none exists.
- `srs-root` skips SMART on USB-attached disks (set `SRS_SMART_USB=1` to include them) - SMART
  pass-through is meaningless on USB sticks and can upset flaky USB enclosures.

### Fixed
- SMBIOS structure count with dmidecode 3.6+ (it no longer prints the summary line).

## 1.0.1 - 2026-10-15

### Added
- Menu entries: **SRS CPU Burnin & Stress**, **SRS Benchmark**, **SRS Full System Report**, and
  jump-list actions on **SRS System Information** (Sensors, Processors, CPU Burnin, CPU Stress,
  Benchmark, Device ID Lookup, Find).
- `srs-gui --gui-open=ITEM[,ITEM...]` opens panels / tools at start.

### Fixed
- Clean builds under Arch's hardening flags (`_FORTIFY_SOURCE=3`, LTO): `realpath` buffers sized to
  `PATH_MAX`; uninitialised-pointer warnings.

## 1.0.0 - 2026-10-15

First release.
- All **538** SIV64X 5.87 report items (364 with Linux data, 136 "not present", 38 Windows-only
  with Linux equivalents) - see `CHECKLIST.tsv`.
- GTK3 GUI laid out like SIV: main window and button row, every panel's sub-buttons, Menu.
- SIV-format `-save` reports; device-ID lookup (Tree / Scan / Info); regex Find over system reports.
- CPU Burnin / CPU Stress, Dhrystone 2.1 / Whetstone benchmarks in SIV's modes, IPI delay and
  memory latency tools.
- `srs-root` helper for root-only data (dmidecode, SPD, SMART, ACPI tables, MSRs, PCI config space).
