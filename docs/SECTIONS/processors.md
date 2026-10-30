# Processors  (SRS Section Reference)

[Back to the section index](README.md) - [Glossary](../GLOSSARY.md)

**Where to find these items (SRS 2.0).** In the main window they are on the **Processor** page (Alt+2). Its tabs are CPU Summary, Processors, CPU Clocks, CPUID Features, Cache Levels, CPUID, Microcode and Speculation Control; **All N items** lists the rest, plus **Each CPU** and **Cache levels of each CPU** (SIV's CPU-n and Cache-n buttons). You can also type an item's name or id into the search box (Ctrl+F). The rest of this introduction describes the classic view (Menu -> Classic View), with its Menu and panel buttons.

This group covers everything about the processor (CPU): its name and family, how many cores and threads
it has, its caches, the features it supports, its clock speeds, power and idle states, temperatures,
voltages and the low-level registers behind them. Open any item from **Menu -> Processors -> item**.
The quickest way in is the **Processors** button on the classic main panel. Its row of sub-buttons opens
**Features**, **Hypervisor**, **Groups**, **CPU-n** (one processor in detail) and **Cache-n** (the cache
table of CPU number n). Most items read normal Linux files and need nothing special. **MSRs** needs root
data: run the srs-root helper once (Menu -> Run as root) and SRS shows the cached result. Several AMD
items need the optional `ryzen_smu`, `zenergy` or `k10temp` drivers. Many pages start with a title line
("srs-gui - ... on \\MYPC - CachyOS ...") and some end with the motherboard name and BIOS version.

## Contents
- [AMD Ryzen Power Management](#amd-ryzen-power-management) `amd-cpu`
- [AMD Fuses](#amd-fuses) `amd-fus`
- [AMD PM Table](#amd-pm-table) `amd-pmt`
- [AMD PM Table 0](#amd-pm-table-0) `amd-pmt-0`
- [AMD PM table data @ second package/CCD](#amd-pm-table-data--second-packageccd) `amd-pmt-1`
- [AMD SMU](#amd-smu) `amd-smu`
- [AMD Telemetry 0](#amd-telemetry-0) `amd-tel-0`
- [AMD SVI telemetry plane @ second die](#amd-svi-telemetry-plane--second-die) `amd-tel-1`
- [AMD Thermal 0](#amd-thermal-0) `amd-thm-0`
- [AMD THM registers @ second die](#amd-thm-registers--second-die) `amd-thm-1`
- [AMD Temperatures](#amd-temperatures) `amd-tmp`
- [CPU Table](#cpu-table) `cpu`
- [CPU Description](#cpu-description) `cpu-bits`
- [CPU Clocks](#cpu-clocks) `cpu-clocks`
- [CPU Export](#cpu-export) `cpu-export`
- [CPUID](#cpuid) `cpuid`
- [CPUID Leaf 0](#cpuid-leaf-0) `cpuid-000`
- [CPU Idle Levels](#cpu-idle-levels) `cpu-levels`
- [CPU OC Data](#cpu-oc-data) `cpu-oc-data`
- [CPU Summary](#cpu-summary) `cpus`
- [CPU SMU](#cpu-smu) `cpu-smu`
- [CPU Speeds](#cpu-speeds) `cpu-speeds`
- [CPU Timers](#cpu-timers) `cpu-timer`
- [CPU Tunables](#cpu-tunables) `cpu-tweak`
- [CPU Voltages](#cpu-voltages) `cpu-volts`
- [CPUID Features](#cpuid-features) `features`
- [Hypervisor](#hypervisor) `hypervisor`
- [Microcode](#microcode) `microcode`
- [MSRs](#msrs) `msrs`
- [MSR Setup](#msr-setup) `msr-setup`
- [IBM/Cyrix MSRs](#ibmcyrix-msrs) `msrs-ibm`
- [NexGen NXOP](#nexgen-nxop) `nexgen-nxop`
- [Nodes](#nodes) `nodes`
- [NUMA](#numa) `numa`
- [NUMA memory speed](#numa-memory-speed) `numa-speeds`
- [CPU power states](#cpu-power-states) `power-states`
- [Processor Features Present](#processor-features-present) `present`
- [Processor](#processor) `processor`
- [Processors](#processors) `processors`
- [Speculation Control](#speculation-control) `speculation`
- [Turbo Boost](#turbo-boost) `turbo-boost`
- [Xeon PIRs](#xeon-pirs) `xeon-pir`
- [Cache Levels](#cache-levels) `cache`
- [CPU Groups](#cpu-groups) `group`

---
## AMD Ryzen Power Management  
`amd-cpu` - Linux data

**In plain words.** How the Linux `amd_pstate` driver controls the speed of each core on a modern AMD
Ryzen CPU, and which cores the chip itself says are its best ones.

**What you see.**
- **Mode** - the amd_pstate mode: `active` (the CPU picks its own speed inside limits the OS sets),
  `guided`, or `passive` (the OS picks the speed). Tells you who is really in charge of the clock.
- A table, one row per logical CPU:

| Column | Meaning | Why it matters |
|---|---|---|
| CPU | Linux CPU number (CPU-0, CPU-1 ...) | Which thread the row is about. |
| Highest perf | The top CPPC performance level this core can reach (a unitless number) | Higher means this core can boost higher. |
| Ranking | The "preferred core" rank the CPU reports | The scheduler puts busy work on the highest-ranked cores first. |
| Max freq | Highest clock the driver will allow | Your real boost ceiling. |
| Nonlinear | Lowest non-linear frequency | Below this speed, saving power gets much less efficient. |
| EPP | Energy Performance Preference hint (`performance`, `balance_performance`, `balance_power`, `power`) | Tells the CPU how much to favour speed over power saving. |
| HW prefcore | Whether the hardware supports preferred-core ranking | If `disabled`, all cores are treated as equal. |

**Healthy / typical values.** On the author's Ryzen 7 7800X3D: Mode `active`, Highest perf 166 on every
core, Max freq 5.05GHz, Nonlinear 2.98GHz, EPP `performance`, HW prefcore `enabled`. Rankings run from
166 to 196; CPU-1/CPU-9 and CPU-5/CPU-13 (the two threads of the same core) share the top rank 196.

**Unusual values and what they suggest.**
- The page says "amd_pstate driver not active": the older `acpi-cpufreq` driver is in use, or the CPU is
  not an AMD Zen 2 or newer. Add `amd_pstate=active` (or `guided`) to the kernel command line.
- Every Ranking is the same: preferred-core support is off in the BIOS or kernel.
- Max freq well below the advertised boost: boost is off, or a power or temperature limit is set.

**Where Linux gets it.** `/sys/devices/system/cpu/amd_pstate/status` and
`/sys/devices/system/cpu/cpuN/cpufreq/amd_pstate_*` plus `energy_performance_preference`.

**Background.** CPPC (Collaborative Processor Performance Control) is an ACPI standard. Instead of a
few fixed speed steps, the CPU offers a smooth scale of "performance levels" and the OS gives it a target
range. The CPU then picks the exact clock many times per second.

---
## AMD Fuses  
`amd-fus` - Linux data

**In plain words.** Fuses are one-time settings burned into the chip at the factory (which cores are on,
limits, and so on). The fuse registers themselves are not read, but since 2.4 this page shows the
per-core factory values that Linux can read without root: how fast each core can go and AMD's quality
ranking of the cores.

**What you see.**
- **amd_pstate driver** (active, passive, guided), **Preferred cores (kernel)** and **Preferred cores
  (CPU)** - whether Linux and the CPU use the core ranking.
- **Per-core factory values** - one row per logical CPU: **CPU**, **Core** (`-` when the core id is
  missing), **Highest perf** (the highest performance level), **Ranking** (the preferred-core ranking),
  **Max MHz** and **Lowest non-linear MHz**.
- **Best ranked CPUs** - the CPUs with the highest ranking, and the value.
- A plain-English note: the ranking is the factory quality order of the cores, set by AMD when the CPU
  was made and reported by the firmware; a higher number means the core reaches higher clocks, and
  Linux uses it to put busy tasks on the best cores ("preferred cores").
- **Fuse registers** - "not read": the physical fuse registers (CCD and core disable, part options)
  need SMN access.

**Healthy / typical values.** On the author's 7800X3D: amd_pstate active, preferred cores enabled;
highest perf 166 on every core, rankings 166-196, best ranked CPUs 1, 5, 9, 13 (196); max 5053 MHz,
lowest non-linear 2983 MHz. Both threads of a core share its ranking.

**Unusual values and what they suggest.** "Per-core data: not available": the CPU is not run by the
amd_pstate driver (older CPUs, or `amd_pstate=disable`). All rankings equal, or preferred cores "disabled": the BIOS
or the kernel turned the feature off (`amd_prefcore=disable`), or the CPU does not report it.

**Where Linux gets it.** `/sys/devices/system/cpu/cpu*/cpufreq/amd_pstate_highest_perf`,
`amd_pstate_prefcore_ranking`, `amd_pstate_max_freq`, `amd_pstate_lowest_nonlinear_freq`,
`amd_pstate_hw_prefcore`, `/sys/devices/system/cpu/amd_pstate/status` and `prefcore` (no root). The fuse registers would need
`/sys/kernel/ryzen_smu_drv/smn` (root).

---
## AMD PM Table  
`amd-pmt` - Linux data

**In plain words.** A raw dump of the power-management table that the CPU's own little control
processor (the SMU) keeps up to date: powers, clocks, voltages, temperatures and limits, all as numbers.

**What you see.**
- A summary line: table size in bytes, number of values, and a reminder that the meaning of each slot
  depends on the table version (shown in [AMD SMU](#amd-smu)).
- Then rows of four values each:
  - **Offset** (purple, hex) - the byte position in the table, e.g. `0000`, `0010`.
  - **[n]** - the slot number (0, 1, 2 ...).
  - **value** - that slot read as a 32-bit floating-point number. Values that are not real numbers or
    are huge are shown grey, because they are almost certainly not a float at all.

**Healthy / typical values.** On the author's 7800X3D: 1704 bytes, 426 values, table version
0x00540104. You can spot patterns: slots around 5.25 look like clock limits in GHz, slots near 1.1-1.5
look like voltages, slots in the 40s-50s change with load and look like temperatures or watts. The
values change every time you open the page because the SMU updates them constantly.

**Unusual values and what they suggest.**
- "ryzen_smu pm_table not available": the `ryzen_smu` kernel module is not installed or not loaded.
- "pm_table not readable as this user": the file permissions on your system only let root read it.
- All zeros: the driver loaded but the SMU has not filled the table yet, or the table version is not
  supported by the driver.

**Where Linux gets it.** `/sys/kernel/ryzen_smu_drv/pm_table` (from the out-of-tree `ryzen_smu` driver).

**Background.** The SMU (System Management Unit) is a small processor inside every Ryzen CPU. It runs
its own firmware and makes the real decisions about boost clocks, voltages and power limits. Tools like
Ryzen Master read this table to show live data. AMD does not publish the layout, so tools map it by
version.

---
## AMD PM Table 0  
`amd-pmt-0` - Linux data

**In plain words.** The same power-management table as [AMD PM Table](#amd-pm-table), for the first
(and on most desktops the only) CPU package.

**What you see.** Exactly the same layout as [AMD PM Table](#amd-pm-table): summary line, then offset,
slot number and float value, four per row. Because the table is live, two openings a moment apart show
slightly different numbers (on the author's PC slot [3] read 42.69 in one capture and 55.19 in the next).

**Where Linux gets it.** `/sys/kernel/ryzen_smu_drv/pm_table`.

---
## AMD PM table data @ second package/CCD  
`amd-pmt-1` - Not present on most PCs

**In plain words.** The power-management table of a second CPU package or die. Desktop Ryzen has one
package and the driver gives one table, so there is nothing to show.

**What you see.** **Status** `Not present on this system` and **Checked**, the source SRS looked at
(`/sys/kernel/ryzen_smu_drv/pm_table (single CCD/package)`). "Not present" is normal on single-socket PCs.

---
## AMD SMU  
`amd-smu` - Linux data - Needs root data

**In plain words.** Version information about the SMU (the CPU's built-in power-management processor)
and the `ryzen_smu` driver that talks to it. SRS shows this part without root; the root flag is for the
SMU mailbox commands, which SRS never sends.

**What you see.**

| Field | Meaning | Why it matters |
|---|---|---|
| Driver version | Version of the `ryzen_smu` driver (`drv_version`) | Old drivers may not know new CPUs. |
| Codename | The driver's number for your CPU generation (`codename`) | Tells the driver which table layout to use. |
| SMU firmware version | SMU firmware version (the driver's `version` file), e.g. `4.84.4.0` | Changes with BIOS (AGESA) updates. |
| MP1 interface version | Version of the MP1 mailbox interface (`mp1_if_version`) | Driver/firmware compatibility. |
| PM table version | Layout version of the PM table, in hex and decimal (`pm_table_version`, a binary 32-bit value) | You need this to know what each [PM table](#amd-pm-table) slot means. |
| PM table size | Size of the PM table in bytes (`pm_table_size`, a binary 64-bit value) | Should match the table dump. |

A closing note says SRS is read-only and never sends SMU commands.

**Healthy / typical values.** On the author's 7800X3D: drv_version 0.1.7, codename 20, mp1_if_version 2,
pm_table_version 0x00540104 (5505284), pm_table_size 1704. A field shows `-` when the driver does not
provide it.

**Unusual values and what they suggest.** "ryzen_smu driver not loaded": install and load the
`ryzen_smu` module (it is not part of the normal kernel).

**Where Linux gets it.** Files in `/sys/kernel/ryzen_smu_drv/`.

---
## AMD Telemetry 0  
`amd-tel-0` - Linux data

**In plain words.** How much power each core and the whole CPU package is using right now, worked out
from the CPU's energy counters.

**What you see.** A table with one row per energy counter:
- **Plane** - the counter's label: `Ecore000`, `Ecore001` ... (one per physical core) and `Esocket0`
  (the whole package).
- **Power** - watts, measured by reading the counter twice 200 ms apart.
- **Energy** - total energy counted since the counter started, in kilojoules.

**Healthy / typical values.** On the author's 7800X3D at light load: cores 2.4-4.7 W each, socket
57.86 W. The socket figure is always larger than the cores added together, because it also counts the
cache, memory controller, Infinity Fabric and I/O die.

**Unusual values and what they suggest.**
- "No zenergy / amd_energy hwmon driver loaded": install the `zenergy` module (or the older `amd_energy`).
- One core much higher than the rest at idle: a background task is pinned to it.
- Socket power high while every core is low: often memory or fabric overclocking, or many devices
  keeping the I/O die busy.

**Where Linux gets it.** The `zenergy` or `amd_energy` hwmon driver, `/sys/class/hwmon/hwmonN/energy*_input`
(these read AMD's RAPL energy counters).

**Background.** RAPL (Running Average Power Limit) counters add up energy used, in tiny steps. Power is
energy divided by time, so reading the counter twice and dividing by the gap gives watts.

---
## AMD SVI telemetry plane @ second die  
`amd-tel-1` - Not present on most PCs

**In plain words.** Power and voltage readings for a second die. Single-package PCs have no second plane.

**What you see.** **Status** `Not present on this system` and **Checked** (the source SRS looked at).

**Background.** SVI (Serial VID Interface) is the link the CPU uses to tell the motherboard's voltage
regulator what voltage to give it, and to read back current and voltage.

---
## AMD Thermal 0  
`amd-thm-0` - Linux data

**In plain words.** The CPU's own temperature sensors, read through the `k10temp` driver.

**What you see.** One line per sensor the driver offers, label and temperature in degrees Celsius:
- **Tctl** - the control temperature that the CPU and fan curves use. On some models it includes a fixed
  offset, so it can read higher than the real die.
- **Tdie** - the real die temperature (only on models where it differs from Tctl).
- **Tccd1, Tccd2 ...** - one per CCD (core chiplet), the die that holds the cores.
- Other labels the driver adds are shown as-is; unlabelled ones as `temp1`, `temp2` ...

A note at the end explains Tctl and TccdN.

**Healthy / typical values.** On the author's 7800X3D (one CCD): Tctl 53.6°C, Tccd1 48.1°C. Idle on a
desktop is usually 35-55°C. Under full load an X3D chip normally stays below about 89°C, the point
where it starts holding clocks back.

**Unusual values and what they suggest.**
- Tctl near 90-95°C under load: cooler not seated well, pump failing, or fans not ramping.
- Tctl jumps quickly up and down by 10°C or more: normal for Ryzen at light load (single cores boosting).
- "k10temp not loaded": the module is missing or blocked; some boards also hide it behind another driver.

**Where Linux gets it.** `k10temp` hwmon: `/sys/class/hwmon/hwmonN/temp*_input` and `temp*_label`.

---
## AMD THM registers @ second die  
`amd-thm-1` - Not present on most PCs

**In plain words.** Thermal registers of a second CPU package. A single-socket PC has only one, so
nothing is listed.

**What you see.** **Status** `Not present on this system` and **Checked** (`/sys/bus/pci/drivers/k10temp
(single package)`).

---
## AMD Temperatures  
`amd-tmp` - Linux data

**In plain words.** The same CPU temperature list as [AMD Thermal 0](#amd-thermal-0).

**What you see.** Identical to [AMD Thermal 0](#amd-thermal-0): Tctl, Tdie (if present) and TccdN in °C,
plus the explanatory note. The page refreshes every 2 seconds in the window.

**Where Linux gets it.** `k10temp` hwmon.

---
## CPU Table  
`cpu` - Linux data

**In plain words.** One line per logical CPU showing where it sits (node, package, core, thread), its
IDs, and its current speed settings. The window refreshes it every 2 seconds.

**What you see.**

| Column | Meaning | Why it matters |
|---|---|---|
| CPU | Linux CPU number | The name tools like `taskset` use. |
| Node | NUMA node the CPU belongs to | Matters on multi-node systems (see [NUMA](#numa)). |
| Pkg | Physical package (socket) number | 0 on single-socket PCs. |
| Die | Die number inside the package | Linux topology die ID. |
| Core | Physical core number | Two CPUs with the same Core are SMT siblings. |
| Thread | 0 = first thread of the core, 1 = its SMT sibling | Shows which CPU numbers share a core. |
| Affinity | The CPU's bit in a 64-bit affinity mask, in hex | Useful when setting masks by hand. Shown as zeros above CPU 63. |
| APIC | The CPU's APIC ID in hex | The hardware's own CPU number. |
| Current | Current clock speed | Changes all the time with load. |
| Minimum / Maximum | The lowest / highest clock the OS currently allows | Your active speed limits. |
| Governor | cpufreq governor (`performance`, `powersave`, `schedutil` ...) | The policy that picks speeds. |
| EPP | Energy Performance Preference | See [AMD Ryzen Power Management](#amd-ryzen-power-management). |
| Ranking | amd_pstate preferred-core rank | Higher = better core. |
| Highest | amd_pstate highest perf level | The core's boost potential. |

Empty values show as `-`.

**Healthy / typical values.** On the author's 7800X3D: CPU-0 to CPU-7 are Thread 0 of cores 0-7 and
CPU-8 to CPU-15 are their siblings (Thread 1). APIC IDs are even for first threads (0000, 0002 ...) and
odd for siblings. Minimum and Maximum are both 5.05GHz because the owner pinned the minimum to the
maximum; on a default setup Minimum is much lower (a few hundred MHz).

**Unusual values and what they suggest.**
- Minimum = Maximum: someone set a fixed speed (gaming tweak, tuned profile). Higher idle power.
- Maximum far below the CPU's boost speed: boost disabled or a power-saving profile is active.
- Current shows `-`: no cpufreq driver loaded (common in virtual machines).

**Where Linux gets it.** `/sys/devices/system/cpu/cpuN/topology/`, `cpufreq/`, `/proc/cpuinfo` (apicid).

**Background.** SMT (Simultaneous Multi-Threading, Intel calls it Hyper-Threading) lets one physical core
run two threads at once. Linux shows each thread as its own "CPU".

---
## CPU Description  
`cpu-bits` - Linux data

**In plain words.** SIV shows the CPU description Windows keeps in the registry. On Linux, SRS shows the
same kind of facts straight from `/proc/cpuinfo` for one CPU (CPU-0 unless another number is given).

**What you see.** A label/value list of these `/proc/cpuinfo` fields:

- **model name** - full CPU name. **vendor_id** - `AuthenticAMD` or `GenuineIntel`.
- **cpu family / model / stepping** - the CPU's generation numbers, in decimal.
- **microcode** - microcode revision loaded. **cpu MHz** - clock at the moment of reading.
- **cache size** - the L2 cache per core, as the kernel reports it.
- **physical id / siblings / core id / cpu cores** - package number, threads per package, this core's
  number, cores per package.
- **apicid / initial apicid** - APIC IDs now and at power-on (normally equal).
- **fpu / fpu_exception / wp** - basic x86 features (always `yes` on modern CPUs).
- **cpuid level** - highest standard CPUID leaf.
- **bugs** - known hardware weaknesses the kernel knows about (see [Speculation Control](#speculation-control)).
- **bogomips** - a rough "busy loop" speed number. It is not a benchmark.
- **TLB size** - number of translation buffer entries (AMD only).
- **clflush size / cache_alignment** - cache line size in bytes.
- **address sizes** - physical and virtual address bits.
- **power management** - power features (`ts` thermal sensor, `hwpstate` hardware P-states, `cpb` core
  boost, and so on; unknown bits appear as `[13]`, `[14]`).
- **flags** - every CPU feature the kernel found (long list), wrapped to about 90 characters a line (2.4).
- **vmx flags** - Intel VT-x sub-features; left out when empty, as on AMD (2.4).

**Healthy / typical values.** On the author's 7800X3D: family 25, model 97, stepping 2, microcode
0xa60120c, cache size 1024 KB, 16 siblings, 8 cpu cores, address sizes 48 bits physical / 48 bits virtual,
TLB size 3584 4K pages, bogomips 8820.31.

**Unusual values and what they suggest.** `siblings` equal to `cpu cores`: SMT is off. A `bugs` list
that grows after a kernel update is normal: newly found issues get added.

**Where Linux gets it.** `/proc/cpuinfo`.

---
## CPU Clocks  
`cpu-clocks` - Linux data

**In plain words.** The current, average and allowed clock speed of every CPU. Same page as
[CPU Speeds](#cpu-speeds); it refreshes every second in the window.

**What you see.** The same layout as [CPU Speeds](#cpu-speeds): the driver/boost line and the
per-CPU table (Current, Average, Minimum, Maximum, HW Min, HW Max, Governor, EPP). Below it, since
2.2, **Stock and in effect now**: each clock at stock next to what it is now.

| Row | Stock | In effect now |
|---|---|---|
| Base clock | 100 MHz, the standard reference clock | Measured: the TSC rate x 100 / the CPU's nominal frequency from ACPI CPPC (once per run, 50 ms) |
| Core base | The CPU's nominal frequency (CPPC) | Nominal x base clock / 100 |
| Core boost (max) | cpufreq's maximum (the kernel works it out at 100 MHz) | Maximum x base clock / 100 |
| Core now (fastest) | The same multiplier at 100 MHz | cpufreq's current clock of the fastest core (already real) |
| FCLK, UCLK, MCLK | What the SMU reports | The same: these are shown, not measured |

**Difference** gives MHz and percent where SRS measured. FCLK (Infinity Fabric), UCLK (memory
controller) and MCLK (memory clock) come from the SMU's PM table through the ryzen_smu driver
(read-only), for the Zen 4 desktop layout (Ryzen 7000, ryzen_smu codename 20) and only when the
values make sense; elsewhere the row says why it is missing. Software cannot measure these clocks,
so SRS does not claim a difference: on AM5 an eCLK drives the cores, and whether anything else
follows it depends on the board's eCLK mode. The **Memory** line adds the DDR data rate (2 x MCLK),
the UCLK:MCLK ratio (1:1 or 1:2) and FCLK.

**Healthy / typical values.** With the normal 100 MHz base clock every difference is about 0.
The author's PC runs a 105 MHz eCLK: base clock 105.0 MHz (+5 %), core base 4201 -> 4410 MHz,
boost 5053 -> 5305 MHz, FCLK 2133 MHz, UCLK 1500 MHz, MCLK 3000 MHz (DDR5-6000 at 1:2).

**Where Linux gets it.** cpufreq files in `/sys/devices/system/cpu/cpuN/cpufreq/`, the TSC (timed
against the system clock), `/sys/devices/system/cpu/cpu0/acpi_cppc/nominal_freq`, and
`/sys/kernel/ryzen_smu_drv/pm_table` (floats 70, 74 and 78 of the Zen 4 desktop table).

---
## CPU Export  
`cpu-export` - Linux data

**In plain words.** A tiny set of CPU values that other tools (LCD panels, on-screen displays, scripts)
can read from a file SRS writes.

**What you see.**
- **CPU.Name** - CPU model name.
- **CPU.Clock.Max** - the highest current clock of any CPU at the moment of reading, in MHz.
- **CPU.Count** - number of logical CPUs.
- A note on how to write this to a file: `srs-gui -save=[cpu-export]=FILE`.

**Healthy / typical values.** On the author's PC: AMD Ryzen 7 7800X3D 8-Core Processor, 5115 MHz, 16.

**Where Linux gets it.** `/proc/cpuinfo` and `scaling_cur_freq` of every CPU.

---
## CPUID  
`cpuid` - Linux data

**In plain words.** A raw dump of every answer the CPU gives to the CPUID instruction. This is the
source for most of the other pages in this group. It is for experts and for comparing two machines.

**What you see.** A title line (CPU number, CPU name, current speed), then one row per leaf and sub-leaf:

| Column | Meaning |
|---|---|
| Leaf | The CPUID question number, in hex. 00000000-000000xx are standard leaves, 80000000-800000xx are extended leaves. |
| Sub | Sub-leaf number, for leaves that have several pages (cache, topology, XSAVE ...). Empty extra sub-leaves are skipped. |
| EAX, EBX, ECX, EDX | The four 32-bit registers of the answer, in hex. |
| ASCII | The same 16 bytes as text, in register order EAX-EBX-ECX-EDX. Non-printable bytes show as `.`. |

The ASCII column makes text answers readable: leaves 80000002-80000004 spell the brand name. In leaf 0
the vendor text looks scrambled ("AuthcAMDenti") because the CPU stores it in the order EBX-EDX-ECX.

**Healthy / typical values.** On the author's 7800X3D: highest standard leaf 00000010, highest extended
leaf 80000028, leaf 1 EAX 00A60F12. Many leaves are all zero: that just means "not used by this CPU".

**Unusual values and what they suggest.** Leaf 1 ECX bit 31 set (hypervisor bit) means you are inside a
virtual machine. Very few leaves: an old CPU or a hypervisor that hides features.

**Where Linux gets it.** The CPUID instruction itself, run with the thread pinned to the chosen CPU
(argument = CPU number).

**Background.** CPUID is an x86 instruction. Software puts a number (the leaf) in a register and the CPU
answers in four registers with facts about itself: vendor, family, features, caches, topology.

---
## CPUID Leaf 0  
`cpuid-000` - Linux data

**In plain words.** The CPU's basic identity card from CPUID: who made it, its name and its exact
generation.

**What you see.**
- **Maximum standard leaf** - highest normal CPUID leaf (hex).
- **Vendor string** - 12-letter maker code: `AuthenticAMD`, `GenuineIntel`, `HygonGenuine` ...
- **Maximum extended leaf** - highest extended leaf (hex).
- **Brand string** - the marketing name, built into the chip.
- **Signature (leaf 1 EAX)** - the raw hex signature, then the decoded family, model and stepping (decimal).

**Healthy / typical values.** On the author's PC: 00000010, AuthenticAMD, 80000028,
"AMD Ryzen 7 7800X3D 8-Core Processor", 00A60F12 = family 25 model 97 stepping 2.

**Where Linux gets it.** CPUID leaves 0, 1, 80000000 and 80000002-80000004.

**Background.** Family 25 (19h) is AMD Zen 3 and Zen 4; model 97 (61h) is the Zen 4 "Raphael" desktop
chip. Stepping is the silicon revision.

---
## CPU Idle Levels  
`cpu-levels` - Linux data

**In plain words.** The sleep levels (C-states) each core can drop into when it has nothing to do, and
how much each level has been used since boot.

**What you see.**
- **Driver** - the cpuidle driver (`acpi_idle`, `intel_idle`, `amd_idle` ...) and its **governor** (the
  policy that picks a level: `menu`, `teo`, `ladder`, `haltpoll`).
- A table with one block per CPU, one row per state:

| Column | Meaning | Why it matters |
|---|---|---|
| CPU | Shown on the first state of each CPU | Group header. |
| State | State name: POLL, C1, C2, C3 ... | Deeper = more power saved, slower to wake. |
| Description | What the state does (e.g. `ACPI HLT`, `ACPI IOPORT 0x414`) | Shows how the state is entered. |
| Latency | Time to wake from it, microseconds | Deep states add this delay to the first work after idle. |
| Residency | Minimum time worth staying in it, microseconds | The governor skips a state if the idle gap is shorter. |
| Usage | How many times it was entered | Shows which states really get used. |
| Time | Total time spent in it, seconds | Where the core spends its idle time. |

**Healthy / typical values.** On the author's 7800X3D: driver `acpi_idle`, governor `teo`; states POLL
(0us), C1 HLT (0us), C2 (18us latency, 36us residency) and C3 (350us, 700us). Most idle time is in C2
and C3, which is what you want for low idle power.

**Unusual values and what they suggest.**
- Only POLL and C1: deep C-states are off in the BIOS ("Global C-state Control"), or the kernel was booted
  with `processor.max_cstate=1` / `idle=poll`. Idle power and temperature go up.
- C3 Usage stays 0: the state is disabled (see [CPU power states](#cpu-power-states)) or wake-ups are too
  frequent for it.

**Where Linux gets it.** `/sys/devices/system/cpu/cpuidle/` and `/sys/devices/system/cpu/cpuN/cpuidle/stateM/`.

**Background.** A C-state is an idle power level. C0 is "running". C1 stops the clock to the core, deeper
ones also cut voltage and flush caches. Waking from a deep state takes longer, which is why
latency-sensitive setups sometimes limit them.

---
## CPU OC Data  
`cpu-oc-data` - Not present on most PCs - Needs root data

**In plain words.** Intel CPUs have an "overclocking mailbox" register that reports and sets
overclocking limits. AMD CPUs do not have it.

**What you see.** **Status** `Not present on this system` and **Reason**: IA32_OC_MAILBOX is an Intel
interface; on AMD look at [AMD Ryzen Power Management](#amd-ryzen-power-management) and
[Turbo Boost](#turbo-boost) instead.

---
## CPU Summary  
`cpus` - Linux data

**In plain words.** A short overview of the processor: name, maker, generation, how many cores and
threads, and which speed driver Linux uses.

**What you see.**
- **Processor** - full model name. **Vendor** - maker code.
- **Family** - family, model and stepping in decimal with hex in brackets.
- **Topology** - packages, cores, logical processors, and threads per core.
- **Microcode** - loaded microcode revision. **Address sizes** - physical/virtual address bits.
- **BogoMIPS** - rough loop speed (not a benchmark). **TLB size** - AMD translation buffer size.
- **Power management** - power feature list from `/proc/cpuinfo`.
- **Frequency driver** - the cpufreq driver: `amd-pstate-epp`, `amd-pstate`, `acpi-cpufreq`,
  `intel_pstate` ...
- Board name and BIOS line at the bottom.

**Healthy / typical values.** On the author's PC: AMD Ryzen 7 7800X3D, Family 25 (0x19) Model 97 (0x61)
Stepping 2, "1 package, 8 cores, 16 logical processors (2 per core)", Microcode 0xa60120c, Frequency
driver amd-pstate-epp.

**Unusual values and what they suggest.** "1 per core" on a CPU that has SMT: SMT is off in the BIOS or
with `nosmt`. Frequency driver `acpi-cpufreq` on a Zen 2+ Ryzen: amd_pstate is not enabled.

**Where Linux gets it.** `/proc/cpuinfo`, `/sys/devices/system/cpu/cpuN/topology/`, `scaling_driver`.

---
## CPU SMU  
`cpu-smu` - Linux data - Needs root data

**In plain words.** The same SMU version page as [AMD SMU](#amd-smu). SIV uses this item for SMU
mailbox results; SRS shows the read-only version data and never sends mailbox commands.

**What you see.** Identical to [AMD SMU](#amd-smu): driver version, codename, SMU firmware version, MP1
interface version, PM table version and size, and the read-only note.

**Where Linux gets it.** `/sys/kernel/ryzen_smu_drv/`.

---
## CPU Speeds  
`cpu-speeds` - Linux data

**In plain words.** The clock speed of every CPU right now, plus the limits that control it. The window
refreshes it every second.

**What you see.**
- **Driver** - the cpufreq driver and whether **boost** is on, off, or `-` (unknown).
- A per-CPU table:

| Column | Meaning | Why it matters |
|---|---|---|
| CPU | Linux CPU number | |
| Current | Clock the kernel last saw | Jumps around with load. |
| Average | Average clock over a short recent window (needs a recent kernel) | Smoother than Current. |
| Minimum / Maximum | Allowed range set by the OS (scaling limits) | What policy currently allows. |
| HW Min / HW Max | Range the hardware supports | The absolute floor and ceiling. |
| Governor | Speed policy | `performance` = always aim high; `powersave` under amd-pstate-epp still boosts but follows EPP. |
| EPP | Energy Performance Preference | Speed vs power hint. |

**Healthy / typical values.** On the author's 7800X3D: driver amd-pstate-epp, boost on; Current 4.2-5.1GHz
under light load; HW Min 426MHz, HW Max 5.05GHz. Current can read a little above Maximum (5.11GHz vs
5.05GHz) because of rounding and a raised reference clock.

**Unusual values and what they suggest.**
- Every core stuck at a low Current under load: a thermal or power limit, or the `powersave` governor
  with `power` EPP.
- HW Max equal to the base clock: boost off in the BIOS.
- All columns `-`: no cpufreq driver (virtual machine, or the driver failed to load).

Since 2.2 the same **Stock and in effect now** table as [CPU Clocks](#cpu-clocks) follows the
per-CPU table (both items share one page).

**Where Linux gets it.** `/sys/devices/system/cpu/cpuN/cpufreq/` (`scaling_*`, `cpuinfo_*`,
`cpuinfo_avg_freq`) and `/sys/devices/system/cpu/cpufreq/boost`.

**Background.** cpufreq is the Linux part that sets CPU speed. The *driver* talks to the hardware; the
*governor* decides which speed to ask for.

---
## CPU Timers  
`cpu-timer` - Linux data

**In plain words.** Which hardware clock Linux uses to keep time, and how fast the CPU's own time
counter (the TSC) runs.

**What you see.**
- **Clock source** - the timer Linux uses now (`tsc`, `hpet`, `acpi_pm`).
- **Available** - all usable timers.
- **TSC frequency** - TSC rate measured by SRS over 50 ms, in MHz.
- **TSC properties** - check boxes: **constant** (same rate at any CPU speed), **nonstop** (keeps
  counting in deep sleep), **known freq** (the kernel was told the exact rate), **RDTSCP** (the
  instruction that also returns the CPU number is available).
- **CLOCK_MONOTONIC res** - resolution of the system's steady clock, in nanoseconds.
- **Kernel HZ (USER_HZ)** - the tick unit the kernel uses when reporting times to programs.
- **Clock event** - the device that fires timer interrupts (`lapic`, `hpet` ...).

**Healthy / typical values.** On the author's PC: clock source `tsc`; available `tsc hpet acpi_pm`;
TSC 4409.857 MHz; constant, nonstop and RDTSCP ticked; resolution 1 ns; USER_HZ 100; clock event
`lapic`. The 7800X3D's TSC is normally 4.20 GHz; 4.41 GHz here comes from the board's reference clock
being raised to 105 MHz (4.20 x 1.05 = 4.41).

**Unusual values and what they suggest.**
- Clock source `hpet` or `acpi_pm`: the kernel decided the TSC is unstable (look in `dmesg` for
  "clocksource tsc unstable"). Timing calls get much slower; games can stutter.
- TSC frequency very different from the base clock with no reference-clock change: a virtual machine, or
  firmware bug.

**Where Linux gets it.** `/sys/devices/system/clocksource/clocksource0/`, `/sys/devices/system/clockevents/`,
`/proc/cpuinfo` flags, and SRS's own timing with the RDTSC instruction.

**Background.** The TSC (Time Stamp Counter) is a counter inside the CPU that goes up at a fixed rate.
Reading it is very cheap, which makes it the best clock for an OS when it is stable.

---
## CPU Tunables  
`cpu-tweak` - Linux data - Needs root data

**In plain words.** A read-only view of the CPU settings you can change in Linux: speed policy, boost,
SMT and CPU isolation. SRS only shows them; changing them needs root and a tool such as tuned or cpupower.

**What you see.** (each line appears only if the file exists)
- **Governor (cpu0)** and **Available governors**.
- **EPP (cpu0)** and **Available EPP** values.
- **Boost** - `on` or `off`.
- **SMT control** - `on`, `off`, `forceoff` or `notsupported`. **SMT active** - 1 if SMT is running.
- **Isolated CPUs** - CPUs kept away from normal tasks (`isolcpus=`), or `none`.
- **nohz_full CPUs** - CPUs that run without the regular timer tick, or `none`.
- **sched_autogroup** - 1 if the scheduler groups tasks by session (better desktop feel under load).
- A note: changes are left to tuned / cpupower / LACT.

**Healthy / typical values.** On the author's PC: governor `performance` (available: performance,
powersave), EPP `performance` (the only option offered while the performance governor is active),
boost on, SMT on and active, no isolated CPUs, sched_autogroup 1.

**Unusual values and what they suggest.** SMT control `forceoff`: the kernel was booted with
`nosmt=force`, usually for security. Only `performance` in Available EPP: that is normal while the
performance governor is selected; switch to `powersave` to see the full list.

**Where Linux gets it.** `/sys/devices/system/cpu/cpu0/cpufreq/`, `/sys/devices/system/cpu/smt/`,
`/sys/devices/system/cpu/isolated`, `nohz_full`, `/proc/sys/kernel/sched_autogroup_enabled`.

---
## CPU Voltages  
`cpu-volts` - Linux data

**In plain words.** The CPU core voltage as measured by the motherboard's sensor chip.

**What you see.** One line per voltage input of a supported motherboard sensor chip (Nuvoton `nct*`,
ITE `it87*`, ASUS `asus*`, or `zenpower`): chip name, input label, and volts with three decimals.
`in0` is shown as "Vcore sense" when it has no label; `in1` is shown too; other unlabelled inputs are
skipped. A note says per-core voltage (VID) is not available from Linux drivers, so the board reading
is shown.

**Healthy / typical values.** On the author's PC: nct6799 in0 (Vcore sense) 1.096 V, in1 1.000 V.
Ryzen 7000 Vcore usually sits between about 0.9 and 1.35 V and moves constantly with load and boost.

**Unusual values and what they suggest.**
- Nothing listed: no matching sensor driver is loaded (for many boards you need `nct6775` or `it87`).
- Vcore above about 1.40 V at all-core load: a manual overvolt or aggressive BIOS setting; check heat.
- A reading of 0 or a fixed, unchanging odd value: that input is not wired to Vcore on your board.

**Where Linux gets it.** Super-I/O hwmon drivers, `/sys/class/hwmon/hwmonN/in*_input` and `in*_label`.

**Background.** Vcore is the main supply voltage of the CPU cores. The CPU asks the board's voltage
regulator for a voltage (VID); the board's sensor measures what actually arrives.

---
## CPUID Features  
`features` - Linux data

**In plain words.** A checklist of 177 well-known CPU features (instruction sets, security and
virtualization features), each ticked if CPU-0 reports it.

**What you see.**
- A count line: "N of 177 features present on CPU-0 (CPU name)".
- Two columns of **Feature** (short name) and **CPUID Feature Description** with a tick box. Present
  features are in normal colour; missing ones are grey.

The list covers standard leaf 1 (e.g. SSE, SSE2, SSE4.2, AVX, AES, RDRAND), leaf 6 (thermal/turbo),
leaf 7 (AVX2, AVX-512 family, BMI1/2, SHA, SMEP/SMAP, CET, speculation controls), and AMD extended leaves
(SVM, SSE4A, LM 64-bit mode, NX, 1GB pages, Invariant TSC, CPB boost, NPT, AVIC, SME/SEV).

**Healthy / typical values.** On the author's 7800X3D: 120 of 177. AVX2 and most AVX-512 parts are
present (Zen 4 has AVX-512). Intel-only items (VMX, SGX, HLE, RTM, EIST) and old AMD items (3DNow!, FMA4,
XOP) are correctly absent. The leaf-7 IBRS/STIBP/SSBD lines are Intel's bits; AMD reports the same
protections in the `AMD_IBRS`, `AMD_STIBP`, `AMD_SSBD` lines, which are ticked.

**Unusual values and what they suggest.**
- AVX or AVX2 missing on a CPU that should have it: inside a VM with a basic CPU model, or the OS has
  disabled the AVX state (OSXSAVE unticked).
- HVM ticked: you are running under a hypervisor.
- SVM/VMX present but virtualization tools fail: it is turned off in the BIOS (CPUID still shows it).

**Where Linux gets it.** The CPUID instruction (leaves 1, 6, 7, 80000001, 80000007, 80000008,
8000000A, 8000001F).

**Background.** Programs check these bits before using special instructions. If a game says "your CPU
does not support AVX2", this page tells you whether that is true.

---
## Hypervisor  
`hypervisor` - Linux data

**In plain words.** Can this CPU run virtual machines well, and is this system itself a virtual machine?

**What you see.** A list of tick boxes for CPU-0:
- **SLAT** - Second Level Address Translation (Intel EPT or AMD RVI): fast memory handling for VMs.
- **HVM** - ticked if this OS is running under a hypervisor.
- **VT-x** and **EPT** - Intel's virtualization and its page tables.
- **AMD-V** (SVM), **RVI** (nested paging), **AVIC** (virtual interrupt controller) and **SEV** (encrypted VMs).

Then:
- **/dev/kvm** - `present (KVM available)` or `absent`.
- **IOMMU** - number of IOMMU groups, or `disabled / none`.
- **kvm_amd nested** - 1 if VMs inside VMs are allowed (AMD only).
- Board/BIOS line.

**Healthy / typical values.** On the author's PC: SLAT, AMD-V, RVI and AVIC ticked; SEV and HVM not;
/dev/kvm present; 36 IOMMU groups; nested 1. That is a fully working KVM host.

**Unusual values and what they suggest.**
- AMD-V or VT-x unticked on a CPU that has it: virtualization (SVM Mode / Intel VT) is probably off in
  the BIOS. On some systems the flag stays ticked even when the BIOS has it off; then `dmesg` shows
  "disabled by BIOS" when KVM loads.
- /dev/kvm absent while AMD-V is ticked: the `kvm_amd` / `kvm_intel` module is not loaded.
- IOMMU `disabled / none`: IOMMU off in the BIOS or kernel; GPU passthrough to a VM will not work.
- SEV unticked on a desktop Ryzen: normal; SEV is an EPYC server feature.

**Where Linux gets it.** `/proc/cpuinfo` flags, `/dev/kvm`, `/sys/kernel/iommu_groups/`,
`/sys/module/kvm_amd/parameters/nested`.

**Background.** An IOMMU (Input-Output Memory Management Unit) controls which memory each device may
reach. IOMMU groups are the smallest sets of devices that can be handed to a VM together.

---
## Microcode  
`microcode` - Linux data

**In plain words.** The microcode version loaded on each CPU. Microcode is a small firmware patch for
the processor, loaded by the BIOS or by Linux at boot.

**What you see.** A two-column table: **CPU** and **Version** (hex). `-` if the kernel does not report it.

**Healthy / typical values.** All CPUs show the same version. On the author's PC: 0xa60120c on all 16.

**Unusual values and what they suggest.**
- Different versions on different CPUs: a failed or partial late update; reboot.
- A version older than your BIOS or distro offers: install your distro's `amd-ucode` / `intel-ucode`
  package, or update the BIOS. New microcode fixes bugs and security issues.

**Where Linux gets it.** `/sys/devices/system/cpu/cpuN/microcode/version`.

---
## MSRs  
`msrs` - Linux data - Needs root data

**In plain words.** The raw values of a set of the CPU's internal control registers, for every CPU.
Only root can read them, so SRS shows what the srs-root helper saved.

**What you see.** Without root data: **Status** `Needs root` and **Source** (`/dev/cpu/*/msr`, read by
srs-root via Menu > Run as root). With root data: a table with **MSR** (register address, hex), **Name**,
and one column per CPU with the 64-bit value in hex (`n/a` if unreadable). The registers are:

| Name | What it holds |
|---|---|
| APIC_BASE | Where the interrupt controller lives; bit 8 marks the boot CPU. |
| MPERF / APERF | Two counters: fixed-rate and actual-rate cycles. Their ratio is the true average clock. |
| EFER | Extended features in use (64-bit mode, NX, SVM enabled). |
| HWCR | AMD hardware configuration (boost lock, TSC behaviour ...). |
| PSTATE_CURRENT_LIMIT / CONTROL / STATUS | Legacy P-state limit, requested and current state. |
| PSTATE_DEF_0..2 | Definitions of the classic P-states (clock multiplier and voltage ID). |
| HW_PSTATE_STATUS | The hardware's current effective P-state. |
| CPPC_CAP1 / CPPC_ENABLE / CPPC_REQUEST | CPPC capabilities (highest/nominal/lowest perf), on/off, and the OS request (min/max/EPP). |
| RAPL_PWR_UNIT | Units for the energy counters. |
| CORE_ENERGY_STAT / PKG_ENERGY_STAT | Raw energy counters for the core and the whole package. |

Under the table, **What the registers are** explains each one in plain English (2.4), and a note
points at [MSR Setup](#msr-setup) for the decoded values.

**Healthy / typical values.** On the author's PC most registers are identical on every CPU (EFER
0000000000009d01, CPPC_ENABLE 1). APIC_BASE differs only on CPU-0 (the boot CPU). MPERF, APERF, energy
counters and CPPC_CAP1 differ per CPU, which is expected.

**Unusual values and what they suggest.** All `n/a`: the `msr` kernel module is not loaded (see
[MSR Setup](#msr-setup)), or Secure Boot / kernel lockdown blocks MSR access.

**Where Linux gets it.** `/dev/cpu/N/msr` (the `msr` kernel module), read by srs-root and cached.

**Background.** An MSR (Model Specific Register) is a CPU control register reached with the RDMSR and
WRMSR instructions. They hold settings and counters that normal programs never see. SRS only reads them.

---
## MSR Setup  
`msr-setup` - Linux data - Needs root data

**In plain words.** How SRS gets at the CPU's model-specific registers (MSRs), and what the ones it
read mean, in plain English. Everything comes from the root cache that **Menu > Run as root** writes
once (`~/.cache/srs/root/msrs.txt`); SRS never writes any register.

**What you see.**
- **msr driver** - built into the kernel or a module, and whether `/dev/cpu/N/msr` is there.
- **cpuid driver**, **Running as root now**, **Access** (root only, read through srs-root, never written).
- **MSRs last read** (the time in `collected.txt`), **Cache** (the file), **In the cache** ("18 registers x
  16 CPUs" - every raw value is on the **MSRs** item), **Refresh** (Menu > Run as root). No cache: how to get it.
- **Decoded** (CPU 0, raw hex in brackets): each **P-state** (enabled or not, and its MHz from AMD's
  PStateDef: CpuFid[7:0] / CpuDfsId[13:8] x 200), **Core Performance Boost** (HWCR CpbDis, bit 25),
  **AMD-V (SVM) in EFER** (SVME bit 12: on = the kernel or a hypervisor has enabled SVM), **CPPC**
  (CPPC_ENABLE), the **RAPL** energy and time units, and (2.4) **Package power** and **Core power**:
  srs-root reads the energy counters twice, one second apart (`rapl.txt`), so the page shows the watts
  of the whole package and of each CPU while Run as root ran - a little above idle, since Run as root
  itself was busy. Live power: Power & Cost, Sensors > Power status.
- **CPPC capabilities per CPU** - CPPC_CAP1's highest, nominal, lowest nonlinear and lowest performance
  for every CPU, highest first with a rank: the CPUs the scheduler prefers (amd_pstate preferred cores).
- Voltages are not shown: the VID encoding of newer AMD CPUs (SVI3) could not be checked.

**Healthy / typical values.** On the author's 7800X3D (family 19h): P0 4200 MHz (`0x800000004ab248a8`),
P1 3000 MHz, P2 not used; boost on; SVM on; CPPC enabled; package 38.4 W, cores 18 W in all while Run
as root ran; CPU0 181 / 138 / 98 / 14, the best cores 196.

**Unusual values and what they suggest.** Boost off: BIOS "Core Performance Boost" or a tool turned it
off. No cache: run Menu > Run as root once. `module not loaded`: srs-root loads it; kernel lockdown
(Secure Boot) can block it.

**Where Linux gets it.** `/dev/cpu/N/msr` (read by srs-root as root), `/lib/modules/<kernel>/modules.builtin`,
the cache `~/.cache/srs/root/msrs.txt`, `rapl.txt` (AMD: PKG_ENERGY_STAT and CORE_ENERGY_STAT read twice,
one second apart) and `collected.txt`.

---
## IBM/Cyrix MSRs  
`msrs-ibm` - Not present on most PCs - Needs root data

**In plain words.** SIV can show the special registers of old IBM and Cyrix x86 CPUs (1990s). Modern
AMD and Intel CPUs do not have them.

**What you see.** **Status** `Not present on this system` and **Checked** `n/a (non-AMD MSR set)`.
"Not present" is the expected result on any modern PC.

---
## NexGen NXOP  
`nexgen-nxop` - Not present on most PCs

**In plain words.** NexGen made x86 CPUs in the mid-1990s (their design became the AMD K6). SIV can show
their special NxOP data. No current PC has one.

**What you see.** **Status** `Not present on this system` and **Checked** `n/a (NexGen CPU)`.

---
## Nodes  
`nodes` - Linux data

**In plain words.** The same NUMA node table as [NUMA](#numa).

**What you see.** Identical to [NUMA](#numa): Node, CPUs, Memory, Free, Distances.

**Where Linux gets it.** `/sys/devices/system/node/`.

---
## NUMA  
`numa` - Linux data

**In plain words.** How memory and CPUs are grouped into nodes. Almost every desktop has one node, so
all CPUs reach all memory at the same speed.

**What you see.**

| Column | Meaning | Why it matters |
|---|---|---|
| Node | Node name (`node0`, `node1` ...) | |
| CPUs | CPUs in this node | Programs run fastest using memory from their own node. |
| Memory | RAM attached to this node | Should roughly match your installed RAM split across nodes. |
| Free | RAM free on this node right now | One full node can slow programs even if total free RAM is fine. |
| Distances | Relative cost to reach each node (10 = local) | 20+ means going through another chip. |

**Healthy / typical values.** On the author's PC: one node, `node0`, CPUs 0-15, 30.45GB memory, 3.44GB
free at that moment, distance 10. Low Free RAM is normal on Linux because spare memory is used as cache.

**Unusual values and what they suggest.** Several nodes on a desktop: the BIOS has a "NPS" or
"memory interleave" option set to split memory (some Threadripper/EPYC settings). Memory 0 on a node that
has CPUs: a memory channel is empty.

**Where Linux gets it.** `/sys/devices/system/node/nodeN/cpulist`, `meminfo`, `distance`.

**Background.** NUMA (Non-Uniform Memory Access) means memory is attached to particular CPUs. Reaching
"far" memory takes longer. Multi-socket servers and some large CPUs are NUMA; normal desktops are not.

---
## NUMA memory speed  
`numa-speeds` - Linux data

**In plain words.** SIV measures memory speed per NUMA node. SRS shows the node count and the result of
the last memory latency test, and (2.3.1) lets you run that test right here: in the new main window the
tab carries the **Memory Latency** controls (**Largest block**, **Run**), with the same question before
it starts, the load test terms and one test at a time. It never runs the test by itself.

**What you see.**
- **NUMA nodes** - count; with one node it adds "(uniform memory access - every CPU sees the same
  latency)".
- **Last Latency run** - when the shown run was made (the file's date and time), then its table of
  **Block** size, **Latency** in nanoseconds and the cache **Level** that block size fits in.
- If no test has been run yet: **Memory latency** "Not measured yet - press Run above" (Classic View:
  Benchmarks & Stress > Memory Latency).

**Healthy / typical values.** On the author's 7800X3D: 1 node. Latency about 1-4 ns up to 32KB (L1),
about 3 ns up to 1MB (L2), and about 8.6-11 ns from 2MB to 64MB (the 96MB L3).

**Unusual values and what they suggest.** Latency jumps much earlier than the L3 size: other programs
were busy during the run, or the L3 is shared with a busy neighbour. Re-run with the PC idle.

**Where Linux gets it.** `/sys/devices/system/node/` and the saved file `~/.cache/srs/bench-latency.txt`.

---
## CPU power states  
`power-states` - Linux data

**In plain words.** All the CPU's power levels in one place: performance levels (P-states), idle levels
(C-states), and the whole-system sleep modes.

**What you see.**

*Performance states (CPPC)* - each line only if the file exists:
- **Highest perf (cpu0)** - top CPPC level (boost).
- **Nominal perf** - the level of the guaranteed base clock.
- **Lowest non-linear perf** - below this, power savings drop off.
- **Lowest perf** - absolute minimum level.
- **Nominal frequency** / **Lowest frequency** - the clocks for nominal and lowest levels.
- **Maximum (boost) frequency** - top clock from cpufreq.
- **Transition latency** - time to change speed, in ns (0 = the hardware does it itself).

*Idle states (cpu0)* - each C-state with its description and exit latency in microseconds. A disabled
state is shown grey with "(disabled)".

*System sleep states*:
- **Supported** - from `/sys/power/state`: `freeze` (suspend-to-idle), `mem` (suspend to RAM), `disk`
  (hibernate).
- **Suspend type** - from `/sys/power/mem_sleep`; the one in [brackets] is used: `s2idle` or `deep` (S3).
- **Hibernate mode** - from `/sys/power/disk`; [bracketed] is active.

**Healthy / typical values.** On the author's 7800X3D: Highest perf 166, Nominal 138, Lowest non-linear
98, Lowest 14, nominal 4.20GHz, lowest 545MHz, boost 5.05GHz, transition 0 ns. Idle states POLL, C1, C2
(18 us), C3 (350 us). Sleep: freeze mem disk, suspend type `[deep]`, hibernate `[platform]`.

**Unusual values and what they suggest.**
- Suspend type only `s2idle`: the firmware offers no S3. Fine on most new laptops; on desktops it can
  mean the BIOS S3 option is off.
- `disk` missing from Supported: no swap big enough, or Secure Boot lockdown blocks hibernation.
- A C-state marked disabled: someone turned it off (tuning tools do this for lower latency).

**Where Linux gets it.** `/sys/devices/system/cpu/cpu0/acpi_cppc/`, `cpufreq/`, `cpuidle/`, `/sys/power/`.

**Background.** A P-state (performance state) is a speed/voltage level while running. A C-state is an
idle level. ACPI S-states (S3 = sleep, S4 = hibernate) are for the whole machine.

---
## Processor Features Present  
`present` - Linux data

**In plain words.** Windows has a function that answers "does this CPU support feature X?" for a fixed
list of features. SIV shows that list; SRS shows the same list using the Linux feature flags.

**What you see.** CPU-0's name, then a table:
- **PF** - the Windows feature number.
- **Windows feature (SIV)** - the Windows name (e.g. `PF_AVX2_INSTRUCTIONS_AVAILABLE`), with a tick
  if present.
- **Linux flag** - the `/proc/cpuinfo` flag SRS checks for it (e.g. `avx2`). A flag shown with a minus
  sign (`-fpu`) means "ticked when this flag is missing".

**Healthy / typical values.** On the author's PC everything is ticked except
PF_FLOATING_POINT_PRECISION_ERRATA (the 1994 Pentium FDIV bug), PF_FLOATING_POINT_EMULATED and
PF_3DNOW_INSTRUCTIONS_AVAILABLE. That is normal for any modern CPU.

**Unusual values and what they suggest.** PF_NX_ENABLED unticked: NX (no-execute) is off in the BIOS,
which is a security risk. PF_VIRT_FIRMWARE_ENABLED unticked: virtualization may be off in the BIOS
(SRS checks the `svm` flag for this line).

**Where Linux gets it.** `/proc/cpuinfo` flags of CPU 0.

---
## Processor  
`processor` - Linux data

**In plain words.** A short card for each CPU: name, generation, where it sits, its speed, its caches
and its main instruction sets. The **CPU-n** panel buttons open one CPU; the menu item shows all.

**What you see.** For each CPU:
- **CPU-n**, model name, then "Family X Model X Stepping n" (family and model in hex, no `0x`) and
  **APIC p.c.t** (package.core.thread) with `(core)` for a first thread or `(logical)` for an SMT sibling.
- Current speed, e.g. "4.96GHz Processor".
- For first threads only, one line per cache level: name (L1 Data, L1 Code, L2 Unified, L3 Unified),
  ways, line size, size, sets, and how many CPUs share it with their numbers. On CPU-0 the size is
  written as "count x size" (e.g. "8 x 32KB") to show the total number of such caches in the chip.
- For first threads, a line with the vendor and key features in brackets: `MMX` or `MMX+`, `SSE4.2`,
  `XD` (no-execute), `SVM` or `VMX` (virtualization), `AVX512` or `AVX2`.

**Healthy / typical values.** On the author's 7800X3D, CPU-0: Family 19 Model 61 Stepping 2 (hex for
25/97), APIC 0.0.0 (core); L1 Data and L1 Code 8 x 32KB, 8-way, 64 sets, shared by CPUs 0 and 8; L2
8 x 1MB, 2048 sets; L3 1 x 96MB, 16-way, shared by 16 CPUs (0-15); `[MMX+ SSE4.2 XD SVM AVX512]`.

**Unusual values and what they suggest.** An L1/L2 cache "shared by 1 CPU": SMT is off. A smaller L3
than the spec sheet: on multi-CCD chips each CCD has its own L3, so you see one CCD's share.

**Where Linux gets it.** `/proc/cpuinfo`, `/sys/devices/system/cpu/cpuN/cache/indexM/`, topology, cpufreq.

---
## Processors  
`processors` - Linux data

**In plain words.** The main processor page (the **Processors** panel button). It shows what kind of
CPU system you have and lists every logical CPU with its IDs.

**What you see.**

*Capabilities* - four tick-box lines with a count and a legend:
- **Multi-processor System** - ticked if there is more than one physical package (|P|).
- **Hybrid-processor System** - mixed big/little cores; SRS always shows this unticked with 1 logical
  package (|e|).
- **Multi-core Processor** - number of cores (|C|) and cores per package.
- **Multi-threading Cores** - number of logical processors (|L|) and threads per core.

Then a note naming the board, and a table:

| Column | Meaning |
|---|---|
| Processors | CPU name with a marker: `\|P\|` the first CPU, `\|C\|` the first thread of a core, `\|L\|` an SMT sibling. |
| APIC (first) | package.core.thread ID. |
| APIC (second) | The hardware APIC ID in hex. |
| Node | NUMA node (always 0 in SRS). |
| Pkg | Physical package. |
| N | Linux CPU number. |
| CPU Affinity Mask | This CPU's bit as a 64-bit hex mask. |
| Speed | Current clock. |
| ACPI | The ACPI device name of the CPU (e.g. `ACPI0007:00`). |
| CPU Device | The ACPI path of the CPU object in the firmware tables (e.g. `\_SB_.PLTF.C000`). |

Board/BIOS line at the end.

**Healthy / typical values.** On the author's 7800X3D: 1 package (unticked multi-processor), 8 cores, 16
logical processors, 2 per core. CPU-0 to CPU-7 are `|P|`/`|C|` (thread 0), CPU-8 to CPU-15 are `|L|`.

**Unusual values and what they suggest.** Missing ACPI names (`-`): the firmware does not link CPUs to
ACPI objects (common in VMs). Fewer CPUs than the spec: some disabled in the BIOS, or `maxcpus=` /
`nosmt` on the kernel command line (compare with [CPU Groups](#cpu-groups)).

**Where Linux gets it.** `/sys/devices/system/cpu/cpuN/topology/`, `firmware_node`, `/proc/cpuinfo`, cpufreq.

**Background.** The APIC (Advanced Programmable Interrupt Controller) ID is the number the hardware uses
for each thread. Linux CPU numbers are just the order Linux brought them up.

---
## Speculation Control  
`speculation` - Linux data

**In plain words.** SIV shows the Windows "speculation control" settings. On Linux, SRS shows the
kernel's own list of CPU security weaknesses (Spectre, Meltdown and friends) and whether each one is
fixed on your machine.

**What you see.**
- One line per known issue, as the kernel names it (e.g. `spectre_v1`, `spectre_v2`, `meltdown`,
  `retbleed`, `spec_rstack_overflow`, `tsa`, `vmscape` ...), and the kernel's status text:
  - green **Not affected** - this CPU does not have the weakness.
  - orange **Mitigation: ...** - the weakness exists and the kernel is protecting against it.
  - red **Vulnerable** - the weakness exists and is not protected.
- **Kernel command line** - the boot options, so you can see if mitigations were turned off.

**Healthy / typical values.** On a default system every line says "Not affected" or "Mitigation: ...".
On the author's PC several lines say "Vulnerable" because the owner boots with `mitigations=off` for a
little more speed. That is a deliberate choice, not a fault.

**Unusual values and what they suggest.** "Vulnerable" without `mitigations=off` in the command line:
old microcode (see [Microcode](#microcode)) or an old kernel. "Vulnerable" on a shared or internet-facing
machine is a real risk; on a single-user gaming PC it is a trade-off some people accept.

**Where Linux gets it.** `/sys/devices/system/cpu/vulnerabilities/*` and `/proc/cmdline`.

**Background.** Speculative execution means the CPU guesses ahead to run faster. Researchers found ways
to leak data through those guesses. Mitigations close the leaks at some cost in speed.

---
## Turbo Boost  
`turbo-boost` - Linux data

**In plain words.** Whether the CPU is allowed to boost above its base clock, and how high it can go.

**What you see.** (lines appear only when the file exists)
- **CPB (CPUID 80000007 EDX bit 9)** - ticked if the CPU supports Core Performance Boost.
- **cpufreq boost** - `enabled` (green) or `disabled` (red).
- **amd_pstate status** - `active`, `guided` or `passive`.
- **amd_pstate prefcore** - whether preferred-core scheduling is on.
- **Maximum boost frequency** - amd_pstate's top clock.
- **Lowest non-linear frequency** - see [AMD Ryzen Power Management](#amd-ryzen-power-management).
- **cpuinfo_max_freq** - top clock as cpufreq reports it.

**Healthy / typical values.** On the author's 7800X3D: CPB ticked, boost enabled, amd_pstate active,
prefcore enabled, maximum 5.05GHz, non-linear 2.98GHz, cpuinfo_max_freq 5.05GHz.

**Unusual values and what they suggest.** Boost disabled: off in the BIOS ("Core Performance Boost"), or
turned off by a power profile. Maximum equal to the base clock (4.20GHz on this chip): boost off.

**Where Linux gets it.** CPUID leaf 80000007, `/sys/devices/system/cpu/cpufreq/boost`,
`/sys/devices/system/cpu/amd_pstate/`, `cpu0/cpufreq/`.

**Background.** "Turbo Boost" is Intel's name and "Core Performance Boost" (CPB) is AMD's for the same
idea: run above the base clock when power and temperature allow.

---
## Xeon PIRs  
`xeon-pir` - Not present on most PCs

**In plain words.** Some older Intel Xeon chips had a small Processor Information ROM (PIR) with factory
data. Desktop and AMD CPUs do not.

**What you see.** **Status** `Not present on this system` and **Checked** `n/a (Intel Xeon only)`.

---
## Cache Levels  
`cache` - Linux data

**In plain words.** A table of the CPU's caches: size, layout and which CPUs share each one. The
**Cache-n** panel buttons show the table for CPU number n.

**What you see.** Title "Cache Levels of CPU-n", then:

| Column | Meaning | Why it matters |
|---|---|---|
| Cache | Level and type: L1 Data, L1 Code, L2 Unified, L3 Unified | L1 is fastest and smallest, L3 slowest and biggest. |
| Size | Size of one such cache (K = kilobytes) | Bigger caches hold more of a program's working data. |
| Ways | Associativity | How many places a line can go; more = fewer collisions. |
| Line | Line size in bytes | The unit the cache moves data in (64 on modern CPUs). |
| Sets | Number of sets | Size / (ways x line). |
| Shared by | How many CPUs share it | 2 = shared by the two threads of one core; all = whole chip. |
| CPUs | Which CPUs share it | |

A note points to Benchmarks & Stress > Latency for measured speeds.

**Healthy / typical values.** On the author's Ryzen 7 7800X3D, CPU-0: L1 Data 32K and L1 Code 32K (8-way,
shared by CPUs 0,8), L2 1024K (8-way), L3 98304K (96MB, 16-way, 98304 sets), shared by 16 CPUs (0-15).
The huge L3 is the "3D V-Cache" that the X3D name refers to.

**Unusual values and what they suggest.** No rows: the kernel has no cache info (some VMs). L3 shared by
fewer CPUs than you have on a multi-CCD chip: normal, each CCD has its own L3.

**Where Linux gets it.** `/sys/devices/system/cpu/cpuN/cache/indexM/` (`level`, `type`, `size`,
`ways_of_associativity`, `coherency_line_size`, `number_of_sets`, `shared_cpu_list`).

**Background.** A cache is a small fast memory on the CPU that keeps copies of recently used RAM. L1 is
inside each core, L2 is per core, L3 is shared. A cache hit takes a few nanoseconds; going out to RAM
takes 60-80 ns or more.

---
## CPU Groups  
`group` - Linux data

**In plain words.** Windows splits large systems into "processor groups" of up to 64 CPUs. Linux has no
such groups; SRS shows the Linux CPU sets instead (the **Groups** panel button).

**What you see.**
- **online** - CPUs running now. **offline** - CPUs switched off (`none` if all are on).
- **possible** - CPUs the kernel could ever bring up. **present** - CPUs physically there.
- **isolated** - CPUs kept away from normal tasks. **nohz_full** - CPUs without the regular timer tick.
- **kernel_max** - the highest CPU number this kernel supports.
- **This process affinity** - the CPUs SRS itself is allowed to run on.

**Healthy / typical values.** On the author's PC: online, possible and present all 0-15; offline,
isolated and nohz_full `none`; kernel_max 15; affinity 0-15.

**Unusual values and what they suggest.**
- Offline CPUs: turned off by hand (`echo 0 > .../online`), by `nosmt`, or by `maxcpus=`.
- possible larger than present: normal on servers and many desktops (room for hot-plug).
- Process affinity smaller than online: SRS was started under `taskset` or a restricted cgroup.

**Where Linux gets it.** `/sys/devices/system/cpu/online`, `offline`, `possible`, `present`, `isolated`,
`nohz_full`, `kernel_max`, and the process's own `sched_getaffinity`.
