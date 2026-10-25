/* sec_network.c - SIV network sections on Linux:
 * [network] [adapters] [ip-address] [mac-address] [net-cards] [interface] [counters] [arp-table]
 * [routing] [tcp-ip4-ports] [tcp-ip6-ports] [udp-ip4-ports] [udp-ip6-ports] [protocols] [transports]
 * [dns-names] [dns-ports] [dns-usage] [dns-cache] [proxy] [firewall] [profiles] [wi-fi] [wlan]
 * [bluetooth] [bluetooth-ids] [blue-tree] [nodes] [browsers] [domains] [servers] [lm-setup]
 * [my-ip-address] [my-browser] [net-names] [modems] [modem-api]
 * Sources: /sys/class/net, /proc/net, `ip -o addr/route/neigh`, nmcli, iw (no scans), bluetoothctl
 * (list/show/devices/info only), firewall-cmd (read-only queries), testparm, /var/cache/samba/browse.dat,
 * avahi-browse (cache dump only). Everything is local: no internet requests, no Wi-Fi/Bluetooth scans,
 * nothing is written. ([ping] belongs to the tools group.) */
#define _GNU_SOURCE
#include "sec.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SCN "/sys/class/net"
#define MAXNIC 128
#define MAXADDR 512

/* ======================================================================
 * helpers
 * ====================================================================== */

/* run a local command with a hard timeout and stderr discarded; returns malloc'd stdout or NULL */
static char *cmd(const char *const *argv, int *rc)
{
    const char *v[48];
    char *out = NULL;
    int n = 0, i, r;
    v[n++] = "sh";
    v[n++] = "-c";
    v[n++] = "exec timeout -k 1 4 \"$@\" 2>/dev/null </dev/null";
    v[n++] = "sh";
    for (i = 0; argv[i] && n < 46; i++) v[n++] = argv[i];
    v[n] = NULL;
    r = run_out(&out, v);
    if (rc) *rc = r;
    if (r < 0 || !out) { free(out); return NULL; }
    return out;
}
#define CMD(...) cmd((const char *const[]){ __VA_ARGS__, NULL }, NULL)

static void strip_ansi(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == 0x1b) {
            r++;
            if (*r == '[') { r++; while (*r && !isalpha((unsigned char)*r)) r++; if (*r) r++; }
            continue;
        }
        if (*r == '\r' || *r == 1 || *r == 2) { r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
}

static void trim(char *s)
{
    size_t l = strlen(s);
    char *p = s;
    while (l && isspace((unsigned char)s[l - 1])) s[--l] = 0;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static int secret_name(const char *k)
{
    static const char *bad[] = { "TOKEN", "KEY", "SECRET", "PASS", "AUTH", "COOKIE", "PSK", NULL };
    char u[256];
    int i;
    for (i = 0; k[i] && i < 255; i++) u[i] = (char)toupper((unsigned char)k[i]);
    u[i] = 0;
    for (i = 0; bad[i]; i++) if (strstr(u, bad[i])) return 1;
    return 0;
}

/* proxy URLs may embed "user:pass@" - mask it */
static void mask_userinfo(const char *in, char *out, size_t sz)
{
    const char *at = strchr(in, '@'), *sch = strstr(in, "://");
    if (at && sch && at > sch) snprintf(out, sz, "%.*s****%s", (int)(sch + 3 - in), in, at);
    else snprintf(out, sz, "%s", in);
}

/* nmcli -t field splitter: ':' separates, "\:" and "\\" are escapes */
static int nm_split(char *line, char **f, int max)
{
    int n = 0;
    char *r = line, *w = line;
    if (max < 1) return 0;
    f[n++] = w;
    while (*r) {
        if (*r == '\\' && r[1]) { *w++ = r[1]; r += 2; continue; }
        if (*r == ':' && n < max) { *w++ = 0; r++; f[n++] = w; continue; }
        *w++ = *r++;
    }
    *w = 0;
    return n;
}

static void fmt_speed(long mbps, char *b, size_t sz)
{
    if (mbps <= 0) snprintf(b, sz, "-");
    else if (mbps >= 1000) snprintf(b, sz, "%.1f Gbps", mbps / 1000.0);
    else snprintf(b, sz, "%ld Mbps", mbps);
}

static void fmt_count(unsigned long long v, char *b, size_t sz)
{
    char t[40], *w = b;
    int l, i;
    snprintf(t, sizeof t, "%llu", v);
    l = (int)strlen(t);
    for (i = 0; i < l && (size_t)(w - b) < sz - 2; i++) {
        *w++ = t[i];
        if ((l - i - 1) % 3 == 0 && i < l - 1) *w++ = ',';
    }
    *w = 0;
}

static int arg_match(srs_out *o, const char *name) { return !o->arg || !*o->arg || !strcmp(o->arg, name); }

/* ======================================================================
 * interfaces (physical first, then virtual, loopback last)
 * ====================================================================== */
typedef struct { char name[64]; int rank, idx; } nic;

static int nic_cmp(const void *a, const void *b)
{
    const nic *x = a, *y = b;
    if (x->rank != y->rank) return x->rank - y->rank;
    return x->idx - y->idx;
}

static int nic_is_phys(const char *n)
{
    char p[300];
    snprintf(p, sizeof p, SCN "/%s/device", n);
    return path_exists(p);
}

static int nic_list(nic *v, int max)
{
    char **names, p[300];
    int n = dir_list(SCN, &names), i, k = 0;
    long t;
    if (n < 0) return 0;
    for (i = 0; i < n && k < max; i++) {
        if (names[i][0] == '.') continue;
        snprintf(p, sizeof p, SCN "/%s/ifindex", names[i]);
        if (!path_exists(p)) continue;  /* bonding_masters etc. */
        snprintf(v[k].name, sizeof v[k].name, "%s", names[i]);
        v[k].idx = rd_long(p, &t) ? (int)t : 9999;
        snprintf(p, sizeof p, SCN "/%s/type", names[i]);
        if (rd_long(p, &t) && t == 772) v[k].rank = 2;
        else v[k].rank = nic_is_phys(names[i]) ? 0 : 1;
        k++;
    }
    dir_free(names, n);
    qsort(v, (size_t)k, sizeof *v, nic_cmp);
    return k;
}

static long nic_long(const char *n, const char *attr)
{
    char p[300];
    long v = -1;
    snprintf(p, sizeof p, SCN "/%s/%s", n, attr);
    if (!rd_long(p, &v)) return -1;
    return v;
}

static void nic_str(const char *n, const char *attr, char *b, size_t sz)
{
    char p[300];
    snprintf(p, sizeof p, SCN "/%s/%s", n, attr);
    if (!rd_str(p, b, sz)) snprintf(b, sz, "-");
}

static unsigned long long nic_stat(const char *n, const char *st)
{
    char p[300], b[64];
    snprintf(p, sizeof p, SCN "/%s/statistics/%s", n, st);
    if (!rd_str(p, b, sizeof b)) return 0;
    return strtoull(b, NULL, 10);
}

static void link_base(const char *path, char *b, size_t sz)
{
    char t[1024];
    ssize_t l = readlink(path, t, sizeof t - 1);
    const char *s;
    if (l <= 0) { snprintf(b, sz, "-"); return; }
    t[l] = 0;
    s = strrchr(t, '/');
    snprintf(b, sz, "%s", s ? s + 1 : t);
}

static void nic_driver(const char *n, char *b, size_t sz)
{
    char p[300];
    snprintf(p, sizeof p, SCN "/%s/device/driver", n);
    if (path_exists(p)) { link_base(p, b, sz); return; }
    if (!strncmp(n, "veth", 4)) snprintf(b, sz, "veth");
    else {
        snprintf(p, sizeof p, SCN "/%s/bridge", n);
        if (path_exists(p)) { snprintf(b, sz, "bridge"); return; }
        snprintf(p, sizeof p, SCN "/%s/tun_flags", n);
        if (path_exists(p)) { snprintf(b, sz, "tun"); return; }
        snprintf(b, sz, "-");
        {
            char *e = CMD("ethtool", "-i", n), *d;
            if (e && (d = strstr(e, "driver: ")) && d[8] != '\n') { snprintf(b, sz, "%.*s", (int)strcspn(d + 8, "\n"), d + 8); }
            free(e);
        }
    }
}

static void nic_type(const char *n, char *b, size_t sz)
{
    char p[300], ue[1024], *d;
    long t = nic_long(n, "type"), tf;
    snprintf(p, sizeof p, SCN "/%s/wireless", n);
    if (path_exists(p)) { snprintf(b, sz, "Wi-Fi"); return; }
    if (t == 772) { snprintf(b, sz, "Loopback"); return; }
    snprintf(p, sizeof p, SCN "/%s/bridge", n);
    if (path_exists(p)) { snprintf(b, sz, "Bridge"); return; }
    tf = nic_long(n, "tun_flags");
    if (tf >= 0) { snprintf(b, sz, (tf & 0x0002) ? "TAP" : "TUN"); return; }
    if (!strncmp(n, "veth", 4)) { snprintf(b, sz, "veth"); return; }
    snprintf(p, sizeof p, SCN "/%s/uevent", n);
    {
        char *all;
        if (rd_all(p, &all) >= 0) {
            d = strstr(all, "DEVTYPE=");
            if (d) {
                snprintf(ue, sizeof ue, "%s", d + 8);
                ue[strcspn(ue, "\n")] = 0;
                if (*ue) ue[0] = (char)toupper((unsigned char)ue[0]);
                snprintf(b, sz, "%s", ue);
                free(all);
                return;
            }
            free(all);
        }
    }
    if (t == 1) snprintf(b, sz, "Ethernet");
    else if (t == 65534) snprintf(b, sz, "None (L3)");
    else if (t == 512) snprintf(b, sz, "PPP");
    else if (t == 776) snprintf(b, sz, "IPv6-in-IPv4");
    else snprintf(b, sz, "ARPHRD %ld", t);
}

/* PCI slot or USB interface of a physical NIC (or Bluetooth radio) */
static void dev_subsys(const char *devpath, char *sub, size_t ssz, char *slot, size_t slsz)
{
    char p[400];
    snprintf(p, sizeof p, "%s/subsystem", devpath);
    link_base(p, sub, ssz);
    link_base(devpath, slot, slsz);
}

static void pci_name(const char *slot, char *b, size_t sz)
{
    char *out = CMD("lspci", "-nn", "-s", slot), *c;
    *b = 0;
    if (out && *out) {
        out[strcspn(out, "\n")] = 0;
        c = strstr(out, ": ");
        snprintf(b, sz, "%s", c ? c + 2 : out);
    }
    free(out);
}

static void nic_desc(const char *n, char *b, size_t sz)
{
    char p[400], sub[32], slot[64], t[256], m[256];
    snprintf(p, sizeof p, SCN "/%s/device", n);
    if (!path_exists(p)) {
        char ty[64];
        char drv[64];
        nic_type(n, ty, sizeof ty);
        nic_driver(n, drv, sizeof drv);
        if (strcmp(drv, "-")) snprintf(b, sz, "%s (virtual, driver %s)", ty, drv);
        else snprintf(b, sz, "%s (virtual)", ty);
        return;
    }
    dev_subsys(p, sub, sizeof sub, slot, sizeof slot);
    if (!strcmp(sub, "usb")) {
        snprintf(p, sizeof p, SCN "/%s/device/../product", n);
        if (!rd_str(p, t, sizeof t)) snprintf(t, sizeof t, "USB network adapter");
        snprintf(p, sizeof p, SCN "/%s/device/../manufacturer", n);
        if (rd_str(p, m, sizeof m) && *m) snprintf(b, sz, "%s %s", m, t);
        else snprintf(b, sz, "%s", t);
        return;
    }
    if (!strcmp(sub, "pci")) {
        pci_name(slot, t, sizeof t);
        if (*t) { snprintf(b, sz, "%s", t); return; }
    }
    nic_driver(n, t, sizeof t);
    snprintf(b, sz, "%s device %s", t, slot);
}

/* ======================================================================
 * addresses (`ip -o addr`), DNS, gateways, multicast
 * ====================================================================== */
typedef struct { char dev[64]; int v6, plen; char addr[64], scope[16], flags[160], lft[64]; } ipaddr;

static int get_addrs(ipaddr *v, int max)
{
    char *out = CMD("ip", "-o", "addr", "show"), *line, *save;
    int n = 0;
    if (!out) return 0;
    for (line = strtok_r(out, "\n", &save); line && n < max; line = strtok_r(NULL, "\n", &save)) {
        char *bs = strchr(line, '\\'), *tok[40], *s2, *t;
        int nt = 0, i;
        ipaddr *a = &v[n];
        memset(a, 0, sizeof *a);
        if (bs) {
            char *vl = strstr(bs, "valid_lft"), *pl = strstr(bs, "preferred_lft");
            *bs = 0;
            if (vl) {
                char x[32] = "", y[32] = "";
                sscanf(vl, "valid_lft %31s", x);
                if (pl) sscanf(pl, "preferred_lft %31s", y);
                snprintf(a->lft, sizeof a->lft, "%s/%s", x, y);
            }
        }
        for (t = strtok_r(line, " \t", &s2); t && nt < 40; t = strtok_r(NULL, " \t", &s2)) tok[nt++] = t;
        if (nt < 4) continue;
        snprintf(a->dev, sizeof a->dev, "%s", tok[1]);
        a->dev[strcspn(a->dev, "@")] = 0;
        a->v6 = !strcmp(tok[2], "inet6");
        if (!a->v6 && strcmp(tok[2], "inet")) continue;
        snprintf(a->addr, sizeof a->addr, "%s", tok[3]);
        if ((t = strchr(a->addr, '/'))) { *t = 0; a->plen = atoi(t + 1); }
        for (i = 4; i < nt; i++) {
            if ((!strcmp(tok[i], "brd") || !strcmp(tok[i], "peer") || !strcmp(tok[i], "metric") || !strcmp(tok[i], "proto")) && i + 1 < nt) { i++; continue; }
            if (!strcmp(tok[i], "scope") && i + 1 < nt) { snprintf(a->scope, sizeof a->scope, "%s", tok[++i]); continue; }
            if (!strcmp(tok[i], a->dev) || !strncmp(tok[i], a->dev, strlen(a->dev))) continue;  /* label */
            if (strlen(a->flags) + strlen(tok[i]) + 2 < sizeof a->flags) {
                if (*a->flags) strcat(a->flags, " ");
                strcat(a->flags, tok[i]);
            }
        }
        n++;
    }
    free(out);
    return n;
}

static int has_word(const char *s, const char *w)
{
    size_t l = strlen(w);
    const char *p = s;
    while ((p = strstr(p, w))) {
        if ((p == s || p[-1] == ' ') && (p[l] == ' ' || !p[l])) return 1;
        p += l;
    }
    return 0;
}

static void addr_origin(const ipaddr *a, const char **pfx, const char **sfx, const char **dad)
{
    if (!strcmp(a->scope, "host")) { *pfx = "WellKnown"; *sfx = "WellKnown"; }
    else if (!a->v6) { *pfx = *sfx = has_word(a->flags, "dynamic") ? "DHCP" : "Manual"; }
    else if (!strcmp(a->scope, "link")) { *pfx = "WellKnown"; *sfx = has_word(a->flags, "stable-privacy") ? "Random" : "Link"; }
    else {
        *pfx = has_word(a->flags, "dynamic") ? "RouterAdv" : "Manual";
        *sfx = (has_word(a->flags, "temporary") || has_word(a->flags, "stable-privacy")) ? "Random" : has_word(a->flags, "dynamic") ? "Link" : "Manual";
    }
    if (has_word(a->flags, "tentative")) *dad = "Tentative";
    else if (has_word(a->flags, "dadfailed")) *dad = "Duplicate";
    else if (has_word(a->flags, "deprecated")) *dad = "Deprecated";
    else *dad = "Preferred";
}

/* resolv.conf */
typedef struct { char ns[8][64]; int nns; char search[256], domain[128], options[256]; } resolv;
static void get_resolv(resolv *r)
{
    char *all, *line, *save;
    memset(r, 0, sizeof *r);
    if (rd_all("/etc/resolv.conf", &all) < 0) return;
    for (line = strtok_r(all, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char k[32], v[256];
        if (*line == '#' || *line == ';') continue;
        if (sscanf(line, "%31s %255[^\n]", k, v) != 2) continue;
        if (!strcmp(k, "nameserver") && r->nns < 8) snprintf(r->ns[r->nns++], 64, "%s", v);
        else if (!strcmp(k, "search")) snprintf(r->search, sizeof r->search, "%s", v);
        else if (!strcmp(k, "domain")) snprintf(r->domain, sizeof r->domain, "%s", v);
        else if (!strcmp(k, "options")) snprintf(r->options, sizeof r->options, "%s", v);
    }
    free(all);
}

/* per-device DNS/gateway from NetworkManager */
typedef struct { char dev[64], dns[6][64], gw4[64], gw6[64], conn[128]; int ndns; } nmdev;
static int get_nmdevs(nmdev *v, int max)
{
    char *out = CMD("nmcli", "-t", "-f", "GENERAL.DEVICE,GENERAL.CONNECTION,IP4.GATEWAY,IP6.GATEWAY,IP4.DNS,IP6.DNS", "device", "show"), *line, *save;
    int n = 0;
    nmdev *c = NULL;
    if (!out) return 0;
    for (line = strtok_r(out, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *f[2], *k;
        if (nm_split(line, f, 2) < 2) continue;
        k = f[0];
        if (!strcmp(k, "GENERAL.DEVICE")) {
            if (n >= max) break;
            c = &v[n++];
            memset(c, 0, sizeof *c);
            snprintf(c->dev, sizeof c->dev, "%s", f[1]);
        } else if (!c) continue;
        else if (!strcmp(k, "GENERAL.CONNECTION")) snprintf(c->conn, sizeof c->conn, "%s", f[1]);
        else if (!strcmp(k, "IP4.GATEWAY")) snprintf(c->gw4, sizeof c->gw4, "%s", f[1]);
        else if (!strcmp(k, "IP6.GATEWAY")) snprintf(c->gw6, sizeof c->gw6, "%s", f[1]);
        else if ((!strncmp(k, "IP4.DNS", 7) || !strncmp(k, "IP6.DNS", 7)) && c->ndns < 6 && *f[1]) snprintf(c->dns[c->ndns++], 64, "%s", f[1]);
    }
    free(out);
    return n;
}
static nmdev *nm_find(nmdev *v, int n, const char *dev) { int i; for (i = 0; i < n; i++) if (!strcmp(v[i].dev, dev)) return &v[i]; return NULL; }

/* IPv4 default gateway of a device from /proc/net/route ("" if none) */
static void gw4_of(const char *dev, char *b, size_t sz)
{
    char *all, *line, *save;
    *b = 0;
    if (rd_all("/proc/net/route", &all) < 0) return;
    for (line = strtok_r(all, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char ifn[64];
        unsigned dst, gw, fl;
        if (sscanf(line, "%63s %x %x %x", ifn, &dst, &gw, &fl) != 4) continue;
        if (dst == 0 && (fl & 2) && (!dev || !strcmp(ifn, dev))) {
            struct in_addr ia = { .s_addr = gw };
            inet_ntop(AF_INET, &ia, b, (socklen_t)sz);
            if (!dev) { size_t l = strlen(b); snprintf(b + l, sz - l, " dev %s", ifn); }
            break;
        }
    }
    free(all);
}

/* multicast groups joined by a device (IPv4 from /proc/net/igmp, IPv6 from /proc/net/igmp6) */
static int mcast_of(const char *dev, char out[][64], int max)
{
    char *all, *line, *save, cur[64] = "";
    int n = 0;
    if (rd_all("/proc/net/igmp", &all) >= 0) {
        for (line = strtok_r(all, "\n", &save); line && n < max; line = strtok_r(NULL, "\n", &save)) {
            char d[64];
            unsigned v;
            if (line[0] != '\t' && line[0] != ' ') {
                if (sscanf(line, "%*d %63s", d) == 1) snprintf(cur, sizeof cur, "%s", d);
                continue;
            }
            if (!strcmp(cur, dev) && sscanf(line, " %x", &v) == 1) {
                struct in_addr ia = { .s_addr = v };
                inet_ntop(AF_INET, &ia, out[n++], 64);
            }
        }
        free(all);
    }
    if (rd_all("/proc/net/igmp6", &all) >= 0) {
        for (line = strtok_r(all, "\n", &save); line && n < max; line = strtok_r(NULL, "\n", &save)) {
            char d[64], hex[40];
            struct in6_addr a;
            int i;
            if (sscanf(line, "%*d %63s %39s", d, hex) != 2 || strcmp(d, dev) || strlen(hex) != 32) continue;
            for (i = 0; i < 16; i++) { unsigned x; sscanf(hex + 2 * i, "%2x", &x); a.s6_addr[i] = (unsigned char)x; }
            inet_ntop(AF_INET6, &a, out[n++], 64);
        }
        free(all);
    }
    return n;
}

static const char *proxy_env(void)
{
    static const char *v[] = { "https_proxy", "HTTPS_PROXY", "http_proxy", "HTTP_PROXY", "all_proxy", "ALL_PROXY", NULL };
    int i;
    for (i = 0; v[i]; i++) if (getenv(v[i]) && *getenv(v[i])) return getenv(v[i]);
    return NULL;
}

/* ======================================================================
 * sockets (/proc/net/{tcp,udp}[6]) with owning process
 * ====================================================================== */
typedef struct { unsigned long ino; int pid; char comm[32]; } inomap;
static inomap *g_im;
static int g_nim, g_cim;

static void build_inomap(void)
{
    DIR *pd = opendir("/proc");
    struct dirent *e;
    g_nim = 0;
    if (!pd) return;
    while ((e = readdir(pd))) {
        char p[300], comm[32] = "?";
        DIR *fd;
        struct dirent *f;
        int pid;
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        pid = atoi(e->d_name);
        snprintf(p, sizeof p, "/proc/%d/fd", pid);
        if (!(fd = opendir(p))) continue;
        {
            char cp[64];
            snprintf(cp, sizeof cp, "/proc/%d/comm", pid);
            rd_str(cp, comm, sizeof comm);
        }
        while ((f = readdir(fd))) {
            char lp[400], t[64];
            ssize_t l;
            if (f->d_name[0] == '.') continue;
            snprintf(lp, sizeof lp, "/proc/%d/fd/%s", pid, f->d_name);
            l = readlink(lp, t, sizeof t - 1);
            if (l <= 8) continue;
            t[l] = 0;
            if (strncmp(t, "socket:[", 8)) continue;
            if (g_nim >= g_cim) {
                int nc = g_cim ? g_cim * 2 : 4096;
                inomap *nm = realloc(g_im, (size_t)nc * sizeof *nm);
                if (!nm) break;
                g_im = nm;
                g_cim = nc;
            }
            g_im[g_nim].ino = strtoul(t + 8, NULL, 10);
            g_im[g_nim].pid = pid;
            snprintf(g_im[g_nim].comm, sizeof g_im[g_nim].comm, "%s", comm);
            g_nim++;
        }
        closedir(fd);
    }
    closedir(pd);
}

static void ino_proc(unsigned long ino, char *b, size_t sz)
{
    int i;
    if (ino)
        for (i = 0; i < g_nim; i++)
            if (g_im[i].ino == ino) { snprintf(b, sz, "%s (%d)", g_im[i].comm, g_im[i].pid); return; }
    snprintf(b, sz, ino ? "-" : "(kernel/time-wait)");
}

typedef struct { int v6, st; char la[64], ra[64]; unsigned lp, rp, uid; unsigned long ino; } sockent;

static void hexaddr(const char *h, int v6, char *b, size_t sz)
{
    if (!v6) {
        struct in_addr ia;
        ia.s_addr = (uint32_t)strtoul(h, NULL, 16);
        inet_ntop(AF_INET, &ia, b, (socklen_t)sz);
    } else {
        struct in6_addr a;
        int i;
        for (i = 0; i < 4; i++) {
            char w[9];
            uint32_t x;
            memcpy(w, h + 8 * i, 8);
            w[8] = 0;
            x = (uint32_t)strtoul(w, NULL, 16);
            memcpy(&a.s6_addr[4 * i], &x, 4);
        }
        inet_ntop(AF_INET6, &a, b, (socklen_t)sz);
    }
}

static int read_socks(const char *path, int v6, sockent *v, int max)
{
    char *all, *line, *save;
    int n = 0, first = 1;
    if (rd_all(path, &all) < 0) return 0;
    for (line = strtok_r(all, "\n", &save); line && n < max; line = strtok_r(NULL, "\n", &save)) {
        char la[64], ra[64];
        unsigned lp, rp, st, uid;
        unsigned long ino;
        if (first) { first = 0; continue; }
        if (sscanf(line, " %*d: %63[0-9A-Fa-f]:%x %63[0-9A-Fa-f]:%x %x %*x:%*x %*x:%*x %*x %u %*d %lu", la, &lp, ra, &rp, &st, &uid, &ino) != 7) continue;
        v[n].v6 = v6;
        hexaddr(la, v6, v[n].la, sizeof v[n].la);
        hexaddr(ra, v6, v[n].ra, sizeof v[n].ra);
        v[n].lp = lp;
        v[n].rp = rp;
        v[n].st = (int)st;
        v[n].uid = uid;
        v[n].ino = ino;
        n++;
    }
    free(all);
    return n;
}

static const char *tcp_state(int st)
{
    static const char *s[] = { "?", "Established", "SYN Sent", "SYN Received", "FIN Wait 1", "FIN Wait 2", "Time Wait",
                               "Closed", "Close Wait", "Last ACK", "Listen", "Closing", "New SYN Recv" };
    return st >= 0 && st <= 12 ? s[st] : "?";
}

static int sock_cmp(const void *a, const void *b)
{
    const sockent *x = a, *y = b;
    int lx = x->st == 10 || x->st == 7, ly = y->st == 10 || y->st == 7;
    if (lx != ly) return ly - lx;
    if (x->lp != y->lp) return (int)x->lp - (int)y->lp;
    return x->st - y->st;
}

static void svc_name(unsigned port, const char *proto, char *b, size_t sz)
{
    struct servent *se;
    if (!port) { snprintf(b, sz, "*"); return; }
    se = getservbyport(htons((uint16_t)port), proto);
    snprintf(b, sz, "%s", se ? se->s_name : "");
}

static void s_ports(srs_out *o, const char *path, int v6, int udp)
{
    static const int t4[] = { 140, 195, 290, 430, 485, 580, 680, 840 };
    static const int t6[] = { 270, 325, 420, 690, 745, 840, 940, 1100 };
    const char *proto = udp ? "udp" : "tcp";
    sockent *v = malloc(8192 * sizeof *v);
    int n, i, nl = 0, ne = 0;
    char title[64];
    if (!v) return;
    n = read_socks(path, v6, v, 8192);
    build_inomap();
    qsort(v, (size_t)n, sizeof *v, sock_cmp);
    snprintf(title, sizeof title, "%s V%d %s", udp ? "UDP" : "TCP", v6 ? 6 : 4, udp ? "Endpoints" : "Connections");
    so_siv_header(o, title);
    so_tabs(o, v6 ? t6 : t4, 8);
    so_row(o, 9, (const char *const[]){ "Local Address", "Port", "Service", "Remote Address", "Port", "Service", "State", "User", "Process (PID)" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < n; i++) {
        char lp[16], rp[16], ls[40], rs[40], us[40], pr[96];
        const char *st;
        struct passwd *pw = getpwuid(v[i].uid);
        snprintf(lp, sizeof lp, "%u", v[i].lp);
        snprintf(rp, sizeof rp, v[i].rp ? "%u" : "*", v[i].rp);
        svc_name(v[i].lp, proto, ls, sizeof ls);
        svc_name(v[i].rp, proto, rs, sizeof rs);
        snprintf(us, sizeof us, "%s", pw ? pw->pw_name : "?");
        ino_proc(v[i].ino, pr, sizeof pr);
        if (udp) st = v[i].st == 7 ? "Listen" : v[i].st == 1 ? "Connected" : tcp_state(v[i].st);
        else st = tcp_state(v[i].st);
        if (v[i].st == 10 || (udp && v[i].st == 7)) nl++; else ne++;
        so_row(o, 9, (const char *const[]){ v[i].la, lp, ls, v[i].ra, rp, rs, st, us, pr },
               (const int[]){ C_VALUE, C_BLACK, C_PURPLE, C_VALUE, C_BLACK, C_PURPLE,
                              v[i].st == 10 || (udp && v[i].st == 7) ? C_GREEN : v[i].st == 1 ? C_TEAL : C_ORANGE, C_GRAY, C_ORANGE });
    }
    so_par(o);
    so_f(o, C_GRAY, "  %d sockets: %d listening, %d other.  Source %s; processes of other users show as \"-\" without root.", n, nl, ne, path);
    so_par(o);
    free(v);
}

static void s_tcp4(srs_out *o) { s_ports(o, "/proc/net/tcp", 0, 0); }
static void s_tcp6(srs_out *o) { s_ports(o, "/proc/net/tcp6", 1, 0); }
static void s_udp4(srs_out *o) { s_ports(o, "/proc/net/udp", 0, 1); }
static void s_udp6(srs_out *o) { s_ports(o, "/proc/net/udp6", 1, 1); }

/* ======================================================================
 * [adapters] - SIV's Adapters panel
 * ====================================================================== */
static void s_adapters(srs_out *o)
{
    static const int th[] = { 280, 420, 560, 700 };
    static const int t[] = { 150, 290, 370, 640, 740, 840, 930, 1080 };
    nic nv[MAXNIC];
    ipaddr *av = malloc(MAXADDR * sizeof *av);
    nmdev nd[64];
    resolv rv;
    char host[256] = "", dom[256] = "", fw[16] = "0", b[256];
    int nn = nic_list(nv, MAXNIC), na, nnd, i, j;
    if (!av) return;
    na = get_addrs(av, MAXADDR);
    nnd = get_nmdevs(nd, 64);
    get_resolv(&rv);
    gethostname(host, sizeof host - 1);
    if (getdomainname(dom, sizeof dom - 1) || !strcmp(dom, "(none)")) *dom = 0;
    if (!*dom) snprintf(dom, sizeof dom, "%s", *rv.domain ? rv.domain : rv.search);
    rd_str("/proc/sys/net/ipv4/ip_forward", fw, sizeof fw);

    so_siv_header(o, "Network Adapters");
    so_tabs(o, th, 4);
    so_row(o, 5, (const char *const[]){ "Linux IP Configuration", "Host", "Domain", "DNS IP Address", "Routing / Proxy" },
           (const int[]){ C_TEAL, C_TEAL, C_PURPLE, C_VALUE, C_BLACK });
    snprintf(b, sizeof b, "%s / %s", atoi(fw) ? "Enabled" : "Disabled", proxy_env() ? "Enabled" : "Disabled");
    so_row(o, 5, (const char *const[]){ sys_os_name(), host, *dom ? dom : "-", rv.nns ? rv.ns[0] : "-", b },
           (const int[]){ C_GRAY, C_TEAL, C_PURPLE, C_VALUE, C_BLACK });
    for (i = 1; i < rv.nns; i++) so_row(o, 4, (const char *const[]){ "", "", "", rv.ns[i] }, NULL);
    so_par(o);

    so_tabs(o, t, 8);
    so_row(o, 9, (const char *const[]){ "Adapter / Description", "Type (Index)", "Speed", "Adapter Address", "Prefix Origin", "Suffix Origin", "DAD State", "DNS and Gateway", "Multicast Address" },
           (const int[]){ C_TEAL, C_ORANGE, C_GREEN, C_VALUE, C_GREEN, C_PURPLE, C_ORANGE, C_TEAL, C_GREEN });
    for (i = 0; i < nn; i++) {
        const char *n = nv[i].name;
        char ty[64], tyi[96], spd[32], mac[64], desc[256], mtu[48], st[32], dg[8][96], mc[16][64], gw[64], drv[64];
        int ida[64], nida = 0, ndg = 0, nmc, rows, r;
        nmdev *d = nm_find(nd, nnd, n);
        if (!arg_match(o, n)) continue;
        for (j = 0; j < na && nida < 64; j++) if (!strcmp(av[j].dev, n)) ida[nida++] = j;
        nic_type(n, ty, sizeof ty);
        snprintf(tyi, sizeof tyi, "%s (%d)", ty, nv[i].idx);
        nic_str(n, "operstate", st, sizeof st);
        fmt_speed(!strcmp(st, "up") ? nic_long(n, "speed") : -1, spd, sizeof spd);
        nic_str(n, "address", mac, sizeof mac);
        for (j = 0; mac[j]; j++) mac[j] = (char)toupper((unsigned char)mac[j]);
        nic_desc(n, desc, sizeof desc);
        snprintf(mtu, sizeof mtu, "MTU %ld", nic_long(n, "mtu"));
        if (d) for (j = 0; j < d->ndns && ndg < 6; j++) snprintf(dg[ndg++], 96, "DNS %s", d->dns[j]);
        gw4_of(n, gw, sizeof gw);
        if (!ndg && *gw) for (j = 0; j < rv.nns && ndg < 4; j++) snprintf(dg[ndg++], 96, "DNS %s", rv.ns[j]);
        if (*gw) snprintf(dg[ndg++], 96, "GW %s", gw);
        if (d && *d->gw6 && ndg < 8) snprintf(dg[ndg++], 96, "GW %s", d->gw6);
        nmc = mcast_of(n, mc, 16);
        if (nmc > 4) { snprintf(mc[3], 64, "+%d more", nmc - 3); nmc = 4; }
        nic_driver(n, drv, sizeof drv);
        rows = 3;
        if (nida > rows) rows = nida;
        if (ndg > rows) rows = ndg;
        if (nmc > rows) rows = nmc;
        for (r = 0; r < rows; r++) {
            const char *pfx = "", *sfx = "", *dad = "", *ad = "";
            char adb[96] = "", c2[64] = "";
            const char *c0 = r == 0 ? n : r == 1 ? drv : "";
            if (r < nida) {
                ipaddr *a = &av[ida[r]];
                snprintf(adb, sizeof adb, "%s/%d", a->addr, a->plen);
                ad = adb;
                addr_origin(a, &pfx, &sfx, &dad);
            }
            if (r == 0) snprintf(c2, sizeof c2, "%s", spd);
            else if (r == 1) snprintf(c2, sizeof c2, "%s", mtu);
            else if (r == 2) snprintf(c2, sizeof c2, "%s", st);
            so_row(o, 9, (const char *const[]){ c0, r == 0 ? tyi : r == 1 ? mac : "", c2, ad, pfx, sfx, dad, r < ndg ? dg[r] : "", r < nmc ? mc[r] : "" },
                   (const int[]){ r == 0 ? C_TEAL : C_GRAY, r == 0 ? C_ORANGE : C_PURPLE, r == 2 ? (strcmp(st, "up") ? C_RED : C_GREEN) : C_GREEN, C_VALUE, C_GREEN, C_PURPLE, C_ORANGE, C_TEAL, C_GREEN });
        }
        so_t(o, C_DEF, "  ");
        so_t(o, nv[i].rank == 0 ? C_BLACK : C_GRAY, desc);
        so_par(o);
        so_par(o);
    }
    so_note(o, "Physical adapters first, then virtual (bridges, veth, tun/tap), loopback last. Open as adapters:<ifname> for one adapter.");
    free(av);
}

/* ======================================================================
 * [network] summary
 * ====================================================================== */
static void s_network(srs_out *o)
{
    static const int tk[] = { 170 };
    static const int t[] = { 150, 240, 320, 420, 560, 720 };
    nic nv[MAXNIC];
    ipaddr *av = malloc(MAXADDR * sizeof *av);
    resolv rv;
    char host[256] = "", gw[128], b[512], fw4[16] = "?", fw6[16] = "?";
    int nn = nic_list(nv, MAXNIC), na, i, j, up = 0, phys = 0;
    char *g6;
    if (!av) return;
    na = get_addrs(av, MAXADDR);
    get_resolv(&rv);
    gethostname(host, sizeof host - 1);
    for (i = 0; i < nn; i++) {
        char st[32];
        nic_str(nv[i].name, "operstate", st, sizeof st);
        if (!strcmp(st, "unknown") && strcmp(nv[i].name, "lo")) {   /* tun / wireguard report "unknown": use carrier */
            char ca[8] = "";
            nic_str(nv[i].name, "carrier", ca, sizeof ca);
            up += !strcmp(ca, "1");
        } else up += !strcmp(st, "up");
        phys += nv[i].rank == 0;
    }
    so_siv_header(o, "Network");
    so_tabs(o, tk, 1);
    so_kv(o, "Host name", C_BLACK, host);
    so_kvf(o, "Adapters", C_VALUE, "%d (%d physical, %d virtual), %d up", nn, phys, nn - phys, up);
    gw4_of(NULL, gw, sizeof gw);
    so_kv(o, "IPv4 default gateway", C_TEAL, *gw ? gw : "none");
    g6 = CMD("ip", "-o", "-6", "route", "show", "default");
    if (g6) { g6[strcspn(g6, "\n")] = 0; trim(g6); }
    so_kv(o, "IPv6 default route", C_TEAL, g6 && *g6 ? g6 : "none");
    free(g6);
    *b = 0;
    for (i = 0; i < rv.nns; i++) { if (*b) strcat(b, "  "); strncat(b, rv.ns[i], sizeof b - strlen(b) - 1); }
    so_kv(o, "DNS servers", C_VALUE, *b ? b : "none (/etc/resolv.conf)");
    if (*rv.search) so_kv(o, "DNS search", C_PURPLE, rv.search);
    rd_str("/proc/sys/net/ipv4/ip_forward", fw4, sizeof fw4);
    rd_str("/proc/sys/net/ipv6/conf/all/forwarding", fw6, sizeof fw6);
    so_kvf(o, "IP forwarding", C_ORANGE, "IPv4 %s, IPv6 %s", atoi(fw4) ? "on" : "off", atoi(fw6) ? "on" : "off");
    rd_str("/proc/sys/net/ipv4/tcp_congestion_control", b, sizeof b);
    so_kv(o, "TCP congestion control", C_PURPLE, b);
    rd_str("/proc/sys/net/core/default_qdisc", b, sizeof b);
    so_kv(o, "Default qdisc", C_PURPLE, b);
    so_par(o);
    so_tabs(o, t, 6);
    so_row(o, 7, (const char *const[]){ "Adapter", "Type", "State", "Speed", "MAC Address", "IPv4", "Driver" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < nn; i++) {
        char ty[64], st[32], spd[32], mac[64], ip4[128] = "", drv[64];
        nic_type(nv[i].name, ty, sizeof ty);
        nic_str(nv[i].name, "operstate", st, sizeof st);
        fmt_speed(!strcmp(st, "up") ? nic_long(nv[i].name, "speed") : -1, spd, sizeof spd);
        nic_str(nv[i].name, "address", mac, sizeof mac);
        nic_driver(nv[i].name, drv, sizeof drv);
        for (j = 0; j < na; j++)
            if (!strcmp(av[j].dev, nv[i].name) && !av[j].v6) {
                if (*ip4) strncat(ip4, " ", sizeof ip4 - strlen(ip4) - 1);
                snprintf(ip4 + strlen(ip4), sizeof ip4 - strlen(ip4), "%s/%d", av[j].addr, av[j].plen);
            }
        so_row(o, 7, (const char *const[]){ nv[i].name, ty, st, spd, mac, *ip4 ? ip4 : "-", drv },
               (const int[]){ nv[i].rank == 0 ? C_BLACK : C_TEAL, C_ORANGE, !strcmp(st, "up") ? C_GREEN : !strcmp(st, "down") ? C_RED : C_GRAY,
                              C_GREEN, C_PURPLE, C_VALUE, C_GRAY });
    }
    so_board_footer(o);
    free(av);
}

/* ======================================================================
 * [ip-address]
 * ====================================================================== */
static void s_ip_address(srs_out *o)
{
    static const int t[] = { 130, 420, 480, 560, 900 };
    nic nv[MAXNIC];
    ipaddr *av = malloc(MAXADDR * sizeof *av);
    int nn = nic_list(nv, MAXNIC), na, i, j;
    if (!av) return;
    na = get_addrs(av, MAXADDR);
    so_siv_header(o, "IP Addresses");
    so_tabs(o, t, 5);
    so_row(o, 6, (const char *const[]){ "Adapter", "Address", "Prefix", "Scope", "Flags", "Lifetime (valid/preferred)" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < nn; i++) {
        int first = 1;
        if (!arg_match(o, nv[i].name)) continue;
        for (j = 0; j < na; j++) {
            char pl[16];
            if (strcmp(av[j].dev, nv[i].name)) continue;
            snprintf(pl, sizeof pl, "/%d", av[j].plen);
            so_row(o, 6, (const char *const[]){ first ? nv[i].name : "", av[j].addr, pl, av[j].scope, *av[j].flags ? av[j].flags : "-", av[j].lft },
                   (const int[]){ C_TEAL, av[j].v6 ? C_PURPLE : C_VALUE, C_BLACK, C_ORANGE, C_GREEN, C_GRAY });
            first = 0;
        }
        if (first) so_row(o, 2, (const char *const[]){ nv[i].name, "(no addresses)" }, (const int[]){ C_TEAL, C_GRAY });
    }
    free(av);
}

/* ======================================================================
 * [mac-address]
 * ====================================================================== */
static char *g_oui;
static void oui_vendor(const char *mac, char *b, size_t sz)
{
    char key[32], *p;
    unsigned x0;
    *b = 0;
    if (strlen(mac) < 8) return;
    if (sscanf(mac, "%2x", &x0) == 1 && (x0 & 2)) { snprintf(b, sz, "(locally administered)"); return; }
    if (!g_oui && rd_all("/usr/share/hwdata/oui.txt", &g_oui) < 0) { g_oui = NULL; return; }
    snprintf(key, sizeof key, "%c%c-%c%c-%c%c   (hex)", toupper((unsigned char)mac[0]), toupper((unsigned char)mac[1]), toupper((unsigned char)mac[3]),
             toupper((unsigned char)mac[4]), toupper((unsigned char)mac[6]), toupper((unsigned char)mac[7]));
    p = strstr(g_oui, key);
    if (!p) return;
    p += strlen(key);
    while (*p == ' ' || *p == '\t') p++;
    snprintf(b, sz, "%.*s", (int)strcspn(p, "\r\n"), p);
}

static void s_mac_address(srs_out *o)
{
    static const int t[] = { 130, 280, 430, 560, 660 };
    static const char *at[] = { "permanent", "random", "stolen", "set by user" };
    nic nv[MAXNIC];
    int nn = nic_list(nv, MAXNIC), i;
    so_siv_header(o, "MAC Addresses");
    so_tabs(o, t, 5);
    so_row(o, 6, (const char *const[]){ "Adapter", "MAC Address", "Permanent", "Assigned", "Broadcast", "Vendor (OUI)" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < nn; i++) {
        char mac[64], brd[64], perm[64] = "-", ven[256];
        long a = nic_long(nv[i].name, "addr_assign_type");
        nic_str(nv[i].name, "address", mac, sizeof mac);
        nic_str(nv[i].name, "broadcast", brd, sizeof brd);
        if (nv[i].rank == 0) {
            char *e = CMD("ethtool", "-P", nv[i].name), *c;
            if (e && (c = strstr(e, "address: "))) { snprintf(perm, sizeof perm, "%s", c + 9); perm[strcspn(perm, "\n")] = 0; }
            free(e);
        }
        if (nv[i].rank == 2 || !strcmp(mac, "00:00:00:00:00:00")) *ven = 0;
        else oui_vendor(!strcmp(perm, "-") ? mac : perm, ven, sizeof ven);
        so_row(o, 6, (const char *const[]){ nv[i].name, mac, perm, a >= 0 && a <= 3 ? at[a] : "-", brd, ven },
               (const int[]){ nv[i].rank == 0 ? C_BLACK : C_TEAL, C_PURPLE, C_VALUE, C_ORANGE, C_GRAY, C_GREEN });
    }
    so_note(o, "Permanent address from `ethtool -P` (physical adapters); vendor from /usr/share/hwdata/oui.txt.");
}

/* ======================================================================
 * [net-cards]
 * ====================================================================== */
static void s_net_cards(srs_out *o)
{
    static const int tk[] = { 170 };
    nic nv[MAXNIC];
    int nn = nic_list(nv, MAXNIC), i, cnt = 0;
    so_siv_header(o, "Network Cards");
    so_tabs(o, tk, 1);
    for (i = 0; i < nn; i++) {
        char p[400], sub[32], slot[64], desc[256], drv[64], st[32], spd[32], dup[32], mac[64], b[256];
        char *e, *line, *save;
        if (nv[i].rank != 0 || !arg_match(o, nv[i].name)) continue;
        cnt++;
        snprintf(p, sizeof p, SCN "/%s/device", nv[i].name);
        dev_subsys(p, sub, sizeof sub, slot, sizeof slot);
        nic_desc(nv[i].name, desc, sizeof desc);
        nic_driver(nv[i].name, drv, sizeof drv);
        nic_str(nv[i].name, "operstate", st, sizeof st);
        nic_str(nv[i].name, "duplex", dup, sizeof dup);
        nic_str(nv[i].name, "address", mac, sizeof mac);
        fmt_speed(!strcmp(st, "up") ? nic_long(nv[i].name, "speed") : -1, spd, sizeof spd);
        so_b(o, C_TEAL, nv[i].name);
        so_par(o);
        so_kv(o, "Description", C_BLACK, desc);
        snprintf(b, sizeof b, "%s %s", sub, slot);
        so_kv(o, "Bus", C_VALUE, b);
        if (!strcmp(sub, "usb")) {
            char v[16] = "", d[16] = "", sp[16] = "";
            snprintf(p, sizeof p, SCN "/%s/device/../idVendor", nv[i].name);
            rd_str(p, v, sizeof v);
            snprintf(p, sizeof p, SCN "/%s/device/../idProduct", nv[i].name);
            rd_str(p, d, sizeof d);
            snprintf(p, sizeof p, SCN "/%s/device/../speed", nv[i].name);
            rd_str(p, sp, sizeof sp);
            so_kvf(o, "USB ID", C_PURPLE, "%s:%s  (USB link %s Mbps)", v, d, sp);
        } else if (!strcmp(sub, "pci")) {
            char v[16] = "", d[16] = "", sv[16] = "", sd[16] = "", w[32] = "", s2[64] = "";
            snprintf(p, sizeof p, SCN "/%s/device/vendor", nv[i].name); rd_str(p, v, sizeof v);
            snprintf(p, sizeof p, SCN "/%s/device/device", nv[i].name); rd_str(p, d, sizeof d);
            snprintf(p, sizeof p, SCN "/%s/device/subsystem_vendor", nv[i].name); rd_str(p, sv, sizeof sv);
            snprintf(p, sizeof p, SCN "/%s/device/subsystem_device", nv[i].name); rd_str(p, sd, sizeof sd);
            snprintf(p, sizeof p, SCN "/%s/device/current_link_width", nv[i].name); rd_str(p, w, sizeof w);
            snprintf(p, sizeof p, SCN "/%s/device/current_link_speed", nv[i].name); rd_str(p, s2, sizeof s2);
            so_kvf(o, "PCI ID", C_PURPLE, "%s:%s  subsystem %s:%s", v + (v[0] == '0' && v[1] == 'x' ? 2 : 0), d + (d[0] == '0' && d[1] == 'x' ? 2 : 0),
                   sv + (sv[0] == '0' && sv[1] == 'x' ? 2 : 0), sd + (sd[0] == '0' && sd[1] == 'x' ? 2 : 0));
            if (*w) so_kvf(o, "PCIe link", C_VALUE, "x%s  %s", w, s2);
        }
        so_kv(o, "Driver", C_ORANGE, drv);
        e = CMD("ethtool", "-i", nv[i].name);
        if (e) {
            for (line = strtok_r(e, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
                char *c = strchr(line, ':');
                if (!c || !c[1] || !c[2]) continue;
                *c = 0;
                if (!strcmp(line, "version")) so_kv(o, "Driver version", C_GRAY, c + 2);
                else if (!strcmp(line, "firmware-version")) so_kv(o, "Firmware", C_PURPLE, c + 2);
                else if (!strcmp(line, "expansion-rom-version")) so_kv(o, "Expansion ROM", C_PURPLE, c + 2);
            }
            free(e);
        }
        so_kv(o, "MAC address", C_PURPLE, mac);
        if (!strcmp(st, "up") && strcmp(dup, "-") && strcmp(dup, "unknown")) so_kvf(o, "Link", C_GREEN, "%s  %s  %s duplex", st, spd, dup);
        else so_kv(o, "Link", !strcmp(st, "up") ? C_GREEN : C_RED, st);
        so_kvf(o, "MTU", C_VALUE, "%ld  (tx queue %ld)", nic_long(nv[i].name, "mtu"), nic_long(nv[i].name, "tx_queue_len"));
        snprintf(p, sizeof p, SCN "/%s/wireless", nv[i].name);
        if (path_exists(p)) {
            snprintf(p, sizeof p, SCN "/%s/phy80211", nv[i].name);
            link_base(p, b, sizeof b);
            so_kv(o, "Wireless PHY", C_TEAL, b);
        }
        so_par(o);
    }
    if (!cnt) so_note(o, "No physical network adapters found.");
    so_note(o, "Physical adapters only (with a /sys/class/net/<if>/device link); virtual adapters are listed in [adapters].");
}

/* ======================================================================
 * [interface] statistics
 * ====================================================================== */
static void s_interface(srs_out *o)
{
    static const int t[] = { 130, 230, 330, 390, 450, 550, 650, 710, 770, 850, 920 };
    static const char *st[] = { "rx_bytes", "rx_packets", "rx_errors", "rx_dropped", "tx_bytes", "tx_packets", "tx_errors", "tx_dropped", "multicast", "collisions" };
    nic nv[MAXNIC];
    int nn = nic_list(nv, MAXNIC), i, k;
    so_siv_header(o, "Network Interface Statistics");
    so_tabs(o, t, 11);
    so_row(o, 12, (const char *const[]){ "Interface", "Rx Bytes", "Rx Packets", "Rx Err", "Rx Drop", "Tx Bytes", "Tx Packets", "Tx Err", "Tx Drop", "Multicast", "Collisions", "Carrier chg" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < nn; i++) {
        char c[12][40];
        const char *cp[12];
        if (!arg_match(o, nv[i].name)) continue;
        snprintf(c[0], 40, "%s", nv[i].name);
        for (k = 0; k < 10; k++) {
            unsigned long long v = nic_stat(nv[i].name, st[k]);
            if (k == 0 || k == 4) fmt_bytes((double)v, c[k + 1], 40); else fmt_count(v, c[k + 1], 40);
        }
        snprintf(c[11], 40, "%ld", nic_long(nv[i].name, "carrier_changes"));
        for (k = 0; k < 12; k++) cp[k] = c[k];
        so_row(o, 12, cp, (const int[]){ nv[i].rank == 0 ? C_BLACK : C_TEAL, C_VALUE, C_VALUE, C_RED, C_ORANGE, C_PURPLE, C_PURPLE, C_RED, C_ORANGE, C_GREEN, C_GRAY, C_GRAY });
    }
    so_note(o, "Source /sys/class/net/<if>/statistics (same counters as /proc/net/dev and `ip -s link`).");
}

/* ======================================================================
 * [counters] - live per-adapter rates + /proc/net/snmp protocol counters
 * ====================================================================== */
typedef struct { char name[64]; unsigned long long v[4]; } ctr;
static ctr g_prev[MAXNIC];
static int g_nprev;
static double g_tprev;

static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }

static int sample(nic *nv, int nn, ctr *c)
{
    static const char *k[] = { "rx_bytes", "tx_bytes", "rx_packets", "tx_packets" };
    int i, j;
    for (i = 0; i < nn; i++) {
        snprintf(c[i].name, sizeof c[i].name, "%s", nv[i].name);
        for (j = 0; j < 4; j++) c[i].v[j] = nic_stat(nv[i].name, k[j]);
    }
    return nn;
}

static void snmp_tables(srs_out *o, const char *path)
{
    char *all, *line, *save, *prev = NULL;
    if (rd_all(path, &all) < 0) return;
    for (line = strtok_r(all, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *h, *v, *hs, *vs, *ht, *vt, *c1, *c2, pfx[32];
        if (!prev) { prev = line; continue; }
        /* prev = header line "Ip: Forwarding DefaultTTL ...", line = values */
        h = prev; v = line; prev = NULL;
        c1 = strchr(h, ':'); c2 = strchr(v, ':');
        if (!c1 || !c2) continue;
        snprintf(pfx, sizeof pfx, "%.*s", (int)(c1 - h), h);
        so_b(o, C_TEAL, pfx);
        so_par(o);
        ht = strtok_r(c1 + 1, " ", &hs);
        vt = strtok_r(c2 + 1, " ", &vs);
        while (ht && vt) {
            char num[40];
            if (vt[0] == '-') snprintf(num, sizeof num, "%s", vt); else fmt_count(strtoull(vt, NULL, 10), num, sizeof num);
            so_t(o, C_DEF, "  ");
            so_kv(o, ht, strtoull(vt, NULL, 10) ? C_VALUE : C_GRAY, num);
            ht = strtok_r(NULL, " ", &hs);
            vt = strtok_r(NULL, " ", &vs);
        }
    }
    free(all);
}

static void s_counters(srs_out *o)
{
    static const int t[] = { 140, 250, 360, 460, 560, 680, 800 };
    static const int tk[] = { 230 };
    nic nv[MAXNIC];
    ctr cur[MAXNIC];
    int nn = nic_list(nv, MAXNIC), i, j;
    double tn, dt;
    if (!g_nprev && !o->gui) {          /* -save / CLI: take a short baseline so rates are real */
        g_nprev = sample(nv, nn, g_prev);
        g_tprev = now_s();
        usleep(500000);
    }
    sample(nv, nn, cur);
    tn = now_s();
    dt = tn - g_tprev;
    so_siv_header(o, "TCP/IP Counters");
    so_tabs(o, t, 7);
    so_row(o, 8, (const char *const[]){ "Adapter", "Rx /s", "Tx /s", "Rx pkt/s", "Tx pkt/s", "Rx total", "Tx total", "State" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < nn; i++) {
        char c[8][48], st[32];
        const char *cp[8];
        ctr *p = NULL;
        for (j = 0; j < g_nprev; j++) if (!strcmp(g_prev[j].name, cur[i].name)) { p = &g_prev[j]; break; }
        snprintf(c[0], 48, "%s", cur[i].name);
        for (j = 0; j < 4; j++) {
            if (p && dt > 0.05 && cur[i].v[j] >= p->v[j]) {
                double r = (double)(cur[i].v[j] - p->v[j]) / dt;
                if (j < 2) { fmt_bytes(r, c[j + 1], 48); strcat(c[j + 1], "/s"); }
                else snprintf(c[j + 1], 48, "%.0f", r);
            } else snprintf(c[j + 1], 48, "-");
        }
        fmt_bytes((double)cur[i].v[0], c[5], 48);
        fmt_bytes((double)cur[i].v[1], c[6], 48);
        nic_str(cur[i].name, "operstate", st, sizeof st);
        snprintf(c[7], 48, "%s", st);
        for (j = 0; j < 8; j++) cp[j] = c[j];
        so_row(o, 8, cp, (const int[]){ nv[i].rank == 0 ? C_BLACK : C_TEAL, C_GREEN, C_PURPLE, C_GREEN, C_PURPLE, C_VALUE, C_VALUE, !strcmp(st, "up") ? C_GREEN : C_GRAY });
    }
    if (dt > 0.05 && g_nprev) so_f(o, C_GRAY, "  rates over the last %.2f s", dt);
    else so_t(o, C_GRAY, "  rates appear after the next refresh");
    so_par(o);
    memcpy(g_prev, cur, (size_t)nn * sizeof *cur);
    g_nprev = nn;
    g_tprev = tn;
    so_par(o);
    so_tabs(o, tk, 1);
    snmp_tables(o, "/proc/net/snmp");
}

/* ======================================================================
 * [arp-table] / [nodes]
 * ====================================================================== */
static void s_mdns(srs_out *o);
static void s_arp(srs_out *o)
{
    static const int t[] = { 260, 400, 480, 560 };
    char *all, *line, *save, *n6;
    int first = 1, cnt = 0;
    so_siv_header(o, "ARP / Neighbour Table");
    so_tabs(o, t, 4);
    so_row(o, 5, (const char *const[]){ "IP Address", "Physical Address", "HW Type", "Flags", "Adapter" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    if (rd_all("/proc/net/arp", &all) >= 0) {
        for (line = strtok_r(all, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char ip[64], hw[16], fl[16], mac[64], mask[16], dev[64];
            unsigned f;
            if (first) { first = 0; continue; }
            if (sscanf(line, "%63s %15s %15s %63s %15s %63s", ip, hw, fl, mac, mask, dev) != 6) continue;
            f = (unsigned)strtoul(fl, NULL, 16);
            so_row(o, 5, (const char *const[]){ ip, mac, !strcmp(hw, "0x1") ? "Ethernet" : hw, (f & 4) ? "Static" : (f & 2) ? "Dynamic" : "Incomplete", dev },
                   (const int[]){ C_VALUE, C_PURPLE, C_ORANGE, (f & 2) ? C_GREEN : C_GRAY, C_TEAL });
            cnt++;
        }
        free(all);
    }
    n6 = CMD("ip", "-o", "-6", "neigh", "show");
    if (n6) {
        for (line = strtok_r(n6, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char ip[64] = "", dev[64] = "", mac[64] = "-", *p;
            const char *stt;
            trim(line);
            sscanf(line, "%63s", ip);
            if ((p = strstr(line, " dev "))) sscanf(p + 5, "%63s", dev);
            if ((p = strstr(line, " lladdr "))) sscanf(p + 8, "%63s", mac);
            stt = strrchr(line, ' ');
            so_row(o, 5, (const char *const[]){ ip, mac, "IPv6 NDP", stt ? stt + 1 : "", dev },
                   (const int[]){ C_PURPLE, C_PURPLE, C_ORANGE, C_GREEN, C_TEAL });
            cnt++;
        }
        free(n6);
    }
    if (!cnt) so_note(o, "Neighbour table is empty.");
    s_mdns(o);
}

/* mDNS part of the neighbour view (SIV [nodes]; that id is owned by the cpu group as NUMA nodes) */
/* "\032" (avahi: 3 decimal digits) / "\040" (mounts: 3 octal digits) escapes -> characters, in place */
static void unescape3(char *s, int base)
{
    char *w = s;
    for (; *s; s++) {
        if (s[0] == '\\' && isdigit((unsigned char)s[1]) && isdigit((unsigned char)s[2]) && isdigit((unsigned char)s[3])) {
            int v = base == 8 ? (s[1] - '0') * 64 + (s[2] - '0') * 8 + (s[3] - '0') : (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
            if (v > 0 && v < 256) { *w++ = (char)v; s += 3; continue; }
        }
        *w++ = *s;
    }
    *w = 0;
}

static void s_mdns(srs_out *o)
{
    static const int ta[] = { 120, 200, 520, 720 };
    char *out, *line, *save;
    int cnt;
    so_par(o);
    so_b(o, C_TEAL, "mDNS / DNS-SD services (avahi cache, no new queries)");
    so_par(o);
    so_tabs(o, ta, 4);
    out = CMD("avahi-browse", "-a", "-t", "-p", "-c");
    cnt = 0;
    if (out) {
        for (line = strtok_r(out, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char *f[8], *s = line;
            int k = 0;
            while (k < 8 && s) { f[k++] = s; s = strchr(s, ';'); if (s) *s++ = 0; }
            if (k < 6 || f[0][0] != '+') continue;
            unescape3(f[3], 10);
            so_row(o, 5, (const char *const[]){ f[1], f[2], f[3], f[4], f[5] }, (const int[]){ C_TEAL, C_ORANGE, C_BLACK, C_PURPLE, C_GRAY });
            cnt++;
        }
        free(out);
    }
    if (!cnt) so_note(o, "avahi-daemon cache empty or avahi not running.");
}

/* ======================================================================
 * [routing]
 * ====================================================================== */
static void s_routing(srs_out *o)
{
    static const int t[] = { 150, 290, 430, 520, 600, 680 };
    char *all, *line, *save, *r6, fw[16] = "0";
    int first = 1;
    so_siv_header(o, "Routing Table");
    rd_str("/proc/sys/net/ipv4/ip_forward", fw, sizeof fw);
    so_f(o, C_LABEL, "  IPv4 routing (forwarding) ");
    so_b(o, atoi(fw) ? C_GREEN : C_GRAY, atoi(fw) ? "Enabled" : "Disabled");
    so_par(o);
    so_par(o);
    so_tabs(o, t, 6);
    so_row(o, 7, (const char *const[]){ "Destination", "Netmask", "Gateway", "Flags", "Metric", "MTU", "Adapter" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    if (rd_all("/proc/net/route", &all) >= 0) {
        for (line = strtok_r(all, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char ifn[64], d[32], g[32], m[32], fs[16] = "", met[16], mtu[16];
            unsigned dst, gw, fl, mask, mt, mtuv;
            int refc, use;
            struct in_addr a;
            if (first) { first = 0; continue; }
            if (sscanf(line, "%63s %x %x %x %d %d %u %x %u", ifn, &dst, &gw, &fl, &refc, &use, &mt, &mask, &mtuv) != 9) continue;
            a.s_addr = dst; inet_ntop(AF_INET, &a, d, sizeof d);
            a.s_addr = gw; inet_ntop(AF_INET, &a, g, sizeof g);
            a.s_addr = mask; inet_ntop(AF_INET, &a, m, sizeof m);
            if (fl & 1) strcat(fs, "U");
            if (fl & 2) strcat(fs, "G");
            if (fl & 4) strcat(fs, "H");
            if (fl & 0x200) strcat(fs, "!");
            snprintf(met, sizeof met, "%u", mt);
            snprintf(mtu, sizeof mtu, "%u", mtuv);
            so_row(o, 7, (const char *const[]){ dst ? d : "default", m, gw ? g : "on-link", fs, met, mtuv ? mtu : "-", ifn },
                   (const int[]){ dst ? C_VALUE : C_BLACK, C_GRAY, gw ? C_GREEN : C_GRAY, C_ORANGE, C_PURPLE, C_GRAY, C_TEAL });
        }
        free(all);
    }
    so_par(o);
    so_b(o, C_TEAL, "IPv6 routes");
    so_par(o);
    r6 = CMD("ip", "-o", "-6", "route", "show", "table", "all");
    if (r6) {
        for (line = strtok_r(r6, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char *b = strchr(line, '\\');
            if (b) *b = 0;
            trim(line);
            so_t(o, C_DEF, "  ");
            so_m(o, strncmp(line, "local", 5) && strncmp(line, "multicast", 9) ? C_PURPLE : C_GRAY, line);
            so_par(o);
        }
        free(r6);
    }
}

/* ======================================================================
 * [protocols] / [transports]
 * ====================================================================== */
static void s_protocols(srs_out *o)
{
    static const int t[] = { 130, 200, 280, 360, 420 };
    char *all, *line, *save;
    int first = 1, n = 0;
    so_siv_header(o, "Network Protocols");
    so_tabs(o, t, 5);
    so_row(o, 6, (const char *const[]){ "Protocol", "Size", "Sockets", "Memory", "Press", "Module" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    if (rd_all("/proc/net/protocols", &all) >= 0) {
        for (line = strtok_r(all, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char nm[32], sz[16], so[16], mem[16], pr[16], mx[16], sl[16], mod[64];
            if (first) { first = 0; continue; }
            if (sscanf(line, "%31s %15s %15s %15s %15s %15s %15s %63s", nm, sz, so, mem, pr, mx, sl, mod) != 8) continue;
            so_row(o, 6, (const char *const[]){ nm, sz, so, mem, pr, mod }, (const int[]){ C_TEAL, C_GRAY, atoi(so) ? C_BLACK : C_GRAY, C_VALUE, C_ORANGE, C_PURPLE });
            n++;
        }
        free(all);
    }
    so_par(o);
    if (rd_all("/etc/protocols", &all) >= 0) {
        int k = 0;
        for (line = strtok_r(all, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) if (*line && *line != '#') k++;
        free(all);
        so_f(o, C_GRAY, "  %d kernel socket protocols (/proc/net/protocols); /etc/protocols defines %d IP protocol numbers.", n, k);
        so_par(o);
    }
}

static void s_transports(srs_out *o)
{
    static const int tk[] = { 120 };
    const char *f[] = { "/proc/net/sockstat", "/proc/net/sockstat6", NULL };
    char *all, *line, *save, **names;
    int i, n;
    so_siv_header(o, "Network Transports");
    so_tabs(o, tk, 1);
    for (i = 0; f[i]; i++) {
        if (rd_all(f[i], &all) < 0) continue;
        for (line = strtok_r(all, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char *c = strchr(line, ':');
            if (!c) continue;
            *c = 0;
            so_kv(o, line, C_VALUE, c + 1 + (c[1] == ' '));
        }
        free(all);
    }
    so_par(o);
    so_b(o, C_TEAL, "Address families with registered sockets");
    so_par(o);
    n = dir_list("/proc/net", &names);
    if (n > 0) {
        char b[2048] = "";
        static const char *fam[] = { "unix", "packet", "netlink", "raw", "raw6", "tcp", "tcp6", "udp", "udp6", "udplite", "udplite6", "icmp", "icmp6", "sctp", "bluetooth", "can", "vsock", "xdp", "rds", "tipc", NULL };
        int k, j;
        for (k = 0; fam[k]; k++)
            for (j = 0; j < n; j++)
                if (!strcmp(names[j], fam[k])) { strncat(b, fam[k], sizeof b - strlen(b) - 2); strcat(b, " "); }
        dir_free(names, n);
        so_t(o, C_DEF, "  ");
        so_t(o, C_PURPLE, b);
        so_par(o);
    }
    so_note(o, "Linux has no NetBIOS transport bindings; SMB/NetBIOS runs over TCP 139/445 and UDP 137/138 (see [servers]).");
}

/* ======================================================================
 * DNS: [dns-names] [dns-ports] [dns-usage] [dns-cache]
 * ====================================================================== */
static int resolved_up(void) { return path_exists("/run/systemd/resolve/io.systemd.Resolve"); }

static void s_dns_names(srs_out *o)
{
    static const int t[] = { 150, 300 };
    char host[256] = "", dom[256] = "", nb[64] = "", full[600], *tp, *line, *save;
    resolv rv;
    struct passwd *pw = getpwuid(getuid());
    gethostname(host, sizeof host - 1);
    if (getdomainname(dom, sizeof dom - 1) || !strcmp(dom, "(none)")) *dom = 0;
    get_resolv(&rv);
    if (!*dom) snprintf(dom, sizeof dom, "%s", *rv.domain ? rv.domain : "");
    tp = CMD("testparm", "-s", "--suppress-prompt", "--parameter-name=netbios name");
    if (tp) { tp[strcspn(tp, "\n")] = 0; trim(tp); snprintf(nb, sizeof nb, "%s", tp); free(tp); }
    if (!*nb) { int i; for (i = 0; host[i] && i < 15; i++) nb[i] = (char)toupper((unsigned char)host[i]); nb[i] = 0; }
    snprintf(full, sizeof full, "%s%s%s", host, *dom ? "." : "", dom);
    so_siv_header(o, "DNS Names");
    so_tabs(o, t, 2);
    so_row(o, 3, (const char *const[]){ "Local Name", "Computer Name", "Computer Physical Name" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    so_row(o, 3, (const char *const[]){ "NetBIOS Name", nb, nb }, (const int[]){ C_LABEL, C_BLACK, C_BLACK });
    so_row(o, 3, (const char *const[]){ "DNS Domain", dom, dom }, (const int[]){ C_LABEL, C_PURPLE, C_PURPLE });
    so_row(o, 3, (const char *const[]){ "DNS Host Name", host, host }, (const int[]){ C_LABEL, C_BLACK, C_BLACK });
    so_row(o, 3, (const char *const[]){ "Full DNS Name", full, full }, (const int[]){ C_LABEL, C_BLACK, C_BLACK });
    so_par(o);
    so_par(o);
    so_row(o, 2, (const char *const[]){ "User Name Format", "Formatted User Name" }, (const int[]){ C_LABEL, C_LABEL });
    so_par(o);
    if (pw) {
        char gec[256], leg[400];
        snprintf(gec, sizeof gec, "%s", pw->pw_gecos ? pw->pw_gecos : "");
        gec[strcspn(gec, ",")] = 0;
        snprintf(leg, sizeof leg, "%s\\%s", nb, pw->pw_name);
        so_row(o, 2, (const char *const[]){ "Display Username", pw->pw_name }, (const int[]){ C_LABEL, C_BLACK });
        so_row(o, 2, (const char *const[]){ "Legacy Account Name", leg }, (const int[]){ C_LABEL, C_BLACK });
        if (*gec) so_row(o, 2, (const char *const[]){ "Full Name (GECOS)", gec }, (const int[]){ C_LABEL, C_BLACK });
        snprintf(leg, sizeof leg, "%u", (unsigned)pw->pw_uid);
        so_row(o, 2, (const char *const[]){ "User ID", leg }, (const int[]){ C_LABEL, C_VALUE });
    }
    so_par(o);
    if (rd_all("/etc/hostname", &tp) >= 0) { tp[strcspn(tp, "\n")] = 0; so_kv(o, "/etc/hostname", C_GRAY, tp); free(tp); }
    tp = CMD("hostnamectl", "--json=short");
    if (tp && *tp) {
        /* pick a few fields without a JSON parser */
        static const char *keys[] = { "StaticHostname", "PrettyHostname", "Chassis", "Deployment", "Location", NULL };
        int i;
        for (i = 0; keys[i]; i++) {
            char k[64], *p, v[128];
            snprintf(k, sizeof k, "\"%s\":\"", keys[i]);
            if ((p = strstr(tp, k))) { snprintf(v, sizeof v, "%.*s", (int)strcspn(p + strlen(k), "\""), p + strlen(k)); so_kv(o, keys[i], C_GRAY, v); }
        }
    }
    free(tp);
    (void)line; (void)save;
}

static int is_dns_port(unsigned p) { return p == 53 || p == 853 || p == 5353 || p == 5355; }

static void s_dns_ports(srs_out *o)
{
    static const int t[] = { 70, 330, 400, 480, 600 };
    static const struct { const char *path; int v6, udp; } f[] = { { "/proc/net/udp", 0, 1 }, { "/proc/net/udp6", 1, 1 }, { "/proc/net/tcp", 0, 0 }, { "/proc/net/tcp6", 1, 0 } };
    sockent *v = malloc(8192 * sizeof *v);
    resolv rv;
    int i, k, n, cnt = 0;
    if (!v) return;
    build_inomap();
    so_siv_header(o, "DNS Ports");
    so_tabs(o, t, 5);
    so_row(o, 6, (const char *const[]){ "Proto", "Address", "Port", "Service", "State", "Process (PID)" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (k = 0; k < 4; k++) {
        n = read_socks(f[k].path, f[k].v6, v, 8192);
        for (i = 0; i < n; i++) {
            char pp[16], sv[32], pr[96], proto[8];
            int listen = f[k].udp ? v[i].st == 7 : v[i].st == 10;
            unsigned port = is_dns_port(v[i].lp) ? v[i].lp : is_dns_port(v[i].rp) ? v[i].rp : 0;
            if (!port) continue;
            snprintf(pp, sizeof pp, "%u", port);
            svc_name(port, f[k].udp ? "udp" : "tcp", sv, sizeof sv);
            ino_proc(v[i].ino, pr, sizeof pr);
            snprintf(proto, sizeof proto, "%s%s", f[k].udp ? "UDP" : "TCP", f[k].v6 ? "6" : "");
            so_row(o, 6, (const char *const[]){ proto, port == v[i].lp ? v[i].la : v[i].ra, pp, sv,
                                                listen ? "Listen" : port == v[i].lp ? "Local" : "To server", pr },
                   (const int[]){ C_ORANGE, C_VALUE, C_BLACK, C_PURPLE, listen ? C_GREEN : C_TEAL, C_ORANGE });
            cnt++;
        }
    }
    if (!cnt) so_note(o, "No sockets on DNS ports 53/853/5353/5355.");
    so_par(o);
    get_resolv(&rv);
    so_b(o, C_TEAL, "Configured DNS servers (/etc/resolv.conf)");
    so_par(o);
    for (i = 0; i < rv.nns; i++) { so_t(o, C_DEF, "  "); so_t(o, C_VALUE, rv.ns[i]); so_t(o, C_GRAY, "  port 53"); so_par(o); }
    if (*rv.options) so_kv(o, "  options", C_GRAY, rv.options);
    so_f(o, C_GRAY, "  systemd-resolved: %s", resolved_up() ? "running (stub 127.0.0.53)" : "not running");
    so_par(o);
    free(v);
}

static void s_dns_usage(srs_out *o)
{
    static const int tk[] = { 200 };
    char *all, *line, *save, *st;
    resolv rv;
    so_siv_header(o, "DNS Usage");
    so_tabs(o, tk, 1);
    if (resolved_up() && (st = CMD("resolvectl", "statistics")) && *st) {
        strip_ansi(st);
        so_lines(o, C_VALUE, st);
        free(st);
        return;
    }
    so_note(o, "systemd-resolved is not running, so there are no resolver usage statistics; showing the resolver setup instead.");
    so_par(o);
    get_resolv(&rv);
    so_kvf(o, "Name servers", C_VALUE, "%d", rv.nns);
    so_kv(o, "Search list", C_PURPLE, *rv.search ? rv.search : "-");
    so_kv(o, "Resolver options", C_ORANGE, *rv.options ? rv.options : "-");
    if (rd_all("/etc/nsswitch.conf", &all) >= 0) {
        for (line = strtok_r(all, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
            if (!strncmp(line, "hosts:", 6)) { trim(line + 6); so_kv(o, "NSS hosts order", C_TEAL, line + 6); }
        free(all);
    }
    if (rd_all("/etc/hosts", &all) >= 0) {
        int n = 0;
        for (line = strtok_r(all, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) { trim(line); if (*line && *line != '#') n++; }
        free(all);
        so_kvf(o, "/etc/hosts entries", C_VALUE, "%d", n);
    }
    so_kv(o, "/etc/gai.conf", C_GRAY, path_exists("/etc/gai.conf") ? "present" : "absent (RFC 3484 defaults)");
}

static int proc_running(const char *name)
{
    DIR *d = opendir("/proc");
    struct dirent *e;
    int found = 0;
    if (!d) return 0;
    while (!found && (e = readdir(d))) {
        char p[300], c[64];
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        snprintf(p, sizeof p, "/proc/%s/comm", e->d_name);
        if (rd_str(p, c, sizeof c) && !strcmp(c, name)) found = atoi(e->d_name);
    }
    closedir(d);
    return found;
}

static void s_dns_cache(srs_out *o)
{
    static const int tk[] = { 200 };
    static const char *caches[] = { "systemd-resolve", "dnsmasq", "nscd", "unbound", "named", "kresd", "dnscrypt-proxy", "stubby", "pdns_recursor", "AdGuardHome", NULL };
    int i, any = 0;
    so_siv_header(o, "DNS Cache");
    so_tabs(o, tk, 1);
    for (i = 0; caches[i]; i++) {
        int pid = proc_running(caches[i]);
        if (pid) { so_kvf(o, caches[i], C_GREEN, "running (pid %d)", pid); any = 1; }
    }
    if (!any) so_kv(o, "Local caching resolver", C_GRAY, "none running");
    so_par(o);
    if (resolved_up()) {
        char *c = CMD("resolvectl", "show-cache");
        if (c && *c) { strip_ansi(c); so_lines(o, C_VALUE, c); }
        else so_note(o, "Needs root: Menu > Run as root (srs-root)  (resolvectl show-cache is root-only)");
        free(c);
    } else {
        so_note(o, "systemd-resolved is not running: glibc does not cache lookups, so there is no system DNS cache to list.");
        char *rc = NULL;
        int local = rd_all("/etc/resolv.conf", &rc) >= 0 && (strstr(rc, "nameserver 127.") || strstr(rc, "nameserver ::1"));
        free(rc);
        if (proc_running("dnsmasq") && local)
            so_note(o, "/etc/resolv.conf points at a local resolver and dnsmasq is running: dnsmasq is the system DNS cache (its cache is not listed).");
        else if (proc_running("dnsmasq"))
            so_note(o, "dnsmasq is running but /etc/resolv.conf does not use it: those are likely libvirt/container DHCP+DNS instances.");
    }
}

/* ======================================================================
 * [proxy]
 * ====================================================================== */
static void s_proxy(srs_out *o)
{
    static const int tk[] = { 200 };
    static const char *ev[] = { "http_proxy", "https_proxy", "ftp_proxy", "all_proxy", "no_proxy", "HTTP_PROXY", "HTTPS_PROXY", "FTP_PROXY", "ALL_PROXY", "NO_PROXY", NULL };
    char *all, *line, *save, b[512], *g;
    int i, any = 0;
    so_siv_header(o, "Proxy Configuration");
    so_tabs(o, tk, 1);
    so_b(o, C_TEAL, "Environment (this process)");
    so_par(o);
    for (i = 0; ev[i]; i++) if (getenv(ev[i]) && *getenv(ev[i])) { mask_userinfo(getenv(ev[i]), b, sizeof b); so_kv(o, ev[i], C_VALUE, b); any = 1; }
    if (!any) so_kv(o, "Proxy variables", C_GRAY, "none set (direct connection)");
    so_par(o);
    so_b(o, C_TEAL, "/etc/environment");
    so_par(o);
    any = 0;
    if (rd_all("/etc/environment", &all) >= 0) {
        for (line = strtok_r(all, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char *eq = strchr(line, '=');
            if (!eq || !strcasestr(line, "proxy") || *line == '#') continue;
            *eq = 0;
            mask_userinfo(eq + 1, b, sizeof b);
            so_kv(o, line, C_VALUE, b);
            any = 1;
        }
        free(all);
    }
    if (!any) so_kv(o, "Proxy settings", C_GRAY, "none");
    so_par(o);
    so_b(o, C_TEAL, "KDE (kioslaverc [Proxy Settings])");
    so_par(o);
    {
        const char *h = getenv("HOME");
        char p[600];
        int in = 0;
        static const char *pt[] = { "No proxy", "Manual", "PAC script", "WPAD auto-detect", "System environment" };
        any = 0;
        snprintf(p, sizeof p, "%s/.config/kioslaverc", h ? h : "");
        if (h && rd_all(p, &all) >= 0) {
            for (line = strtok_r(all, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
                char *eq;
                if (*line == '[') { in = !strncmp(line, "[Proxy Settings]", 16); continue; }
                if (!in || !(eq = strchr(line, '='))) continue;
                *eq = 0;
                if (!strcmp(line, "ProxyType")) { int t = atoi(eq + 1); so_kv(o, "ProxyType", C_ORANGE, t >= 0 && t <= 4 ? pt[t] : eq + 1); }
                else if (secret_name(line)) so_kv(o, line, C_GRAY, "****");
                else { mask_userinfo(eq + 1, b, sizeof b); so_kv(o, line, C_VALUE, b); }
                any = 1;
            }
            free(all);
        }
        if (!any) so_kv(o, "ProxyType", C_GRAY, "not configured (No proxy)");
    }
    g = CMD("gsettings", "get", "org.gnome.system.proxy", "mode");
    if (g && *g) { g[strcspn(g, "\n")] = 0; so_par(o); so_kv(o, "GNOME proxy mode", C_ORANGE, g); }
    free(g);
}

/* ======================================================================
 * [firewall]
 * ====================================================================== */
static void s_firewall(srs_out *o)
{
    static const int tk[] = { 200 };
    char *st, *z, *line, *save;
    int rc = -1;
    so_siv_header(o, "Firewall");
    so_tabs(o, tk, 1);
    st = cmd((const char *const[]){ "firewall-cmd", "--state", NULL }, &rc);
    if (rc < 0 || !st) {
        so_kv(o, "firewalld", C_GRAY, "not installed");
    } else {
        st[strcspn(st, "\n")] = 0;
        so_kv(o, "firewalld state", !strcmp(st, "running") ? C_GREEN : C_RED, *st ? st : "not running");
        if (!strcmp(st, "running")) {
            char *pm = CMD("firewall-cmd", "--query-panic");
            if (pm) { pm[strcspn(pm, "\n")] = 0; so_kv(o, "Panic mode", strcmp(pm, "no") ? C_RED : C_GREEN, pm); }
            free(pm);
            so_par(o);
            /* one call for every zone; show the active ones (firewall-cmd is slow to start) */
            z = CMD("firewall-cmd", "--list-all-zones");
            if (z) {
                int show = 0;
                for (line = strtok_r(z, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
                    char *c;
                    if (!*line) continue;
                    if (!isspace((unsigned char)line[0])) {
                        if (show) so_par(o);
                        show = strstr(line, "active") != NULL;
                        if (show) { so_b(o, C_TEAL, line); so_par(o); }
                        if (strstr(line, "default")) { char zn[64]; sscanf(line, "%63s", zn); so_kv(o, "Default zone", C_BLACK, zn); }
                        continue;
                    }
                    if (!show) continue;
                    trim(line);
                    c = strchr(line, ':');
                    if (!c) continue;
                    *c = 0;
                    trim(c + 1);
                    if (!c[1]) continue;
                    so_t(o, C_DEF, "  ");
                    so_kv(o, line, C_VALUE, c + 1);
                }
                free(z);
                if (show) so_par(o);
            }
        }
    }
    free(st);
    so_kv(o, "nftables ruleset", C_GRAY, "`nft list ruleset` is root-only (CAP_NET_ADMIN)");
    so_note(o, "Needs root: Menu > Run as root (srs-root)");
}

/* ======================================================================
 * [profiles] / [wlan]
 * ====================================================================== */
static void s_profiles_impl(srs_out *o, int wifi_only)
{
    static const int t[] = { 230, 400, 510, 600, 840 };
    char *out = CMD("nmcli", "-t", "-f", "NAME,TYPE,DEVICE,AUTOCONNECT,TIMESTAMP-REAL,UUID", "connection", "show"), *line, *save;
    int cnt = 0;
    so_siv_header(o, wifi_only ? "WLAN Profiles" : "Network Profiles");
    if (!out) { so_note(o, "NetworkManager (nmcli) is not available."); return; }
    so_tabs(o, t, 5);
    so_row(o, 6, (const char *const[]){ "Profile", "Type", "Device", "Auto", "Last used", wifi_only ? "SSID / Security / IPv4" : "UUID" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (line = strtok_r(out, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *f[6], extra[300] = "";
        if (nm_split(line, f, 6) < 6) continue;
        if (wifi_only && strcmp(f[1], "802-11-wireless")) continue;
        if (wifi_only) {
            char *d = CMD("nmcli", "-t", "-f", "802-11-wireless.ssid,802-11-wireless.mode,802-11-wireless-security.key-mgmt,ipv4.method", "connection", "show", f[5]), *l2, *s2;
            char ssid[128] = "", mode[32] = "", km[32] = "open", m4[32] = "";
            if (d) {
                for (l2 = strtok_r(d, "\n", &s2); l2; l2 = strtok_r(NULL, "\n", &s2)) {
                    char *g[2];
                    if (nm_split(l2, g, 2) < 2) continue;
                    if (!strcmp(g[0], "802-11-wireless.ssid")) snprintf(ssid, sizeof ssid, "%s", g[1]);
                    else if (!strcmp(g[0], "802-11-wireless.mode")) snprintf(mode, sizeof mode, "%s", g[1]);
                    else if (!strcmp(g[0], "802-11-wireless-security.key-mgmt") && *g[1]) snprintf(km, sizeof km, "%s", g[1]);
                    else if (!strcmp(g[0], "ipv4.method")) snprintf(m4, sizeof m4, "%s", g[1]);
                }
                free(d);
            }
            snprintf(extra, sizeof extra, "%s  [%s, %s, ipv4 %s]", ssid, mode, km, m4);
        }
        so_row(o, 6, (const char *const[]){ f[0], f[1], *f[2] ? f[2] : "-", f[3], f[4], wifi_only ? extra : f[5] },
               (const int[]){ *f[2] ? C_BLACK : C_TEAL, C_ORANGE, C_GREEN, C_VALUE, C_GRAY, wifi_only ? C_PURPLE : C_GRAY });
        cnt++;
    }
    free(out);
    if (!cnt) so_note(o, wifi_only ? "No Wi-Fi profiles." : "No profiles.");
    if (wifi_only) so_note(o, "Keys/PSKs are never shown (they need root or a secret agent and srs does not request them).");
}
static void s_profiles(srs_out *o) { s_profiles_impl(o, 0); }
static void s_wlan(srs_out *o) { s_profiles_impl(o, 1); }

/* ======================================================================
 * [wi-fi]
 * ====================================================================== */
static void s_wifi(srs_out *o)
{
    static const int tk[] = { 170 };
    static const int t[] = { 30, 260, 400, 450, 530, 640, 710, 780 };
    nic nv[MAXNIC];
    int nn = nic_list(nv, MAXNIC), i, any = 0;
    char *out, *line, *save;
    so_siv_header(o, "Wi-Fi");
    so_tabs(o, tk, 1);
    for (i = 0; i < nn; i++) {
        char p[300], *l;
        snprintf(p, sizeof p, SCN "/%s/wireless", nv[i].name);
        if (!path_exists(p) || !arg_match(o, nv[i].name)) continue;
        any = 1;
        so_b(o, C_TEAL, nv[i].name);
        so_par(o);
        l = CMD("iw", "dev", nv[i].name, "info");
        if (l) {
            for (line = strtok_r(l, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
                char k[64], v[200];
                trim(line);
                if (sscanf(line, "%63s %199[^\n]", k, v) != 2) continue;
                if (!strcmp(k, "addr") || !strcmp(k, "type") || !strcmp(k, "wiphy") || !strcmp(k, "channel") || !strcmp(k, "txpower") || !strcmp(k, "ssid"))
                    so_kv(o, k, C_VALUE, v);
            }
            free(l);
        }
        l = CMD("iw", "dev", nv[i].name, "link");
        if (l) {
            strip_ansi(l);
            for (line = strtok_r(l, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
                char *c = strchr(line, ':');
                trim(line);
                if (!strncmp(line, "Not connected", 13)) { so_kv(o, "Link", C_GRAY, "Not connected"); continue; }
                if (!strncmp(line, "Connected to", 12)) { so_kv(o, "Connected to", C_BLACK, line + 13); continue; }
                c = strchr(line, ':');
                if (!c) continue;
                *c = 0;
                trim(c + 1);
                so_kv(o, line, C_PURPLE, c + 1);
            }
            free(l);
        }
        so_par(o);
    }
    if (!any) { so_note(o, "No wireless interfaces."); return; }
    if (rd_all("/proc/net/wireless", &out) >= 0) {
        int n = 0;
        for (line = strtok_r(out, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            if (n++ < 2) continue;
            so_kv(o, "/proc/net/wireless", C_GRAY, line);
        }
        if (n <= 2) so_kv(o, "/proc/net/wireless", C_GRAY, "no associated interfaces");
        free(out);
    }
    so_par(o);
    so_b(o, C_TEAL, "Visible networks (NetworkManager cache, no rescan)");
    so_par(o);
    so_tabs(o, t, 8);
    so_row(o, 9, (const char *const[]){ "", "SSID", "BSSID", "Chan", "Band", "Rate", "Signal", "Bars", "Security" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    out = CMD("nmcli", "-t", "-f", "IN-USE,SSID,BSSID,CHAN,FREQ,RATE,SIGNAL,BARS,SECURITY", "device", "wifi", "list", "--rescan", "no");
    any = 0;
    if (out) {
        for (line = strtok_r(out, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char *f[9], sig[16], band[16];
            if (nm_split(line, f, 9) < 9) continue;
            snprintf(sig, sizeof sig, "%s%%", f[6]);
            snprintf(band, sizeof band, "%s", atoi(f[4]) >= 5925 ? "6 GHz" : atoi(f[4]) >= 5000 ? "5 GHz" : "2.4 GHz");
            so_row(o, 9, (const char *const[]){ *f[0] == '*' ? "*" : "", *f[1] ? f[1] : "(hidden)", f[2], f[3], band, f[5], sig, f[7], *f[8] ? f[8] : "open" },
                   (const int[]){ C_GREEN, *f[0] == '*' ? C_BLACK : C_TEAL, C_PURPLE, C_VALUE, C_ORANGE, C_GRAY, atoi(f[6]) >= 60 ? C_GREEN : atoi(f[6]) >= 30 ? C_ORANGE : C_RED, C_GRAY, C_VALUE });
            any = 1;
        }
        free(out);
    }
    if (!any) so_note(o, "NetworkManager has no cached scan results (srs never triggers a scan).");
}

/* ======================================================================
 * Bluetooth: [bluetooth] [bluetooth-ids] [blue-tree]
 * ====================================================================== */
static int bt_hcis(char out[][32], int max)
{
    char **names;
    int n = dir_list("/sys/class/bluetooth", &names), i, k = 0;
    if (n < 0) return 0;
    for (i = 0; i < n && k < max; i++) if (!strchr(names[i], ':') && names[i][0] != '.') snprintf(out[k++], 32, "%s", names[i]);
    dir_free(names, n);
    return k;
}

static void bt_sysfs(srs_out *o, const char *hci)
{
    char p[400], b[256], sub[32], slot[64], **names;
    int n, i;
    snprintf(p, sizeof p, "/sys/class/bluetooth/%s/device", hci);
    if (path_exists(p)) {
        dev_subsys(p, sub, sizeof sub, slot, sizeof slot);
        snprintf(b, sizeof b, "%s %s", sub, slot);
        so_kv(o, "Bus", C_VALUE, b);
        snprintf(p, sizeof p, "/sys/class/bluetooth/%s/device/driver", hci);
        link_base(p, b, sizeof b);
        so_kv(o, "Driver", C_ORANGE, b);
        if (!strcmp(sub, "usb")) {
            char v[16] = "", d[16] = "", m[128] = "", pr[128] = "", bcd[16] = "";
            snprintf(p, sizeof p, "/sys/class/bluetooth/%s/device/../idVendor", hci); rd_str(p, v, sizeof v);
            snprintf(p, sizeof p, "/sys/class/bluetooth/%s/device/../idProduct", hci); rd_str(p, d, sizeof d);
            snprintf(p, sizeof p, "/sys/class/bluetooth/%s/device/../bcdDevice", hci); rd_str(p, bcd, sizeof bcd);
            snprintf(p, sizeof p, "/sys/class/bluetooth/%s/device/../manufacturer", hci); rd_str(p, m, sizeof m);
            snprintf(p, sizeof p, "/sys/class/bluetooth/%s/device/../product", hci); rd_str(p, pr, sizeof pr);
            so_kvf(o, "USB ID", C_PURPLE, "%s:%s rev %s", v, d, bcd);
            if (*m || *pr) so_kvf(o, "USB device", C_BLACK, "%s %s", m, pr);
        }
    }
    snprintf(p, sizeof p, "/sys/class/bluetooth/%s", hci);
    n = dir_list(p, &names);
    for (i = 0; i < n; i++) {
        if (strncmp(names[i], "rfkill", 6)) continue;
        {
            char s[16] = "?", h[16] = "?", q[400];
            snprintf(q, sizeof q, "/sys/class/bluetooth/%s/%s/soft", hci, names[i]); rd_str(q, s, sizeof s);
            snprintf(q, sizeof q, "/sys/class/bluetooth/%s/%s/hard", hci, names[i]); rd_str(q, h, sizeof h);
            so_kvf(o, "rfkill", atoi(s) || atoi(h) ? C_RED : C_GREEN, "%s  soft-blocked %s, hard-blocked %s", names[i], atoi(s) ? "yes" : "no", atoi(h) ? "yes" : "no");
        }
    }
    dir_free(names, n);
}

static void s_bluetooth(srs_out *o)
{
    static const int tk[] = { 190 };
    char hci[16][32], *l, *line, *save;
    int nh = bt_hcis(hci, 16), i;
    so_siv_header(o, "Bluetooth Radios");
    so_tabs(o, tk, 1);
    if (!nh) { so_note(o, "No Bluetooth controllers in /sys/class/bluetooth."); return; }
    for (i = 0; i < nh; i++) {
        so_b(o, C_TEAL, hci[i]);
        so_par(o);
        bt_sysfs(o, hci[i]);
        so_par(o);
    }
    l = CMD("bluetoothctl", "list");
    if (!l || !*l) { free(l); so_note(o, "bluetoothctl: no controllers reported (bluetoothd not running?)."); return; }
    strip_ansi(l);
    {
        char macs[16][32];
        int nm = 0, k;
        for (line = strtok_r(l, "\n", &save); line && nm < 16; line = strtok_r(NULL, "\n", &save))
            if (sscanf(line, "Controller %31s", macs[nm]) == 1) nm++;
        free(l);
        for (k = 0; k < nm; k++) {
            char *s = CMD("bluetoothctl", "show", macs[k]);
            int nu = 0;
            if (!s) continue;
            strip_ansi(s);
            for (line = strtok_r(s, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
                char *c;
                if (!strncmp(line, "Controller ", 11)) { so_b(o, C_BLACK, line); so_par(o); continue; }
                if (!isspace((unsigned char)line[0])) { so_b(o, C_GRAY, line); so_par(o); continue; }
                trim(line);
                if (!(c = strchr(line, ':'))) continue;
                *c = 0;
                trim(c + 1);
                if (!strcmp(line, "UUID")) { if (nu++ == 0) so_kv(o, "Services (UUID)", C_PURPLE, c + 1); else so_kv(o, "", C_PURPLE, c + 1); continue; }
                so_kv(o, line, !strcmp(c + 1, "yes") ? C_GREEN : !strcmp(c + 1, "no") ? C_GRAY : C_VALUE, c + 1);
            }
            free(s);
            so_par(o);
        }
    }
}

typedef struct { char mac[32], name[128]; } btdev;
static int bt_devices(btdev *v, int max)
{
    char *l = CMD("bluetoothctl", "devices"), *line, *save;
    int n = 0;
    if (!l) return 0;
    strip_ansi(l);
    for (line = strtok_r(l, "\n", &save); line && n < max; line = strtok_r(NULL, "\n", &save)) {
        char m[32];
        int off = 0;
        if (sscanf(line, "Device %31s %n", m, &off) < 1 || !off) continue;
        snprintf(v[n].mac, sizeof v[n].mac, "%s", m);
        snprintf(v[n].name, sizeof v[n].name, "%s", line + off);
        n++;
    }
    free(l);
    return n;
}

static void bt_info_field(const char *info, const char *key, char *b, size_t sz)
{
    char k[64]; const char *p;
    snprintf(k, sizeof k, "\t%s: ", key);
    *b = 0;
    if ((p = strstr(info, k))) snprintf(b, sz, "%.*s", (int)strcspn(p + strlen(k), "\n"), p + strlen(k));
}

static void s_bluetooth_ids(srs_out *o)
{
    static const int t[] = { 170, 330, 560, 700 };
    char hci[16][32], b[256];
    btdev dv[32];
    int nh = bt_hcis(hci, 16), nd, i;
    so_siv_header(o, "Bluetooth IDs");
    so_tabs(o, t, 4);
    so_row(o, 5, (const char *const[]){ "Device", "Address", "Name", "Modalias / ID", "Class / Icon" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < nh; i++) {
        char p[300], ma[128] = "-";
        snprintf(p, sizeof p, "/sys/class/bluetooth/%s/device/modalias", hci[i]);
        rd_str(p, ma, sizeof ma);
        if (!strncmp(ma, "usb:", 4) && strlen(ma) > 19) ma[19] = 0;    /* usb:vXXXXpXXXXdXXXX */
        so_row(o, 5, (const char *const[]){ hci[i], "(controller)", "", ma, "" }, (const int[]){ C_TEAL, C_GRAY, C_BLACK, C_PURPLE, C_GRAY });
    }
    nd = bt_devices(dv, 32);
    for (i = 0; i < nd; i++) {
        char *info = CMD("bluetoothctl", "info", dv[i].mac), mod[128] = "", cls[64] = "", icon[64] = "", ci[160];
        if (info) {
            strip_ansi(info);
            bt_info_field(info, "Modalias", mod, sizeof mod);
            bt_info_field(info, "Class", cls, sizeof cls);
            bt_info_field(info, "Icon", icon, sizeof icon);
            free(info);
        }
        snprintf(ci, sizeof ci, "%s %s", cls, icon);
        so_row(o, 5, (const char *const[]){ "paired device", dv[i].mac, dv[i].name, *mod ? mod : "-", ci },
               (const int[]){ C_ORANGE, C_PURPLE, C_BLACK, C_VALUE, C_GRAY });
    }
    snprintf(b, sizeof b, "%d controller(s), %d known device(s)  (bluetoothctl devices / info; no discovery)", nh, nd);
    so_par(o);
    so_note(o, b);
}

static void s_blue_tree(srs_out *o)
{
    static const int t[] = { 260, 420 };
    char *l, *line, *save;
    btdev dv[32];
    int nd, i;
    so_siv_header(o, "Bluetooth Device Tree");
    so_tabs(o, t, 2);
    l = CMD("bluetoothctl", "list");
    if (!l || !*l) {
        char hci[16][32];
        int nh = bt_hcis(hci, 16);
        free(l);
        for (i = 0; i < nh; i++) { so_b(o, C_TEAL, hci[i]); so_par(o); }
        so_note(o, nh ? "bluetoothd not answering; devices unknown." : "No Bluetooth controllers.");
        return;
    }
    strip_ansi(l);
    for (line = strtok_r(l, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) { so_b(o, C_TEAL, line); so_par(o); }
    free(l);
    nd = bt_devices(dv, 32);
    for (i = 0; i < nd; i++) {
        char *info = CMD("bluetoothctl", "info", dv[i].mac), pa[16] = "", co[16] = "", tr[16] = "", icon[64] = "", st[96], node[200];
        if (info) {
            strip_ansi(info);
            bt_info_field(info, "Paired", pa, sizeof pa);
            bt_info_field(info, "Connected", co, sizeof co);
            bt_info_field(info, "Trusted", tr, sizeof tr);
            bt_info_field(info, "Icon", icon, sizeof icon);
            free(info);
        }
        snprintf(node, sizeof node, "  %s %s", i == nd - 1 ? "`-" : "|-", dv[i].name);
        snprintf(st, sizeof st, "%s%s%s", !strcmp(pa, "yes") ? "paired " : "", !strcmp(tr, "yes") ? "trusted " : "", !strcmp(co, "yes") ? "CONNECTED" : "");
        so_row(o, 3, (const char *const[]){ node, dv[i].mac, *icon ? icon : "-" }, (const int[]){ C_BLACK, C_PURPLE, C_ORANGE });
        if (*st) { so_t(o, C_DEF, "        "); so_t(o, !strcmp(co, "yes") ? C_GREEN : C_GRAY, st); so_par(o); }
    }
    if (!nd) so_note(o, "No known devices.");
}

/* ======================================================================
 * SMB / NetBIOS: [browsers] [domains] [servers] [lm-setup]
 * ====================================================================== */
typedef struct { char name[64], comment[128], domain[64]; unsigned long type; } brent;
static int read_browse(brent *v, int max)
{
    char *all, *line, *save;
    int n = 0;
    if (rd_all("/var/cache/samba/browse.dat", &all) < 0 && rd_all("/var/lib/samba/browse.dat", &all) < 0) return -1;
    for (line = strtok_r(all, "\n", &save); line && n < max; line = strtok_r(NULL, "\n", &save)) {
        char ty[32];
        if (sscanf(line, " \"%63[^\"]\" %31s \"%127[^\"]\" \"%63[^\"]\"", v[n].name, ty, v[n].comment, v[n].domain) < 2) {
            if (sscanf(line, " \"%63[^\"]\" %31s \"\" \"%63[^\"]\"", v[n].name, ty, v[n].domain) < 2) continue;
            v[n].comment[0] = 0;
        }
        v[n].type = strtoul(ty, NULL, 16);
        n++;
    }
    free(all);
    return n;
}

static void sv_type(unsigned long t, char *b, size_t sz)
{
    static const struct { unsigned long bit; const char *s; } bits[] = {
        { 0x1, "Workstation" }, { 0x2, "Server" }, { 0x4, "SQL" }, { 0x8, "DC" }, { 0x10, "BDC" }, { 0x20, "Time" },
        { 0x80, "Novell" }, { 0x100, "Member" }, { 0x200, "PrintQ" }, { 0x400, "Dial-in" }, { 0x800, "Unix" },
        { 0x1000, "NT" }, { 0x2000, "WfW" }, { 0x8000, "NT Server" }, { 0x10000, "Potential Browser" },
        { 0x20000, "Backup Browser" }, { 0x40000, "Master Browser" }, { 0x80000, "Domain Master" },
        { 0x400000, "Win95+" }, { 0x80000000UL, "Domain Enum" }, { 0, NULL } };
    int i;
    *b = 0;
    for (i = 0; bits[i].s; i++)
        if (t & bits[i].bit) {
            if (*b) strncat(b, ", ", sz - strlen(b) - 1);
            strncat(b, bits[i].s, sz - strlen(b) - 1);
        }
}

static char *testparm_param(const char *name)
{
    char arg[96], *r;
    snprintf(arg, sizeof arg, "--parameter-name=%s", name);
    r = CMD("testparm", "-s", "--suppress-prompt", arg);
    if (r) { r[strcspn(r, "\n")] = 0; trim(r); }
    return r;
}

static void browse_table(srs_out *o, int which) /* 0 all, 1 masters, 2 domains */
{
    static const int t[] = { 160, 300, 560 };
    brent v[256];
    int n = read_browse(v, 256), i, cnt = 0;
    so_tabs(o, t, 3);
    if (n < 0) { so_note(o, "nmbd browse list (/var/cache/samba/browse.dat) not present - nmbd is not running."); return; }
    so_row(o, 4, (const char *const[]){ which == 2 ? "Domain / Workgroup" : "Name", "Workgroup", "Type", "Comment" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < n; i++) {
        char ty[400];
        int isdom = (v[i].type & 0x80000000UL) != 0;
        if (which == 1 && !(v[i].type & 0x40000)) continue;
        if (which == 2 && !isdom) continue;
        sv_type(v[i].type, ty, sizeof ty);
        so_row(o, 4, (const char *const[]){ v[i].name, v[i].domain, ty, v[i].comment }, (const int[]){ isdom ? C_TEAL : C_BLACK, C_PURPLE, C_ORANGE, C_GRAY });
        cnt++;
    }
    if (!cnt) so_note(o, "none in nmbd's browse list");
    so_par(o);
    so_note(o, "From nmbd's cached browse list (/var/cache/samba/browse.dat); srs sends no NetBIOS queries.");
}

static void s_browsers(srs_out *o)
{
    static const int tk[] = { 190 };
    static const char *p[] = { "workgroup", "local master", "preferred master", "domain master", "os level", NULL };
    int i;
    so_siv_header(o, "Network Browse Masters");
    so_tabs(o, tk, 1);
    so_kv(o, "nmbd", proc_running("nmbd") ? C_GREEN : C_GRAY, proc_running("nmbd") ? "running" : "not running");
    for (i = 0; p[i]; i++) { char *v = testparm_param(p[i]); if (v && *v) so_kv(o, p[i], C_VALUE, v); free(v); }
    so_par(o);
    browse_table(o, 1);
}

static void s_domains(srs_out *o)
{
    static const int tk[] = { 190 };
    resolv rv;
    char dom[256] = "", *wg = testparm_param("workgroup"), *rl = testparm_param("realm"), *sec = testparm_param("security");
    so_siv_header(o, "Domains / Workgroups");
    so_tabs(o, tk, 1);
    get_resolv(&rv);
    if (getdomainname(dom, sizeof dom - 1) || !strcmp(dom, "(none)")) *dom = 0;
    so_kv(o, "SMB workgroup", C_BLACK, wg && *wg ? wg : "-");
    so_kv(o, "Kerberos realm", C_PURPLE, rl && *rl ? rl : "-");
    so_kv(o, "Samba security", C_ORANGE, sec && *sec ? sec : "-");
    so_kv(o, "NIS domain", C_VALUE, *dom ? dom : "-");
    so_kv(o, "DNS domain", C_VALUE, *rv.domain ? rv.domain : "-");
    so_kv(o, "DNS search list", C_VALUE, *rv.search ? rv.search : "-");
    so_kv(o, "AD/realmd join", C_GRAY, path_exists("/etc/krb5.keytab") ? "keytab present" : "not joined");
    free(wg); free(rl); free(sec);
    so_par(o);
    browse_table(o, 2);
}

static void s_servers(srs_out *o)
{
    static const int t[] = { 70, 300, 370, 520 };
    static const struct { const char *path; int v6, udp; } f[] = { { "/proc/net/tcp", 0, 0 }, { "/proc/net/tcp6", 1, 0 }, { "/proc/net/udp", 0, 1 }, { "/proc/net/udp6", 1, 1 } };
    sockent *v = malloc(8192 * sizeof *v);
    int k, i, n;
    if (!v) return;
    build_inomap();
    so_siv_header(o, "Network Servers");
    so_b(o, C_TEAL, "Local listening services");
    so_par(o);
    so_tabs(o, t, 4);
    so_row(o, 5, (const char *const[]){ "Proto", "Bound Address", "Port", "Service", "Process (PID)" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (k = 0; k < 4; k++) {
        n = read_socks(f[k].path, f[k].v6, v, 8192);
        qsort(v, (size_t)n, sizeof *v, sock_cmp);
        for (i = 0; i < n; i++) {
            char pp[16], sv[32], pr[96], proto[8];
            int loop = !strcmp(v[i].la, "127.0.0.1") || !strcmp(v[i].la, "::1") || !strncmp(v[i].la, "127.", 4);
            if (f[k].udp ? v[i].st != 7 : v[i].st != 10) continue;
            snprintf(pp, sizeof pp, "%u", v[i].lp);
            svc_name(v[i].lp, f[k].udp ? "udp" : "tcp", sv, sizeof sv);
            ino_proc(v[i].ino, pr, sizeof pr);
            snprintf(proto, sizeof proto, "%s%s", f[k].udp ? "UDP" : "TCP", f[k].v6 ? "6" : "");
            so_row(o, 5, (const char *const[]){ proto, v[i].la, pp, sv, pr },
                   (const int[]){ C_ORANGE, loop ? C_GRAY : C_VALUE, C_BLACK, C_PURPLE, C_ORANGE });
        }
    }
    free(v);
    so_par(o);
    so_b(o, C_TEAL, "SMB servers seen on the LAN");
    so_par(o);
    browse_table(o, 0);
}

static void s_lm_setup(srs_out *o)
{
    char *tp = CMD("testparm", "-s", "--suppress-prompt"), *line, *save;
    so_siv_header(o, "LAN Manager Share Setup");
    if (!tp || !*tp) { free(tp); so_note(o, "Samba is not installed or smb.conf is unreadable."); return; }
    for (line = strtok_r(tp, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *eq = strchr(line, '=');
        if (*line == '[') { so_par(o); so_b(o, C_TEAL, line); so_par(o); continue; }
        if (eq) {
            char k[128];
            snprintf(k, sizeof k, "%.*s", (int)(eq - line), line);
            trim(k);
            so_t(o, C_DEF, "    ");
            so_t(o, C_LABEL, k);
            so_t(o, C_DEF, " = ");
            {
                int hide = secret_name(k) && strcmp(eq + 2, "Yes") && strcmp(eq + 2, "No");
                so_t(o, hide ? C_GRAY : C_VALUE, hide ? "****" : eq + 2);
            }
            so_par(o);
        }
    }
    free(tp);
    so_par(o);
    so_note(o, "Effective smb.conf as parsed by `testparm -s` (defaults omitted).");
}

/* ======================================================================
 * [my-ip-address] [my-browser] [net-names] [modems] [modem-api]
 * ====================================================================== */
static void s_my_ip(srs_out *o)
{
    static const int tk[] = { 200 };
    ipaddr *av = malloc(MAXADDR * sizeof *av);
    char gw[128], *src;
    int na, i;
    if (!av) return;
    na = get_addrs(av, MAXADDR);
    so_siv_header(o, "My IP Address");
    so_tabs(o, tk, 1);
    src = CMD("ip", "-o", "route", "get", "192.0.2.1");   /* TEST-NET-1: route lookup only, nothing is sent */
    if (src) {
        char *p = strstr(src, " src "), a[64] = "";
        if (p) sscanf(p + 5, "%63s", a);
        so_kv(o, "Outgoing IPv4 source", C_BLACK, *a ? a : "-");
        free(src);
    }
    gw4_of(NULL, gw, sizeof gw);
    so_kv(o, "Default gateway", C_TEAL, *gw ? gw : "none");
    so_kv(o, "Public (WAN) address", C_GRAY, "not looked up - srs makes no internet requests");
    so_par(o);
    so_b(o, C_TEAL, "Global-scope addresses");
    so_par(o);
    for (i = 0; i < na; i++) {
        char b[160];
        if (strcmp(av[i].scope, "global")) continue;
        snprintf(b, sizeof b, "%s/%d  %s", av[i].addr, av[i].plen, av[i].flags);
        so_kv(o, av[i].dev, av[i].v6 ? C_PURPLE : C_VALUE, b);
    }
    free(av);
}

static void s_my_browser(srs_out *o)
{
    static const int tk[] = { 220 };
    char *d = CMD("xdg-settings", "get", "default-web-browser"), *h = CMD("xdg-mime", "query", "default", "x-scheme-handler/https");
    so_siv_header(o, "My Browser");
    so_tabs(o, tk, 1);
    if (d) d[strcspn(d, "\n")] = 0;
    if (h) h[strcspn(h, "\n")] = 0;
    so_kv(o, "Default web browser", C_BLACK, d && *d ? d : "-");
    so_kv(o, "https handler", C_VALUE, h && *h ? h : "-");
    so_kv(o, "$BROWSER", C_VALUE, getenv("BROWSER") ? getenv("BROWSER") : "-");
    so_kv(o, "IP geolocation", C_GRAY, "not looked up - srs makes no internet requests");
    free(d); free(h);
}

static void s_net_names(srs_out *o)
{
    static const int t[] = { 300, 620, 720 };
    static const int tk[] = { 200 };
    static const char *fs[] = { "cifs", "smb3", "nfs", "nfs4", "fuse.sshfs", "9p", "davfs", "fuse.rclone", "fuse.gvfsd-fuse", "ceph", "glusterfs", NULL };
    static const char *tools[][2] = { { "/usr/bin/smbclient", "Samba client (SMB/CIFS)" }, { "/usr/bin/mount.cifs", "CIFS kernel mounts" }, { "/usr/bin/mount.nfs", "NFS client" },
                                      { "/usr/bin/sshfs", "SSHFS" }, { "/usr/lib/gvfsd-smb", "GVfs SMB" }, { "/usr/lib/kf6/kiod6", "KIO (smb:/ sftp:/ fish:/)" },
                                      { "/usr/bin/lpstat", "CUPS network printing" }, { NULL, NULL } };
    char *all, *line, *save;
    int i, cnt = 0;
    so_siv_header(o, "Network Provider Names");
    so_tabs(o, tk, 1);
    so_b(o, C_TEAL, "Network providers available");
    so_par(o);
    for (i = 0; tools[i][0]; i++) if (path_exists(tools[i][0])) so_kv(o, tools[i][1], C_VALUE, tools[i][0]);
    so_par(o);
    so_b(o, C_TEAL, "Mounted network file systems");
    so_par(o);
    so_tabs(o, t, 3);
    if (rd_all("/proc/self/mounts", &all) >= 0) {
        for (line = strtok_r(all, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char src[512], ms[512], mp[512], ty[64];
            int k;
            if (sscanf(line, "%511s %511s %63s", src, mp, ty) != 3) continue;
            for (k = 0; fs[k]; k++) if (!strcmp(ty, fs[k])) break;
            if (!fs[k]) continue;
            unescape3(src, 8);
            unescape3(mp, 8);
            mask_userinfo(src, ms, sizeof ms);
            so_row(o, 3, (const char *const[]){ ms, mp, ty }, (const int[]){ C_VALUE, C_BLACK, C_ORANGE });
            cnt++;
        }
        free(all);
    }
    if (!cnt) so_note(o, "none mounted");
}

static void s_modems(srs_out *o)
{
    int rc = -1;
    char *m = cmd((const char *const[]){ "mmcli", "-L", NULL }, &rc);
    so_siv_header(o, "Modems");
    if (rc < 0 || !m) so_note(o, "ModemManager (mmcli) not available.");
    else if (!*m || strstr(m, "No modems")) so_note(o, "ModemManager reports no modems.");
    else { strip_ansi(m); so_lines(o, C_VALUE, m); }
    free(m);
    so_note(o, "Source: `mmcli -L` (ModemManager). Serial/TAPI modem enumeration has no Linux equivalent.");
}

/* ======================================================================
 * table
 * ====================================================================== */
const srs_item sec_network[] = {
    { "network", "windows", "Network", B_LINUX, 0, "ip addr; /sys/class/net", s_network, 0, "network" },
    { "adapters", "windows", "Network adapters", B_LINUX, 0, "/sys/class/net; ip -o addr; nmcli; /proc/net/igmp", s_adapters, 0, "network" },
    { "ip-address", "windows", "IP address", B_LINUX, 0, "ip -o addr", s_ip_address, 0, "network" },
    { "mac-address", "windows", "MAC addresses", B_LINUX, 0, "/sys/class/net/*/address; ethtool -P; hwdata oui.txt", s_mac_address, 0, "network" },
    { "net-cards", "windows", "Network cards", B_LINUX, 0, "/sys/class/net/*/device; lspci -nn; ethtool -i", s_net_cards, 0, "network" },
    { "interface", "windows", "Network interface statistics", B_LINUX, 0, "/sys/class/net/*/statistics", s_interface, 0, "network" },
    { "counters", "windows", "TCP/IP counters", B_LINUX, 0, "/sys/class/net/*/statistics deltas; /proc/net/snmp", s_counters, 1000, "network" },
    { "arp-table", "windows", "ARP table", B_LINUX, 0, "/proc/net/arp; ip -6 neigh; avahi-browse -c", s_arp, 0, "network" },
    { "routing", "windows", "Routing table", B_LINUX, 0, "/proc/net/route; ip -6 route", s_routing, 0, "network" },
    { "tcp-ip4-ports", "windows", "TCP V4 ports", B_LINUX, 0, "/proc/net/tcp; /proc/*/fd", s_tcp4, 0, "network" },
    { "tcp-ip6-ports", "windows", "TCP V6 ports", B_LINUX, 0, "/proc/net/tcp6; /proc/*/fd", s_tcp6, 0, "network" },
    { "udp-ip4-ports", "windows", "UDP V4 ports", B_LINUX, 0, "/proc/net/udp; /proc/*/fd", s_udp4, 0, "network" },
    { "udp-ip6-ports", "windows", "UDP V6 ports", B_LINUX, 0, "/proc/net/udp6; /proc/*/fd", s_udp6, 0, "network" },
    { "protocols", "windows", "Network protocols", B_LINUX, 0, "/proc/net/protocols; /etc/protocols", s_protocols, 0, "network" },
    { "transports", "windows", "Network transports", B_LINUX, 0, "/proc/net/sockstat{,6}", s_transports, 0, "network" },
    { "dns-names", "windows", "DNS names", B_LINUX, 0, "gethostname; testparm; hostnamectl", s_dns_names, 0, "network" },
    { "dns-ports", "windows", "DNS ports", B_LINUX, 0, "/proc/net/{tcp,udp}{,6}; /etc/resolv.conf", s_dns_ports, 0, "network" },
    { "dns-usage", "windows", "DNS usage", B_LINUX, 0, "resolvectl statistics; resolv.conf; nsswitch.conf", s_dns_usage, 0, "network" },
    { "dns-cache", "windows", "DNS cache", B_LINUX, 1, "resolvectl show-cache (root)", s_dns_cache, 0, "network" },
    { "proxy", "windows", "Proxy config", B_LINUX, 0, "environment; /etc/environment; kioslaverc; gsettings", s_proxy, 0, "network" },
    { "firewall", "windows", "Firewall", B_LINUX, 1, "firewall-cmd (read-only queries); nft (root)", s_firewall, 0, "network" },
    { "profiles", "windows", "Network profiles", B_LINUX, 0, "nmcli connection show", s_profiles, 0, "network" },
    { "wlan", "windows", "WLAN profiles", B_LINUX, 1, "nmcli connection show (no secrets)", s_wlan, 0, "network" },
    { "wi-fi", "windows", "Wi-Fi SSIDs", B_LINUX, 0, "iw dev; /proc/net/wireless; nmcli wifi list --rescan no", s_wifi, 0, "network" },
    { "bluetooth", "windows", "Bluetooth radios", B_LINUX, 0, "/sys/class/bluetooth; bluetoothctl show", s_bluetooth, 0, "network" },
    { "bluetooth-ids", "windows", "Bluetooth IDs", B_LINUX, 0, "/sys/class/bluetooth/*/device; bluetoothctl info", s_bluetooth_ids, 0, "network" },
    { "blue-tree", "windows", "Bluetooth device tree", B_LINUX, 0, "bluetoothctl list/devices/info", s_blue_tree, 0, "network" },
    { "browsers", "windows", "Network browse masters", B_LINUX, 0, "testparm; /var/cache/samba/browse.dat", s_browsers, 0, "network" },
    { "domains", "windows", "Domains / workgroups", B_LINUX, 0, "testparm; resolv.conf; browse.dat", s_domains, 0, "network" },
    { "servers", "windows", "Servers", B_LINUX, 0, "/proc/net listeners; browse.dat", s_servers, 0, "network" },
    { "lm-setup", "windows", "LAN Manager share setup", B_LINUX, 0, "testparm -s", s_lm_setup, 0, "network" },
    { "my-ip-address", "windows", "My IP address", B_LINUX, 0, "ip addr; ip route get (local)", s_my_ip, 0, "network" },
    { "my-browser", "windows", "My browser / IP location", B_LINUX, 0, "xdg-settings; xdg-mime", s_my_browser, 0, "network" },
    { "net-names", "windows", "Network provider names", B_WINONLY, 0, "client tools; /proc/self/mounts", s_net_names, 0, "network" },
    { "modems", "windows", "Modems", B_ABSENT, 0, "mmcli -L (no modems)", s_modems, 0, "network" },
    { "modem-api", "windows", "Modem API", B_ABSENT, 0, "mmcli -L (no modems)", s_modems, 0, "network" },
    { NULL, NULL, NULL, B_LINUX, 0, NULL, NULL, 0, NULL }
};
