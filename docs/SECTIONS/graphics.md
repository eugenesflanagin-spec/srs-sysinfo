# Graphics / Monitors / Audio  (SRS Section Reference)

[Back to the section index](README.md) - [Glossary](../GLOSSARY.md)

This group covers what you see and hear: the graphics card (GPU) with its clocks, temperatures, fans, power, firmware and PCIe link, the monitors plugged into it, the 3D and compute software (Vulkan, OpenGL, OpenCL), and sound, MIDI and game-controller devices. Many item names come from Windows (Direct3D, DirectX, dxdiag, RTSS); for those, SRS shows the closest Linux equivalent and says so in the item's title line. Open any item from **Menu -> Graphics / Monitors / Audio -> item**, or click the **Graphics** button on the Linux panel. Only **GPU register dump** (`gpu-dump`) needs root data: it shows the full PCI config space after you run **Menu -> Run as root (srs-root)** once. Everything here is read only: SRS never writes GPU settings, never changes fan speeds and never talks over the monitor's I2C/DDC bus.

The examples come from the author's PC: AMD Ryzen 7 7800X3D, ASUS TUF GAMING X870-PRO WIFI7 board, XFX AMD Radeon RX 9070 XT (amdgpu driver, RDNA 4), an LG ULTRAGEAR 1440p 165 Hz monitor on DisplayPort and a BenQ RL2755 1080p monitor on HDMI.

## Contents
- [Audio devices](#audio-devices) `audio`
- [Direct3D info](#direct3d-info) `d3d-info`
- [GPU D3D engine nodes](#gpu-d3d-engine-nodes) `d3d-node`
- [DirectX information](#directx-information) `directx`
- [DirectX sound devices](#directx-sound-devices) `dxd-aud`
- [DirectX system devices](#directx-system-devices) `dxd-dev`
- [DirectX logical disks](#directx-logical-disks) `dxd-dsk`
- [DirectX files](#directx-files) `dxd-fil`
- [DirectShow filters](#directshow-filters) `dxd-ftr`
- [DirectInput gameports](#directinput-gameports) `dxd-gam`
- [DirectX GPU displays](#directx-gpu-displays) `dxd-gpu`
- [DirectX system information](#directx-system-information) `dxd-inf`
- [DirectInput devices](#directinput-devices) `dxd-inp`
- [DirectMusic ports](#directmusic-ports) `dxd-mic`
- [DirectPlay adapters](#directplay-adapters) `dxd-ply`
- [DirectInput PS/2 devices](#directinput-ps2-devices) `dxd-ps2`
- [DirectX summary](#directx-summary) `dxd-sum`
- [DirectInput USB root](#directinput-usb-root) `dxd-usb`
- [GOverlay status](#goverlay-status) `goverlay`
- [GPU all details](#gpu-all-details) `gpu-all`
- [GPU API routines](#gpu-api-routines) `gpu-api`
- [GPU applications](#gpu-applications) `gpu-apps`
- [GPU VBIOS status](#gpu-vbios-status) `gpu-bios`
- [GPU clock speeds](#gpu-clock-speeds) `gpu-clocks`
- [CUDA information](#cuda-information) `gpu-cuda`
- [GPU D3D status](#gpu-d3d-status) `gpu-d3d`
- [GPU register dump](#gpu-register-dump) `gpu-dump`
- [GPU export](#gpu-export) `gpu-export`
- [GPU fans](#gpu-fans) `gpu-fans`
- [GPU generations](#gpu-generations) `gpu-gens`
- [GPU G-Sync / VRR](#gpu-g-sync--vrr) `gpu-g-sync`
- [GPU I2C buses](#gpu-i2c-buses) `gpu-i2c`
- [GPU information](#gpu-information) `gpu-info`
- [GPU LEDs](#gpu-leds) `gpu-leds`
- [GPU mapping](#gpu-mapping) `gpu-map`
- [GPU power status](#gpu-power-status) `gpu-power`
- [GPU registry](#gpu-registry) `gpu-reg`
- [GPU SCF](#gpu-scf) `gpu-scf`
- [GPU SLI topology](#gpu-sli-topology) `gpu-sli`
- [GPU temperatures](#gpu-temperatures) `gpu-temp`
- [GPU tree](#gpu-tree) `gpu-tree`
- [Graphics](#graphics) `graphics`
- [Joysticks](#joysticks) `joysticks`
- [Monitor ID database](#monitor-id-database) `mon-info`
- [Monitors](#monitors) `monitors`
- [Monitor tree](#monitor-tree) `monitor-tree`
- [OpenCL](#opencl) `opencl`
- [RSR info](#rsr-info) `rsr-info`
- [RTSS status](#rtss-status) `rtss-sts`
- [Sound devices](#sound-devices) `sound`
- [UAA (HD Audio) devices](#uaa-hd-audio-devices) `uaa-dev`
- [Wave devices](#wave-devices) `wave`

**Picking a GPU.** Items that show one GPU pick the first card using the `amdgpu` driver, or else the first card found. On a PC with more than one GPU you can open an item as `gpu-info:card1` (or `:1`, or part of the PCI slot such as `:03:00`) to choose another one.

---
## Audio devices
`audio` - Linux data

**In plain words.** A list of the sound cards the kernel found, followed by the PipeWire view of the same hardware: devices, outputs (sinks), inputs (sources) and the programs playing sound right now.

**What you see.**

| Field | Meaning | Why it matters |
|---|---|---|
| `cardN` | ALSA card number (ALSA = Advanced Linux Sound Architecture, the kernel sound layer). | `hw:N,...` device names and mixer tools use this number. |
| Name | The card's long name, e.g. "HD-Audio Generic". | Tells you which chip is which. |
| Driver `[id]` | Kernel driver (`HDA-Intel` covers almost all HD Audio chips, Intel or not) and the short card ID in brackets. | The short ID is what you use in ALSA configs (`hw:Generic`). |
| Second line | Where the card lives: memory address and IRQ (interrupt line). | Mostly for debugging. Two cards on the same IRQ is fine today. |
| PipeWire (`wpctl status`) | The Audio part of `wpctl status`: Devices, Sinks, Sources, Filters, Streams. A `*` marks the default sink or source. `[vol: 0.90]` is the volume (1.00 = 100 %). | This is what applications actually use. If a card is in the ALSA list but missing here, PipeWire or WirePlumber is not using it. |

If `wpctl` is missing but `pactl` is installed, SRS shows the first 40 lines of `pactl info` instead.

**Healthy / typical values.** On the author's PC: `card0 HDA ATI HDMI` (the audio part of the RX 9070 XT, used for sound over HDMI/DisplayPort) and `card1 HD-Audio Generic` (the Realtek ALC1220 on the motherboard). PipeWire lists "Navi 48 HDMI/DP Audio Controller" and "Ryzen HD Audio Controller", and the default sink is a software sink ("JamesDSP Sink") that feeds the real optical output. Streams show programs such as a MIDI synth with `output_FL > ... [active]`.

**Unusual values and what they suggest.**
- No cards at all: the sound driver did not load (check `dmesg` for `snd_hda_intel`), or the onboard audio is disabled in the BIOS.
- Card present in ALSA but no PipeWire device: WirePlumber may have disabled it (profile "Off"), or PipeWire is not running for your user.
- Sinks empty: no usable output profile; pick one in your desktop's sound settings.
- A stream marked `[paused]` is normal for idle programs (speech services, for example).

**Where Linux gets it.** `/proc/asound/cards`, then `wpctl status` (PipeWire) or `pactl info`.

**Background.** Linux sound has two layers. ALSA is the kernel driver layer that talks to the chip. A sound server (today PipeWire, earlier PulseAudio) sits on top, mixes many programs together and routes them to outputs. A *sink* is somewhere sound goes (speakers, HDMI, a virtual effects sink). A *source* is somewhere sound comes from (microphone, line in).

---
## Direct3D info
`d3d-info` - Linux data

**In plain words.** Direct3D is Windows' 3D graphics interface. Linux uses Vulkan and OpenGL instead, so this item shows which Vulkan and OpenGL drivers are installed and which versions they support.

**What you see.**

| Field | Meaning | Why it matters |
|---|---|---|
| Vulkan instance | Version of the Vulkan loader (the library programs talk to first). | Newer loaders support newer features. |
| Vulkan device N | Each Vulkan device: name, then type (`DISCRETE_GPU`, `INTEGRATED_GPU`, `CPU`...), API version, driver name and driver info. | Shows which driver a game will use. |
| OpenGL renderer | The OpenGL renderer string (driver, chip, compiler, kernel DRM version). | Confirms you are on the real GPU, not a software renderer. |
| OpenGL core / compat / ES | Highest OpenGL versions for the core profile, the compatibility profile and OpenGL ES. | Old games need compat; mobile-style apps use ES. |
| GL video memory | Memory the OpenGL driver reports. | Should match your card's VRAM. |
| Direct rendering | `Yes` means the GPU draws directly. | `No` means slow software rendering. |

If `glxinfo` is missing, SRS falls back to `eglinfo` and shows the EGL version too. If neither is installed it says so (package `mesa-utils`).

**Healthy / typical values.** On the author's PC: Vulkan instance 1.4.363; device 0 "AMD Radeon RX 9070 XT (RADV GFX1201)", `DISCRETE_GPU`, driver `radv`, Mesa 27.0.0-devel; device 1 "llvmpipe", type `CPU`. OpenGL renderer "AMD Radeon RX 9070 XT (radeonsi, gfx1201, ACO, DRM 3.64, ...)", core 4.6, ES 3.2, video memory 16384MB, direct rendering Yes.

**Unusual values and what they suggest.**
- Only `llvmpipe` listed, or OpenGL renderer says `llvmpipe`: the GPU driver is not working and everything is drawn by the CPU. Check that `vulkan-radeon` (or your vendor's driver) is installed and that the kernel driver loaded.
- Two GPU entries with the same name: usually two Vulkan drivers installed for one card (for example RADV and AMDVLK). Games may pick the wrong one.
- OpenGL version far below 4.6 on a modern card: old Mesa, or a fallback driver.

**Where Linux gets it.** `vulkaninfo --summary` (run with implicit layers such as overlays turned off for the query) and `glxinfo -B`, or `eglinfo -B`.

**Background.** Mesa is the open-source graphics driver collection. On AMD it contains RADV (the Vulkan driver) and radeonsi (the OpenGL driver). `llvmpipe` is Mesa's software renderer. It is always present and is used only as a last resort.

---
## GPU D3D engine nodes
`d3d-node` - Linux data

**In plain words.** A modern GPU is several engines on one chip: a 3D/compute engine, copy engines, a video engine, a display engine, a security processor and a power-management processor. This item lists each engine and its hardware version.

**What you see.**

| Column | Meaning |
|---|---|
| Engine | Short name of the hardware block (IP block) from AMD's IP discovery table. |
| IP version | Major.minor.revision of that block. |
| Instances | How many copies of the block the chip has. |
| Function | Plain-English job: `GC` 3D / Compute (graphics core), `SDMA0/1` Copy (system DMA), `VCN` or `UVD` video decode/encode, `VCE` video encode, `JPEG` JPEG decode, `DMU` display (DCN), `MP0` security (PSP), `MP1` power (SMU). |

Below the table: **GFX busy** and **Memory busy** in percent (live, refreshed every second in the GUI), and a pointer to [GPU applications](#gpu-applications) for per-program engine time.

**Healthy / typical values.** On the RX 9070 XT: GC 12.0.1, SDMA0 and SDMA1 7.0.1, UVD 5.0.0 (the video engine is reported under the old name UVD), DMU 4.1.0, MP0 and MP1 14.0.3. At the desktop, GFX busy is a few percent (8 % here) and memory busy about 1 %.

**Unusual values and what they suggest.**
- Empty table: the GPU is not AMD, or it is older than about 2019 and has no IP discovery table.
- GFX busy stuck at 100 % with nothing running: a program is still drawing in the background, or the GPU hung.

**Where Linux gets it.** `/sys/class/drm/card0/device/ip_discovery/die/0/<block>/0/{major,minor,revision}`, `gpu_busy_percent`, `mem_busy_percent`.

**Background.** Windows calls these engines "nodes" (3D, Copy, Video Decode...). AMD builds GPUs from reusable blocks called IP blocks, each with its own version. The same video block can appear in several GPU generations, so the block versions tell you more than the marketing name.

---
## DirectX information
`directx` - Linux data

**In plain words.** DirectX is a Windows-only family of interfaces. This item shows the Linux graphics interface versions instead, then checks which DirectX-to-Vulkan translation layers are installed (these let Windows games run through Wine and Proton).

**What you see.**
- **Vulkan instance** - loader version.
- **Vulkan device API** - Vulkan version of the first device, with its name.
- **OpenGL** / **OpenGL ES** - core profile and ES versions from `glxinfo`.
- **EGL** - EGL version from `eglinfo` (EGL connects OpenGL to windows on Wayland).
- **OpenCL** - platform version of the first OpenCL platform, from `clinfo`.
- **DirectX** - always "Windows API - on Linux provided by the translation layers below".
- Translation layers, each with `[=]` (found) or `[_]` (not found) and the path that was checked:
  - DXVK (Direct3D 8/9/10/11 to Vulkan) - `/usr/share/dxvk`
  - VKD3D-Proton (Direct3D 12 to Vulkan) - `/usr/share/vkd3d-proton`
  - wine vkd3d (D3D12) - Wine's own `d3d12.dll`
  - WineD3D (D3D to OpenGL/Vulkan) - Wine's `wined3d.dll`
  - DXVK (user), VKD3D-Proton (user) - copies in `~/.local/share`
  - Steam compat tools (Proton) - `~/.local/share/Steam/compatibilitytools.d`

In the GUI the checks show as ticked or empty boxes.

**Healthy / typical values.** On the author's PC: Vulkan 1.4.363, OpenGL 4.6, OpenGL ES 3.2, EGL 1.5, OpenCL "OpenCL 2.1 AMD-APP (3581.0)". System DXVK, VKD3D-Proton, Wine's vkd3d and WineD3D are all found; the per-user DXVK and VKD3D-Proton folders are not (normal, they are optional).

**Unusual values and what they suggest.**
- No translation layers at all: Windows games can still run through Steam's Proton (it brings its own copies), but plain Wine will fall back to the slower WineD3D.
- "vulkaninfo not installed": install `vulkan-tools`.

**Where Linux gets it.** `vulkaninfo --summary`, `glxinfo -B`, `eglinfo -B`, `clinfo`, and file checks on the paths above.

**Background.** Wine and Proton turn Windows API calls into Linux ones. For graphics, DXVK and VKD3D-Proton translate Direct3D calls into Vulkan calls, which is usually fast enough that games run close to native speed.

---
## DirectX sound devices
`dxd-aud` - Linux data

**In plain words.** The Linux version of the dxdiag "Sound" tab: every playback and capture device, whether it is in use right now, and the sound server's outputs.

**What you see.**
- **Playback** and **Capture** lists, one line per ALSA PCM device (PCM = pulse-code modulation, plain digital audio):
  - `hw:card,device` - the ALSA address.
  - Name - e.g. "HDMI 0", "ALC1220 Analog".
  - State - `closed` (not in use) or `open: <format> <channels>ch <rate>` when a program has it open.
- **Sound server sinks** - `pactl list short sinks`: ID, sink name, server module, sample format, channels, rate, state (`RUNNING`, `IDLE`, `SUSPENDED`).
- **Default sink** - where new sound goes.

**Healthy / typical values.** On the author's PC: four HDMI/DP outputs on card 0 (`hw:0,3`, `hw:0,7`, `hw:0,8`, `hw:0,9`, one per GPU display output), `hw:1,0` ALC1220 Analog, and `hw:1,1` ALC1220 Digital `open: S32_LE 2ch 96000` (the optical output in use at 32-bit, 2 channels, 96 kHz). Capture: ALC1220 Analog and Alt Analog. Default sink `jamesdsp_sink`.

**Unusual values and what they suggest.**
- Every device `closed` while music is playing: the sound is going somewhere else (Bluetooth, USB headset, or a network sink).
- Open at 44100 or 48000 when you expected 96000: PipeWire is resampling; set the allowed rates in PipeWire's config if you care.
- Sink `SUSPENDED`: idle for a few seconds. Normal; it wakes when sound starts.

**Where Linux gets it.** `/proc/asound/pcm`, `/proc/asound/cardN/pcmXp/sub0/hw_params` (`p` playback, `c` capture), `pactl list short sinks`, `pactl get-default-sink`.

---
## DirectX system devices
`dxd-dev` - Linux data

**In plain words.** Every PCI device in the computer, one per line. This is the Linux version of dxdiag's device list.

**What you see.** The plain output of `lspci -nn` (up to 400 lines). Each line is: bus address (`03:00.0` = bus 3, device 0, function 0), device class with its code (`VGA compatible controller [0300]`), vendor and device name, the vendor:device ID pair in brackets (`[1002:7550]`), and the chip revision (`rev c0`).

**Healthy / typical values.** On the author's PC the GPU shows as `03:00.0 VGA compatible controller [0300]: ... Navi 48 [Radeon RX 9070/9070 XT/9070 GRE] [1002:7550] (rev c0)`, with its audio function at `03:00.1`. Lines `01:00.0` and `02:00.0` ("Navi 10 XL Upstream/Downstream Port of PCI Express Switch") are a small PCIe switch built into the graphics card itself. Also listed: NVMe drives, the Realtek 5GbE and Wi-Fi 7 chips, USB controllers, the chipset's PCIe switch ports and an ASMedia USB4 controller.

**Unusual values and what they suggest.**
- A device you installed is missing: it is not seated, not powered, disabled in the BIOS, or its slot shares lanes with another device that is in use.
- A device listed as `Device [xxxx:yyyy]` with no name: the PCI ID database (`hwdata`) is older than the device. Update the package.

**Where Linux gets it.** `lspci -nn` (pciutils), which reads `/sys/bus/pci/devices` and names devices from `/usr/share/hwdata/pci.ids`.

---
## DirectX logical disks
`dxd-dsk` - Linux data

**In plain words.** Every disk and partition, its size, filesystem, label and where it is mounted. Like dxdiag's disk list.

**What you see.** Output of `lsblk -e7 -o NAME,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINT` (loop devices are left out):

| Column | Meaning |
|---|---|
| NAME | Kernel name, shown as a tree (disk, then its partitions). `sdX` = SATA/USB, `nvmeXnY` = NVMe, `zram0` = compressed RAM swap, `sr0` = optical drive. |
| SIZE | Size in binary units (G = GiB, T = TiB). |
| TYPE | `disk`, `part` (partition), `rom` (optical). |
| FSTYPE | Filesystem: `btrfs`, `ext4`, `ntfs`, `vfat`, `exfat`, `swap`, `iso9660`... |
| LABEL | Filesystem label. |
| MOUNTPOINT | Where it is mounted, or `[SWAP]`. Empty = not mounted. |

**Healthy / typical values.** On the author's PC: a 931.5G NVMe with an EFI partition on `/boot/efi` and the Btrfs root, a 1.9T NVMe with Windows, EFI, a Btrfs Steam library and a swap partition, a 3.6T NTFS hard drive, several USB sticks, and `zram0` 15.2G used as swap.

**Unusual values and what they suggest.**
- A disk showing `0B`: an empty card reader slot or a drive that dropped off the bus. Check `dmesg`.
- A partition with no FSTYPE: unformatted, encrypted (shows `crypto_LUKS` once unlocked tools are installed), or damaged.

**Where Linux gets it.** `lsblk` (util-linux), which reads `/sys/block` and udev data.

---
## DirectX files
`dxd-fil` - Linux data

**In plain words.** The graphics runtime files and packages on the system: which Vulkan and OpenCL drivers are registered, which versions of Mesa and friends are installed, and which DirectX translation layers exist.

**What you see.**
- **Vulkan ICDs** (`/usr/share/vulkan/icd.d`) - one line per JSON file: file name, the driver library it points to, and its Vulkan API version. `i686` files are the 32-bit copies used by older games.
- **OpenCL ICDs** (`/etc/OpenCL/vendors`) - one line per `.icd` file and the library it names.
- **Packages (pacman -Q)** - versions of: mesa, lib32-mesa, vulkan-radeon, vulkan-icd-loader, libdrm, libglvnd, ocl-icd, opencl-mesa, rocm-opencl-runtime, dxvk, vkd3d, vkd3d-proton, linux-firmware. Only packages that are installed (or that another installed package provides) appear. Shown only on systems with `pacman` (Arch and its relatives).
- **Translation layers** - the same `[=]` / `[_]` checks as [DirectX information](#directx-information).

**Healthy / typical values.** On the author's PC: four Vulkan ICDs (`lvp_icd` = llvmpipe software Vulkan and `radeon_icd` = RADV, each in 32-bit and 64-bit), all API 1.4.363. OpenCL ICDs: `amdocl64.icd` (ROCm), `rusticl.icd` (Mesa) and `xilinx.icd`. Packages include a self-built Mesa 27.0 development snapshot (`mesa-tkg-git`), `rocm-opencl-runtime 10.0.0` and `dxvk-mingw-git 3.1.1`.

**Unusual values and what they suggest.**
- No `radeon_icd` on an AMD system: Vulkan games will fall back to llvmpipe or fail. Install `vulkan-radeon`.
- Missing `i686` ICD: 32-bit games (many older Steam titles) will not find a Vulkan driver. Install `lib32-vulkan-radeon`.
- An ICD pointing at a library that no longer exists: left over from an uninstalled driver. It can slow down or confuse Vulkan start-up.

**Where Linux gets it.** JSON files in `/usr/share/vulkan/icd.d`, text files in `/etc/OpenCL/vendors`, `pacman -Q`.

**Background.** An ICD (installable client driver) is a small file that tells the Vulkan or OpenCL loader where a driver library is. The loader reads all of them at start-up and offers every driver it finds to the program.

---
## DirectShow filters
`dxd-ftr` - Linux data

**In plain words.** DirectShow is Windows' old media framework. The nearest Linux equivalent is GStreamer, so this item lists the GStreamer plugin files that are installed.

**What you see.** For `/usr/lib/gstreamer-1.0` and (if present) `/usr/lib32/gstreamer-1.0`: one line per plugin file, with the `libgst` prefix and `.so` suffix removed (`audioconvert`, `alsa`, `aom`...), then a count. Files that do not start with `libgst` keep their full name. A note at the end says SRS only lists files: it does not run `gst-inspect-1.0`, which is slow and rebuilds its cache.

**Healthy / typical values.** On the author's PC: 553 plugins in the 64-bit folder (including many LSP audio-effect plugins) and 106 in the 32-bit folder. A desktop with the usual "good", "bad", "ugly" and "libav" plugin sets has a few hundred.

**Unusual values and what they suggest.**
- Very few plugins: some media players (GNOME Videos, many GTK apps) will not play common formats. Install `gst-plugins-good`, `gst-plugins-bad`, `gst-plugins-ugly` and `gst-libav`.
- A plugin file exists but a player still fails: the plugin may be blacklisted after a crash. Clearing `~/.cache/gstreamer-1.0` lets GStreamer test it again.

**Where Linux gets it.** Directory listing of `/usr/lib/gstreamer-1.0` and `/usr/lib32/gstreamer-1.0`.

---
## DirectInput gameports
`dxd-gam` - Not present on most PCs

**In plain words.** The gameport was the 15-pin joystick socket on old sound cards. Modern PCs do not have one.

**What it would show.** Each gameport device the kernel found. Otherwise one of:
- "not present (/sys/bus/gameport absent - no gameport driver loaded)" - normal on any PC from the last 20 years.
- "none" - the gameport driver is loaded but found no port.

**Where Linux gets it.** `/sys/bus/gameport/devices`.

---
## DirectX GPU displays
`dxd-gpu` - Linux data

**In plain words.** The Linux version of the dxdiag "Display" tab: graphics card name, maker, chip, memory, connected monitors and drivers.

**What you see.**

| Field | Meaning |
|---|---|
| Card name | Marketing name (from the AMD ID table, or `lspci`). |
| Manufacturer | GPU vendor and its PCI vendor ID. |
| Chip type | Chip name and PCI device ID. |
| DAC type | Always "Integrated digital" (modern cards have no analog output converter). |
| Device type | PCI class and slot address. |
| Device key | Windows-style hardware ID: `PCI\VEN_<vendor>&DEV_<device>&SUBSYS_<subsystem device><subsystem vendor>&REV_<revision>`. |
| Dedicated memory | VRAM size. |
| Shared memory | GTT size (system RAM the GPU may use). |
| Monitor | One line per connected output: connector name, monitor maker and model (or "(no EDID)"). |
| Driver | Kernel driver. |
| Shader target | The GPU's compiler target name (e.g. `gfx1201`). |

Then **User-space drivers**: the same Vulkan and OpenGL rows as [Direct3D info](#direct3d-info).

**Healthy / typical values.** On the author's PC: AMD Radeon RX 9070 XT, Navi 48 `[7550]`, device key `PCI\VEN_1002&DEV_7550&SUBSYS_88111EAE&REV_C0` (1EAE = XFX), dedicated memory 15.92GB, shared memory 15.23GB, monitors "DP-3: LG Electronics LG ULTRAGEAR" and "HDMI-A-1: BenQ Corporation BenQ RL2755", driver `amdgpu`, shader target `gfx1201`.

**Unusual values and what they suggest.**
- Dedicated memory much smaller than the box says (for example 256 MB): the driver is not fully loaded, or you are looking at an integrated GPU.
- Monitor "(no EDID)": a bad cable, adapter or KVM switch that does not pass the monitor's ID data.

**Where Linux gets it.** `/sys/class/drm/card*/device` (IDs, `mem_info_*`), `/usr/share/libdrm/amdgpu.ids`, `lspci -mm -nn`, connector EDID, `ip_discovery`, `vulkaninfo --summary`, `glxinfo -B`.

---
## DirectX system information
`dxd-inf` - Linux data

**In plain words.** The Linux version of the dxdiag "System" tab: date and time, computer name, operating system, board, BIOS, processor and memory.

**What you see.** Current date/time; Computer name (hostname); Operating system; Kernel (name, release, architecture); System manufacturer and System model (from DMI, the firmware's hardware table); BIOS (vendor, board, BIOS version and date); Processor; Memory (total RAM the kernel can use); Page file (swap) - total swap; DirectX version - "n/a on Linux; Vulkan <version>".

**Healthy / typical values.** On the author's PC: computer name X3D, CachyOS with kernel 7.3.0, ASUS TUF GAMING X870-PRO WIFI7 W NEO, BIOS 1805 dated 09/18/2026, AMD Ryzen 7 7800X3D, memory 30.45GB, swap 48.04GB, Vulkan 1.4.363.

**Unusual values and what they suggest.**
- Memory a lot lower than the RAM you installed (for example 15 GB with 32 GB fitted): a stick is not detected, or memory is reserved for an integrated GPU. A few hundred MB less than installed is normal (firmware and kernel reservations); 30.45 GB of 32 GB is normal.
- System manufacturer "To Be Filled By O.E.M." or empty: the board maker did not fill in the DMI fields. Harmless.

**Where Linux gets it.** `uname`, `/sys/class/dmi/id`, `/proc/cpuinfo`, `/proc/meminfo` (MemTotal, SwapTotal), `vulkaninfo --summary`.

---
## DirectInput devices
`dxd-inp` - Linux data

**In plain words.** Every input device the kernel knows: keyboards, mice, game controllers, power buttons, and even the "jack sensing" switches of the sound card.

**What you see.**

| Column | Meaning |
|---|---|
| Name | Device name as reported by the device or driver. One physical device often appears several times (a keyboard with media keys = keyboard + consumer control + mouse part). |
| Bus / Vendor / Product | `Bus=0003` USB, `0005` Bluetooth, `0019` built-in platform devices, `0000` virtual; then the USB vendor and product IDs and a version. |
| Handlers | Device files the kernel made: `eventN` (generic input event device), `kbd` (keyboard handler), `mouseN`, `jsN` (old joystick interface), `leds`, `sysrq`. |

The count of input devices is at the bottom.

**Healthy / typical values.** On the author's PC: 23 devices, including two Power Buttons, a SteelSeries Apex 7 keyboard (four entries), a Razer DeathAdder Elite mouse (three entries), a Logitech mouse, a Nintendo Switch Pro Controller (`event21 js0`) plus its motion sensor "Pro Controller (IMU)", and the HDMI and headphone jack-sense entries of the two sound cards.

**Unusual values and what they suggest.**
- A controller with no `jsN` handler: old games that use `/dev/input/js*` will not see it, but Steam and SDL games use the event device and will.
- The same device listed twice with different vendor IDs: often a virtual device from a remapping tool (here "OL virtual mouse").

**Where Linux gets it.** `/proc/bus/input/devices`.

---
## DirectMusic ports
`dxd-mic` - Linux data

**In plain words.** DirectMusic was Windows' MIDI system. On Linux, MIDI programs connect through the ALSA sequencer. This item lists every sequencer client, its ports and who is connected to whom.

**What you see.** The ALSA sequencer client list, with memory-pool lines removed:
- `Client N : "Name" [type]` - a program or kernel driver. `Kernel` = built into the kernel, `User` = a program. `Legacy` = MIDI 1.0 (newer clients may say UMP, MIDI 2.0).
- `Port N : "Name" (flags) [direction]` - one MIDI port. Flags: `R`/`r` others can read events from it (it sends MIDI), `W`/`w` others can write to it (it receives MIDI, like a synth), `e` exported (visible to others), `X` duplex. A capital letter means other programs may also subscribe.
- `Connecting To:` / `Connected From:` - live connections, as `client:port`.

If `/proc/asound/seq/clients` is missing, SRS shows `aconnect -l` instead.

**Healthy / typical values.** On the author's PC: System (timer and announce ports), Midi Through, OSS sequencer, and software synths "MT-32", "Nuked SC55" and "FLUID Synth (Tyroland GS)", plus PipeWire's MIDI bridge clients. "Midi Through Port-0" is connected to the Nuked SC55 client (`Connecting To: 129:0`), so anything sent to Midi Through plays on that synth.

**Unusual values and what they suggest.**
- Only System and Midi Through: no MIDI hardware and no software synth running. Old games and MIDI players will make no music.
- A USB MIDI keyboard missing: check that `snd_usb_audio` loaded.

**Where Linux gets it.** `/proc/asound/seq/clients`, or `aconnect -l`.

---
## DirectPlay adapters
`dxd-ply` - Windows-only (Linux equivalent shown)

**In plain words.** DirectPlay was an old Windows networking layer for multiplayer games. It does not exist on Linux, so SRS lists the network interfaces instead.

**What it shows.** "DirectPlay: Windows-only (deprecated DirectX networking)", "Nearest: network interfaces", then one line per interface: name, state (`up`, `down`, `unknown`) and hardware (MAC) address, for example `wlan0 up 00:11:22:33:44:55`.

**Typical values.** Real interfaces (`em1` wired Ethernet, `wlan0` Wi-Fi), `lo` (loopback, state `unknown` is normal) and virtual ones from Docker (`docker0`, `veth...`), virtual machines (`virbr0`, `vmnet1`, `vmnet8`) and VPNs. State `unknown` on virtual adapters is normal.

**Where Linux gets it.** `/sys/class/net/<name>/operstate` and `/address`. For more detail see the network sections.

---
## DirectInput PS/2 devices
`dxd-ps2` - Not present on most PCs

**In plain words.** PS/2 is the old round keyboard and mouse socket. Many new boards no longer have one.

**What it would show.** Each serial-input (serio) port with its description and driver, for example `serio0 i8042 KBD port driver atkbd`. When there are none it shows "no PS/2 ports or devices (/sys/bus/serio/devices empty)". That is normal on boards without a PS/2 socket, such as the author's.

**Where Linux gets it.** `/sys/bus/serio/devices/*/description` and `driver`.

---
## DirectX summary
`dxd-sum` - Linux data

**In plain words.** A one-screen summary in the style of dxdiag: OS, CPU, RAM, graphics, monitors, 3D drivers, sound cards and input devices.

**What you see.** Operating system; Processor; Memory; one **Display adapter** line per GPU (name, VRAM, kernel driver); **Monitors** (how many are connected); the Vulkan and OpenGL rows from [Direct3D info](#direct3d-info); **Sound cards** (ALSA card count); **Input devices** (count from `/proc/bus/input/devices`).

**Healthy / typical values.** On the author's PC: CachyOS, Ryzen 7 7800X3D, 30.45GB, "AMD Radeon RX 9070 XT 15.92GB (amdgpu)", 2 monitors connected, Vulkan 1.4.363, OpenGL 4.6, 2 sound cards, 23 input devices.

**Unusual values and what they suggest.** See the individual items. A monitor count lower than the screens you see means a screen is driven by a different GPU or by a USB display adapter.

**Where Linux gets it.** `/sys/class/drm`, `/proc/meminfo`, `/proc/asound/cards`, `/proc/bus/input/devices`, `vulkaninfo --summary`, `glxinfo -B`.

---
## DirectInput USB root
`dxd-usb` - Linux data

**In plain words.** The USB tree: each USB controller (root hub), the hubs and devices plugged into it, and the speed each one runs at. Then a list of the input devices that are on USB.

**What you see.**
- The output of `lsusb -t` (up to 300 lines). Each line: bus and port, device number, interface number (`If 0`), class (`Hub`, `Human Interface Device`, `Mass Storage`, `Video`, `Wireless`...), the driver bound to it (`usbhid`, `uas`, `usb-storage`, `btusb`, `[none]` = no driver), and the link speed.
- **USB HID input devices** - name and physical path (`usb-<controller PCI address>-<port path>/inputN`) of each input device on USB.

Speeds: `12M` Full Speed (USB 1.1), `480M` High Speed (USB 2.0), `5000M` USB 3.2 Gen 1, `10000M` USB 3.2 Gen 2, `20000M/x2` USB 3.2 Gen 2x2 (two lanes).

**Healthy / typical values.** On the author's PC: keyboards and mice at 12M (normal, they need no more), a USB 3 flash drive with the `uas` driver at 10000M, a webcam at 480M with `uvcvideo`, a Bluetooth adapter with `btusb`, and root hubs from the CPU, the chipset (`xhci-pci-prom21`) and the ASMedia USB4 controller.

**Unusual values and what they suggest.**
- A USB 3 drive at `480M`: it is in a USB 2 port, behind a USB 2 hub, or the cable is USB 2 only. Expect about 40 MB/s instead of hundreds.
- A drive on `usb-storage` instead of `uas`: slower and older protocol; the drive or adapter does not support UAS (USB Attached SCSI), or it is blacklisted for bugs.
- `Driver=[none]` on a device you need: no kernel driver matches it; it may need a user-space tool.

**Where Linux gets it.** `lsusb -t` (usbutils) and `/proc/bus/input/devices`.

---
## GOverlay status
`goverlay` - Linux data

**In plain words.** GOverlay is a settings program for MangoHud, the in-game overlay that shows frame rate, temperatures and clocks. This item shows whether they are installed, which versions, and the MangoHud settings you are using.

**What you see.**
- **goverlay**, **mangohud** - installed or not.
- Package versions (on pacman systems) for goverlay, mangohud (and their `-git` builds), lib32-mangohud (32-bit games) and vkbasalt. Only installed ones appear.
- **MANGOHUD env** - the value of the `MANGOHUD` environment variable. `1` means MangoHud is switched on for every Vulkan program you start; "(not set - not injected globally)" means it only runs where you ask for it.
- **goverlay.conf** and **GOverlay version** - the config path and the version recorded in it.
- **MangoHud.conf (active lines)** - every line of `~/.config/MangoHud/MangoHud.conf` that is not empty or a comment (up to 80). A line with `=` shows key and value; a line without `=` is a switch and shows `on`. Values of keys that look secret (token, key, secret, pass, auth, cookie) are shown as `********`.

**Healthy / typical values.** On the author's PC: goverlay-git 1.9.3, mangohud-git 0.8.4, lib32-mangohud-git, vkbasalt, `MANGOHUD env 1`. The config turns on GPU load, VRAM, core clock, edge/junction/memory temperatures, fan, power and power limit, CPU load and temperature, RAM, frame time graph, FSR and present mode, sets `fps_limit 0` (no limit) and `toggle_hud Shift_R+F12`.

**Unusual values and what they suggest.**
- mangohud installed but lib32-mangohud missing: the overlay will not appear in 32-bit games.
- `MANGOHUD=1` set globally and a desktop program misbehaves: add it to the `blacklist` line, as the author's config does for several tools.

**Where Linux gets it.** File checks in `/usr/bin`, `pacman -Q`, the `MANGOHUD` environment variable, `~/.config/goverlay/goverlay.conf`, `~/.config/MangoHud/MangoHud.conf`.

---
## GPU all details
`gpu-all` - Linux data

**In plain words.** Everything SRS knows about one GPU on one page: information, clocks, temperatures, fans, power, firmware, PCI mapping, and the `lspci -vv` view.

**What you see.** These headed blocks, in order. Each is described in full in its own item:
1. **Information** - as [GPU information](#gpu-information).
2. **Clocks** - as [GPU clock speeds](#gpu-clock-speeds), including the OverDrive table.
3. **Temperatures** - as [GPU temperatures](#gpu-temperatures).
4. **Fans** - as [GPU fans](#gpu-fans).
5. **Power** - as [GPU power status](#gpu-power-status), plus the `gpu_metrics` header line.
6. **VBIOS / firmware** - as [GPU VBIOS status](#gpu-vbios-status).
7. **Mapping** - as [GPU mapping](#gpu-mapping).
8. **lspci -vv (unprivileged)** - up to 200 lines of `lspci -vv -nn` for the GPU: subsystem, control/status flags, interrupt routing, IOMMU group, memory regions (BARs), expansion ROM, kernel driver and module. Without root, `lspci` cannot read the capability list, so it prints "Capabilities: <access denied>". For the full list see [GPU register dump](#gpu-register-dump).
9. A footer line with the motherboard and BIOS version.

**Healthy / typical values.** See each item. On the author's PC `lspci` shows "Region 0: Memory at ... (64-bit, prefetchable) [size=16G]", which means Resizable BAR is on (see below).

**Where Linux gets it.** All the sources of the items above, plus `lspci -vv -nn -s <slot>`.

---
## GPU API routines
`gpu-api` - Linux data

**In plain words.** A check-list of the libraries that programs use to talk to the GPU: the kernel interface, Vulkan, OpenGL, video decoding, OpenCL, ROCm, and CUDA-related ones. Each row says whether the library is installed and where.

**What you see.** Columns **Library**, **API** (what it is for) and **Path** (or "not found"), with `[=]` / `[_]` (ticked boxes in the GUI):

| Library | What it is |
|---|---|
| `libdrm_amdgpu.so.1` | libdrm amdgpu - the user-space wrapper around the amdgpu kernel interface. |
| `libdrm.so.2` | libdrm core - talks to the kernel's DRM (Direct Rendering Manager). |
| `libvulkan.so.1` | Vulkan loader. |
| `libGL.so.1` | OpenGL through GLVND (the vendor-neutral GL dispatcher). |
| `libEGL.so.1` | EGL (connects GL to Wayland/X11 windows). |
| `libgbm.so.1` | GBM (Generic Buffer Management - lets compositors allocate GPU buffers). |
| `libOpenCL.so.1` | OpenCL ICD loader. |
| `libva.so.2` | VA-API (Video Acceleration API - hardware video decode/encode). |
| `libvdpau.so.1` | VDPAU (older video decode API). |
| `libamdhip64.so` | ROCm HIP runtime (AMD's CUDA-like compute language). |
| `libhsa-runtime64.so.1` | ROCm HSA runtime (low-level compute runtime under HIP and OpenCL). |
| `librocm_smi64.so` | ROCm SMI (System Management Interface - monitoring library). |
| `libamd_smi.so` | AMD SMI (newer monitoring library). |
| `libcuda.so.1` | CUDA driver API. On an AMD system this is usually ZLUDA, a layer that runs CUDA programs on AMD GPUs. |
| `libnvidia-ml.so.1` | NVIDIA NVML (NVIDIA's monitoring library). |
| `libatiadlxx.so` | AMD ADL - a Windows-only AMD API; on Linux it is expected to be "not found". |

**Healthy / typical values.** On the author's PC every library is found except `libatiadlxx.so`. ROCm libraries live in `/opt/rocm/lib`; the rest in `/usr/lib`.

**Unusual values and what they suggest.**
- `libva.so.2` missing: browsers and video players cannot use the GPU for video decoding and will use more CPU.
- `libvulkan.so.1` missing: no Vulkan games at all. Install `vulkan-icd-loader`.
- `libnvidia-ml.so.1` present on a PC with no NVIDIA GPU: some package pulled it in as a dependency. Usually harmless.

**Where Linux gets it.** `ldconfig -p` (the shared-library cache), with a fallback check in `/opt/rocm/lib` and `/usr/lib`.

---
## GPU applications
`gpu-apps` - Linux data

**In plain words.** Which programs have the GPU open right now, how much video memory each uses, and how much GPU time each has used since it started. Like the GPU columns in Windows Task Manager.

**What you see.** One row per GPU client (a program can open the GPU more than once, so it can appear on several rows):

| Column | Meaning |
|---|---|
| PID | Process ID. |
| Process | Program name. |
| VRAM | Video memory the client holds. |
| GTT | System memory the client has mapped for the GPU. |
| GFX s | Seconds of 3D (graphics) engine time used so far. |
| Compute s | Seconds of compute engine time. |
| DMA s | Seconds of copy engine time. |
| Video s | Seconds of video decode plus encode engine time. |

The bottom line says how many GPU clients were visible and how many processes could not be inspected. Without root you can only look inside your own processes; system processes (such as `kwin_wayland`, the KDE compositor) are skipped. The list refreshes every two seconds in the GUI.

**Healthy / typical values.** On the author's PC at the desktop: `plasmashell` with about 377 MB VRAM and 37 s of GFX time, `Xwayland` about 38 MB, `coolercontrol` about 133 MB, `krunner` about 74 MB. Rows with 12KB VRAM and 2MB GTT are small helper contexts and are normal.

**Unusual values and what they suggest.**
- A program you closed still listed: it is still running in the background.
- A small tool using gigabytes of VRAM: a leak in that program, or it is caching textures. Games and AI models normally use many GB.
- Compute seconds rising with no compute work running: something (a miner, an AI service) is using the GPU.

**Where Linux gets it.** For every process, the open files in `/proc/<pid>/fd` that point to `/dev/dri/*`, and their `/proc/<pid>/fdinfo/<fd>` statistics (`drm-client-id`, `drm-memory-vram`, `drm-memory-gtt`, `drm-engine-gfx`, `-compute`, `-dma`, `-dec`, `-enc`).

---
## GPU VBIOS status
`gpu-bios` - Linux data

**In plain words.** The version of the graphics card's own BIOS (VBIOS) and the versions of all the small firmware programs that run inside the GPU.

**What you see.**
- **VBIOS version** - the card maker's VBIOS part number.
- **VBIOS build** - build number or date.
- **Option ROM** - whether the PCI option ROM is exposed ("present (not read)"). SRS never reads it.
- **Firmware versions** - one line per firmware the driver loaded, in hex. Common names on AMD:

| Name | What it runs |
|---|---|
| `asd` | AMD Secure Display / security driver (part of the PSP). |
| `sos` | PSP secure OS (the security processor's operating system). |
| `ta_ras` | Trusted app for RAS (reliability: memory error handling). |
| `smc` | SMU firmware - the power, clock and fan manager. |
| `dmcub` | Display microcontroller (DCN). |
| `imu` | Power-up microcontroller for the graphics block. |
| `me`, `pfp`, `mec` | Command processor parts: micro engine, pre-fetch parser, compute micro engine. |
| `mes`, `mes_kiq` | Micro Engine Scheduler - schedules work queues on the GPU. |
| `rlc` | RunList Controller - power gating and context switching for graphics. |
| `sdma`, `sdma2` | Copy engine firmware. |
| `vcn` | Video codec firmware. |

**Healthy / typical values.** On the author's PC: VBIOS `113-EXT108832-100`, build `00131175`; smc `0x00685000`, mes `0x00000097`, vcn `0x0910d021`. The exact values depend on the card and on the `linux-firmware` package.

**Unusual values and what they suggest.**
- A firmware showing `0x00000000`: that firmware did not load. Check `dmesg | grep amdgpu` for firmware errors and update `linux-firmware`.
- Firmware versions that changed after an update and new GPU problems started: a firmware regression is possible; compare with an older `linux-firmware`.
- VBIOS version different from the maker's latest: you may want a VBIOS update, but only from the card maker, and only if it fixes something you have.

**Where Linux gets it.** `/sys/class/drm/card0/device/vbios_version`, `vbios_build`, the presence of `rom`, and `/sys/class/drm/card0/device/fw_version/*_fw_version`.

**Background.** The VBIOS lives on a chip on the card. It brings the card up at power-on (so you see the boot screen) and holds the board's power and clock tables. The other firmware files are loaded by the Linux driver from `/usr/lib/firmware/amdgpu` every boot.

---
## GPU clock speeds
`gpu-clocks` - Linux data

**In plain words.** How fast each part of the GPU is running right now, and the list of speed steps it can choose from. Updates every second in the GUI.

**What you see.** Three columns: **Clock**, **Current**, **DPM levels ([active])**.

| Row | Meaning |
|---|---|
| GFX (sclk) sensor | Live shader (core) clock from the hardware monitor. |
| Memory (mclk) sensor | Live memory clock. |
| GFX (sclk) | Core clock DPM table. |
| Memory (mclk) | Memory clock DPM table. |
| Fabric (fclk) | Internal data fabric clock. |
| SoC (socclk) | Clock for the chip's support logic. |
| DCE/DCN (dcefclk) | Display engine clock. |
| PCIe | PCIe link steps: speed, width and the clock used for that step. |
| GPU load | Percent of time the graphics engine was busy. |
| Memory load | Percent of time the memory controller was busy. |
| Performance level | The driver's DPM mode: `auto`, `low`, `high`, `manual`, `profile_*`. |

In the DPM level column the active step is in `[brackets]`. A row appears only if the driver offers that file.

In text reports (not the GUI) an **OverDrive table** follows: the raw contents of `pp_od_clk_voltage`, read only. On RDNA 4 it shows `OD_SCLK_OFFSET` (how much the top core clock is raised or lowered), `OD_MCLK` (memory clock range), `OD_VDDGFX_OFFSET` (core voltage offset) and `OD_RANGE` (the limits the driver allows).

**Healthy / typical values.** On the author's PC at the desktop: core about 910-1014 MHz with levels 500 / [910] / 2400 MHz; memory [875] of 96-1258 MHz; fclk [2105] of 313-2400 MHz; socclk [1280]; dcefclk [243]; PCIe [32.0GT/s, x16]. GPU load 4-8 %. Performance level `manual` because the tuning tool LACT has applied an overclock profile. The OverDrive table shows a +300 MHz core offset, memory up to 1375 MHz and a -110 mV voltage offset (an undervolt). On a stock card the level is `auto` and the offsets are 0.

**Unusual values and what they suggest.**
- Core clock pinned at the top step at idle: something is using the GPU (check [GPU applications](#gpu-applications)), or Performance level is `high`. Wastes power and makes heat.
- Memory clock pinned high at idle with two or more monitors: common with mixed refresh rates. The memory cannot change speed without flicker unless both screens give it time to, so the driver keeps it high. Harmless, but idle power is noticeably higher.
- Core clock low under full game load: the card is power-limited or thermal-limited (see [GPU power status](#gpu-power-status) and [GPU temperatures](#gpu-temperatures)).
- PCIe stuck at the lowest step under load: the link failed to train at full speed (see [GPU information](#gpu-information)).

**Where Linux gets it.** `/sys/class/drm/card0/device/pp_dpm_sclk`, `pp_dpm_mclk`, `pp_dpm_fclk`, `pp_dpm_socclk`, `pp_dpm_dcefclk`, `pp_dpm_pcie`, `gpu_busy_percent`, `mem_busy_percent`, `power_dpm_force_performance_level`, `pp_od_clk_voltage`, and hwmon `freq1_input` / `freq2_input`.

**Background.** DPM (dynamic power management) means the GPU keeps a short list of clock steps and jumps between them many times a second. The firmware (SMU) picks the step from load, temperature and power. The memory clock number is the base clock: GDDR6 moves 16 bits per clock per pin, so 1258 MHz is about 20 Gbps per pin, the RX 9070 XT's rated memory speed.

---
## CUDA information
`gpu-cuda` - Not present on most PCs

**In plain words.** CUDA is NVIDIA's GPU computing system. It only runs on NVIDIA cards. This item checks for NVIDIA hardware and then shows AMD's nearest equivalent, ROCm with HIP.

**What it shows.**
- **NVIDIA GPUs** - how many NVIDIA cards were found (vendor ID 10DE).
- **/dev/nvidia0** - present or absent (the NVIDIA driver's device file).
- **nvidia-smi** - installed or not.
- **libcuda.so.1** - whether a CUDA driver library exists in `/usr/lib`. On AMD systems this is often ZLUDA (which lets CUDA programs run on AMD), so SRS says "(ZLUDA or NVIDIA)".
- **Nearest on AMD: ROCm / HIP** - ROCm install folder, HIP runtime library, `/dev/kfd` (the kernel's compute device; "present (compute enabled)" means compute programs can use the GPU), and **HIP target** (the GPU's compiler name, e.g. `gfx1201`).

**What "not present" means.** On the author's PC: 0 NVIDIA GPUs, no `/dev/nvidia0`, no `nvidia-smi`; `libcuda.so.1` exists (ZLUDA); ROCm in `/opt/rocm`; `/dev/kfd` present; HIP target `gfx1201`. If `/dev/kfd` is absent on an AMD system, ROCm programs cannot run: check that your user is in the `render` group and that the driver loaded.

**Where Linux gets it.** PCI vendor IDs in `/sys/class/drm`, file checks for `/dev/nvidia0`, `/dev/kfd`, `/opt/rocm`, `libcuda.so.1`.

---
## GPU D3D status
`gpu-d3d` - Linux data

**In plain words.** A quick "how busy is the GPU" view per card: 3D load, memory load and memory use, followed by the Vulkan devices.

**What you see.** For each GPU: card and name; **3D/GFX busy** (percent); **Memory busy** (percent); **Dedicated memory** (VRAM used of total); **Shared memory (GTT)** (GTT used of total). Then the Vulkan rows from [Direct3D info](#direct3d-info).

**Healthy / typical values.** On the author's PC at the desktop: 3D/GFX busy 8 %, memory busy 0 %, dedicated memory 1.03GB of 15.92GB, shared memory 130.06MB of 15.23GB.

**Unusual values and what they suggest.**
- Dedicated memory almost full and GTT use climbing in a game: the game wants more VRAM than you have. Textures spill into system RAM over PCIe and you get stutter. Lower texture quality.
- High GTT use at the desktop: some program is keeping large buffers in system memory. Usually harmless.

**Where Linux gets it.** `gpu_busy_percent`, `mem_busy_percent`, `mem_info_vram_total/used`, `mem_info_gtt_total/used` in `/sys/class/drm/card*/device`, and `vulkaninfo --summary`.

---
## GPU register dump
`gpu-dump` - Linux data - Needs root data

**In plain words.** A raw hex dump of the GPU's PCI configuration space: the small block of settings every PCI device has (IDs, memory windows, power state, PCIe link settings). This is for experts and bug reports.

**What you see.**
- A header line: slot address and how many bytes a normal user may read. Without root, Linux only lets you read the first 64 bytes.
- The dump: 16 bytes per line, offset first (`000:`, `010:`...).
- **Full config space (srs-root cache: lspci -vvvnnxxx)** - when you have run srs-root, the full decoded view of the GPU from the root cache: all capabilities (Power Management, PCI Express link capability/control/status, MSI, Advanced Error Reporting, Resizable BAR and more), up to 400 lines.
- Without the root cache: "Needs root: Menu > Run as root (srs-root)" and a note that the extended config space (bytes 256-4095) and amdgpu debugfs registers need root. GPU memory-mapped (MMIO) registers are never read.

**Healthy / typical values.** On the author's PC: 64 bytes readable; the first bytes `02 10 50 75` are the vendor ID 1002 and device ID 7550 (stored low byte first). In the root view: `LnkCap: Speed 32GT/s, Width x16` and `LnkSta: Speed 32GT/s, Width x16` (the link runs at its full capability), `ASPM Disabled`, MSI enabled.

**Unusual values and what they suggest.**
- `LnkSta` speed or width below `LnkCap`: see the PCIe notes in [GPU information](#gpu-information).
- `DevSta: CorrErr+` repeatedly: the link sees correctable errors. A few are normal; a flood in `dmesg` (AER messages) points to a riser cable, a dirty slot or a too-high PCIe generation setting in the BIOS.

**Where Linux gets it.** `/sys/class/drm/card0/device/config`, and the srs-root cache file `pci-all.txt` (made with `lspci -vvvnnxxx` as root).

**Background.** PCI configuration space is 256 bytes (PCI) or 4096 bytes (PCI Express) per device. The first 64 bytes are a standard header. Capabilities follow as a linked list; reading them needs root because some devices react badly to being read.

---
## GPU export
`gpu-export` - Linux data

**In plain words.** A list of every live GPU sensor, its current value, and the exact file it comes from. Useful when you want to set up your own monitoring tool or script.

**What you see.** Columns **Sensor** (hwmon name and label), **Value** (converted to °C, RPM, W, mV or MHz) and **Source** (the full sysfs path). Every `*_input` and `*_average` file in the GPU's hwmon folder is listed, then **GPU load** (`gpu_busy_percent`) and **VRAM used** (`mem_info_vram_used`). A note says these sources can be saved with `srs -save` or read by any monitor (MangoHud, CoolerControl...).

**Healthy / typical values.** On the author's PC (hwmon9): fan1 551 RPM, freq1 sclk 910 MHz, freq2 mclk 875 MHz, in0 vddgfx 641 mV, power1 PPT 35.0 W, temp1 edge 25.0 °C, temp2 junction 28.0 °C, temp3 mem 50.0 °C, GPU load 8 %, VRAM used 1.03GB.

**Unusual values and what they suggest.** The `hwmonN` number can change between boots. Scripts should find the folder through `/sys/class/drm/card0/device/hwmon/` (as shown here) instead of hard-coding `/sys/class/hwmon/hwmon9`.

**Where Linux gets it.** `/sys/class/drm/card0/device/hwmon/hwmon*/` and `/sys/class/drm/card0/device/`.

**Background.** hwmon is the kernel's standard sensor interface. Each driver makes a folder with files such as `temp1_input` (millidegrees C), `fan1_input` (RPM), `power1_average` (microwatts) and `in0_input` (millivolts). Every monitoring tool on Linux reads these same files.

---
## GPU fans
`gpu-fans` - Linux data

**In plain words.** How fast the graphics card's fans are spinning, how fast the card wants them to spin, and the control mode. Read only; updates every second in the GUI.

**What you see.**

| Field | Meaning |
|---|---|
| Fan 1 speed | Measured fan speed in RPM. |
| Fan 1 target | Speed the firmware is aiming for. |
| Fan 1 range | Lowest and highest supported speed. |
| PWM duty | Fan drive signal as raw value / max, and percent (PWM = pulse-width modulation). |
| PWM mode | `0 full speed`, `1 manual` (a program sets the speed), `2 automatic (firmware)`. |
| Fan enabled | Yes / No. |

| Fan-control software | `running: lactd` (or `coolercontrold`, `corectrl`, `fancontrol`, ...) when one of the known fan-control programs is running, else `none found running (driver / firmware fan curve)`. |

A note says SRS only reads and never writes fan or PWM settings. Rows appear only if the driver offers the matching file.

**Healthy / typical values.** On the author's PC at the desktop: 551 RPM, target 551 RPM, range 0 - 3200 RPM, PWM 38 / 255 (15 %). Many cards stop their fans completely (0 RPM, "zero-RPM mode") below about 50-60 °C; that is normal.

**Unusual values and what they suggest.**
- 0 RPM while the junction temperature is above about 80 °C: fan failure, unplugged fan cable, or a fan curve set wrong in a tuning tool.
- Speed far below target under load: a worn or blocked fan.
- PWM mode `1 manual` when you never set it: a tuning program (LACT, CoreCtrl, CoolerControl) has taken over the fan.

**Where Linux gets it.** GPU hwmon files `fan1_input`, `fan1_target`, `fan1_min`, `fan1_max`, `pwm1`, `pwm1_min`, `pwm1_max`, `pwm1_enable`, `fan1_enable`.

---
## GPU generations
`gpu-gens` - Linux data

**In plain words.** Which GPU generation (architecture) the card is, and the version of every hardware block inside the chip.

**What you see.**
- **Generation** - family name worked out from the graphics core version: RDNA 4 (Navi 4x), RDNA 3.5 (APU), RDNA 3, RDNA 2, RDNA 1, CDNA (Instinct), GCN 5 (Vega), GCN 3/4 (Polaris/Fiji/Tonga), GCN 2, GCN 1. If there is no IP discovery data: "no ip_discovery data (pre-Navi or non-AMD GPU)".
- **GFX IP** - graphics core version and the matching LLVM compiler target (e.g. `gfx1201`).
- A table of all IP blocks: **IP block** name, **Version**, **Instances** (copies) and **HW ID** (AMD's internal block number).

Some block names: `GC` graphics core, `SDMA0/1` copy engines, `LSDMA` light SDMA, `UVD` video, `DMU` display, `MP0` security processor (PSP), `MP1` power manager (SMU), `UMC` memory controllers, `DF` data fabric, `MMHUB` memory hub, `NBIF` PCIe bus interface, `PCIE` PCIe block, `THM` thermal, `SMUIO` SMU input/output, `HDP` host data path, `OSSSYS` interrupt handling, `CLKA` clock blocks, `FUSE` chip fuses, `ATHUB`, `SYSTEMHUB`, `DFX`, `DFX_DAP`, `DBGU_NBIO` (test and debug blocks).

**Healthy / typical values.** On the RX 9070 XT: Generation RDNA 4 (Navi 4x), GFX IP 12.0.1 (gfx1201). 27 blocks, including UMC 8.14.0 with 8 instances (8 memory controller channels for the 256-bit GDDR6 bus), CLKA 9 instances, GC 12.0.1, MP1 14.0.3. A few blocks (`DAZ`, `DIO`) report 127.127.63, which is a placeholder value rather than a real version.

**Unusual values and what they suggest.** The generation is only a lookup of the GC version. A brand-new chip may show "unknown" until SRS learns its version. That does not mean anything is wrong with the card.

**Where Linux gets it.** `/sys/class/drm/card0/device/ip_discovery/die/0/<block>/<instance>/{major,minor,revision,hw_id}`.

**Background.** Since RDNA 1 (2019), AMD GPUs carry a table in their VBIOS that lists every hardware block and its version. The driver reads this table instead of keeping a fixed list per chip. The `gfx` name (`gfx1201` = GC 12.0.1) is what compilers such as ROCm and Mesa use to pick the right instruction set.

---
## GPU G-Sync / VRR
`gpu-g-sync` - Linux data

**In plain words.** Whether each monitor supports variable refresh rate (VRR: FreeSync, G-Sync Compatible, or HDMI VRR), whether it is switched on right now, and the refresh range the monitor reports.

**What you see.** One row per connected output:

| Column | Meaning |
|---|---|
| Connector | Output name (`DP-3`, `HDMI-A-1`). |
| Monitor | Monitor name from EDID. |
| VRR capable | `Yes` if the kernel says this monitor and link can do adaptive sync. |
| VRR enabled | `Yes` if the compositor has turned VRR on for this output now. "No (compositor off/idle)" means it is off at the moment. |
| EDID range | Lowest and highest vertical refresh rate in the monitor's EDID. |

If `drm_info` is not installed, SRS says so and shows only the EDID ranges (`?` in the VRR columns).

**Healthy / typical values.** On the author's PC: DP-3 LG ULTRAGEAR, capable Yes, enabled No, range 48 - 165 Hz; HDMI-A-1 BenQ RL2755, capable No, range 50 - 76 Hz. KDE switches VRR on only while a full-screen game runs if Adaptive Sync is set to "Automatic", so "No" at the desktop is normal.

**Unusual values and what they suggest.**
- Capable `No` on a FreeSync monitor: FreeSync is turned off in the monitor's on-screen menu, the cable or adapter does not support it, or (on HDMI) the driver does not support HDMI VRR for that monitor. DisplayPort is the most reliable.
- Enabled `No` while a full-screen game runs: set Adaptive Sync to "Automatic" or "Always" in the display settings.
- A narrow range such as 48-75 Hz: VRR only helps inside that window. Below it, LFC (low framerate compensation) needs the maximum to be at least about twice the minimum.

**Where Linux gets it.** `drm_info` (connector property `vrr_capable`, CRTC property `VRR_ENABLED`) and the EDID range-limits block in `/sys/class/drm/card0-*/edid`.

**Background.** Normally a monitor refreshes at a fixed rate and the GPU must wait for it. With VRR the monitor waits for the GPU instead, so frames are shown as soon as they are ready. That removes tearing without the delay of V-Sync. The kernel says whether a screen *can* do it; the compositor (KWin, Mutter, gamescope) decides when to turn it on, per CRTC. A CRTC is the display engine part that scans out one picture to one or more outputs.

---
## GPU I2C buses
`gpu-i2c` - Linux data

**In plain words.** I2C is a slow two-wire bus that chips use to talk to each other. A GPU has several: one per monitor output (DDC, used to read the monitor's EDID and to change its brightness), plus buses for its power manager. This item lists all I2C buses in the system and marks which ones belong to the GPU. It lists names only and never talks to any bus.

**What you see.**

| Column | Meaning |
|---|---|
| Bus | `i2c-N` name (highlighted when it belongs to the GPU). |
| Name | Adapter name from the driver. |
| Owner | For GPU buses: the card, and for DDC buses the output they serve and whether it is connected, e.g. `card0 DDC DP-3 (connected)`. For other buses: the device path they hang off. |

**Healthy / typical values.** On the author's PC: `AMDGPU DM i2c hw bus 0-3` are the DDC lines for DP-1, DP-2, DP-3 and HDMI-A-1; `AMDGPU DM aux hw bus 0-2` are DisplayPort AUX channels; `AMDGPU SMU 0` and `1` belong to the power manager. Non-GPU buses: the chipset's SMBus (`SMBus PIIX4`, used for RAM SPD and sensors) and a DesignWare I2C controller with two Realtek devices on it.

**Unusual values and what they suggest.**
- No `AMDGPU DM i2c` buses: the display part of the driver is not running (for example a headless compute setup).
- For safety, avoid scanning the GPU's SMU buses with I2C tools. On some cards, DDC/CI tools have hung the GPU's power manager. SRS avoids this by only reading bus names.

**Where Linux gets it.** `/sys/bus/i2c/devices/i2c-*/name`, the bus's sysfs path, and `/sys/class/drm/card0-*/ddc` links.

---
## GPU information
`gpu-info` - Linux data

**In plain words.** The GPU's identity card: name, chip, maker of the board, architecture, video memory, VBIOS and PCIe link.

**What you see.**

| Field | Meaning | Why it matters |
|---|---|---|
| GPU | Marketing name. | What you bought. |
| Chip | Chip name and PCI device ID from `lspci`. | Several cards share one chip (Navi 48 is the 9070, 9070 XT and 9070 GRE). |
| Device IDs | Windows-style hardware ID (vendor, device, subsystem, revision). | Matches driver databases and the Windows hardware ID. |
| Vendor | GPU chip maker. | |
| Board vendor | Company that built the card, with subsystem vendor:device IDs. | Tells XFX from Sapphire, PowerColor and others; BIOS updates come from them. |
| Architecture | Generation, graphics core (GC) version and `gfx` target. | See [GPU generations](#gpu-generations). |
| Kernel driver | Driver and DRM card name. | |
| VRAM | Total, used (and percent), and memory chip maker. | Running out of VRAM causes stutter. |
| CPU-visible VRAM | How much VRAM the CPU can reach directly. "(Resizable BAR active)" when it is all of it. | See Background. |
| GTT | Graphics translation table: system RAM the GPU may map; total and used. | Overflow space when VRAM is full. |
| VBIOS | VBIOS version. | |
| PCIe link | Current link speed and width, and the maximum. | A slow link costs performance. |
| Unique ID | A chip-specific ID number (shown as `XXXX` in this guide). | Tells two identical cards apart. |
| Boot VGA | Whether the firmware used this card for the boot screen. | |
| Performance level | DPM mode (`auto`, `manual`...). | `manual` means a tuning tool is in control. |
| Runtime PM | `active` or `suspended`. | A second GPU may sleep when unused. |
| Board info | Form factor reported by the driver (`type : cem` = a normal plug-in PCIe card). | |

A footer line shows the motherboard and BIOS.

**Healthy / typical values.** On the author's PC: AMD Radeon RX 9070 XT, Navi 48 `[7550]`, board vendor XFX Limited (1EAE:8811), RDNA 4 GC 12.0.1 gfx1201, VRAM 15.92GB (used 1.03GB, 6.5 %) made by hynix, CPU-visible VRAM 15.92GB (Resizable BAR active), GTT 15.23GB (about half of system RAM, the default), VBIOS 113-EXT108832-100, PCIe link "32.0 GT/s PCIe x16 (max 32.0 GT/s PCIe x16)" (PCIe 5.0 x16, full speed), Boot VGA Yes, Performance level manual, Runtime PM active.

**Unusual values and what they suggest.**
- **PCIe x8 or x4 on an x16 card**: the card is in a secondary slot, the slot shares lanes with an M.2 drive or another card (see your board manual), or there is dirt in the slot. A riser cable can also drop lanes.
- **PCIe 2.5 GT/s at idle**: normal on some cards, which drop the link speed to save power. Check again under load.
- **Lower speed than max under load** (for example 16 GT/s when both card and board support 32): a riser cable, a BIOS setting forcing a lower generation, or signal problems. For gaming the loss from PCIe 5.0 to 4.0 is very small.
- **CPU-visible VRAM 256 MB**: Resizable BAR (also called Smart Access Memory) is off. Turn on "Above 4G Decoding" and "Re-Size BAR" in the BIOS. It can add a few percent in some games.
- **VRAM vendor** (hynix, samsung, micron): just information. It does not mean one card is worse.
- **Runtime PM `suspended`** on your main GPU while the screen is on: unusual; it should be `active`.

**Where Linux gets it.** `/sys/class/drm/card0/device/` (`vendor`, `device`, `subsystem_*`, `revision`, `mem_info_vram_total/used/vendor`, `mem_info_vis_vram_total`, `mem_info_gtt_total/used`, `vbios_version`, `current_link_speed/width`, `max_link_speed/width`, `unique_id`, `boot_vga`, `power_dpm_force_performance_level`, `power/runtime_status`, `board_info`), `/usr/share/libdrm/amdgpu.ids`, `lspci -mm -nn`, `ip_discovery`.

**Background.**
- *VRAM* is the card's own fast memory. *GTT* is ordinary system RAM the GPU can use over PCIe; it is much slower for the GPU.
- *BAR* (base address register) is a window through which the CPU sees a device's memory. Classic BARs are 256 MB. *Resizable BAR* lets the window cover all VRAM, so the CPU can write textures directly instead of through a small moving window.
- *PCIe link speed* is the transfer rate per lane: 2.5 GT/s = PCIe 1, 5 = PCIe 2, 8 = PCIe 3, 16 = PCIe 4, 32 = PCIe 5. *Width* is the number of lanes (x1, x4, x8, x16). Bandwidth is speed times width.
- AMD cards contain a small PCIe switch (the "Navi 10 XL Upstream/Downstream Port" entries in `lspci`). The GPU itself sits behind it, so the link value here is the link inside the card. On most AMD cards it reports the same as the slot link, but `lspci -vv` of the upstream port shows the real slot link if you need to be sure.

---
## GPU LEDs
`gpu-leds` - Not present on most PCs

**In plain words.** Some graphics cards have RGB lighting that software can control. This item says whether such a controller is present.

**What it shows.**
- **Board** - GPU vendor:device and subsystem IDs.
- **LED device** - each LED class device (`/sys/class/leds`) that belongs to the GPU, if the kernel exposes any.
- **RGB controller** - `none exposed by the kernel for this GPU` when there is none (the author's PC).
- **Checked** - how it was decided: the GPU's LED class devices. SRS never probes the GPU's I2C buses to look for lighting chips; many GPU RGB controllers sit there and only tools such as OpenRGB reach them.

**What "not present" means.** No RGB lighting that the GPU controls. A card can still have lights driven from a motherboard ARGB header; those belong to the motherboard, not the GPU. Tools such as OpenRGB handle both.

**Where Linux gets it.** PCI IDs in `/sys/class/drm/card0/device`; `/sys/class/leds/*/device`.

---
## GPU mapping
`gpu-map` - Linux data

**In plain words.** Where the GPU sits in the PCI system: its address, the path from the CPU to it, its interrupt, IOMMU group, device files, other functions on the same card, and the memory windows (BARs) it uses.

**What you see.**

| Field | Meaning |
|---|---|
| Bus/Dev/Func | PCI address (domain:bus:device.function). |
| PCI path | Chain of bridges from the CPU's root port to the GPU. |
| IOMMU group | The isolation group for this device (matters for passing the GPU to a virtual machine). |
| IRQ | Interrupt number. |
| NUMA node | Memory node the device is closest to; `-1` = no NUMA information (normal on single-socket desktops). |
| Local CPUs | CPUs that are "close" to the device. |
| DRM nodes | Device files: `cardN` (full access, used by compositors), `renderDN` (render-only, used by games and compute), `controlDN` (old, unused). |
| hwmon | Name of the GPU's sensor folder. |
| Function | Other functions at the same slot, e.g. the HDMI/DP audio controller. |
| Resources | Each BAR: address range, size, Memory or I/O, prefetchable or not. `ROM` = expansion ROM window. |

**Healthy / typical values.** On the author's PC: 0000:03:00.0, path `pci0000:00/0000:00:01.1/0000:01:00.0/0000:02:00.0/0000:03:00.0` (CPU root port, then the card's own switch, then the GPU), IOMMU group 15, IRQ 161, NUMA node -1, local CPUs 0-15, DRM nodes `card0 controlD64 renderD128`, hwmon9, audio function at 03:00.1. BAR 0 = 16GB prefetchable (all VRAM visible, so Resizable BAR is on), BAR 2 = 256MB (doorbell/registers window), BAR 4 = 256B I/O, BAR 5 = 512KB registers, ROM 128KB.

**Unusual values and what they suggest.**
- BAR 0 only 256MB: Resizable BAR is off (see [GPU information](#gpu-information)).
- GPU and many other devices in the same IOMMU group: passing just the GPU to a virtual machine will be hard on this board or slot.
- PCI path through the chipset (more bridges, a `600 Series Chipset` port): the card is in a slot wired to the chipset instead of the CPU, which adds latency and limits bandwidth.

**Where Linux gets it.** `/sys/class/drm/card0/device` (realpath, `iommu_group`, `irq`, `numa_node`, `local_cpulist`, `drm/`, `hwmon/`, `resource`), `/sys/bus/pci/devices`, `lspci -mm -nn`.

**Background.** An IOMMU (input-output memory management unit) controls which memory each device may reach. Devices that cannot be separated from each other share a group.

---
## GPU power status
`gpu-power` - Linux data

**In plain words.** How much power the GPU is using now, its power limit, core voltage and power-management state. Updates every second in the GUI.

**What you see.**

| Field | Meaning |
|---|---|
| Power sensor | Label of the power sensor. `PPT` = package power tracking, the power the firmware counts toward the limit. |
| Power (average) | Average power in watts. |
| Power (input) | Instant power in watts (only on drivers that offer it). |
| Power limit | Current limit, with the default and the allowed range. |
| vddgfx (in0) / in1 | Voltages in millivolts (`vddgfx` = core voltage). |
| GPU load | Graphics engine busy percent. |
| DPM state | Overall power state: `battery`, `balanced` or `performance`. |
| Performance level | DPM mode, as in [GPU clock speeds](#gpu-clock-speeds). |
| Runtime PM | `active` / `suspended`. |
| PCI power state | `D0` = fully on; `D3hot` / `D3cold` = asleep. |
| gpu_metrics | Only in text reports: the version and size of the driver's metrics block. Only its header is decoded. |

**Healthy / typical values.** On the author's PC at the desktop: PPT 34-35 W, power limit 374 W (default 304 W, range 212 - 374 W), vddgfx 616-701 mV, DPM state performance, Performance level manual, PCI power state D0, gpu_metrics v1.3 120 bytes. The limit is at the top of the range because it has been raised in LACT; stock it would read 304 W.

**Unusual values and what they suggest.**
- Idle power above about 50 W on one monitor: the memory clock is stuck high (see [GPU clock speeds](#gpu-clock-speeds)) or a program keeps the GPU busy.
- In a game, power sitting exactly at the limit while clocks drop: the card is power-limited. That is normal for modern GPUs. Raising the limit (if your PSU and cooling allow) or undervolting gives more speed.
- Power limit lower than default: something lowered it (a tuning tool or a power-saving profile).
- `D3cold` on your display GPU: should not happen while screens are on.

**Where Linux gets it.** GPU hwmon `power1_label`, `power1_average`, `power1_input`, `power1_cap`, `power1_cap_default`, `power1_cap_min`, `power1_cap_max`, `in0_input`, `in1_input` (and labels); `gpu_busy_percent`, `power_dpm_state`, `power_dpm_force_performance_level`, `power/runtime_status`, `power_state`, `gpu_metrics` in `/sys/class/drm/card0/device`.

**Background.** The SMU (system management unit) is a small processor inside the GPU. It watches power, temperature and current many times a second and lowers clocks when a limit is reached. The power limit is how many watts it lets the card use. The allowed range is set by the card maker in the VBIOS.

---
## GPU registry
`gpu-reg` - Linux data

**In plain words.** Windows keeps driver settings in the registry. On Linux, the equivalent is how the kernel registered the device and the options the GPU driver module was loaded with.

**What you see.**
- **uevent** - the device's registration data: `DRIVER`, `PCI_CLASS` (30000 = display controller), `PCI_ID`, `PCI_SUBSYS_ID`, `PCI_SLOT_NAME`, `MODALIAS` (the string udev uses to find the right driver).
- **Module** - driver module name.
- **Module srcversion** - a checksum of the module's source; changes when the driver changes.
- **Kernel** - running kernel version.
- **Module parameters** - every parameter in `/sys/module/amdgpu/parameters` and its current value.

Some useful amdgpu parameters: `ppfeaturemask` (which power features are on; `0xfff7ffff` has the OverDrive bit set, which tuning tools need), `dc` (Display Core, `-1` = auto), `runpm` (runtime power management), `aspm` (PCIe link power saving), `gpu_recovery` (`1` = try to reset the GPU after a hang), `dcfeaturemask` / `dcdebugmask` (display feature and debug bits), `sg_display`, `freesync_video`, `deep_color`, `lockup_timeout`, `vm_*` (GPU virtual memory settings), `pcie_gen_cap` / `pcie_lane_cap` (forced PCIe limits, `0` = none). `-1` almost always means "driver default".

**Healthy / typical values.** On the author's PC: `DRIVER amdgpu`, `PCI_ID 1002:7550`, `PCI_SUBSYS_ID 1EAE:8811`, kernel 7.3.0, `ppfeaturemask 0xfff7ffff`, `dcfeaturemask 8`, `gpu_recovery 1`, most others `-1` or `0`.

**Unusual values and what they suggest.**
- A parameter that is not default and you do not remember setting: look in `/etc/modprobe.d/` and the kernel command line (`amdgpu.<name>=`).
- `ppfeaturemask` without the OverDrive bit: tuning tools such as LACT or CoreCtrl cannot change clocks or voltages.

**Where Linux gets it.** `/sys/class/drm/card0/device/uevent`, `driver/module`, `/sys/module/amdgpu/srcversion`, `uname`, `/sys/module/amdgpu/parameters/*`.

---
## GPU SCF
`gpu-scf` - Linux data

**In plain words.** SCF stands for Scalable Control Fabric (with the Scalable Data Fabric next to it): AMD's internal network that links the parts of a chip. This item shows the versions of the fabric-related blocks and their clocks.

**What you see.**
- **Data Fabric IP** - version of the data fabric (`DF`).
- **SMU (MP1) IP** - version of the power manager.
- **NBIF IP** - version of the PCIe bus interface.
- **MMHUB IP** - version of the memory hub.
- DPM rows for **Fabric (fclk)**, **SoC (socclk)** and **PCIe**, as in [GPU clock speeds](#gpu-clock-speeds).

**Healthy / typical values.** On the RX 9070 XT: Data Fabric 4.15.1, SMU 14.0.3, NBIF 6.3.1, MMHUB 4.1.0; fclk [2105] of 313 / 2105 / 2400 MHz; socclk [1280] of 417 / 1280 / 1476 MHz; PCIe [32.0GT/s, x16].

**Unusual values and what they suggest.** Fabric clock stuck at its lowest step under heavy load would limit memory bandwidth, but this is rare; usually the firmware handles it. Rows missing means the driver does not offer that clock table for this GPU.

**Where Linux gets it.** `ip_discovery` (`DF`, `MP1`, `NBIF`, `MMHUB`) and `pp_dpm_fclk`, `pp_dpm_socclk`, `pp_dpm_pcie`.

---
## GPU SLI topology
`gpu-sli` - Not present on most PCs

**In plain words.** SLI (NVIDIA) and CrossFire (AMD) were ways to make two graphics cards share one game. They are dead on modern cards and never existed on Linux in that form.

**What it shows.** **GPUs** (count), one line per GPU (card, name, slot), and **SLI / CrossFire**: "not applicable - single GPU", or for more than one GPU, "multiple GPUs present; Linux has no SLI/CrossFire link (PRIME / Vulkan device groups instead)".

**What "not present" means.** Normal. On Linux, several GPUs work through PRIME (one GPU renders, another displays, common on laptops) or through programs that use several Vulkan devices on their own.

**Where Linux gets it.** `/sys/class/drm`.

---
## GPU temperatures
`gpu-temp` - Linux data

**In plain words.** The GPU's temperature sensors with their shutdown limits. Updates every second in the GUI.

**What you see.** One row per sensor, columns **Sensor**, **Current**, **Critical**, **Hysteresis**, **Emergency** (`-` when the driver gives no value). AMD cards have:
- **edge** - temperature at the edge of the chip. The oldest and lowest reading.
- **junction** - the hottest spot on the chip (also called hotspot). This is what the fan curve and throttling use.
- **mem** - the video memory temperature.

Below the table, if the driver offers it, a note about thermal throttling logging (for example "thermal throttling logging enabled, with interval 60 seconds": the driver writes a line to the kernel log, at most once a minute, when the card slows down because of heat).

**Healthy / typical values.** On the author's PC at the desktop: edge 25 °C, junction 28-29 °C, mem 50 °C. Critical 110 °C and emergency 115 °C for edge and junction; 108 / 113 °C for memory. Under gaming load, junction 70-90 °C and memory up to about 90 °C are normal for this class of card.

**Unusual values and what they suggest.**
- Junction more than about 20-25 °C above edge under load: poor contact between cooler and chip (thermal paste or pad problem, or a warped cooler).
- Junction near 100-110 °C: the card is throttling. Check fans, case airflow and dust.
- Memory hotter than about 95-100 °C: memory pads are not touching the cooler well.
- At idle, memory warmer than the chip (as here) is normal: memory does not power down as much.

**Where Linux gets it.** GPU hwmon `temp*_input`, `temp*_label`, `temp*_crit`, `temp*_crit_hyst`, `temp*_emergency`, and `thermal_throttling_logging` in the device folder.

**Background.** *Critical* is where the driver warns and the firmware slows the card hard. *Emergency* is where the card switches itself off to protect the chip. *Hysteresis* is how far the temperature must fall before a limit counts as cleared.

---
## GPU tree
`gpu-tree` - Linux data

**In plain words.** A tree view of every GPU: where it is on the PCI bus, its driver, sensor folder and device files, then each display output and the monitor on it.

**What you see.** For each card: the card name and GPU name, then branches:
- `PCI` - the device path.
- `driver` - kernel driver.
- `hwmonN` - sensor folder.
- `/dev/dri/...` - device files other than `cardN` (`renderD128`, `controlD64`).
- One branch per output (`DP-1`, `HDMI-A-1`...) with its status (`connected` / `disconnected`) and whether it is enabled (`enabled` / `disabled`). Under connected outputs: the monitor's PnP ID, name, maker and preferred resolution from EDID.

**Healthy / typical values.** On the author's PC: `card0 AMD Radeon RX 9070 XT`, driver amdgpu, hwmon9, `/dev/dri/controlD64`, `/dev/dri/renderD128`; DP-1 and DP-2 disconnected; DP-3 connected enabled, "GSM LG ULTRAGEAR LG Electronics 2560×1440"; HDMI-A-1 connected enabled, "BNQ BenQ RL2755 BenQ Corporation 1920×1080".

**Unusual values and what they suggest.**
- `connected disabled`: the monitor is plugged in and detected, but the desktop has it turned off (or it is a mirrored or disabled screen).
- A monitor plugged in but shown `disconnected`: cable, port or monitor input problem.

**Where Linux gets it.** `/sys/class/drm` (cards and `cardN-<connector>` folders, `status`, `enabled`, `edid`), the device's `drm/` and `hwmon/` folders.

---
## Graphics
`graphics` - Linux data

**In plain words.** One block per active display: which card drives it, which monitor it is, the connector, the current resolution and refresh rate, and every video mode the monitor offers.

**What you see.** For each connected display:
- Heading: `Display N`, VRAM size, VBIOS version, GPU name.
- **Graphics Card** - GPU name.
- **Monitor** - maker and model from EDID, with the connector.
- **Graphics Chip** - chip name and device ID.
- **Graphics DAC** - always "Integrated (digital output)".
- **Connector** - connector name, kernel connector ID, and DPMS state (`On`, `Standby`, `Suspend`, `Off`; DPMS = display power management signaling, the monitor's sleep state).
- **Resolution** - current mode, `width×height@refresh`.
- **Unique X of Y** - how many different resolutions are in the mode list, out of how many modes in total.
- **Video Modes** - one row per resolution, with all its refresh rates. `*` marks the monitor's preferred (native) mode.

At the end: the total number of displays, how long the scan took, and where monitor names come from. If `drm_info` is not installed the mode list is replaced by a note saying so. A GPU with nothing connected shows "Monitor none connected".

**Healthy / typical values.** On the author's PC: Display 0 = LG ULTRAGEAR on DP-3, connector id 159, DPMS On, resolution 2560×1440@164.96; 21 modes, 8 unique resolutions; the native row reads "2560×1440 120.00 * 164.96 143.97 59.95". Display 1 = BenQ RL2755 on HDMI-A-1, 1920×1080@60.00, 31 modes, 14 unique.

**Unusual values and what they suggest.**
- Current refresh lower than the monitor's maximum (for example 60 Hz on a 165 Hz monitor): change it in display settings. If the high rate is not offered, the cable or port may not have enough bandwidth (older HDMI cables, DisplayPort to HDMI adapters).
- The `*` preferred mode is not the highest rate: normal. Many high-refresh monitors mark a lower rate as preferred for compatibility, as the LG does here (120 Hz).
- Resolution lower than native: the picture will be blurry. Set the native resolution.

**Where Linux gets it.** `/sys/class/drm/card*-*/` (status, EDID, connector ID, DPMS), `drm_info` (mode list, current mode on each CRTC), `/usr/share/libdrm/amdgpu.ids`, `lspci`, `/usr/share/hwdata/pnp.ids`.

**Background.** KMS (kernel mode setting) is the part of the kernel's DRM that sets screen modes. A connector is a physical output. Each mode is a resolution plus timing; the same resolution can appear at many refresh rates.

---
## Joysticks
`joysticks` - Linux data

**In plain words.** Game controllers and joysticks the system can see.

**What you see.**
- A table of `/dev/input/js*` devices: **Device** path, **Name**, **VID:PID** (USB vendor and product ID) and **Bus** (`USB`, `Bluetooth`, or the raw bus number). If there are none: "No joystick (/dev/input/js*) devices".
- **Game controllers in /proc/bus/input/devices** - every input device that has a `js` handler, with its handlers (`eventN jsN`).
- A note that event devices are only listed, never opened.

**Healthy / typical values.** On the author's PC: `/dev/input/js0 Pro Controller 057e:2009 USB` (a Nintendo Switch Pro Controller), handlers `event21 js0`.

**Unusual values and what they suggest.**
- Controller works in Steam but not listed here: Steam Input may be presenting it as a virtual device, or the controller exposes only an event device (no `js`). Modern games use event devices, so this is fine.
- Not listed at all: check the cable, the Bluetooth pairing, or whether the controller needs a driver (some Xbox wireless adapters need `xone`).

**Where Linux gets it.** `/sys/class/input/js*/device/{name,id/vendor,id/product,id/bustype}` and `/proc/bus/input/devices`.

---
## Monitor ID database
`mon-info` - Linux data

**In plain words.** Every monitor carries a three-letter maker code (PnP ID) in its EDID, such as `GSM` for LG or `BNQ` for BenQ. This item shows the database that turns those codes into names, the codes of your connected monitors, and lets you search the database.

**What you see.**
- Database path, its date and how many vendor IDs it holds.
- **Connected monitors** - for each: PnP ID, maker name (or "(unknown vendor)"), product code (PnP ID plus 4 hex digits), monitor name and connector.
- A hint: open the item as `mon-info:<text>` to search, for example `mon-info:GSM` or `mon-info:BenQ`.
- With a search: every matching line (up to 200) as code and name. A few common monitor makers that the official list lacks are built into SRS and marked "(built-in, not in pnp.ids)". "no match" if nothing fits.

**Healthy / typical values.** On the author's PC: `/usr/share/hwdata/pnp.ids` dated 2026-10-04 with 2557 IDs; GSM LG Electronics GSM7766 LG ULTRAGEAR on DP-3; BNQ BenQ Corporation BNQ7F41 BenQ RL2755 on HDMI-A-1.

**Unusual values and what they suggest.**
- "(unknown vendor)": the code is not in the database. Update `hwdata`, or the monitor uses an unregistered code (common with cheap no-name monitors).
- "pnp.ids not found": install the `hwdata` package.

**Where Linux gets it.** `/usr/share/hwdata/pnp.ids` and connector EDID.

---
## Monitors
`monitors` - Linux data

**In plain words.** Everything each monitor says about itself in its EDID: maker, model, serial number, manufacture date, size, supported refresh range, native mode, plus the current mode and adaptive sync state.

**What you see.**

First a summary table, one row per connected monitor ("Showing X of Y for \\<computer>"):

| Column | Meaning |
|---|---|
| PnP ID | Maker code plus product code (e.g. `GSM7766`). |
| Device Description | Maker name and model. |
| Device Name | Connector (`DP-3`). |
| Serial Number | Serial number text from EDID, or `-`. |
| EDID Version | 1.3, 1.4... |
| Manufacture Year - Week | When the monitor was made. |
| Monitor Display Size | Width × height in cm. |
| Monitor Resolution | Native resolution. |
| Vertical Rates | Supported refresh range in Hz (or the native rate if the EDID has no range). |
| Horizontal Rates | Supported line-rate range in kHz (or `-`). |

Then a detail block per monitor:

| Field | Meaning |
|---|---|
| Heading | Connector, kernel name and connector ID. |
| Monitor | Model name. |
| Manufacturer | PnP ID and maker name ("(not in pnp.ids)" if unknown). |
| Product code | Hex product code. |
| Serial number | Serial text and the separate numeric serial in hex (shown as `XXXX` in this guide). |
| Manufactured / Model year | Year and week, or a model year if the maker gave only that. |
| EDID version | Version, checksum `OK` or `BAD`, number of extension blocks. |
| Extensions | Type of each extension: `CTA-861` (audio, HDR, extra modes for TVs and HDMI), `DisplayID` (newer timing data, common for high refresh rates), `VTB`, `DI`, `LS`, `DPVL`, `Block map`, `Manufacturer`. |
| Display size | Screen size in cm (rounded). |
| Image size | Exact picture size in mm, if given. |
| Input | Digital or Analog; for EDID 1.4 also bits per colour (`bpc`) and interface (DVI, HDMI-a, HDMI-b, MDDI, DisplayPort). |
| Gamma | Display gamma (typically 2.20). |
| Preferred mode | Native mode with exact refresh rate and pixel clock. |
| Vertical rates / Horizontal rates / Max pixel clock | From the range-limits block: refresh range, line-rate range, highest pixel clock. |
| Text | Free text in the EDID, if any. |
| Status | Connected or not, enabled or not, DPMS state. |
| Current mode | Mode in use now (needs `drm_info`). |
| Adaptive sync | `capable, enabled`, `capable, off now` or `not capable` (needs `drm_info`). |

You can open `monitors:DP-3` to show only one output. The last line gives the date and size of the PnP ID database.

**Healthy / typical values.** On the author's PC:
- LG ULTRAGEAR on DP-3: EDID 1.4, checksum OK, 2 extensions (CTA-861 and DisplayID), made 2023 week 3, 70 × 39 cm (697 × 392 mm image, a 31.5" class screen), digital 10 bpc DisplayPort, gamma 2.20, preferred 2560 × 1440 @ 120.00 Hz (pixel clock 497.75 MHz), vertical 48 - 165 Hz, max pixel clock 800 MHz, current mode 2560×1440@164.96, adaptive sync capable, off now.
- BenQ RL2755 on HDMI-A-1: EDID 1.3, 1 extension (CTA-861), made 2015 week 38, 60 × 34 cm, digital, preferred 1920 × 1080 @ 60.00 Hz (148.50 MHz), vertical 50 - 76 Hz, horizontal 30 - 83 kHz, max pixel clock 170 MHz, adaptive sync not capable.
- Serial number: `XXXX` (each monitor has its own).

**Unusual values and what they suggest.**
- **checksum BAD**: the EDID got corrupted on the way (bad cable, cheap adapter or KVM switch), or the monitor's EDID chip is faulty. The driver may then fall back to safe low resolutions.
- **"no EDID"** for a connected output: the cable or adapter does not carry the DDC wires, or the monitor is off.
- **Display size 0 × 0 cm**: projectors and some TVs leave it out on purpose.
- **Manufacture year far in the future or before 1990**: a made-up value from the maker. Harmless.
- **Horizontal range with min = max**: some monitors fill in placeholder numbers. Trust the DisplayID or CTA extension data instead.
- **6 bpc on a monitor sold as 8-bit**: the panel may use dithering (FRC).

**Where Linux gets it.** `/sys/class/drm/card*-*/edid` (decoded by SRS itself), `/usr/share/hwdata/pnp.ids`, and `drm_info` for current mode and VRR.

**Background.** EDID (Extended Display Identification Data) is a 128-byte block, plus 128-byte extensions, stored in a small chip in the monitor. The GPU reads it over the DDC wires in the cable when the monitor is plugged in. It tells the computer who made the monitor, what modes it can show and its physical size, so the desktop can pick the right resolution and scaling.

---
## Monitor tree
`monitor-tree` - Linux data

**In plain words.** The same tree as [GPU tree](#gpu-tree), but only the outputs and monitors, without the PCI, driver and device-file branches.

**What you see.** For each card: card name and GPU name, then each output with `connected`/`disconnected` and `enabled`/`disabled`, and under connected outputs the monitor's PnP ID, name, maker and preferred resolution.

**Healthy / typical values.** On the author's PC: DP-1 and DP-2 disconnected; DP-3 connected enabled, GSM LG ULTRAGEAR LG Electronics 2560×1440; HDMI-A-1 connected enabled, BNQ BenQ RL2755 BenQ Corporation 1920×1080.

**Unusual values and what they suggest.** See [GPU tree](#gpu-tree).

**Where Linux gets it.** `/sys/class/drm` connectors and EDID.

---
## OpenCL
`opencl` - Linux data

**In plain words.** OpenCL is an open standard for running general computing tasks (not graphics) on the GPU, used by some photo, video, science and crypto programs. This item shows the OpenCL platforms (drivers) and the devices each one offers.

**What you see.**
- **Platforms** - how many OpenCL platforms are installed.
- **Platform** - one line per platform: name and version.
- Then a block per platform, headed by its name: **Devices** (count), then for each device:

| Field | Meaning |
|---|---|
| Device Type | `CL_DEVICE_TYPE_GPU`, `CPU` or `ACCELERATOR`. |
| Board name | Marketing name (some drivers only). |
| Name | Device name (ROCm gives the `gfx` target). |
| Vendor | Driver vendor. |
| Max compute units | Number of compute units the driver reports. |
| Max clock frequency | Highest core clock. |
| Global memory size | Memory usable by kernels, in bytes. |
| Max memory allocation | Largest single buffer allowed, in bytes. |
| Local memory size | Fast on-chip shared memory per work-group, in bytes (64 KB on AMD). |
| Version / Device OpenCL C version | OpenCL version of the device and of its kernel language. |
| Driver version | Driver build. |
| Profile | `FULL_PROFILE` (full standard) or `EMBEDDED_PROFILE` (reduced feature set). |

A note at the end names the source (`clinfo`) and the ICD folder. If `clinfo` is missing it says "not installed"; if it prints nothing: "no output (no OpenCL platform?)".

**Healthy / typical values.** On the author's PC there are 2 platforms:
- **AMD Accelerated Parallel Processing** (ROCm), OpenCL 2.1 platform: RX 9070 XT, 32 compute units, 2400 MHz, global memory 17095983104 bytes (15.92 GiB), max allocation about 14.5 GB, local memory 65536, name `gfx1201`, OpenCL C 2.0, driver 3581.0, FULL_PROFILE.
- **rusticl** (Mesa), OpenCL 3.1: 64 compute units, global memory 17179869184 bytes, max allocation 2147483647 (2 GiB), OpenCL C 1.2, EMBEDDED_PROFILE.

The two drivers count compute units differently: the RX 9070 XT has 64 compute units, grouped in 32 pairs (WGPs, work-group processors). ROCm reports the pairs, rusticl reports the units.

**Unusual values and what they suggest.**
- No GPU platform, only a CPU one (for example PoCL): install `rocm-opencl-runtime` (AMD), or enable rusticl.
- Programs fail with "out of resources" on large jobs under rusticl: its 2 GiB maximum allocation is lower; use the ROCm platform for those.
- Platform listed but 0 devices: the driver cannot access the GPU, often because your user is not in the `render` group, or `/dev/kfd` is missing for ROCm.

**Where Linux gets it.** `clinfo`, which loads every ICD in `/etc/OpenCL/vendors`.

---
## RSR info
`rsr-info` - Windows-only (Linux equivalent shown)

**In plain words.** RSR (Radeon Super Resolution) is a feature of AMD's Windows driver that runs a game at a lower resolution and upscales it with FSR 1 (FidelityFX Super Resolution). It does not exist in the Linux driver, so SRS shows the Linux tools that do the same job.

**What it shows.**
- **RSR** - "Windows-only (Adrenalin driver feature)".
- **Linux equivalent** - `gamescope -F fsr` (Valve's game compositor with FSR upscaling), `WINE_FULLSCREEN_FSR` (FSR built into Proton-GE and wine-tkg builds) and vkBasalt CAS (a sharpening filter).
- **gamescope** - installed or not.
- **vkBasalt** - installed or not (the program, or its Vulkan layer file).
- **WINE_FULLSCREEN_FSR** - current value of that environment variable, or "(not set)".

**Typical values.** On the author's PC: gamescope installed, vkBasalt installed, `WINE_FULLSCREEN_FSR` not set (normal: it is usually set per game in Steam launch options).

**Where Linux gets it.** File checks in `/usr/bin` and `/usr/share/vulkan/implicit_layer.d`, and the environment.

---
## RTSS status
`rtss-sts` - Not present on most PCs

**In plain words.** RTSS (RivaTuner Statistics Server) is a Windows tool for on-screen statistics and frame-rate limits. It does not run on Linux. The nearest Linux tool is MangoHud.

**What it shows.**
- **RTSS** - "RivaTuner Statistics Server is Windows-only - not present".
- **Nearest** - "MangoHud (overlay + fps_limit)".
- **mangohud** - installed or not.
- **MANGOHUD env** - value of the `MANGOHUD` variable (`1` = on for all Vulkan programs), or "(not set)".
- **MangoHud.conf** and **fps_limit** - the config file path and its frame-rate limit (`0` = no limit), when the file exists.

**Typical values.** On the author's PC: mangohud installed, `MANGOHUD env 1`, `fps_limit 0`. For all MangoHud settings see [GOverlay status](#goverlay-status).

**Where Linux gets it.** File check for `mangohud`, the environment, `~/.config/MangoHud/MangoHud.conf`.

---
## Sound devices
`sound` - Linux data

**In plain words.** The sound cards, every ALSA device file they created, and the ALSA driver version.

**What you see.**
- The card list, as in [Audio devices](#audio-devices).
- **ALSA device nodes (/proc/asound/devices)** - one line per device: a minor number, `[card- device]`, and the type: `digital audio playback`, `digital audio capture`, `control` (the mixer), `hardware dependent` (the codec's own interface), `sequencer` (MIDI), `timer`.
- A note with the ALSA driver version line.

**Healthy / typical values.** On the author's PC: card 0 has four playback devices (3, 7, 8, 9: one per GPU output), a hardware-dependent node and a control; card 1 has playback 0 and 1, capture 0 and 2, a hardware-dependent node and a control; plus the global sequencer and timer. ALSA version follows the kernel ("k7.3.0-...").

**Unusual values and what they suggest.** A card with a control but no playback or capture devices: the codec was not recognised. Check `dmesg` for `snd_hda_codec` messages; a newer kernel or `linux-firmware` may help.

**Where Linux gets it.** `/proc/asound/cards`, `/proc/asound/devices`, `/proc/asound/version`.

---
## UAA (HD Audio) devices
`uaa-dev` - Linux data

**In plain words.** UAA (Universal Audio Architecture) is Windows' name for the HD Audio standard. This item shows the audio chips (codecs) on each HD Audio controller, and which monitors are connected to the HDMI/DisplayPort audio outputs.

**What you see.**
- For each card and codec (`card0 codec#0`...):
  - **Codec** - chip name (e.g. Realtek ALC1220).
  - **Address** - codec slot on the HD Audio link (0-15).
  - **AFG Function Id** - the audio function group; `unsol` says whether the codec can send unsolicited events (used for jack detection).
  - **Vendor Id** - codec vendor and chip ID (`0x10ec` = Realtek, `0x1002` = AMD).
  - **Subsystem Id** - board maker and model ID (`0x1043` = ASUS).
  - **Revision Id** - chip revision.
- `eld#X.Y` lines - for each HDMI/DP audio pin with a monitor present: the monitor name and connection type, from its ELD.
- `hwCxDy` lines - the kernel's codec device nodes: vendor name, chip name and vendor ID.

If none are found: "No HD Audio codecs found".

**Healthy / typical values.** On the author's PC: card 0 codec "ATI R6xx HDMI" (the GPU's HDMI/DP audio; the name is historic) with ELDs "LG ULTRAGEAR (DisplayPort, ELD)" and "BenQ RL2755 (HDMI, ELD)"; card 1 codec Realtek ALC1220, vendor `0x10ec1168`, subsystem `0x1043891c` (ASUS); nodes `hwC0D0` and `hwC1D0`.

**Unusual values and what they suggest.**
- No ELD line for a monitor with speakers: the monitor did not report audio support, the output is in a mode without audio, or the cable or adapter drops audio.
- Codec name "Generic" or missing: the kernel does not know this codec model yet.

**Where Linux gets it.** `/proc/asound/cardN/codec#M`, `/proc/asound/cardN/eld#X.Y`, `/sys/class/sound/hwC*D*/{vendor_name,chip_name,vendor_id}`.

**Background.** HD Audio (High Definition Audio) splits a sound system into a controller (inside the chipset, CPU or GPU) and one or more codecs (the chips that turn digital into analog and drive the jacks). The GPU's HDMI audio is just another HD Audio controller with its own codec. ELD (EDID-Like Data) is the audio part of the monitor's EDID, passed to the sound driver: speaker layout and supported formats.

---
## Wave devices
`wave` - Linux data

**In plain words.** "Wave" devices are Windows' name for basic digital audio playback and recording. This item lists every ALSA playback and capture device and whether a program is using it right now.

**What you see.** Two lists, **Wave out (playback)** and **Wave in (capture)**. Each line: `hw:card,device`, device name, and state: `closed`, or `open: <format> <channels>ch <rate>` with the sample format (e.g. `S32_LE` = signed 32-bit little-endian, `S16_LE` = 16-bit), channel count and sample rate.

**Healthy / typical values.** On the author's PC: playback HDMI 0-3 (`hw:0,3`, `hw:0,7`, `hw:0,8`, `hw:0,9`) closed, `hw:1,0` ALC1220 Analog closed, `hw:1,1` ALC1220 Digital "open: S32_LE 2ch 96000 (96000/1)" (PipeWire is sending 32-bit, 96 kHz stereo to the optical output); capture `hw:1,0` and `hw:1,2` closed.

**Unusual values and what they suggest.**
- A device you are not using shown open: some program holds it. With PipeWire, the sound server usually holds the active output open, which is normal.
- A program cannot open a device that is listed here as open: another program has it exclusively. Route through PipeWire instead of using `hw:` directly.

**Where Linux gets it.** `/proc/asound/pcm` and `/proc/asound/cardN/pcmD{p,c}/sub0/hw_params`.
