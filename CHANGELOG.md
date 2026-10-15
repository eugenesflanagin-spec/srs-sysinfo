# Changelog

SRS is a clean-room implementation of SIV64X (System Information Viewer) for Linux.
SIV is by Ray Hinchliffe, RH Software (https://rh-software.com); SRS is not affiliated with RH Software.

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
