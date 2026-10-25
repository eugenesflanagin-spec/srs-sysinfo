# Changelog

SRS is a Linux system information viewer inspired by SIV64X (System Information Viewer), built to improve on it
and shared freely with the Linux and open-source community.
SIV is by Ray Hinchliffe, RH Software (https://rh-software.com); SRS is not affiliated with RH Software.

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
