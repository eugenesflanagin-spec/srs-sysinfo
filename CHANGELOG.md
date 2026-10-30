# Changelog

SRS is a Linux system information viewer inspired by SIV64X (System Information Viewer), built to improve on it
and shared freely with the Linux and open-source community.
SIV is by Ray Hinchliffe, RH Software (https://rh-software.com); SRS is not affiliated with RH Software.

## 2.4.0 - 2026-10-30

A Memory Timings page with the live DDR5 timings, a GPU Burnin that loads the card much harder, stop limits up to
110 C, pages that said "not read" now showing their data, Update hardware lists, and ACPI method evaluation for
experts.

### Added

- **Memory Timings** (Memory page, `mem-tweak`; was an SMBIOS speed summary). Everything on it is only read.
  - **AMD Ryzen 7000 / 9000 desktop** (family 19h models 60h-7Fh, 1Ah models 40h-4Fh): srs-root reads the memory
    controller (UMC) registers through the `ryzen_smu` driver's `smn` file into `umc.txt`. Writing exactly 4 bytes
    (an address) there only selects what the driver reads; SRS never writes 8 bytes (a register write). Each value
    is read twice under a lock (`/run/lock/srs-smn.lock`) and left out if the two differ. The page decodes 38
    timings plus tRFC / tRFC2 / tRFCsb per channel, with the time in nanoseconds for the main ones and a short
    plain-English meaning for each, and the controller settings: channels with a DIMM, Gear Down Mode, command
    rate, power down, bank groups, refresh mode and Nitro mode.
  - **Clocks and voltages** from the ryzen_smu PM table (readable without root): memory speed, MEMCLK, UCLK with
    the UCLK:MEMCLK ratio, FCLK, VSOC, VDDP, VDDG and VDD MISC. PM table 0x5401xx layouts were checked; 0x6211xx /
    0x6212xx (Ryzen 9000) not yet.
  - **Modules (SPD)**, no root: every DDR5 module's SPD through the kernel's `spd5118` driver - slot, maker, part
    number, size, ranks and chip size, DRAM maker, manufacturing date, temperature, and its JEDEC, EXPO and XMP 3.0
    profiles (speed, CL-RCD-RP-RAS-RC, tRFC1 / 2 / sb, VDD / VDDQ / VPP, profile name).
  - **BIOS memory settings (AMD AOD table)**: ProcODT, processor / DRAM data and CAD bus drive strengths, RttNomWr /
    RttNomRd, RttWr, RttPark / RttParkDqs, the set DIMM VDD / VDDQ / VPP and CPU VDDIO. srs-root reads the table
    (named by an SSDT, `OperationRegion AODE`) into `aod.bin` only when the kernel allows it: it lies in ACPI NVS
    memory, which kernels built with IO_STRICT_DEVMEM keep from `/dev/mem` unless booted with `iomem=relaxed`. SRS
    does not ask you to change that; the page says why the table could not be read. Shown only when the table's
    own timing block matches the live timings, so a layout that does not fit a BIOS is never shown.
  - **DIMM voltages from their PMIC chips** (opt-in, off by default): **Options > Read DIMM voltages from their PMIC
    chips** asks first (an SMBus read can, rarely, collide with the BIOS or the board's controller); srs-root then
    reads each DDR5 PMIC's registers 0x00-0x3F with `i2cdump` (byte reads, no `-f`, nothing written) into
    `pmic-*.txt`, and the page shows the set VDD / VDDQ / VPP (JESD301-2 voltage codes; the high-voltage
    overclocking mode is told apart by the module's profile voltage) and whether the PMIC is locked. The live
    voltage meter is not read: it needs a write to the PMIC.
  - **Intel Core 6th gen to Core Ultra (experimental)**: Skylake to Comet Lake, Rocket Lake, Alder / Raptor Lake,
    Meteor / Arrow Lake. srs-root runs `sysreport-search intel-imc-dump` (root only), which takes the memory
    controller's register window (MCHBAR) address from the host bridge, maps it read-only from `/dev/mem` and
    reads the timing registers with aligned 32-bit reads into `intel-imc.txt`. The window is never switched on.
    The page shows speed, type, channels and CL-RCD-RP-RAS, marked experimental.
  - Register maps and layouts: ZenStates-Core / ZenTimings by Ivan Rusanov (AMD UMC, SPD and AOD layouts) and
    memtest86+ by Sam Demeulemeester and contributors (AMD and Intel memory controllers); facts only, no code
    copied.
- **Stop limits up to 110 C** in Stress & Burnin: the CPU and the GPU stop limits go 50..110 C (the GPU's was
  50..120). A value over 100 C asks once ("Stop limits over 100 C: at your own risk", **Yes, I accept the risk** /
  **No**); No puts both limits back to 100, Yes is kept (`Accepted High Temperature Limits`). A limit over 100 in
  `SRSDEF.ini` without that yes loads as 100. The CPU limit is now remembered (`Stress CPU Limit`). Command line:
  `burn` and `gpu-burn` take `--limit` 50..100, and 101..110 only with the new `--accept-high-limit`.
- **Update hardware lists** (Tools page, and `sysreport-search update-lists [--list] [--yes]`): downloads
  `pci.ids` (pci-ids.ucw.cz), `usb.ids` (linux-usb.org) and `pnp.ids` (hwdata) only when you ask, into
  `~/.local/share/srs/lists` (no root), with curl or wget. Each download must look like the list (its header,
  enough entries) or the copy in use stays; the previous copy is kept as `.old`. Every name lookup (device names,
  monitor makers, the Lookup and data-file rows) then uses the newer of your copy and the system's, by the list's
  own version (by entry count for `pnp.ids`, which has none). The window downloads in the background and reports
  each list.
- **ACPI method evaluation** for experts (Tools page; Menu > ACPI method evaluation (experts)...): type one ACPI
  method and its arguments (numbers, simple "strings", bHEX buffers, at most 7); `srs-root --acpi-eval` checks the
  name path and every argument again as root, evaluates it through the `acpi_call` kernel module (one password
  prompt per call) and logs the time, the call and the result to `~/.cache/srs/root/acpi-calls.txt`. Off until you
  accept a warning once (`Accepted ACPI Evaluation`). The ACPI Method Evaluation page shows the module's state and
  the last 30 evaluations. This is the one SRS feature that can change hardware state.
- **ACPI embedded controller register dump** (opt-in, off by default): **Options > Read the embedded controller**
  asks first (on some laptops reading an EC register clears a pending event); srs-root then loads `ec_sys` with
  `write_support=0`, copies the 256 EC registers once into `ec.bin` and unloads the module again if it loaded it.
  The ACPI Embedded Controller page shows the dump, or how to turn it on.
- Pages that said "not read" now show their data:
  - **System CMOS**: the CMOS RAM (0x0E-0x7F) from the kernel's `cmos_nvram0` device - no root, no port I/O - as a
    hexdump with CMOS addresses, the standard AT bytes decoded (diagnostic and shutdown status, floppy types, base
    / extended memory, checksum, century). The bytes are masked as `XX` when Redact Reports is on.
  - **System EBDA**: srs-root reads the BIOS Data Area pointer at 0x40E and, when there is an EBDA, its size and
    contents (below 0xA0000, at most 64 KiB) into `ebda.txt` / `ebda.bin`; the page shows pointer, size and dump
    (on UEFI PCs usually "no EBDA").
  - **GPU VBIOS status**: srs-root copies amdgpu's own copy of the VBIOS from debugfs (`vbios-<pci>.bin`; the GPU's
    ROM is not touched); the page checks the 55 AA signature and the first image's checksum, lists the images
    (PCIR) and the part number and build strings, and says when the driver's copy ends before the last image.
  - **I/O GPIO**: srs-root copies the kernel's debugfs GPIO table (`gpio.txt`), shown under the controllers.
  - **AMD Fuses**: the per-core factory values readable without root (amd_pstate highest performance,
    preferred-core ranking, max and lowest non-linear clock), the best-ranked CPUs and a plain-English note. The
    fuse registers themselves stay unread.
  - **MSR Setup**: package and per-core power in watts from the RAPL energy counters, which srs-root reads twice,
    one second apart (`rapl.txt`). The **MSRs** page explains each register in plain English.
  - **Firmware**: the UEFI System Table's ACPI / SMBIOS pointers and, for each ESRT entry, what it updates, its
    class GUID, version, lowest allowed version and the last update attempt, from srs-root (`efi-systab.txt`,
    `esrt.txt`). An AMI-style hex version is shown as the BIOS version it encodes (0x1805 = BIOS 1805).
- GPU Burnin's result shows which matrix multiply kernel ran. `SRS_GPUBURN_SIMPLE=1` forces the older, simpler
  kernel (for comparing).
- Tests: `tests/hot-limit-test.sh` (the 110 C question in the window and the command line); the GPU stub test runs
  the new kernel thread by thread and checks it in full against a plain multiply, and checks its launch shape on
  HIP and OpenCL and that the CUDA burn uses it from the built-in PTX; the Vulkan start-up check now also compares
  two whole rows with the host's sums (a deliberately broken shader is caught). New: `tests/mem-timings-test.sh`
  (the UMC, SPD, AOD and Intel decoders and the Memory Timings page from made-up data), `tests/lists-test.sh`
  (Update hardware lists from local files, no network), `tests/root-pages-test.sh` (the pages that read the root
  cache) and `tests/srs-root-args-test.sh` (what `srs-root --acpi-eval` accepts); test-only settings
  `SRS_TEST_LISTS_DIR` and `SRS_TEST_CMOS_FILE`.

### Changed

- **GPU Burnin loads the GPU much harder**: a register-blocked matrix multiply (64 x 64 tiles, 4 x 4 results per
  thread, the same summation order) on all four back ends - ROCm / HIP and OpenCL (`gpu_kernels.h`), Vulkan (new
  `gemm32b` / `gemm64b` shaders) and CUDA / ZLUDA (in the built-in PTX). ROCm's compilers turned the old 16 x 16
  tiled kernel into code that reached only about 3.4 TFLOPS FP32 on a Radeon RX 9070 XT; with the new one ROCm /
  HIP reached about 13.0 TFLOPS and OpenCL about 13.4 TFLOPS on the same card, Vulkan 6.2 -> 16.2 and CUDA via
  ZLUDA 5.3 -> 17.2 TFLOPS, 0 errors on every back end. It falls back to the old
  kernel (and on HIP then to hipBLAS) when it cannot be used. The GPU Compute comparison keeps the old kernel on
  every back end, so its numbers stay comparable with 2.3.
- Stress & Burnin: the closed **GPU options** line shows what it holds - back end, precision and the GPU stop
  limit (for example "GPU options: Auto (Vulkan), FP32, stop at 100 °C") - so the GPU's own limit is never hidden.
- System CMOS and AMD Fuses no longer need root data.
- srs-root also collects `umc.txt`, `aod.bin` / `aod.txt`, `intel-imc.txt`, `rapl.txt`, `ebda.txt` / `ebda.bin`,
  `vbios-<pci>.bin`, `gpio.txt`, `efi-systab.txt` and `esrt.txt`, and with the opt-ins `pmic-*.txt` and `ec.bin`.
  It still writes nothing to devices; the opt-ins and ACPI method evaluation are described above.
- Redact Reports: hexdump lines are masked in copied text too (System CMOS can hold the BIOS password check value).
- CPU Description: the flags are wrapped to about 90 characters a line; the empty `vmx flags` line is left out
  on AMD.
- BIOS option ROMs: the note points at the GPU VBIOS page, which shows the driver's copy of the GPU's image.

### Fixed

- GPU Burnin's live readout showed 0.0 multiplications/s (the rate was only worked out at the end).
- Power & Cost: the estimates note opened taller than the screen, with its text at the bottom and the button off
  screen (the wrapped label had no reading width). It now has a fixed width, the text at the top, and scrolls on
  small screens. The other dialogs with wrapped text got a minimum reading width for the same reason.
- OpenCL Information was empty when SRS found the cross-vendor `clinfo` (`/usr/bin/clinfo`, as when started from
  the desktop) instead of ROCm's: both output layouts are read now, each platform shows its own version, and
  clinfo's NULL-platform lines at the end are skipped.
- I/O GPIO: the pin table's tabs are expanded so the rows stay aligned.
- GPU VBIOS: the Option ROM line points at the image shown below when it is cached.

### Known limits

- Tested on one PC (Ryzen 7 7800X3D, Radeon RX 9070 XT, DDR5-6000 EXPO on an ASUS X870 board).
- **Intel memory timings are experimental**: there was no Intel PC to check them on; the decoding was tested
  only with made-up register values. DDR3 generations (Sandy Bridge to Haswell) are not covered.
- AMD memory timings: checked on one Ryzen 7 7800X3D (PM table 0x540104); the Ryzen 9000 PM table layouts
  (0x6211xx / 0x6212xx) are not checked, so some clocks or voltages may be missing there. Other AMD processors
  (Ryzen 5000 and older, laptops, Threadripper) say "not supported yet".
- The AOD table layout was checked on one BIOS; on other boards it is shown only when its timings match the
  live ones. It needs `iomem=relaxed` on kernels with IO_STRICT_DEVMEM; SRS never asks you to change that.
- The PMIC and embedded controller opt-ins were not checked on real hardware yet; the EBDA dump was not either
  (the author's UEFI PC has no EBDA).
- GPU Burnin's new kernel was run on the real card with ROCm / HIP and OpenCL; on Vulkan (lavapipe) and CUDA (a
  fake driver) only in the automatic tests so far.
- ACPI method evaluation runs BIOS code and can change hardware state: evaluate only methods you know.
- The fuse registers on AMD Fuses are still not read, and MSR Setup still shows no voltages.

## 2.3.1 - 2026-10-29

One Stress & Burnin window for the CPU and the GPU, every tool as a tab of the new main window, a "use at your
own risk" agreement for the load tests, columns that line up on every page, and smaller fixes.

### Added

- **Stress & Burnin**: CPU Burnin, CPU Stress and GPU Burnin in one window. CPU part: **CPU load**, **Timed**
  (burn / cool cycles, Pass or Fail) or **Until Stop**, temperature limit, Same / Swap CPU, the CPU tick boxes. GPU
  part: **GPU Burnin (built-in)** with **Time (min)** and **Sync to CPU stress / burn time** (Until Stop: until Stop;
  Timed: during each burn phase), and **GPU options** folded away (back end, GPU, FP32 / FP64, GPU temperature limit;
  defaults Auto, first GPU, FP32, 100 C). **Custom GPU command** (the old GPU Load command) is the third choice;
  it and the built-in one are either-or. CPU load off = a GPU-only run. The built-in GPU Burnin runs as its own
  process (`srs-gui --gpu-burn-child`): a hung GPU driver cannot freeze the window, Stop ends it (it ends cleanly
  on SIGTERM and is killed after 20 s), it ends with SRS (PR_SET_PDEATHSIG), and its exit code says PASS, stopped,
  FAIL, not started or ended early; compute errors or a GPU safety stop make the whole run Fail. Its live page
  (errors, GFLOPS, temperatures, power, clock, fan) and its result show in the same results area as the CPU
  numbers. The old menu entries open the window in their layout. New SRSDEF.ini keys `Stress GPU Burnin`,
  `Stress GPU Minutes`, `Stress GPU Sync`, `Stress GPU Backend`, `Stress GPU Precision`, `Stress GPU Limit`,
  `Stress Until Stop`, `Stress GPU Options Open`.
- **Tools as tabs** (new main window): on the Benchmarks & Stress page every tool is a tab with its controls and
  live results - Stress & Burnin, CPU Benchmarks, Cache & Memory, Storage, OS Latency, CPU Compute, Everyday Work,
  Network, GPU Benchmark, GPU Compare, SRS Pi, Crypto, Prime Sieve, IPI Delay, Memory Latency - built by the same
  code as the windows. The page's sections are compact (one short line each, two per row; full text in the
  tooltip) and open the tool's tab; the Tools page's sections are compact too. The Basic lineup keeps the full
  descriptions. Classic View keeps the separate windows. Leaving a running tool's tab, its page or the window
  asks **Stop it** / **Keep running**; Quit (Ctrl+Q) and the tray's Exit ask too.
- **Load test terms** ("use at your own risk"): shown once before the first stress test or benchmark (both
  views), **I Agree** / **I Disagree**; agreeing is kept (`Accepted Load Test Terms`, terms version 1), I Disagree
  starts nothing and Start asks again. A short notice with a **Terms...** link under every tool's controls;
  **Options -> Load test terms (warranty)...** reads them again or withdraws. Command line: the start question
  and `--help` of every load command carry the notice; `--yes` counts as agreeing; `burn --help` is new and
  `burn` prints the notice.
- **NUMA memory speed** runs the Memory Latency benchmark right on the page (new main window) and says when the
  shown run was made.
- **MSR Setup** shows how SRS reads MSRs (driver built in or module, root or not, cache time and size) and
  decodes the cached ones: P-state MHz (checked on a 7800X3D: 4200 / 3000 MHz), Core Performance Boost, SVM in
  EFER, CPPC on and CPPC_CAP1 per CPU ranked by highest performance, the RAPL units. Read-only as always.
- **Side tabs and the mouse wheel**: over a side tab list the wheel goes to the tab above or below, one notch per
  tab, no wrap-around; it never starts or stops a test (leaving a running tool still asks). With the tab list
  focused, **Home / End** go to the first / last tab and **Page Up / Page Down** 10 tabs back / on (no wrap-around);
  lists keep their own Home / End / Page Up / Page Down.
- **Close to tray**: the first time the close button only hides SRS, a notice says so and how to quit (Don't
  show this again; `Tray Close Notice Off`).
- Tests: `tests/stress-burnin-test.sh` (the GPU child and the window on lavapipe, 1 CPU for 3 s),
  `tests/load-terms-test.sh`, `tests/tab-wheel-test.sh` (xdotool), `tests/columns-test.py` (with the
  `SRS_TEST_ALIGN_DUMP` hook: 509 items with tables, 0 misaligned). Test hooks `SRS_TEST_GPU_BACKEND`,
  `SRS_TEST_GB_NMAX`, `SRS_TEST_BURN_AUTOSTART`, `SRS_TEST_TERMS_ANSWER`, `SRS_TEST_TAB_LOG`,
  `SRS_TEST_FIXED_COLS`, `SRS_TEST_ALIGN_MAIN`, `SRS_TEST_TRAY_NOTICE`.

### Changed

- **Columns line up on every page**: each block of tab stops gets columns as wide as their longest cell (at least
  the item's own widths, at most 380 px); a longer cell ends in "..." with the full text in its tooltip, in Copy
  and in a copied selection (Save Report is unchanged). Rows with more cells than stops get extra columns, and
  command output with tabs in it is cut into cells. 2.3.0 had 38 misaligned items in Classic View and 55 in the
  new main window's font (PCI bus, PnP Manager, USB bus, Processes, Software, ...).
- Page names: **Benchmarks & Stress**, **Power & Cost**, **Graphics & Audio**.
- Tool tabs and tool windows of the new view use compact controls (`srs-compact`) so the results get the height.
- srs-root lists the CPUs in the MSRs cache in order (CPU-0, 1, 2 ... instead of 0, 1, 10, 11 ...).
- The **SRS Stress & Burnin** menu entry replaces "SRS CPU Burnin & Stress"; the main entry's Stress and GPU
  Burnin actions are folded into Stress & Burnin.

### Fixed

- Start-up abort ("buffer overflow detected") in builds with FORTIFY_SOURCE (the AppImage and distro packages):
  the Network Benchmark, now built as a tab when SRS starts, looked up a wired adapter with `realpath()` into a
  buffer smaller than `PATH_MAX`. Found by the release start-test before 2.3.1 shipped. Builds from the Makefile
  now use `-D_FORTIFY_SOURCE=2` unless the packager sets it, so this kind of bug shows in every build.
- Tool titles in Title Case like the tab names: Cache & Memory Benchmark, Storage Benchmark, OS Latency
  Benchmark, CPU Compute Benchmark, Everyday Work Benchmark, Network Benchmark, GPU Benchmark, Crypto Benchmark,
  Prime Sieve, Benchmark History.
- GPU Burnin's live-status file (`~/.cache/srs/gpuburn-live-<pid>.bin`) is deleted when its window or tab
  closes; ones left by an SRS that crashed are removed at the next GPU run.
- Stress & Burnin, Timed with the GPU part synced: with a short Cool Time the GPU run of the next burn phase is
  started as soon as the last one has ended (it was skipped when that one was still stopping).
- Copy gives a "..." cell's full text once, also when a colour starts inside the cell.
- MSR Setup decodes P-state MHz only on AMD family 17h / 19h (Zen 1-4), where the layout is checked; other
  CPUs show the raw value. The AMD-V (SVM) line is AMD only.
- No benchmark is started (it says why) while the GPU part of a Stress & Burnin run is still ending (GPU Benchmark,
  GPU Burnin and GPU Compare check it on the command line too).
- `--help` lists the `stressburn` and `loadterms` panels; a few more names in Title Case (OS Latency, GPU Compute
  Comparison, Benchmarks and Stress).

### Known limits

- Tested on one PC (Ryzen 7 7800X3D, Radeon RX 9070 XT). The built-in GPU Burnin in Stress & Burnin was tested
  on lavapipe (software Vulkan) only, not on the real card; the custom GPU command path (gpu-burn-rocm) was run
  by the author with a CPU Burnin and passed.
- The mouse-wheel tab change was tested with synthetic clicks on Xvfb, where a tab's tooltip opens under the
  pointer and takes the wheel, so long runs of notches were checked on a real desktop only by hand.
- MSR Setup shows no voltages: the VID encoding of newer AMD CPUs (SVI3) could not be checked.

## 2.3.0 - 2026-10-29

CUDA without the NVIDIA runtime compiler (so ZLUDA runs every CUDA test), clearer OpenCL FP64 results, and an
optional, explained NVIDIA CUDA toolkit install.

### Added

- CUDA: SRS's own GPU kernels are built in, pre-compiled to PTX (NVIDIA's portable GPU code) with clang
  (`tools/gen-cuda-ptx.sh`, `make cuda-ptx`; `gpu_kernels_ptx.inc` is committed, so building SRS needs no CUDA
  tools). They are the same source and kernels NVRTC builds (`gemm_r`, `fma_r`, `copy_u4`, `cmp_u32`, `tick`, FP32 and
  FP64), PTX ISA 6.0 for `sm_50` and newer. The driver (or ZLUDA) is given the PTX first; NVRTC is only used when
  the driver refuses it. GPU Burnin and every CUDA row of the GPU Compute comparison, including the cuBLAS rows
  (their error check is one of the own kernels), now run without `libnvrtc` - which ZLUDA does not have.
- The optional NVIDIA CUDA toolkit: a plain-English explanation of what it is for; a question before the first
  CUDA run when its runtime compiler is missing (Install / Not now / Don't ask again, asked once, only when an
  NVIDIA card or a CUDA library is there); **Options -> NVIDIA CUDA toolkit for GPU tests...** (state, install
  command, Install, Ask me again); `sysreport-search cuda-toolkit [--check|--install|--ask-again|--never-ask]`.
  The install shows the exact command first (Arch `pacman -S --needed cuda`; Debian / Ubuntu `apt-get install
  libnvrtc12` or `libnvrtc11.2`; Fedora, openSUSE and others: instructions for NVIDIA's repository), runs it in a
  terminal window with sudo (or pkexec) only after you confirm, refuses while a benchmark or burn-in runs, with a
  locked package database, without network or without a password tool, and reports the result. With ZLUDA it
  warns that the toolkit can break or remove ZLUDA, defaults to Cancel and asks twice. The "Don't ask again"
  choice is `Ask CUDA Toolkit` in SRSDEF.ini.
- Packaging: the toolkit is an optional dependency (Arch optdepends `cuda`, plus `nvidia-utils` for libcuda;
  Debian Suggests `libnvrtc12 | libnvrtc11.2`; the RPM spec explains why it names none).
- Tests: `tests/gpu-stub-test.sh` checks the built-in PTX offline (header, entry names, order, parameter types
  against the NVRTC source, no outside calls; recompiled with clang when available) and runs CUDA with no NVRTC,
  with the PTX refused, and both; the fake runtime takes PTX and now also fakes OpenCL and CLBlast.
  `tests/cuda-toolkit-test.sh` covers detection, the command per distribution, the refusals, the typed yes,
  "Don't ask again" and the GUI windows on Xvfb with fake package managers (nothing is installed).
  Test-only overrides `SRS_CLBLAST_LIB`, `SRS_CTK_DRY_RUN`, `SRS_CTK_TOOL_DIRS` (read only in a dry run),
  `SRS_OS_RELEASE`, `SRS_PCI_SYSFS`, `SRS_CTK_ROUTE`, `SRS_CTK_PACMAN_LOCK`. The toolkit test also checks that a
  pacman / sudo planted in `~/.local/bin`, an empty or relative `PATH` entry or the current folder is never chosen,
  that the terminal script survives a `'` in `XDG_CACHE_HOME`, runs its typed-yes cases with every GPU library
  override and the `LD_DEBUG` check, and searches every trace once more for the real libcuda / libcublas.
  `tests/gpu-stub-test.sh` adds a GEMM library that says "not supported" only during the run
  (`STUB_DGEMM_UNSUP_AFTER`).

### Changed

- Dark theme: warning text ("not recommended", failed checks) uses a brighter red; Adwaita dark's error colour was
  hard to read on the dark background.
- A GEMM library that answers "not supported" for a precision (ZLUDA's cuBLAS has no FP64 GEMM; cuBLAS 15 /
  hipBLAS 7) gives a "not available" row with that reason instead of "failed" (seen on the RX 9070 XT with ZLUDA).
- The CUDA / HIP column's software line says how the own kernels are made: "own kernels built in (PTX)" with
  "NVRTC as fallback" or "no NVRTC" for CUDA, "no runtime compiler (libhiprtc), own kernels unavailable" for HIP.
- When the driver refuses the built-in kernels and NVRTC is missing, the CUDA rows say so and point to
  Options > NVIDIA CUDA toolkit; after a CUDA run without NVRTC the command line prints one note with the menu path
  and the command (not after "Don't ask again").

### Fixed

- Security (review finding, before release): the toolkit install ran the package manager - and the GUI `pkexec` /
  `sudo` themselves - by a bare name, so `pkexec` looked it up in the user's `PATH`: a `pacman` or `apt-get`
  planted in `~/.local/bin` would have run as root after the password. The package manager, `sudo`, `pkexec` and
  `apt-cache` are now taken only from `/usr/bin`, `/usr/sbin`, `/bin` or `/sbin` and run by their full path (GUI
  pkexec and terminal, CLI `execv`); otherwise SRS refuses and shows the command to type. Empty and relative
  `PATH` entries no longer mean the current folder. The confirmation and `cuda-toolkit --check` show the full
  paths ("Programs used").
- The toolkit install's terminal script quotes every path and word for the shell, so a `'` in `XDG_CACHE_HOME`
  no longer breaks it; terminals start `/bin/sh` by full path.
- The GPU Compute comparison: a GEMM library that says "not supported" during the run (after it took the size in
  preparation) now marks that one row "not available" instead of ending the column as if the GPU had hung.
- The GUI no longer waits for `apt-cache` (Debian / Ubuntu, up to a few seconds) when Run starts a CUDA test: the
  answers are asked on the GPU window's worker thread and kept for the session (asked again after an install).
- The toolkit question no longer loads the CUDA library in the test mode (`SRS_GPU_SOFTWARE_ONLY=1`) unless
  `SRS_CUDA_LIB` names one; the rule "before Yes only dlopen / dlsym, no cuInit / hipInit / OpenCL query" is
  written down where the code relies on it.
- "Asked once": Install... followed by Cancel in the question no longer asks again at the next CUDA run in the
  same session.
- Status files of terminal installs SRS stopped waiting for are removed at the next install and when SRS exits.
- `Ask CUDA Toolkit` in SRSDEF.ini: a longer key that starts the same (`Ask CUDA Toolkit2`) is left alone.
- OpenCL drivers without double precision (often Mesa's Rusticl) showed the FP64 rows as "failed" with an empty
  reason. FP64 own-kernel, CLBlast and multiply-add rows are now "not available: this OpenCL driver has no FP64
  (double precision)" when the device reports neither `CL_DEVICE_DOUBLE_FP_CONFIG` nor `cl_khr_fp64`, and a kernel
  that does not build always shows the OpenCL error code (and says when the driver gave no build log).

### Known limits

- The built-in PTX was checked offline and against a fake driver, and run through ZLUDA (preview build, no
  NVRTC) on a Radeon RX 9070 XT in the GPU Compute comparison on 2026-10-29: every CUDA row ran from it
  (own-kernel FP32 matrix multiply 5377 GFLOPS, cuBLAS FP32 16392, cuBLAS FP16 77046, own-kernel FP64 860 GFLOPS); cuBLAS FP64 is "not available" there because ZLUDA's cuBLAS has no FP64 GEMM (error 15). GPU
  Burnin with CUDA was not run on the real card, and nothing was run on NVIDIA hardware.
- Without a terminal program the install uses `pkexec` with pacman's `--noconfirm` (apt `-y`): the default
  answers, which for pacman means no to removing a conflicting package. With ZLUDA `pkexec` is never used.
- HIP still compiles its kernels with hipRTC: HIP code objects are per GPU generation and per ROCm release, so
  shipping them would bloat SRS and still miss GPUs.
- The toolkit install was tested with fake package managers only; the terminal programs other than their names
  in PATH were not started in a test.

## 2.2.2 - 2026-10-27

Fix for a crash in the GPU Compute comparison and GPU Burnin, found by a user running CUDA through ZLUDA.

### Fixed

- GPU Comparison/GPU Burnin: fixed a crash when the vendor GEMM library (hipBLAS/cuBLAS) was measured
  before the own kernels (found by a user running CUDA via ZLUDA). The library variant checks every
  result with one of SRS's own kernels, which was only loaded for the own variant, so the driver got
  a NULL kernel. The kernels are now always loaded for GEMM, a kernel launch with a missing kernel
  is refused with a message (HIP, CUDA and OpenCL), and a half-loaded module is never kept.
- Without libnvrtc / libhiprtc the library test says it needs the runtime compiler for the error
  check; GPU Burnin shows that reason instead of "cannot run that precision".
- GPU Comparison lists why a test is "not available" under the table.

### Added

- `tests/gpu-stub-test.sh` and `tests/gpu_stub.c`: the HIP and CUDA back ends run against a fake GPU
  runtime that aborts on any NULL kernel launch (no GPU needed). Test-only overrides
  `SRS_HIP_RTC`, `SRS_CUDA_RTC`, `SRS_HIP_BLAS`, `SRS_CUDA_BLAS` (see docs/CLI-REFERENCE.md).

## 2.2.1 - 2026-10-27

Fixes from the final review of 2.2.0: GPU Burnin messages, the All GPUs verdict, and the window size.

### Fixed

- After a GPU driver call that never returned (the 15-second path), the next GPU test said "GPU
  Burnin is running; wait for it to finish" and CPU Burnin Start did nothing. They now say "a GPU
  call did not return earlier; restart SRS (or the PC) before the next GPU test", and a refused
  CPU Burnin start shows its reason in the status line.
- GPU Burnin with **All GPUs**: a card that cannot start is marked COULD NOT START and the verdict
  comes from the cards that ran, with a note (GUI status line and `gpu-burn` on stderr). Before,
  the whole run was reported as ended early by a safety rule although the others passed.
- The flag that keeps CPU Burnin and the GPU tests from starting together is now an atomic
  (seq_cst), so it no longer relies on x86 memory ordering (aarch64 and others).
- GPU Compute comparison: Run while the back-end check is still going says "Checking which back
  ends are installed..." instead of asking for a tick on greyed boxes.
- The default size of windows is limited to about 90% of the monitor's work area (the main window
  asked for 1240 x 800 on every screen). A window still cannot shrink below what its content needs
  (the main window about 990 px wide).
- User guide: new screenshots of the Benchmarks and Tools pages and the classic menu.

## 2.2.0 - 2026-10-26

Power & cost, new benchmarks, and a main window you can arrange: tab lineups, sortable tables,
a Startup manager, CPU clocks at stock and in effect, and a badge that shows what SRS can read.

### Added

**Power & cost**

- **Power & cost: how much power the PC uses and what it costs.** A new page in the sidebar,
  right under Sensors (Alt+9). **Estimates only, not a meter reading and not a bill**: the page
  says so in full the first time it opens, then in a banner and at the top of every export.
  - **Now:** the whole PC's power at the wall with its likely range (for example "about 160 W,
    likely between 116 W and 205 W"), the cost per hour, day, month and year at your price, a
    10-minute graph with the range as a band, and where the watts go. The processor is measured
    (zenergy, amd_energy, or the RAPL counter when it is readable), the graphics card as its
    driver reports it (amdgpu, Intel, nouveau); everything else is estimated from a parts list
    and the power supply's efficiency.
  - **Recording:** off, for a set time, or always, one row per minute in a local SQLite file
    (`~/.local/share/srs/power.db`, readable only by you). Energy counters that run round are
    unwrapped; sleep (from the clocks) and shut-downs (from the boot ID) are noticed and counted
    at the "asleep" and "off" watts you set; time SRS was not running is a gap, never made-up
    data. Optional **background recorder** (a systemd user service running
    `srs-gui --power-recorder`) for when SRS is closed.
  - **History:** energy or cost by hour, day, week or month with the likely range, the busy vs
    idle split, a table, **CSV export** (any resolution, decimal point or comma) and re-pricing.
  - **Sessions:** stretches of heavy use such as games or renders, with length, energy, cost,
    peak and CO2.
  - **Price:** any currency; flat, tiered (with the rest of the home's use counted first) or
    time-of-use (a 24-hour weekday/weekend grid); taxes and an optional standing charge.
    **Typical prices for your area, offline:** US state averages (EIA), the utility for a US ZIP
    code (NREL), European countries (Eurostat), the GB price cap (Ofgem). Nothing is sent
    anywhere. What-ifs (left on 24/7, this month's forecast) and an optional CO2 estimate from
    grid averages (Ember).
  - **Setup:** the power supply (presets for the 80 PLUS levels at 115 V and 230 V, and the
    Montech CENTURY II 1200W; or type a curve from a Cybenetics report), the parts list SRS
    detects on this PC, editable, with typical parts to add; sleep and off watts; how often to read.
  - **Alarms:** power now or on average over a limit, energy or cost per day or month, and month
    forecasts, as desktop notifications; limits in W, kW, BTU/h, Wh, kWh, MWh, kJ or MJ with the
    conversions shown.
  - **Sources:** what can be measured on this PC and why. SRS never loosens the RAPL permission
    that Linux 5.10 made root-only (PLATYPUS).
  - Guide section 6.11 explains all of it in plain words, with a glossary of W, Wh, kWh and the
    rest; [CREDITS.md](CREDITS.md) lists the data sources and their licences.

**Benchmarks**

- **SRS Pi**: works out pi to 128 thousand, 1, 2, 4, 8, 16 or 32 million decimal places and checks
  every digit against built-in SHA-256 checksums of the true digits. Two methods: Chudnovsky (fast,
  can split its work over threads) and Gauss-Legendre / AGM (the method SuperPi uses, one thread).
  Window with Digits, Method, Threads, Run and Stop, and a status line showing the current step;
  command line `sysreport-search pi [--digits 1m] [--threads N] [--method chudnovsky|agm]
  [--save-digits FILE]`. Times are SRS Pi times, not SuperPi times (SuperPi's "1M" is 1,048,576
  digits). Example, Ryzen 7 7800X3D, package build, one thread: 1 million digits 3.1 s.
  **SuperPi sizes**: a tick box (and `--digits 16ki` ... `32mi` on the command line) switches to the
  power-of-two digit counts SuperPi uses, 16K = 16,384 ... 1M = 1,048,576 ... 32M = 33,554,432,
  each with its own built-in checksum.
- **Cache & Memory benchmark** - Linux System Audit (LSA), the author's own project, is now part
  of SRS. Read / Write / Copy bandwidth and Latency of the RAM and the L1, L2 and L3 caches, with
  the measuring engine of LSA 0.2.0 unchanged (same results), in an SRS window and SRS's own
  wording: Threads, Seconds per test, Run (asks first: about 20 s on every thread), Stop, live
  status. Below the table: threads and vector code, RAM write method, measured clock, the
  **measured base clock** (105 MHz on boards that raise it), multiplier, **Infinity Fabric clock**
  (Ryzen 7000 desktop with `ryzen_smu`), memory type / speed / channels (root data), cache sizes.
  Command line `sysreport-search cachemem` with LSA's options (`--threads`, `--seconds`, `--isa`,
  `--mem-mib`, `--only`, `--tests`, `--mib`, `--mem-write`, `--yes`). x86-64 only.
- **Storage benchmark**: read and write speed of the drive that holds a folder you choose -
  Sequential 1 MiB (Q8T1), 4K random (Q32T16, or Q8T4 in the Light profile) and 4K random (Q1T1),
  each read and write, with average, peak, IOPS, 4K response times (mean, median, 95% / 99%,
  slowest), drive model and connection (never the serial number), file system and drive
  temperature before / after. Works only on its own temporary test file (created new and unlinked
  at once, so nothing can be left behind - also after Stop, a crash or a kill); `O_DIRECT` with
  Linux native AIO for real queue depths. Refuses RAM (tmpfs), network and FUSE folders and too
  little free space; USB drives only when allowed; asks before it starts. Test file 256 MB - 16 GB,
  1-30 s per test. Command line `sysreport-search storage --dir FOLDER`.
- **OS Latency benchmark** (`sysreport-search os-latency`, Benchmarks & stress -> OS Latency,
  `--gui-open=oslatency`): how quickly the operating system answers a program - system calls
  (getpid, vDSO and real clock_gettime, 1-byte read, sched_yield), pipe context switches between
  threads and processes on one CPU and on two cores, thread create, fork and posix_spawn, futex and
  condition-variable wake-ups, sleep overshoot and a periodic 1 ms tick (missed ticks counted),
  4 KiB and 2 MiB page faults, mmap / munmap, and small-file create / stat / unlink, each as
  median / 95% / 99% / worst / best. Shows the CPU scheduler in use (sched_ext ops such as
  scx_lavd, BORE, EEVDF), preemption mode, CONFIG_HZ, clock source, timer slack and THP mode.
  Light (one or two CPUs, about 6 s), changes no system setting, asks before it starts (`--yes`
  skips the question), Stop works; its temporary file folder in `~/.cache/srs` is always removed.
- **CPU Compute benchmark** (`sysreport-search compute`, Benchmarks & stress -> CPU Compute,
  `--gui-open=compute`): floating-point work in GFLOPS / Mpixels/s on 1 thread and N threads -
  matrix multiply SGEMM / DGEMM (288 x 288), peak multiply-add (single, double), FFT 4096 points
  (single, double), N-body (1,024 bodies), Mandelbrot (640 x 480) and a small ray tracer
  (320 x 240). Plain C, SSE2, AVX2 + FMA and AVX-512 code chosen at run time (a generic x86-64
  package still uses AVX-512; `--code` forces one). Every answer is checked against a reference
  (exact for matrix multiply and multiply-add); a wrong one is shown as WRONG, not as a score.
  Asks before it starts and warns that an unstable overclock or undervolt can crash the PC under
  this load; `--single` / "1 thread only" keeps it light. Stop works.
- **Everyday Work benchmark** (`sysreport-search work`, Benchmarks & stress -> Everyday Work,
  `--gui-open=work`): compression and decompression with the system's zlib (-1/-6/-9), zstd
  (-1/-3/-19), lz4 and xz (-1/-6) in MB/s with the ratio on text, records and random data;
  sorting (qsort vs radix sort, qsort vs multikey quicksort) in Mkeys/s; CRC32, CRC32C (also the
  CPU instruction on x86-64), xxHash64 and FNV-1a in GB/s; substring search, JSON and UTF-8
  checks in GB/s; a bytecode interpreter (switch and threaded dispatch) in Mops/s - on 1 and N
  threads, every result checked (round trips, sorted order, known answers). The compression
  libraries are loaded at run time (`dlopen`), so they are optional; a missing one shows "not
  installed". Generated data only. Asks before it starts with the expected time; Stop works; one
  history line per test group.
- **Network benchmark** (`sysreport-search net`, Benchmarks & stress -> Network,
  `--gui-open=netbench`): loopback TCP throughput with 1 and N streams, UDP throughput and packet
  loss, TCP and UDP round trip time (nothing leaves the computer); optional internet test with
  DNS lookup (first, repeated, uncached), TCP connect and HTTPS (connect, TLS, reply) times and
  download / upload speed with 1 and N streams against Cloudflare's public speed test. The
  internet test asks separately (No is the default; `--yes` does not answer it, `--yes-internet`
  does), stating the server, the data use (up to about 1000 MB with the defaults) and that the
  server sees the public IP. libcurl is optional (loaded at run time). No addresses, host names or
  network names in the results. Stop works.
- **GPU Benchmark** (`sysreport-search gpu`, Benchmarks & stress -> GPU Benchmark,
  `--gui-open=gpubench`): through Vulkan, FP32, FP16 and FP64 multiply-add (GFLOPS), INT32
  multiply-add (GIOPS), video memory read, write and copy and upload / download (GB/s, 1 GB =
  10^9 bytes), and dispatch latency (µs, median, best and 95%). Every result is checked against a
  value SRS works out itself; a wrong one is shown as WRONG RESULT, never as a speed. Timed with
  GPU timestamps when the device has them. The Vulkan loader is opened at run time (`dlopen`), so
  Vulkan is optional and SRS needs no Vulkan headers to build; the shaders are pre-compiled into
  SRS (`shaders/gpu/`, `make gpu-shaders`). Work is cut into pieces of about 25 ms, one at a time,
  each with a 5 s watchdog; a lost or stuck GPU ends the run cleanly. Asks before it starts
  (the GPU, the time, full GPU load, overclock / undervolt warning); Stop works; one history line
  per run. Vulkan overlay layers such as MangoHud are kept out of the run. `--list` lists the
  Vulkan devices without running anything.
- **GPU Burnin** (`sysreport-search gpu-burn`, Benchmarks & stress -> GPU Burnin,
  `--gui-open=gpuburn`): keeps one GPU or all GPUs fully busy with repeated large matrix
  multiplications (FP32 or FP64) and counts compute errors - the first result is the reference and
  every later one is compared with it on the GPU (idea from gpu-burn by Ville Timonen; SRS's own
  code); 0 errors = PASS. Back end Vulkan, ROCm / HIP, CUDA (labelled "CUDA (ZLUDA)" when the CUDA
  library is ZLUDA) or OpenCL (each driver its own entry), or Auto; all loaded at run time, a
  missing one says "not installed" with a hint. Own kernel through hipRTC / NVRTC / the OpenCL
  compiler, else hipBLAS / cuBLAS / CLBlast. Live readout of GFLOPS, errors, edge / hotspot /
  memory temperature, power, clock and fan (hwmon by PCI address; "not available" when not
  reported). Ends by itself at the temperature limit (default 100 °C, hotspot when reported),
  on any driver error, device lost, or a piece of work that takes more than 2 s; never changes
  clocks, voltages or fans. Asks before it starts (No is the default in the window). Default 60 s,
  up to 24 hours. One GPU test at a time, and CPU Burnin / Stress refuses to start while a GPU
  test runs (and the other way round).
- **GPU Compute: Vulkan vs HIP vs CUDA vs OpenCL** (`sysreport-search gpu-compare`, Benchmarks &
  stress -> GPU Compute comparison, `--gui-open=gpucompare`): the same workloads with the same
  sizes through every installed back end on the same GPU (matched by PCI address), in one table:
  FP32 / FP16 / FP64 matrix multiply with SRS's own kernel and with the vendor library (hipBLAS,
  cuBLAS, CLBlast), FP32 / FP64 multiply-add chain, video memory copy, upload, download and kernel
  launch round trip; the fastest marked per row and every column as a percentage of Vulkan.
  "CUDA (ZLUDA)" when the CUDA library is ZLUDA; one column per OpenCL driver (CPU drivers marked);
  "not installed" with a hint for missing back ends. Every result checked. Asks before it starts;
  HIP / CUDA / OpenCL runtimes start only after Yes; Stop works.
- **Crypto benchmark**: AES-128-CTR and SHA-256 speed in MB/s on one thread and on N threads, with
  AES-NI / SHA-NI and in plain C; the FIPS 197 / FIPS 180-4 test vectors are checked before every
  run. `sysreport-search crypto [--threads N] [--seconds S]`.
- **Prime Sieve**: counts the primes up to 10^8 ... 10^11 on N threads and checks the count.
  `sysreport-search prime [--limit 1e10] [--threads N]`.
- **Benchmark history**: every run of Cache & Memory, Storage, OS Latency, CPU Compute, Everyday Work,
  Network, GPU Benchmark, GPU Burnin, GPU Compute comparison, SRS Pi, Crypto and Prime Sieve adds a line to
  `~/.cache/srs/bench-history.tsv`, shown newest first on the new Benchmark history tab. Each
  result carries a **Build** line (compiler, CPU code level), because a generic package build and a
  `-march=native` build of the same version score differently.
- Benchmarks & stress page: Cache & Memory, Storage, OS Latency, CPU Compute, Everyday Work, Network, GPU Benchmark,
  GPU Burnin, GPU Compute comparison, SRS Pi, Crypto and Prime Sieve buttons and tabs; Menu -> Benchmarks &
  stress entries (classic view); desktop-menu actions;
  `--gui-open=cachemem|storagebench|oslatency|compute|work|netbench|gpubench|gpuburn|gpucompare|pi|crypto|prime`.
  Every benchmark asks before it starts, saying what it does, for how long and how much load (see
  Changed). 558 items in all.

**Main window**

- **Tabs you can arrange.** On every page: **+** (or Ctrl+T) adds a tab that stays; right-click a
  tab for **Keep this tab**, **Replace with...**, **Move left / right**, **Close tab**, **Tabs on
  this page...** and **Reset this page's tabs to the lineup**; drag to move; close with its x,
  Ctrl+W or a middle-click. SRS remembers each page's tabs and their order. Items opened from the
  item list, the search, Details or a device lookup are temporary (in italics) until you keep them.
- **All N items is a searchable jump list**: type to filter, Enter or a click goes to the item's
  tab or opens it.
- **Which tabs a page opens with** (Options -> Tabs on each page...): the standard tabs, the ones
  you tick, or every item; **Show every item as a tab** does it for all pages at once.
- **Tab bar layouts** (Options -> Tab bar): one row that scrolls, wrapped rows (every tab visible),
  or a side list. Ctrl+PageUp / Ctrl+PageDown step through the tabs in every layout.
- **Lineups**: saved arrangements of every page's tabs, their order and the tab bar. Built in:
  **Basic** (a few friendly tabs), **Enthusiast** (the standard tabs, as in 2.0) and **Expert** (the default)
  (every item as a tab, side list). Save your own, save changes to it, export and import
  `.srslineup` files, reset to the default. Menu -> Lineup, Options -> Lineup, or a tab's
  right-click menu; the sidebar shows the lineup in use and "(changed)".
- **Items that cannot show anything here are hidden**: SIV's Windows-only items and hardware that
  is not present, always. Items that come out empty on this PC are hidden too once SRS has checked:
  **Options -> Check now** (in a separate process), or **Check daily**, which is off unless you turn
  it on. The check never runs the network items or Ping. **Options -> Show hidden items** lists
  them greyed with the reason.
- **Sortable tables**: click a column heading in a tab's table to sort it, first click ascending
  (▲), second descending (▼). Numbers sort by value with their units (KB / MB / GB, MHz / GHz,
  °C, RPM, %, V, W, times), dates by date, process numbers by number, names naturally. **Reset
  view** (only on tabs with tables) puts the item's own order back. Copy and Save Report keep the
  item's own order. Options -> Sortable tables turns it off.
- **Startup manager** on Operating system -> Startup programs. The tab opens on **Report** (the
  item as text, read-only); **Manage**, one click away, lists every autostart entry, systemd user
  unit and, if you tick it, system service with a switch. Nothing is deleted: a system autostart entry is turned off with a copy in your autostart
  folder that says Hidden=true, your own entries get Hidden=, and files SRS moves aside or changes
  are kept in `~/.config/srs/startup-backup`. **Undo**, **Restore default** (systemd: the
  distribution's preset) and **Add to startup...** for an installed program or a command, with an
  optional delay. Desktop essentials (Plasma, KWin, KWallet, PipeWire, portals, polkit agent ...)
  ask before they are turned off; system services ask and use pkexec. `SRS_STARTUP_DRY_RUN=1`
  shows the systemctl / pkexec commands instead of running them.
- **Privilege badge** in the title bar: **Full access** when SRS runs as root or srs-root's data is
  there (the tooltip says when it was collected), else **Limited (user)** with a **Run as root...**
  button and a tooltip listing what root unlocks.
- **CPU Clocks: stock and in effect now.** A second table: the base clock measured (TSC x 100 /
  CPPC nominal frequency, once per run), core base, boost and the fastest core now, each at stock
  (100 MHz reference) and in effect, with the difference; FCLK, UCLK and MCLK from the SMU's PM
  table (ryzen_smu, Zen 4 desktop layout, values checked) shown without a difference, because
  software cannot measure them. Also in Save Report and the command line.

**Tests**

- `make test-power`: unit tests of the estimate, counter unwrapping, sleep/reboot handling,
  prices, sessions, alarms and the history database with synthetic counter data; the Debian and
  RPM builds run them.
- `tests/bench-test.sh`: correctness tests (digit for digit against mpmath when installed), and
  for `bench`, `ipi`, `latency`, `pi`, `crypto` and `prime`: asks first (no terminal and no `--yes`:
  exit 2, nothing written), unknown option / missing value refused, `--help`, the `y` / `n` answer
  on a terminal, and tiny `bench` / `ipi` / `latency` runs.
- `tests/bench-os-test.sh`: OS Latency engine test (tiny runs, ordered statistics, Stop, no child
  process or temporary folder left, ask-first in the CLI). `make test-bench` runs all benchmark
  tests against `dist/native/sysreport-search`.
- `tests/bench-compute-test.sh`: CPU Compute self-test on every code path the CPU has, detection
  of a spoilt result on every kernel, each code path forced, Stop, sanitizer run, CLI.
- `tests/bench-work-test.sh`: Everyday Work under AddressSanitizer + UBSan: known answers, every
  codec round trip, the "not installed" path, Stop, result file, history lines, CLI.
- `tests/bench-net-test.sh`: Network loopback test, no addresses or host name in the output,
  Stop, refusals (http:// server, nothing chosen), strace check that only 127.0.0.1 is contacted,
  and that `--internet` never starts without its own yes.
- `tests/bench-gpu-test.sh`: the GPU benchmarks on lavapipe (Mesa's software Vulkan renderer)
  only - the test refuses to run anything unless every Vulkan device it sees is the software
  renderer - with the GPU files built under AddressSanitizer + UBSan; Vulkan type layouts checked
  against the Khronos header for 64-bit and 32-bit; the pre-compiled shaders checked against their
  sources; Vulkan / driver missing paths; Stop; result file, history line; the CLI (help, unknown
  option, no terminal, `--yes`). GPU Burnin: PASS with 0 errors, live callback, FP64, Stop, a
  second run right after (lock released), CPU Burnin refused while a GPU test runs; HIP, CUDA and
  OpenCL only as far as loading the libraries and finding every function (`dlopen` + `dlsym`;
  no HIP, CUDA or OpenCL work is run) and the "not installed" / "no OpenCL platform" paths. GPU
  Compute comparison: the Vulkan column with the own-kernel rows, "not available" library rows,
  the fastest marker, three "not installed" columns, Stop, and the CLI; outside the test mode a
  software renderer is refused by GPU Burnin.
- `tools/check-mnemonics.py` (`make check-mnemonics`): no two entries of a menu or window page share
  an Alt+letter. `packaging/test.sh` (run in every distro container) checks the `--help` and the
  not-started path of every benchmark command and `gpu --list` on a system without GPU libraries.

### Changed

- **Every benchmark now asks before it starts.** The Benchmark (Dhrystone / Whetstone), IPI Delay,
  Latency, SRS Pi, Crypto and Prime Sieve windows used to start the moment you pressed Run; now Run
  asks first, like the newer benchmarks, saying what runs, about how long, how many CPU threads are
  busy and (SRS Pi) how much memory it needs. Heavy runs - SRS Pi from 8 million digits, Prime
  Sieve from 10 billion, all-thread runs and the CPU Benchmark modes that load many threads at once
  (`ac`, `pc`, `lc`) - warn about unstable overclocks and undervolts and have **No** as the default
  answer, like GPU Burnin and the internet speed test. On the command line `bench`, `ipi`, `latency`, `pi`, `crypto` and `prime` ask the same
  question (`Start now? [y/N]` on stderr); `--yes` (`-y`) starts at once; without a terminal and
  without `--yes` they exit 2 and start nothing. `--help` prints the usage; an unknown option or a
  missing value exits 2 without starting (`bench`, `ipi` and `latency` used to ignore unknown
  options). The CPU Burnin and CPU Stress windows already asked and are unchanged; the `burn` command
  is the burn-in itself and starts at once, as before.
- Page keys: the Alt keys follow the sidebar order with Power & cost under Sensors, so USB & PCI
  devices moves to Alt+0, Operating system to Alt+-, Benchmarks & stress to Alt+= and Tools to
  Alt+Backspace (Overview ... Sensors keep Alt+1 ... Alt+8; Power & cost is Alt+9).
- The first time the 2.2 main window opens it asks which tab lineup you want (Basic, Enthusiast or
  Expert, the default) and says where to change it later (menu button or Ctrl+M, then Lineup). If
  you used SRS before, your pages keep the standard tabs of 2.0 (Enthusiast) unless you pick another
  lineup.
- Classic view: the classic Menu keeps its benchmarks and stress tests in a **Benchmarks & stress**
  submenu (it had more entries than there are letters, so Alt+letter could not reach them all).
  Every menu and page now has one Alt+letter per entry; some letters changed (for example
  Options -> S_ortable tables, Upd_ate interval, Chec_k daily, Hide to Tra_y).
- `srs-gui` now needs SQLite (all packages and the AppImage). The command-line program does not.
- The user guide's Tools chapter no longer sends readers to the separate lsa-sysinfo project for
  memory bandwidth: the Cache & Memory benchmark is in SRS (9.11).
- Every tab can be closed now, the standard ones too (All N items and Reset bring them back); the
  close button shows on the tab in view and on temporary tabs.
- The Startup programs item reads `$XDG_CONFIG_HOME/autostart` when that is set (before: always
  `~/.config/autostart`).

### Known limits

**Tested on one PC.** I build and test SRS on a single machine: a Ryzen 7 7800X3D with a Radeon
RX 9070 XT, on a wired connection. I've done everything I can for the rest (checks that don't need
the hardware, careful reading of the docs, test builds), but I can't try what I don't own. So
anything that needs other hardware, such as NVIDIA CUDA (and ZLUDA), Intel graphics, any ROCm/HIP or
OpenCL setup other than mine, Wi-Fi or VPN network tests, or ARM and 32-bit builds, may have a bug or two I haven't met yet. If you find one, please
tell me: open an issue on GitHub (https://github.com/eugenesflanagin-spec/srs-sysinfo/issues)
with your hardware and the report SRS made, and I'll fix it. Not tried on real hardware so far:

<!-- untested areas: each benchmark adds its line; the GPU pass adds the GPU lines here -->
- GPU benchmarks (GPU Benchmark, GPU Burnin, GPU Compute comparison): the Vulkan tests, GPU
  Burnin and the GPU Compute comparison were run on lavapipe (the Mesa software renderer, which
  runs on the CPU), with small sizes, so the logic, the error counting, Stop and the result pages
  are checked; the Vulkan type layouts were checked against the official Khronos header for 64-bit
  and 32-bit builds. ROCm/HIP, CUDA and OpenCL were tried only in part: I checked that the
  libraries load and that every function SRS needs is found in them (including on my own ROCm, my
  ZLUDA and my OpenCL loader), and that a missing library or an OpenCL loader with no driver is
  handled cleanly, but I did not run any HIP, CUDA or OpenCL work on a GPU. Not run on real
  hardware: Vulkan on a real GPU, GPU Burnin on a real GPU, CUDA on NVIDIA, ZLUDA, ROCm/HIP on AMD,
  OpenCL on AMD (ROCm) and Mesa Rusticl, anything on Intel graphics, and the temperature, power,
  clock and fan read-outs for NVIDIA and Intel.
- ARM (aarch64) and 32-bit (i686) builds: compile-checked only (plus self-tests where a benchmark
  says so), not used day to day.
- OS Latency: run only on Linux 7.x with the scx_lavd scheduler; BORE, EEVDF and CFS are recognised
  from the kernel's settings but were not run here.
- CPU Compute: the AVX-512 code has only run on a Zen 4 CPU (Ryzen 7 7800X3D); the SSE2, AVX2 and
  plain C paths were checked by forcing each one on that same CPU (self-tests), not on older CPUs.
  The 32-bit (i686) self-test passed; the aarch64 build ran its plain C self-test only.
- Everyday Work: run with one Arch Linux system's compression libraries (zlib-ng in zlib mode,
  zstd 1.5, lz4 1.10, xz 5.8); its time estimate is rough. The CPU-instruction CRC32C row is
  x86-64 only (the i686 and ARM builds show it as "not built into this image").
- Network: run on one wired Ethernet connection only; Wi-Fi, VPN and metered or mobile
  connections are untested. The internet speed test has been run once by the author on that wired
  connection, against Cloudflare, with sensible results (download several hundred Mbit/s, upload
  about 60 Mbit/s; the report showed no IP address, host name or MAC address). Cloudflare's speed
  test endpoints are not a documented API; if they change, the speed rows fail and DNS / connect
  times still work.

A GPU back end that is not installed shows as "not installed" with a one-line hint; CUDA that is
really ZLUDA is labelled "CUDA (ZLUDA)"; every OpenCL driver is its own column (CPU drivers such as
PoCL are marked CPU); speeds are only comparable between alike SRS builds and the same GPU driver /
runtime versions. Opening a GPU window's list of GPUs, or pressing Run, reads the GPU list through
Vulkan the way `vulkaninfo` does (the GPUs are named, no work is sent until you answer Yes); ROCm /
HIP, CUDA and OpenCL start only after Yes. A card that reports no temperature to SRS cannot be
stopped at the temperature limit: GPU Burnin says so in the live readout and the result.

### Fixed

- Only one benchmark runs at a time across all windows (before, two benchmark windows could run at
  once and spoil each other's results), and CPU Burnin / Stress will not start while a benchmark
  runs. The window says why it did not start.
- A live tab's refreshed line could take the colour of the text next to it.


## 2.0.0 - 2026-10-26

This is SRS 2.0, with a new main window: an Overview that answers "Is my PC OK?", one page per
topic and a search box. The classic SIV-style windows are still there, one click away.

### Added
- **A new main window: easier to find your way, nothing taken away.** One window with a sidebar of
  pages: **Overview**, Processor, Memory, Motherboard & BIOS, Graphics & audio, Storage, Network,
  Sensors, USB & PCI devices, Operating system, Benchmarks & stress, and Tools. It follows the
  desktop's light or dark theme.
  - **Overview** answers "Is my PC OK?" with a status light per check: CPU and graphics
    temperature, drive health (SMART) and drive temperature, hardware errors (machine checks, ECC
    memory errors, faulty memory pages), free space on the system disk, memory in use, sensor alarm
    flags, and CPU throttling on CPUs that count it. Each line says what it means in plain words;
    hover over it for the limits, press **Details** for the readings behind it. Below are cards
    for the processor, graphics, memory, storage, operating system and motherboard with the
    numbers people look for, each with a button to its page.
  - **Each page** starts with a short explanation and the key facts as tiles, then the same panels
    as before as tabs, with the same text, colours and live updates. Every tab has Refresh, Copy,
    Guide (F1), a Live switch where the item updates by itself, and **Open in window** for the
    classic panel. **All N items** lists every item of the page, the expert ones too, and opens
    them as extra tabs (Processor also has each CPU and its caches).
  - **Search** (Ctrl+F) in the title bar: type a name such as "temperature", "SMART" or "BIOS"
    and pick an item from the list; Enter finds the text on the current tab (F3 / Shift+F3 for the
    next / previous match). Item ids work too, with an argument: `processor:3`, `volumes:/`.
  - **Menu**: Save Report, Save All Items, Copy This Tab, Options, Run as root, Find in System
    Report, Device ID Lookup, Classic View, the Classic Menu (every item and tool, as before), User
    Guide, About, Quit.
  - Keys: Alt+1 ... Alt+0, Alt+- and Alt+= jump to a page, F5 reads the page again, Ctrl+W closes
    an extra tab, Ctrl+Shift+F opens Find in System Report. Every button has a plain-English
    tooltip and an underlined Alt key.
- **Classic view** keeps the SIV-style windows exactly as they were: **Menu -> Classic View**, the
  **Classic view** button under the sidebar, `srs-gui --gui-classic`, and the menu entry's
  **Classic View** action. **Options -> Start in classic view** makes it the default (saved as
  `Classic View=1` in `SRSDEF.ini`); `--gui-new` opens the new window anyway.
- **"Is my PC OK?" uses each sensor's own temperature limits**: the CPU's maximum (Intel's own
  report, or AMD's specification for desktop Ryzen models), the graphics card's critical
  hotspot temperature, and each drive's own warning temperature. General limits only where none
  is known.
- **AppImage for any distribution, can update itself**: `srs-sysinfo-2.0.0-x86_64.AppImage` is one
  file with everything inside; make it executable and run it, nothing is installed. It runs on
  distributions with glibc 2.35 or newer (built on Ubuntu 22.04). AppImageUpdate fetches only the
  changed parts of a new release. With arguments it is the command line (`--version`, `find ...`,
  `srs-sysreport`). **Run as root** works from it: the helper script is copied to a private
  folder first, because the administrator account cannot look inside a running AppImage. The
  boot-time refresh needs the distribution package. The release workflow builds it, starts it in
  fresh Debian 12 and Fedora 44 containers, and attaches it with its `.zsync` file.

### Changed
- **About SIV** is now called **About SRS** (ID still `about`).
- **About** now starts with who made SRS, its licence and its home page: "Created by Eugene
  Flanagin", "Licensed under GPL-2.0-or-later", https://github.com/eugenesflanagin-spec/srs-sysinfo.
- The **Windows (OS) panel** is now called **General Info**, in the new window, the classic menu and
  reports. Its ID is still `windows`, so `--gui-open=windows`, `-save` reports and saved links keep working.
- Without a `Theme=` setting the new window follows the desktop's light / dark setting; the classic
  view still starts light. A saved Theme is used as before.
- In the new window the panel text is 9 pt instead of 8 pt (the classic panels keep 8 pt).
- `--gui-open=ITEM`, the tray menu's Sensors / Processors and a double-clicked device ID open the
  item on its page in the new window, and as a panel in the classic view.
- The command line, `-save` reports and their format are unchanged.
- **User guide and Section Reference updated for the new window**: a new tour of the main window
  (section 6) with screenshots of the Overview (light and dark), the Processor and Sensors pages
  and the search box; every tool, troubleshooting row and FAQ answer says where to find it in the
  new window and in the classic view; each group of the Section Reference names its page.

## 1.1.4 - 2026-10-25

### Changed
- **"Redact identifiers" now also covers copied text.** With **Options -> Redact identifiers in saved
  reports and copied text** on, the Copy button and right-click Copy / Ctrl+C mask serial numbers,
  UUIDs, MAC addresses (vendor half kept), boot / machine IDs and the host name, the same way Save
  Report does. Before, only saved reports were masked. The panels still show everything.

## 1.1.3 - 2026-10-25

### Changed
- **User guide: example screenshots and example values use a generic computer name (MYPC).**
  Seven screenshots (main window, Machine, CPUs, PCI Bus, USB Bus, Sensor Status, CPU Benchmarks)
  and two "typical values" examples (Graphics, Linux OS) now show MYPC as the computer name.

## 1.1.2 - 2026-10-25

### Added
- **User guide: "Reading the CPU IPI Delay table - a worked example"** (section 9.2.2). A real
  16 x 16 result from one Ryzen 7 7800X3D (one CCD, 100,000 rounds per pair), step by step: row =
  from, column = to, the `-` diagonal; partner threads (CPU n and n+8) are the fastest pair in each
  row at 23-25 ns, other pairs 27-34 ns through the shared L3, the farthest pairs 33-34 ns; why
  one-CCD CPUs give an even table and two-CCD CPUs typically show a second block around 70-80 ns;
  what to use it for, and that Curve Optimizer / voltage changes do not change it. Linked from the
  CPU IPI Delay item, so Guide (F1) on the IPI Delay window leads there.

## 1.1.1 - 2026-10-25

### Fixed
- **CPU Burnin / CPU Stress: GPU Frequency "Rolling Average" always showed 0 MHz.** The GPU clock was
  never added to the rolling average; the other six rows were not affected.

### Added
- **Burn-in results are saved.** Each finished CPU Burnin or CPU Stress run (Pass, Fail or Stopped,
  also when the window is closed during a run) is saved to `~/.cache/srs/burnin-YYYYMMDD-HHMMSS.txt`:
  the same text as the window's Copy button (status, sensor table, thread table). The status line
  names the file. `sysreport-search burn` saves its summary the same way and prints the path.
  One file per run, so runs can be compared (`diff`, `meld`).
- **User guide: "What hyperthreading (SMT) really does - a worked example"** (section 9.2.1). Real
  benchmark results from one Ryzen 7 7800X3D, step by step: the two threads of a core are equally
  fast, all-core runs score about 5 % lower because of boost, a second thread adds 66-80 %, mixed
  integer + floating-point work shares a core better than identical work, and why one core can score
  a little lower than the rest.

### Changed
- `--version` shows the project's home page.
- The generic Linux tarball is made from the Ubuntu 24.04 package, so it runs on glibc 2.38 and
  newer (1.1.0's needed glibc 2.42).

## 1.1.0 - 2026-10-25

### Added
- **A full user guide and section-by-section reference, also as a PDF.** The guide explains every
  window, option and command, and every one of the 545 items in plain words (what you see, normal
  and unusual values, where Linux gets it). Installed in `/usr/share/doc/srs-sysinfo/`:
  `SRS-User-Guide.pdf`, an HTML copy in `html/`, and the Markdown sources. New menu entry
  **SRS User Guide (PDF)**.
- **Guide links everywhere.** Every panel has a **Guide** button (and **F1**, and "Open guide for
  this item" in its right-click menu) that opens the guide at that item's section: in Okular,
  Evince or Papers at the right page, otherwise the HTML guide in your web browser. **Menu -> User
  Guide...** opens the start. Shift+F1 still opens the Help panel.
  - Command line: `sysreport-search guide ITEM` (`--print` shows the command instead of running it).
- **Redact identifiers in reports you share.** `sysreport-search -save ... --redact`, or **Menu ->
  Options -> Redact identifiers in saved reports** for Save Report / Save All Items. Serial
  numbers, UUIDs, WWNs, GPU and Thunderbolt unique IDs, boot and machine IDs and the host name
  become `XXXX` (same length, so columns stay lined up); MAC addresses keep their vendor half
  (`04:7c:16:XX:XX:XX`). The report header says `(identifiers redacted)`. Normal reports are
  unchanged and complete. IP addresses and Wi-Fi names are not masked.
- `--help` now lists every command: `-save`, `--list`, `guide`, `burn`, `bench`, `ipi`,
  `latency`, and srs-gui's `--gui-open` / `--gui-tray` / `SRS_OPEN`.

### Changed
- **No more notes written for the author's PC.** Several items printed sentences that were only
  true on the PC SRS was developed on. They now check your system:
  - GPU Fans names the fan-control program only if one is really running (LACT, CoolerControl,
    CoreCtrl, fancontrol), instead of always saying "owned by LACT".
  - GPU LEDs looks for LED devices the kernel exposes for your GPU, instead of naming one
    graphics card.
  - Thunderbolt lists the Thunderbolt / USB4 routers and domains the kernel found, and no longer
    names one motherboard's USB4 chip.
  - GPU I2C no longer says the SMU was hung "here".
  - Sensors: Super-I/O inputs are marked "probably not connected" when they read far above the
    board's own SYSTIN / CPUTIN temperatures, instead of from a list for one board model. A real
    probe on a temperature header is no longer greyed out.
  - Memory Latency labels blocks L1 / L2 / L3 / DRAM from your CPU's own cache sizes (they were
    fixed at 32 KB / 1 MB / 96 MB).
  - Benchmark modes `ec` / `ep` find efficiency cores on Intel hybrid CPUs, and `pc` / `pp` leave
    them out (they always said "not a hybrid processor"). The Processors page counts them too.
- `CHECKLIST.tsv` now matches the program: a new `sync_checklist.py` copies each item's root flag
  and data source from the code. DIMMs, Storage spaces, SIO debug, I/O GPIO and Fake MCHBAR no
  longer claim to need root.

### Fixed
- **SPD was empty on DDR5 PCs.** `decode-dimms` cannot read DDR5 SPD hubs and reports 0 DIMMs;
  SPD now shows the SMBIOS memory summary in that case.
- **Double-clicking a USB ID opened a PCI lookup.** USB is now recognised from the item, the
  line, `VID_xxxx PID_xxxx` pairs and HID names (`0003:046D:C52B.0001`); PCI slot addresses are
  no longer taken for IDs.
- AMD SMU / CPU SMU: the SMU firmware version was always `-` (it is in the driver's `version`
  file). Fake PM tables showed the binary table version and size as garbage. AMD PM Table now
  shows the table layout; AMD PM Table 0 keeps the values.
- Groups: with more than 256 groups the current user's groups were not found (showed `(-1)`).
- ACPI tables: the FACS table was read as if it had a standard header (revision 137, OEM `$X`);
  `acpi-tables:FACS` now lists its real fields.
- Device names: 8-character ACPI IDs were looked up as 3-letter PNP vendors (`AMDI0015` showed
  "Amdek", `ACPI000C` "Aspen Tech"); they now use ACPI vendor IDs, and unregistered PNP prefixes
  are shown as such.
- SIV setup listed only 4 of the 12 settings.
- RAID items counted a JMicron USB-NVMe enclosure as a RAID controller; SMART counted skipped
  USB drives as cached; `ntfs:<mountpoint>` was titled "NTFS" for btrfs; Volumes, Drive Geometry
  and Environment had no title line.
- Desktop PCs showed "Battery Present" / an empty "Battery %" because of a mouse or game
  controller battery; peripheral batteries no longer count.
- Network: adapters in state "unknown" without a link no longer count as up. DNS cache no longer
  calls dnsmasq a libvirt helper when `/etc/resolv.conf` uses it as the system cache. mDNS names
  (`\032`) and mount paths (`\040`) are decoded.
- CPU timer shows the real kernel tick rate (`CONFIG_HZ`) next to `USER_HZ`; Processors shows each
  CPU's NUMA node and a "Sub-ID" column header; family / model print as `0x` hex.
- Benchmarks: changing the seconds per measurement now takes effect in the same session; the GUI
  allows the same ranges as the command line (60 s per measurement, latency up to 4096 MB);
  `bench --seconds` error text matches the accepted range.
- `find --report DIR` with a missing folder names that folder; udev "priority" label no longer cut;
  "1 port" singular; file locks show full paths.

## 1.0.8 - 2026-10-24

### Added
- **Menu -> Options -> Refresh root data at boot.** Root-only panels (SMBIOS serials, DIMM SPD,
  SMART, ACPI tables, MSRs, full PCI config) are filled at every boot without a password prompt.
  - New systemd unit `srs-root@.service` (in `/usr/lib/systemd/system`). Enabled per user as
    `srs-root@<user>.service`, it runs the existing read-only `srs-root` collector once per boot
    as root, at Nice 19 and idle I/O priority, and hands `~/.cache/srs/root` to that user.
  - Ticking the option asks for your password once (pkexec) and collects straight away;
    unticking disables the unit. A cancelled prompt leaves the tick as it was.
  - SRS itself keeps running as your user; only the collector runs as root.
- `srs-root --user NAME` (root only) collects for that user; the boot unit uses it.

## 1.0.7 - 2026-10-24

### Fixed
- **No more flicker on live updates** (Sensors, the main window, clocks, CPU Burnin / Stress…).
  Every refresh used to clear the whole text view and rebuild it, so GTK re-laid out and repainted
  everything and the scroll position had to be put back afterwards.
  - Each refresh is now drawn into an off-screen copy that shares the window's styles.
  - Just before GTK paints, only the lines whose text changed are replaced (text and colours);
    unchanged lines are never touched, and an unchanged panel isn't redrawn at all.
  - The scroll position no longer jumps, since nothing above it is rebuilt.
  - Less work per refresh: with the Sensors panel open, SRS went from about 4% to 2% CPU.
  - One-off output (Find, Device ID Lookup, benchmark results) still draws directly.

### Added
- **Menu -> Options -> Update interval**: Default (each panel's own rate), 0.5 / 1 / 2 / 3 / 5 /
  10 seconds, or Paused (F5 refreshes by hand). Applies to every open window at once; saved as
  `Update Interval=` in SRSDEF.ini.

## 1.0.6 - 2026-10-16

### Fixed
- **Memory Latency: small blocks no longer show false spikes.** 1.0.5 timed each block size once,
  for about 2 ms at L1 sizes, so a single interrupt or clock change could make a block read
  several times too slow (e.g. 8 KB = 4.54 ns on a 7800X3D whose L1 is ~1 ns).
  - Each block size is now timed 5 times and the fastest pass is kept. Interruptions only ever
    make a pass slower, so the fastest pass is the true latency.
  - A ~200 ms warm-up runs first, so the CPU is at full clock before the first block.
  - Each block is walked once in full before timing, so the cache and TLB are filled for every
    size, not just the first 10,000 lines.
  - Result on a 7800X3D, 3 runs each: L1 4-32 KB 0.95-1.09 ns (was 1.02-1.27 with outliers);
    64 MB, which still fits in the 96 MB L3, 11.1-11.6 ns (was 13.8-19.2).
- **aarch64 build:** `burner.c` included `<errno.h>` and `<fcntl.h>` only on x86, so the
  GPU Load command code added in 1.0.5 failed to compile on aarch64.
- Help and error text now name `srs-sysreport` instead of the old `sysreport.sh`.

## 1.0.5 - 2026-10-15

### Added
- **CPU Burnin / Stress: GPU Load command diagnostics** - a wrong or misspelled command no longer
  fails silently.
  - Before starting, SRS checks the command's program exists (PATH or full path). If not, an
    error dialog names it and nothing is started.
  - New "GPU Load" status line while running: running (pid), finished (exit 0) after N s - the
    CPU run continues, failed (exit N), not found (exit 127), not executable (exit 126), killed by
    signal N, or stopped with the CPU load. Failures show in red.
  - The command's own output (stdout + stderr) is saved to `~/.cache/srs/gpu-load.log`, so its
    error messages are kept.

## 1.0.4 - 2026-10-15

### Added
- **Dark mode.** Menu -> Options -> Theme (also in the tray's SRS Menu):
  - *Light (SIV classic)* - default, unchanged SIV look.
  - *Dark* - dark panels, light text, SIV's blue / purple / teal / orange / green / red palette
    brightened for a dark background; dark buttons, inputs, menus (Adwaita dark) and graphs
    (Burnin / Stress history, Latency curve).
  - *Follow desktop* - uses the desktop colour scheme from xdg-desktop-portal
    (`org.freedesktop.appearance color-scheme`; KDE Plasma and GNOME set it) and switches live when
    the desktop scheme changes. Falls back to `GTK_THEME` / `~/.config/gtk-3.0/settings.ini`.
- Switching applies immediately to every open window; saved as `Theme=Light|Dark|System` in
  `~/.config/srs/SRSDEF.ini`. Find highlights keep dark text in dark mode so they stay readable.

### Changed
- srs-gui no longer forces `GTK_THEME=Adwaita:light`; the Theme option drives Adwaita light / dark
  for the SRS process itself (a dark `GTK_THEME` such as adw-gtk3-dark no longer half-themes it).

## 1.0.3 - 2026-10-15

### Added
- **System tray icon, like SIV's Icon Tray** (StatusNotifierItem via `libayatana-appindicator`,
  loaded at run time - optional; KDE Plasma, XFCE, GNOME with the AppIndicator extension).
  - Live tooltip: CPU temperature and load, GPU temperature and load (every 2 s).
  - Tray menu: Show SRS / Hide to Tray, Sensors, Processors, CPU Burnin, CPU Stress, Benchmark,
    Find, the full SRS Menu, Exit. Middle-click toggles show / hide.
- **Menu -> Options** (also in the tray's SRS Menu), saved in `~/.config/srs/SRSDEF.ini`:
  - System tray icon (default on)
  - Close button hides to tray (default off)
  - Minimize hides to tray (X11 / XWayland - native Wayland apps are not told when they are minimised)
  - Start hidden in tray
  - Start with the desktop - writes `~/.config/autostart/srs-gui.desktop` running `srs-gui --gui-tray`
  - Hide to Tray
- `srs-gui --gui-tray` starts hidden in the tray.
- Safety: SRS never hides its windows unless a tray icon actually exists, so it cannot get lost.

## 1.0.2 - 2026-10-15

### Changed
- Running as root (`sudo sysreport-search -save`) uses the invoking user's root-data cache
  (`SUDO_USER` / `PKEXEC_UID`), or collects one first with `srs-root` if none exists.
- `srs-root` skips SMART on USB-attached disks (set `SRS_SMART_USB=1` to include them) - SMART
  pass-through is meaningless on USB sticks and can upset flaky USB enclosures.

### Fixed
- SMBIOS structure count with dmidecode 3.6+ (it no longer prints the summary line).

## 1.0.1 - 2026-10-15

### Added
- Menu entries: **SRS CPU Burnin & Stress**, **SRS Benchmark**, **SRS Full System Report**, and
  jump-list actions on **SRS System Information** (Sensors, Processors, CPU Burnin, CPU Stress,
  Benchmark, Device ID Lookup, Find).
- `srs-gui --gui-open=ITEM[,ITEM...]` opens panels / tools at start.

### Fixed
- Clean builds under Arch's hardening flags (`_FORTIFY_SOURCE=3`, LTO): `realpath` buffers sized to
  `PATH_MAX`; uninitialised-pointer warnings.

## 1.0.0 - 2026-10-15

First release.
- All **538** SIV64X 5.87 report items (364 with Linux data, 136 "not present", 38 Windows-only
  with Linux equivalents) - see `CHECKLIST.tsv`.
- GTK3 GUI laid out like SIV: main window and button row, every panel's sub-buttons, Menu.
- SIV-format `-save` reports; device-ID lookup (Tree / Scan / Info); regex Find over system reports.
- CPU Burnin / CPU Stress, Dhrystone 2.1 / Whetstone benchmarks in SIV's modes, IPI delay and
  memory latency tools.
- `srs-root` helper for root-only data (dmidecode, SPD, SMART, ACPI tables, MSRs, PCI config space).
