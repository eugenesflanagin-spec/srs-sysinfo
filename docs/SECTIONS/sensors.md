# Sensors / Cooling / Power  (SRS Section Reference)

[Back to the section index](README.md) - [Glossary](../GLOSSARY.md)

**Where to find these items (SRS 2.0).** In the main window they are on the **Sensors** page (Alt+8). Its tabs are Sensor status (with Zero and Hide unused), Hardware status, Cooling status (fans) and Power status; **All N items** lists the rest. The Overview's **Is my PC OK?** sums up the temperatures and alarms. You can also type an item's name or id into the search box (Ctrl+F). The rest of this introduction describes the classic view (Menu -> Classic View), with its Menu and panel buttons.

This group shows what your PC can measure about itself: temperatures, fan speeds, voltages, power use and
batteries. It also covers the chips and firmware that supply those readings (the Super I/O chip, ASUS WMI,
IPMI on servers) and the export and "setup" items that SIV offers for its own sensor tools.
Open it from **Menu -> Sensors / Cooling / Power -> item**, or use the **Sensors** panel button. The panel
has sub-buttons: **Zero** (reset the Minimum / Maximum columns), **Status** ([Sensor status](#sensor-status)),
**Cooling** ([Cooling status](#cooling-status-fans)), **Power** ([Power status](#power-status)) and **Hide**
(hide unused sensors).
Everything here is read only: SRS never changes a fan speed, a limit or a register.
Four items are only complete with root data from **srs-root** (Menu -> Run as root):
[ASUS ATK/WMI info](#asus-atkwmi-info), [ASUS WMI ACPI methods](#asus-wmi-acpi-methods),
[Super I/O LDNs](#super-io-ldns) and [SIO debug](#sio-debug).

All examples come from the author's PC: an ASUS TUF GAMING X870-PRO WIFI7 W NEO board (Nuvoton NCT6799
Super I/O chip), an AMD Ryzen 7 7800X3D, a Radeon RX 9070 XT, DDR5 memory, three NVMe drives and one SATA
hard disk.

## Contents
- [AIO Link export](#aio-link-export) `aio-export`
- [ASUS ATK/WMI info](#asus-atkwmi-info) `asus-info`
- [ASUS WMI ACPI methods](#asus-wmi-acpi-methods) `asus-wmi`
- [ASUS ATK0110 debug](#asus-atk0110-debug) `atk-debug`
- [Battery](#battery) `battery`
- [Cooling status (fans)](#cooling-status-fans) `cooling`
- [Sensor formula setup](#sensor-formula-setup) `formula-setup`
- [Hardware status](#hardware-status) `hw-status`
- [IPMI dump](#ipmi-dump) `ipmi-dump`
- [IPMI status](#ipmi-status) `ipmi-info`
- [IPMI time](#ipmi-time) `ipmi-time`
- [IPMI wait](#ipmi-wait) `ipmi-wait`
- [kWh cost](#kwh-cost) `kwh-cost`
- [LCD panel setup](#lcd-panel-setup) `lcd-panel`
- [Super I/O LDNs](#super-io-ldns) `ldn-info`
- [AIO Link devices](#aio-link-devices) `link-devices`
- [AIO Link dump](#aio-link-dump) `link-dump`
- [AIO Link fans](#aio-link-fans) `link-fans`
- [AIO Link LEDs](#aio-link-leds) `link-leds`
- [AIO Link limits](#aio-link-limits) `link-limits`
- [AIO Link power](#aio-link-power) `link-power`
- [AIO Link setup](#aio-link-setup) `link-setup`
- [AIO Link status](#aio-link-status) `link-status`
- [AIO Link test](#aio-link-test) `link-test`
- [AIO Link update](#aio-link-update) `link-update`
- [Power status](#power-status) `power`
- [PWM export](#pwm-export) `pwm-export`
- [QST status](#qst-status) `qst-status`
- [Sensor export](#sensor-export) `sen-export`
- [Sensor status](#sensor-status) `sensors`
- [SIO debug](#sio-debug) `sio-dbg`
- [Synthetic sensors setup](#synthetic-sensors-setup) `synthetic`
- [Thermal zones](#thermal-zones) `thermal-zones`
- [UPS export](#ups-export) `ups-export`

---
## AIO Link export
`aio-export` - Not present on most PCs

**In plain words.** An AIO (all-in-one) liquid cooler or a fan/RGB hub can report its own pump speed,
liquid temperature and fan speeds over USB. SIV can read some of these hubs directly. SRS only checks
whether Linux already has a kernel driver for one, and tells you if it found any.

**What you see.** This item, and all eleven "AIO Link ..." items below, print the same short report:
- Status line - "No AIO Link Devices found on this system", or one line per hub found
  (`hwmonN` - driver name). When a hub is found, a note says its readings are part of
  [Sensor status](#sensor-status) and [Cooling status](#cooling-status-fans).
- Checked - which kernel drivers were looked for: Corsair (Commander Pro `corsair_cpro`, Corsair PSUs
  `corsair-psu`), NZXT (`nzxt-kraken2`, `nzxt-kraken3`, `nzxt-smart2`), Aquacomputer (`d5next`, `quadro`,
  `octo`, `highflownext`), ASUS ROG Ryujin and Gigabyte Waterforce.
- A note that USB HID cooler devices belong to CoolerControl or liquidctl. SRS never opens `hidraw`
  (the raw USB HID device files), so it cannot clash with those tools.

**Healthy / typical values.** On the author's PC: "No AIO Link Devices found". That is normal for any
cooler without a Linux hwmon driver, and for an AIO whose pump is plugged into a motherboard header.
In that case the pump shows up as an ordinary fan in [Cooling status](#cooling-status-fans).

**Unusual values and what they suggest.** You own a supported hub but nothing is found: the driver
module is not loaded, or another tool has claimed the device. Check `lsmod` for the driver name.

**Where Linux gets it.** `/sys/class/hwmon/hwmon*/name`, matched against the list of cooler drivers.

**Background.** Corsair iCUE LINK is Corsair's single-cable system for fans, pumps and lights. SIV
talks to such hubs itself. On Linux the usual tools are liquidctl and CoolerControl, which drive the
hub over USB HID. SRS stays out of their way on purpose.

---
## ASUS ATK/WMI info
`asus-info` - Linux data - Needs root data

**In plain words.** ASUS boards hide some sensor and control functions behind firmware interfaces
called ATK and ASUS WMI. This item shows which of those interfaces your board has, and whether any
Linux ASUS sensor driver supports your exact board.

**What you see.**

| Field | Meaning | Why it matters |
|---|---|---|
| Board vendor / Board / BIOS | From the DMI (Desktop Management Interface) tables | Driver support is decided per board name |
| ASUS WMI GUID | `97845ED0-...` (the ATK / ASUSWMI interface). Green if present | If present, the ASUS WMI interface exists in firmware |
| ASUS sensor drivers | One line per driver: `asus_ec_sensors`, `asus_wmi_sensors`, `asus_atk0110`, `asus_wmi`, `asus_nb_wmi`. Each says loaded / not loaded, then either "supports this board" or why not | Tells you if an extra ASUS driver could add sensors |
| ASUS WMI blocks | Table of WMI blocks that belong to ASUS firmware devices: GUID, Obj, Inst, ACPI parent, Driver, Description | Shows what ASUS put in the firmware |
| ACPI tables / DSDT / SSDT | Where the copied ACPI tables are, and which ones were copied | Needed if you want to read the firmware code yourself |

The reasons a driver line can give:
- "N supported boards, this board is not listed" - the driver has a list of boards; yours is not on it.
- "binds by ACPI device id, not by board (DMI)" - the driver loads on a firmware device, not a board name
  (true for `asus_atk0110`).
- "module not built for this kernel" - your kernel has no such module file.
- "modinfo failed" - SRS could not read the module's info.

WMI table columns (same as in [ASUS WMI ACPI methods](#asus-wmi-acpi-methods)):
- GUID - the 128-bit name of the interface.
- Obj - the two-letter object id. `MO` is common for a data block holding a binary MOF (a description of
  the interface). `ev D2` means an event, with notify id `D2`.
- Inst - how many instances the block has.
- ACPI parent - the `PNP0C14:NN` firmware device and its UID (for example `AsusMbSwInterface`).
- Driver - the Linux driver bound to the block, or `-` for none.
- Description - a name SRS knows for that GUID, if any.

**Healthy / typical values.** On the author's TUF GAMING X870-PRO WIFI7 W NEO (BIOS 1805):
the ASUS WMI GUID is present on `PNP0C14:02 AsusMbSwInterface`; `asus_ec_sensors` says "60 supported boards,
this board is not listed" and `asus_wmi_sensors` says "16 supported boards, this board is not listed".
That is fine: board sensors come from the Super I/O chip instead (see [Sensor status](#sensor-status)).
Only the `wmi-bmof` driver is bound.

**Unusual values and what they suggest.**
- "supports this board" but "not loaded": you could get extra readings (often chipset, VRM or water
  temperatures) by loading that module.
- ACPI tables "Needs root: Menu > Run as root (srs-root)": the copy of the tables has not been made yet.

**Where Linux gets it.** `/sys/class/dmi/id/*`, `/sys/bus/wmi/devices`, `modinfo -F alias` (the
`dmi:` board patterns), `/sys/module`, and the srs-root copy of `/sys/firmware/acpi/tables`.

**Background.** WMI (Windows Management Instrumentation) is a Windows interface. Board makers put
WMI "blocks" inside the ACPI (Advanced Configuration and Power Interface) firmware so Windows tools can
call them. Linux can see these blocks through the `wmi` bus. A driver must exist for each one before it
does anything useful.

---
## ASUS WMI ACPI methods
`asus-wmi` - Linux data - Needs root data

**In plain words.** A full list of every WMI firmware device and WMI block on the board, not only the
ASUS ones. SRS lists them but does not run them.

**What you see.**
- ACPI WMI devices (PNP0C14) - one line per WMI device: device name, UID (the firmware's own name
  for it, for example `AsusLedControlWmi`) and ACPI path (for example `\ALCW`).
- WMI GUID blocks - the same table as in [ASUS ATK/WMI info](#asus-atkwmi-info), but for all blocks.
- A note: "WMI methods are not evaluated". Calling firmware methods from user space could change
  settings, so SRS never does it.
- ACPI tables / DSDT / SSDT - as in ASUS ATK/WMI info.

**Healthy / typical values.** The author's board has six WMI devices (`PNP0C14:00` to `:05`), including
`AsusLedControlWmi` (RGB lights), `AsusMbSwInterface` (the main ASUS WMI interface), `AsusMiniZipWmi`
and `AsusWifiOptionWmi`, plus 14 GUID blocks. Most have no Linux driver (`-`). That is normal.

**Unusual values and what they suggest.** "No WMI devices (/sys/bus/wmi/devices empty)": the `wmi`
kernel module is not loaded, or the firmware has no WMI at all (common on older or server boards).

**Where Linux gets it.** `/sys/bus/acpi/devices/PNP0C14*` (uid, path), `/sys/bus/wmi/devices`, and the
srs-root ACPI table copy.

**Background.** `PNP0C14` is the standard ACPI id for a "WMI mapper" device. Each one holds a `_WDG`
table that lists its GUID blocks. The method code lives in the DSDT and SSDT tables (the main and
extra ACPI code tables).

---
## ASUS ATK0110 debug
`atk-debug` - Not present on most PCs

**In plain words.** Older ASUS boards (roughly up to the early 2010s) gave sensor readings through a
firmware device called ATK0110. This item checks for it.

**What you see.**
- ACPI device - the ATK0110 device name if found, or "No ATK0110 ACPI device (used by older ASUS boards
  only)".
- asus_atk0110 module - loaded / not loaded.
- Checked - `/sys/bus/acpi/devices/ATK0110*`.

**Healthy / typical values.** Modern boards, like the author's X870, have no ATK0110. "Not present" is
the expected answer.

**Where Linux gets it.** `/sys/bus/acpi/devices`, `/sys/module/asus_atk0110`.

---
## Battery
`battery` - Not present on most PCs

**In plain words.** Lists every battery or UPS that Linux knows about. On a desktop that is often
only the batteries inside wireless controllers, mice and headsets.

**What you see.** One block per battery, named after the Linux power-supply entry. Each block shows
only the fields the driver provides:

| Field | Meaning |
|---|---|
| type | `Battery` or `UPS` |
| scope | `System` (powers the PC) or `Device` (powers a peripheral, like a game pad) |
| online | 1 if on external power |
| status | Charging, Discharging, Full, Not charging |
| capacity | Charge level in % |
| capacity_level | A rough level (Full, Normal, Low, Critical) when no % is given |
| health | Good, Overheat, Dead and so on |
| technology | Li-ion, Li-poly ... |
| manufacturer / model_name | Who made it |
| cycle_count | Charge cycles so far (battery wear) |
| voltage_now, current_now, power_now | Live V, A, W |
| energy_now / energy_full / energy_full_design | Wh now, Wh when full today, Wh when new |
| charge_now / charge_full / charge_full_design | the same in mAh, for drivers that report charge |

The serial number is never shown. A last line, Checked, says where SRS looked.

**Healthy / typical values.** The author's desktop has no system battery. It shows one Nintendo Switch
controller battery: type Battery, scope Device, status Discharging, capacity_level Full.

**Unusual values and what they suggest.** On a laptop, `energy_full` well below `energy_full_design`
means a worn battery (for example 70% of design = 30% wear). Health other than Good is worth acting on.

**Where Linux gets it.** `/sys/class/power_supply/*` (entries of type Battery or UPS).

---
## Cooling status (fans)
`cooling` - Linux data

**In plain words.** One line per fan header: how fast the fan spins, how hard the board drives it, who
is in control, which temperature it follows and the fan curve the chip uses.

**What you see.**

| Column | Meaning | Why it matters |
|---|---|---|
| Fan | Fan name (driver label if any, else `fanN`, or `pwmN` for an output with no speed sensor) | Matches the header number in the chip, not always the board's printed name |
| Current | Speed now, in RPM (revolutions per minute) | The main health sign |
| Minimum / Maximum | Lowest / highest speed SRS has seen since start or since **Zero** | Shows whether the fan ramps up |
| Target | Target speed, if the chip uses one | Shown for non-Super-I/O chips, and for Super I/O fans only in Fan Speed Cruise mode |
| Duty | How hard the header drives the fan: % and the raw value 0-255 in brackets | 100% (255) is full power |
| Mode | `PWM` (4-pin signal control) or `DC` (3-pin voltage control) | A 4-pin fan on a DC header still works, but control is coarser |
| Control | The chip's control mode number and name (see below), or "firmware / driver" when the driver gives no mode | Who decides the fan speed |
| Temperature source | The temperature input the fan follows: number and label | If this is a wrong or floating input, the fan reacts to nothing |
| Curve | Fan curve points as `temperature:duty` | What the board will do as things heat up |

Control mode names for Nuvoton (NCT) and Winbond (W83) chips: 0 off (full speed), 1 manual (software),
2 Thermal Cruise, 3 Fan Speed Cruise, 4 Smart Fan III, 5 Smart Fan IV. Other chips: 0 full speed / off,
1 manual (software), 2 and up automatic (hardware). The curve is only shown for automatic modes.

Fans reading 0 RPM are greyed. With **Hide** on, they are left out.

Under the table, **Thermal cooling devices** groups the kernel's cooling devices by type and state, for
example "16 devices Processor state 0/3" (16 CPU threads, throttle step 0 of 3, so no throttling).

**Healthy / typical values.** On the author's NCT6799, all seven headers run in mode 5 Smart Fan IV and
follow input 8 "PECI/TSI Agent 0 Calibration" (the CPU temperature as the chip sees it). fan1 runs at
874 RPM at 45% duty with the curve `20°C:35% 30°C:35% 40°C:35% 45°C:50% 125°C:100%`. fan7 runs at
3325 RPM at 100% - a pump-style reading. The Radeon's own fan runs at 553 RPM, 15% duty, with a Target of
553 RPM, controlled by its firmware.

**Unusual values and what they suggest.**
- 0 RPM with duty above 0%: nothing is plugged into that header, the fan has a zero-RPM mode, or the
  fan is stuck. On the author's PC, fan4 (68%) and fan6 (100%) read 0 RPM because nothing is plugged in.
- A fan stuck at 100% with a flat curve (`0°C:100% 100°C:100% ...`): the BIOS is set to full speed for that
  header. Often chosen for pumps.
- Speed never changes under load: the temperature source may be an unconnected input, or the curve is flat.
- Very low RPM at high duty on a fan that used to be faster: dust, a worn bearing, or a fan that needs
  more starting voltage.
- Mode `DC` on a 4-pin fan: switch the header to PWM in the BIOS for finer control.

**Where Linux gets it.** `/sys/class/hwmon/hwmon*/fanN_input`, `pwmN`, `pwmN_mode`, `pwmN_enable`,
`pwmN_temp_sel`, `pwmN_auto_pointN_temp/pwm`, `fanN_target`; `/sys/class/thermal/cooling_device*`.

**Background.** PWM (Pulse Width Modulation) switches the fan's power on and off very fast. The "duty"
is the share of time it is on. A fan curve is a list of points: at this temperature, use this duty. The
chip draws straight lines between the points.

---
## Sensor formula setup
`formula-setup` - Linux data

**In plain words.** Shows any rename, formula or "ignore" rule from your lm-sensors config that applies to
the sensor chips in your PC. SIV keeps its own formula table; on Linux the same job is done by
`/etc/sensors3.conf` and `/etc/sensors.d/*`.

**What you see.** A table with one row for each matching rule:

| Column | Meaning |
|---|---|
| SIV Device Name | The chip name (for example `nct6799`) |
| User Name | The new label from a `label` line |
| Formula | A `compute` formula (read, write), or `set = value (applied only by sensors -s)`, or `ignore` |
| Units | Units of that sensor (°C, RPM, V, W, J, A, %, Hz) |
| SIV Device ID | `hwmonN/feature`, for example `hwmon4/in4` |
| Source | File and line number, for example `sensors3.conf:233` |

If no rule matches, SRS says so and lists every chip with its full lm-sensors name, so you know what name
to write in a `chip` line.

**Healthy / typical values.** On the author's PC: "No label / compute / set / ignore statements match the
chips present", followed by the 13 chips (nct6799-isa-0290, k10temp-pci-00c3, ...). That is normal: the
stock `sensors3.conf` has no section for the NCT6799.

**Unusual values and what they suggest.** A rule you wrote does not show: the `chip` pattern does not match
the name shown here. Copy the name from this list.

**Where Linux gets it.** `/etc/sensors3.conf` (or `/etc/sensors.conf`) and `/etc/sensors.d/*`, matched to
`/sys/class/hwmon` chips. SRS only reads these files.

**Background.** In lm-sensors config, `label in4 "+12V"` renames a reading, `compute in4 @*12, @/12`
scales it (the `@` is the raw value; the first formula converts the reading, the second converts back
for writing limits), `ignore` hides it, and `set` writes a limit when you run `sensors -s`.

---
## Hardware status
`hw-status` - Linux data

**In plain words.** A one-screen summary of every sensor chip: how many readings of each type it has,
how many are not connected, and how many are in alarm. Plus the case-open switch and the list of loaded
monitoring drivers.

**What you see.**

| Column | Meaning |
|---|---|
| Chip | Driver name of the chip (`nct6799`, `k10temp`, `amdgpu` ...) |
| Sensors name | Full lm-sensors name, for example `nct6799-isa-0290` |
| Device | The kernel device it hangs off (PCI address, platform device, NVMe name ...) |
| Temp / Fans / Volts / Power / PWM | How many readings of each type |
| Other | Energy, current, humidity and clock readings together |
| NC | Readings judged "not connected" (see [Sensor status](#sensor-status)) |
| Alarms | Readings with an alarm flag set (red if any, green if none) |

Below the table:
- `<chip> intrusionN` - the chassis intrusion switch: "Closed", or "Intrusion latched (set since last
  clear, not live; ...)".
- `<chip> beep` - whether the chip's alarm beeper is Enabled or Disabled.
- Monitoring modules - which known sensor drivers are loaded.

**Healthy / typical values.** The author's PC has 13 chips. The NCT6799 has 13 temperatures, 7 fans,
18 voltages and 7 PWM outputs, with 9 not connected and 0 alarms. k10temp has 2 temperatures, zenergy 9
energy readings, amdgpu 3 temperatures, 1 fan, 1 voltage, 1 power, 1 PWM and 2 clocks. Each NVMe drive
has 3 temperatures. Modules: `nct6775 nct6775_core k10temp zenergy drivetemp spd5118 amdgpu ryzen_smu`.

**Unusual values and what they suggest.**
- Alarms above 0: open [Sensor status](#sensor-status) to see which reading.
- "Intrusion latched": the case-open flag was set at some point and nobody cleared it. It is not live.
  It is often set after a CMOS reset or on boards with no intrusion header wired. On the author's PC
  intrusion0 is latched and intrusion1 is Closed.
- A chip you expect is missing: its driver is not loaded (for example no `nct6775`, so no board fans).

**Where Linux gets it.** `/sys/class/hwmon/hwmon*` (all attributes), `intrusionN_alarm`, `beep_enable`,
`/proc/modules`.

**Background.** "hwmon" is the Linux hardware-monitoring framework. Every sensor driver puts its readings
in a folder `/sys/class/hwmon/hwmonN` using the same file names, so any tool can read any chip.

---
## IPMI dump
`ipmi-dump` - Not present on most PCs

**In plain words.** IPMI (Intelligent Platform Management Interface) is a server feature. A small
separate computer on the board, the BMC (Baseboard Management Controller), watches sensors and lets
admins manage the machine remotely. In SIV this item dumps the BMC's records. SRS only checks if a BMC is
there.

**What you see.** (All four IPMI items print the same check.)
- Status - "No IPMI BMC found on this system", or "IPMI device present (not opened)".
- /dev/ipmi0 - present / absent (the device file used to talk to the BMC).
- /sys/class/ipmi - present / absent.
- ipmi_si / ipmi_devintf - the two kernel modules, loaded / not loaded.
- ipmitool - installed / not installed.

**Healthy / typical values.** Desktop boards, like the author's, have no BMC: everything reads absent.

**Unusual values and what they suggest.** On a server where you expect a BMC: load `ipmi_si` and
`ipmi_devintf`, then use `ipmitool sdr` (sensor records) or `ipmitool sel list` (event log). SRS does not
open the device itself.

**Where Linux gets it.** `/dev/ipmi0`, `/dev/ipmi/0`, `/sys/class/ipmi`, `/sys/module/ipmi_*`.

---
## IPMI status
`ipmi-info` - Not present on most PCs

**In plain words.** In SIV: the BMC's identity and health. In SRS: the same "is there a BMC" check as
[IPMI dump](#ipmi-dump). "Not present" means the board has no server management controller, which is
normal for desktops.

---
## IPMI time
`ipmi-time` - Not present on most PCs

**In plain words.** In SIV: the BMC's own clock, used to time-stamp its event log. In SRS: the same
presence check as [IPMI dump](#ipmi-dump). On a server, `ipmitool sel time get` shows that clock.

---
## IPMI wait
`ipmi-wait` - Not present on most PCs

**In plain words.** In SIV: timing of requests to the BMC. In SRS: the same presence check as
[IPMI dump](#ipmi-dump). Nothing to wait for when there is no BMC.

---
## kWh cost
`kwh-cost` - Linux data

**In plain words.** Turns the power your parts are drawing right now into money: per hour, per day and
per year, as if the PC ran like this all the time.

**What you see.**
- Rate - price per kWh (kilowatt-hour). Default $0.1500. Set your own with the environment variables
  `SRS_KWH_COST` (for example `0.32`) and `SRS_CURRENCY` (for example `€`).
- A table: Component, Power (W), per Hour, per Day, per Year.
  - CPU line: the RAPL package power (`CPU package-0 (RAPL)`), or if there is none, the zenergy
    `Esocket` reading.
  - One line for each other hwmon power sensor (the GPU shows as `GPU PPT`).
- Total Power (measured) - the sum, with its cost.
- A note that only parts with power sensors are counted.

**Healthy / typical values.** On the author's PC at light load: CPU 49.60 W and GPU 36.00 W, total
85.60 W, which is $0.31 per day or $112.48 per year at $0.15/kWh.

**Unusual values and what they suggest.** The total is always lower than the wall socket reading. The
board, drives, fans, RAM and the power supply's own losses are not measured. A wall power meter shows the
real figure; expect it to be clearly higher than this total.

**Where Linux gets it.** `/sys/class/powercap/*/energy_uj` (RAPL), hwmon `energy*_input` (zenergy) and
`power*_input` / `power*_average` (for example amdgpu). Power is worked out from two energy readings
about 0.2 s apart.

**Background.** 1 kWh is 1000 W used for one hour. A PC drawing 100 W all day uses 2.4 kWh per day.

---
## LCD panel setup
`lcd-panel` - Not present on most PCs

**In plain words.** SIV can show its readings on small external screens, such as LCDSysInfo USB panels
or the screen on some AIO coolers. SRS does not drive such screens.

**What you see.** "No supported external LCD panel (LCDSysInfo / AIO LCD) found" and a note that case and
AIO displays are driven over USB HID by their own tools and are not probed.

**Healthy / typical values.** This message is the normal answer on every Linux PC.

---
## Super I/O LDNs
`ldn-info` - Linux data - Needs root data

**In plain words.** The Super I/O is a small chip on the motherboard that reads fans, voltages and
board temperatures. It is split into "logical devices". This item shows which chip you have, where its
hardware-monitor part lives, and what the kernel said when it found it.

**What you see.**
- Chip - the Super I/O driver name, for example `nct6799`.
- Platform device - the kernel device, for example `nct6775.656`. The number is the base address in
  decimal (656 = 0x290).
- HWM base (LDN 0x0B) - the I/O address of the hardware-monitor logical device, for example `0x0290`.
- hwmon - the sysfs folder, for example `/sys/class/hwmon/hwmon4`.
- Driver - `nct6775 + nct6775_core (loaded)` when the Nuvoton driver is in use.
- I/O ports - lines from the I/O port map that mention the chip or its address. Without root data the
  addresses are hidden (shown as zeros), so SRS uses the srs-root copy.
- Kernel log - the driver's start-up message (from the srs-root copy of the kernel log).
- A note that the LDN configuration registers are not read: SRS never does raw port I/O.

**Healthy / typical values.** On the author's PC: chip nct6799 at HWM base 0x0290; I/O ports
`0290-029f : PNP0C02:03`; kernel log `nct6775: Found NCT6796D-S/NCT6799D-R or compatible chip at 0x2e:0x290`.

**Unusual values and what they suggest.**
- "No Super-I/O hardware monitor driver bound": no fan or board readings at all. Load `nct6775` (Nuvoton)
  or `it87` (ITE). On some boards the firmware reserves the address range and the driver refuses to load;
  the common workaround is the kernel option `acpi_enforce_resources=lax`, used at your own risk.
- "Needs root: Menu > Run as root (srs-root)" under I/O ports or Kernel log: run srs-root once.

**Where Linux gets it.** `/sys/class/hwmon/*/device` (platform device), `/proc/ioports` or the srs-root
`ioports.txt`, the srs-root `dmesg.txt`, `/sys/module/nct6775`.

**Background.** The chip is reached through two "config" ports, 0x2E/0x2F (or 0x4E/0x4F). Each logical
device (serial port, keyboard, hardware monitor ...) has a number, the LDN (Logical Device Number). On
Nuvoton chips the hardware monitor is LDN 0x0B. The kernel log's `0x2e:0x290` means "config port 0x2E,
hardware monitor at 0x290". A `PNP0C02` entry is an ACPI "motherboard resources" device that reserves the range.

---
## AIO Link devices
`link-devices` - Not present on most PCs

**In plain words.** In SIV: the list of devices on an AIO / fan hub chain. In SRS: the same cooler driver
check as [AIO Link export](#aio-link-export). If a supported hub is found it is listed here as
`hwmonN - driver name`.

---
## AIO Link dump
`link-dump` - Not present on most PCs

**In plain words.** In SIV: a raw data dump from the hub. SRS never reads raw USB data from coolers, so
it shows the same driver check as [AIO Link export](#aio-link-export).

---
## AIO Link fans
`link-fans` - Not present on most PCs

**In plain words.** In SIV: fan speeds reported by the hub. In SRS: the same check as
[AIO Link export](#aio-link-export). Fans of a hub with a Linux driver appear in
[Cooling status](#cooling-status-fans).

---
## AIO Link LEDs
`link-leds` - Not present on most PCs

**In plain words.** In SIV: the hub's RGB lighting channels. In SRS: the same check as
[AIO Link export](#aio-link-export). For lighting on Linux, tools like OpenRGB or liquidctl are used.

---
## AIO Link limits
`link-limits` - Not present on most PCs

**In plain words.** In SIV: warning limits stored in the hub. In SRS: the same check as
[AIO Link export](#aio-link-export). Limits of a driver-supported hub show in the Limits column of
[Sensor status](#sensor-status).

---
## AIO Link power
`link-power` - Not present on most PCs

**In plain words.** In SIV: power drawn through the hub (and, for Corsair PSUs, PSU rails). In SRS: the
same check as [AIO Link export](#aio-link-export). A `corsair-psu` driver's readings would show in
[Power status](#power-status).

---
## AIO Link setup
`link-setup` - Not present on most PCs

**In plain words.** In SIV: settings for the hub link. SRS has nothing to set up and shows the same
check as [AIO Link export](#aio-link-export).

---
## AIO Link status
`link-status` - Not present on most PCs

**In plain words.** In SIV: live status of each linked device. In SRS: the same check as
[AIO Link export](#aio-link-export).

---
## AIO Link test
`link-test` - Not present on most PCs

**In plain words.** In SIV: a communication test with the hub. SRS never talks to the hub directly, so
it shows the same check as [AIO Link export](#aio-link-export).

---
## AIO Link update
`link-update` - Not present on most PCs

**In plain words.** In SIV: hub firmware details. SRS does not touch cooler firmware and shows the same
check as [AIO Link export](#aio-link-export). Use the maker's tool to update hub firmware.

---
## Power status
`power` - Linux data

**In plain words.** Every power and energy reading in the PC: the CPU's own energy counters, power
readings from drivers such as the GPU, and any batteries or other power supplies.

**What you see.** Three blocks:

1. **RAPL energy (powercap)** - one row per energy zone: zone name and folder, power now (W), total energy
   counted so far (J, joules), and any power limits the zone reports (`name N W`).
2. **hwmon power and energy** - one row per hwmon power, energy or current reading: chip and label,
   value, then either total energy (J) or the sysfs file name. GPU power rows also show
   `cap` (current power limit), `max` (highest limit allowed) and `default` (factory limit).
3. **Power supplies** - every entry in `/sys/class/power_supply`, with the same fields as
   [Battery](#battery) (mains adapters too, not only batteries).

**Healthy / typical values.** On the author's 7800X3D near idle: package-0 52.77 W, core 1.91 W.
zenergy shows each core at about 2-4 W and Esocket0 at 52.22 W. The Radeon shows `amdgpu PPT 35.00 W
cap 374 W max 374 W default 304 W` - the power limit has been raised from the 304 W default to the 374 W
maximum. The only power supply is a game controller battery.

**Unusual values and what they suggest.**
- Package power far above core power at idle is normal on Ryzen desktop chips: the I/O die, memory
  controller and cache use a lot of the package power.
- `cap` lower than `default` on a GPU: something (a tuning tool or a profile) has lowered the power
  limit, which can cut performance.
- "No powercap energy zones": the `intel_rapl` powercap driver is not loaded (it is also used for AMD).
- A watt value of `-`: the counter could not be read twice in time; open the item again.

**Where Linux gets it.** `/sys/class/powercap/*/energy_uj` and `constraint_N_*`; hwmon `power*_average`,
`power*_input`, `power*_cap*`, `energy*_input`, `curr*_input`; `/sys/class/power_supply`.

**Background.** RAPL (Running Average Power Limit) is a CPU feature that counts the energy used. The
counter only goes up; power is the change in energy divided by the time between two readings. Linux
names the zones `intel-rapl` even on AMD CPUs. zenergy is an extra (out-of-tree) driver that reads the
same AMD counters per core. PPT (Package Power Tracking) is AMD's name for the power the GPU or CPU is
allowed to draw.

---
## PWM export
`pwm-export` - Linux data

**In plain words.** A CSV (comma-separated values) snapshot of every fan control output, ready to paste
into a spreadsheet.

**What you see.** A header line, then one line per PWM output:

| Column | Meaning |
|---|---|
| hwmon | The hwmon folder (`hwmon4`) |
| chip | Full chip name (`nct6799-isa-0290`) |
| pwm | Output name (`pwm1`) |
| raw | Raw drive value 0-255 |
| duty_pct | Duty in % |
| enable | Control mode number (see [Cooling status](#cooling-status-fans)) |
| mode | 1 = PWM, 0 = DC |
| temp_sel | Number of the temperature input the fan follows |
| fan_rpm | Speed of the fan with the same number |

`-1` means the driver does not provide that value. A note says values are read only.

**Healthy / typical values.** From the author's PC: `hwmon4,nct6799-isa-0290,pwm1,133,52.2,5,1,8,955`
(52% duty, Smart Fan IV, PWM mode, following input 8, 955 RPM). The GPU line is
`hwmon9,amdgpu-pci-0300,pwm1,40,15.7,-1,-1,-1,554`: the amdgpu driver gives no mode or source here.

**Where Linux gets it.** hwmon `pwmN`, `pwmN_enable`, `pwmN_mode`, `pwmN_temp_sel`, `fanN_input`.

---
## QST status
`qst-status` - Not present on most PCs

**In plain words.** QST (Intel Quiet System Technology) was fan control built into the Intel
management engine on some Intel desktop boards around 2007-2010. SIV can read it. SRS reports
"Not present on this system" (Checked: n/a (Intel QST)). Modern boards, and all AMD boards, use the
Super I/O or the embedded controller instead.

---
## Sensor export
`sen-export` - Linux data

**In plain words.** A CSV snapshot of every sensor reading, for spreadsheets or logs.

**What you see.** Header `hwmon,chip,feature,label,value,unit,minimum,maximum,status`, then one line per
reading:
- hwmon, chip - where it comes from.
- feature - the hwmon name (`temp3`, `fan1`, `in0`, `energy9` ...).
- label - the name in quotes (driver label, lm-sensors label, a standard pin name, or the feature name).
- value - with 3 decimals, after any lm-sensors formula. Energy readings are exported as watts.
- unit - C, RPM, V, W, A, %, MHz.
- minimum, maximum - lowest and highest values seen since start or since **Zero**.
- status - `ok`, `alarm`, or `not-connected` (a floating or unwired input; these lines are greyed).

**Healthy / typical values.** From the author's PC:
`hwmon5,k10temp-pci-00c3,temp1,"Tctl",56.375,C,56.375,57.625,ok` and
`hwmon4,nct6799-isa-0290,temp3,"AUXTIN0",86.000,C,86.000,86.000,not-connected`.

**Unusual values and what they suggest.** Filter out `not-connected` lines before you graph or average
anything. They are not real temperatures (see [Sensor status](#sensor-status)).

**Where Linux gets it.** All of `/sys/class/hwmon/hwmon*`.

---
## Sensor status
`sensors` - Linux data

**In plain words.** The main sensor page. Every temperature, fan, voltage, power, energy, clock and PWM
reading from every chip, with the lowest and highest values seen, the chip's limits and a plain status.
It refreshes about once a second.

**What you see.**

A top line: "Minimum / Maximum since <time> (Zero resets)   Hide unused: on/off".

Then one block per chip. The chip line shows: driver name, full lm-sensors name, hwmon folder and the
kernel device path. Chips are sorted: Super I/O first, then CPU, GPU, memory, drives, everything else.
Inside each chip, readings are grouped under Temperatures, Fans, Voltages, Power, Energy, Currents,
Humidity, Clocks and PWM duty.

| Column | Meaning | Why it matters |
|---|---|---|
| Sensor | The label (from the driver, your lm-sensors config, a standard pin name, or just `in4`) | Tells you what it measures |
| Current | Value now (°C, RPM, V, W, A, %, MHz) | |
| Minimum / Maximum | Lowest / highest seen since SRS started or since **Zero** | Catches spikes you were not watching |
| Limits | The chip's own limits: `min`, `max`, `crit` (critical); `cap` for power; `total N J` for energy | What the chip or driver treats as too high |
| Status | See the list below | The quick answer |
| Source | The sysfs file read, plus `(compute)` if an lm-sensors formula was applied, or `(pin)` for a standard pin name | Lets you check the raw value yourself |

Status values, checked in this order:
- `not connected (...)` - the input is not wired to anything. Greyed. The reasons are explained below.
- `ignored (sensors.conf)` - your lm-sensors config says to ignore it. Greyed.
- `ALARM` (red) - the chip has set an alarm flag for this reading.
- `CRITICAL` (red) - at or above the `crit` limit.
- `HIGH` (orange) - above `max` (not used for power).
- `LOW` (orange) - below `min` (not used for temperatures).
- `unused` (grey) - reads exactly 0, for example an empty fan header.
- `OK` (green).

A voltage or fan alarm is ignored when the chip has no limits set (both 0). Those alarm flags mean nothing.

At the bottom: a count line ("13 chips, 79 sensors, 79 shown, 0 hidden, 9 not connected, 0 in alarm"),
plus notes explaining greyed rows and `(pin)` names.

**Zero** clears the Minimum / Maximum columns and restarts the "since" time.
**Hide** removes rows that are not connected, ignored, or reading 0.

**Healthy / typical values.** From the author's PC, near idle:
- CPU: k10temp Tctl 56.4°C, Tccd1 52.4°C (it has peaked at 57.6°C and 56.5°C). The Super I/O's TSI0_TEMP
  (56.8°C) is the same CPU temperature, read over the CPU's SB-TSI interface.
- Board: SYSTIN 22.0°C and CPUTIN 30.0°C, both with `max 80 crit 125`.
- Vcore 1.120 V (range 1.096-1.144 V), `+3.3V` 3.312 V, 3VSB 3.408 V, Vbat 3.344 V.
- GPU: edge 25.0°C, junction 29.0°C, memory 50.0°C, PPT 37.00 W (`cap 374`), sclk 311 MHz.
- DDR5 modules (spd5118): 31.0°C and 30.5°C, `max 55 crit 85`.
- NVMe Composite: 34.9, 37.9 and 39.9°C. SATA disk (drivetemp): 38.0°C. USB controller in the chipset
  (prom21_xhci): 51.9°C. Network chip PHY (r8169): 55.0°C, `max 120`.

**Unusual values and what they suggest.**
- **AUXTIN reading 66-86°C and never moving.** This confuses many people. See "Not connected inputs"
  below. On the author's board AUXTIN0 reads 86°C, AUXTIN3 84°C and AUXTIN4 70°C. They are not real and
  nothing is overheating.
- `PCH_...` temperatures at 0.0°C: AMD boards have no Intel-style PCH (Platform Controller Hub) feeding
  this chip, so these read 0. SRS marks them "not connected (PCH reading 0)".
- CPU temperature (Tctl) above 90°C under normal load: check the cooler mounting, paste and fan curve.
  Ryzen 7000 chips are designed to run up to 89-95°C under full load, but not at light load.
- GPU junction more than about 20-25°C above edge under load: possible poor paste or pad contact.
- NVMe Composite near its `max`: the drive will slow itself down (thermal throttle). Add a heatsink or airflow.
- Vcore that never drops at idle: power saving may be off in the BIOS.
- Vbat below about 3.0 V: the CMOS coin cell is getting weak. While the PC is on, some boards feed
  this input from standby power, so a good reading does not prove the coin cell is fine.
- A plain `inN` voltage with an odd value (0.344 V, 1.472 V): see "Voltages" below.

**Not connected inputs (floating AUXTIN).** A Super I/O chip has many temperature inputs. Each one
is built for a thermistor: a small part whose resistance changes with heat. The chip measures the
voltage at the pin and turns it into °C using a fixed table. The board maker decides which inputs get a
real thermistor. The rest are left unwired. An unwired pin is "floating": its voltage drifts to some
level set by leakage and noise, not by heat. The chip still turns that voltage into a temperature, so you
get a number that looks real but is not. Signs of a floating input:
- It stays the same, or nearly so: Minimum = Current = Maximum, even under heavy load.
- It does not match anything nearby (86°C while the board and CPU are at 22-30°C).
- It may be very low (1-12°C, colder than the room) instead of high.
- Its alarm flag may be set because the fake value is above the 80°C `max`. Tools such as `sensors`
  then show ALARM. SRS shows it greyed as not connected and does not count it as an alarm.

SRS marks these as follows:
- `probably not connected (floating high)` - an AUXTIN input reading more than 25°C above the
  board's own SYSTIN / CPUTIN readings (a real probe on a board header reads near those). On the
  author's ASUS TUF GAMING X870-PRO these are AUXTIN0, AUXTIN3 and AUXTIN4 of the NCT6799 (66-86°C
  while SYSTIN / CPUTIN read 22-29°C).
- `not connected (floating low)` - any AUXTIN input reading below 15°C (AUXTIN1, AUXTIN2, AUXTIN5 here).
- `not connected (implausible)` - any Super I/O temperature at or below 0°C or at or above 120°C.
- `not connected (PCH reading 0)` - PCH inputs reading exactly 0.

If you plug a probe into the board's optional thermistor header (often labelled T_SENSOR), one AUXTIN
input will start to follow real temperatures and is no longer greyed. To make sure SRS treats an input
as connected, give it a label in `/etc/sensors.d/` (inputs labelled there are never marked floating).

**Voltages and `(pin)`.** The Nuvoton driver gives voltage inputs no names. SRS names the five that are
the same on every NCT67xx chip: in0 Vcore, in2 AVCC, in3 +3.3V, in7 3VSB, in8 Vbat, marked `(pin)`. The
other `inN` inputs are wired differently on each board. The chip can only measure small voltages (about
2 V at most), so rails such as +12 V and +5 V reach it through a resistor divider. SRS shows the raw pin
voltage and does not apply the divider. An lm-sensors `compute` line can scale it, and the result shows
as `(compute)`.

**Where Linux gets it.** Every `/sys/class/hwmon/hwmon*` folder: `*_input`, `*_label`, `*_min`, `*_max`,
`*_crit`, `*_alarm`, `power*_average`, `power*_cap`, `energy*_input` (sampled to watts), `pwmN`; labels,
formulas and ignores from `/etc/sensors3.conf` and `/etc/sensors.d/*`.

**Background.**
- Tctl is the AMD "control" temperature that the CPU uses for fan and boost decisions. Tccd1 is the
  temperature of the first CCD (core complex die, the chip with the cores).
- "PECI/TSI Agent 0 Calibration" is the CPU temperature as the Super I/O sees it, after the board's own
  adjustment. Board fan curves usually follow it. SB-TSI is AMD's sideband temperature interface.
- GPU edge is the die's edge sensor, junction is the hottest spot on the die, and mem is the video memory.
- NVMe Composite is the drive's own summary temperature. Sensor 1 and 2 are extra sensors (often the
  controller and the flash).

---
## SIO debug
`sio-dbg` - Linux data - Needs root data

**In plain words.** A raw list of every value the Super I/O driver exposes, in the driver's own units.
It is the "under the bonnet" view behind [Sensor status](#sensor-status) and
[Cooling status](#cooling-status-fans).

**What you see.**
- Chip and Source - the chip name and its hwmon folder.
- A note: these are register-derived driver values, read only. Raw register dumps would need
  `/dev/port` and are not done.
- Then one `name  value` line for every file in the folder, sorted by name. Raw units: milli-degrees
  (`46000` = 46°C), millivolts (`1120` = 1.120 V), RPM, PWM 0-255, times in milliseconds.

The main families:

| Attribute | Meaning |
|---|---|
| `tempN_input`, `_label` | Temperature and its name |
| `tempN_max`, `_max_hyst`, `_crit` | Warning limit, the value it must fall back below to clear, critical limit |
| `tempN_offset` | Correction added by the chip |
| `tempN_type` | Sensor type; `4` = thermistor |
| `tempN_alarm`, `_beep` | Alarm flag; whether the alarm beeps |
| `inN_input`, `_min`, `_max`, `_alarm`, `_beep` | Voltage inputs and their limits |
| `fanN_input`, `_min`, `_pulses`, `_target`, `_tolerance`, `_alarm`, `_beep` | Fan speed, low limit, tacho pulses per turn, target and allowed error for speed modes |
| `pwmN`, `_enable`, `_mode`, `_temp_sel` | Drive value, control mode, PWM/DC, temperature source |
| `pwmN_auto_pointN_temp`, `_pwm` | The fan curve points |
| `pwmN_floor`, `_start`, `_stop_time` | Lowest duty allowed, duty used to start a stopped fan, delay before stopping |
| `pwmN_step_up_time`, `_step_down_time` | How fast the duty may change |
| `pwmN_target_temp`, `_temp_tolerance`, `_crit_temp_tolerance` | Settings for Thermal Cruise mode |
| `pwmN_weight_*` | A second temperature that can add to the fan duty |
| `intrusionN_alarm`, `_beep` | Case-open switch |
| `beep_enable`, `name` | Master beeper switch; chip name |

**Healthy / typical values.** On the author's NCT6799: `temp1_label SYSTIN`, `temp1_input 22000`,
`temp1_max 80000`, `temp1_type 4`; `pwm1 131`, `pwm1_enable 5`, `pwm1_temp_sel 8`;
`pwm1_auto_point4_temp 45000` with `pwm1_auto_point4_pwm 127`; `fan1_pulses 2`; `in0_input 1120`,
`in0_max 1744`.

**Unusual values and what they suggest.**
- `in10_alarm 1` to `in16_alarm 1` with `_min 0` and `_max 0`: the BIOS set no limits for these inputs, so
  the chip flags them. These alarms mean nothing, and Sensor status ignores them.
- `temp3_alarm 1` and `temp6_alarm 1`: the floating AUXTIN0 and AUXTIN3 inputs read above their 80°C
  `max`. Again not real.
- `fanN_target` values look odd unless that fan is in mode 3 (Fan Speed Cruise). The chip only uses them
  in that mode.
- `fanN_pulses` wrong for your fan (most fans give 2 pulses per turn): the RPM reading will be double or half.

**Where Linux gets it.** Every readable file in the Super I/O's `/sys/class/hwmon/hwmonN` folder.

---
## Synthetic sensors setup
`synthetic` - Linux data

**In plain words.** SIV lets you build your own "computed" sensors. On Linux the matching feature is
the lm-sensors `compute` line. This item lists every `compute` line in your config and, if the chip is
present, shows the raw and computed value side by side.

**What you see.**
- Config files - which files were read (`/etc/sensors3.conf`, `/etc/sensors.d/*`), or "none".
- A table:

| Column | Meaning |
|---|---|
| Source | File and line, for example `sensors3.conf:233` |
| Chip match | The chip pattern from the `chip` line (`...` if there are more), or the real chip name if it matches one in your PC |
| Feature | The reading it applies to (`in6`) |
| Formula (read, write) | The two formulas; `@` is the raw value |
| Raw | The raw reading (only for present chips) |
| Computed | The result, or `n/a` if the formula could not be worked out |

- A summary: number of compute statements and the list of chips present.
- A note: greyed rows are for chips not in this PC. Add your own in `/etc/sensors.d/<name>.conf`.

**Healthy / typical values.** On the author's PC: 19 compute statements from the stock
`/etc/sensors3.conf`, all for other chips (pc87365, w83627thf, nct6106, f71808e ...), so all rows are greyed
and Raw / Computed are empty.

**Unusual values and what they suggest.** Your own line shows greyed: the `chip` pattern does not match.
Use the full names from the summary line or from [Sensor formula setup](#sensor-formula-setup).

**Where Linux gets it.** `/etc/sensors3.conf` (or `/etc/sensors.conf`) and `/etc/sensors.d/*`, applied to
`/sys/class/hwmon` readings.

**Background.** Example for a +12 V rail measured through a divider that divides by 12:
`chip "nct6799-*"` then `compute in4 @*12, @/12` and `label in4 "+12V"`. Find the right factor in your
board's documentation or by comparing with the BIOS hardware monitor.

---
## Thermal zones
`thermal-zones` - Not present on most PCs

**In plain words.** Thermal zones are temperature areas defined by the firmware (ACPI), mostly on laptops.
Each zone has trip points: temperatures where the system slows down, starts a fan, or shuts down.

**What you see.**
- One row per zone: zone name (`thermal_zone0`), type, temperature, and the governor policy that acts on it.
- Under each zone, its trip points: type (`active`, `passive`, `hot`, `critical`) and temperature.
- If there are none: "No ACPI / thermal_zone* zones exposed by this firmware".
- Cooling devices - how many cooling devices exist (details in [Cooling status](#cooling-status-fans)).
- Checked - `/sys/class/thermal`.

**Healthy / typical values.** Many desktop boards have no zones, like the author's PC ("No ACPI /
thermal_zone* zones", 16 cooling devices). That is normal; the CPU protects itself anyway.

**Unusual values and what they suggest.** On a laptop, a `critical` trip only a few degrees above the
current temperature means the machine is about to shut down to protect itself.

**Where Linux gets it.** `/sys/class/thermal/thermal_zone*` (type, temp, policy, trip_point_N_type/temp).

---
## UPS export
`ups-export` - Not present on most PCs

**In plain words.** A UPS (Uninterruptible Power Supply) is a battery box that keeps the PC running in a
power cut. SIV can export UPS data. SRS shows which UPS tools are installed and any UPS or battery that
Linux reports.

**What you see.**
- apcupsd (apcaccess) - installed (not queried) / not installed.
- Network UPS Tools (upsc) - installed (not queried) / not installed.
- Battery and UPS entries from `/sys/class/power_supply`, with the same fields as [Battery](#battery).
- "No UPS data to export".

**Healthy / typical values.** On the author's PC: apcupsd is installed but not queried, NUT is not
installed, and the only entry is a game controller battery.

**Unusual values and what they suggest.** You have a USB UPS but no entry shows: many UPS units are
handled only by apcupsd or NUT and do not appear in `/sys/class/power_supply`. Ask those tools directly
(`apcaccess status` or `upsc <name>`).

**Where Linux gets it.** `/usr/bin/apcaccess`, `/usr/bin/upsc` (presence only), `/sys/class/power_supply`.
