# Tools  (SRS Section Reference)

[Back to the section index](README.md) - [Glossary](../GLOSSARY.md)

This group holds the things you *do* with SRS rather than the things it simply reads: CPU benchmarks,
the CPU Burnin and CPU Stress load tests, a core-to-core delay test, a memory latency test, and the
look-up and search tools (device ID lookup, report search, configuration search, ping). Open the
items from **Menu -> Tools -> item** (each entry shows as "Title [id]"). The live tool windows sit at
the top of the Menu itself: CPU Burnin..., CPU Stress..., Benchmark..., IPI Delay..., Latency...,
Device ID Lookup... and Find in Report.... The Processors window also has CPU Stress, CPU Burnin and
Benchmark buttons, and the keys Ctrl+F (Find), Ctrl+L (Device ID Lookup) and Ctrl+D (ALL Devices)
work in every window. None of these items needs root data (srs-root): everything here works as a
normal user. Load tests and benchmarks never start by themselves. Saving a report (`-save`) only
shows the last stored result.

## Contents
- [CPU Benchmarks](#cpu-benchmarks) `benchmark`
- [Benchmark: all CPUs concurrent](#benchmark-all-cpus-concurrent) `benchmark-ac`
- [Benchmark: all CPUs sequential](#benchmark-all-cpus-sequential) `benchmark-as`
- [Benchmark: D+D then W+W](#benchmark-dd-then-ww) `benchmark-dd`
- [Benchmark: D+W then W+D](#benchmark-dw-then-wd) `benchmark-dw`
- [Benchmark: E-cores concurrent](#benchmark-e-cores-concurrent) `benchmark-ec`
- [Benchmark: E-cores one at a time](#benchmark-e-cores-one-at-a-time) `benchmark-ep`
- [Benchmark: logical threads concurrent](#benchmark-logical-threads-concurrent) `benchmark-lc`
- [Benchmark: logical threads one at a time](#benchmark-logical-threads-one-at-a-time) `benchmark-lp`
- [Benchmark: physical cores concurrent](#benchmark-physical-cores-concurrent) `benchmark-pc`
- [Benchmark: physical cores one at a time](#benchmark-physical-cores-one-at-a-time) `benchmark-pp`
- [BOINC status](#boinc-status) `boinc`
- [BOINC RPC info](#boinc-rpc-info) `boinc-rpc`
- [ClearCase](#clearcase) `clear-case`
- [ClearQuest](#clearquest) `clear-quest`
- [ClearQuest e-mail](#clearquest-e-mail) `cq-mail`
- [CPU IPI Delay](#cpu-ipi-delay) `ipi-delay`
- [IPI Dump](#ipi-dump) `ipi-dmp`
- [IPI Plots](#ipi-plots) `ipi-plots`
- [Memory Latency](#memory-latency) `latency`
- [Lookup](#lookup) `lookup`
- [Ping](#ping) `ping`
- [Configuration Find](#configuration-find) `reg-find`
- [Device ID Lookup](#device-id-lookup) `id-lookup`
- [Find](#find) `find`
- [ALL Devices](#all-devices) `all-dev`
- [CPU Burnin](#cpu-burnin) `burnin`
- [CPU Stress](#cpu-stress) `cpu-stress`

---
## CPU Benchmarks  
`benchmark` - Linux data

**In plain words.** A summary table of the ten benchmark modes. It shows the last total score of
each mode, or "not run". It does not run anything.

**What you see.**

| Column | Meaning | Why it matters |
|---|---|---|
| Mode | Two-letter mode code (ac, as, dd, dw, ec, ep, lc, lp, pc, pp). | The same code is used on the command line: `srs-gui bench ac`. |
| Description | What the mode does (which CPUs run, and in what order). | Tells you what the score is measuring. |
| Last result | The `Total` line of that mode's last run: total DMIPS and total MWIPS, or `not run`. | A quick side-by-side view of all modes. |

The two scores:
- **DMIPS** (Dhrystone MIPS) - from the Dhrystone 2.1 test, which does whole-number (integer) work:
  string copies, record and pointer handling, small function calls. 1 DMIPS = 1757 Dhrystone loops
  per second, the speed of a 1978 DEC VAX 11/780. So "125000 DMIPS" means one thread works about
  125,000 times faster than that machine on this test.
- **MWIPS** (Million Whetstone Instructions Per Second) - from the Whetstone test, which does
  floating-point (decimal) maths: arithmetic, sine/cosine/arctangent, square root, log and exp.

**Healthy / typical values.** On the author's Ryzen 7 7800X3D (8 cores, 16 threads):

| Mode | Total DMIPS | Total MWIPS |
|---|---|---|
| ac | 1644837 | 200303 |
| as | 2024492 | 236824 |
| dd | 1789518 | 218525 |
| dw | 1888968 | 230582 |
| lc | 983919 | 116411 |
| lp | 1023750 | 120536 |
| pc | 987836 | 116316 |
| pp | 1024949 | 120486 |

ec and ep show `not run` on any non-hybrid CPU.

**Unusual values and what they suggest.**
- The results are stored separately and can be from different days, kernels or BIOS settings. Open
  the single mode item and check its `Run` line before you compare two modes.
- A total far below an earlier run of the same mode: the CPU was hot and throttling, a background
  job was busy, or a power plan / BIOS setting changed.

**Where Linux gets it.** Built-in Dhrystone 2.1 and Whetstone code in SRS. Results are kept in
`~/.cache/srs/bench-<mode>.txt`. SMT sibling pairs come from
`/sys/devices/system/cpu/cpuN/topology/thread_siblings_list`.

**Background.** Dhrystone and Whetstone are classic, very small tests. They fit inside the L1 cache,
so they measure the core itself (clock speed, pipeline, compiler), not memory or disks. Scores
depend a lot on the compiler, so compare SRS with SRS, not with other programs.

---
## Benchmark: all CPUs concurrent  
`benchmark-ac` - Linux data

**In plain words.** Every logical CPU runs Dhrystone at the same time, then every logical CPU runs
Whetstone at the same time. This is the "whole chip, fully loaded" score.

**What you see.** The layout below is the same for every benchmark mode item.

| Field | Meaning | Why it matters |
|---|---|---|
| Header | "CPU Benchmark [ac] on \\\\host - distro kernel". | Shows which kernel the run used. |
| Mode | The mode description. | What was measured. |
| Run | Date and time of the run, then "N s per measurement". | How old the result is, and how long each test ran. Longer runs are steadier. |
| Note | Only shown when the run has a remark. | Extra information about the run. |
| CPU | `CPU-n`, the logical CPU the test thread was pinned to. | Lets you spot one weak CPU. |
| Dhrystone | That CPU's score in DMIPS, or `-` if not measured. | Integer speed. |
| Whetstone | That CPU's score in MWIPS, or `-` if not measured. | Floating-point speed. |
| Total | Sum of the column. | The overall score for the mode. |
| Footer | Board maker, model and BIOS version. | Ties the result to a BIOS version. |

If a mode has never been run, the item instead shows `Status Not run yet`, `Run it` (Menu > Tools >
Benchmark, then Run), `Command line` (for example `srs-gui bench ac`) and the note "Benchmarks are
never started by -save; this section shows the last result."

**Healthy / typical values.** On the author's 7800X3D (5 s per measurement): each CPU scored about
101000-104000 DMIPS and 12300-12700 MWIPS. Total 1644837 DMIPS and 200303 MWIPS. A spread of 2-3 %
between CPUs is normal.

**Unusual values and what they suggest.**
- One CPU 10 % or more below the rest: something else was running on it, or that core is clocked
  lower (core parking, a per-core undervolt or Curve Optimizer value that is too aggressive and
  makes the core stretch its clock).
- Scores fall during a long run: heat. Check the cooler and the CPU temperature in Sensors.

**Where Linux gets it.** Built-in tests. Each thread is pinned to its CPU with CPU affinity
(`pthread_setaffinity_np`).

**Background.** With all 16 threads busy, the two threads of each core share one core. That is why
each CPU scores lower here than in the "one at a time" modes. The total is still higher, because
SMT (simultaneous multithreading) lets a core do more work with two threads than with one.

---
## Benchmark: all CPUs sequential  
`benchmark-as` - Linux data

**In plain words.** The test runs on one logical CPU at a time, CPU-0 to the last one. Each CPU runs
Dhrystone and then Whetstone while all the other CPUs are idle.

**What you see.** The same layout as [all CPUs concurrent](#benchmark-all-cpus-concurrent).

**Healthy / typical values.** On the author's 7800X3D (0.1 s per measurement): about 125000-129000
DMIPS and 14400-15100 MWIPS per CPU. Total 2024492 DMIPS.

**Unusual values and what they suggest.**
- The total here is a sum of single-thread scores. The chip can never reach it in real use, because
  two threads on one core share it. Compare it with `ac` to see the SMT effect: on the author's PC,
  1644837 / 2024492 = 0.81. With all threads busy, each thread keeps 81 % of its solo speed, so one
  core with two threads does about 1.6 times the work of one thread.
- One or two CPUs clearly faster than the rest: those are the "preferred" (best-binned) cores that
  boost highest when only one core is busy. That is normal on Ryzen and Intel Turbo Boost Max 3.0.
- Very short runs (0.1 s) are noisy. Use 1 s or more for a fair comparison.

**Where Linux gets it.** Built-in tests, one pinned thread at a time.

---
## Benchmark: D+D then W+W  
`benchmark-dd` - Linux data

**In plain words.** The test goes core by core. Both threads of one core run Dhrystone together,
then both run Whetstone together. Then it moves to the next core.

**What you see.** The same layout as [all CPUs concurrent](#benchmark-all-cpus-concurrent). The rows
come in sibling pairs: CPU-0, CPU-8, CPU-1, CPU-9 and so on. CPU-0 and CPU-8 are the two threads of
core 0.

**Healthy / typical values.** On the author's 7800X3D: about 109000-114000 DMIPS and 13400-13900
MWIPS per thread. Total 1789518 DMIPS and 218525 MWIPS. Both threads of a pair score almost the
same.

**Unusual values and what they suggest.**
- The two threads of a pair score very differently: something else was running on one of them.
- One whole pair is lower than the others: that core is slower (a lower boost or a Curve Optimizer
  offset), or it is hotter.

**Where Linux gets it.** Built-in tests. Sibling pairs come from `thread_siblings_list`.

**Background.** Two threads doing the *same* kind of work fight over the same execution units, so
each one slows down.

---
## Benchmark: D+W then W+D  
`benchmark-dw` - Linux data

**In plain words.** Core by core, one thread runs Dhrystone while its sibling runs Whetstone at the
same time. Then they swap. Each thread ends up with one Dhrystone and one Whetstone score.

**What you see.** The same layout and pair order as [D+D then W+W](#benchmark-dd-then-ww).

**Healthy / typical values.** On the author's 7800X3D: total 1888968 DMIPS and 230582 MWIPS, about
5 % higher than `dd`.

**Unusual values and what they suggest.** If `dw` is *not* higher than `dd`, the core's integer and
floating-point sides are not working in parallel well. This can happen with SMT disabled in the
BIOS, or when the scheduler moved threads around during the run.

**Where Linux gets it.** Built-in tests, sibling pairs from sysfs.

**Background.** Dhrystone uses the integer units and Whetstone uses the floating-point units. Mixed
work shares a core better than identical work. That is the main reason SMT helps real programs.

---
## Benchmark: E-cores concurrent  
`benchmark-ec` - Not present on most PCs

**In plain words.** Meant for hybrid CPUs (for example Intel 12th generation and newer) that mix
big Performance cores (P-cores) with small Efficiency cores (E-cores). It would run the test on all
E-cores at once.

**What you see.** On a CPU without E-cores: `Mode Efficiency cores concurrently`, `Status No
efficiency cores detected on this CPU` and `Checked /sys/devices/cpu_atom/cpus (Intel hybrid CPUs)`.
On an Intel hybrid CPU the mode runs like the other modes, on the E-cores listed in that file.

**Healthy / typical values.** On the author's Ryzen 7 7800X3D, and on any CPU where all cores are
the same type, "No efficiency cores detected" is the normal and correct answer. AMD CPUs that mix
Zen 5 and Zen 5c cores are not told apart yet.

---
## Benchmark: E-cores one at a time  
`benchmark-ep` - Not present on most PCs

**In plain words.** The same idea as [E-cores concurrent](#benchmark-e-cores-concurrent), but on one
E-core at a time.

**What you see.** On a CPU without E-cores: `Mode Efficiency cores one at a time` and `Status No
efficiency cores detected on this CPU`; on an Intel hybrid CPU, one result per E-core.

---
## Benchmark: logical threads concurrent  
`benchmark-lc` - Linux data

**In plain words.** Only the *second* thread of each core runs (CPU-8 to CPU-15 on the author's
PC), all at the same time. The first threads stay idle.

**What you see.** The same layout as [all CPUs concurrent](#benchmark-all-cpus-concurrent). Only
the second-sibling CPUs are listed.

**Healthy / typical values.** On the author's 7800X3D: about 121500-123500 DMIPS per thread, total
983919 DMIPS and 116411 MWIPS. That is almost the same as `pc` (987836), which runs the first
threads.

**Unusual values and what they suggest.** If `lc` is much lower than `pc`, the "second" threads are
not on their own cores. The topology may be reported wrongly, or another task was using the first
threads.

**Where Linux gets it.** Built-in tests. A CPU counts as the "second" thread when its sibling has a
lower number in `thread_siblings_list`.

**Background.** A logical CPU (logical thread) is what Linux calls each hardware thread. With SMT,
one physical core shows up as two logical CPUs. Neither one is "real" or "fake": when its partner
is idle, either one gets the whole core.

---
## Benchmark: logical threads one at a time  
`benchmark-lp` - Linux data

**In plain words.** The second thread of each core runs alone, one after another.

**What you see.** The same layout as [all CPUs concurrent](#benchmark-all-cpus-concurrent).

**Healthy / typical values.** On the author's 7800X3D: about 124500-130000 DMIPS per thread, total
1023750 DMIPS. Almost the same as `pp` (1024949).

**Unusual values and what they suggest.** A clear gap between `lp` and `pp` for the same core means
one of the two threads was disturbed during its run. Run it again.

**Where Linux gets it.** Built-in tests.

---
## Benchmark: physical cores concurrent  
`benchmark-pc` - Linux data

**In plain words.** One thread per physical core (the first sibling), all cores at the same time.
This is the all-core score without SMT.

**What you see.** The same layout as [all CPUs concurrent](#benchmark-all-cpus-concurrent). On the
author's PC: CPU-0 to CPU-7.

**Healthy / typical values.** On the author's 7800X3D: about 122000-124000 DMIPS per core, total
987836 DMIPS and 116316 MWIPS.

**Unusual values and what they suggest.** Compare with `ac`. On the author's PC, 1644837 / 987836 =
1.67, so the second threads add about 67 % on this test. A ratio near 1.0 means SMT is off or not
working.

**Where Linux gets it.** Built-in tests, first sibling of each core.

---
## Benchmark: physical cores one at a time  
`benchmark-pp` - Linux data

**In plain words.** One thread per physical core, one core after another, with the rest of the chip
idle. This is the single-core score of each core.

**What you see.** The same layout as [all CPUs concurrent](#benchmark-all-cpus-concurrent).

**Healthy / typical values.** On the author's 7800X3D: about 125800-129700 DMIPS per core, total
1024949 DMIPS and 120486 MWIPS.

**Unusual values and what they suggest.** The ranking of cores here matches the CPU's "preferred
core" order. One core well below the others in this mode (with the chip otherwise idle) points to a
per-core limit: a Curve Optimizer value that is too negative, or a core that cannot boost.

**Where Linux gets it.** Built-in tests.

**Background.** Single-core speed matters most for games and older programs that use one main
thread. All-core speed matters for compiling, rendering and encoding.

---
## BOINC status  
`boinc` - Not present on most PCs

**In plain words.** BOINC (Berkeley Open Infrastructure for Network Computing) is a volunteer
computing client (SETI@home, Rosetta@home and others). SIV can show the state of a running BOINC
client.

**What you see.** `Status Not present on this system` and `Checked n/a (boinc not installed)`.

**What "not present" means.** No BOINC client data is shown. If you do not run BOINC, there is
nothing to do.

---
## BOINC RPC info  
`boinc-rpc` - Not present on most PCs

**In plain words.** BOINC clients take commands over a small network control channel, RPC (Remote
Procedure Call). In SIV this item shows the details of that connection.

**What you see.** `Status Not present on this system` and `Checked n/a (boinc not installed)`.

---
## ClearCase  
`clear-case` - Not present on most PCs

**In plain words.** IBM Rational ClearCase is a version-control system used in large companies.
SIV can report on an installed ClearCase client.

**What you see.** `Status Not present on this system` and `Checked n/a (not installed)`.

---
## ClearQuest  
`clear-quest` - Not present on most PCs

**In plain words.** IBM Rational ClearQuest is a bug- and change-tracking system that goes with
ClearCase.

**What you see.** `Status Not present on this system` and `Checked n/a (not installed)`.

---
## ClearQuest e-mail  
`cq-mail` - Not present on most PCs

**In plain words.** The e-mail notification settings of a ClearQuest client.

**What you see.** `Status Not present on this system` and `Checked n/a (ClearQuest not installed)`.

---
## CPU IPI Delay  
`ipi-delay` - Linux data

**In plain words.** It measures how long it takes one CPU to pass a message to another CPU through
the cache. Every pair of CPUs is tested, and the result is a table in nanoseconds.

**What you see.**

| Field | Meaning | Why it matters |
|---|---|---|
| Header | "CPU IPI Delay (core-to-core cache-line round trip / 2, ns)". | The number is the one-way time: a full there-and-back trip divided by 2. |
| Rounds per pair | How many there-and-back trips were timed for each pair (GUI default 20000, range 1000-1000000). | More rounds give a steadier average. |
| From\To | Row = sending CPU, column = receiving CPU. | Find any pair at the crossing point. |
| `-` | The diagonal (a CPU to itself). Not measured. | |
| Numbers | One-way delay in ns. Shown in green below 40 ns, normal colour from 40 to 99 ns, orange at 100 ns or more. | Low = the two CPUs are close together on the chip. |
| Footer | Board and BIOS. | |

Each pair is measured once, so the table is a mirror: the CPU-3 -> CPU-5 cell always equals
CPU-5 -> CPU-3. If it has never been run, the item shows `Status Not run yet`, `Run it` (Menu >
Tools > IPI Delay, then Run) and `Command line srs-gui ipi [--cpus N] [--rounds R]`.

The GUI window (Menu -> IPI Delay...) has a CPUs box (test only the first N CPUs), a Rounds per pair
box, a Run button, and a heat map under the text. The heat map is coloured from green (fastest) to
red (slowest), scaled to the largest value.

**Healthy / typical values.** On the author's 7800X3D (one CCD, 8 cores, 50000 rounds):
- Sibling pairs (CPU-0 and CPU-8, CPU-1 and CPU-9, ...): 24-25 ns. They share one core and its L1
  and L2 caches.
- All other pairs: about 28-39 ns. They meet in the shared L3 cache.
- The table is flat apart from the sibling diagonal, because all 8 cores sit on the same CCD.

**Unusual values and what they suggest.**
- A block of the table much higher than the rest (often 70-150 ns): the CPU has more than one
  CCD (core complex die), for example a Ryzen 9 7950X, or more than one socket or NUMA node. Pairs on
  different dies must go through the Infinity Fabric (or the ring/mesh between tiles) and are much
  slower. Games and audio work run better when their threads stay on one die.
- One CPU slow to every other CPU: it was busy with other work during the test, or it was in a deep
  sleep state. Close other programs and run again.
- Siblings no faster than other pairs: SMT may be off, or the CPU numbering is not what you expect.

**Where Linux gets it.** Built-in test. Two threads are pinned to the two CPUs and bounce one shared
cache line (a flag in memory) back and forth with atomic loads and stores. The first third of the
rounds is an untimed warm-up. Timing uses `CLOCK_MONOTONIC`. The result is stored in
`~/.cache/srs/bench-ipi-delay.txt`.

**Background.** "IPI" stands for Inter-Processor Interrupt, a signal one CPU sends to another. SRS
keeps SIV's name, but it does not send real interrupts. It measures the core-to-core cache-line
delay, which is the cost every program pays when threads on two cores share data (locks, queues,
game engine job systems).

---
## IPI Dump  
`ipi-dmp` - Linux data

**In plain words.** In SIV this is the raw dump of the IPI measurements. In SRS it prints exactly the
same table as [CPU IPI Delay](#cpu-ipi-delay).

**What you see.** The same `Rounds per pair` line and From\To matrix, from the last stored run. See
[CPU IPI Delay](#cpu-ipi-delay) for what every number means.

**Where Linux gets it.** `~/.cache/srs/bench-ipi-delay.txt` (the last IPI run).

---
## IPI Plots  
`ipi-plots` - Linux data

**In plain words.** In SIV this shows the IPI results as graphs. In a saved report SRS prints the same
table as [CPU IPI Delay](#cpu-ipi-delay). The picture version is the heat map in the GUI IPI Delay
window.

**What you see.** In a report: the same matrix as CPU IPI Delay. In the GUI: a square grid with one
cell per CPU pair, green for the fastest pairs and red for the slowest. The heat map appears after a
run in the open program. After a restart you see the stored table but an empty map until you press
Run again.

**Healthy / typical values.** On the author's single-CCD 7800X3D the map is almost one colour, with
a darker green diagonal line for the sibling pairs. A dual-CCD CPU shows four clear squares: two
fast (inside each die) and two slow (between the dies).

**Where Linux gets it.** Same test as [CPU IPI Delay](#cpu-ipi-delay).

---
## Memory Latency  
`latency` - Linux data

**In plain words.** It measures how long the CPU waits for one piece of data, using blocks of memory
that get bigger and bigger. Small blocks fit in the fast caches. Big blocks have to come from RAM.
The jumps in the result show where each cache ends.

**What you see.**

| Column | Meaning | Why it matters |
|---|---|---|
| Block | Size of the memory block tested: 4KB, 8KB, 16KB, ... doubling up to the largest size (default 256MB; 1-4096 MB in the GUI and on the command line). | Each size lands in a different cache level. |
| Latency | Average time for one load, in nanoseconds. Best of 5 passes of 2 million loads. | Lower is faster. |
| Level | L1, L2, L3 or DRAM - a rough label for where a block of that size usually lives. | Helps you read the table. |

The last line names the cache sizes used for the Level column, read from
`/sys/devices/system/cpu/cpu0/cache` (for example "L1d 32KB, L2 1MB, L3 96MB"): a block is labelled
with the smallest cache it fits in, and DRAM above the last level. Labels are indicative; the jumps
in the Latency column are the real boundaries (compare [Cache Levels](processors.md#cache-levels), `cache`). If never run, the item
shows `Status Not run yet` and `Command line srs-gui latency [--max MB]`.

The GUI window (Menu -> Latency...) has a "Largest block (MB)" box, a Run button and a line graph of
latency against block size.

**Healthy / typical values.** On the author's 7800X3D (L1 data 32K, L2 1024K, L3 96 MB "3D V-Cache"),
run up to 64MB:
- 4KB-32KB: about 1-4 ns (L1). The 4KB result, 0.94 ns, is about 4-5 clock cycles at 5 GHz.
- 64KB-1MB: about 3.0-3.3 ns (L2), roughly 14 cycles.
- 2MB-64MB: about 8.6-11 ns (L3). 64MB still reads as L3 because the X3D's L3 is 96 MB.
- Larger blocks (128MB, 256MB) would show the DRAM step. On a typical DDR5 system that is around
  60-90 ns.

Small blocks can jump around a little (8KB was 3.74 ns in the same run). That is timing noise: the
whole 4-32 KB range is still L1.

**Unusual values and what they suggest.**
- The L2 to L3 step happens at a smaller block than your L2 size: something else is using the cache,
  or the run was disturbed.
- DRAM latency well above about 100 ns on DDR5 (or about 80 ns on a tuned DDR4 system): loose memory
  timings, EXPO/XMP not enabled, or the memory controller running at a lower ratio (for example
  UCLK at half the memory clock on AM5).
- L3 latency much higher than expected on a multi-die CPU: on most Ryzen chips each CCD has its own
  L3. A block bigger than one CCD's L3 goes to DRAM, not to the other die's cache.

**Where Linux gets it.** Built-in "pointer chase": a buffer is filled with links in random order, and
each load needs the address from the load before. That defeats prefetching, so every step pays the
full delay. Lines are 64 bytes. A 200 ms busy spin first brings the core to full clock. Stored in
`~/.cache/srs/bench-latency.txt`.

**Background.** A cache level is a small, fast memory between the core and RAM. L1 is the smallest
and fastest (tens of KB per core), L2 is bigger (about 1-2 MB per core), and L3 is shared by many
cores (tens of MB). Data the CPU uses often stays close. Latency (waiting time) matters most for
programs that jump around in memory, such as games and databases.

---
## Lookup  
`lookup` - Linux data

**In plain words.** It lists the name databases SRS uses to turn ID numbers into names (PCI, USB,
monitor maker and network card maker), with their dates and versions. It also shows the SRS program
itself.

**What you see.**

| Column | Meaning | Why it matters |
|---|---|---|
| Description | What the file is for: PCI Device Names, USB Device Names, PNP / Monitor Names, MAC Vendor Names, SRS Program, SRS Kernel Driver. | |
| File Specification | Full path of the file, for example `/usr/share/hwdata/pci.ids`. | Where to look if names are missing. |
| Current Identification | "Date ... Version ... (N)". N is the number of record lines in the file. When the file has no version line, the file size in bytes is shown instead, and Date is `?`. For SRS Program it is the SRS version. | An old file means new hardware shows as "Unknown". |
| Code Page | Text encoding (UTF-8). | |
| WEB Download Location | Where the newest copy of each file is published. | |

Under the table: "No network access: srs-gui shows the upstream URLs but never downloads." The SRS
Kernel Driver row says `none`: SRS loads no kernel driver of its own, and root-only data comes from
srs-root.

**Healthy / typical values.** On the author's PC: pci.ids Version 2026.10.01 (42452 records),
usb.ids Version 2026.06.26 (25615), pnp.ids 63479 bytes, oui.txt 6321491 bytes. SRS Program
`/usr/bin/sysreport-search`, V1.1.0.

**Unusual values and what they suggest.**
- `not found`: the hwdata package is missing. Install `hwdata` (Arch) or `hwdata` / `pciutils` /
  `usbutils` (other distros).
- A pci.ids or usb.ids date more than a year old with very new hardware: update the hwdata package
  (or run `update-pciids` where it exists) to get names for new devices.

**Where Linux gets it.** `/usr/share/hwdata/pci.ids`, `usb.ids`, `pnp.ids` and `oui.txt`, plus
`/proc/self/exe` for the program path.

---
## Ping  
`ping` - Linux data

**In plain words.** It sends three small test packets to your router (the default gateway) and shows
whether they came back and how fast. In SIV this is a Windows ping. SRS uses the Linux `ping` tool.

**What you see.**
- `Target` - the IPv4 default gateway, read from the routing table.
- The normal `ping` output: one line per reply with `icmp_seq` (packet number), `ttl` (time to live,
  how many more router hops the reply could have made) and `time` (round trip in ms).
- The summary: packets sent and received, `% packet loss`, and `rtt min/avg/max/mdev` (fastest,
  average, slowest, and how much the times vary).
- Error lines in red when the ping fails, `No IPv4 default gateway` when there is no route out, or
  `ping not available` when the tool is missing.

**Healthy / typical values.** On the author's wired PC, pinging a home router (for example
192.168.1.1): 3 sent, 3 received, 0% loss, average about 0.3 ms. Wired links to a router are usually
below 1 ms. Wi-Fi is usually 1-10 ms.

**Unusual values and what they suggest.**
- Any packet loss to your own router: a bad cable or port, weak Wi-Fi, or an overloaded router.
- Times of tens of ms on a wired link: the router is busy, or the link fell back to a slow speed.
- 100 % loss but the internet works: the router is set to ignore ping. That is harmless.

**Where Linux gets it.** `/proc/net/route` for the gateway, then `ping -c 3 -i 0.2 -W 1 <gateway>`.

---
## Configuration Find  
`reg-find` - Linux data

**In plain words.** SIV searches the Windows Registry. Linux keeps its settings in text files
instead, so SRS searches the text files in `/etc` (system settings) and `~/.config` (your own
settings) for a word or pattern.

**What you see.**
- With no pattern: `Usage reg-find:PATTERN (regex; searches /etc and ~/.config text files)`.
- With a pattern: one line per hit, `path:line-number` followed by the matching line.
- At the end: `N Matches`, `N Matches (limit 200)` when it stopped at 200 hits, or `No Matches for
  PATTERN`. A broken pattern gives `Match String [...] is Invalid:` and the reason.

Run it from a report with `-save=[reg-find:PATTERN]`. Example: `-save=[reg-find:HandleLidSwitch]`
finds where lid-close behaviour is set.

**Healthy / typical values.** On the author's PC, `^HandleLidSwitch` gives `No Matches`, because the
setting only appears commented out (`#HandleLidSwitch=...`) in `/etc/systemd/logind.conf`. Leave out
the `^` to see commented lines too.

**Unusual values and what they suggest.** Fewer hits than expected: some files under `/etc` can only
be read by root and are skipped without a message. Use `sudo grep` for those.

**Where Linux gets it.** A walk of `/etc` and `~/.config`, up to 4 folders deep. Only regular files
up to 256 KB. Files whose name contains "shadow" (password hashes) are skipped.
Case is ignored.

**Background.** A regex (regular expression) is a search pattern. `.` means any character, `^` means
"start of line", `a|b` means "a or b".

---
## Device ID Lookup  
`id-lookup` - Linux data

**In plain words.** Type a device's ID numbers and SRS tells you the maker and the device name, from
the same databases Linux uses.

**What you see.**
- With no ID: `Usage id-lookup:VVVV:DDDD[:SSSS:ssss][:RR] or id-lookup:usb:VVVV:PPPP`.
- A list of keys, from the most exact to the least exact. Each key shows its database entry, or
  `(no entry)`. A `<` marks the key that matched.
- `Vendor` - the maker of the chip (from VVVV).
- `Subsystem` - the maker of the card or board (from SSSS). Only shown for 4-part PCI IDs.
- `Stock` - the plain chip name (VVVV:DDDD), as the chip maker names it.
- `Resolved` - the best name found (the `<` row), or `No Matches`.
- Errors: `bad ID ... expected VVVV[:DDDD[:SSSS:ssss]][:RR]` when the format is wrong, or `cannot
  find the .ids file`.

ID parts: VVVV = vendor, DDDD = device, SSSS:ssss = subsystem vendor and subsystem device (the card
maker's own ID), RR = revision. All are hexadecimal. For USB: VVVV = vendor, PPPP = product.

The GUI window (Menu -> Device ID Lookup..., or Ctrl+L) adds a PCI / USB choice and a three-way
mode box. **Tree** does a fast binary search of a sorted table. **Scan** reads the .ids file from top
to bottom. **Info** asks the system device database (systemd-hwdb) instead. A footer shows the
number of records and the load and lookup times.

**Healthy / typical values.** On the author's PC, `1002:7550:1EAE:8811:C0` (the graphics card) gives
no entry for the three exact keys and matches `1002:7550` = "Navi 48 [Radeon RX 9070/9070 XT/9070
GRE]". Vendor = Advanced Micro Devices, Inc. [AMD/ATI], Subsystem = XFX Limited. `usb:1d6b:0002`
resolves to "2.0 root hub" by the Linux Foundation.

**Unusual values and what they suggest.** Only the vendor matches, or nothing at all: the device is
newer than your pci.ids / usb.ids file (see [Lookup](#lookup)), or the ID was typed wrongly. Info mode
can sometimes still name it, because systemd-hwdb is updated separately.

**Where Linux gets it.** `/usr/share/hwdata/pci.ids` and `usb.ids`. Info mode uses systemd-hwdb.

**Background.** Every PCI and USB device reports a vendor ID and a device ID. These are the numbers
Linux uses to choose a driver. The name is only a label from a community-kept list.

---
## Find  
`find` - Linux data

**In plain words.** It searches your newest saved system report (made by srs-sysreport) for a word or
pattern and shows every matching line, grouped by section file.

**What you see.**
- With no pattern: `Usage find:PATTERN (regex over the newest ~/sysreports report)`.
- `Searching <report folder> for "PATTERN"`.
- For each file with hits: `== file - N Matches`, then each hit as `line-number: text`, with the
  matching part highlighted.
- `N Matches in X of Y sections`, or `No Matches for PATTERN`.
- `No report found under ~/sysreports - run srs-sysreport first` when there is no report yet.

From a report (`-save=[find:PATTERN]`) the search is case-sensitive and shows up to 20 hits per file.
The GUI window (Menu -> Find in Report..., or Ctrl+F) adds: Ignore case, Literal (search for the exact
text, not a regex), Highlight Matches, Max (hits shown per file, then "... N more (raise Max)"),
Report (which report folder; Browse... and Latest buttons), Next / Prev buttons and F3 / Shift+F3 to
jump between hits, Colour (the highlight colour), and Lookup (opens Device ID Lookup).

**Healthy / typical values.** On the author's PC, `find:Tctl` found 1 match in `sensors.txt` (line 7,
`Tctl: +59.6°C`), "1 Matches in 1 of 16 sections".

**Unusual values and what they suggest.** The report folder date is old: the search only looks at the
newest `sysreport-*` folder. Run srs-sysreport again for fresh data, or pick a folder with Browse.

**Where Linux gets it.** The newest `~/sysreports/sysreport-*` folder: every `.txt` and `.html` file
except `index.html`.

---
## ALL Devices  
`all-dev` - Linux data

**In plain words.** Every PCI and USB device in the PC, each one turned from its ID numbers into a
maker and device name. Devices the name lists do not know are marked.

**What you see.** One block per bus, PCI first, then USB.
- Block header: `[PCI] 54 devices, 42,228 records /usr/share/hwdata/pci.ids` - how many devices
  were found, how many entries the name list has, and which file was used.

| Column | Meaning | Why it matters |
|---|---|---|
| Slot | PCI address (domain:bus:device.function, e.g. `0000:03:00.0`) or USB port path (e.g. `1-2.4`, or `usb1` for a root hub). | Where the device sits. |
| ID | The full ID: PCI `vendor:device:subvendor:subdevice:revision`, USB `vendor:product`. | The numbers to search for online. |
| Matched | The most exact part of the ID that has a name in the list, or `-`. | Shows how well the device is known. |
| Vendor | The chip maker, or `Unknown Vendor`. | |
| Resolved | The device name. `Unknown Device` (red) when only the vendor is known. `[subsystem: ...]` is added when the card maker is known but the exact card is not in the list. | |

At the end: `N device(s) resolved only to vendor or not at all`. Use `all-dev:pci` or `all-dev:usb`
to show one bus only.

**Healthy / typical values.** On the author's PC: 54 PCI devices, all named. For example
`0000:03:00.0` = Navi 48 [Radeon RX 9070/9070 XT/9070 GRE] [subsystem: XFX Limited], and the Samsung
SSD 980 PRO matched on all four ID parts. 40 USB devices, with 16 known only by vendor or not at all.
That is common for USB: many hubs, receivers and controllers are simply not in usb.ids.

**Unusual values and what they suggest.**
- Many PCI devices "Unknown Device": your pci.ids is old (see [Lookup](#lookup)).
- A device you expect is missing from the list: it is switched off in the BIOS, not seated, or (for
  USB) not powered or not detected. Check `dmesg` for errors.

**Where Linux gets it.** PCI devices from `/sys/bus/pci/devices`, USB devices from
`/sys/bus/usb/devices`, names from `/usr/share/hwdata/pci.ids` and `usb.ids`.

---
## CPU Burnin  
`burnin` - Linux data

**In plain words.** A stress test: it makes the CPUs you choose 100 % busy for a set time, watches the
temperatures, and ends with **Pass** or **Fail**. Use it to check that a new build, cooler, overclock
or undervolt is stable. The saved item only shows the current state and sensor readings. It never
starts a run.

**What you see in a report.**

| Field | Meaning |
|---|---|
| State | `Running`, or `Idle (start it from Menu > Tools > CPU Burnin)`. |
| Worker threads | How many CPUs can get a worker thread ("16 CPUs available"). |
| Hottest CPU sensor | The hottest CPU temperature right now, with chip and label (k10temp, coretemp or zenpower). |
| Hottest SIO sensor | The hottest motherboard Super I/O sensor right now. |
| GPU | Graphics card load %, temperature (junction if available) and core clock. |
| Command line | `srs-gui burn [--threads N \| --cpus 0,2] [--seconds S] [--mode same\|swap] [--limit C]`. |
| Safety note | Never auto-starts; stops at the temperature limit (default 90 C) and the time cap. |

**What you see in the CPU Burnin window** (Menu -> CPU Burnin...).
- Settings: Burn Time (0-750, in minutes, or in hours if "Hours" is ticked; default 15 minutes),
  Cool Time (0-900 seconds idle between burns, default 60), Cycles (1-99 burn/cool rounds, default 1),
  Temperature limit °C (50-100, default 90), **Same CPU (Whetstone)** or **Swap CPU (affinity
  test)**, an optional GPU Load command, and one tick box per CPU with Select All / Deselect All.
  Start asks for confirmation first.
- Status line: Idle / Burning / Cooling / Pass / Fail / Stopped, with Elapsed and Remaining time
  (hh:mm:ss) and the reason text (for example "Pass - Finished").
- A sensor table with 7 rows (CPU Load, CPU Frequency, Hottest Core Temp, Hottest SIO Sensor Temp,
  GPU Load, GPU Temp, GPU Frequency) and 4 columns: **Current**, **Overall Average** (since start),
  **Rolling Average** (recent values count most) and **Maximum**.
- A thread table:

| Column | Meaning |
|---|---|
| Thread | Worker number. |
| CPU | The CPU this worker was pinned to. |
| On CPU | The CPU it actually ran on during its last pass. |
| User / Kernel | How busy that CPU was with program work / kernel work, in %. |
| Clock | That CPU's current clock in MHz. |
| Whetstone | Score of the last pass, in MWIPS. |
| Average / Min / Max | Average, lowest and highest pass score so far. |
| Affinity | `ok`, `FAIL` (red), or `-` before the first pass ends. |

- A graph of the last 10 minutes: green CPU load %, red hottest core °C, orange SIO °C, blue GPU °C,
  on a 0-100 scale.

**Pass and Fail.**
- **Fail** - the hottest CPU sensor reached the temperature limit ("Fail - k10temp Tctl 90.0 C
  reached the 90 C limit"), or a worker ran on the wrong CPU ("Fail - a worker thread ran on the
  wrong CPU (affinity / APIC check)"). The run stops at once.
- **Pass** - all cycles finished ("Pass - Finished"), or the overall time cap was reached.
- **Stopped** - you pressed Stop or closed the window.
- Command line `srs-gui burn`: exit code 0 = Pass, 1 = Fail, 130 = Stopped (Ctrl+C). It prints one
  status line per second and a thread table at the end. It also accepts `--cool S` and `--cycles N`.

**Saved results.** When a run ends (Pass, Fail or Stopped), SRS saves the window's summary - the
status, the sensor table and the thread table, the same text the Copy button gives - to
`~/.cache/srs/burnin-YYYYMMDD-HHMMSS.txt` (date and time the run ended), and the status line says
where. Each run gets its own file, so you can compare runs before and after a change, for example
with `diff` or `meld`. The command line `burn` saves its summary the same way. SRS never deletes
these files; remove old ones by hand when you no longer need them.

**Affinity ok / FAIL.** After every pass each worker checks two things: (1) Linux says it is running
on the CPU it was pinned to (`sched_getcpu`), and (2) the CPU's own ID number (APIC ID, read with the
CPUID instruction) matches the one `/proc/cpuinfo` lists for that CPU. `ok` means both matched every
time. `FAIL` means the thread was found somewhere else at least once. That should never happen, so it
points to a scheduler problem, a CPU that went offline, or a firmware or topology fault. In **Swap**
mode each worker moves to the next selected CPU before every pass. That tests that moving threads
between cores works.

**Healthy / typical values.** On the author's 7800X3D at idle: Tctl 56.0°C, nct6799 CPUTIN 30.0°C,
GPU 5 % load, 29.0°C, 1725 MHz. Under a full all-core load, a 7800X3D usually settles around
80-89°C. It is designed to hold near its 89°C limit (its Tjmax), so on this chip the default 90°C cut-off never triggers: the CPU slows itself down first. If you want the Fail cut-off to act, set the limit a few degrees below your own CPU's Tjmax. A good run shows steady Whetstone scores per
thread (Min close to Max), all Affinity `ok`, and Pass.

**Unusual values and what they suggest.**
- Fail on temperature within a few minutes: cooler not seated well, pump not running, or not enough
  case airflow.
- Min far below Max on some threads: the CPU is throttling (heat or power limit). Watch CPU
  Frequency drop at the same time.
- A sudden reboot or freeze during the burn: the system is not stable. Undo the latest overclock,
  undervolt or memory change.
- Hottest SIO sensor reading very high (80°C or more) with a cool case: usually a disconnected
  sensor input. SRS already ignores AUXTIN, PECI, TSI and PCH inputs. You can turn a real one back on
  with `SIO Sensors=chip:LABEL` in `~/.config/srs/SRSDEF.ini`.

**Where Linux gets it.** Load: built-in Whetstone in one pinned thread per CPU. Sensors:
`/proc/stat` (load), `/sys/devices/system/cpu/cpuN/cpufreq/scaling_cur_freq` (clock),
`/sys/class/hwmon` (CPU and Super I/O temperatures), `/sys/class/drm/cardN/device` (`gpu_busy_percent`,
`pp_dpm_sclk`, hwmon temperature). The GPU load command's output goes to `~/.cache/srs/gpu-load.log`.
Finished runs are saved to `~/.cache/srs/burnin-YYYYMMDD-HHMMSS.txt`.

**Background.** Tctl is AMD's "control temperature". It is the value the CPU uses for fan control and
throttling. A Super I/O (SIO) chip is the motherboard chip that reads fan speeds, voltages and board
temperatures. A burn-in finds weak cooling and unstable settings now, at your desk, instead of in the
middle of a game or a long job.

---
## CPU Stress  
`cpu-stress` - Linux data

**In plain words.** The same load engine as [CPU Burnin](#cpu-burnin), but with no timer. The CPUs
stay 100 % busy until you press Stop (or a safety limit hits). Use it to watch temperatures and clocks
for as long as you like.

**What you see.** In a report: exactly the same lines as [CPU Burnin](#cpu-burnin) (State, Worker
threads, sensors, GPU, Command line, Safety note). In the CPU Stress window (Menu -> CPU Stress...):
the same sensor table, thread table, graph and controls as CPU Burnin, but without Burn Time, Cool
Time and Cycles.

**Pass and Fail.** Stress has no "finished" point. It ends as **Stopped** when you press Stop, as
**Fail** if the hottest CPU sensor reaches the temperature limit or an affinity check fails, or as
**Pass** if the overall time cap (750 hours) is reached. Either way the summary is saved to
`~/.cache/srs/burnin-YYYYMMDD-HHMMSS.txt`, as for [CPU Burnin](#cpu-burnin).

**Healthy / typical values.** As for CPU Burnin: temperatures climb for a few minutes and then level
off. Clocks stay steady.

**Unusual values and what they suggest.** Temperatures that keep rising after 10-15 minutes and never
level off: the cooler is saturated (for example an AIO whose liquid keeps heating up). Improve
airflow or the fan curve.

**Where Linux gets it.** The same sources as [CPU Burnin](#cpu-burnin).
