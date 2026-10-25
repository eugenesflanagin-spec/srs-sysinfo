# Network  (SRS Section Reference)

[Back to the section index](README.md) - [Glossary](../GLOSSARY.md)

This group covers everything about how your PC talks to other machines: network adapters and
their addresses, traffic counters, the routing and neighbour tables, open ports, DNS, the firewall,
saved connection profiles, Wi-Fi, Bluetooth, and Windows-style file sharing (SMB/Samba).
Open it from **Menu -> Network -> item**, or press the **Adapters** panel button. Its sub-buttons
jump to Counters, Interface, ARP, Routing, TCP V4, TCP V6, Wi-Fi, Bluetooth, Show DNS, Ping and
Main. Every item here only reads local files and asks local tools; nothing is sent on the network.
The one exception is the **Ping** button. It opens the Tools group's Ping item, which sends three
pings to your default gateway (your router). That is the only network traffic SRS ever makes.
Three items need root data (run **srs-root**) to be complete: DNS cache, Firewall and WLAN
profiles. The port tables also show more process names under root.

## Contents
- [Network adapters](#network-adapters) `adapters`
- [ARP table](#arp-table) `arp-table`
- [Bluetooth radios](#bluetooth-radios) `bluetooth`
- [Bluetooth IDs](#bluetooth-ids) `bluetooth-ids`
- [Bluetooth device tree](#bluetooth-device-tree) `blue-tree`
- [Network browse masters](#network-browse-masters) `browsers`
- [TCP/IP counters](#tcpip-counters) `counters`
- [DNS cache](#dns-cache) `dns-cache`
- [DNS names](#dns-names) `dns-names`
- [DNS ports](#dns-ports) `dns-ports`
- [DNS usage](#dns-usage) `dns-usage`
- [Domains / workgroups](#domains--workgroups) `domains`
- [Firewall](#firewall) `firewall`
- [Network interface statistics](#network-interface-statistics) `interface`
- [IP address](#ip-address) `ip-address`
- [LAN Manager share setup](#lan-manager-share-setup) `lm-setup`
- [MAC addresses](#mac-addresses) `mac-address`
- [Modem API](#modem-api) `modem-api`
- [Modems](#modems) `modems`
- [My browser / IP location](#my-browser--ip-location) `my-browser`
- [My IP address](#my-ip-address) `my-ip-address`
- [Network cards](#network-cards) `net-cards`
- [Network provider names](#network-provider-names) `net-names`
- [Network](#network) `network`
- [Network profiles](#network-profiles) `profiles`
- [Network protocols](#network-protocols) `protocols`
- [Proxy config](#proxy-config) `proxy`
- [Routing table](#routing-table) `routing`
- [Servers](#servers) `servers`
- [TCP V4 ports](#tcp-v4-ports) `tcp-ip4-ports`
- [TCP V6 ports](#tcp-v6-ports) `tcp-ip6-ports`
- [Network transports](#network-transports) `transports`
- [UDP V4 ports](#udp-v4-ports) `udp-ip4-ports`
- [UDP V6 ports](#udp-v6-ports) `udp-ip6-ports`
- [Wi-Fi SSIDs](#wi-fi-ssids) `wi-fi`
- [WLAN profiles](#wlan-profiles) `wlan`

A note on the examples: real addresses from the author's PC have been replaced. The home network
is shown as 192.168.1.x (router 192.168.1.1, wired card 192.168.1.20, Wi-Fi 192.168.1.21).
MAC addresses, link-local IPv6 addresses and Wi-Fi names are shown as `XX:XX:...`, `fe80::XXXX`
and "HomeWiFi".

---
## Network adapters  
`adapters` - Linux data

**In plain words.** One block per network adapter (real or virtual), with its addresses, how it
got them, its DNS servers, gateway and the multicast groups it listens to. This is the Linux
version of Windows' `ipconfig /all`.

**What you see.**

Top line (whole PC):

| Field | Meaning | Why it matters |
|---|---|---|
| Linux IP Configuration | Your OS name and kernel. | Context. |
| Host | The computer's host name. | How other machines may know you. |
| Domain | DNS (Domain Name System) domain or search domain, "-" if none. | Home PCs usually have none. |
| DNS IP Address | DNS servers from `/etc/resolv.conf`, one per line. | Who turns names into addresses. |
| Routing / Proxy | "Enabled/Disabled" for IP forwarding, then for a proxy in the environment. | Forwarding on means the PC can pass packets between adapters (Docker and VMs turn it on). |

Then one block per adapter (physical first, then virtual, loopback last). Each block has at least
three rows:

| Column | Meaning | Why it matters |
|---|---|---|
| Adapter / Description | Row 1: interface name. Row 2: driver. Under the block: full description (PCI name, USB product, or "Bridge (virtual, driver bridge)"). | Tells you which chip and driver handle the port. |
| Type (Index) | Row 1: Ethernet, Wi-Fi, Bridge, TUN, TAP, veth, Loopback, PPP, etc., plus the kernel's interface index. Row 2: MAC address. | The index is a stable number tools use instead of the name. |
| Speed | Row 1: link speed (only when up). Row 2: MTU. Row 3: state (up, down, unknown, dormant). | Wrong speed or "down" is the first thing to check when the network is slow or dead. |
| Adapter Address | Each IPv4 and IPv6 address with its prefix length (for example 192.168.1.20/24). | The address other machines use to reach this adapter. |
| Prefix Origin | Where the network part came from: DHCP (Dynamic Host Configuration Protocol: the router hands out the address), Manual, RouterAdv (IPv6 router advertisement), WellKnown (loopback, link-local). | Shows if an address was handed out or typed in. |
| Suffix Origin | Where the host part came from: DHCP, Manual, Link (built from the MAC), Random (privacy address), WellKnown. | "Random" means IPv6 privacy addresses are on. |
| DAD State | Duplicate Address Detection: Preferred (fine), Tentative (still checking), Duplicate (another device has it), Deprecated (being phased out). | "Duplicate" is a real address conflict. |
| DNS and Gateway | DNS servers for this adapter (from NetworkManager, else resolv.conf), then GW = default gateway (IPv4, then IPv6). | Only adapters with a default route show a GW. |
| Multicast Address | Multicast groups the adapter has joined, up to four (then "+N more"). | Normal: 224.0.0.1 (all hosts), 224.0.0.251 (mDNS), 239.255.255.250 (SSDP/UPnP: Simple Service Discovery Protocol / Universal Plug and Play), ff02::1. |

You can open one adapter on its own with `adapters:<name>`, for example `adapters:em1`.

**Healthy / typical values.** On the author's PC the 2.5 GbE wired port `em1` (Realtek RTL8126,
driver r8169) shows `Ethernet (3)`, `2.5 Gbps`, `MTU 1500`, `up`, a DHCP address 192.168.1.20/24
in state Preferred, a link-local `fe80::XXXX/64` (WellKnown / Link), `DNS 192.168.1.1` and
`GW 192.168.1.1`. Virtual adapters (VMware vmnet1/vmnet8, libvirt virbr0, Docker docker0, a veth
pair, a TAP device) have Manual addresses and no gateway. Docker bridges and veth report a fake
10.0 Gbps. `lo` is `Loopback (1)` with 127.0.0.1/8 and ::1/128, MTU 65536.

**Unusual values and what they suggest.**
- Wired link at 100 Mbps or 10 Mbps on a gigabit card: bad cable (a broken pair), old switch port, or a power-saving setting.
- An address starting 169.254.: DHCP failed and Linux gave itself a fallback address.
- DAD State "Duplicate": two devices have the same IP. Check for a static address clashing with the router's DHCP range.
- Two adapters (wired and Wi-Fi) in the same subnet, both with a gateway: works, but the one with the lower route metric wins (see [Routing table](#routing-table)).
- Physical adapter state "down" with no cable plugged in is normal.

**Where Linux gets it.** `/sys/class/net/<if>/` (type, operstate, speed, mtu, address),
`ip -o addr show`, `nmcli device show`, `/etc/resolv.conf`, `/proc/net/route`, `/proc/net/igmp`
and `/proc/net/igmp6`.

**Background.** A *prefix length* (the "/24") says how many leading bits are the network part.
/24 means the first three numbers (192.168.1) are the network and the last number picks the
host. *Multicast* is one-to-many traffic: a device joins a group address and receives anything
sent to it, which is how printers and media players announce themselves.

---
## ARP table  
`arp-table` - Linux data

**In plain words.** The list of nearby devices your PC has recently talked to on the local
network, with their hardware (MAC) addresses. Below it, the services those devices announce over
mDNS (printers, speakers, phones).

**What you see.**

Neighbour table:

| Column | Meaning |
|---|---|
| IP Address | The neighbour's IPv4 or IPv6 address. |
| Physical Address | Its MAC address ("-" if not resolved). |
| HW Type | "Ethernet" for IPv4 ARP (Address Resolution Protocol) entries, "IPv6 NDP" (Neighbor Discovery Protocol) for IPv6. |
| Flags | IPv4: Dynamic (learned), Static (set by hand), Incomplete (asked, no answer yet). IPv6: the kernel state, such as REACHABLE, STALE, DELAY, PROBE, FAILED, PERMANENT. |
| Adapter | Which of your interfaces it was seen on. |

mDNS / DNS-SD services (read from avahi's cache; SRS sends no new queries):

| Column | Meaning |
|---|---|
| 1 | Your interface the announcement arrived on. |
| 2 | IPv4 or IPv6. |
| 3 | The service's name (for example a printer's name). Names keep avahi's escape codes, so `\032` is a space and `\064` is "@". |
| 4 | Service type, such as "Internet Printer", "AirPlay Remote Video", `_kdeconnect._udp`, `_adb._tcp`. |
| 5 | Domain, normally `local`. |

**Healthy / typical values.** On the author's PC the router 192.168.1.1 appears as Dynamic on both
`em1` and `wlan0`, plus a few other home devices. IPv6 entries show STALE (seen a while ago, still
usable) and the odd FAILED. The mDNS list shows a smart TV ("AirPlay Remote Video"), a Lexmark
printer and scanner (Internet Printer, `_uscan._tcp`), KDE Connect and a phone with ADB enabled.
The same service usually appears once per interface.

**Unusual values and what they suggest.**
- The router's IP shows two different MAC addresses over time: possible ARP spoofing (someone pretending to be the router), or you swapped the router.
- Many Incomplete or FAILED entries: devices that went offline, or a scan of the subnet.
- "Neighbour table is empty" on a connected PC: you have not talked to anything yet, or the adapter is down.
- "avahi-daemon cache empty or avahi not running": avahi is off, so no mDNS list.

**Where Linux gets it.** `/proc/net/arp`, `ip -o -6 neigh show`, and `avahi-browse -a -t -p -c`
(cache only).

**Background.** IP addresses are for routing; on the local cable the hardware needs a MAC address.
ARP (IPv4) and NDP (IPv6) ask "who has 192.168.1.1?" and remember the answer for a short time.
mDNS (multicast DNS) lets devices announce names and services on the LAN (local area network) without a DNS server.

---
## Bluetooth radios  
`bluetooth` - Linux data

**In plain words.** Your Bluetooth controller(s): what chip it is, which driver runs it, whether it
is blocked, and the settings and services the Bluetooth daemon reports.

**What you see.** Per controller (`hci0`, `hci1`...), from sysfs:
- **Bus** - `usb` or `pci` plus the port. Most built-in Bluetooth is a USB device even on a Wi-Fi card.
- **Driver** - usually `btusb`.
- **USB ID** - vendor:product and revision (USB only).
- **USB device** - manufacturer and product strings.
- **rfkill** - the radio kill switch: soft-blocked (turned off in software) and hard-blocked (a physical switch or BIOS).

Then, from `bluetoothctl show`, one block per controller:
- **Controller** line - its Bluetooth address and type (public/random).
- **Manufacturer, Version** - Bluetooth SIG company ID and the core spec version code (for example 0x0c = Bluetooth 5.3).
- **Name, Alias** - the name other devices see.
- **Class** - the device class code (desktop computer, etc.).
- **Powered, PowerState** - is the radio on.
- **Discoverable, DiscoverableTimeout, Pairable** - can others find and pair with it (timeout 0 = no time limit).
- **Services (UUID)** - the profiles it offers (Audio Sink/Source, A/V Remote Control, Phonebook Access, etc.).
- **Modalias** - the ID of the Bluetooth stack (BlueZ).
- **Discovering** - is a scan running now.
- **Roles** - central (connects to devices) and/or peripheral (devices connect to it).
- **Advertising Features** block - Bluetooth Low Energy advertising limits: instances, what can be included, secondary PHYs (1M, 2M, Coded), min/max transmit power, max advert length.

**Healthy / typical values.** On the author's PC: `hci0`, Bus `usb 8-11:1.0`, Driver `btusb`,
USB ID `13d3:3617`, "Realtek Bluetooth Radio" (the Bluetooth half of the RTL8922AE Wi-Fi 7 card),
rfkill "soft-blocked no, hard-blocked no", Powered yes, Version 0x0c (12), roles central and
peripheral, TX power -11 to 16 dBm.

**Unusual values and what they suggest.**
- soft-blocked yes: aeroplane mode or a desktop toggle turned Bluetooth off; `rfkill unblock bluetooth` turns it on.
- hard-blocked yes: a laptop switch or BIOS setting.
- "No Bluetooth controllers in /sys/class/bluetooth": no adapter, it is disabled in BIOS, or firmware failed to load (check `dmesg`).
- "bluetoothctl: no controllers reported": the hardware is there but `bluetoothd` (bluetooth.service) is not running.
- Discoverable yes all the time: anyone nearby can see the PC. Fine at home, less so in public.

**Where Linux gets it.** `/sys/class/bluetooth/hci*`, `bluetoothctl list` and `bluetoothctl show`.

**Background.** *rfkill* is the kernel's on/off switch for every radio (Wi-Fi, Bluetooth, mobile).
A *UUID* here is a standard number for a Bluetooth profile; the short ones (like 110b = Audio
Sink) are defined by the Bluetooth SIG.

---
## Bluetooth IDs  
`bluetooth-ids` - Linux data

**In plain words.** The ID codes of your Bluetooth controller and of every device you have paired.

**What you see.**

| Column | Meaning |
|---|---|
| Device | `hci0` etc. for a controller, "paired device" for a known device. |
| Address | "(controller)", or the device's Bluetooth address. |
| Name | Device name. |
| Modalias / ID | For a controller, `usb:vVVVVpPPPPdDDDD` (vendor, product, device release). For a device, its `bluetooth:` or `usb:` modalias, if it sends one. |
| Class / Icon | The device class code and the icon name BlueZ picks (phone, audio-headset, input-mouse...). |

A footer counts controllers and known devices.

**Healthy / typical values.** On the author's PC: `hci0` with `usb:v13D3p3617d0000`, and one paired
phone with class `0x005a020c` and icon `phone`.

**Unusual values and what they suggest.** A device with Modalias "-" just did not send one (common
for cheap devices). A long list of old devices you no longer own can be removed in your desktop's
Bluetooth settings.

**Where Linux gets it.** `/sys/class/bluetooth/<hci>/device/modalias`, `bluetoothctl devices`,
`bluetoothctl info <address>`. No discovery scan is run.

**Background.** A *modalias* is a short ID string the kernel uses to pick a driver automatically.

---
## Bluetooth device tree  
`blue-tree` - Linux data

**In plain words.** A simple tree: each controller, and under it the devices it knows, with their
status.

**What you see.**
- Controller lines from `bluetoothctl list` (address, name, `[default]` for the default one).
- One branch per known device: name, address, icon (device type).
- Under a device, its status words: `paired`, `trusted`, and `CONNECTED` if connected right now.

**Healthy / typical values.** On the author's PC: controller "X3D [default]" with one phone,
"paired trusted" (not connected at that moment).

**Unusual values and what they suggest.** Paired but not trusted means the device has to be
confirmed each time it connects. "bluetoothd not answering; devices unknown" means the controller
exists but the Bluetooth service is stopped. "No known devices" means nothing was ever paired.

**Where Linux gets it.** `bluetoothctl list`, `bluetoothctl devices`, `bluetoothctl info`.

---
## Network browse masters  
`browsers` - Linux data

**In plain words.** Which machine on your LAN keeps the list of Windows-style computers ("Network
Neighbourhood"), and how your own Samba is set up to take part.

**What you see.**
- **nmbd** - is Samba's NetBIOS name daemon running.
- **workgroup, local master, preferred master, domain master, os level** - your Samba settings for the browser election (os level 0 means "never try to win").
- A table of machines in nmbd's browse list that are **Master Browsers**: Name, Workgroup, Type (the server type bits: Workstation, Server, PrintQ, Unix, NT, NT Server, Potential/Backup/Master Browser, Domain Master, DC, Time, and others), Comment.

**Healthy / typical values.** On the author's PC nmbd is running, workgroup WORKGROUP, all master
options No, os level 0. The master browser is the router (a GL.iNet GL-BE9300), with type
"Workstation, Server, PrintQ, Unix, NT, NT Server, Master Browser, Domain Master".

**Unusual values and what they suggest.**
- "browse list not present - nmbd is not running": Samba's name service is off, so there is no list. Normal on PCs that do not share files.
- No master browser at all: Windows PCs may not see each other by name. Modern Windows uses WS-Discovery (Web Services Discovery) instead, so this matters less today.
- Two machines both claiming Master Browser: an election fight, often from two Samba boxes both set to "preferred master = yes".

**Where Linux gets it.** `testparm -s` for settings, `/var/cache/samba/browse.dat` (nmbd's cached
list). SRS sends no NetBIOS queries.

**Background.** *NetBIOS browsing* is the old Windows way to list computers. One machine per
workgroup wins an election and becomes the *master browser*; others ask it for the list.

---
## TCP/IP counters  
`counters` - Linux data

**In plain words.** Live traffic rates for every adapter, then the kernel's protocol counters for
IP, ICMP, TCP and UDP since boot.

**What you see.**

Rates table (refreshes every second in the window):

| Column | Meaning |
|---|---|
| Adapter | Interface name. |
| Rx /s, Tx /s | Bytes received / sent per second. |
| Rx pkt/s, Tx pkt/s | Packets per second. |
| Rx total, Tx total | Totals since the adapter came up. |
| State | up, down, unknown. |

The footer says how long the measuring window was ("rates over the last 0.50 s"). On first open in
the window it says "rates appear after the next refresh". In a saved report SRS takes a half-second
sample so the rates are real.

Then the `/proc/net/snmp` tables. Each has a heading (Ip, Icmp, IcmpMsg, Tcp, Udp, UdpLite) and one
counter per line. Zero counters are greyed. Key ones:
- **Ip**: Forwarding (1 = router mode on, 2 = off), DefaultTTL, InReceives, InHdrErrors, InDiscards, OutRequests, OutNoRoutes, fragment and reassembly counts.
- **Icmp / IcmpMsg**: ping and error messages in and out (InType0 = echo reply, InType3 = destination unreachable, InType8 = echo request).
- **Tcp**: ActiveOpens (connections you started), PassiveOpens (connections to your servers), AttemptFails, EstabResets, CurrEstab (open now), InSegs, OutSegs, RetransSegs (resent), InErrs, OutRsts.
- **Udp**: InDatagrams, NoPorts (sent to a port nobody listens on), InErrors, RcvbufErrors and SndbufErrors (buffer full).

**Healthy / typical values.** On the author's PC, idle: `em1` about 1.9 KB/s in and 1.2 KB/s out,
14.19 GB received since boot; `lo` about 1 KB/s each way. Tcp CurrEstab 19, RetransSegs 2,142 out of
3.8 million OutSegs (well under 1%). Udp RcvbufErrors 0.

**Unusual values and what they suggest.**
- RetransSegs above about 1-2% of OutSegs: packet loss somewhere (Wi-Fi interference, bad cable, overloaded router).
- InCsumErrors or InHdrErrors rising: damaged packets, often a bad cable or failing NIC.
- Udp RcvbufErrors rising: a program cannot read fast enough (game servers, streaming); the receive buffer is too small.
- Big AttemptFails compared with ActiveOpens: many connections refused or timed out, for example a blocked port or a dead server.
- Ip Forwarding 1 on a desktop: normal if you run Docker or VMs, otherwise check why.

**Where Linux gets it.** `/sys/class/net/<if>/statistics/` sampled twice, and `/proc/net/snmp`.

**Background.** *TCP* (Transmission Control Protocol) is the reliable, ordered connection used by
web, SSH and most apps; lost pieces are resent. *UDP* (User Datagram Protocol) just fires packets
with no resend, used by DNS, games and voice. *ICMP* (Internet Control Message Protocol) carries
ping and error replies.

---
## DNS cache  
`dns-cache` - Linux data, Needs root data

**In plain words.** Is there a local DNS cache on this PC, and what is in it.

**What you see.**
- One line per known caching program that is running, with its process ID: systemd-resolved, dnsmasq, nscd, unbound, named (BIND), kresd, dnscrypt-proxy, stubby, pdns_recursor, AdGuardHome. "none running" if none.
- If systemd-resolved runs: the cache contents from `resolvectl show-cache`. That command is root-only, so without srs-root you get "Needs root: Menu > Run as root (srs-root)".
- If systemd-resolved does not run: a note that glibc itself does not cache lookups, and that dnsmasq copies are often the ones libvirt or containers start for their own bridges.

**Healthy / typical values.** On the author's PC, `dnsmasq running (pid ...)` and systemd-resolved
not running. Here dnsmasq is a real system cache: `/etc/resolv.conf` points at 127.0.0.1, where
dnsmasq listens (see [DNS ports](#dns-ports)). SRS lists cache contents only for systemd-resolved,
so the dnsmasq cache itself is not shown.

**Unusual values and what they suggest.** Two different caches running at once (for example
systemd-resolved and dnsmasq both on port 53) can fight; one usually fails to start. No cache at all
is fine; lookups just go straight to your router or DNS provider each time.

**Where Linux gets it.** Process names in `/proc/*/comm`; `resolvectl show-cache` (root).

**Background.** A *DNS cache* remembers answers ("example.com is at ...") for their time-to-live,
so repeated lookups are instant and work for a while even if the upstream server is slow.

---
## DNS names  
`dns-names` - Linux data

**In plain words.** All the names this computer and your user account go by.

**What you see.**

| Row | Meaning |
|---|---|
| NetBIOS Name | Windows-network name (from Samba, or the host name in capitals, max 15 characters). |
| DNS Domain | The PC's DNS domain, often empty at home. |
| DNS Host Name | The host name. |
| Full DNS Name | Host name plus domain (the FQDN, fully qualified domain name). |

The two columns, Computer Name and Computer Physical Name, are the same on Linux (Windows can
differ on clusters).

Then the user block:
- **Display Username** - your login name.
- **Legacy Account Name** - `NETBIOSNAME\user`, the Windows-style form.
- **Full Name (GECOS)** - the real name stored in `/etc/passwd` (only if set).
- **User ID** - your numeric UID.

Then `/etc/hostname`, and from `hostnamectl`: StaticHostname, PrettyHostname, Chassis (desktop,
laptop, vm...), Deployment and Location, when set.

**Healthy / typical values.** On the author's PC: NetBIOS Name X3D, no DNS domain, Full DNS Name
X3D, user `user`, legacy name `X3D\user`, User ID 1000, Chassis desktop.

**Unusual values and what they suggest.** `/etc/hostname` different from DNS Host Name: the name
was changed without a reboot, or something (DHCP, a VM tool) set a temporary name. A host name
longer than 15 characters is cut short in the NetBIOS name, which can confuse Windows shares.

**Where Linux gets it.** `gethostname()`, `getdomainname()`, `/etc/resolv.conf`,
`testparm` (netbios name), `/etc/passwd`, `/etc/hostname`, `hostnamectl --json=short`.

---
## DNS ports  
`dns-ports` - Linux data

**In plain words.** Every socket on a DNS-related port: local DNS servers listening, and your
open connections to DNS servers.

**What you see.**

| Column | Meaning |
|---|---|
| Proto | UDP, UDP6, TCP, TCP6. |
| Address | The local address (for a listener) or the remote server address. |
| Port | 53 (DNS), 853 (DNS over TLS), 5353 (mDNS), 5355 (LLMNR, Link-Local Multicast Name Resolution). |
| Service | Name from `/etc/services` (domain, mdns...). |
| State | Listen, Local, or "To server" (an outgoing query). |
| Process (PID) | The program, if SRS can see it. Programs of other users (including root) show "-" unless you run srs-root. |

Below that: the DNS servers from `/etc/resolv.conf` (each on port 53), resolver `options`, and
whether systemd-resolved is running (its stub listens on 127.0.0.53).

**Healthy / typical values.** On the author's PC dnsmasq listens on 127.0.0.1:53 (UDP and TCP) and
on the libvirt and retro-VM bridge addresses; avahi listens on 0.0.0.0:5353 (mDNS). resolv.conf
lists 127.0.0.1 with `options timeout:2 attempts:3`.

**Unusual values and what they suggest.**
- Something listening on port 53 on your LAN address (not 127.x or a VM bridge): your PC is acting as a DNS server for the network. Fine if intended.
- resolv.conf points at 127.0.0.53 but systemd-resolved is "not running": DNS will fail.
- Nothing listening on 127.0.0.1:53 while resolv.conf says 127.0.0.1: your local cache died, so names stop resolving.

**Where Linux gets it.** `/proc/net/udp`, `udp6`, `tcp`, `tcp6`, process sockets under
`/proc/*/fd`, `/etc/resolv.conf`.

---
## DNS usage  
`dns-usage` - Linux data

**In plain words.** How name lookups are set up on this PC, or, with systemd-resolved, its lookup
statistics.

**What you see.** If systemd-resolved runs, the output of `resolvectl statistics`: transactions,
cache size, cache hits and misses, DNSSEC verdicts. Otherwise a note, then:
- **Name servers** - how many are in resolv.conf.
- **Search list** - domains tried after short names.
- **Resolver options** - resolv.conf options (timeout, attempts, edns0...).
- **NSS hosts order** - the `hosts:` line of `/etc/nsswitch.conf`, the order Linux tries: files (`/etc/hosts`), mdns, resolve, dns, myhostname, wins...
- **/etc/hosts entries** - number of non-comment lines.
- **/etc/gai.conf** - present, or absent (default IPv6-first rules).

**Healthy / typical values.** On the author's PC: 1 name server, no search list,
`timeout:2 attempts:3`, NSS order `mymachines mdns_minimal resolve files myhostname dns wins`,
9 hosts entries, gai.conf present.

**Unusual values and what they suggest.** A huge `/etc/hosts` (thousands of lines from an ad
blocker) slows every lookup because it is read top to bottom; a local DNS cache is faster for
that. With resolved statistics, a low cache-hit rate is normal for browsing many new sites; many
DNSSEC "bogus" verdicts suggest a broken upstream server or a clock that is far off.

**Where Linux gets it.** `resolvectl statistics`, `/etc/resolv.conf`, `/etc/nsswitch.conf`,
`/etc/hosts`, `/etc/gai.conf`.

**Background.** *NSS* (Name Service Switch) is glibc's plug-in list that decides where names are
looked up, and in what order.

---
## Domains / workgroups  
`domains` - Linux data

**In plain words.** Which Windows workgroup or domain, Kerberos realm and DNS domain this PC
belongs to, plus the workgroups seen on your LAN.

**What you see.**
- **SMB workgroup** - Samba's workgroup (WORKGROUP is the Windows default).
- **Kerberos realm** - for Active Directory joins.
- **Samba security** - USER (normal standalone), ADS (Active Directory member), etc.
- **NIS domain** - old Unix network login domain, usually "-".
- **DNS domain, DNS search list** - from resolv.conf.
- **AD/realmd join** - "keytab present" if `/etc/krb5.keytab` exists (joined to a domain), else "not joined".
- A table of domains/workgroups in nmbd's browse list: name, workgroup, type, comment (usually the master browser's name).

**Healthy / typical values.** On the author's PC: workgroup WORKGROUP, security USER, not joined;
the LAN list shows WORKGROUP with type "NT, Domain Enum", led by the router.

**Unusual values and what they suggest.** A workgroup name different from your other PCs means they
will not list each other in the old browse view. "keytab present" on a home PC is unusual and
means it was once joined to a company domain.

**Where Linux gets it.** `testparm` (workgroup, realm, security), `getdomainname()`,
`/etc/resolv.conf`, `/etc/krb5.keytab`, `/var/cache/samba/browse.dat`.

---
## Firewall  
`firewall` - Linux data, Needs root data

**In plain words.** Is the firewall running, and what each active zone allows in.

**What you see.**
- **firewalld state** - running, not running, or "not installed".
- **Panic mode** - "yes" means all traffic is being blocked.
- **Default zone** - the zone new interfaces fall into.
- One block per active zone (the heading says "(active)" and "(default)" where true). Each line is a zone setting that has a value: target (ACCEPT, default, %%REJECT%%, DROP), ingress/egress priority, icmp-block-inversion, interfaces, sources, services, ports, protocols, forward, masquerade, forward-ports, rich rules.
- **nftables ruleset** - the raw kernel rules need root (`nft list ruleset`), so this line points you to srs-root.

**Healthy / typical values.** On the author's PC firewalld is running, panic mode no, default zone
`public` (interfaces em1 and wlan0, services dhcpv6-client, kdeconnect, ssh). Extra zones exist for
Docker (target ACCEPT), libvirt (dhcp, dns, ssh, tftp), a retro-VM bridge (target %%REJECT%% with
only NFS (Network File System), Samba and AFP (Apple Filing Protocol) allowed) and a trusted zone for Waydroid.

**Unusual values and what they suggest.**
- Your LAN interfaces in zone `trusted` or a zone with target ACCEPT: everything is allowed in. Fine for VM bridges, risky for the adapter that faces the internet or a public Wi-Fi.
- Services open that you do not use (ssh, samba, cockpit): close them to reduce what others can reach.
- "not installed": you may use plain nftables, ufw, or no firewall at all. Check with root.
- Panic mode yes: someone ran `firewall-cmd --panic-on`; the network will look dead.

**Where Linux gets it.** `firewall-cmd --state`, `--query-panic`, `--list-all-zones` (read-only
queries); `nft list ruleset` needs root.

**Background.** *firewalld* groups rules into *zones*. Each network interface belongs to one zone,
and the zone decides which incoming services are allowed. *Masquerade* is NAT (network address
translation): sharing one address for many devices behind it.

---
## Network interface statistics  
`interface` - Linux data

**In plain words.** The kernel's error and traffic counters for every adapter since it came up.

**What you see.**

| Column | Meaning |
|---|---|
| Interface | Name. |
| Rx Bytes / Rx Packets | Received data and packets. |
| Rx Err | Received frames with errors (bad checksum, wrong length). |
| Rx Drop | Received but thrown away (no room in the queue, or unwanted traffic). |
| Tx Bytes / Tx Packets | Sent. |
| Tx Err / Tx Drop | Failed or dropped sends. |
| Multicast | Multicast packets received. |
| Collisions | Half-duplex collisions; should be 0 on modern full-duplex links. |
| Carrier chg | How many times the link went up or down. |

**Healthy / typical values.** On the author's PC `em1` shows 14.19 GB and 11.8 million packets in,
0 errors, 0 drops, Carrier chg 2 (link came up at boot). Bridges with no members show some Tx Drop
(711 on one bridge), which is harmless: packets sent to an empty bridge go nowhere.

**Unusual values and what they suggest.**
- Rx Err rising on a wired port: bad cable, bad connector, or electrical noise. Try another cable or port.
- Collisions above 0: the link fell back to half duplex; usually an auto-negotiation problem with an old switch.
- Carrier chg climbing while you watch: the link keeps dropping. Loose cable, power-saving (EEE, Energy-Efficient Ethernet), or a flaky driver.
- Rx Drop on a busy server: the receive ring is too small or the CPU is too slow to keep up.

**Where Linux gets it.** `/sys/class/net/<if>/statistics/*` and `carrier_changes` (the same numbers
as `/proc/net/dev` and `ip -s link`).

---
## IP address  
`ip-address` - Linux data

**In plain words.** Every IP address on every adapter, with its scope, flags and lifetime.

**What you see.**

| Column | Meaning |
|---|---|
| Adapter | Interface name ("(no addresses)" if it has none). |
| Address | IPv4 or IPv6 address. |
| Prefix | Prefix length, such as /24 or /64. |
| Scope | host (only this PC, like 127.0.0.1), link (only this cable/Wi-Fi, like fe80::), global (routable). |
| Flags | dynamic (from DHCP or SLAAC, stateless address autoconfiguration, where IPv6 builds its own address from router adverts), noprefixroute, temporary (privacy address), stable-privacy, tentative, deprecated, dadfailed, mngtmpaddr... "-" if none. |
| Lifetime (valid/preferred) | Seconds until the lease runs out (valid) and until it stops being used for new connections (preferred), or "forever". |

Open one adapter with `ip-address:<name>`.

**Healthy / typical values.** On the author's PC `em1` has 192.168.1.20 /24 global
`dynamic noprefixroute` with about 37,900 seconds left (a 12-hour DHCP lease), and a link-local
`fe80::XXXX /64` lasting forever. VM bridges have static addresses lasting forever.

**Unusual values and what they suggest.** A lifetime counting down to near 0 without renewing means
DHCP renewals are failing. `dadfailed` means address conflict. No global IPv6 address while your
provider supports IPv6: the router is not sending router advertisements, or IPv6 is off.

**Where Linux gets it.** `ip -o addr show`.

**Background.** A *DHCP lease* is an address the router lends you for a set time. Your PC renews it
halfway through. *Link-local* addresses (fe80:: for IPv6, 169.254. for IPv4) only work on the local
segment and are made automatically.

---
## LAN Manager share setup  
`lm-setup` - Linux data

**In plain words.** Your Samba (Windows file sharing) configuration, as Samba itself reads it.

**What you see.** The effective `smb.conf` from `testparm -s`, section by section:
- **[global]** - server-wide settings: workgroup, server string, security, interfaces and bind interfaces only, hosts allow/deny, min/max protocol, map to guest, logging, printing, usershare limits, wins support, idmap.
- **One section per share** ([homes], [printers], and your own shares) - path, comment, read only, guest ok, guest only, force user, valid users, create mask, directory mask, inherit acls, printable.

Only settings that differ from the defaults are printed. Values of any setting whose name contains
TOKEN, KEY, SECRET, PASS, AUTH, COOKIE or PSK are hidden as `****` (except plain Yes/No).

**Healthy / typical values.** On the author's PC: security USER, standalone server, minimum
protocol SMB2 for client and server, Samba bound only to `lo` and the home subnet, `hosts allow`
limited to the LAN and 127.0.0.1, `hosts deny = 0.0.0.0/0`. Shares include [homes], several
file shares and a printer.

**Unusual values and what they suggest.**
- `guest ok = Yes` with `read only = No`: anyone on the network can write there without a password. Only safe on a trusted LAN with `hosts allow` set.
- A share with `path = /`: the whole disk is shared. Limit with `valid users` or remove it if not needed.
- `server min protocol = NT1` (SMB1): very old and insecure protocol; only enable for retro machines that need it.
- "Samba is not installed or smb.conf is unreadable": no Samba, nothing shared this way.

**Where Linux gets it.** `testparm -s --suppress-prompt` (reads `/etc/samba/smb.conf`).

**Background.** *SMB* (Server Message Block) is the Windows file-sharing protocol; *Samba* is its
Linux implementation. "LAN Manager" is the old name from the 1980s Microsoft/IBM network software.

---
## MAC addresses  
`mac-address` - Linux data

**In plain words.** The hardware address of every adapter, whether it is the factory one, and which
company made the chip.

**What you see.**

| Column | Meaning |
|---|---|
| Adapter | Interface name. |
| MAC Address | The address in use now. |
| Permanent | The factory address burned into the card (physical adapters only, from `ethtool -P`), "-" for virtual ones. |
| Assigned | How the current address was set: permanent (factory), random (made up at start), stolen (copied from another device, as bonds do), set by user (changed by software or NetworkManager). |
| Broadcast | The link broadcast address (ff:ff:ff:ff:ff:ff on Ethernet and Wi-Fi). |
| Vendor (OUI) | Maker looked up from the first half of the address, or "(locally administered)" for made-up addresses. |

**Healthy / typical values.** On the author's PC the wired port is "permanent" with vendor
ASUSTek (the motherboard maker), the Wi-Fi card shows AzureWave Technology (the module maker), the
USB adapter shows Realtek, VMware adapters show VMware, and bridges, TAP, Docker and veth are
"(locally administered)" with "set by user". `lo` is all zeros.

**Unusual values and what they suggest.**
- Wi-Fi with Assigned "random" or "set by user" and a current address different from Permanent: MAC randomisation is on (a privacy feature). Normal, but your router will see a "new device" per network.
- Two adapters with the same MAC on one network: they will fight; usually a cloned VM.
- A vendor that does not match the card you bought: the card was rebranded, or the address was changed.

**Where Linux gets it.** `/sys/class/net/<if>/address`, `addr_assign_type`, `broadcast`;
`ethtool -P`; vendor from `/usr/share/hwdata/oui.txt`.

**Background.** A *MAC address* (Media Access Control) is the 6-byte hardware address used on the
local network. The first three bytes are the *OUI* (Organizationally Unique Identifier), bought by
the maker. If the second-lowest bit of the first byte is set, the address is *locally
administered*: made up by software, not by a factory.

---
## Modem API  
`modem-api` - Not present on most PCs

**In plain words.** On Windows this lists the telephony (TAPI) modem interface. Linux has no such
API; SRS shows what ModemManager knows instead. The page title reads "Modems" because it shares its
output with the next item.

**What you see.** Either the `mmcli -L` list of modems (mobile broadband sticks, built-in LTE/5G
modules), or "ModemManager reports no modems", or "ModemManager (mmcli) not available".

**Healthy / typical values.** On the author's PC: "ModemManager reports no modems". This is normal
for any desktop without a mobile data module.

**Where Linux gets it.** `mmcli -L` (ModemManager).

---
## Modems  
`modems` - Not present on most PCs

**In plain words.** Mobile broadband modems (4G/5G USB sticks, built-in WWAN (wireless wide area network) cards) known to
ModemManager.

**What you see.** Each modem line from `mmcli -L` (its path, maker and model), or "ModemManager
reports no modems" / "ModemManager (mmcli) not available". Old serial dial-up modems are not
listed; Linux has no general way to enumerate those.

**Healthy / typical values.** "ModemManager reports no modems" on a desktop. That just means there
is no mobile modem, not that anything is wrong.

**Unusual values and what they suggest.** A laptop with a WWAN card that shows no modem: the card
may be disabled in BIOS, rfkill-blocked, or need a firmware unlock (FCC lock) before ModemManager
will use it.

**Where Linux gets it.** `mmcli -L` (ModemManager).

---
## My browser / IP location  
`my-browser` - Linux data

**In plain words.** Which web browser opens your links. The Windows version also looks up your
public IP's location online; SRS does not, because it makes no internet requests.

**What you see.**
- **Default web browser** - from `xdg-settings` (a `.desktop` file name).
- **https handler** - the app registered for `https:` links (`xdg-mime`).
- **$BROWSER** - the BROWSER environment variable, used by many terminal programs.
- **IP geolocation** - always "not looked up - srs makes no internet requests".

**Healthy / typical values.** On the author's PC the default browser and https handler are the same
Floorp `.desktop` file, while `$BROWSER` is set to a different browser (firedragon).

**Unusual values and what they suggest.** Default browser and https handler disagree: some apps
open links in one browser and some in the other. Fix it in your desktop's Default Applications
settings. A `$BROWSER` that differs, as here, only affects programs that read that variable.

**Where Linux gets it.** `xdg-settings get default-web-browser`,
`xdg-mime query default x-scheme-handler/https`, the environment.

---
## My IP address  
`my-ip-address` - Linux data

**In plain words.** The address your PC uses when it talks to the outside world, and all your
routable addresses.

**What you see.**
- **Outgoing IPv4 source** - the address the kernel would use for a packet to the internet. SRS finds this with a route lookup for the documentation address 192.0.2.1; nothing is sent.
- **Default gateway** - your router address and the adapter it is reached through.
- **Public (WAN) address** - "not looked up - srs makes no internet requests". Your router's status page or any "what is my IP" site shows it. WAN (wide area network) means the internet side of your router.
- **Global-scope addresses** - every global address per adapter, with prefix and flags.

**Healthy / typical values.** On the author's PC: outgoing source 192.168.1.20 (the wired port),
gateway `192.168.1.1 dev em1`, and global addresses on em1, wlan0 and the VM/Docker bridges.

**Unusual values and what they suggest.** Outgoing source is the Wi-Fi address while a cable is
plugged in: the Wi-Fi route has the better metric, so traffic goes over the slower link. Outgoing
source on a VPN (virtual private network) interface (tun0, wg0): all traffic goes through the VPN, as intended for a
full-tunnel VPN. "-" means no route to the internet.

**Where Linux gets it.** `ip -o route get 192.0.2.1`, `/proc/net/route`, `ip -o addr show`.

**Background.** At home your PC has a *private* address (192.168.x.x, 10.x, 172.16-31.x). The router
swaps it for your one *public* address on the way out (NAT), so websites never see the private one.

---
## Network cards  
`net-cards` - Linux data

**In plain words.** The physical network hardware: chip name, bus slot, IDs, driver, firmware and
link details. Virtual adapters are left out (see [Network adapters](#network-adapters)).

**What you see.** One block per physical adapter:
- **Description** - PCI device name, or USB maker and product.
- **Bus** - `pci` with the slot address, or `usb` with the port path.
- **PCI ID** - vendor:device and subsystem vendor:device (PCI cards). The subsystem tells you the board maker.
- **PCIe link** - lane count and speed the card is running at now.
- **USB ID** - vendor:product and the USB link speed in Mbps (USB adapters).
- **Driver**, **Driver version**, **Firmware**, **Expansion ROM** (if the card has a boot ROM) - from `ethtool -i`.
- **MAC address**.
- **Link** - state; when up, also speed and duplex.
- **MTU** - with the transmit queue length.
- **Wireless PHY** - for Wi-Fi cards, the radio name (phy0...).

Open one card with `net-cards:<name>`.

**Healthy / typical values.** On the author's PC:
- `em1`: Realtek RTL8126 5GbE, PCI ID 10ec:8126, subsystem 1043 (ASUS), PCIe link x1 8.0 GT/s, driver r8169, firmware rtl8126a-3, link up 2.5 Gbps full duplex, MTU 1500.
- `wlan0`: Realtek RTL8922AE Wi-Fi 7, PCIe x1 5.0 GT/s, driver rtw89_8922ae, firmware 0.35.80.3, Wireless PHY phy0.
- `enp115s0u2u4u4`: a Realtek USB gigabit adapter (0bda:8153, driver r8152) on a 5000 Mbps USB port, link down.

**Unusual values and what they suggest.**
- A 5 GbE card linked at 2.5 Gbps: the switch or router port is only 2.5 GbE (as here), or the cable is Cat5. Not a fault.
- PCIe link slower than the card supports (for example 2.5 GT/s on a 10 GbE card): slot shares lanes or is set to an older PCIe generation in BIOS. Can cap throughput on fast cards.
- A USB adapter on a 480 Mbps (USB 2) port: a gigabit adapter will top out around 300 Mbps. Move it to a blue/USB 3 port.
- Half duplex on a modern link: auto-negotiation failed. Check the cable and switch.
- Firmware missing or very old: some features (and bug fixes) need the `linux-firmware` package updated.

**Where Linux gets it.** `/sys/class/net/<if>/device/` (vendor, device, subsystem IDs,
current_link_width, current_link_speed), `lspci -nn`, `ethtool -i`.

**Background.** *Duplex* is whether both ends can send at the same time (full) or must take turns
(half). *MTU* (Maximum Transmission Unit) is the biggest packet the link carries, 1500 bytes on
normal Ethernet.

---
## Network provider names  
`net-names` - Windows-only (Linux equivalent shown)

**In plain words.** On Windows this lists "network providers" (the plug-ins that let Explorer open
`\\server\share` paths). Linux has no such list, so SRS shows the network file-system tools
installed and the network shares mounted right now.

**What you see.**
- **Network providers available** - which client tools exist: Samba client (smbclient), CIFS kernel mounts (mount.cifs), NFS client, SSHFS, GVfs SMB, KIO (KDE's smb:/ sftp:/ fish:/), CUPS network printing (lpstat). Each with its path.
- **Mounted network file systems** - source, mount point, and type for every cifs, smb3, nfs, nfs4, fuse.sshfs, 9p, davfs, fuse.rclone, fuse.gvfsd-fuse, ceph or glusterfs mount. Any user:password in a source is masked.

**Healthy / typical values.** On the author's PC all seven tools are present, and three network
mounts are active: an NFS4 share, GVfs's FUSE mount at `/run/user/1000/gvfs`, and a CIFS mount of
the router's USB disk.

**Unusual values and what they suggest.** A network mount you did not expect: something (fstab,
a file manager, an automounter) mounted it. A mount whose server is gone can make file managers and
`df` hang. "none mounted" is normal.

**Where Linux gets it.** Tool paths on disk; `/proc/self/mounts`.

---
## Network  
`network` - Linux data

**In plain words.** The one-page summary of your networking: host name, adapters, gateway, DNS,
forwarding and TCP tuning, then one line per adapter.

**What you see.**
- **Host name**.
- **Adapters** - total, how many physical and virtual, and how many are up (state "up" or "unknown"; loopback and some virtual adapters always report "unknown").
- **IPv4 default gateway** - router address and adapter.
- **IPv6 default route** - or "none".
- **DNS servers** - from `/etc/resolv.conf`.
- **DNS search** - if set.
- **IP forwarding** - on/off for IPv4 and IPv6.
- **TCP congestion control** - the algorithm that decides how fast TCP sends (cubic, bbr, bbr3...).
- **Default qdisc** - the default queueing discipline (fq, fq_codel, pfifo_fast...).

Then a table: Adapter, Type, State, Speed, MAC Address, IPv4 (all addresses with prefix), Driver.
The last line names the motherboard maker, model, BIOS version and BIOS date (for example
"ASUSTeK COMPUTER INC. TUF GAMING X870-PRO WIFI7 W NEO BIOS 1805").

**Healthy / typical values.** On the author's PC: host X3D, "11 (3 physical, 8 virtual), 7 up",
gateway `192.168.1.1 dev em1`, no IPv6 default route, DNS 127.0.0.1 (a local dnsmasq), IPv4
forwarding on (needed by Docker and VMs), TCP congestion control `bbr3`, default qdisc `fq`.

**Unusual values and what they suggest.**
- IPv4 default gateway "none": no internet route. Check the cable/Wi-Fi and DHCP.
- IPv6 default route "none" while your provider offers IPv6: router advertisements are not arriving or IPv6 is disabled on the router.
- DNS servers "none": names will not resolve at all.
- BBR with a qdisc other than fq on older kernels: BBR worked best with fq pacing; modern kernels pace internally, so this matters less now.

**Where Linux gets it.** `/sys/class/net`, `ip -o addr show`, `/proc/net/route`,
`ip -o -6 route show default`, `/etc/resolv.conf`, `/proc/sys/net/ipv4/ip_forward`,
`/proc/sys/net/ipv6/conf/all/forwarding`, `/proc/sys/net/ipv4/tcp_congestion_control`,
`/proc/sys/net/core/default_qdisc`.

**Background.** *Congestion control* is how TCP guesses the safe sending speed. Classic algorithms
(cubic) slow down when packets are lost; *BBR* measures bandwidth and delay instead and often does
better on lossy or long links. A *qdisc* (queueing discipline) is the kernel's waiting line for
outgoing packets; `fq` (fair queue) shares the link fairly between connections.

---
## Network profiles  
`profiles` - Linux data

**In plain words.** Every connection NetworkManager has saved: wired, Wi-Fi, VPN, bridges,
Bluetooth tethering.

**What you see.**

| Column | Meaning |
|---|---|
| Profile | The connection name. For Wi-Fi this is usually the network name. |
| Type | 802-3-ethernet (wired), 802-11-wireless (Wi-Fi), vpn, wireguard, bridge, tun, loopback, bluetooth, gsm... |
| Device | The adapter using it now, or "-" if not active. |
| Auto | Does it connect automatically. |
| Last used | When it was last activated, or "never". |
| UUID | The profile's unique ID, used by `nmcli connection up <uuid>`. |

**Healthy / typical values.** On the author's PC: "Wired connection 3" on em1, the home Wi-Fi
profile on wlan0, profiles for lo and two bridges and a TAP device, plus inactive ones: older wired
profiles, two other Wi-Fi networks, a VPN profile, and a Bluetooth phone-tethering profile.

**Unusual values and what they suggest.** Several "Wired connection N" profiles for one port:
NetworkManager made a new one each time the adapter or its MAC changed; the old ones can be
deleted. A Wi-Fi profile with Auto yes for a public network you no longer visit: your PC will join
it whenever it sees that name. Remove it.

**Where Linux gets it.** `nmcli -t connection show`.

---
## Network protocols  
`protocols` - Linux data

**In plain words.** The socket types the kernel has loaded right now, how many sockets of each are
open, and how much memory they use.

**What you see.**

| Column | Meaning |
|---|---|
| Protocol | TCP, UDP, TCPv6, UDPv6, UNIX, UNIX-STREAM, NETLINK, PACKET, RAW, PING, MPTCP, XDP, MCTP, Bluetooth ones (HCI, L2CAP, SCO, RFCOMM, BNEP), ALG (kernel crypto), and others. |
| Size | Bytes per socket structure in the kernel. |
| Sockets | Number open now. |
| Memory | Memory pages used by this protocol's buffers (-1 = not tracked). |
| Press | Memory pressure: "no" (fine), "yes" (the kernel is limiting buffers), "NI" (not implemented for this protocol). |
| Module | Kernel module that provides it ("kernel" if built in). |

A footer counts the protocols and the IP protocol numbers defined in `/etc/protocols`.

**Healthy / typical values.** On the author's PC 22 protocols: about 1,500 UNIX-STREAM sockets
(normal for a desktop; apps talk to each other this way), 50 TCP, 53 UDP, 88 NETLINK, and Press
"no" for TCP.

**Unusual values and what they suggest.** Press "yes" for TCP: the system is short of socket memory,
often a server with very many connections; raise `net.ipv4.tcp_mem`. Thousands of TCP sockets on a
desktop: a program is leaking connections.

**Where Linux gets it.** `/proc/net/protocols`; `/etc/protocols`.

**Background.** A *socket* is a program's handle for one network conversation (or local channel).
*Netlink* sockets are how tools like `ip` talk to the kernel; *UNIX* sockets are local-only pipes
between programs.

---
## Proxy config  
`proxy` - Linux data

**In plain words.** Whether apps are told to send web traffic through a proxy server, and where
that setting comes from.

**What you see.** Three or four blocks:
- **Environment (this process)** - http_proxy, https_proxy, ftp_proxy, all_proxy, no_proxy (both lower and upper case), or "none set (direct connection)".
- **/etc/environment** - any proxy lines there, or "none".
- **KDE (kioslaverc [Proxy Settings])** - ProxyType (No proxy, Manual, PAC script, WPAD auto-detect, System environment) and the other keys in that section, such as httpProxy and NoProxyFor. Secret-looking keys are hidden.
- **GNOME proxy mode** - from gsettings, if available ('none', 'manual', 'auto').

Any `user:password@` in a proxy URL is shown as `****`.

**Healthy / typical values.** On the author's PC: no environment proxy, none in /etc/environment,
KDE ProxyType "No proxy" (an old httpProxy value of `http://127.0.0.1:9099` is still stored but
unused), GNOME mode 'none'.

**Unusual values and what they suggest.** A proxy set that you did not configure: some VPN or
"privacy" tools, and some malware, set one to route your traffic. A proxy in KDE but not in the
environment means KDE apps use it while terminal tools and many other apps do not.

**Where Linux gets it.** The process environment, `/etc/environment`, `~/.config/kioslaverc`,
`gsettings get org.gnome.system.proxy mode`.

**Background.** A *proxy* is a middle server that makes web requests for you. A *PAC* (proxy
auto-config) script picks the proxy per address; *WPAD* finds that script automatically.

---
## Routing table  
`routing` - Linux data

**In plain words.** The kernel's map of "to reach this network, go this way".

**What you see.** First, whether IPv4 routing (forwarding between adapters) is Enabled or Disabled.
Then the IPv4 table:

| Column | Meaning |
|---|---|
| Destination | Network address, or "default" for everything else. |
| Netmask | Which part is the network (255.255.255.0 = /24). |
| Gateway | Next router to hand packets to, or "on-link" (the destination is directly on that adapter). |
| Flags | U = route is up, G = uses a gateway, H = route to a single host, ! = reject route. |
| Metric | Cost; when two routes match, the lower metric wins. |
| MTU | Per-route MTU, "-" if not set. |
| Adapter | Interface used. |

Then **IPv6 routes**: each line of `ip -6 route show table all` as text. Normal routes are coloured;
`local` and `multicast` housekeeping routes are greyed.

**Healthy / typical values.** On the author's PC: two default routes via 192.168.1.1, one on em1
with metric 100 and one on wlan0 with metric 600, so the wired port wins. On-link routes for the
home subnet and each VM/Docker bridge. IPv6 shows only link-local `fe80::/64` routes (no IPv6
internet).

**Unusual values and what they suggest.**
- No "default" line: no internet access, only local networks.
- Two default routes with the same metric: traffic may flip between adapters.
- A default route on a VPN adapter (tun0, wg0): all traffic goes through the VPN.
- Two routes for the same subnet on different adapters (for example a VM bridge reusing your home subnet): local traffic can go to the wrong place.

**Where Linux gets it.** `/proc/net/route`, `/proc/sys/net/ipv4/ip_forward`,
`ip -o -6 route show table all`.

**Background.** A *gateway* is the router that forwards your packets to other networks. A *metric*
is a priority number: NetworkManager gives wired links 100 and Wi-Fi 600 so cables are preferred.

---
## Servers  
`servers` - Linux data

**In plain words.** Every service on this PC that is waiting for incoming connections, and the SMB
(Windows sharing) servers seen on your LAN.

**What you see.**

Local listening services:

| Column | Meaning |
|---|---|
| Proto | TCP, TCP6, UDP, UDP6. |
| Bound Address | 0.0.0.0 or `::` = all addresses (reachable from the network, unless the firewall blocks it); 127.0.0.1 or `::1` = this PC only (greyed); a specific address = only that adapter. |
| Port | Port number. |
| Service | Standard name from `/etc/services`, blank if none. |
| Process (PID) | The program, if visible. Other users' programs show "-" unless you run srs-root. |

SMB servers seen on the LAN: Name, Workgroup, Type and Comment from nmbd's browse list (see
[Network browse masters](#network-browse-masters)).

**Healthy / typical values.** On the author's PC, a fairly busy home server: ssh (22), dnsmasq (53),
rpcbind (111), Samba (139, 445), AFP (548) for retro Macs, CUPS printing (631), NFS (2049, mountd
20048), WS-Discovery (5357), syncthing on 127.0.0.1:8384, and a few local helpers. The router shows
up as an SMB server.

**Unusual values and what they suggest.**
- A service on 0.0.0.0 you did not know about: it is reachable from your network. Check what it is and whether the firewall blocks it.
- A database or admin panel on 0.0.0.0 instead of 127.0.0.1: often a misconfiguration.
- Note that `/etc/services` names are just the registered meaning of the number, not the real program. Port 2049 shows as "shilp" but is NFS, and 8080 is labelled "http-alt" whatever runs there.

**Where Linux gets it.** `/proc/net/tcp`, `tcp6`, `udp`, `udp6` (listening sockets), `/proc/*/fd`
for owners, `/var/cache/samba/browse.dat`.

**Background.** A *port* is a number (0-65535) that picks which program on a machine gets a packet.
A *listening* socket is a program waiting for others to connect.

---
## TCP V4 ports  
`tcp-ip4-ports` - Linux data

**In plain words.** Every IPv4 TCP socket: servers waiting for connections and every open
connection, with the program that owns it.

**What you see.**

| Column | Meaning |
|---|---|
| Local Address / Port / Service | Your side of the socket. |
| Remote Address / Port / Service | The other side; `0.0.0.0 *` for a listener. |
| State | Listen, Established, SYN Sent, SYN Received, FIN Wait 1, FIN Wait 2, Time Wait, Closed, Close Wait, Last ACK, Closing, New SYN Recv. |
| User | Account that owns the socket. |
| Process (PID) | Program and process ID; "-" for other users' programs without root; "(kernel/time-wait)" for sockets no program owns any more. |

Listeners are listed first, sorted by port. The footer counts sockets, listening and other.

**Healthy / typical values.** On the author's PC 51 sockets: 38 listening, 13 other. For example
`127.0.0.1 8384 ... Listen user syncthing (18116)` and outgoing `https` (443) connections in state
Established, plus a few in Time Wait.

**Unusual values and what they suggest.**
- Many Close Wait sockets for one program: that program is not closing connections (a bug); it will slowly run out of sockets.
- Hundreds of Time Wait: normal on a busy web client or server; they clear after about a minute.
- Many SYN Received from different addresses: someone is flooding your server (SYN flood).
- An Established connection to an address you do not recognise, owned by an unknown program: worth looking up.

**Where Linux gets it.** `/proc/net/tcp`; owners from `/proc/<pid>/fd` socket links.

**Background.** TCP connections go through *states*: a client sends SYN, the server answers, and the
connection is Established. Closing passes through FIN Wait / Close Wait, and the side that closes
first waits in Time Wait so late packets are not mistaken for a new connection.

---
## TCP V6 ports  
`tcp-ip6-ports` - Linux data

**In plain words.** The same as [TCP V4 ports](#tcp-v4-ports) but for IPv6 TCP sockets.

**What you see.** The same columns and states as TCP V4 ports, with IPv6 addresses. `::` means "all
addresses", `::1` is this PC only. Addresses like `::ffff:192.168.1.20` are IPv4 connections
accepted by an IPv6 socket (IPv4-mapped addresses).

**Healthy / typical values.** On the author's PC 30 sockets: 23 listening, 7 other. For example
KDE Connect (`kdeconnectd`) listening on `:: 1716` and connected to a phone over an IPv4-mapped
address, syncthing on 22000, and CoolerControl talking to its daemon on `::1`.

**Unusual values and what they suggest.** As for TCP V4. Also, a service listening only on `::`
may still accept IPv4 through mapped addresses, so it can be more reachable than it looks.

**Where Linux gets it.** `/proc/net/tcp6`; owners from `/proc/<pid>/fd`.

---
## Network transports  
`transports` - Linux data

**In plain words.** A count of sockets in use per transport type, and which address families have
socket tables. Windows lists NetBIOS transport bindings here; Linux has none.

**What you see.**
- From `/proc/net/sockstat` and `sockstat6`: **sockets used**, **TCP** (inuse, orphan, tw = time-wait, alloc, mem), **UDP**, **UDPLITE**, **RAW**, **FRAG** (packets being reassembled), and the IPv6 versions.
- **Address families with registered sockets** - which of unix, packet, netlink, raw, tcp, udp, icmp, sctp, bluetooth, can, vsock, xdp and others have a table in `/proc/net`.
- A note that SMB/NetBIOS on Linux simply runs over TCP 139/445 and UDP 137/138.

**Healthy / typical values.** On the author's PC: 1,937 sockets used, TCP inuse 50, orphan 0, tw 1,
UDP inuse 53, FRAG inuse 0.

**Unusual values and what they suggest.** A large TCP "orphan" count: connections closed by programs
but not yet finished in the kernel; many orphans can mean a DoS (denial-of-service) attack or a
buggy server. TCP mem near the limit: see Press in [Network protocols](#network-protocols).

**Where Linux gets it.** `/proc/net/sockstat`, `/proc/net/sockstat6`, the `/proc/net` directory.

---
## UDP V4 ports  
`udp-ip4-ports` - Linux data

**In plain words.** Every IPv4 UDP socket, with its owner.

**What you see.** The same columns as [TCP V4 ports](#tcp-v4-ports). UDP has no real connection, so
State is "Listen" (bound and waiting) or "Connected" (fixed to one remote address).

**Healthy / typical values.** On the author's PC 53 sockets, 51 listening: dnsmasq (53), DHCP
server (67, bootps) for the VM bridges, rpcbind (111), Samba's NetBIOS name and datagram services
(137, 138) on each address including broadcast addresses, WS-Discovery (3702) on each address plus
the multicast group 239.255.255.250, avahi mDNS (5353), and syncthing discovery (21027).

**Unusual values and what they suggest.** A program listening on 0.0.0.0 with a port you do not
recognise. Games and voice apps open random high ports while running; that is normal. Port 1900
(SSDP) or 5353 bound by many apps is also normal.

**Where Linux gets it.** `/proc/net/udp`; owners from `/proc/<pid>/fd`.

---
## UDP V6 ports  
`udp-ip6-ports` - Linux data

**In plain words.** The same as [UDP V4 ports](#udp-v4-ports) but for IPv6 UDP sockets.

**What you see.** The same columns. Addresses starting `ff02::` are IPv6 multicast groups (for
example `ff02::c` for WS-Discovery); `fe80::` addresses are link-local, one per adapter.

**Healthy / typical values.** On the author's PC 29 sockets, all listening: wsdd on each adapter's
link-local address and `ff02::c`, avahi on `:: 5353`, KDE Connect on 1716, syncthing on 21027 and
22000.

**Unusual values and what they suggest.** As for UDP V4.

**Where Linux gets it.** `/proc/net/udp6`; owners from `/proc/<pid>/fd`.

---
## Wi-Fi SSIDs  
`wi-fi` - Linux data

**In plain words.** Your Wi-Fi card's current connection (channel, signal, speed), and the
networks NetworkManager last saw. SRS never starts a new scan.

**What you see.** Per wireless adapter, from `iw dev <if> info`:
- **addr** - its MAC address. **ssid** - the network it is on. **type** - managed (normal client), AP, monitor... **wiphy** - radio number. **channel** - channel number, frequency, width. **txpower** - transmit power.

Then from `iw dev <if> link`:
- **Connected to** - the access point's address, or **Link: Not connected**.
- **SSID**, **freq** - network and frequency in MHz.
- **RX / TX** - bytes and packets on this link.
- **signal** - received strength in dBm.
- **rx bitrate / tx bitrate** - the current data rate and how it is built: width, standard (HT = Wi-Fi 4, VHT = Wi-Fi 5, HE = Wi-Fi 6, EHT = Wi-Fi 7), MCS (coding level), NSS (number of spatial streams), GI (guard interval).
- **bss flags, dtim period, beacon int** - access point timing details.
- On Wi-Fi 7 multi-link connections, extra Link and MLD lines per link.

Then **/proc/net/wireless** - the old wireless-extensions quality line (many modern drivers leave
it empty, giving "no associated interfaces").

Then **Visible networks** from NetworkManager's cache:

| Column | Meaning |
|---|---|
| (first) | `*` = the one you are on. |
| SSID | Network name, "(hidden)" if it does not broadcast one. |
| BSSID | The access point's radio address. |
| Chan, Band | Channel and band: 2.4 GHz, 5 GHz or 6 GHz. |
| Rate | Max rate NetworkManager reports. |
| Signal, Bars | Strength in percent (green 60% and up, orange 30-59%, red below 30%) and as bars. |
| Security | WPA2, WPA3, WPA1, "open"... |

**Healthy / typical values.** On the author's PC `wlan0` (Wi-Fi 7 card) is on channel 36, 5180 MHz,
160 MHz wide, txpower 20 dBm, signal -28 dBm (excellent; the router is close), tx bitrate 2882.3
MBit/s using EHT-MCS 13 with 2 streams, security WPA3, 92%. The rx bitrate was only 6 MBit/s
because the link was idle at that moment; it rises with traffic.

**Unusual values and what they suggest.**
- Signal weaker than about -70 dBm: slow and unstable. Move closer, or use 5 GHz only where coverage is good.
- Connected on 2.4 GHz with a 5/6 GHz-capable router nearby: slower; the bands may share one name and the PC chose the far-reaching one.
- 20 MHz channel width on 5 GHz: the router or regulatory setting is limiting width.
- Security "open" or WPA1 on your own network: change it to WPA2 or WPA3.
- "NetworkManager has no cached scan results": no scan has happened yet; open your Wi-Fi menu to trigger one.

**Where Linux gets it.** `iw dev <if> info`, `iw dev <if> link`, `/proc/net/wireless`,
`nmcli device wifi list --rescan no`.

**Background.** *SSID* (Service Set Identifier) is the network name; *BSSID* is the radio address of
one access point. *dBm* is signal power on a log scale: -30 is excellent, -67 is good for video
calls, -80 is barely usable. *MCS* (Modulation and Coding Scheme) is the encoding step; higher
means faster but needs a cleaner signal.

---
## WLAN profiles  
`wlan` - Linux data, Needs root data

**In plain words.** The saved Wi-Fi networks only, with their name, mode and type of security.
Passwords are never shown.

**What you see.** The same columns as [Network profiles](#network-profiles), but the last column is
**SSID / Security / IPv4**: the network name, then in brackets the mode (infrastructure = normal
router, adhoc, ap = hotspot), the key management (none/open, wpa-psk = WPA2 password, sae = WPA3
password, wpa-eap = enterprise login, owe = enhanced open), and the IPv4 method (auto = DHCP,
manual, shared, disabled).

The footer says keys are never shown: reading them needs root or a secret agent, and SRS does not
ask for them.

**Healthy / typical values.** On the author's PC three saved Wi-Fi networks: the home network
(`[infrastructure, sae, ipv4 auto]`, WPA3), a university profile (`wpa-eap`, enterprise login),
and one more WPA3 network last used weeks ago.

**Unusual values and what they suggest.** A saved network with security "open" and Auto yes: your
PC will join any network that copies that name, which is how fake hotspots work. Profiles from long
ago can be deleted. `wpa-psk` (WPA2) on your own router is fine, but switch to `sae` (WPA3) if all
your devices support it.

**Where Linux gets it.** `nmcli connection show` and
`nmcli connection show <uuid>` (ssid, mode, key-mgmt, ipv4.method). Full profiles, including
secrets, live in `/etc/NetworkManager/system-connections`, which is root-only.

**Background.** *WPA2* and *WPA3* are Wi-Fi security standards. WPA3's *SAE* (Simultaneous
Authentication of Equals) protects the password even if someone records your traffic.
