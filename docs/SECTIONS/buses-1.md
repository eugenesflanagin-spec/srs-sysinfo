# Buses / Devices (part 1 of 2)  (SRS Section Reference)

[Back to the section index](README.md) - [Glossary](../GLOSSARY.md) - [Part 2](buses-2.md)

**Where to find these items (SRS 2.0).** In the main window they are on the **USB & PCI devices** page (Alt+0). Its tabs are ALL Devices, PCI bus, USB bus, USB tree, HID bus and Thunderbolt; **All N items** lists the rest. You can also type an item's name or id into the search box (Ctrl+F). The rest of this introduction describes the classic view (Menu -> Classic View), with its Menu and panel buttons.

This group shows how the parts of your PC are wired together: the PCI Express (PCIe) bus that carries
the graphics card and SSDs, the USB and HID (Human Interface Device) devices, the small management buses
(SMBus / I2C), the old ISA / LPC (Low Pin Count) bus, serial ports, ACPI (Advanced Configuration and
Power Interface) devices, and the I/O port and memory address maps. Open any item with
**Menu -> Buses / Devices -> item**. The classic main window also has a **PCI Bus** button (its panel has the
sub-buttons Properties, PnP Dev, Drives, PCI Dev, UAA Dev and ALL Dev) and a **USB Bus** button (sub-buttons
Link Devices, USB Disks, HID Bus and USB Info). Most items work as a normal user. The items marked
"Needs root data" show address ranges or raw register copies that Linux only gives to root. Run
**Menu > Run as root (srs-root)** once to fill that cache. Until you do, those items show zeros and an
orange "Needs root" line. In many panels you can **double-click a device ID** (a `VVVV:DDDD` pair such as
`1022:14D8`) to open it in **Device ID Lookup**.

SRS never opens the risky device files these items are about (`/dev/port`, `/dev/mem`, `/dev/i2c-*`,
`/dev/hidraw*`). It also never scans a bus. Everything comes from sysfs, procfs, udev and the srs-root cache.

## Contents
- [COM port resource arbiter](#com-port-resource-arbiter) `arbiter`
- [Chipsets](#chipsets) `chipsets`
- [System device classes](#system-device-classes) `class`
- [Remote system classes](#remote-system-classes) `class-remote`
- [Co-device installers](#co-device-installers) `codevice`
- [Compatible IDs](#compatible-ids) `compatible`
- [Device IDs](#device-ids) `device-ids`
- [All devices](#all-devices) `devices`
- [Device owners](#device-owners) `dev-owner`
- [Device property keys](#device-property-keys) `devpropkey`
- [Embedded controller I/O](#embedded-controller-io) `embedded`
- [System enum (device tree)](#system-enum-device-tree) `enum`
- [Remote system enum](#remote-system-enum) `enum-remote`
- [FireWire](#firewire) `firewire`
- [Ghost (non-present) devices](#ghost-non-present-devices) `ghost-devices`
- [HID all devices](#hid-all-devices) `hid-all`
- [HID bus](#hid-bus) `hid-bus`
- [HID tree](#hid-tree) `hid-tree`
- [Device interfaces](#device-interfaces) `interfaces`
- [Device interfaces (extended)](#device-interfaces-extended) `interfacex`
- [Interface IDs](#interface-ids) `int-ids`
- [Interface names](#interface-names) `int-names`
- [Interface owner](#interface-owner) `int-owner`
- [ASUS I/O](#asus-io) `io-asus`
- [I/O port dump](#io-port-dump) `io-dump`
- [I/O GPIO](#io-gpio) `io-gpio`
- [I/O PM2 MMIO](#io-pm2-mmio) `io-mmio`
- [nForce I/O](#nforce-io) `io-nforce`
- [I/O ports](#io-ports) `io-ports`
- [IrDA ports](#irda-ports) `irports`
- [ISA bus](#isa-bus) `isa-bus`
- [MCA bus](#mca-bus) `mca-bus`
- [Chipset MCH](#chipset-mch) `mch`
- [Chipset MRH](#chipset-mrh) `mrh`
- [NPU names](#npu-names) `npu-names`
- [PCI all](#pci-all) `pci-all`
- [PCI BARs](#pci-bars) `pci-bars`
- [PCI bus](#pci-bus) `pci-bus`
- [PCI devices](#pci-devices) `pci-dev`
- [PCI flags](#pci-flags) `pci-flag`
- [PCI HAL](#pci-hal) `pci-hal`
- [PCI ID database](#pci-id-database) `pci-info`
- [PCI MMIO](#pci-mmio) `pci-mmio`
- [PCMCIA all](#pcmcia-all) `pcm-all`
- [PCMCIA bus](#pcmcia-bus) `pcm-bus`
- [PCMCIA ID database](#pcmcia-id-database) `pcm-info`
- [I/O PM1 data](#io-pm1-data) `pm1-area`
- [PMBus](#pmbus) `pmb-bus`
- [PnP manager](#pnp-manager) `pnp`
- [PnP devices](#pnp-devices) `pnp-dev`
- [PnP ID database](#pnp-id-database) `pnp-info`
- [PnP manager details](#pnp-manager-details) `pnp-man`
- [COM ports](#com-ports) `ports`
- [Device properties](#device-properties) `properties`
- [Device property](#device-property) `property`
- [PYXIS 21174](#pyxis-21174) `pyxis`
- [Smart cards](#smart-cards) `scard`
- [Serial COM ports](#serial-com-ports) `serial`
- [SI USB](#si-usb) `si-usb`
- [SMBus](#smbus) `smb`
- [SMBus devices](#smbus-devices) `smb-bus`
- [SMBus status](#smbus-status) `smb-sts`
- [SMBus setup](#smbus-setup) `smbus-setup`
- [System SMBus access](#system-smbus-access) `smbus-system`

---

## COM port resource arbiter

`arbiter` - Windows-only (Linux equivalent shown)

**In plain words.** Windows has an "arbiter" that hands out I/O addresses and IRQs (interrupt lines)
to COM (serial) ports. Linux has no such component. SRS shows the serial port list instead, plus
the I/O ranges that the serial driver has claimed.

**What you see.**
- The full [Serial COM ports](#serial-com-ports) table (Port, I/O, IRQ, UART, Base clock, Device),
  the placeholder count line and the TTY driver list.
- **COM resources (Linux equivalent of the COM port arbiter)** - every line of the I/O port map
  that mentions `serial`, e.g. `03f8-03ff  serial`. This is the address range the port really uses.

**Healthy / typical values.** On the author's PC: one real port, `ttyS0` at `0x3F8`, IRQ 4, and the
claimed range `03f8-03ff serial`. COM1 has used these values since the IBM PC.

**Unusual values and what they suggest.** No `serial` range while a port is listed means the
driver has not claimed it yet. Two devices on the same range would be a firmware conflict, but
that is very rare today.

**Where Linux gets it.** `/sys/class/tty`, `/proc/tty/drivers`, `/proc/ioports` (addresses need the
srs-root cache).

---

## Chipsets

`chipsets` - Linux data

**In plain words.** This is a short list of the "glue" chips on the motherboard: host bridges, the
IOMMU, the chipset's PCIe switch and its ports, the SMBus controller and the LPC bridge.

**What you see.** One line per device. The label on the left is the role SRS gives it:

| Label | Meaning |
|---|---|
| Host Bridge | PCI class 0600. The link between the CPU and the PCI bus. On AMD this includes the Root Complex, the "Dummy Host Bridges" and the eight Data Fabric functions (00:18.0-7). |
| IOMMU | Class 0806. The input-output memory management unit. It controls which memory each device may reach by DMA (Direct Memory Access). |
| ISA/LPC Bridge | Class 0601. It connects the legacy LPC bus (Super I/O chip, TPM, BIOS flash). |
| SMBus Controller | Class 0C05. The controller for the small management bus that reads DIMM SPD chips and sensors. |
| Chipset Bridge | A PCI bridge whose name contains "Chipset", such as one port of the chipset's PCIe switch. |
| Chipset Function | Any other device named "Chipset", such as the chipset's USB or SATA controller. |

After the label comes the PCI address, the vendor and device names, `[VVVV:DDDD rev RR]` and the
driver if one is bound. The last line is the board name and BIOS version.

**Healthy / typical values.** On the author's ASUS TUF GAMING X870-PRO WIFI7 W NEO (Ryzen 9000
"Granite Ridge" CPU): the Root Complex `1022:14D8` with driver `ryzen_smu`, the IOMMU `1022:14D9`, and the
"600 Series Chipset PCIe Switch Upstream Port" `1022:43F4` with its downstream ports `1022:43F5` (X870 boards
use the same Promontory 21 chip). The chipset USB controller `1022:43FC` and SATA controller `1022:43F6`
also appear, plus `00:18.3` Data Fabric Function 3 with driver `k10temp` (the CPU temperature driver).

**Unusual values and what they suggest.** If the SMBus controller has no driver, `piix4_smbus` is not
loaded, and RAM and RGB tools will not find their devices. If there are no "Chipset" lines on a board
that has a chipset, the PCI ID database is old (see [PCI ID database](#pci-id-database)).

**Where Linux gets it.** `/sys/bus/pci/devices/*/class`, `vendor`, `device`, `revision`, `driver`, with
names from `/usr/share/hwdata/pci.ids`.

**Background.** On a modern AMD desktop the memory controller and most PCIe lanes are in the CPU. The
"chipset" is a separate chip joined to the CPU by a PCIe x4 link. It acts like a PCIe switch with extra
USB and SATA controllers built in.

---

## System device classes

`class` - Linux data

**In plain words.** Linux sorts every device into a "class" by what it does (block disks, network,
sound, input, hwmon sensors...). This item lists each class and the devices in it.

**What you see.** One row per directory in `/sys/class`:
- **Class name** - such as `block`, `drm` (graphics), `hwmon`, `net`, `tty`.
- **Count** - how many entries the class has.
- **Members** - their names, cut off with `...` after about 150 characters.

**Healthy / typical values.** On the author's PC: `block 27` (loop devices, three NVMe drives and their
partitions, SATA disks), `cpuid 16` (one per logical CPU), `drm 8` (card0, its four connectors, a
writeback connector, renderD128, version), `bluetooth 1 hci0`. Many classes are `0`, which is normal.
They exist because the kernel supports them, not because hardware is present.

**Unusual values and what they suggest.** A count of 0 where you expect hardware, such as `net` without
your network card or `drm` without `card0`, means the driver did not load. Check
[Device owners](#device-owners).

**Where Linux gets it.** `/sys/class/*`.

**Background.** A sysfs "class" is a view by function. The same device also appears under its bus
(`/sys/bus/pci/...`) and at its place in the device tree (`/sys/devices/...`).

---

## Remote system classes

`class-remote` - Windows-only (Linux equivalent shown)

**In plain words.** SIV on Windows can read the device classes of another PC over the network,
through the remote registry. Linux has nothing like that.

**What you see.** `Status: Windows-only item in SIV` and `Linux equivalent: none (remote registry);
nearest ssh + /sys/class`. To see another Linux PC's classes, log in with `ssh` and look at
`/sys/class`, or run SRS there.

---

## Co-device installers

`codevice` - Windows-only (Linux equivalent shown)

**In plain words.** Windows "co-installers" are small helper programs that run when a driver is
installed. The closest thing on Linux is udev rules. These are text files that say what to do when
a device appears: set permissions, make symlinks, load firmware, start a service.

**What you see.** Three folders, each with its file count and then every rule file in it:
- `/usr/lib/udev/rules.d` - rules shipped by packages.
- `/etc/udev/rules.d` - your own rules. A file with the same name here overrides the package one.
- `/run/udev/rules.d` - temporary rules made at run time.

**Healthy / typical values.** On the author's PC: 290 package rules (e.g. `30-amdgpu-pm.rules`,
`10-rtl-sdr.rules`, `01-asrock-LED-controller.rules`) and 1 run-time rule (`30-drm-awaiter.rules`).
The number at the front sets the order. Lower numbers run first.

**Unusual values and what they suggest.** A rule left behind by a removed program can still change
device permissions. If a USB gadget works only as root, look here for a rule that should give access
to your user and is missing.

**Where Linux gets it.** A file listing of the three `rules.d` folders.

---

## Compatible IDs

`compatible` - Linux data

**In plain words.** Every device has a "modalias". This is one long ID string that Linux matches against
each driver's list of supported hardware to pick the right driver. It is the Linux version of Windows
"Compatible IDs".

**What you see.** Three groups, one line each:
- **PCI devices** (teal label = PCI address) - e.g.
  `pci:v00001022d000014D8sv00001043sd00008877bc06sc00i00`.
- **USB interfaces** (label = interface name such as `1-2:1.0`) - e.g. `usb:v2109p2817d0623dc09dsc00dp02ic09isc00ip02in00` (a VIA USB hub).
- **ACPI devices** (label = ACPI name such as `ACPI0007:00`) - e.g. `acpi:ACPI0007:`.

How to read a PCI modalias:

| Part | Meaning |
|---|---|
| `v00001022` | Vendor ID 1022 (AMD) |
| `d000014D8` | Device ID 14D8 |
| `sv00001043` | Subsystem vendor 1043 (ASUS, the board maker) |
| `sd00008877` | Subsystem device 8877 (the board's own number for this part) |
| `bc06 sc00 i00` | Base class 06 (bridge), subclass 00 (host), programming interface 00 |

For USB: `v`=vendor, `p`=product, `d`=device release, `dc/dsc/dp`=device class/subclass/protocol,
`ic/isc/ip`=interface class/subclass/protocol, `in`=interface number.

**Healthy / typical values.** On the author's PC the Radeon RX 9070 XT reads `pci:v00001002d00007550sv00001EAEsd00008811bc03sc00i00`:
AMD/ATI chip 7550, built by subsystem vendor 1EAE (XFX), class 03 (display).

**Unusual values and what they suggest.** A subsystem of `sv00000000sd00000000` is normal for internal
bridges. On a plug-in card it means the maker did not program one. You can add a filter after the item
name (e.g. `compatible:03:00`) to show only matching devices.

**Where Linux gets it.** `/sys/bus/pci/devices/*/modalias`, `/sys/bus/usb/devices/*:*/modalias`,
`/sys/bus/acpi/devices/*/modalias`.

---

## Device IDs

`device-ids` - Linux data

**In plain words.** This shows every PCI and USB device's ID, and how SRS turned that ID into a name.
It shows which entry in the ID database matched.

**What you see.**

| Column | Meaning |
|---|---|
| Device | The PCI address (`03:00.0`) or the USB device name (`5-1`, `usb3`). |
| Lookup Key | For PCI: `Vendor:Device:SubVendor:SubDevice:Revision`. Internal parts with no subsystem use `Vendor:Device:Revision`. For USB: `Vendor:Product`. |
| Matched | Which step of the lookup chain found a name. For PCI: `rung 0` is the most exact. For USB: `V:D` (vendor and product found), `V` (only the vendor is known) or `none`. |
| Name | The vendor name, then the device name from the step that matched. |

The PCI lookup chain for a full key goes from most to least exact:
rung 0 `V:D:S:s:RR` -> rung 1 `V:D:S:s` -> rung 2 `V:D:RR` -> rung 3 `V:D` -> rung 4 `V`.
For a short key (no subsystem) it is rung 0 `V:D:RR` -> rung 1 `V:D` -> rung 2 `V`.

Double-click any key to open it in **Device ID Lookup**, which shows every step of the chain side by side.

**Healthy / typical values.** On the author's PC most PCI rows match at `rung 3` (the generic
vendor:device name). The Samsung SSD matches at `rung 1`: key `144D:A80A:144D:A801`, where the subsystem
entry gives the retail name "SSD 980 PRO" instead of the chip name. Internal bridges like `1022:14DA:00`
match at `rung 1` (their short-key V:D). On the USB side the Razer mouse `1532:005C` is `V:D`
"DeathAdder Elite". Some devices are only `V` (the SteelSeries keyboard `1038:1612`) or `none` (`9129:1583`).

**Unusual values and what they suggest.** `none` on PCI means the vendor is missing from the database.
This happens with brand-new hardware or a very old `pci.ids`. Update the `hwdata` package. `V` or `none` on
USB is common for cheap gadgets and is harmless. The device's own name strings still show in
[Device owners](#device-owners).

**Where Linux gets it.** sysfs `vendor`, `device`, `subsystem_vendor`, `subsystem_device`, `revision` and
`idVendor`/`idProduct`, looked up in `/usr/share/hwdata/pci.ids` and `usb.ids`.

**Background.** A PCI device carries two pairs of IDs. The **vendor/device ID** names the chip (who made
the silicon). The **subsystem vendor/device ID** names the product built around it (who made the card or
board). The same Navi 48 chip `1002:7550` shows up with a different subsystem on every card brand. The
**revision ID** tells chip steppings apart.

---

## All devices

`devices` - Linux data

**In plain words.** This is every device udev knows about: real hardware, virtual devices and
kernel helpers. It is the Linux equivalent of Device Manager with "show hidden devices" on.

**What you see.**

| Column | Meaning |
|---|---|
| Device | The device path under `/sys/devices` (the leading `/devices/` is dropped), such as `pci0000:00/0000:00:01.2/0000:04:00.0/nvme/nvme0/nvme0n1`. |
| Subsystem | The bus or class it belongs to: `pci`, `usb`, `block`, `net`, `acpi`, `workqueue`... |
| Driver | The bound driver, if any. |
| Node | Its `/dev` node, if any (e.g. `nvme0n1`, `bus/usb/002/002`). |

At the end: `Total of N devices in the udev database`.

**Healthy / typical values.** On the author's PC: 1583 devices. The list starts with the ACPI tree
(`LNXSYSTM:00/...`) and ends with `virtual/workqueue/...` entries. Hundreds of rows with no driver are
normal. Most entries are containers, ports or virtual objects.

**Unusual values and what they suggest.** A real device (PCI or USB) with an empty Driver column has
no driver. A missing device that you can see in `lspci` means udev has not processed it. This is rare, and
restarting `systemd-udevd` fixes it.

**Where Linux gets it.** `udevadm info --export-db` (read only).

---

## Device owners

`dev-owner` - Linux data

**In plain words.** For each PCI and USB device, this shows which driver "owns" it and which kernel module
that driver lives in.

**What you see.**

| Column | Meaning |
|---|---|
| Device | The PCI address or USB device name. |
| Driver | For PCI, the bound driver or `(none)`. For USB, every interface driver, comma-separated (e.g. `usbhid, usbfs`). |
| Module | For PCI, the module file the driver comes from, or `built-in` if it is compiled into the kernel. It is blank when there is no driver, and blank for USB. |
| Description | For PCI, the device name. For USB, the maker and product strings the device reports about itself. Root hubs show `xHCI Host Controller - <controller name>`. |

**Healthy / typical values.** On the author's PC: GPU `03:00.0` -> `amdgpu`, NVMe drives -> `nvme`,
the 5GbE card -> `r8169`, PCIe bridges -> `pcieport` (module `pcieportdrv`), USB hubs -> `hub`, the
webcam -> `uvcvideo`, the Bluetooth radio -> `btusb`. The IOMMU and "Dummy Host Bridges" show `(none)`.
That is normal, because they need no driver.

**Unusual values and what they suggest.**
- A plug-in card or USB gadget with `(none)` has no driver: it is unsupported, or its module is not loaded.
- `usbfs` means a user-space program (OpenRGB, a game launcher, a printer driver) has claimed the
  interface directly. This is expected for RGB keyboards and printers.
- Garbled description text (stray symbols) comes from the device's own firmware strings, not from SRS.

**Where Linux gets it.** `/sys/bus/pci/devices/*/driver`, `.../driver/module`,
`/sys/bus/usb/devices/*:*/driver`, plus the USB `manufacturer` and `product` files.

---

## Device property keys

`devpropkey` - Linux data

**In plain words.** udev stores key=value "properties" for each device. This item lists every property
name in use and how many devices have it. Use it to find a key you can then look up with
[Device property](#device-property).

**What you see.** One line per key, A-Z, with `N devices`. The last line is
`Total of K keys across D devices`.

**Healthy / typical values.** On the author's PC: 298 keys across 1583 devices. Common ones:
`DEVPATH` (every device), `DEVNAME` (370 devices with a /dev node), `DRIVER` (258), `HID_ID` and
`HID_NAME` (18), `BUSNUM`/`DEVNUM` (40 USB devices).

**Where Linux gets it.** The `E:` lines of `udevadm info --export-db`.

**Background.** Keys such as `ID_VENDOR_FROM_DATABASE` come from udev's hardware database (hwdb).
Keys such as `ID_SERIAL` and `ID_PATH` come from udev helper programs. Desktop software uses them to
decide how to treat a device.

---

## Embedded controller I/O

`embedded` - Linux data - Needs root data

**In plain words.** The EC (Embedded Controller) is a small microcontroller on the motherboard. It
handles things like power buttons, battery and fans on laptops, and some sensors on desktops. This
item shows where it lives. It does not read it.

**What you see.**
- **ACPI device** - `PNP0C09:00 (ACPI Embedded Controller)`, or "No ACPI PNP0C09 device".
- **Namespace path** - its place in the firmware's ACPI tables, e.g. `\_SB_.PCI0.SBRG.EC0_`.
- **Bound to** - the Linux device it is attached to, or `(ACPI core EC driver)`.
- **ECDT table** - `present` if the firmware gives an early EC description table, otherwise
  `absent (EC described in DSDT only)`.
- **ec_sys debugfs** - whether the `ec_sys` debug module (which lets root read EC RAM) is loaded.
- The EC's I/O ports from the port map: on PCs, `0062` = EC data and `0066` = EC command.

**Healthy / typical values.** On the author's desktop the EC is at `\_SB_.PCI0.SBRG.EC0_`, there is
no ECDT, `ec_sys` is not loaded, and the ports are `0062` and `0066`. That is a standard layout.

**Unusual values and what they suggest.** No EC at all is fine on many desktops. On a laptop, a
missing EC explains missing battery or fan readings. SRS never reads EC RAM, because traffic on
0x62/0x66 can collide with the firmware.

**Where Linux gets it.** `/sys/bus/acpi/devices/PNP0C09:00`, `/sys/firmware/acpi/tables/ECDT`,
`/proc/ioports` (addresses from the srs-root cache).

---

## System enum (device tree)

`enum` - Linux data

**In plain words.** These are the same devices as [All devices](#all-devices), drawn as an indented tree,
so you can see what is plugged into what.

**What you see.** One line per device: its short name, indented by depth, then `[subsystem]`.
Top-level entries are in bold. For example:

```
  LNXSYSTM:00  [acpi]
    LNXPWRBN:00  [acpi]
    LNXSYBUS:00  [acpi]
      ACPI0010:00  [acpi]
        ACPI0007:00  [acpi]
```

**Healthy / typical values.** On the author's PC the tree starts with the ACPI root and its 16
processor objects (`ACPI0007:00` to `0f`, one per logical CPU). Then come the PCI tree
(`pci0000:00`), platform devices and the virtual devices. There are about 1580 lines.

**Unusual values and what they suggest.** If a device sits under an unexpected parent (an NVMe drive
under the chipset instead of the CPU, say), it is in an M.2 slot wired to the chipset. That slot is
slower and shares bandwidth.

**Where Linux gets it.** `udevadm info --export-db`, sorted by device path.

---

## Remote system enum

`enum-remote` - Windows-only (Linux equivalent shown)

**In plain words.** SIV on Windows can list another PC's device tree through the remote registry.
Linux has no such feature.

**What you see.** `Status: Windows-only item in SIV` and `Linux equivalent: none (remote registry);
nearest ssh + udevadm`. On the other PC, run `udevadm info --export-db` over `ssh`, or run SRS there.

---

## FireWire

`firewire` - Not present on most PCs

**In plain words.** FireWire (IEEE 1394) is an older high-speed bus, used for DV camcorders and pro
audio interfaces. Modern boards do not have it.

**What it would show.** One `1394 controller` line per FireWire card (PCI class 0C00), and whether
`/sys/bus/firewire` exists.

**What "not present" means.** On the author's PC: `/sys/bus/firewire absent` and "No PCI class 0C00
(FireWire) controller present." Nothing is wrong. You would need an add-in card.

---

## Ghost (non-present) devices

`ghost-devices` - Windows-only (Linux equivalent shown)

**In plain words.** Windows keeps every device ever plugged in, even after it is gone. These are
"ghost devices". Linux forgets a device when it is removed. SRS checks the closest thing: udev
records in `/run/udev/data` whose device no longer exists.

**What you see.** One orange line per stale record: `stale udev record (device not present)`. Or, if
there are none: "No stale entries in /run/udev/data: Linux drops records when devices go away." A
second note explains the Windows difference.

**Healthy / typical values.** No stale entries, as on the author's PC.

**Unusual values and what they suggest.** A few stale records after hot-unplugging can happen and are
harmless. They vanish at reboot, because `/run` is in memory.

**Where Linux gets it.** `/run/udev/data` compared with `/sys/dev`, `/sys/bus` and `/sys/class`.

---

## HID all devices

`hid-all` - Linux data

**In plain words.** HID (Human Interface Device) is the standard way keyboards, mice, game pads, RGB
controllers and security keys talk to the PC. This item gives a full card for each HID device.

**What you see.** For each device, a title line `<bus>:<VID>:<PID>.<instance>  <name>`, then:

| Field | Meaning |
|---|---|
| Bus | The bus number and name (`0003 USB`, `0005 Bluetooth`, `0018 I2C`, `0006 Virtual`...), then `VID_xxxx PID_xxxx`. |
| usb.ids | For USB devices only: the vendor and product names from the USB ID database. |
| Driver | The HID driver: `hid-generic`, or a special one like `logitech-djreceiver`, `razermouse`, `nintendo`. |
| Physical | Where it is attached, e.g. `usb-0000:0b:00.0-8.4.4/input1:1` (controller, port path, interface). |
| Unique ID | The device's own unique ID or serial, if it reports one (shown only when present). |
| Parent | The USB interface or other device it hangs off. |
| Report descriptor | The size in bytes of the descriptor that tells the PC what buttons, axes and LEDs the device has. |
| hidraw node | The raw access node (`hidrawN`). It is listed but never opened. |
| Input devices | The `inputN` devices the kernel made from it (the keyboard and mouse events your desktop reads). |
| Modalias | The driver-matching ID, e.g. `hid:b0003g0102v0000046Dp00004057`. |

**Healthy / typical values.** On the author's PC: a Logitech mouse `046D:4057` behind a Nano Receiver
`046D:C52F` (driver `logitech-hidpp-device`, parent `0003:046D:C52F.000F`, report descriptor 192 bytes,
Unique ID `XXXX`). Also a Nintendo Switch Pro Controller with driver `nintendo`, and a Razer DeathAdder with
driver `razermouse`.

**Unusual values and what they suggest.**
- A game pad on `hid-generic` instead of its own driver may lose rumble, LEDs or the gyro.
- No `Input devices` on a keyboard or mouse interface is normal for vendor-only interfaces (RGB,
  macro setup). It is a problem only if typing does not work.
- A tiny report descriptor (a few bytes) can mean a broken or half-initialised device.

**Where Linux gets it.** `/sys/bus/hid/devices/*` (`uevent`, `report_descriptor`, `hidraw/`, `input/`).

**Background.** One physical USB device often exposes several HID interfaces. A gaming keyboard can have
one for keys, one for media keys and one for lighting, so it shows up several times here.

---

## HID bus

`hid-bus` - Linux data

**In plain words.** This is a one-line-per-device table of the HID bus. It is the **HID Bus** button in the
USB Bus panel.

**What you see.**

| Column | Meaning |
|---|---|
| HID Device | `bus:VID:PID.instance`, e.g. `0003:1532:005C.0001`. |
| Bus | USB, Bluetooth, I2C, SPI, Virtual, Host or Other. |
| Name | The name the device reports. |
| Driver | The HID driver bound to it. |
| Nodes | The `hidrawN` node and any `inputN` devices. |

The last line is `Total of N HID devices`. You can filter, e.g. `hid-bus:1532`.

**Healthy / typical values.** On the author's PC: 15 HID devices. The SteelSeries Apex 7 keyboard appears
four times (four interfaces), the ASUS AURA LED controller twice, and the Razer DeathAdder three times. There
is also a "Virtual FIDO/U2F security Key" `0003:FFFF:FFFF` made by software.

**Unusual values and what they suggest.** A device you can see on the USB bus but not here is not a
HID device, or its HID driver failed. Check the kernel log.

**Where Linux gets it.** `/sys/bus/hid/devices`.

---

## HID tree

`hid-tree` - Linux data

**In plain words.** This groups HID devices under the USB device they belong to, so you can see which
interfaces of one gadget are HID.

**What you see.** For each USB device that has HID interfaces, a header line
`<usb name>  VID:PID <description>`, then one line per HID interface:
`|__ <usb interface>  <HID device>  [driver] <input devices>`. HID devices that are not on USB
(Bluetooth, I2C touchpads, virtual) are listed at the end with their bus name.

**Healthy / typical values.** On the author's PC:
```
5-2  1038:1612 SteelSeries SteelSeries Apex 7
  |__ 5-2:1.0  0003:1038:1612.0006  [hid-generic] input6
  |__ 5-2:1.2  0003:1038:1612.0008  [hid-generic] input7
```
Double-click a `VID:PID` such as `1038:1612` to open it in Device ID Lookup. If the lookup window opens
on the PCI bus, switch it to USB.

**Where Linux gets it.** `/sys/bus/hid/devices` matched to `/sys/bus/usb/devices`.

---

## Device interfaces

`interfaces` - Linux data

**In plain words.** On Windows, "device interfaces" are the named access points programs open. On Linux
the matching idea is the `/sys/class` entries, so this item shows the same table as
[System device classes](#system-device-classes) (its window title says so too).

**What you see.** Class name, count, members (see [System device classes](#system-device-classes)).
For more detail use the extended form [Device interfaces (extended)](#device-interfaces-extended).

**Where Linux gets it.** `/sys/class`.

---

## Device interfaces (extended)

`interfacex` - Linux data

**In plain words.** This lists every single `/sys/class` entry with its device number, the hardware
behind it and its driver.

**What you see.**

| Column | Meaning |
|---|---|
| class/name | e.g. `drm/card0`, `hidraw/hidraw0`, `block/nvme0n1`. |
| dev_t | The major:minor device number, if it has a `/dev` node (e.g. `226:0`). |
| Device | The hardware it belongs to (e.g. PCI `0000:03:00.0`, HID `0003:1532:005C.0001`, `nvme0`). |
| Driver | That hardware's driver. |

**Healthy / typical values.** On the author's PC: `drm/card0  226:0  0000:03:00.0  amdgpu` and
`hidraw/hidraw0  243:0  0003:1532:005C.0001  razermouse`. Many entries (bdi, virtual ones) have empty
columns. That is normal.

**Where Linux gets it.** `/sys/class/*/*/dev`, `.../device`, `.../device/driver`.

**Background.** A **dev_t** is the pair of numbers the kernel uses to find a device file. The *major*
picks the driver (259 = NVMe/block extended, 226 = DRM graphics). The *minor* picks one device.

---

## Interface IDs

`int-ids` - Linux data

**In plain words.** These are only the `/sys/class` entries that have a device file, with their device
number and `/dev` path.

**What you see.** class/name, dev_t (`major:minor`) and the `/dev/...` node name from udev.

**Healthy / typical values.** On the author's PC: `block/nvme0n1  259:5  /dev/nvme0n1`,
`block/sda  8:0  /dev/sda`. Block disks use major 8 for SATA/SCSI and 259 for NVMe.

**Unusual values and what they suggest.** An entry with a dev_t but no `/dev` path means udev did not
make the node. Programs will not find the device.

**Where Linux gets it.** `/sys/class/*/*/dev`, plus the `N:` (node) line in the udev database.

---

## Interface names

`int-names` - Linux data

**In plain words.** This shows the friendly symlink udev made for each `/sys/class` entry, such as
`/dev/disk/by-label/EFI`.

**What you see.** class/name and the first udev symlink for it (relative to `/dev`), or `-` if there
is none.

**Healthy / typical values.** On the author's PC: `block/loop0  disk/by-diskseq/142`,
`block/nvme0n1p1  disk/by-path/pci-0000:04:00.0-nvme-1-part/by-label/EFI`. Most non-disk entries show `-`.

**Unusual values and what they suggest.** A disk with no `by-id` or `by-uuid` names can make `/etc/fstab`
entries that use those names fail at boot.

**Where Linux gets it.** The `S:` (symlink) lines of the udev database.

---

## Interface owner

`int-owner` - Linux data

**In plain words.** This shows which driver and kernel module own each `/sys/class` entry.

**What you see.** class/name, Driver (or `-`), Module (or `built-in`), Device (the parent hardware).

**Healthy / typical values.** On the author's PC: `drm/card0  amdgpu  amdgpu  0000:03:00.0`,
`input/input25  razermouse  razermouse  0003:1532:005C.0001`.

**Where Linux gets it.** `/sys/class/*/*/device/driver` and `.../driver/module`.

---

## ASUS I/O

`io-asus` - Linux data - Needs root data

**In plain words.** ASUS boards keep their fan and voltage sensors in a Super I/O chip and, on some
models, the EC. This item shows which ASUS-related drivers are present. The sensor values themselves
are in the Sensors group.

**What you see.**
- **Board** - the board vendor and name from SMBIOS (System Management BIOS).
- **hwmonN** - each Nuvoton (`nct...`) or ASUS hwmon chip, with a pointer to `[sensors]`.
- **asus-wmi** and **asus_ec_sensors** - `loaded` or `not loaded`.
- I/O port ranges claimed by `nct`, `asus` or `it87` drivers, or "no matching entries".

**Healthy / typical values.** On the author's ASUS TUF GAMING X870-PRO WIFI7 W NEO: `hwmon4 nct6799`, with
`asus-wmi` and `asus_ec_sensors` not loaded. "No matching entries" in the port map is normal, because the
`nct6775` driver's port shows up under the Super I/O name, not under "nct".

**Unusual values and what they suggest.** No `nct` chip on an ASUS board usually means the `nct6775`
module is not loaded, or the board needs `acpi_enforce_resources=lax`. `asus_ec_sensors` adds extra
readings (chipset temperature, water flow) on supported ROG/TUF models.

**Where Linux gets it.** `/sys/class/dmi/id/board_*`, `/sys/class/hwmon/*/name`, `/sys/module/*`,
`/proc/ioports` (srs-root cache).

---

## I/O port dump

`io-dump` - Linux data - Needs root data

**In plain words.** SIV can dump the raw contents of the PC's I/O ports. SRS deliberately does not,
because reading some ports can confuse the SMBus or EC. It shows which driver owns which port range
instead.

**What you see.** A note saying why `/dev/port` is not read, then the full I/O port map (the same as
[I/O ports](#io-ports)).

**Where Linux gets it.** `/proc/ioports` (srs-root cache for real addresses).

---

## I/O GPIO

`io-gpio` - Linux data - Needs root data

**In plain words.** GPIO (General Purpose Input/Output) pins are simple on/off signals the chipset
uses for LEDs, buttons, and enabling or resetting other chips. This item lists the GPIO controllers.

**What you see.** One line per GPIO chip: `gpiochipN  <ACPI device>  <name>  driver <driver>`, then a
note that pin states are not read, and how root can see them (`cat /sys/kernel/debug/gpio`).

**Healthy / typical values.** On the author's PC: `gpiochip0  AMDI0030:00  AMD GPIO Controller  driver amd_gpio`
(the FCH (Fusion Controller Hub) pins) and `gpiochip1  AMDIF031:00  AMD GPIO Controller  driver pt-gpio`
(the chipset's pins).

**Unusual values and what they suggest.** "no GPIO chips" on an AMD board means `pinctrl_amd` is not
loaded. Some laptops need it for the touchpad interrupt.

**Where Linux gets it.** `/sys/bus/gpio/devices`.

---

## I/O PM2 MMIO

`io-mmio` - Linux data - Needs root data

**In plain words.** AMD chipsets put their power-management, SMBus, GPIO and pin-mux registers in one
memory-mapped block at `0xFED80000`. This item shows the memory-map lines around it. MMIO
(Memory-Mapped I/O) means hardware registers that look like memory addresses.

**What you see.** A note (registers are not read; no `/dev/mem`), then every line of the physical
memory map that mentions AMDI devices, IOAPIC, HPET, Local APIC, `amd_iommu`, `pnp` or `Reserved`.

**Healthy / typical values.** On the author's PC: `fec00000-fec003ff IOAPIC 0`, `fed00000-fed003ff HPET 0`,
`fed81500-fed818ff AMDI0030:00` (the GPIO block inside the FCH region), `f7600000-f767ffff amd_iommu`, and
several `Reserved` ranges for firmware.

**Where Linux gets it.** `/proc/iomem` (srs-root cache; non-root readers see zeros).

---

## nForce I/O

`io-nforce` - Not present on most PCs

**In plain words.** These would be the I/O registers of NVIDIA nForce chipsets (about 2001-2008). No
current PC uses one.

**What you see.** `Status: Not present on this system`, `Checked: n/a (no NVIDIA nForce chipset)`.

---

## I/O ports

`io-ports` - Linux data - Needs root data

**In plain words.** x86 PCs have a separate 64K "I/O port" address space, left over from the
original PC. This is the map of who owns which ports.

**What you see.** One line per range: `start-end  owner`, indented to show nesting (a range inside a
bigger one). Owners are drivers or firmware devices.

**Healthy / typical values.** From the author's PC, all classic PC values:

| Range | Owner |
|---|---|
| `0020-0021`, `00a0-00a1` | pic1, pic2 (the two old interrupt controllers) |
| `0040-0043` | timer0 (the PIT, the original PC timer) |
| `0060`, `0064` | keyboard (the PS/2 controller) |
| `0062`, `0066` | EC data, EC cmd |
| `0070-0071` | rtc0 (the CMOS real-time clock) |
| `0cf8-0cff` | PCI conf1 (the old way to reach PCI configuration) |
| `0290-029f` | reserved by the firmware (`PNP0C02`) for the Super I/O hardware monitor (the `nct6775.656` device: 656 = 0x290) |
| `0800-0827` | ACPI PM1, PM timer and GPE blocks |
| `0b00-0b28` | piix4_smbus |

**Unusual values and what they suggest.** If the addresses show as `0000-0000`, you are not root and
there is no srs-root cache. The orange line tells you how to fill it. Two drivers on one range can
mean a firmware resource conflict (the kernel log says "resource busy").

**Where Linux gets it.** `/proc/ioports`.

---

## IrDA ports

`irports` - Not present on most PCs

**In plain words.** IrDA was infrared data transfer, used on old laptops and phones. Linux removed
IrDA support in kernel 4.17.

**What you see.** `Status: Not present on this system`, `Checked: n/a (no IrDA)`.

---

## ISA bus

`isa-bus` - Linux data

**In plain words.** The ISA (Industry Standard Architecture) bus is gone, but its job lives on in the
LPC bus. LPC is a slim bus that links the chipset to the Super I/O chip, TPM, serial port and BIOS flash.
This item shows the LPC bridge and what sits behind it.

**What you see.**
- **LPC bridge** - the PCI class 0601 device, with its driver (often `(none)`, which is normal).
- **ISA bus devices** - how many old-style ISA drivers are probing (usually 0).
- **LPC devices enumerated by ACPI PnP** - the same resource map as [PnP manager](#pnp-manager).
- **Super I/O** - hwmon chips from Nuvoton (`nct`), ITE (`it8`) or Fintek (`f718`), with their platform device.

**Healthy / typical values.** On the author's PC: LPC bridge `0000:00:14.3 FCH LPC Bridge`, 0 ISA devices,
one PnP device (`PNP0501` COM port at `io 0x3f8-0x3ff`, `irq 4`), and the Super I/O `hwmon4 nct6799 (nct6775.656)`.

**Where Linux gets it.** `/sys/bus/pci/devices` (class 0601), `/sys/bus/isa/devices`,
`/sys/bus/pnp/devices`, `/sys/class/hwmon`.

**Background.** The **Super I/O** chip handles the slow, old-style jobs: the serial port, PS/2 and
fan/voltage monitoring. On most desktop boards it is the chip SRS reads fan speeds from.

---

## MCA bus

`mca-bus` - Not present on most PCs

**In plain words.** MCA (Micro Channel Architecture) was IBM's PS/2 expansion bus from 1987. Linux dropped
it long ago.

**What you see.** `Status: Not present on this system`, `Checked: n/a (no MicroChannel)`.

---

## Chipset MCH

`mch` - Linux data - Needs root data

**In plain words.** On older Intel PCs the MCH (Memory Controller Hub, the "northbridge") held the
memory controller. AMD has no MCH. The nearest thing is the Root Complex at `00:00.0`, so SRS shows
that device in full.

**What you see.** The full PCI card for `0000:00:00.0`:

| Field | Meaning |
|---|---|
| Location | `[ bus - device - function ]` in SIV style. |
| IDs | `VEN_ DEV_ SUBSYS_ REV_` in Windows style. |
| Lookup key | The key used for the name lookup. Double-click it for Device ID Lookup. |
| Class | The 6-digit class code with its class and sub-class names. |
| Subsystem | Subsystem vendor:device and the vendor name. |
| Parent bridge | The upstream bridge, or `root bus`. |
| Driver | The driver and module. |
| IRQ, NUMA node, Local CPUs | The interrupt, memory node (-1 = single node) and the CPUs closest to it. |
| PCIe link, Power state, IOMMU group | Shown when the device has them. |
| BAR / ROM lines | Its memory windows, if any. |
| Modalias | The driver-matching ID. |

Then a note on why AMD has no MCH, then (with the srs-root cache) the `lspci -vvv` text and the hex
dump of its configuration space.

**Healthy / typical values.** On the author's PC: `1022:14D8`, subsystem `1043:8877 ASUSTeK Computer Inc.`,
class `060000 Bridge / Host bridge`, driver `ryzen_smu`, NUMA node `-1`, local CPUs `0-15`, power state `D0`.

**Where Linux gets it.** `/sys/bus/pci/devices/0000:00:00.0/*`; configuration space from the srs-root
cache (`pci-all.txt`).

**Background.** On Ryzen the memory controllers (UMC, Unified Memory Controller) are part of the CPU's
Data Fabric. Their settings are reached through SMN (System Management Network), which SRS never touches,
because doing so can hang the CPU's management firmware. For memory timings see the memory items.

---

## Chipset MRH

`mrh` - Not present on most PCs

**In plain words.** The MRH (Memory Repeater Hub) was an Intel 820/840-era chip (1999-2000) that let
RDRAM boards use SDRAM. No current PC has one.

**What you see.** `Status: Not present on this system`, `Checked: n/a (Intel memory repeater hub)`.

---

## NPU names

`npu-names` - Not present on most PCs

**In plain words.** An NPU (Neural Processing Unit) is an AI accelerator. AMD Ryzen AI laptop chips
have one (XDNA). Linux lists such devices under `/sys/class/accel`.

**What you see.**
- `/sys/class/accel  N devices`.
- `amdxdna  loaded / not loaded` (the AMD NPU driver).
- A note: "Desktop Ryzen 9000 (Granite Ridge) has no XDNA NPU."

**What "not present" means.** 0 devices on a desktop Ryzen is correct. On a Ryzen AI laptop with
0 devices, install or load `amdxdna` (kernel 6.14 or newer).

---

## PCI all

`pci-all` - Linux data - Needs root data

**In plain words.** This is everything SRS knows about every PCI device, in one long report.

**What you see.**
- **With the srs-root cache:** "Full configuration space (lspci -vvvnnxxx, srs-root cache)", then for
  each device the full `lspci -vvv` text (Control/Status flags, capabilities, link status, ASPM, AER)
  and a hex dump of its configuration space (256 bytes, or 4096 for PCIe).
- **Without it:** the same per-device card as [Chipset MCH](#chipset-mch) for every device
  (Location, IDs, Lookup key, Class, Subsystem, Parent bridge, Driver, IRQ, NUMA node, Local CPUs,
  PCIe link, Power state, IOMMU group, BARs, Modalias), then an orange line saying extended
  configuration space needs root.

**Healthy / typical values.** On the author's PC the root cache holds about 3700 lines for 54 devices.
Useful lines in the `lspci` text: `LnkCap` (what the link can do), `LnkSta` (what it is doing now) and
`Kernel driver in use`.

**Unusual values and what they suggest.** `LnkSta` showing `(downgraded)`. Uncorrectable error bits set
under AER (Advanced Error Reporting). `DevSta: CorrErr+` that keeps coming back points to a weak link:
reseat the card, or set the slot to a lower PCIe generation in the BIOS.

**Where Linux gets it.** sysfs per device; the root cache holds `lspci -vvvnnxxx` output.

---

## PCI BARs

`pci-bars` - Linux data

**In plain words.** A BAR (Base Address Register) is a window of addresses that a PCI device claims so
the CPU can talk to it. This item lists every window: where it is, how big it is and what kind it is.

**What you see.**

| Column | Meaning |
|---|---|
| Location | The PCI address. |
| BAR | `BAR 0`-`BAR 5`, or `ROM` (the option ROM / VBIOS window). |
| Start / End | The address range (hex). |
| Type | `Memory` or `I/O`, plus `Prefetch` (safe to read ahead and cache) and `64-bit` (can sit above 4 GB), and the size. |
| Device | The device name. |

At the end: `< 4GB Total` and `> 4GB Total`, the memory BAR space below and above the 4 GB line.
You can filter by slot, e.g. `pci-bars:03:00.0`.

**Healthy / typical values.** On the author's PC the Radeon RX 9070 XT's `BAR 0` is
`Memory Prefetch 64-bit 16GB` at `0x1F800000000`. That is the whole 16 GB of VRAM visible to the CPU.
Totals: 6.70 MB below 4 GB and 16.25 GB above.

**Unusual values and what they suggest.** If a modern graphics card's big BAR is only `256MB`,
Resizable BAR (Smart Access Memory) is off. To turn it on, enable "Above 4G Decoding" and "Re-Size BAR"
in the BIOS. This can help some games. A BAR with no address (all zeros) was not assigned: the kernel ran
out of address space for it, and the kernel log will say "can't assign".

**Where Linux gets it.** `/sys/bus/pci/devices/*/resource` (start, end and flags for each BAR).

---

## PCI bus

`pci-bus` - Linux data

**In plain words.** This is the main PCI view: every PCI and PCIe device in a tree, with its name, type,
driver and link speed. It is the **PCI Bus** button on the classic main window, and the **PCI bus** tab of the **USB & PCI devices** page.

**What you see.**

| Column | Meaning |
|---|---|
| PCI Location | `bus:device.function`, indented under the bridge it sits behind. |
| Vendor and Device | Names from the PCI ID database. |
| Device Class | e.g. `VGA compatible controller`, `Non-Volatile memory controller`, `PCI bridge`. |
| Driver | The bound driver, or `-`. |
| Link | For PCIe devices: `xW S GT/s PCIe (max xW S GT/s PCIe)`, the current width and speed, then the maximum. It is blank for devices with no PCIe link. |

Footer: `Total of N PCI devices (B bridges) in T seconds using <pci.ids> Date ... Version ...`, then the
board and BIOS line.

The panel buttons go to [Device properties](#device-properties), [PnP devices](#pnp-devices),
[PCI devices](#pci-devices) and the drive, UAA (audio) and all-device views.

**Healthy / typical values.** On the author's PC (54 devices, 35 bridges):
- GPU `03:00.0` Navi 48 -> `amdgpu`, `x16 32.0 GT/s PCIe (max x16 32.0 GT/s PCIe)`. That is the full
  PCIe 5.0 x16, through the card's own internal switch (`01:00.0`/`02:00.0`).
- Samsung 980 PRO `04:00.0` on the CPU's M.2 slot: `x4 16.0 GT/s` (PCIe 4.0 x4, its maximum).
- Chipset uplink `00:02.1` -> `05:00.0`: `x4 16.0 GT/s`. Everything behind the chipset shares this.
- Realtek 5GbE `08:00.0`: `x1 8.0 GT/s`. Wi-Fi 7 `09:00.0`: `x1 5.0 GT/s`.

**Unusual values and what they suggest.**
- **Current width lower than max** (e.g. `x8 ... (max x16 ...)` on a graphics card): the card is in a
  slot wired for fewer lanes, the slot shares lanes with an M.2 drive or a second slot, or the
  contacts are dirty or the card is not fully seated.
- **Current speed lower than max:** often harmless. Graphics cards drop to 2.5 GT/s at idle to save
  power, so check under load. A bridge port also trains down to the slowest partner: on the author's PC
  port `06:07.0` reads `x1 5.0 GT/s (max x1 8.0 GT/s)` because the Wi-Fi card behind it tops out at 5.0.
  A device that never reaches its maximum under load has a signal problem: a riser cable, or a slot
  forced to a lower generation in the BIOS.
- A device with Driver `-` that should have one (network, storage, graphics) is not supported by your kernel.

Double-click a device row's ID in [PCI devices](#pci-devices) or [Device IDs](#device-ids) to open
Device ID Lookup.

**Where Linux gets it.** `/sys/bus/pci/devices/*` (`vendor`, `device`, `class`, `driver`,
`current_link_speed`, `current_link_width`, `max_link_speed`, `max_link_width`) and `pci.ids`.

**Background - PCIe link speed and width.** A PCIe link is made of **lanes**. Each lane is a pair of
wires in each direction. **Width** (x1, x4, x8, x16) is how many lanes. **Speed** is how fast each lane
runs, in GT/s (gigatransfers per second). Each PCIe generation doubles the speed:

| Speed | Generation | About per lane | x4 | x16 |
|---|---|---|---|---|
| 2.5 GT/s | PCIe 1.x | 250 MB/s | 1 GB/s | 4 GB/s |
| 5.0 GT/s | PCIe 2.0 | 500 MB/s | 2 GB/s | 8 GB/s |
| 8.0 GT/s | PCIe 3.0 | 985 MB/s | 3.9 GB/s | 15.8 GB/s |
| 16.0 GT/s | PCIe 4.0 | 1.97 GB/s | 7.9 GB/s | 31.5 GB/s |
| 32.0 GT/s | PCIe 5.0 | 3.94 GB/s | 15.8 GB/s | 63 GB/s |

The link runs at the best speed and width that **both ends** support. Bridges and switches (upstream
and downstream ports) fan one link out to several devices, and those devices share the upstream bandwidth.

---

## PCI devices

`pci-dev` - Linux data

**In plain words.** This is a flat list of every PCI device with its ID and class code, like
`lspci -nn`. Add a slot to see one device in full. It is the **PCI Dev** button in the PCI Bus panel.

**What you see.**
- List form: `0000:03:00.0  1002:7550 [0300]  VGA compatible controller: <vendor> <device>`, which is the
  full address, vendor:device, the 4-digit class code, and the class and names.
- Per-device form `pci-dev:<slot>` (e.g. `pci-dev:0000:03:00.0` or `pci-dev:03:00.0`): the full card
  described under [Chipset MCH](#chipset-mch), including **PCIe link** and **IOMMU group**.

Double-click a `1002:7550`-style ID to open Device ID Lookup.

**Healthy / typical values.** On the author's PC: `0000:04:00.0 144D:A80A [0108] Non-Volatile memory
controller: Samsung Electronics Co Ltd SSD 980 PRO` and `0000:00:14.3 1022:790E [0601] ISA bridge: ... FCH LPC Bridge`.

**Where Linux gets it.** `/sys/bus/pci/devices/*` and `pci.ids`.

**Background - PCI address and class code.** `0000:03:00.0` is *domain:bus:device.function*. One chip
can hold several **functions**. The GPU is `03:00.0` and its HDMI audio is `03:00.1`. The **class
code** says what kind of device it is: `0300` display, `0108` NVMe, `0200` Ethernet, `0280` other
network (Wi-Fi), `0403` HD audio, `0604` PCI bridge, `0C03` USB controller.

---

## PCI flags

`pci-flag` - Windows-only (Linux equivalent shown)

**In plain words.** On Windows, SIV shows the HAL's PCI workaround flags. On Linux the matching
controls are kernel command-line options, so SRS lists the PCI, PCIe, IOMMU and ASPM options you
booted with.

**What you see.** One line per option starting with `pci`, `pcie`, or containing `iommu` or `aspm`. Or
the note "No pci=/pcie_*/iommu options on the kernel command line".

**Healthy / typical values.** No options at all is the normal default. On the author's PC:
`pcie_aspm.policy=performance` (keeps links out of power saving), `pci=pcie_bus_perf` (sets the largest
safe packet size) and `iommu=pt` (pass-through: the IOMMU does not remap the host's own devices, which is
a little faster).

**Unusual values and what they suggest.** `pci=noaer` or `pcie_aspm=off` are often added to silence
error floods. They hide the symptom and do not fix the link. `pci=nomsi` is a heavy workaround that can
reduce performance.

**Where Linux gets it.** `/proc/cmdline`.

---

## PCI HAL

`pci-hal` - Linux data - Needs root data

**In plain words.** This shows the address "windows" that the firmware gave each PCI bus: which slices of
memory and I/O space each bridge passes on to the devices behind it.

**What you see.**
- **Memory windows** - lines of the memory map naming `PCI Bus`, `PCI ECAM`, `MMCONFIG` or `pcie`, up
  to 3 levels deep.
- **I/O windows** - the same for I/O ports, 2 levels deep, including `0cf8-0cff PCI conf1`.
- **PCI buses** - the count and the list of bus numbers.

**Healthy / typical values.** On the author's PC: root window `80000000-dfffffff` below 4 GB, a big
window `10000000000-1ffffffffff` above 4 GB (where the GPU's 16 GB BAR lives, `1f800000000-1fc0fffffff`
for buses 01-02), the ECAM block `e0000000-efffffff`, and 21 buses.

**Unusual values and what they suggest.** No window above 4 GB means "Above 4G Decoding" is off, so
Resizable BAR cannot work.

**Where Linux gets it.** `/proc/iomem`, `/proc/ioports` (srs-root cache), `/sys/class/pci_bus`.

---

## PCI ID database

`pci-info` - Linux data

**In plain words.** These are facts about the file SRS uses to turn PCI ID numbers into names.

**What you see.**

| Field | Meaning |
|---|---|
| Database | The file path, normally `/usr/share/hwdata/pci.ids`. |
| SIV equivalent | `pcidevs.txt` (SIV's own file). |
| Version / Date | When the list was built. |
| Records | How many vendor, device and subsystem names it holds. |
| Load time | How long loading took, and how long this lookup took. |
| Lookup chain | `V:D:S:s:RR -> V:D:S:s -> V:D:RR -> V:D -> V`, the order SRS tries (see [Device IDs](#device-ids)). |
| Class names | How many class, subclass and programming-interface names it holds. |

**Healthy / typical values.** On the author's PC: version `2026.10.01`, 42,228 names, 224 class
records, loaded in about 10 ms.

**Unusual values and what they suggest.** A date more than a year old means new hardware will show as
"Device XXXX". Update the `hwdata` package. "ids file not found" means `hwdata` is not installed.

**Where Linux gets it.** `/usr/share/hwdata/pci.ids`.

---

## PCI MMIO

`pci-mmio` - Linux data - Needs root data

**In plain words.** PCIe devices have up to 4 KB of configuration space each. The CPU reaches it
through one big memory window called ECAM (Enhanced Configuration Access Mechanism). The ACPI MCFG
table tells the OS where that window is. This item shows it.

**What you see.**
- **MCFG table** - `present` or `absent`.
- **ECAM base** - its address, PCI segment and bus range (needs the srs-root cache).
- The `PCI ECAM` / `MMCONFIG` line from the memory map.

**Healthy / typical values.** On the author's PC: `ECAM base 0xE0000000 segment 0 buses 0-255`, and
`e0000000-efffffff PCI ECAM 0000 [bus 00-ff]`. That is 256 MB: 1 MB per bus × 256 buses.

**Unusual values and what they suggest.** If MCFG is absent, Linux falls back to the old port
`0xCF8` method. That method only reaches the first 256 bytes, so PCIe extended capabilities (AER, ReBAR,
link details) become invisible.

**Where Linux gets it.** `/sys/firmware/acpi/tables/MCFG`, `/proc/iomem`.

---

## PCMCIA all

`pcm-all` - Not present on most PCs

**In plain words.** PCMCIA / CardBus were credit-card-sized expansion cards for laptops from the 1990s
and 2000s. ExpressCard and USB replaced them.

**What it would show.** Whether `/sys/class/pcmcia_socket` exists, and how many CardBus/PCMCIA bridges
(PCI class 0605/0607) are on the PCI bus.

**What "not present" means.** On the author's PC: `absent` and `0`. That is expected on any desktop.

---

## PCMCIA bus

`pcm-bus` - Not present on most PCs

**In plain words.** This is the same check as [PCMCIA all](#pcmcia-all): socket class and bridge count.

**What you see.** `/sys/class/pcmcia_socket  absent` and `CardBus/PCMCIA bridges  0 (PCI class 0605/0607)`.

---

## PCMCIA ID database

`pcm-info` - Not present on most PCs

**In plain words.** SIV has a name list for PCMCIA cards. With no PCMCIA hardware, SRS does not
load one.

**What you see.** `Status: Not present on this system`, `Checked: n/a (no PCMCIA)`.

---

## I/O PM1 data

`pm1-area` - Linux data - Needs root data

**In plain words.** ACPI defines a few fixed blocks of I/O ports for power management. The **PM1**
blocks handle sleep, wake and the power button. **PM_TMR** is a 3.58 MHz timer. **GPE**
(General Purpose Events) are wake and event signals. The firmware's FADT (Fixed ACPI Description Table)
says where they are. SRS shows the addresses and does not read the registers.

**What you see.**
- From the FADT (srs-root cache): `PM1a_EVT_BLK`, `PM1b_EVT_BLK`, `PM1a_CNT_BLK`, `PM1b_CNT_BLK`,
  `PM2_CNT_BLK`, `PM_TMR_BLK`, `GPE0_BLK`, `GPE1_BLK`, each with its port address and length. Unused
  blocks show `0x0000` in gray.
- The matching I/O port map lines (`ACPI`, `PM1`, `PM_TMR`, `GPE`).
- **PM profile** - the firmware's "preferred power profile": 1 = Desktop, 2 = Mobile,
  3 = Workstation, 4 = Enterprise server, 0 = unspecified.

**Healthy / typical values.** On the author's PC: `PM1a_EVT_BLK 0x0800 length 4`, `PM1a_CNT_BLK 0x0804
length 2`, `PM2_CNT_BLK 0x00B4 length 1`, `PM_TMR_BLK 0x0808 length 4`, `GPE0_BLK 0x0820 length 8`, the "b"
blocks unused, and PM profile `1` (Desktop).

**Unusual values and what they suggest.** A laptop reporting profile 1, or a desktop reporting 2, can
change some power defaults. That is a firmware quirk.

**Where Linux gets it.** `/sys/firmware/acpi/tables/FACP` (root), `/proc/ioports`,
`/sys/firmware/acpi/pm_profile`.

---

## PMBus

`pmb-bus` - Not present on most PCs

**In plain words.** PMBus is an SMBus-based protocol for "smart" power supplies and voltage regulators
that report volts, amps and temperature. Servers have it. A few PSUs expose it over USB instead.

**What you see.** `Status: Not present on this system`, `Checked: /sys/bus/i2c/devices (no PMBus devices)`.

**What "not present" means.** No kernel PMBus driver is bound. A USB-linked PSU (Corsair, NZXT)
shows up under the USB items and sensor tools instead.

---

## PnP manager

`pnp` - Linux data

**In plain words.** PnP (Plug and Play) devices here are the motherboard's built-in legacy devices that
the firmware describes, such as the COM port. This item shows the resources (ports, IRQ, DMA) each one has.

**What you see.** For each device: `<pnp name>  <ID>  <description>  (ACPI <firmware node>)`, then its
resource lines exactly as Linux reports them: `state = active`, `io 0x3f8-0x3ff`, `irq 4`, `dma disabled`.

**Healthy / typical values.** On the author's PC there is one device, `00:00 PNP0501 16550A-compatible COM
Port (ACPI PNP0501:00)`, active at `io 0x3f8-0x3ff`, `irq 4`. Modern Linux binds most other firmware
devices through ACPI directly, so the PnP list is short (see [PnP manager details](#pnp-manager-details)).

**Where Linux gets it.** `/sys/bus/pnp/devices/*/id`, `.../resources`, `.../firmware_node`.

---

## PnP devices

`pnp-dev` - Linux data

**In plain words.** These are the same PnP devices as a one-line table. It is the **PnP Dev** button in
the PCI Bus panel.

**What you see.**

| Column | Meaning |
|---|---|
| Device | The PnP device name (`00:00`). |
| ID | The PnP ID (`PNP0501`). |
| Description | A built-in name, or the vendor from `pnp.ids`. |
| Driver | The bound driver (`serial`). |
| Resources | The active resources in one line (lines that say `disabled` are left out). |

**Healthy / typical values.** `00:00  PNP0501  16550A-compatible COM Port  serial  io 0x3f8-0x3ff, irq 4`.

**Where Linux gets it.** `/sys/bus/pnp/devices`.

---

## PnP ID database

`pnp-info` - Linux data

**In plain words.** This item explains the PnP/ACPI ID names and lists every kind of ACPI device ID
found on this PC.

**What you see.**
- **Database** - `/usr/share/hwdata/pnp.ids`. **SIV equivalent** - `pnpdevs.txt`.
- **Records** - how many 3-letter vendor codes the file has.
- **Built-in** - SRS also knows the standard `PNPxxxx`, `ACPIxxxx` and `LNXxxxx` names itself.
- **IDs on this system** - each distinct ACPI ID with its name.

**Healthy / typical values.** On the author's PC: 2557 vendor codes. Some IDs: `PNP0C09 ACPI Embedded
Controller`, `PNP0A08 PCI Express Root Bus`, `MSFT0101 TPM 2.0`, `AMDI0030 AMD GPIO Controller`,
`ACPI0007 Processor`, `LNXVIDEO ACPI Video Bus`.

**Unusual values and what they suggest.** For IDs SRS does not know by heart, the name is a guess from
the first three letters of the ID. That guess comes from the old 3-letter EISA vendor list. Newer ACPI IDs
use a 4-letter vendor prefix (`AMDI`, `ASMT`, `RTK`), so for those the guessed vendor can be wrong.
Read such names as hints only.

**Where Linux gets it.** `/usr/share/hwdata/pnp.ids`, `/sys/bus/acpi/devices`.

**Background.** A PnP ID is 3 letters for the vendor plus 4 hex digits (`PNP0501`). ACPI also allows
4 letters plus 4 hex digits (`AMDI0030`). `PNP` and `ACPI` IDs are industry-standard device types.

---

## PnP manager details

`pnp-man` - Linux data

**In plain words.** This is every device the firmware's ACPI tables describe, with its path in the ACPI
"namespace", whether the firmware says it is present, and what Linux attached it to.

**What you see.**

| Column | Meaning |
|---|---|
| Device | The ACPI device name (`AMDI0010:00`). |
| ACPI Path | Its namespace path (`\_SB_.I2CA`). |
| _STA | The firmware's status value. `0x0F` = present, enabled and working. `0x00 absent` = turned off or not fitted. `-` = no status method, so treated as present. |
| Description | The name from the built-in list or `pnp.ids`. |
| Bound to | The Linux device it became (e.g. `cpu0`), if any. |

Last line: `Total of N ACPI devices, P present (no _STA = present)`.

**Healthy / typical values.** On the author's PC: 258 ACPI devices, 229 present. The 16 `ACPI0007`
processor objects map to `cpu0`-`cpu15` (in the order C000 -> cpu0, C001 -> cpu8...: the firmware lists
the SMT siblings in turn). `AMDI0010:00` (I2C controller A) is `0x0F`. I2C B-D and I3C controllers are
`0x00 absent`, because the board does not wire them.

**Unusual values and what they suggest.** Many `absent` entries are normal. Boards share one firmware
across models. A device you need (touchpad I2C, TPM) showing `absent` means it is turned off in the BIOS.

**Where Linux gets it.** `/sys/bus/acpi/devices/*/path`, `status`, `physical_node`.

---

## COM ports

`ports` - Linux data

**In plain words.** This lists the serial (COM) ports. It is the same view as
[Serial COM ports](#serial-com-ports).

**What you see / values.** See [Serial COM ports](#serial-com-ports).

---

## Device properties

`properties` - Linux data

**In plain words.** These are all the udev properties of every PCI and USB device: IDs, names from the
hardware database, paths, driver. It is the **Properties** button in the PCI Bus panel.

**What you see.** For each PCI device and each USB device (USB interfaces are skipped), the sysfs path in
bold, then one line per property:
- Teal lines from udev's own records: `symlink`, `devnode`, `priority` (symlink priority) and `driver`.
- `E:` properties such as `PCI_ID`, `PCI_SUBSYS_ID`, `PCI_CLASS`, `MODALIAS`, `ID_VENDOR_FROM_DATABASE`,
  `ID_MODEL_FROM_DATABASE`, `ID_PATH`. USB devices add `BUSNUM`, `DEVNUM`, `ID_VENDOR_ID`, `ID_MODEL_ID`,
  `ID_REVISION`, `ID_SERIAL` and so on.

Values whose key contains TOKEN, KEY, SECRET, PASS, AUTH or COOKIE are shown as `********`. Use
`properties:<name>` for any single device, e.g. `properties:nvme0n1` or `properties:0000:03:00.0`.
Double-click a `PCI_ID` value (`1022:14D8`) to open Device ID Lookup.

**Healthy / typical values.** On the author's PC the Root Complex shows `PCI_ID 1022:14D8`,
`PCI_SUBSYS_ID 1043:8877`, `ID_MODEL_FROM_DATABASE Raphael/Granite Ridge Root Complex`,
`ID_PATH pci-0000:00:00.0`.

**Unusual values and what they suggest.** Missing `ID_*_FROM_DATABASE` keys mean udev's hardware
database (hwdb) does not know the device. Run `systemd-hwdb update` after updating `hwdata`. Note that
USB serial numbers (`ID_SERIAL`) appear in full. Remove them before you share a report.

**Where Linux gets it.** `udevadm info --export-db`.

---

## Device property

`property` - Linux data

**In plain words.** This shows one udev property across every device that has it. By default it shows
`ID_MODEL_FROM_DATABASE` (the database model name).

**What you see.** `Property  <KEY>`, then one line per device: the device path and the value. At the end:
`N devices have <KEY>`. Choose another key with `property:<KEY>`, e.g. `property:ID_PATH` or
`property:DRIVER`. [Device property keys](#device-property-keys) lists all keys.

**Healthy / typical values.** On the author's PC 117 devices have `ID_MODEL_FROM_DATABASE`, e.g.
`.../0000:03:00.0  Navi 48 [Radeon RX 9070/9070 XT/9070 GRE]`. Child entries (sound cards, network
interfaces) repeat their parent's name.

**Where Linux gets it.** `udevadm info --export-db`.

---

## PYXIS 21174

`pyxis` - Not present on most PCs

**In plain words.** The 21174 "Pyxis" was the core logic chipset of DEC Alpha workstations (about 1997).
SIV can read it on Alpha Windows NT. No x86 PC has one.

**What you see.** `Status: Not present on this system`, `Checked: n/a (DEC Alpha chipset)`.

---

## Smart cards

`scard` - Not present on most PCs

**In plain words.** These are smart card readers: CCID (Chip Card Interface Device) USB readers for ID
cards, bank cards and some security tokens.

**What you see.** `Status: Not present on this system`, `Checked: pcsc_scan (no CCID reader; pcscd inactive)`.

**What "not present" means.** No reader is plugged in, and the `pcscd` smart-card service is not running.
With a reader attached, start `pcscd` and run `pcsc_scan`. A FIDO/U2F security key is a HID device,
not a smart card (see [HID bus](#hid-bus)).

---

## Serial COM ports

`serial` - Linux data

**In plain words.** This lists the real serial ports (UARTs, Universal Asynchronous Receiver-Transmitters)
and every serial-type TTY driver the kernel has.

**What you see.**

| Column | Meaning |
|---|---|
| Port | The Linux name (`ttyS0` = COM1, `ttyUSB0` = a USB adapter). |
| I/O | The I/O port base address (`0x3F8`). |
| IRQ | The interrupt line. |
| UART | The chip type (`16550A`, `16950`...). USB and other adapters show `usb/other`. |
| Base clock | The UART's input clock. The standard is 1.8432 MHz, which gives 115200 baud at the maximum. |
| Device | The device it belongs to (the PnP device `00:00:0.0`). |

Then `N real ports; M unconfigured 8250 placeholders (nr_uarts) hidden`. Linux makes empty `ttyS1`-`ttyS31`
slots by default, and SRS hides those. Then comes the **TTY drivers** table from `/proc/tty/drivers`: driver
name, `/dev` prefix, major number, minor range and type. The note says per-line statistics need root.

**Healthy / typical values.** On the author's PC: `ttyS0  0x3F8  4  16550A  1.8432 MHz  00:00:0.0`, with 31
placeholders hidden. The drivers include `usbserial /dev/ttyUSB 188`, `usbch343 /dev/ttyCH343USB 170`
(a CH343 USB adapter driver), `rfcomm` (Bluetooth serial) and `serial_8250 /dev/ttyS 4 64-95`.

**Unusual values and what they suggest.** A port with UART `unknown` is not really there. Many boards
have a COM header that only works with a bracket cable fitted. An IRQ of 0 means the port is polled,
which is slow. A USB serial adapter appears here only while it is plugged in.

**Where Linux gets it.** `/sys/class/tty/*` (`type`, `port`, `irq`, `uartclk`, `device`), `/proc/tty/drivers`.

---

## SI USB

`si-usb` - Linux data

**In plain words.** This lists the USB host controllers. Each controller appears as two "root hubs":
one for USB 2.0 devices and one for USB 3.x devices.

**What you see.** Two lines per root hub:
- `usbN  <PCI address>  <controller name>`. Software-only controllers show the platform device and
  "virtual host controller".
- `USB <version> root hub, <speed> Mbps, <P> ports, <D> devices attached, driver <driver>`.

**Healthy / typical values.** On the author's PC (11 root hubs):
- `usb1`/`usb2` on `73:00.0` ASMedia ASM4242: USB 2.00 at 480 Mbps and USB 3.10 at 20000 Mbps
  (20 Gbps, the USB 3.2 Gen 2x2 / USB4 ports).
- `usb3`-`usb6` on the CPU's two USB 3.1 controllers (`75:00.3`, `75:00.4`): 480 and 10000 Mbps.
- `usb7` on the CPU's USB 2.0 controller `76:00.0`.
- `usb8`/`usb9` on the chipset controller `0b:00.0`: 12 USB 2.0 ports with 13 devices, and 5 USB 3 ports
  at 20000 Mbps.
- `usb10`/`usb11` = `vhci_hcd.0`, the USB/IP virtual controller (USB over the network), with 0 devices.

**Unusual values and what they suggest.** Too many devices on one controller can run out of bandwidth
or endpoints. Webcams and audio interfaces are the usual culprits. Move one to a port on a different
`usbN`. A controller that is missing completely may be turned off in the BIOS.

**Where Linux gets it.** `/sys/bus/usb/devices/usb*` (`version`, `speed`, `maxchild`), with the
controller found from the sysfs path.

**Background - USB speeds and descriptors.**

| Speed | Name | First in |
|---|---|---|
| 1.5 Mbps | Low Speed | USB 1.0 (keyboards, mice) |
| 12 Mbps | Full Speed | USB 1.1 |
| 480 Mbps | High Speed | USB 2.0 |
| 5000 Mbps | SuperSpeed | USB 3.0 / 3.2 Gen 1 |
| 10000 Mbps | SuperSpeed+ | USB 3.1 / 3.2 Gen 2 |
| 20000 Mbps | SuperSpeed+ 20 Gbps | USB 3.2 Gen 2x2 |

A USB 3 port is really two buses in one connector. That is why each controller has a 2.0 root hub and a
3.x root hub, and why a slow device plugged into a blue port appears on the 2.0 hub. Every USB device
describes itself with **descriptors**. The *device descriptor* gives the vendor ID (VID), product ID
(PID), release number (bcdDevice) and class. The *configuration descriptor* gives the power draw. Each
*interface descriptor* has a class, subclass and protocol, for example 03 = HID, 08 = mass storage,
0E = video. Each *endpoint descriptor* is one data pipe with its type and packet size. Part 2 of this
reference covers the USB device lists, which show all of these.

---

## SMBus

`smb` - Linux data

**In plain words.** SMBus (System Management Bus) and I2C (Inter-Integrated Circuit) are slow two-wire
buses for small chips: RAM SPD (Serial Presence Detect) EEPROMs, temperature sensors, RGB controllers, and
the graphics card's monitor (DDC) lines. This item lists every bus adapter.

**What you see.**
- **Controller** - the PCI SMBus controller (class 0C05) with its revision and driver.
- One line per adapter: `i2c-N  <adapter name>  <parent device>`. SMBus adapters are in bold.
- A note: names come from sysfs only. No bus is opened or scanned.

**Healthy / typical values.** On the author's PC: controller `0000:00:14.0 FCH SMBus Controller rev 71
driver piix4_smbus`. Adapters `i2c-1`/`i2c-2` "SMBus PIIX4 adapter port 0/2 at 0b00" and `i2c-3` "port 1
at 0b20" (the RAM slots are on port 0). `i2c-0` is the Synopsys DesignWare I2C, `i2c-4`/`5` are "AMDGPU SMU",
and `i2c-6` to `12` are the GPU's display (DM) and DisplayPort AUX buses.

**Unusual values and what they suggest.** No PIIX4 adapters means `i2c_piix4` is not loaded. Then RAM
SPD, temperature and OpenRGB RAM lighting will not work.

**Where Linux gets it.** `/sys/bus/i2c/devices/i2c-*/name`, `/sys/bus/pci/devices` (class 0C05).

---

## SMBus devices

`smb-bus` - Linux data

**In plain words.** These are the chips on the SMBus / I2C buses that a kernel driver has claimed.
SRS lists only those. It never probes for others.

**What you see.**

| Column | Meaning |
|---|---|
| Client | `<bus>-<address>` (e.g. `1-0051` = bus 1, address 0x51), or an ACPI name. |
| Name | The chip type. |
| Driver | The bound driver, or `(none)`. |
| Adapter | The bus it is on and that bus's name. |

Last line: `Total of N I2C client devices (bus-addr; 0x50-0x57 = SPD5118 hub on DDR5)`.

**Healthy / typical values.** On the author's PC: `1-0051` and `1-0053`, both `spd5118`, on
`i2c-1 SMBus PIIX4 adapter port 0`. These are the SPD hubs of the two DDR5 sticks (their addresses tell
you which slots are filled). There are also two Realtek ACPI devices on the DesignWare I2C bus with no driver.

**Unusual values and what they suggest.** Fewer `spd5118` entries than DIMMs fitted means the
`spd5118` module is not loaded, or a stick's SPD hub did not answer. That is worth a reseat if the
memory count is also wrong.

**Where Linux gets it.** `/sys/bus/i2c/devices/<bus>-<addr>` (`name`, `driver`).

---

## SMBus status

`smb-sts` - Linux data - Needs root data

**In plain words.** SIV reads the SMBus controller's status registers. SRS only shows where they are,
because touching them can collide with the kernel driver and with RGB or sensor programs that use the
bus at the same time.

**What you see.** The I/O port ranges owned by `piix4_smbus` / `smbus`, and a note explaining why the
registers are not read.

**Healthy / typical values.** On the author's PC: `0b00-0b08` and `0b20-0b28`, both `piix4_smbus` (the
FCH's two SMBus controllers).

**Where Linux gets it.** `/proc/ioports` (srs-root cache).

---

## SMBus setup

`smbus-setup` - Linux data

**In plain words.** This shows how SRS handles SMBus: it never probes, and it trusts only what the
kernel has found. It also lists every I2C driver and how many chips each one has claimed.

**What you see.**
- **Probing** - `disabled`, with the reason (CoolerControl, liquidctl, OpenRGB and the kernel own the bus).
- **Data source** - kernel-made clients in `/sys/bus/i2c/devices/<bus>-<addr>`.
- **I2C drivers** - every loaded I2C driver with `N clients`.

**Healthy / typical values.** On the author's PC almost every driver has 0 clients. These are built into
the kernel for other hardware. `spd5118` has 2 clients (the two DDR5 sticks).

**Where Linux gets it.** `/sys/bus/i2c/drivers/*`.

---

## System SMBus access

`smbus-system` - Linux data

**In plain words.** This shows whether programs can reach the SMBus / I2C buses from user space, and who
is allowed to.

**What you see.**
- **i2c-dev module** - `loaded (/dev/i2c-* nodes exist)` or `not loaded`.
- One line per `/dev/i2c-N`: `mode` (permissions), `uid`, `gid`. These are checked with stat only, never opened.
- **Device nodes** - the count.
- **piix4_smbus** - whether the AMD/Intel PIIX4 SMBus driver is loaded.

**Healthy / typical values.** On the author's PC: 13 nodes, all `mode 0660 uid 0 gid 963`. Root and one
group (the `i2c` group that OpenRGB-style packages create) can read and write. Everyone else cannot.

**Unusual values and what they suggest.** `0666` (anyone can write) lets any program send raw commands
to RAM or GPU chips. That is a risk, so prefer a group. `i2c-dev not loaded` explains why OpenRGB or
`decode-dimms` find nothing. Load it with `modprobe i2c-dev`.

**Where Linux gets it.** `/sys/class/i2c-dev`, `stat()` of `/dev/i2c-*`, `/sys/module/i2c_dev`,
`/sys/module/i2c_piix4`.
