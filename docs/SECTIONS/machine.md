# Machine / BIOS / Memory  (SRS Section Reference)

[Back to the section index](README.md) - [Glossary](../GLOSSARY.md)

This group describes the computer itself rather than any one add-in card: the motherboard and its
firmware (BIOS / UEFI), the memory sticks and how memory is laid out, the ACPI tables and devices
the firmware hands to the operating system, the TPM security chip and the CMOS clock. Open it in
SRS with **Menu -> Machine / BIOS / Memory -> item**, or use the **Machine** panel button and its
sub-buttons **DIMMs** and **Status**. Most items work as a normal user. Items marked "Needs root
data" are only complete after you run **Menu > Run as root (srs-root)** once. That helper saves
four read-only copies that SRS then reads: `dmidecode.txt` (full SMBIOS data: serials, UUID, CPU
socket), `spd.txt` (memory-stick SPD decode), `acpi-tables/` (copies of the ACPI tables) and
`iomem.txt` (the I/O memory map). The item [Machine options](#machine-options) shows which of
these sources are present on your PC.

All examples come from the author's PC: an ASUS TUF GAMING X870-PRO WIFI7 W NEO board with an
AMD Ryzen 7 7800X3D, 2 x 16 GB DDR5 and BIOS 1805.

### A few words first

Some ideas come up again and again in this group. Read these once and the rest is easier.

- **ACPI** (Advanced Configuration and Power Interface) is how the firmware describes the board to
  the operating system: which devices exist, how to power them on and off, how interrupts are
  wired. It comes as a set of binary **tables**, each with a four-letter signature such as `DSDT`
  or `APIC`.
- The **ACPI namespace** is a tree of named objects built from those tables. Paths look like
  `\_SB_.PCI0.GPP0` ("System Bus, PCI root 0, general purpose port 0"). Each device has a
  **HID** (hardware ID) such as `PNP0A08` (a PCI Express root bridge) or `AMDI0030` (an AMD GPIO
  controller).
- **_STA** is the ACPI "status" value of a device. SRS prints it as a hex number plus four
  letters: **P** present, **E** enabled, **S** shown in the user interface, **F** functioning.
  `0x0F PESF` means all four are true. `0x00 ----` means the firmware hides the device (normal for
  unused controllers). A `-` means the device has no _STA method, which the operating system
  treats as "present and working".
- **SMBIOS** (System Management BIOS), also called **DMI** (Desktop Management Interface), is a
  second firmware table set. It is a plain inventory: board name, BIOS version, memory slots,
  serial numbers. Linux shows most of it in `/sys/class/dmi/id`; the serial numbers and some
  detail are root-only.
- **UEFI** (Unified Extensible Firmware Interface) is the modern replacement for the old PC
  BIOS. Most people still say "BIOS" for both.
- **srs-root cache**: root-only data that the srs-root helper copied for SRS. SRS itself never
  runs as root and never writes to hardware.

## Contents
- [ACPI PCI root buses](#acpi-pci-root-buses) `acpi-buses`
- [ACPI devices](#acpi-devices) `acpi-dev`
- [ACPI embedded controller](#acpi-embedded-controller) `acpi-ec`
- [ACPI method evaluation](#acpi-method-evaluation) `acpi-eval`
- [ACPI HAL](#acpi-hal) `acpi-hal`
- [ACPI interrupts and GPEs](#acpi-interrupts-and-gpes) `acpi-int`
- [ACPI IPF (Intel Platform Framework) methods](#acpi-ipf-intel-platform-framework-methods) `acpi-ipf`
- [ACPI ICD0001 methods](#acpi-icd0001-methods) `acpi-itm`
- [ACPI methods](#acpi-methods) `acpi-methods`
- [ACPI extension (PDX)](#acpi-extension-pdx) `acpi-pdx`
- [ACPI PCI buses and ports](#acpi-pci-buses-and-ports) `acpi-ports`
- [ACPI power resource methods](#acpi-power-resource-methods) `acpi-pwr`
- [ACPI SPB (serial peripheral bus) methods](#acpi-spb-serial-peripheral-bus-methods) `acpi-spb`
- [ACPI tables](#acpi-tables) `acpi-tables`
- [ACPI Toshiba test](#acpi-toshiba-test) `acpi-test`
- [ACPI TFP INT3403 methods](#acpi-tfp-int3403-methods) `acpi-tfp`
- [ACPI TMS INT340E methods](#acpi-tms-int340e-methods) `acpi-tms`
- [ACPI TOS1900 methods](#acpi-tos1900-methods) `acpi-tos`
- [ACPI TPM](#acpi-tpm) `acpi-tpm`
- [ACPI WMI (PNP0C14) methods](#acpi-wmi-pnp0c14-methods) `acpi-wmi`
- [APIC (MADT) summary](#apic-madt-summary) `bios-apic`
- [ASPT table summary](#aspt-table-summary) `bios-aspt`
- [DMAR table summary](#dmar-table-summary) `bios-dmar`
- [FACP (FADT) summary](#facp-fadt-summary) `bios-facp`
- [PCI IRQ routing ($PIR)](#pci-irq-routing-pir) `bios-irq`
- [BIOS maker](#bios-maker) `bios-make`
- [MPS table summary](#mps-table-summary) `bios-mps`
- [MSDM table summary](#msdm-table-summary) `bios-msdm`
- [OEMB table summary](#oemb-table-summary) `bios-oemb`
- [BIOS option ROMs](#bios-option-roms) `bios-option`
- [SDEV table summary](#sdev-table-summary) `bios-sdev`
- [SFI summary](#sfi-summary) `bios-sfi`
- [SLIC table summary](#slic-table-summary) `bios-slic`
- [SLIT table summary](#slit-table-summary) `bios-slit`
- [SRAT table summary](#srat-table-summary) `bios-srat`
- [BIOS / SMBIOS tables](#bios--smbios-tables) `bios-tables`
- [CMOS / RTC status](#cmos--rtc-status) `cmos-status`
- [Dell BIOS](#dell-bios) `dell-bios`
- [DIMMs](#dimms) `dimms`
- [DIMM status/temperatures](#dimm-statustemperatures) `dimm-status`
- [Firmware](#firmware) `firmware`
- [Firmware interface table](#firmware-interface-table) `f-w-table`
- [Hardware summary](#hardware-summary) `hardware`
- [HECI (Intel ME) status](#heci-intel-me-status) `heci-status`
- [Machine](#machine) `machine`
- [Machine options](#machine-options) `machine-opt`
- [Memory](#memory) `memory`
- [Memory timings](#memory-timings) `mem-tweak`
- [Overview](#overview) `overview`
- [Physical memory map](#physical-memory-map) `physical`
- [SPD (DIMMs)](#spd-dimms) `spd`
- [SPD thermal](#spd-thermal) `spd-temp`
- [System CMOS](#system-cmos) `system-cmos`
- [System EBDA](#system-ebda) `system-ebda`
- [BIOS IDs](#bios-ids) `system-ids`
- [TPM device](#tpm-device) `tpm-device`
- [UEFI data](#uefi-data) `uefi-data`
- [UEFI information](#uefi-information) `uefi-info`

---
## ACPI PCI root buses  
`acpi-buses` - Linux data

**In plain words.** Shows where the PCI Express "tree" of the PC starts, and how every PCI bus
hangs off a bridge that the firmware has named in ACPI.

**What you see.** Two tables.

First table, one row per PCI root bridge (HID `PNP0A08`, `PNP0A03` or `ACPI0016`):

| Field | Meaning | Why it matters |
|---|---|---|
| Device | ACPI device name, e.g. `PNP0A08:00` | `PNP0A08` = PCI Express root bridge, `PNP0A03` = older PCI root |
| Path | ACPI namespace path, e.g. `\_SB_.PCI0` | The name the firmware's own code uses for it |
| UID | Unique ID inside that HID | Tells several root bridges apart (servers often have more than one) |
| Physical node | The Linux device it became, e.g. `pci0000:00` | Links the ACPI view to the Linux PCI view |
| Status | `_STA` value and letters | `0x0F PESF` = present and working |

Second table, one row per PCI bus Linux knows (from `/sys/class/pci_bus`):

| Field | Meaning | Why it matters |
|---|---|---|
| PCI Bus | Domain and bus number, e.g. `0000:05` | Bus numbers are what `lspci` addresses start with |
| Bridge | The PCI bridge that creates this bus, or `root` | Shows the parent of each bus |
| Bridge ACPI path | ACPI name of that bridge, e.g. `\_SB_.PCI0.GPP7` | Ties a bus to a named slot or chipset port |
| Bus range | Secondary-subordinate bus numbers behind the bridge, e.g. `05-0C` | Everything below that bridge lives in this range |

**Healthy / typical values.** One root bridge `PNP0A08:00 \_SB_.PCI0 ... 0x0F PESF` on a desktop.
On the author's board the chipset hangs off `GPP7` (buses `05-0C`), the USB4 controller off `GPP8`
(buses `0D-74`, a big range reserved for hot-plugged Thunderbolt devices) and the graphics card off
`GPP0` through two extra switch levels (`SWUS`, `SWDS`).

**Unusual values and what they suggest.**
- A bridge with an empty ACPI path: the firmware did not describe it. Usually harmless, but power
  management and hot-plug for that branch may then fall back to generic code.
- A very wide bus range (like `0D-74`) on a USB4 / Thunderbolt port is normal: the firmware
  reserves bus numbers for devices you might plug in later.
- More than one root bridge on a desktop: rare; normal on multi-socket servers.

**Where Linux gets it.** `/sys/bus/acpi/devices/PNP0A08:*` (path, uid, status, physical_node) and
`/sys/class/pci_bus/*` plus `/sys/bus/pci/devices/*/firmware_node` and `secondary_bus_number` /
`subordinate_bus_number`.

**Background.** A **PCI bridge** (shown elsewhere as "PtP", PCI-to-PCI) creates a new bus number
behind it. Every slot, M.2 socket and chipset port is really a bridge. The GPU's own switch chips
(`SWUS` = switch upstream, `SWDS` = switch downstream) are why a graphics card often appears three
bus numbers deep.

---
## ACPI devices  
`acpi-dev` - Linux data

**In plain words.** The full list of devices the firmware describes in ACPI, with their status
and which Linux driver picked each one up.

**What you see.** One row per ACPI device, sorted by name:

| Field | Meaning | Why it matters |
|---|---|---|
| Device | Linux name, HID plus instance, e.g. `AMDI0030:00` | Unique key for the detail view |
| HID | Hardware ID, e.g. `PNP0C14` (WMI), `MSFT0101` (TPM 2.0) | Tells you what kind of device it is |
| _STA | Status hex value + P/E/S/F letters, or `-` if no _STA method | Whether the firmware says it is present and working |
| Path | ACPI namespace path, e.g. `\_SB_.PCI0.SBRG.EC0_` | The firmware's name for it |
| Physical node / driver | The Linux device and driver bound to it | Empty driver = nothing in Linux claimed it |

The footer gives the device count and repeats the meaning of the _STA letters.

**Detail view** (`acpi-dev:<device>`, e.g. `acpi-dev:PNP0C09:00`): every attribute Linux exposes
for that device, when present: `path`, `hid`, `modalias`, `uid`, `adr` (bus address), `status`,
`description`, `power_state`, `real_power_state`, `resource_in_use`, then `physical_node`,
`driver`, and one `child` line per child device.

**Healthy / typical values.** On the author's PC: 258 ACPI devices. The 16 `ACPI0007` rows are the
CPU threads (`\_SB_.PLTF.C000` to `C00F`, bound to `processor`). `PNP0A08:00` is the PCI root,
`PNP0B00:00` the CMOS clock (`rtc_cmos`), `PNP0C09:00` the embedded controller (`acpi-ec`),
`MSFT0101:00` the TPM (`tpm_crb_acpi`), `AMDI0030:00` the GPIO controller (`amd_gpio`).
Many rows show `0x00 ----`: spare I2C, I3C and UART controllers the board does not use.

**Unusual values and what they suggest.**
- `0x0B PE-F` (present, enabled, not shown, functioning) is normal for interrupt link devices
  (`PNP0C0F`, `LNKA`..`LNKH`) and the power button.
- A device you expect (for example a touchpad on a laptop) that shows `0x0F PESF` but has no
  driver: Linux is missing a driver or a kernel option for it.
- A device with `0x00 ----` that you expected to work: it may be switched off in the BIOS setup.

**Where Linux gets it.** `/sys/bus/acpi/devices/*` (`hid`, `status`, `path`, `physical_node`,
`physical_node/driver`).

**Background.** HID prefixes tell you who defined the ID: `PNP` and `ACPI` are industry-standard,
`LNX` are Linux-internal pseudo-devices (`LNXSYSTM` is the root, `LNXPOWER` a power resource),
and vendor IDs such as `AMDI` (AMD), `MSFT` (Microsoft), `RTK` (Realtek) and `ASMT` (ASMedia)
are listed on their own in [ACPI extension (PDX)](#acpi-extension-pdx).

---
## ACPI embedded controller  
`acpi-ec` - Linux data - Needs root data

**In plain words.** The embedded controller (EC) is a small helper microcontroller on the board.
On laptops it runs the keyboard, battery and fans; on desktops it is usually much simpler. This
item shows whether ACPI describes one and how Linux talks to it.

**What you see.**

| Field | Meaning | Why it matters |
|---|---|---|
| `PNP0C09:00` (first line) | ACPI path of the EC | Confirms an EC is described |
| _STA | Status of the EC | `-` = no _STA method, treated as present |
| UID | Unique ID | Usually `-` or `0` |
| Driver | Linux driver | `acpi-ec` is the normal one |
| ec_storm_threshold | How many EC events in a row before Linux switches to polling | Protects against "GPE storms" from buggy firmware |
| ec_busy_polling | Whether Linux busy-waits for EC replies (`Y`/`N`) | `Y` is a workaround on some laptops |
| ec_max_queries | How many EC queries may be outstanding | Tuning value, default 16 |
| ec_sys module | Whether the `ec_sys` debug module is loaded | Needed for an EC register dump |
| EC register dump | Not read: root-only debugfs file, not in the srs-root cache | SRS never reads EC registers |

If there is no EC, the item says "No ACPI EC (PNP0C09)".

**Healthy / typical values.** On the author's desktop: `\_SB_.PCI0.SBRG.EC0_`, driver `acpi-ec`,
storm threshold 8, busy polling N, max queries 16, `ec_sys` not loaded.

**Unusual values and what they suggest.** No EC on a laptop is very unusual and means battery and
lid events will not work. Changed `ec_*` values usually come from a kernel command-line workaround
(`acpi.ec_*=`); check your boot loader settings.

**Where Linux gets it.** `/sys/bus/acpi/devices/PNP0C09:00` and
`/sys/module/acpi/parameters/ec_*`. A register dump would need `/sys/kernel/debug/ec/ec0/io`
(root, `ec_sys` module).

---
## ACPI method evaluation  
`acpi-eval` - Linux data - Needs root data

**In plain words.** In the Windows original this item could run ("evaluate") ACPI methods. SRS
is strictly read-only, so on Linux it only reports whether the tool that could do this is
loaded, and points you to the read-only alternatives.

**What you see.**
- **acpi_call** - whether the third-party `acpi_call` kernel module is loaded (`/proc/acpi/call
  present (root)` or `not loaded`).
- **Evaluation** - always "disabled": running an ACPI method means writing to the kernel, and SRS
  never writes.
- **Read-only alternative** - [ACPI methods](#acpi-methods) for the list of methods, and
  [ACPI devices](#acpi-devices) for _STA, _HID, _UID and _ADR values.

**Where Linux gets it.** Checks for `/proc/acpi/call`.

**Background.** An ACPI **method** is a small program written in AML (ACPI Machine Language)
inside the firmware tables. Running the wrong one can switch off a device or the whole PC, which
is why SRS does not do it.

---
## ACPI HAL  
`acpi-hal` - Linux data

**In plain words.** A summary of the ACPI layer as Linux sees it: what kind of machine the
firmware claims to be, how many tables and devices there are, and the ACPI driver's settings.
(HAL, Hardware Abstraction Layer, is the Windows name for this layer.)

**What you see.**

| Field | Meaning | Why it matters |
|---|---|---|
| Preferred PM Profile | Machine type the firmware reports: Desktop, Mobile, Workstation, Server, Tablet... | Linux and Windows pick power defaults from it |
| ACPICA Version | Version of the ACPI interpreter built into the kernel | Newer versions fix firmware quirks |
| ACPI Tables | Number of tables (including dynamically loaded ones) | See [ACPI tables](#acpi-tables) |
| ACPI Devices | Number of ACPI namespace devices | See [ACPI devices](#acpi-devices) |
| SCI Interrupts | Total System Control Interrupts since boot | ACPI's own interrupt line |
| GPE Events | Total General Purpose Events since boot | Wake and hot-plug signals |
| Hardware-reduced ACPI | `No (fixed hardware present)` or `Yes / unknown` | Hardware-reduced is used by ARM and some tablets |
| Firmware Performance (FPDT) | Whether a boot-timing table exists | Used by [Firmware](#firmware) |
| Hotplug ... | One line per ACPI hot-plug type (memory, pci_root, processor) | Enabled = Linux will react to hot-add events |
| acpi.* | Every ACPI driver parameter and its value | Shows kernel command-line tweaks |

**Healthy / typical values.** Author's PC: `1 (Desktop)`, ACPICA 20260408, 30 tables, 258 devices,
3512 SCI interrupts, "No (fixed hardware present)", FPDT present, all three hot-plug types enabled.

**Unusual values and what they suggest.**
- PM profile `2 (Mobile)` on a desktop board, or `0 (Unspecified)`: lazy firmware; power defaults
  may be odd.
- SCI and GPE counts that rise by thousands per second: an "interrupt storm", often a buggy GPE.
  Look at [ACPI interrupts and GPEs](#acpi-interrupts-and-gpes) to find which one.

**Where Linux gets it.** `/sys/firmware/acpi/pm_profile`, `/sys/firmware/acpi/interrupts/`,
`/sys/firmware/acpi/hotplug/`, `/sys/module/acpi/parameters/`.

---
## ACPI interrupts and GPEs  
`acpi-int` - Linux data

**In plain words.** Counts how often each ACPI event has fired since boot: the power button, the
real-time clock alarm, and the General Purpose Events (GPEs) that devices use to wake the PC or
signal hot-plug.

**What you see.**

| Field | Meaning | Why it matters |
|---|---|---|
| Event | Event name: `ff_*` fixed events, `gpeXX` numbered GPEs, `sci`, `gpe_all`, `error`, `sci_not` | Which signal |
| Count | How many times it fired since boot | Shows busy or stuck events |
| State | Kernel state words: `EN` enabled, `STS` status bit set, `enabled`/`disabled`, `masked`/`unmasked`, `invalid` | Whether Linux is listening to it |

Then: how many idle (count 0 and not enabled) GPEs were hidden, and a line `SCI IRQ n: total
interrupts (all CPUs)` from `/proc/interrupts`.

**Healthy / typical values.** Author's PC: `gpe04` 3506, `gpe1B` 6, everything else 0;
`sci` and `gpe_all` 3512; `SCI IRQ 9`. `ff_pwr_btn` enabled (the power button works),
`ff_rt_clk` disabled (no RTC alarm set). 27 idle GPEs hidden.

**Unusual values and what they suggest.**
- One GPE with a huge, fast-rising count: a GPE storm. It costs CPU time and battery. The usual
  fix is a BIOS update or the kernel option `acpi_mask_gpe=0xNN`.
- `error` above 0: the ACPI interpreter hit errors handling events; check `dmesg` for ACPI
  messages.
- `sci_not` above 0: SCIs arrived that no handler claimed.

**Where Linux gets it.** `/sys/firmware/acpi/interrupts/*` and the `acpi` line in
`/proc/interrupts`.

**Background.** The **SCI** (System Control Interrupt) is ACPI's single shared interrupt line,
usually IRQ 9. Each **GPE** is one bit in the chipset's event register; the firmware's `_Lxx` /
`_Exx` methods say what each bit means.

---
## ACPI IPF (Intel Platform Framework) methods  
`acpi-ipf` - Not present on most PCs

**In plain words.** Looks for Intel Platform Framework devices, Intel's newer laptop
power and thermal framework (the successor of DPTF, Dynamic Platform and Thermal Framework).

**What it would show.** One line per matching ACPI device with its path, then **Device**
(description), **Probed HIDs** (`INTC1040 INTC1041 INTC1042 INTC1043`) and **Status**
(`present` or `Not present on this system`).

**What "not present" means.** Normal on AMD PCs and on desktops. It only appears on recent Intel
laptops.

**Where Linux gets it.** Name match in `/sys/bus/acpi/devices`.

---
## ACPI ICD0001 methods  
`acpi-itm` - Not present on most PCs

**In plain words.** Looks for an ACPI device with ID `ICD0001`, a thermal / management device
found on a few OEM systems.

**What it would show.** Matching devices with their ACPI path, then Device, Probed HIDs
(`ICD0001`) and Status.

**What "not present" means.** Normal for nearly every PC.

**Where Linux gets it.** Name match in `/sys/bus/acpi/devices`.

---
## ACPI methods  
`acpi-methods` - Linux data - Needs root data

**In plain words.** Lists every method (small firmware program) defined in the main ACPI code
tables, the DSDT and the SSDTs.

**What you see.**

| Field | Meaning | Why it matters |
|---|---|---|
| Table | Which table holds the method: `DSDT`, `SSDT1`... | Tells you which firmware module defines it |
| Method | Method name, relative to its enclosing Scope, e.g. `_PRT`, `_STA`, `_CRS`, `WMBC` | Names starting with `_` are standard ACPI; others are vendor-private |
| Args | Number of arguments (0-7) | Part of the method's definition |
| Flags | `Serialized` or `NotSerialized`, plus `Sync` if a sync level is set | Serialized methods cannot run twice at once |

The footer gives the total. `acpi-methods:<SIG>` (e.g. `acpi-methods:SSDT6`) shows one table only.

**Healthy / typical values.** Hundreds to a few thousand rows. On the author's PC the DSDT alone
has many `_PRT` (PCI interrupt routing), `_STA`, `_CRS` (current resources), `_PRS`/`_SRS`/`_DIS`
(interrupt link handling), and vendor methods such as `WMBC` (an ASUS WMI method), `_PTS`/`_WAK`
(prepare to sleep / wake).

**Unusual values and what they suggest.** The list is made by scanning the table bytes for the AML
"Method" opcode, so an odd row (for example a standard name with an unexpected argument count)
can be a scan match inside data rather than a real method. For exact work use `acpidump` and
`iasl -d`. If the item says it needs the cached tables, run srs-root.

**Where Linux gets it.** The srs-root copies of `/sys/firmware/acpi/tables/DSDT` and `SSDT*`
(root-only on Linux).

**Background.** Common standard methods: `_STA` status, `_CRS` resources, `_PRW` wake
capability, `_PSx` / `_PRx` power states, `_OSC` operating-system capability handshake, `_PTS` and
`_WAK` sleep and wake hooks, `_Lxx` GPE handlers.

---
## ACPI extension (PDX)  
`acpi-pdx` - Linux data

**In plain words.** Lists the vendor-specific device IDs in the ACPI namespace, that is, every
device whose HID is not a standard `PNP`, `LNX` or `ACPI` one. These are the parts that need
vendor drivers.

**What you see.**

| Field | Meaning | Why it matters |
|---|---|---|
| HID | Vendor hardware ID, e.g. `AMDI0010` | First letters name the vendor |
| Count | How many devices share that HID | e.g. 4 I2C controllers |
| First path | ACPI path of the first one | Where it sits |
| Driver | Linux driver of the first one, or `-` | `-` = unclaimed (often disabled anyway) |

A note explains the prefixes: AMDI = AMD, MSFT = Microsoft, RTK = Realtek, ASMT = ASMedia.

**Healthy / typical values.** Author's PC: `AMDI0010` x4 (I2C, `i2c_designware`), `AMDI0030`
(GPIO, `amd_gpio`), `AMDI0101` (`amd_x3d_vcache`, the X3D cache-preference driver), `AMDIF031`
(`pt-gpio`), `MSFT0101` (TPM, `tpm_crb_acpi`), `RTK5439` and `RTK5452` (Realtek USB-C / USB4
power-delivery chips on the I2C bus).

**Unusual values and what they suggest.** A vendor HID with no driver may simply be disabled in
firmware (check its _STA in [ACPI devices](#acpi-devices)). If it is `0x0F PESF` and has no
driver, a feature (sensor, button, power profile) may not work until a newer kernel adds support.

**Where Linux gets it.** `/sys/bus/acpi/devices`.

---
## ACPI PCI buses and ports  
`acpi-ports` - Linux data

**In plain words.** Every PCI device that the firmware also describes in ACPI, matched to its
ACPI name. This tells you which physical port or slot a device is behind.

**What you see.**

| Field | Meaning | Why it matters |
|---|---|---|
| PCI | PCI address bus:device.function, e.g. `03:00.0` | Same as `lspci` |
| Class | PCI class code; `0604 PtP` marks a PCI-to-PCI bridge (a port) | 0300 = display, 0108 = NVMe, 0C03 = USB, 0200 = Ethernet |
| ID | Vendor:device ID, e.g. `1002:7550` | Identifies the chip |
| ACPI node | Linux name of the ACPI companion, e.g. `device:3e` | Links to [ACPI devices](#acpi-devices) |
| ACPI path | The firmware's name, e.g. `\_SB_.PCI0.GPP0.SWUS.SWDS.VGA_` | Shows the chain of ports down to the device |
| Driver | Linux driver bound to the PCI device | `pcieport` for ports |

Footer: number of PCI functions with an ACPI companion.

**Healthy / typical values.** Author's PC: 45 functions. The graphics card is
`\_SB_.PCI0.GPP0.SWUS.SWDS.VGA_` (`amdgpu`); NVMe drives sit at `...GPP7.UP00.DP00.NV00` and
`...DP40.EP00`; the 5 GbE network chip at `...DP30.EP00` (`r8169`); Wi-Fi at `...DP38.EP00`.

**Unusual values and what they suggest.** A PCI device missing from this list has no ACPI
companion. That is normal for some devices behind the chipset, but can mean ACPI power management
(D3cold, wake) will not work for it. Devices behind a port marked `GPP7.UP00` share the chipset
link to the CPU; heavy traffic on several of them at once shares that bandwidth.

**Where Linux gets it.** `/sys/bus/pci/devices/*/firmware_node` plus `class`, `vendor`, `device`
and `driver`.

---
## ACPI power resource methods  
`acpi-pwr` - Linux data

**In plain words.** Shows the ACPI "power resources" (switchable power rails the firmware can
turn on and off) and the ACPI power state of each device that supports power management.

**What you see.** Two tables.

| Field (power resources) | Meaning |
|---|---|
| Resource | `LNXPOWER:nn` |
| Path | ACPI path of the power resource, e.g. `\_SB_.PCI0.GPP8.PWRS` |
| In use | `on` if a device is currently using it, else `off` |
| Status | _STA of the resource (`0x01 P---` = on) |

| Field (devices) | Meaning |
|---|---|
| Device | ACPI device name |
| Path | ACPI path |
| power_state | State Linux believes: `D0` (fully on) to `D3hot` / `D3cold` (off) |
| real_power_state | State read back from the hardware, or `-` |

Footer: number of devices with ACPI power management (`_PSx` / `_PRx` methods).

**Healthy / typical values.** Author's PC: 14 power resources; the USB4 branch (`GPP8...`) all
`on`, the graphics-card branch resources (`M237`) `off`. The I2C controller sits in `D3hot`
(idle, powered down), the USB4 ports in `D0`.

**Unusual values and what they suggest.**
- A device stuck in `D0` that should idle (for example an unused USB4 controller) costs power.
  Check runtime power management settings (`power/control` in sysfs, or `powertop`).
- `power_state` and `real_power_state` disagreeing: the device did not reach the state Linux
  asked for; a common cause of failed suspend.

**Where Linux gets it.** `/sys/bus/acpi/devices/LNXPOWER:*` (`path`, `resource_in_use`, `status`)
and `/sys/bus/acpi/devices/*/power_state`, `real_power_state`.

**Background.** **D-states** are device power states: D0 on, D1/D2 light sleep (rare), D3hot
off but still powered, D3cold power fully removed.

---
## ACPI SPB (serial peripheral bus) methods  
`acpi-spb` - Linux data

**In plain words.** Lists the low-speed serial buses on the board (I2C, I3C, UART serial ports,
SPI) and the small devices on them, such as USB-C controllers or memory temperature sensors.

**What you see.** First table, ACPI serial-bus controllers and serial devices:

| Field | Meaning |
|---|---|
| Device | ACPI device (`AMDI0010` I2C, `AMDI0015` I3C, `AMDI0020` UART, `PNP0500`/`PNP0501` serial port, `RTK*` Realtek devices...) |
| Path | ACPI path |
| Driver | Linux driver or `-` |
| Physical node | Linux device it became |

Second table, every I2C adapter and client Linux knows:

| Field | Meaning |
|---|---|
| I2C | Linux name: `i2c-N` for a bus, `N-00AA` for a device at address 0xAA on bus N |
| Adapter / client name | e.g. `SMBus PIIX4 adapter port 0`, `spd5118`, `AMDGPU DM i2c hw bus 0` |
| ACPI path | ACPI companion if any |

Then one line per SPI device, and a note that SRS lists names only and never talks on these buses.

**Healthy / typical values.** Author's PC: one active I2C controller `\_SB_.I2CA`
(`i2c_designware`) with two Realtek chips on it (`U4PD`, `TYPC`, the USB4 / USB-C power
controllers); one active serial port `PNP0501:00` (`serial`); the chipset SMBus (`SMBus PIIX4`,
three ports) with two `spd5118` clients at `1-0051` and `1-0053`, which are the two DDR5 sticks'
temperature sensors; and many `AMDGPU` buses that belong to the graphics card's display outputs.

**Unusual values and what they suggest.** On DDR5 systems, no `spd5118` clients means the memory
temperature sensors are not visible (the driver is not loaded, or the board blocks SMBus access).
See [DIMM status/temperatures](#dimm-statustemperatures).

**Where Linux gets it.** `/sys/bus/acpi/devices`, `/sys/bus/i2c/devices/*/name`,
`/sys/bus/spi/devices`.

**Background.** **I2C** / **SMBus** (System Management Bus) is a two-wire bus for slow chips:
sensors, memory SPD chips, RGB controllers. Each chip has a 7-bit address; DDR5 memory sticks
answer at 0x50-0x57.

---
## ACPI tables  
`acpi-tables` - Linux data - Needs root data

**In plain words.** Lists every ACPI table the firmware provides, with its header: size, version
and who built it.

**What you see.**

| Field | Meaning | Why it matters |
|---|---|---|
| Table | Signature, e.g. `DSDT`, `SSDT3`, `APIC` (Linux numbers repeated tables) | What the table is |
| Length | Size in bytes | |
| Rev | Table revision | Newer revisions add fields |
| Check | Checksum byte | All bytes of a valid table add up to 0 |
| OEM | OEM ID, e.g. `ALASKA` (AMI's placeholder), `AMD` | Who supplied the table |
| OEM Table | OEM table ID, e.g. `A M I`, `AmdTable`, `CPMCMN` | Often names the firmware module |
| OEM Rev | OEM revision | |
| Creator | Tool that compiled it: `AMI`, `INTL` (Intel's iasl compiler), `MSFT`, `AMD` | |
| Creator Rev | Version of that tool | |

Footer: total tables. Without the srs-root cache only the name and file size are shown.
`acpi-tables:<SIG>` (e.g. `acpi-tables:HPET`) shows the parsed header of one table plus a hex
dump of up to 4 KB.

**Healthy / typical values.** Author's PC: 30 tables, among them `APIC` (CPU interrupt
controllers), `DSDT` (88990 bytes, the main ACPI code), `FACP`, `FACS`, `HPET` (timer), `IVRS`
(AMD IOMMU), `MCFG` (PCIe configuration space), 16 `SSDT`s from AMD modules, `TPM2`, `VFCT`
(a copy of the graphics card's video BIOS), `WPBT`, `WSMT`.

**Unusual values and what they suggest.**
- `WPBT` (Windows Platform Binary Table) lets firmware hand Windows a program to run at startup
  (on ASUS boards, the Armoury Crate installer). Linux ignores it. You can usually turn it off in
  the BIOS setup ("Download Armoury Crate app").
- `FACS` has no standard header, so its OEM and revision columns are not meaningful.
- A missing `MCFG` or `HPET` on a modern PC is rare and worth a BIOS update.

**Where Linux gets it.** `/sys/firmware/acpi/tables/` (root-only to read; SRS uses the srs-root
copies in `acpi-tables/`). Same data as `acpidump`.

**Background.** **DSDT** (Differentiated System Description Table) holds the main ACPI code;
**SSDTs** (Secondary System Description Tables) add more, often one per firmware module (CPU,
USB4, memory tools).

---
## ACPI Toshiba test  
`acpi-test` - Not present on most PCs

**In plain words.** Looks for any ACPI device whose ID starts with `TOS`, which Toshiba (now
Dynabook) laptops use.

**What it would show.** Matching devices with their ACPI path, then Device ("Toshiba laptop ACPI
devices"), Probed HIDs (`TOS`) and Status.

**What "not present" means.** You do not have a Toshiba laptop. Normal.

**Where Linux gets it.** Name match in `/sys/bus/acpi/devices`.

---
## ACPI TFP INT3403 methods  
`acpi-tfp` - Not present on most PCs

**In plain words.** Looks for Intel DPTF (Dynamic Platform and Thermal Framework) thermal
"participants", the temperature sensors and cooling devices of Intel laptops.

**What it would show.** Matching devices with their ACPI path, then Device ("Intel DPTF thermal
participant"), Probed HIDs (`INT3403 INTC1043`) and Status.

**What "not present" means.** Normal on AMD PCs and desktops.

**Where Linux gets it.** Name match in `/sys/bus/acpi/devices`.

---
## ACPI TMS INT340E methods  
`acpi-tms` - Not present on most PCs

**In plain words.** Looks for the Intel DPTF `INT340E` device (part of Intel's laptop thermal
management).

**What it would show.** Matching devices with their ACPI path, then Device, Probed HIDs
(`INT340E`) and Status.

**What "not present" means.** Normal on AMD PCs and desktops.

**Where Linux gets it.** Name match in `/sys/bus/acpi/devices`.

---
## ACPI TOS1900 methods  
`acpi-tos` - Not present on most PCs

**In plain words.** Looks for the Toshiba laptop "extras" device that controls hotkeys,
backlight and fan on Toshiba / Dynabook laptops.

**What it would show.** Matching devices with their ACPI path, then Device ("Toshiba laptop
extras"), Probed HIDs (`TOS1900 TOS6200 TOS6207 TOS6208`) and Status.

**What "not present" means.** Not a Toshiba laptop. Normal.

**Where Linux gets it.** Name match in `/sys/bus/acpi/devices`.

---
## ACPI TPM  
`acpi-tpm` - Linux data

**In plain words.** Shows how the firmware describes the TPM (Trusted Platform Module, the
security chip used for disk encryption keys and Secure Boot measurements).

**What you see.** For each TPM device in ACPI (`MSFT0101` = TPM 2.0, `PNP0C31` / `IFX0102` =
TPM 1.2):

- **Path**, **HID**, **UID**, **_STA**, **Driver** - as in [ACPI devices](#acpi-devices).

Then the decoded **TPM2 table** (when the srs-root cache has it):

| Field | Meaning |
|---|---|
| Signature ... Creator Revision | The standard ACPI table header (see [ACPI tables](#acpi-tables)) |
| Platform Class | `0 (Client)` for PCs, `1 (Server)` |
| Control Area | Memory address where commands are exchanged with the TPM |
| Start Method | How the OS tells the TPM to start a command: `2 ACPI Start`, `6 TIS 1.3 / FIFO`, `7 Command Response Buffer`, `8 CRB with ACPI Start`, and ARM / I2C variants |

Without the cache only the table size is shown. Without a TPM: "No ACPI TPM device" and / or
"No TPM2 ACPI table".

**Healthy / typical values.** Author's PC: `MSFT0101:00`, `\_SB_.TPM_`, `0x0F PESF`,
`tpm_crb_acpi`; TPM2 table 76 bytes, Platform Class 0 (Client), Start Method 2 ACPI Start. That
is the usual pattern for AMD's firmware TPM.

**Unusual values and what they suggest.** No `MSFT0101` device on a board that has a TPM: it is
switched off in the BIOS setup (on AMD boards look for "fTPM" or "Firmware TPM"). Windows 11
requires it.

**Where Linux gets it.** `/sys/bus/acpi/devices/MSFT0101:*` and the srs-root copy of
`/sys/firmware/acpi/tables/TPM2`.

---
## ACPI WMI (PNP0C14) methods  
`acpi-wmi` - Linux data

**In plain words.** WMI (Windows Management Instrumentation) is a way for firmware to offer
vendor features (fan modes, RGB, sensor readings, keyboard lights) through ACPI. This item lists
the WMI interfaces the firmware offers and which Linux drivers use them.

**What you see.**

| Field | Meaning | Why it matters |
|---|---|---|
| GUID | The 128-bit ID of the WMI interface | Each feature has its own GUID |
| Obj | ACPI object id (the `WQxx`/`WMxx` suffix) or notify id | Which firmware object implements it |
| Inst | Instance count | |
| Exp | "Expensive" flag (1 = slow to query) | |
| Set | Whether the data block can be written (`-` for method / event blocks) | |
| Driver | Linux driver bound to it, or `-` | e.g. `wmi-bmof`, `asus-wmi`, `dell-wmi` |
| Description | Name SRS knows for a few common GUIDs | e.g. "Binary MOF (WMI class descriptions)", "ASUS management (ATK / asus-wmi)" |

Then **WMI mapper devices**: every `PNP0C14` ACPI device with its path. A note says SRS never
invokes WMI methods.

**Healthy / typical values.** Author's PC: six WMI mapper devices (`\AWW0`, `\ALCW`, `\RMTW`,
`\AMZW`, `\WFDV`, `\AOD_`), several Binary MOF blocks bound to `wmi-bmof`, and the ASUS
management GUID with no driver bound (normal on ASUS desktops; `asus-wmi` is mainly for laptops).

**Unusual values and what they suggest.** A vendor GUID with no driver means that feature is not
used by Linux. That is common. Projects such as `asus-wmi-sensors` or vendor-specific modules
may add support for some of them.

**Where Linux gets it.** `/sys/bus/wmi/devices/*` (`guid`, `object_id` / `notify_id`,
`instance_count`, `expensive`, `setable`, `driver`).

**Background.** **Binary MOF** is a compressed description of the vendor's WMI classes; tools can
decode it to learn what methods exist.

---
## APIC (MADT) summary  
`bios-apic` - Linux data - Needs root data

**In plain words.** Decodes the MADT (Multiple APIC Description Table, signature `APIC`). It
tells the operating system how many CPU threads exist and how interrupts are wired to them.

**What you see.** The standard ACPI header (Signature, Length, Revision, Checksum, OEM ID, OEM
Table ID, OEM Revision, Creator ID, Creator Revision), then:

- **Local APIC Address** - memory address of each CPU's local interrupt controller (normally
  `0xFEE00000`).
- **Flags** - `PCAT_COMPAT (dual 8259)` means the old PC interrupt controllers also exist.

Then one row per entry:

| Entry | ID | UID/Addr | GSI/Flags | Detail |
|---|---|---|---|---|
| Local APIC | APIC ID of a CPU thread | ACPI processor UID | flags | `Enabled`, `Online capable` or `Disabled` |
| Local x2APIC | x2APIC ID (for more than 255 threads) | UID | flags | Enabled / Disabled |
| I/O APIC | I/O APIC ID | its address | `GSI n` = first interrupt number it handles | |
| Int Override | `IRQ n` (legacy ISA number) | `bus 0` | `GSI n` it is remapped to | `flags` (polarity / trigger) |
| Local APIC NMI | `all` or a CPU UID | | `flags` | which `LINTn` pin carries the NMI |
| LAPIC Addr Override, NMI Source, x2APIC NMI | rarely used entry types | | | |
| Other | unknown type | | | `type n len n` |

Footer: counts of Local APIC, x2APIC, I/O APIC, Overrides and LAPIC NMI entries. Without the
srs-root cache the item shows only "present, N bytes".

**Healthy / typical values.** Author's PC (8 cores, 16 threads): 32 Local APIC entries, 16
`Enabled` (IDs 0-15) and 16 `Disabled` placeholders; 2 I/O APICs at `FEC00000` (GSI 0) and
`FEC01000` (GSI 24); overrides IRQ 0 -> GSI 2 (the timer) and IRQ 9 -> GSI 9 (the ACPI SCI).

**Unusual values and what they suggest.**
- Fewer enabled entries than CPU threads you expect: SMT or some cores are disabled in the BIOS.
- `Disabled` placeholders are normal. One firmware image supports several CPU models, so it
  lists slots for the biggest one (here 16 cores / 32 threads).

**Where Linux gets it.** The srs-root copy of `/sys/firmware/acpi/tables/APIC`.

**Background.** An **APIC** (Advanced Programmable Interrupt Controller) delivers interrupts to
CPUs. Each thread has a **local APIC**; the **I/O APIC** collects device interrupts and maps them
to **GSIs** (Global System Interrupts).

---
## ASPT table summary  
`bios-aspt` - Not present on most PCs - Needs root data

**In plain words.** Looks for the ASPT (AMD Secure Processor Table), which tells the OS where
the AMD security processor's mailbox is.

**What it would show.** If present: the ACPI header and a hex dump (up to 1 KB) from the srs-root
cache. If absent: `ASPT not present in /sys/firmware/acpi/tables`, its Purpose, and **Tables
present** (count and list of all tables).

**What "not present" means.** Normal on most desktop boards, including the author's. The AMD
security processor is still there; see [HECI (Intel ME) status](#heci-intel-me-status).

**Where Linux gets it.** `/sys/firmware/acpi/tables`.

---
## DMAR table summary  
`bios-dmar` - Not present on most PCs - Needs root data

**In plain words.** The DMAR (DMA Remapping) table describes Intel's IOMMU, called VT-d. AMD
systems use a different table, IVRS, so on AMD this item decodes IVRS instead.

**What you see.** If DMAR is absent: `DMAR not present`, its Purpose ("Intel VT-d DMA
remapping; AMD platforms use IVRS instead") and the Tables present line. If DMAR is present: its
header and a hex dump.

If an IVRS table exists, a second block follows:
- The ACPI header of IVRS.
- **IVinfo** - capability word: virtual address bits (VA), physical address bits (PA), guest
  virtual address bits (GVA), and "EFR supported" (extended feature register).
- One row per entry: **Type** (`IVHD 10h/11h/40h` = IOMMU hardware definition, `IVMD` = memory
  definition), **Flags**, **DevID** (PCI ID of the IOMMU), **Segment**, **Base** (register
  address).

**Healthy / typical values.** Author's PC: no DMAR (AMD). IVRS present: `VA 64 bits, PA 48 bits,
GVA 2 bits, EFR supported`, two IVHD entries (types 10h and 11h) for one IOMMU at
`00000000F7600000`.

**Unusual values and what they suggest.** No IVRS (on AMD) or no DMAR (on Intel): the IOMMU is
switched off in the BIOS setup ("IOMMU" / "SVM" / "VT-d"). You need it for passing a GPU through to
a virtual machine and for DMA protection of Thunderbolt / USB4 ports.

**Where Linux gets it.** `/sys/firmware/acpi/tables` and the srs-root copy of `IVRS`.

**Background.** An **IOMMU** (Input-Output Memory Management Unit) controls which memory each
device may read and write with DMA (direct memory access). It isolates devices from each other
and from the OS.

---
## FACP (FADT) summary  
`bios-facp` - Linux data - Needs root data

**In plain words.** Decodes the FADT (Fixed ACPI Description Table, signature `FACP`). It is the
"master" table: it points to the other key tables and lists the fixed power-management hardware.

**What you see.** The ACPI header, then:

| Field | Meaning |
|---|---|
| FIRMWARE_CTRL (FACS) | Address of the FACS table (used for sleep / wake) |
| DSDT / X_DSDT | 32-bit and 64-bit address of the DSDT |
| Preferred PM Profile | Desktop, Mobile, Server... |
| SCI Interrupt | IRQ used by ACPI (usually 9) |
| SMI Command Port | I/O port used to switch between firmware and OS control, with the enable / disable values |
| PM1a Event / Control | I/O ports of the main power-management registers |
| PM Timer Block | ACPI timer port, length, and whether the timer is 24-bit or 32-bit |
| GPE0 Block / GPE1 Block | I/O ports of the GPE registers (see [ACPI interrupts and GPEs](#acpi-interrupts-and-gpes)) |
| C2 / C3 Latency | Advertised wake-up time of legacy CPU idle states, in microseconds (values above 100 / 1000 mean "not supported") |
| RTC Day/Month Alarm, Century | CMOS register indexes for these RTC fields |
| FADT Minor Version | Sub-version of the table |
| Reset Register | How the OS can reset the PC: address space, address and value |
| Hypervisor Vendor | Set only when running inside a virtual machine |

Then **Flags** (hex) and a tick list of the 24 FADT feature flags (WBINVD, PROC_C1, SLP_BUTTON,
RTC_S4, TMR_VAL_EXT, RESET_REG_SUP, PCI_EXP_WAK, USE_PLATFORM_CLOCK, HW_REDUCED_ACPI,
LOW_POWER_S0_IDLE_CAPABLE and others). `[=]` = set, `[_]` = clear.

Then **IA-PC Boot Architecture** (hex) and its six flags: LEGACY_DEVICES, 8042 (PS/2 keyboard
controller), VGA Not Present, MSI Not Supported, PCIe ASPM Controls, CMOS RTC Not Present.

Without the cache, only the PM profile from sysfs is shown.

**Healthy / typical values.** Author's PC: profile 1 (Desktop), SCI 9, SMI port 0xB2, PM timer
32-bit, C2/C3 latency 101 / 1001 us (that is, "not used": modern CPUs use their own idle states),
reset by writing 0xBE to port 0xB2. Flags 0x0003C5A5 include RESET_REG_SUP, PCI_EXP_WAK and
REMOTE_POWER_ON_CAPABLE. Boot architecture 0x0011: LEGACY_DEVICES and PCIe ASPM Controls set,
8042 clear (no PS/2 controller is advertised).

**Unusual values and what they suggest.**
- `LOW_POWER_S0_IDLE_CAPABLE` set: the board prefers "Modern Standby" (s2idle) over classic S3
  sleep. Common on laptops; if suspend misbehaves, check `/sys/power/mem_sleep`.
- `HW_REDUCED_ACPI` set on a normal PC: unusual; fixed hardware (power button, PM timer) is then
  missing.
- `PCIe ASPM Controls` set means the firmware asks the OS not to change PCIe link power saving
  (ASPM). Linux respects that unless told otherwise (`pcie_aspm=force`).

**Where Linux gets it.** The srs-root copy of `/sys/firmware/acpi/tables/FACP`; fallback
`/sys/firmware/acpi/pm_profile`.

---
## PCI IRQ routing ($PIR)  
`bios-irq` - Not present on most PCs - Needs root data

**In plain words.** Old PCs had a BIOS table called `$PIR` that said which interrupt each PCI slot
used. UEFI boards no longer have it; ACPI `_PRT` methods and the MADT do the job.

**What you see.**
- **Status** - "Not present on this system".
- **Structure** - "$PIR PCI IRQ routing table".
- **Reason** - UEFI routes PCI interrupts through ACPI `_PRT`; scanning for `$PIR` would need
  `/dev/mem`, which SRS never opens.
- **Firmware** - "UEFI x86-64 (ACPI MADT/_PRT replace the legacy tables)" or "Legacy BIOS".
- **Linux routing** - how Linux actually routes interrupts: number of IO-APIC lines and
  PCI-MSI / MSI-X vectors in `/proc/interrupts`.

**Healthy / typical values.** Author's PC: 5 IO-APIC lines and 79 MSI / MSI-X vectors. On modern
systems almost every device uses MSI (message-signalled interrupts).

**Unusual values and what they suggest.** Many devices on IO-APIC lines and few MSI vectors can
mean MSI is disabled (`pci=nomsi`) or a driver does not support it. Shared IO-APIC lines are
slower than MSI.

**Where Linux gets it.** `/proc/interrupts`.

---
## BIOS maker  
`bios-make` - Linux data

**In plain words.** Who made the firmware, which version and date it is, and what features it
says it supports.

**What you see.**

| Field | Meaning |
|---|---|
| BIOS Vendor | Firmware maker, e.g. American Megatrends Inc. |
| Family | Product family SRS recognises: AMI (Aptio V UEFI), Phoenix, Insyde H2O |
| BIOS Version | The version you compare with the board maker's download page |
| BIOS Date | Release date |
| BIOS Release | Major.minor release number from SMBIOS |
| Board Maker / Board | Motherboard maker and model |
| Firmware Type | UEFI or Legacy BIOS (how this boot started) |
| Address, Runtime Size, ROM Size, BIOS Revision, Firmware Revision | From the srs-root cache, when listed by SMBIOS |
| Characteristics | Tick list of the BIOS feature flags from SMBIOS type 0 |

Without the cache: "ROM Size / Address (needs root)".

**Healthy / typical values.** Author's PC: American Megatrends Inc., AMI (Aptio V UEFI), version
1805, 09/18/2026, release 18.5, UEFI, ROM 64 MiB, runtime 64 KiB at 0xF0000. Characteristics
include "UEFI is supported", "ACPI is supported", "Firmware is upgradeable".

**Unusual values and what they suggest.** The characteristics list still shows ancient features
(5.25" floppy, NEC 9800 floppy, CGA video): firmware makers keep these flags for compatibility.
They do not mean the hardware exists. A BIOS date years older than the board's latest download
means a BIOS update is available; updates often fix memory stability, CPU support and security
issues.

**Where Linux gets it.** `/sys/class/dmi/id/bios_*`, `board_*`; `/sys/firmware/efi`; SMBIOS type 0
from the srs-root `dmidecode.txt`.

---
## MPS table summary  
`bios-mps` - Not present on most PCs - Needs root data

**In plain words.** The Intel MultiProcessor Specification (MPS) table was how 1990s PCs listed
their CPUs. ACPI's MADT replaced it.

**What you see.** Status ("Not present on this system"), Structure ("Intel MultiProcessor
Specification (_MP_ floating pointer)"), Reason (superseded by the ACPI MADT; scanning needs
`/dev/mem`, which SRS never opens), Firmware type.

**What "not present" means.** Normal on every UEFI PC. See [APIC (MADT) summary](#apic-madt-summary)
for the modern equivalent.

---
## MSDM table summary  
`bios-msdm` - Not present on most PCs - Needs root data

**In plain words.** The MSDM (Microsoft Data Management) table holds an embedded Windows product
key on PCs sold with Windows pre-installed.

**What it would show.** If present: ACPI header and hex dump from the srs-root cache. If absent:
`MSDM not present`, its Purpose, and the Tables present list.

**What "not present" means.** The PC (or motherboard) was sold without an OEM Windows licence.
Normal for self-built PCs.

**Where Linux gets it.** `/sys/firmware/acpi/tables`.

---
## OEMB table summary  
`bios-oemb` - Not present on most PCs - Needs root data

**In plain words.** OEMB is an AMI OEM-specific table found on some older AMI BIOS boards.

**What it would show.** If present: header and hex dump. If absent: `OEMB not present`, Purpose,
Tables present.

**What "not present" means.** Normal on current boards.

**Where Linux gets it.** `/sys/firmware/acpi/tables`.

---
## BIOS option ROMs  
`bios-option` - Linux data - Needs root data

**In plain words.** Lists the PCI devices that carry their own firmware ROM (an "option ROM"),
such as a graphics card's video BIOS or a storage controller's boot code.

**What you see.**

| Field | Meaning |
|---|---|
| PCI | PCI address |
| Class | PCI class code (030000 = VGA, 010601 = AHCI SATA...) |
| ID | Vendor:device ID |
| ROM size | Size of the expansion ROM window |
| Driver | Linux driver |

Footer: number of devices with an expansion ROM, and a note that the ROM images themselves
(header, code type, EFI or legacy) are root-only and not read.

**Healthy / typical values.** Author's PC: the graphics card `03:00.0` (128 KB, `amdgpu`) and the
SATA controller `0c:00.0` (512 KB, `ahci`).

**Unusual values and what they suggest.** If you turn off CSM (legacy boot support), the firmware
uses only the EFI part of each option ROM. An old card without an EFI option ROM then shows no
picture before Linux starts, or cannot boot.

**Where Linux gets it.** `/sys/bus/pci/devices/*/rom` (the file size only).

---
## SDEV table summary  
`bios-sdev` - Not present on most PCs - Needs root data

**In plain words.** The SDEV (Secure Devices) table lists devices that must only be used by
the secure (virtualisation-protected) part of Windows, such as some fingerprint readers.

**What it would show.** If present: header and hex dump. If absent: `SDEV not present`, Purpose,
Tables present.

**What "not present" means.** Normal on desktops.

**Where Linux gets it.** `/sys/firmware/acpi/tables`.

---
## SFI summary  
`bios-sfi` - Not present on most PCs

**In plain words.** SFI (Simple Firmware Interface) was a cut-down ACPI replacement for Intel's
Moorestown / Medfield phone and tablet chips. Linux removed support in version 5.12.

**What you see.** Status ("Not present on this system"), Structure, Reason ("Intel MID only;
removed from Linux 5.12"), Firmware type.

**What "not present" means.** Always the case on normal PCs.

---
## SLIC table summary  
`bios-slic` - Not present on most PCs - Needs root data

**In plain words.** The SLIC (Software Licensing) table was used for OEM activation of Windows
Vista and 7 on brand-name PCs.

**What it would show.** If present: header and hex dump. If absent: `SLIC not present`, Purpose,
Tables present.

**What "not present" means.** Normal on self-built and most current PCs.

**Where Linux gets it.** `/sys/firmware/acpi/tables`.

---
## SLIT table summary  
`bios-slit` - Not present on most PCs - Needs root data

**In plain words.** The SLIT (System Locality Information Table) gives the relative "distance"
between NUMA nodes, that is, how much slower memory on another CPU socket is.

**What it would show.** If present: header and hex dump. If absent: `SLIT not present`, Purpose,
Tables present.

**What "not present" means.** The PC has a single NUMA node (one CPU socket, one memory pool).
Normal for desktops. Multi-socket servers and some workstation CPUs have it.

**Where Linux gets it.** `/sys/firmware/acpi/tables`. `numactl --hardware` shows the distances
Linux uses.

**Background.** **NUMA** (Non-Uniform Memory Access): on big systems each CPU has "near" memory
and "far" memory. The OS tries to keep a program's memory near the CPU running it.

---
## SRAT table summary  
`bios-srat` - Not present on most PCs - Needs root data

**In plain words.** The SRAT (System Resource Affinity Table) says which CPUs and which memory
ranges belong to which NUMA node.

**What it would show.** If present: header and hex dump. If absent: `SRAT not present`, Purpose,
Tables present.

**What "not present" means.** Single-node system. Normal for desktops. Some boards add it only
when a "NUMA nodes per socket" BIOS option is enabled.

**Where Linux gets it.** `/sys/firmware/acpi/tables`.

---
## BIOS / SMBIOS tables  
`bios-tables` - Linux data - Needs root data

**In plain words.** An overview of the SMBIOS inventory tables: version, size, and how many
records of each type the firmware provides.

**What you see.** Header row:

| Field | Meaning |
|---|---|
| BIOS | SMBIOS version, e.g. `SMBios V3.6.0` (only `V3.x` / `V2.x` without root) |
| Anchor | Entry-point signature: `"_SM3_"` (64-bit, SMBIOS 3) or `"_SM_" + "_DMI_"` (32-bit, SMBIOS 2) |
| EP Size | Size of the entry point: 24 bytes (SMBIOS 3) or 31 (SMBIOS 2) |
| Table | Total size of the structure table in bytes |
| Structures | Number of records (needs root) |

Then **Table address** (memory address of the table), and a list: **Type**, **Name**, **Count**,
one row per SMBIOS record type present (types 128 and up are shown as "OEM-specific"; unknown
standard types as `-`).

**Healthy / typical values.** Author's PC: SMBIOS 3.6.0, `_SM3_`, 24-byte entry point, 3448-byte
table, 80 structures. Types include 0 BIOS Information, 1 System, 2 Base Board, 4 Processor,
7 Cache (3), 8 Port Connector (36), 9 System Slots (7), 17 Memory Device (4, one per slot),
43 TPM Device, 45 Firmware Inventory.

**Unusual values and what they suggest.** Type 17 count lower than your number of slots: the
firmware hides empty slots, so slot positions in [DIMMs](#dimms) may not match the board printing.
SMBIOS 2.x on a new board is very unusual.

**Where Linux gets it.** `/sys/firmware/dmi/tables/smbios_entry_point` and `DMI` (sizes); type
counts from the srs-root `dmidecode.txt`.

**Background.** SMBIOS record **types**: each describes one kind of thing (type 4 a CPU socket,
type 17 a memory slot, type 9 an expansion slot). `dmidecode -t <type>` prints one type.

---
## CMOS / RTC status  
`cmos-status` - Linux data

**In plain words.** The real-time clock (RTC) keeps the time while the PC is off, powered by the
coin battery on the board. This item shows the clock, its alarm, its settings and the battery
check.

**What you see.** First, every line the kernel's RTC driver reports:

| Field | Meaning |
|---|---|
| rtc_time / rtc_date | Time and date stored in the clock chip |
| alrm_time / alrm_date | Wake-up alarm setting |
| alarm_IRQ, alrm_pending | Whether the alarm is armed / has fired |
| update IRQ enabled, periodic IRQ enabled, periodic IRQ frequency, max user IRQ frequency | Timer interrupt settings of the chip |
| 24hr, BCD | Clock format (24-hour, binary-coded decimal) |
| periodic_IRQ, update_IRQ, HPET_emulated, DST_enable, periodic_freq | More chip flags |
| batt_status | Coin-battery check: `okay` (green) or a warning (red) |

Then the `/sys/class/rtc/rtc0` block: **name** (driver and ACPI device), **date**, **time**,
**since_epoch** (seconds since 1970), **hctosys** (1 = Linux set its clock from this RTC at boot),
**max_user_freq**, **wakealarm** and **offset** when set, then:

- **RTC - system clock** - difference between the RTC and Linux's clock in seconds (orange if
  more than 2 s).
- **RTC time scale** - whether the RTC holds UTC or local time (from `/etc/adjtime`).

**Healthy / typical values.** Author's PC: `rtc_cmos PNP0B00:00`, 24-hour BCD, batt_status
`okay`, hctosys 1, difference -1 s, time scale UTC.

**Unusual values and what they suggest.**
- `batt_status` not `okay`, or a clock that is wrong after every power cut: the CR2032 coin cell
  is flat. A flat cell can also reset BIOS settings.
- A difference of many seconds or hours: time sync (NTP) is off, or Windows and Linux disagree on
  UTC versus local time on a dual-boot PC. Linux expects UTC.
- `hctosys 0`: Linux did not read its clock from this RTC.

**Where Linux gets it.** `/proc/driver/rtc`, `/sys/class/rtc/rtc0/*`, `/etc/adjtime`.

**Background.** **CMOS** (originally the chip technology) is the old name for the RTC chip and its
small battery-backed memory, which once held all BIOS settings.

---
## Dell BIOS  
`dell-bios` - Not present on most PCs

**In plain words.** Dell PCs have extra BIOS interfaces (for fans, sensors, settings). This item
checks for them.

**What you see.** **System Vendor**. On a Dell: **dell-smbios** and **dell_smm_hwmon**, each
`loaded` or `not loaded`. Otherwise **Status**: "Not a Dell system: Dell BIOS SMI data does not
apply".

**What "not present" means.** Not a Dell. On a Dell where both show `not loaded`, load the
`dell_smm_hwmon` module to get fan and temperature readings.

**Where Linux gets it.** `/sys/class/dmi/id/sys_vendor`, `/sys/bus/platform/devices/dell-smbios.0`,
`/sys/module/dell_smm_hwmon`.

---
## DIMMs  
`dimms` - Linux data - Needs root data

**In plain words.** One line per memory slot: what is in it, how big, how fast it is rated, how
fast it is actually running, who made it, and its temperature. Open it with the **DIMMs**
panel button.

**What you see.**

| Field | Meaning | Why it matters |
|---|---|---|
| Slot | Slot name from the firmware, e.g. `DIMM 1` | Match it to the board printing |
| Bank | Channel name, e.g. `P0 CHANNEL A` | Which memory channel the slot is on |
| Size | Capacity of the stick, or `Empty` | |
| Type | DDR4, DDR5... | |
| Form | DIMM (desktop) or SODIMM (laptop) | |
| Rated | Base speed the stick reports (JEDEC) | The "safe default" speed |
| Configured | Speed the memory is running at now | Shows whether EXPO / XMP is active |
| Rank | Number of ranks (1 or 2) | Dual-rank is a little faster, harder to overclock |
| Manufacturer | Module maker | |
| Part Number | Model code of the stick | Use it to look up the kit's spec |
| Serial | Module serial number | Tells identical sticks apart |
| SPD | The `spd5118` sensor device paired with this slot, e.g. `1-0051` | |
| Temp | Current stick temperature from that sensor | |

Footer: "N of M slots populated, X installed", the data source, and a note that the SPD pairing
is inferred (slot index i is matched to the sensor at SMBus address 0x50 + i).

**Detail view** (`dimms:<slot index>`, e.g. `dimms:1`): every SMBIOS field the system has for
that slot (from udev: locator, bank, size, type, type detail, form factor, speeds, manufacturer,
module manufacturer ID, part number, serial, rank, configured voltage, memory technology...; or,
from the srs-root cache: Locator, Bank Locator, Size, Type, Type Detail, Form Factor, Speed,
Configured Memory Speed, Manufacturer, Part Number, Serial Number, Rank, Minimum / Maximum /
Configured Voltage, Total Width, Data Width), plus a line for the paired SPD5118 sensor and its
temperature.

**Healthy / typical values.** Author's PC: 2 of 4 slots used, 32 GB. Both sticks in the
recommended slots (`DIMM 1` of channel A and channel B, the "A2 / B2" slots on most boards):
16 GB DDR5 DIMM, rank 1, rated 4800 MT/s, configured 6000 MT/s (EXPO profile active), G.Skill,
temperatures 30-31 °C.

**Unusual values and what they suggest.**
- **Configured equals Rated** (for example 4800 on a 6000 kit): EXPO / XMP is off in the BIOS.
- **Two sticks in the same channel** (both "CHANNEL A"): the memory runs single-channel, roughly
  half the bandwidth. Move one stick to the other channel.
- **Different part numbers** on the sticks: a mixed pair. It can work, but mixed kits are a common
  cause of instability at EXPO / XMP speeds. Matched kits are safer.
- **Serial all zeros** on a stick: some modules simply do not program a serial. Harmless by
  itself.
- Size shown smaller than expected or a stick missing: poor seating, a dead stick, or a slot
  disabled by the board.

**Where Linux gets it.** SMBIOS type 17 as imported by systemd-udevd into
`/run/udev/data/+dmi:id` (readable without root), else the srs-root `dmidecode.txt`; temperatures
from the `spd5118` hwmon driver. It is marked "needs root" because the fallback path
and full detail come from the cache; on most systemd distributions the table is complete without
it.

**Background.** **MT/s** (mega-transfers per second) is the data rate. DDR memory moves data twice
per clock, so 6000 MT/s is a 3000 MHz clock. Marketing often writes "6000 MHz", which is not
strictly right.

---
## DIMM status/temperatures  
`dimm-status` - Linux data

**In plain words.** Live temperatures and limits of the thermal sensors on DDR5 memory sticks.
This is what the **Status** panel sub-button shows. It refreshes every 2 seconds.

**What you see.**

| Field | Meaning |
|---|---|
| Sensor | Linux hwmon name, e.g. `hwmon7` |
| Device | I2C device of the sensor, e.g. `1-0051` (bus 1, address 0x51) |
| Slot | Compact slot label, e.g. `A1` = channel A, DIMM 1 (inferred from the address) |
| Temp | Current temperature |
| Min / Max / Crit / LCrit | Limits programmed into the sensor: low warning, high warning, critical high, critical low |
| Alarms | Which limit alarms are active (`min`, `max`, `crit`, `lcrit`), or `none` (green) |
| Enabled | Whether the sensor is enabled |

A note says only the hwmon values are read, never the SPD memory chip itself. If there are no
sensors: "No spd5118 hwmon sensors" with a hint that the `spd5118` driver must bind to SMBus
addresses 0x50-0x57.

**Healthy / typical values.** Author's PC at idle: 31.0 °C and 30.5 °C; limits 0 / 55 / 85 / 0 °C;
alarms none. Under heavy load DDR5 typically runs 40-55 °C.

**Unusual values and what they suggest.**
- Above about 55 °C: the stick reaches its warning limit. DDR5 then refreshes more often (a little
  slower), and errors become more likely when overclocked. Add airflow over the memory.
- One stick much hotter than the other: it may be under a CPU cooler's exhaust, or have no heat
  spreader contact.
- No sensors on DDR5: the `spd5118` module is not loaded, or the board's SMBus is locked.
  DDR4 sticks usually have no sensor.

**Where Linux gets it.** `/sys/class/hwmon/hwmon*/` where `name` is `spd5118`: `temp1_input`,
`temp1_min`, `temp1_max`, `temp1_crit`, `temp1_lcrit`, `temp1_*_alarm`, `temp1_enable`.

**Background.** Every DDR5 stick has an **SPD hub** chip (spd5118) that holds the stick's
description (the SPD data) and contains a temperature sensor.

---
## Firmware  
`firmware` - Linux data

**In plain words.** How the PC's firmware started Linux: UEFI or legacy BIOS, Secure Boot state,
firmware update records, and how long the firmware took to boot.

**What you see.**

| Field | Meaning |
|---|---|
| Firmware Type | `UEFI`, or "Legacy BIOS (no /sys/firmware/efi)" |
| Platform Size | 64-bit or 32-bit UEFI |
| Firmware Vendor Pointer, Runtime Services, Configuration Table | Memory addresses of UEFI structures (for debugging) |
| System Table | Address of the UEFI system table (root only) |
| Secure Boot | Enabled / Disabled / Unknown |
| ESRT Entries | Number of firmware-update resources (EFI System Resource Table); per-entry `fw_version` lines when readable |
| UEFI Variables | Number of variables stored in firmware |
| BIOS Vendor / BIOS Version | From SMBIOS |

**Firmware Boot Performance (ACPI FPDT)**, when the board provides it:

| Field | Meaning |
|---|---|
| Firmware start | When firmware timing started |
| Boot loader load | When the firmware started loading the boot loader |
| ExitBootServices | When the OS took over from the firmware (start -> end) |
| Time in firmware + loader | Total time from power-on timing start to OS hand-over |

**Healthy / typical values.** Author's PC: UEFI, 64-bit, Secure Boot Disabled, 1 ESRT entry,
159 variables. Typical firmware time is 5-20 s; boards with DDR5 that re-train memory at every
boot (or right after a BIOS settings change) can take much longer. The boot-loader-to-hand-over
part also includes any time spent in the boot menu.

**Unusual values and what they suggest.**
- A very long "Boot loader load" time: memory training. On AMD AM5 boards enable "Memory Context
  Restore" in the BIOS so training is skipped on normal boots (if your memory is stable with it).
- Large gap between Boot loader load and ExitBootServices: time spent in the boot loader menu or
  its timeout.
- ESRT 0: the firmware does not offer capsule updates, so `fwupd` cannot update it.

**Where Linux gets it.** `/sys/firmware/efi/` (`fw_platform_size`, `fw_vendor`, `runtime`,
`config_table`, `systab`, `esrt/`, `efivars/`), `/sys/firmware/acpi/fpdt/boot/*_ns`.

---
## Firmware interface table  
`f-w-table` - Not present on most PCs

**In plain words.** The Firmware Interface Table described firmware components on Intel Itanium
(IA-64) servers.

**What you see.** Status ("Not present on this system"), Structure ("IA-64 (Itanium) firmware
interface table"), Reason ("IA-64 only"), Firmware type.

**What "not present" means.** Always the case on x86 PCs.

---
## Hardware summary  
`hardware` - Linux data

**In plain words.** A one-page summary of the main parts of the PC.

**What you see.**

| Field | Meaning |
|---|---|
| Processor | CPU model name |
| Logical CPUs | Number of CPU threads |
| System | System maker and product name (`NA` if the firmware left a placeholder) |
| Motherboard | Board maker and model |
| BIOS | Vendor, version, date, and `(UEFI)` or `(Legacy)` |
| Memory | Total, type, speed, slots used, and the first stick's maker and part number |
| TPM | TPM 2.0, TPM 1.2 or none |
| Secure Boot | Enabled / Disabled / "n/a (legacy BIOS)" |

Then a device list from `lspci`: **Class**, **Address**, **Device** for graphics, audio,
network, NVMe, SATA, RAID, USB and Thunderbolt controllers.

**Healthy / typical values.** Author's PC: AMD Ryzen 7 7800X3D, 16 logical CPUs, ASUS TUF GAMING
X870-PRO WIFI7 W NEO, BIOS 1805 (UEFI), 32GB DDR5 6000 MT/s in 2 of 4 DIMMs, TPM 2.0, Secure Boot
Disabled; Radeon RX 9070 series graphics, three NVMe controllers, RTL8126 5GbE, RTL8922AE Wi-Fi 7,
ASM4242 USB4.

**Unusual values and what they suggest.** "System ASUS NA": self-built boards often leave the
system product name as a placeholder. That is normal. A missing controller (for example no
network) usually means it is disabled in the BIOS setup.

**Where Linux gets it.** `/sys/class/dmi/id`, `/sys/class/tpm/tpm0`, efivars, `lspci -mm`.

---
## HECI (Intel ME) status  
`heci-status` - Not present on most PCs

**In plain words.** HECI (Host Embedded Controller Interface) is how the OS talks to the Intel
Management Engine, a separate processor inside Intel chipsets. AMD has its own equivalent, the
PSP (Platform Security Processor). This item reports either.

**What you see.**
- **Intel MEI / HECI** - `present (/sys/class/mei)` or "Not present on this system".
- **AMD PSP / CCP** - the PCI address of the AMD security processor bound to the `ccp` driver, or
  "no ccp-bound device".

**Healthy / typical values.** Author's PC (AMD): no Intel MEI; AMD PSP at `0000:75:00.2` (ccp).

**What "not present" means.** On an AMD PC, Intel MEI is never present. On an Intel PC with no
MEI, the `mei_me` driver is not loaded or the ME interface is hidden in the BIOS.

**Where Linux gets it.** `/sys/class/mei`, `/sys/bus/pci/drivers/ccp`.

**Background.** The **PSP** runs the firmware TPM (fTPM) and memory encryption on AMD systems. It
starts before the main CPU.

---
## Machine  
`machine` - Linux data

**In plain words.** The main page of this group, modelled on SIV's Machine panel: the SMBIOS
identity of the PC in a compact grid, from BIOS to CPU socket to memory slots. The **Machine**
panel button opens it.

**What you see.** Rows of four columns:

| Row | Contents |
|---|---|
| SMBios | Version, record count ("Count (needs root)" without the cache), table size in decimal and hex |
| BIOS | Version, date, release, vendor, plus ROM base address and ROM size (cache) |
| System | Product name, version, vendor and UUID (needs root if not readable) |
| (second line) | Family, SKU, system serial number |
| Chassis | Version and chassis type (Desktop, Tower, Laptop, Mini PC...), vendor, serial, asset tag |
| Board | Revision, model, board serial, maker |
| CPU-0 Socket #0 | CPU vendor ID (`AuthenticAMD` / `GenuineIntel`), socket name (AM5, LGA1700...), model, max speed, external clock, cores, threads, voltage |
| System Boot | SMBIOS boot status ("No errors detected"), and `UEFI  Secure Boot on/off` or `Legacy BIOS` |
| Memory Array | Location, maximum capacity, ECC type, DIMMs used of total, total installed |
| One row per slot | Slot, bank, then size / type / configured speed and maker / part, or `Empty` |

Placeholder strings the firmware leaves ("To be filled by O.E.M.", "Default string", "System
Product Name"...) are shown as `NA`. If the record count, serials or socket are missing, the page
ends with the "Needs root" hint.

**Healthy / typical values.** Author's PC: SMBios V3.6.0, Count 80, size 3448 (0D78); BIOS 1805
release 18.5, ROM 64 MiB; System `NA NA ASUS` (placeholders); Chassis Desktop; Board Rev 1.xx TUF
GAMING X870-PRO WIFI7 W NEO, serial XXXX; socket AM5, Max 5050 MHz, Ext 100 MHz, 8 cores, 16
threads, 1.3 V; System Boot "No errors detected", UEFI Secure Boot off; Memory Array maximum 256GB,
ECC None, 2 of 4 DIMMs, 32GB.

**Unusual values and what they suggest.**
- **Ext** (external clock) not 100 MHz: base clock (BCLK) overclocking is active.
- **ECC** other than None on a desktop with normal memory is unusual; with ECC sticks installed,
  "None" means ECC is not enabled.
- **System Boot** status other than "No errors detected": the firmware logged a problem at the
  last boot (for example a memory training failure or a watchdog reset).

**Where Linux gets it.** `/sys/class/dmi/id/*`, `/run/udev/data/+dmi:id`, `/proc/cpuinfo`,
efivars, plus SMBIOS types 0, 1, 4, 16, 32 from the srs-root `dmidecode.txt`.

---
## Machine options  
`machine-opt` - Linux data

**In plain words.** A checklist of the data sources this group can use on your PC, so you can see
at a glance why an item is empty.

**What you see.** One tick line per source, `[=]` available, `[_]` not available, with its path:

- DMI identification (unprivileged) - `/sys/class/dmi/id/`
- SMBIOS memory devices via udev - `/run/udev/data/+dmi:id`
- dmidecode cache (serials, UUID, socket) - `~/.cache/srs/root/dmidecode.txt`
- SPD decode cache - `~/.cache/srs/root/spd.txt`
- ACPI table copies - `~/.cache/srs/root/acpi-tables/`
- I/O memory map cache - `~/.cache/srs/root/iomem.txt`
- spd5118 DIMM thermal sensors - hwmon devices named `spd5118`
- EDAC memory controllers - `/sys/devices/system/edac/mc`
- UEFI variables (read-only) - `/sys/firmware/efi/efivars`
- TPM - `/sys/class/tpm/tpm0`
- CMOS RTC - `/proc/driver/rtc`
- ACPI WMI bus - `/sys/bus/wmi/devices`
- Three lines that are always off by design: direct SMBus / SPD EEPROM reads, ACPI method
  evaluation / EC writes, and `/dev/mem`, `/dev/nvram`, `/dev/port`.

**Healthy / typical values.** Author's PC: everything on except EDAC (non-ECC memory) and the
three always-off lines.

**Unusual values and what they suggest.** Cache lines off: run **Menu > Run as root (srs-root)**.
EDAC off with ECC memory installed: load the `amd64_edac` (AMD) or `ie31200_edac` / `igen6_edac`
(Intel) module, or ECC is disabled in the BIOS.

**Where Linux gets it.** Checks that each path exists.

---
## Memory  
`memory` - Linux data

**In plain words.** How much memory is in use, how much is free, and the detailed counters Linux
keeps. Refreshes every second.

**What you see.** First a usage table:

| Row | Used | Available | Total | Load |
|---|---|---|---|---|
| System Physical Memory | RAM in use (total minus "available") | RAM available to programs | Usable RAM | Percent used |
| System Paging File (swap) | Swap in use | Free swap | Swap size | Percent used |
| Commit Charge | Memory programs have asked for (`Committed_AS`) | Commit limit minus that | Commit limit | Percent of the limit |

Then the `/proc/meminfo` counters: MemFree, Buffers, Cached, SwapCached, Active, Inactive, Dirty,
AnonPages, Mapped, Shmem, Slab, SReclaimable, KernelStack, PageTables, VmallocUsed, Percpu,
HardwareCorrupted (red if not 0), AnonHugePages, HugePages_Total, Hugepagesize, Zswap, Zswapped,
DirectMap4k / 2M / 1G.

Then:
- **Installed (SMBIOS)** - capacity and slots used, from the firmware.
- **Memory Speed** - type, configured speed and rated speed.
- **Memory Blocks** - online and total memory blocks, block size, online total.
- **Auto Online Blocks** - policy for hot-added memory.
- **EDAC** - number of memory controllers with error reporting and, for the first one, its name
  and counts of corrected (CE) and uncorrected (UE) errors; or "no memory controllers registered".

**Healthy / typical values.** Author's PC, desktop in use: 10.60 GB used of 30.45 GB (35%), swap
6.39 GB of 48.04 GB used, commit charge 39.57 GB of a 63.26 GB limit; Cached 16.38 GB;
HardwareCorrupted 0B; installed 32GB, DDR5 6000 MT/s (rated 4800); 251 of 251 blocks online.

**Unusual values and what they suggest.**
- **Total well below installed** (for example 24 GB of 32 GB): memory reserved for integrated
  graphics, or a stick not detected. A small gap (here 30.45 of 32 GB) is normal: firmware,
  the kernel and reserved areas take some.
- **Cached** large and **MemFree** small: normal. Linux uses spare RAM as disk cache and gives it
  back when needed. Look at **Available**, not Free.
- **HardwareCorrupted** above 0: the kernel took pages offline because of memory errors. Test
  your RAM (memtest86+) and lower memory overclocks.
- **Commit Charge** close to the limit: programs may fail to get memory; add swap or RAM.
- **EDAC CE** rising: correctable errors on ECC memory, an early warning of a failing stick.

**Where Linux gets it.** `/proc/meminfo`, `/sys/devices/system/memory/`,
`/sys/devices/system/edac/mc/`, SMBIOS type 17.

**Background.** **Swap** (Windows: paging file) is disk or compressed memory (zram, zswap) used
when RAM is full. **Commit charge** is how much memory programs have reserved, not how much they
actually touch.

---
## Memory timings  
`mem-tweak` - Linux data - Needs root data

**In plain words.** Shows the memory speed. Detailed timings (CL, tRCD and so on) are not read,
because on AMD they sit in memory-controller registers that SRS would have to write to reach.

**What you see.**
- **DRAM** - type, size, form factor and ranks of the first stick.
- **Configured Speed** - current data rate.
- **MEMCLK** - real memory clock, half the data rate ("DDR: 2 transfers per clock").
- **Rated (JEDEC) Speed** - the stick's base speed.
- **ryzen_smu** - whether the third-party `ryzen_smu` driver is loaded.
- **Timing registers (UMC)** - "not read": needs SMN register access; SRS stays read-only.
- Two notes: primary / secondary timings (tCL, tRCD, tRP, tRAS, tRFC...) would come from the AMD
  memory controller (UMC) via SMN; FCLK / UCLK / MCLK are in the SMU PM table item `amd-pmt`.

**Healthy / typical values.** Author's PC: DDR5 16GB, ranks 1, 6000 MT/s, MEMCLK 3000 MHz,
rated 4800 MT/s.

**Unusual values and what they suggest.** MEMCLK equal to half the rated speed (2400 MHz on a 6000
kit): EXPO / XMP is off. For timings use a tool that reads the memory controller (for example
`ZenTimings` on Windows) or read them in the BIOS setup.

**Where Linux gets it.** SMBIOS type 17; `/sys/kernel/ryzen_smu_drv` presence.

**Background.** **SMN** (System Management Network) is AMD's internal register bus. **UMC** is the
Unified Memory Controller. On AM5, **FCLK** (Infinity Fabric clock), **UCLK** (memory controller
clock) and **MCLK** (memory clock) should be in a good ratio; UCLK = MCLK ("1:1") is fastest.

---
## Overview  
`overview` - Linux data

**In plain words.** SIV's classic overview header: one screen with the OS, board, user, uptime,
power source, memory use, CPU and memory sticks. Refreshes every second.

**What you see.**
- **First line** - OS name and kernel, board maker and model, `host::user`, language (`LANG`),
  time zone, BIOS version and date.
- **Second line** - uptime as `DD HH:MM:SS` (days first), current date and time, `[BIOS version
  date]`, and the power source: `Mains Power` or `Battery Power`, followed by `Battery N%` for
  each battery the system reports.
- **Resource table** - "Resource Usage N%" (physical memory load), then three rows with
  **Current**, **Committed** and **Maximum**: System Physical Memory (RAM in use, total commit
  charge, usable RAM), System Paging File (swap in use, swap size), Commit Charge (limit)
  (committed memory, commit limit).
- **Linux** kernel release.
- **CPUs / Cores / Chips / DIMMs** - logical CPUs, physical cores, CPU packages, sticks installed.
- CPU model name.
- One line per installed stick: bank, slot, size, type, speed, maker and part number.

**Healthy / typical values.** Author's PC: `CachyOS 7.3.0-...  ASUS TUF GAMING X870-PRO WIFI7 W
NEO  MYPC::user  en_US.UTF-8  EDT  BIOS 1805 09/18/2026`; uptime 21 h 46 min; Mains Power;
Resource Usage 35%; CPUs 16 Cores 8 Chips 1 DIMMs 2.

**Unusual values and what they suggest.** The Committed figure on the physical-memory row is the
total commit charge, so it can be larger than the RAM size; that is normal when swap exists.
A "Battery" entry on a desktop usually comes from a wireless mouse, keyboard or UPS (uninterruptible power supply) that reports
a battery to Linux.

**Where Linux gets it.** `/etc/os-release`, `/sys/class/dmi/id`, `/proc/uptime`,
`/sys/class/power_supply`, `/proc/meminfo`, `/sys/devices/system/cpu/*/topology`, SMBIOS type 17.

---
## Physical memory map  
`physical` - Linux data

**In plain words.** The memory map the firmware gave Linux at boot: which address ranges are
usable RAM and which are reserved for firmware, ACPI or devices.

**What you see.**

| Field | Meaning |
|---|---|
| # | Entry number |
| Start / End | First and last physical address (hex) |
| Size | Length of the range |
| Type | `System RAM`, `Reserved`, `ACPI Tables` (reclaimable after boot), `ACPI Non-volatile Storage` (kept for firmware), `Persistent Memory`, `Unusable memory`... |

Then **Total** rows: size and count of entries per type. Then, with the srs-root cache, the
top-level `/proc/iomem` list: the same address space with owners, including PCI windows, the
IOMMU, I/O APICs, HPET and the graphics card's memory window. Without it, a note that resource
owners need root.

**Healthy / typical values.** Author's PC: 23 entries; System RAM total 31.10 GB in 8 ranges,
Reserved 13.68 GB (that is address space, mostly not real RAM), ACPI NVS 32.06 MB, ACPI Tables
8.82 MB. The biggest RAM range is 0x100000000-0x87DEFFFFF (29.97 GB above 4 GB). The iomem list
shows `PCI ECAM` at 0xE0000000 (256 MB of PCIe configuration space) and a large window at the top
for the graphics card (Resizable BAR).

**Unusual values and what they suggest.**
- System RAM total far below installed RAM: a stick is not detected, or memory is set aside
  (integrated graphics, `memmap=` kernel option, crash kernel).
- `Unusable memory` or `Bad RAM` entries: the firmware marked memory as faulty.
- Reserved address space is not lost RAM: most of it is device windows and the "PCI hole" below
  4 GB, which modern systems remap above 4 GB.

**Where Linux gets it.** `/sys/firmware/memmap/*` (start, end, type); owners from the srs-root
`iomem.txt` (copy of `/proc/iomem`, whose addresses are hidden from normal users).

**Background.** This is the **E820** / UEFI memory map. Physical addresses are also used by
devices, so the address space is bigger than the installed RAM.

---
## SPD (DIMMs)  
`spd` - Linux data

**In plain words.** The SPD (Serial Presence Detect) is the small description each memory stick
carries: maker, size, speeds, timings, and XMP / EXPO overclock profiles. This item shows the
decoded SPD.

**What you see.** Two possible forms:

- **With the srs-root cache `spd.txt`**: the text output of `decode-dimms` (from i2c-tools),
  unchanged. A full decode lists per stick the module maker, manufacture date, size and
  organisation, supported speeds and CAS latencies, primary timings, and any XMP / EXPO profiles.
- **Without it**: an SMBIOS-based summary per installed stick: bank and slot, Module Manufacturer,
  Manufacturer ID (JEDEC), Part Number, Size, Type (type, form factor, detail), Rated Speed
  (SPD/JEDEC), Configured Speed, Ranks, SPD Hub (the `spd5118` device). Two notes explain that the
  full decode comes from the cache and that SRS never reads the SPD chip directly. Then the
  "Needs root" hint.

When the cached `decode-dimms` run decoded no DIMMs ("Number of SDRAM DIMMs detected and decoded: 0",
normal with DDR5), SRS says so and shows the SMBIOS summary instead.

**Healthy / typical values.** A full decode shows one block per stick. On the author's DDR5 PC
`decode-dimms` decodes 0 DIMMs, so the page shows the SMBIOS summary of both sticks.

**Unusual values and what they suggest.** "0 DIMMs decoded" with DDR5: `decode-dimms` needs the
`spd5118` driver's `eeprom` file and a version of i2c-tools that understands DDR5. Updating
i2c-tools, then running srs-root again, usually fixes it. Rated speed and part number are also
in [DIMMs](#dimms).

**Where Linux gets it.** srs-root cache `spd.txt` (output of `decode-dimms`, which reads
`/sys/bus/i2c/drivers/spd5118/*/eeprom`); fallback SMBIOS type 17.

**Background.** **XMP** (Intel Extreme Memory Profile) and **EXPO** (AMD Extended Profiles for
Overclocking) are tested faster settings stored in the SPD. The BIOS applies them when you enable
the profile.

---
## SPD thermal  
`spd-temp` - Linux data

**In plain words.** The same DDR5 stick temperature table as
[DIMM status/temperatures](#dimm-statustemperatures), under the SPD name. Refreshes every 2
seconds.

**What you see.** Sensor, Device, Slot, Temp, Min, Max, Crit, LCrit, Alarms, Enabled, exactly as
described for DIMM status.

**Healthy / typical values.** Author's PC: A1 31.0 °C, B1 30.5 °C, limits 0 / 55 / 85 / 0 °C, no
alarms.

**Where Linux gets it.** `spd5118` hwmon attributes `temp1_*`.

---
## System CMOS  
`system-cmos` - Linux data - Needs root data

**In plain words.** The CMOS RAM is a small battery-backed memory next to the clock. SIV can dump
it on Windows. SRS does not read it (that needs raw port access), so it reports what it can
without that.

**What you see.**
- **CMOS RAM dump** - "not read: needs /dev/nvram or port I/O (root), not in the srs-root cache".
- **/dev/nvram** - `present (root only)` or `absent (nvram module not loaded)`.
- **RTC driver** - e.g. `rtc_cmos PNP0B00:00`.
- **CMOS battery** - `okay` (green) or the warning the driver reports (red).
- A pointer to [CMOS / RTC status](#cmos--rtc-status).

**Healthy / typical values.** Author's PC: `/dev/nvram` present (root only), `rtc_cmos
PNP0B00:00`, battery okay.

**Unusual values and what they suggest.** Battery not okay: replace the coin cell (usually a
CR2032) before BIOS settings and the clock get lost.

**Where Linux gets it.** `/dev/nvram` existence, `/sys/class/rtc/rtc0/name`, `/proc/driver/rtc`.

---
## System EBDA  
`system-ebda` - Linux data - Needs root data

**In plain words.** The EBDA (Extended BIOS Data Area) is a small block just below 640 KB that
legacy BIOSes used for their own data. SRS does not read its contents, but shows what the memory
map says about that area.

**What you see.**
- **EBDA contents** - "not read: needs /dev/mem (root); srs never opens /dev/mem".
- **Conventional memory** - the first memory-map entry (start - end, type).
- **EBDA (implied)** - either "none reserved: the firmware map gives RAM up to 0x9FFFF", or the
  reserved range between the end of that RAM and 0x9FFFF.
- **Firmware** - "UEFI (EBDA kept only for CSM/legacy compatibility)" or "Legacy BIOS".
- With the srs-root cache: the `/proc/iomem` lines below 1 MB (Reserved, System RAM, PCI Bus,
  System ROM).

**Healthy / typical values.** Author's PC: conventional memory 0x0 - 0x9FFFF System RAM, no EBDA
reserved, UEFI; iomem shows the System ROM at 0xF0000-0xFFFFF.

**Unusual values and what they suggest.** Nothing to fix here. An EBDA range on a UEFI PC means
CSM (legacy boot support) is active.

**Where Linux gets it.** `/sys/firmware/memmap/0/*`, srs-root `iomem.txt`.

---
## BIOS IDs  
`system-ids` - Linux data - Needs root data

**In plain words.** The identity strings in SMBIOS: UUID, serial numbers, asset tags, and the
memory sticks' serials. Useful for warranty claims and asset lists. Keep them private when you
share reports.

**What you see.**

| Field | Meaning |
|---|---|
| System UUID | Unique ID of this machine (green) |
| System Serial / Board Serial / Chassis Serial | Serial numbers from SMBIOS types 1, 2, 3 |
| System SKU / System Family | Product variant and family |
| Board Asset Tag / Chassis Asset Tag | Inventory tags (often blank) |
| DIMM Serial *channel slot* | Serial of each installed memory stick |
| DMI modalias | One-line summary Linux uses to match drivers to the board (`bvn` BIOS vendor, `bvr` version, `bd` date, `svn` system vendor, `pn` product name, `rvn` board vendor, `rn` board name, `rvr` board revision, `cvn` chassis vendor...) |

Fields that need root and are not in the cache show "(needs root)", followed by the root hint.

**Healthy / typical values.** Author's PC: a UUID, Board Serial XXXX, and placeholder strings for
the rest ("System Serial Number", "Default string", "SKU", "To be filled by O.E.M."), which is
typical for a retail motherboard. DIMM serials: one real serial and one `00000000`.

**Unusual values and what they suggest.** Placeholder strings are normal on self-built PCs. On a
brand-name PC they can mean the board was replaced and the service centre did not program the
IDs, which can break OEM Windows activation or warranty look-ups. An all-`FF` or all-`0` UUID
means the firmware did not set one.

**Where Linux gets it.** `/sys/class/dmi/id/*` (serials and UUID are root-only there), else the
srs-root `dmidecode.txt`; DIMM serials from SMBIOS type 17; `/sys/class/dmi/id/modalias`.

---
## TPM device  
`tpm-device` - Linux data

**In plain words.** The TPM (Trusted Platform Module) as Linux sees it: version, driver, how it is
connected, and its measurement banks.

**What you see.**

| Field | Meaning |
|---|---|
| TPM Version | TPM 2.0 or TPM 1.2 |
| Driver | e.g. `tpm_crb_acpi`, `tpm_tis` |
| Interface | `CRB (Command Response Buffer)` or `TIS/FIFO` |
| Implementation | On AMD with CRB: "firmware TPM (AMD fTPM in the PSP, likely)" |
| ACPI Node / ACPI Path / ACPI Status | The ACPI device behind it |
| Physical Presence Interface | PPI version (how the OS asks the firmware to clear or enable the TPM) |
| PCR Banks | Hash algorithms with PCR banks, e.g. `sha256` |
| Device Nodes | `/dev/tpm0` and `/dev/tpmrm0` (the resource-managed node) |
| ACPI TPM2 table | Size of the TPM2 table |

PCR values are not read by default. `tpm-device:pcr` adds the 24 sha256 PCR values (PCR 0 to
PCR 23). If there is no TPM: "No TPM (/sys/class/tpm empty)".

**Healthy / typical values.** Author's PC: TPM 2.0, `tpm_crb_acpi`, CRB, AMD fTPM, `MSFT0101:00`,
`\_SB_.TPM_`, `0x0F PESF`, PPI 1.3, bank sha256, `/dev/tpm0 /dev/tpmrm0`, TPM2 table 76 bytes.

**Unusual values and what they suggest.**
- No TPM: enable fTPM (AMD) or PTT (Intel) in the BIOS setup.
- Only a `sha1` bank: old firmware or TPM 1.2; some disk encryption tools want sha256.
- PCR values that change after a BIOS update or settings change are expected. If your disk
  encryption is sealed to PCRs, it will then ask for the recovery key.

**Where Linux gets it.** `/sys/class/tpm/tpm0/*` (`tpm_version_major`, `device/driver`,
`device/firmware_node`, `ppi/version`, `pcr-*`), `/sys/firmware/acpi/tables/TPM2` size.

**Background.** **PCRs** (Platform Configuration Registers) hold hashes of everything that ran
during boot (firmware, boot loader, kernel). Disk encryption can be "sealed" so the key is only
released when the PCRs match.

---
## UEFI data  
`uefi-data` - Linux data

**In plain words.** Lists every UEFI variable stored in the firmware's flash: boot entries,
Secure Boot keys, and the board maker's own settings. Names and sizes only.

**What you see.** Variables grouped by vendor GUID (the 128-bit ID of whoever defined them). Each
GUID line may carry a known label: "EFI Global Variable" (the standard boot variables such as
`BootOrder`, `Boot0000`, `SecureBoot`, `PK`, `KEK`), "Image Security Database" (`db` allowed
signatures, `dbx` revoked signatures), "systemd Boot Loader Interface" (variables set by
systemd-boot or GRUB). Under each GUID: **Variable** name and **Size** in bytes.

**Detail view** (`uefi-data:<Name-GUID>`, the full file name): **Attributes** in hex with flags
**NV** (non-volatile, survives power-off), **BS** (boot-services access), **RT** (runtime access,
visible to the OS), **TA** (time-based authenticated, i.e. signed), the size, and a hex dump of up
to 4 KB. Read-only.

**Healthy / typical values.** Author's PC: 159 variables. The standard group has `Boot0000`,
`Boot0001`, `BootCurrent`, `BootOrder`, `KEK`, `ConIn`, `ConOut`; `db` is 11440 bytes and `dbx`
22314 bytes. Many vendor groups hold ASUS and AMD BIOS settings (`Setup`, `AmdSetup...`,
`AsusQFanSetupData`, `MyFav`, `StdDefaults`...).

**Unusual values and what they suggest.**
- A very large number of variables or a nearly full variable store can make firmware updates or
  new boot entries fail. Old `Boot####` entries can be removed with `efibootmgr`.
- An empty `dbx` (or a very small one) means the revocation list was never updated, which matters
  if you use Secure Boot.

**Where Linux gets it.** `/sys/firmware/efi/efivars/` (each file is 4 bytes of attributes then the
data).

---
## UEFI information  
`uefi-info` - Linux data

**In plain words.** The key UEFI boot settings: Secure Boot mode, boot menu timeout, language,
which boot entry started this session, and the boot order.

**What you see.**

| Field | Meaning |
|---|---|
| UEFI Platform | 64-bit or 32-bit UEFI |
| SecureBoot | 1 (on) / 0 (off) |
| SetupMode | 1 = no Platform Key installed, Secure Boot keys can be changed freely |
| AuditMode / DeployedMode | Extra Secure Boot modes, when the firmware supports them |
| Timeout | Firmware boot menu timeout in seconds |
| PlatformLang | Firmware language |
| OsIndicationsSupported | Hex mask of OS-to-firmware requests the firmware supports; SRS names BootToFwUI (reboot into setup), FileCapsule and CapsuleResultVar (firmware updates) |
| Boot Loader / Firmware Info / Firmware Type / Kernel Stub | Strings from the systemd Boot Loader Interface, when the boot loader sets them |
| BootCurrent | Boot entry used for this boot (green) |
| BootOrder | Boot entries in the order the firmware tries them |
| Boot Entries | One line per entry in BootOrder: `Boot####`, `*` if active, and its description |

If the PC did not boot via UEFI: "Not booted via UEFI (/sys/firmware/efi absent)".

**Healthy / typical values.** Author's PC: 64-bit, SecureBoot 0 (off), SetupMode 0, timeout 1 s,
en-US, Boot Loader GRUB 2.16, BootCurrent Boot0001 ("UEFI OS", the Linux entry), BootOrder
0001,0000,001D,001C,001E,001F, with a Windows Boot Manager entry and several USB-stick entries.

**Unusual values and what they suggest.**
- **SetupMode 1**: no Platform Key is installed. Secure Boot cannot be enforced until keys are
  enrolled (normal right after "Clear Secure Boot keys").
- **BootCurrent** not the first entry in BootOrder: the PC booted from a fallback, a one-time
  boot (boot menu) or the first entry failed.
- Stale entries (removed drives, old distributions) can be cleaned with `efibootmgr -b XXXX -B`.

**Where Linux gets it.** `/sys/firmware/efi/fw_platform_size` and efivars `SecureBoot`,
`SetupMode`, `AuditMode`, `DeployedMode`, `Timeout`, `PlatformLang`, `OsIndicationsSupported`,
`BootCurrent`, `BootOrder`, `Boot####`, and the systemd `Loader*` / `StubInfo` variables.
