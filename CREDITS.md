# Credits

SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under the GNU General Public License v2.0 or later (GPL-2.0-or-later).

SIV - System Information Viewer - is the work of Ray Hinchliffe, RH Software (<https://rh-software.com>).
SRS is an independent project, not affiliated with SIV or its author (see [README.md](README.md)).

## Libraries

| What | Used for | Licence |
|---|---|---|
| GTK 3, GLib, Pango, cairo | the GUI | LGPL-2.1-or-later (cairo: LGPL-2.1 / MPL-1.1) |
| SQLite | the Power & Cost history (`srs-gui` only) | public domain |
| libcurl (<https://curl.se/libcurl/>) | the Network Benchmark's HTTPS and speed tests; the system's own library, loaded at run time when installed, not shipped or linked; no code copied | curl licence (MIT-style) |
| zlib (or zlib-ng in zlib mode), Zstandard, LZ4, XZ Utils liblzma | Everyday Work's compression rows; the system's own libraries, loaded at run time when installed, not shipped or linked | zlib licence; BSD-3-Clause / GPL-2.0; BSD-2-Clause (library); 0BSD / public domain |
| Vulkan loader (`libvulkan.so.1`) and your GPU's Vulkan driver | the GPU Benchmarks; the system's own, loaded at run time when installed, not shipped or linked | Apache-2.0 (loader); the driver's own |
| Khronos Vulkan-Headers (<https://github.com/KhronosGroup/Vulkan-Headers>) | the layouts of the few Vulkan structures copied field for field into `gpu_vk.h` (prefix `vki_`), so SRS needs no Vulkan header to build; checked against the real header by `tests/bench-gpu-test.sh` | Apache-2.0 OR MIT |
| ROCm / HIP (`libamdhip64`, hipRTC, hipBLAS), CUDA (`libcuda`, NVRTC, cuBLAS), an OpenCL loader and drivers | the ROCm / HIP, CUDA and OpenCL back ends of GPU Burnin and the GPU Compute comparison; the system's own, loaded at run time when installed, not shipped or linked; no code copied | MIT (ROCm); NVIDIA's licence (CUDA); the loader's and drivers' own |
| CLBlast by Cedric Nugteren (<https://github.com/CNugteren/CLBlast>) | the "vendor library" variant of the OpenCL matrix multiply; loaded at run time (`libclblast`) when installed, not bundled | Apache-2.0 |
| LLVM / clang (<https://llvm.org>) | compiles SRS's own CUDA kernels (`gpu_kernels.h`) to PTX at development time (`tools/gen-cuda-ptx.sh`); the generated PTX text in `gpu_kernels_ptx.inc` is compiler output of SRS's own source, built in so CUDA needs no runtime compiler; clang is not needed to build SRS and not shipped | Apache-2.0 WITH LLVM-exception |
| NVIDIA CUDA toolkit (NVRTC) | optional: the runtime compiler fallback for the CUDA kernels; installed only when the user chooses (Options > NVIDIA CUDA toolkit), never shipped | NVIDIA CUDA Toolkit EULA |
| Mesa lavapipe (<https://www.mesa3d.org>) | the software Vulkan renderer the GPU tests run on, so they never touch a real GPU (test only, not shipped) | MIT |
| hwdata (`pci.ids`, `usb.ids`, `pnp.ids`) | device names (not part of SRS) | GPL-2.0-or-later / BSD-3-Clause |

## Memory Timings, hardware lists and ACPI (2.4)

SRS's code for these is its own. The register layouts below are facts taken from the projects named;
no code was copied from them.

| What | Used for | Licence / terms |
|---|---|---|
| ZenStates-Core / ZenTimings by Ivan Rusanov (<https://github.com/irusanov/ZenStates-Core>, <https://github.com/irusanov/ZenTimings>) | the AMD memory controller (UMC) register map, the PM table offsets of the memory clocks and SoC voltages, the DDR5 SPD and the BIOS AOD table layouts and value tables of the Memory Timings page | facts only, credited; ZenStates-Core is GPL-3.0, no code copied |
| memtest86+ by Sam Demeulemeester and contributors (<https://github.com/memtest86plus/memtest86plus>) | the AMD and Intel memory controller timing register layouts (Intel: MCHBAR, Core 6th gen to Core Ultra), a cross-check of the AMD ones | facts only, credited; memtest86+ is GPL-2.0, no code copied |
| ryzen_smu driver (leogx9r; the amkillam fork, <https://github.com/amkillam/ryzen_smu>) | SRS reads the AMD PM table and, through srs-root, the memory controller registers through this kernel driver (AUR `ryzen_smu-dkms-git`); not shipped | GPL-2.0 |
| acpi_call kernel module (<https://github.com/nix-community/acpi_call>) | Tools > ACPI method evaluation evaluates methods through it (Arch `acpi_call-dkms`); not shipped | GPL-3.0-or-later |
| JEDEC JESD301-2 (DDR5 PMIC) and the DDR5 SPD specification | the PMIC voltage codes and the SPD fields | published standards, credited |
| The PCI ID Repository (<https://pci-ids.ucw.cz>) | `pci.ids`, which Update hardware lists can download | GPL-2.0-or-later or BSD-3-Clause |
| The Linux USB ID list (<http://www.linux-usb.org/usb.ids>) | `usb.ids`, which Update hardware lists can download | the list's own terms; distributed with hwdata (GPL-2.0-or-later / BSD-3-Clause) |
| hwdata (<https://github.com/vcrhonek/hwdata>) | `pnp.ids` (PnP / monitor maker IDs, from the UEFI PNP ID list), which Update hardware lists can download | GPL-2.0-or-later (hwdata) |

## Data in the Power & Cost page

SRS shows these figures unchanged, apart from unit conversion (cents to currency units) and
reformatting. The organisations below are not responsible for SRS's use of their data. The tables
are made by `tools/gen_power_rates.py` from the files listed in that script; the dates are those of
the copies in SRS 2.3.0.

| Data | Source | Licence / terms |
|---|---|---|
| US average residential electricity price by state (`power_rates_data.inc`) | U.S. Energy Information Administration, *Electric Power Monthly*, Table 5.6.B, year to date through August 2026: <https://www.eia.gov/electricity/monthly/xls/table_5_06_b.xlsx> | U.S. government work, public domain; source credited as EIA asks |
| US electric utility by ZIP code, with its average residential price (`data/power-zip-rates.tsv`) | National Renewable Energy Laboratory (NREL), *U.S. Electric Utility Companies and Rates: Look-up by Zip Code (2024)*, from EIA Form EIA-861 data: <https://catalog.data.gov/dataset/u-s-electric-utility-companies-and-rates-look-up-by-zip-code-2024> (files at <https://data.openei.org/files/8563/>) | Creative Commons Attribution 4.0 (CC BY 4.0); reformatted by SRS (one line per utility and per ZIP code) |
| European household electricity prices by country | Eurostat, dataset `nrg_pc_204` (households, 2 500-4 999 kWh a year, all taxes and levies), 2026-S1: <https://ec.europa.eu/eurostat/databrowser/view/nrg_pc_204/default/table> | Reuse authorised with the source acknowledged (<https://ec.europa.eu/eurostat/web/main/help/copyright-notice>). Eurostat is not responsible for SRS's use of the data. |
| Great Britain energy price cap (GB average, Direct Debit), 1 October to 31 December 2026 | Ofgem: <https://www.ofgem.gov.uk/your-energy-supply/your-energy-bill/energy-price-cap-unit-rates-and-standing-charges> | Published figures, credited to Ofgem |
| Grid CO2 intensity by country (gCO2e/kWh) | Ember, *Yearly electricity data*: <https://ember-energy.org/data/yearly-electricity-data/> | Creative Commons Attribution 4.0 (CC BY 4.0) |
| 80 PLUS efficiency levels (PSU presets) | Wikipedia, "80 Plus" (<https://en.wikipedia.org/wiki/80_Plus>), citing CLEAResult, the certifier (<https://www.clearesult.com/80plus/program-details>) | Facts, credited |
| Montech CENTURY II 1200W preset | HWbusters review (<https://hwbusters.com/psus/montech-century-ii-1200w-atx-v3-1-psu-review/7/>, <https://hwbusters.com/psus/montech-century-ii-1200w-atx-v3-1-psu-review/11/>): Cybenetics Platinum average efficiency, under 70 % at 2 % load; PCPartPicker: 80 PLUS Gold certified | Facts, credited; the curve is derived, not copied |
| Typical part figures (fans, drives, memory, chipset) | Samsung 980 PRO data sheet, Noctua NF-A12x25 specification, reseller spec sheets for the Seagate ST4000NM0033, published AMD Promontory 21 figures | Facts, credited here |

## Benchmarks

The benchmarks are SRS's own code, written from published sources.

| What | Source | Licence / terms |
|---|---|---|
| SRS Pi | the Chudnovsky series (D. V. and G. V. Chudnovsky, 1988) and the Gauss-Legendre / Brent-Salamin AGM (1976); reference checksums computed once with [mpmath](https://mpmath.org) | published methods; mpmath (BSD licence) is not part of SRS |
| Crypto | FIPS 197 (AES) and FIPS 180-4 (SHA-256), with their published test vectors | U.S. government standards |
| Prime Sieve | the sieve of Eratosthenes; known prime counts pi(10^n) | published facts |
| Cache & Memory | the measuring engine of Linux System Audit (LSA) 0.2.0, the author's own project (GPL-2.0-or-later), now part of SRS | GPL-2.0-or-later |
| CPU Compute | matrix multiply: the register-blocked micro-kernel described in K. Goto and R. van de Geijn, "Anatomy of High-Performance Matrix Multiplication" (2008), as used in GotoBLAS / BLIS; FFT: iterative radix-2 Cooley-Tukey (J. W. Cooley and J. W. Tukey, 1965), with the usual 5 N log2 N operation count (as used by benchFFT); N-body: direct-sum gravity with Plummer softening (H. C. Plummer, 1911; S. Aarseth); Mandelbrot: the escape-time algorithm (B. Mandelbrot, 1980); ray tracer: Whitted-style recursive ray tracing (T. Whitted, 1980); vector code: the Intel Intrinsics Guide | published methods; all code written for SRS, none copied |
| GPU Benchmark | the Vulkan 1.1 specification and SPIR-V (Khronos, <https://registry.khronos.org/vulkan/>) for the compute pipelines and timestamp queries; peak multiply-add by long chains of fused multiply-adds per thread, memory bandwidth by large streaming reads / writes / copies, both widely published measuring methods | public specification; all code and shaders written for SRS, none copied |
| GPU Compute comparison | the back ends' own public documentation (Vulkan, HIP, CUDA, OpenCL) and the vendor libraries' public APIs (hipBLAS, cuBLAS, CLBlast), loaded at run time when installed | public documentation; no code copied |
| GPU Burnin | gpu-burn by Ville Timonen (<https://github.com/wilicc/gpu-burn>, BSD-2-Clause, "Copyright (c) 2020, Ville Timonen", and its ROCm / HIP port gpu-burn-rocm): the idea of a GPU burn-in by repeated matrix multiplications whose results are compared with a reference, errors counted | inspiration only; all code written for SRS, none copied |
| Network | Cloudflare speed test (<https://speed.cloudflare.com>): the public endpoints `GET /__down?bytes=N` and `POST /__up` are the default server - a public service run by Cloudflare, Inc. (the endpoints its own speed test page uses), not a documented API; SRS is not affiliated with Cloudflare and uses no Cloudflare code | public service, used as is |
| Everyday Work | xxHash64: the xxHash design by Yann Collet (BSD-2-Clause reference, <https://github.com/Cyan4973/xxHash>), written for SRS from the published specification and checked against its known values; CRC32 (IEEE 802.3, polynomial 0xEDB88320) and CRC32C (Castagnoli, 0x82F63B78), slicing-by-8 after M. Kounavis and F. Berry (Intel); FNV-1a (G. Fowler, L. C. Noll, K.-P. Vo); Boyer-Moore-Horspool (R. N. Horspool, 1980); multikey quicksort (J. Bentley and R. Sedgewick, 1997); LSD radix sort (textbook) | published methods; all code written for SRS, none copied |
| OS Latency | Linux and POSIX interfaces as documented in the Linux man-pages project (<https://man7.org/linux/man-pages/>) and the kernel documentation (<https://docs.kernel.org/>: sched_ext, transparent huge pages, clock sources); pipe ping-pong timing of context switches, a widely published method | public documentation; no code copied |
