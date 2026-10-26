# Volumes / Drives  (SRS Section Reference)

[Back to the section index](README.md) - [Glossary](../GLOSSARY.md)

**Where to find these items (SRS 2.0).** In the main window they are on the **Storage** page (Alt+6). Its tabs are Disk drives, Volumes, SMART drives, Partitions, Disk performance and USB disk drives; **All N items** lists the rest. The Overview checks drive health and drive temperature. You can also type an item's name or id into the search box (Ctrl+F). The rest of this introduction describes the classic view (Menu -> Classic View), with its Menu and panel buttons.

This group covers everything about storage: the physical drives in the PC, how they are split into
partitions, which filesystems are on them, where those filesystems are mounted, how busy the drives
are, and how healthy they are. Open any item with **Menu -> Volumes / Drives -> item**. The panel
button **Volumes** opens the volume list straight away; next to it, **Drive /** shows the full
detail of the root filesystem (`volumes:/`) and **FS** shows the filesystem statistics of the root
filesystem (`ntfs:/`). Most items read only files the kernel already makes public, so they work as
a normal user. The drive health items (SMART drives, SMART all drives, SMART drive monitor) and the
NVMe controller items need root data: use **Menu -> Run as root** (`srs-root`) once, and SRS shows
the cached results from then on. SMART on USB-attached disks is skipped unless you set
`SRS_SMART_USB=1` before running `srs-root`.

A note on units: SRS shows sizes in powers of 1024 but writes them as "GB" and "TB". So a "1 TB"
drive (10^12 bytes on the box) shows as 931.51GB. The [Drive geometry](#drive-geometry) item shows
both styles side by side.

SRS (srs-sysinfo) is a Linux system information viewer inspired by SIV64X (System Information Viewer). SIV
is the work of Ray Hinchliffe, RH Software. SRS is not affiliated with or endorsed by RH Software
and contains no SIV code or data. Item names follow SIV, which is why some titles name Windows
things (NTFS, stornvme, Storage Spaces); SRS shows the nearest Linux equivalent.

## Contents
- [Disk drives](#disk-drives) `drives`
- [Drive tree](#drive-tree) `drive-tree`
- [Disk API routines](#disk-api-routines) `dsk-api`
- [FAT statistics](#fat-statistics) `fat`
- [Drive geometry](#drive-geometry) `geometry`
- [Hidden disks](#hidden-disks) `hidden`
- [JMicron JMB RAID](#jmicron-jmb-raid) `jmb-raid`
- [JMicron NVMe RAID](#jmicron-nvme-raid) `jms-raid`
- [Disk mapping](#disk-mapping) `mapping`
- [LSI MegaRAID](#lsi-megaraid) `mega-raid`
- [NTFS statistics](#ntfs-statistics) `ntfs`
- [Partitions](#partitions) `partitions`
- [Disk performance](#disk-performance) `performance`
- [Mount points](#mount-points) `points`
- [CSMI RAID](#csmi-raid) `raid`
- [RocketRAID](#rocketraid) `rocket-raid`
- [SCSI devices](#scsi-devices) `scsi`
- [Samsung NVMe (secnvme)](#samsung-nvme-secnvme) `secnvme`
- [Disk sector sizes](#disk-sector-sizes) `sectors`
- [SMART drives](#smart-drives) `smart`
- [SMART all drives](#smart-all-drives) `smart-all`
- [SMART drive monitor](#smart-drive-monitor) `smart-sdm`
- [Storage spaces](#storage-spaces) `spaces`
- [NVMe (stornvme)](#nvme-stornvme) `stornvme`
- [USB disk drives](#usb-disk-drives) `usb-disks`
- [Volume list](#volume-list) `vol-list`
- [Volumes](#volumes) `volumes`
- [Intel VROC](#intel-vroc) `vroc`

---
## Disk drives  
`drives` - Linux data

**In plain words.** One line per physical drive: what it is, how it is connected, and a short list of
its main traits. Optical drives (CD/DVD/Blu-ray) are listed last, after a blank line.

**What you see.**

| Column | Meaning | Why it matters |
|---|---|---|
| Drive | Kernel name: `nvme0n1`, `sda`, `sr0`... | The name every other item and Linux tool uses. |
| Bus | NVMe, SATA, USB, SCSI, MMC (SD/eMMC), VirtIO (virtual machine) | Tells you the connection, and so the speed you can expect. |
| Location | udev `ID_PATH`: the PCI address and port the drive hangs off, e.g. `pci-0000:0c:00.0-ata-1.0` | Lets you tell two identical drives apart and see which controller each one uses. |
| Type | Fixed, Removable, CD Drive (also RAM Disk / Virtual, but those are left out here) | "Removable" is a flag the device itself reports; most USB sticks and card readers set it. |
| Vendor and Product (Serial Number) | Model name, with the serial in brackets | Identify the exact drive for warranty or firmware lookups. |
| Revision | Firmware version | Check against the maker's site for known-bug fixes. |
| State | `live` for NVMe controllers, `running` for SATA/USB/SCSI devices | Anything else (`offline`, `dead`, `resetting`) means the kernel has lost or is recovering the drive. |
| Characteristic | Size, media (SSD, HDD, HDD/Flash, Optical, No media), spindle speed in rpm (HDDs that report it), `TRIM` if the drive accepts discard, `WriteCache` if write-back caching is on, `RO` if read-only | A quick health-of-setup check: an SSD without TRIM, or a disk stuck read-only, stands out here. |

The last line counts physical drives. Loop devices, zram and device-mapper volumes are left out
(see [Drive tree](#drive-tree) for those). The board name and BIOS version follow as a footer.

**Healthy / typical values.** On the author's PC: three NVMe SSDs (Samsung 980 PRO 1TB, ADATA
XPG GAMMIX S70 Blade 2TB, PNY CS2340 500GB), all `live`, all "SSD TRIM WriteCache". One SATA
Seagate 4TB disk: `running`, "3.64TB HDD 7200rpm WriteCache". USB sticks and card readers show as
Removable with media "HDD/Flash", and a USB DVD writer shows as "1024.00MB Optical".

**Unusual values and what they suggest.**
- Size `0` with media "SSD" or "No media": the device answered but has no usable storage behind it.
  For a card reader slot that just means "no card inserted". For a USB enclosure (on the author's PC,
  a USB 3.1 NVMe enclosure listed as `0 SSD`) it usually means the drive inside is missing, dead,
  or not detected by the bridge chip.
- An SSD without `TRIM`: the drive or its USB bridge does not pass discard commands. The drive can
  slow down over time; many USB bridges cause this.
- `RO` on a disk you expect to write to: a write-protect switch, a failing SSD that went read-only
  to protect your data, or a kernel decision after errors. Check `dmesg`.
- A USB drive marked HDD/Flash: SRS cannot tell spinning disks from flash behind most USB bridges,
  because they report "rotational" by default.

**Where Linux gets it.** `/sys/block/<disk>/` (size, removable, queue/rotational,
queue/discard_max_bytes, queue/write_cache, device/model, device/rev or firmware_rev,
device/state), `/sys/class/nvme/<ctrl>/state`, and the udev database (`/run/udev/data/b<maj>:<min>`)
for model, serial, path and rpm.

**Background.** *TRIM* (called *discard* on Linux) lets the operating system tell an SSD which
blocks no longer hold data, so the SSD can erase them ahead of time. *Write-back cache* means the
drive confirms a write once it is in its own fast memory, before it is on the flash or platter;
it is faster, and safe as long as the system shuts down cleanly or the drive has power-loss
protection.

`drives:<disk>` (for example `drives:sda`) shows just that one drive.

---
## Drive tree  
`drive-tree` - Linux data

**In plain words.** A tree that shows each disk, the partitions on it, what filesystem each one holds
and where it is mounted. Stacked devices (encryption, LVM, RAID, zram) hang underneath the device
they sit on.

**What you see.**
- **Disk line**: name, size, bus, type, model (serial), and the partition table type (`gpt` or `dos`).
- **Partition lines** (`|--` and `` `-- ``): name, size, filesystem type (`-` if none is known),
  filesystem label, then every mount point of that partition. Swap in use shows as `[SWAP]`.
- **(whole disk)**: a disk with no partition table but a filesystem or swap directly on it, such as
  `zram0  swap  [SWAP]`.
- **`` `-> `` holder lines**: devices built on top of this one (for example a dm-crypt or LVM
  volume on a partition), with size, name and mount points. They nest up to six levels.

Unlike [Disk drives](#disk-drives), this tree also lists zram, loop-backed and device-mapper disks.
`drive-tree:<disk>` shows one disk only.

**Healthy / typical values.** On the author's PC the Samsung NVMe holds `nvme0n1p1 2.05GB vfat EFI
/boot/efi` and `nvme0n1p2 929.46GB btrfs LINUX / /srv /root /home /var/cache /var/log /var/tmp`.
That one partition has seven mount points because it is btrfs with several subvolumes. A dual-boot
disk shows an `ntfs WIN11` partition with no mount point, which is normal when Windows is not in use.

**Unusual values and what they suggest.**
- A partition with `-` as the filesystem: empty, a filesystem udev does not recognise, or a damaged
  filesystem. Cross-check with [Volume list](#volume-list).
- A data partition you expect to see but which has no mount point: it is simply not mounted (yet).
- A disk with size `0` and no partitions: no media, or a dead drive behind a USB bridge.

**Where Linux gets it.** `/sys/class/block` (partitions, sizes, `holders/` links), udev
`ID_FS_*` and `ID_PART_TABLE_TYPE`, `/proc/self/mountinfo` and `/proc/swaps`.

**Background.** A *holder* is the kernel's word for a device that uses another device. If you put an
encrypted container on `sda2`, the decrypted `dm-0` is a holder of `sda2`.

---
## Disk API routines  
`dsk-api` - Windows-only (Linux equivalent shown)

**In plain words.** On Windows, SIV lists the entry points of a vendor disk toolkit (the JMicron
SDK). Linux has no such toolkit; the kernel drivers plus a set of command-line tools do the same
jobs. This item shows which of those tools and kernel interfaces exist on your PC.

**What you see.**
- **Disk tools**: a tick box for each of `smartctl`, `nvme`, `hdparm`, `sdparm`, `sg_inq`,
  `lsscsi`, `blkid`, `lsblk`, `udisksctl`, `mdadm`, `lvm`, `btrfs`, `skdump`, `fio`, ticked if it
  is in your PATH (or in /usr/sbin, /sbin). Then "Found N of 14 disk tools in PATH".
- **Kernel interfaces**: tick boxes for SCSI bsg (`/dev/bsg`), NVMe class (`/sys/class/nvme`),
  SCSI generic (sg), udev database, hwmon (drivetemp/nvme), device-mapper (`/dev/mapper/control`),
  md RAID (`/proc/mdstat`), btrfs sysfs (`/sys/fs/btrfs`).

**Healthy / typical values.** The author's PC: 13 of 14 tools found (only `sg_inq` missing), and
every kernel interface present except md RAID, because no software RAID is in use.

**Unusual values and what they suggest.** `smartctl` missing means `srs-root` cannot collect SMART
data, so the SMART items stay empty; install `smartmontools`. `nvme` missing means no
`nvme smart-log` copy (install `nvme-cli`). An unticked md RAID box only means the md driver is not
loaded, which is normal without software RAID.

**Where Linux gets it.** A PATH search for each tool, and a check that each kernel path exists.
Nothing is run.

---
## FAT statistics  
`fat` - Linux data

**In plain words.** Facts about every mounted FAT or exFAT filesystem: which FAT type, how many
clusters, how many are free, and the volume serial.

**What you see.** For each filesystem, a heading "Statistics for <mount point> (<label>) <type>
/dev/<device> <size>", then:

| Field | Meaning | Why it matters |
|---|---|---|
| FAT type | FAT12, FAT16 or FAT32 for vfat; the exFAT version (e.g. `1.0`) for exFAT | FAT32 cannot hold files of 4 GiB or more; exFAT can. |
| Clusters | Total number of clusters (allocation units) | More clusters means more small files fit without waste. |
| Free clusters | Clusters not in use | Free space = free clusters x cluster size. |
| Cluster size | Size of one cluster | Every file uses at least one whole cluster. |
| Serial | Volume serial number (the `XXXX-XXXX` value) | The same number Windows shows; Linux uses it as the UUID of FAT volumes. |

If nothing matches, "Status: No mounted FAT/exFAT volumes" is shown. `fat:<mount point or device>`
shows one volume, whatever its type.

**Healthy / typical values.** On the author's PC: the EFI system partition at `/boot/efi` is FAT32
with 4KB clusters and 537,058 clusters, almost all free. A 16 GB USB stick is FAT32 with 8KB
clusters. A Ventoy USB stick is exFAT with 32KB clusters and only 51,149 of 953,065 clusters free
(it is nearly full of ISO images).

**Unusual values and what they suggest.** Free clusters near zero: the volume is full. On an EFI
partition that can stop kernel or bootloader updates. Very large clusters (64KB or more) on a disk
of small files waste a lot of space.

**Where Linux gets it.** `statvfs()` on the mount point, plus udev `ID_FS_VERSION` and `ID_FS_UUID`.
The kernel vfat and exfat drivers export no statistics of their own.

**Background.** A *cluster* is the smallest piece of space a FAT filesystem hands out. A 1-byte file
still takes one whole cluster.

---
## Drive geometry  
`geometry` - Linux data

**In plain words.** Each physical drive's size in bytes, in decimal units (as printed on the box)
and in binary units (as most software shows), plus the old cylinder/head/sector figures.

**What you see.**

| Column | Meaning |
|---|---|
| Physical Drive Description | `/dev/<disk>` plus model and serial |
| Media | SSD, HDD, HDD/Flash, Optical, No media |
| Cylinders | Size divided by (255 x 63 x sector size): the legacy "CHS" cylinder count |
| Tracks | Always 255 (heads per cylinder in the legacy translation) |
| Sectors | Always 63 (sectors per track in the legacy translation) |
| Bytes | Logical sector size in bytes (usually 512) |
| Total Size | Exact size in bytes, with thousands separators |
| Size (1000) | Size in decimal GB/TB (1 GB = 1,000,000,000 bytes), as drive makers count |
| Size (1024) | Size in binary units (1 GB here = 1,073,741,824 bytes), as Linux tools count |

A **Total** row adds up all drives.

**Healthy / typical values.** The Samsung 980 PRO "1TB" is 1,000,204,886,016 bytes: 1.00TB in the
1000 column and 931.51GB in the 1024 column. The Seagate "4TB" is 4.00TB / 3.64TB. The author's
whole system adds up to 7.63TB decimal (6.94TB binary).

**Unusual values and what they suggest.** A total size much smaller than the label: a fake-capacity
USB stick, a drive limited by a Host Protected Area, or a USB bridge that only sees part of a disk.
A size of 0: no media. The 1000 vs 1024 gap itself is normal and not "missing space".

**Where Linux gets it.** `/sys/block/<disk>/size` (always in 512-byte units) and
`/sys/block/<disk>/queue/logical_block_size`.

**Background.** *CHS* (cylinder-head-sector) is how disks were addressed in the 1980s and 1990s.
Modern drives use *LBA* (Logical Block Addressing): the drive is just a numbered list of blocks.
The 255/63 figures are a fixed translation kept for old partitioning tools; they say nothing about
the real platters or flash chips.

---
## Hidden disks  
`hidden` - Linux data

**In plain words.** Storage devices that exist but do not show up as normal, usable disks.

**What you see.** Three short lists:
- **Block devices flagged hidden**: block devices with `/sys/block/*/hidden` set to 1. These are
  NVMe multipath "path" devices: the kernel hides the individual paths and shows one combined disk.
  Normally "none".
- **Devices present but without media (size 0)**: name, bus, type and model of each device that
  reports a size of 0.
- **NVMe generic (char) namespaces**: the `/dev/ngXnY` character devices. Every NVMe namespace has
  one; tools use them to send raw NVMe commands. They are not disks you can mount.

**Healthy / typical values.** The author's PC shows no hidden devices; two size-0 devices (an empty
USB card-reader slot and a USB NVMe enclosure whose drive is not detected); and `/dev/ng0n1
/dev/ng1n1 /dev/ng2n1`, one per NVMe SSD.

**Unusual values and what they suggest.** A fixed internal drive under "without media": the drive
answers on the bus but reports no capacity, which can mean failure, a locked drive, or a firmware
fault. A hidden device on a desktop with no multipath setup is rare; check `nvme list-subsys`.

**Where Linux gets it.** `/sys/block/*/hidden`, `/sys/block/*/size`, and a listing of `/dev/ng*`.

**Background.** *Multipath* means one drive is reachable over more than one route (common in servers
with dual-port NVMe). The kernel joins the routes into one disk so you do not write to the same
drive twice by mistake.

---
## JMicron JMB RAID  
`jmb-raid` - Not present on most PCs

**In plain words.** Looks for JMicron storage controllers. On Windows, SIV reads RAID details through
JMicron's own toolkit. On Linux there is no JMicron RAID interface, so SRS only reports whether any
JMicron hardware is present.

**What you see.** "Checked: PCI vendor 0x197b, USB vendor 152d (JMicron)", then one line per match
(PCI address with ID, class and driver; or USB port with VID:PID and product name), and a Status
line: "Not present on this system" or "N device(s) found; Linux manages these with the ahci driver +
md/dm".

**Healthy / typical values.** Most PCs have no JMicron PCI controller. Note that JMicron also makes
very common USB-to-SATA and USB-to-NVMe bridge chips (USB vendor 152d), so a USB enclosure shows up
here too. On the author's PC a USB 3.1 NVMe enclosure (152d:0583) is listed. That is a bridge, not a
RAID controller.

**Unusual values and what they suggest.** A real JMicron SATA RAID card on Linux runs in plain AHCI
mode: the disks appear separately, and any RAID is done with `mdadm` or device-mapper.

**Where Linux gets it.** `/sys/bus/pci/devices/*/vendor` and `/sys/bus/usb/devices/*/idVendor`.

---
## JMicron NVMe RAID  
`jms-raid` - Not present on most PCs

**In plain words.** The same JMicron check as [JMicron JMB RAID](#jmicron-jmb-raid), aimed at
JMicron's USB bridge chips (the "JMS" series), some of which can join two drives into one inside the
enclosure.

**What you see.** The same layout as JMicron JMB RAID. The Status line says Linux uses
"uas/usb-storage (bridge RAID is configured in its firmware)".

**Healthy / typical values.** The author's PC lists one JMicron bridge (152d:0583, a single-drive USB
NVMe enclosure). A single-drive enclosure has no RAID to show.

**Unusual values and what they suggest.** If a dual-bay JMicron enclosure is set to RAID 0 or 1, Linux
only sees the one combined disk the bridge presents. The RAID mode is set with the enclosure's
switch or the maker's tool, not from Linux.

**Where Linux gets it.** PCI and USB vendor IDs in sysfs.

---
## Disk mapping  
`mapping` - Linux data

**In plain words.** For each disk: every stable name Linux gives it, and for each partition its
filesystem, label, mount points and IDs. Use it to write reliable `/etc/fstab` entries.

**What you see.** For each disk, a heading "Disk Drive /dev/<disk>  <model (serial)>", then:
- **Symbolic Link**: each `/dev/disk/by-id/...` name (model + serial, `eui.`, `wwn-` and `nvme.`
  IDs). These names never change between boots.
- **Path**: the `/dev/disk/by-path/...` names (which port the disk is on).
- One line per partition: name, filesystem and label (`<none>` if no label), and mount points (or
  "Not mounted", or `[SWAP]`). Below it, in grey, the **PARTUUID** (the partition's own ID, from the
  partition table) and the **UUID** (the filesystem's ID).
- A disk with no partitions gets one **Point** line with its mount points, or "No Volumes".

**Healthy / typical values.** On the author's PC the Seagate shows two by-id names (a `wwn-0x...` and
an `ata-ST4000NM0033-9ZM170_XXXX` name) and two by-path names. A DOS (MBR) partitioned USB stick has
short PARTUUIDs like `XXXXXXXX-01`: the 8-digit disk signature plus the partition number.

**Unusual values and what they suggest.**
- Two disks with the same by-id serial: cheap card readers and clones often share serials; use by-path
  or the filesystem UUID for those.
- A partition with a UUID but no filesystem name: udev saw something but could not identify it.
- An ISO-style UUID such as `2026-08-19-18-44-21-00`: that is an ISO 9660 (CD image) filesystem,
  whose "UUID" is its creation date. Normal on live USB sticks.

**Where Linux gets it.** The udev database (`S:` symlink lines and `ID_PART_ENTRY_UUID`,
`ID_FS_UUID`), `/proc/self/mountinfo` and `/proc/swaps`.

**Background.** `sda` and `nvme0n1` can swap places between boots. A *UUID* (Universally Unique
Identifier) or *PARTUUID* stays the same, so it is the safe way to point at a filesystem in
`/etc/fstab` or a bootloader.

---
## LSI MegaRAID  
`mega-raid` - Not present on most PCs

**In plain words.** Looks for an LSI / Broadcom (also Avago) MegaRAID hardware RAID card, found in
servers and workstations.

**What you see.** "Checked: PCI vendor 0x1000 (LSI/Broadcom)", any matching PCI devices (address,
ID, class, driver), and Status: "Not present on this system" or "N device(s) found; Linux manages
these with megaraid_sas + storcli".

**Healthy / typical values.** "Not present on this system" on the author's PC and on almost every
desktop.

**Unusual values and what they suggest.** If a card is found, the disks behind it are hidden by the
card's firmware; use Broadcom's `storcli` tool for array status. Note the check matches any PCI
device from vendor 0x1000, which also covers LSI SAS HBAs (non-RAID host adapters).

**Where Linux gets it.** `/sys/bus/pci/devices/*/vendor`.

---
## NTFS statistics  
`ntfs` - Linux data

**In plain words.** Inside facts about each mounted NTFS volume, followed by the same kind of facts
for native Linux filesystems (btrfs, ext2/3/4, XFS, F2FS).

**What you see.** For each filesystem, a heading "Statistics for <mount point> (<label>) <type>
/dev/<device> <size>", then fields that depend on the filesystem type.

**NTFS (ntfs3 driver):**

| Field | Meaning | Why it matters |
|---|---|---|
| NTFS version | Driver/format version, e.g. `ntfs3.1` | |
| Cluster size | Bytes per cluster (usually 4096) | |
| Clusters | Total clusters on the volume | |
| MFT records | Size of the Master File Table, in records | Each file or folder uses at least one record. |
| MFT records in use | Records holding files | Close to "MFT records" just means the table will grow; NTFS does that by itself. |
| Dirty (runtime) | `clean` or `dirty`: the driver's current view | |
| Dirty flag (on disk) | `clean` or `dirty`: the flag stored on the volume | `dirty` means Windows (or the driver) thinks the volume was not closed cleanly. |
| Label (ntfs3) | Volume label as the driver reads it | |

**btrfs:** btrfs fsid (filesystem ID), Label, Node size (size of a metadata tree block, usually
16384), Sector size, Checksum (algorithm, e.g. `crc32c`), Generation (a counter that goes up at
every commit), Features (on-disk features enabled), Devices (count and list), three Allocation lines
(data, metadata, system: profile, space set aside, space used, percent, and raw disk use), Global
reserve (emergency metadata space), "Device N errors" (write, read, flush, corruption and generation
error counters; green when all zero, orange otherwise), and Commit stats (how many commits, and how
long they took in ms).

**ext2/3/4:** the files the kernel exports under `/sys/fs/ext4/<dev>/`: `lifetime_write_kbytes`,
`session_write_kbytes`, `errors_count`, `first_error_time`, `last_error_time`,
`delayed_allocation_blocks`, `reserved_clusters`, `inode_readahead_blks`, `mb_stream_req`,
`journal_task`.

**XFS:** Log head LSN and Log tail LSN (positions in the journal), then the raw XFS statistics block.

Other filesystems get the note "No filesystem-specific statistics exported by this filesystem
driver." `ntfs:<mount point or device>` shows any single filesystem; the **FS** panel button uses
`ntfs:/` to show the root filesystem.

**Healthy / typical values.** On the author's PC the 4TB NTFS data disk has 976,754,175 clusters of
4096 bytes and 5,139,584 MFT records, 4,563,941 in use. The btrfs root shows `data single`,
`metadata dup`, `system dup`, all device error counters at 0, and a max commit time of 427 ms.

**Unusual values and what they suggest.**
- NTFS `dirty`: the volume was not closed cleanly (Windows Fast Startup or hibernation, a crash, or
  pulling the drive). On the author's PC the 4TB disk shows `dirty`. Fix it by running
  `chkdsk /f` from Windows (or `ntfsfix` as a lesser fallback). Writing to a dirty volume from Linux
  is a risk.
- btrfs data allocation at 95% or more used: the filesystem is close to full; btrfs can report
  "no space" before `df` shows 0. A balance (`btrfs balance start -dusage=50`) frees half-empty
  chunks.
- Any btrfs device error counter above 0: the disk has returned bad data or failed writes. Check
  SMART, cables and run `btrfs scrub`. `corruption_errs` means checksums did not match.
- ext4 `errors_count` above 0: the filesystem found damage; run `fsck` when it is unmounted.
- Very long btrfs commit times (several seconds): heavy write load or a slow or failing disk.

**Where Linux gets it.** `/proc/fs/ntfs3/<dev>/volinfo` and `label`, `/sys/fs/btrfs/<fsid>/`,
`/sys/fs/ext4/<dev>/`, `/sys/fs/xfs/<dev>/`. All read-only.

**Background.** The *MFT* (Master File Table) is NTFS's index of every file. btrfs keeps data and
metadata in separate pools of *chunks*; "allocated" is the space turned into chunks, "used" is what
is really filled. The *dup* profile stores two copies of metadata on the same disk, which lets btrfs
repair a bad metadata block by itself.

---
## Partitions  
`partitions` - Linux data

**In plain words.** The partition table of each disk: where every partition starts, how long it is,
what type it claims to be, and whether it is bootable.

**What you see.** For each partitioned disk, a heading "/dev/<disk> <model (serial)>  <table type>
Signature <id>". The table type is `gpt` or `dos` (MBR); the signature is the GPT disk GUID or the
8-digit MBR disk ID. Then one row per partition:

| Column | Meaning |
|---|---|
| # | Partition number |
| Device | Partition name |
| Type | Partition type, translated to a name where SRS knows it (EFI System, Microsoft basic data, Microsoft reserved, Windows recovery, Linux filesystem, Linux swap, Linux root (x86-64), Linux LVM, Linux RAID, Linux home, Linux extended boot, BIOS boot, LUKS, Linux /srv; for MBR: FAT16, FAT32, NTFS/exFAT/HPFS, Linux, Linux swap, EFI (FAT), Extended, hidden types and more). Unknown types show the raw GUID or hex code. |
| Partition Start / about | Start offset in bytes, then rounded |
| Partition Length / about | Length in bytes, then rounded |
| Name | GPT partition name (MBR partitions have none) |
| Hidden | `Yes` for MBR "hidden" type codes (0x10-0x1F range), else `No` |
| Boot | `ESP` for an EFI System Partition, `Active` for an MBR partition with the boot flag, else `No` |

Disks with no partitions are skipped. `partitions:<disk>` shows one disk and says "no partition table"
if it has none.

**Healthy / typical values.** Most partitions start at 1,048,576 bytes (1 MiB), the modern default.
On a dual-boot NVMe on the author's PC: `EFI System` (2GB, Boot = ESP), `Microsoft basic data`
(Windows), `Linux filesystem`, and a swap partition. A Ventoy USB stick shows an `Active` exFAT
partition and a small `EFI (FAT)` one. Partition numbers do not have to follow the order on disk.

**Unusual values and what they suggest.**
- A type that does not match the content. On the author's Samsung NVMe, both the EFI partition and
  the btrfs root carry the GPT type GUID for "BIOS boot", so the EFI partition shows Boot = `No`
  instead of `ESP`. Linux mounts by UUID and does not care, but some firmware and installers look for
  the EFI System type to find the boot partition. `gdisk` or `sgdisk -t` can fix the type code.
- Type `0x0` on a USB stick: an "isohybrid" live image whose partition entry has no type. Normal for
  many Linux live ISOs written with `dd`.
- A start that is not a multiple of 1 MiB: see [Disk sector sizes](#disk-sector-sizes) for whether it
  hurts.

**Where Linux gets it.** `/sys/class/block/<part>/{start,size,partition}` and the udev
`ID_PART_TABLE_*` and `ID_PART_ENTRY_*` properties. SRS never reads raw sectors.

**Background.** *GPT* (GUID Partition Table) is the modern partition format: up to 128 partitions,
each with a type GUID, a unique PARTUUID and a name. *MBR* (Master Boot Record, shown as `dos`) is
the old format: four primary partitions, a one-byte type code, and an "active" flag for the old
BIOS boot method. The *ESP* (EFI System Partition) is the FAT partition the UEFI firmware boots from.

---
## Disk performance  
`performance` - Linux data

**In plain words.** A live view of how much each drive is reading and writing, and how busy it is.
In the GUI it refreshes about once a second.

**What you see.** Each drive gets two rows.

| First row | Second row | Meaning |
|---|---|---|
| Disk Drive Description and Serial Number | Type, Location | Which drive |
| Read Size | Write Size | Total data read / written since boot |
| Read Delta | Write Delta | Read / write speed right now (bytes per second) |
| Read Count | Write Count | Total read / write requests completed since boot |
| Read Delta | Write Delta | Requests per second right now |
| Merge Count | Queue Depth | Merged reads since boot / requests in flight right now |
| Merge Delta | Busy % | Merged reads per second / share of time the drive had work to do |

The last line gives the measured interval. The first time you open the item SRS takes two samples
0.25 s apart; after that the delta is since the previous refresh.

**Healthy / typical values.** An idle desktop shows most drives at `0/s` and `0.0%` busy. On the
author's PC, the Samsung system SSD had read 1.83TB since boot and showed 0.8% busy, while the
Seagate HDD showed 68.8% busy with a queue depth of 3 during a short read burst. HDDs reach high
busy percentages at low speeds because each seek takes milliseconds.

**Unusual values and what they suggest.**
- Busy % near 100 for a long time with low speeds: the drive is the bottleneck (often an HDD doing
  random I/O, or a failing drive retrying reads).
- A large queue depth on a SATA HDD that never drains: the disk cannot keep up.
- Steady writes on an "idle" system: a background job (indexer, backup, swap, a browser cache).
  Use `iotop` to find the program.
- Busy % on an NVMe SSD can read high even when it has spare capacity, because an SSD works on many
  requests in parallel. Use speed and queue depth for SSDs.

**Where Linux gets it.** `/proc/diskstats` (read and write sectors, requests, merges, in-flight count
and busy time), sampled twice.

**Background.** A *merge* happens when the kernel joins two requests for neighbouring blocks into one
larger request. *Queue depth* is how many requests are waiting at the drive at once.

---
## Mount points  
`points` - Linux data

**In plain words.** Every place where a filesystem is attached to the folder tree, sorted into real
disks, network shares, and virtual (kernel) filesystems.

**What you see.** Four columns: **Mount Point** (the folder), **Source** (the device, share or
pseudo-name), **Type** (filesystem type), **Options** (mount options, cut at 90 characters).
Three groups:
- **Block device filesystems**: filesystems on real disks.
- **Network filesystems**: NFS, SMB/CIFS, SSHFS, 9p, Ceph, GlusterFS, rclone, davfs, AFS.
- **Virtual / pseudo filesystems** (grey): `/proc`, `/sys`, tmpfs, cgroups, FUSE mounts, overlay and
  similar.

The last line gives the totals: "N block, N network, N virtual mount points".

**Healthy / typical values.** The author's PC has 17 block, 2 network and 33 virtual mount points.
The btrfs root partition appears seven times (`/`, `/home`, `/var/log`...), one per subvolume. A
second mount of the NTFS data disk at `/srv/newdl-ro` uses `ro` (read-only). Network shares include an
NFS v4 export and a CIFS share from a router at `//192.168.1.x/...`.

**Unusual values and what they suggest.**
- A disk filesystem mounted `ro` that should be writable: the kernel may have switched it to read-only
  after errors. Check `dmesg`.
- `noatime` or `relatime` are normal and good for SSDs. A plain `atime`-only mount writes on every
  read.
- The same mount point listed twice: something was mounted over it. Only the last one is visible.

**Where Linux gets it.** `/proc/self/mountinfo`.

**Background.** A *bind mount* or *subvolume mount* attaches part of an existing filesystem at a
second place. That is why one partition can have many mount points.

---
## CSMI RAID  
`raid` - Not present on most PCs

**In plain words.** On Windows, SIV reads RAID arrays through a CSMI driver. Linux has no CSMI;
instead SRS shows the three kinds of Linux RAID: md software RAID, device-mapper, and btrfs
multi-device profiles.

**What you see.**
- **md software RAID (/proc/mdstat)**: the contents of `/proc/mdstat` (arrays, members, state and any
  rebuild progress), or "md driver not loaded - no md RAID arrays".
- **device-mapper (LVM / dm-raid / dm-crypt)**: one line per dm or md device: name, size, kind (`LVM`,
  `dm-crypt` or the md level) and the devices under it. Or "no device-mapper devices".
- **btrfs multi-device profiles**: for each btrfs filesystem, its label, device count and the profile
  of data, metadata and system (single, dup, raid0, raid1, raid1c3, raid1c4, raid10, raid5, raid6).

**Healthy / typical values.** On the author's PC: no md arrays, no device-mapper devices, and three
single-device btrfs filesystems with `data=single metadata=dup system=dup`.

**Unusual values and what they suggest.** In `/proc/mdstat`, a member list like `[U_]` means one disk of
a mirror is missing (the array is *degraded*); `recovery` or `resync` with a percentage means a rebuild
is running. A btrfs filesystem with several devices but `data=single` has no redundancy: losing one
disk loses data.

**Where Linux gets it.** `/proc/mdstat`, `/sys/block/dm-*/dm/{name,uuid}`, `/sys/block/md*/md/level`,
`/sys/block/*/slaves`, `/sys/fs/btrfs/<fsid>/allocation/`.

**Background.** *CSMI* (Common Storage Management Interface) is a Windows driver interface some RAID
controllers offer. *RAID* (Redundant Array of Independent Disks) joins disks for speed (raid0), safety
(raid1, mirrors) or both. Linux does RAID in software through *md* (`mdadm`), *device-mapper* (LVM) or
the filesystem itself (btrfs).

---
## RocketRAID  
`rocket-raid` - Not present on most PCs

**In plain words.** Looks for a HighPoint RocketRAID (or other HighPoint) controller card.

**What you see.** "Checked: PCI vendor 0x1103 (HighPoint)", any matching PCI devices, and Status:
"Not present on this system" or "N device(s) found; Linux manages these with the HighPoint rr*
driver".

**Healthy / typical values.** "Not present on this system" on the author's PC and nearly all desktops.

**Unusual values and what they suggest.** A found card with driver `-` means no driver is bound:
HighPoint drivers are usually out-of-tree, so install the vendor package (often a DKMS module).

**Where Linux gets it.** `/sys/bus/pci/devices/*/vendor`.

---
## SCSI devices  
`scsi` - Linux data

**In plain words.** Everything the Linux SCSI layer manages. That includes SATA disks, USB sticks,
USB card readers and optical drives, because Linux talks to all of them using SCSI commands.

**What you see.**

| Column | Meaning |
|---|---|
| H:C:T:L | Host : Channel : Target : LUN, the SCSI address |
| Type | Direct-Access (disk), CD/DVD, Sequential (tape), Enclosure, RAID, ZBC (zoned disk) and others |
| Vendor / Product / Revision | As the device reports over SCSI (SATA disks say `ATA` as vendor and cut the model to 16 characters) |
| Block | The block device name (`sda`, `sr0`) |
| Generic | The SCSI generic device (`sg0`...) used by tools such as `smartctl` and `sg_inq` |
| Host | Host adapter number and its driver (`ahci`, `usb-storage`, `uas`...) |
| State | `running` is normal |

The last line lists every SCSI host and its driver.

**Healthy / typical values.** The author's PC: the Seagate SATA disk on `host0 ahci`, and USB devices on
their own hosts with `usb-storage`. A multi-slot card reader appears as one target with several LUNs
(`8:0:0:0` and `8:0:0:1`), one per slot.

**Unusual values and what they suggest.**
- State `offline` or `blocked`: the kernel stopped talking to the device after errors or a reset.
- A fast USB SSD on `usb-storage` rather than `uas`: the slower BOT protocol is in use, often because
  the bridge is on a quirks list. This costs speed.
- A host driver shown as `(null)`: the driver does not publish a name in sysfs. The `uas` driver does
  this; see [USB disk drives](#usb-disk-drives) for the real driver.

**Where Linux gets it.** `/sys/class/scsi_device/*/device/{type,vendor,model,rev,state,block,scsi_generic}`
and `/sys/class/scsi_host/*/proc_name`.

**Background.** *SCSI* (Small Computer System Interface) began as a parallel cable standard, but its
command set lives on: SATA, USB storage and SAS all carry SCSI-style commands. A *LUN* (Logical Unit
Number) is one device behind one address, such as one slot of a card reader.

---
## Samsung NVMe (secnvme)  
`secnvme` - Linux data - Needs root data

**In plain words.** The same NVMe controller report as [NVMe (stornvme)](#nvme-stornvme), limited to
Samsung controllers. On Windows, "secnvme" is Samsung's own NVMe driver. Linux uses the in-kernel
`nvme` driver for all brands, so this is just a Samsung filter.

**What you see.** For each controller with PCI vendor 0x144d (or "Samsung" in the model), all the
fields described under [NVMe (stornvme)](#nvme-stornvme). If none: "No Samsung NVMe controllers found".

**Healthy / typical values.** The author's Samsung 980 PRO 1TB: PCI ID 144d:a80a, PCIe link "16.0 GT/s
PCIe x4 (max 16.0 GT/s PCIe x4)", Composite temperature 37.9C, one namespace of 931.51GB with 512-byte
LBAs.

**Unusual values and what they suggest.** See [NVMe (stornvme)](#nvme-stornvme). The Samsung tool
"Magician" is Windows-only; on Linux, firmware updates use Samsung's bootable ISO or `fwupd` where
supported.

**Where Linux gets it.** `/sys/class/nvme/*` and the `srs-root` cache.

---
## Disk sector sizes  
`sectors` - Linux data

**In plain words.** The block sizes each disk reports, its preferred I/O sizes, its I/O scheduler,
and whether each partition starts on a well-aligned boundary.

**What you see.** A table per disk:

| Column | Meaning | Why it matters |
|---|---|---|
| Logical | Smallest block the OS can address (usually 512, sometimes 4096) | |
| Physical | Real block size inside the drive (512 or 4096) | A 4096-physical / 512-logical drive ("512e") does slow read-modify-write cycles on badly aligned writes. |
| HW Sector | Hardware sector size the driver uses | |
| Min I/O | Smallest efficient I/O | |
| Optimal I/O | Preferred I/O size (0 = not reported); RAID arrays report the stripe size here | |
| Align Ofs | Alignment offset in bytes (should be 0) | Non-zero means the drive's first block is shifted. |
| Discard Gran | Discard (TRIM) granularity; 0 = no discard support | 0 on an SSD means TRIM cannot be used. |
| Max I/O | Largest single request the kernel will send (KB) | |
| Scheduler | Active I/O scheduler (`none`, `mq-deadline`, `bfq`, `kyber`, or others) | |

Then **Partition alignment**: for each partition, its start sector, byte offset, and three checks:
**Physical** (start is a multiple of the physical block size), **4K**, and **1MiB**.

**Healthy / typical values.** On the author's PC all NVMe and SATA drives report 512/512, align offset 0,
and every main partition starts at sector 2048 (1 MiB) or another 1 MiB multiple, so all three boxes are
ticked. NVMe drives use `none` (or another low-overhead scheduler) and spinning or USB disks use `bfq`.
zram reports 4096 for everything, as expected for a RAM-backed device.

**Unusual values and what they suggest.**
- **4K unticked** on a partition of a 4K-physical drive: every write may touch two physical blocks.
  This was common with Windows XP-era partitioning (start sector 63). Recreate or move the partition.
- **1MiB unticked but 4K ticked**: fine for performance. It is common on live-USB images (the
  author's Garuda stick starts its first partition at sector 64).
- **Physical and 4K both unticked**: misaligned. On SSDs and 4K drives this costs speed and extra wear.
- Discard granularity 0 on an SSD: TRIM is not possible on this path (common behind USB bridges).

**Where Linux gets it.** `/sys/block/<disk>/queue/{logical_block_size,physical_block_size,hw_sector_size,
minimum_io_size,optimal_io_size,discard_granularity,max_sectors_kb,scheduler}`,
`/sys/block/<disk>/alignment_offset`, and each partition's `start`.

**Background.** An *I/O scheduler* decides the order in which waiting requests go to the drive. Fast
NVMe drives usually do best with `none`; HDDs benefit from `bfq` or `mq-deadline`, which group nearby
requests to save seeks.

---
## SMART drives  
`smart` - Linux data - Needs root data

**In plain words.** A health summary of each drive: is SMART available, the temperature, the drive's
own pass/fail verdict, and how many hours it has been powered on. The full reports are in
[SMART all drives](#smart-all-drives).

**What you see.**

| Column | Meaning |
|---|---|
| Drive | Kernel name |
| Model (Serial) | Model and serial |
| Bus | NVMe, SATA, USB... |
| Size | Capacity |
| SMART | `NVMe log` for NVMe drives; `Enabled` or `Disabled` for ATA drives (from udev); `unknown (USB bridge)` for USB drives; `-` if unknown |
| Temperature | Live, from the kernel's hwmon sensor for that drive, or else from the SMART data |
| Health | The overall verdict: `PASSED` or `FAILED` (from `smartctl`), or for NVMe-only data "OK (no critical warning)" or "CRITICAL WARNING" |
| Power On | Power-on hours |

Below the table: either "Health / attributes: Needs root" (no `srs-root` data yet), or "srs-root
cache: N drives". Optical drives and devices without media are left out.

`smart:<dev>` (for example `smart:sda`) shows one drive in full: bus, firmware, every temperature
sensor, then for NVMe the controller state, or for ATA drives the feature flags udev knows (SMART,
SMART enabled, Security, Security frozen, Power management, APM, Power-up in standby, Write cache,
Read look-ahead, HPA, Download microcode, SATA 3Gb/s, SATA 1.5Gb/s) and the WWN, and finally the
whole cached `smartctl -x` report.

**Healthy / typical values.** On the author's PC all four internal drives say `PASSED`. NVMe SSDs run
at 35-40C at idle, the Seagate HDD at 38C. Power-on hours: 24,137 for the Samsung SSD (about 2.75
years of running time), 640 for a new PNY SSD, 76,271 for the Seagate (about 8.7 years). The three USB
sticks and card readers show `unknown (USB bridge)` and `-` because `srs-root` skips USB disks by
default.

**Unusual values and what they suggest.**
- `FAILED`: the drive itself says it is failing or about to fail. Back up now and replace it.
- `PASSED` does not mean "perfect". The verdict only flips when a normalised value crosses its
  threshold; a drive can pass while its raw counters (for example reallocated sectors) are rising.
  Read the attributes in [SMART all drives](#smart-all-drives).
- `Disabled`: SMART is switched off in the drive; `smartctl -s on /dev/sdX` turns it on.
- NVMe temperature above about 70C at idle: poor airflow or a missing heatsink. HDDs prefer to stay
  below about 45-50C.

**Where Linux gets it.** The `srs-root` cache (`~/.cache/srs/root/smart-<dev>.txt` from `smartctl -x`,
and `nvme-<dev>.txt` from `nvme smart-log`), the udev `ID_ATA_FEATURE_SET_*` properties, and the
`nvme` / `drivetemp` hwmon sensors.

**Background: what SMART is.** *SMART* (Self-Monitoring, Analysis and Reporting Technology) is a
log the drive keeps about itself: error counts, wear, temperatures, hours. Reading it needs raw access
to the drive, which needs root; that is why SRS collects it with `srs-root` and caches it. USB disks
are skipped by default because many USB bridges do not pass SMART commands through, and some flaky
enclosures disconnect when they get one. Set `SRS_SMART_USB=1` to include them anyway.

**Background: how to read ATA (SATA) SMART attributes.** SATA and older drives report a table of
numbered *attributes*. Each row has these columns (as `smartctl` prints them):

| Column | Meaning |
|---|---|
| ID# / ATTRIBUTE_NAME | The attribute number and its usual name. Numbers are mostly standard; meanings can differ by maker. |
| FLAGS | Letters: **P** pre-failure (crossing the threshold predicts failure), **O** updated during normal use, **S** speed/performance, **R** error rate, **C** event count, **K** auto-keep. |
| VALUE | The *normalised* value: the drive's own score, usually starting at 100 (or 200, or 253) and going **down** as things get worse. |
| WORST | The lowest normalised value ever recorded. |
| THRESH | The *threshold*: the maker's failure line. If VALUE drops to or below THRESH, that attribute has failed. 0 means "never fails, information only". |
| FAIL | `-` = fine; `FAILING_NOW` = VALUE is at or below THRESH now; `In_the_past` (shown short as `Past`) = WORST once dropped to or below THRESH, but VALUE has since recovered. |
| RAW_VALUE | The real counter (sectors, hours, degrees, events). Its meaning and format are up to the maker. |

So: *normalised* values are the maker's verdict on a common scale; *raw* values are the actual counts.
For counters such as reallocated sectors, the raw number is the one to watch, because the normalised
value often stays at 100 until things are already bad. For "rate" attributes on Seagate drives (1 Raw
Read Error Rate, 7 Seek Error Rate, 195 Hardware ECC Recovered) the raw number is a packed value of
several counters and looks huge even on a healthy drive; judge those by the normalised value only.

Attributes that matter most:

| ID | Name | What it counts | Worry when |
|---|---|---|---|
| 5 | Reallocated_Sector_Ct | Bad sectors the drive has replaced with spares | Raw above 0 and growing. A few may be stable for years; a rising count means the surface is failing. |
| 197 | Current_Pending_Sector | Sectors that could not be read and wait to be remapped | Any raw above 0. These are unreadable right now. |
| 198 | Offline_Uncorrectable | Sectors that failed during the drive's own offline scan | Any raw above 0. |
| 187 | Reported_Uncorrect | Errors the drive could not fix with ECC and reported to the host | Raw above 0 means data was lost on a read. |
| 10 | Spin_Retry_Count | Spin-ups that needed a retry (HDD) | Raw above 0: motor or power problems. |
| 184 | End-to-End_Error | Data corrupted inside the drive between cache and media | Any raw above 0. |
| 188 | Command_Timeout | Commands that timed out | Rising counts: cable, power or drive problems. Seagate packs three counters into the raw value, so judge by the normalised value. |
| 199 | UDMA_CRC_Error_Count | Data corrupted on the cable between drive and controller | A rising count points to a bad SATA cable or connector, not the disk itself. The count never resets. |
| 9 | Power_On_Hours | Hours powered on | Information; compare with the drive's rated life. |
| 12 | Power_Cycle_Count | Times powered on | Information. |
| 4 / 193 | Start_Stop_Count / Load_Cycle_Count | Spin-ups / head parking events (HDD) | Load cycles growing by hundreds per day: aggressive power saving (APM) is wearing the drive. |
| 190 / 194 | Airflow_Temperature_Cel / Temperature_Celsius | Temperature (raw = degrees C; extra numbers are min/max) | Above the drive's rated maximum, often 55-60C. |
| 177 / 231 / 233 / 202 | Wear_Leveling_Count / SSD_Life_Left / Media_Wearout_Indicator / Percent_Lifetime_Remain | SSD wear (names vary by maker) | Normalised value near the threshold, or "life left" under about 10%. |

**Example from the author's Seagate 4TB disk.** VALUE / WORST / THRESH for 5 Reallocated_Sector_Ct are
100 / 100 / 010 with raw 0: no bad sectors. 197 and 198 are also 0. 190 Airflow_Temperature_Cel is
064 / 041 / 045 and its FAIL column says `Past`: Seagate scores this attribute as 100 minus the
temperature, so a WORST of 41 means the disk once reached 59C, above its limit of 55C (the SCT data
agrees: lifetime maximum 59C). The disk is fine now at 36C, but it was too hot at some point. 199
UDMA_CRC_Error_Count raw is 19: there were cable glitches in the past; if the number stays at 19 there
is nothing to do, if it grows, replace the SATA cable. 1, 7 and 195 have huge raw values that are
normal for Seagate.

**Background: how to read NVMe SMART.** NVMe drives have no attribute table. They give one fixed
"SMART / Health Information" log page:

| Field | Meaning | Worry when |
|---|---|---|
| Critical Warning | A bit field; 0x00 = all good. Bits mean: spare below threshold, temperature over limit, reliability degraded, read-only mode, volatile backup failed. | Anything other than 0x00. |
| Temperature / Temperature Sensor N | Composite and per-sensor temperatures | Near the "Warning Comp. Temp. Threshold" given in the report. |
| Available Spare | Spare flash blocks left, as a percentage (starts at 100%) | Falling towards the Available Spare Threshold. |
| Available Spare Threshold | The level at which the drive raises a critical warning | |
| Percentage Used | The drive's estimate of how much of its rated write life is used | Near or above 100%. It can go past 100; the drive still works but is past its rating. |
| Data Units Read / Written | Data moved, in units of 1,000 x 512 bytes (512,000 bytes); smartctl adds the total in TB | Compare written TB with the drive's TBW (terabytes written) rating on its spec sheet. |
| Host Read / Write Commands | Commands completed | Information. |
| Controller Busy Time | Minutes the controller was busy | Information. |
| Power Cycles / Power On Hours | Power-ups and powered hours | Information. |
| Unsafe Shutdowns | Times power was lost without a proper shutdown notice | A high share of power cycles points to hard resets or power cuts; usually harmless, but data in flight can be lost. |
| Media and Data Integrity Errors | Unrecovered data errors | Any value above 0. |
| Error Information Log Entries | Entries in the error log over the drive's life | Rising numbers; many drives log harmless entries, so check the error log itself. |
| Warning / Critical Comp. Temperature Time | Minutes spent above the warning / critical temperature | Growing values: improve cooling. |
| Thermal Temp. N Transition Count / Total Time | How often and how long the drive slowed itself to cool down | Many transitions: the drive throttles under load. |

**Example from the author's Samsung 980 PRO 1TB.** Critical Warning 0x00, Available Spare 100%
(threshold 10%), Percentage Used 38%, about 333 TB written, Media and Data Integrity Errors 0, and 7,932
unsafe shutdowns out of 11,989 power cycles. The drive is healthy; it has used about a third of its
rated write life. The high unsafe-shutdown share means the PC was often reset or powered off without a
clean shutdown.

**Self-test logs.** Both kinds of drive keep a list of recent self-tests (short and extended). "Completed
without error" is good. "Aborted" (by the host, by a command, or by a controller reset) only means the
test was interrupted, often by a reboot or by the system going to sleep; run it again. A test that ends
with "read failure" and a failing LBA means a real bad sector.

---
## SMART all drives  
`smart-all` - Linux data - Needs root data

**In plain words.** The full SMART report of every drive, one after another. This is the place to read
the attributes explained under [SMART drives](#smart-drives).

**What you see.** For each drive with media (optical drives skipped):
- A heading with the drive name and model (serial).
- **Bus / type**, **Firmware**, **Temperatures** (every hwmon sensor, for example "Composite 37.9C
  Sensor 1 37.9C Sensor 2 45.9C" on an NVMe SSD), **SMART** (as in the SMART column of
  [SMART drives](#smart-drives)).
- **Source**: the cache file the report came from.
- The cached `smartctl -x` output in full: an information section (model, firmware, capacity, versions,
  power states, temperature thresholds), the SMART data section (health verdict, attributes or the NVMe
  health log), error logs, self-test logs, and for SATA drives also SCT temperature history, device
  statistics and SATA phy event counters.
- For USB disks without `SRS_SMART_USB=1`: "skipped: USB-attached (set SRS_SMART_USB=1 to include)".
- Without `srs-root` data: "SMART data: Needs root".

`smart-all:<dev>` gives the same single-drive view as `smart:<dev>`.

**Healthy / typical values.** See the examples under [SMART drives](#smart-drives). For a SATA disk,
the SCT section shows "Lifetime Min/Max Temperature" (9/59 C on the author's Seagate) and a
10-minute temperature history. "?" entries in that history are times when the PC was off or asleep.
The SATA phy event counters should be 0 apart from a few COMRESET entries after boots.

**Unusual values and what they suggest.** Non-zero "Command failed due to ICRC error" or R_ERR counters
in the SATA phy events point at the cable or port, like attribute 199. "ATA Security is: ... frozen"
is normal: the BIOS locks the security commands at boot so nothing can set a drive password.

**Where Linux gets it.** `~/.cache/srs/root/smart-<dev>.txt`, written by `srs-root` running
`smartctl -x /dev/<dev>`; NVMe drives also get `nvme-<dev>.txt` from `nvme smart-log`.

**Background.** *SCT* (SMART Command Transport) is a newer ATA channel for extra data such as
temperature history and error recovery timeouts. *SCT Error Recovery Control* (shown as e.g. "Read:
100 (10.0 seconds)") limits how long the drive retries a bad sector; NAS and RAID disks set it low so
the array does not drop the disk.

---
## SMART drive monitor  
`smart-sdm` - Linux data - Needs root data

**In plain words.** A compact live table of drive temperatures, with power-on hours and power cycles.
In the GUI it refreshes about every 2 seconds.

**What you see.**

| Column | Meaning | Source |
|---|---|---|
| Drive / Model | Kernel name and model | sysfs / udev |
| Current | Temperature now | hwmon `temp1_input` (live, no root) |
| Lowest | Lowest since the sensor started | hwmon `temp1_lowest` (drivetemp only) |
| Highest | Highest since the sensor started | hwmon `temp1_highest` (drivetemp only) |
| Critical | The drive's critical temperature limit | hwmon `temp1_crit` (NVMe) |
| Power On | Power-on hours | `srs-root` SMART cache |
| Cycles | Power cycles | `srs-root` SMART cache |

`-` means the value is not available. Without `srs-root` data, a note says power-on hours and cycles
need root.

**Healthy / typical values.** On the author's PC: NVMe drives at 35.9-39.9C with critical limits of
84.8C (Samsung), 109.8C (ADATA) and 94.8C (PNY); the Seagate at 38.0C with lowest 34.0C and highest
41.0C since boot. NVMe drives show `-` for Lowest/Highest because the kernel NVMe sensor does not
track them; SATA drives show `-` for Critical because `drivetemp` does not always report one.

**Unusual values and what they suggest.** Current within about 10C of Critical: the drive is close to
throttling. A Highest far above Current on an HDD: a heavy job or poor airflow earlier. USB drives
show `-` everywhere: no hwmon sensor and no SMART data through the bridge.

**Where Linux gets it.** `/sys/class/nvme/<ctrl>/hwmonN/` for NVMe and
`/sys/block/<disk>/device/hwmon/hwmonN/` for SATA (the `drivetemp` kernel module), plus the
`srs-root` cache.

**Background.** *hwmon* is the kernel's hardware monitoring interface. The `drivetemp` module reads
SATA drive temperatures through SMART without root; load it with `modprobe drivetemp` if SATA
temperatures are missing.

---
## Storage spaces  
`spaces` - Linux data - Needs root data

**In plain words.** On Windows, "Storage Spaces" pools several disks into one. The Linux equivalents
are btrfs (which can span disks) and device-mapper / LVM / md. This item lists all of them. In
practice everything shown here comes from public sysfs files, so it is complete without `srs-root`.

**What you see.**
- **btrfs filesystems (pooled storage)**: for each btrfs filesystem, a "btrfs <label>" heading and the
  full btrfs detail described under [NTFS statistics](#ntfs-statistics): fsid, label, node and sector
  size, checksum, generation, features, devices, allocation per type with profile, global reserve,
  per-device error counters, commit stats.
- **device-mapper / LVM / md volumes**: name, size, kind (`LVM`, `dm-crypt`, or the md RAID level) and
  the underlying devices.
- **Pools**: "N btrfs, N dm/md".

**Healthy / typical values.** On the author's PC: three btrfs filesystems (LINUX, SteamLibrary,
500GB_SSD), each on one device, with error counters all 0, and no dm/md volumes.

**Unusual values and what they suggest.** A btrfs filesystem whose Devices count is lower than you
set up: a member disk is missing (the filesystem may only mount with `-o degraded`). Non-zero error
counters: see [NTFS statistics](#ntfs-statistics). A dm-crypt line with no mount: an unlocked
encrypted container that is not mounted.

**Where Linux gets it.** `/sys/fs/btrfs/<fsid>/`, `/sys/block/dm-*/dm/`, `/sys/block/md*/md/`.

**Background.** *LVM* (Logical Volume Manager) and *dm-crypt* (disk encryption) are built on the
kernel's *device-mapper*, which maps one virtual block device onto parts of real ones.

---
## NVMe (stornvme)  
`stornvme` - Linux data - Needs root data

**In plain words.** A detailed card for each NVMe SSD controller. On Windows, "stornvme" is
Microsoft's NVMe driver; on Linux the in-kernel `nvme` driver handles every brand, and SRS reads what
it publishes.

**What you see.** For each controller (`/dev/nvme0`, `/dev/nvme1`...):

| Field | Meaning | Why it matters |
|---|---|---|
| Model / Serial number / Firmware | Identity | Firmware updates fix real bugs on many SSDs. |
| State | `live` is normal | `resetting`, `connecting`, `dead`: the drive is having trouble. |
| Transport | `pcie` for an internal SSD (`tcp`, `rdma`, `fc` for network NVMe) | |
| Controller ID | The controller's number inside the subsystem | |
| Controller type | `io`, `admin`, `discovery`, or `reserved` (older drives that do not report a type) | |
| Subsystem NQN | NVMe Qualified Name: a unique name for the drive, often with the serial in it | |
| I/O queues | Number of queues the driver set up (often one per CPU, plus one) | |
| SQ size | Submission queue depth | |
| NUMA node | Which memory node the slot belongs to (`-1` on single-node desktops) | |
| Keep-alive | Keep-alive timeout (0 for local PCIe drives) | |
| PCI address / PCI ID | Where it sits on the PCI bus, and vendor:device ID | |
| PCIe link | Current speed and width, with the maximum in brackets; green when they match, orange when lower | The single most useful line here: shows whether the SSD runs at full speed. |
| Driver | `nvme` | |
| Temperatures | Every sensor (Composite plus Sensor 1, 2...) | |
| Namespace nvmeXnY | Size and LBA (block) size of each namespace | |

Then either "SMART log available in the srs-root cache, see [smart]" or, if no cache exists,
"Identify/SMART: Needs root".

**Healthy / typical values.** On the author's PC all three SSDs show "16.0 GT/s PCIe x4 (max 16.0 GT/s
PCIe x4)" (PCIe 4.0 x4, full speed), state `live`, 17 I/O queues on a 16-thread CPU, and NUMA node -1.
The PNY's NQN contains its serial, shown here as `nqn.2014-08.com.00000:nvme:XXXX`.

**Unusual values and what they suggest.**
- PCIe link lower than max, e.g. "8.0 GT/s x4 (max 16.0 GT/s x4)": a slot that only runs PCIe 3.0, a
  BIOS setting, or a riser/adapter. "x2" on an x4 drive: the M.2 slot shares lanes with another device
  (check the board manual). Some drives also drop speed at idle to save power, so check under load.
- Temperatures: NVMe drives start throttling near the warning threshold (shown in
  [SMART all drives](#smart-all-drives)).
- State not `live`: see `dmesg` for "nvme ... timeout" or "resetting controller" messages; often a
  power-saving (APST) problem or failing drive.

**Where Linux gets it.** `/sys/class/nvme/<ctrl>/{model,serial,firmware_rev,state,transport,cntlid,
cntrltype,subsysnqn,queue_count,sqsize,numa_node,kato}`, the PCI device's `current_link_speed`,
`current_link_width`, `max_link_speed`, `max_link_width`, and the controller's hwmon sensor.

**Background.** *NVMe* (Non-Volatile Memory Express) is the protocol fast SSDs use over *PCIe*
(PCI Express). *GT/s* (gigatransfers per second) is the per-lane speed: 8.0 = PCIe 3.0, 16.0 = PCIe
4.0, 32.0 = PCIe 5.0. A *namespace* is a slice of the SSD presented as one disk; consumer SSDs have one.

---
## USB disk drives  
`usb-disks` - Linux data

**In plain words.** Every disk connected over USB, with the bridge chip inside the enclosure, its
USB speed and the driver in use.

**What you see.**

| Column | Meaning | Why it matters |
|---|---|---|
| Disk | Kernel name | |
| Vendor and Product | What the disk reports (with serial) | |
| USB Bridge (Manufacturer Product) | What the USB device reports | Shows the real chip or enclosure brand. |
| VID:PID | USB vendor and product ID | Search this to find the exact bridge chip and known quirks. |
| Speed | Link speed in Mbit/s: 480M = USB 2.0, 5000M = USB 3.x Gen 1, 10000M = Gen 2, 20000M = Gen 2x2 | A fast drive on 480M is stuck at USB 2.0 speed. |
| Driver | `uas` (USB Attached SCSI, faster) or `usb-storage` (older Bulk-Only Transport) | |
| Size | Capacity, or "no media" | |
| Port | USB bus and port path, e.g. `2-2.4.1` = bus 2, port 2, hub port 4, hub port 1 | |

The last line counts USB disks.

**Healthy / typical values.** On the author's PC: a USB NVMe enclosure (JMicron 152d:0583) at 10000M
on `uas`; a USB 3.0 card reader (05e3:0749) at 5000M with two slots (`sdd`, `sde`); a USB 2.0 flash
drive and a DVD writer at 480M on `usb-storage`.

**Unusual values and what they suggest.**
- A USB 3 drive at 480M: it is in a USB 2.0 port, on a USB 2.0 hub or cable, or the connector is
  not fully seated.
- A fast SSD enclosure on `usb-storage` instead of `uas`: the kernel turned UAS off for this bridge
  (a known-quirk list), costing speed but avoiding hangs.
- "no media" on an enclosure that should hold a drive: the drive inside is not detected. On the
  author's PC the NVMe enclosure shows this.

**Where Linux gets it.** The sysfs path of each `/sys/block/<disk>`, walked up to the USB device
(`idVendor`, `idProduct`, `manufacturer`, `product`, `speed`) and the interface `driver` link.
Devices are not opened.

**Background.** A *USB bridge* is the chip in an enclosure, stick or reader that turns USB into SATA,
NVMe or flash commands. Its quality decides speed, TRIM support and whether SMART works at all.

---
## Volume list  
`vol-list` - Linux data

**In plain words.** Every disk and partition with its filesystem type, version, label, UUID, size and
mount points: the same idea as `lsblk -f`.

**What you see.** Columns **Name** (disks at the left, partitions indented), **FSType** (vfat, ntfs,
btrfs, ext4, swap, iso9660...), **Version** (filesystem version, e.g. FAT32, FAT16, FAT12, exFAT
1.0, swap 1, "Joliet Extension" for ISO images), **Label**, **UUID**, **Size**, **Mount points**
(all of them, or `[SWAP]`). zram and other virtual disks are included.

**Healthy / typical values.** On the author's PC: `nvme0n1p1 vfat FAT32 EFI 2.05GB /boot/efi`,
`nvme1n1p4 swap 1 SWAP 32.81GB [SWAP]`, `zram0 swap 1 zram0 15.23GB [SWAP]`. A small Ventoy EFI
partition is FAT16 and a 4MB live-USB EFI partition is FAT12, both normal for their size. NTFS
partitions show a 16-hex-digit UUID.

**Unusual values and what they suggest.** A whole disk showing the same filesystem as its first
partition (for example `iso9660` on both `sde` and `sde1`): an isohybrid live image, normal. A partition
with an empty FSType: no filesystem udev recognises (or an encrypted or damaged one). Two partitions
with the same UUID: a cloned disk; mounting by UUID becomes unpredictable, so change one with
`tune2fs -U`, `btrfstune -u` or similar.

**Where Linux gets it.** `/sys/class/block`, the udev database (`ID_FS_TYPE`, `ID_FS_VERSION`,
`ID_FS_LABEL_ENC`, `ID_FS_UUID`), `/proc/self/mountinfo`, `/proc/swaps`. No `blkid` is run.

---
## Volumes  
`volumes` - Linux data

**In plain words.** The main "how full are my drives" table: every mounted disk filesystem and network
share, with free space, total space, and percent used. This is what the **Volumes** panel button opens.

**What you see.**

| Column | Meaning |
|---|---|
| Drive and Path | Mount point |
| Volume Label | Filesystem label, `<none>`, or the share path for network mounts |
| Format | Filesystem type; `(R)` means mounted read-only |
| Quota | Empty (kept for the SIV layout) |
| Free | Space available to normal users |
| Total | Filesystem size |
| Used | Percent of the filesystem in use (total minus all free blocks) |
| Cluster | Block size the filesystem reports |
| Drive Type | Fixed, Removable, CD Drive, RAM Disk, Virtual, Network |
| Volume Serial | FAT serial, NTFS serial (the low 8 hex digits of its ID, as Windows shows it), or the UUID |

Network mounts show `-` for sizes: SRS never queries them, because a stalled server would freeze the
query. After the table come **Total** rows per drive type: "count (distinct)", where count is the
number of mount points and the bracketed number is how many different filesystems they are; Free,
Total and Used are added up once per filesystem.

**Per-volume detail** (`volumes:<mount point or device>`, or the **Drive /** button for `/`): Mount
point, Source, Device (with major:minor numbers), Physical disk, Drive type (type, bus, media),
Format (with version), Volume label, Volume serial / UUID, Partition (number, GPT name, type GUID),
Subvolume / bind root (for btrfs subvolumes and bind mounts, e.g. `/@`), Mount options, Super options
(filesystem-level options such as `compress=zstd:3,ssd,discard=async`), Total, Free ("available" and
"incl. reserved"), Used (amount and percent), Block / fragment size, Inodes (total and free, `n/a`
for filesystems such as btrfs that do not have a fixed count), Max name length, Access (read/write or
read-only), followed by the filesystem statistics described under [NTFS statistics](#ntfs-statistics).

**Healthy / typical values.** On the author's PC the btrfs root shows 146.84GB free of 929.46GB (83%
used) on seven mount points; the totals line "Fixed 14 (5)" means 14 mount points but only 5 different
filesystems. The read-only NTFS bind mount shows `ntfs3 (R)`. An ISO live stick shows 100% used with
0 free, which is normal for a read-only ISO 9660 filesystem.

**Unusual values and what they suggest.**
- Used above 90% on the root filesystem: updates and logs can fail; btrfs gets slow and may report
  "no space" early.
- Free (available) much lower than "incl. reserved" on ext4: ext4 keeps 5% for root by default;
  `tune2fs -m 1` lowers that on a big data disk.
- `(R)` on a filesystem you did not mount read-only: the kernel switched it after errors, or NTFS is
  dirty (see [NTFS statistics](#ntfs-statistics)).

**Where Linux gets it.** `/proc/self/mountinfo`, `statvfs()` on each local mount point, the udev
`ID_FS_*` and `ID_PART_ENTRY_*` properties, and sysfs for the disk under each partition.

**Background.** An *inode* is a filesystem's record for one file. ext4 creates a fixed number when
formatted, so it can run out of inodes before it runs out of space; btrfs and XFS create them as needed.
*Reserved space* is kept back for root so the system can still log and recover when users fill a disk.

---
## Intel VROC  
`vroc` - Not present on most PCs

**In plain words.** Looks for Intel VROC (Virtual RAID on CPU), Intel's RAID for NVMe drives attached
to the processor, found on workstation and server boards.

**What you see.** "Checked: PCI vendor 0x8086 class 0x0104 (Intel RAID/VMD)", any matching PCI
devices (address, ID, class, driver), and Status: "Not present on this system" or "N device(s) found;
Linux manages these with vmd + mdadm (IMSM)".

**Healthy / typical values.** "Not present on this system" on the author's AMD PC and most desktops.

**Unusual values and what they suggest.** On Intel laptops with "RAID / Intel RST" mode on in the
BIOS, a VMD controller appears and NVMe drives hide behind it. Linux supports it with the `vmd`
driver; if Linux cannot see the SSD at all, switching the BIOS storage mode to AHCI/NVMe is the
common fix (Windows then needs its drivers adjusted first).

**Where Linux gets it.** `/sys/bus/pci/devices/*/{vendor,class}`.

**Background.** *VMD* (Volume Management Device) is the Intel PCIe block VROC and RST use to gather
NVMe drives. Linux builds the RAID itself with `mdadm` in Intel's *IMSM* (Intel Matrix Storage Manager)
metadata format, so the array also works under Windows.
