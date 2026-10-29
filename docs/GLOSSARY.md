# SRS Glossary

[Back to the User Guide](USER-GUIDE.md) - [Section Reference](SECTIONS/README.md)

Plain-English meanings of the technical words used in the SRS documents, A to Z. After each definition, "See" lists the Section Reference pages that use the word most.

SRS is a Linux system information viewer inspired by SIV64X (System Information Viewer) by Ray Hinchliffe, RH Software; it is an independent project, not affiliated with SIV or its author.

## 0-9

- **80 PLUS** - a certificate for power supply efficiency (Bronze, Gold, Platinum, Titanium ...); see PSU efficiency. *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*

## A

- **ABI (Application Binary Interface)** - the low-level rules a program and the kernel (or libraries) agree on so compiled code can work together. *See: [srs-items](SECTIONS/srs-items.md).*
- **ACPI (Advanced Configuration and Power Interface)** - Advanced Configuration and Power Interface; the firmware tables and code that describe the board's devices, power states and interrupts to the operating system. *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md), [linux-os](SECTIONS/linux-os.md), [machine](SECTIONS/machine.md), [sensors](SECTIONS/sensors.md).*
- **ACPI namespace** - The tree of named objects in the ACPI tables, e.g. `\_SB_.PCI0.SBRG.EC0_`, which is the firmware's own map of the board's devices. *See: [buses-1](SECTIONS/buses-1.md), [machine](SECTIONS/machine.md).*
- **AER (Advanced Error Reporting)** - A PCI Express feature that counts and reports link errors (correctable and uncorrectable) to the kernel. *See: [buses-1](SECTIONS/buses-1.md), [graphics](SECTIONS/graphics.md).*
- **AES (Advanced Encryption Standard), AES-NI** - the standard way to encrypt data (disk encryption, VPN, HTTPS). AES-NI are CPU instructions that do one AES round in hardware, many times faster than plain code. *See: [tools](SECTIONS/tools.md).*
- **Affinity mask** - a bit pattern, one bit per CPU, saying which CPUs a task may run on. `FFFF` = CPUs 0-15. *See: [processors](SECTIONS/processors.md), [srs-items](SECTIONS/srs-items.md).*
- **AGM (arithmetic-geometric mean), Gauss-Legendre** - a way to compute pi that doubles the number of correct digits at every step; the method SuperPi uses. *See: [tools](SECTIONS/tools.md).*
- **AIO** - All-In-One liquid cooler: a sealed pump, radiator and fans in one unit. *See: [tools](SECTIONS/tools.md).*
- **AIO (all-in-one) cooler** - A sealed liquid cooler with pump, radiator and fans in one kit. *See: [sensors](SECTIONS/sensors.md).*
- **ALSA (Advanced Linux Sound Architecture)** - The kernel's sound driver layer. It talks to the sound chips and gives each card numbered devices such as `hw:1,0`. *See: [graphics](SECTIONS/graphics.md).*
- **Alt+letter (underlined letter)** - in a menu or window, the underlined letter of an entry: press Alt and that letter to pick it. Every entry of a menu has its own. *See: [User Guide 6.6](USER-GUIDE.md#66-classic-view-the-siv-style-windows).*
- **amd_pstate** - the Linux driver that controls clock speed on AMD Zen 2 and newer CPUs using CPPC; modes are active, guided and passive. *See: [processors](SECTIONS/processors.md).*
- **AML** - ACPI Machine Language; the bytecode in the DSDT and SSDTs that the operating system runs to control devices. *See: [machine](SECTIONS/machine.md).*
- **APIC** - Advanced Programmable Interrupt Controller; each CPU thread has one, and its APIC ID is the hardware's own number for that thread. *See: [machine](SECTIONS/machine.md), [processors](SECTIONS/processors.md).*
- **APIC ID** - the ID number each logical CPU reports about itself through the CPUID instruction. CPU Burnin uses it to check that a thread really runs where it was pinned. *See: [tools](SECTIONS/tools.md).*
- **ARP (Address Resolution Protocol)** - how an IPv4 machine finds the MAC (hardware) address that belongs to an IP address on the local network. *See: [network](SECTIONS/network.md).*
- **ASLR (Address Space Layout Randomization)** - loading programs and libraries at random memory addresses each run, to make attacks harder. *See: [linux-os](SECTIONS/linux-os.md).*
- **ASPM (Active State Power Management)** - lets a PCIe link drop into a low-power state when idle. *See: [machine](SECTIONS/machine.md), [srs-items](SECTIONS/srs-items.md).*
- **Associativity (ways)** - how many places in a cache a given piece of memory may be stored; more ways means fewer collisions. *See: [processors](SECTIONS/processors.md).*
- **Asynchronous I/O (AIO)** - handing the kernel several read or write requests at once and collecting the answers later; how the storage benchmark keeps Q requests in flight. *See: [tools](SECTIONS/tools.md).*
- **ATA** - AT Attachment: the command set used by SATA (and older IDE/PATA) hard disks and SSDs. *See: [storage](SECTIONS/storage.md).*
- **At the wall** - what the PC draws from the socket, including the power supply's own losses; what the electricity meter counts and you pay for. *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*
- **AUR (Arch User Repository)** - community build recipes for Arch-based systems. Packages built from it show as "foreign" in pacman. *See: [linux-os](SECTIONS/linux-os.md).*
- **Autosuspend** - the USB core's habit of putting an idle device to sleep after a set number of seconds; -1 turns it off. *See: [buses-2](SECTIONS/buses-2.md).*
- **AUXTIN** - Auxiliary temperature inputs on a Nuvoton Super I/O chip. The board maker decides which are wired; unwired ones float and show fake values. *See: [sensors](SECTIONS/sensors.md).*
- **Available Spare** - NVMe health field: the percentage of spare flash blocks an SSD has left (starts at 100%). *See: [storage](SECTIONS/storage.md).*
- **AVX / AVX2 / AVX-512** - Advanced Vector Extensions; instruction sets that work on many numbers at once (256 or 512 bits wide: 8 or 16 single-precision numbers per instruction). AVX-512 draws a lot of power under full load. *See: [processors](SECTIONS/processors.md), [tools](SECTIONS/tools.md).*

## B

- **Back end (GPU)** - the way a program talks to the GPU for computing: Vulkan, ROCm / HIP, CUDA or OpenCL. GPU Burnin and the GPU Compute comparison can use each one. *See: [tools](SECTIONS/tools.md).*
- **BAR (Base Address Register)** - a register in a PCI device that asks for a window of memory or I/O address space; the firmware or kernel assigns its start address at boot. *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md), [graphics](SECTIONS/graphics.md).*
- **BBR** - a TCP congestion-control algorithm that measures bandwidth and delay instead of waiting for packet loss. *See: [network](SECTIONS/network.md).*
- **bcdDevice / bcdUSB** - fields in a USB device descriptor: the device's own release number, and the USB version it claims to follow. *See: [buses-2](SECTIONS/buses-2.md).*
- **bfq** - Budget Fair Queueing: a Linux I/O scheduler that suits spinning disks and slow USB devices. *See: [storage](SECTIONS/storage.md).*
- **Binary MOF** - a compressed description of a vendor's WMI classes, stored in the firmware. *See: [machine](SECTIONS/machine.md).*
- **Binary splitting** - computing a long series by splitting it in halves again and again and joining the halves with big multiplications, so the work stays fast and can be shared between threads. *See: [tools](SECTIONS/tools.md).*
- **Bind mount** - Attaching a folder of an existing filesystem at a second place in the folder tree. *See: [storage](SECTIONS/storage.md).*
- **BMC (Baseboard Management Controller)** - A small separate computer on server boards that monitors sensors and allows remote management through IPMI. *See: [sensors](SECTIONS/sensors.md).*
- **BogoMIPS** - a rough number from a busy loop the kernel times at boot; not a real speed benchmark. *See: [processors](SECTIONS/processors.md).*
- **BOINC** - Berkeley Open Infrastructure for Network Computing, a volunteer computing client. *See: [tools](SECTIONS/tools.md).*
- **Boost (CPB / Turbo Boost)** - running above the base clock when power and temperature allow; CPB (Core Performance Boost) is AMD's name, Turbo Boost is Intel's. *See: [processors](SECTIONS/processors.md).*
- **BootOrder** - the UEFI variable listing the boot entries (Boot####) in the order the firmware tries them. *See: [machine](SECTIONS/machine.md).*
- **BORE (Burst-Oriented Response Enhancer)** - a change to Linux's EEVDF CPU scheduler that favours tasks which run in short bursts (typically interactive programs), shipped in some gaming kernels. *See: [tools](SECTIONS/tools.md).*
- **Boyer-Moore-Horspool** - a fast way to find a word in a text by skipping ahead using the last letter of the word. *See: [tools](SECTIONS/tools.md).*
- **Bridge (USB bridge)** - The chip in a USB enclosure, stick or card reader that converts USB to SATA, NVMe or flash commands. *See: [buses-1](SECTIONS/buses-1.md), [storage](SECTIONS/storage.md).*
- **BSSID** - the radio (MAC) address of one Wi-Fi access point; one network name (SSID) can have several. *See: [network](SECTIONS/network.md).*
- **btrfs** - A Linux copy-on-write filesystem with checksums, subvolumes, snapshots, compression and built-in multi-disk support. *See: [storage](SECTIONS/storage.md).*
- **BTU/h (British thermal units per hour)** - a unit of heat flow: 1 W is about 3.412 BTU/h. All the power a PC uses ends up as heat in the room. *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*
- **Built-in driver** - a driver compiled into the kernel file itself. It is always present and never appears in `/proc/modules`. *See: [linux-os](SECTIONS/linux-os.md).*
- **Bulk-Only Transport (BOT)** - the older, simpler USB mass-storage protocol (interface 08/06/50); slower than UAS. *See: [buses-2](SECTIONS/buses-2.md).*

## C

- **Carbon intensity** - grams of CO2 released per kWh of electricity on a grid, on average over a year. *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*
- **CLBlast** - a free, tuned matrix-maths library for OpenCL; the "vendor library" SRS uses on OpenCL when it is installed. *See: [tools](SECTIONS/tools.md).*
- **Compute error (GPU)** - a GPU result that differs from the right one. GPU Burnin counts them: any error means the card is not stable as set up (overclock, undervolt, heat, power) or faulty. *See: [tools](SECTIONS/tools.md).*
- **C-state** - an idle power level of a CPU core; C0 is running, deeper states (C1, C2, C3) save more power but take longer to wake. *See: [processors](SECTIONS/processors.md).*
- **Cache** - small fast memory on the CPU that keeps copies of recently used RAM; L1 is smallest and fastest, L3 largest and shared. *See: [processors](SECTIONS/processors.md).*
- **Cache level (L1, L2, L3)** - small, fast memories between the core and RAM. L1 is the smallest and fastest; L3 is the largest and is shared by many cores. *See: [tools](SECTIONS/tools.md).*
- **Capability (Linux)** - one split-off piece of root's power, such as `CAP_NET_BIND_SERVICE`. It is the Linux counterpart of a Windows privilege. *See: [linux-os](SECTIONS/linux-os.md).*
- **CardBus / PCMCIA** - Old credit-card-sized laptop expansion cards, replaced by ExpressCard and USB. *See: [buses-1](SECTIONS/buses-1.md).*
- **CCD** - Core Complex Die, one chiplet of CPU cores on a Ryzen processor. Each CCD usually has its own L3 cache. *See: [processors](SECTIONS/processors.md), [sensors](SECTIONS/sensors.md), [tools](SECTIONS/tools.md).*
- **CCID (Chip Card Interface Device)** - The standard USB class for smart card readers. *See: [buses-1](SECTIONS/buses-1.md).*
- **CEM (Card Electromechanical)** - The PCI Express standard for normal plug-in cards; `board_info type : cem` means a regular desktop graphics card. *See: [graphics](SECTIONS/graphics.md).*
- **Charmap** - a glibc file that defines a character set; used when building locales. *See: [linux-os](SECTIONS/linux-os.md).*
- **CHS** - Cylinder-Head-Sector: the old way of addressing disk blocks, replaced by LBA. SRS shows the legacy 255-head, 63-sector translation. *See: [storage](SECTIONS/storage.md).*
- **Chudnovsky series** - a formula for pi that gives about 14 more digits for every term; the fastest way known to compute many digits. *See: [tools](SECTIONS/tools.md).*
- **Class code** - The PCI number that says what kind of device it is, e.g. 0300 display, 0108 NVMe, 0604 PCI bridge. *See: [buses-1](SECTIONS/buses-1.md).*
- **Clock source** - the hardware timer Linux uses to keep time (tsc, hpet, acpi_pm). *See: [processors](SECTIONS/processors.md).*
- **Clocksource** - the hardware counter the kernel reads to keep time (TSC, HPET, ACPI PM timer). *See: [linux-os](SECTIONS/linux-os.md).*
- **Cluster** - The smallest unit of space a FAT, exFAT or NTFS filesystem hands out to a file. *See: [storage](SECTIONS/storage.md).*
- **CLZERO** - an AMD instruction that writes a whole zeroed 64-byte cache line straight to memory; the Cache & Memory benchmark uses it to measure how fast the RAM itself can take writes. *See: [tools](SECTIONS/tools.md).*
- **CMOS** - the battery-backed clock chip and its small memory on the motherboard; the name comes from the chip technology. *See: [machine](SECTIONS/machine.md).*
- **Code page** - an old table that maps byte values to characters (e.g. 437, 1252). UTF-8 has replaced code pages on Linux. *See: [linux-os](SECTIONS/linux-os.md).*
- **Code point** - Unicode's number for one character, written like `U+0041`. *See: [linux-os](SECTIONS/linux-os.md).*
- **COM port** - A serial port. COM1 is traditionally at I/O 0x3F8, IRQ 4 (`ttyS0` on Linux). *See: [buses-1](SECTIONS/buses-1.md).*
- **Commit charge** - the total memory programs have reserved; it can exceed RAM when swap exists. *See: [machine](SECTIONS/machine.md).*
- **Compression ratio** - how much smaller data became: 2.5 means the packed data is 2.5 times smaller than the original. *See: [tools](SECTIONS/tools.md).*
- **Compute unit (CU)** - A block of shader cores inside the GPU. RDNA GPUs pair two CUs into a WGP. *See: [graphics](SECTIONS/graphics.md).*
- **Congestion control** - the method TCP uses to guess how fast it can send without overloading the path. *See: [network](SECTIONS/network.md), [srs-items](SECTIONS/srs-items.md).*
- **Context switch** - the kernel stopping one task on a CPU and starting another; the OS Latency benchmark times it with two threads or programs passing a byte back and forth through a pipe. *See: [tools](SECTIONS/tools.md).*
- **Core dump** - a snapshot of a crashed program's memory, saved for debugging. *See: [linux-os](SECTIONS/linux-os.md).*
- **CPPC** - Collaborative Processor Performance Control; an ACPI standard where the CPU offers a smooth scale of performance levels and the OS sets a target range. *See: [processors](SECTIONS/processors.md).*
- **CPU affinity** - a rule that ties a thread to one or more chosen CPUs, so the scheduler does not move it elsewhere. *See: [tools](SECTIONS/tools.md).*
- **cpufreq** - the Linux subsystem that sets CPU clock speed, made of a driver (talks to hardware) and a governor (picks the speed). *See: [processors](SECTIONS/processors.md).*
- **CPUID** - an x86 instruction that returns facts about the CPU (vendor, family, features, caches) in four registers, organised by leaf number. *See: [processors](SECTIONS/processors.md).*
- **cpuidle** - the Linux subsystem that puts idle CPUs into C-states. *See: [processors](SECTIONS/processors.md).*
- **CRB** - Command Response Buffer; the memory-based interface used by firmware TPMs. *See: [machine](SECTIONS/machine.md).*
- **CRC32, CRC32C** - checksums that detect damaged data (CRC32 in ZIP and Ethernet, CRC32C in storage and networks); many CPUs have an instruction for CRC32C. *See: [tools](SECTIONS/tools.md).*
- **Critical Warning** - NVMe health field: a bit field that is 0x00 when the drive sees no serious problem. *See: [storage](SECTIONS/storage.md).*
- **CRTC** - The part of the display engine that reads one picture from memory and sends it to one or more outputs. VRR is switched on per CRTC. *See: [graphics](SECTIONS/graphics.md).*
- **CSM (Compatibility Support Module)** - Compatibility Support Module; the UEFI option that emulates a legacy BIOS for old operating systems and cards. *See: [linux-os](SECTIONS/linux-os.md), [machine](SECTIONS/machine.md).*
- **CSMI** - Common Storage Management Interface: a Windows driver interface some RAID controllers offer for status queries. *See: [storage](SECTIONS/storage.md).*
- **CSV (comma-separated values)** - A plain-text table format, one line per row, columns split by commas; opens in any spreadsheet. *See: [sensors](SECTIONS/sensors.md), [srs-items](SECTIONS/srs-items.md).*
- **CTA-861** - An EDID extension block with TV and HDMI data: extra video modes, audio formats and HDR information. *See: [graphics](SECTIONS/graphics.md).*
- **cuBLAS** - NVIDIA's tuned matrix-maths library for CUDA. *See: [tools](SECTIONS/tools.md).*
- **CUDA** - NVIDIA's GPU computing platform, used by most AI programs; needs the NVIDIA driver. With ZLUDA, CUDA programs can run on AMD cards. *See: [tools](SECTIONS/tools.md).*
- **CUDA toolkit (NVIDIA CUDA toolkit)** - NVIDIA's free set of developer tools for running programs on NVIDIA graphics cards. SRS can use one small part of it, NVRTC, as a fallback; since 2.3 it is optional, and with ZLUDA it is not recommended (it can break or remove ZLUDA). Install it later from Options > NVIDIA CUDA toolkit for GPU tests... *See: [User Guide 9.20](USER-GUIDE.md#920-the-nvidia-cuda-toolkit-optional), [tools](SECTIONS/tools.md).*
- **Curve Optimizer** - an AMD Ryzen BIOS setting that lowers (or raises) the voltage curve of each core. Too negative a value makes a core unstable or slow. *See: [tools](SECTIONS/tools.md).*

## D

- **DC power (inside the PC)** - the power after the power supply, used by the parts; always less than the power at the wall. *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*
- **Device lost** - the GPU stopped answering and its driver reset it; SRS ends a GPU test cleanly when it happens. *See: [tools](SECTIONS/tools.md).*
- **D-states** - device power states: D0 fully on, D3hot off but powered, D3cold power removed. *See: [machine](SECTIONS/machine.md).*
- **D0 / D3hot / D3cold** - PCI power states. D0 is fully on; D3hot and D3cold are sleep states (D3cold removes power completely). *See: [graphics](SECTIONS/graphics.md).*
- **DAD (Duplicate Address Detection)** - a check that no other device already uses an address before the PC starts using it. *See: [network](SECTIONS/network.md).*
- **Data Units** - NVMe counters of data read and written; one unit is 1,000 x 512 bytes (512,000 bytes). *See: [storage](SECTIONS/storage.md).*
- **dBm** - signal power on a logarithmic scale; for Wi-Fi, -30 is excellent and -80 is barely usable. *See: [network](SECTIONS/network.md).*
- **dconf** - the binary settings database used by GNOME and GTK apps. *See: [linux-os](SECTIONS/linux-os.md).*
- **DDC (Display Data Channel)** - The I2C wires in a video cable that carry the monitor's EDID and DDC/CI commands such as brightness. *See: [graphics](SECTIONS/graphics.md), [srs-items](SECTIONS/srs-items.md).*
- **Default gateway** - the router your PC sends traffic to when the destination is not on the local network. *See: [tools](SECTIONS/tools.md).*
- **Descriptor (USB)** - a small data table a USB device sends when plugged in, describing its IDs, class, interfaces and endpoints. *See: [buses-2](SECTIONS/buses-2.md).*
- **.desktop file** - a small text file that describes an app (name, icon, command, file types it opens). *See: [linux-os](SECTIONS/linux-os.md).*
- **dev_t** - The major:minor number pair the kernel uses to find a device file in /dev. *See: [buses-1](SECTIONS/buses-1.md).*
- **Device descriptor** - The USB record that gives a device's vendor ID, product ID, release number and class. *See: [buses-1](SECTIONS/buses-1.md).*
- **Device-mapper (dm)** - The Linux kernel layer that maps virtual block devices onto real ones; used by LVM and dm-crypt. *See: [storage](SECTIONS/storage.md).*
- **DGEMM, SGEMM** - double- and single-precision general matrix multiply: the standard way to measure floating-point speed, counted as 2 x n³ operations for n x n matrices. *See: [tools](SECTIONS/tools.md).*
- **DHCP (Dynamic Host Configuration Protocol)** - the way a router hands out IP addresses, gateway and DNS settings to devices automatically. *See: [network](SECTIONS/network.md).*
- **DHCP lease** - an IP address the router lends to a device for a set time; the device renews it before it runs out. *See: [network](SECTIONS/network.md).*
- **Dhrystone** - a classic integer (whole-number) CPU benchmark. Its score is given in DMIPS. *See: [tools](SECTIONS/tools.md).*
- **DIMM** - Dual In-line Memory Module; a desktop memory stick (SODIMM is the laptop size). *See: [machine](SECTIONS/machine.md), [srs-items](SECTIONS/srs-items.md).*
- **Dirty flag** - A filesystem marker meaning it was not closed cleanly and should be checked. *See: [storage](SECTIONS/storage.md).*
- **Dirty pages** - file data changed in RAM but not yet written to disk. *See: [linux-os](SECTIONS/linux-os.md).*
- **Discard** - The Linux name for TRIM. *See: [storage](SECTIONS/storage.md).*
- **DisplayID** - A newer monitor description format, often added as an EDID extension for high resolutions and refresh rates. *See: [graphics](SECTIONS/graphics.md).*
- **DKMS (Dynamic Kernel Module Support)** - a system that rebuilds out-of-tree kernel modules automatically for each new kernel. *See: [linux-os](SECTIONS/linux-os.md).*
- **DLL (Dynamic-Link Library)** - a Windows shared code file; the Linux equivalent is a shared object (`.so`). *See: [linux-os](SECTIONS/linux-os.md), [srs-items](SECTIONS/srs-items.md).*
- **dm-crypt** - Linux disk encryption built on device-mapper (usually set up with LUKS). *See: [storage](SECTIONS/storage.md).*
- **DMA (Direct Memory Access)** - A device reading or writing memory by itself, without the CPU copying the data. *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md), [machine](SECTIONS/machine.md).*
- **DMI (Desktop Management Interface)** - Desktop Management Interface; another name for the SMBIOS inventory, shown by Linux in `/sys/class/dmi/id`. *See: [linux-os](SECTIONS/linux-os.md), [machine](SECTIONS/machine.md), [sensors](SECTIONS/sensors.md).*
- **DMIPS** - Dhrystone MIPS: Dhrystone loops per second divided by 1757, the speed of a DEC VAX 11/780. *See: [tools](SECTIONS/tools.md).*
- **DNS (Domain Name System)** - the service that turns names like example.com into IP addresses. *See: [network](SECTIONS/network.md).*
- **DNS cache** - a local store of recent DNS answers so repeated lookups are instant. *See: [network](SECTIONS/network.md).*
- **DNS lookup** - turning a name like speed.cloudflare.com into an address; the first lookup asks the network, a repeat usually comes from a cache. *See: [tools](SECTIONS/tools.md).*
- **DPM (dynamic power management)** - The GPU's list of clock and voltage steps; the firmware switches between them many times a second. *See: [graphics](SECTIONS/graphics.md), [srs-items](SECTIONS/srs-items.md).*
- **DPMS (Display Power Management Signaling)** - The monitor's power state: On, Standby, Suspend or Off. *See: [graphics](SECTIONS/graphics.md).*
- **DRAM** - Dynamic RAM, the main system memory. It is much slower to reach than the caches. *See: [tools](SECTIONS/tools.md).*
- **drivetemp** - A Linux kernel module that reports SATA drive temperatures through hwmon without root. *See: [storage](SECTIONS/storage.md).*
- **DRM (Direct Rendering Manager)** - The kernel subsystem for GPUs. It creates `/dev/dri/cardN` and `/dev/dri/renderDN` and the folders in `/sys/class/drm`. *See: [graphics](SECTIONS/graphics.md).*
- **DSDT** - Differentiated System Description Table; the main ACPI table holding the board's AML code. *See: [machine](SECTIONS/machine.md).*
- **DSDT / SSDT** - The main and the extra ACPI code tables; they contain the firmware's methods, including WMI code. *See: [sensors](SECTIONS/sensors.md).*
- **dup** - A btrfs profile that keeps two copies of a block on the same disk. *See: [storage](SECTIONS/storage.md).*
- **Duplex** - whether both ends of a link can send at once (full) or must take turns (half). *See: [network](SECTIONS/network.md).*
- **Duty (duty cycle)** - The share of time a PWM signal is on; 100% means full power to the fan. *See: [sensors](SECTIONS/sensors.md).*
- **DXVK** - A translation layer that turns Direct3D 8/9/10/11 calls into Vulkan, used by Wine and Proton. *See: [graphics](SECTIONS/graphics.md).*

## E

- **E-core** - Efficiency core, a small, low-power core on hybrid CPUs such as Intel 12th generation and newer. *See: [tools](SECTIONS/tools.md).*
- **E820** - the firmware memory map that lists which physical address ranges are RAM or reserved. *See: [machine](SECTIONS/machine.md).*
- **e820 map** - the memory map the PC firmware hands to the operating system at boot, marking RAM, reserved and ACPI ranges. *See: [buses-2](SECTIONS/buses-2.md).*
- **EBDA** - Extended BIOS Data Area; a small legacy-BIOS data block just below 640 KB. *See: [machine](SECTIONS/machine.md).*
- **EC (Embedded Controller)** - A small microcontroller on the motherboard that handles power buttons, battery, fans and some sensors (ACPI ID PNP0C09). *See: [buses-1](SECTIONS/buses-1.md).*
- **ECAM (Enhanced Configuration Access Mechanism)** - The memory window through which the CPU reads each PCIe device's 4 KB configuration space. *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md).*
- **ECC** - Error-Correcting Code memory; detects and fixes single-bit memory errors. *See: [machine](SECTIONS/machine.md).*
- **ECDT** - The ACPI Embedded Controller Boot Resources Table, an optional early description of the EC. *See: [buses-1](SECTIONS/buses-1.md).*
- **EDAC** - Error Detection And Correction; the Linux subsystem that reports ECC memory errors. *See: [machine](SECTIONS/machine.md).*
- **EDID (Extended Display Identification Data)** - A 128-byte block (plus extensions) stored in the monitor that describes its maker, model, size and supported modes. *See: [graphics](SECTIONS/graphics.md).*
- **EEVDF (Earliest Eligible Virtual Deadline First)** - the standard Linux CPU scheduler since Linux 6.6 (it replaced CFS). *See: [tools](SECTIONS/tools.md).*
- **EGL** - The interface that connects OpenGL and OpenGL ES to windows on Wayland or X11. *See: [graphics](SECTIONS/graphics.md).*
- **ELD (EDID-Like Data)** - The audio part of a monitor's EDID, passed to the HDMI/DisplayPort sound driver. *See: [graphics](SECTIONS/graphics.md).*
- **ELF (Executable and Linkable Format)** - the file format of Linux programs and shared libraries. *See: [srs-items](SECTIONS/srs-items.md).*
- **Embedded controller (EC)** - a small microcontroller on the board for keyboard, battery, fans and buttons (mainly on laptops). *See: [machine](SECTIONS/machine.md).*
- **Endpoint (USB)** - a one-way data pipe inside a USB interface; its type is Control, Interrupt, Bulk or Isochronous. *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md).*
- **EPP (Energy Performance Preference)** - Energy Performance Preference; a hint to the CPU about how much to favour speed over power saving. *See: [linux-os](SECTIONS/linux-os.md), [processors](SECTIONS/processors.md), [srs-items](SECTIONS/srs-items.md).*
- **ESP (EFI System Partition)** - EFI System Partition: the FAT partition the UEFI firmware loads bootloaders from. *See: [linux-os](SECTIONS/linux-os.md), [storage](SECTIONS/storage.md).*
- **ESRT** - EFI System Resource Table; lists the firmware parts that can be updated with UEFI capsules. *See: [machine](SECTIONS/machine.md).*
- **exFAT** - Extended FAT: a simple filesystem for flash media that allows files over 4 GiB. *See: [storage](SECTIONS/storage.md).*
- **EXPO** - AMD Extended Profiles for Overclocking; faster memory settings stored in the SPD. *See: [machine](SECTIONS/machine.md).*
- **EXPO / XMP** - memory profiles (AMD EXPO, Intel XMP) stored on the RAM sticks, which set the rated speed and timings in one step. *See: [tools](SECTIONS/tools.md).*

## F

- **FADT (Fixed ACPI Description Table)** - Fixed ACPI Description Table (signature FACP); points to the other key tables and lists fixed power-management hardware. *See: [buses-1](SECTIONS/buses-1.md), [machine](SECTIONS/machine.md).*
- **Family / Model / Stepping** - numbers from CPUID that identify a CPU's generation, design and silicon revision. *See: [processors](SECTIONS/processors.md).*
- **Fan curve** - A list of temperature:duty points the fan controller follows, with straight lines between them. *See: [sensors](SECTIONS/sensors.md).*
- **FAT** - File Allocation Table: a simple filesystem family (FAT12, FAT16, FAT32) used on USB sticks and EFI partitions. *See: [storage](SECTIONS/storage.md).*
- **FCH (Fusion Controller Hub)** - AMD's built-in southbridge logic: SMBus, LPC, GPIO, power management. *See: [buses-1](SECTIONS/buses-1.md).*
- **fclk / socclk / dcefclk** - GPU clocks for the internal data fabric, the chip's support logic, and the display engine. *See: [graphics](SECTIONS/graphics.md).*
- **FCLK / UCLK / MCLK** - on AMD: Infinity Fabric clock, memory controller clock and memory clock. *See: [machine](SECTIONS/machine.md).*
- **FFT (Fast Fourier Transform)** - a fast way to split a signal into its frequencies; used in audio, images and science. Counted as 5 N log2 N operations for N points. *See: [tools](SECTIONS/tools.md).*
- **File descriptor** - a small number a process uses for each open file, pipe, socket or device; the Linux version of a Windows handle. *See: [linux-os](SECTIONS/linux-os.md).*
- **File lock** - a marker a program sets so others know it is using a file; listed in `/proc/locks`. *See: [srs-items](SECTIONS/srs-items.md).*
- **firewalld zone** - a named set of firewall rules; each network interface belongs to one zone. *See: [network](SECTIONS/network.md).*
- **FireWire (IEEE 1394)** - An older high-speed bus used for camcorders and pro audio. *See: [buses-1](SECTIONS/buses-1.md).*
- **Fixture** - a saved data dump used to test a program without the real hardware. *See: [srs-items](SECTIONS/srs-items.md).*
- **Floating input** - A sensor pin with nothing connected. Its voltage drifts with leakage and noise, so the reading is meaningless. *See: [sensors](SECTIONS/sensors.md).*
- **fontconfig** - the Linux library that finds fonts and picks one for each character. *See: [linux-os](SECTIONS/linux-os.md).*
- **FP16 / FP32 / FP64** - numbers with a decimal point stored in 16, 32 or 64 bits: half, single and double precision. Games mostly use FP32; most gaming GPUs are much slower at FP64. *See: [tools](SECTIONS/tools.md).*
- **FPDT** - Firmware Performance Data Table; records how long the firmware took to boot. *See: [machine](SECTIONS/machine.md).*
- **FPGA (Field-Programmable Gate Array)** - a chip whose logic circuits are loaded from a configuration file, so its function can be changed after manufacture. *See: [buses-2](SECTIONS/buses-2.md).*
- **FQDN (fully qualified domain name)** - a host name plus its domain, such as pc.example.lan. *See: [network](SECTIONS/network.md).*
- **FSR (FidelityFX Super Resolution)** - AMD's upscaling technique: render at a lower resolution, then scale up and sharpen. *See: [graphics](SECTIONS/graphics.md).*
- **fTPM** - firmware TPM; a TPM implemented inside the CPU's security processor instead of a separate chip. *See: [machine](SECTIONS/machine.md).*
- **Function (PCI)** - One logical device inside a PCI chip. A GPU's audio part is function 1 (`03:00.1`). *See: [buses-1](SECTIONS/buses-1.md).*
- **Futex** - "fast user-space mutex": the Linux building block that lets a waiting thread sleep and be woken by another; the OS Latency benchmark times the wake-up. *See: [tools](SECTIONS/tools.md).*

## G

- **gamescope** - Valve's small game compositor; it can upscale games with FSR (`gamescope -F fsr`). *See: [graphics](SECTIONS/graphics.md).*
- **Gateway** - the router that forwards your packets to other networks, usually your home router. *See: [network](SECTIONS/network.md).*
- **GBM (Generic Buffer Management)** - A library compositors use to get GPU memory buffers for the screen. *See: [graphics](SECTIONS/graphics.md).*
- **GC (graphics core)** - The 3D and compute engine of an AMD GPU. Its version gives the `gfx` target name (GC 12.0.1 = `gfx1201`). *See: [graphics](SECTIONS/graphics.md).*
- **GEMM (matrix multiplication)** - multiplying two big tables of numbers (general matrix multiply); the core of AI and many science programs, and a hard, steady load for a GPU. *See: [tools](SECTIONS/tools.md).*
- **GFLOPS** - billions of floating-point (decimal-point) operations per second. *See: [tools](SECTIONS/tools.md).*
- **GIOPS** - billions of whole-number (integer) operations per second. *See: [tools](SECTIONS/tools.md).*
- **GLVND** - The vendor-neutral OpenGL dispatch library that lets several OpenGL drivers be installed side by side. *See: [graphics](SECTIONS/graphics.md).*
- **Governor (CPU)** - the cpufreq policy that decides which speed to ask for (performance, powersave, schedutil). *See: [linux-os](SECTIONS/linux-os.md), [processors](SECTIONS/processors.md), [srs-items](SECTIONS/srs-items.md).*
- **GPE (General Purpose Event)** - General Purpose Event; an ACPI event bit used for wake-up and hot-plug signals. *See: [buses-1](SECTIONS/buses-1.md), [machine](SECTIONS/machine.md).*
- **GPIO (General Purpose Input/Output)** - Simple on/off signal pins that chips use for LEDs, buttons, resets and enables. *See: [buses-1](SECTIONS/buses-1.md).*
- **GPT** - GUID Partition Table: the modern partition table format, with type GUIDs, PARTUUIDs and names for each partition. *See: [storage](SECTIONS/storage.md).*
- **GSI** - Global System Interrupt; the interrupt number space used by I/O APICs. *See: [machine](SECTIONS/machine.md).*
- **GStreamer** - The main Linux media framework; plugins handle file formats, codecs and audio/video effects. *See: [graphics](SECTIONS/graphics.md).*
- **GT/s (gigatransfers per second)** - Gigatransfers per second: the per-lane speed of a PCIe link (8.0 = PCIe 3.0, 16.0 = PCIe 4.0, 32.0 = PCIe 5.0). *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md), [srs-items](SECTIONS/srs-items.md), [storage](SECTIONS/storage.md).*
- **GTT (graphics translation table)** - System RAM that the GPU is allowed to map and use, much slower for the GPU than VRAM. *See: [graphics](SECTIONS/graphics.md).*
- **GUID** - Globally Unique Identifier; a 128-bit ID, used for WMI interfaces and UEFI variable vendors. *See: [machine](SECTIONS/machine.md).*

## H

- **H:C:T:L** - Host:Channel:Target:LUN, the address of a device in the Linux SCSI layer. *See: [storage](SECTIONS/storage.md).*
- **HAL (Hardware Abstraction Layer)** - the lowest layer of Windows that hides hardware differences; SIV uses the name for its resource-map pages. *See: [buses-2](SECTIONS/buses-2.md).*
- **HD Audio (High Definition Audio)** - The Intel standard used by almost all PC sound chips and by GPU HDMI audio: a controller plus one or more codecs. *See: [graphics](SECTIONS/graphics.md).*
- **HDD** - Hard disk drive: storage on spinning magnetic platters. *See: [storage](SECTIONS/storage.md).*
- **Heap** - the memory a program requests while it runs (`malloc`). *See: [linux-os](SECTIONS/linux-os.md).*
- **HECI** - Host Embedded Controller Interface; the link between the OS and the Intel Management Engine (MEI on Linux). *See: [machine](SECTIONS/machine.md).*
- **HID (Human Interface Device)** - The device class and protocol for keyboards, mice, game pads, RGB controllers and security keys. *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md), [machine](SECTIONS/machine.md), [srs-items](SECTIONS/srs-items.md).*
- **hidraw** - A Linux device node (`/dev/hidrawN`) that gives programs raw access to a HID device. *See: [buses-1](SECTIONS/buses-1.md).*
- **HIP** - AMD's CUDA-like programming language for GPU computing, part of ROCm. *See: [graphics](SECTIONS/graphics.md).*
- **hipBLAS** - AMD's tuned matrix-maths library for ROCm / HIP. *See: [tools](SECTIONS/tools.md).*
- **hipRTC** - ROCm's runtime compiler, the HIP counterpart of NVRTC. The ROCm / HIP rows of GPU Burnin and the GPU Compute comparison still need it. *See: [tools](SECTIONS/tools.md).*
- **Holder** - A Linux block device built on top of another one (for example an encrypted volume on a partition). *See: [storage](SECTIONS/storage.md).*
- **Host bridge** - The PCI device that connects the CPU to the PCI bus (class 0600). *See: [buses-1](SECTIONS/buses-1.md).*
- **Hotspot temperature** - the hottest point measured on a GPU chip; AMD calls it junction temperature. Usually 10-20 °C above the edge temperature. *See: [tools](SECTIONS/tools.md).*
- **HPA** - Host Protected Area: a part of an ATA disk hidden from the operating system, which makes the disk look smaller. *See: [storage](SECTIONS/storage.md).*
- **HPET (High Precision Event Timer)** - High Precision Event Timer; a hardware timer described by its own ACPI table. *See: [buses-2](SECTIONS/buses-2.md), [machine](SECTIONS/machine.md).*
- **Hub (USB)** - a device that turns one USB port into several. *See: [buses-2](SECTIONS/buses-2.md).*
- **hwdata** - The package that ships the `pci.ids`, `usb.ids` and `pnp.ids` name lists. *See: [buses-1](SECTIONS/buses-1.md).*
- **hwdb** - udev's hardware database, which adds properties such as vendor and model names to devices. *See: [buses-1](SECTIONS/buses-1.md).*
- **hwmon** - The kernel's standard sensor interface. Each driver makes a folder of files such as `temp1_input` and `fan1_input`. *See: [graphics](SECTIONS/graphics.md), [linux-os](SECTIONS/linux-os.md), [machine](SECTIONS/machine.md), [processors](SECTIONS/processors.md), [sensors](SECTIONS/sensors.md), [srs-items](SECTIONS/srs-items.md), [storage](SECTIONS/storage.md).*
- **Hybrid CPU** - a CPU that mixes two kinds of cores (P-cores and E-cores). *See: [tools](SECTIONS/tools.md).*

## I

- **I/O APIC** - the interrupt controller that collects legacy interrupt lines and routes them to CPUs; "IR-" in front means the IOMMU remaps them. *See: [buses-2](SECTIONS/buses-2.md).*
- **I/O port** - An address in the separate 64K x86 I/O space, used by legacy devices (keyboard controller, RTC, COM ports, SMBus). *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md).*
- **I/O scheduler** - The kernel part that decides the order in which waiting read/write requests go to a drive. *See: [srs-items](SECTIONS/srs-items.md), [storage](SECTIONS/storage.md).*
- **I2C (Inter-Integrated Circuit)** - A slow two-wire bus that chips use to talk to each other. Monitors use it for DDC. *See: [buses-1](SECTIONS/buses-1.md), [graphics](SECTIONS/graphics.md), [srs-items](SECTIONS/srs-items.md).*
- **I2C / SMBus** - slow two-wire buses for small chips such as sensors and memory SPD hubs; SMBus is the PC variant. *See: [machine](SECTIONS/machine.md).*
- **ICD (installable client driver)** - A small file that tells the Vulkan or OpenCL loader where a driver library is. *See: [graphics](SECTIONS/graphics.md).*
- **ICMP (Internet Control Message Protocol)** - the protocol that carries ping and network error messages. *See: [network](SECTIONS/network.md).*
- **iconv** - the glibc library for converting text between character sets. *See: [linux-os](SECTIONS/linux-os.md).*
- **IMC (Integrated Memory Controller)** - the part of the CPU that talks to RAM. *See: [srs-items](SECTIONS/srs-items.md).*
- **IMSM** - Intel Matrix Storage Manager: the RAID metadata format used by Intel RST and VROC, supported by Linux mdadm. *See: [storage](SECTIONS/storage.md).*
- **Infinity Fabric** - AMD's internal link between the CCDs, the I/O die and the memory controller. *See: [tools](SECTIONS/tools.md).*
- **initramfs** - a small early-boot file system that loads drivers needed to mount the real root disk. *See: [linux-os](SECTIONS/linux-os.md).*
- **Inode** - A filesystem's record for one file or folder. *See: [storage](SECTIONS/storage.md).*
- **Interface (USB)** - One function of a USB device, with its own class and driver. One device can have several. *See: [buses-1](SECTIONS/buses-1.md).*
- **IOMMU (input-output memory management unit)** - Input-Output Memory Management Unit; controls which memory each device can reach, needed for passing devices to virtual machines. *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md), [graphics](SECTIONS/graphics.md), [machine](SECTIONS/machine.md), [processors](SECTIONS/processors.md).*
- **IOMMU group** - The smallest set of devices the IOMMU can isolate from each other. It matters for passing a device to a virtual machine. *See: [buses-1](SECTIONS/buses-1.md).*
- **IOPS** - input/output operations per second: how many separate read or write requests a drive answers each second. *See: [tools](SECTIONS/tools.md).*
- **IP block** - A reusable hardware building block inside an AMD chip (graphics core, video engine, memory controller...), each with its own version. *See: [graphics](SECTIONS/graphics.md).*
- **IPI** - Inter-Processor Interrupt, a signal from one CPU to another. SRS's IPI Delay measures the core-to-core cache-line delay instead. *See: [tools](SECTIONS/tools.md).*
- **IPMI (Intelligent Platform Management Interface)** - A standard for talking to a server's BMC: sensors, event log, remote power control. *See: [sensors](SECTIONS/sensors.md).*
- **IrDA** - Infrared data transfer on old laptops and phones. Linux removed support in kernel 4.17. *See: [buses-1](SECTIONS/buses-1.md).*
- **IRQ (Interrupt Request)** - A signal a device uses to get the CPU's attention. Also the number of that interrupt line. *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md), [graphics](SECTIONS/graphics.md).*
- **ISA (Industry Standard Architecture)** - The original PC expansion bus. Its job now lives on in LPC. *See: [buses-1](SECTIONS/buses-1.md).*
- **isohybrid** - A CD/DVD image (ISO 9660) built so it can also boot when written straight to a USB stick. *See: [storage](SECTIONS/storage.md).*
- **Itanium (IA-64)** - Intel and HP's 64-bit server processor family, unrelated to x86-64, discontinued in 2021. *See: [buses-2](SECTIONS/buses-2.md).*
- **IVRS** - I/O Virtualization Reporting Structure; the ACPI table describing AMD's IOMMU (Intel uses DMAR). *See: [machine](SECTIONS/machine.md).*

## J

- **JEDEC** - the standards body for memory; the JEDEC speed is a stick's standard base speed. *See: [machine](SECTIONS/machine.md).*
- **Jitter** - how much a time (round trip, connect) changes from one try to the next. *See: [tools](SECTIONS/tools.md).*
- **Joule (J)** - a unit of energy: 3,600 J = 1 Wh. Linux energy counters count microjoules (millionths of a joule). *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*
- **Junction temperature** - the hottest point measured on a graphics chip, usually higher than its "edge" temperature. *See: [tools](SECTIONS/tools.md).*

## K

- **k10temp** - the Linux driver that reads AMD CPU temperature sensors (Tctl, Tccd). *See: [processors](SECTIONS/processors.md).*
- **Kernel (GPU)** - a small program that runs on the GPU, many copies at once (not the Linux kernel). *See: [tools](SECTIONS/tools.md).*
- **Kernel module** - a loadable piece of the kernel, usually a driver; the Linux counterpart of a Windows driver. *See: [linux-os](SECTIONS/linux-os.md), [srs-items](SECTIONS/srs-items.md).*
- **KMS (kernel mode setting)** - The part of DRM that sets screen resolutions and refresh rates. *See: [graphics](SECTIONS/graphics.md).*
- **KSM (Kernel Same-page Merging)** - the kernel finds identical memory pages and keeps only one copy. *See: [srs-items](SECTIONS/srs-items.md).*
- **KVM** - Kernel-based Virtual Machine; Linux's built-in hypervisor, available through /dev/kvm. *See: [processors](SECTIONS/processors.md).*
- **kWh (kilowatt-hour)** - 1000 watts used for one hour; the unit electricity is billed in. *See: [sensors](SECTIONS/sensors.md).*
- **kW (kilowatt)** - 1,000 watts. *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*

## L

- **LACT** - A Linux tool for AMD GPU overclocking, undervolting and fan control. *See: [graphics](SECTIONS/graphics.md).*
- **LAN (local area network)** - the devices in your home or office connected to the same router or switch. *See: [network](SECTIONS/network.md).*
- **Lane (PCIe)** - One pair of signal wires in each direction. A link has 1, 2, 4, 8 or 16 lanes. *See: [buses-1](SECTIONS/buses-1.md).*
- **Latency** - waiting time: how long it takes for a request (a memory load, a ping) to be answered. *See: [tools](SECTIONS/tools.md).*
- **Lavapipe** - Mesa's software Vulkan driver, which runs on the CPU; SRS's GPU tests use it so that they never touch a real graphics card. *See: [tools](SECTIONS/tools.md).*
- **LBA** - Logical Block Addressing: numbering a disk's blocks from 0 upwards, the way all modern drives are addressed. *See: [storage](SECTIONS/storage.md).*
- **LDN (Logical Device Number)** - The number of one function block (serial port, hardware monitor ...) inside a Super I/O chip. *See: [sensors](SECTIONS/sensors.md).*
- **Leaf (CPUID)** - the question number given to the CPUID instruction; some leaves have several sub-leaves. *See: [processors](SECTIONS/processors.md).*
- **LFC (low framerate compensation)** - A VRR trick that shows frames twice when the frame rate falls below the monitor's VRR minimum. *See: [graphics](SECTIONS/graphics.md).*
- **Lineup** - a saved arrangement of the main window's tabs: which items each page shows as tabs, in what order, and the tab bar shape. Basic, Enthusiast and Expert are built in; you can save your own. *See: [User Guide 6.7](USER-GUIDE.md#67-arranging-the-tabs-and-lineups).*
- **Link width (PCIe)** - The number of lanes a PCIe link is using (x1, x4, x8, x16). *See: [buses-1](SECTIONS/buses-1.md).*
- **Link-local address** - an address that only works on the local cable or Wi-Fi (fe80:: for IPv6, 169.254. for IPv4), made automatically. *See: [network](SECTIONS/network.md).*
- **llvmpipe** - Mesa's software renderer. It draws on the CPU and is only a fallback; its Vulkan driver is lavapipe. GPU Burnin never burns it as if it were a GPU. *See: [graphics](SECTIONS/graphics.md), [tools](SECTIONS/tools.md).*
- **lm-sensors** - The standard Linux sensor package; its config files (/etc/sensors3.conf, /etc/sensors.d) rename, scale or hide readings. *See: [sensors](SECTIONS/sensors.md).*
- **Load average** - the average number of tasks running or waiting for a CPU over 1, 5 and 15 minutes. *See: [linux-os](SECTIONS/linux-os.md).*
- **Locale** - a bundle of language and regional rules: language, date and number format, sorting, currency. *See: [linux-os](SECTIONS/linux-os.md), [srs-items](SECTIONS/srs-items.md).*
- **Locally administered MAC** - a MAC address made up by software rather than assigned by the hardware maker. *See: [network](SECTIONS/network.md).*
- **Logical CPU** - one hardware thread as Linux sees it. With SMT, each physical core shows up as two logical CPUs. *See: [srs-items](SECTIONS/srs-items.md), [tools](SECTIONS/tools.md).*
- **Loopback (127.0.0.1)** - the computer's connection to itself; data never reaches a network card. *See: [tools](SECTIONS/tools.md).*
- **LPC (Low Pin Count)** - A slim bus from the chipset to the Super I/O chip, TPM, serial port and BIOS flash. *See: [buses-1](SECTIONS/buses-1.md).*
- **LPM (Link Power Management)** - USB power saving that puts the link into low-power states between transfers; some devices need it disabled. *See: [buses-2](SECTIONS/buses-2.md).*
- **LSM (Linux Security Module)** - a pluggable security layer in the kernel, such as Yama, Landlock, AppArmor or SELinux. *See: [linux-os](SECTIONS/linux-os.md).*
- **LUN** - Logical Unit Number: one device behind one SCSI address, such as one slot of a card reader. *See: [storage](SECTIONS/storage.md).*
- **LVM** - Logical Volume Manager: Linux tool for flexible volumes spread over one or more disks. *See: [storage](SECTIONS/storage.md).*

## M

- **MAC address (Media Access Control)** - the 6-byte hardware address of a network adapter. *See: [network](SECTIONS/network.md).*
- **MADT** - Multiple APIC Description Table (signature APIC); lists the CPU threads and interrupt controllers. *See: [machine](SECTIONS/machine.md).*
- **Mandelbrot set** - a famous fractal; drawing it is a simple, repeatable floating-point test. Measured in Mpixels/s. *See: [tools](SECTIONS/tools.md).*
- **MangoHud** - A Linux in-game overlay that shows frame rate, temperatures, clocks and more, and can limit the frame rate. *See: [graphics](SECTIONS/graphics.md).*
- **Masquerade** - NAT (network address translation): many devices share one address on the way out. *See: [network](SECTIONS/network.md).*
- **Mbit/s** - million bits per second; 8 bits = 1 byte, so 100 Mbit/s is 12.5 MB/s. Internet plans are sold in Mbit/s. *See: [tools](SECTIONS/tools.md).*
- **MBR** - Master Boot Record: the old partition table format (shown as `dos`), with four primary partitions and one-byte type codes. *See: [storage](SECTIONS/storage.md).*
- **MCA (Micro Channel Architecture)** - IBM's PS/2 expansion bus from 1987. *See: [buses-1](SECTIONS/buses-1.md).*
- **MCE (Machine Check Exception)** - the CPU reporting a hardware error it detected, for example in cache or memory. *See: [buses-2](SECTIONS/buses-2.md).*
- **MCFG** - The ACPI table that gives the ECAM base address. *See: [buses-1](SECTIONS/buses-1.md).*
- **MCH (Memory Controller Hub)** - The old Intel "northbridge" chip that held the memory controller. AMD CPUs have no MCH. *See: [buses-1](SECTIONS/buses-1.md).*
- **MCHBAR (Memory Controller Hub Base Address Register)** - the address of a block of Intel memory-controller registers. *See: [srs-items](SECTIONS/srs-items.md).*
- **mclk** - The GPU's memory clock. GDDR6 moves 16 bits per clock per pin, so 1258 MHz is about 20 Gbps. *See: [graphics](SECTIONS/graphics.md).*
- **mclk / sclk** - a GPU's memory clock and shader (core) clock. *See: [srs-items](SECTIONS/srs-items.md).*
- **MCS (Modulation and Coding Scheme)** - the Wi-Fi encoding step; higher is faster but needs a cleaner signal. *See: [network](SECTIONS/network.md).*
- **md** - The Linux software RAID driver, managed with `mdadm`. *See: [storage](SECTIONS/storage.md).*
- **mDNS (multicast DNS)** - lets devices announce names and services on the LAN without a DNS server. *See: [network](SECTIONS/network.md).*
- **Mesa** - The open-source graphics driver collection on Linux, including RADV (Vulkan) and radeonsi (OpenGL) for AMD. *See: [graphics](SECTIONS/graphics.md).*
- **Metric** - a route's priority number; when two routes match, the lower metric wins. *See: [network](SECTIONS/network.md).*
- **MFT** - Master File Table: the NTFS index that holds a record for every file. *See: [storage](SECTIONS/storage.md).*
- **Microcode** - a small firmware patch for the processor, loaded by the BIOS or the OS at boot to fix bugs and security issues. *See: [processors](SECTIONS/processors.md).*
- **MIDI sequencer (ALSA)** - The kernel service that routes MIDI events between programs, synths and MIDI hardware. *See: [graphics](SECTIONS/graphics.md).*
- **MIME type (Multipurpose Internet Mail Extensions type)** - a standard name for a kind of file, such as `image/png`. *See: [linux-os](SECTIONS/linux-os.md).*
- **MIO (Memory and I/O controller)** - the main chip of HP's zx1 chipset, linking Itanium 2 processors to memory and I/O. *See: [buses-2](SECTIONS/buses-2.md).*
- **MMIO (Memory-Mapped I/O)** - device registers placed in the physical memory address space so the CPU reaches them with normal reads and writes. *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md).*
- **Modalias** - A device's driver-matching ID string, e.g. `pci:v00001022d000014D8...`. Linux uses it to load the right driver. *See: [buses-1](SECTIONS/buses-1.md).*
- **Module (kernel)** - A driver file loaded into the kernel at run time. "Built-in" means it is compiled into the kernel. *See: [buses-1](SECTIONS/buses-1.md).*
- **Mount point** - The folder where a filesystem is attached to the folder tree. *See: [storage](SECTIONS/storage.md).*
- **MRH (Memory Repeater Hub)** - An Intel 820/840-era chip that let RDRAM boards use SDRAM. *See: [buses-1](SECTIONS/buses-1.md).*
- **MSI (message signaled interrupts)** - A way for PCI devices to send interrupts as memory writes instead of dedicated wires. *See: [graphics](SECTIONS/graphics.md).*
- **MSI / MSI-X (Message Signalled Interrupts)** - PCI Express interrupts sent as a memory write instead of a wire; each device or queue gets its own vector. *See: [buses-2](SECTIONS/buses-2.md), [machine](SECTIONS/machine.md).*
- **MSR** - Model Specific Register; an internal CPU control or counter register read with RDMSR, root only on Linux. *See: [processors](SECTIONS/processors.md).*
- **MT/s** - mega-transfers per second; the memory data rate, twice the memory clock for DDR. *See: [machine](SECTIONS/machine.md).*
- **MTP (Media Transfer Protocol)** - the protocol phones and media players use to share files over USB without exposing a raw disk. *See: [buses-2](SECTIONS/buses-2.md).*
- **MTU (Maximum Transmission Unit)** - the largest packet a link carries, normally 1500 bytes on Ethernet. *See: [network](SECTIONS/network.md).*
- **Multicast** - one-to-many traffic: devices join a group address and receive what is sent to it. *See: [network](SECTIONS/network.md).*
- **Multikey quicksort** - a way of sorting text that compares one letter position at a time. *See: [tools](SECTIONS/tools.md).*
- **Multipath** - Reaching one drive over more than one route; Linux combines the routes into one disk. *See: [storage](SECTIONS/storage.md).*
- **Mutex (mutual exclusion)** - a lock that stops two threads using the same thing at the same time. *See: [srs-items](SECTIONS/srs-items.md).*
- **MWIPS** - Million Whetstone Instructions Per Second, the score of the Whetstone benchmark. *See: [tools](SECTIONS/tools.md).*

## N

- **N-body simulation** - working out the gravity between every pair of a group of bodies (stars, planets); a classic floating-point workload. *See: [tools](SECTIONS/tools.md).*
- **Namespace** - A slice of an NVMe SSD presented as one disk; consumer SSDs have one. *See: [storage](SECTIONS/storage.md).*
- **NDP (Neighbor Discovery Protocol)** - the IPv6 version of ARP. *See: [network](SECTIONS/network.md).*
- **NetBIOS browsing** - the old Windows way of listing computers on a LAN through an elected master browser. *See: [network](SECTIONS/network.md).*
- **Netlink** - the socket type tools like `ip` use to talk to the Linux kernel. *See: [network](SECTIONS/network.md).*
- **Nice** - a process's politeness value (-20 to 19); higher nice means it gives way to other processes. *See: [linux-os](SECTIONS/linux-os.md), [srs-items](SECTIONS/srs-items.md).*
- **NMI (Non-Maskable Interrupt)** - an interrupt the CPU cannot ignore, used for watchdogs, profiling and serious hardware errors. *See: [buses-2](SECTIONS/buses-2.md).*
- **Normalised value** - A SMART attribute's score on the maker's scale (often starting at 100 or 200), which goes down as things get worse. *See: [storage](SECTIONS/storage.md).*
- **NPU (Neural Processing Unit)** - An AI accelerator such as AMD XDNA in Ryzen AI laptops. *See: [buses-1](SECTIONS/buses-1.md).*
- **NQN** - NVMe Qualified Name: a unique name an NVMe subsystem reports about itself. *See: [storage](SECTIONS/storage.md).*
- **NSS (Name Service Switch)** - glibc's list that decides where names are looked up and in what order. *See: [network](SECTIONS/network.md).*
- **NTFS** - New Technology File System: the main Windows filesystem; Linux reads and writes it with the ntfs3 driver. *See: [storage](SECTIONS/storage.md).*
- **NTP (Network Time Protocol)** - the protocol that keeps the clock in sync with time servers over the network. *See: [linux-os](SECTIONS/linux-os.md).*
- **NTT (number-theoretic transform)** - a relative of the FFT that works with whole numbers modulo a prime, used to multiply numbers with millions of digits quickly and exactly. *See: [tools](SECTIONS/tools.md).*
- **NUMA** - Non-Uniform Memory Access; a design where memory is attached to particular CPUs, so "far" memory is slower. *See: [machine](SECTIONS/machine.md), [processors](SECTIONS/processors.md), [tools](SECTIONS/tools.md).*
- **NUMA node** - A group of CPUs and memory that are close to each other. Desktops have one node, which Linux shows as -1 for PCI devices. *See: [buses-1](SECTIONS/buses-1.md), [graphics](SECTIONS/graphics.md), [storage](SECTIONS/storage.md).*
- **NVMe** - Non-Volatile Memory Express: the protocol fast SSDs use over PCIe. *See: [storage](SECTIONS/storage.md).*
- **NVMe Composite** - An NVMe drive's own summary temperature, used for its thermal throttling. *See: [sensors](SECTIONS/sensors.md).*
- **NVRTC (NVIDIA runtime compiler)** - the part of the CUDA toolkit that turns GPU program text into GPU code while a program runs (`libnvrtc`). SRS used it for its CUDA tests before 2.3; now only when the driver refuses the built-in PTX. ZLUDA has none. *See: [User Guide 9.20](USER-GUIDE.md#920-the-nvidia-cuda-toolkit-optional), [tools](SECTIONS/tools.md).*
- **NX / XD** - No-Execute (AMD) / Execute Disable (Intel); marks memory as data-only so it cannot be run as code. *See: [processors](SECTIONS/processors.md).*

## O

- **O_DIRECT** - opening a file so reads and writes go straight to the drive, past Linux's page cache in RAM; benchmarks use it so they measure the drive, not the RAM. *See: [tools](SECTIONS/tools.md).*
- **OpenCL** - An open standard for running general computing tasks on GPUs and other processors. *See: [graphics](SECTIONS/graphics.md).*
- **OpenGL** - The older cross-platform 3D graphics interface; still used by many programs and older games. *See: [graphics](SECTIONS/graphics.md).*
- **Option ROM** - firmware stored on a PCI card (listed as the ROM BAR), for example a graphics card's video BIOS. *See: [buses-2](SECTIONS/buses-2.md), [machine](SECTIONS/machine.md).*
- **OS latency** - how long the operating system takes to answer a program: system calls, task switches, wake-ups, timers. Lower and steadier means a more responsive PC. *See: [tools](SECTIONS/tools.md).*
- **OUI (Organizationally Unique Identifier)** - the first three bytes of a MAC address, identifying the maker. *See: [network](SECTIONS/network.md).*
- **Overcommit** - the kernel promising programs more memory than it really has, betting they will not all use it at once. *See: [linux-os](SECTIONS/linux-os.md).*
- **OverDrive** - AMD's name for user clock, voltage and power tuning. On Linux it appears as `pp_od_clk_voltage`. *See: [graphics](SECTIONS/graphics.md).*

## P

- **P-core** - Performance core, a big, fast core on a hybrid CPU. *See: [tools](SECTIONS/tools.md).*
- **Page fault** - what happens the first time a program touches a page of memory the kernel has not set up yet: the kernel stops the program for a moment and supplies the page. *See: [tools](SECTIONS/tools.md).*
- **PLATYPUS** - a 2020 attack that used CPU energy readings to learn secrets (CVE-2020-8694/8695, CVE-2020-12912); since Linux 5.10 the powercap RAPL counters are root-only because of it. *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*
- **P-state** - a performance state: a running speed and voltage level of the CPU. *See: [processors](SECTIONS/processors.md).*
- **PAC (proxy auto-config)** - a script that picks which proxy to use for each address. *See: [network](SECTIONS/network.md).*
- **Page** - the unit the CPU and kernel use to manage memory, usually 4 KB on x86. *See: [srs-items](SECTIONS/srs-items.md).*
- **Page cache** - spare RAM the kernel uses to hold recently read or written file data. *See: [linux-os](SECTIONS/linux-os.md).*
- **PARTUUID** - The unique ID of a partition, stored in the partition table (not in the filesystem). *See: [storage](SECTIONS/storage.md).*
- **PCH (Platform Controller Hub)** - Intel's chipset; PCH temperature inputs on a Super I/O read 0 on AMD boards. *See: [sensors](SECTIONS/sensors.md).*
- **PCI address** - a device's location written as domain:bus:device.function, for example 0000:03:00.0. *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md).*
- **PCI bridge (PtP)** - a PCI-to-PCI bridge; each slot, M.2 socket or chipset port is one and creates a new bus number. *See: [machine](SECTIONS/machine.md).*
- **PCI function** - one logical PCI device; one chip can show several (for example graphics and audio). *See: [srs-items](SECTIONS/srs-items.md).*
- **PCI ID** - the vendor, device, subsystem and revision numbers a PCI device reports. Linux uses them to pick a driver. *See: [tools](SECTIONS/tools.md).*
- **PCI ID database** - The `pci.ids` file that turns vendor and device numbers into names. *See: [buses-1](SECTIONS/buses-1.md).*
- **PCIe (PCI Express)** - The serial point-to-point version of PCI used for graphics cards, NVMe drives and most onboard devices. *See: [buses-1](SECTIONS/buses-1.md), [storage](SECTIONS/storage.md).*
- **PCIe link speed and width** - Speed is the transfer rate per lane (2.5, 5, 8, 16, 32 GT/s = PCIe 1 to 5); width is the number of lanes (x1 to x16). *See: [graphics](SECTIONS/graphics.md).*
- **PCIe link width** - the number of PCI Express lanes in use (x1, x4, x8, x16); bandwidth scales with it. *See: [buses-2](SECTIONS/buses-2.md), [srs-items](SECTIONS/srs-items.md).*
- **PCIe switch** - A chip with one upstream port and several downstream ports that share one link between devices. *See: [buses-1](SECTIONS/buses-1.md).*
- **PCM (pulse-code modulation)** - Plain digital audio. An ALSA PCM device is one playback or recording stream. *See: [graphics](SECTIONS/graphics.md).*
- **PCR** - Platform Configuration Register; a TPM register holding a hash of what ran during boot. *See: [machine](SECTIONS/machine.md).*
- **PECI / SB-TSI** - Side channels that let the Super I/O read the CPU temperature (PECI on Intel, SB-TSI on AMD). *See: [sensors](SECTIONS/sensors.md).*
- **Percentage Used** - NVMe health field: the drive's estimate of how much of its rated write life is used. *See: [storage](SECTIONS/storage.md).*
- **PipeWire** - The modern Linux sound (and video) server. It mixes programs together and routes them to outputs. *See: [graphics](SECTIONS/graphics.md).*
- **PM profile** - The firmware's preferred power profile: 1 Desktop, 2 Mobile, 3 Workstation, 4 Enterprise server. *See: [buses-1](SECTIONS/buses-1.md).*
- **PM table** - the power-management table the AMD SMU keeps, with live powers, clocks, voltages and temperatures. *See: [processors](SECTIONS/processors.md), [srs-items](SECTIONS/srs-items.md).*
- **PM1 block** - The ACPI I/O registers for sleep, wake and the power button. *See: [buses-1](SECTIONS/buses-1.md).*
- **PM_TMR** - The ACPI power-management timer, which runs at 3.58 MHz. *See: [buses-1](SECTIONS/buses-1.md).*
- **PMBus** - An SMBus-based protocol for smart power supplies and voltage regulators. *See: [buses-1](SECTIONS/buses-1.md).*
- **PnP (Plug and Play)** - The firmware's description of built-in legacy devices, with IDs like `PNP0501`. *See: [buses-1](SECTIONS/buses-1.md).*
- **PnP ID** - A 3-letter vendor code plus 4 hex digits (`PNP0501`), or with ACPI a 4-letter code plus 4 hex digits (`AMDI0030`). *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md), [graphics](SECTIONS/graphics.md).*
- **Pointer chase** - a memory test where each load needs the address from the previous load, so prefetching cannot hide the delay. *See: [tools](SECTIONS/tools.md).*
- **polkit** - the Linux service that decides which desktop users may perform admin actions, and whether a password is needed. *See: [linux-os](SECTIONS/linux-os.md), [srs-items](SECTIONS/srs-items.md).*
- **Port** - a number (0-65535) that picks which program on a machine receives a packet. *See: [network](SECTIONS/network.md).*
- **Port path** - the chain of USB port numbers from the root hub to a device, for example 2.3.4. *See: [buses-2](SECTIONS/buses-2.md).*
- **Power cap** - the power limit firmware holds a chip to. *See: [srs-items](SECTIONS/srs-items.md).*
- **PPI** - Physical Presence Interface; how the operating system asks the firmware to clear or enable the TPM at the next boot. *See: [machine](SECTIONS/machine.md).*
- **PPT (Package Power Tracking)** - AMD's measured power for a CPU or GPU package, and the limit applied to it. *See: [graphics](SECTIONS/graphics.md), [sensors](SECTIONS/sensors.md), [srs-items](SECTIONS/srs-items.md).*
- **Preemption** - how quickly the kernel can interrupt a running task to run another; `full` gives the lowest latency. *See: [linux-os](SECTIONS/linux-os.md).*
- **Preferred core (prefcore)** - a per-core ranking from the CPU telling the scheduler which cores boost best. *See: [processors](SECTIONS/processors.md).*
- **Prefetchable** - A BAR whose memory can be read ahead and cached safely, such as graphics memory. *See: [buses-1](SECTIONS/buses-1.md).*
- **Prefix length** - the "/24" or "/64" after an address: how many leading bits are the network part. *See: [network](SECTIONS/network.md).*
- **Proton** - Valve's version of Wine, built into Steam, that runs Windows games on Linux. *See: [graphics](SECTIONS/graphics.md).*
- **Proxy** - a middle server that makes web requests on your behalf. *See: [network](SECTIONS/network.md).*
- **PSP (Platform Security Processor)** - A small security processor inside AMD chips (the `MP0` block); it loads and checks other firmware. *See: [graphics](SECTIONS/graphics.md), [machine](SECTIONS/machine.md).*
- **PSS (Proportional Set Size)** - a process's RAM use with shared pages split fairly among the processes sharing them. *See: [linux-os](SECTIONS/linux-os.md).*
- **PSU efficiency (80 PLUS, Cybenetics)** - the share of the wall power that reaches the parts; 90 % means 10 % turns into heat in the power supply. 80 PLUS and Cybenetics certify it at set loads. *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*
- **PTP (Picture Transfer Protocol)** - the older camera file-transfer protocol that MTP is built on; uses USB class 06 (Still Image). *See: [buses-2](SECTIONS/buses-2.md).*
- **PTX (Parallel Thread Execution)** - NVIDIA's portable GPU code, a step between program text and the code of one GPU. SRS 2.3 carries its CUDA tests as PTX; the graphics driver (or ZLUDA) finishes it for the card when a test starts. *See: [tools](SECTIONS/tools.md).*
- **PWM (pulse-width modulation)** - A way to control fan speed by switching power on and off quickly; the duty cycle sets the speed. *See: [graphics](SECTIONS/graphics.md), [sensors](SECTIONS/sensors.md).*

## Q

- **Q1T1, Q8T1, Q32T16** - storage test settings: Q = queue depth (how many requests wait at once: 1 is like most desktop programs, 32 like a busy server), T = threads. *See: [tools](SECTIONS/tools.md).*
- **qdisc (queueing discipline)** - the kernel's waiting line for outgoing packets, such as fq or fq_codel. *See: [network](SECTIONS/network.md), [srs-items](SECTIONS/srs-items.md).*
- **QST (Quiet System Technology)** - Intel fan control built into the management engine of some 2007-2010 desktop boards. *See: [sensors](SECTIONS/sensors.md).*
- **Queue depth (Q), threads (T)** - Q = how many requests one thread keeps waiting at the drive at once, T = how many threads do so; "Q32T16" keeps 512 requests in flight. *See: [tools](SECTIONS/tools.md).*
- **Quirk (USB)** - a kernel workaround flag for a device that does not follow the USB standard exactly. *See: [buses-2](SECTIONS/buses-2.md).*

## R

- **radeonsi** - Mesa's OpenGL driver for AMD GPUs. *See: [graphics](SECTIONS/graphics.md).*
- **Radix sort** - sorting numbers by their digits (or bytes) instead of comparing them with each other. *See: [tools](SECTIONS/tools.md).*
- **RADV** - Mesa's Vulkan driver for AMD GPUs. *See: [graphics](SECTIONS/graphics.md).*
- **RAID** - Redundant Array of Independent Disks: joining disks for speed (raid0), safety (raid1 mirrors) or both. *See: [storage](SECTIONS/storage.md).*
- **Rank** - a set of memory chips on a stick that is accessed together; sticks are single- or dual-rank. *See: [machine](SECTIONS/machine.md).*
- **RAPL (Running Average Power Limit)** - A CPU feature that counts energy used; Linux exposes it under /sys/class/powercap, named intel-rapl even on AMD. *See: [processors](SECTIONS/processors.md), [sensors](SECTIONS/sensors.md).*
- **Raw value** - A SMART attribute's actual counter (sectors, hours, degrees); its format is up to the maker. *See: [storage](SECTIONS/storage.md).*
- **Ray tracing** - drawing a picture by following rays of light from the eye into a scene, with shadows and reflections. *See: [tools](SECTIONS/tools.md).*
- **Regex** - regular expression, a text search pattern (for example `^` = start of line, `.` = any character). *See: [tools](SECTIONS/tools.md).*
- **Regular expression (regex)** - a search pattern, e.g. `nvme[0-9]` matches nvme0, nvme1 and so on. *See: [srs-items](SECTIONS/srs-items.md).*
- **Report descriptor** - The HID record that tells the PC which buttons, axes and LEDs a device has. *See: [buses-1](SECTIONS/buses-1.md).*
- **Resizable BAR** - A PCIe feature that lets the CPU see all of the GPU's VRAM at once instead of through a 256 MB window. AMD calls it Smart Access Memory. *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md), [graphics](SECTIONS/graphics.md), [machine](SECTIONS/machine.md).*
- **Revision ID** - The PCI number that tells chip steppings or versions apart. *See: [buses-1](SECTIONS/buses-1.md).*
- **rfkill** - the Linux kernel's on/off switch for radios (Wi-Fi, Bluetooth, mobile data); soft = software, hard = physical switch. *See: [network](SECTIONS/network.md).*
- **ROCm** - AMD's open GPU computing platform (HIP, OpenCL, libraries) for Linux. *See: [graphics](SECTIONS/graphics.md).*
- **Root complex** - The part of the CPU where PCIe starts. On AMD it is the device at `00:00.0`. *See: [buses-1](SECTIONS/buses-1.md).*
- **Root hub** - The virtual hub that stands for a USB host controller's ports. Each USB 3 controller has a 2.0 root hub and a 3.x root hub. *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md).*
- **Round trip (compression)** - compress, then decompress, then compare with the original; it must be identical. *See: [tools](SECTIONS/tools.md).*
- **Round trip time** - the time for a message to go to the other end and come back. *See: [tools](SECTIONS/tools.md).*
- **RPC** - Remote Procedure Call, a way for one program to send commands to another over a network connection. *See: [tools](SECTIONS/tools.md).*
- **RPM (revolutions per minute)** - Fan or pump speed. *See: [sensors](SECTIONS/sensors.md).*
- **RSS (Resident Set Size)** - the RAM a process is really using right now. *See: [linux-os](SECTIONS/linux-os.md).*
- **RTC (real-time clock)** - the battery-backed clock on the motherboard that keeps time while the PC is off. *See: [linux-os](SECTIONS/linux-os.md), [machine](SECTIONS/machine.md).*
- **Runtime PM** - Kernel power management that lets an idle device sleep while the system is running. *See: [graphics](SECTIONS/graphics.md).*

## S

- **SAE (Simultaneous Authentication of Equals)** - the WPA3 password handshake, safer than WPA2's. *See: [network](SECTIONS/network.md).*
- **Samba** - the Linux implementation of Windows file and printer sharing (SMB). *See: [network](SECTIONS/network.md).*
- **SATA** - Serial ATA: the cable and protocol for most 2.5-inch and 3.5-inch drives. *See: [storage](SECTIONS/storage.md).*
- **sched_ext** - a Linux feature that lets a CPU scheduler be loaded as a BPF program while the system runs. *See: [linux-os](SECTIONS/linux-os.md), [srs-items](SECTIONS/srs-items.md).*
- **SCI** - System Control Interrupt; ACPI's shared interrupt line, usually IRQ 9. *See: [machine](SECTIONS/machine.md).*
- **sclk** - The GPU's shader (core) clock. *See: [graphics](SECTIONS/graphics.md).*
- **SCSI** - Small Computer System Interface: a command set Linux uses for SATA, USB and SAS storage. *See: [storage](SECTIONS/storage.md).*
- **SCT** - SMART Command Transport: an ATA channel for extra data such as temperature history and error recovery limits. *See: [storage](SECTIONS/storage.md).*
- **SDMA (system DMA)** - The GPU's copy engines, which move data between memory areas without using the 3D engine. *See: [graphics](SECTIONS/graphics.md).*
- **Section (ELF)** - one named part of a program file: `.text` code, `.data` variables, `.rodata` fixed data, `.bss` zeroed memory. *See: [srs-items](SECTIONS/srs-items.md).*
- **Secure Boot** - a UEFI feature that only starts boot loaders and kernels signed with trusted keys. *See: [linux-os](SECTIONS/linux-os.md), [machine](SECTIONS/machine.md).*
- **Session (busy session)** - in Power & cost, a stretch of heavy use (a game, a render) found from the CPU and graphics load; its energy is the whole PC's while it lasted. *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*
- **Setup Mode** - UEFI Secure Boot state with no Platform Key installed, in which keys can be changed freely. *See: [machine](SECTIONS/machine.md).*
- **SEV** - Secure Encrypted Virtualization; AMD server feature that encrypts virtual machine memory. *See: [processors](SECTIONS/processors.md).*
- **SHA-256, SHA-NI** - SHA-256 makes a 256-bit "fingerprint" of data (package checks, backups, git); SHA-NI are CPU instructions that speed it up. *See: [tools](SECTIONS/tools.md).*
- **Shared library** - a `.so` code file many programs use at once, such as `libc.so.6`. *See: [srs-items](SECTIONS/srs-items.md).*
- **Sieve of Eratosthenes** - finding prime numbers by crossing out the multiples of each prime; what remains are the primes. *See: [tools](SECTIONS/tools.md).*
- **Sink / source** - In a sound server, a sink is where sound goes (speakers, HDMI); a source is where sound comes from (microphone, line in). *See: [graphics](SECTIONS/graphics.md).*
- **SLAAC (stateless address autoconfiguration)** - the way an IPv6 device builds its own address from the router's advertisements. *See: [network](SECTIONS/network.md).*
- **Slab** - the kernel's allocator for its own small objects; "reclaimable" slab is cache it can drop. *See: [linux-os](SECTIONS/linux-os.md).*
- **SLAT** - Second Level Address Translation (Intel EPT, AMD RVI/NPT); hardware help that makes virtual machine memory fast. *See: [processors](SECTIONS/processors.md).*
- **Slicing-by-8** - a way of computing CRC checksums 8 bytes at a time with lookup tables. *See: [tools](SECTIONS/tools.md).*
- **SMART (Self-Monitoring, Analysis and Reporting Technology)** - Self-Monitoring, Analysis and Reporting Technology: the health log a drive keeps about itself. *See: [srs-items](SECTIONS/srs-items.md), [storage](SECTIONS/storage.md).*
- **SMB (Server Message Block)** - the Windows file-sharing protocol. *See: [network](SECTIONS/network.md).*
- **SMBIOS (System Management BIOS)** - System Management BIOS; the firmware inventory of board, BIOS, CPU socket and memory slots. *See: [buses-1](SECTIONS/buses-1.md), [machine](SECTIONS/machine.md), [srs-items](SECTIONS/srs-items.md).*
- **SMBus (System Management Bus)** - an I2C-based bus on the motherboard that links RAM SPD chips and sensors. *See: [buses-1](SECTIONS/buses-1.md), [srs-items](SECTIONS/srs-items.md).*
- **SMN (System Management Network)** - AMD's internal register network for the Data Fabric and memory controllers. SRS never touches it. *See: [buses-1](SECTIONS/buses-1.md), [machine](SECTIONS/machine.md), [processors](SECTIONS/processors.md).*
- **SMT** - Simultaneous Multi-Threading (Intel: Hyper-Threading); one physical core runs two threads, shown as two CPUs. *See: [processors](SECTIONS/processors.md), [srs-items](SECTIONS/srs-items.md), [tools](SECTIONS/tools.md).*
- **SMT sibling** - the other logical CPU that shares the same physical core. *See: [tools](SECTIONS/tools.md).*
- **SMU (System Management Unit)** - System Management Unit; a small processor inside a Ryzen CPU that controls boost, voltage and power limits. *See: [graphics](SECTIONS/graphics.md), [processors](SECTIONS/processors.md), [srs-items](SECTIONS/srs-items.md).*
- **Socket** - a program's handle for one network conversation or local channel. *See: [network](SECTIONS/network.md).*
- **soname** - the version number in a shared library's file name (the 6 in `libc.so.6`); it changes only when compatibility breaks. *See: [linux-os](SECTIONS/linux-os.md).*
- **SPD (Serial Presence Detect)** - Serial Presence Detect; the data on each memory stick describing its size, speeds, timings and profiles. *See: [buses-1](SECTIONS/buses-1.md), [machine](SECTIONS/machine.md), [srs-items](SECTIONS/srs-items.md).*
- **spd5118** - the SPD hub chip on DDR5 sticks, and its Linux driver; it also provides a temperature sensor. *See: [machine](SECTIONS/machine.md).*
- **Speculative execution** - the CPU guessing ahead to run faster; flaws in it (Spectre, Meltdown) can leak data and are closed by mitigations. *See: [processors](SECTIONS/processors.md).*
- **SPIR-V** - the compiled form of a Vulkan compute or graphics program (shader); SRS ships its GPU shaders pre-compiled as SPIR-V. *See: [tools](SECTIONS/tools.md).*
- **SQLite** - a small database kept in one file; SRS keeps the Power & cost history in one. *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*
- **srs-root** - the SRS root helper, started from Menu > Run as root; it saves root-only data (such as real resource addresses) for SRS to read. *See: [buses-2](SECTIONS/buses-2.md), [srs-items](SECTIONS/srs-items.md).*
- **SSD** - Solid-state drive: storage on flash memory, with no moving parts. *See: [storage](SECTIONS/storage.md).*
- **SSDT** - Secondary System Description Table; extra ACPI code tables, often one per firmware module. *See: [machine](SECTIONS/machine.md).*
- **SSID (Service Set Identifier)** - a Wi-Fi network's name. *See: [network](SECTIONS/network.md).*
- **_STA** - the ACPI status value of a device; SRS shows it as hex plus letters P (present), E (enabled), S (shown), F (functioning). *See: [machine](SECTIONS/machine.md).*
- **Standing charge** - what an electricity supplier charges per day whatever you use. *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*
- **statvfs** - The system call that returns a mounted filesystem's size, free space and block size. *See: [storage](SECTIONS/storage.md).*
- **Stratum (NTP)** - how many steps a time server is from a reference clock; stratum 1 is directly attached to one. *See: [linux-os](SECTIONS/linux-os.md).*
- **Subsystem ID** - The second PCI ID pair, which names the board or card maker and their product built around a chip. *See: [buses-1](SECTIONS/buses-1.md).*
- **Subvolume** - A btrfs folder tree that can be mounted on its own, like a partition inside the filesystem. *See: [storage](SECTIONS/storage.md).*
- **Super I/O** - A motherboard chip that reads fans, voltages and board temperatures and drives fan headers (Nuvoton NCT, ITE IT87 ...). *See: [buses-1](SECTIONS/buses-1.md), [sensors](SECTIONS/sensors.md), [tools](SECTIONS/tools.md).*
- **Super-I/O** - a motherboard chip (Nuvoton, ITE and others) that reads fan speeds, voltages and board temperatures. *See: [srs-items](SECTIONS/srs-items.md).*
- **SuperSpeed / SuperSpeed+** - USB 3 speeds: 5 Gbps (SuperSpeed, USB 3.2 Gen 1) and 10 or 20 Gbps (SuperSpeed+, Gen 2 and Gen 2x2). *See: [buses-2](SECTIONS/buses-2.md).*
- **SVI** - Serial VID Interface; the link a CPU uses to ask the board's voltage regulator for a voltage and read it back. *See: [processors](SECTIONS/processors.md).*
- **SVM / AMD-V** - AMD's hardware virtualization feature (Secure Virtual Machine). *See: [processors](SECTIONS/processors.md).*
- **Swap** - disk space or compressed RAM used when real RAM is full; the Linux version of the Windows paging file. *See: [linux-os](SECTIONS/linux-os.md), [machine](SECTIONS/machine.md), [storage](SECTIONS/storage.md).*
- **Swappiness** - a kernel setting for how eagerly memory is moved to swap. *See: [srs-items](SECTIONS/srs-items.md).*
- **sysctl** - a kernel setting you can read and change at run time, under `/proc/sys`. *See: [srs-items](SECTIONS/srs-items.md).*
- **sysfs** - The `/sys` folder tree where the kernel shows devices and their settings as small text files. *See: [buses-1](SECTIONS/buses-1.md), [graphics](SECTIONS/graphics.md), [storage](SECTIONS/storage.md).*
- **System call** - a request from a program to the operating system kernel (open a file, read the clock, start a thread); the cost of crossing from the program into the kernel and back. *See: [tools](SECTIONS/tools.md).*
- **systemd target** - a named group of systemd units that the system boots into, similar to a Windows boot mode. *See: [linux-os](SECTIONS/linux-os.md).*
- **systemd unit** - a systemd configuration object (service, socket, timer...); a service unit is the Linux version of a Windows service. *See: [linux-os](SECTIONS/linux-os.md).*

## T

- **Taint flag** - a kernel record of an unusual condition since boot (unsigned module, hardware error, warning); it clears only on reboot. *See: [linux-os](SECTIONS/linux-os.md).*
- **TBW** - Terabytes written: the amount of data an SSD maker rates the drive to survive. *See: [storage](SECTIONS/storage.md).*
- **TCP (Transmission Control Protocol)** - the reliable, ordered connection protocol used by the web, SSH and most apps. *See: [network](SECTIONS/network.md).*
- **TCP states** - the stages a TCP connection goes through, such as Listen, Established, Close Wait and Time Wait. *See: [network](SECTIONS/network.md).*
- **Tctl** - AMD's control temperature, the CPU temperature value used for fan control and throttling. *See: [sensors](SECTIONS/sensors.md), [tools](SECTIONS/tools.md).*
- **Tctl / Tccd** - AMD temperatures: Tctl is the control value used for fans and limits (may include an offset), Tccd is each core die. *See: [processors](SECTIONS/processors.md).*
- **Thermal zone** - A firmware-defined temperature area with trip points (passive, active, hot, critical); common on laptops. *See: [sensors](SECTIONS/sensors.md), [srs-items](SECTIONS/srs-items.md).*
- **Thermistor** - A resistor whose value changes with temperature; most board temperature probes are thermistors. *See: [sensors](SECTIONS/sensors.md).*
- **THP (Transparent Huge Pages)** - grouping memory into 2 MB pages automatically so the CPU spends less time on address lookups. *See: [linux-os](SECTIONS/linux-os.md), [srs-items](SECTIONS/srs-items.md).*
- **Thread state** - a one-letter code: R running, S sleeping, D waiting on hardware, T stopped, Z zombie. *See: [srs-items](SECTIONS/srs-items.md).*
- **Threaded dispatch** - an interpreter technique where each instruction jumps straight to the next one's code instead of going back through one big switch. *See: [tools](SECTIONS/tools.md).*
- **Threshold** - The SMART failure line for an attribute; a normalised value at or below it means the attribute has failed. *See: [storage](SECTIONS/storage.md).*
- **Throttling** - the CPU lowering its clock to stay under a temperature or power limit. *See: [tools](SECTIONS/tools.md).*
- **Thunderbolt** - Intel's high-speed port that carries PCI Express, DisplayPort and USB over one cable; USB4 is based on it. *See: [buses-2](SECTIONS/buses-2.md).*
- **Tiered price** - an electricity price that changes after so many kWh a month. *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*
- **Time-of-use price** - an electricity price that depends on the hour and the day. *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*
- **Timer slack** - how much later than asked Linux may wake a sleeping thread, so that wake-ups can be grouped to save power; 50 µs by default. *See: [tools](SECTIONS/tools.md).*
- **Tjmax** - the highest temperature a CPU allows before it throttles itself. *See: [tools](SECTIONS/tools.md).*
- **TLB shootdown** - an interrupt one CPU sends others to make them forget cached address translations after memory mappings change. *See: [buses-2](SECTIONS/buses-2.md).*
- **TLS handshake** - setting up the encrypted connection that https uses, before any data flows. *See: [tools](SECTIONS/tools.md).*
- **TPM** - Trusted Platform Module; a security device that stores keys and measures the boot process. *See: [machine](SECTIONS/machine.md).*
- **Transparent huge pages (THP)** - Linux handing out memory in 2 MiB pages instead of 4 KiB ones when it can, so the CPU needs fewer page-table look-ups. *See: [tools](SECTIONS/tools.md).*
- **TRIM** - A command that tells an SSD which blocks no longer hold data, so it can erase them ahead of time. *See: [storage](SECTIONS/storage.md).*
- **Trip point** - a temperature where the system acts: passive = slow down, critical = shut down. *See: [srs-items](SECTIONS/srs-items.md).*
- **TSC (Time Stamp Counter)** - Time Stamp Counter; a CPU counter that rises at a fixed rate and is the fastest clock for the OS when stable. *See: [linux-os](SECTIONS/linux-os.md), [processors](SECTIONS/processors.md).*
- **TTL** - Time To Live, the number of router hops a network packet may still make. *See: [tools](SECTIONS/tools.md).*
- **TTY** - A terminal or serial device in Linux (`ttyS0`, `ttyUSB0`, `pts/0`). *See: [buses-1](SECTIONS/buses-1.md).*

## U

- **UART (Universal Asynchronous Receiver-Transmitter)** - The chip behind a serial port, e.g. 16550A. *See: [buses-1](SECTIONS/buses-1.md).*
- **UAS (USB Attached SCSI)** - USB Attached SCSI: the faster protocol for USB storage; the older one is Bulk-Only Transport (`usb-storage`). *See: [buses-2](SECTIONS/buses-2.md), [graphics](SECTIONS/graphics.md), [storage](SECTIONS/storage.md).*
- **udev** - The Linux device manager; it names devices, creates `/dev/disk/by-*` links and keeps a database of device properties. *See: [buses-1](SECTIONS/buses-1.md), [linux-os](SECTIONS/linux-os.md), [storage](SECTIONS/storage.md).*
- **UDP (User Datagram Protocol)** - a simple send-and-forget protocol used by DNS, games and voice. *See: [network](SECTIONS/network.md).*
- **UEFI (Unified Extensible Firmware Interface)** - Unified Extensible Firmware Interface; the modern firmware that replaced the PC BIOS. *See: [linux-os](SECTIONS/linux-os.md), [machine](SECTIONS/machine.md).*
- **UEFI variable** - a named setting stored in the firmware's flash, such as BootOrder or SecureBoot. *See: [machine](SECTIONS/machine.md).*
- **UID (user ID)** - the number the kernel uses for a user account; the name is only a label. *See: [linux-os](SECTIONS/linux-os.md).*
- **UMC (Unified Memory Controller)** - AMD's memory controller, built into the CPU's Data Fabric. *See: [buses-1](SECTIONS/buses-1.md), [machine](SECTIONS/machine.md).*
- **UPS (Uninterruptible Power Supply)** - A battery unit that keeps the PC powered during a power cut. *See: [sensors](SECTIONS/sensors.md).*
- **USB (Universal Serial Bus)** - The common peripheral bus: 1.5 and 12 Mbps (USB 1), 480 Mbps (USB 2), 5/10/20 Gbps (USB 3.x). *See: [buses-1](SECTIONS/buses-1.md).*
- **usb.ids** - the community list that maps USB vendor and product numbers to names, installed by the hwdata package. *See: [buses-2](SECTIONS/buses-2.md).*
- **USB/IP** - a Linux feature that shares USB devices over the network; its virtual host controller is vhci_hcd. *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md).*
- **USB4** - the newest USB standard (up to 40 or 80 Gbps) that can tunnel PCI Express and DisplayPort. *See: [buses-2](SECTIONS/buses-2.md).*
- **usbfs** - the Linux interface that lets a user program talk to a USB device directly instead of through a kernel driver. *See: [buses-2](SECTIONS/buses-2.md).*
- **UUID (Bluetooth service)** - Universally Unique Identifier: an ID stored in a filesystem, used to mount it reliably. *See: [network](SECTIONS/network.md), [storage](SECTIONS/storage.md).*

## V

- **VA-API (Video Acceleration API)** - The Linux interface for hardware video decoding and encoding. *See: [graphics](SECTIONS/graphics.md).*
- **VBIOS** - The graphics card's own firmware chip. It starts the card at power-on and holds its power and clock tables. *See: [graphics](SECTIONS/graphics.md).*
- **VCN / UVD** - AMD's video decode/encode engine. VCN is the current name; some chips still report it as UVD. *See: [graphics](SECTIONS/graphics.md).*
- **Vcore** - the main supply voltage of the CPU cores, measured by the motherboard's sensor chip. *See: [processors](SECTIONS/processors.md).*
- **vDSO** - a small piece of kernel code mapped into every program so that reading the clock does not need a real system call. *See: [tools](SECTIONS/tools.md).*
- **Vendor ID** - The 16-bit number that names a chip maker, e.g. 1022 AMD, 1002 AMD/ATI, 10EC Realtek. *See: [buses-1](SECTIONS/buses-1.md).*
- **Vendor library (GPU)** - the GPU maker's own tuned maths library: hipBLAS (AMD ROCm), cuBLAS (NVIDIA CUDA) or CLBlast (OpenCL). *See: [tools](SECTIONS/tools.md).*
- **VID** - Voltage Identification; the voltage the CPU requests from the regulator. *See: [processors](SECTIONS/processors.md).*
- **VID / PID (Vendor ID / Product ID)** - the two 16-bit numbers that identify a USB device's maker and model, written VVVV:PPPP. *See: [buses-2](SECTIONS/buses-2.md).*
- **VID:PID** - USB vendor ID and product ID, which identify a USB chip or device model. *See: [storage](SECTIONS/storage.md).*
- **VKD3D-Proton** - A translation layer that turns Direct3D 12 calls into Vulkan, used by Proton. *See: [graphics](SECTIONS/graphics.md).*
- **VMD** - Volume Management Device: an Intel PCIe block that gathers NVMe drives for Intel RST and VROC. *See: [storage](SECTIONS/storage.md).*
- **VPN (virtual private network)** - an encrypted tunnel that carries your traffic through another network or server. *See: [network](SECTIONS/network.md).*
- **VRAM** - The graphics card's own fast memory. *See: [graphics](SECTIONS/graphics.md).*
- **VROC** - Virtual RAID on CPU: Intel's RAID for NVMe drives attached to the processor. *See: [storage](SECTIONS/storage.md).*
- **VRR (variable refresh rate)** - The monitor waits for the GPU's next frame instead of refreshing at a fixed rate. Sold as FreeSync, G-Sync Compatible or HDMI VRR. *See: [graphics](SECTIONS/graphics.md).*
- **VT-x / VMX** - Intel's hardware virtualization feature. *See: [processors](SECTIONS/processors.md).*
- **Vulkan** - The modern cross-platform graphics and compute interface, used by most new Linux games and by DXVK and VKD3D-Proton. *See: [graphics](SECTIONS/graphics.md).*

## W

- **WAN (wide area network)** - the internet side of your router. *See: [network](SECTIONS/network.md).*
- **WGP (work-group processor)** - Two compute units working as a pair on RDNA GPUs. ROCm OpenCL counts WGPs as compute units. *See: [graphics](SECTIONS/graphics.md).*
- **Whetstone** - a classic floating-point (decimal maths) CPU benchmark. Its score is given in MWIPS. *See: [tools](SECTIONS/tools.md).*
- **Wh (watt-hour)** - energy used over time: 100 W for one hour is 100 Wh; 1,000 Wh is 1 kWh. *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*
- **Wine** - A compatibility layer that runs Windows programs on Linux. *See: [graphics](SECTIONS/graphics.md).*
- **WMI** - Windows Management Instrumentation; a way for firmware to offer vendor features through ACPI (PNP0C14 devices). *See: [machine](SECTIONS/machine.md), [sensors](SECTIONS/sensors.md).*
- **WPA2 / WPA3** - Wi-Fi security standards; WPA3 is newer and stronger. *See: [network](SECTIONS/network.md).*
- **WPAD** - Web Proxy Auto-Discovery, a way for a PC to find a PAC script automatically. *See: [network](SECTIONS/network.md).*
- **WPBT** - Windows Platform Binary Table; lets firmware hand Windows a program to run at startup. *See: [machine](SECTIONS/machine.md).*
- **WPD (Windows Portable Devices)** - the Windows name for MTP/PTP devices such as phones and cameras. *See: [buses-2](SECTIONS/buses-2.md).*
- **Wrap-around (counter)** - a counter reaching its highest value and starting again from zero, like an odometer; SRS unwraps the CPU energy counter. *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*
- **Write-back cache** - A drive feature that confirms a write once it is in the drive's own fast memory. *See: [storage](SECTIONS/storage.md).*
- **WS-Discovery (Web Services Discovery)** - the method modern Windows uses to find other PCs and printers on the LAN. *See: [network](SECTIONS/network.md).*
- **W (watt)** - how fast power is being used right now. *See: [User Guide 6.11](USER-GUIDE.md#611-power--cost).*
- **WWN** - World Wide Name: a globally unique ID some drives report, used for stable device names. *See: [storage](SECTIONS/storage.md).*

## X

- **x86-64-v2 / v3 / v4** - CPU feature levels; v3 needs AVX2, v4 needs AVX-512. *See: [srs-items](SECTIONS/srs-items.md).*
- **XDG autostart** - the desktop standard: `.desktop` files in an autostart folder run at login. *See: [srs-items](SECTIONS/srs-items.md).*
- **XDG (freedesktop.org Cross-Desktop Group) folders** - the standard per-user folders such as `~/.config`, `~/.local/share` and `~/.cache`. *See: [linux-os](SECTIONS/linux-os.md).*
- **XFS** - A high-performance Linux journaling filesystem. *See: [storage](SECTIONS/storage.md).*
- **xHCI** - The USB 3 host controller standard. One xHCI controller also serves USB 2 and USB 1 devices. *See: [buses-1](SECTIONS/buses-1.md), [buses-2](SECTIONS/buses-2.md).*
- **XMC (Switched Mezzanine Card)** - an ANSI/VITA 42 plug-in module for industrial and embedded computers, connected over PCI Express. *See: [buses-2](SECTIONS/buses-2.md).*
- **XMP** - Intel Extreme Memory Profile; faster memory settings stored in the SPD. *See: [machine](SECTIONS/machine.md).*
- **xxHash** - a very fast non-cryptographic hash by Yann Collet, used to check data quickly; xxHash64 gives a 64-bit result. *See: [tools](SECTIONS/tools.md).*

## Z

- **zenergy** - An out-of-tree Linux driver that reads AMD energy counters per core and per socket. *See: [sensors](SECTIONS/sensors.md).*
- **ZLUDA** - A layer that lets programs written for NVIDIA CUDA run on AMD GPUs. *See: [graphics](SECTIONS/graphics.md).*
- **zram** - a RAM-based block device that stores compressed data, often used as fast swap. *See: [linux-os](SECTIONS/linux-os.md), [srs-items](SECTIONS/srs-items.md), [storage](SECTIONS/storage.md).*
- **Zstandard (zstd), LZ4, xz, zlib** - common compression formats and libraries: lz4 is the fastest, zstd fast with good ratios, zlib (gzip / ZIP) the classic, and xz the smallest but slowest. *See: [tools](SECTIONS/tools.md).*
- **zswap** - a compressed cache in RAM that sits in front of disk swap. *See: [linux-os](SECTIONS/linux-os.md).*
- **zx1** - HP's chipset for Itanium 2 servers and workstations. *See: [buses-2](SECTIONS/buses-2.md).*
