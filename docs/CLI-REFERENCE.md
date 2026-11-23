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

> **Note:** `sysreport-search --help` lists every command, including `-save`, `--list`, `burn`,
> `bench`, `ipi`, `latency`, `pi`, `crypto`, `prime`, `cachemem`, `storage`, `os-latency`, `compute`, `work`, `net`, `gpu`, `gpu-burn`, `gpu-compare`, `gpu-tune`, `cuda-toolkit`, `gpu-top` (also `big-top`), `ir` and `update-lists`.

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
sysreport-search burn [--threads N | --cpus 0,2,5] [--seconds S] [--mode same|swap] [--limit C] [--accept-high-limit] [--cool S] [--cycles N]
sysreport-search bench ac|as|dd|dw|ec|ep|lc|lp|pc|pp [--seconds S] [--yes]
sysreport-search ipi [--cpus N] [--rounds R] [--yes]
sysreport-search latency [--max MB] [--yes]
sysreport-search pi [--digits 128k|1m|..|32m|16ki|..|1mi|..|32mi|N] [--threads N] [--method chudnovsky|agm] [--save-digits FILE] [--yes]
sysreport-search crypto [--threads N] [--seconds S] [--all-threads-only] [--yes]
sysreport-search prime [--limit 1e8|1e9|1e10|1e11|N] [--threads N] [--yes]
sysreport-search storage --dir FOLDER [--size MB] [--seconds S] [--profile standard|light] [--allow-usb] [--yes]
sysreport-search cachemem [--threads N] [--seconds S] [--isa avx512|avx2|sse2] [--mem-mib M] [--only mem,l1,l2,l3] [--tests read,write,copy,latency] [--mib] [--mem-write auto|clzero|stream] [--yes]
sysreport-search os-latency [--samples N] [--tick-seconds S] [--only syscall,switch,spawn,wake,timer,memory,files] [--yes]
sysreport-search compute [--threads N] [--seconds S] [--tests sgemm,dgemm,...] [--code auto|c|sse2|avx2|avx512] [--single | --all-threads-only] [--yes]
sysreport-search work [--threads N] [--seconds S] [--only compress,sort,hash,text,interp] [--libs zlib,zstd,lz4,xz] [--single | --all-threads-only] [--part-kib K] [--yes]
sysreport-search net [--local] [--internet] [--seconds S] [--streams N] [--samples N] [--max-mb MB] [-4|-6] [--server URL] [--hosts HOST[:PORT],...] [--yes] [--yes-internet]
sysreport-search gpu [--device N] [--seconds S] [--only fp32,fp16,fp64,int32,read,write,copy,upload,download,latency] [--mem-mib M] [--list] [--yes]
sysreport-search gpu-burn [--seconds N] [--backend vulkan|hip|cuda|opencl|auto] [--device N|all | --pci ADDR] [--fp32|--fp64] [--limit C] [--accept-high-limit] [--list] [--yes] [--json] [--json-live] [--watch-kernel]
sysreport-search gpu-compare [--backends vulkan,hip,cuda,opencl] [--seconds S] [--device N] [--yes]
sysreport-search gpu-tune status [--json] [--pci ADDR | --all] [--wake]
sysreport-search cuda-toolkit [--check|--install|--ask-again|--never-ask]
sysreport-search update-lists [--list] [--yes]
sysreport-search ir [--status|--list|--send REMOTE BUTTON|--send-code HEX|--learn [--save REMOTE BUTTON]|--probe|--reset|--export FILE|--import FILE] [--repeat N] [--dry-run] [--yes]
sysreport-search gpu-top|big-top [--list|--dump|--gpu-metrics|--drm-info|--pp-table|--process|--xdna|--smi|--tui] [-J] [--device N [--wake]] [--pc] [--interval MS] [-n COUNT]

srs-gui [--gui-open=WHAT[,WHAT...]] [--gui-tray | --tray] [--gui-classic | --gui-new]
srs-gui --power-recorder
srs-root [--user NAME]
srs-root --acpi-eval METHOD [ARG...]
srs-root -h | --help
srs-sysreport
```

---

## Information commands (safe, read-only)

### `--help`, `-h`

Prints the usage text: every command, `-save`, `--list`, `guide`, the load commands and the
`srs-gui` start options. Exit code 0.

```
usage: sysreport-search [-h] [--version] {id,find,devices,-save,--list,burn,bench,ipi,latency,pi,crypto,prime,cachemem,storage,os-latency,compute,work,net,gpu,gpu-burn,gpu-compare,gpu-tune,cuda-toolkit,gpu-top,big-top,ir,update-lists} ...

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

Benchmarks and Stress (load the CPU, the drive, the network or the GPU; run only when you ask):
  burn [--threads N | --cpus 0,2,5] [--seconds S] [--mode same|swap] [--limit C] [--accept-high-limit] [--cool S] [--cycles N]
        CPU burn-in with a temperature limit (default 10 s, limit 90 C)
  bench {ac,as,dd,dw,ec,ep,lc,lp,pc,pp} [--seconds S] [--yes]
        Dhrystone / Whetstone in a SIV benchmark mode (S = 0.1..60 s per measurement);
        asks first, --yes starts at once
  ipi [--cpus N] [--rounds R] [--yes]
        core-to-core (cache-line ping-pong) latency matrix; asks first, --yes starts at once
  latency [--max MB] [--yes]
        memory latency by block size, up to MB (1..4096, default 256); asks first, --yes starts at once
  pi [--digits 128k|1m|..|32m|16ki|..|1mi|..|32mi|N] [--threads N] [--method chudnovsky|agm] [--save-digits FILE]
     [--yes]
        SRS Pi: pi to N decimal places (default 1m, 1 thread), every digit checked;
        ki / mi = SuperPi sizes (powers of two): 1mi = 1,048,576 digits, 32mi = 33,554,432;
        asks first (with the time and the memory it needs), --yes starts at once
  crypto [--threads N] [--seconds S] [--all-threads-only] [--yes]
        AES-128 and SHA-256 speed, plain C and with AES-NI / SHA-NI (default all threads, 1 s);
        --all-threads-only skips the 1-thread pass (the GUI's All Cores; about half the time);
        asks first, --yes starts at once
  cachemem [--threads N] [--seconds S] [--isa avx512|avx2|sse2] [--mem-mib M] [--only mem,l1,l2,l3]
           [--tests read,write,copy,latency] [--mib] [--mem-write auto|clzero|stream] [--yes]
        Cache & Memory Benchmark: Read / Write / Copy and Latency of the RAM and L1-L3 (all threads,
        ~20 s; asks first, --yes starts at once)
  storage --dir FOLDER [--size MB] [--seconds S] [--profile standard|light] [--allow-usb] [--yes]
        read / write speed of the drive holding FOLDER, on a temporary test file (default 1024 MB,
        5 s per test); never touches existing files; asks first, --yes starts at once
  os-latency [--samples N] [--tick-seconds S] [--only syscall,switch,spawn,wake,timer,memory,files] [--yes]
        how quickly the OS answers: system calls, context switches, thread / process start, wake-ups,
        timer lateness (with the CPU scheduler in use), page faults, file create / stat / unlink
        (default 20000 samples, 3 s tick test; ~6 s on one or two CPUs; asks first, --yes starts at once)
  compute [--threads N] [--seconds S] [--tests sgemm,dgemm,fma-sp,fma-dp,fft-sp,fft-dp,nbody,mandelbrot,raytrace]
          [--code auto|c|sse2|avx2|avx512] [--single | --all-threads-only] [--yes]
        CPU Compute: matrix multiply, peak multiply-add, FFT, N-body, Mandelbrot, ray tracer in GFLOPS /
        Mpixels/s, 1 thread and N threads (default all), every answer checked; ~10 s, all threads 100%
        busy (--single: one thread; --all-threads-only: N threads only, as All Cores); asks first,
        --yes starts at once
  work [--threads N] [--seconds S] [--only compress,sort,hash,text,interp] [--libs zlib,zstd,lz4,xz]
       [--single | --all-threads-only] [--part-kib K] [--yes]
        Everyday work: compression (system zlib / zstd / lz4 / xz), sorting, checksums, text processing
        and a bytecode interpreter, 1 thread and N threads (default all; --all-threads-only: N threads
        only, as All Cores), every result checked;
        generated data only; asks first, --yes starts at once
  net [--local] [--internet] [--seconds S] [--streams N] [--samples N] [--max-mb MB] [-4|-6]
      [--server URL] [--hosts HOST[:PORT],...] [--yes] [--yes-internet]
        Network: loopback TCP / UDP speed and round trip (default; nothing leaves the computer);
        --internet adds DNS, connect, HTTPS, download and upload tests against speed.cloudflare.com
        (up to ~1000 MB of data). Asks first; --yes answers the loopback question, the internet test
        asks separately and only --yes-internet skips that question
  gpu [--device N] [--seconds S] [--only fp32,fp16,fp64,int32,read,write,copy,upload,download,latency]
      [--mem-mib M] [--list] [--yes]
        GPU compute (Vulkan): FP32 / FP16 / FP64 / INT32 multiply-add (GFLOPS / GIOPS), video memory
        read / write / copy and upload / download (GB/s), dispatch latency (us), every result checked;
        ~15 s at full GPU load; Vulkan is loaded at run time; asks first, --yes starts at once
  gpu-burn [--seconds N] [--backend vulkan|hip|cuda|opencl|auto] [--device N|all | --pci ADDR] [--fp32|--fp64]
           [--limit C] [--accept-high-limit] [--list] [--yes] [--json] [--json-live] [--watch-kernel]
        GPU Burnin: repeated matrix multiplications at full GPU load, compute errors counted
        (0 = PASS), live temperature / power / clock; stops at the temperature limit (default
        100 C), on a driver error or a GPU that stops answering; never changes clocks, voltages
        or fans (default 60 s); asks first, --yes starts at once. --json / --json-live: the result
        as JSON for programs, with exit codes 0 PASS, 10 stopped, 11 FAIL, 12 could not start, 13 ended
        early, 14 GPU reset in the kernel log, 15 temperature limit. --watch-kernel: a GPU reset or ring
        timeout in the kernel log during the run fails it, also when it hit another program
  gpu-compare [--backends vulkan,hip,cuda,opencl] [--seconds S] [--device N] [--yes]
        GPU Compute comparison: the same tests (matrix multiply with the own kernel and the vendor
        library, multiply-add, copy, upload / download, launch time) through every installed back end
        on the same GPU, side by side: fastest marked, % of Vulkan; asks first, --yes starts at once
  gpu-tune status [--json] [--pci ADDR | --all] [--wake]
        GPU Tuning, read-only: what the graphics card is set to right now (voltage offset, clock offset,
        memory clock, power limit), the driver's ranges, Tux Burner's limits and state, LACT's state and saved
        values. It only reads (the kernel's files, two read requests to Tux Burner's service); it never
        changes a setting. Changing GPU settings needs Tux Burner, a separate program
  cuda-toolkit [--check|--install|--ask-again|--never-ask]
        the optional NVIDIA CUDA toolkit (its runtime compiler, NVRTC): whether it is installed, what it
        is for, the install command for this distribution; --install runs that command after you type
        yes (needs a terminal; warns and asks twice when ZLUDA is in use). SRS's CUDA tests are built
        in, so it is optional. --ask-again / --never-ask: whether the GUI may ask about it
  gpu-top [--list|--dump|--gpu-metrics|--drm-info|--pp-table|--process|--xdna|--smi|--tui] [-J] [--device N [--wake]]
          [--pc] [--interval MS] [-n COUNT]       (big-top is the same command)
        The Big Top's GPU part: activity, GRBM blocks, VRAM, sensors, gpu_metrics and processes of every GPU (AMD like
        amdgpu_top; NVIDIA through NVML); read-only, changes nothing
  ir [--status|--list|--send REMOTE BUTTON [--repeat N]|--send-code HEX [--nec]|--learn [--save REMOTE BUTTON]|
     --probe|--reset|--export FILE|--import FILE [--replace]] [--dry-run] [--yes]
        IR Blaster: ELK Smart / OCRUSTAR USB infrared dongles - learn and send remote-control codes, kept in IR
        Remote's remotes.json (shared with that app); --send and --learn ask first, --yes starts at once
  update-lists [--list] [--yes]
        download fresh pci.ids, usb.ids and pnp.ids from their official sources into
        ~/.local/share/srs/lists (no root); SRS then uses the newer of these and the system's copies.
        Each download is checked first; a failed one leaves the copy in use. --list shows the copies in
        use and their versions; asks first, --yes downloads at once
  prime [--limit 1e8|1e9|1e10|1e11|N] [--threads N] [--yes]
        count the primes up to the limit, checked against the known count (default 1e9, all threads);
        asks first, --yes starts at once

srs-gui only: --gui-open=ITEM[,ITEM|stressburn|burnin|stress|bench|ipi|latency|pi|crypto|prime|cachemem|storagebench|oslatency|compute|work|netbench|gpubench|gpuburn|gpucompare|gputune|cudatoolkit|loadterms|sensors|idlookup|find]
        open those panels / tools at start (also SRS_OPEN=...), --gui-tray start in the tray
        --gui-classic start in the classic SIV-style main window, --gui-new in the new one
        --gui-open=power-cost the Power & Cost page; --power-recorder record its history with no window
```

`--gui-scan-empty` (not listed) is internal: the main window runs it in a child process to find the
items that have nothing to show on this PC, only on Options > Check now or Check daily; it never
runs the network items or Ping.

### `--version`

```
sysreport-search 2.6.0 (ELF x86-64, gcc 16.2.1 20261122, glibc (system C runtime))
Inspired by SIV64X (System Information Viewer) by Ray Hinchliffe, RH Software (rh-software.com),
built to improve on it; free for the Linux and open-source community.
Independent project, not affiliated with SIV or its author. GPL-2.0-or-later.
Home page: https://github.com/eugenesflanagin-spec/srs-sysinfo
```

The part in brackets describes how your copy was built.

### `--list`

Lists every item SRS knows (560 lines), one per line:

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
srs-gui - System Report Search V2.6.0 MYPC 2026-10-26 08:28:53  [[cpuid][cache]]

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

### `gpu-top` / `big-top` - The Big Top's GPU view

```
sysreport-search gpu-top [--list | --dump | --gpu-metrics | --drm-info | --pp-table | --process | --xdna |
                          --smi | --tui] [-J|--json] [--device N [--wake]] [--pc] [--interval MS] [-n COUNT]
sysreport-search big-top ...        (the same command, named after the window; 2.5)
```

**In plain words:** what each graphics card is doing right now, in a terminal: the GPU part of The
Big Top window (User Guide 6.13). It is a C reimplementation of amdgpu_top by Umio Yasuno for AMD
cards, with NVIDIA cards through NVML, and prints the same data in the same shapes (text and JSON).
**Read-only:** it never changes a clock, a voltage, a fan or a power limit. Needs no root.

Pick one view (at most one):

| Option | Shows |
|---|---|
| `--list` | The GPUs: number, name, PCI bus, vendor, device and revision ID, driver, device nodes, and "asleep (runtime suspended)" for a card that is asleep (listing never wakes it) |
| `--dump`, `-d` | Device info, in amdgpu_top's `-d` order: chip, type, family, ASIC, shader engines / arrays, compute units, render back ends, ROPs, peak fill rate, clocks and overclock maximum, peak FP32 / FP16, VRAM type / width / vendor / clock / peak bandwidth / Resizable BAR / ECC, VRAM / CPU-visible VRAM / GTT use, sensors (temperatures with critical / emergency limits, power, power cap, fan, PCIe links, FCLK / SOCCLK), power profiles, caches (with Infinity Cache), hardware IP, IP discovery, firmware, video caps, VBIOS, pp_feature_mask and the gpu_metrics version |
| `--gpu-metrics` | Every field of the driver's gpu_metrics table (versions 1.0-1.5, 2.0-2.4, 3.0) |
| `--drm-info` | Display connectors, their modes and properties (through libdrm; monitors are not probed again) |
| `--pp-table` | The PowerPlay table (Radeon RX 5000 / 6000 / 7000; other cards: "not available on this card") |
| `--process` | The processes using each GPU, with their VRAM and GTT |
| `--xdna` | The AMD XDNA NPU (Ryzen AI), from `/sys/class/accel` |
| `--smi` | A compact table, one block per GPU, after one interval (one sample; amdgpu_top's `--smi` is live - use `--tui` for that) |
| `--tui` | A live terminal view of every GPU; `q` or Ctrl+C quits |

With no view option it runs `--tui` on a terminal and `--smi` otherwise.

| Option | Meaning | Default |
|---|---|---|
| `-J`, `--json` | JSON in amdgpu_top's shapes. Alone (no view option) it prints one JSON sample per interval | off |
| `-n COUNT`, `--count COUNT` | How many JSON samples; `0` = until Ctrl+C | 0 |
| `--device N`, `-i N` | Only GPU N (the number from `--list`) | every GPU |
| `--wake` | With `--device N`: wake GPU N up and read it when it is asleep (runtime suspended) | off |
| `--pc` | Read the GRBM / GRBM2 performance counters for `--smi`, `--tui` and the `-J` stream. Off by default, as in the window: reading them can keep a card out of its power-saving states. The one-shot views never read them. `--no-pc` is still accepted and does nothing | off |
| `--interval MS`, `-s MS` | Sampling interval, 100-60000 ms, kept exactly (the counters are read in 10 ms steps) | 1000 |

`--tui` and `--json` do not go together. **Sleeping cards are not opened:** a card that is asleep
(runtime suspended, typically a laptop's second GPU) is skipped with "asleep (runtime suspended) - not
read", so the command never wakes it. `--device N` alone on a sleeping card exits with 2 and says to
add `--wake`; `--device N --wake` wakes it and reads it. An awake card that no program uses is also
left free to fall asleep: values that would keep it awake are not read (the same rule as the window).

```
$ sysreport-search gpu-top --list
$ sysreport-search gpu-top --smi
$ sysreport-search gpu-top --gpu-metrics --device 0
$ sysreport-search big-top -J -n 5 --interval 500
```

Libraries, each loaded at run time only when needed (none is needed to build SRS):
`libdrm_amdgpu.so.1` (AMD device info and GRBM), `libdrm.so.2` (`--drm-info`) and
`libnvidia-ml.so.1` (only when an NVIDIA card, PCI vendor 0x10de, is present; never on an AMD-only PC).

Exit code 0: printed; 1: no GPU found or a GPU could not be opened; 2: a bad option or value, two
views at once, `--tui` with `--json`, a `--device` that does not exist, or `--device N` on a sleeping
card without `--wake`.

### `gpu-tune` - GPU Tuning (read-only, 2.6)

```
sysreport-search gpu-tune status [--json] [--pci ADDR | --all] [--wake]
```

Shows what a graphics card is **set to right now** - voltage offset, core clock offset, highest memory
clock, power limit - the driver's own ranges, the live readings, whether **Tux Burner** is installed and
its service runs (with the voltage floor the service reports), and whether **LACT** runs, with LACT's saved
values for the card and whether the card is at them. The same text as the **GPU Tuning** tab
([tools.md](SECTIONS/tools.md#gpu-tuning) explains every line).

**It only reads**, and it does not ask before it starts, because it loads nothing and changes nothing: it
reads the kernel's files under `/sys`, sends two read requests to Tux Burner's service socket (`status` and
`gpu_capabilities` - SRS has no code that sends anything else) and reads LACT's settings file. It never
changes a clock, a voltage, a power limit or a fan, never contacts LACT's service and never runs Tux
Burner's command line tool. `status` is the only `gpu-tune` command in this version; the commands that will
change a setting through Tux Burner (`apply`, `auto-uv`, `restore` ...) answer "not in this version" and
exit with 2.

| Option | Meaning |
|---|---|
| `--json` (`-J`) | One JSON object for the card (with `--all`: a list of them) |
| `--pci ADDR` | The card at this PCI address, as in `0000:03:00.0`. Default: the first card with tuning controls, else the first card |
| `--all` | Every graphics card |
| `--wake` | Also read a card that is asleep (a second card the kernel powered down to save energy). Reading wakes it; without `--wake` such a card is only named, and Tux Burner's service is not asked about any card (it numbers the cards its own way, so the question could wake the sleeping one) |

Exit code 0: shown; 1: no graphics card found; 2: a wrong option, or `--pci` names no card.

The JSON object (one line; shown wrapped here, with example values):

```json
{"gpu":{"name":"<GPU name>","pci":"0000:03:00.0","driver":"amdgpu","card":"card0","vendor":"amd",
        "asleep":false,"overdrive":"offsets","backend":"tux-burner-amd"},
 "now":{"voltage_offset_mv":-110,"sclk_offset_mhz":300,"sclk_max_mhz":null,"mclk_max_mhz":1375,"mclk_offset_mhz":null,"power_cap_w":374},
 "live":{"edge_c":27,"junction_c":31,"power_w":33,"clock_mhz":63,"vddgfx_mv":700,"fan_rpm":589},
 "ranges":{"voltage_offset_mv":[-200,0],"sclk_offset_mhz":[-500,1000],"sclk_mhz":null,"mclk_mhz":[97,1500],"mclk_offset_mhz":null,
           "power_cap_w":[212,374],"power_cap_default_w":304},
 "limits":{"voltage_floor_mv":-150,"voltage_floor_source":"default","suggest_floor_mv":null,"sclk_ceiling_mhz":null},
 "tux_burner":{"installed":true,"cli":"/usr/bin/tburn-cli","daemon":"running","version":"0.99.0","build":"...",
               "state":"idle","protocol":"2.0","message":null,"serves_this_card":true,
               "missing_capabilities":["amd-lact-gate-v1","amd-sclk-ceiling-v1","gpu-reset-events-v1","tuning-session-v1"]},
 "lact":{"running":true,"active_profile":"123",
         "active_values":{"voltage_offset_mv":-110,"sclk_offset_mhz":300,"mclk_max_mhz":1375,"power_cap_w":374},
         "matches_card":true,"config_sha256":"..."},
 "can_write":false,"why_not":"needs a newer Tux Burner for: ..."}
```

| Field | Meaning |
|---|---|
| `gpu.overdrive` | `offsets` (a table SRS reads: a voltage offset, with a clock offset on RDNA 4 or the highest core clock on RDNA 3), `older-table` (clock and voltage points: not supported yet), `not-readable`, `none` |
| `now.sclk_max_mhz`, `ranges.sclk_mhz` | RDNA 3 only: the highest core clock and its range (these cards have no `sclk_offset_mhz`) |
| `gpu.backend` | `tux-burner-amd` / `tux-burner-nvidia` when Tux Burner's service knows the card, else `null` |
| `now`, `ranges` | From the kernel's files; for a card with no table of its own (NVIDIA) from Tux Burner's service. `null` = not available |
| `limits.voltage_floor_mv` | The deepest undervolt Tux Burner's service will write, as the service reports it; `null` without the service |
| `limits.suggest_floor_mv`, `limits.sclk_ceiling_mhz` | Always `null` in this version: Tux Burner does not report them yet, and SRS does not guess |
| `tux_burner.daemon` | `running`, `not-running` (no socket), `not-answering` (a socket nobody answers), `not-allowed` (the service does not serve this user), `no-answer` (too slow, or not understood), `unusable-version` (another protocol), `not-checked` (tests) |
| `tux_burner.missing_capabilities` | Abilities the coming search functions will require that this service does not have |
| `lact.active_values` | LACT's saved values for this card: of the active profile, else of its base settings; `null` when the selected profile has nothing for the card |
| `lact.matches_card` | `true` the card is at those values, `false` it is not, `null` nothing to compare |
| `lact.config_sha256` | A fingerprint of LACT's settings file as it is now (to see later whether it changed) |
| `can_write`, `why_not` | Whether everything a change would need is in place (Tux Burner's tool, its service, this card, the abilities), and if not, the first thing missing. This version has no command that changes anything |

Examples:

```
$ sysreport-search gpu-tune status
$ sysreport-search gpu-tune status --all
$ sysreport-search gpu-tune status --pci 0000:03:00.0 --json
```

Not tried on a real card, a real Tux Burner service or a real LACT yet (tests use made-up files and a fake
service); other AMD generations, NVIDIA, Intel and laptops with two cards may show something odd.

### `ir` - IR Blaster: learn and send infrared codes

```
sysreport-search ir [--status | --list | --send REMOTE BUTTON [--repeat N] | --send-code HEX [--nec] [--frequency HZ] [--repeat N] |
                     --learn [--save REMOTE BUTTON] [--timeout S] [--frequency HZ] | --probe | --reset |
                     --export FILE | --import FILE [--replace]] [--dry-run] [--huffman] [--yes]
```

**In plain words:** uses an ELK Smart / OCRUSTAR USB infrared dongle (USB IDs `045c:0132`, `045c:0184`,
`045c:0195`, `045c:0134`) as a TV remote: learn a button from your real remote, keep it under a name,
send it again. The codes live in IR Remote's `~/.config/irremote/remotes.json` and are shared with
that app (User Guide 6.15). Only the `045c:0132` has been used for real so far.

| Option | Meaning |
|---|---|
| `--status` | (default) Is a dongle plugged in, and may it be used? Read-only (sysfs). Its `Rule` line shows the `70-elksmart-ir.rules` found (in `/etc`, `/run`, `/usr/local/lib` or `/usr/lib` `udev/rules.d`) or "not installed"; with no permission it says how to fix it (install the rule, or replug when the rule is there, or a missing device file) |
| `--list` | The remotes and their codes |
| `--send REMOTE BUTTON` | Send a saved code (names as `--list` shows them; case does not matter) |
| `--send-code HEX` | Send a code given as hex; with `--nec`, NEC address + command pairs (4 hex digits per press) |
| `--learn` | Wait for one button of a remote, held within 1 cm of the dongle, and print its code; `--save REMOTE BUTTON` also keeps it (a new remote or button is made when needed) |
| `--probe` | Ask the dongle to identify itself and say which protocol it uses (plain or Huffman); sends no infrared. Exits 1 when the dongle is busy (IR Remote or another SRS action is using it): it was not asked |
| `--reset` | USB reset, for a dongle that stopped answering (for example left in learn mode); sends no infrared |
| `--export FILE`, `--import FILE` | Copy the codes to / from a file in IR Remote's format; import adds, `--replace` replaces them all |
| `--repeat N`, `-r N` | Send it N times (1-100), 0.1 s apart |
| `--frequency HZ`, `-f HZ` | Carrier frequency for `--send-code` / `--learn`, 10000-100000 (default 38000) |
| `--timeout S`, `-t S` | How long `--learn` waits, 1-600 s (default 20) |
| `--dry-run` | With `--send` / `--send-code`: print the USB packets only, send nothing (works without a dongle); `--huffman` for a dongle that uses the compressed protocol |
| `--yes` | `--send` and `--learn` ask first (`[y/N]`); `--yes` starts at once |

```
$ sysreport-search ir --status
$ sysreport-search ir --send TV Power --dry-run
$ sysreport-search ir --send TV "Volume Up" --repeat 3
$ sysreport-search ir --learn --save TV "Input"
```

The first time SRS changes the codes file it keeps a copy, `remotes.json.before-srs`; it never
overwrites a file it cannot read. Exit code 0: done (or `--status` found a usable dongle); 1: no usable
dongle, not confirmed (or no terminal and no `--yes`), nothing learned, or the dongle did not answer;
2: a bad option or value, or two actions at once.

---

## Load and benchmark commands (make the CPU busy)

`burn` starts at once: it is the burn-in itself, and you choose its threads, time and temperature
limit on the command line. **Every benchmark asks first** - `bench`, `ipi`, `latency`, `pi`,
`crypto`, `prime` and the ones below - on the terminal (stderr): what it runs, about how long, how
many CPU threads it keeps busy and (SRS Pi) how much memory it needs, then `Start now? [y/N]`.
Only `y` starts it. `--yes` (or `-y`) starts it without the question, for scripts. Without a
terminal and without `--yes` it does not start and exits with 2. `--help` (`-h`) prints the
command's usage and exits 0; an unknown option or a missing value exits 2 - neither starts
anything. The windows ask the same question. Close other heavy programs first. Press **Ctrl+C** to
stop. Never run them on a PC you are not allowed to load.

**Use at your own risk (2.3.1).** These commands push the CPU, GPU, memory, drives and power supply to
their limits; SRS comes without any warranty, and its authors are not responsible for any damage (see
the licence, GNU GPL v2). The question (`Start now (and agree)? [y/N]`), every load command's
`--help` and `burn` print that notice. **`--yes` counts as agreeing to it** (so do answering `y` and
starting `burn`). The window asks once with I Agree / I Disagree.

```
$ sysreport-search crypto --threads 1
It checks the official AES and SHA-256 test vectors, then measures AES-128 encryption and SHA-256
speed with the CPU's crypto instructions and in plain C, 1.0 s per measurement: about 8 seconds.
Each test runs on one CPU thread (light load).

Close other programs for accurate results.
Use at your own risk: SRS comes without any warranty, and its authors are not responsible for any damage (see the licence, GNU GPL v2).
Start now (and agree)? [y/N] n
crypto: not started
```

### `burn` - CPU Burnin from the command line

`burn --help` (new in 2.3.1) prints the options and the notice. In the window this is the CPU part of
**Stress & Burnin**; the GPU part is `gpu-burn` below.

| Option | Meaning | Default |
|---|---|---|
| `--threads N` | Use logical CPUs 0..N-1 | all |
| `--cpus 0,2,5` | Use exactly these logical CPUs | all |
| `--seconds S` | Burn time per cycle, 1 to 2,700,000 seconds | 10 |
| `--mode same\|swap` | `same`: each worker stays on its CPU and runs Whetstone. `swap`: workers move between CPUs (affinity test). | same |
| `--limit C` | Stop if the hottest CPU sensor reaches C degrees Celsius: 50-100, or 101-110 with `--accept-high-limit` (2.4; any other value exits 2 without starting) | 90 |
| `--accept-high-limit` | Allow a `--limit` of 101-110 °C. Above 100 °C many CPUs and GPUs are already at or near their own maximum; using it means you accept that risk (the window asks the same once, Yes / No) | off |
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
sysreport-search: error: burn [--threads N | --cpus 0,2,5] [--seconds S] [--mode same|swap] [--limit C] [--accept-high-limit] [--cool S] [--cycles N]
```

A limit over 100 °C without `--accept-high-limit` is refused the same way (exit 2, nothing started):

```
$ sysreport-search burn --limit 105
burn: a --limit over 100 C needs --accept-high-limit (above 100 C many CPUs and GPUs are already at or near their own maximum; using it means you accept that risk)
```

### `bench` - Dhrystone and Whetstone

```
sysreport-search bench MODE [--seconds S] [--yes]
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

`--seconds S` is the time per measurement, 0.1 to 60 (default 1). **Asks first** (the mode, the
time and how many CPU threads are busy; the modes that load many threads at once also warn about
unstable overclocks); `--yes` starts at once. The result is
stored and shown in the window and in `-save` reports. Results: DMIPS (Dhrystone) and MWIPS
(Whetstone), per CPU and in total; see [tools.md](SECTIONS/tools.md#cpu-benchmarks).
Exit code 0 when it ran, 1 when there is nothing to run in that mode on this CPU (for example
`ec` without efficiency cores; then there is no question either), 2 for a bad mode or option, or
when it was not started.

### `ipi` - core-to-core delay

```
sysreport-search ipi [--cpus N] [--rounds R] [--yes]
```

`--cpus N` tests CPUs 0..N-1 (default all), `--rounds R` round trips per pair (default 20,000).
Two CPU threads are busy at a time, for about a second at the defaults. **Asks first**; `--yes`
starts at once. Prints a matrix of nanoseconds, one row and column per CPU. With one CPU
(`--cpus 1`, or a one-CPU system) there is no pair to measure: it says so, exits with 2 and keeps
the last saved result. See [tools.md](SECTIONS/tools.md#cpu-ipi-delay).

### `latency` - memory latency by block size

```
sysreport-search latency [--max MB] [--yes]
```

`--max MB` is the largest block, 1 to 4096 MB (default 256; the window allows the same range). It uses
that much memory and one CPU thread, for about 3 seconds at the default. **Asks first**; `--yes`
starts at once. Prints block size, nanoseconds per read and the cache level. See
[tools.md](SECTIONS/tools.md#memory-latency).

### `pi` - SRS Pi: pi to many digits, every digit checked

```
sysreport-search pi [--digits 128k|1m|..|32m|16ki|..|1mi|..|32mi|N] [--threads N] [--method chudnovsky|agm] [--save-digits FILE] [--yes]
```

`--digits` is the number of decimal places (default `1m` = 1,000,000; `k` = thousand, `m` =
million; `ki` = 1,024 and `mi` = 1,048,576 for the **SuperPi sizes** 16ki ... 32mi; 100 to 32mi). The listed sizes are checked digit for digit with built-in SHA-256
checksums; other sizes only have their first 100 digits checked. `--threads N` (default 1) splits
the Chudnovsky series over N threads; `--method agm` uses Gauss-Legendre (always one thread).
`--save-digits FILE` writes the digits (without "3.") to FILE. **Asks first**, with the time, the
threads and the memory it needs (up to about 1.6 GB for 32mi digits); from 8 million digits, or
with all threads, it also warns about unstable overclocks. `--yes` starts at once. Exit code 0
when every checked digit is right, 1 when a digit is wrong or memory ran out, 2 for a bad option or
when it was not started. See
[tools.md](SECTIONS/tools.md#srs-pi).

### `crypto` - AES-128 and SHA-256 speed

```
sysreport-search crypto [--threads N] [--seconds S] [--all-threads-only] [--yes]
```

Measures AES-128-CTR and SHA-256 in MB/s on one thread and on N threads (default all), with
AES-NI / SHA-NI when the CPU has them and in plain C. `--seconds S` per measurement, 0.1 to 60
(default 1), about 8 measurements in all (4 with `--all-threads-only`, which skips the 1-thread
pass like the window's **All Cores**: the 1-thread column says "skipped", the run history has "-" there
and `1-THREAD-SKIPPED`). **Asks first** (with all threads it warns that every CPU
thread is 100% busy); `--yes` starts at once. The FIPS test vectors are checked first; exit code 1
if they fail, 2 for a bad option or when it was not started. See
[tools.md](SECTIONS/tools.md#crypto-benchmark).

### `prime` - count the primes up to a limit

```
sysreport-search prime [--limit 1e8|1e9|1e10|1e11|N] [--threads N] [--yes]
```

Counts the primes up to `--limit` (default 1e9; any number from 100 to 1e11, for example `5e9` or
`1g`) on N threads (default all) and checks
the count for the powers of ten 1e7 to 1e11. **Asks first** (from 1e10, or with all threads, it
also warns about unstable overclocks); `--yes` starts at once. Exit code 1 when the count is wrong,
2 for a bad option or when it was not started. See
[tools.md](SECTIONS/tools.md#prime-sieve).

### `cachemem` - Cache & Memory Benchmark

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

### `os-latency` - how quickly the OS answers

```
sysreport-search os-latency [--samples N] [--tick-seconds S]
                            [--only syscall,switch,spawn,wake,timer,memory,files] [--yes]
```

Times many single samples of system calls, pipe context switches (threads and processes, same CPU
and two cores), thread create, fork and posix_spawn, futex and condition-variable wake-ups, sleep
overshoot and a periodic 1 ms tick, page faults (4 KiB and 2 MiB), mmap / munmap, and small-file
create / stat / unlink. Each row shows the median, 95%, 99%, worst and best. The CPU scheduler in
use (sched_ext such as scx_lavd, BORE, EEVDF), the preemption mode, the timer tick, the clock
source and the timer slack are shown with the result. It keeps one or two CPUs busy for about
6 seconds at the defaults and **asks first**; `--yes` (`-y`) starts at once. Without a terminal and
without `--yes` it does not start (exit code 2).

| Option | Meaning |
|---|---|
| `--samples N` | Samples for the fast tests, 100-1,000,000 (default 20,000); the slow tests use a tenth to a fortieth |
| `--tick-seconds S` | How long the 1 ms tick is watched, 0.1-60 (default 3) |
| `--only` | Run only some groups, e.g. `--only syscall,switch` |

Nothing on the system is changed. The file test uses a temporary folder in `~/.cache/srs` that
is always removed. Exit code 0 when it ran, 1 when it was stopped, 2 when it did not start (bad
option, not confirmed, not available). Example:

```
$ sysreport-search os-latency --only syscall,timer --yes
```

See [tools.md](SECTIONS/tools.md#os-latency-benchmark).

### `compute` - CPU Compute (floating-point work)

```
sysreport-search compute [--threads N] [--seconds S]
                         [--tests sgemm,dgemm,fma-sp,fma-dp,fft-sp,fft-dp,nbody,mandelbrot,raytrace]
                         [--code auto|c|sse2|avx2|avx512] [--single | --all-threads-only] [--yes]
```

Matrix multiply (SGEMM / DGEMM), peak multiply-add, FFT, N-body, Mandelbrot and a small ray tracer,
in GFLOPS or Mpixels/s, on 1 thread and on N threads, with the best vector code the CPU has. Every
answer is checked; a wrong one is shown as WRONG. With N threads every CPU is 100% busy (AVX-512
where the CPU has it), so it **asks first**; `--yes` (`-y`) starts at once. Without a terminal and
without `--yes` it does not start (exit code 2). An unstable overclock or undervolt can crash the
PC under this load; `--single` keeps it light.

| Option | Meaning |
|---|---|
| `--threads N` | Threads for the right-hand column (default 0 = all logical CPUs) |
| `--seconds S` | Measuring time per test and column, 0.1-60 (default 0.5) |
| `--tests` | Run only some tests, e.g. `--tests dgemm,mandelbrot` |
| `--code` | Force a code path (a path the CPU lacks falls back to the best it has) |
| `--single` | Only the 1-thread column (light load, half the time) |
| `--all-threads-only` | Only the N-threads column, as the window's **All Cores**: the 1-thread column says "skipped", the history line has "-" and `1-THREAD-SKIPPED` (about half the time; not with `--single`) |

Exit code 0 when every check passed, 1 when a check failed (WRONG) or it was stopped, 2 when it did
not start (bad option, not confirmed, Stress & Burnin running). Example:

```
$ sysreport-search compute --single --tests sgemm,nbody --yes
```

See [tools.md](SECTIONS/tools.md#cpu-compute-benchmark).

### `work` - Everyday work

```
sysreport-search work [--threads N] [--seconds S] [--only compress,sort,hash,text,interp]
                      [--libs zlib,zstd,lz4,xz] [--single | --all-threads-only] [--part-kib K] [--yes]
```

Compression and decompression with the system's zlib, zstd, lz4 and xz libraries (MB/s and
ratio), sorting (Mkeys/s), checksums and hashes (GB/s), text processing (GB/s) and a bytecode
interpreter (Mops/s), on 1 thread and on N threads; every result is checked. The data is generated
by SRS (your files are never read or written). It keeps every CPU busy and **asks first** with the
expected time; `--yes` (`-y`) starts at once. Without a terminal and without `--yes` it does not
start (exit code 2). A library that is not installed shows "not installed" and its rows are
skipped.

| Option | Meaning |
|---|---|
| `--threads N` | Threads for the right-hand column (default 0 = all logical CPUs) |
| `--seconds S` | Measuring time per test and column, 0.1-60 (default 0.5) |
| `--only` | Test groups, e.g. `--only compress,hash` |
| `--libs` | Compression libraries, e.g. `--libs zstd,lz4` |
| `--single` | Only the 1-thread column |
| `--all-threads-only` | Only the N-threads column, as the window's **All Cores** ("skipped" in the 1-thread column, `1-THREAD-SKIPPED` in the history; not with `--single`) |
| `--part-kib K` | Size of each generated data part per thread, 64-16384 KiB (default 1024) |

Exit code 0 when every check passed, 1 when a check failed or it was stopped, 2 when it did not
start (bad option, not confirmed, Stress & Burnin running, not enough memory). Each test group adds a
line (`work-compress` ... `work-interp`) to the history. Example:

```
$ sysreport-search work --only compress --libs zstd,lz4 --single --yes
```

See [tools.md](SECTIONS/tools.md#everyday-work-benchmark).

### `net` - Network Benchmark

```
sysreport-search net [--local] [--internet] [--seconds S] [--streams N] [--samples N] [--max-mb MB]
                     [-4|-6] [--server URL] [--hosts HOST[:PORT],...] [--yes] [--yes-internet]
```

Without options: the loopback test only - TCP throughput with 1 and N streams, UDP throughput and
loss, TCP and UDP round trip time between two programs on this PC (127.0.0.1). Nothing leaves the
computer. It **asks first**; `--yes` (`-y`) starts the loopback test at once.

`--internet` adds DNS, connect, HTTPS, download and upload tests against
`https://speed.cloudflare.com`. Before it starts it prints the server, the hosts, the tests, the
time and the data use (up to about 4 x `--max-mb`, 1000 MB with the defaults) and says the server
sees your public IP address. **It asks its own question, which `--yes` does not answer**: type `y`
at the prompt, or pass `--yes-internet` (scripts). Without a terminal and without
`--yes-internet` the internet test does not start (exit code 2). `--internet` alone runs only the
internet part; `--local --internet` runs both.

| Option | Meaning |
|---|---|
| `--local` | The loopback test (the default when neither `--local` nor `--internet` is given) |
| `--internet` | The internet tests (asks separately) |
| `--seconds S` | Seconds per throughput test, 0.1-60 (default 5); round trip tests use at most 1 s |
| `--streams N` | Connections for the "N streams" tests, 1-32 (default 4) |
| `--samples N` | TCP connects per host for the connect-time test, 1-100 (default 10) |
| `--max-mb MB` | Each transfer test stops after this many MB (default 250, 0 = no cap) |
| `-4` / `-6` | IPv4 only / IPv6 only |
| `--server URL` | Another speed test server with the same `/__down` and `/__up` paths (https only; a port in the URL is used for its connect-time row too) |
| `--hosts H,H` | Extra hosts for the connect-time test (`host` or `host:port`); one typed as a numeric address is printed and saved as "host N (a numeric address)" |
| `--yes` | Start the loopback test without asking |
| `--yes-internet` | Start the internet test without asking |

Without libcurl the DNS and connect tests still run and the HTTPS and speed rows say "not
available". No IP address, MAC address, network name or host name is printed or saved. Exit code
0 when it ran, 1 when it was stopped or a test failed, 2 when it did not start (bad option, not
confirmed, no network connection for the internet test). Examples:

```
$ sysreport-search net --yes                      # loopback only
$ sysreport-search net --internet --seconds 3 --max-mb 50   # asks; at most about 200 MB
```

See [tools.md](SECTIONS/tools.md#network-benchmark).

### `gpu` - GPU Benchmark (Vulkan)

```
sysreport-search gpu [--device N] [--seconds S] [--only fp32,fp16,fp64,int32,read,write,copy,upload,download,latency]
                     [--mem-mib M] [--list] [--yes]
```

Measures the graphics card through Vulkan: FP32, FP16, FP64 and INT32 multiply-add (GFLOPS /
GIOPS), video memory read, write and copy, upload and download (GB/s, 1 GB = 10^9 bytes) and the
round trip of an empty dispatch (µs). Every result is checked; a wrong one is printed as WRONG
RESULT. The Vulkan loader is opened at run time: without it, or without a Vulkan driver, the
command says so and exits with 2. It **asks first** (the GPU, the time, full GPU load, and that
an unstable overclock or undervolt can crash the PC); `--yes` (`-y`) starts at once. Without a
terminal and without `--yes` it does not start (exit code 2).

| Option | Meaning |
|---|---|
| `--device N` | The Vulkan device to test, numbered as `--list` shows them (default: the first discrete GPU, else the first device) |
| `--seconds S` | GPU time per test, 0.1-60 (default 1) |
| `--only LIST` | Run only these tests (comma-separated): fp32, fp16, fp64, int32, read, write, copy, upload, download, latency |
| `--mem-mib M` | Buffer size of the memory and transfer tests in MiB, 1-4096 (default: 256, less on small cards); a size above a quarter of the video memory or of the PC's memory is lowered to that, and the result says so |
| `--list` | List the Vulkan devices (number, name, type, video memory, driver) and exit; nothing runs on a GPU (like `vulkaninfo`, this creates a Vulkan instance to name the devices) |
| `--yes` | Start without asking |

Exit code 0 when every test ran and was right, 1 when it was stopped, a result was wrong or the
GPU failed during the run, 2 when it did not start (bad option, not confirmed, no Vulkan, no such
device, Stress & Burnin running). Examples:

```
$ sysreport-search gpu --list
$ sysreport-search gpu --only fp32,read,upload --seconds 0.5
```

See [tools.md](SECTIONS/tools.md#gpu-benchmark).

### `gpu-burn` - GPU Burnin

```
sysreport-search gpu-burn [--seconds N] [--backend vulkan|hip|cuda|opencl|auto] [--device N|all | --pci ADDR]
                          [--fp32|--fp64] [--limit C] [--accept-high-limit] [--list] [--yes]
                          [--json] [--json-live] [--watch-kernel]
```

Keeps the GPU fully busy with large matrix multiplications and counts every result that differs
from the first one (0 = PASS). On a terminal a live line shows the time, GFLOPS, the error count,
edge and hotspot temperature, power and clock; the full result follows at the end. It **asks
first** (the GPU, the back end, the time, high power and heat, the overclock / undervolt warning,
the temperature limit); `--yes` (`-y`) starts at once; without a terminal and without `--yes` it
does not start (exit code 2). Before you say yes it starts no HIP, CUDA or OpenCL runtime; to name
the GPU in the question it reads only Vulkan's device list (like `vulkaninfo`, no work is sent). It
ends by itself at the temperature limit, on a driver error, when the GPU stops answering or when a
piece of work takes more than 2 s; Ctrl+C stops it. A driver call that never comes back is waited
for 15 s after the end, then the run ends ("the GPU driver did not come back"). A card that reports
no temperature is shown with "no temperature sensor: limit not active". **It never changes clocks,
voltages or fans.** It never burns a software renderer such as llvmpipe as a GPU: the default and
`--device all` skip it, and with only a software renderer in Vulkan it does not start (exit 2).

| Option | Meaning |
|---|---|
| `--seconds N` | How long, 1-86400 (default 60) |
| `--backend B` | `vulkan`, `hip` (ROCm), `cuda`, `opencl` or `auto` (default: Vulkan when it has a GPU, else the first back end that has one) |
| `--device N` | The device number in that back end's list (see `--list`), or `all` (default: the first GPU; CPU / software devices are skipped) |
| `--fp32` / `--fp64` | Precision of the multiplications (default FP32) |
| `--limit C` | Stop at this GPU temperature in °C, 50-100 (default 100; hotspot / junction when the card reports it, else edge); 101-110 only with `--accept-high-limit` (2.4; before 2.4 the range was 50-120). `--limit-c` is the same |
| `--accept-high-limit` | Allow a `--limit` of 101-110 °C (above 100 °C many GPUs are already at or near their own maximum; you accept that risk) |
| `--list` | List the devices of each installed back end (or only `--backend`'s) and exit. This starts each runtime but runs no work |
| `--pci ADDR` | The GPU at this PCI address (as in `0000:03:00.0`) instead of a device number; not together with `--device` (2.6) |
| `--yes` | Start without asking |
| `--json` | For programs (2.6): print **one JSON object** on standard output when the run is over - nothing else is written there, the usual report still goes to `~/.cache/srs/bench-gpuburn.txt` - and use the exit codes of the table below. Needs `--yes` |
| `--json-live` | `--json`, plus one JSON line every half second while it runs (`"event":"sample"`); the result object is the last line |
| `--watch-kernel` | Also watch the kernel log during the run and for 5 seconds after it (2.6): a GPU reset or ring timeout there fails the run. See below |

With `--device all`, a card that cannot start is marked in the table and a `note -` line says how many;
the exit code comes from the cards that ran (all of them failing to start = 1, ended early).
Exit code 0 = PASS, 1 = FAIL (errors found), stopped, or ended early by a safety rule, 2 = not
started (bad option, not confirmed, back end not installed, no device, Stress & Burnin running).

**For programs: `--json` (2.6).** Without `--json` the exit codes above stay as they are. With it:

| Exit code | Meaning | `result` in the JSON |
|---|---|---|
| 0 | PASS: no wrong result, and (with `--watch-kernel`) no GPU reset seen. **Also 0 when `--watch-kernel` was asked for but the kernel log could not be read**: then `"kernel_watch":"unreadable"`, and a caller **must treat the run as not watched** (see below) | `pass` |
| 10 | Stopped by a signal (Ctrl+C, or SIGTERM from the program that started it); no errors so far | `stopped` |
| 11 | FAIL: wrong results were found | `fail` |
| 12 | Could not start: no device, the back end is not installed, `--pci` names no device of that back end, the GPU could not be opened | `not-started` |
| 13 | Ended early: the GPU did not answer in time, the device was lost, or the GPU runtime reported an error | `ended-early` |
| 14 | A GPU reset or ring timeout appeared in the kernel log during the run (`--watch-kernel`), even if the numbers were right | `fail` |
| 15 | Ended early at the temperature limit: a cooling limit, **not** a verdict about stability | `ended-early` |
| 2 | A wrong option (also `--json` without `--yes`); no JSON is printed | |

Wrong results **and** a reset in the kernel log give 11 (the kernel events are in the JSON all the same); a
reset together with anything else gives 14. A process killed by a signal it cannot catch has no exit code of
its own: treat that as a crash of the test.

The result object (one line; shown wrapped here, with example values):

```json
{"tool":"srs gpu-burn","version":"2.6.0","result":"fail","exit":14,
 "reason":"kernel log: the GPU stopped answering, ring timeout (gfx_0.0.0, in kwin_wayland), then GPU reset (MODE1, video memory wiped)",
 "end":"completed","backend":"vulkan","device":"<GPU name>","pci":"0000:03:00.0",
 "seconds":61.3,"planned_seconds":60,"precision":"fp32","limit_c":100,
 "iterations":18234,"errors":0,"gflops_avg":2710.4,
 "max":{"edge_c":64,"junction_c":83,"power_w":338,"clock_mhz":3140},
 "devices":[{"device":"<GPU name>","state":"done","iterations":18234,"errors":0,"gflops_avg":2710.4,"matrix":4096,
             "temperature_limit_active":true,"max":{"edge_c":64,"junction_c":83,"power_w":338,"clock_mhz":3140},"note":""}],
 "kernel_watch":"on",
 "kernel_events":[{"t_s":41.1,"kind":"ring-timeout","pci":"0000:03:00.0","detail":"gfx_0.0.0","process":"kwin_wayland","text":"..."},
                  {"t_s":41.9,"kind":"gpu-reset","pci":"0000:03:00.0","detail":"MODE1","vram_lost":true,"text":"..."}],
 "kernel_events_other_cards":0,"kernel_other":[]}
```

| Field | Meaning |
|---|---|
| `result`, `exit` | The verdict and the exit code (table above) |
| `reason` | Why it ended early, could not start or failed, in plain words; empty after a clean PASS |
| `end` | How the test itself ended: `completed`, `stopped`, `compute-errors`, `not-started`, `temperature-limit`, `no-answer`, `device-lost`, `runtime-error` |
| `backend`, `device`, `pci` | What ran and where (`null` when nothing ran or the back end gives no PCI address, as a software renderer) |
| `seconds`, `planned_seconds` | How long it ran, and how long it was asked to |
| `iterations`, `errors`, `gflops_avg` | Matrix multiplications done, wrong results, average speed - summed over the devices |
| `max` | The highest sensor readings of the run; `null` for a sensor the card does not have |
| `devices` | The same per device (`--device all`); `state` is `done`, `stopped`, `failed`, `ended-early` or `not-started` |
| `kernel_watch` | `off` (not asked for), `on`, or `unreadable` with a `kernel_watch_note` saying why. **`unreadable` = not watched**: the exit code and `result` then come from the test's own numbers alone, exactly as without `--watch-kernel`. A program that needs the watch must check for `"kernel_watch":"on"`, not only for exit 0 |
| `kernel_events` | With `--watch-kernel`: what the kernel log said about **this** card during the run. `kind` is `ring-timeout`, `gpu-reset`, `gpu-reset-done`, `device-lost`, `parser-error`, `nvidia-xid` or `gpu-hang`; `t_s` = seconds after the watch began; `process` = the program the driver blamed, when it said |
| `kernel_events_other_cards` | Events about another graphics card (not counted) |
| `kernel_other` | Driver error lines seen that SRS does not know as a reset (shown, not counted) |

A `--json-live` line: `{"event":"sample","t_s":12.5,"planned_seconds":60,"iterations":3620,"errors":0,
"gflops":2712.0,"edge_c":61,"junction_c":79,"power_w":331,"clock_mhz":3105}`.

**`--watch-kernel` (2.6).** A GPU that is undervolted or overclocked too far often does not fail inside the
test program. The driver notices that the GPU stopped answering one of its work queues (a *ring timeout*),
resets the GPU, and the program that was hit is the desktop, not the test. `--watch-kernel` reads the
kernel log (`/dev/kmsg`, opened read-only, from its end: only lines that appear after the start count) while
the test runs **and for 5 seconds after the load ends**, because the moment the clocks drop is a known moment
for hangs. It knows these lines: amdgpu's `ring ... timeout`, `GPU reset begin` / `succeeded`, `MODE1 reset`,
`VRAM is lost`, `device lost from bus`, `Failed to initialize parser`; NVIDIA's `NVRM: Xid` reports; and, best
effort, i915's `GPU HANG` / `Resetting` and xe's `Engine reset`. A line that names another card's PCI address
is not counted. A line that names no card (older kernels) cannot be ruled out and is counted. Without `--json`
a reset is said on standard error ("FAIL - kernel log: ...") and the exit code is 1.

When the kernel log cannot be read (many distributions set `kernel.dmesg_restrict=1`), the run says so -
"Kernel log not readable ...: only the test's own result is checked." - and the JSON has
`"kernel_watch":"unreadable"`. It never reports "nothing seen" for a log it could not read. **The exit code
stays the test's own (0 after a clean run), so callers must treat `unreadable` as "not watched"**: a GPU
reset during such a run would not have been seen. Check `kernel_watch` for `on` before you trust a 0 as
"no reset" (to let a user read the log: run as root, add the user to a group that may read `/dev/kmsg`, or
set `kernel.dmesg_restrict=0`).

A run that fails on the kernel log (exit 14, or 1 without `--json`) is also **saved** as a failure:
`~/.cache/srs/bench-gpuburn.txt` has a red "Result of the run: FAIL - kernel log: ..." line and
"FAIL (kernel log)" in the table, and the Benchmark History line starts with FAIL and ends with the reason
(2.6.0 as first built saved such a run as PASS, because the report was written before the log was looked at).

The JSON of `gpu-burn` and `gpu-tune` is always valid, whatever a device name, a service or a settings file
holds: quotes, backslashes and control characters are escaped, and bytes that are not UTF-8 (also a letter cut
in the middle) are written as U+FFFD, the replacement character.
Not tested with the messages of a real reset yet: the patterns follow the drivers' wording.

Examples:

```
$ sysreport-search gpu-burn --list
$ sysreport-search gpu-burn --seconds 600 --limit 95
$ sysreport-search gpu-burn --backend hip --fp64 --seconds 120
$ sysreport-search gpu-burn --seconds 900 --limit 105 --accept-high-limit
$ sysreport-search gpu-burn --pci 0000:03:00.0 --seconds 60 --yes --json --watch-kernel
```

Since 2.4 every back end uses a register-blocked matrix multiply (64 x 64 tiles, 4 x 4 results per
thread) that loads the GPU much harder; the result's `Kernel` line says which kernel ran, and
`SRS_GPUBURN_SIMPLE=1` forces the older one.

See [tools.md](SECTIONS/tools.md#gpu-burnin).

### `gpu-compare` - GPU Compute: Vulkan vs HIP vs CUDA vs OpenCL

```
sysreport-search gpu-compare [--backends vulkan,hip,cuda,opencl] [--seconds S] [--device N] [--yes]
```

Runs the same tests (matrix multiply FP32 / FP16 / FP64 with SRS's own kernel and with the vendor
library - hipBLAS, cuBLAS, CLBlast -, FP32 / FP64 multiply-add chain, video memory copy, upload,
download, kernel launch round trip) through every chosen back end that is installed, on the same
GPU, and prints one table: `*` = fastest in the row, (N%) = that column compared with Vulkan.
CUDA that is really ZLUDA is labelled "CUDA (ZLUDA)"; every OpenCL driver is its own column; a
back end that is not installed shows "not installed" and a hint. It **asks first**; `--yes`
(`-y`) starts at once; without a terminal and without `--yes` it does not start (exit code 2).
HIP, CUDA and OpenCL start their runtimes only after that. Ctrl+C stops it. CUDA runs SRS's
built-in kernels (PTX, since 2.3), so it needs no runtime compiler (NVRTC); the CUDA column's
software line says so. When CUDA was used and NVRTC is missing, one note at the end gives the
`cuda-toolkit` command and the menu path (not after "Don't ask again"). An OpenCL driver without
FP64 shows its FP64 rows as "not available: this OpenCL driver has no FP64 (double precision)". When the GPU hangs or
stops answering in one column, the run ends there: no later back end touches that GPU, and the
tests that did not run say "not run".

Since 2.6 a column can have a **how it ran** line under the table. ROCm / HIP and OpenCL (ROCm): the
FP32 own kernels are built for 64-wide waves, as the Vulkan driver builds its shaders
(`SRS_GPU_WAVE=32` keeps AMD's default of 32; if the compiler refuses the option or SRS's tuned
kernel text, the other is kept, and the line says what was refused and what the compiler said). OpenCL (Rusticl): the kernels are timed on the host on a queue without profiling
(`SRS_CL_TIMING=events` keeps the driver's event times), and upload and download are GPU copies
between a system-memory buffer and video memory (`SRS_CL_XFER=plain` keeps
`clEnqueueWriteBuffer` / `ReadBuffer`, and so does a driver that refuses the copy). All of this is
for `gpu-compare` only: `gpu-burn` builds, times and queues as in 2.5. A WRONG RESULT in a 64-wide
column: run again with `SRS_GPU_WAVE=32` before suspecting the card. The launch round
trip is timed on the host from the launch call until the host knows the kernel has finished, with
no sleep in SRS's wait loop for the first 2 ms; run it on a quiet PC.

| Option | Meaning |
|---|---|
| `--backends L` | Comma-separated: `vulkan`, `hip`, `cuda`, `opencl` (default: every installed one). `--backend B` is the same |
| `--seconds S` | GPU time per test, 0.1-60 (default 1) |
| `--device N` | The Vulkan device that fixes "the GPU" (numbers from `gpu --list`); the others use the card at the same PCI address (default: the first discrete GPU) |
| `--yes` | Start without asking |

Exit code 0 when it ran and every result was right, 1 when it was stopped, a result was wrong or
a GPU failed, 2 when it did not start (bad option, not confirmed, none of the chosen back ends
installed). Example:

```
$ sysreport-search gpu-compare --backends vulkan,hip --seconds 0.5
```

See [tools.md](SECTIONS/tools.md#gpu-compute-vulkan-vs-hip-vs-cuda-vs-opencl).

### `cuda-toolkit` - the optional NVIDIA CUDA toolkit

```
sysreport-search cuda-toolkit [--check|--install|--ask-again|--never-ask]
```

**In plain words:** the NVIDIA CUDA toolkit is NVIDIA's free set of developer tools for NVIDIA
graphics cards. SRS can use one small part of it, the runtime compiler (NVRTC), but since 2.3 its
CUDA tests are built in, so most people do not need it. This command explains that, shows what is
installed and the install command for your Linux, and can run it.

| Option | Meaning |
|---|---|
| `--check` | (default) The explanation; NVIDIA card found or not; the CUDA library (NVIDIA's, ZLUDA, none); NVRTC installed (version, file) or not; the install command and its size (or instructions); a red-flag warning when ZLUDA is in use; whether the GUI will ask |
| `--install` | Checks first (no install command for this Linux, pacman's database locked, no network, no `sudo` / `pkexec`), needs a terminal, shows the exact command and the full paths of the programs it uses, and runs it with `sudo` only after you type `yes` - with ZLUDA you must type `yes` twice. Then reports the result and looks for NVRTC again |
| `--ask-again` | Let the GUI ask again before the next CUDA run (`Ask CUDA Toolkit=1` in `~/.config/srs/SRSDEF.ini`) |
| `--never-ask` | The GUI never asks, and `gpu-burn` / `gpu-compare` print no toolkit note (`Ask CUDA Toolkit=0`, the GUI's "Don't ask again") |

Commands it offers: Arch-based `sudo pacman -S --needed cuda` (about 2.4 GB download, 5.2 GB on
disk); Debian / Ubuntu `sudo apt-get install libnvrtc12` or `libnvrtc11.2` (about 20 MB), else
`nvidia-cuda-toolkit`; Fedora, openSUSE and others: instructions for NVIDIA's CUDA repository
(<https://developer.nvidia.com/cuda-downloads>, package `cuda-nvrtc-<version>`). With ZLUDA,
installing the toolkit is not recommended (it can replace ZLUDA's cuBLAS on the library path, and
NVIDIA's driver libraries conflict with ZLUDA). The package manager, `sudo` and `pkexec` are only
taken from `/usr/bin`, `/usr/sbin`, `/bin` or `/sbin` and run by their full path; a program of the
same name elsewhere in `PATH` (for example `~/.local/bin`) is never used, and without them in those
folders SRS does not start the install and shows the command to type yourself. The GUI has the
same under **Options -> NVIDIA CUDA toolkit for GPU tests...** (User Guide 9.20).

Exit code 0: shown, or installed and NVRTC found; 1: not started (with the reason) or the install
failed; 2: not confirmed, no terminal, or a bad option.

### `update-lists` - newer hardware name lists (2.4)

```
sysreport-search update-lists [--list] [--yes]
```

**In plain words:** downloads the newest PCI, USB and monitor maker name lists from their official
sources, so new devices get names. Only when you run it; no root.

First it prints each list in use: file, version (or the number of entries for `pnp.ids`, which has no
version), path and whether it is yours or the system's. Then it shows the sources - `pci.ids` from
<https://pci-ids.ucw.cz/v2.2/pci.ids>, `usb.ids` from <http://www.linux-usb.org/usb.ids>, `pnp.ids` from
hwdata (<https://raw.githubusercontent.com/vcrhonek/hwdata/master/pnp.ids>) - and asks `Go on? [y/N]`.
The files go to `~/.local/share/srs/lists` (`$XDG_DATA_HOME/srs/lists` when that is set), downloaded
with `curl` (or `wget`). Each must look like the real list (its header and enough entries) or the copy
in use stays; the previous copy is kept as `.old`. From then on every lookup uses the newer of your
copy and the system's (`/usr/share/hwdata`), by the list's own version.

| Option | Meaning |
|---|---|
| `--list` | Only show the copies in use; download nothing |
| `--yes` | Download without asking |

Exit code 0: at least one list was updated or checked; 1: not downloaded (answered no, no terminal,
or every download failed); 2: a bad option. The window has the same on the Tools page (User Guide 9.21).

Every `cachemem`, `storage`, `os-latency`, `compute`, `work`, `net`, `gpu`, `gpu-burn`, `gpu-compare`, `pi`, `crypto` and `prime` run is added to `~/.cache/srs/bench-history.tsv`.

---

## `srs-gui` window options

| Option | What it does |
|---|---|
| (none) | Open the main window (the window with the sidebar, or the classic one when **Options -> Start in classic view** is on). |
| `--gui-open=WHAT[,WHAT...]` | Also open these panels or tools at start. In the main window an item opens on its page (for example `sensors` on the Sensors page); in the classic view it opens as its own panel. |
| `--gui-tray` | Start hidden in the system tray (used by the autostart entry). |
| `--tray` | The same as `--gui-tray` (2.5): start with only the tray icons - SRS's own and / or the live tray meters (User Guide 11.1). |
| `--gui-classic` | Start in the classic view: SIV's main window with its button row, every item in its own panel (as in SRS 1.1). |
| `--gui-new` | Start in the main window with the sidebar, even when **Start in classic view** is on. |
| `--power-recorder` | Record **Power & Cost** history with no window, following the page's Recording setting (Off / For a set time / Always). Used by the optional systemd user service that **Power & Cost -> Setup -> Keep recording when SRS is closed** writes. Only one process records at a time (a lock file beside the database); stop it with Ctrl+C or SIGTERM. |
| any other argument | Run as the command line (`srs-gui -save` = `sysreport-search -save`). |

`WHAT` can be any item id from `--list` (e.g. `sensors`, `processors`, `pci-bus`, `processor:4`),
or one of these tool names:

| Name | Opens |
|---|---|
| `stressburn` | Stress & Burnin as last used (2.3.1) |
| `burnin` | Stress & Burnin set to Timed (CPU Burnin) |
| `stress` | Stress & Burnin set to Until Stop (CPU Stress) |
| `gpuburn` | Stress & Burnin with only GPU Burnin ticked |
| `loadterms` | Options -> Load test terms (warranty)... |
| `bench` | Benchmark |
| `ipi` | IPI Delay |
| `latency` | Latency |
| `pi` | SRS Pi |
| `crypto` | Crypto Benchmark |
| `prime` | Prime Sieve |
| `cachemem` | Cache & Memory |
| `storagebench` | Storage Benchmark (`storage` alone is the Storage page's item) |
| `oslatency` | OS Latency |
| `compute` | CPU Compute |
| `work` | Everyday Work |
| `netbench` | Network Benchmark (`network` alone is the Network page's item) |
| `gpubench` | GPU Benchmark |
| `gpucompare` | GPU Compute: Vulkan vs HIP vs CUDA vs OpenCL |
| `gputune` | The GPU Tuning tab (read-only) on Benchmarks & Stress (2.6); the item name `gpu-tuning` does the same |
| `cudatoolkit` | The **Options -> NVIDIA CUDA toolkit for GPU tests...** window |
| `lists` | Update hardware lists (asks first) (2.4) |
| `acpieval` | ACPI method evaluation (the one-time warning first, if not accepted yet) (2.4) |
| `idlookup` | Device ID Lookup |
| `gputop` | The Big Top window (2.5), on its Performance tab |
| `traysettings` | The Task Manager Settings (update speeds, start page, CPU units, tray meters, summon shortcut) (2.5) |
| `find` | Find |
| `menu` | Pops up the Menu |
| `power-cost` | Shows the Power & Cost page (the item `power` is the expert Power status tab on Sensors) |

Opening a tool **never starts it**. Example: `srs-gui --gui-open=sensors,burnin`.
The environment variable `SRS_OPEN=sensors,burnin` does the same.

The menu entries use: `--gui-open=stressburn` (SRS Stress & Burnin),
`--gui-open=bench,ipi,latency` (SRS Benchmark), and the jump-list actions `sensors`, `processors`,
`burnin`, `stress`, `bench`, `cachemem`, `storagebench`, `oslatency`, `compute`, `work`,
`netbench`, `gpubench`, `gpuburn`, `gpucompare`, `pi`, `idlookup`, `find`, and **Classic View** (`--gui-classic`).

---

## `srs-root`

```
pkexec srs-root            # or: sudo srs-root
sudo srs-root --user NAME  # collect for another user (used by the boot unit)
pkexec srs-root --acpi-eval METHOD [ARG...]   # evaluate one ACPI method (2.4; what the window's ACPI method evaluation runs)
srs-root --help            # the usage text (also -h); needs no root
```

Collects root-only data for the user who started it (`PKEXEC_UID` under pkexec, `SUDO_UID` under sudo,
else root itself) and puts it into that user's `~/.cache/srs/root/`. It must run as root.

**What it checks first (2.6).** Before it creates or changes anything, srs-root checks its arguments and
that it is root. Only these forms are accepted: no arguments, `--user NAME` (exactly one name),
`--acpi-eval METHOD [ARG...]`, and `-h` / `--help`. Anything else - an unknown option, `--user` with no
name, a stray word - prints the usage text and exits 2. Started without root it says
`srs-root: needs root (use: pkexec srs-root). Nothing was changed.` and exits 2. (Before 2.6 all of
these ran a full collection anyway, which without root overwrote the good cached files with
"Permission denied" texts.)

**How the data gets to you (2.6).** Root no longer writes inside your home folder:

1. Everything is collected in a new folder that only root can enter (`/run/srs-root.XXXXXX`). It is
   removed when srs-root ends, however it ends.
2. When the collection is complete, the folder is given to you (`chown`) and put in place of
   `~/.cache/srs/root` by a process that runs **as you** (through `runuser`, or `setpriv`). The cache
   is replaced only then, in one step: a run that fails or is interrupted leaves the earlier data as it
   was. `acpi-calls.txt` is carried over. The folder is yours alone (mode 700).
3. Only one srs-root runs at a time (a lock, `/run/srs-root.lock`), so the boot unit and the window's
   Run as root cannot mix their files. A second one waits up to 3 minutes, then exits 4.

Because the last step runs as you, a link planted in `~/.cache/srs/root` can no longer make root write
to the file it points at (this closes a symlink issue that was there since the helper was added).

| Exit code | Meaning |
|---|---|
| 0 | Done (or `--help`) |
| 2 | Wrong arguments, not root, or no such user; `--acpi-eval`: a method or argument that is not allowed. Nothing was changed |
| 3 | `--acpi-eval`: the `acpi_call` module is not installed |
| 4 | Another srs-root did not finish within 3 minutes; nothing was collected this time |
| 5 | The data was collected but could not be put into the cache; the earlier data is unchanged |

A collection prints one line per step:

```
srs-root: dmidecode
srs-root: decode-dimms
srs-root: smart
srs-root: acpi-tables
srs-root: lspci
srs-root: msrs
srs-root: rapl
srs-root: umc
srs-root: aod
srs-root: ebda
srs-root: vbios
srs-root: gpio
srs-root: done -> /home/you/.cache/srs/root
```

Since 2.4 it also collects (each only where it applies; files in `~/.cache/srs/root/`):

| File | What |
|---|---|
| `umc.txt` | AMD Ryzen 7000 / 9000 desktop: memory controller (UMC) registers, read through the ryzen_smu driver's `smn` file (an address written there only selects what is read: 4 bytes, never the 8-byte form that writes; each value read twice under `/run/lock/srs-smn.lock`). Without the driver: a "not read" line |
| `intel-imc.txt` | Intel Core 6th gen to Core Ultra (experimental): the memory controller's timing registers, read by `sysreport-search intel-imc-dump` (MCHBAR mapped read-only from `/dev/mem`, aligned 32-bit reads; the window is never switched on) |
| `aod.bin`, `aod.txt` | AMD: the BIOS's AOD table (memory settings), named by an SSDT; read from `/dev/mem` only when the kernel allows it (`iomem=relaxed` on kernels with IO_STRICT_DEVMEM), else `aod.txt` says why; without it, the page reads `/sys/kernel/aod_voltages/full_raw` when QTuxTimings' `aod_voltages` module is loaded (never loaded by SRS; `SRS_AOD_SYSFS` overrides the path for tests) |
| `rapl.txt` | AMD: the package and per-core energy counters, read twice one second apart |
| `ebda.txt`, `ebda.bin` | The BIOS Data Area's EBDA pointer (0x40E) and, when there is one, the EBDA (below 0xA0000, at most 64 KiB) |
| `vbios-<pci>.bin` | Each AMD GPU's VBIOS, copied from the driver's memory (debugfs `amdgpu_vbios`); the GPU's ROM is not touched |
| `gpio.txt` | The kernel's GPIO table (debugfs `gpio`) |
| `efi-systab.txt`, `esrt.txt` | The UEFI System Table pointers and the ESRT entries' root-only files |
| `pmic-*.txt` | Opt-in (Options > Read DIMM voltages from their PMIC chips, `Read DIMM PMIC=1`): each DDR5 PMIC's registers 0x00-0x3F, `i2cdump -y -r 0x00-0x3F BUS ADDR b` (byte reads, no `-f`) |
| `ec.bin` | Opt-in (Options > Read the embedded controller, `Read EC=1`): the EC's 256 registers via `ec_sys` loaded with `write_support=0`, unloaded again if srs-root loaded it |
| `acpi-calls.txt` | The log of `--acpi-eval` (time, call, result) |

**`--acpi-eval METHOD [ARG...]`** (2.4) evaluates one ACPI method through the `acpi_call` module and
exits; nothing else is collected. METHOD must be an ACPI name path (`\_SB.PCI0.GPP0._STA`, segments of
up to 4 characters `A-Z 0-9 _`); each ARG a number (decimal or `0x` hex), a `"string"` (letters,
digits, space, `. _ -`, up to 64) or a buffer `b` + hex bytes; at most 7. Anything else exits 2 without
running. It loads `acpi_call` (exit 3 if it is not installed), writes the call to `/proc/acpi/call`,
prints the result and appends it to `acpi-calls.txt`. **This runs BIOS code and can change hardware
state**; the window asks once before it allows it (User Guide 9.22).

- `-h` / `--help` prints the usage text (2.6). Any other argument than `--user NAME` or `--acpi-eval ...` is
  refused with exit 2; before 2.6 it was ignored and a full collection ran.
- The log line of `--acpi-eval` is appended by a process running as you as well (2.6).
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
| 1 | `find` found no matches; lookup or benchmark failed; `burn` failed; `gpu-top` found no GPU or could not open one; `gpu-tune status` found no graphics card |
| 2 | Usage error (missing or unknown argument, bad value); `find` found no report folder; `burn` had nothing to run; a benchmark was not started (answered No, or no terminal and no `--yes`) |
| 130 | `burn` stopped with Ctrl+C |
| 10-15 | `gpu-burn --json` only: 10 stopped, 11 FAIL (compute errors), 12 could not start, 13 ended early, 14 a GPU reset in the kernel log (`--watch-kernel`), 15 the temperature limit - see [`gpu-burn`](#gpu-burn---gpu-burnin) |

## Environment variables

| Variable | Effect |
|---|---|
| `SRS_SMART_USB=1` | `srs-root`: include USB disks in SMART |
| `SRS_KWH_COST`, `SRS_CURRENCY` | Price per kWh and currency symbol for the kWh Cost item (default `$` 0.15) |
| `SRS_OPEN` | Same as `--gui-open=` |
| `SRS_POWER_DB`, `SRS_POWER_INI` | Power & Cost: use another history database / settings file instead of `~/.local/share/srs/power.db` and `~/.config/srs/power.ini` (for tests) |
| `SRS_DATA_DIR` | Folder holding `power-zip-rates.tsv` for the offline US ZIP price lookup (default: next to the program, then `/usr/share/srs-sysinfo`) |
| `SRS_DOC_DIR` | Folder holding `SRS-User-Guide.pdf` / `html/` for `guide`, F1 and the Guide button (default `/usr/share/doc/srs-sysinfo`) |
| `SRS_VULKAN_LIB` | GPU Benchmarks: open this file instead of `libvulkan.so.1` (a name that does not exist = "not installed"; for tests) |
| `SRS_HIP_LIB`, `SRS_CUDA_LIB`, `SRS_OPENCL_LIB` | GPU Burnin and the GPU Compute comparison: open this file instead of the ROCm / HIP, CUDA or OpenCL library (a name that does not exist = "not installed"; for tests) |
| `SRS_HIP_RTC`, `SRS_CUDA_RTC`, `SRS_HIP_BLAS`, `SRS_CUDA_BLAS`, `SRS_CLBLAST_LIB` | Same, for the runtime compiler (hipRTC / NVRTC), the GEMM library (hipBLAS / cuBLAS) and CLBlast (for tests: `tests/gpu-stub-test.sh` runs against a fake runtime) |
| `SRS_CTK_DRY_RUN=1` | `cuda-toolkit --install` and the GUI's toolkit install show what they would run and run nothing (for tests) |
| `SRS_CTK_TOOL_DIRS` | Only together with `SRS_CTK_DRY_RUN=1`: look for the package manager, `sudo` and `pkexec` in these folders (colon-separated) instead of `/usr/bin`, `/usr/sbin`, `/bin`, `/sbin` (for tests: fake tools that are never run). Ignored otherwise |
| `SRS_OS_RELEASE`, `SRS_PCI_SYSFS`, `SRS_CTK_ROUTE`, `SRS_CTK_PACMAN_LOCK` | NVIDIA CUDA toolkit checks: read this file instead of `/etc/os-release`, this folder instead of `/sys/bus/pci/devices`, this route table instead of `/proc/net/route`, this lock file instead of pacman's `db.lck` (for tests: `tests/cuda-toolkit-test.sh`) |
| `SRS_GPUBURN_SIMPLE=1` | GPU Burnin uses the older tiled matrix multiply instead of the 2.4 register-blocked one (for comparing) |
| `SRS_GPU_WAVE=32` | GPU Compute comparison, ROCm / HIP and OpenCL (ROCm): build the FP32 own kernels for AMD's default of 32 threads per wave instead of 64 (for comparing with 2.5 and older) |
| `SRS_CL_XFER=plain` | GPU Compute comparison, OpenCL (Rusticl): upload and download with `clEnqueueWriteBuffer` / `ReadBuffer` as before 2.6, instead of a GPU copy from a system-memory buffer (for comparing) |
| `SRS_CL_TIMING=events` / `host` | GPU Compute comparison, OpenCL: time the kernels with the driver's event times on a profiling queue (`events`: every driver, as before 2.6) or on the host on a plain queue (`host`). Default: host on Rusticl, events elsewhere |
| `SRS_SPD_DIR` | Memory Timings: read the modules' SPD from `<dir>/<device>/eeprom` instead of `/sys/bus/i2c/drivers/spd5118` (for tests) |
| `XDG_DATA_HOME` | Update hardware lists keeps its copies in `$XDG_DATA_HOME/srs/lists` (default `~/.local/share/srs/lists`) |
| `SRS_GPU_SOFTWARE_ONLY=1` | GPU Benchmarks: refuse every Vulkan device that is not a software renderer such as lavapipe (for tests) |
| `SRS_TUX_BURNER_SOCKET` | GPU Tuning: the path of Tux Burner's service socket instead of `/run/tux-burnerd.sock` (for tests: a fake service, or a path that does not exist) |
| `SRS_TEST_SYSFS_ROOT` | GPU Tuning and GPU Burnin's sensors: a folder put in front of `/sys`, `/proc`, `/run`, `/etc` and `/usr` (for tests: a made-up graphics card). With it set and no `SRS_TUX_BURNER_SOCKET`, the real service socket is not used |
| `SRS_TEST_KMSG`, `SRS_TEST_KWATCH_TAIL_S` | `gpu-burn --watch-kernel`: read this text file instead of `/dev/kmsg`, and watch this many seconds after the load instead of 5 (for tests) |
| `SUDO_USER`, `PKEXEC_UID` | Used to find whose root-data cache to use when run as root |
