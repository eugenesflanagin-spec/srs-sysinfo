# SRS (SIV items)  (SRS Section Reference)

[Back to the section index](README.md) - [Glossary](../GLOSSARY.md)

**Where to find these items (SRS 2.0).** In the main window they are on the **Tools** page (Alt+Backspace). Its tabs are About SRS and Help; **All N items** lists the rest. **Menu -> About SRS** opens About too. You can also type an item's name or id into the search box (Ctrl+F). The rest of this introduction describes the classic view (Menu -> Classic View), with its Menu and panel buttons.

This group holds the items that describe SRS itself rather than your hardware: its version and
credits, help text, settings file, command-line options, program image, threads, and a set of
small "buffer" snapshots of live system data. In SIV64X (System Information Viewer, by Ray
Hinchliffe, RH Software) these items describe SIV's own program, driver and internal buffers. SRS
keeps the same item names so a save file lines up with SIV's, and fills each one with the nearest
Linux data. Open them from **Menu -> SRS (SIV items) -> item**. The About and Help panels have
their own button row (DNS Names, Lookup, USB Dev, About, Help), and the SRS DLLs panel has the
Lookup row (DNS Names, Signature, SRS DLLs, Lookup, ID Lookup). Almost everything here works as a
normal user. Only the board serial number in [SGB info](#sgb-info-systemboard-buffer) and the SPD
cache line in [Fake SPDs](#fake-spds) use data from the root helper (srs-root); start it with
**Menu -> Run as root**.

## Contents
- [About SRS](#about-srs) `about`
- [BSB info (battery/HID sensor buffer)](#bsb-info-batteryhid-sensor-buffer) `bsi`
- [CPU group buffer](#cpu-group-buffer) `cgb`
- [CSV logging](#csv-logging) `csv`
- [CSV titles](#csv-titles) `csv-titles`
- [Debug API](#debug-api) `debug-api`
- [Delta/cost settings](#deltacost-settings) `delta`
- [DSB info (DIMM sensor buffer)](#dsb-info-dimm-sensor-buffer) `dsb`
- [Emoji glyph test](#emoji-glyph-test) `emoji`
- [Fake IMCs](#fake-imcs) `fak-imcs`
- [Fake MCHBAR](#fake-mchbar) `fak-mchbar`
- [Fake PM tables](#fake-pm-tables) `fak-pmts`
- [Fake SPDs](#fake-spds) `fak-spds`
- [Filter](#filter) `filter`
- [Final processing](#final-processing) `final`
- [GPU info buffer](#gpu-info-buffer) `gpu`
- [Help](#help) `help`
- [Highlight setup](#highlight-setup) `highlight`
- [Hotkeys](#hotkeys) `hot-keys`
- [SIV image (EXE)](#siv-image-exe) `image-exe`
- [SIV image (SYS driver)](#siv-image-sys-driver) `image-sys`
- [Image tree](#image-tree) `image-tree`
- [Initial processing](#initial-processing) `initial`
- [Install](#install) `install`
- [SIV languages](#siv-languages) `languages`
- [Lock setup](#lock-setup) `lck-setup`
- [Maker icons](#maker-icons) `makers`
- [SIV manifest](#siv-manifest) `manifest`
- [Mapping cache](#mapping-cache) `map-cache`
- [Map/unmap test](#mapunmap-test) `map-unmap`
- [Menus](#menus) `menus`
- [Mutex info](#mutex-info) `mutex`
- [PMB info (power monitor buffer)](#pmb-info-power-monitor-buffer) `pmb`
- [Colour RGB debug](#colour-rgb-debug) `rgb-debug`
- [SDM info (storage device monitor)](#sdm-info-storage-device-monitor) `sdm`
- [Sequence setup](#sequence-setup) `sequence`
- [SGB end](#sgb-end) `sgb-end`
- [SGB info (system/board buffer)](#sgb-info-systemboard-buffer) `sgb-ini`
- [Active SIV instances](#active-siv-instances) `siv-active`
- [SIV autorun](#siv-autorun) `siv-autorun`
- [SIV DLLs](#siv-dlls) `siv-dll`
- [SIV driver](#siv-driver) `siv-drv`
- [SIVDEF.ini](#sivdefini) `siv-ini`
- [SIV location / window poke](#siv-location--window-poke) `siv-location`
- [SIV qualifiers](#siv-qualifiers) `siv-qualifiers`
- [SIV qualifier list](#siv-qualifier-list) `siv-quals`
- [SIV setup](#siv-setup) `siv-setup`
- [SIV sizes](#siv-sizes) `siv-sizes`
- [SIV driver service](#siv-driver-service) `siv-srv`
- [SIV usage history](#siv-usage-history) `siv-usage`
- [SIV use / update](#siv-use--update) `siv-use`
- [SMB end](#smb-end) `smb-end`
- [SMB info](#smb-info) `smb-ini`
- [Lock states](#lock-states) `states`
- [STC info (thread contexts)](#stc-info-thread-contexts) `stc`
- [Tune system](#tune-system) `tune`
- [Tuning](#tuning) `tuning`
- [TZM info (thermal zone buffer)](#tzm-info-thermal-zone-buffer) `tzm`
- [Words](#words) `words`
- [XXB internal buffers](#xxb-internal-buffers) `xxb`

### How to read these items

- Most items start with a title line: `srs-gui - <what> on \\<host> - <OS>`. On the author's PC
  that reads `srs-gui - Help on \\MYPC - CachyOS 7.3.0-273-tkg-bore-llvm`.
- `[=]` is a ticked box and `[_]` an empty one. A few items use `|X|` and `|_|` the same way.
- SRS is read-only. None of these items change a setting, write a register or open a device.
- SRS is one program image. Run with no arguments it is the window (srs-gui). Run with arguments
  it is the command line (sysreport-search). So "this process" in the items below is whichever one
  made the report.

---
## About SRS  
`about` - Linux data

**In plain words.** Who made this program, which version you have, what it is running on, and
which name-database files it uses.

**What you see.**

| Field | What it means | Why it matters |
|---|---|---|
| Title line | `srs-gui - System Report Search V<version> <host>::<user>` | Quick check of version and machine. |
| Program | One image works as the GUI (no arguments) and the CLI (with arguments). | Explains why reports name `sysreport-search`. |
| Version | Program version, e.g. `V2.2.0`. | Quote this in bug reports. |
| Program Build | File format, compiler and C runtime the program was built with. | Tells a self-built copy from a packaged one. |
| Built | Date and time the program was compiled. | Shows how old your copy is. |
| Image | Full path and size of the running program file. | Shows which copy ran, if you have several. |
| Running on | Kernel name, release, build string and machine type (from `uname`). | The exact kernel the data came from. |
| Operating System | Distribution name and kernel. | Context for every other item. |
| Machine | Board maker, model and BIOS version/date. | Context; BIOS updates change many values. |
| Processor | CPU brand string. | Context. |
| PCI Device Names / USB Device Names / PnP / Monitor Names | Path of each name database, then its date and version (from the file header) or the file date. | Old files mean new devices show as "unknown". |
| Settings | Path of the settings file SRSDEF.ini. | Where your choices are kept. |
| Live data | The kinds of source SRS reads. | Reminds you some data needs the root helper. |
| Created by / Licensed under / Home page | The author (Eugene Flanagin), the licence (GPL-2.0-or-later) and the GitHub page. | Who made it and where to get it. |
| Why SRS / Credit / Disclaimer | Origin, credit to SIV's author and use-at-your-own-risk note. | Credit information. |

**Healthy / typical values.** On the author's PC: `V2.2.0`, built with gcc 16.2.1 and glibc,
image `/usr/bin/sysreport-search (1.38MB)`, Linux 7.3.0 x86_64 on an ASUS TUF GAMING X870-PRO
WIFI7 W NEO (BIOS 1805), AMD Ryzen 7 7800X3D. pci.ids version 2026.10.01, usb.ids 2026.06.26.
pnp.ids has no version header, so only its file date is shown.

**Unusual values and what they suggest.**
- A database line says `not found`: the hwdata package is missing, so device names cannot be resolved.
- A database date that is a year or more old: update your distribution's hwdata package.

**Where Linux gets it.** Built-in version strings, `uname()`, `/etc/os-release`, `/sys/class/dmi/id`,
and the header lines of `/usr/share/hwdata/*.ids`.

**Background.** The `.ids` files are community lists that turn numeric device IDs (like `1002:7550`)
into names (like an AMD Radeon card). SRS does not ship its own copy; it uses the system's.

---
## BSB info (battery/HID sensor buffer)  
`bsi` - Linux data

**In plain words.** A list of the batteries and power supplies Linux knows about, plus a count of
HID (Human Interface Device: keyboards, mice, game pads) devices.

**What you see.** One row per entry in `/sys/class/power_supply`:

| Column | What it means | Why it matters |
|---|---|---|
| # | Row number. | - |
| Device Name | Kernel name of the supply, e.g. `BAT0`, `AC`, or a wireless controller battery. | Tells you which device it is. |
| Type | `Battery`, `Mains`, `USB` and so on. | Mains = wall power; Battery = something that drains. |
| Status | `Charging`, `Discharging`, `Full`, `Not charging`. | Shows if the battery is being charged. |
| Capacity | Charge left, in percent. | Low values mean recharge soon. |
| Model | Model name if the driver reports one. | Identifies the battery pack. |

Below the rows: `act N num N` (how many supplies were found) and a count of HID devices
registered in `/sys/bus/hid/devices`. SRS only counts them; it never opens them.
This panel refreshes every 2 seconds.

**Healthy / typical values.** A desktop often has no battery at all, or only the batteries of
wireless game pads and mice. On the author's PC: one entry, a Nintendo Switch controller battery,
type Battery, status Discharging; 15 HID devices. A laptop shows `BAT0` (Battery) and `AC` or `ADP1` (Mains).

**Unusual values and what they suggest.**
- Capacity or Model blank: the driver does not report it. Common for game pads.
- A laptop with no `BAT0`: battery not detected, or the ACPI battery driver is not loaded.
- `Not charging` at less than 100% on a laptop: often a charge limit set in firmware or by a vendor tool.

**Where Linux gets it.** `/sys/class/power_supply/*/{type,status,capacity,model_name}` and the
entries in `/sys/bus/hid/devices`.

---
## CPU group buffer  
`cgb` - Linux data

**In plain words.** Shows that all your CPUs belong to one group, and which CPUs are online.

**What you see.**

| Column | What it means | Why it matters |
|---|---|---|
| CGN | CPU group number (always 0 on Linux), with a tick box. | Windows splits more than 64 CPUs into groups; Linux does not. |
| Num | How many logical CPUs are online. | Should match your thread count. |
| Affinity | A 64-bit hex mask, one bit per online CPU. | `FFFF` = CPUs 0-15. |
| Members | `[CPU-0 to CPU-N]`, the highest online CPU number. | Quick range check. |
| Online / Possible | The kernel's online list and possible list. | Different lists mean some CPUs are switched off. |

**Healthy / typical values.** On the author's 7800X3D (8 cores, 16 threads): `0  16
000000000000FFFF  [CPU-0 to CPU-15]  0-15 / 0-15`.

**Unusual values and what they suggest.**
- Online smaller than Possible: CPUs were taken offline (for example with `nosmt`, `maxcpus=` on
  the kernel command line, or by writing 0 to a CPU's `online` file).
- Possible much bigger than your real CPU count: firmware reports spare hot-plug slots. Harmless.

**Where Linux gets it.** `/sys/devices/system/cpu/online`, `/sys/devices/system/cpu/possible` and
`/sys/devices/system/cpu/cpuN/online`.

**Background.** A *logical CPU* is one hardware thread. With SMT (Simultaneous Multi-Threading)
each core runs two, so 8 cores show as 16 CPUs. An *affinity mask* is a bit pattern saying which
CPUs a task may run on.

---
## CSV logging  
`csv` - Linux data

**In plain words.** SIV can log sensor data to a CSV (Comma-Separated Values) file. SRS has no
separate CSV logger; this item tells you what to use instead.

**What you see.** Fixed lines: CSV logging (not separate), Save (the `-save` syntax), Example
(`srs-gui '-save=[sensors][cpu]=/tmp/log.txt'`), Tabular data (columns are padded with spaces at
the panel tab stops), and Environmental log (SIV's `-ENVLOG` / `-EXCEL` sensor log has no
equivalent; run `-save` on `[sensors]` instead).

**Healthy / typical values.** Always the same text.

**Where Linux gets it.** Built-in text.

**Background.** To log sensors over time, run a `-save` of `[sensors]` on a timer (for example
from a shell loop or a systemd timer) and keep each file.

---
## CSV titles  
`csv-titles` - Linux data

**In plain words.** Describes the layout of a save file, since there is no CSV header to show.

**What you see.**
- File header - `srs-gui - System Report Search V<version> <host> <YYYY-MM-DD HH:MM:SS>  [<items>]`.
- Section header - `[<item>]  [<category>]`, e.g. `[gpu]  [xxb]`.
- Section body - the item's own column titles, then its rows.
- Separator - two blank lines after each section.

**Healthy / typical values.** Fixed text. Useful if you write a script that splits a save file into items.

**Where Linux gets it.** Built-in text.

---
## Debug API  
`debug-api` - Linux data

**In plain words.** Build facts and pointers for people who debug SRS.

**What you see.**
- Build - compiler and runtime, same as in About.
- Assertions - `on` or `off (NDEBUG)`. Assertions are self-checks compiled into the program.
- Item list - the command `srs-gui --list` prints every item.
- Dump any item - `srs-gui -save=[item]=STDOUT` prints one item.
- Process state - where to look: `[stc]` threads, `[mutex]` locks, `[siv-dll]` modules.

**Healthy / typical values.** On the author's PC: Assertions `on`.

**Where Linux gets it.** Compile-time build information.

---
## Delta/cost settings  
`delta` - Linux data

**In plain words.** SIV can turn power use into an energy cost. SRS has no such setting.

**What you see.** Energy cost (not used) and Nearest (live power is in [PMB info](#pmb-info-power-monitor-buffer)
and the sensor items).

**Where Linux gets it.** Built-in text.

---
## DSB info (DIMM sensor buffer)  
`dsb` - Linux data

**In plain words.** The temperature of each memory stick (DIMM) that has a sensor.

**What you see.** One row per DIMM sensor:

| Column | What it means | Why it matters |
|---|---|---|
| # | Row number. | - |
| hwmon | The hwmon device name, e.g. `hwmon7`. | Lets you find the raw files. |
| Device | The I2C (Inter-Integrated Circuit) bus and address, e.g. `1-0051` = bus 1, address 0x51. | Address 0x50-0x57 maps to a slot. |
| Current | Temperature now, in degrees C. | The main value. |
| Maximum | The "high" limit set in the sensor. | Above this the stick may slow itself down. |
| Critical | The "critical" limit. | Above this is a real problem. |

If no sensor is found you see `no DIMM temperature sensors (spd5118 / jc42 hwmon) loaded`.
This panel refreshes every 2 seconds.

**Healthy / typical values.** On the author's two DDR5 sticks: `31.00 C` and `30.50 C` at idle,
limits 55 C (max) and 85 C (critical). Idle 30-45 C is normal; 50-60 C under heavy load is common
with no airflow over the RAM.

**Unusual values and what they suggest.**
- Above 55 C at idle: poor case airflow, or a hot GPU blowing on the RAM.
- Above 70-80 C: DDR5 starts to slow itself (it refreshes more often) and errors become more likely. Improve cooling.
- One stick much hotter than the other: it sits closer to a heat source, or its sensor is off.
- No rows on DDR5: load the `spd5118` driver (`modprobe spd5118`). DDR4 sticks with a sensor use `jc42`; many DDR4 sticks have none.

**Where Linux gets it.** `/sys/class/hwmon/hwmonN/temp1_{input,max,crit}` for hwmon devices named
`spd5118` (DDR5) or `jc42` (DDR4 and older).

**Background.** *hwmon* (hardware monitoring) is the Linux system that exposes sensor chips as
files. Each DDR5 stick has a small hub chip on the SMBus (System Management Bus) that holds the
SPD (Serial Presence Detect: the stick's own description of its size and speeds) and a temperature sensor.

---
## Emoji glyph test  
`emoji` - Linux data

**In plain words.** Checks that your fonts can draw symbols and emoji, so the display looks right.

**What you see.**
- Glyphs - a row of sample symbols (smiley, monitor, thermometer, lightning, battery, floppy disk,
  tick, cross, `°C`, `µs`, arrow).
- Boxes - one ticked and one empty tick box.
- Fonts with emoji coverage - every font that has the smiley emoji (U+1F600), from fontconfig.

**Healthy / typical values.** All glyphs drawn, and at least one colour emoji font in the list.
The author's PC lists Noto Color Emoji, Twemoji, Segoe UI Emoji, DejaVu Sans and others.

**Unusual values and what they suggest.** Empty boxes or question marks instead of glyphs, or
`none found`: install an emoji font such as Noto Color Emoji.

**Where Linux gets it.** `fc-list :charset=1f600 family`.

---
## Fake IMCs  
`fak-imcs` - Not present on most PCs

**In plain words.** In SIV this replays saved memory-controller data from Intel Xeon servers, for
testing. SRS has no replay mode, so this always says "Not present".

**What you see.** Status (`Not present on this system`), Checked (what it looked for: Intel Xeon
ICX IMC fixtures) and your Processor name.

**Background.** An IMC (Integrated Memory Controller) is the part of the CPU that talks to RAM.
"ICX" is Intel's Ice Lake server family. A "fixture" is a saved data dump used to test a program
without the real hardware.

---
## Fake MCHBAR  
`fak-mchbar` - Linux data - Needs root data

**In plain words.** In SIV this replays saved Intel memory-controller register dumps, for testing.
SRS reads no such registers; this item tells you where memory timing data comes from instead.

**What you see.** Status (what the SIV feature is), srs-gui (reads no MCHBAR registers, never
opens `/dev/mem`, uses no replay files) and Memory timings (come from the [spd] item via the
srs-root cache file `spd.txt`, and from the kernel EDAC / amd64 drivers).

**Where Linux gets it.** Nothing is read. The memory timing data it points to needs the root
helper (**Menu -> Run as root**).

**Background.** MCHBAR (Memory Controller Hub Base Address Register) is the address of a block
of Intel memory-controller registers. Reading it needs raw physical memory access, which SRS avoids.

---
## Fake PM tables  
`fak-pmts` - Linux data

**In plain words.** SIV can replay saved AMD power-management tables. SRS has no replay; it
tells you if the live table is available.

**What you see.**
- Status - no replay; the live PM table is in the `[amd-pmt-0]` item.
- pm_table - `present` if the ryzen_smu driver exposes the table, else `ryzen_smu driver not loaded`.
- pm_table_version, pm_table_size - the driver's table version and size files.
- SMU firmware - the firmware version of the SMU, e.g. `4.84.4.0` on the author's PC.

**Unusual values and what they suggest.** `ryzen_smu driver not loaded`: install and load the
out-of-tree `ryzen_smu` module if you want PM table data. Intel PCs never have it.

**Where Linux gets it.** `/sys/kernel/ryzen_smu_drv/{pm_table,pm_table_version,pm_table_size,version}`.

**Background.** The SMU (System Management Unit) is a small controller inside AMD Ryzen chips
that runs clocks, voltages and power limits. Its PM (power management) table is a live block of
numbers: power, temperatures, clocks and limits.

---
## Fake SPDs  
`fak-spds` - Linux data

**In plain words.** SIV can replay saved memory-stick SPD dumps. SRS has no replay; it shows
where the real SPD data comes from.

**What you see.**
- Status - no replay; real SPD data is in the `[spd]` item.
- SPD cache - path of `spd.txt` in the root cache, or `Needs root: Menu > Run as root (srs-root)`.
- One line per DIMM sensor (`spd5118` or `jc42`), with its hwmon name and I2C address,
  e.g. `spd5118  hwmon7 at i2c 1-0051`.
- `no spd5118 / jc42 hwmon devices` if none are loaded.

**Healthy / typical values.** On the author's PC: cache present, two `spd5118` sensors at 1-0051 and 1-0053.

**Unusual values and what they suggest.** `Needs root`: run **Menu -> Run as root** once so the
`[spd]` item can show memory timings.

**Where Linux gets it.** `~/.cache/srs/root/spd.txt` (written by srs-root) and `/sys/class/hwmon`.

---
## Filter  
`filter` - Linux data

**In plain words.** Lists the ways you can narrow what SRS shows.

**What you see.**
- Find pattern - regular expression or literal text, Ignore case, Max matches.
- Lookup filter - PCI or USB bus; Tree, Scan or Info mode.
- Device panels - PCI Bus, USB Bus and ALL Dev pick the bus.
- -save filter - `[item][item]...` picks which items are written.
- Column filter - none; every column is always shown.

**Where Linux gets it.** Built-in text.

**Background.** A *regular expression* (regex) is a search pattern. For example `nvme[0-9]` finds
`nvme0`, `nvme1` and so on.

---
## Final processing  
`final` - Linux data

**In plain words.** How long SRS has been running and how much CPU time it used so far.

**What you see.**
- Elapsed - seconds since the process started.
- CPU time - CPU seconds the process has used.
- Sections - how many items SRS has.
- On exit - what happens at close (save settings in GUI mode, free tables, close panels).

**Healthy / typical values.** On the author's PC, during a full `[all]` save: Elapsed 3.140 s,
CPU time 0.160 s, 558 items. Elapsed is much larger than CPU time because most of the time is
spent waiting on files and helper tools, not computing.

**Unusual values and what they suggest.** In a full save, Elapsed far above 10-20 s means some
item waited a long time; often a slow or sleeping disk, or a network tool timing out.

**Where Linux gets it.** `/proc/uptime`, `/proc/self/stat` (start time) and the process CPU clock.

---
## GPU info buffer  
`gpu` - Linux data

**In plain words.** One line per graphics card: driver, PCI address, load, video memory, PCIe link and current clocks.

**What you see.**

| Column | What it means | Why it matters |
|---|---|---|
| GPU | DRM card name, e.g. `card0`. | Matches `/sys/class/drm/card0`. |
| Driver | Kernel driver, e.g. `amdgpu`, `i915`, `nvidia`. | Wrong or missing driver = no acceleration. |
| PCI ID / Slot | Vendor-device ID and PCI address, e.g. `1002-7550 0000:03:00.0`. | Identifies the exact chip. |
| Busy | GPU load in percent. | High at idle means something is rendering. |
| VRAM Used / Total | Video memory in use and installed. | Near full causes stutter in games. |
| Link | PCIe link width and speed now, e.g. `x16 32.0 GT/s PCIe`. | Lower than expected costs bandwidth. |
| Clocks (sclk / mclk) | Current core clock and memory clock (the starred level). | Low at idle, high under load. |

Columns the driver does not provide show `-`. Busy, VRAM and Clocks come from amdgpu files, so on
other drivers they are usually `-`. This panel refreshes every 2 seconds.

**Healthy / typical values.** On the author's Radeon RX 9070 XT: `card0 amdgpu 1002-7550
0000:03:00.0 7% 1.03GB / 15.92GB x16 32.0 GT/s PCIe 664Mhz / 875Mhz` on the desktop.

**Unusual values and what they suggest.**
- Link x8 or x4 on an x16 card: the card is in a lower slot, or that slot shares lanes with an M.2 drive.
- Link 2.5 GT/s at idle: normal power saving; it rises under load. If it stays low under load, check the BIOS PCIe setting.
- Busy stuck near 100% at idle: a program is using the GPU; check with a GPU monitor.

**Where Linux gets it.** `/sys/class/drm/cardN/device/` files: `driver`, `vendor`, `device`,
`gpu_busy_percent`, `mem_info_vram_used`, `mem_info_vram_total`, `current_link_speed`,
`current_link_width`, `pp_dpm_sclk`, `pp_dpm_mclk`.

**Background.** *PCIe link width* is how many lanes the card uses (x16 is full size). *GT/s*
(gigatransfers per second) is the speed per lane: 8 = PCIe 3.0, 16 = 4.0, 32 = 5.0.
*sclk* is the shader (core) clock and *mclk* the memory clock. *DPM* (Dynamic Power Management)
levels are the clock steps the card can pick from.

---
## Help  
`help` - Linux data

**In plain words.** The built-in help: what each main-window button does, the hot-keys, and the
command-line options. Also opened with **F1**.

**What you see.** Three blocks:
- Main window buttons - OK, Copy, PCI Bus / USB Bus, ALL Dev, Lookup, Find, Data Files, About / Help, Menu, Refresh, Next / Prev, Highlight / Colour.
- Hot-keys - Ctrl+F Find, Ctrl+L Lookup, Ctrl+D ALL Dev, F3 / Shift+F3 next / previous match, Ctrl+H highlight on/off, F5 refresh, F1 help, Esc close.
- Command line - `srs-gui`, `-save`, `id`, `find`, `devices`, `burn`, `--list`, `--version` / `-h`.

A last line gives the file locations: settings in `~/.config/srs/SRSDEF.ini`, root cache in
`~/.cache/srs/root`, reports in `~/sysreports`.

**Where Linux gets it.** Built-in text.

**Background.** The PCI Bus button resolves each device by trying the most exact ID first:
vendor:device:subvendor:subdevice:revision, then shorter keys, down to vendor only. That is what
`V:D:S:s:RR -> ... -> V` means.

---
## Highlight setup  
`highlight` - Linux data

**In plain words.** Your Find highlight colour, whether highlighting is on, and the panel font.

**What you see.** Columns: (key), Colour, Find Description, Setting, Reported Value.
- `[F]` row - the Colour Find Text as `#RRGGBB` and as `RGB( r, g, b )`.
- Highlight Matches - tick box and `on` / `off`.
- Panel Font - the Font Name setting.

Below: the settings file path (or `SRSDEF.ini not written yet - defaults shown`), and a note that
SIV's CPU/GPU speed highlight colours are the coloured columns of the `[cpu]` and `[gpu-clocks]` items.

**Healthy / typical values.** Defaults, and the author's values: `#FFFF00` (yellow, `RGB( 255, 255, 0 )`), on, Tahoma.

**Where Linux gets it.** Keys `Colour Find Text`, `Highlight Matches` and `Font Name` in section `[SRSDEF]` of SRSDEF.ini.

---
## Hotkeys  
`hot-keys` - Linux data

**In plain words.** The keyboard shortcuts of the SRS window.

**What you see.** A header (Profile, Hotkeys Enabled ticked) and one row per shortcut: letter
code, action, key and `panel accelerator (fixed)`. The ten shortcuts are Find (Ctrl+F), Lookup
(Ctrl+L), ALL Dev (Ctrl+D), Highlight Matches (Ctrl+H), Next Match (F3), Previous Match
(Shift+F3), Refresh (F5), Help (F1), Close Panel (Esc) and Copy Selection (Ctrl+C).

**Where Linux gets it.** Built-in list.

**Background.** These keys only work while an SRS window has focus. SIV's global hot-keys
(which work from any program and can switch screens or LEDs) have no SRS equivalent.

---
## SIV image (EXE)  
`image-exe` - Linux data

**In plain words.** The internal layout of the SRS program file: its sections.

**What you see.** The program path and size, then the full `readelf -S -W` table:
`[Nr]` section number, Name, Type, Address, Off (file offset), Size, ES (entry size), Flg (flags,
explained in the "Key to Flags" at the end), Lk, Inf, Al (alignment). If binutils is missing you
see `readelf not available (binutils)`.

**Healthy / typical values.** About 28 sections. `.text` (the code) is the largest; on the
author's 1.38 MB build it is `0x0d333c` bytes (about 845 KB). `.rodata` holds fixed text and tables.

**Where Linux gets it.** `readelf -S -W /proc/self/exe`.

**Background.** Linux programs use the ELF (Executable and Linkable Format) file format. A
*section* is one named part of the file: `.text` = code, `.data` = initial variables, `.bss` =
zero-filled memory reserved at start, `.rodata` = read-only data.

---
## SIV image (SYS driver)  
`image-sys` - Windows-only (Linux equivalent shown)

**In plain words.** SIV on Windows loads its own kernel driver (SIVX64.sys). SRS needs none.
Instead this lists the Linux kernel modules SRS reads data from.

**What you see.**
- Status - no kernel driver; root data comes from the srs-root helper.
- SIV equivalent - SIVX64.sys is not needed.
- One line per module that is present: name, `loaded` (a loadable module) or `built-in` (part of
  the kernel), its version if it reports one, and `refcnt` (how many users hold it).

Modules checked: k10temp, zenpower, coretemp, amdgpu, i915, nvidia, nct6775, nct6683, nct6687,
it87, asus_ec_sensors, asus_wmi_sensors, spd5118, jc42, ee1004, ryzen_smu, msr, cpuid, drivetemp,
nvme, acpi_cpufreq, amd_pstate, zenergy. Only those present are listed.

**Healthy / typical values.** On the author's AMD PC: k10temp, amdgpu (refcnt 61), nct6775,
spd5118, ee1004, ryzen_smu (version 0.1.7), msr (built-in), drivetemp, nvme, zenergy.

**Unusual values and what they suggest.**
- No CPU temperature module (k10temp on AMD, coretemp on Intel): CPU temperatures will be missing.
- No Super-I/O module (nct6775, it87 and so on): board fan speeds and voltages will be missing.
- No `drivetemp`: SATA drive temperatures are not in hwmon.

**Where Linux gets it.** `/sys/module/<name>/`, `version` and `refcnt`.

**Background.** A *kernel module* is a driver that can be loaded into the running kernel. A
*Super-I/O* chip on the motherboard (Nuvoton, ITE) reads fan speeds, voltages and board temperatures.

---
## Image tree  
`image-tree` - Linux data

**In plain words.** Which shared libraries SRS needs, and which libraries those need.

**What you see.** The program name and path, then a tree: each library the program needs, the
file it was loaded from (or `(not loaded)`), and below it the libraries that one needs. A last
line counts the shared objects mapped into the process.

**Healthy / typical values.** SRS needs very little. On the author's PC: `libm.so.6` and
`libc.so.6`, each pulling in `ld-linux-x86-64.so.2`; 6 shared objects mapped.

**Unusual values and what they suggest.** `(not loaded)` next to a needed library: unusual for a
running program; the library may be loaded under another path.

**Where Linux gets it.** `readelf -d` (the NEEDED entries) and `/proc/self/maps`. SRS does not run
`ldd`, since `ldd` can run the program.

**Background.** A *shared library* (`.so` file) is code many programs use at once, such as the C
library `libc.so.6`. `ld-linux` is the loader that links them at start.

---
## Initial processing  
`initial` - Linux data

**In plain words.** The steps SRS takes when the window starts.

**What you see.** A numbered list: 1 read SRSDEF.ini; 2 load pci.ids and usb.ids; 3 find PCI and
USB devices and resolve names; 4 find the newest report in `~/sysreports` for Find; 5 build the
item list for the Menu; 6 open the main window and set up the shortcut keys; 7 start refresh
timers only for open live panels. A note: with arguments, the GUI is skipped and the command line runs.

**Where Linux gets it.** Built-in text.

---
## Install  
`install` - Linux data

**In plain words.** Where SRS is installed and whether its menu entry and folders exist.

**What you see.** Program image path, then a tick box for each:
- Menu entry in `~/.local/share/applications`, `/usr/share/applications` and
  `/usr/local/share/applications` (`srs-gui.desktop`). If found, the `Exec` line it runs.
- Settings folder `~/.config/srs`.
- Root cache folder `~/.cache/srs/root`.

**Healthy / typical values.** A packaged install has the `/usr/share/applications` entry with
`Exec srs-gui`. On the author's PC that entry, the settings folder and the root cache folder are all ticked.

**Unusual values and what they suggest.**
- No menu entry ticked: SRS will not show in your app menu; reinstall the package.
- Root cache folder not ticked: the root helper has never run, so root-only items will say "Needs root".

**Where Linux gets it.** Checks those paths and reads `Exec=` from the `.desktop` file.

---
## SIV languages  
`languages` - Linux data

**In plain words.** SRS is English only, using UTF-8 text. This also shows your language settings.

**What you see.** UI language (`English only`), Encoding (`UTF-8`), then the values of the
environment variables LANG, LC_ALL, LC_MESSAGES, LC_CTYPE and LANGUAGE (`<unset>` if not set).

**Healthy / typical values.** On the author's PC: `LANG en_US.UTF-8`, the LC_ values unset.

**Unusual values and what they suggest.** A LANG without `UTF-8` (for example `C` or `POSIX`):
symbols such as `°C` and `µs` may print wrongly in a terminal.

**Where Linux gets it.** The process environment.

**Background.** A *locale* is the set of language, number and date rules a program uses. `LANG`
sets the default; `LC_*` variables override parts of it.

---
## Lock setup  
`lck-setup` - Linux data

**In plain words.** How SRS keeps its own threads from getting in each other's way. In short: it
needs almost no locks.

**What you see.** Instance lock (none; several copies may run), Find (one worker thread, the GUI
thread owns all windows), Burn-in (one thread per CPU, pinned, polls a stop flag), Hardware locks
(none, because SRS does no SMBus, ISA or PCI config access).

**Where Linux gets it.** Built-in text.

**Background.** A *lock* or *mutex* (mutual exclusion) stops two threads using the same thing at
the same time. SIV needs global locks because it talks to hardware buses directly; SRS leaves that to the kernel.

---
## Maker icons  
`makers` - Linux data

**In plain words.** The companies that made the PCI devices in your PC, with a count for each.

**What you see.** Columns: Vendor (4-digit hex PCI vendor ID), Maker (name from pci.ids, or
`(unknown)`), PCI functions (how many PCI functions from that vendor). If pci.ids is missing:
`pci.ids not found`.

**Healthy / typical values.** On the author's AMD PC: 1022 AMD (38 functions, the chipset and CPU
parts), 1002 AMD/ATI (4, the graphics card and its audio), 1B21 ASMedia (7, USB and SATA
controllers), 10EC Realtek (2, network), plus Samsung, ADATA and MAXIO (one each, NVMe drives).

**Unusual values and what they suggest.** `(unknown)` maker: pci.ids is too old for that device,
or it is a very new or rare vendor. Update hwdata.

**Where Linux gets it.** The PCI devices in sysfs, names from pci.ids.

**Background.** A *PCI function* is one logical device. One physical chip can show several (a GPU
usually shows a graphics function and an audio function). SIV shows maker logos here; SRS shows names.

---
## SIV manifest  
`manifest` - Windows-only (Linux equivalent shown)

**In plain words.** Windows programs carry a "manifest" describing them. The Linux equivalent is
the ELF *note* sections, shown here.

**What you see.** The output of `readelf -n`: for each note section, Owner, Data size and
Description. Typical notes:
- `.note.gnu.build-id` - a unique build ID (a long hex number) for this exact build.
- `.note.gnu.property` - CPU instruction-set levels the code needs and uses (x86-64-v2, v3, v4)
  and features (XMM, YMM, ZMM).
- `.note.ABI-tag` - the oldest Linux kernel ABI the program supports.

**Healthy / typical values.** On the author's build: build ID `XXXX...`, ABI `OS: Linux, ABI: 6.1.0`.

**Unusual values and what they suggest.** If "x86 ISA needed" lists `x86-64-v4` (AVX-512 level),
that build only runs on CPUs with AVX-512. A copy built that way will crash with "illegal
instruction" on an older CPU. Use a build made for a lower level.

**Where Linux gets it.** `readelf -n -W /proc/self/exe`.

**Background.** The *ABI* (Application Binary Interface) is the set of low-level rules a program
and the kernel agree on. *x86-64-v2/v3/v4* are CPU feature levels: v3 needs AVX2, v4 needs AVX-512.

---
## Mapping cache  
`map-cache` - Windows-only (Linux equivalent shown)

**In plain words.** In SIV this shows how its driver maps physical memory. SRS never maps
physical memory, so it just explains that.

**What you see.** Status (Windows-only feature), srs-gui (no driver, `/dev/mem` is never opened),
Nearest Linux (kernel drivers expose registers in sysfs; root-only tables come from the srs-root cache).

**Background.** *Mapping physical memory* means reading hardware registers by their raw address.
It is powerful but risky; on Linux the kernel drivers do it for you.

---
## Map/unmap test  
`map-unmap` - Windows-only (Linux equivalent shown)

**In plain words.** In SIV this times its driver mapping and unmapping physical memory. SRS has
nothing to time; it shows the same three lines as [Mapping cache](#mapping-cache).

---
## Menus  
`menus` - Linux data

**In plain words.** The full list of every item SRS has, grouped by category.

**What you see.** For each category, a line `[category]  N items`, then one line per item:
`[id]`, title, bucket and group. At the end: `N items in M categories`.
- Bucket `Linux` (green) - real Linux data.
- `Absent` - hardware most PCs do not have.
- `Windows-only` - a Windows feature; the Linux equivalent is shown.
- `(stub)` after an item - the item has no data function yet.

**Healthy / typical values.** On the author's PC the `[tuning]` category alone has 48 items, and
the whole program has 558.

**Where Linux gets it.** The program's own item list (same as `srs-gui --list`).

---
## Mutex info  
`mutex` - Linux data

**In plain words.** File locks that SRS holds right now. Normally none.

**What you see.** Columns # (lock number), Class (`POSIX`, `FLOCK`, `OFDLCK`), Mode (`ADVISORY`
or `MANDATORY`), Access (`READ` or `WRITE`), PID / Inode (process, device and file, and byte
range). Then a count: `N file locks held by this process (/proc/locks)`.

**Healthy / typical values.** `0 file locks held by this process` on the author's PC.

**Where Linux gets it.** `/proc/locks`, filtered to this process ID.

**Background.** A *file lock* tells other programs "I am using this file". The kernel lists every
lock in `/proc/locks`.

---
## PMB info (power monitor buffer)  
`pmb` - Linux data

**In plain words.** Every live power (watts) and current (amps) reading the kernel's sensor
drivers provide.

**What you see.**

| Column | What it means | Why it matters |
|---|---|---|
| # | Row number. | - |
| Device | hwmon chip name, e.g. `amdgpu`, `zenergy`. | Which part the reading belongs to. |
| Description | Sensor label (e.g. `PPT`) or the raw name (`power1`). | Which rail or limit. |
| Current | Power in W (3 decimals) or current in A. | The live value. |
| Characteristic | `cap N W` if a power cap is set for that sensor. | How close you are to the limit. |

The panel refreshes every second.

**Healthy / typical values.** On the author's PC: `amdgpu PPT 35.000 W cap 374 W` (graphics card
on the desktop). A desktop GPU idles at roughly 10-40 W; under game load it runs near the cap.

**Unusual values and what they suggest.**
- GPU power near the cap at idle: something is rendering, or a monitor setup keeps memory clocks high
  (common with several screens or high refresh rates).
- A cap well below the card's rated power: a power limit was lowered, by a tuning tool or the BIOS.
- No rows: no hwmon driver reports power; CPU power on AMD needs a module such as `zenergy`.

**Where Linux gets it.** `/sys/class/hwmon/*/power*_input`, `power*_average`, `curr*_input`,
with `*_label` and `power*_cap`.

**Background.** *PPT* (Package Power Tracking) is AMD's name for total power drawn by the chip
package. A *power cap* is the limit the firmware holds it to.

---
## Colour RGB debug  
`rgb-debug` - Linux data

**In plain words.** The colour palette SRS uses for text, with what each colour means.

**What you see.** Columns Colour (internal name), Default Colour (`RGB( r, g, b )`), Hex and
Colour Description. Rows: C_LABEL (labels, light blue 3399FF), C_VALUE (values, dark blue 0000CC),
C_PURPLE (paths, IDs, commands, 9C0FB8), C_TEAL (secondary values, 009999), C_ORANGE (build and
warnings, FF8800), C_GREEN (OK / enabled, 00A000), C_RED (errors / not found, E00000), C_BLACK
(plain text), C_GRAY (notes, disabled, 808080), and Find Text (your Colour Find Text setting).

**Where Linux gets it.** Built-in palette, plus SRSDEF.ini for Find Text.

**Background.** Knowing the colours helps you scan a panel: red means missing or error, orange
means "changed from default" or a warning, green means OK.

---
## SDM info (storage device monitor)  
`sdm` - Linux data

**In plain words.** Every whole disk the kernel sees, with its size and how much has been read and
written since boot.

**What you see.**

| Column | What it means | Why it matters |
|---|---|---|
| # | Row number. | - |
| Device Name | Kernel name: `nvme0n1`, `sda`, `sr0`, `zram0`. | Matches `/dev/<name>`. |
| Model | Model string, or `-`. | Identifies the drive. |
| Size | Capacity. | `0B` = no media (empty card reader or USB slot). |
| Reads | Data read since boot. | Shows which drives are busy. |
| Writes | Data written since boot. | Heavy writes wear SSDs. |
| In flight | Requests in progress right now. | Stuck above 0 can mean a hung drive. |
| Rotational / Removable | Spinning disk or solid state; fixed or removable. | Affects which I/O scheduler suits it. |

Partitions, loop and ram devices are skipped. Refreshes every 2 seconds.

**Healthy / typical values.** On the author's PC: three NVMe SSDs (Samsung 980 PRO 1 TB, ADATA
S70 2 TB, PNY CS2340 500 GB), a 4 TB Seagate hard disk (`rotational / fixed`), USB sticks, a DVD
writer and `zram0` (15.23 GB compressed RAM swap). In flight `0` on all of them.

**Unusual values and what they suggest.**
- In flight stays above 0 for many seconds: the drive is stuck or very slow. Check `dmesg` for errors.
- A drive you know is there is missing: cable, power, or the drive has dropped off the bus.
- USB sticks shown as `rotational`: normal. Many USB bridges report this by default, not because the stick spins.
- An optical drive shows a size even with no disc: the kernel keeps the last size; ignore it.

**Where Linux gets it.** `/sys/class/block/<dev>/{size,stat,removable,queue/rotational,device/model}`.

**Background.** *zram* is a block device in RAM that stores compressed data, used as fast swap.
Size and sector counts are in 512-byte sectors, which SRS converts to bytes.

---
## Sequence setup  
`sequence` - Linux data

**In plain words.** The order in which items appear in a plain `-save` (one with no item list).
It is the same order SIV64X uses.

**What you see.** A numbered list: position, `[id]` and title. It starts `1 [overview]`,
`2 [sgb-ini]`, `3 [sdm]`, `4 [dsb]`, `5 [bsi]` and so on.

**Healthy / typical values.** About 120 items in the default save.

**Where Linux gets it.** The program's built-in default save list.

**Background.** Keeping SIV's order means you can compare an SRS save with a SIV save side by side.

---
## SGB end  
`sgb-end` - Linux data

**In plain words.** In SIV this marks the end of the system/board buffer. In SRS it is a marker
line plus how long the process has been running.

**What you see.** SGB end (explains there is no shared buffer; [SGB info](#sgb-info-systemboard-buffer)
reads sysfs live) and Elapsed (seconds since start).

**Healthy / typical values.** On the author's full save: `9.230 seconds since start` at that point.

---
## SGB info (system/board buffer)  
`sgb-ini` - Linux data

**In plain words.** The motherboard, BIOS and chipset IDs, plus a few facts about the SRS process.

**What you see.**

| Field | What it means | Why it matters |
|---|---|---|
| Maker Name / Model Name / Board Version | The motherboard maker, model and revision. | Find the right manual and BIOS. |
| Serial Number | Board serial. Needs root; read from the cached dmidecode output. Else `Needs root: ...`. | Warranty and asset tracking. Keep it private. |
| System | System maker and product name. | Prebuilt PCs show a product here; self-builds show placeholders. |
| BIOS Vendor / Version / Date / Release | The firmware maker, version, date and release number. | Check if a newer BIOS exists. |
| Chassis Type | SMBIOS chassis code (3 = desktop, 10 = notebook, and so on). | What kind of case firmware reports. |
| MCH (00:00.0) | Vendor-device ID of PCI device 00:00.0, the host bridge. | Identifies the CPU / chipset family. |
| ACT CPUs | Online CPUs (configured CPUs). | Should match your thread count. |
| SRS Affinity | Hex mask of CPUs SRS may run on. | Normally all CPUs. |
| SRS PID | Process ID of this SRS run. | - |
| SRS Driver | `none`. | SRS uses no kernel driver. |
| Root cache | Whether `dmidecode.txt` is in the root cache. | If not, the serial cannot be shown. |
| RAW Command | Path of the running program. | - |

**Healthy / typical values.** On the author's PC: ASUSTeK COMPUTER INC., TUF GAMING X870-PRO WIFI7
W NEO, Rev 1.xx, serial `XXXX`, System `ASUS System Product Name` (a placeholder, normal for a
self-built PC), American Megatrends BIOS 1805 dated 09/18/2026, release 18.5, chassis type 3,
MCH `1022-14d8` (AMD), ACT CPUs `16 (16)`, affinity `000000000000FFFF`.

**Unusual values and what they suggest.**
- Fields like "To Be Filled By O.E.M." or "Default string": the board maker left them blank. Harmless.
- SRS Affinity not all ones: SRS was started with `taskset` or inside a restricted cgroup or container.
- ACT CPUs smaller than the number in brackets: some CPUs are offline.

**Where Linux gets it.** `/sys/class/dmi/id/*` (SMBIOS types 0, 1, 2 and 3), PCI `0000:00:00.0`
vendor and device files, `sched_getaffinity()`, and `~/.cache/srs/root/dmidecode.txt` for the serial.

**Background.** *SMBIOS* (System Management BIOS) is a table the firmware fills with facts about
the board, BIOS, memory and case. Linux copies the public fields to `/sys/class/dmi/id`; serial
numbers are root-only.

---
## Active SIV instances  
`siv-active` - Linux data

**In plain words.** Every copy of SRS running right now.

**What you see.** Columns Name (process name), PID, Version (`V<version> (this)` for the copy
making the report, `-` for others) and Image Filename (program path, or `(not readable)`). Then a
count. Names come from `/proc/<pid>/comm`, which the kernel cuts to 15 characters, so
`sysreport-search` shows as `sysreport-searc`.

**Healthy / typical values.** On the author's PC: the tray copy `srs-gui` and the `sysreport-searc`
copy making the save; 2 instances.

**Unusual values and what they suggest.** Many old copies: earlier windows or tray icons did not
close. They are safe to close; SRS holds no driver or hardware lock.

**Where Linux gets it.** `/proc/*/comm` and `/proc/*/exe`.

---
## SIV autorun  
`siv-autorun` - Linux data

**In plain words.** Whether SRS starts by itself when you log in.

**What you see.** Two tick-box lines: User autostart (`~/.config/autostart/srs-gui.desktop`) and
System autostart (`/etc/xdg/autostart/srs-gui.desktop`), each with its `Exec` command if present.
Then `srs-gui starts at login.` or how to enable it.

**Healthy / typical values.** Off by default. On the author's PC the user entry is on and runs
`/usr/bin/srs-gui --gui-tray` (start in the system tray).

**Where Linux gets it.** The XDG autostart folders.

**Background.** *XDG autostart* is the desktop standard: any `.desktop` file in an autostart
folder is run at login. Delete or rename the file to stop it.

---
## SIV DLLs  
`siv-dll` - Linux data

**In plain words.** The files SRS depends on: name databases, the program itself, the settings
file and every shared library it has loaded. Opens with the Lookup button row.

**What you see.** A table with Description, File Specification (path), Current Identification
(version or date), Code Page (text encoding, always UTF-8) and WEB Download Location. Rows:
- PCI Device Names, USB Device Names, PnP / Monitor Names - the `.ids` files, their version and upstream URL.
- srs-gui Program - path, version and size.
- Settings - SRSDEF.ini and when it was last written, or `not written yet`.
- Kernel driver - `none`.

Then "Shared objects mapped into this process": each library name, path, size and file date.

**Healthy / typical values.** On the author's PC: libc, libm, ld-linux and a few `gconv` modules
(character-set converters loaded by the C library, such as CP932.so), all from `/usr/lib`.

**Unusual values and what they suggest.** A library loaded from an odd folder (your home
directory, `/tmp`): something is setting `LD_LIBRARY_PATH` or `LD_PRELOAD`. Check your shell setup.

**Where Linux gets it.** `/proc/self/maps`, file sizes and dates, and the `.ids` file headers.
No network check is made.

**Background.** On Windows a DLL (Dynamic-Link Library) is a shared code file. The Linux
equivalent is a shared object (`.so`).

---
## SIV driver  
`siv-drv` - Windows-only (Linux equivalent shown)

**In plain words.** SIV shows its kernel driver here. SRS has no driver; it shows the root
helper's cache instead, which is how SRS gets root-only data.

**What you see.**
- Kernel driver - none.
- Root helper - srs-root (**Menu -> Run as root**) writes a read-only cache; items never run as root.
- Root cache - the cache folder path.
- One line per cache file: name, size and date. Or `empty - Needs root: Menu > Run as root (srs-root)`.

**Healthy / typical values.** On the author's PC the cache has about 24 files, all from the same
run: `dmidecode.txt`, `dmesg.txt`, `msrs.txt`, `pci-all.txt`, `spd.txt`, `smart-<drive>.txt`, NVMe
details and more. Tiny `smart-*.txt` files (55 bytes) belong to drives that report no SMART data,
such as card readers.

**Unusual values and what they suggest.**
- Cache files days or weeks old: root-only items show old data. Run the root helper again.
- `smart-*` files for drives no longer attached: left over from an earlier run. Harmless.

**Where Linux gets it.** `~/.cache/srs/root/`.

**Background.** SMART (Self-Monitoring, Analysis and Reporting Technology) is a drive's own health
log. Reading it needs root, which is why it comes from the cache.

---
## SIVDEF.ini  
`siv-ini` - Linux data

**In plain words.** The whole SRS settings file, printed as it is.

**What you see.** Each `[section]` in bold, then each `key  value` line. Comment lines (`#` or
`;`) show as notes. Last line: `N section(s) in <path>`. If the file does not exist yet: `No
Sections Found within the SRSDEF.ini file`, `Status 2 File not found.` and the path.

**Healthy / typical values.** One `[SRSDEF]` section. On the author's PC: Colour Find Text FFFF00,
Highlight Matches 1, Font Name Tahoma, GPU Load Command, Tray Icon 1, Close To Tray 1, Minimize
To Tray 1, Start In Tray 1, Theme Dark.

**Unusual values and what they suggest.** "File not found" on a fresh install: normal until you
change a setting in the window.

**Where Linux gets it.** `$XDG_CONFIG_HOME/srs/SRSDEF.ini`, by default `~/.config/srs/SRSDEF.ini`.
SIV keeps its settings in SIVDEF.ini and the Windows registry; SRS uses this file.

---
## SIV location / window poke  
`siv-location` - Linux data

**In plain words.** How SRS sizes and places its windows, and what kind of desktop session it is in.

**What you see.** Main window (default 1000 x 420), Panels (default 1180 x 560, cascaded on X11),
Saved positions (none; SRS does not remember positions), Session (`Wayland`, `X11` or `no display
(CLI)`), Desktop (from XDG_CURRENT_DESKTOP, if set), GDK_SCALE (window scale factor) and Window
poke (not supported on Linux).

**Healthy / typical values.** On the author's PC: Wayland, KDE, GDK_SCALE `1 (default)`.

**Unusual values and what they suggest.** Text far too small or large: set `GDK_SCALE=2` (or 1)
before starting SRS on a high-DPI screen.

**Where Linux gets it.** The environment variables WAYLAND_DISPLAY, DISPLAY, XDG_CURRENT_DESKTOP and GDK_SCALE.

**Background.** On Wayland the compositor (the desktop) decides where windows go; programs cannot
place themselves. "Window poke" in SIV sends messages to another program's window; Linux has no
general way to do that.

---
## SIV qualifiers  
`siv-qualifiers` - Linux data

**In plain words.** Every command-line option SRS accepts, with what it does.

**What you see.** A two-column table, Qualifier and Meaning:
- `-save`, `-save=[item][item]...`, `-save=[...]=file|STDOUT` - write items as text.
- `id ID` with `--bus pci|usb` and `--mode tree|scan|info` - look up a device ID.
- `find PATTERN` with `--report DIR`, `-i`, `-F`, `--max N`, `--color auto|always|never` - search a report.
- `devices` - resolve every local device.
- `burn` - CPU burn-in test, with thread, CPU, time, mode and temperature limit options.
- `--list`, `--version`, `-h, --help`.

Last line: Command Used (the program path).

**Healthy / typical values.** Fixed list for each version.

**Where Linux gets it.** Built-in list.

**Background.** In SIV, options are called *qualifiers*. Defaults are given in brackets, e.g.
`--bus pci` and `--mode tree`.

---
## SIV qualifier list  
`siv-quals` - Linux data

**In plain words.** The same options as [SIV qualifiers](#siv-qualifiers), names only, without
sub-options or meanings.

**What you see.** `-save`, `-save=[item][item]...`, `-save=[...]=file|STDOUT`, `id ID`,
`find PATTERN`, `devices`, `burn`, `--list`, `--version`, `-h, --help`.

---
## SIV setup  
`siv-setup` - Linux data

**In plain words.** The main settings SRS knows about, and whether each is set in your file.

**What you see.** Columns (tick), Name, Data and Meaning:
- `|X|` = the key is in SRSDEF.ini, with its value in quotes. `|_|` = not set, showing `<default ...>`.
- Keys: Colour Find Text (Find match background, RRGGBB), Highlight Matches, Font Name, and
  SIO Sensors (Super-I/O sensor inputs you have switched back on, e.g. `nct6799:AUXTIN1,...`).

**Healthy / typical values.** On the author's PC: the first three set (`"FFFF00"`, `"1"`,
`"Tahoma"`), SIO Sensors `<default none>`.

**Where Linux gets it.** Section `[SRSDEF]` of SRSDEF.ini. (SIV keeps these in the Windows registry.)

**Background.** Super-I/O chips have spare inputs that boards often leave unconnected, so SRS
hides some by default. SIO Sensors lets you show chosen ones again.

---
## SIV sizes  
`siv-sizes` - Linux data

**In plain words.** The size in bytes of SRS's main internal data structures. Mainly for developers.

**What you see.** Columns Structure, Bytes and Use, for: srs_item (one item), srs_out (output
sink), srs_db (a loaded `.ids` database), srs_rec (one database record), srs_result (a Lookup
result), srs_info (an Info lookup), srs_findres and srs_hit (Find results), srs_dev (one device),
and the C types `void *`, `long`, `size_t`. Then Items (how many registered) and Page size.

**Healthy / typical values.** On a 64-bit PC: pointer, long and size_t are 8 bytes; page size
4096. The author's build: srs_item 64 bytes, 558 items.

**Unusual values and what they suggest.** Page size 16384 or 65536: normal on some ARM systems.

**Where Linux gets it.** Compile-time sizes and `sysconf(_SC_PAGESIZE)`.

**Background.** A memory *page* is the unit the CPU and kernel use to manage memory, usually 4 KB on x86.

---
## SIV driver service  
`siv-srv` - Windows-only (Linux equivalent shown)

**In plain words.** In SIV this controls the Windows service that loads its driver. SRS installs
no service.

**What you see.** Status (Windows-only), srs-gui (no driver, no service; root data via the
srs-root helper), Helper start (on demand from **Menu -> Run as root**, with a polkit password
prompt; it exits after writing the cache) and Nearest Linux (kernel modules, see
[SIV image (SYS driver)](#siv-image-sys-driver)).

**Background.** *polkit* is the Linux service that asks for your password before a program may do
something as root.

---
## SIV usage history  
`siv-usage` - Linux data

**In plain words.** Which version you have, when the program file was made, and when your
settings were last saved.

**What you see.** Columns Version, Image Date (program file date) and Settings Last Written
(SRSDEF.ini date, `-` if none). A note: SRS keeps no usage counter.

**Healthy / typical values.** On the author's PC: `V2.2.0`, image 2026-10-26, settings 2026-10-26.

**Where Linux gets it.** File modification times of the program and SRSDEF.ini.

---
## SIV use / update  
`siv-use` - Linux data

**In plain words.** Version facts and how up to date the name databases are. SRS never goes
online to check for updates.

**What you see.** Program (version, local build, no update channel), Update check (not performed;
no network access), then one line per database: name, version or date, and the upstream URL.
Last line: update the `.ids` files with your distribution's hwdata package (or `update-pciids` / `update-usbids`).

**Healthy / typical values.** pci.ids a few weeks old at most on a rolling distribution. On the
author's PC: pci.ids 2026.10.01, usb.ids 2026.06.26.

**Unusual values and what they suggest.** Databases many months old: new devices may show as
unknown. Update the system's hwdata package.

**Where Linux gets it.** The header lines of the `.ids` files.

---
## SMB end  
`smb-end` - Linux data

**In plain words.** In SIV this marks the end of the SMBus buffer. In SRS it is one marker line:
SRS does no SMBus access; see [SMB info](#smb-info).

---
## SMB info  
`smb-ini` - Linux data

**In plain words.** The I2C / SMBus adapters and devices the kernel has registered, by name only.

**What you see.** Columns Device (sysfs name), Name (kernel's name for it) and Driver (driver bound
to it, or `-`). Adapters are named `i2c-N`; devices on them are named `bus-address`, e.g. `1-0051`.
A note: SRS never opens `/dev/i2c-*` or probes addresses.

**Healthy / typical values.** On the author's AMD PC: `SMBus PIIX4 adapter` ports (the chipset
SMBus, where the RAM sits), `AMDGPU DM` buses (one per display output, used to read monitor
data), `AMDGPU SMU`, a Synopsys DesignWare adapter, and two `spd5118` devices at 1-0051 and 1-0053
(the RAM sticks).

**Unusual values and what they suggest.**
- No PIIX4 / i801 adapter: the chipset SMBus driver is not loaded (`i2c_piix4` on AMD, `i2c_i801` on Intel), so RAM sensors cannot appear.
- RAM devices missing while the adapter is there: load `spd5118` (DDR5) or `ee1004` / `jc42` (DDR4).

**Where Linux gets it.** `/sys/bus/i2c/devices/*/name` and the `driver` link.

**Background.** *I2C* and *SMBus* are slow two-wire buses that link small chips: RAM SPD chips,
sensors, RGB controllers and the monitor's DDC (Display Data Channel) for its name and settings.
Each device has a 7-bit address.

---
## Lock states  
`states` - Linux data

**In plain words.** Each thread of SRS and what it is doing right now.

**What you see.** Columns TID (thread ID), Thread (name), State (one letter) and Wait channel
(the kernel function it sleeps in, or `-`).

**Healthy / typical values.** In a command-line save: one thread, state `R` (running).

**Unusual values and what they suggest.** A thread in state `D` for a long time: it is stuck
waiting on a device (often a disk or USB drive).

**Where Linux gets it.** `/proc/self/task/*/{comm,stat,wchan}`.

**Background.** Thread states: `R` running, `S` sleeping (waiting for an event), `D` waiting on
hardware and cannot be interrupted, `T` stopped, `Z` zombie (finished but not yet collected).

---
## STC info (thread contexts)  
`stc` - Linux data

**In plain words.** A fuller view of SRS's threads: CPU time used, which CPUs each may run on,
and priority.

**What you see.**

| Column | What it means |
|---|---|
| TID | Thread ID. |
| Name | Thread name (cut to 15 characters). |
| State | One-letter state, see [Lock states](#lock-states). |
| Total CPU | User + system CPU time, as minutes:seconds. |
| Affinity | CPUs the thread may run on, e.g. `0-15`. |
| Last CPU | The CPU it last ran on, e.g. `CPU-15`. |
| Priority / Nice | Kernel priority and nice value. |

Then a count of threads. Refreshes every 2 seconds in the window.

**Healthy / typical values.** On the author's PC during a save: one thread, `R`, `0:00.420`,
`0-15`, `CPU-15`, `16 / -4`.

**Unusual values and what they suggest.** Nice not 0: the program was started with `nice`, or a
tool such as a scheduler daemon or `ananicy` changed it.

**Where Linux gets it.** `/proc/self/task/*/stat` and `status` (`Cpus_allowed_list`).

**Background.** *Nice* runs from -20 (most favoured) to 19 (least). The kernel *priority* shown
is 20 + nice for normal tasks.

---
## Tune system  
`tune` - Linux data

**In plain words.** A list of common kernel tuning values (sysctls), with the kernel default, your
current value and a commonly used "tuned" value side by side.

**What you see.**

| Column | What it means |
|---|---|
| Value Name | The sysctl name, e.g. `vm.swappiness`. |
| Default | The usual kernel default, in brackets. |
| Current | Your value now. Green = same as default; orange = changed; grey `<none>` = not present in this kernel. |
| Other | A value often used by tuning guides and gaming distributions, in brackets. |
| Path | The `/proc/sys` file it comes from. |

The values covered: memory (`vm.swappiness`, `vm.vfs_cache_pressure`, `vm.dirty_*`,
`vm.page-cluster`, `vm.max_map_count`, `vm.compaction_proactiveness`, `vm.watermark_boost_factor`,
`vm.overcommit_memory`, `vm.zone_reclaim_mode`), kernel (`nmi_watchdog`, `watchdog`,
`sched_autogroup_enabled`, `split_lock_mitigate`, `numa_balancing`, `kptr_restrict`,
`dmesg_restrict`, `perf_event_paranoid`, `unprivileged_bpf_disabled`, `printk`,
`timer_migration`), files (`fs.inotify.max_user_watches`, `fs.file-max`) and network
(`default_qdisc`, `tcp_congestion_control`, `rmem_max`, `wmem_max`, `netdev_max_backlog`,
`tcp_fastopen`, `tcp_mtu_probing`). SRS only reads; it offers no Apply button.

**Healthy / typical values.** There is no single "right" value. On the author's tuned gaming
setup: `vm.swappiness 150` (high, normal with zram swap), `vm.max_map_count 2147483642` (needed by
some games), `net.core.default_qdisc fq`, `tcp_congestion_control bbr3`, `kernel.split_lock_mitigate 0`.

**Unusual values and what they suggest.**
- `vm.max_map_count` at 65530 and some games crash or fail to load: raise it (many distributions now set 1048576 or more).
- `vm.swappiness` high without zram: the system may swap to disk too soon.
- `kernel.numa_balancing <none>`: the kernel was built without NUMA balancing. Normal on single-socket kernels.
- `fs.inotify.max_user_watches` at 8192 and an IDE or file sync tool says "too many watches": raise it.

**Where Linux gets it.** `/proc/sys/<name with dots changed to slashes>`.

**Background.** A *sysctl* is a kernel setting you can read and change at run time. *Swappiness*
sets how eagerly the kernel swaps. *Dirty* values set how much changed data may wait in RAM before
it is written to disk. *qdisc* (queueing discipline) and *congestion control* decide how network
packets are sent.

---
## Tuning  
`tuning` - Linux data

**In plain words.** The current power and performance profile: CPU frequency driver, governor,
boost, CPU scheduler, memory and disk scheduler settings.

**What you see.** Lines appear only when the source exists:

| Field | What it means |
|---|---|
| tuned profile / tuned mode | The active profile of the `tuned` service, or `tuned not active`. |
| cpufreq driver | The CPU frequency driver, e.g. `amd-pstate-epp`, `intel_pstate`, `acpi-cpufreq`. |
| Governor (CPU-0) | The frequency policy, e.g. `performance`, `powersave`, `schedutil`. |
| EPP (CPU-0) | Energy Performance Preference hint to the CPU. |
| amd_pstate | amd-pstate mode: `active`, `passive` or `guided`. |
| Boost | Turbo / boost clocks enabled or disabled. |
| Platform profile | ACPI platform profile (laptops): `balanced`, `performance`, `low-power`. |
| sched_ext | Whether an extensible CPU scheduler is loaded, and which one. |
| THP enabled / THP defrag | Transparent huge page settings; the value in [brackets] is active. |
| KSM run | Kernel same-page merging on (1) or off (0). |
| PCIe ASPM policy | PCIe link power saving policy; the [bracketed] one is active. |
| vm.swappiness | Same as in [Tune system](#tune-system). |
| I/O scheduler <disk> | The disk request schedulers available; the [bracketed] one is active. |

**Healthy / typical values.** On the author's PC: tuned `cachyos-gaming`, `amd-pstate-epp`,
governor and EPP `performance`, amd_pstate `active`, boost enabled, sched_ext enabled with
`lavd`, THP `[always]`, NVMe drives on `[none]` or `[adios]`, the hard disk and USB drives on `[bfq]`.
A plain distribution install usually shows `powersave` / `balance_performance` and `madvise` THP.

**Unusual values and what they suggest.**
- Boost disabled: the CPU never reaches its rated top speed. Check the BIOS or a power tool.
- `acpi-cpufreq` on a modern Ryzen: amd-pstate is not in use (old kernel, or disabled in BIOS / kernel command line).
- A hard disk on `none`: works, but `bfq` or `mq-deadline` usually suits spinning disks better.

**Where Linux gets it.** `/etc/tuned/active_profile`, `/sys/devices/system/cpu/cpu0/cpufreq/*`,
`/sys/devices/system/cpu/amd_pstate/status`, `/sys/firmware/acpi/platform_profile`,
`/sys/kernel/sched_ext/`, `/sys/kernel/mm/transparent_hugepage/`, `/sys/kernel/mm/ksm/run`,
`/sys/module/pcie_aspm/parameters/policy`, `/sys/class/block/*/queue/scheduler`.

**Background.** A *governor* decides how fast the CPU should run. *EPP* is a hint the CPU's own
firmware uses to balance speed and power. *Transparent huge pages* use 2 MB memory pages instead
of 4 KB, which can speed up large programs. *ASPM* (Active State Power Management) lets PCIe links
sleep when idle. An *I/O scheduler* orders disk requests.

---
## TZM info (thermal zone buffer)  
`tzm` - Linux data

**In plain words.** The ACPI thermal zones: temperatures the firmware itself reports, with its
trip points.

**What you see.**

| Column | What it means |
|---|---|
| # | Row number. |
| Device Name (Zone) | Zone type and number, e.g. `acpitz (0)`, `x86_pkg_temp (1)`. |
| Current | Temperature now. |
| Passive | Temperature at which the system starts slowing down to cool. |
| Critical | Temperature at which the system shuts down. |
| Policy | The kernel's thermal governor for the zone, e.g. `step_wise`. |

Then `act N num N`. Refreshes every 2 seconds.

**Healthy / typical values.** Many desktops, including the author's, have no thermal zones at
all: `act 0 num 0` and a note that CPU and GPU temperatures come from hwmon. Laptops usually have
several (`acpitz`, `x86_pkg_temp`, `iwlwifi`).

**Unusual values and what they suggest.**
- Current near Passive: the system is about to throttle. Clean fans, check the cooler.
- A zone stuck at a fixed value like 27.8 C or 0 C: a dummy zone from the firmware. Ignore it.

**Where Linux gets it.** `/sys/class/thermal/thermal_zone*/{type,temp,policy,trip_point_*_type,trip_point_*_temp}`.

**Background.** A *thermal zone* is a firmware-defined area with a sensor. A *trip point* is a
temperature where the system acts: *passive* = slow down, *critical* = shut down.

---
## Words  
`words` - Linux data

**In plain words.** The list of interface words SRS uses. SIV uses this to check translations;
SRS is English only.

**What you see.** Language (`English (built in, no translation tables)`) and a comma list of the
button and label words: OK, Copy, PCI Bus, USB Bus, ALL Dev, Lookup, Find, Data Files, About,
Help, Refresh, Next, Prev, Highlight, Colour, Browse..., Latest, Ignore case, Literal, Highlight
Matches, Menu, Run as root, Save.

---
## XXB internal buffers  
`xxb` - Linux data

**In plain words.** An index of the "buffer" items. In SIV these are internal data buffers. In
SRS each is a live reading of a Linux source.

**What you see.** Columns Buffer, Description and srs-gui snapshot source:

| Buffer | Description | Source |
|---|---|---|
| [sgb-ini](#sgb-info-systemboard-buffer) | System/board buffer | `/sys/class/dmi/id`, PCI 00:00.0, process affinity |
| [sdm](#sdm-info-storage-device-monitor) | Storage device monitor | `/sys/class/block/*/stat` |
| [dsb](#dsb-info-dimm-sensor-buffer) | DIMM sensor buffer | hwmon spd5118 / jc42 |
| [bsi](#bsb-info-batteryhid-sensor-buffer) | Battery / HID sensor buffer | `/sys/class/power_supply`, `/sys/bus/hid/devices` |
| [pmb](#pmb-info-power-monitor-buffer) | Power monitor buffer | hwmon power* / curr* |
| [tzm](#tzm-info-thermal-zone-buffer) | Thermal zone buffer | `/sys/class/thermal` |
| [cgb](#cpu-group-buffer) | CPU group buffer | `/sys/devices/system/cpu/online` |
| cpu | CPU info buffer (in the CPU group) | `/sys/devices/system/cpu`, cpufreq |
| [gpu](#gpu-info-buffer) | GPU info buffer | `/sys/class/drm` |
| [stc](#stc-info-thread-contexts) | Thread contexts | `/proc/self/task` |

A note: SRS keeps no shared buffers; each item reads its source when shown.
