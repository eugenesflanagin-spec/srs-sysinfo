# Linux (SIV's Windows items)  (SRS Section Reference)

[Back to the section index](README.md) - [Glossary](../GLOSSARY.md)

This group holds the items that SIV64X (System Information Viewer, by Ray Hinchliffe, RH Software) files under "Windows": the operating system itself, its services, drivers, users, memory, time, fonts, languages and crash handling. SRS (srs-sysinfo) is an independent Linux program. It is not affiliated with or endorsed by RH Software and contains no SIV code or data. Most Windows ideas have a close Linux twin, so each item shows that twin: services become systemd units, drivers become kernel modules, the registry becomes the files in `/etc` and `~/.config`, and Windows Error Reporting (WER) becomes `coredumpctl`. Open an item with **Menu -> Linux (SIV Windows items) -> item**, or press the **Linux** button in the main window (SIV's "Windows" button). That button opens the overview panel; its sub-buttons are Machine, Sensors, USB Bus, Processors, Volumes, PCI Bus, ALL Dev and Graphics. Only **Open server files** needs root data from the srs-root helper (Menu -> Run as root) to be complete; **Sessions** and **Product IDs** each have one line that also fills in only after srs-root has run.

**How to read the saved text.** In a saved report (`sysreport-search '-save=[id]'`), `[=]` is a ticked box and `[_]` is an empty box. The GUI draws them as real check boxes. Some items take an argument after a colon, such as `driver-vsn:amdgpu`. You can type that form in the GUI or save it from the command line, for example `sysreport-search '-save=[signing:amdgpu]'`.

**"This process" items.** Several items (Environment, the DLL items, Process heap, Memory heap, Memory mapping, Virtual memory, Privileges, Handles, and part of Scheduling) describe SRS's own running process. SIV does the same thing on Windows. They show what any normal program on your desktop gets: its libraries, memory, limits and rights.

## Contents
- [ASUS control service](#asus-control-service) `acs-srv`
- [Boot mode options](#boot-mode-options) `boot-mode`
- [Corsair Link 4 service](#corsair-link-4-service) `cl4-srv`
- [Shell CLSIDs](#shell-clsids) `clsid`
- [Code pages](#code-pages) `codepage`
- [TAPI country list](#tapi-country-list) `country`
- [Corsair service](#corsair-service) `cue-srv`
- [Base DLLs](#base-dlls) `dll`
- [DLLs by address](#dlls-by-address) `dll-addr`
- [Known DLLs](#known-dlls) `dll-known`
- [DLLs by name](#dlls-by-name) `dll-name`
- [Drivers](#drivers) `drivers`
- [Driver versions](#driver-versions) `driver-vsn`
- [E-mail accounts](#e-mail-accounts) `e-mail`
- [Environment](#environment) `environment`
- [ESU status](#esu-status) `esu-sts`
- [System file cache](#system-file-cache) `file-cache`
- [Open server files](#open-server-files) `files`
- [Feature management overrides](#feature-management-overrides) `fmo-sts`
- [Folder paths](#folder-paths) `folders`
- [Font families](#font-families) `fonts`
- [Groups](#groups) `groups`
- [Handles](#handles) `handle`
- [Process heap](#process-heap) `heap`
- [Registry hives](#registry-hives) `hives`
- [Code integrity](#code-integrity) `integrity`
- [Key names](#key-names) `key-names`
- [Locales](#locales) `locales`
- [Memory heap](#memory-heap) `mem-heap`
- [Memory mapping](#memory-mapping) `mem-map`
- [System metrics](#system-metrics) `metrics`
- [MLang conversions](#mlang-conversions) `mlang`
- [DOS device names](#dos-device-names) `ms-dos`
- [N2K service](#n2k-service) `n2k-srv`
- [NLS code pages](#nls-code-pages) `nls-info`
- [Paging files](#paging-files) `paging`
- [Credentials](#credentials) `passwords`
- [Pending file renames](#pending-file-renames) `pending`
- [ProLiant monitor service](#proliant-monitor-service) `plm-srv`
- [Windows policies](#windows-policies) `policies`
- [Memory pools](#memory-pools) `pools`
- [Power policy](#power-policy) `power-policy`
- [Power scheme](#power-scheme) `power-scheme`
- [Privileges](#privileges) `priv`
- [Processes](#processes) `processes`
- [Product IDs](#product-ids) `product-ids`
- [Product keys](#product-keys) `products`
- [Font ranges](#font-ranges) `ranges`
- [RDP session info](#rdp-session-info) `rdp-info`
- [Remote registry paths](#remote-registry-paths) `reg-path`
- [Remote registry service](#remote-registry-service) `reg-srv`
- [Text converter render format](#text-converter-render-format) `render-format`
- [Intel RST service](#intel-rst-service) `rst-srv`
- [Scheduling](#scheduling) `scheduling`
- [Driver services](#driver-services) `services-drv`
- [Win32 services](#win32-services) `services-w32`
- [Sessions](#sessions) `sessions`
- [Setup directories](#setup-directories) `setup`
- [Setup API logging](#setup-api-logging) `setup-api`
- [Driver signing](#driver-signing) `signing`
- [Installed software](#installed-software) `software`
- [Startup programs](#startup-programs) `startup`
- [System](#system) `system`
- [System timers](#system-timers) `sys-tim`
- [System windows](#system-windows) `sys-win`
- [Thread windows](#thread-windows) `tid-win`
- [Time](#time) `time`
- [Times](#times) `times`
- [Users](#users) `users`
- [Driver verifier](#driver-verifier) `verifier`
- [Virtual memory](#virtual-memory) `virtual`
- [Visual Studio paths](#visual-studio-paths) `vs-paths`
- [Dr Watson / crash debugger](#dr-watson--crash-debugger) `watson`
- [Error reporting](#error-reporting) `wer`
- [Windows CE devices](#windows-ce-devices) `win-ce`
- [Windows (OS) panel](#windows-os-panel) `windows`
- [WinSAT](#winsat) `winsat`
- [Time zones](#time-zones) `zones`

---
## ASUS control service  
`acs-srv` - Not present on most PCs

**In plain words.** On Windows, ASUS laptops and some desktops run an "ASUS control service" (Armoury Crate and friends). Linux has no such service. SRS shows the nearest Linux pieces instead.

**What you see.**
- `Windows service` - always "not applicable on Linux - showing the Linux analogue".
- Unit lines `asusd.service`, `supergfxd.service` - two columns: Active state (`active`, `inactive`, `failed`) and Enabled state (`enabled`, `disabled`, `masked`, `static`, or `not-found` / "not installed"). `asusd` is the asusctl daemon for ASUS ROG/TUF laptops (fan curves, keyboard light, charge limit). `supergfxd` switches laptop graphics modes.
- Module lines `asus_wmi`, `asus_nb_wmi`, `asus_ec_sensors`, `asus_wmi_sensors`, `asus_armoury` - "loaded" or "not loaded". These are kernel drivers that talk to ASUS firmware: hot keys, fan and temperature sensors, BIOS settings.

**Healthy / typical values.** On the author's ASUS TUF X870 desktop board every module is "not loaded" and `asusd` is `inactive / masked`. That is normal for a desktop. On an ASUS laptop you would expect `asus_nb_wmi` loaded and, if you installed asusctl, `asusd` active.

**Unusual values and what they suggest.** An ASUS laptop with `asus_nb_wmi` "not loaded": Fn hot keys and fan modes will not work; check that the kernel was built with the driver. `asus_ec_sensors` not loaded on a supported ROG/TUF desktop board: board sensors (chipset, VRM temperature) will be missing from the Sensors items.

**Where Linux gets it.** `systemctl is-active` / `is-enabled` for each unit; `/sys/module/<name>` for each module.

**Background.** A **systemd unit** is Linux's version of a Windows service entry. "Masked" means it has been blocked on purpose and cannot start, even by hand.

---
## Boot mode options  
`boot-mode` - Linux data

**In plain words.** How this PC started: UEFI or old BIOS, Secure Boot on or off, which boot loader, and which "target" (Linux's version of a boot mode) it boots into.

**What you see.**
- `Firmware` - "UEFI" or "Legacy BIOS (CSM)". CSM (Compatibility Support Module) is the old-BIOS emulation inside UEFI firmware.
- `UEFI platform size` - 64-bit or 32-bit UEFI. Some cheap tablets have 32-bit UEFI on a 64-bit CPU, and that needs a special boot loader.
- `Secure Boot` - the line from `mokutil --sb-state`, e.g. "SecureBoot enabled" / "SecureBoot disabled".
- `Default target` - the systemd target used at boot. `graphical.target` = desktop login; `multi-user.target` = text console only.
- `Kernel command line` - the options the boot loader passed to the kernel.
- `Boot loader (bootctl status)` - the System block from `bootctl`: firmware arch, Secure Boot, TPM2 support, measured boot, "Boot into FW" (can the OS reboot you straight into BIOS setup), platform language, and the boot loader product.
- `Safe-mode equivalents (systemd)` - a fixed cheat sheet. Add `systemd.unit=rescue.target` (like Windows Safe Mode), `emergency.target` (bare root shell, read-only root) or `multi-user.target` (like Safe Mode with Networking, no desktop) to the kernel command line for one boot.

**Healthy / typical values.** Author's PC: UEFI, 64-bit, SecureBoot disabled, `graphical.target`, boot loader "GRUB 2.16", TPM2 Support yes. The command line starts `BOOT_IMAGE=/@/boot/vmlinuz-linux73-tkg-bore-llvm root=UUID=XXXX rw rootflags=subvol=@ ...` followed by tuning options such as `amd_pstate=active preempt=full`.

**Unusual values and what they suggest.** "Legacy BIOS (CSM)" on a modern board: the disk was installed in old mode. You cannot use Secure Boot, and Windows 11 dual-boot needs UEFI. `Default target` `multi-user.target` on a desktop: the PC will boot to a text login. Options you do not recognise on the command line (such as `mitigations=off`) change security or performance. Check the GRUB or systemd-boot config before you copy them.

**Where Linux gets it.** `/sys/firmware/efi` (present = UEFI), `/sys/firmware/efi/fw_platform_size`, `mokutil --sb-state`, `systemctl get-default`, `/proc/cmdline`, `bootctl status`.

**Background.** UEFI (Unified Extensible Firmware Interface) replaced the PC BIOS. Secure Boot is a UEFI feature that only runs boot loaders signed with trusted keys. A **systemd target** is a named group of services. Booting into a target is like picking a Windows boot mode.

---
## Corsair Link 4 service  
`cl4-srv` - Not present on most PCs

**In plain words.** Corsair Link 4 was Corsair's Windows service for fans, pumps and power supplies. On Linux, the same hardware is run by CoolerControl, liquidctl or kernel hwmon drivers. SRS shows those. This item prints the same probe as [Corsair service](#corsair-service).

**What you see.**
- `Windows service` - "not applicable on Linux - showing the Linux analogue".
- Units `coolercontrold.service`, `coolercontrol-liqctld.service`, `liquidcfg.service` - Active state and Enabled state.
- Modules `corsair_cpro` (Commander Pro), `corsair_psu` (Corsair HXi/RMi power supplies), `nzxt_kraken3`, `nzxt_smart2` - "loaded" / "not loaded".
- Processes `coolercontrold`, `coolercontrol-l` (the liquidctl helper; the kernel cuts process names to 15 characters), `liquidctl` - "running PID n" or "not running".

**Healthy / typical values.** Author's PC: `coolercontrold.service active enabled`, process `coolercontrold` running. No Corsair or NZXT kernel modules are loaded because no such device is attached.

**Unusual values and what they suggest.** A Corsair PSU on USB but `corsair_psu` "not loaded": power-supply sensors will be missing. Try `modprobe corsair_psu` or let CoolerControl read it. `coolercontrold` enabled but not running: the daemon crashed. Check `journalctl -u coolercontrold`.

**Where Linux gets it.** `systemctl is-active` / `is-enabled`, `/sys/module/<name>`, the process list in `/proc`.

**Background.** **hwmon** is the kernel's hardware-monitoring framework. Each sensor chip appears under `/sys/class/hwmon`, and tools like CoolerControl read and control it there.

---
## Shell CLSIDs  
`clsid` - Windows-only (Linux equivalent shown)

**In plain words.** In Windows, a CLSID (class ID) is a registry number that ties a file type or shell object to a program. Linux has no CLSIDs. The nearest thing is the default-application list, which says which app opens which file type or link type.

**What you see.**
- Table `MIME Type / Scheme` - `Default Handler (.desktop)`: one row per line in the `[Default Applications]` part of `~/.config/mimeapps.list`. Lines starting `x-scheme-handler/` (links such as `mailto:` or `https:`) are tinted differently.
- `System .desktop entries` - how many files are in `/usr/share/applications`.
- `User .desktop entries` - how many are in `~/.local/share/applications`.
- If you have no defaults: "no user defaults in ~/.config/mimeapps.list".

**Healthy / typical values.** Author's PC: `application/pdf -> okularApplication_pdf.desktop;`, `application/ogg -> vlc.desktop;`, `application/vnd.flatpak -> org.kde.discover.desktop;`. A trailing `;` is normal; the format allows a list.

**Unusual values and what they suggest.** A handler whose `.desktop` file no longer exists (the app was removed): double-clicking that file type does nothing or asks every time. Fix it in System Settings -> Default Applications or File Associations.

**Where Linux gets it.** `~/.config/mimeapps.list`; the counts are directory listings of `/usr/share/applications` and `~/.local/share/applications`.

**Background.** A **MIME type** (Multipurpose Internet Mail Extensions type) is a standard name for a kind of file, such as `image/png`. A **.desktop file** is a small text file that describes an app: its name, icon, command, and the MIME types it can open.

---
## Code pages  
`codepage` - Linux data

**In plain words.** A code page is an old table that maps byte values to letters. Windows still lists dozens of them. Linux uses UTF-8 almost everywhere, but its iconv library can still convert the old ones. This item checks each Windows code page against it.

**What you see.**
- `Page ID` - the Windows code page number (437, 850, 1252, 65001...).
- `Description` - what it is, e.g. "OEM United States", "ANSI Latin 1", "Mac Roman", "Unicode UTF-8".
- `glibc iconv` - the name glibc uses for it (CP437, IBM037, ISO-8859-15, MACINTOSH...), or `-` if none was found.
- `Supported` - ticked if glibc can convert that page to UTF-8.
- Footer: "N of 70 supported; locale charset X" and the number of glibc charmaps in `/usr/share/i18n/charmaps`.

**Healthy / typical values.** Author's PC: "58 of 70 supported; locale charset UTF-8", 233 charmaps. The gaps are pages glibc does not ship, such as 720 (Arabic Transparent ASMO) and several Mac pages.

**Unusual values and what they suggest.** Very few pages supported (say under 10): the glibc `gconv` modules are missing or were trimmed, for example in a minimal container. Old text files and some emulators will then show garbled letters. A locale charset of `ANSI_X3.4-1968` means plain ASCII: no `LANG` is set, so programs cannot print accented letters.

**Where Linux gets it.** glibc `iconv_open()` tested for each name; `/usr/share/i18n/charmaps`; the charset part of `LC_ALL` / `LC_CTYPE` / `LANG`.

**Background.** "OEM" pages (437, 850...) are the old DOS character sets. "ANSI" pages (1250-1258) are the old Windows ones. UTF-8 (page 65001) can hold every Unicode character, which is why Linux uses it by default.

---
## TAPI country list  
`country` - Linux data

**In plain words.** On Windows, TAPI (Telephony API) keeps a country list with dialling codes. Linux has no TAPI. SRS lists the ISO 3166 country table from the time-zone database instead, with the number of time zones in each country.

**What you see.**
- `Code` - the two-letter ISO 3166 country code. Your own country (from your locale) is shown in green.
- `Country` - the country name.
- `Zones` - how many time-zone entries `zone.tab` lists for it.
- `Representative Time Zone` - the first zone listed for that country.
- Footer: "N countries; current locale territory XX".

**Healthy / typical values.** Author's PC: 249 countries, current territory US. Examples: `AR Argentina 12 America/Argentina/Buenos_Aires`, `AQ Antarctica 10 Antarctica/McMurdo`.

**Unusual values and what they suggest.** "current locale territory -": your `LANG` has no country part (for example `C.UTF-8`), so programs fall back to default number, date and paper formats. Error "iso3166.tab not readable": the `tzdata` package is missing or damaged.

**Where Linux gets it.** `/usr/share/zoneinfo/iso3166.tab` and `/usr/share/zoneinfo/zone.tab` (both from tzdata); the territory comes from `LC_ALL` or `LANG`.

---
## Corsair service  
`cue-srv` - Not present on most PCs

**In plain words.** The Windows "Corsair Service" belongs to iCUE (Corsair Utility Engine). Linux has no iCUE. SRS shows the same Linux analogue as [Corsair Link 4 service](#corsair-link-4-service): CoolerControl and liquidctl units, Corsair and NZXT hwmon modules, and their processes.

**What you see.** Exactly the same lines as `cl4-srv`: three units with Active and Enabled state, four modules loaded / not loaded, three processes running / not running.

**Healthy / typical values.** Author's PC: `coolercontrold.service active enabled`, process running, no Corsair/NZXT modules loaded.

**Unusual values and what they suggest.** Corsair RGB keyboards and mice are not covered here. On Linux they are driven by OpenRGB or ckb-next, which this probe does not check.

**Where Linux gets it.** `systemctl`, `/sys/module`, `/proc`.

---
## Base DLLs  
`dll` - Linux data

**In plain words.** A DLL (dynamic-link library) is Windows' shared code file. The Linux version is the **shared object** (`.so` file). This item lists every file mapped into SRS's own process: the program, its libraries, and some data files.

**What you see.**
- `Image` - the running program file.
- `Dynamic loader` - the program that loads shared libraries at start, normally `/usr/lib/ld-linux-x86-64.so.2`.
- `glibc` - the C library version.
- Table:
  - `Base` / `End` - lowest and highest memory address the file occupies, in hex.
  - `Size` - End minus Base.
  - `Module` - file name.
  - `Version / Path` - for `name.so.N` files, "VN" is the number after `.so.` (the soname major number), then the full path.
- Footer: "N mapped files (executable, shared libraries, locale/data files)".

**Healthy / typical values.** Author's PC: glibc 2.44, 9 mapped files. These are `sysreport-search` (1.38MB), `libc.so.6` (V6, 2.25MB), `libm.so.6`, the loader (V2), `/etc/ld.so.cache`, and glibc `gconv` converters such as `UTF-16.so`. The converters appear because other items have already used iconv.

**Unusual values and what they suggest.** A library from an unexpected place (for example `/home/...` or `/tmp/...` instead of `/usr/lib`): something is setting `LD_LIBRARY_PATH` or `LD_PRELOAD`. Check [Known DLLs](#known-dlls). The GUI process (`srs-gui`) maps many more files (GTK and friends) than the command-line run.

**Where Linux gets it.** `/proc/self/maps`, `readlink /proc/self/exe`, `gnu_get_libc_version()`.

**Background.** The **soname** number (the 6 in `libc.so.6`) changes only when a library breaks compatibility. It is not the library's release version.

---
## DLLs by address  
`dll-addr` - Linux data

**In plain words.** The same table as [Base DLLs](#base-dlls), in memory-address order and without the header lines.

**What you see.** Base, End, Size, Module, Version / Path, and the "N mapped files" footer, exactly as in `dll`.

**Healthy / typical values.** The program itself sits low (around `6023822F3000` on the author's PC) and the libraries sit high (`7A58...`). The exact numbers change on every run.

**Unusual values and what they suggest.** Addresses that are the same on every run would mean ASLR is off; see `randomize_va_space` in [Virtual memory](#virtual-memory).

**Where Linux gets it.** `/proc/self/maps`.

**Background.** ASLR (Address Space Layout Randomization) loads programs and libraries at random addresses each time. That makes many attacks harder.

---
## Known DLLs  
`dll-known` - Linux data

**In plain words.** Windows keeps a list of "Known DLLs" that always load from the system folder. Linux has a cache of every library the loader knows about, plus the list of folders it searches. This item shows both.

**What you see.**
- `ld.so.cache` - the first line of `ldconfig -p`, e.g. "12521 libs found in cache `/etc/ld.so.cache'".
- `LD_LIBRARY_PATH` - extra library folders set in your environment, or `<not set>`.
- `LD_PRELOAD` - libraries forced into every program, or `<not set>`.
- `/etc/ld.so.preload` - shown only if the file exists. It forces libraries into every program system-wide.
- `Search directories` - each folder from `/etc/ld.so.conf` and `/etc/ld.so.conf.d/*.conf`, with the file that added it.
- `Core system libraries (ldconfig -p)` - cache entries for key libraries (libc, libm, libstdc++, libz, libssl, libsystemd, libdrm, libGL, libEGL, libvulkan, libX11, libwayland-client, libasound, libpulse...), both 64-bit (`libc6,x86-64`) and 32-bit (`libc6`).

**Healthy / typical values.** Author's PC: 12521 libraries, both variables not set. Extra folders come from installed packages, e.g. `/opt/rocm/lib` (rocm.conf), `/usr/lib32` (lib32-glibc.conf).

**Unusual values and what they suggest.** `LD_PRELOAD` set when you did not set it, or a `/etc/ld.so.preload` you do not recognise: something injects code into every program. That is sometimes a tool (a game overlay, a memory allocator) and sometimes malware. Find out which package owns it. A newly installed library that is missing from the cache: run `sudo ldconfig`.

**Where Linux gets it.** `ldconfig -p`, `/etc/ld.so.conf`, `/etc/ld.so.conf.d/`, `/etc/ld.so.preload`, environment variables.

---
## DLLs by name  
`dll-name` - Linux data

**In plain words.** The same table as [Base DLLs](#base-dlls), sorted by file name (A-Z, ignoring case).

**What you see.** Base, End, Size, Module, Version / Path, and the footer. Author's PC: `gconv-modules.cache`, `ISO8859-13.so`, `ISO8859-15.so`, `ld-linux-x86-64.so.2`, `ld.so.cache`, `libc.so.6`, `libm.so.6`, `sysreport-search`, `UTF-16.so`.

**Healthy / typical values / unusual values.** As for [Base DLLs](#base-dlls). Sorting by name makes it easy to spot two copies of the same library from different folders.

**Where Linux gets it.** `/proc/self/maps`.

---
## Drivers  
`drivers` - Linux data

**In plain words.** Windows drivers become Linux **kernel modules**: pieces of the kernel that are loaded on demand. This item lists every loaded module, the same list as `lsmod`.

**What you see.**
- `Module Name` - the module name (A-Z).
- `Size` - memory the module uses.
- `Used` - how many other modules or users hold it.
- `State` - `Live` (normal), `Loading` or `Unloading` (anything other than Live is shown in orange).
- `Taint` - letters from the kernel if this module "taints" the kernel: `O` = out-of-tree (built outside the kernel source), `E` = unsigned, `P` = proprietary licence. Blank = clean.
- `Used By` - the modules that depend on it.
- Footer: "N loadable modules loaded, M built into kernel <version> (modules.builtin)".

**Healthy / typical values.** Author's PC: 237 loaded, 256 built in. `amdgpu` is 15.32MB with Used 61. `bluetooth` is used by `btusb`, `btrtl`, `btintel` and others. `zenergy` shows `OE` (an out-of-tree, unsigned energy-sensor module).

**Unusual values and what they suggest.** A module stuck in `Unloading`: something still holds it, and a reboot may be needed. `P` on a module: a closed-source driver (for example the NVIDIA one) is loaded, and kernel developers will ignore bug reports from that kernel. Your GPU driver missing from the list (and not built in): the desktop is running on a fallback driver with no 3D acceleration.

**Where Linux gets it.** `/proc/modules`; the built-in count is the line count of `/usr/lib/modules/<release>/modules.builtin`.

**Background.** A **built-in** driver is compiled into the kernel file itself. It is always present and never shows in `/proc/modules`.

---
## Driver versions  
`driver-vsn` - Linux data

**In plain words.** Every module the kernel knows about, loaded or built in, with whatever version facts it publishes.

**What you see.**
- `Module` - name, from `/sys/module`.
- `Type` - `loadable` (a separate `.ko` file) or `built-in`.
- `Version` - the module's own version string, if its author set one (most in-tree modules do not, so `-` is normal).
- `Source Version` - `srcversion`, a checksum of the module's source. It changes whenever the code changes.
- `Refs` - reference count for loadable modules.
- `Parameters` - how many tunable parameters it has under `/sys/module/<name>/parameters`.
- Footer note: "Open as driver-vsn:<module> for full modinfo output".
- With an argument, e.g. `driver-vsn:zram`, the item prints the full `modinfo` output instead: file name, licence, author, description, dependencies, `intree`, `vermagic` (the kernel it was built for), signer, hash algorithm, and each `parm:` with its description.

**Healthy / typical values.** Author's PC: 390 lines. `ahci` is built in, version 3.0, 4 parameters. `amdgpu` is loadable with 95 parameters. `zenergy` is loadable with version 1.0. `driver-vsn:zram` shows `description: Compressed RAM Block Device`, `intree: Y`, `signer: Build time autogenerated kernel key`.

**Unusual values and what they suggest.** In `modinfo`, a `vermagic` that names a different kernel than the running one: the module was built for another kernel and will refuse to load. Usually a DKMS (Dynamic Kernel Module Support) rebuild is missing. `intree` absent: an out-of-tree module (DKMS or a vendor package).

**Where Linux gets it.** `/sys/module/<name>/{version,srcversion,refcnt,initstate,parameters}`; `modinfo <name>` for the argument form.

---
## E-mail accounts  
`e-mail` - Windows-only (Linux equivalent shown)

**In plain words.** SIV lists Windows mail accounts. SRS never reads account data. It only reports which mail programs have a profile on this Linux account, and which app opens `mailto:` links.

**What you see.**
- `Thunderbird` - the path of each `profiles.ini` it finds (normal install, Flatpak, Snap), then one `Profile` line per profile name.
- `Evolution` - "N source definitions in ~/.config/evolution/sources".
- `KMail / Akonadi` - the Akonadi data folder, if present.
- "No Thunderbird/Evolution/KMail profiles found" if none.
- `mailto: handler` - the `.desktop` file that opens e-mail links.

**Healthy / typical values.** Author's PC: Evolution with 10 source definitions, the KMail/Akonadi folder present, and a `mailto:` handler set (for example `thunderbird.desktop` on a typical system).

**Unusual values and what they suggest.** `mailto: handler` `-`: clicking an e-mail link in a browser does nothing. Set a default mail client in System Settings.

**Where Linux gets it.** `~/.thunderbird/profiles.ini` (and the Flatpak and Snap copies), `~/.config/evolution/sources`, `~/.local/share/akonadi`, `xdg-mime query default x-scheme-handler/mailto`.

---
## Environment  
`environment` - Linux data

**In plain words.** Environment variables are named text settings that every program inherits from the one that started it, such as `PATH`, `HOME` and `LANG`. Windows and Linux both have them.

**What you see.**
- `Environment Variable Name` - sorted A-Z.
- `Size` - length of the value in characters.
- `Environment Variable Expansion` - the value. Values whose names contain TOKEN, KEY, SECRET, PASS, AUTH, COOKIE, CREDENTIAL or SESSION_ID are replaced by "******** (masked)".
- `/etc/environment` - a second block with the system-wide settings from that file (comment lines skipped), in the same three columns.
- Footer: "N process variables; values of names matching ... are masked".

**Healthy / typical values.** You should see `HOME=/home/user`, `LANG=en_US.UTF-8`, `PATH=/usr/local/bin:/usr/bin...`, `XDG_CURRENT_DESKTOP=KDE`, `XDG_SESSION_TYPE=wayland`. The author's PC also has tuning variables such as `AMD_VULKAN_ICD=RADV` and `CC=aocc-clang`.

**Unusual values and what they suggest.** `PATH` missing `/usr/bin`: many commands will "not be found". A very long `PATH` with repeated folders: a shell start-up file adds the same folder every time it runs. `LD_PRELOAD` or `LD_LIBRARY_PATH` set globally: see [Known DLLs](#known-dlls). If SRS was started from a menu, it shows the desktop session's variables. Started from a terminal, it shows that shell's variables.

**Where Linux gets it.** The process environment (`/proc/self/environ`); `/etc/environment`.

---
## ESU status  
`esu-sts` - Windows-only (Linux equivalent shown)

**In plain words.** ESU (Extended Security Updates) is Microsoft's paid extra support for old Windows versions. The Linux question is: "does my distribution still get updates, and when did I last sync them?"

**What you see.**
- `Distribution` - `PRETTY_NAME` from `/etc/os-release`.
- `Support end` - `SUPPORT_END` if the distribution sets one, else "none (rolling release)".
- `Home`, `Support` - the distribution's web links, if set.
- `Repository databases (last sync)` - each pacman repository database with the date it was last downloaded.

**Healthy / typical values.** Author's PC: CachyOS, "none (rolling release)", most databases synced on 2026-10-23 or 2026-10-24.

**Unusual values and what they suggest.** A database that is months old (on the author's PC two old third-party repositories are from 2025-04-12): that repository has stopped publishing or was removed upstream. Consider dropping it from `/etc/pacman.conf`. A `Support end` date in the past on a fixed-release distribution: you are no longer getting security fixes, so upgrade to the next release.

**Where Linux gets it.** `/etc/os-release`; file times of `/var/lib/pacman/sync/*.db`.

---
## System file cache  
`file-cache` - Linux data

**In plain words.** Linux keeps recently used file data in spare RAM (the **page cache**) so it can be read again without touching the disk. Windows calls this the system file cache. This item shows how big the cache is, how busy it is, and the rules for writing changed data back to disk.

**What you see.** In the GUI it refreshes every second.
- `Page cache (/proc/meminfo)` table, each with a readable size and raw kB:
  - `Cached` - file data held in RAM.
  - `Buffers` - raw disk blocks (metadata) held in RAM.
  - `Active(file)` / `Inactive(file)` - recently used vs. older cache. Inactive pages are freed first when memory runs short.
  - `Dirty` - changed data not yet written to disk.
  - `Writeback` - data being written right now.
  - `Mapped` - cached files that programs have mapped into memory (libraries, for example).
  - `Shmem` - shared memory and tmpfs (RAM disks).
  - `Unevictable` - pages that may not be dropped (locked memory).
  - `FileHugePages` - cache held in large 2 MB pages.
- `Counters (/proc/vmstat)` - totals since boot: `pgpgin`/`pgpgout` (data read from / written to disk, in kB), `pgfault`/`pgmajfault` (page faults; "major" means the disk had to be read), `workingset_refault_file` (cache pages dropped and then needed again), `workingset_activate_file`, `pgsteal_kswapd`/`pgsteal_direct` (pages reclaimed by the background thread vs. by a program that had to wait), `drop_pagecache` (times someone ran "drop caches").
- `Writeback policy (/proc/sys/vm)` - `dirty_ratio`, `dirty_background_ratio`, `dirty_bytes`, `dirty_background_bytes`, `dirty_expire_centisecs`, `dirty_writeback_centisecs`, `vfs_cache_pressure`.
- `Page Size` - normally 4096 bytes.

**Healthy / typical values.** Author's PC (32 GB RAM): Cached 17.76GB, Dirty 3.75MB, Writeback 0B. Writeback limits are set in bytes (`dirty_bytes 268435456` = 256 MB, background 64 MB), `vfs_cache_pressure 30`. A big `Cached` value is good. It is free RAM doing useful work and is handed back as soon as programs need it.

**Unusual values and what they suggest.** `Dirty` staying in the gigabytes: a slow disk (USB stick, network share) is behind on writes, and pulling it out now loses data. `pgsteal_direct` rising fast while a game stutters: programs are waiting for memory to be freed, so RAM is short. A high `workingset_refault_file` with a small cache: the cache is too small for your workload.

**Where Linux gets it.** `/proc/meminfo`, `/proc/vmstat`, `/proc/sys/vm/*`, `sysconf(_SC_PAGESIZE)`.

**Background.** **Dirty** pages are changed in RAM but not yet saved to disk. The `dirty_*` settings decide how much may pile up before the kernel starts writing (background) and before programs are made to wait.

---
## Open server files  
`files` - Linux data - Needs root data

**In plain words.** On Windows this lists files that other PCs have open on your shared folders. On Linux, SRS shows file locks (who is holding which file), files open over Samba (SMB, Windows-style file sharing) and NFS (Network File System) exports.

**What you see.**
- `File locks (lslocks)` - columns `COMMAND`, `PID`, `TYPE` (`FLOCK`, `POSIX`, `OFDLCK` - three kinds of Linux file lock), `MODE` (`READ`/`WRITE`), `M` (1 = mandatory lock), `PATH`. Up to 400 lines.
- `SMB open files (smbstatus -L)` - comes from the srs-root cache. Without it you see "Needs root: Menu > Run as root (srs-root)".
- `NFS exports (/var/lib/nfs/etab)` - each exported folder with its options, or "none / not readable".

**Healthy / typical values.** Author's PC: locks held by `kwin_wayland`, `krunner`, `akonadi`, `libvirtd`, `dockerd` and Flatpak's `bwrap`. A virtual machine holds `OFDLCK` read locks on its disk image. Samba reports "No locked files".

**Unusual values and what they suggest.** A program can't open a file ("resource busy", "database is locked"): look here for who holds it. A lock on a path ending "(deleted)": the file was removed while still open, so its disk space is not freed until that process exits. Long paths may be cut short with "..." by `lslocks`.

**Where Linux gets it.** `lslocks`, `smbstatus -L` (run by srs-root), `/var/lib/nfs/etab`.

---
## Feature management overrides  
`fmo-sts` - Windows-only (Linux equivalent shown)

**In plain words.** Windows uses hidden "feature management" switches to turn new features on or off. Linux has no such system, and SRS shows no equivalent.

**What you see.** A short stub: `Status - Windows-only item in SIV`, `Linux equivalent - none`. That is the whole item, and nothing is wrong.

---
## Folder paths  
`folders` - Linux data

**In plain words.** Windows has "known folders" (AppData, Documents, Startup...). Linux has the XDG (freedesktop.org Cross-Desktop Group) folders. This item shows each one, with the Windows-style name next to the Linux variable, and ticks the ones that exist.

**What you see.** `Name` - tick (exists) + `Folder Path`:
- `PROFILE (HOME)`, `APPDATA (XDG_CONFIG_HOME)` = `~/.config`, `LOCAL_APPDATA (XDG_DATA_HOME)` = `~/.local/share`, `STATE (XDG_STATE_HOME)` = `~/.local/state`, `INTERNET_CACHE (XDG_CACHE_HOME)` = `~/.cache`, `RUNTIME (XDG_RUNTIME_DIR)` = `/run/user/<uid>`.
- Your user folders from `~/.config/user-dirs.dirs`: DESKTOP, DOCUMENTS, DOWNLOAD, MUSIC, PICTURES, PUBLICSHARE, TEMPLATES, VIDEOS, plus any extra ones.
- Fixed ones: FONTS (`~/.local/share/fonts`), PROGRAMS (`~/.local/share/applications`), STARTUP (`~/.config/autostart`), TRASH, RECENT (`recently-used.xbel`), COMMON_PROGRAMS (`/usr/share/applications`), COMMON_STARTUP (`/etc/xdg/autostart`), COMMON_APPDATA (`/etc/xdg`), COMMON_FONTS (`/usr/share/fonts`), PROGRAM_FILES (`/usr/bin`), PROGRAM_FILES_OPT (`/opt`), SYSTEM (`/usr/lib`), TEMP (`$TMPDIR` or `/tmp`).
- `XDG_DATA_DIRS`, `XDG_CONFIG_DIRS` - the system search lists.

**Healthy / typical values.** Author's PC: every path ticked; RUNTIME is `/run/user/1000`. DOCUMENTS, DOWNLOAD, MUSIC, PICTURES and VIDEOS all point to `/home/user/`, and there is an extra PROJECTS folder.

**Unusual values and what they suggest.** An empty box next to a user folder: `user-dirs.dirs` points to a folder that was deleted or renamed. Apps may save into your home folder instead. Run `xdg-user-dirs-update` or fix the file. No RUNTIME line: you are not in a proper login session (for example `su` without `-l`), so many desktop apps will fail.

**Where Linux gets it.** Environment variables, `~/.config/user-dirs.dirs`, fixed paths checked with `stat`.

---
## Font families  
`fonts` - Linux data

**In plain words.** Every installed font family, and which writing systems (character sets) each one covers. On Windows SIV lists GDI (Graphics Device Interface) character sets. On Linux, SRS asks fontconfig which fonts support each language.

**What you see.**
- `Unique N font families`.
- One line per character set with how many families cover it: `WER Western (:lang=en)`, `CER Central European (pl)`, `CRK Cyrillic (ru)`, `GRK Greek (el)`, `TRK Turkish (tr)`, `BTC Baltic (lt)`, `HBW Hebrew (he)`, `ARC Arabic (ar)`, `VMN Vietnamese (vi)`, `THI Thai (th)`, `JAP Japanese (ja)`, `HGL Hangul (ko)`, `CGB Chinese GB2312 (zh-cn)`, `CB5 Chinese BIG5 (zh-tw)`.
- A grid: one row per family (A-Z), with the code of each set it fully supports.

**Healthy / typical values.** Author's PC: 2141 families; 728 cover Western, 32 Japanese, 10 Thai. `AR PL New Kai` covers Cyrillic, Japanese and both Chinese sets. `3270 Nerd Font` covers the six European sets. Icon fonts (`octicons`, `icomoon`) cover none, which is normal.

**Unusual values and what they suggest.** JAP, HGL, CGB or CB5 at 0: you have no CJK (Chinese, Japanese, Korean) font, so those scripts show as empty boxes. Install Noto Sans CJK. A family listed with no sets is usually a symbol, icon or decorative font.

**Where Linux gets it.** `fc-list : family` and `fc-list :lang=<code> family` (fontconfig).

**Background.** **fontconfig** is the Linux library that finds fonts and decides which font to use for each character. "Covers a language" means the font has every letter fontconfig expects for it.

---
## Groups  
`groups` - Linux data

**In plain words.** Linux groups give a set of users shared rights, such as `audio` for sound devices or `wheel` for admin rights. Windows has local groups in the same way.

**What you see.**
- `Group Name`, `GID` (group ID number), `Members` (users listed for that group).
- Groups the SRS process belongs to are meant to show in green.
- Footer: "N groups; green = groups of the current process (n)".

**Healthy / typical values.** Author's PC: 286 groups. `root 0`, `wheel 998 user` (wheel members may use sudo), `audio 995` with several service accounts, and `nobody 65534` with no members.

**Unusual values and what they suggest.** Your user missing from `wheel` (or `sudo` on Debian-style systems): you cannot run admin commands. Your user missing from `video`, `input` or `uucp` when a device needs it: "permission denied" on GPUs, game controllers or serial adapters. Group changes take effect only after you log out and back in.

**Where Linux gets it.** `/etc/group`; the process's own groups from `getgroups()`.

**Background.** A user's main group is in `/etc/passwd`. `/etc/group` lists only the extra (supplementary) memberships, so a group can look empty and still be someone's main group.

---
## Handles  
`handle` - Linux data

**In plain words.** A Windows handle is a ticket to an open object. The Linux version is a **file descriptor**: a small number a process uses for each open file, pipe, socket or device. This item shows system limits, SRS's own descriptors, and the processes holding the most.

**What you see.**
- `System file-nr (alloc free max)` - descriptors in use system-wide, a legacy "free" figure (always 0 on modern kernels), and the system maximum.
- `file-max` - system-wide maximum.
- `nr_open (per process max)` - the highest per-process limit allowed.
- `inode-nr (alloc free)` - in-memory file records allocated and free.
- `RLIMIT_NOFILE (soft hard)` - SRS's own open-file limit.
- `This process (/proc/self/fd)` - each descriptor number and what it points to (0 = input, 1 = output, 2 = errors).
- `Open handles per process - top 30 of N readable` - process name, PID, descriptor count. Only processes you may read are counted. Without root, that means your own.

**Healthy / typical values.** Author's PC: `31933 0 524288` in use, soft/hard limit 1048576. Top holders: `dbus-broker` 341, `systemd` 248, `plasmashell` 145, `pipewire` 135.

**Unusual values and what they suggest.** One process with tens of thousands and rising: a **descriptor leak**. That program will eventually fail with "Too many open files". The first number near `file-max`: the whole system is about to run out. A low soft limit (1024) with games or Wine/Proton: some titles need far more. Esync needs a high `RLIMIT_NOFILE`.

**Where Linux gets it.** `/proc/sys/fs/{file-nr,file-max,nr_open,inode-nr}`, `getrlimit(RLIMIT_NOFILE)`, `/proc/self/fd`, `/proc/<pid>/fd` for every PID.

---
## Process heap  
`heap` - Linux data

**In plain words.** The **heap** is the pool of memory a program asks for while it runs (`malloc` in C). This item shows SRS's own heap as seen by glibc's allocator.

**What you see.**
- `Arena (sbrk heap)` - size of the main heap area.
- `mmap'd blocks` - big allocations served straight from the kernel, and how many.
- `In use (allocated)` - bytes the program is actually using.
- `Free (in arena)` - free space inside the heap and how many free chunks.
- `Fastbin free` - small freed blocks kept for quick reuse.
- `Releasable (top)` - free space at the top that could be handed back to the system.
- `[heap] mapping (/proc/self/smaps)` - the heap's address range, then `Size`, `Rss` (in RAM), `Pss` (proportional share), `Pss_Dirty`, `Private_Clean`, `Private_Dirty`, `Anonymous`, `Private_Hugetlb`, `Swap`.

**Healthy / typical values.** Author's PC, command-line run: arena 12.23MB, 9.88MB in use, 2.35MB free in 113 chunks, 2 mmap'd blocks of 18.01MB total. The heap mapping is 12528 kB with 6392 kB resident.

**Unusual values and what they suggest.** For SRS itself these numbers only matter to developers. A `Free` figure much larger than `In use` means fragmentation (memory freed in small scattered pieces).

**Where Linux gets it.** glibc `mallinfo2()`; the `[heap]` entry in `/proc/self/smaps`.

---
## Registry hives  
`hives` - Windows-only (Linux equivalent shown)

**In plain words.** Windows keeps settings in registry hive files. Linux keeps them mostly in plain text files under `/etc` (system) and `~/.config` (yours). A few binary databases act like hives, and SRS lists those, with their sizes and dates.

**What you see.**
- Table `Database` - `Size` - `Modified`:
  - `~/.config/dconf/user` - GNOME/GTK settings database (dconf).
  - `/usr/share/glib-2.0/schemas/gschemas.compiled` - compiled list of all GTK/GNOME setting keys.
  - `/etc/ld.so.cache` - library cache (see [Known DLLs](#known-dlls)).
  - `/usr/lib/udev/hwdb.bin`, `/etc/udev/hwdb.bin` - the hardware database (device names, quirks).
  - Files in `/etc/dconf/db` - system-wide dconf defaults.
- `KDE config files (~/.config/*rc)` - how many KDE-style `...rc` text files you have and their total size.

**Healthy / typical values.** Author's PC: dconf user 66.07KB, hwdb.bin 13.87MB, 145 KDE rc files totalling 519.95KB. Dates match the last system update.

**Unusual values and what they suggest.** `hwdb.bin` much older than the last udev update: the database wasn't rebuilt, so new devices may get generic names. Run `sudo systemd-hwdb update`. A dconf file of several MB: an app is storing too much there.

**Where Linux gets it.** `stat` of the listed files; directory listings of `/etc/dconf/db` and `~/.config`.

---
## Code integrity  
`integrity` - Linux data

**In plain words.** Windows Code Integrity decides which drivers may load. On Linux, the same job is shared by module signing, kernel lockdown, Secure Boot, Linux Security Modules and hardening switches. The kernel also keeps a **taint** record of anything unusual that has happened since boot.

**What you see.**
- `Kernel lockdown` - `[none] integrity confidentiality`, or "lockdown LSM not built/active" (an LSM, Linux Security Module, is a pluggable security layer in the kernel). Lockdown stops even root from changing the running kernel.
- `Secure Boot (mokutil)` - enabled / disabled.
- `module.sig_enforce` - `Y` = only signed modules may load; `N` = unsigned ones load but taint the kernel.
- `kernel.modules_disabled` - 1 = no more modules can be loaded until reboot.
- `kernel.kexec_load_disabled` - 1 = no booting another kernel without a reboot.
- `kernel.kptr_restrict` - hides kernel addresses (0 shown, 1 hidden from non-root, 2 hidden from everyone).
- `kernel.dmesg_restrict` - 1 = only root can read the kernel log.
- `kernel.perf_event_paranoid` - how much performance monitoring normal users may do (higher = stricter).
- `kernel.unprivileged_bpf_disabled` - whether normal users can load BPF (Berkeley Packet Filter) programs, small sandboxed programs that run inside the kernel.
- `kernel.yama.ptrace_scope` - who may attach a debugger to a process (1 = only to your own child processes).
- `kernel.randomize_va_space` - ASLR (2 = full).
- `Active LSMs` - the security modules running.
- `TPM version` - 2 for a TPM (Trusted Platform Module) 2.0 security chip.
- `IMA` - Integrity Measurement Architecture, present / not active.
- Taint table: `Tainted N (0xHEX)`, then one box per bit 0-18 with its letter and meaning (P proprietary module, F forced load, S out-of-spec system, R forced unload, M machine check, B bad page, U userspace request, D recent oops, A ACPI (Advanced Configuration and Power Interface) table override, W warning, C staging driver, I firmware workaround, O out-of-tree module, E unsigned module, L soft lockup, K live patch, X distro, T struct randomisation, N test run).

**Healthy / typical values.** Author's PC: lockdown not active, SecureBoot disabled, `sig_enforce N`, `kptr_restrict 2`, `ptrace_scope 1`, LSMs `capability,landlock,yama,bpf`, TPM 2. Tainted 12356 (0x3044) = bits S, U, O, E: out-of-tree unsigned modules, a userspace-set flag, and the "out of spec" flag that an overclocked or tuned system can trigger. A stock distro kernel with no extra modules shows `Tainted 0`.

**Unusual values and what they suggest.** Bit `M` (machine check) set: the CPU reported a hardware error. Look in `journalctl -k` for "Machine check". This often points to unstable overclocking or memory. Bit `D` or `W`: the kernel hit a bug or warning since boot. Bit `L`: a CPU got stuck for over 20 seconds. Any of these is worth checking in the kernel log before blaming an app.

**Where Linux gets it.** `/sys/kernel/security/lockdown`, `mokutil --sb-state`, `/sys/module/module/parameters/sig_enforce`, `/proc/sys/kernel/*`, `/sys/kernel/security/lsm`, `/sys/class/tpm/tpm0/tpm_version_major`, `/sys/kernel/security/ima`, `/proc/sys/kernel/tainted`.

**Background.** A **taint flag** never clears until reboot. It does not mean the kernel is broken. It records conditions that make crash reports harder to trust.

---
## Key names  
`key-names` - Linux data

**In plain words.** The name of each keyboard key code 0-127, as the Linux kernel numbers them. SIV shows Windows scan-code names. For the main keys, the Linux codes match the classic PC scan codes (set 1).

**What you see.**
- A 4-column grid of `Scan` (code number) and `Key Name` (from the kernel's `KEY_` list: `ESC`, `A`, `LEFTSHIFT`, `KPENTER`, `VOLUMEUP`...). Codes with no name are blank.
- A note with your console keymap and X11 layout, e.g. "VC Keymap: us; X11 Layout: us".
- A note that the letters you actually get depend on the active keyboard layout (XKB, the X Keyboard Extension).

**Healthy / typical values.** `1 ESC`, `2`-`11` = the 1-0 number row, `28 ENTER`, `57 SPACE`, `85` blank, `125 LEFTMETA` (the Windows key). Japanese keys appear at 85-95 (`ZENKAKUHANKAKU`, `HENKAN`, `KATAKANAHIRAGANA`, `MUHENKAN`).

**Unusual values and what they suggest.** Error "input-event-codes.h not found": the `linux-api-headers` package (or your distro's kernel headers) is missing. The table cannot be built without it.

**Where Linux gets it.** `/usr/include/linux/input-event-codes.h`; `localectl status` for the layout note.

**Background.** A key code is the number for a physical key. The **layout** (US, UK, German...) turns that number into a letter. That is why the same key can type `y` or `z`.

---
## Locales  
`locales` - Linux data

**In plain words.** A **locale** is a bundle of language and regional rules: language, date and number format, currency, sorting, paper size. This item shows your current settings and every locale compiled on this PC.

**What you see.**
- `Current settings` - the `localectl status` lines (System Locale, VC Keymap, X11 Layout/Model/Options). Then each of `LANG`, `LANGUAGE`, `LC_ALL`, `LC_CTYPE`, `LC_NUMERIC`, `LC_TIME`, `LC_COLLATE`, `LC_MONETARY`, `LC_MESSAGES`, `LC_PAPER`, `LC_MEASUREMENT` that is set in the environment ("(empty)" if set to nothing).
- Table:
  - `Locale` - the name, e.g. `en_US.utf8`. Your current one is green.
  - `Description` - its title.
  - `Code` - language-country, e.g. `en-US`.
  - `Code Set` - character set (`UTF-8`, `ISO-8859-1`, `BIG5`...).
  - `Country` - territory name.
- Footer: "Showing N compiled locales (locale -a -v)".

**Healthy / typical values.** Author's PC: System Locale `LANG=C.UTF-8`, session `LANG en_US.UTF-8`, 502 compiled locales (nearly all of glibc's list, in both legacy and `.utf8` forms). A typical install has only two or three: `C.utf8`, `POSIX`, and your own language.

**Unusual values and what they suggest.** Your session `LANG` not in the table: the locale was never generated. Programs print "Cannot set LC_ALL" warnings and fall back to English/ASCII. Uncomment it in `/etc/locale.gen` and run `locale-gen`. `LC_ALL` set in your environment: it overrides every other setting, which is useful for testing and confusing if you forget it is there.

**Where Linux gets it.** `localectl status`, environment variables, `locale -a -v`.

---
## Memory heap  
`mem-heap` - Linux data

**In plain words.** A full memory summary for SRS's own process: how much it has reserved, how much is really in RAM, and its stack.

**What you see.**
- `/proc/self/status` - `VmPeak` (largest address space so far), `VmSize` (current address space), `VmLck`/`VmPin` (locked memory), `VmHWM` (peak RAM use), `VmRSS` (RAM in use now) split into `RssAnon` (program data), `RssFile` (mapped files) and `RssShmem`, then `VmData`, `VmStk` (stack), `VmExe` (program code), `VmLib` (library code), `VmPTE` (page tables), `VmSwap`, `HugetlbPages`, `Threads`.
- `/proc/self/smaps_rollup` - totals for the whole process: `Rss`, `Pss`, `Pss_Dirty`, `Pss_Anon`, `Pss_File`, `Shared_*`, `Private_*`, `Referenced`, `Anonymous`, `Swap` and so on.
- `Thread stacks / anonymous [stack] mapping` - the main stack's address range and its Size, Rss, Pss, Private, Anonymous and Swap lines.

**Healthy / typical values.** Author's PC, command-line run: VmSize 50056 kB, VmRSS 23636 kB (19108 anon + 4528 file), 1 thread, VmSwap 0. Stack 128 kB dirty.

**Unusual values and what they suggest.** `VmSize` far bigger than `VmRSS` is normal. Address space is only reserved, not used. A `VmSwap` above 0 for an active program means the system was short of RAM at some point.

**Where Linux gets it.** `/proc/self/status`, `/proc/self/smaps_rollup`, `/proc/self/smaps`.

**Background.** **RSS** (Resident Set Size) is RAM really in use. **PSS** (Proportional Set Size) splits shared pages fairly between the processes that share them. Adding up PSS for all processes gives a true total.

---
## Memory mapping  
`mem-map` - Linux data

**In plain words.** The raw memory map of SRS's own process: every region of its address space and what is in it.

**What you see.** One line per region: `Address Range` (start-end in hex), `Perm` (`r` read, `w` write, `x` execute, `p` private / `s` shared), `Offset` into the file, `Dev` (device major:minor), `Inode` (file number), and `Path` (file name, or `[heap]`, `[stack]`, `[vdso]`, or blank for anonymous memory).

**Healthy / typical values.** Each library appears as 4-5 lines: read-only header, `r-xp` code, read-only data, and `rw-p` writable data. Example: `/usr/lib/libc.so.6` with one `r-xp` region of about 1.6 MB.

**Unusual values and what they suggest.** A region that is both writable and executable (`rwxp`) in a normal program is a security smell. JIT (just-in-time) compilers (browsers, Java) use them legitimately; SRS does not.

**Where Linux gets it.** `/proc/self/maps`.

---
## System metrics  
`metrics` - Windows-only (Linux equivalent shown)

**In plain words.** Windows "system metrics" are screen and input facts (screen size, mouse buttons, border widths). Linux desktops keep these in the compositor, so SRS shows scaling variables, input-device counts and the KDE screen setup.

**What you see.**
- Scaling and cursor variables, if set: `XCURSOR_THEME`, `XCURSOR_SIZE`, `GDK_SCALE`, `GDK_DPI_SCALE`, `QT_SCALE_FACTOR`, `QT_SCREEN_SCALE_FACTORS`, `QT_AUTO_SCREEN_SCALE_FACTOR`, `PLASMA_USE_QT_SCALING`.
- `Pointer devices` - input devices with a mouse handler (mice, touchpads, some receivers).
- `Keyboards (with LEDs)` - devices with keyboard and LED handlers.
- `Outputs (kscreen-doctor -o)` - for each monitor: output number, connector name and an ID, enabled/connected, priority, connector type, all modes (`*` = current, `!` = preferred), geometry, scale, rotation, overscan, VRR (variable refresh rate), RGB range, HDR (High Dynamic Range), wide colour gamut, ICC (International Color Consortium) colour profile, brightness control.

**Healthy / typical values.** Author's PC: 4 pointer devices, 3 keyboards. `HDMI-A-1` enabled at `1920x1080@60.00*!`, Scale 1, Vrr incapable, RgbRange Full.

**Unusual values and what they suggest.** `RgbRange: Limited` on a PC monitor: blacks look grey. Set it to Full in the display settings. `Vrr: incapable` on a FreeSync monitor: FreeSync is off in the monitor's menu, or the cable or port does not support it. "kscreen-doctor not available": you are not in a KDE session, so only the first lines appear.

**Where Linux gets it.** Environment variables; `/proc/bus/input/devices`; `kscreen-doctor -o`.

---
## MLang conversions  
`mlang` - Windows-only (Linux equivalent shown)

**In plain words.** MLang is a Windows library for converting text between code pages. On Linux, glibc iconv does that job. This item tests each Windows code page from [Code pages](#code-pages) for three conversions.

**What you see.**
- Header names your locale charset (the "ACP", Active Code Page).
- `Identifier` - code page number.
- Three boxes: can convert to UTF-16, to UTF-8, to your locale charset.
- `Codepage Description` in angle brackets, and the `iconv name`.
- Note explaining the three boxes.

**Healthy / typical values.** Author's PC: ACP is UTF-8, and nearly every page shows `[=][=][=]`. Page 720 shows `[_][_][_]` because glibc has no converter for it.

**Unusual values and what they suggest.** A page with a UTF-8 tick but no ACP tick: your locale charset is not UTF-8, so that text may lose characters on your screen.

**Where Linux gets it.** glibc `iconv_open()`.

---
## DOS device names  
`ms-dos` - Linux data

**In plain words.** Windows maps DOS names like `C:` to internal devices. Linux has no drive letters. Instead, udev makes stable names under `/dev/disk/` that point to the real device (`/dev/nvme0n1p2`, `/dev/sda1`...). This item lists them all.

**What you see.** One block per folder that exists, each line `name` -> `/dev/<device>`:
- `/dev/disk/by-label` - filesystem labels you gave the partitions.
- `/dev/disk/by-uuid` - filesystem UUIDs (Universally Unique Identifiers; what `/etc/fstab` usually uses).
- `/dev/disk/by-partlabel`, `/dev/disk/by-partuuid` - GPT (GUID Partition Table) partition names and IDs.
- `/dev/disk/by-id` - model and serial-based names.
- `/dev/disk/by-path` - names based on the physical port or bus position.

**Healthy / typical values.** Author's PC by-label: `LINUX -> /dev/nvme0n1p2`, `EFI -> /dev/nvme1n1p2`, `SWAP -> /dev/nvme1n1p4`, `WIN11 -> /dev/nvme1n1p1`, `Ventoy -> /dev/sdd1`, `zram0 -> /dev/zram0`.

**Unusual values and what they suggest.** Two partitions with the same UUID (common after cloning a disk): the system may mount the wrong one at boot. Change one with `tune2fs -U` or your filesystem's tool. A disk missing from `by-id` but present in `/dev`: it is behind a USB bridge or RAID controller that hides the serial.

**Where Linux gets it.** Symlinks in `/dev/disk/*`, created by udev.

**Background.** Device names like `sda` or `nvme1n1` can change between boots when disks are added or removed. The `by-uuid` and `by-label` names do not change, so use those in `/etc/fstab`.

---
## N2K service  
`n2k-srv` - Not present on most PCs

**In plain words.** SIV checks for a Windows vendor service called N2K. It has no Linux counterpart, so SRS prints a short stub.

**What you see.** `Status - Not present on this system` and `Checked - n/a (Windows vendor service)`. "Not present" is the expected result on Linux. Nothing is wrong.

---
## NLS code pages  
`nls-info` - Linux data

**In plain words.** NLS (National Language Support) is the Windows name for its code-page tables. The Linux version is the set of glibc **charmaps**: definitions of every character set glibc knows, used when building locales.

**What you see.**
- `ACP (locale charset)` - your current charset.
- `Charmap directory` - `/usr/share/i18n/charmaps`.
- `Charmaps` - how many.
- `Compiled locales` - where compiled locales live (`/usr/lib/locale/locale-archive`).
- A 4-column list of every charmap name, each with a box ticked if iconv can also convert it to UTF-8 under that same name.

**Healthy / typical values.** Author's PC: ACP UTF-8, 233 charmaps. Most are ticked (`BIG5`, `CP1252`, `EUC-JP`, `GB18030`, `SHIFT_JIS`, `UTF-8`...). A few rare ones are not (`BS_VIEWDATA`, `SAMI`, `T.101-G2`).

**Unusual values and what they suggest.** Charmaps 0: the glibc locale source files were removed (some "slim" setups do this). `locale-gen` then cannot build new locales.

**Where Linux gets it.** Directory listing of `/usr/share/i18n/charmaps`; `iconv_open()`.

---
## Paging files  
`paging` - Linux data

**In plain words.** The Windows paging file is Linux **swap**: disk space (or compressed RAM) used when real RAM is full. In the GUI this item refreshes every 2 seconds.

**What you see.**
- Table `Swap Name`, `Type` (`partition` or `file`), `Size`, `Used`, `Priority` (higher is used first). "no swap active" if none.
- `Swap Total`, `Swap Free`, `Swap Cached` (pages in both RAM and swap), `Zswap pool` (compressed cache in RAM in front of disk swap).
- `Pages swapped in / out` since boot.
- `swappiness` (0-200: how willing the kernel is to swap rather than drop file cache), `page-cluster` (pages read per swap-in, as a power of 2), `watermark_scale_factor`, `min_free_kbytes` (RAM the kernel keeps free for emergencies).
- One line per zram device: disk size, compression algorithm, data stored, compressed size, real memory used, and compression ratio.
- `zswap.*` - each zswap setting (enabled, compressor, pool limit...).

**Healthy / typical values.** Author's PC: `/dev/zram0` 15.23GB at priority 100 (6.39GB used) and a 32.81GB disk swap partition at priority -1 (unused). Swappiness 150, page-cluster 0. zram uses zstd, ratio 3.63 (6.21GB of data in 1.75GB of RAM). zswap is off, which is right when zram is in use.

**Unusual values and what they suggest.** Disk swap in heavy use and "pages swapped in" climbing while you play: you are short of RAM, which causes stutter. Close programs or add RAM. zswap and zram both enabled: memory gets compressed twice for no gain. No swap at all: when RAM fills, the out-of-memory killer ends programs with no warning. Hibernation needs a disk swap at least as large as the RAM in use.

**Where Linux gets it.** `/proc/swaps`, `/proc/meminfo`, `/proc/vmstat`, `/proc/sys/vm/*`, `/sys/block/zram*/{comp_algorithm,disksize,mm_stat}`, `/sys/module/zswap/parameters`.

**Background.** **zram** is a RAM disk that compresses what is written to it. Used as swap, it lets more fit in memory. Swap **priority** decides the order: the higher one fills first.

---
## Credentials  
`passwords` - Windows-only (Linux equivalent shown)

**In plain words.** Windows has one Credential Manager. Linux has several **credential stores** (password keyrings). SRS only checks which ones exist and which agents are running. It never opens or reads them.

**What you see.**
- `Stores present` - a box and path for each: KWallet (KDE), GNOME Keyring, `pass`, GnuPG, SSH keys, KeePassXC config, Git credentials (`~/.git-credentials`).
- `Secret service / agent processes` - a box and PID for `kwalletd6`, `kwalletd5`, `gnome-keyring-daemon` (shown cut to `gnome-keyring-d`), `keepassxc`, `ssh-agent`, `gpg-agent`, counting only your own processes.

**Healthy / typical values.** Author's PC: KWallet, GNOME Keyring folder, GnuPG and SSH present. `kwalletd6` and `ssh-agent` running.

**Unusual values and what they suggest.** `~/.git-credentials` present: Git is storing passwords or tokens there as plain text. A keyring helper is safer. No secret-service process running on a desktop: browsers and chat apps keep asking for passwords, or save them unencrypted.

**Where Linux gets it.** Existence checks under `$HOME`; the process list in `/proc`.

---
## Pending file renames  
`pending` - Windows-only (Linux equivalent shown)

**In plain words.** Windows can queue file renames and deletes for the next reboot (often after an update). Linux replaces files right away. SRS answers the real question behind it: "is an update half-done, or does the system need a reboot or a config merge?"

**What you see.**
- `pacman lock` - "none", or `/var/lib/pacman/db.lck` present (a package transaction is running or crashed).
- `Running kernel modules` - the modules folder for the running kernel, or "missing - kernel was upgraded/removed, reboot pending".
- `Last full upgrade` - the last "starting full system upgrade" line from the pacman log.
- `Last pacman transactions` - the last 15 installed / upgraded / removed lines.
- `Unmerged config files` - every `.pacnew` / `.pacsave` under `/etc`.
- Note: updates are not checked, because that needs the network.

**Healthy / typical values.** Author's PC: no lock, modules folder present, last upgrade 2026-10-24, one unmerged file: `/etc/pacman.d/endeavouros-mirrorlist.pacnew`.

**Unusual values and what they suggest.** "missing - ... reboot pending" in red: the running kernel's modules were removed by an upgrade. New USB devices or filesystems may fail until you reboot. A lock file with no pacman running: a past update was interrupted. Make sure no pacman is running, then delete the lock and re-run the update. `.pacnew` files: the package shipped a new default config. Compare and merge it (for example with `pacdiff`), otherwise you may miss new settings.

**Where Linux gets it.** `/var/lib/pacman/db.lck`, `/usr/lib/modules/<release>`, `/var/log/pacman.log`, `find /etc -name '*.pacnew' -o -name '*.pacsave'`.

---
## ProLiant monitor service  
`plm-srv` - Not present on most PCs

**In plain words.** SIV checks for HP's ProLiant server monitoring service. Only HP ProLiant servers have it, and it has no Linux counterpart in SRS.

**What you see.** `Status - Not present on this system`, `Checked - n/a (HP ProLiant)`. That is the expected result on a desktop.

---
## Windows policies  
`policies` - Windows-only (Linux equivalent shown)

**In plain words.** Windows Group Policy sets rules for what users may do. On Linux the nearest pieces are **polkit** rules (who may do admin actions from the desktop without a password) and PAM (Pluggable Authentication Modules) resource limits.

**What you see.**
- `/etc/polkit-1/rules.d` - your local polkit rule files.
- `/usr/share/polkit-1/rules.d` - rule files shipped by packages.
- `/etc/security/limits.d` - extra limit files.
- Each folder shows "(empty)" or "not readable (root-only directory)" if so.
- `polkit action files` - how many actions apps have registered.
- `/etc/security/limits.conf` - each active (non-comment) line, or "(only comments)".

**Healthy / typical values.** Author's PC: local rules such as `10-udisks2.rules` and `49-nopasswd_global.rules`. Package rules include `50-default.rules` and `50-libvirt.rules`.

**Unusual values and what they suggest.** A rule like `49-nopasswd_global.rules`: admin actions are allowed without a password for some users. That is handy but weaker security, so make sure you meant it. A `limits.conf` line with a low `nofile` or `memlock` value: games, audio or Wine can fail.

**Where Linux gets it.** Directory listings of the polkit folders, `/usr/share/polkit-1/actions`; `/etc/security/limits.conf`.

---
## Memory pools  
`pools` - Linux data

**In plain words.** Windows reports paged and non-paged kernel pools. Linux has no pools by that name, so SRS shows the matching kernel and system memory totals, plus process, thread and handle counts. In the GUI it refreshes every second, and the Δ (delta) columns show the change since the last refresh.

**What you see.** Header: "Δ-time" since the last refresh and system up-time. Then a 3 x 3 grid of Description / Current / Δ:
- `Kernel Total` - Slab + KernelStack + PageTables (memory the kernel uses for itself).
- `Kernel Reclaimable` - slab memory the kernel can free on demand (mostly caches).
- `Kernel Unreclaim` - slab memory it cannot free.
- `Physical Total` - RAM the kernel manages.
- `Physical Available` - RAM that can be given to programs without swapping.
- `System Cache` - file cache (Cached + Buffers).
- `Commit Total` - memory promised to programs (Committed_AS).
- `Commit Limit` - the commit ceiling (RAM share + swap).
- `Shared (shmem)` - shared memory and tmpfs.
- Then `Process Count`, `Thread Count`, `Handle Count` (open file handles system-wide).

**Healthy / typical values.** Author's PC after 21 h: Kernel Total 3.66GB (2.56GB reclaimable), Physical 30.45GB with 19.82GB available, Commit 39.58GB of 63.26GB, 841 processes, 2194 threads, 31968 handles. A one-shot saved report always shows Δ 0 and Δ-time 00 00:00:00.

**Unusual values and what they suggest.** `Kernel Unreclaim` growing by hundreds of MB over hours: a kernel memory leak, often in a driver. `slabtop` shows which cache is growing. Commit Total above Commit Limit is allowed with default overcommit. But if Physical Available is also low, the out-of-memory killer is near.

**Where Linux gets it.** `/proc/meminfo`, `/proc/loadavg` (thread count), `/proc` (process count), `/proc/sys/fs/file-nr`.

**Background.** The **slab** is the kernel's own allocator for small objects (file records, network buffers). "Reclaimable" slab is cache it can drop when memory is needed.

---
## Power policy  
`power-policy` - Linux data

**In plain words.** The Windows power plan settings become the Linux CPU-frequency driver and governor, CPU idle settings, sleep modes and device power saving.

**What you see.**
- `CPU frequency (cpu0)`: `Scaling driver` (e.g. `amd-pstate-epp`, `intel_pstate`, `acpi-cpufreq`), `Governor`, `Available governors`, `Energy performance preference` (EPP), `Available EPP`, `Scaling min / max (kHz)` (the allowed range), `Hardware max (kHz)`.
- `Platform` (each shown only if present): `amd_pstate status` and `prefcore`, `Boost` (1 = turbo allowed), `intel_pstate status`, `cpuidle driver` and `governor`, `ACPI platform_profile` and its choices (laptop power modes), `Sleep states` (`freeze` = s2idle, `mem` = suspend, `disk` = hibernate), `mem_sleep` (which suspend is used, in brackets), `Hibernate (disk)` mode, `pm_async`, `PCIe ASPM policy` (Active State Power Management for PCI Express links), `USB autosuspend (s)` (-1 = off), `NMI watchdog` (a Non-Maskable Interrupt timer that detects hard CPU lockups), `laptop_mode`, `Wakeup sources armed`.
- `Power supplies` - each power-supply device with type, status and charge %, or "none (desktop on mains power, no battery)".

**Healthy / typical values.** Author's PC (tuned for gaming): `amd-pstate-epp`, governor and EPP `performance`, min and max both 5053000 kHz (the minimum is pinned to maximum), Boost 1, cpuidle `acpi_idle` with `teo`, `mem_sleep s2idle [deep]`, USB autosuspend -1. A wireless controller's battery appears as a power supply. A default desktop would show governor `powersave` with EPP `balance_performance` and a low minimum such as 400000-600000 kHz.

**Unusual values and what they suggest.** `Scaling max` well below `Hardware max`: something caps the CPU (a power tool, thermal daemon, or a laptop on battery). `Boost 0`: turbo is off, and single-thread speed will be much lower. `mem_sleep [s2idle]` on a desktop that wakes up warm or drains power: try `deep` if the firmware offers it. Driver `acpi-cpufreq` on a modern Ryzen: amd-pstate is not active. Check the BIOS CPPC (Collaborative Processor Performance Control) setting.

**Where Linux gets it.** `/sys/devices/system/cpu/cpu0/cpufreq/*`, `/sys/devices/system/cpu/{amd_pstate,intel_pstate,cpuidle,cpufreq}`, `/sys/firmware/acpi/platform_profile*`, `/sys/power/*`, `/sys/module/{pcie_aspm,usbcore}/parameters`, `/proc/sys/kernel/nmi_watchdog`, `/proc/sys/vm/laptop_mode`, `/sys/class/power_supply`.

**Background.** The **governor** decides how fast the CPU should run. **EPP** (Energy Performance Preference) is a hint to the CPU's own firmware about speed versus power. With amd-pstate-epp or intel_pstate in active mode, EPP does most of the work.

---
## Power scheme  
`power-scheme` - Linux data

**In plain words.** The Windows "active power scheme" becomes the profile chosen in power-profiles-daemon or tuned, and the governor and EPP actually set on every CPU.

**What you see.**
- `power-profiles-daemon` - current profile (`performance`, `balanced`, `power-saver`) or "not running".
- `tuned` - "Current active profile: ..." or "not running".
- `ACPI platform_profile` - if the firmware has one.
- `Governor` - one line per distinct value: "X on N of M CPUs".
- `EPP` - the same for energy preference.
- `Available profiles (powerprofilesctl list)` - the daemon's list. The active profile has `*`, and each shows the driver behind it.

**Healthy / typical values.** Author's PC: power-profiles-daemon `performance`, tuned `cachyos-gaming`, governor and EPP `performance` on 16 of 16 CPUs. Here tuned is the driver behind each power-profiles-daemon profile.

**Unusual values and what they suggest.** Two governor lines (for example `performance on 8 of 16` and `powersave on 8 of 16`): two tools are fighting, or a script set only some cores. Pick one power manager. Both daemons "not running": nothing manages power, and you get whatever the kernel default is.

**Where Linux gets it.** `powerprofilesctl get` / `list`, `tuned-adm active`, `/sys/firmware/acpi/platform_profile`, `/sys/devices/system/cpu/cpu*/cpufreq/{scaling_governor,energy_performance_preference}`.

---
## Privileges  
`priv` - Linux data

**In plain words.** Windows privileges (SeDebugPrivilege and so on) correspond to Linux **capabilities**: split-up pieces of root's power. A normal program has none of them in effect. This item shows SRS's own.

**What you see.**
- `User ... (uid euid gid egid)` - real and effective user and group IDs, and the program file.
- `NoNewPrivs` (1 = this process can never gain rights, even through setuid programs) and `Seccomp` (0 off, 1 strict, 2 filter = a system-call sandbox).
- Table of all 41 capabilities with five boxes:
  - `Effect` (effective) - in force right now.
  - `Permit` (permitted) - may be switched on.
  - `Inherit` (inheritable) - passed to programs it starts.
  - `Bound` (bounding set) - the upper limit; anything not here can never be gained.
  - `Ambient` - kept across starting normal programs.
- Capabilities in effect are shown in green.

**Healthy / typical values.** Author's PC as user: uid/euid 1000, NoNewPrivs 0, Seccomp 0. Every capability is ticked only under `Bound`, so the process has no special rights.

**Unusual values and what they suggest.** `Effect` ticks while running as a normal user: SRS was given file capabilities or started by a privileged service. Inside a Flatpak or container you may see `NoNewPrivs 1`, `Seccomp 2` and a reduced bounding set. That is the sandbox working.

**Where Linux gets it.** `/proc/self/status` (`CapEff`, `CapPrm`, `CapInh`, `CapBnd`, `CapAmb`, `NoNewPrivs`, `Seccomp`); `getuid()` and friends.

**Background.** Capabilities let a program do one root-only thing without full root. For example, `CAP_NET_BIND_SERVICE` lets a web server use port 80.

---
## Processes  
`processes` - Linux data

**In plain words.** Every running process, like Task Manager's Details tab.

**What you see.** One row per process, sorted by name:
- `Parent` - the PID of the process that started it, in brackets.
- `PID` - process ID.
- `Threads` - thread count.
- `State` - `R` running, `S` sleeping, `D` waiting on disk/device (uninterruptible), `Z` zombie (finished, parent hasn't collected it), `T` stopped, `I` idle kernel thread.
- `RSS` - RAM in use.
- `Account Name` - owner. root is shown in red.
- `Image Name` - program file name (or the process name if the program file can't be read).
- `Image Filename` - full program path, or `-` if hidden (other users' processes when not root, and kernel threads). The header also shows "Returned N of <pid_max>". pid_max is the highest PID number allowed, not a process limit.

**Healthy / typical values.** Author's PC: 841 processes. For example `akonadiserver` with 28 threads and 13.00MB, `Xwayland` with 8 threads and 10.94MB. Names in parentheses such as `(sd-pam)` are processes that renamed themselves.

**Unusual values and what they suggest.** Many `D` processes: something is stuck waiting on a disk, USB device or network share. A failing drive or dropped share is likely. Lots of `Z` zombies from one parent: that parent is buggy. A program running from `/tmp` or a hidden folder that you do not recognise: investigate.

**Where Linux gets it.** `/proc/<pid>/stat`, owner of `/proc/<pid>`, `readlink /proc/<pid>/exe`, `/proc/sys/kernel/pid_max`.

---
## Product IDs  
`product-ids` - Windows-only (Linux equivalent shown)

**In plain words.** Windows has a product ID tied to its licence. Linux has no licence keys, but it does have IDs for the installation, the current boot, and the hardware.

**What you see.**
- `Machine ID` - the unique ID of this installation (from `/etc/machine-id`). SRS shows only the first 8 characters and masks the rest.
- `Boot ID` - a random ID made fresh on every boot.
- `OS ID`, `Build ID`, `Image ID` - from `/etc/os-release`, where set.
- `System Vendor`, `Product Name`, `Product SKU` (Stock Keeping Unit, a model variant code) - from the firmware's DMI (Desktop Management Interface) tables.
- `Product Serial` - always masked. Without root data the line reads `Product Serial / UUID - Needs root: Menu > Run as root (srs-root)`.
- Note: "Linux has no product activation keys".

**Healthy / typical values.** Author's PC: OS ID `cachyos`, Build ID `rolling`, vendor `ASUS`, Product Name "System Product Name" and SKU "SKU". Those last two are the board maker's unfilled placeholder text, which is common on retail desktop boards.

**Unusual values and what they suggest.** Two PCs with the same Machine ID (common when a disk image was cloned): DHCP (Dynamic Host Configuration Protocol) servers, journald and some licence tools treat them as one machine. Make a new one with `systemd-machine-id-setup` after deleting the old file. Do not post your full machine ID online.

**Where Linux gets it.** `/etc/machine-id`, `/proc/sys/kernel/random/boot_id`, `/etc/os-release`, `/sys/class/dmi/id/*`.

---
## Product keys  
`products` - Windows-only (Linux equivalent shown)

**In plain words.** SIV can show Windows and Office product keys. Linux has no product keys, so SRS shows a stub.

**What you see.** `Status - Windows-only item in SIV`, `Linux equivalent - none`. That is expected.

---
## Font ranges  
`ranges` - Linux data

**In plain words.** Which Unicode characters one font contains, as blocks of code points. By default it checks your monospace (terminal) font.

**What you see.**
- `Font` - the family fontconfig picked; `File` - the font file; `Pattern` - what was asked for (default `monospace`).
- Table:
  - `Range` - row number.
  - `Start`, `End` - first and last code point, e.g. `U+0020` to `U+007E`.
  - `Glyphs` - how many code points the range covers (End - Start + 1).
  - `Characters` - a sample: the first printable character of the range.
- Footer: "N ranges, M characters supported. Open as ranges:<font name> for another font".
- Argument form: `ranges:DejaVu Sans` checks that font. Letters, digits, spaces and `_ . : - +` are allowed in the name.

**Healthy / typical values.** Author's PC: monospace is FiraCode Nerd Font Mono, 173 ranges, 12438 characters. It starts with `U+0020-U+007E` (95, basic Latin, sample `!`) and `U+00A0-U+017E` (223, sample `¡`), and ends with a 6896-character private-use block of Nerd Font icons. `ranges:DejaVu Sans` has `U+00A0-U+02E9` (586) as its second range.

**Unusual values and what they suggest.** A terminal font with only 1-3 ranges: box-drawing, accents or icons will fall back to another font and may look misaligned. Error "fc-match returned nothing": fontconfig is broken or has no fonts.

**Where Linux gets it.** `fc-match -f '%{family[0]} %{file} %{charset}' <pattern>`.

**Background.** A **code point** is Unicode's number for a character (`U+0041` is "A"). A font holds drawings (glyphs) for only some code points. Fontconfig fills the gaps from other fonts.

---
## RDP session info  
`rdp-info` - Linux data

**In plain words.** On Windows this shows Remote Desktop (RDP, Remote Desktop Protocol) sessions. On Linux, every login (local or remote) is a systemd-logind session. SRS lists them and checks the common remote-desktop and remote-login services.

**What you see.**
- Table per session: `SID` (session ID), `User Name`, `Type` (`wayland`, `x11`, `tty`, `unspecified`), `Class` (`user` = a real login, `greeter` = login screen, `manager` = the per-user service manager), `State` (`active`, `online`, `closing`), `Remote` (yes/no), `Remote Host`, `Service` (what created it: `sddm`, `sshd`, `login`, `systemd-user`), `TTY`.
- `Remote desktop / remote login services` - "active-state / enabled-state" for `sshd`, `xrdp`, `gnome-remote-desktop`, `wayvnc`, `x11vnc`, `vncserver@:1`, and KDE's `krdp` user service. Active ones are shown in orange so you notice them.

**Healthy / typical values.** Author's PC: session 1 `user wayland user active no - sddm-autologin tty3`, session 2 `user unspecified manager active no - systemd-user`. `sshd.service active / enabled`; all others `inactive / not-found`.

**Unusual values and what they suggest.** A session with `Remote yes` and a host you do not know: someone is logged in from another machine. Check it, and end it with `loginctl terminate-session <SID>` if needed. A remote-desktop service active that you never set up: turn it off.

**Where Linux gets it.** `loginctl list-sessions`, `loginctl show-session <id> -p Name -p Type ...`, `systemctl is-active` / `is-enabled`.

---
## Remote registry paths  
`reg-path` - Windows-only (Linux equivalent shown)

**In plain words.** SIV lists which registry paths remote users may read. Linux has no registry and no remote registry, so SRS shows a stub.

**What you see.** `Status - Windows-only item in SIV`, `Linux equivalent - none`. For remote administration services, see [Remote registry service](#remote-registry-service).

---
## Remote registry service  
`reg-srv` - Windows-only (Linux equivalent shown)

**In plain words.** The Windows Remote Registry service lets another PC read and change your settings. On Linux, the same risk comes from remote login and remote admin services. SRS checks the common ones.

**What you see.** Table `Unit`, `Active`, `Enabled` for `sshd.service`, `sshd.socket`, `cockpit.socket` (web admin console), `webmin.service`, `smb.service` (Samba file sharing), `nfs-server.service`, `libvirtd.service` (virtual machines, which can be managed remotely). Active units are shown in orange. Missing ones say "not installed" or `not-found`.

**Healthy / typical values.** Author's PC: `sshd`, `cockpit.socket`, `smb`, `nfs-server` and `libvirtd` are all active and enabled. That is a lot for a desktop, but each was chosen on purpose.

**Unusual values and what they suggest.** Anything active that you did not set up yourself. Each one is a door into the PC. Disable unused ones with `systemctl disable --now <unit>`, and check your firewall for the rest.

**Where Linux gets it.** `systemctl is-active` / `is-enabled`.

---
## Text converter render format  
`render-format` - Windows-only (Linux equivalent shown)

**In plain words.** SIV lists the Windows text-converter render formats (an Office/WordPad document-conversion feature). Linux has nothing comparable, so SRS shows a stub.

**What you see.** `Status - Windows-only item in SIV`, `Linux equivalent - none`. That is expected.

---
## Intel RST service  
`rst-srv` - Not present on most PCs

**In plain words.** Intel RST (Rapid Storage Technology) is Intel's Windows RAID (Redundant Array of Independent Disks) and NVMe (Non-Volatile Memory Express) caching driver and service. On Linux, the same disks are run by the md RAID driver (`mdadm`, which understands Intel's IMSM, Intel Matrix Storage Manager, format), and Intel VMD (Volume Management Device) by the `vmd` module. This item checks for those.

**What you see.**
- `Windows service` - "not applicable on Linux - showing the Linux analogue".
- Units `mdmonitor.service`, `mdadm.service` - active and enabled state.
- Modules `md_mod`, `raid0`, `raid1`, `raid456`, `dm_raid`, `vmd` - loaded / not loaded.
- `/proc/mdstat` - its contents (arrays and their health), or "not present".

**Healthy / typical values.** Author's PC (AMD, no RAID): all modules not loaded, `mdmonitor.service inactive static`, `/proc/mdstat` not present. On an Intel laptop with VMD turned on in the BIOS, `vmd` must be loaded or the NVMe drive is invisible.

**Unusual values and what they suggest.** In `/proc/mdstat`, `[U_]` instead of `[UU]`: one disk of a mirror has failed or dropped out. Replace it soon. An NVMe drive not found by the Linux installer on an Intel PC: VMD/RST is on in the BIOS. Switch the storage mode to AHCI (Advanced Host Controller Interface), or make sure `vmd` is in the initramfs.

**Where Linux gets it.** `systemctl`, `/sys/module/<name>`, `/proc/mdstat`.

---
## Scheduling  
`scheduling` - Linux data

**In plain words.** How the kernel shares CPU time between programs. Windows has one fixed scheduler with a few tweaks. Linux has many settings, can run alternative schedulers, and on newer kernels can load one as a BPF program (sched_ext).

**What you see.**
- `Kernel build` - the kernel's build string (shows the preemption model, e.g. `PREEMPT_DYNAMIC`).
- `sched_ext state` - `enabled` + the loaded scheduler's name, or `disabled`; then `switch_all` (1 = all tasks use it), `enable_seq` (how many times one was loaded), `nr_rejected`. If the kernel lacks it: "not available (CONFIG_SCHED_CLASS_EXT off)".
- `scx_loader.service / scx.service` - whether a sched_ext loader service is active.
- `Scheduler sysctls` - every `/proc/sys/kernel/sched_*` setting, plus `timer_migration` and `numa_balancing`. Which ones appear depends on the kernel and its patches.
- `This process`: `Policy` (SCHED_OTHER normal, SCHED_FIFO/RR real-time, SCHED_BATCH, SCHED_IDLE, SCHED_EXT), `Nice` (-20 high priority to 19 low), `Time slice (sched_rr_get_interval)`, `Autogroup` (the group and its nice value), `preempt=` from the command line.

**Healthy / typical values.** Author's PC: sched_ext enabled with `lavd_1.1.2...` (LAVD, the Latency-criticality Aware Virtual Deadline scheduler), switch_all 1, nr_rejected 0, `scx_loader active`. Sysctls include `sched_autogroup_enabled 1` and the BORE (Burst-Oriented Response Enhancer) patch set (`sched_bore 1`, `sched_burst_*`). This process: SCHED_OTHER, nice -4 (set by a priority daemon), `preempt=full`. A stock kernel shows sched_ext disabled and fewer `sched_*` entries. The time slice reads 0.000 ms when a sched_ext scheduler handles normal tasks. Under the default scheduler it shows a few ms.

**Unusual values and what they suggest.** `nr_rejected` above 0: the sched_ext scheduler refused tasks. `enable_seq` climbing while you are not switching schedulers: the BPF scheduler keeps crashing and the kernel falls back to the default. Check `journalctl -k` for "sched_ext". Programs at SCHED_FIFO that you did not set: a misbehaving one can freeze the desktop.

**Where Linux gets it.** `uname`, `/sys/kernel/sched_ext/*`, `systemctl is-active`, `/proc/sys/kernel/sched_*`, `sched_getscheduler()`, `getpriority()`, `sched_rr_get_interval()`, `/proc/self/autogroup`, `/proc/cmdline`.

**Background.** **Nice** is a process's politeness: higher nice means it gives way to others. **Preemption** decides how quickly the kernel can interrupt a running task. `full` gives the lowest latency (good for games and audio).

---
## Driver services  
`services-drv` - Linux data

**In plain words.** Windows lists kernel drivers as "driver services" that are running or stopped. On Linux, each bus (PCI, USB, HID = Human Interface Device, I2C = a two-wire chip-to-chip bus...) has registered drivers. A driver is "Running" when it is bound to at least one device.

**What you see.**
- `Bus` - the bus type (`pci`, `usb`, `hid`, `i2c`, `platform`, `hdaudio`, `cpu`...).
- `Driver Name`.
- `Devices` - how many devices it is bound to.
- `State` - `Running` (1 or more devices) or `Stopped` (registered, no device).
- `Module` - the module that provides it, or `(built-in)`.
- Footer: "N drivers, M bound to devices (Running)".

**Healthy / typical values.** Author's PC: 191 drivers, 63 running. `cpu processor 16 Running (built-in)`, `hid hid-generic 8 Running`, `hid razermouse 3 Running`, `hdaudio snd_hda_codec_alc882 1 Running`. Many built-in drivers for chips this PC does not have show `0 Stopped`, which is normal.

**Unusual values and what they suggest.** The driver for a device you own shows `Stopped` and the device does not work: it is bound to another driver, or the driver failed to probe it. Check `lspci -k` / `lsusb -t` and `journalctl -k`. Two drivers for the same hardware both registered (e.g. `nouveau` and `nvidia`): only one should be bound.

**Where Linux gets it.** `/sys/bus/*/drivers/*` (bound devices are the links inside), `/sys/bus/*/drivers/*/module`.

---
## Win32 services  
`services-w32` - Linux data

**In plain words.** Windows services are systemd **service units** on Linux. There are two sets: system services (run as root or service accounts) and user services (run inside your login).

**What you see.** Two tables, `System services` then `User services`, each with:
- `Service Name` - the unit name.
- `Load` - `loaded`, `not-found` (referenced but no file), `masked` (blocked).
- `Active` - `active`, `inactive`, `failed` (red), `activating`.
- `State` - the detail: `running`, `exited` (a one-shot job that finished), `dead`, `failed`.
- `Description`.
- Footer for each: "N units, A active, F failed".

**Healthy / typical values.** Author's PC: 291 system units (123 active, 1 failed), 156 user units (97 active, 0 failed). Examples: `bluetooth.service loaded active running`, `alsa-restore.service loaded active exited`, `auditd.service masked inactive dead`.

**Unusual values and what they suggest.** Any `failed` unit: read why with `systemctl status <unit>` and `journalctl -u <unit> -b`. On the author's PC a scheduled rootkit-check job is failed. `not-found` units: something still refers to a service that was uninstalled. Usually harmless, but it can slow boot if it is waited for.

**Where Linux gets it.** `systemctl list-units --type=service --all` (and `--user`).

**Background.** `inactive dead` is normal for a service that only starts on demand or on a timer. It is not an error.

---
## Sessions  
`sessions` - Linux data

**In plain words.** Who is logged in, how, and from where: local logins, SSH (Secure Shell) connections and Samba (Windows file sharing) clients.

**What you see.**
- `Login sessions (loginctl list-sessions)` - `Session`, `UID`, `User`, `Seat` (`seat0` = this PC's screen and keyboard), `Leader` (PID that started it), `Class`, then `TTY / Idle / Since`.
- `Users with sessions (loginctl list-users)` - UID, name, linger (yes = their services keep running after logout), state.
- `Inbound SSH connections` - established TCP connections to port 22, or "none".
- `SMB sessions (smbstatus -b)` - from the srs-root cache, or "Needs root: Menu > Run as root (srs-root)".

**Healthy / typical values.** Author's PC: session 1 on `seat0` `tty3` (the desktop), session 2 with class `manager` (the user service manager), user 1000 `no active`, no SSH connections. Samba shows its version and empty client tables.

**Unusual values and what they suggest.** An SSH connection or SMB client you do not recognise. Several stale `closing` sessions for your user: a program is still running after a logout, often a leftover background app.

**Where Linux gets it.** `loginctl`, `ss -Htn state established '( sport = :22 )'`, `smbstatus -b` (run by srs-root).

---
## Setup directories  
`setup` - Linux data

**In plain words.** Windows lists its setup and install folders. The Linux version: what is in `/boot`, where the EFI System Partition is mounted, and the package manager's own folders.

**What you see.**
- `Boot Dir /boot`, then each kernel (`vmlinuz-*`), initramfs (`initramfs-*`), microcode (`*-ucode.img`), and the `grub`, `EFI` or `loader` folders.
- `EFI System Partition` - where the ESP (EFI System Partition) is mounted (`/efi`, `/boot/efi` or `/boot`), or "not mounted".
- `Shared Dir` - `/usr/share`.
- pacman settings from `/etc/pacman.conf`, if set: `RootDir`, `DBPath`, `CacheDir`, `LogFile`, `GPGDir`, `HookDir`, `Architecture`, `ParallelDownloads`.
- `Installed packages` - count.
- `Last full upgrade` - from the pacman log.

**Healthy / typical values.** Author's PC: `/boot` holds three kernels with their initramfs and fallback images, plus `amd-ucode.img` and `grub`. ESP on `/boot/efi`, Architecture `auto`, ParallelDownloads 10, 13538 packages.

**Unusual values and what they suggest.** An initramfs older than its kernel, or a kernel with no initramfs: the image was not rebuilt after an update, and that kernel may not boot. Run `mkinitcpio -P` (or your distro's tool). No microcode image on a GRUB system: CPU bug fixes from AMD or Intel are not loaded early. ESP "not mounted": boot-loader updates cannot be written.

**Where Linux gets it.** Directory listing of `/boot`, `/proc/mounts`, `/etc/pacman.conf`, `pacman -Qq`, `/var/log/pacman.log`.

---
## Setup API logging  
`setup-api` - Windows-only (Linux equivalent shown)

**In plain words.** Windows SetupAPI logs every device-driver install. On Linux, udev handles new devices using rule files, and logs to the journal. SRS shows its log level and rule counts.

**What you see.**
- `udev_log (/etc/udev/udev.conf)` - the log level, or "default (info)".
- `/etc/udev/rules.d` - your local rule files (count).
- `/usr/lib/udev/rules.d` - rule files from packages (count).
- `/run/udev/rules.d` - temporary rules made at runtime (count).
- `udevadm version`.

**Healthy / typical values.** Author's PC: default log level, 41 local rules, 290 package rules, 1 runtime rule, udev 262.

**Unusual values and what they suggest.** Many local rules: each one can rename, re-permission or run a script for a device. If a device behaves oddly, look here first. To watch devices being set up live, use `udevadm monitor`.

**Where Linux gets it.** `/etc/udev/udev.conf`, directory counts, `udevadm --version`.

---
## Driver signing  
`signing` - Linux data

**In plain words.** Like Windows driver signing: are kernel modules signed, and does the kernel insist on it?

**What you see.**
- `module.sig_enforce` - `Y` (unsigned modules refused), `N` (allowed, but taint the kernel), or `-` (signing not built into the kernel).
- `Loaded modules` - count.
- `Unsigned (E)`, `Out-of-tree (O)`, `Proprietary (P)` - how many loaded modules carry each taint letter.
- `Tainting modules` - each module with its taint letters.
- `Kernel signing configuration` - the `CONFIG_MODULE_SIG*`, `CONFIG_SYSTEM_TRUSTED_KEYS` and `CONFIG_MODULE_HASHES` lines from the running kernel's config.
- Argument form `signing:<module>`: `Module`, `Signer` (or "(unsigned)"), `Hash` algorithm.

**Healthy / typical values.** Author's PC: `sig_enforce N`, 237 modules, 10 unsigned, 10 out-of-tree, 0 proprietary. The tainting ones are out-of-tree extras such as `ryzen_smu`, `vboxdrv`, `vmmon`, `zenergy`, each `OE`. Kernel config: `CONFIG_MODULE_SIG=y`, `CONFIG_MODULE_SIG_ALL=y`, SHA-512. `signing:amdgpu` shows `Signer: Build time autogenerated kernel key`, `Hash: sha512`.

**Unusual values and what they suggest.** With Secure Boot on and lockdown active, unsigned modules (DKMS builds such as VirtualBox) will not load. Sign them with your own MOK (Machine Owner Key), or turn enforcement off. `Proprietary (P)` above 0: a closed driver is loaded.

**Where Linux gets it.** `/sys/module/module/parameters/sig_enforce`, `/proc/modules` (taint column), `/proc/config.gz`, `modinfo -F signer` / `sig_hashalgo`.

**Background.** "Build time autogenerated kernel key" means the kernel builder made a one-time key and signed all of that kernel's own modules with it. Modules built later (DKMS) cannot be signed with that key.

---
## Installed software  
`software` - Linux data

**In plain words.** Windows "Programs and Features" becomes the package manager's list. SRS lists every pacman package and every Flatpak app.

**What you see.**
- Table `Package`, `Version`, `Origin`. `repository` = from a configured repo. `foreign (AUR/local)` = not in any configured repo (built from the AUR, the Arch User Repository, installed from a file, or dropped from its repo).
- Footer: "N pacman packages (E explicitly installed, F foreign)".
- `Flatpak (flatpak list)` - application ID, version, branch, installation (`system` or `user`), or "none".

**Healthy / typical values.** Author's PC: "13538 pacman packages (7211 explicitly installed, 3585 foreign)", and a long Flatpak list (`... stable system`). A typical desktop has 800-1500 packages.

**Unusual values and what they suggest.** A package marked foreign that you thought came from the repos: it was dropped from the repo and will never get updates, so look for a replacement. A large foreign count: each AUR package has to be rebuilt by you when its libraries change.

**Where Linux gets it.** `pacman -Q`, `pacman -Qqm` (foreign), `pacman -Qqe` (explicit), `flatpak list`.

---
## Startup programs  
`startup` - Linux data

**In plain words.** Like the Windows Startup folder and Task Manager's Startup tab: what starts when you log in, and which systemd units are enabled at boot.

**What you see.**
- `XDG autostart (user) ~/.config/autostart` and `XDG autostart (system) /etc/xdg/autostart` - each `.desktop` file with a box (ticked = will start), the file name, the app name, and its command. Notes in brackets:
  - `[KDE;GNOME;...]` - `OnlyShowIn`: it starts only on those desktops. The box is empty if yours is not listed.
  - `[overridden by user file]` - a file with the same name in your own folder replaces the system one.
  - The box is also empty for `Hidden=true` or `X-GNOME-Autostart-enabled=false`.
- `Plasma login scripts ~/.config/plasma-workspace/env` - scripts KDE runs before the desktop starts, or "(none)".
- `Enabled systemd units (system)` and `(user)` - every enabled unit file with its state and preset.

**Healthy / typical values.** Author's PC: user autostart for CoolerControl, conky, JamesDSP, SRS's own tray (`srs-gui --gui-tray`) and others. System autostart includes `baloo_file` `[KDE;GNOME;Unity;XFCE]` (ticked under KDE) and `at-spi-dbus-bus` `[GNOME;Unity;]` (empty under KDE). User timers include `arch-update.timer`.

**Unusual values and what they suggest.** A ticked entry you do not recognise: check its command. Malware and adware often hide in `~/.config/autostart`. Two entries starting the same tray app: you get two icons. Slow logins: look for `sleep N &&` commands and heavy apps here.

**Where Linux gets it.** `~/.config/autostart/*.desktop`, `/etc/xdg/autostart/*.desktop`, `~/.config/plasma-workspace/env`, `systemctl list-unit-files --state=enabled` (and `--user`).

---
## System  
`system` - Linux data

**In plain words.** The overview panel: what Linux you run, on what hardware, since when, and how the desktop is set up. It is the Linux version of SIV's Windows panel. The main-window **Linux** button opens it, and [Windows (OS) panel](#windows-os-panel) shows the same page.

**What you see.**
- Top block (four lines):
  - Distribution name, version (or "Build rolling"), kernel release and architecture.
  - `ID` and `ID_LIKE` from os-release, the kernel build string, and the `Hostname`.
  - `InstallDate` - the oldest believable install time, with its Unix time and where it came from: the pacman install date of the `filesystem` or `base` package, the `/etc/machine-id` file time, or the first line of the pacman log, whichever is oldest.
  - `Product Type Linux`, `Init` (the systemd version, or "unknown (not systemd)") and `Chassis` (Desktop, Laptop, Notebook, Mini Tower... from SMBIOS, the System Management BIOS tables).
- `Linux Desktop Session` - desktop name, session type (`wayland`/`x11`), and the `WAYLAND_DISPLAY` / `DISPLAY` values.
- `Root Directory /`, `System Directory /usr/bin`, `Library Directory /usr/lib`, `Boot Directory /boot` - the Linux counterparts of `C:\`, `System32` and so on.
- `Kernel Command Line`.
- One line per logical CPU: `[CPU-n vendor microcode]`, model name, maximum clock, and Family / Model / Stepping.
- Power line: "Mains Power" or "Battery Power", and "Battery Present" or "No Battery". Then `Time Zone` with abbreviation and offset from UTC (Coordinated Universal Time), the `Clocksource`, and the timer `Resolution`.
- `Up-time` (days hh:mm:ss), `Booted` (date and time), `Load` (the raw `/proc/loadavg`: 1, 5 and 15-minute load averages, running/total threads, last PID).
- `User` (name and uid), `Locale`, `Shell`.
- `System BIOS Date` - BIOS date, vendor, version, and `Firmware UEFI` or `Legacy BIOS`.
- One block per connected monitor: `Primary Desktop Display #1` / `Desktop Display #n` with connector name, `enabled`/`disabled`, the preferred mode, and its `Kernel driver`.
- Footer: the board's maker, model and BIOS.

**Healthy / typical values.** Author's PC: "CachyOS Build rolling Kernel 7.3.0-273-tkg-bore-llvm x86_64", ID `cachyos` like `arch`, Hostname X3D, installed 2026-03-09, systemd 262, Chassis Desktop. KDE on Wayland. 16 lines of `AMD Ryzen 7 7800X3D 8-Core Processor 5.05GHz (Family 25 Model 97 Stepping 2)` with microcode `0xa60120c`. Time zone America/New_York (EDT -0400), clocksource tsc, resolution 1 ns. BIOS 09/18/2026 American Megatrends 1805, UEFI. Displays `DP-3` 2560x1440 and `HDMI-A-1` 1920x1080, both on `amdgpu`.

**Unusual values and what they suggest.**
- Microcode differs between CPU lines: a microcode update reached only some cores. This is rare; update the BIOS or the `*-ucode` package.
- Maximum clock far below the CPU's rated boost: boost is off or the CPU frequency driver did not load. See [Power policy](#power-policy).
- `Clocksource hpet` or `acpi_pm` instead of `tsc`: the kernel judged the TSC unstable. Timing and game frame pacing get worse. See [System timers](#system-timers).
- "Battery Present" on a desktop: a wireless mouse, headset or controller reports its battery as a power supply. That is harmless.
- A monitor's `Kernel driver` reads `simpledrm` or `efifb`: the real GPU driver did not load.

**Where Linux gets it.** `/etc/os-release`, `uname()`, `gethostname()`, `/var/lib/pacman/local/*/desc`, `/etc/machine-id` time, `/var/log/pacman.log`, `systemctl --version`, `/sys/class/dmi/id/{chassis_type,bios_*}`, environment, `/proc/cmdline`, `/proc/cpuinfo`, `/sys/devices/system/cpu/cpu*/cpufreq/cpuinfo_max_freq`, `/sys/class/power_supply`, `/etc/localtime`, `/proc/uptime`, `/proc/stat` (btime), `/proc/loadavg`, `/sys/class/drm/card*-*/{status,enabled,modes}`.

**Background.** **Load average** is the average number of tasks running or waiting to run. On a 16-thread CPU, a load of 3.6 means the CPU is mostly idle. A load steadily above 16 means work is queuing.

---
## System timers  
`sys-tim` - Linux data

**In plain words.** Which hardware clock the kernel uses to keep time, how fine its timers are, and how the timer tick is set up. Windows exposes a little of this (the timer resolution); Linux shows it all.

**What you see.**
- `Current clocksource` (green) and `Available clocksources` - e.g. `tsc hpet acpi_pm`.
- `hrtimer resolution` - high-resolution timer precision (1 ns is normal).
- `Coarse (tick) resolution` - one timer tick (1.000 ms = 1000 Hz kernel; 4.000 ms = 250 Hz).
- `timer_migration` - 1 = timers may move to busy CPUs so idle ones can sleep.
- `TSC flags` - which of `tsc`, `constant_tsc` (rate does not change with CPU speed), `nonstop_tsc` (keeps counting in deep sleep), `tsc_deadline_timer` the CPU reports.
- `Clock event devices` - the per-CPU timer device (e.g. "lapic 16 CPU clockevent devices"), then the `broadcast` device used when CPUs sleep deeply.
- `Kernel timer configuration` - `CONFIG_HZ`, `CONFIG_NO_HZ*`, `CONFIG_HIGH_RES_TIMERS`, `CONFIG_TICK_*`, `CONFIG_HPET*` lines from the running kernel.
- `nohz / tsc on cmdline` - shows the kernel command line if it contains `nohz` or `tsc=` options, otherwise `<none>`.

**Healthy / typical values.** Author's PC: clocksource `tsc` (available `tsc hpet acpi_pm`), 1 ns hrtimers, 1.000 ms tick (`CONFIG_HZ=1000`), flags `tsc constant_tsc nonstop_tsc`, `lapic` on all 16 CPUs, broadcast `hpet`, `NO_HZ_IDLE`. The command line contains `tsc=reliable clocksource=tsc`.

**Unusual values and what they suggest.** Current clocksource `hpet` on a modern CPU: the kernel found the TSC unreliable (look for "clocksource: Marking clocksource 'tsc' as unstable" in `journalctl -k`). Reading the time becomes much slower, and games and benchmarks suffer. Overclocking or BIOS bugs can cause it. Forcing `tsc=reliable` hides the symptom but not the cause.

**Where Linux gets it.** `/sys/devices/system/clocksource/clocksource0/*`, `clock_getres()`, `/proc/sys/kernel/timer_migration`, `/proc/cpuinfo` flags, `/sys/devices/system/clockevents/*`, `/proc/config.gz`, `/proc/cmdline`.

**Background.** The **TSC** (Time Stamp Counter) is a counter inside the CPU that ticks at a fixed rate. It is the fastest clock to read. HPET (High Precision Event Timer) and ACPI PM timer are slower chipset clocks kept as backups.

---
## System windows  
`sys-win` - Windows-only (Linux equivalent shown)

**In plain words.** SIV lists every top-level window Windows knows about. On Linux under X11 or XWayland, SRS can list windows with `wmctrl`. Native Wayland apps cannot be listed: Wayland deliberately gives programs no global window list.

**What you see.**
- A header row and one line per window from `wmctrl -l -G -x`: `Window ID`, `Desk` (virtual desktop, -1 = all), `X`, `Y`, `W`, `H` (position and size), `Class` (app class), `Host`, `Title`.
- Note: native Wayland clients are not listed.
- Without `DISPLAY` set: a note that wmctrl needs X11/XWayland.

**Healthy / typical values.** Author's PC (KDE Wayland): one window, conky's (`Conky.Conky`), because conky is the only X11 app running. Everything else is native Wayland.

**Unusual values and what they suggest.** "wmctrl returned nothing" with X11 apps open: the `wmctrl` package is not installed.

**Where Linux gets it.** `wmctrl -l -G -x`.

---
## Thread windows  
`tid-win` - Windows-only (Linux equivalent shown)

**In plain words.** SIV lists windows by the thread that owns them. On Linux, SRS lists X11/XWayland windows with the process that owns them.

**What you see.** `Window ID`, `PID`, `Process` (the process name from `/proc/<pid>/comm`), `Title`. The same Wayland limit as [System windows](#system-windows) applies.

**Healthy / typical values.** Author's PC: `0x01600002 0 - conky (MYPC)`. PID 0 and `-` mean the window did not publish its process ID (`_NET_WM_PID`). Some lightweight apps, conky among them, don't.

**Where Linux gets it.** `wmctrl -l -p`, `/proc/<pid>/comm`.

---
## Time  
`time` - Linux data

**In plain words.** The current time in every useful form, time-zone and NTP (Network Time Protocol) sync status, and the motherboard's real-time clock (RTC).

**What you see.**
- `Local Time`, `UTC`, `Epoch` (seconds since 1970-01-01 UTC).
- `Time Zone`, `Abbreviation / Offset` (e.g. `EDT UTC-0400 daylight saving time`).
- `Day of year / ISO week`.
- `timedatectl show` - `Timezone`, `LocalRTC` (no = the RTC keeps UTC), `CanNTP`, `NTP` (sync on), `NTPSynchronized`, `TimeUSec`, `RTCTimeUSec`.
- `Real-time clock (/sys/class/rtc/rtc0)` - `name`, `date`, `time`, `since_epoch`, `hctosys` (1 = system time was set from this clock at boot), `max_user_freq`.
- `System Up-time`, `Boot Time`.

**Healthy / typical values.** Author's PC: America/New_York, EDT, `LocalRTC no`, `NTP yes`, `NTPSynchronized yes`, RTC `rtc_cmos`, with RTC time and UTC matching to the second.

**Unusual values and what they suggest.** `LocalRTC yes`: the clock stores local time, usually for Windows dual-boot. It causes one-hour jumps at daylight-saving changes. Setting Windows to UTC instead is better. RTC time far from UTC right after boot, or a date in 2000 or so: the CMOS battery is flat. `NTPSynchronized no` for a long time: no network time. Check `systemd-timesyncd` or chrony.

**Where Linux gets it.** `time()`, `localtime_r()`, `/etc/localtime` or `$TZ`, `timedatectl show`, `/sys/class/rtc/rtc0/*`, `/proc/uptime`, `/proc/stat`.

---
## Times  
`times` - Linux data

**In plain words.** All of Linux's clocks side by side, and how well network time sync is doing.

**What you see.**
- `Clock` table, each with value in seconds and resolution:
  - `CLOCK_REALTIME` - wall-clock time (can jump when set).
  - `CLOCK_MONOTONIC` - time since boot, never jumps, stops during suspend.
  - `CLOCK_MONOTONIC_RAW` - the same without NTP speed correction.
  - `CLOCK_BOOTTIME` - like monotonic, but keeps counting during suspend.
  - `CLOCK_TAI` - International Atomic Time (no leap seconds).
  - `CLOCK_PROCESS_CPUTIME_ID` / `CLOCK_THREAD_CPUTIME_ID` - CPU time used by SRS.
  - `CLOCK_REALTIME_COARSE` / `CLOCK_MONOTONIC_COARSE` - cheaper, tick-resolution versions.
- `Time spent suspended` - BOOTTIME minus MONOTONIC.
- `/proc/uptime (up idle)` - seconds up, and total idle CPU-seconds across all CPUs.
- `This process user / system` - CPU time SRS has spent in its own code and in the kernel.
- `Time synchronisation` - every `timedatectl show-timesync` property (configured, fallback and current NTP servers, poll intervals, the last NTP message), then the `timesync-status` summary: server, poll interval, leap, stratum, precision, offset, delay, jitter, packet count. If timesyncd is not used: "systemd-timesyncd not running".

**Healthy / typical values.** Author's PC: all fine clocks at 1 ns, coarse ones at 1000000 ns (1 ms). 0 s suspended. Server `time.cloudflare.com`, stratum 3, offset +113us, jitter 883us.

**Unusual values and what they suggest.** NTP offset of many milliseconds that keeps growing: the sync server is unreachable or the PC's clock crystal drifts badly. MONOTONIC_RAW running ahead of or behind MONOTONIC by a lot over a day: NTP is correcting a fast or slow crystal, which is normal in small amounts.

**Where Linux gets it.** `clock_gettime()` / `clock_getres()`, `/proc/uptime`, `getrusage()`, `timedatectl show-timesync --all`, `timedatectl timesync-status`.

**Background.** NTP **stratum** is how far a server is from a reference clock. Stratum 1 is connected to an atomic or GPS clock, and stratum 3 is two steps away.

---
## Users  
`users` - Linux data

**In plain words.** The accounts on this PC: real people first, then the system accounts that services run as, then the account rules.

**What you see.**
- Table of human accounts (UID at or above `UID_MIN`, not `nobody`): `User Name` (yours is green), `UID`, `GID` (main group), `Full Name` (first field of the comment), `Home`, `Shell`.
- `System accounts (N, UID < min or nobody)` - a comma list of every service account.
- `Account policy (/etc/login.defs)` - `UID_MIN`, `UID_MAX`, `SYS_UID_MAX`, `GID_MIN`, `PASS_MAX_DAYS` / `PASS_MIN_DAYS` / `PASS_WARN_AGE` (password ageing), `ENCRYPT_METHOD` (password hash type), `UMASK` (default permissions for new files), `HOME_MODE`, `USERGROUPS_ENAB`.
- Note: `/etc/shadow` (the password hashes) is never read.

**Healthy / typical values.** Author's PC: `user 1000 1000 ... /home/user /bin/fish`, one test account, and 215 system accounts (`root`, `daemon`, `systemd-*`, `polkitd`, `sddm`...). Policy: `UID_MIN 1000`, `ENCRYPT_METHOD YESCRYPT`, `UMASK 022`, `HOME_MODE 0700`.

**Unusual values and what they suggest.** A human-range account you did not create: check where it came from (some packages create one). A system account with a real login shell (`/bin/bash` instead of `/usr/bin/nologin`) is a common hardening issue, but this table only shows shells for human accounts. `ENCRYPT_METHOD MD5` or `DES`: weak password hashing. Change it to `YESCRYPT` or `SHA512`.

**Where Linux gets it.** `/etc/passwd`, `/etc/login.defs`.

**Background.** A **UID** (user ID) is the number the kernel really uses for an account. The name is just a label. UIDs below 1000 are reserved for system services on most distributions.

---
## Driver verifier  
`verifier` - Windows-only (Linux equivalent shown)

**In plain words.** Windows Driver Verifier stress-tests drivers to catch bugs. Linux has no single switch for this. Its checkers are kernel build options (KASAN, UBSAN, KFENCE, lockdep...), and the results end up in the taint flags. SRS shows both.

**What you see.**
- The same taint table as [Code integrity](#code-integrity): `Tainted N (0xHEX)` and bits 0-18.
- `Debug/checker options built into the running kernel` - any of `CONFIG_KASAN` (memory-error detector), `UBSAN` (undefined-behaviour checks), `KFENCE` (low-cost sampling memory checker), `DEBUG_PAGEALLOC`, `PROVE_LOCKING`/`LOCKDEP` (lock-order checks), `DEBUG_KMEMLEAK`, `SLUB_DEBUG`, `DEBUG_LIST`, `DEBUG_OBJECTS`, `FAULT_INJECTION`, `KCSAN` (data-race detector), `DEBUG_VM`, `HARDENED_USERCOPY`, `INIT_ON_ALLOC_DEFAULT_ON`, `INIT_ON_FREE_DEFAULT_ON`.
- `kfence.sample_interval` - if KFENCE is present (ms between samples; 0 = off).
- `slub_debug on cmdline` - yes / no.

**Healthy / typical values.** Author's PC: Tainted 0x3044 (S, U, O, E), only `CONFIG_SLUB_DEBUG=y` built in (available but off unless turned on at boot), `slub_debug` no. Distribution kernels usually also have KFENCE and HARDENED_USERCOPY.

**Unusual values and what they suggest.** If you are chasing a driver crash, a kernel with KASAN or `slub_debug` turned on will catch the bad access at the moment it happens. These options slow the system, so use them only for debugging.

**Where Linux gets it.** `/proc/sys/kernel/tainted`, `/proc/config.gz`, `/sys/module/kfence/parameters/sample_interval`, `/proc/cmdline`.

---
## Virtual memory  
`virtual` - Linux data

**In plain words.** The system's virtual-memory rules (how much memory may be promised, overcommit, huge pages) and SRS's own address-space use and limits.

**What you see.**
- `System`:
  - `Commit Limit`, `Committed` - the commit ceiling and what has been promised so far.
  - `Vmalloc Total`, `Vmalloc Used` - kernel virtual address space (the total is huge on 64-bit; used is the real figure).
  - `Anonymous Pages`, `Anon Huge Pages` - program memory not backed by files, and how much of it is in 2 MB transparent huge pages.
  - `HugePages Total / Free (size)` - reserved huge pages.
  - `overcommit_memory` (0 heuristic, 1 always allow, 2 strict), `overcommit_ratio`, `overcommit_kbytes`, `max_map_count` (memory regions allowed per process), `mmap_min_addr`, `randomize_va_space` (ASLR, 2 = full), `THP enabled` (`always` / `madvise` / `never` in brackets).
- `This process` - SRS's `Vm*` and `Rss*` lines (see [Memory heap](#memory-heap)), then `RLIMIT_AS`, `RLIMIT_DATA`, `RLIMIT_STACK`, `RLIMIT_MEMLOCK` with soft and hard values.

**Healthy / typical values.** Author's PC: Commit 39.54GB of 63.26GB, Vmalloc Total 32.00TB, Anon Huge 2.17GB, `overcommit_memory 0`, `max_map_count 2147483642` (raised for games), THP `[always]`, stack limit 8MB, memlock unlimited.

**Unusual values and what they suggest.** `max_map_count` at the old default 65530: some games (many Proton titles) crash when they run out of regions. Many distributions now ship 1048576 or higher. `overcommit_memory 2` with a low ratio: programs get "out of memory" errors even with free RAM. A small `RLIMIT_MEMLOCK` (8 MB): some audio, RDMA and emulator tools fail.

**Where Linux gets it.** `/proc/meminfo`, `/proc/sys/vm/*`, `/proc/sys/kernel/randomize_va_space`, `/sys/kernel/mm/transparent_hugepage/enabled`, `/proc/self/status`, `getrlimit()`.

**Background.** **Overcommit** means the kernel promises programs more memory than it really has, betting they will not all use it at once. THP (Transparent Huge Pages) groups memory into 2 MB pages so the CPU spends less time looking up addresses.

---
## Visual Studio paths  
`vs-paths` - Windows-only (Linux equivalent shown)

**In plain words.** SIV shows where Visual Studio is installed. On Linux, SRS shows where each common developer tool is found on your `PATH`, and what it really points to.

**What you see.** `Tool` - `Path` (first match on `PATH`, or `<not found>`) - `Resolves To` (the real file if `Path` is a link; blank if not). Tools checked: `cc`, `gcc`, `g++`, `clang`, `clang++`, `ld`, `make`, `cmake`, `ninja`, `meson`, `rustc`, `cargo`, `go`, `python3`, `node`, `java`, `dotnet`, `gdb`, `lldb`, `code`, `codium`, `x86_64-w64-mingw32-gcc` (Windows cross-compiler).

**Healthy / typical values.** Author's PC: `cc -> /usr/bin/gcc`, `clang -> /usr/bin/clang-23`, `rustc` in `~/.cargo/bin` resolving to `rustup`, `python3 -> python3.14`, `java -> /usr/lib/jvm/java-21-openjdk/bin/java`.

**Unusual values and what they suggest.** A tool found in an unexpected place first (e.g. `~/.local/bin/gcc` or a Homebrew copy) shadows the system one, and builds may use the wrong version. `<not found>` for a tool a build needs: install it or fix `PATH`.

**Where Linux gets it.** A walk over `$PATH` with `access(X_OK)`, then `realpath()`.

---
## Dr Watson / crash debugger  
`watson` - Linux data

**In plain words.** Dr Watson was Windows' crash catcher. On Linux, when a program crashes the kernel can write a **core dump** (a snapshot of its memory). systemd-coredump normally catches it. This item shows how that is set up.

**What you see.**
- `core_pattern` - what happens to a core dump. `|/usr/lib/systemd/systemd-coredump ...` means it is piped to systemd-coredump. A plain file name means a `core` file is written in the program's folder.
- `core_pipe_limit`, `core_uses_pid`, `print-fatal-signals` (log every fatal signal), `panic_on_oops` (1 = a kernel bug reboots the PC), `panic` (seconds before auto-reboot after a kernel panic; 0 = never).
- `fs.suid_dumpable` - core dumps for setuid programs (0 off, 1 on, 2 root-only/safe).
- `RLIMIT_CORE (this process)` - maximum core size for SRS (0 = no dumps; unlimited).
- `systemd-coredump.socket` - active or not.
- `coredump.conf` - every active setting from `/etc/systemd/coredump.conf` and its `.conf.d` folders, with the file it came from, or "(defaults only)".

**Healthy / typical values.** Author's PC: piped to systemd-coredump, `core_uses_pid 1`, `panic 0`, `suid_dumpable 2`, RLIMIT_CORE unlimited, socket active. The main file sets 32G limits, then a drop-in `99-limits.conf` lowers them to `ProcessSizeMax=1G`, `ExternalSizeMax=1G`, `MaxUse=2G`, `KeepFree=10G`. Later files win.

**Unusual values and what they suggest.** `core_pattern` is plain `core` and `RLIMIT_CORE 0`: crashes leave nothing to debug. `Storage=none`: dumps are logged but not kept. A huge `MaxUse` on a small disk: crash dumps from games can fill `/var`.

**Where Linux gets it.** `/proc/sys/kernel/{core_pattern,core_pipe_limit,core_uses_pid,print-fatal-signals,panic_on_oops,panic}`, `/proc/sys/fs/suid_dumpable`, `getrlimit(RLIMIT_CORE)`, `systemctl is-active systemd-coredump.socket`, `/etc/systemd/coredump.conf`, `/etc/systemd/coredump.conf.d/`, `/usr/lib/systemd/coredump.conf.d/`.

---
## Error reporting  
`wer` - Linux data

**In plain words.** The Linux counterpart of Windows Error Reporting's crash list: which programs have crashed recently, and how much space the saved dumps use.

**What you see.**
- `Stored core dumps` - "N files, SIZE in /var/lib/systemd/coredump".
- `Most recent crashes (coredumpctl list -r -n 30)` - newest first: `TIME`, `PID`, `UID`, `GID`, `SIG` (the signal that killed it), `COREFILE` (`present` = dump kept, `missing` = deleted or never stored, `truncated` = cut at the size limit, `none`), `EXE` (the program), `SIZE`.
- "no core dumps recorded (or journal not readable)" if empty.

**Healthy / typical values.** Author's PC: 8 files, 411.80MB. Recent entries include `plasmashell` (SIGSEGV and SIGABRT, `truncated` at the 1 GB limit), `Xwayland` (SIGABRT, present) and several `kioworker` SIGSEGVs. A healthy desktop has none to a few per month.

**Unusual values and what they suggest.** One program crashing again and again (e.g. four `kioworker` crashes in five minutes): a real bug or a broken plugin. Run `coredumpctl info <PID>` for its backtrace. `plasmashell` crashes: often a broken widget or a Plasma/Qt version mismatch after a partial update. `truncated` dumps cannot give a full backtrace; raise `ProcessSizeMax` if you need one.

**Where Linux gets it.** Directory listing of `/var/lib/systemd/coredump`; `coredumpctl list --no-pager -r -n 30`.

**Background.** **SIGSEGV** (segmentation fault) means the program touched memory it does not own. **SIGABRT** means the program stopped itself after finding something wrong, often a failed internal check.

---
## Windows CE devices  
`win-ce` - Windows-only (Linux equivalent shown)

**In plain words.** SIV lists Windows CE / Windows Mobile handhelds paired with the PC. These have no Linux counterpart in SRS.

**What you see.** `Status - Windows-only item in SIV`, `Linux equivalent - none`. That is expected.

---
## Windows (OS) panel  
`windows` - Linux data

**In plain words.** This is the panel SIV shows behind its "Windows" button. In SRS it is the Linux overview, the same page as [System](#system), opened by the main-window **Linux** button.

**What you see.** Exactly the fields described under [System](#system): distribution and kernel, install date, init and chassis, desktop session, system folders, kernel command line, one line per CPU, power and time zone, up-time and load, user and locale, BIOS, and connected monitors with their kernel driver. The panel's sub-buttons (Machine, Sensors, USB Bus, Processors, Volumes, PCI Bus, ALL Dev, Graphics) open the matching items in other groups.

**Healthy / typical values / unusual values.** As for [System](#system).

**Where Linux gets it.** `/etc/os-release`, `uname()`, `hostname`, sysfs and procfs (see [System](#system)).

---
## WinSAT  
`winsat` - Windows-only (Linux equivalent shown)

**In plain words.** WinSAT (Windows System Assessment Tool) produced the old "Windows Experience Index" scores. Linux has no built-in equivalent, so SRS shows a stub.

**What you see.** `Status - Windows-only item in SIV`, `Linux equivalent - none`. To measure performance on Linux, use SRS's own benchmark tools instead.

---
## Time zones  
`zones` - Linux data

**In plain words.** Every time zone in the tz database (tzdata), and which one this PC uses.

**What you see.**
- `Current` - your zone (from `/etc/localtime` or `$TZ`), in green.
- `timedatectl` - the zone systemd reports.
- Table from `zone1970.tab`:
  - `Countries` - country codes that use the zone (several for shared zones, e.g. `AE,OM,RE,SC,TF`).
  - `Coordinates` - latitude and longitude of the zone's main city, as `+DDMM+DDDMM`.
  - `Time Zone` - the zone name. Yours is green.
  - `Comments` - which area it covers.
- Footer: "N time zones".

**Healthy / typical values.** Author's PC: Current and timedatectl both `America/New_York`, 312 zones. Argentina alone has 12, such as `America/Argentina/Cordoba` "most areas: CB, CC, CN...".

**Unusual values and what they suggest.** `Current` and `timedatectl` differ: `$TZ` is set in your environment and overrides the system zone for your programs only. A zone name not in the table (for example `US/Eastern`): it is an old alias. It still works, but `America/New_York` is the modern name.

**Where Linux gets it.** `/etc/localtime` link, `$TZ`, `timedatectl show -p Timezone --value`, `/usr/share/zoneinfo/zone1970.tab`.
