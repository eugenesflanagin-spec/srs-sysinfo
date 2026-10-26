# Credits

SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under the GNU General Public License v2.0 or later (GPL-2.0-or-later).

SIV - System Information Viewer - is the work of Ray Hinchliffe, RH Software (<https://rh-software.com>).
SRS is an independent project, not affiliated with SIV or its author (see [README.md](README.md)).

## Libraries

| What | Used for | Licence |
|---|---|---|
| GTK 3, GLib, Pango, cairo | the GUI | LGPL-2.1-or-later (cairo: LGPL-2.1 / MPL-1.1) |
| SQLite | the Power & cost history (`srs-gui` only) | public domain |
| hwdata (`pci.ids`, `usb.ids`, `pnp.ids`) | device names (not part of SRS) | GPL-2.0-or-later / BSD-3-Clause |

## Data in the Power & cost page

SRS shows these figures unchanged, apart from unit conversion (cents to currency units) and
reformatting. The organisations below are not responsible for SRS's use of their data. The tables
are made by `tools/gen_power_rates.py` from the files listed in that script; the dates are those of
the copies in SRS 2.2.0.

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
| OS Latency | Linux and POSIX interfaces as documented in the Linux man-pages project (<https://man7.org/linux/man-pages/>) and the kernel documentation (<https://docs.kernel.org/>: sched_ext, transparent huge pages, clock sources); pipe ping-pong timing of context switches, a widely published method | public documentation; no code copied |
