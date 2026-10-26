# SRS Section Reference
[Back to the User Guide](../USER-GUIDE.md) - [Glossary](../GLOSSARY.md)
I wanted a Linux version of SIV (System Information Viewer) for myself, so I built one and set out to improve on it along the way. SRS is free and open source for the Linux and open-source community. (SRS is an independent project, not affiliated with SIV or its author.) SIV is the work of Ray Hinchliffe, RH Software (rh-software.com).
## How to use this reference

This reference explains **every item SRS can show** - all 551 of them - one by one. For each item it
says, in this order:

1. **In plain words** - what it is, in one to three short sentences.
2. **What you see** - every column and field, and why it matters.
3. **Healthy / typical values** - what normal looks like, with real examples from the author's PC
   (AMD Ryzen 7 7800X3D, ASUS TUF GAMING X870-PRO WIFI7, 2 x 16 GB DDR5-6000, Radeon RX 9070 XT).
4. **Unusual values and what they suggest.**
5. **Where Linux gets it** - the file, table or tool the value comes from.
6. **Background** - the idea behind it (what a cache is, what a P-state is, ...), where it helps.

Every technical word is explained the first time a page uses it, and again in the
[Glossary](../GLOSSARY.md).

**Badges** under each title:

- **Linux data** - shows real data from your Linux system.
- **Not present on most PCs** - SIV items for hardware most PCs lack (Itanium, IPMI, RAID cards...).
  On a normal PC they say so.
- **Windows-only (Linux equivalent shown)** - a Windows idea; SRS shows the nearest Linux equivalent.
- **Needs root data** - some or all of the values come from the `srs-root` cache
  (`~/.cache/srs/root`, see the User Guide). Without it those values are empty.

**How to open an item:**

- **In the main window** (SRS 2.0): pick the page in the sidebar (the table below names it). The
  most used items are tabs; **All N items** at the right end of the tab row lists every item of
  the page. Or type the item's name or id into the search box at the top (**Ctrl+F**). The
  **Guide** button (F1) on each tab opens the item's section here.
- **In the classic view:** Menu -> the group -> the item (each menu entry shows the item id in
  brackets), or the panel buttons named in each group's introduction.
- **From the command line:** `sysreport-search '-save=[id]'`.

## Groups

The groups are in the same order as SRS's Menu.

| Group | Items | Page | Main-window page |
|---|---|---|---|
| Processors | 44 | [processors.md](processors.md) | Processor |
| Machine / BIOS / Memory | 58 | [machine.md](machine.md) | Memory; Motherboard & BIOS |
| Sensors / Cooling / Power | 34 | [sensors.md](sensors.md) | Sensors |
| Volumes / Drives | 28 | [storage.md](storage.md) | Storage |
| Network | 36 | [network.md](network.md) | Network |
| Linux (SIV's Windows items) | 78 | [linux-os.md](linux-os.md) | Operating system |
| Graphics / Monitors / Audio | 52 | [graphics.md](graphics.md) | Graphics & audio |
| Buses / Devices (part 1) | 64 | [buses-1.md](buses-1.md) | USB & PCI devices |
| Buses / Devices (part 2) | 63 | [buses-2.md](buses-2.md) | USB & PCI devices |
| Tools | 34 | [tools.md](tools.md) | Benchmarks & stress; Tools |
| SRS (SIV items) | 60 | [srs-items.md](srs-items.md) | Tools |

## Every item

### Processors

| Item | id | Badge |
|---|---|---|
| [AMD Ryzen Power Management](processors.md#amd-ryzen-power-management) | `amd-cpu` | Linux data |
| [AMD Fuses](processors.md#amd-fuses) | `amd-fus` | Linux data - Needs root data |
| [AMD PM Table](processors.md#amd-pm-table) | `amd-pmt` | Linux data |
| [AMD PM Table 0](processors.md#amd-pm-table-0) | `amd-pmt-0` | Linux data |
| [AMD PM table data @ second package/CCD](processors.md#amd-pm-table-data--second-packageccd) | `amd-pmt-1` | Not present on most PCs |
| [AMD SMU](processors.md#amd-smu) | `amd-smu` | Linux data - Needs root data |
| [AMD Telemetry 0](processors.md#amd-telemetry-0) | `amd-tel-0` | Linux data |
| [AMD SVI telemetry plane @ second die](processors.md#amd-svi-telemetry-plane--second-die) | `amd-tel-1` | Not present on most PCs |
| [AMD Thermal 0](processors.md#amd-thermal-0) | `amd-thm-0` | Linux data |
| [AMD THM registers @ second die](processors.md#amd-thm-registers--second-die) | `amd-thm-1` | Not present on most PCs |
| [AMD Temperatures](processors.md#amd-temperatures) | `amd-tmp` | Linux data |
| [CPU Table](processors.md#cpu-table) | `cpu` | Linux data |
| [CPU Description](processors.md#cpu-description) | `cpu-bits` | Linux data |
| [CPU Clocks](processors.md#cpu-clocks) | `cpu-clocks` | Linux data |
| [CPU Export](processors.md#cpu-export) | `cpu-export` | Linux data |
| [CPUID](processors.md#cpuid) | `cpuid` | Linux data |
| [CPUID Leaf 0](processors.md#cpuid-leaf-0) | `cpuid-000` | Linux data |
| [CPU Idle Levels](processors.md#cpu-idle-levels) | `cpu-levels` | Linux data |
| [CPU OC Data](processors.md#cpu-oc-data) | `cpu-oc-data` | Not present on most PCs - Needs root data |
| [CPU Summary](processors.md#cpu-summary) | `cpus` | Linux data |
| [CPU SMU](processors.md#cpu-smu) | `cpu-smu` | Linux data - Needs root data |
| [CPU Speeds](processors.md#cpu-speeds) | `cpu-speeds` | Linux data |
| [CPU Timers](processors.md#cpu-timers) | `cpu-timer` | Linux data |
| [CPU Tunables](processors.md#cpu-tunables) | `cpu-tweak` | Linux data - Needs root data |
| [CPU Voltages](processors.md#cpu-voltages) | `cpu-volts` | Linux data |
| [CPUID Features](processors.md#cpuid-features) | `features` | Linux data |
| [Hypervisor](processors.md#hypervisor) | `hypervisor` | Linux data |
| [Microcode](processors.md#microcode) | `microcode` | Linux data |
| [MSRs](processors.md#msrs) | `msrs` | Linux data - Needs root data |
| [MSR Setup](processors.md#msr-setup) | `msr-setup` | Linux data - Needs root data |
| [IBM/Cyrix MSRs](processors.md#ibmcyrix-msrs) | `msrs-ibm` | Not present on most PCs - Needs root data |
| [NexGen NXOP](processors.md#nexgen-nxop) | `nexgen-nxop` | Not present on most PCs |
| [Nodes](processors.md#nodes) | `nodes` | Linux data |
| [NUMA](processors.md#numa) | `numa` | Linux data |
| [NUMA memory speed](processors.md#numa-memory-speed) | `numa-speeds` | Linux data |
| [CPU power states](processors.md#cpu-power-states) | `power-states` | Linux data |
| [Processor Features Present](processors.md#processor-features-present) | `present` | Linux data |
| [Processor](processors.md#processor) | `processor` | Linux data |
| [Processors](processors.md#processors) | `processors` | Linux data |
| [Speculation Control](processors.md#speculation-control) | `speculation` | Linux data |
| [Turbo Boost](processors.md#turbo-boost) | `turbo-boost` | Linux data |
| [Xeon PIRs](processors.md#xeon-pirs) | `xeon-pir` | Not present on most PCs |
| [Cache Levels](processors.md#cache-levels) | `cache` | Linux data |
| [CPU Groups](processors.md#cpu-groups) | `group` | Linux data |

### Machine / BIOS / Memory

| Item | id | Badge |
|---|---|---|
| [ACPI PCI root buses](machine.md#acpi-pci-root-buses) | `acpi-buses` | Linux data |
| [ACPI devices](machine.md#acpi-devices) | `acpi-dev` | Linux data |
| [ACPI embedded controller](machine.md#acpi-embedded-controller) | `acpi-ec` | Linux data - Needs root data |
| [ACPI method evaluation](machine.md#acpi-method-evaluation) | `acpi-eval` | Linux data - Needs root data |
| [ACPI HAL](machine.md#acpi-hal) | `acpi-hal` | Linux data |
| [ACPI interrupts and GPEs](machine.md#acpi-interrupts-and-gpes) | `acpi-int` | Linux data |
| [ACPI IPF (Intel Platform Framework) methods](machine.md#acpi-ipf-intel-platform-framework-methods) | `acpi-ipf` | Not present on most PCs |
| [ACPI ICD0001 methods](machine.md#acpi-icd0001-methods) | `acpi-itm` | Not present on most PCs |
| [ACPI methods](machine.md#acpi-methods) | `acpi-methods` | Linux data - Needs root data |
| [ACPI extension (PDX)](machine.md#acpi-extension-pdx) | `acpi-pdx` | Linux data |
| [ACPI PCI buses and ports](machine.md#acpi-pci-buses-and-ports) | `acpi-ports` | Linux data |
| [ACPI power resource methods](machine.md#acpi-power-resource-methods) | `acpi-pwr` | Linux data |
| [ACPI SPB (serial peripheral bus) methods](machine.md#acpi-spb-serial-peripheral-bus-methods) | `acpi-spb` | Linux data |
| [ACPI tables](machine.md#acpi-tables) | `acpi-tables` | Linux data - Needs root data |
| [ACPI Toshiba test](machine.md#acpi-toshiba-test) | `acpi-test` | Not present on most PCs |
| [ACPI TFP INT3403 methods](machine.md#acpi-tfp-int3403-methods) | `acpi-tfp` | Not present on most PCs |
| [ACPI TMS INT340E methods](machine.md#acpi-tms-int340e-methods) | `acpi-tms` | Not present on most PCs |
| [ACPI TOS1900 methods](machine.md#acpi-tos1900-methods) | `acpi-tos` | Not present on most PCs |
| [ACPI TPM](machine.md#acpi-tpm) | `acpi-tpm` | Linux data |
| [ACPI WMI (PNP0C14) methods](machine.md#acpi-wmi-pnp0c14-methods) | `acpi-wmi` | Linux data |
| [APIC (MADT) summary](machine.md#apic-madt-summary) | `bios-apic` | Linux data - Needs root data |
| [ASPT table summary](machine.md#aspt-table-summary) | `bios-aspt` | Not present on most PCs - Needs root data |
| [DMAR table summary](machine.md#dmar-table-summary) | `bios-dmar` | Not present on most PCs - Needs root data |
| [FACP (FADT) summary](machine.md#facp-fadt-summary) | `bios-facp` | Linux data - Needs root data |
| [PCI IRQ routing ($PIR)](machine.md#pci-irq-routing-pir) | `bios-irq` | Not present on most PCs - Needs root data |
| [BIOS maker](machine.md#bios-maker) | `bios-make` | Linux data |
| [MPS table summary](machine.md#mps-table-summary) | `bios-mps` | Not present on most PCs - Needs root data |
| [MSDM table summary](machine.md#msdm-table-summary) | `bios-msdm` | Not present on most PCs - Needs root data |
| [OEMB table summary](machine.md#oemb-table-summary) | `bios-oemb` | Not present on most PCs - Needs root data |
| [BIOS option ROMs](machine.md#bios-option-roms) | `bios-option` | Linux data - Needs root data |
| [SDEV table summary](machine.md#sdev-table-summary) | `bios-sdev` | Not present on most PCs - Needs root data |
| [SFI summary](machine.md#sfi-summary) | `bios-sfi` | Not present on most PCs |
| [SLIC table summary](machine.md#slic-table-summary) | `bios-slic` | Not present on most PCs - Needs root data |
| [SLIT table summary](machine.md#slit-table-summary) | `bios-slit` | Not present on most PCs - Needs root data |
| [SRAT table summary](machine.md#srat-table-summary) | `bios-srat` | Not present on most PCs - Needs root data |
| [BIOS / SMBIOS tables](machine.md#bios--smbios-tables) | `bios-tables` | Linux data - Needs root data |
| [CMOS / RTC status](machine.md#cmos--rtc-status) | `cmos-status` | Linux data |
| [Dell BIOS](machine.md#dell-bios) | `dell-bios` | Not present on most PCs |
| [DIMMs](machine.md#dimms) | `dimms` | Linux data - Needs root data |
| [DIMM status/temperatures](machine.md#dimm-statustemperatures) | `dimm-status` | Linux data |
| [Firmware](machine.md#firmware) | `firmware` | Linux data |
| [Firmware interface table](machine.md#firmware-interface-table) | `f-w-table` | Not present on most PCs |
| [Hardware summary](machine.md#hardware-summary) | `hardware` | Linux data |
| [HECI (Intel ME) status](machine.md#heci-intel-me-status) | `heci-status` | Not present on most PCs |
| [Machine](machine.md#machine) | `machine` | Linux data |
| [Machine options](machine.md#machine-options) | `machine-opt` | Linux data |
| [Memory](machine.md#memory) | `memory` | Linux data |
| [Memory timings](machine.md#memory-timings) | `mem-tweak` | Linux data - Needs root data |
| [Overview](machine.md#overview) | `overview` | Linux data |
| [Physical memory map](machine.md#physical-memory-map) | `physical` | Linux data |
| [SPD (DIMMs)](machine.md#spd-dimms) | `spd` | Linux data |
| [SPD thermal](machine.md#spd-thermal) | `spd-temp` | Linux data |
| [System CMOS](machine.md#system-cmos) | `system-cmos` | Linux data - Needs root data |
| [System EBDA](machine.md#system-ebda) | `system-ebda` | Linux data - Needs root data |
| [BIOS IDs](machine.md#bios-ids) | `system-ids` | Linux data - Needs root data |
| [TPM device](machine.md#tpm-device) | `tpm-device` | Linux data |
| [UEFI data](machine.md#uefi-data) | `uefi-data` | Linux data |
| [UEFI information](machine.md#uefi-information) | `uefi-info` | Linux data |

### Sensors / Cooling / Power

| Item | id | Badge |
|---|---|---|
| [AIO Link export](sensors.md#aio-link-export) | `aio-export` | Not present on most PCs |
| [ASUS ATK/WMI info](sensors.md#asus-atkwmi-info) | `asus-info` | Linux data - Needs root data |
| [ASUS WMI ACPI methods](sensors.md#asus-wmi-acpi-methods) | `asus-wmi` | Linux data - Needs root data |
| [ASUS ATK0110 debug](sensors.md#asus-atk0110-debug) | `atk-debug` | Not present on most PCs |
| [Battery](sensors.md#battery) | `battery` | Not present on most PCs |
| [Cooling status (fans)](sensors.md#cooling-status-fans) | `cooling` | Linux data |
| [Sensor formula setup](sensors.md#sensor-formula-setup) | `formula-setup` | Linux data |
| [Hardware status](sensors.md#hardware-status) | `hw-status` | Linux data |
| [IPMI dump](sensors.md#ipmi-dump) | `ipmi-dump` | Not present on most PCs |
| [IPMI status](sensors.md#ipmi-status) | `ipmi-info` | Not present on most PCs |
| [IPMI time](sensors.md#ipmi-time) | `ipmi-time` | Not present on most PCs |
| [IPMI wait](sensors.md#ipmi-wait) | `ipmi-wait` | Not present on most PCs |
| [kWh cost](sensors.md#kwh-cost) | `kwh-cost` | Linux data |
| [LCD panel setup](sensors.md#lcd-panel-setup) | `lcd-panel` | Not present on most PCs |
| [Super I/O LDNs](sensors.md#super-io-ldns) | `ldn-info` | Linux data - Needs root data |
| [AIO Link devices](sensors.md#aio-link-devices) | `link-devices` | Not present on most PCs |
| [AIO Link dump](sensors.md#aio-link-dump) | `link-dump` | Not present on most PCs |
| [AIO Link fans](sensors.md#aio-link-fans) | `link-fans` | Not present on most PCs |
| [AIO Link LEDs](sensors.md#aio-link-leds) | `link-leds` | Not present on most PCs |
| [AIO Link limits](sensors.md#aio-link-limits) | `link-limits` | Not present on most PCs |
| [AIO Link power](sensors.md#aio-link-power) | `link-power` | Not present on most PCs |
| [AIO Link setup](sensors.md#aio-link-setup) | `link-setup` | Not present on most PCs |
| [AIO Link status](sensors.md#aio-link-status) | `link-status` | Not present on most PCs |
| [AIO Link test](sensors.md#aio-link-test) | `link-test` | Not present on most PCs |
| [AIO Link update](sensors.md#aio-link-update) | `link-update` | Not present on most PCs |
| [Power status](sensors.md#power-status) | `power` | Linux data |
| [PWM export](sensors.md#pwm-export) | `pwm-export` | Linux data |
| [QST status](sensors.md#qst-status) | `qst-status` | Not present on most PCs |
| [Sensor export](sensors.md#sensor-export) | `sen-export` | Linux data |
| [Sensor status](sensors.md#sensor-status) | `sensors` | Linux data |
| [SIO debug](sensors.md#sio-debug) | `sio-dbg` | Linux data - Needs root data |
| [Synthetic sensors setup](sensors.md#synthetic-sensors-setup) | `synthetic` | Linux data |
| [Thermal zones](sensors.md#thermal-zones) | `thermal-zones` | Not present on most PCs |
| [UPS export](sensors.md#ups-export) | `ups-export` | Not present on most PCs |

### Volumes / Drives

| Item | id | Badge |
|---|---|---|
| [Disk drives](storage.md#disk-drives) | `drives` | Linux data |
| [Drive tree](storage.md#drive-tree) | `drive-tree` | Linux data |
| [Disk API routines](storage.md#disk-api-routines) | `dsk-api` | Windows-only (Linux equivalent shown) |
| [FAT statistics](storage.md#fat-statistics) | `fat` | Linux data |
| [Drive geometry](storage.md#drive-geometry) | `geometry` | Linux data |
| [Hidden disks](storage.md#hidden-disks) | `hidden` | Linux data |
| [JMicron JMB RAID](storage.md#jmicron-jmb-raid) | `jmb-raid` | Not present on most PCs |
| [JMicron NVMe RAID](storage.md#jmicron-nvme-raid) | `jms-raid` | Not present on most PCs |
| [Disk mapping](storage.md#disk-mapping) | `mapping` | Linux data |
| [LSI MegaRAID](storage.md#lsi-megaraid) | `mega-raid` | Not present on most PCs |
| [NTFS statistics](storage.md#ntfs-statistics) | `ntfs` | Linux data |
| [Partitions](storage.md#partitions) | `partitions` | Linux data |
| [Disk performance](storage.md#disk-performance) | `performance` | Linux data |
| [Mount points](storage.md#mount-points) | `points` | Linux data |
| [CSMI RAID](storage.md#csmi-raid) | `raid` | Not present on most PCs |
| [RocketRAID](storage.md#rocketraid) | `rocket-raid` | Not present on most PCs |
| [SCSI devices](storage.md#scsi-devices) | `scsi` | Linux data |
| [Samsung NVMe (secnvme)](storage.md#samsung-nvme-secnvme) | `secnvme` | Linux data - Needs root data |
| [Disk sector sizes](storage.md#disk-sector-sizes) | `sectors` | Linux data |
| [SMART drives](storage.md#smart-drives) | `smart` | Linux data - Needs root data |
| [SMART all drives](storage.md#smart-all-drives) | `smart-all` | Linux data - Needs root data |
| [SMART drive monitor](storage.md#smart-drive-monitor) | `smart-sdm` | Linux data - Needs root data |
| [Storage spaces](storage.md#storage-spaces) | `spaces` | Linux data - Needs root data |
| [NVMe (stornvme)](storage.md#nvme-stornvme) | `stornvme` | Linux data - Needs root data |
| [USB disk drives](storage.md#usb-disk-drives) | `usb-disks` | Linux data |
| [Volume list](storage.md#volume-list) | `vol-list` | Linux data |
| [Volumes](storage.md#volumes) | `volumes` | Linux data |
| [Intel VROC](storage.md#intel-vroc) | `vroc` | Not present on most PCs |

### Network

| Item | id | Badge |
|---|---|---|
| [Network adapters](network.md#network-adapters) | `adapters` | Linux data |
| [ARP table](network.md#arp-table) | `arp-table` | Linux data |
| [Bluetooth radios](network.md#bluetooth-radios) | `bluetooth` | Linux data |
| [Bluetooth IDs](network.md#bluetooth-ids) | `bluetooth-ids` | Linux data |
| [Bluetooth device tree](network.md#bluetooth-device-tree) | `blue-tree` | Linux data |
| [Network browse masters](network.md#network-browse-masters) | `browsers` | Linux data |
| [TCP/IP counters](network.md#tcpip-counters) | `counters` | Linux data |
| [DNS cache](network.md#dns-cache) | `dns-cache` | Linux data, Needs root data |
| [DNS names](network.md#dns-names) | `dns-names` | Linux data |
| [DNS ports](network.md#dns-ports) | `dns-ports` | Linux data |
| [DNS usage](network.md#dns-usage) | `dns-usage` | Linux data |
| [Domains / workgroups](network.md#domains--workgroups) | `domains` | Linux data |
| [Firewall](network.md#firewall) | `firewall` | Linux data, Needs root data |
| [Network interface statistics](network.md#network-interface-statistics) | `interface` | Linux data |
| [IP address](network.md#ip-address) | `ip-address` | Linux data |
| [LAN Manager share setup](network.md#lan-manager-share-setup) | `lm-setup` | Linux data |
| [MAC addresses](network.md#mac-addresses) | `mac-address` | Linux data |
| [Modem API](network.md#modem-api) | `modem-api` | Not present on most PCs |
| [Modems](network.md#modems) | `modems` | Not present on most PCs |
| [My browser / IP location](network.md#my-browser--ip-location) | `my-browser` | Linux data |
| [My IP address](network.md#my-ip-address) | `my-ip-address` | Linux data |
| [Network cards](network.md#network-cards) | `net-cards` | Linux data |
| [Network provider names](network.md#network-provider-names) | `net-names` | Windows-only (Linux equivalent shown) |
| [Network](network.md#network) | `network` | Linux data |
| [Network profiles](network.md#network-profiles) | `profiles` | Linux data |
| [Network protocols](network.md#network-protocols) | `protocols` | Linux data |
| [Proxy config](network.md#proxy-config) | `proxy` | Linux data |
| [Routing table](network.md#routing-table) | `routing` | Linux data |
| [Servers](network.md#servers) | `servers` | Linux data |
| [TCP V4 ports](network.md#tcp-v4-ports) | `tcp-ip4-ports` | Linux data |
| [TCP V6 ports](network.md#tcp-v6-ports) | `tcp-ip6-ports` | Linux data |
| [Network transports](network.md#network-transports) | `transports` | Linux data |
| [UDP V4 ports](network.md#udp-v4-ports) | `udp-ip4-ports` | Linux data |
| [UDP V6 ports](network.md#udp-v6-ports) | `udp-ip6-ports` | Linux data |
| [Wi-Fi SSIDs](network.md#wi-fi-ssids) | `wi-fi` | Linux data |
| [WLAN profiles](network.md#wlan-profiles) | `wlan` | Linux data, Needs root data |

### Linux (SIV's Windows items)

| Item | id | Badge |
|---|---|---|
| [ASUS control service](linux-os.md#asus-control-service) | `acs-srv` | Not present on most PCs |
| [Boot mode options](linux-os.md#boot-mode-options) | `boot-mode` | Linux data |
| [Corsair Link 4 service](linux-os.md#corsair-link-4-service) | `cl4-srv` | Not present on most PCs |
| [Shell CLSIDs](linux-os.md#shell-clsids) | `clsid` | Windows-only (Linux equivalent shown) |
| [Code pages](linux-os.md#code-pages) | `codepage` | Linux data |
| [TAPI country list](linux-os.md#tapi-country-list) | `country` | Linux data |
| [Corsair service](linux-os.md#corsair-service) | `cue-srv` | Not present on most PCs |
| [Base DLLs](linux-os.md#base-dlls) | `dll` | Linux data |
| [DLLs by address](linux-os.md#dlls-by-address) | `dll-addr` | Linux data |
| [Known DLLs](linux-os.md#known-dlls) | `dll-known` | Linux data |
| [DLLs by name](linux-os.md#dlls-by-name) | `dll-name` | Linux data |
| [Drivers](linux-os.md#drivers) | `drivers` | Linux data |
| [Driver versions](linux-os.md#driver-versions) | `driver-vsn` | Linux data |
| [E-mail accounts](linux-os.md#e-mail-accounts) | `e-mail` | Windows-only (Linux equivalent shown) |
| [Environment](linux-os.md#environment) | `environment` | Linux data |
| [ESU status](linux-os.md#esu-status) | `esu-sts` | Windows-only (Linux equivalent shown) |
| [System file cache](linux-os.md#system-file-cache) | `file-cache` | Linux data |
| [Open server files](linux-os.md#open-server-files) | `files` | Linux data - Needs root data |
| [Feature management overrides](linux-os.md#feature-management-overrides) | `fmo-sts` | Windows-only (Linux equivalent shown) |
| [Folder paths](linux-os.md#folder-paths) | `folders` | Linux data |
| [Font families](linux-os.md#font-families) | `fonts` | Linux data |
| [Groups](linux-os.md#groups) | `groups` | Linux data |
| [Handles](linux-os.md#handles) | `handle` | Linux data |
| [Process heap](linux-os.md#process-heap) | `heap` | Linux data |
| [Registry hives](linux-os.md#registry-hives) | `hives` | Windows-only (Linux equivalent shown) |
| [Code integrity](linux-os.md#code-integrity) | `integrity` | Linux data |
| [Key names](linux-os.md#key-names) | `key-names` | Linux data |
| [Locales](linux-os.md#locales) | `locales` | Linux data |
| [Memory heap](linux-os.md#memory-heap) | `mem-heap` | Linux data |
| [Memory mapping](linux-os.md#memory-mapping) | `mem-map` | Linux data |
| [System metrics](linux-os.md#system-metrics) | `metrics` | Windows-only (Linux equivalent shown) |
| [MLang conversions](linux-os.md#mlang-conversions) | `mlang` | Windows-only (Linux equivalent shown) |
| [DOS device names](linux-os.md#dos-device-names) | `ms-dos` | Linux data |
| [N2K service](linux-os.md#n2k-service) | `n2k-srv` | Not present on most PCs |
| [NLS code pages](linux-os.md#nls-code-pages) | `nls-info` | Linux data |
| [Paging files](linux-os.md#paging-files) | `paging` | Linux data |
| [Credentials](linux-os.md#credentials) | `passwords` | Windows-only (Linux equivalent shown) |
| [Pending file renames](linux-os.md#pending-file-renames) | `pending` | Windows-only (Linux equivalent shown) |
| [ProLiant monitor service](linux-os.md#proliant-monitor-service) | `plm-srv` | Not present on most PCs |
| [Windows policies](linux-os.md#windows-policies) | `policies` | Windows-only (Linux equivalent shown) |
| [Memory pools](linux-os.md#memory-pools) | `pools` | Linux data |
| [Power policy](linux-os.md#power-policy) | `power-policy` | Linux data |
| [Power scheme](linux-os.md#power-scheme) | `power-scheme` | Linux data |
| [Privileges](linux-os.md#privileges) | `priv` | Linux data |
| [Processes](linux-os.md#processes) | `processes` | Linux data |
| [Product IDs](linux-os.md#product-ids) | `product-ids` | Windows-only (Linux equivalent shown) |
| [Product keys](linux-os.md#product-keys) | `products` | Windows-only (Linux equivalent shown) |
| [Font ranges](linux-os.md#font-ranges) | `ranges` | Linux data |
| [RDP session info](linux-os.md#rdp-session-info) | `rdp-info` | Linux data |
| [Remote registry paths](linux-os.md#remote-registry-paths) | `reg-path` | Windows-only (Linux equivalent shown) |
| [Remote registry service](linux-os.md#remote-registry-service) | `reg-srv` | Windows-only (Linux equivalent shown) |
| [Text converter render format](linux-os.md#text-converter-render-format) | `render-format` | Windows-only (Linux equivalent shown) |
| [Intel RST service](linux-os.md#intel-rst-service) | `rst-srv` | Not present on most PCs |
| [Scheduling](linux-os.md#scheduling) | `scheduling` | Linux data |
| [Driver services](linux-os.md#driver-services) | `services-drv` | Linux data |
| [Win32 services](linux-os.md#win32-services) | `services-w32` | Linux data |
| [Sessions](linux-os.md#sessions) | `sessions` | Linux data |
| [Setup directories](linux-os.md#setup-directories) | `setup` | Linux data |
| [Setup API logging](linux-os.md#setup-api-logging) | `setup-api` | Windows-only (Linux equivalent shown) |
| [Driver signing](linux-os.md#driver-signing) | `signing` | Linux data |
| [Installed software](linux-os.md#installed-software) | `software` | Linux data |
| [Startup programs](linux-os.md#startup-programs) | `startup` | Linux data |
| [System](linux-os.md#system) | `system` | Linux data |
| [System timers](linux-os.md#system-timers) | `sys-tim` | Linux data |
| [System windows](linux-os.md#system-windows) | `sys-win` | Windows-only (Linux equivalent shown) |
| [Thread windows](linux-os.md#thread-windows) | `tid-win` | Windows-only (Linux equivalent shown) |
| [Time](linux-os.md#time) | `time` | Linux data |
| [Times](linux-os.md#times) | `times` | Linux data |
| [Users](linux-os.md#users) | `users` | Linux data |
| [Driver verifier](linux-os.md#driver-verifier) | `verifier` | Windows-only (Linux equivalent shown) |
| [Virtual memory](linux-os.md#virtual-memory) | `virtual` | Linux data |
| [Visual Studio paths](linux-os.md#visual-studio-paths) | `vs-paths` | Windows-only (Linux equivalent shown) |
| [Dr Watson / crash debugger](linux-os.md#dr-watson--crash-debugger) | `watson` | Linux data |
| [Error reporting](linux-os.md#error-reporting) | `wer` | Linux data |
| [Windows CE devices](linux-os.md#windows-ce-devices) | `win-ce` | Windows-only (Linux equivalent shown) |
| [General Info](linux-os.md#general-info) | `windows` | Linux data |
| [WinSAT](linux-os.md#winsat) | `winsat` | Windows-only (Linux equivalent shown) |
| [Time zones](linux-os.md#time-zones) | `zones` | Linux data |

### Graphics / Monitors / Audio

| Item | id | Badge |
|---|---|---|
| [Audio devices](graphics.md#audio-devices) | `audio` | Linux data |
| [Direct3D info](graphics.md#direct3d-info) | `d3d-info` | Linux data |
| [GPU D3D engine nodes](graphics.md#gpu-d3d-engine-nodes) | `d3d-node` | Linux data |
| [DirectX information](graphics.md#directx-information) | `directx` | Linux data |
| [DirectX sound devices](graphics.md#directx-sound-devices) | `dxd-aud` | Linux data |
| [DirectX system devices](graphics.md#directx-system-devices) | `dxd-dev` | Linux data |
| [DirectX logical disks](graphics.md#directx-logical-disks) | `dxd-dsk` | Linux data |
| [DirectX files](graphics.md#directx-files) | `dxd-fil` | Linux data |
| [DirectShow filters](graphics.md#directshow-filters) | `dxd-ftr` | Linux data |
| [DirectInput gameports](graphics.md#directinput-gameports) | `dxd-gam` | Not present on most PCs |
| [DirectX GPU displays](graphics.md#directx-gpu-displays) | `dxd-gpu` | Linux data |
| [DirectX system information](graphics.md#directx-system-information) | `dxd-inf` | Linux data |
| [DirectInput devices](graphics.md#directinput-devices) | `dxd-inp` | Linux data |
| [DirectMusic ports](graphics.md#directmusic-ports) | `dxd-mic` | Linux data |
| [DirectPlay adapters](graphics.md#directplay-adapters) | `dxd-ply` | Windows-only (Linux equivalent shown) |
| [DirectInput PS/2 devices](graphics.md#directinput-ps2-devices) | `dxd-ps2` | Not present on most PCs |
| [DirectX summary](graphics.md#directx-summary) | `dxd-sum` | Linux data |
| [DirectInput USB root](graphics.md#directinput-usb-root) | `dxd-usb` | Linux data |
| [GOverlay status](graphics.md#goverlay-status) | `goverlay` | Linux data |
| [GPU all details](graphics.md#gpu-all-details) | `gpu-all` | Linux data |
| [GPU API routines](graphics.md#gpu-api-routines) | `gpu-api` | Linux data |
| [GPU applications](graphics.md#gpu-applications) | `gpu-apps` | Linux data |
| [GPU VBIOS status](graphics.md#gpu-vbios-status) | `gpu-bios` | Linux data |
| [GPU clock speeds](graphics.md#gpu-clock-speeds) | `gpu-clocks` | Linux data |
| [CUDA information](graphics.md#cuda-information) | `gpu-cuda` | Not present on most PCs |
| [GPU D3D status](graphics.md#gpu-d3d-status) | `gpu-d3d` | Linux data |
| [GPU register dump](graphics.md#gpu-register-dump) | `gpu-dump` | Linux data - Needs root data |
| [GPU export](graphics.md#gpu-export) | `gpu-export` | Linux data |
| [GPU fans](graphics.md#gpu-fans) | `gpu-fans` | Linux data |
| [GPU generations](graphics.md#gpu-generations) | `gpu-gens` | Linux data |
| [GPU G-Sync / VRR](graphics.md#gpu-g-sync--vrr) | `gpu-g-sync` | Linux data |
| [GPU I2C buses](graphics.md#gpu-i2c-buses) | `gpu-i2c` | Linux data |
| [GPU information](graphics.md#gpu-information) | `gpu-info` | Linux data |
| [GPU LEDs](graphics.md#gpu-leds) | `gpu-leds` | Not present on most PCs |
| [GPU mapping](graphics.md#gpu-mapping) | `gpu-map` | Linux data |
| [GPU power status](graphics.md#gpu-power-status) | `gpu-power` | Linux data |
| [GPU registry](graphics.md#gpu-registry) | `gpu-reg` | Linux data |
| [GPU SCF](graphics.md#gpu-scf) | `gpu-scf` | Linux data |
| [GPU SLI topology](graphics.md#gpu-sli-topology) | `gpu-sli` | Not present on most PCs |
| [GPU temperatures](graphics.md#gpu-temperatures) | `gpu-temp` | Linux data |
| [GPU tree](graphics.md#gpu-tree) | `gpu-tree` | Linux data |
| [Graphics](graphics.md#graphics) | `graphics` | Linux data |
| [Joysticks](graphics.md#joysticks) | `joysticks` | Linux data |
| [Monitor ID database](graphics.md#monitor-id-database) | `mon-info` | Linux data |
| [Monitors](graphics.md#monitors) | `monitors` | Linux data |
| [Monitor tree](graphics.md#monitor-tree) | `monitor-tree` | Linux data |
| [OpenCL](graphics.md#opencl) | `opencl` | Linux data |
| [RSR info](graphics.md#rsr-info) | `rsr-info` | Windows-only (Linux equivalent shown) |
| [RTSS status](graphics.md#rtss-status) | `rtss-sts` | Not present on most PCs |
| [Sound devices](graphics.md#sound-devices) | `sound` | Linux data |
| [UAA (HD Audio) devices](graphics.md#uaa-hd-audio-devices) | `uaa-dev` | Linux data |
| [Wave devices](graphics.md#wave-devices) | `wave` | Linux data |

### Buses / Devices (part 1)

| Item | id | Badge |
|---|---|---|
| [COM port resource arbiter](buses-1.md#com-port-resource-arbiter) | `arbiter` | Windows-only (Linux equivalent shown) |
| [Chipsets](buses-1.md#chipsets) | `chipsets` | Linux data |
| [System device classes](buses-1.md#system-device-classes) | `class` | Linux data |
| [Remote system classes](buses-1.md#remote-system-classes) | `class-remote` | Windows-only (Linux equivalent shown) |
| [Co-device installers](buses-1.md#co-device-installers) | `codevice` | Windows-only (Linux equivalent shown) |
| [Compatible IDs](buses-1.md#compatible-ids) | `compatible` | Linux data |
| [Device IDs](buses-1.md#device-ids) | `device-ids` | Linux data |
| [All devices](buses-1.md#all-devices) | `devices` | Linux data |
| [Device owners](buses-1.md#device-owners) | `dev-owner` | Linux data |
| [Device property keys](buses-1.md#device-property-keys) | `devpropkey` | Linux data |
| [Embedded controller I/O](buses-1.md#embedded-controller-io) | `embedded` | Linux data - Needs root data |
| [System enum (device tree)](buses-1.md#system-enum-device-tree) | `enum` | Linux data |
| [Remote system enum](buses-1.md#remote-system-enum) | `enum-remote` | Windows-only (Linux equivalent shown) |
| [FireWire](buses-1.md#firewire) | `firewire` | Not present on most PCs |
| [Ghost (non-present) devices](buses-1.md#ghost-non-present-devices) | `ghost-devices` | Windows-only (Linux equivalent shown) |
| [HID all devices](buses-1.md#hid-all-devices) | `hid-all` | Linux data |
| [HID bus](buses-1.md#hid-bus) | `hid-bus` | Linux data |
| [HID tree](buses-1.md#hid-tree) | `hid-tree` | Linux data |
| [Device interfaces](buses-1.md#device-interfaces) | `interfaces` | Linux data |
| [Device interfaces (extended)](buses-1.md#device-interfaces-extended) | `interfacex` | Linux data |
| [Interface IDs](buses-1.md#interface-ids) | `int-ids` | Linux data |
| [Interface names](buses-1.md#interface-names) | `int-names` | Linux data |
| [Interface owner](buses-1.md#interface-owner) | `int-owner` | Linux data |
| [ASUS I/O](buses-1.md#asus-io) | `io-asus` | Linux data - Needs root data |
| [I/O port dump](buses-1.md#io-port-dump) | `io-dump` | Linux data - Needs root data |
| [I/O GPIO](buses-1.md#io-gpio) | `io-gpio` | Linux data - Needs root data |
| [I/O PM2 MMIO](buses-1.md#io-pm2-mmio) | `io-mmio` | Linux data - Needs root data |
| [nForce I/O](buses-1.md#nforce-io) | `io-nforce` | Not present on most PCs |
| [I/O ports](buses-1.md#io-ports) | `io-ports` | Linux data - Needs root data |
| [IrDA ports](buses-1.md#irda-ports) | `irports` | Not present on most PCs |
| [ISA bus](buses-1.md#isa-bus) | `isa-bus` | Linux data |
| [MCA bus](buses-1.md#mca-bus) | `mca-bus` | Not present on most PCs |
| [Chipset MCH](buses-1.md#chipset-mch) | `mch` | Linux data - Needs root data |
| [Chipset MRH](buses-1.md#chipset-mrh) | `mrh` | Not present on most PCs |
| [NPU names](buses-1.md#npu-names) | `npu-names` | Not present on most PCs |
| [PCI all](buses-1.md#pci-all) | `pci-all` | Linux data - Needs root data |
| [PCI BARs](buses-1.md#pci-bars) | `pci-bars` | Linux data |
| [PCI bus](buses-1.md#pci-bus) | `pci-bus` | Linux data |
| [PCI devices](buses-1.md#pci-devices) | `pci-dev` | Linux data |
| [PCI flags](buses-1.md#pci-flags) | `pci-flag` | Windows-only (Linux equivalent shown) |
| [PCI HAL](buses-1.md#pci-hal) | `pci-hal` | Linux data - Needs root data |
| [PCI ID database](buses-1.md#pci-id-database) | `pci-info` | Linux data |
| [PCI MMIO](buses-1.md#pci-mmio) | `pci-mmio` | Linux data - Needs root data |
| [PCMCIA all](buses-1.md#pcmcia-all) | `pcm-all` | Not present on most PCs |
| [PCMCIA bus](buses-1.md#pcmcia-bus) | `pcm-bus` | Not present on most PCs |
| [PCMCIA ID database](buses-1.md#pcmcia-id-database) | `pcm-info` | Not present on most PCs |
| [I/O PM1 data](buses-1.md#io-pm1-data) | `pm1-area` | Linux data - Needs root data |
| [PMBus](buses-1.md#pmbus) | `pmb-bus` | Not present on most PCs |
| [PnP manager](buses-1.md#pnp-manager) | `pnp` | Linux data |
| [PnP devices](buses-1.md#pnp-devices) | `pnp-dev` | Linux data |
| [PnP ID database](buses-1.md#pnp-id-database) | `pnp-info` | Linux data |
| [PnP manager details](buses-1.md#pnp-manager-details) | `pnp-man` | Linux data |
| [COM ports](buses-1.md#com-ports) | `ports` | Linux data |
| [Device properties](buses-1.md#device-properties) | `properties` | Linux data |
| [Device property](buses-1.md#device-property) | `property` | Linux data |
| [PYXIS 21174](buses-1.md#pyxis-21174) | `pyxis` | Not present on most PCs |
| [Smart cards](buses-1.md#smart-cards) | `scard` | Not present on most PCs |
| [Serial COM ports](buses-1.md#serial-com-ports) | `serial` | Linux data |
| [SI USB](buses-1.md#si-usb) | `si-usb` | Linux data |
| [SMBus](buses-1.md#smbus) | `smb` | Linux data |
| [SMBus devices](buses-1.md#smbus-devices) | `smb-bus` | Linux data |
| [SMBus status](buses-1.md#smbus-status) | `smb-sts` | Linux data - Needs root data |
| [SMBus setup](buses-1.md#smbus-setup) | `smbus-setup` | Linux data |
| [System SMBus access](buses-1.md#system-smbus-access) | `smbus-system` | Linux data |

### Buses / Devices (part 2)

| Item | id | Badge |
|---|---|---|
| [System HAL resources](buses-2.md#system-hal-resources) | `sys-hal` | Linux data - Needs root data |
| [System resources](buses-2.md#system-resources) | `sys-res` | Linux data - Needs root data |
| [Thunderbolt](buses-2.md#thunderbolt) | `thunder` | Not present on most PCs |
| [Bus address translation](buses-2.md#bus-address-translation) | `translation` | Linux data |
| [UID database](buses-2.md#uid-database) | `uid-info` | Linux data |
| [USB all](buses-2.md#usb-all) | `usb-all` | Linux data |
| [USB bus](buses-2.md#usb-bus) | `usb-bus` | Linux data |
| [USB devices](buses-2.md#usb-devices) | `usb-dev` | Linux data |
| [USB flags](buses-2.md#usb-flags) | `usb-flag` | Linux data |
| [USB flags detail](buses-2.md#usb-flags-detail) | `usb-flags` | Linux data |
| [USB ID database](buses-2.md#usb-id-database) | `usb-info` | Linux data |
| [USB names](buses-2.md#usb-names) | `usb-names` | Linux data |
| [USB owner](buses-2.md#usb-owner) | `usb-owner` | Linux data |
| [USB tree](buses-2.md#usb-tree) | `usb-tree` | Linux data |
| [WPD tree (portable devices)](buses-2.md#wpd-tree-portable-devices) | `wpd-tree` | Linux data |
| [XMC cards](buses-2.md#xmc-cards) | `xmc` | Not present on most PCs |
| [XMC card 0](buses-2.md#xmc-card-0) | `xmc-card-0` | Not present on most PCs |
| [XMC card 1](buses-2.md#xmc-card-1) | `xmc-card-1` | Not present on most PCs |
| [XMC card 2](buses-2.md#xmc-card-2) | `xmc-card-2` | Not present on most PCs |
| [XMC card 3](buses-2.md#xmc-card-3) | `xmc-card-3` | Not present on most PCs |
| [XMC control](buses-2.md#xmc-control) | `xmc-control` | Not present on most PCs |
| [XMC flash 0](buses-2.md#xmc-flash-0) | `xmc-flash-0` | Not present on most PCs |
| [XMC flash 1](buses-2.md#xmc-flash-1) | `xmc-flash-1` | Not present on most PCs |
| [XMC flash 2](buses-2.md#xmc-flash-2) | `xmc-flash-2` | Not present on most PCs |
| [XMC flash 3](buses-2.md#xmc-flash-3) | `xmc-flash-3` | Not present on most PCs |
| [XMC FPGA 0](buses-2.md#xmc-fpga-0) | `xmc-fpga-0` | Not present on most PCs |
| [XMC FPGA 1](buses-2.md#xmc-fpga-1) | `xmc-fpga-1` | Not present on most PCs |
| [XMC FPGA 2](buses-2.md#xmc-fpga-2) | `xmc-fpga-2` | Not present on most PCs |
| [XMC FPGA 3](buses-2.md#xmc-fpga-3) | `xmc-fpga-3` | Not present on most PCs |
| [XMC names](buses-2.md#xmc-names) | `xmc-names` | Not present on most PCs |
| [XMC PCIe 0](buses-2.md#xmc-pcie-0) | `xmc-pcie-0` | Not present on most PCs |
| [XMC PCIe 1](buses-2.md#xmc-pcie-1) | `xmc-pcie-1` | Not present on most PCs |
| [XMC PCIe 2](buses-2.md#xmc-pcie-2) | `xmc-pcie-2` | Not present on most PCs |
| [XMC PCIe 3](buses-2.md#xmc-pcie-3) | `xmc-pcie-3` | Not present on most PCs |
| [XMC setup 0](buses-2.md#xmc-setup-0) | `xmc-setup-0` | Not present on most PCs |
| [XMC setup 1](buses-2.md#xmc-setup-1) | `xmc-setup-1` | Not present on most PCs |
| [XMC setup 2](buses-2.md#xmc-setup-2) | `xmc-setup-2` | Not present on most PCs |
| [XMC setup 3](buses-2.md#xmc-setup-3) | `xmc-setup-3` | Not present on most PCs |
| [XMC state 0](buses-2.md#xmc-state-0) | `xmc-state-0` | Not present on most PCs |
| [XMC state 1](buses-2.md#xmc-state-1) | `xmc-state-1` | Not present on most PCs |
| [XMC state 2](buses-2.md#xmc-state-2) | `xmc-state-2` | Not present on most PCs |
| [XMC state 3](buses-2.md#xmc-state-3) | `xmc-state-3` | Not present on most PCs |
| [XMC video 0](buses-2.md#xmc-video-0) | `xmc-video-0` | Not present on most PCs |
| [XMC video 1](buses-2.md#xmc-video-1) | `xmc-video-1` | Not present on most PCs |
| [XMC video 2](buses-2.md#xmc-video-2) | `xmc-video-2` | Not present on most PCs |
| [XMC video 3](buses-2.md#xmc-video-3) | `xmc-video-3` | Not present on most PCs |
| [ZX1 MIO-0](buses-2.md#zx1-mio-0) | `zx1-mio-0` | Not present on most PCs |
| [ZX1 MIO-1](buses-2.md#zx1-mio-1) | `zx1-mio-1` | Not present on most PCs |
| [ZX1 MIO-2](buses-2.md#zx1-mio-2) | `zx1-mio-2` | Not present on most PCs |
| [ZX1 MIO-3](buses-2.md#zx1-mio-3) | `zx1-mio-3` | Not present on most PCs |
| [ZX1 MIO-4](buses-2.md#zx1-mio-4) | `zx1-mio-4` | Not present on most PCs |
| [ZX1 MIO-5](buses-2.md#zx1-mio-5) | `zx1-mio-5` | Not present on most PCs |
| [ZX1 MIO-6](buses-2.md#zx1-mio-6) | `zx1-mio-6` | Not present on most PCs |
| [ZX1 MIO-7](buses-2.md#zx1-mio-7) | `zx1-mio-7` | Not present on most PCs |
| [ZX1 MIO-8](buses-2.md#zx1-mio-8) | `zx1-mio-8` | Not present on most PCs |
| [ZX1 MIO-9](buses-2.md#zx1-mio-9) | `zx1-mio-9` | Not present on most PCs |
| [ZX1 MIO-A](buses-2.md#zx1-mio-a) | `zx1-mio-a` | Not present on most PCs |
| [ZX1 MIO-B](buses-2.md#zx1-mio-b) | `zx1-mio-b` | Not present on most PCs |
| [ZX1 MIO-C](buses-2.md#zx1-mio-c) | `zx1-mio-c` | Not present on most PCs |
| [ZX1 MIO-D](buses-2.md#zx1-mio-d) | `zx1-mio-d` | Not present on most PCs |
| [ZX1 MIO-E](buses-2.md#zx1-mio-e) | `zx1-mio-e` | Not present on most PCs |
| [ZX1 MIO-F](buses-2.md#zx1-mio-f) | `zx1-mio-f` | Not present on most PCs |
| [ZX1 summary](buses-2.md#zx1-summary) | `zx1-mio-s` | Not present on most PCs |

### Tools

| Item | id | Badge |
|---|---|---|
| [CPU Benchmarks](tools.md#cpu-benchmarks) | `benchmark` | Linux data |
| [Benchmark: all CPUs concurrent](tools.md#benchmark-all-cpus-concurrent) | `benchmark-ac` | Linux data |
| [Benchmark: all CPUs sequential](tools.md#benchmark-all-cpus-sequential) | `benchmark-as` | Linux data |
| [Benchmark: D+D then W+W](tools.md#benchmark-dd-then-ww) | `benchmark-dd` | Linux data |
| [Benchmark: D+W then W+D](tools.md#benchmark-dw-then-wd) | `benchmark-dw` | Linux data |
| [Benchmark: E-cores concurrent](tools.md#benchmark-e-cores-concurrent) | `benchmark-ec` | Not present on most PCs |
| [Benchmark: E-cores one at a time](tools.md#benchmark-e-cores-one-at-a-time) | `benchmark-ep` | Not present on most PCs |
| [Benchmark: logical threads concurrent](tools.md#benchmark-logical-threads-concurrent) | `benchmark-lc` | Linux data |
| [Benchmark: logical threads one at a time](tools.md#benchmark-logical-threads-one-at-a-time) | `benchmark-lp` | Linux data |
| [Benchmark: physical cores concurrent](tools.md#benchmark-physical-cores-concurrent) | `benchmark-pc` | Linux data |
| [Benchmark: physical cores one at a time](tools.md#benchmark-physical-cores-one-at-a-time) | `benchmark-pp` | Linux data |
| [BOINC status](tools.md#boinc-status) | `boinc` | Not present on most PCs |
| [BOINC RPC info](tools.md#boinc-rpc-info) | `boinc-rpc` | Not present on most PCs |
| [ClearCase](tools.md#clearcase) | `clear-case` | Not present on most PCs |
| [ClearQuest](tools.md#clearquest) | `clear-quest` | Not present on most PCs |
| [ClearQuest e-mail](tools.md#clearquest-e-mail) | `cq-mail` | Not present on most PCs |
| [CPU IPI Delay](tools.md#cpu-ipi-delay) | `ipi-delay` | Linux data |
| [IPI Dump](tools.md#ipi-dump) | `ipi-dmp` | Linux data |
| [IPI Plots](tools.md#ipi-plots) | `ipi-plots` | Linux data |
| [Memory Latency](tools.md#memory-latency) | `latency` | Linux data |
| [Cache & Memory benchmark](tools.md#cache--memory-benchmark) | `cache-memory` | Linux data |
| [Storage benchmark](tools.md#storage-benchmark) | `storage-bench` | Linux data |
| [OS latency benchmark](tools.md#os-latency-benchmark) | `os-latency` | Linux data |
| [SRS Pi](tools.md#srs-pi) | `srs-pi` | Linux data |
| [Crypto benchmark](tools.md#crypto-benchmark) | `crypto-bench` | Linux data |
| [Prime sieve](tools.md#prime-sieve) | `prime-sieve` | Linux data |
| [Benchmark history](tools.md#benchmark-history) | `bench-history` | Linux data |
| [Lookup](tools.md#lookup) | `lookup` | Linux data |
| [Ping](tools.md#ping) | `ping` | Linux data |
| [Configuration Find](tools.md#configuration-find) | `reg-find` | Linux data |
| [Device ID Lookup](tools.md#device-id-lookup) | `id-lookup` | Linux data |
| [Find](tools.md#find) | `find` | Linux data |
| [ALL Devices](tools.md#all-devices) | `all-dev` | Linux data |
| [CPU Burnin](tools.md#cpu-burnin) | `burnin` | Linux data |
| [CPU Stress](tools.md#cpu-stress) | `cpu-stress` | Linux data |

### SRS (SIV items)

| Item | id | Badge |
|---|---|---|
| [About SRS](srs-items.md#about-srs) | `about` | Linux data |
| [BSB info (battery/HID sensor buffer)](srs-items.md#bsb-info-batteryhid-sensor-buffer) | `bsi` | Linux data |
| [CPU group buffer](srs-items.md#cpu-group-buffer) | `cgb` | Linux data |
| [CSV logging](srs-items.md#csv-logging) | `csv` | Linux data |
| [CSV titles](srs-items.md#csv-titles) | `csv-titles` | Linux data |
| [Debug API](srs-items.md#debug-api) | `debug-api` | Linux data |
| [Delta/cost settings](srs-items.md#deltacost-settings) | `delta` | Linux data |
| [DSB info (DIMM sensor buffer)](srs-items.md#dsb-info-dimm-sensor-buffer) | `dsb` | Linux data |
| [Emoji glyph test](srs-items.md#emoji-glyph-test) | `emoji` | Linux data |
| [Fake IMCs](srs-items.md#fake-imcs) | `fak-imcs` | Not present on most PCs |
| [Fake MCHBAR](srs-items.md#fake-mchbar) | `fak-mchbar` | Linux data - Needs root data |
| [Fake PM tables](srs-items.md#fake-pm-tables) | `fak-pmts` | Linux data |
| [Fake SPDs](srs-items.md#fake-spds) | `fak-spds` | Linux data |
| [Filter](srs-items.md#filter) | `filter` | Linux data |
| [Final processing](srs-items.md#final-processing) | `final` | Linux data |
| [GPU info buffer](srs-items.md#gpu-info-buffer) | `gpu` | Linux data |
| [Help](srs-items.md#help) | `help` | Linux data |
| [Highlight setup](srs-items.md#highlight-setup) | `highlight` | Linux data |
| [Hotkeys](srs-items.md#hotkeys) | `hot-keys` | Linux data |
| [SIV image (EXE)](srs-items.md#siv-image-exe) | `image-exe` | Linux data |
| [SIV image (SYS driver)](srs-items.md#siv-image-sys-driver) | `image-sys` | Windows-only (Linux equivalent shown) |
| [Image tree](srs-items.md#image-tree) | `image-tree` | Linux data |
| [Initial processing](srs-items.md#initial-processing) | `initial` | Linux data |
| [Install](srs-items.md#install) | `install` | Linux data |
| [SIV languages](srs-items.md#siv-languages) | `languages` | Linux data |
| [Lock setup](srs-items.md#lock-setup) | `lck-setup` | Linux data |
| [Maker icons](srs-items.md#maker-icons) | `makers` | Linux data |
| [SIV manifest](srs-items.md#siv-manifest) | `manifest` | Windows-only (Linux equivalent shown) |
| [Mapping cache](srs-items.md#mapping-cache) | `map-cache` | Windows-only (Linux equivalent shown) |
| [Map/unmap test](srs-items.md#mapunmap-test) | `map-unmap` | Windows-only (Linux equivalent shown) |
| [Menus](srs-items.md#menus) | `menus` | Linux data |
| [Mutex info](srs-items.md#mutex-info) | `mutex` | Linux data |
| [PMB info (power monitor buffer)](srs-items.md#pmb-info-power-monitor-buffer) | `pmb` | Linux data |
| [Colour RGB debug](srs-items.md#colour-rgb-debug) | `rgb-debug` | Linux data |
| [SDM info (storage device monitor)](srs-items.md#sdm-info-storage-device-monitor) | `sdm` | Linux data |
| [Sequence setup](srs-items.md#sequence-setup) | `sequence` | Linux data |
| [SGB end](srs-items.md#sgb-end) | `sgb-end` | Linux data |
| [SGB info (system/board buffer)](srs-items.md#sgb-info-systemboard-buffer) | `sgb-ini` | Linux data |
| [Active SIV instances](srs-items.md#active-siv-instances) | `siv-active` | Linux data |
| [SIV autorun](srs-items.md#siv-autorun) | `siv-autorun` | Linux data |
| [SIV DLLs](srs-items.md#siv-dlls) | `siv-dll` | Linux data |
| [SIV driver](srs-items.md#siv-driver) | `siv-drv` | Windows-only (Linux equivalent shown) |
| [SIVDEF.ini](srs-items.md#sivdefini) | `siv-ini` | Linux data |
| [SIV location / window poke](srs-items.md#siv-location--window-poke) | `siv-location` | Linux data |
| [SIV qualifiers](srs-items.md#siv-qualifiers) | `siv-qualifiers` | Linux data |
| [SIV qualifier list](srs-items.md#siv-qualifier-list) | `siv-quals` | Linux data |
| [SIV setup](srs-items.md#siv-setup) | `siv-setup` | Linux data |
| [SIV sizes](srs-items.md#siv-sizes) | `siv-sizes` | Linux data |
| [SIV driver service](srs-items.md#siv-driver-service) | `siv-srv` | Windows-only (Linux equivalent shown) |
| [SIV usage history](srs-items.md#siv-usage-history) | `siv-usage` | Linux data |
| [SIV use / update](srs-items.md#siv-use--update) | `siv-use` | Linux data |
| [SMB end](srs-items.md#smb-end) | `smb-end` | Linux data |
| [SMB info](srs-items.md#smb-info) | `smb-ini` | Linux data |
| [Lock states](srs-items.md#lock-states) | `states` | Linux data |
| [STC info (thread contexts)](srs-items.md#stc-info-thread-contexts) | `stc` | Linux data |
| [Tune system](srs-items.md#tune-system) | `tune` | Linux data |
| [Tuning](srs-items.md#tuning) | `tuning` | Linux data |
| [TZM info (thermal zone buffer)](srs-items.md#tzm-info-thermal-zone-buffer) | `tzm` | Linux data |
| [Words](srs-items.md#words) | `words` | Linux data |
| [XXB internal buffers](srs-items.md#xxb-internal-buffers) | `xxb` | Linux data |

