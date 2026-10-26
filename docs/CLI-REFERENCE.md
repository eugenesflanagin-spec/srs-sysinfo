# SRS - Command-Line Reference

[Back to the User Guide](USER-GUIDE.md) - [Section Reference](SECTIONS/README.md) - [Glossary](GLOSSARY.md)

I wanted a Linux version of SIV (System Information Viewer) for myself, so I built one and set out to improve on it along the way. SRS is free and open source for the Linux and open-source community. (SRS is an independent project, not affiliated with SIV or its author.) SIV is the
work of Ray Hinchliffe, RH Software (rh-software.com).

---

## In plain words

SRS has four programs:

| Program | What it is |
|---|---|
| `srs-gui` | The window. **Given arguments, it behaves exactly like `sysreport-search`** (except arguments starting with `--g`, which are window options). |
| `sysreport-search` | The command line: text reports, device-ID lookup, report search, burn-in, benchmarks. Needs no desktop. |
| `srs-root` | The root helper that saves root-only data to `~/.cache/srs/root`. |
| `srs-sysreport` | Writes a full text report folder to `~/sysreports/`. |

So `srs-gui -save` and `sysreport-search -save` do the same thing.

> **Note:** `sysreport-search --help` lists only `id`, `find` and `devices`. The other commands
> below (`-save`, `--list`, `burn`, `bench`, `ipi`, `latency`) work the same way but are not in
> the built-in help text.

> **zsh users:** quote anything with square brackets: `sysreport-search '-save=[cpuid]'`.
> Without quotes zsh treats `[...]` as a file pattern and says "no matches found".

## Quick list

```
sysreport-search -h | --help
sysreport-search --version
sysreport-search --list
sysreport-search -save[=FILE] [--redact]
sysreport-search '-save=[item][item]...[=FILE]' [--redact]
sysreport-search guide [ITEM] [--print]
sysreport-search id VVVV[:DDDD[:SSSS:ssss]][:RR] [--bus pci|usb] [--mode tree|scan|info]
sysreport-search find PATTERN [--report DIR] [-i] [-F] [--max N] [--color auto|always|never]
sysreport-search devices [--bus pci|usb|all]
sysreport-search burn [--threads N | --cpus 0,2,5] [--seconds S] [--mode same|swap] [--limit C] [--cool S] [--cycles N]
sysreport-search bench ac|as|dd|dw|ec|ep|lc|lp|pc|pp [--seconds S]
sysreport-search ipi [--cpus N] [--rounds R]
sysreport-search latency [--max MB]
sysreport-search pi [--digits 128k|1m|..|32m|16ki|..|1mi|..|32mi|N] [--threads N] [--method chudnovsky|agm] [--save-digits FILE]
sysreport-search crypto [--threads N] [--seconds S]
sysreport-search prime [--limit 1e8|1e9|1e10|1e11|N] [--threads N]
sysreport-search storage --dir FOLDER [--size MB] [--seconds S] [--profile standard|light] [--allow-usb] [--yes]
sysreport-search cachemem [--threads N] [--seconds S] [--isa avx512|avx2|sse2] [--mem-mib M] [--only mem,l1,l2,l3] [--tests read,write,copy,latency] [--mib] [--mem-write auto|clzero|stream] [--yes]

srs-gui [--gui-open=WHAT[,WHAT...]] [--gui-tray] [--gui-classic | --gui-new]
srs-gui --power-recorder
srs-root [--user NAME]
srs-sysreport
```

---

## Information commands (safe, read-only)

### `--help`, `-h`

Prints the usage text: every command, `-save`, `--list`, `guide`, the load commands and the
`srs-gui` start options. Exit code 0.

```
usage: sysreport-search [-h] [--version] {id,find,devices,-save,--list,burn,bench,ipi,latency} ...

SIV-style search for the native system report (srs-sysreport).

  id VVVV[:DDDD[:SSSS:ssss]][:RR] [--bus pci|usb] [--mode tree|scan|info]
        hierarchical device-ID lookup, most specific key first
  find PATTERN [--report DIR] [-i] [-F] [--max N] [--color auto|always|never]
        regex search across a report's sections with highlighted matches
  devices [--bus pci|usb|all]
        resolve every local PCI/USB device

System information items (SIV syntax; quote the argument in zsh/fish, [..] is a glob):
  -save[=[item][item:arg]...][=FILE|STDOUT] [--redact]
        render items as text (no items = SIV's default set, [all] = every item)
        --redact masks serial numbers, UUIDs, MAC addresses (vendor prefix kept),
        boot/machine IDs and the host name, for reports you share
  --list
        every item: id, category, bucket (linux/absent/windows-only), group, title
  guide [ITEM] [--print]
        open the user guide at ITEM's section (PDF viewer, else the HTML guide);
        --print only shows the section and the command that would run

Benchmarks and stress (load the CPU; run only when you ask):
  burn [--threads N | --cpus 0,2,5] [--seconds S] [--mode same|swap] [--limit C] [--cool S] [--cycles N]
        CPU burn-in with a temperature limit (default 10 s, limit 90 C)
  bench {ac,as,dd,dw,ec,ep,lc,lp,pc,pp} [--seconds S]
        Dhrystone / Whetstone in a SIV benchmark mode (S = 0.1..60 s per measurement)
  ipi [--cpus N] [--rounds R]
        core-to-core (cache-line ping-pong) latency matrix
  latency [--max MB]
        memory latency by block size, up to MB (1..4096, default 256)
  pi [--digits 128k|1m|..|32m|16ki|..|1mi|..|32mi|N] [--threads N] [--method chudnovsky|agm] [--save-digits FILE]
        SRS Pi: pi to N decimal places (default 1m, 1 thread), every digit checked;
        ki / mi = SuperPi sizes (powers of two): 1mi = 1,048,576 digits, 32mi = 33,554,432
  crypto [--threads N] [--seconds S]
        AES-128 and SHA-256 speed, plain C and with AES-NI / SHA-NI (default all threads, 1 s)
  storage --dir FOLDER [--size MB] [--seconds S] [--profile standard|light] [--allow-usb] [--yes]
        read / write speed of the drive holding FOLDER, on a temporary test file (default 1024 MB,
        5 s per test); never touches existing files; asks first, --yes starts at once
  cachemem [--threads N] [--seconds S] [--isa avx512|avx2|sse2] [--mem-mib M] [--only mem,l1,l2,l3]
           [--tests read,write,copy,latency] [--mib] [--mem-write auto|clzero|stream] [--yes]
        Cache & Memory benchmark: Read / Write / Copy and Latency of the RAM and L1-L3 (all threads,
        ~20 s; asks first, --yes starts at once)
  prime [--limit 1e8|1e9|1e10|N] [--threads N]
        count the primes up to the limit, checked against the known count (default 1e9, all threads)

srs-gui only: --gui-open=ITEM[,ITEM|burnin|stress|bench|ipi|latency|pi|crypto|prime|cachemem|storagebench|sensors|idlookup|find]
        open those panels / tools at start (also SRS_OPEN=...), --gui-tray start in the tray
        --gui-classic start in the classic SIV-style main window, --gui-new in the new one
        --gui-open=power-cost the Power & cost page; --power-recorder record its history with no window
        (--gui-scan-empty is internal: the main window runs it in a child process to find
        the items that have nothing to show on this PC)
```

### `--version`

```
sysreport-search 2.0.0 (ELF x86-64, gcc 16.2.1 20260810, glibc (system C runtime))
Inspired by SIV64X (System Information Viewer) by Ray Hinchliffe, RH Software (rh-software.com),
built to improve on it; free for the Linux and open-source community.
Independent project, not affiliated with SIV or its author. GPL-2.0-or-later.
Home page: https://github.com/eugenesflanagin-spec/srs-sysinfo
```

The part in brackets describes how your copy was built.

### `--list`

Lists every item SRS knows (551 lines), one per line:

```
about                tuning    linux         siv      About SRS
acpi-buses           hardware  linux         machine  ACPI PCI root buses
acpi-ipf             hardware  absent        machine  ACPI IPF (Intel Platform Framework) methods
```

| Column | Meaning |
|---|---|
| 1 | Item id - use it in `-save=[id]` and `--gui-open=id` |
| 2 | Category (SIV's: hardware, tuning, ...) |
| 3 | Bucket: `linux` (real Linux data), `absent` (hardware most PCs lack), `windows-only` |
| 4 | Group (the Menu group: cpu, machine, sensors, storage, network, os, gpu, bus, tools, siv) |
| 5 | Title |

What each item shows: [Section Reference](SECTIONS/README.md).

### `-save` - text reports (SIV syntax)

| Form | What it does |
|---|---|
| `-save` | SIV's default set of items, to the screen. |
| `-save=FILE` | Default items, to FILE. |
| `-save=[id1][id2]` | Chosen items, to the screen. |
| `-save=[id1][id2]=FILE` | Chosen items, to FILE. |
| `-save=[all]` | Every item (about 52,000 lines and 12 seconds on the author's PC). |
| `-save=...=STDOUT` | The file name `STDOUT` means the screen. |

- `-SAVE` works too (case does not matter), as in SIV.
- Some items take an argument after a colon, the same as the panel buttons:
  `[processor:4]` (CPU-4), `[cache:2]`, `[volumes:/]`, `[ntfs:/]`.
- An unknown id prints `No such item` for that item (exit code still 0).
- `--redact` (anywhere on the command line) masks identifiers for reports you share: serial
  numbers (board, system, chassis, DIMM, disk, NVMe, USB, monitor), UUIDs (file systems,
  partitions, the system UUID, the root file system in the kernel command line), MAC addresses
  (the vendor half is kept: `04:7c:16:XX:XX:XX`), WWNs, the GPU and Thunderbolt unique IDs, the
  boot and machine IDs, and the host name. Letters and digits become `X` and the length is kept, so
  columns stay aligned; the header says `(identifiers redacted)`. Published constants such as
  GPT partition-type GUIDs are left as they are. Without `--redact` the report is complete, as in SIV.
  IP addresses and Wi-Fi network names are not masked.
- Saving never starts the burner or a benchmark. Benchmark items print their last stored result.
- Root-only values come from `~/.cache/srs/root` (run `srs-root` first). Run as root
  (`sudo sysreport-search -save`) it uses the invoking user's cache, or collects one first.

Examples (all verified):

```
sysreport-search -save | less
sysreport-search '-save=[cpuid][cache]=cpu.txt'
srs-gui '-save=[sensors]'
sysreport-search '-save=[all]=full-report.txt'
sysreport-search '-save=[all]=for-forum.txt' --redact
```

Output starts with a header and then each item:

```
srs-gui - System Report Search V2.0.0 MYPC 2026-10-25 08:28:53  [[cpuid][cache]]

[cpuid]  [hardware]
...
```

### `guide` - open the user guide at an item

```
sysreport-search guide [ITEM] [--print]
```

Opens this guide at ITEM's section (no ITEM: the start). With Okular, Evince or Papers as the
default PDF viewer the PDF opens at the section; with any other viewer the HTML copy of the guide
(`/usr/share/doc/srs-sysinfo/html/SRS-User-Guide.html`) opens in the web browser at the section.
`--print` only prints the section and the command that would run:

```
$ sysreport-search guide sensors --print
Item     sensors
Section  sections-sensors--sensor-status
Command  'okular' '/usr/share/doc/srs-sysinfo/SRS-User-Guide.pdf#sections-sensors--sensor-status'
```

Exit code 0, 1 when the guide is not installed, 2 for an unknown item. In `srs-gui` the same is
the **Guide** button on every panel, **F1** and the panel's right-click menu.

### `id` - device-ID lookup

Turns PCI or USB numbers into names, using hwdata's `pci.ids` / `usb.ids` (or systemd-hwdb).

| Argument / option | Meaning | Default |
|---|---|---|
| `VVVV` | Vendor ID (4 hex digits) | required |
| `:DDDD` | Device ID | |
| `:SSSS:ssss` | Subsystem vendor and subsystem device (the card maker's IDs) | |
| `:RR` | Revision (2 hex digits) | |
| `--bus pci\|usb` | Which list | `pci` |
| `--mode tree\|scan\|info` | `tree`: load the sorted list once, binary search (fast). `scan`: read the file line by line. `info`: ask systemd-hwdb, the system's device database. | `tree` |

Options can be written `--bus usb` or `--bus=usb`.

```
$ sysreport-search id 1002:7550
  < 1002:7550                Navi 48 [Radeon RX 9070/9070 XT/9070 GRE]
    1002                     Advanced Micro Devices, Inc. [AMD/ATI]
Vendor     Advanced Micro Devices, Inc. [AMD/ATI]
Stock      Navi 48 [Radeon RX 9070/9070 XT/9070 GRE]
Resolved   Navi 48 [Radeon RX 9070/9070 XT/9070 GRE]
tree mode: 42,228 records loaded in 9.4 ms, lookup 0.002 ms
```

- Each key tried is listed, most specific first; `<` marks the one that matched.
  `(no entry)` means that exact key is not in the list.
- **Vendor** - the maker. **Subsystem** - the card maker, when a subsystem ID is given and known
  (e.g. `1002:7550:1EAE:7550:C0` -> `XFX Limited`). **Stock** - the chip's plain name.
  **Resolved** - the most specific name found.

```
$ sysreport-search id 046d:c52b --bus usb
  < 046D:C52B                Unifying Receiver
    046D                     Logitech, Inc.
...
$ sysreport-search id 1002:7550 --mode info
Query      pci:v00001002d00007550
Vendor     Advanced Micro Devices, Inc. [AMD/ATI]
Resolved   Navi 48 [Radeon RX 9070/9070 XT/9070 GRE]
info mode (systemd-hwdb) in 5.00 ms
```

To find the IDs of your own devices: `sysreport-search devices`, or `lspci -nn` / `lsusb`.

### `devices` - every PCI and USB device, named

| Option | Meaning | Default |
|---|---|---|
| `--bus pci\|usb\|all` | Which buses | `all` |

```
$ sysreport-search devices --bus usb
[USB] 40 devices, 23,964 records (/usr/share/hwdata/usb.ids)
Slot           ID                         Matched              Vendor                       Resolved
1-2.4          0BDA:5411                  0BDA:5411            Realtek Semiconductor Corp.  RTS5411 Hub
1-2.4.5        0BDA:1100                  0BDA                 Realtek Semiconductor Corp.  Unknown Device
```

- **Slot** - where it is plugged in (PCI address, or USB bus-port path).
- **ID** - its full ID. **Matched** - the most specific key found in the list
  (`0BDA` alone means only the vendor is known). **Resolved** - the name, or "Unknown Device"
  when the list does not have it yet (update the `hwdata` package).

### `find` - search a full system report

Searches every section file (`*.txt`, `lshw.html`) of a folder made by `srs-sysreport`.

| Argument / option | Meaning | Default |
|---|---|---|
| `PATTERN` | A regular expression (Python-style), unless `-F` | required |
| `--report DIR` | Which report folder | the newest in `~/sysreports` |
| `-i`, `--ignore-case` | Ignore upper/lower case | off |
| `-F`, `--fixed` | Literal text, not a regular expression | off |
| `--max N` | Show at most N matches per section | 20 |
| `--color auto\|always\|never` | Highlight matches (auto = only on a terminal) | auto |

```
$ sysreport-search find 'RX 9070' --max 3 --color never
Searching /home/user/sysreports/sysreport-20261017-050453 for "RX 9070"

== devices.txt - 1 Match
     30: 0000:03:00.0   1002:7550:1EAE:8811:C0     1002:7550            Advanced Micro Devices, Inc. Navi 48 ...

== inxi.txt - 3 Matches
     59:   Device-1: Advanced Micro Devices [AMD/ATI] Navi 48 [Radeon RX 9070/9070
...
7 Matches in 4 of 16 sections
```

Numbers on the left are line numbers in that section file. When a section has more matches than
`--max`, a line says `... N more (raise --max)`.

---

## Load and benchmark commands (make the CPU busy)

These **start immediately, without asking** (the window always asks first). Close other heavy
programs first. Press **Ctrl+C** to stop. Never run them on a PC you are not allowed to load.

### `burn` - CPU Burnin from the command line

| Option | Meaning | Default |
|---|---|---|
| `--threads N` | Use logical CPUs 0..N-1 | all |
| `--cpus 0,2,5` | Use exactly these logical CPUs | all |
| `--seconds S` | Burn time per cycle, 1 to 2,700,000 seconds | 10 |
| `--mode same\|swap` | `same`: each worker stays on its CPU and runs Whetstone. `swap`: workers move between CPUs (affinity test). | same |
| `--limit C` | Stop if the hottest CPU sensor reaches C degrees Celsius | 90 |
| `--cool S` | Rest between cycles, seconds | 0 |
| `--cycles N` | Number of burn + cool cycles | 1 |

Output: a start line, then one line per second (example values):

```
    12s  load 100.0%  Tctl 78.5 C  CPUTIN 33.0 C  CPU 5050 MHz  GPU 3% 28.0 C  Burning
```

(elapsed, CPU load, hottest core sensor, hottest motherboard sensor, CPU clock, GPU load and
temperature, state), then a table per worker (Thread, CPU, Passes, MWIPS last / average / min / max,
Affinity `ok` or `FAIL`), the maxima and the final status. The last line names the file the summary
was saved to (`Summary saved to /home/you/.cache/srs/burnin-YYYYMMDD-HHMMSS.txt`, one file per run; the same
layout as srs-gui's Burnin window).

Exit codes: **0 = Pass**, **1 = Fail** (affinity error or temperature limit), **130 = stopped**
(Ctrl+C), **2 = bad option or nothing to run**.

An unknown option prints the usage line and exits 2 **without starting anything**:

```
$ sysreport-search burn --nope
usage: sysreport-search [-h] [--version] {id,find,devices} ...
sysreport-search: error: burn [--threads N | --cpus 0,2,5] [--seconds S] [--mode same|swap] [--limit C] [--cool S] [--cycles N]
```

### `bench` - Dhrystone and Whetstone

```
sysreport-search bench MODE [--seconds S]
```

| Mode | What runs |
|---|---|
| `ac` | All CPUs concurrently: Dhrystone then Whetstone on every logical CPU at once |
| `as` | All CPUs sequentially, one logical CPU at a time |
| `dd` | Per core: both SMT threads run Dhrystone, then both run Whetstone |
| `dw` | Per core: one thread Dhrystone while its sibling runs Whetstone, then swap |
| `ec` / `ep` | Efficiency cores concurrently / one at a time (Intel hybrid CPUs; Linux lists the E-cores in `/sys/devices/cpu_atom/cpus`) |
| `lc` / `lp` | Logical (SMT sibling) threads concurrently / one at a time |
| `pc` / `pp` | Performance (physical) cores, one thread per core, concurrently / one at a time (E-cores left out) |

`--seconds S` is the time per measurement, 0.1 to 60 (default 1). The result is
stored and shown in the window and in `-save` reports. Results: DMIPS (Dhrystone) and MWIPS
(Whetstone), per CPU and in total; see [tools.md](SECTIONS/tools.md#cpu-benchmarks).
Exit code 0 when it ran, 1 when there is nothing to run in that mode on this CPU (for example
`lc` without SMT), 2 for a bad mode.

### `ipi` - core-to-core delay

```
sysreport-search ipi [--cpus N] [--rounds R]
```

`--cpus N` tests CPUs 0..N-1 (default all), `--rounds R` round trips per pair (default 20,000).
Prints a matrix of nanoseconds, one row and column per CPU. See
[tools.md](SECTIONS/tools.md#cpu-ipi-delay).

### `latency` - memory latency by block size

```
sysreport-search latency [--max MB]
```

`--max MB` is the largest block, 1 to 4096 MB (default 256; the window allows up to 1024). Prints
block size, nanoseconds per read and the cache level. See
[tools.md](SECTIONS/tools.md#memory-latency).

### `pi` - SRS Pi: pi to many digits, every digit checked

```
sysreport-search pi [--digits 128k|1m|..|32m|16ki|..|1mi|..|32mi|N] [--threads N] [--method chudnovsky|agm] [--save-digits FILE]
```

`--digits` is the number of decimal places (default `1m` = 1,000,000; `k` = thousand, `m` =
million; `ki` = 1,024 and `mi` = 1,048,576 for the **SuperPi sizes** 16ki ... 32mi; 100 to 32mi). The listed sizes are checked digit for digit with built-in SHA-256
checksums; other sizes only have their first 100 digits checked. `--threads N` (default 1) splits
the Chudnovsky series over N threads; `--method agm` uses Gauss-Legendre (always one thread).
`--save-digits FILE` writes the digits (without "3.") to FILE. Exit code 0 when every checked digit
is right, 1 when a digit is wrong or memory ran out, 2 for a bad option. See
[tools.md](SECTIONS/tools.md#srs-pi).

### `crypto` - AES-128 and SHA-256 speed

```
sysreport-search crypto [--threads N] [--seconds S]
```

Measures AES-128-CTR and SHA-256 in MB/s on one thread and on N threads (default all), with
AES-NI / SHA-NI when the CPU has them and in plain C. `--seconds S` per measurement, 0.1 to 60
(default 1). The FIPS test vectors are checked first; exit code 1 if they fail. See
[tools.md](SECTIONS/tools.md#crypto-benchmark).

### `prime` - count the primes up to a limit

```
sysreport-search prime [--limit 1e8|1e9|1e10|1e11|N] [--threads N]
```

Counts the primes up to `--limit` (default 1e9; 100 to 1e11) on N threads (default all) and checks
the count for the powers of ten 1e7 to 1e11. Exit code 1 when the count is wrong. See
[tools.md](SECTIONS/tools.md#prime-sieve).

### `cachemem` - Cache & Memory benchmark

```
sysreport-search cachemem [--threads N] [--seconds S] [--isa avx512|avx2|sse2] [--mem-mib M]
                          [--only mem,l1,l2,l3] [--tests read,write,copy,latency] [--mib]
                          [--mem-write auto|clzero|stream] [--yes]
```

Read / Write / Copy (MB/s, caches in GB/s) and Latency (ns) of the RAM and the L1, L2 and L3
caches. It loads every CPU thread and the memory bus for about 20 seconds and **asks first**;
`--yes` (`-y`) starts at once (scripts). Without a terminal and without `--yes` it does not start
(exit code 2).

| Option | Meaning |
|---|---|
| `--threads N` | Threads for the bandwidth tests (default all logical CPUs) |
| `--seconds S` | Measuring time per value, 0.1-60 (default 1) |
| `--isa` | Force the vector code (default: the best the CPU has) |
| `--mem-mib M` | RAM block in MiB, all threads together (default 1024, at least 4 x L3) |
| `--only` / `--tests` | Run only some rows / columns, e.g. `--only l1 --tests read` |
| `--mib` | Binary units (MiB/s, GiB/s) |
| `--mem-write` | RAM write method: `auto` (CLZERO on AMD CPUs that have it), `clzero`, `stream` |

x86-64 only. Exit code 0 when it ran, 1 if memory could not be allocated, 2 for a bad option or no
confirmation. See [tools.md](SECTIONS/tools.md#cache--memory-benchmark).

### `storage` - drive read / write speed

```
sysreport-search storage --dir FOLDER [--size MB] [--seconds S] [--profile standard|light] [--allow-usb] [--yes]
```

Tests the drive that holds FOLDER on a new temporary test file (`--size`, 16-65536 MB, default
1024) that is unlinked at once and gone when the test ends - existing files are never touched.
Six tests of `--seconds S` each (0.5-60, default 5): sequential 1 MiB Q8T1, 4K random Q32T16
(`--profile light`: Q8T4) and 4K random Q1T1, read and write; average, peak, IOPS and 4K Q1T1
response times. Refuses RAM (tmpfs), network and FUSE folders and too little free space; USB
drives only with `--allow-usb`. **Asks first**; `--yes` starts at once. Exit code 0 when it ran,
1 when it was stopped or hit an I/O error, 2 when it did not start (the message says why). See
[tools.md](SECTIONS/tools.md#storage-benchmark).

Every `cachemem`, `storage`, `pi`, `crypto` and `prime` run is added to `~/.cache/srs/bench-history.tsv`.

---

## `srs-gui` window options

| Option | What it does |
|---|---|
| (none) | Open the main window (the window with the sidebar, or the classic one when **Options -> Start in classic view** is on). |
| `--gui-open=WHAT[,WHAT...]` | Also open these panels or tools at start. In the main window an item opens on its page (for example `sensors` on the Sensors page); in the classic view it opens as its own panel. |
| `--gui-tray` | Start hidden in the system tray (used by the autostart entry). |
| `--gui-classic` | Start in the classic view: SIV's main window with its button row, every item in its own panel (as in SRS 1.1). |
| `--gui-new` | Start in the main window with the sidebar, even when **Start in classic view** is on. |
| `--power-recorder` | Record **Power & cost** history with no window, following the page's Recording setting (Off / For a set time / Always). Used by the optional systemd user service that **Power & cost -> Setup -> Keep recording when SRS is closed** writes. Only one process records at a time (a lock file beside the database); stop it with Ctrl+C or SIGTERM. |
| any other argument | Run as the command line (`srs-gui -save` = `sysreport-search -save`). |

`WHAT` can be any item id from `--list` (e.g. `sensors`, `processors`, `pci-bus`, `processor:4`),
or one of these tool names:

| Name | Opens |
|---|---|
| `burnin` | CPU Burnin |
| `stress` | CPU Stress |
| `bench` | Benchmark |
| `ipi` | IPI Delay |
| `latency` | Latency |
| `pi` | SRS Pi |
| `crypto` | Crypto benchmark |
| `prime` | Prime Sieve |
| `cachemem` | Cache & Memory |
| `storagebench` | Storage benchmark (`storage` alone is the Storage page's item) |
| `idlookup` | Device ID Lookup |
| `find` | Find |
| `menu` | Pops up the Menu |
| `power-cost` | Shows the Power & cost page (the item `power` is the expert Power status tab on Sensors) |

Opening a tool **never starts it**. Example: `srs-gui --gui-open=sensors,burnin`.
The environment variable `SRS_OPEN=sensors,burnin` does the same.

The menu entries use: `--gui-open=burnin,stress` (SRS CPU Burnin & Stress),
`--gui-open=bench,ipi,latency` (SRS Benchmark), and the jump-list actions `sensors`, `processors`,
`burnin`, `stress`, `bench`, `idlookup`, `find`, and **Classic View** (`--gui-classic`).

---

## `srs-root`

```
pkexec srs-root            # or: sudo srs-root
sudo srs-root --user NAME  # collect for another user (used by the boot unit)
```

Collects root-only data into `~/.cache/srs/root/` of the user who started it (via `PKEXEC_UID`
or the current user), then gives the files to that user. It prints one line per step:

```
srs-root: dmidecode
srs-root: decode-dimms
srs-root: smart
srs-root: acpi-tables
srs-root: lspci
srs-root: msrs
srs-root: done -> /home/you/.cache/srs/root
```

- It has no `--help`; any argument other than `--user NAME` is ignored.
- `--user NAME` works only as root; exit code 2 if not root or the user does not exist.
- Set `SRS_SMART_USB=1` to include USB-attached disks in the SMART collection.
- Tools that are not installed are skipped.

Boot unit: `srs-root@NAME.service` runs `srs-root --user NAME` once per boot (Nice 19, idle I/O).
`sudo systemctl enable --now srs-root@$USER.service` / `disable`.

## `srs-sysreport`

```
srs-sysreport
```

No options. Writes `~/sysreports/sysreport-YYYYMMDD-HHMMSS/` (one file per tool plus `index.html`),
asks once for your password (pkexec) for the root sections, opens `index.html` in your browser and
prints:

```
Report written to: /home/you/sysreports/sysreport-20261017-050453
Search it with: sysreport-search find PATTERN   (or: sysreport-search id VVVV:DDDD)
```

## Exit codes (sysreport-search)

| Code | Meaning |
|---|---|
| 0 | Success; `find` found matches; `burn` passed |
| 1 | `find` found no matches; lookup or benchmark failed; `burn` failed |
| 2 | Usage error (missing or unknown argument, bad value); `find` found no report folder; `burn` had nothing to run |
| 130 | `burn` stopped with Ctrl+C |

## Environment variables

| Variable | Effect |
|---|---|
| `SRS_SMART_USB=1` | `srs-root`: include USB disks in SMART |
| `SRS_KWH_COST`, `SRS_CURRENCY` | Price per kWh and currency symbol for the kWh Cost item (default `$` 0.15) |
| `SRS_OPEN` | Same as `--gui-open=` |
| `SRS_POWER_DB`, `SRS_POWER_INI` | Power & cost: use another history database / settings file instead of `~/.local/share/srs/power.db` and `~/.config/srs/power.ini` (for tests) |
| `SRS_DATA_DIR` | Folder holding `power-zip-rates.tsv` for the offline US ZIP price lookup (default: next to the program, then `/usr/share/srs-sysinfo`) |
| `SRS_DOC_DIR` | Folder holding `SRS-User-Guide.pdf` / `html/` for `guide`, F1 and the Guide button (default `/usr/share/doc/srs-sysinfo`) |
| `SUDO_USER`, `PKEXEC_UID` | Used to find whose root-data cache to use when run as root |
