# Buses / Devices (part 2 of 2)  (SRS Section Reference)

[Back to the section index](README.md) - [Glossary](../GLOSSARY.md)

[Part 1](buses-1.md)

This is the second half (alphabetical) of the Buses / Devices group. It covers the system resource maps (memory, I/O ports, interrupts), bus address translation, Thunderbolt / USB4, every USB view (bus, tree, devices, names, owners, flags, ID database), portable (MTP/PTP) devices, and two families of items for rare hardware: XMC mezzanine cards and the HP zx1 chipset. Open any item from **Menu -> Buses / Devices -> item**. The panel buttons lead to the busiest ones: **USB Bus** opens [USB bus](#usb-bus), and its sub-buttons are Link Devices, USB Disks, HID Bus and USB Info (the last opens [USB ID database](#usb-id-database)); **PCI Bus** has the sub-buttons Properties, PnP Dev, Drives, PCI Dev, UAA Dev and ALL Dev (see part 1). Only [System HAL resources](#system-hal-resources) and [System resources](#system-resources) need root data: without it the kernel hides the real addresses, so start the root helper once with **Menu -> Run as root (srs-root)**. In any panel, double-clicking a device ID written as `VVVV:DDDD` (for example `0bda:8153`) opens Device ID Lookup for that ID.

## Contents
- [System HAL resources](#system-hal-resources) `sys-hal`
- [System resources](#system-resources) `sys-res`
- [Thunderbolt](#thunderbolt) `thunder`
- [Bus address translation](#bus-address-translation) `translation`
- [UID database](#uid-database) `uid-info`
- [USB all](#usb-all) `usb-all`
- [USB bus](#usb-bus) `usb-bus`
- [USB devices](#usb-devices) `usb-dev`
- [USB flags](#usb-flags) `usb-flag`
- [USB flags detail](#usb-flags-detail) `usb-flags`
- [USB ID database](#usb-id-database) `usb-info`
- [USB names](#usb-names) `usb-names`
- [USB owner](#usb-owner) `usb-owner`
- [USB tree](#usb-tree) `usb-tree`
- [WPD tree (portable devices)](#wpd-tree-portable-devices) `wpd-tree`
- [XMC cards](#xmc-cards) `xmc`
- [XMC card 0](#xmc-card-0) `xmc-card-0`
- [XMC card 1](#xmc-card-1) `xmc-card-1`
- [XMC card 2](#xmc-card-2) `xmc-card-2`
- [XMC card 3](#xmc-card-3) `xmc-card-3`
- [XMC control](#xmc-control) `xmc-control`
- [XMC flash 0](#xmc-flash-0) `xmc-flash-0`
- [XMC flash 1](#xmc-flash-1) `xmc-flash-1`
- [XMC flash 2](#xmc-flash-2) `xmc-flash-2`
- [XMC flash 3](#xmc-flash-3) `xmc-flash-3`
- [XMC FPGA 0](#xmc-fpga-0) `xmc-fpga-0`
- [XMC FPGA 1](#xmc-fpga-1) `xmc-fpga-1`
- [XMC FPGA 2](#xmc-fpga-2) `xmc-fpga-2`
- [XMC FPGA 3](#xmc-fpga-3) `xmc-fpga-3`
- [XMC names](#xmc-names) `xmc-names`
- [XMC PCIe 0](#xmc-pcie-0) `xmc-pcie-0`
- [XMC PCIe 1](#xmc-pcie-1) `xmc-pcie-1`
- [XMC PCIe 2](#xmc-pcie-2) `xmc-pcie-2`
- [XMC PCIe 3](#xmc-pcie-3) `xmc-pcie-3`
- [XMC setup 0](#xmc-setup-0) `xmc-setup-0`
- [XMC setup 1](#xmc-setup-1) `xmc-setup-1`
- [XMC setup 2](#xmc-setup-2) `xmc-setup-2`
- [XMC setup 3](#xmc-setup-3) `xmc-setup-3`
- [XMC state 0](#xmc-state-0) `xmc-state-0`
- [XMC state 1](#xmc-state-1) `xmc-state-1`
- [XMC state 2](#xmc-state-2) `xmc-state-2`
- [XMC state 3](#xmc-state-3) `xmc-state-3`
- [XMC video 0](#xmc-video-0) `xmc-video-0`
- [XMC video 1](#xmc-video-1) `xmc-video-1`
- [XMC video 2](#xmc-video-2) `xmc-video-2`
- [XMC video 3](#xmc-video-3) `xmc-video-3`
- [ZX1 MIO-0](#zx1-mio-0) `zx1-mio-0`
- [ZX1 MIO-1](#zx1-mio-1) `zx1-mio-1`
- [ZX1 MIO-2](#zx1-mio-2) `zx1-mio-2`
- [ZX1 MIO-3](#zx1-mio-3) `zx1-mio-3`
- [ZX1 MIO-4](#zx1-mio-4) `zx1-mio-4`
- [ZX1 MIO-5](#zx1-mio-5) `zx1-mio-5`
- [ZX1 MIO-6](#zx1-mio-6) `zx1-mio-6`
- [ZX1 MIO-7](#zx1-mio-7) `zx1-mio-7`
- [ZX1 MIO-8](#zx1-mio-8) `zx1-mio-8`
- [ZX1 MIO-9](#zx1-mio-9) `zx1-mio-9`
- [ZX1 MIO-A](#zx1-mio-a) `zx1-mio-a`
- [ZX1 MIO-B](#zx1-mio-b) `zx1-mio-b`
- [ZX1 MIO-C](#zx1-mio-c) `zx1-mio-c`
- [ZX1 MIO-D](#zx1-mio-d) `zx1-mio-d`
- [ZX1 MIO-E](#zx1-mio-e) `zx1-mio-e`
- [ZX1 MIO-F](#zx1-mio-f) `zx1-mio-f`
- [ZX1 summary](#zx1-summary) `zx1-mio-s`

---
## System HAL resources  
`sys-hal` - Linux data - Needs root data

**In plain words.** A short map of the computer's physical address space: which address ranges are RAM, which belong to the firmware, and which are handed to PCI devices. It also shows the top-level map of the old x86 I/O ports. Only the top level is shown, so it fits on one screen.

**What you see.** Two lists, each line is `start-end   owner`:

| Part | What it means | Why it matters |
|---|---|---|
| Physical memory map (top level) | Every top-level range from the kernel's memory map. Owners include `System RAM`, `Reserved`, `ACPI Tables`, `ACPI Non-volatile Storage`, `PCI Bus 0000:00` (a window of addresses given to PCI devices), `PCI ECAM` (PCI configuration space), `IOAPIC`, `HPET`, `amd_iommu`, `RAM buffer` and ACPI device names like `PNP0C02:04` (motherboard resources). | Shows how much RAM the firmware gave the operating system and where devices live. |
| I/O port map (top level) | The 64K x86 I/O port space (0000-ffff). Usually almost all of it belongs to `PCI Bus 0000:00`, plus `PCI conf1` at 0cf8-0cff (the old way to reach PCI configuration). | Rarely matters today; legacy devices (serial ports, PS/2 keyboard, SMBus) still use ports. |
| Footer | "Addresses from the srs-root cache." when root data was used. Without root data the addresses are zeroed by the kernel, so SRS shows only the names, adds "Addresses are zeroed by the kernel for non-root readers." and the hint "Needs root: Menu > Run as root (srs-root)". | Tells you whether the numbers are real. |

**Healthy / typical values.** On the author's PC (32 GB of RAM) the map starts with small `System RAM` and `Reserved` pieces below 1 MB, has `ACPI Tables` and `ACPI Non-volatile Storage` near 0x6D000000, a big `PCI Bus 0000:00` window from 0x80000000 to 0xDFFFFFFF, `PCI ECAM 0000 [bus 00-ff]` at 0xE0000000, and the last `System RAM` range ends at 0x87DEFFFFF (just under 34 GiB). That is normal: about 2 GiB below 4 GiB is used by devices, and the firmware moves ("remaps") that RAM above 4 GiB. A large `PCI Bus` window high above (here 0x10000000000-0x1FFFFFFFFFF) holds 64-bit device memory such as a graphics card's VRAM window.

**Unusual values and what they suggest.**
- Total `System RAM` far below the installed amount: memory remapping off in the BIOS, a stick not detected, or a 32-bit limitation.
- All addresses shown as names only: no root data yet (start srs-root).
- No high (above 4 GiB) `PCI Bus` window: "Above 4G decoding" is off in the BIOS, so Resizable BAR cannot work.

**Where Linux gets it.** `/proc/iomem` and `/proc/ioports`, depth 0 (top level only). Real addresses need root, so SRS reads the copy saved by srs-root.

**Background.** CPUs talk to RAM and to devices through the same address space. Device registers mapped into it are called MMIO (memory-mapped I/O). The firmware (UEFI/BIOS) builds this map at boot and reports it to Linux (the "e820" map). "HAL" is the Windows Hardware Abstraction Layer, the name SIV uses for this page.

---
## System resources  
`sys-res` - Linux data - Needs root data

**In plain words.** The full list of hardware resources in use: interrupts, legacy DMA channels, I/O ports and the memory map, one level deeper than [System HAL resources](#system-hal-resources).

**What you see.** Four parts:

| Part | What it means | Why it matters |
|---|---|---|
| Interrupts (IRQ, total count, controller / devices) | One line per interrupt: the IRQ number (or a short name), the total count summed over all CPUs since boot, the interrupt controller and trigger type, and the device(s) using it. | Shows which devices are busy and whether an interrupt is shared or stuck. |
| DMA channels | The old ISA DMA channels in use. | Almost always just `4: cascade` today. |
| I/O ports | The whole I/O port tree, nested by indentation (for example `PNP0C09:00` with `EC data` under it). | Shows legacy devices: timer, PS/2 keyboard, RTC, serial port, ACPI PM blocks, SMBus (`piix4_smbus`). |
| Memory map | The memory map with the top level plus one level of children (for example `Kernel code` inside `System RAM`, or the sub-windows of each PCI bridge). | Shows where each PCI bridge's address window sits. |

Controller names in the interrupt list: `IR-IO-APIC` (I/O APIC, with interrupt remapping by the IOMMU), `IR-PCI-MSI-<address>` and `IR-PCI-MSIX-<address>` (message-signalled interrupts from that PCI device). The text after it, like `2-edge` or `9-fasteoi`, is the hardware line number and trigger mode. Named rows (shown grey) are per-CPU events: `NMI` (non-maskable interrupts), `LOC` (local timer), `RES` (rescheduling), `CAL` (function calls between CPUs), `TLB` (TLB shootdowns), `TRM` (thermal events), `MCE` (machine check exceptions), `MCP` (machine check polls) and so on.

**Healthy / typical values.** On the author's PC each NVMe drive has 17 MSI-X vectors (`nvme0q0` to `nvme0q16`), one per CPU queue; the busiest single source is the chipset USB controller `xhci_hcd` on 0000:0b:00.0 with about 10 million interrupts after 21 hours. `MCE 0` and `TRM 0` are what you want. `DMA channels` shows only `4: cascade`.

**Unusual values and what they suggest.**
- `MCE` above 0: the CPU reported a hardware error (memory, cache or bus). Check the kernel log and memory stability.
- `TRM` above 0: the CPU hit a thermal limit.
- One device with a huge count while idle (millions per minute): an "interrupt storm", often a misbehaving USB or GPIO device.
- A device still on `IR-IO-APIC` instead of MSI: older or legacy-mode hardware, and the line may be shared.
- An NVMe drive with only one or two vectors: the driver fell back to fewer queues (rare; can lower performance).

**Where Linux gets it.** `/proc/interrupts`, `/proc/dma`, `/proc/ioports` (all levels) and `/proc/iomem` (two levels). Addresses need root and come from the srs-root cache.

**Background.** An interrupt is a signal a device sends to make the CPU stop and handle it. Old PCs had 16 shared lines (IRQs). Modern PCIe devices send MSI / MSI-X "messages" instead, so each device (or each queue) gets its own vector and nothing is shared.

---
## Thunderbolt  
`thunder` - Not present on most PCs

**In plain words.** Shows whether the PC has a Thunderbolt / USB4 host and lists the related PCI devices.

**What you see.**
- `/sys/bus/thunderbolt` - `present` or `absent (thunderbolt module not loaded)`. Present means the kernel's Thunderbolt/USB4 driver is running.
- `USB4 host` lines - every PCI device whose class is "USB4 host interface" (class code 0C0340), with its PCI address and name.
- `Thunderbolt` lines - other PCI devices with "Thunderbolt" in their name (for example Intel Thunderbolt controllers that are not class 0C0340).
- `0-0`, `1-0`, ... lines - the routers the kernel's thunderbolt driver found (`/sys/bus/thunderbolt/devices`): vendor, name, `USB4` or `Thunderbolt <generation>`, and the NVM firmware version.
- `domainN` lines - each Thunderbolt / USB4 domain and its security level (`none`, `user`, `secure`, `dponly`, `usbonly`).
- If nothing is found: `USB4 controller  none enumerated on the PCI bus`, followed by a short note on likely reasons (USB4 disabled in the firmware or not powered up on this boot).

**Healthy / typical values.** On the author's ASUS X870 board: `/sys/bus/thunderbolt present`, a `USB4 host` at 0000:74:00.0 (ASMedia ASM4242 USB 4 / Thunderbolt 3 Host Router), router `0-0  ASMedia Technology Inc. ASM4242  (USB4, NVM 200111.250708)` and `domain0  security level user`.

**Unusual values and what they suggest.**
- Board has USB4 ports but nothing is listed: USB4 disabled in the BIOS, or the controller did not come up (a cold power-off sometimes helps).
- Switch ports listed but nothing behind them: normal when no Thunderbolt/USB4 PCIe device (dock, eGPU, NVMe enclosure) is plugged in.

**Where Linux gets it.** `/sys/bus/thunderbolt` and the PCI device list in `/sys/bus/pci/devices` (class 0C0340, vendor 1B21).

**Background.** USB4 (and Thunderbolt 3/4, which it is based on) can tunnel PCI Express (PCIe) over the cable. That is why a USB4 chip shows up as a PCIe switch: devices you plug in appear behind its downstream ports as if they were in a slot. Each PCI device is named by its address `domain:bus:device.function`, for example 0000:74:00.0. A PCIe link has a speed (generation: 2.5, 5, 8, 16, 32 GT/s for Gen 1-5) and a width (number of lanes: x1, x4, x16); the tunnel is limited by both the cable and the chip's own link.

---
## Bus address translation  
`translation` - Linux data

**In plain words.** For each PCI device memory or I/O window (a BAR), shows the address the device sees on the bus and the address the CPU uses for it.

**What you see.**

| Column | What it means | Why it matters |
|---|---|---|
| Location | PCI address `bus:device.function` (domain 0000 left out). | Which device. |
| BAR | `BAR 0` to `BAR 5`, or `ROM` (the option ROM). Only BARs that are in use are listed. | A device can have several windows: registers, VRAM, doorbells. |
| Bus Address | Where the window starts as seen from the PCI bus. | |
| CPU Address | Where the CPU finds the same window in its physical address space. | |
| Space | `Memory` (MMIO) or `I/O` (legacy port space). | |

A note at the bottom explains that on x86-64 PCI bus addresses map 1:1 to CPU addresses, so both columns are always the same here; the IOMMU (input-output memory management unit) only translates DMA traffic from devices to RAM.

**Healthy / typical values.** On the author's PC the graphics card 03:00.0 has `BAR 0` at 0x1F800000000 and `BAR 2` at 0x1FC00000000 (both far above 4 GiB), `BAR 4` as `I/O` at 0xF000, `BAR 5` (registers) at 0xDD300000 and a `ROM`. The 16 GiB gap between BAR 0 and BAR 2 matches the card's 16 GB of VRAM, which suggests Resizable BAR is active. NVMe drives and USB controllers each have one memory BAR below 4 GiB.

**Unusual values and what they suggest.**
- Graphics card BAR 0 only 256 MB below 4 GiB: Resizable BAR (or "Above 4G decoding") is off.
- A device you expect is missing: it has no assigned resources (BIOS ran out of address space, or the device is disabled).

**Where Linux gets it.** `/sys/bus/pci/devices/*/resource` (start, end and flags of each BAR).

**Background.** A BAR (Base Address Register) is how a PCI device asks for a piece of the address space. The firmware or kernel assigns each BAR a start address at boot. On some non-x86 systems the host bridge adds an offset, so bus and CPU addresses differ; that is what Windows "HalTranslateBusAddress" reports.

---
## UID database  
`uid-info` - Linux data

**In plain words.** Information about the USB ID name database SRS uses to turn numbers like `0bda:8153` into names. It shows the same page as [USB ID database](#usb-id-database).

**What you see.** Same fields as [USB ID database](#usb-id-database): Database, SIV equivalent, Version, Date, Records, Load time, Lookup chain, Class names.

**Healthy / typical values.** On the author's PC: `/usr/share/hwdata/usb.ids`, version 2026.06.26, 23,964 names.

**Where Linux gets it.** `/usr/share/hwdata/usb.ids` (the `hwdata` package).

---
## USB all  
`usb-all` - Linux data

**In plain words.** Every USB device, root hub and hub with all its details: IDs, strings, speed, power, interfaces, drivers and endpoints. This is the long "everything" view.

**What you see.** One block per device. The first line is the kernel name (`usb1`, `1-2.3.4`) and a description. Then:

| Field | What it means | Why it matters |
|---|---|---|
| Bus / Device | Bus number, device number and port path, e.g. `Bus 001 Device 019  port path 2.3.4`. Root hubs show `port path 0 (root hub)`. | Device numbers change each time you replug; the port path tells you the physical route. |
| IDs | `VID_xxxx PID_xxxx REV_xxxx`: vendor ID, product ID and the device's release number (bcdDevice). | The IDs identify the chip; drivers match on them. |
| Database | Vendor and product names from usb.ids, or `(vendor not in usb.ids)`. | Helps when the device's own strings are generic ("Generic", "USB2.0 Hub"). |
| Manufacturer / Product / Serial | Strings the device reports about itself (only shown if present). | Cheap devices often report fake or empty strings. |
| USB version | The USB version the device claims (bcdUSB) and the speed it actually connected at, e.g. `3.20  10Gbps SuperSpeed+`. | The speed is what matters for performance. |
| Device class | Class code and name; `00 (Defined at Interface level)` means each interface says its own class. | |
| Max power | Current the device asked for (bMaxPower). | USB 2 ports supply up to 500 mA, USB 3 up to 900 mA. |
| Hub ports | Number of downstream ports (hubs only). | |
| Controller | PCI address and name of the host controller, or the virtual host for USB/IP. | Tells you which chip (CPU, chipset, add-on) the port belongs to. |
| Removable | `removable`, `fixed` (built in, like an internal LED controller) or `unknown`, from the firmware's port description. | |
| Quirks | Kernel workarounds active for this device, in hex (shown in orange, only when not 0). See [USB flags detail](#usb-flags-detail). | |
| Interface | One line per interface: name (`1-2:1.0` = configuration 1, interface 0), class/subclass/protocol in hex and their most specific name, number of endpoints, bound driver (or `(none)`) and the interface's own string. | Each function of a device (keyboard, mouse, media keys) is a separate interface. |
| Endpoint lines | `Endpoint Address 0x81 (in)  Interrupt  Maximum Packet Size 0x0004  Interval 256ms`: address and direction, transfer type (Control, Interrupt, Bulk, Isoc), largest packet in bytes (hex) and polling interval. | Bulk packet size 0x0400 (1024) means SuperSpeed; 0x0200 (512) High Speed; 0x0040 (64) Full Speed. |

A footer line gives totals (USB entities, root hubs, external hubs, devices), the time taken and the usb.ids file, date and version.

**Healthy / typical values.** On the author's PC, `2-1` is a JMicron JMS583 USB-to-NVMe bridge (`152d:0583`), `USB version 3.20  10Gbps SuperSpeed+`, `Max power 896mA`, interface `08/06/62 USB Attached SCSI  4 endpoints  driver uas`. The Razer mouse `5-1` runs at `12Mbps Full Speed` with interface `03/01/02 Mouse` (driver usbhid), which is normal for mice.

**Unusual values and what they suggest.**
- A USB 3 device (USB version 3.x) at `480Mbps High Speed`: it is on a USB 2 port, a USB 2 cable or hub, or the USB 3 contacts are dirty.
- A storage device with `08/06/50 Bulk-Only` instead of `08/06/62 USB Attached SCSI` (UAS): slower, older protocol, or the kernel disabled UAS with a quirk.
- An interface with `driver (none)`: no Linux driver claimed it. Normal for vendor-specific parts (RGB, IR blasters) that a user program talks to; a problem if it is your keyboard or network adapter.
- Garbage product strings (odd characters): a cheap device with broken string descriptors; harmless.

**Where Linux gets it.** `/sys/bus/usb/devices/*` (idVendor, idProduct, bcdDevice, speed, version, bMaxPower, strings, quirks, removable, interfaces and their `ep_*` endpoint folders) plus usb.ids for names. Similar to `lsusb -v`.

**Background.** USB descriptors are small data tables every device sends when plugged in. The device descriptor holds the IDs and class; each interface descriptor describes one function; each endpoint is a one-way data pipe. USB speeds: Low Speed 1.5 Mbps, Full Speed 12 Mbps (USB 1.1), High Speed 480 Mbps (USB 2.0), SuperSpeed 5 Gbps (USB 3.2 Gen 1), SuperSpeed+ 10 Gbps (Gen 2) and 20 Gbps (Gen 2x2).

---
## USB bus  
`usb-bus` - Linux data

**In plain words.** The main USB panel (the **USB Bus** button): one line per root hub, hub and device, indented to show what plugs into what.

**What you see.**

| Column | What it means |
|---|---|
| Connection | `Hub N` for a root hub (N = bus number), or `Port x.y.z` indented by depth for anything behind it. |
| Vendor and Device Description | Manufacturer and product strings, or names from usb.ids. Root hubs show the host controller's PCI name. |
| Version | The USB version the device claims (bcdUSB). |
| mA | Requested current in milliamps. |
| Device Class | `V2 Root Hub` / `V3 Root Hub` for root hubs, otherwise the device class (or the first interface's class when the device defines it per interface), e.g. `Hub`, `Mass Storage`, `Human Interface Device`. |
| Device Information | Connected speed, port count for hubs, and the driver(s) bound to its interfaces. |

The footer is the same totals line as [USB all](#usb-all) (entities, root hubs, external hubs, devices, usb.ids version), followed by the motherboard name and BIOS version.

**Healthy / typical values.** On the author's PC: 40 USB entities, 11 root hubs, 11 external hubs and 18 devices. Every USB controller makes two buses: a USB 2 one and a USB 3 one (Hub 1 = `V2 Root Hub 480Mbps`, Hub 2 = `V3 Root Hub 20Gbps SuperSpeed+ 20Gbps` on the ASMedia ASM4242). Hubs 10 and 11 are `USB/IP Virtual Host Controller` (vhci_hcd), a software controller for USB over the network. A USB 3 hub appears twice, once on each bus (VIA `USB2.0 Hub` on Hub 1 and `USB3.0 Hub` on Hub 2).

**Unusual values and what they suggest.**
- A fast device under a `V2 Root Hub` only: it is running at USB 2 speed (see [USB all](#usb-all)).
- mA total on one bus-powered hub above 500: devices may disconnect; use a powered hub.
- Very deep chains (hub in hub in hub): each extra hub adds latency and can cause dropouts.

**Where Linux gets it.** `/sys/bus/usb/devices` and usb.ids. Similar to `lsusb -t`.

**Background.** A root hub is the virtual hub built into each USB host controller (xHCI, eXtensible Host Controller Interface, is the modern controller type). External hubs fan one port out to several. The port path 2.3.4 means "root port 2, then port 3 of that hub, then port 4 of the next hub".

---
## USB devices  
`usb-dev` - Linux data

**In plain words.** A compact one-line-per-device list, like `lsusb`.

**What you see.** Kernel name (`usb1`, `1-2.3`), `Bus NNN Device NNN`, `ID vvvv:pppp` and the description. A note at the end says you can open one device on its own: `usb-dev:<name>`, for example `usb-dev:1-1` or `usb-dev:usb3`, which shows the full detail block from [USB all](#usb-all) for just that device. Double-click an `ID vvvv:pppp` value to open Device ID Lookup for it.

**Healthy / typical values.** On the author's PC: `8-8.4.4  Bus 008 Device 013  ID 046d:c52f Logitech USB Receiver`, `5-2  Bus 005 Device 003  ID 1038:1612 SteelSeries SteelSeries Apex 7`. Root hubs always have `ID 1d6b:0002` (USB 2) or `1d6b:0003` (USB 3), the Linux Foundation's IDs.

**Unusual values and what they suggest.** A device missing from the list was not enumerated: bad cable, not enough power, or the port is disabled. Asking for a name that does not exist prints "No such USB device".

**Where Linux gets it.** `/sys/bus/usb/devices` and usb.ids.

---
## USB flags  
`usb-flag` - Linux data

**In plain words.** SIV's Windows "UsbFlags" page (per-device USB settings in the registry). On Linux SRS shows the equivalent: the USB core driver's settings and every per-device quirk the kernel applied. The page is identical to [USB flags detail](#usb-flags-detail); read that section for the fields.

**Where Linux gets it.** `/sys/module/usbcore/parameters/*` and `/sys/bus/usb/devices/*/quirks`.

---
## USB flags detail  
`usb-flags` - Linux data

**In plain words.** The settings of the Linux USB core and the list of devices that need special kernel workarounds ("quirks").

**What you see.** First the usbcore parameters:

| Field | What it means | Typical |
|---|---|---|
| quirks | Extra quirks added by the user on the kernel command line (`usbcore.quirks=VID:PID:flags`). `(empty)` = none. | `(empty)` |
| autosuspend | Seconds of idle before a USB device is put to sleep. `-1` = autosuspend off. | `2` (kernel default) |
| authorized_default | Whether new devices are allowed automatically: `1` or `-1` all, `0` none, `2` internal only. | `1` |
| old_scheme_first / use_both_schemes | How the kernel first talks to a new device (two enumeration methods). | `N` / `Y` |
| initial_descriptor_timeout | Milliseconds to wait for a new device's first descriptor. | `5000` |
| usbfs_memory_mb | Memory limit for programs that talk to USB devices directly (usbfs), in MB. | `16` (kernel default) |
| nousb | `Y` would disable USB completely. | `N` |

Then one line per device with active quirks: kernel name, `quirks 0x...` in hex and the `VID:PID`. If there are none: "No per-device USB quirks active" (with the Windows registry path for comparison). You can double-click the VID:PID to open Device ID Lookup.

**Healthy / typical values.** On the author's PC `autosuspend -1` (turned off by the distribution's tuning) and `usbfs_memory_mb 256` (raised, common for cameras and capture devices). Four devices have quirks: the VIA hub `2109:2817` and the Realtek network adapter `0BDA:8153` have `0x400`, and two Terminus hubs `1A40:0101` have `0x4000`. These come from the kernel's built-in quirk table, not from the user.

**Unusual values and what they suggest.**
- Common quirk bits: `0x2` reset-resume (needs reset after sleep), `0x40` delay-init, `0x400` no LPM (Link Power Management off, used for devices that drop out in power-saving), `0x4000` hub slow reset. A device that randomly disconnects may need a quirk added in `quirks`.
- `nousb Y`: USB disabled on the kernel command line.
- `authorized_default 0`: new devices are blocked until authorised (security setups, USBGuard).

**Where Linux gets it.** `/sys/module/usbcore/parameters/*` and `/sys/bus/usb/devices/*/quirks`. The quirk bits are defined in the kernel header `linux/usb/quirks.h`.

---
## USB ID database  
`usb-info` - Linux data

**In plain words.** Facts about the file that turns USB vendor and product numbers into names (the **USB Info** button).

**What you see.**

| Field | What it means |
|---|---|
| Database | The path of the file SRS loaded. |
| SIV equivalent | SIV's own file for the same job (`usbdevs.txt`). |
| Version / Date | Version and date written in the file's header. |
| Records | How many names it holds (vendors, devices, subsystems). |
| Load time | Time to load the file, and time for this lookup. |
| Lookup chain | Order SRS tries when naming a device: `V:D -> V` = vendor plus device first, then vendor only. |
| Class names | Number of class / subclass / protocol names (used for columns like "Device Class"). |

If the file cannot be found SRS prints "ids file not found" and names are left blank.

**Healthy / typical values.** On the author's PC: `/usr/share/hwdata/usb.ids`, `Version 2026.06.26`, `23,964 names`, `Load time 4.291 ms`, `Class names 141`.

**Unusual values and what they suggest.** A date more than a year old means new devices show `(vendor not in usb.ids)` or no product name; update the `hwdata` package.

**Where Linux gets it.** `/usr/share/hwdata/usb.ids` (community list at linux-usb.org).

**Background.** Every USB vendor buys a 16-bit vendor ID (VID) from the USB Implementers Forum and numbers its products with a 16-bit product ID (PID). The ids file is a volunteer-maintained list of these numbers and names.

---
## USB names  
`usb-names` - Linux data

**In plain words.** Puts the name a device reports about itself next to the name from the ID database, so you can see which is more useful.

**What you see.**

| Column | What it means |
|---|---|
| Device | Kernel name. |
| VID:PID | Vendor and product ID (double-click to open Device ID Lookup). |
| Device strings | `Manufacturer / Product` as reported by the device. |
| usb.ids name | Vendor and product name from the database, or `-` if the vendor is unknown. |

**Healthy / typical values.** On the author's PC the card reader `05E3:0749` calls itself `Generic / USB3.0 Card Reader`, while the database says `Genesys Logic, Inc.  SD Card Reader and Writer`; the Bluetooth adapter `13D3:3617` says `Realtek / Bluetooth Radio` while the database names the module maker `IMC Networks`. Both are normal: the chip maker and the board maker are often different companies.

**Unusual values and what they suggest.**
- A strange vendor in the database column (for example a famous brand for a no-name device): the maker used someone else's VID, which is common with cheap devices.
- `-` in the last column: vendor not in usb.ids (new, unregistered or fake ID).
- Unreadable device strings: broken string descriptors.

**Where Linux gets it.** `/sys/bus/usb/devices/*/manufacturer`, `product` and usb.ids.

---
## USB owner  
`usb-owner` - Linux data

**In plain words.** Which driver owns each USB interface, and which kernel module that driver lives in.

**What you see.**

| Column | What it means |
|---|---|
| Interface | Interface name, e.g. `5-1:1.0`. |
| Class | Interface class name (Hub, Human Interface Device, Mass Storage, Printer, Video, Wireless, Vendor Specific Class...). |
| Driver | Bound driver, or `(none)`. |
| Module | Kernel module that provides the driver, or `built-in` if compiled into the kernel; blank when no driver is bound. |

**Healthy / typical values.** On the author's PC hubs use `hub` from `usbcore`, keyboards and mice `usbhid`, the NVMe enclosure `uas`, the webcam `uvcvideo`, Bluetooth `btusb`, the network adapter `r8152`. Some interfaces show `usbfs` (module usbcore): a user program opened the device directly, for example the printer's IPP-over-USB interfaces or a keyboard's lighting interface.

**Unusual values and what they suggest.** `(none)` on an interface that should work (a network adapter, a sound card): the driver module is missing or blacklisted. `usb-storage` instead of `uas` for a fast SSD enclosure: UAS was refused or disabled by a quirk.

**Where Linux gets it.** `/sys/bus/usb/devices/*:*/driver` and `driver/module` symlinks.

**Background.** One USB device can have several interfaces, each with its own driver. A gaming mouse, for example, may have a mouse interface and two keyboard interfaces for its macro buttons.

---
## USB tree  
`usb-tree` - Linux data

**In plain words.** The USB layout as a text tree, in the same style as `lsusb -t`.

**What you see.** For each bus: `/:  Bus NNN.Port 1: Dev 1, Class=root_hub, <driver>, <speed>M  [<controller>]`. Under it, each device as `|__ Port x.y: Dev N, VVVV:PPPP <description>, <speed>M`, and under each device one line per interface: `If 1.0, Class=..., Driver=...` (`[none]` when no driver). Speeds are in Mbit/s: 1.5, 12, 480, 5000, 10000, 20000. You can double-click a VVVV:PPPP ID to open Device ID Lookup.

**Healthy / typical values.** On the author's PC `Bus 002 ... 20000M [0000:73:00.0]` is the ASM4242's USB 3 bus, with the JMS583 NVMe bridge at `10000M` and a VIA USB 3 hub at `5000M`; `Bus 010` and `Bus 011` are the USB/IP virtual controller `[vhci_hcd.0]`.

**Unusual values and what they suggest.** A device shown at a lower speed than its parent hub and than it supports: cable or port problem. A device whose parent is the wrong hub: it is plugged into a different port than you thought.

**Where Linux gets it.** `/sys/bus/usb/devices`.

---
## WPD tree (portable devices)  
`wpd-tree` - Linux data

**In plain words.** Lists phones, cameras and media players that connect as "portable devices" (MTP or PTP) instead of as a USB drive. WPD (Windows Portable Devices) is the Windows name for this.

**What you see.** One line per device: kernel name, description, and in brackets how it was detected: `[PTP/MTP still image interface]` (an interface of class 06, Still Image, or an interface named MTP) or `[MTP (udev ID_MTP_DEVICE)]` (udev's device database marks it as MTP). If none: "No MTP/PTP portable devices attached (checked USB interface class 06 and udev ID_MTP_DEVICE)."

**Healthy / typical values.** The author's PC had no phone or camera attached, so the "No MTP/PTP portable devices attached" line is shown. An Android phone set to "File transfer" would appear here.

**Unusual values and what they suggest.** A phone plugged in but not listed: it is in "charging only" mode, or the phone is locked (many phones hide MTP until unlocked).

**Where Linux gets it.** USB interface class 06 and the interface string in `/sys/bus/usb/devices`, plus the udev property `ID_MTP_DEVICE`.

**Background.** MTP (Media Transfer Protocol) and PTP (Picture Transfer Protocol) let the device keep control of its own storage and hand over files one at a time, instead of exposing a raw disk. On Linux, file managers reach them through gvfs or kio (KDE).

---
## XMC cards  
`xmc` - Not present on most PCs

**In plain words.** The summary page for XMC cards. XMC (Switched Mezzanine Card, standard ANSI/VITA 42) is a small plug-in module used in industrial, military and telecom computers. It sits on a carrier board (VME, VPX, CompactPCI or a PCIe adapter) and talks to it over PCI Express. Many XMC cards carry an FPGA (field-programmable gate array, a chip whose logic is loaded from a file), often from Xilinx (now AMD), plus flash memory and video or signal inputs. SRS lists this as "no Xilinx XMC card".

**What you see.** On a PC without such a card: `Status  Not present on this system` and `Checked  n/a (no Xilinx XMC card)`. The other XMC items below are the per-card pages SIV offers for up to four cards (0 to 3) and show the same two lines.

**What "not present" means.** Nothing is wrong. Desktop and gaming PCs never have XMC cards. On a system with one, the card shows up first as an ordinary PCI device in the PCI views (see part 1), where its link speed and width can be checked.

---
## XMC card 0  
`xmc-card-0` - Not present on most PCs

**In plain words.** The page for XMC card number 0: the identity of the card in that position (which card and model it is).

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC card 1  
`xmc-card-1` - Not present on most PCs

**In plain words.** The page for XMC card number 1: the identity of the card in that position (which card and model it is).

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC card 2  
`xmc-card-2` - Not present on most PCs

**In plain words.** The page for XMC card number 2: the identity of the card in that position (which card and model it is).

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC card 3  
`xmc-card-3` - Not present on most PCs

**In plain words.** The page for XMC card number 3: the identity of the card in that position (which card and model it is).

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC control  
`xmc-control` - Not present on most PCs

**In plain words.** The control page for XMC cards: the shared control settings of the card driver.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC flash 0  
`xmc-flash-0` - Not present on most PCs

**In plain words.** The flash memory page for XMC card 0. The flash chip on such a card stores the FPGA's configuration file and the card's firmware.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC flash 1  
`xmc-flash-1` - Not present on most PCs

**In plain words.** The flash memory page for XMC card 1. The flash chip on such a card stores the FPGA's configuration file and the card's firmware.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC flash 2  
`xmc-flash-2` - Not present on most PCs

**In plain words.** The flash memory page for XMC card 2. The flash chip on such a card stores the FPGA's configuration file and the card's firmware.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC flash 3  
`xmc-flash-3` - Not present on most PCs

**In plain words.** The flash memory page for XMC card 3. The flash chip on such a card stores the FPGA's configuration file and the card's firmware.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC FPGA 0  
`xmc-fpga-0` - Not present on most PCs

**In plain words.** The FPGA page for XMC card 0: the programmable chip at the heart of the card and the design loaded into it.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC FPGA 1  
`xmc-fpga-1` - Not present on most PCs

**In plain words.** The FPGA page for XMC card 1: the programmable chip at the heart of the card and the design loaded into it.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC FPGA 2  
`xmc-fpga-2` - Not present on most PCs

**In plain words.** The FPGA page for XMC card 2: the programmable chip at the heart of the card and the design loaded into it.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC FPGA 3  
`xmc-fpga-3` - Not present on most PCs

**In plain words.** The FPGA page for XMC card 3: the programmable chip at the heart of the card and the design loaded into it.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC names  
`xmc-names` - Not present on most PCs

**In plain words.** The list of names (models) of the XMC cards found.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC PCIe 0  
`xmc-pcie-0` - Not present on most PCs

**In plain words.** The PCI Express page for XMC card 0: how the card is connected to the carrier (link speed in GT/s and width in lanes, x1 to x8). A card running below its rated link loses bandwidth.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC PCIe 1  
`xmc-pcie-1` - Not present on most PCs

**In plain words.** The PCI Express page for XMC card 1: how the card is connected to the carrier (link speed in GT/s and width in lanes, x1 to x8). A card running below its rated link loses bandwidth.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC PCIe 2  
`xmc-pcie-2` - Not present on most PCs

**In plain words.** The PCI Express page for XMC card 2: how the card is connected to the carrier (link speed in GT/s and width in lanes, x1 to x8). A card running below its rated link loses bandwidth.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC PCIe 3  
`xmc-pcie-3` - Not present on most PCs

**In plain words.** The PCI Express page for XMC card 3: how the card is connected to the carrier (link speed in GT/s and width in lanes, x1 to x8). A card running below its rated link loses bandwidth.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC setup 0  
`xmc-setup-0` - Not present on most PCs

**In plain words.** The setup (configuration) page for XMC card 0.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC setup 1  
`xmc-setup-1` - Not present on most PCs

**In plain words.** The setup (configuration) page for XMC card 1.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC setup 2  
`xmc-setup-2` - Not present on most PCs

**In plain words.** The setup (configuration) page for XMC card 2.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC setup 3  
`xmc-setup-3` - Not present on most PCs

**In plain words.** The setup (configuration) page for XMC card 3.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC state 0  
`xmc-state-0` - Not present on most PCs

**In plain words.** The state page for XMC card 0: its current running status.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC state 1  
`xmc-state-1` - Not present on most PCs

**In plain words.** The state page for XMC card 1: its current running status.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC state 2  
`xmc-state-2` - Not present on most PCs

**In plain words.** The state page for XMC card 2: its current running status.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC state 3  
`xmc-state-3` - Not present on most PCs

**In plain words.** The state page for XMC card 3: its current running status.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC video 0  
`xmc-video-0` - Not present on most PCs

**In plain words.** The video page for XMC card 0, for cards that capture or output video.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC video 1  
`xmc-video-1` - Not present on most PCs

**In plain words.** The video page for XMC card 1, for cards that capture or output video.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC video 2  
`xmc-video-2` - Not present on most PCs

**In plain words.** The video page for XMC card 2, for cards that capture or output video.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## XMC video 3  
`xmc-video-3` - Not present on most PCs

**In plain words.** The video page for XMC card 3, for cards that capture or output video.

**What you see.** `Status  Not present on this system` and `Checked  n/a (no XMC card)`. See [XMC cards](#xmc-cards) for what XMC is.

---
## ZX1 MIO-0  
`zx1-mio-0` - Not present on most PCs

**In plain words.** The first of sixteen register pages for the HP zx1 chipset. The zx1 was HP's chipset for Intel Itanium 2 servers and workstations (for example the HP rx2600 server and zx6000 workstation) in the early 2000s. Its MIO (Memory and I/O controller) is the main chip that links the processors to memory and to the I/O adapters that drive the PCI and AGP buses. SIV can show its registers in pages 0 to F, plus a summary.

**What you see.** On any ordinary PC: `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`.

**What "not present" means.** Nothing is wrong. The zx1 only exists in HP Itanium (IA-64) machines. A modern x86-64 PC (Intel or AMD) can never have one, so all ZX1 items always say "not present" there.

---
## ZX1 MIO-1  
`zx1-mio-1` - Not present on most PCs

**In plain words.** Register page 1 of the HP zx1 Memory and I/O controller (MIO), found only in HP Itanium 2 systems.

**What you see.** `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`. See [ZX1 MIO-0](#zx1-mio-0) for background.

---
## ZX1 MIO-2  
`zx1-mio-2` - Not present on most PCs

**In plain words.** Register page 2 of the HP zx1 Memory and I/O controller (MIO), found only in HP Itanium 2 systems.

**What you see.** `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`. See [ZX1 MIO-0](#zx1-mio-0) for background.

---
## ZX1 MIO-3  
`zx1-mio-3` - Not present on most PCs

**In plain words.** Register page 3 of the HP zx1 Memory and I/O controller (MIO), found only in HP Itanium 2 systems.

**What you see.** `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`. See [ZX1 MIO-0](#zx1-mio-0) for background.

---
## ZX1 MIO-4  
`zx1-mio-4` - Not present on most PCs

**In plain words.** Register page 4 of the HP zx1 Memory and I/O controller (MIO), found only in HP Itanium 2 systems.

**What you see.** `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`. See [ZX1 MIO-0](#zx1-mio-0) for background.

---
## ZX1 MIO-5  
`zx1-mio-5` - Not present on most PCs

**In plain words.** Register page 5 of the HP zx1 Memory and I/O controller (MIO), found only in HP Itanium 2 systems.

**What you see.** `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`. See [ZX1 MIO-0](#zx1-mio-0) for background.

---
## ZX1 MIO-6  
`zx1-mio-6` - Not present on most PCs

**In plain words.** Register page 6 of the HP zx1 Memory and I/O controller (MIO), found only in HP Itanium 2 systems.

**What you see.** `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`. See [ZX1 MIO-0](#zx1-mio-0) for background.

---
## ZX1 MIO-7  
`zx1-mio-7` - Not present on most PCs

**In plain words.** Register page 7 of the HP zx1 Memory and I/O controller (MIO), found only in HP Itanium 2 systems.

**What you see.** `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`. See [ZX1 MIO-0](#zx1-mio-0) for background.

---
## ZX1 MIO-8  
`zx1-mio-8` - Not present on most PCs

**In plain words.** Register page 8 of the HP zx1 Memory and I/O controller (MIO), found only in HP Itanium 2 systems.

**What you see.** `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`. See [ZX1 MIO-0](#zx1-mio-0) for background.

---
## ZX1 MIO-9  
`zx1-mio-9` - Not present on most PCs

**In plain words.** Register page 9 of the HP zx1 Memory and I/O controller (MIO), found only in HP Itanium 2 systems.

**What you see.** `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`. See [ZX1 MIO-0](#zx1-mio-0) for background.

---
## ZX1 MIO-A  
`zx1-mio-a` - Not present on most PCs

**In plain words.** Register page A of the HP zx1 Memory and I/O controller (MIO), found only in HP Itanium 2 systems.

**What you see.** `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`. See [ZX1 MIO-0](#zx1-mio-0) for background.

---
## ZX1 MIO-B  
`zx1-mio-b` - Not present on most PCs

**In plain words.** Register page B of the HP zx1 Memory and I/O controller (MIO), found only in HP Itanium 2 systems.

**What you see.** `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`. See [ZX1 MIO-0](#zx1-mio-0) for background.

---
## ZX1 MIO-C  
`zx1-mio-c` - Not present on most PCs

**In plain words.** Register page C of the HP zx1 Memory and I/O controller (MIO), found only in HP Itanium 2 systems.

**What you see.** `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`. See [ZX1 MIO-0](#zx1-mio-0) for background.

---
## ZX1 MIO-D  
`zx1-mio-d` - Not present on most PCs

**In plain words.** Register page D of the HP zx1 Memory and I/O controller (MIO), found only in HP Itanium 2 systems.

**What you see.** `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`. See [ZX1 MIO-0](#zx1-mio-0) for background.

---
## ZX1 MIO-E  
`zx1-mio-e` - Not present on most PCs

**In plain words.** Register page E of the HP zx1 Memory and I/O controller (MIO), found only in HP Itanium 2 systems.

**What you see.** `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`. See [ZX1 MIO-0](#zx1-mio-0) for background.

---
## ZX1 MIO-F  
`zx1-mio-f` - Not present on most PCs

**In plain words.** Register page F of the HP zx1 Memory and I/O controller (MIO), found only in HP Itanium 2 systems.

**What you see.** `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`. See [ZX1 MIO-0](#zx1-mio-0) for background.

---
## ZX1 summary  
`zx1-mio-s` - Not present on most PCs

**In plain words.** The overview page for the HP zx1 chipset: a summary of the MIO controller pages 0 to F in one view.

**What you see.** `Status  Not present on this system` and `Checked  n/a (HP zx1 Itanium chipset)`. See [ZX1 MIO-0](#zx1-mio-0) for what the zx1 is.

**What "not present" means.** Expected on every x86-64 PC.
