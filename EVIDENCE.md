# How the SIV64X specification was gathered (clean-room evidence)

SIV (System Information Viewer) is freeware by Ray Hinchliffe, RH Software (https://rh-software.com).
SRS is an independent project and is not affiliated with RH Software. No SIV code was used, disassembled or decompiled.

## Original search-feature evidence

Source: the SIV64X 5.87 distribution (freeware, rh-software.com). Gathered 2026-10-15 by black-box means only:
the shipped data files, the batch files, and printable strings in `SIV64X.exe`
(`strings -n3` + `strings -n3 -el`, line numbers refer to that combined dump).
No disassembly/decompilation (SIV is closed freeware). Menu resources are not stored
as RT_MENU (`wrestool -x --type=menu` returns nothing) — SIV builds its menus at runtime
from the strings below.

SIV has **three separate search mechanisms**.

## 1. Device-ID lookup (`pcidevs.txt`, `usbdevs.txt`, `mondevs.txt`, `pnpdevs.txt`, …)

**Data format** — flat `KEY=Name` text, one header line, `;` comments:

```
= Date 2026-05-13 Version 273 Records 183,259        (pcidevs.txt line 1)
1002=ATI Technologies                                 line 1993
1002:7550=Radeon RX 9070 XT                           line 20246
1002:7550:C0==                                        line 20247   ("=" value = inherit parent name)
1002:7550:C2=Radeon RX 9070 GRE                       line 20248   (revision-specific name)
1002:7550:1025:1878:C0==                              line 20250
```

Key shapes counted across the file (hex digits → `h`):

| file | records | dominant key shapes |
|---|---|---|
| pcidevs.txt | 183,259 | `V:D:S:s` 122,773 · `V:D:S:s:RR` 29,450 · `V:D` 25,893 · `V` 3,328 · `V:D:RR` 1,815 |
| usbdevs.txt | 71,439 | `V:P` 45,443 · `V:P:RR` 19,132 · `V` 4,273 |
| mondevs.txt | 25,650 | EDID PnP IDs (`GSMhhhh`, …) |
| pnpdevs.txt | 9,019 | ACPI/PnP IDs — 0 out-of-order |

**Sorted for binary search.** Only 164 of 183,259 adjacent pairs in pcidevs.txt are
out of plain string order, and every one is the same pattern — the revision-only key
sorts before the subsystem keys of the same device:

```
1002:1114:E5  >  1002:1114:1019:A10B:C2      (lines 2003/2004)
0E11:A0F8:11  >  0E11:A0F8:0E11:A0F8         (lines 425/426)
```

i.e. a custom comparator, not corruption. The binary imports both halves of a
sort-then-binary-search design:

```
177101: qsort
177119: bsearch
```

**Fallback chain + result reporting** strings (around line 198872):

```
UNK%=Unknown Vendor
UNK%:0000=Unknown Device
:DID:OID:SID:RID          <- key segment order: device, OEM(subsys vendor), subsys ID, revision
:PID:RID:MID              <- USB: product, revision, ...
%s Matches
No Matches for %s
No Context for %s
   Hits
Matches	Vendor	Stock %s%s%s	Resolved %s%s%s
```

→ Lookup = most specific key first, falling back toward vendor; it reports what
matched, the stock (device-level) name and the resolved (most specific) name.

## 2. Text find (Software / Registry / BIOS-ID find, highlight)

```
181090: S/W &Find                       (menu item)
181961: &Find Port
210800: RegFind	[<]%-128s
221602: %s find in %s
221603: Searching %s for "%s"%s
221604: Failed to Crrate Worker Thread   (sic — find runs on a worker thread)
221605: Match String [%s] is Invalid     (pattern is validated before searching)
221606: Invalid Call - No worker Thread
221608: BID-FIND
221609: BIOS ID Find from %s
213956: &%s Matches                      (menu shows match count)
212567: %s	[Highlight Matches]	%s%s   (hot-key action)
220401: Colour Find Text                 (configurable highlight colour)
```

## 3. Port-name lookup (Ports panel, `etc\services`) — tri-state mode

```
246133:  Checked      	Tree Lookup mode	Do bsearch() of a %s B-Tree
         Indeterminate	Scan Lookup mode	Scan the %s file
         Unchecked    	Info Lookup mode	Use Windows GetNameInfo() API
208936: %s used %u thread%s, %s mode, found %u Port Names out of %s checked after %.3f seconds ...
```

The same three strategies (sorted bsearch / linear file scan / ask the OS) are what
`sysreport-search id --mode tree|scan|info` re-creates, over open data.

## Clean-room rebuild — `~/.local/bin/sysreport-search`

Now native C (`~/.local/src/sysreport-search/`): `sysreport-search` CLI and the `srs-gui` GTK3 GUI on
Linux, plus `SRS64X.exe` / `SRS32X.exe` Win32 images. The CLI output is byte-identical to the original
Python version (41/41 parity cases).

| SIV | sysreport-search / srs-gui |
|---|---|
| `pcidevs.txt`/`usbdevs.txt` (SIV's own DB — not reused) | `/usr/share/hwdata/pci.ids`, `usb.ids` flattened to `V`, `V:D`, `V:D:S:s` keys |
| qsort at load + bsearch | `qsort` at load + `bsearch` (tree mode) - the same libc pair SIV imports |
| Scan Lookup mode | `--mode scan` (linear re-read of the .ids file) |
| Info Lookup (OS API) | `--mode info` → `systemd-hwdb query pci:v…d…sv…sd…` |
| `:DID:OID:SID:RID` fallback | `V:D:S:s:RR → V:D:S:s → V:D:RR → V:D → V` |
| Matches / Vendor / Stock / Resolved | `devices` table: Matched / Vendor / Resolved + `[subsystem: …]` |
| Searching %s for "%s" / Match String Invalid / No Matches / Highlight | `find PATTERN` over the newest `~/sysreports/sysreport-*` |

Measured on this box (RX 9070 XT, XFX 1eae:8811 — not in pci.ids, so it falls back):
tree lookup 0.016–0.058 ms vs scan 29.6 ms vs hwdb 1.65 ms; 42,228 PCI records load in ~16 ms.
(Native C: tree 0.003 ms, scan ~2–26 ms, hwdb ~3–5 ms.)

## How SIV64X is built (header metadata only)

From `objdump -p`, the PE Rich header, and SIV's own Lookup panel (which prints
`SIV64X.exe  V5.87 Released 14-May-2026  WDK 6001.18000  V14.00 ... x64 APP 6.00 5.02`):

| Field | SIV64X.exe | SIV32X.exe | SRS64X.exe | SRS32X.exe |
|---|---|---|---|---|
| Format | PE32+ x64 | PE32 x86 | PE32+ x64 | PE32 x86 |
| Toolchain | WDK 6001.18000, cl 14.00 (Rich: build 50727 / 40310) | same | mingw-w64 GCC 16 | same |
| Language | C (64 C objects, 16 C++) + Win32 API | same | C + Win32 API | same |
| C runtime | system `msvcrt.dll` | `msvcrt.dll` | `msvcrt.dll` (`-mcrtdll=msvcrt-os`) | `msvcrt.dll` |
| Subsystem | GUI, OS 6.00, subsys 5.02 | GUI, subsys 5.00 | GUI, OS 6.00, subsys 5.02 | GUI, subsys 5.00 |
| ImageBase | 0x1000000 | 0x01000000 | 0x1000000 | 0x01000000 |
| Stack reserve / commit | 0x1800000 / 0x200000 | 0x1800000 / 0x200000 | same | same |
| DllCharacteristics | 0x8040 | 0x8140 | 0x8040 | 0x8140 |
| Images per source | 8 (32A/L/N/O/P/X, 64I/X) | | 5 (Linux x86-64/i686/aarch64 + Win x64/x86) | |

Not reproduced: `.rdata` merged into `.text` (GNU ld), the WDK compiler itself.

The **native Linux GUI** (`srs-gui`) is the same feature set on GTK3 with Linux equivalents
for the Windows-only APIs: sysfs for SetupAPI, systemd-hwdb for the OS name database,
`~/.config/srs/SRSDEF.ini` for the exe-side ini, GThread for the find worker thread.

## SIV's UI, observed under wine (screenshots, no code inspection)

A main text panel with coloured label/value columns (Tahoma, #F5F5F5 background) and a row of
buttons along the bottom (OK, Copy, Windows, Machine, Sensors, USB Bus, Processors, Volumes,
Adapters, PCI Bus, About, Help). Each button opens a separate panel window with its own button
row. Help -> Lookup lists every data file (Description / File Specification / Current
Identification / Code Page / Latest WEB Update / WEB Download Location). SRS keeps that
layout: main panel + PCI Bus / USB Bus / ALL Dev / Lookup / Find / Data Files / About / Help.

## How the full item list was derived

- **Item names (538):** the `[item]` tokens in SIV64X.exe's printable string table (`strings`), e.g. `[cpuid]`, `[smart]`, `[usb-bus]`.
- **Layouts and wording:** SIV64X's own `-save` text dump (its documented command-line option), which writes every default item as `[item]  [category]` followed by its content.
- **Panels and button rows:** screenshots of SIV64X's windows, taken while running it.
- **Classification:** each item was then mapped to a Linux data source, or marked "not present" (hardware SIV supports that a typical PC lacks) or "Windows-only" with the nearest Linux equivalent, in `CHECKLIST.tsv`.

No part of SIV's executable code was disassembled, decompiled or reused, and none of SIV's data files (`PCIDEVS.txt`, `USBDEVS.txt`, ...) are included. Device names come from the hwdata project instead.
