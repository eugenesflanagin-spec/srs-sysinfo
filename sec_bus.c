/* sec_bus.c - SIV bus sections on Linux: [pci-bus] [pci-dev] [pci-all] [pci-bars] [pci-info]
 * [chipsets] [usb-bus] [usb-tree] [usb-dev] [usb-all] [usb-names] [usb-owner] [usb-info] [hid-*]
 * [pnp*] [isa-bus] [devices] [enum] [class] [properties] [interfaces] [int-*] [smb*] [serial]
 * [ports] [io-*] [sys-res] [sys-hal] [pci-hal] [pci-mmio] [pm1-area] [thunder] ...
 * Sources: /sys/bus/{pci,usb,hid,pnp,acpi,i2c,gpio}, /sys/class, udevadm info --export-db,
 * /proc/{ioports,iomem,interrupts,dma,tty/drivers}, hwdata pci.ids/usb.ids/pnp.ids via the shared
 * srs_db lookup (V:D:S:s:RR chain). Read-only: no /dev/i2c-*, /dev/hidraw*, /dev/port, /dev/mem,
 * no USB device opens, no PCI config space reads (full config only from the root cache). */
#define _GNU_SOURCE
#include "sec.h"
#include "srs.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NEEDS_ROOT "Needs root: Menu > Run as root (srs-root)"

static const char *arg(srs_out *o) { return o->arg && *o->arg ? o->arg : NULL; }
static const char *bname(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }
static int at(const char *dir, const char *f, char *b, size_t sz) { return rd_fmt(b, sz, "%s/%s", dir, f); }
static unsigned athex(const char *dir, const char *f) { char b[64]; return at(dir, f, b, sizeof b) ? (unsigned)strtoul(b, NULL, 16) : 0; }
static void link_base(const char *dir, const char *f, char *b, size_t sz)
{
    char p[1100], t[1024];
    ssize_t n;
    snprintf(p, sizeof p, "%s/%s", dir, f);
    *b = 0;
    if ((n = readlink(p, t, sizeof t - 1)) > 0) { t[n] = 0; snprintf(b, sz, "%s", bname(t)); }
}
static int is_pci_slot(const char *s, size_t l)
{
    return l == 12 && s[4] == ':' && s[7] == ':' && s[10] == '.' && isxdigit((unsigned char)s[0]);
}
static void mod_of(const char *dir, char *b, size_t sz)   /* driver's module or built-in */
{
    char p[1100];
    *b = 0;
    snprintf(p, sizeof p, "%s/driver", dir);
    if (!path_exists(p)) return;
    link_base(p, "module", b, sz);
    if (!*b) snprintf(b, sz, "built-in");
}

/* ======================================================================
 * ID databases (shared srs_db lookup) and class names from the .ids "C" section
 * ====================================================================== */
static srs_db g_db[2];
static int g_dbok[2] = { -1, -1 };
static const srs_db *db(enum srs_bus b)
{
    if (g_dbok[b] < 0) g_dbok[b] = srs_db_load(&g_db[b], b) == 0;
    return g_dbok[b] ? &g_db[b] : NULL;
}

/* key like srs_devices(): VVVV:DDDD[:SSSS:ssss][:RR] -> "Vendor  Device" */
static void id_name(enum srs_bus b, const char *key, char *vendor, size_t vsz, char *dev, size_t dsz, int *rung)
{
    srs_result r;
    char err[160];
    const srs_db *d = db(b);
    *vendor = *dev = 0;
    if (rung) *rung = -1;
    if (!d || srs_resolve(d, key, MODE_TREE, &r, err, sizeof err)) return;
    snprintf(vendor, vsz, "%s", r.vendor);
    if (rung) *rung = r.matched;
    if (r.matched >= 0 && r.matched < r.nchain - 1) snprintf(dev, dsz, "%s", r.resolved);
    else if (*r.stock) snprintf(dev, dsz, "%s", r.stock);
}

typedef struct { unsigned code; int lvl; char name[96]; } clsrec;
static clsrec *g_cls[2];
static int g_ncls[2] = { -1, -1 };
static void load_classes(enum srs_bus b)
{
    char path[1100], line[512];
    FILE *f;
    int cap = 0, in = 0;
    unsigned cc = 0, sc = 0;
    g_ncls[b] = 0;
    if (db(b)) snprintf(path, sizeof path, "%s", g_db[b].path);
    else snprintf(path, sizeof path, "%s/%s", plat_hwdata_dir(), b == BUS_PCI ? "pci.ids" : "usb.ids");
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof line, f)) {
        clsrec r;
        char *p = line;
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == 'C' && line[1] == ' ') { in = 1; cc = (unsigned)strtoul(line + 2, &p, 16); r.code = cc << 16; r.lvl = 0; }
        else if (!in || !*line || *line == '#') { if (in && *line && *line != '\t' && *line != '#') in = 0; continue; }
        else if (line[0] == '\t' && line[1] == '\t') { r.code = cc << 16 | sc << 8 | (unsigned)strtoul(line + 2, &p, 16); r.lvl = 2; }
        else if (line[0] == '\t') { sc = (unsigned)strtoul(line + 1, &p, 16); r.code = cc << 16 | sc << 8; r.lvl = 1; }
        else { in = 0; continue; }
        while (*p == ' ') p++;
        snprintf(r.name, sizeof r.name, "%s", p);
        if (g_ncls[b] >= cap) {
            clsrec *n = realloc(g_cls[b], sizeof *n * (size_t)(cap = cap ? cap * 2 : 256));
            if (!n) break;
            g_cls[b] = n;
        }
        g_cls[b][g_ncls[b]++] = r;
    }
    fclose(f);
}
/* most specific name for class/subclass/progif (lvl 0..2 = how deep to look) */
static const char *cls_name(enum srs_bus b, unsigned cc, unsigned sc, unsigned pi, int deep)
{
    int i, best = -1, bl = -1;
    if (g_ncls[b] < 0) load_classes(b);
    for (i = 0; i < g_ncls[b]; i++) {
        const clsrec *r = &g_cls[b][i];
        unsigned want = r->lvl == 0 ? cc << 16 : r->lvl == 1 ? (cc << 16 | sc << 8) : (cc << 16 | sc << 8 | pi);
        if (r->code == want && r->lvl <= deep && r->lvl > bl) { best = i; bl = r->lvl; }
    }
    return best >= 0 ? g_cls[b][best].name : "Unknown class";
}

/* ======================================================================
 * PCI devices from sysfs
 * ====================================================================== */
typedef struct {
    char slot[16], parent[16], rpath[512];
    int depth;
    unsigned ven, dev, sven, sdev, rev, cls;
    char driver[48], module[48], cur_sp[32], max_sp[32], cur_w[8], max_w[8], key[40], vname[200], dname[256];
    char modalias[128], iommu[16], power[16], cpus[64];
    long irq, numa;
    int nbar;
    unsigned long long bs[7], be[7], bf[7];
} pcid;
static pcid *g_pci;
static int g_npci = -1;
static int cmp_pci(const void *a, const void *b) { return strcmp(((const pcid *)a)->rpath, ((const pcid *)b)->rpath); }

static void pci_load(void)
{
    char **e, dir[512], b[256];
    int n, i;
    if (g_npci >= 0) return;
    g_npci = 0;
    if ((n = dir_list("/sys/bus/pci/devices", &e)) <= 0) return;
    g_pci = calloc((size_t)n, sizeof *g_pci);
    for (i = 0; g_pci && i < n; i++) {
        pcid *d = &g_pci[g_npci];
        char rp[PATH_MAX], *s, *tok, *sv, prev[16] = "";
        FILE *f;
        int k = 0;
        snprintf(dir, sizeof dir, "/sys/bus/pci/devices/%s", e[i]);
        snprintf(d->slot, sizeof d->slot, "%s", e[i]);
        if (!realpath(dir, rp)) snprintf(rp, sizeof rp, "%s", dir);
        snprintf(d->rpath, sizeof d->rpath, "%s", rp);
        for (s = rp, tok = strtok_r(s, "/", &sv); tok; tok = strtok_r(NULL, "/", &sv))
            if (is_pci_slot(tok, strlen(tok))) {
                if (strcmp(tok, d->slot)) snprintf(prev, sizeof prev, "%s", tok);
                d->depth++;
            }
        d->depth = d->depth ? d->depth - 1 : 0;
        snprintf(d->parent, sizeof d->parent, "%s", prev);
        d->ven = athex(dir, "vendor"); d->dev = athex(dir, "device");
        d->sven = athex(dir, "subsystem_vendor"); d->sdev = athex(dir, "subsystem_device");
        d->rev = athex(dir, "revision"); d->cls = athex(dir, "class");
        link_base(dir, "driver", d->driver, sizeof d->driver);
        mod_of(dir, d->module, sizeof d->module);
        link_base(dir, "iommu_group", d->iommu, sizeof d->iommu);
        at(dir, "current_link_speed", d->cur_sp, sizeof d->cur_sp);
        at(dir, "max_link_speed", d->max_sp, sizeof d->max_sp);
        at(dir, "current_link_width", d->cur_w, sizeof d->cur_w);
        at(dir, "max_link_width", d->max_w, sizeof d->max_w);
        at(dir, "modalias", d->modalias, sizeof d->modalias);
        at(dir, "power_state", d->power, sizeof d->power);
        at(dir, "local_cpulist", d->cpus, sizeof d->cpus);
        d->irq = -1; d->numa = -1;
        if (at(dir, "irq", b, sizeof b)) d->irq = atol(b);
        if (at(dir, "numa_node", b, sizeof b)) d->numa = atol(b);
        snprintf(b, sizeof b, "%s/resource", dir);
        if ((f = fopen(b, "r"))) {
            while (k < 7 && fscanf(f, "%llx %llx %llx", &d->bs[k], &d->be[k], &d->bf[k]) == 3) k++;
            fclose(f);
        }
        d->nbar = k;
        snprintf(d->key, sizeof d->key, "%04X:%04X", d->ven, d->dev);
        if (d->sven) snprintf(d->key + 9, sizeof d->key - 9, ":%04X:%04X", d->sven, d->sdev);
        snprintf(d->key + strlen(d->key), sizeof d->key - strlen(d->key), ":%02X", d->rev);
        id_name(BUS_PCI, d->key, d->vname, sizeof d->vname, d->dname, sizeof d->dname, NULL);
        if (!*d->vname) snprintf(d->vname, sizeof d->vname, "Vendor %04X", d->ven);
        if (!*d->dname) snprintf(d->dname, sizeof d->dname, "Device %04X", d->dev);
        g_npci++;
    }
    dir_free(e, n);
    if (g_pci) qsort(g_pci, (size_t)g_npci, sizeof *g_pci, cmp_pci);
}
static const pcid *pci_find(const char *slot)
{
    int i;
    char s[32];
    pci_load();
    if (!slot) return NULL;
    snprintf(s, sizeof s, "%s%s", strlen(slot) == 7 ? "0000:" : "", slot);   /* accept 03:00.0 */
    for (i = 0; i < g_npci; i++) if (!strcasecmp(g_pci[i].slot, s)) return &g_pci[i];
    return NULL;
}
static void pci_loc(const pcid *d, char *b, size_t sz)   /* SIV style [ bus - dev - fn ] */
{
    unsigned bus = 0, dv = 0, fn = 0;
    sscanf(d->slot + 5, "%x:%x.%x", &bus, &dv, &fn);
    snprintf(b, sz, "[ %u - %02u - %u ]", bus, dv, fn);
}
static void pci_link(const pcid *d, char *b, size_t sz)
{
    *b = 0;
    if (*d->cur_sp && strncmp(d->cur_sp, "Unknown", 7))
        snprintf(b, sz, "x%s %s (max x%s %s)", d->cur_w, d->cur_sp, d->max_w, d->max_sp);
}
static const char *pci_cls(const pcid *d) { return cls_name(BUS_PCI, d->cls >> 16, (d->cls >> 8) & 0xFF, d->cls & 0xFF, 1); }

static void bar_desc(const pcid *d, int k, char *b, size_t sz)
{
    unsigned long long fl = d->bf[k], len = d->be[k] - d->bs[k] + 1;
    char sz2[32];
    fmt_bytes((double)len, sz2, sizeof sz2);
    snprintf(b, sz, "%s%s%s %s", fl & 0x100 ? "I/O" : "Memory", fl & 0x2000 ? " Prefetch" : "",
             (fl & 0x100000) ? " 64-bit" : "", sz2);
}

static void pci_detail(srs_out *o, const pcid *d)
{
    char b[512], loc[48];
    int k;
    pci_loc(d, loc, sizeof loc);
    so_b(o, C_BLACK, d->slot); so_tab(o); so_b(o, C_VALUE, d->vname); so_t(o, C_VALUE, "  "); so_b(o, C_VALUE, d->dname); so_par(o);
    so_kv(o, "Location", C_TEAL, loc);
    so_kvf(o, "IDs", C_PURPLE, "VEN_%04X DEV_%04X SUBSYS_%04X%04X REV_%02X", d->ven, d->dev, d->sdev, d->sven, d->rev);
    so_kv(o, "Lookup key", C_PURPLE, d->key);
    so_kvf(o, "Class", C_VALUE, "%06X  %s / %s", d->cls, cls_name(BUS_PCI, d->cls >> 16, 0, 0, 0), cls_name(BUS_PCI, d->cls >> 16, (d->cls >> 8) & 0xFF, d->cls & 0xFF, 2));
    if (d->sven) {
        char v[200], dn[256];
        snprintf(b, sizeof b, "%04X", d->sven);
        id_name(BUS_PCI, b, v, sizeof v, dn, sizeof dn, NULL);
        so_kvf(o, "Subsystem", C_VALUE, "%04X:%04X  %s", d->sven, d->sdev, *v ? v : "");
    }
    so_kv(o, "Parent bridge", C_VALUE, *d->parent ? d->parent : "root bus");
    snprintf(b, sizeof b, "%s%s%s%s", *d->driver ? d->driver : "none", *d->module ? " (module " : "", d->module, *d->module ? ")" : "");
    so_kv(o, "Driver", *d->driver ? C_GREEN : C_ORANGE, b);
    if (d->irq > 0) so_kvf(o, "IRQ", C_VALUE, "%ld", d->irq);
    if (d->numa >= -1) so_kvf(o, "NUMA node", C_VALUE, "%ld", d->numa);
    if (*d->cpus) so_kv(o, "Local CPUs", C_VALUE, d->cpus);
    pci_link(d, b, sizeof b);
    if (*b) so_kv(o, "PCIe link", C_TEAL, b);
    if (*d->power) so_kv(o, "Power state", C_VALUE, d->power);
    if (*d->iommu) so_kv(o, "IOMMU group", C_VALUE, d->iommu);
    for (k = 0; k < d->nbar; k++) {
        char lab[16], ds[96];
        if (!d->bf[k] && !d->be[k]) continue;
        snprintf(lab, sizeof lab, k == 6 ? "ROM" : "BAR %d", k);
        bar_desc(d, k, ds, sizeof ds);
        so_kvf(o, lab, C_VALUE, "0x%010llX - 0x%010llX  %s", d->bs[k], d->be[k], ds);
    }
    so_kv(o, "Modalias", C_GRAY, d->modalias);
    so_par(o);
}

/* ======================================================================
 * USB devices from sysfs
 * ====================================================================== */
typedef struct { char name[32]; int cls, sub, proto, neps; char driver[48], iface[96], modalias[128]; } usbif;
typedef struct {
    char name[32], devpath[32], speed[16], ver[16], power[16], mfr[96], prod[128], serial[64], ctrl[16];
    char key[16], vname[200], dname[256], quirks[16], removable[16], dir[64];
    int bus, devnum, depth, dcls, maxchild, nif, root;
    unsigned ven, pid, bcd;
    usbif ifs[16];
} usbd;
static usbd *g_usb;
static int g_nusb = -1;

static int cmp_devpath(const char *a, const char *b)
{
    while (*a || *b) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
        if (!*a || !*b) return *a ? 1 : *b ? -1 : 0;
    }
    return 0;
}
static int cmp_usb(const void *pa, const void *pb)
{
    const usbd *a = pa, *b = pb;
    if (a->bus != b->bus) return a->bus - b->bus;
    if (a->root != b->root) return b->root - a->root;
    return cmp_devpath(a->devpath, b->devpath);
}

static void usb_load(void)
{
    char **e, dir[128], b[256];
    int n, i;
    if (g_nusb >= 0) return;
    g_nusb = 0;
    if ((n = dir_list("/sys/bus/usb/devices", &e)) <= 0) return;
    g_usb = calloc((size_t)n, sizeof *g_usb);
    for (i = 0; g_usb && i < n; i++) {
        usbd *d = &g_usb[g_nusb];
        char rp[PATH_MAX], *tok, *sv, **ie;
        int ni, j;
        if (strchr(e[i], ':')) continue;
        snprintf(dir, sizeof dir, "/sys/bus/usb/devices/%s", e[i]);
        snprintf(d->dir, sizeof d->dir, "%s", dir);
        snprintf(d->name, sizeof d->name, "%s", e[i]);
        d->root = !strncmp(e[i], "usb", 3);
        if (at(dir, "busnum", b, sizeof b)) d->bus = atoi(b);
        if (at(dir, "devnum", b, sizeof b)) d->devnum = atoi(b);
        at(dir, "devpath", d->devpath, sizeof d->devpath);
        if (d->root) snprintf(d->devpath, sizeof d->devpath, "0");
        else { const char *p; d->depth = 1; for (p = d->devpath; *p; p++) d->depth += *p == '.'; }
        at(dir, "speed", d->speed, sizeof d->speed);
        at(dir, "version", d->ver, sizeof d->ver);
        { char *v = d->ver; while (*v == ' ') v++; memmove(d->ver, v, strlen(v) + 1); }
        at(dir, "bMaxPower", d->power, sizeof d->power);
        at(dir, "manufacturer", d->mfr, sizeof d->mfr);
        at(dir, "product", d->prod, sizeof d->prod);
        at(dir, "serial", d->serial, sizeof d->serial);
        at(dir, "quirks", d->quirks, sizeof d->quirks);
        at(dir, "removable", d->removable, sizeof d->removable);
        d->ven = athex(dir, "idVendor"); d->pid = athex(dir, "idProduct"); d->bcd = athex(dir, "bcdDevice");
        d->dcls = (int)athex(dir, "bDeviceClass");
        if (at(dir, "maxchild", b, sizeof b)) d->maxchild = atoi(b);
        if (realpath(dir, rp))
            for (tok = strtok_r(rp, "/", &sv); tok; tok = strtok_r(NULL, "/", &sv))
                if (is_pci_slot(tok, strlen(tok))) snprintf(d->ctrl, sizeof d->ctrl, "%s", tok);
        if (d->root && !*d->ctrl && realpath(dir, rp)) {   /* virtual host (vhci_hcd, dummy_hcd): platform parent */
            char *s = strrchr(rp, '/');
            if (s) { *s = 0; snprintf(d->ctrl, sizeof d->ctrl, "%s", bname(rp)); }
        }
        snprintf(d->key, sizeof d->key, "%04X:%04X", d->ven, d->pid);
        id_name(BUS_USB, d->key, d->vname, sizeof d->vname, d->dname, sizeof d->dname, NULL);
        if ((ni = dir_list(dir, &ie)) > 0) {
            for (j = 0; j < ni && d->nif < 16; j++) {
                usbif *f = &d->ifs[d->nif];
                char id[256], **ep;
                int ne, q;
                if (!strchr(ie[j], ':')) continue;   /* <dev>:<cfg>.<if>; root hubs use <bus>-0:1.0 */
                snprintf(id, sizeof id, "%s/%s", dir, ie[j]);
                { char t[300]; snprintf(t, sizeof t, "%s/bInterfaceClass", id); if (!path_exists(t)) continue; }
                snprintf(f->name, sizeof f->name, "%s", ie[j]);
                f->cls = (int)athex(id, "bInterfaceClass"); f->sub = (int)athex(id, "bInterfaceSubClass");
                f->proto = (int)athex(id, "bInterfaceProtocol");
                link_base(id, "driver", f->driver, sizeof f->driver);
                at(id, "interface", f->iface, sizeof f->iface);
                at(id, "modalias", f->modalias, sizeof f->modalias);
                if ((ne = dir_list(id, &ep)) > 0) { for (q = 0; q < ne; q++) f->neps += !strncmp(ep[q], "ep_", 3); dir_free(ep, ne); }
                d->nif++;
            }
            dir_free(ie, ni);
        }
        g_nusb++;
    }
    dir_free(e, n);
    if (g_usb) qsort(g_usb, (size_t)g_nusb, sizeof *g_usb, cmp_usb);
}
static const usbd *usb_find(const char *name)
{
    int i;
    usb_load();
    for (i = 0; name && i < g_nusb; i++) if (!strcmp(g_usb[i].name, name)) return &g_usb[i];
    return NULL;
}
static const char *usb_cls(int c, int s, int p, int deep) { return cls_name(BUS_USB, (unsigned)c, (unsigned)s, (unsigned)p, deep); }
static int usb_devclass(const usbd *d) { return d->dcls || !d->nif ? d->dcls : d->ifs[0].cls; }
static void usb_desc(const usbd *d, char *b, size_t sz)
{
    if (d->root) {
        const pcid *c = pci_find(d->ctrl);
        snprintf(b, sz, "%s - %s %s", *d->prod ? d->prod : "Root Hub", c ? c->vname : d->ctrl, c ? c->dname : "");
    } else if (*d->prod || *d->mfr)
        snprintf(b, sz, "%s%s%s", *d->mfr ? d->mfr : d->vname, (*d->mfr || *d->vname) ? " " : "", *d->prod ? d->prod : d->dname);
    else snprintf(b, sz, "%s %s", *d->vname ? d->vname : "Unknown vendor", *d->dname ? d->dname : "Unknown device");
}
static void usb_speed(const usbd *d, char *b, size_t sz)
{
    double v = atof(d->speed);
    const char *n = v >= 20000 ? "SuperSpeed+ 20Gbps" : v >= 10000 ? "SuperSpeed+" : v >= 5000 ? "SuperSpeed" :
                    v >= 480 ? "High Speed" : v >= 12 ? "Full Speed" : v > 0 ? "Low Speed" : "?";
    if (v >= 1000) snprintf(b, sz, "%gGbps %s", v / 1000, n); else snprintf(b, sz, "%gMbps %s", v, n);
}
static void usb_drivers(const usbd *d, char *b, size_t sz)
{
    int i;
    *b = 0;
    for (i = 0; i < d->nif; i++) {
        const char *dr = *d->ifs[i].driver ? d->ifs[i].driver : "(none)";
        if (strstr(b, dr)) continue;
        snprintf(b + strlen(b), sz - strlen(b), "%s%s", *b ? ", " : "", dr);
    }
}

static void usb_detail(srs_out *o, const usbd *d, int eps)
{
    char b[512], s[64];
    int i;
    usb_desc(d, b, sizeof b);
    so_b(o, C_BLACK, d->name); so_tab(o); so_b(o, C_VALUE, b); so_par(o);
    so_kvf(o, "Bus / Device", C_TEAL, "Bus %03d Device %03d  port path %s%s", d->bus, d->devnum, d->devpath, d->root ? " (root hub)" : "");
    so_kvf(o, "IDs", C_PURPLE, "VID_%04X PID_%04X REV_%04X", d->ven, d->pid, d->bcd);
    so_kvf(o, "Database", C_VALUE, "%s  %s", *d->vname ? d->vname : "(vendor not in usb.ids)", d->dname);
    if (*d->mfr) so_kv(o, "Manufacturer", C_VALUE, d->mfr);
    if (*d->prod) so_kv(o, "Product", C_VALUE, d->prod);
    if (*d->serial) so_kv(o, "Serial", C_VALUE, d->serial);
    usb_speed(d, s, sizeof s);
    so_kvf(o, "USB version", C_VALUE, "%s  %s", d->ver, s);
    so_kvf(o, "Device class", C_VALUE, "%02X  %s", d->dcls, usb_cls(d->dcls, 0, 0, 0));
    so_kv(o, "Max power", C_VALUE, d->power);
    if (d->maxchild) so_kvf(o, "Hub ports", C_VALUE, "%d", d->maxchild);
    if (*d->ctrl) { const pcid *c = pci_find(d->ctrl); so_kvf(o, "Controller", C_VALUE, "%s  %s", d->ctrl, c ? c->dname : ""); }
    if (*d->removable) so_kv(o, "Removable", C_VALUE, d->removable);
    if (*d->quirks && strcmp(d->quirks, "0x0")) so_kv(o, "Quirks", C_ORANGE, d->quirks);
    for (i = 0; i < d->nif; i++) {
        const usbif *f = &d->ifs[i];
        so_kvf(o, "Interface", C_VALUE, "%s  %02X/%02X/%02X %s  %d endpoint%s  driver %s%s%s", f->name, f->cls, f->sub, f->proto,
               usb_cls(f->cls, f->sub, f->proto, 2), f->neps, f->neps == 1 ? "" : "s", *f->driver ? f->driver : "(none)",
               *f->iface ? "  " : "", f->iface);
        if (eps) {
            char id[256], **ep;
            int ne, q;
            snprintf(id, sizeof id, "%s/%s", d->dir, f->name);
            if ((ne = dir_list(id, &ep)) > 0) {
                for (q = 0; q < ne; q++) {
                    char ed[300], t[32], dr[16], mp[16], iv[32];
                    if (strncmp(ep[q], "ep_", 3)) continue;
                    snprintf(ed, sizeof ed, "%s/%s", id, ep[q]);
                    at(ed, "type", t, sizeof t); at(ed, "direction", dr, sizeof dr);
                    at(ed, "wMaxPacketSize", mp, sizeof mp); at(ed, "interval", iv, sizeof iv);
                    so_kvf(o, "", C_GRAY, "  Endpoint Address 0x%02X (%s)  %s  Maximum Packet Size 0x%s  Interval %s",
                           athex(ed, "bEndpointAddress"), dr, t, mp, iv);
                }
                dir_free(ep, ne);
            }
        }
    }
    so_par(o);
}

/* ======================================================================
 * HID devices
 * ====================================================================== */
typedef struct { char name[40], hname[128], phys[96], uniq[64], driver[48], modalias[96], usbif[32], hidraw[16], input[64]; unsigned bus, ven, pid; long rdlen; } hidd;
static hidd *g_hid;
static int g_nhid = -1;
static void hid_load(void)
{
    char **e;
    int n, i;
    if (g_nhid >= 0) return;
    g_nhid = 0;
    if ((n = dir_list("/sys/bus/hid/devices", &e)) <= 0) return;
    g_hid = calloc((size_t)n, sizeof *g_hid);
    for (i = 0; g_hid && i < n; i++) {
        hidd *h = &g_hid[g_nhid++];
        char dir[256], *u, *p, *sv, rp[PATH_MAX], **sub;
        int ns, j;
        struct stat st;
        snprintf(dir, sizeof dir, "/sys/bus/hid/devices/%s", e[i]);
        snprintf(h->name, sizeof h->name, "%s", e[i]);
        sscanf(e[i], "%x:%x:%x", &h->bus, &h->ven, &h->pid);
        link_base(dir, "driver", h->driver, sizeof h->driver);
        snprintf(rp, sizeof rp, "%s/uevent", dir);
        if (rd_all(rp, &u) >= 0) {
            for (p = strtok_r(u, "\n", &sv); p; p = strtok_r(NULL, "\n", &sv)) {
                if (!strncmp(p, "HID_NAME=", 9)) snprintf(h->hname, sizeof h->hname, "%s", p + 9);
                else if (!strncmp(p, "HID_PHYS=", 9)) snprintf(h->phys, sizeof h->phys, "%s", p + 9);
                else if (!strncmp(p, "HID_UNIQ=", 9)) snprintf(h->uniq, sizeof h->uniq, "%s", p + 9);
                else if (!strncmp(p, "MODALIAS=", 9)) snprintf(h->modalias, sizeof h->modalias, "%s", p + 9);
            }
            free(u);
        }
        if (realpath(dir, rp)) { char *s = strrchr(rp, '/'); if (s) { *s = 0; snprintf(h->usbif, sizeof h->usbif, "%s", bname(rp)); } }
        snprintf(rp, sizeof rp, "%s/report_descriptor", dir);
        h->rdlen = -1;
        { char *rd; int l = rd_all(rp, &rd); if (l >= 0) { h->rdlen = l; free(rd); } else if (!stat(rp, &st)) h->rdlen = -1; }
        snprintf(rp, sizeof rp, "%s/hidraw", dir);
        if ((ns = dir_list(rp, &sub)) > 0) { snprintf(h->hidraw, sizeof h->hidraw, "%s", sub[0]); dir_free(sub, ns); }
        snprintf(rp, sizeof rp, "%s/input", dir);
        if ((ns = dir_list(rp, &sub)) > 0) {
            for (j = 0; j < ns; j++) snprintf(h->input + strlen(h->input), sizeof h->input - strlen(h->input), "%s%s", j ? "," : "", sub[j]);
            dir_free(sub, ns);
        }
    }
    dir_free(e, n);
}
static const char *hid_busname(unsigned b)
{
    switch (b) {
    case 0x03: return "USB";
    case 0x05: return "Bluetooth";
    case 0x06: return "Virtual";
    case 0x18: return "I2C";
    case 0x19: return "Host";
    case 0x1C: return "SPI";
    default: return "Other";
    }
}

/* ======================================================================
 * udev database (udevadm info --export-db, read-only), split into records
 * ====================================================================== */
typedef struct { char *rec; const char *path; } udrec;
static udrec *g_ud;
static int g_nud = -1;
static void ud_load(void)
{
    static const char *const argv[] = { "udevadm", "info", "--export-db", NULL };
    size_t cap = 16u << 20;
    char *b, *p;
    int cap2 = 0;
    if (g_nud >= 0) return;
    g_nud = 0;
    if (!(b = malloc(cap))) return;
    if (plat_run_capture(argv, b, cap) < 0) { free(b); return; }
    for (p = b; p && *p;) {
        char *e = strstr(p, "\n\n");
        if (e) *e = 0;
        if (!strncmp(p, "P: ", 3)) {
            if (g_nud >= cap2) { udrec *n = realloc(g_ud, sizeof *n * (size_t)(cap2 = cap2 ? cap2 * 2 : 1024)); if (!n) break; g_ud = n; }
            g_ud[g_nud].rec = p;
            g_ud[g_nud].path = p + 3;
            p[strcspn(p, "\n")] = 0;          /* path is now its own string; rest follows after the NUL */
            g_nud++;
        }
        p = e ? e + 2 : NULL;
    }
}
/* the rest of a record (after the P: line) */
static const char *ud_body(const udrec *r) { return r->path + strlen(r->path) + 1; }
static int ud_get(const udrec *r, const char *pfx, char *out, size_t sz)   /* pfx like "E: DRIVER=" or "U: " */
{
    const char *b = ud_body(r), *p = b;
    size_t l = strlen(pfx);
    *out = 0;
    while (p && *p) {
        if (!strncmp(p, pfx, l)) { snprintf(out, sz, "%.*s", (int)strcspn(p + l, "\n"), p + l); return 1; }
        p = strchr(p, '\n');
        if (p) p++;
    }
    return 0;
}
static const udrec *ud_find(const char *name)   /* by full devpath or last component */
{
    int i;
    size_t l;
    ud_load();
    if (!name) return NULL;
    l = strlen(name);
    for (i = 0; i < g_nud; i++) {
        const char *p = g_ud[i].path;
        size_t pl = strlen(p);
        if (!strcmp(p, name) || (pl > l && p[pl - l - 1] == '/' && !strcmp(p + pl - l, name))) return &g_ud[i];
    }
    return NULL;
}
static int masked_key(const char *k)
{
    return strstr(k, "TOKEN") || strstr(k, "KEY") || strstr(k, "SECRET") || strstr(k, "PASS") || strstr(k, "AUTH") || strstr(k, "COOKIE");
}
static void ud_props(srs_out *o, const udrec *r)
{
    const char *p = ud_body(r);
    so_b(o, C_BLACK, r->path); so_par(o);
    while (p && *p) {
        const char *e = strchr(p, '\n');
        int l = e ? (int)(e - p) : (int)strlen(p);
        if (l > 3 && p[0] == 'E' && p[1] == ':') {
            char k[128], v[512];
            const char *eq = memchr(p + 3, '=', (size_t)(l - 3));
            if (eq) {
                snprintf(k, sizeof k, "%.*s", (int)(eq - p - 3), p + 3);
                snprintf(v, sizeof v, "%.*s", (int)(l - (eq + 1 - p)), eq + 1);
                so_kv(o, k, C_VALUE, masked_key(k) ? "********" : v);
            }
        } else if (l > 3 && strchr("SNLV", p[0]) && p[1] == ':') {
            char k[16], v[512];
            snprintf(k, sizeof k, "%s", p[0] == 'S' ? "symlink" : p[0] == 'N' ? "devnode" : p[0] == 'L' ? "priority" : "driver");
            snprintf(v, sizeof v, "%.*s", l - 3, p + 3);
            so_kv(o, k, C_TEAL, v);
        }
        p = e ? e + 1 : NULL;
    }
    so_par(o);
}

/* ======================================================================
 * resource maps (/proc/ioports, /proc/iomem) - addresses need root
 * ====================================================================== */
/* print lines of ioports/iomem matching any of pats (NULL = all); depth-limited (-1 = all) */
static int res_map(srs_out *o, const char *proc, const char *cache, const char *const *pats, int maxdepth)
{
    const char *cf = so_root_file(cache);
    char *txt, *p, *sv;
    int n = 0, live = !cf, hidden = 0;
    if (rd_all(cf ? cf : proc, &txt) < 0) { so_err(o, "unreadable"); return 0; }
    for (p = strtok_r(txt, "\n", &sv); p; p = strtok_r(NULL, "\n", &sv)) {
        int dep = 0, ok = !pats, i;
        const char *name = strstr(p, " : ");
        char addr[64], line[512];
        while (p[dep] == ' ') dep++;
        if (maxdepth >= 0 && dep / 2 > maxdepth) continue;
        for (i = 0; pats && pats[i]; i++) if (strcasestr(p, pats[i])) ok = 1;
        if (!ok || !name) continue;
        snprintf(addr, sizeof addr, "%.*s", (int)(name - p - dep), p + dep);
        if (live && strspn(addr, "0-") == strlen(addr)) { hidden = 1; snprintf(line, sizeof line, "%*s%s", dep, "", name + 3); }
        else snprintf(line, sizeof line, "%-22s %*s%s", addr, dep, "", name + 3);
        so_m(o, C_VALUE, line); so_par(o);
        n++;
    }
    free(txt);
    if (!n) so_note(o, "no matching entries");
    if (hidden) { so_note(o, "Addresses are zeroed by the kernel for non-root readers."); so_t(o, C_ORANGE, NEEDS_ROOT); so_par(o); }
    else if (cf) so_note(o, "Addresses from the srs-root cache.");
    so_par(o);
    return n;
}
static void cached_dump(srs_out *o, const char *name)
{
    const char *cf = so_root_file(name);
    char *t;
    if (cf && rd_all(cf, &t) >= 0) { so_lines(o, C_VALUE, t); free(t); }
    else { so_t(o, C_ORANGE, NEEDS_ROOT); so_par(o); }
}

/* ======================================================================
 * sections: PCI
 * ====================================================================== */
static void db_footer(srs_out *o, enum srs_bus b, const char *what, int count, double ms)
{
    const srs_db *d = db(b);
    char t[1400];
    snprintf(t, sizeof t, "Total of %d %s in %.3f seconds using %s Date %s Version %s", count, what, ms / 1000,
             d ? d->path : "(no ids file)", d ? d->date : "-", d ? d->version : "-");
    so_par(o); so_t(o, C_TEAL, t); so_par(o);
}

/* [pci-bus] - SIV PCI Bus panel: Location, Vendor and Device, Class, Driver, Link */
static void s_pci_bus(srs_out *o)
{
    static const int tabs[] = { 210, 700, 960, 1100 };
    static const int hc[] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL };
    static const char *const head[] = { "PCI Location", "Vendor and Device", "Device Class", "Driver", "Link" };
    double t0 = srs_now_ms();
    int i, br = 0;
    so_siv_header(o, "PCI Bus");
    pci_load();
    so_tabs(o, tabs, 4);
    so_row(o, 5, head, hc);
    for (i = 0; i < g_npci; i++) {
        const pcid *d = &g_pci[i];
        char loc[64], vd[480], lk[96];
        const char *c[5];
        static const int col[] = { C_TEAL, C_VALUE, C_GREEN, C_PURPLE, C_ORANGE };
        snprintf(loc, sizeof loc, "%*s%s", d->depth * 2, "", d->slot + 5);
        snprintf(vd, sizeof vd, "%s  %s", d->vname, d->dname);
        pci_link(d, lk, sizeof lk);
        br += (d->cls >> 16) == 6;
        c[0] = loc; c[1] = vd; c[2] = pci_cls(d); c[3] = *d->driver ? d->driver : "-"; c[4] = lk;
        so_row(o, 5, c, col);
    }
    {
        char w[64];
        snprintf(w, sizeof w, "PCI devices (%d bridges)", br);
        db_footer(o, BUS_PCI, w, g_npci, srs_now_ms() - t0);
    }
    so_board_footer(o);
}

/* [pci-dev] / [pci-dev:0000:03:00.0] */
static void s_pci_dev(srs_out *o)
{
    static const int tabs[] = { 140, 280 };
    int i;
    pci_load();
    so_tabs(o, tabs, 1);
    if (arg(o)) {
        const pcid *d = pci_find(arg(o));
        so_siv_header(o, "PCI Device");
        if (!d) { so_err(o, "No such PCI device (use e.g. pci-dev:0000:03:00.0)"); return; }
        pci_detail(o, d);
        return;
    }
    so_siv_header(o, "PCI Devices");
    so_tabs(o, tabs, 2);
    for (i = 0; i < g_npci; i++) {
        const pcid *d = &g_pci[i];
        char id[64], nm[480];
        const char *c[3];
        static const int col[] = { C_TEAL, C_PURPLE, C_VALUE };
        snprintf(id, sizeof id, "%04X:%04X [%04X]", d->ven, d->dev, d->cls >> 8);
        snprintf(nm, sizeof nm, "%s: %s %s", pci_cls(d), d->vname, d->dname);
        c[0] = d->slot; c[1] = id; c[2] = nm;
        so_row(o, 3, c, col);
    }
    so_par(o);
    so_note(o, "Per-device view: pci-dev:<slot>, e.g. pci-dev:0000:03:00.0");
}

static void s_pci_all(srs_out *o)
{
    static const int tabs[] = { 140 };
    int i;
    so_siv_header(o, "PCI All");
    if (so_root_file("pci-all.txt")) { so_note(o, "Full configuration space (lspci -vvvnnxxx, srs-root cache):"); cached_dump(o, "pci-all.txt"); return; }
    pci_load();
    so_tabs(o, tabs, 1);
    for (i = 0; i < g_npci; i++) pci_detail(o, &g_pci[i]);
    so_t(o, C_ORANGE, "Configuration space beyond the standard header: " NEEDS_ROOT); so_par(o);
}

static void s_pci_bars(srs_out *o)
{
    static const int tabs[] = { 110, 170, 330, 490, 680 };
    static const char *const head[] = { "Location", "BAR", "Start", "End", "Type", "Device" };
    static const int hc[] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL };
    int i, k, n = 0;
    double below = 0, above = 0;
    pci_load();
    so_siv_header(o, "PCI Base Address Registers");
    so_tabs(o, tabs, 5);
    so_row(o, 6, head, hc);
    for (i = 0; i < g_npci; i++) {
        const pcid *d = &g_pci[i];
        if (arg(o) && d != pci_find(arg(o))) continue;
        for (k = 0; k < d->nbar; k++) {
            char lab[16], s[32], e[32], t[96];
            const char *c[6];
            static const int col[] = { C_TEAL, C_PURPLE, C_VALUE, C_VALUE, C_GREEN, C_VALUE };
            if (!d->bf[k] && !d->be[k]) continue;
            snprintf(lab, sizeof lab, k == 6 ? "ROM" : "BAR %d", k);
            snprintf(s, sizeof s, "0x%010llX", d->bs[k]);
            snprintf(e, sizeof e, "0x%010llX", d->be[k]);
            bar_desc(d, k, t, sizeof t);
            c[0] = d->slot + 5; c[1] = lab; c[2] = s; c[3] = e; c[4] = t; c[5] = d->dname;
            so_row(o, 6, c, col);
            if (!(d->bf[k] & 0x100)) { if (d->bs[k] < 0x100000000ULL) below += (double)(d->be[k] - d->bs[k] + 1); else above += (double)(d->be[k] - d->bs[k] + 1); }
            n++;
        }
    }
    if (!n) { so_note(o, "no PCI BARs found"); return; }
    {
        char a[32], b[32];
        fmt_bytes(below, a, sizeof a); fmt_bytes(above, b, sizeof b);
        so_par(o);
        so_kvf(o, "< 4GB Total", C_TEAL, "%s", a);
        so_kvf(o, "> 4GB Total", C_TEAL, "%s", b);
    }
}

/* [translation] - bus address -> CPU physical address (x86: identity, no IOMMU translation for MMIO) */
static void s_translation(srs_out *o)
{
    static const int tabs[] = { 110, 170, 340, 510 };
    static const char *const head[] = { "Location", "BAR", "Bus Address", "CPU Address", "Space" };
    static const int hc[] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL };
    int i, k;
    pci_load();
    so_siv_header(o, "Bus Address Translation");
    so_tabs(o, tabs, 4);
    so_row(o, 5, head, hc);
    for (i = 0; i < g_npci; i++)
        for (k = 0; k < g_pci[i].nbar; k++) {
            const pcid *d = &g_pci[i];
            char lab[16], a[32];
            const char *c[5];
            if (!d->bf[k] && !d->be[k]) continue;
            snprintf(lab, sizeof lab, k == 6 ? "ROM" : "BAR %d", k);
            snprintf(a, sizeof a, "0x%010llX", d->bs[k]);
            c[0] = d->slot + 5; c[1] = lab; c[2] = a; c[3] = a; c[4] = d->bf[k] & 0x100 ? "I/O" : "Memory";
            so_row(o, 5, c, NULL);
        }
    so_par(o);
    so_note(o, "x86-64: PCI bus addresses map 1:1 to CPU physical addresses (no host-bridge offset); the AMD IOMMU translates DMA only.");
}

static void s_chipsets(srs_out *o)
{
    static const int tabs[] = { 200 };
    int i;
    pci_load();
    so_siv_header(o, "Chipsets");
    so_tabs(o, tabs, 1);
    for (i = 0; i < g_npci; i++) {
        const pcid *d = &g_pci[i];
        unsigned c = d->cls >> 8;
        const char *what = c == 0x0600 ? "Host Bridge" : c == 0x0601 ? "ISA/LPC Bridge" : c == 0x0C05 ? "SMBus Controller" :
                           c == 0x0806 ? "IOMMU" : (c == 0x0604 && strstr(d->dname, "Chipset")) ? "Chipset Bridge" :
                           strstr(d->dname, "Chipset") ? "Chipset Function" : NULL;
        char b[600];
        if (!what) continue;
        snprintf(b, sizeof b, "%s  %s %s  [%04X:%04X rev %02X]%s%s", d->slot, d->vname, d->dname, d->ven, d->dev, d->rev,
                 *d->driver ? "  driver " : "", d->driver);
        so_kv(o, what, c == 0x0600 ? C_BLACK : C_VALUE, b);
    }
    so_board_footer(o);
}

static void s_mch(srs_out *o)
{
    static const int tabs[] = { 140 };
    const pcid *d = pci_find("0000:00:00.0");
    so_siv_header(o, "Chipset MCH (Root Complex)");
    so_tabs(o, tabs, 1);
    if (d) pci_detail(o, d);
    so_note(o, "AMD has no MCH; the memory controller is in the CPU's data fabric (UMC). Fabric/UMC registers are");
    so_note(o, "reached through SMN, which srs never touches (SMU-wedge risk on this machine). See [dram-timings]/[spd].");
    if (so_root_file("pci-all.txt")) {
        char *t, *p, *e;
        if (rd_all(so_root_file("pci-all.txt"), &t) >= 0) {
            if ((p = strstr(t, "00:00.0 "))) { if ((e = strstr(p, "\n\n"))) *e = 0; so_lines(o, C_VALUE, p); }
            free(t);
        }
    } else { so_t(o, C_ORANGE, "Host bridge config space: " NEEDS_ROOT); so_par(o); }
}

static void ids_db_info(srs_out *o, enum srs_bus b, const char *sivname)
{
    static const int tabs[] = { 170 };
    const srs_db *d;
    double t0 = srs_now_ms();
    char n[32];
    so_tabs(o, tabs, 1);
    d = db(b);
    if (!d) { so_err(o, "ids file not found"); return; }
    srs_thousands(d->n, n, sizeof n);
    so_kv(o, "Database", C_BLACK, d->path);
    so_kvf(o, "SIV equivalent", C_GRAY, "%s", sivname);
    so_kv(o, "Version", C_VALUE, d->version);
    so_kv(o, "Date", C_VALUE, d->date);
    so_kvf(o, "Records", C_VALUE, "%s names (vendors, devices, subsystems)", n);
    so_kvf(o, "Load time", C_VALUE, "%.3f ms (this lookup %.3f ms)", d->load_ms, srs_now_ms() - t0);
    so_kv(o, "Lookup chain", C_PURPLE, b == BUS_PCI ? "V:D:S:s:RR -> V:D:S:s -> V:D:RR -> V:D -> V" : "V:D -> V");
    if (g_ncls[b] < 0) load_classes(b);
    so_kvf(o, "Class names", C_VALUE, "%d class/subclass/prog-if records", g_ncls[b]);
}
static void s_pci_info(srs_out *o) { so_siv_header(o, "PCI ID Database"); ids_db_info(o, BUS_PCI, "pcidevs.txt"); }
static void s_usb_info(srs_out *o) { so_siv_header(o, "USB ID Database"); ids_db_info(o, BUS_USB, "usbdevs.txt"); }

/* [device-ids] - every PCI/USB device ID with the rung of the lookup chain that matched */
static void s_device_ids(srs_out *o)
{
    static const int tabs[] = { 110, 330, 470 };
    static const char *const head[] = { "Device", "Lookup Key", "Matched", "Name" };
    static const int hc[] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL };
    int i;
    pci_load(); usb_load();
    so_siv_header(o, "Device IDs");
    so_tabs(o, tabs, 3);
    so_row(o, 4, head, hc);
    for (i = 0; i < g_npci; i++) {
        char v[200], d[256], m[32], nm[480];
        const char *c[4];
        int rung;
        id_name(BUS_PCI, g_pci[i].key, v, sizeof v, d, sizeof d, &rung);
        snprintf(m, sizeof m, rung < 0 ? "none" : "rung %d", rung);
        snprintf(nm, sizeof nm, "%s  %s", v, d);
        c[0] = g_pci[i].slot + 5; c[1] = g_pci[i].key; c[2] = m; c[3] = nm;
        so_row(o, 4, c, NULL);
    }
    for (i = 0; i < g_nusb; i++) {
        char nm[480];
        const char *c[4];
        snprintf(nm, sizeof nm, "%s  %s", g_usb[i].vname, g_usb[i].dname);
        c[0] = g_usb[i].name; c[1] = g_usb[i].key; c[2] = *g_usb[i].dname ? "V:D" : *g_usb[i].vname ? "V" : "none"; c[3] = nm;
        so_row(o, 4, c, NULL);
    }
}

/* [compatible] - hardware/compatible IDs = modalias strings */
static void s_compatible(srs_out *o)
{
    static const int tabs[] = { 140 };
    int i, j;
    pci_load(); usb_load();
    so_siv_header(o, "Compatible IDs (modalias)");
    so_tabs(o, tabs, 1);
    for (i = 0; i < g_npci; i++) if (!arg(o) || strstr(g_pci[i].slot, arg(o))) so_kv(o, g_pci[i].slot + 5, C_VALUE, g_pci[i].modalias);
    for (i = 0; i < g_nusb; i++)
        for (j = 0; j < g_usb[i].nif; j++)
            if (!arg(o) || strstr(g_usb[i].ifs[j].name, arg(o))) so_kv(o, g_usb[i].ifs[j].name, C_TEAL, g_usb[i].ifs[j].modalias);
    {
        char **e;
        int n = dir_list("/sys/bus/acpi/devices", &e);
        for (i = 0; i < n; i++) {
            char m[256];
            if (rd_fmt(m, sizeof m, "/sys/bus/acpi/devices/%s/modalias", e[i]) && (!arg(o) || strstr(e[i], arg(o)))) so_kv(o, e[i], C_PURPLE, m);
        }
        dir_free(e, n);
    }
}

/* [dev-owner] - owning driver/module per PCI and USB device */
static void s_dev_owner(srs_out *o)
{
    static const int tabs[] = { 110, 280, 440 };
    static const char *const head[] = { "Device", "Driver", "Module", "Description" };
    static const int hc[] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL };
    int i;
    pci_load(); usb_load();
    so_siv_header(o, "Device Owners");
    so_tabs(o, tabs, 3);
    so_row(o, 4, head, hc);
    for (i = 0; i < g_npci; i++) {
        const char *c[4];
        static const int col[] = { C_TEAL, C_GREEN, C_PURPLE, C_VALUE };
        c[0] = g_pci[i].slot + 5; c[1] = *g_pci[i].driver ? g_pci[i].driver : "(none)"; c[2] = g_pci[i].module; c[3] = g_pci[i].dname;
        so_row(o, 4, c, col);
    }
    for (i = 0; i < g_nusb; i++) {
        char dr[256], ds[400];
        const char *c[4];
        static const int col[] = { C_TEAL, C_GREEN, C_PURPLE, C_VALUE };
        usb_drivers(&g_usb[i], dr, sizeof dr);
        usb_desc(&g_usb[i], ds, sizeof ds);
        c[0] = g_usb[i].name; c[1] = dr; c[2] = ""; c[3] = ds;
        so_row(o, 4, c, col);
    }
}

/* ======================================================================
 * sections: USB
 * ====================================================================== */
static void usb_totals(srs_out *o, double ms)
{
    int i, roots = 0, hubs = 0, devs = 0;
    char w[160];
    for (i = 0; i < g_nusb; i++) { if (g_usb[i].root) roots++; else if (g_usb[i].dcls == 9) hubs++; else devs++; }
    snprintf(w, sizeof w, "USB entities, %d Root Hubs, %d Ext Hubs and %d Devices", roots, hubs, devs);
    db_footer(o, BUS_USB, w, g_nusb, ms);
}

/* [usb-bus] - SIV USB Bus panel: Connection, Vendor and Device Description, Version, mA, Device Class, Device Information */
static void s_usb_bus(srs_out *o)
{
    static const int tabs[] = { 150, 620, 690, 740, 900 };
    static const char *const head[] = { "Connection", "Vendor and Device Description", "Version", "mA", "Device Class", "Device Information" };
    static const int hc[] = { C_LABEL, C_LABEL, C_ORANGE, C_LABEL, C_GREEN, C_LABEL };
    double t0 = srs_now_ms();
    int i;
    usb_load();
    so_siv_header(o, "USB Bus");
    so_tabs(o, tabs, 5);
    so_row(o, 6, head, hc);
    for (i = 0; i < g_nusb; i++) {
        const usbd *d = &g_usb[i];
        char con[64], ds[480], ma[16], inf[300], sp[64], dr[200];
        const char *c[6];
        static const int col[] = { C_TEAL, C_VALUE, C_ORANGE, C_VALUE, C_GREEN, C_PURPLE };
        if (d->root) snprintf(con, sizeof con, "Hub %d", d->bus);
        else snprintf(con, sizeof con, "%*sPort %s", d->depth * 2, "", d->devpath);
        usb_desc(d, ds, sizeof ds);
        snprintf(ma, sizeof ma, "%d", atoi(d->power));
        usb_speed(d, sp, sizeof sp);
        usb_drivers(d, dr, sizeof dr);
        if (d->root || d->dcls == 9) snprintf(inf, sizeof inf, "%s  %d port%s  %s", sp, d->maxchild, d->maxchild == 1 ? "" : "s", dr);
        else snprintf(inf, sizeof inf, "%s  %s", sp, dr);
        c[0] = con; c[1] = ds; c[2] = d->ver; c[3] = ma;
        c[4] = d->root ? (atof(d->ver) >= 3 ? "V3 Root Hub" : "V2 Root Hub") : usb_cls(usb_devclass(d), 0, 0, 0);
        c[5] = inf;
        so_row(o, 6, c, col);
    }
    usb_totals(o, srs_now_ms() - t0);
    so_board_footer(o);
}

static void s_usb_tree(srs_out *o)
{
    int i, j;
    usb_load();
    so_siv_header(o, "USB Tree");
    for (i = 0; i < g_nusb; i++) {
        const usbd *d = &g_usb[i];
        char b[600], ds[480];
        usb_desc(d, ds, sizeof ds);
        if (d->root) snprintf(b, sizeof b, "/:  Bus %03d.Port 1: Dev 1, Class=root_hub, %s, %sM  [%s]", d->bus, d->ifs[0].driver, d->speed, d->ctrl);
        else snprintf(b, sizeof b, "%*s|__ Port %s: Dev %d, %04X:%04X %s, %sM", d->depth * 4, "", d->devpath, d->devnum, d->ven, d->pid, ds, d->speed);
        so_m(o, d->root ? C_BLACK : C_VALUE, b); so_par(o);
        for (j = 0; j < d->nif && !d->root; j++) {
            snprintf(b, sizeof b, "%*s    If %s, Class=%s, Driver=%s", d->depth * 4, "", strchr(d->ifs[j].name, ':') + 1,
                     usb_cls(d->ifs[j].cls, 0, 0, 0), *d->ifs[j].driver ? d->ifs[j].driver : "[none]");
            so_m(o, C_GRAY, b); so_par(o);
        }
    }
}

static void s_usb_dev(srs_out *o)
{
    static const int tabs[] = { 140, 210 };
    int i;
    usb_load();
    so_tabs(o, tabs, 1);
    if (arg(o)) {
        const usbd *d = usb_find(arg(o));
        so_siv_header(o, "USB Device");
        if (!d) { so_err(o, "No such USB device (use e.g. usb-dev:1-1 or usb-dev:usb3)"); return; }
        usb_detail(o, d, 1);
        return;
    }
    so_siv_header(o, "USB Devices");
    so_tabs(o, tabs, 2);
    for (i = 0; i < g_nusb; i++) {
        char id[64], ds[480];
        snprintf(id, sizeof id, "Bus %03d Device %03d", g_usb[i].bus, g_usb[i].devnum);
        usb_desc(&g_usb[i], ds, sizeof ds);
        so_t(o, C_TEAL, g_usb[i].name); so_tab(o); so_t(o, C_VALUE, id); so_tab(o);
        so_f(o, C_VALUE, "ID %04x:%04x %s", g_usb[i].ven, g_usb[i].pid, ds); so_par(o);
    }
    so_par(o);
    so_note(o, "Per-device view: usb-dev:<name>, e.g. usb-dev:1-1");
}

static void s_usb_all(srs_out *o)
{
    static const int tabs[] = { 140 };
    int i;
    double t0 = srs_now_ms();
    usb_load();
    so_siv_header(o, "USB All");
    so_tabs(o, tabs, 1);
    for (i = 0; i < g_nusb; i++) usb_detail(o, &g_usb[i], 1);
    usb_totals(o, srs_now_ms() - t0);
}

static void s_usb_names(srs_out *o)
{
    static const int tabs[] = { 110, 220, 520 };
    static const char *const head[] = { "Device", "VID:PID", "Device strings", "usb.ids name" };
    static const int hc[] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL };
    int i;
    usb_load();
    so_siv_header(o, "USB Names");
    so_tabs(o, tabs, 3);
    so_row(o, 4, head, hc);
    for (i = 0; i < g_nusb; i++) {
        const usbd *d = &g_usb[i];
        char s[256], n[480];
        const char *c[4];
        snprintf(s, sizeof s, "%s%s%s", d->mfr, *d->mfr ? " / " : "", d->prod);
        snprintf(n, sizeof n, "%s  %s", *d->vname ? d->vname : "-", d->dname);
        c[0] = d->name; c[1] = d->key; c[2] = s; c[3] = n;
        so_row(o, 4, c, NULL);
    }
}

static void s_usb_owner(srs_out *o)
{
    static const int tabs[] = { 140, 300, 450 };
    static const char *const head[] = { "Interface", "Class", "Driver", "Module" };
    static const int hc[] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL };
    int i, j;
    usb_load();
    so_siv_header(o, "USB Owners");
    so_tabs(o, tabs, 3);
    so_row(o, 4, head, hc);
    for (i = 0; i < g_nusb; i++)
        for (j = 0; j < g_usb[i].nif; j++) {
            const usbif *f = &g_usb[i].ifs[j];
            char m[64] = "", id[200];
            const char *c[4];
            static const int col[] = { C_TEAL, C_VALUE, C_GREEN, C_PURPLE };
            snprintf(id, sizeof id, "%s/%s", g_usb[i].dir, f->name);
            mod_of(id, m, sizeof m);
            c[0] = f->name; c[1] = usb_cls(f->cls, 0, 0, 0); c[2] = *f->driver ? f->driver : "(none)"; c[3] = m;
            so_row(o, 4, c, col);
        }
}

/* [si-usb] - per host controller summary */
static void s_si_usb(srs_out *o)
{
    static const int tabs[] = { 160 };
    int i, j;
    usb_load();
    so_siv_header(o, "USB Host Controllers");
    so_tabs(o, tabs, 1);
    for (i = 0; i < g_nusb; i++) {
        const usbd *r = &g_usb[i];
        const pcid *c;
        int n = 0;
        if (!r->root) continue;
        c = pci_find(r->ctrl);
        for (j = 0; j < g_nusb; j++) n += !g_usb[j].root && g_usb[j].bus == r->bus;
        so_kvf(o, r->name, C_BLACK, "%s  %s %s", r->ctrl, c ? c->vname : "virtual host controller", c ? c->dname : "");
        so_kvf(o, "", C_VALUE, "USB %s root hub, %s Mbps, %d port%s, %d device%s attached, driver %s", r->ver, r->speed, r->maxchild, r->maxchild == 1 ? "" : "s", n,
               n == 1 ? "" : "s", c && *c->driver ? c->driver : r->ctrl);
    }
}

static void s_usb_flags(srs_out *o)
{
    static const int tabs[] = { 220 };
    static const char *const p[] = { "quirks", "autosuspend", "authorized_default", "old_scheme_first", "use_both_schemes",
                                     "initial_descriptor_timeout", "usbfs_memory_mb", "nousb", NULL };
    int i, any = 0;
    char b[512];
    usb_load();
    so_siv_header(o, "USB Flags (usbcore parameters and per-device quirks)");
    so_tabs(o, tabs, 1);
    for (i = 0; p[i]; i++) if (rd_fmt(b, sizeof b, "/sys/module/usbcore/parameters/%s", p[i])) so_kv(o, p[i], C_VALUE, *b ? b : "(empty)");
    so_par(o);
    for (i = 0; i < g_nusb; i++)
        if (*g_usb[i].quirks && strcmp(g_usb[i].quirks, "0x0")) { so_kvf(o, g_usb[i].name, C_ORANGE, "quirks %s  %s", g_usb[i].quirks, g_usb[i].key); any = 1; }
    if (!any) so_note(o, "No per-device USB quirks active (Windows: HKLM\\System\\CurrentControlSet\\Control\\UsbFlags).");
}

/* [wpd-tree] - portable devices (MTP/PTP) */
static void s_wpd_tree(srs_out *o)
{
    static const int tabs[] = { 140 };
    int i, j, n = 0;
    usb_load();
    so_siv_header(o, "Portable Devices");
    so_tabs(o, tabs, 1);
    for (i = 0; i < g_nusb; i++) {
        const usbd *d = &g_usb[i];
        const udrec *r = NULL;
        char mtp[16] = "", ds[480];
        int ptp = 0;
        for (j = 0; j < d->nif; j++) ptp |= d->ifs[j].cls == 6 || strcasestr(d->ifs[j].iface, "MTP") != NULL;
        if (!ptp && !d->root && (r = ud_find(d->name))) ud_get(r, "E: ID_MTP_DEVICE=", mtp, sizeof mtp);
        if (!ptp && !*mtp) continue;
        usb_desc(d, ds, sizeof ds);
        so_kvf(o, d->name, C_VALUE, "%s  [%s]", ds, ptp ? "PTP/MTP still image interface" : "MTP (udev ID_MTP_DEVICE)");
        n++;
    }
    if (!n) so_note(o, "No MTP/PTP portable devices attached (checked USB interface class 06 and udev ID_MTP_DEVICE).");
}

/* ======================================================================
 * sections: HID
 * ====================================================================== */
static void s_hid_bus(srs_out *o)
{
    static const int tabs[] = { 190, 270, 600, 780 };
    static const char *const head[] = { "HID Device", "Bus", "Name", "Driver", "Nodes" };
    static const int hc[] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL };
    int i;
    hid_load();
    so_siv_header(o, "HID Bus");
    so_tabs(o, tabs, 4);
    so_row(o, 5, head, hc);
    for (i = 0; i < g_nhid; i++) {
        const hidd *h = &g_hid[i];
        char nodes[128];
        const char *c[5];
        static const int col[] = { C_TEAL, C_PURPLE, C_VALUE, C_GREEN, C_GRAY };
        if (arg(o) && !strstr(h->name, arg(o))) continue;
        snprintf(nodes, sizeof nodes, "%s%s%s", h->hidraw, *h->hidraw && *h->input ? " " : "", h->input);
        c[0] = h->name; c[1] = hid_busname(h->bus); c[2] = h->hname; c[3] = *h->driver ? h->driver : "(none)"; c[4] = nodes;
        so_row(o, 5, c, col);
    }
    so_par(o);
    so_f(o, C_TEAL, "Total of %d HID devices", g_nhid); so_par(o);
}

static void s_hid_all(srs_out *o)
{
    static const int tabs[] = { 160 };
    int i;
    hid_load();
    so_siv_header(o, "HID All Devices");
    so_tabs(o, tabs, 1);
    for (i = 0; i < g_nhid; i++) {
        const hidd *h = &g_hid[i];
        char v[200] = "", d[256] = "", k[16];
        if (arg(o) && !strstr(h->name, arg(o))) continue;
        so_b(o, C_BLACK, h->name); so_tab(o); so_b(o, C_VALUE, h->hname); so_par(o);
        so_kvf(o, "Bus", C_PURPLE, "%04X %s  VID_%04X PID_%04X", h->bus, hid_busname(h->bus), h->ven, h->pid);
        if (h->bus == 3) { snprintf(k, sizeof k, "%04X:%04X", h->ven, h->pid); id_name(BUS_USB, k, v, sizeof v, d, sizeof d, NULL); so_kvf(o, "usb.ids", C_VALUE, "%s  %s", v, d); }
        so_kv(o, "Driver", C_GREEN, *h->driver ? h->driver : "(none)");
        so_kv(o, "Physical", C_VALUE, h->phys);
        if (*h->uniq) so_kv(o, "Unique ID", C_VALUE, h->uniq);
        so_kv(o, "Parent", C_VALUE, h->usbif);
        if (h->rdlen >= 0) so_kvf(o, "Report descriptor", C_VALUE, "%ld bytes", h->rdlen);
        if (*h->hidraw) so_kv(o, "hidraw node", C_GRAY, h->hidraw);
        if (*h->input) so_kv(o, "Input devices", C_VALUE, h->input);
        so_kv(o, "Modalias", C_GRAY, h->modalias);
        so_par(o);
    }
    so_note(o, "hidraw nodes are listed, never opened (CoolerControl/liquidctl own them).");
}

static void s_hid_tree(srs_out *o)
{
    int i, j;
    usb_load(); hid_load();
    so_siv_header(o, "HID Tree");
    for (i = 0; i < g_nusb; i++) {
        const usbd *d = &g_usb[i];
        int shown = 0;
        for (j = 0; j < g_nhid; j++) {
            size_t l = strlen(d->name);
            char b[400], ds[480];
            if (strncmp(g_hid[j].usbif, d->name, l) || g_hid[j].usbif[l] != ':') continue;
            if (!shown++) { usb_desc(d, ds, sizeof ds); snprintf(b, sizeof b, "%s  %04X:%04X %s", d->name, d->ven, d->pid, ds); so_m(o, C_BLACK, b); so_par(o); }
            snprintf(b, sizeof b, "  |__ %s  %s  [%s] %s", g_hid[j].usbif, g_hid[j].name, g_hid[j].driver, g_hid[j].input);
            so_m(o, C_VALUE, b); so_par(o);
        }
    }
    for (j = 0; j < g_nhid; j++) if (g_hid[j].bus != 3) { so_f(o, C_PURPLE, "%s  %s  %s [%s]", hid_busname(g_hid[j].bus), g_hid[j].name, g_hid[j].hname, g_hid[j].driver); so_par(o); }
}

/* ======================================================================
 * sections: PnP / ACPI / ISA
 * ====================================================================== */
static const char *pnp_known(const char *id)
{
    static const char *const t[][2] = {
        { "PNP0000", "AT Programmable Interrupt Controller" }, { "PNP0100", "AT System Timer" }, { "PNP0103", "HPET System Timer" },
        { "PNP0200", "AT DMA Controller" }, { "PNP0303", "IBM Enhanced Keyboard" }, { "PNP0400", "Standard LPT Printer Port" },
        { "PNP0501", "16550A-compatible COM Port" }, { "PNP0700", "Floppy Disk Controller" }, { "PNP0800", "AT System Speaker" },
        { "PNP0A03", "PCI Bus" }, { "PNP0A05", "Generic Container Device" }, { "PNP0A06", "Generic Container Device" },
        { "PNP0A08", "PCI Express Root Bus" }, { "PNP0B00", "AT Real-Time Clock" }, { "PNP0C01", "System Board" },
        { "PNP0C02", "Motherboard Resources" }, { "PNP0C04", "Math Coprocessor" }, { "PNP0C09", "ACPI Embedded Controller" },
        { "PNP0C0A", "Control Method Battery" }, { "PNP0C0B", "ACPI Fan" }, { "PNP0C0C", "ACPI Power Button" },
        { "PNP0C0D", "ACPI Lid" }, { "PNP0C0E", "ACPI Sleep Button" }, { "PNP0C0F", "PCI Interrupt Link" },
        { "PNP0C14", "WMI Mapper" }, { "PNP0C33", "Error Device" }, { "PNP0C80", "Memory Device" }, { "PNP0F13", "PS/2 Mouse" },
        { "ACPI0003", "AC Adapter" }, { "ACPI0007", "Processor" }, { "ACPI0010", "Processor Container" },
        { "ACPI000E", "Time and Alarm Device" }, { "AMDI0010", "AMD I2C Controller" }, { "AMDI0030", "AMD GPIO Controller" },
        { "AMDIF031", "AMD GPIO Controller" }, { "MSFT0101", "TPM 2.0" }, { "LNXSYSTM", "ACPI Root" }, { "LNXSYBUS", "ACPI System Bus" },
        { "LNXPWRBN", "Power Button (fixed)" }, { "LNXCPU", "Processor (legacy)" }, { "LNXTHERM", "Thermal Zone" },
        { "LNXPOWER", "ACPI Power Resource" }, { "LNXVIDEO", "ACPI Video Bus" }, { "device", "ACPI device (no _HID)" }, { NULL, NULL } };
    int i;
    for (i = 0; t[i][0]; i++) if (!strncmp(id, t[i][0], strlen(t[i][0]))) return t[i][1];
    return NULL;
}
/* Vendor of a device ID. "PNP0A08" style IDs (7 chars) carry a 3-letter EISA/PNP vendor (hwdata
   pnp.ids); "AMDI0010" style IDs (8 chars) carry a 4-character ACPI vendor ID, which pnp.ids does not
   list - those use a short table of ACPI vendor IDs, else the raw prefix is shown. */
static void pnp_vendor(const char *id, char *b, size_t sz)
{
    static const char *const acpi[][2] = {
        { "ACPI", "ACPI (generic, defined by the ACPI specification)" }, { "AMDI", "AMD" }, { "AMDP", "AMD" },
        { "INTC", "Intel" }, { "INT3", "Intel" }, { "MSFT", "Microsoft" }, { "NVDA", "NVIDIA" }, { "QCOM", "Qualcomm" },
        { "BRCM", "Broadcom" }, { "SYNA", "Synaptics" }, { "ELAN", "ELAN Microelectronics" }, { "NXP0", "NXP" },
        { "LNXS", "Linux (kernel-defined)" }, { "LNXP", "Linux (kernel-defined)" }, { "LNXV", "Linux (kernel-defined)" },
        { "LNXT", "Linux (kernel-defined)" }, { "LNXC", "Linux (kernel-defined)" }, { "HPQC", "HP" }, { "DELL", "Dell" },
        { "LEN0", "Lenovo" }, { "IBM0", "IBM / Lenovo" }, { "ASUS", "ASUSTeK" }, { NULL, NULL } };
    char path[512], line[256];
    size_t l = strcspn(id, ":"), i;
    FILE *f;
    int alpha3 = 1;
    *b = 0;
    if (!strncmp(id, "PNP", 3) && l == 7) { snprintf(b, sz, "PNP (generic, defined by the PnP / ACPI specifications)"); return; }
    if (l == 8) {
        for (i = 0; acpi[i][0]; i++) if (!strncmp(id, acpi[i][0], 4)) { snprintf(b, sz, "%s", acpi[i][1]); return; }
        snprintf(b, sz, "ACPI vendor ID %.4s", id);
        return;
    }
    for (i = 0; i < 3; i++) if (!isupper((unsigned char)id[i])) alpha3 = 0;
    if (l != 7 || !alpha3) return;
    snprintf(path, sizeof path, "%s/pnp.ids", plat_hwdata_dir());
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, id, 3) && line[3] == '\t') { line[strcspn(line, "\r\n")] = 0; snprintf(b, sz, "%s", line + 4); break; }
    fclose(f);
    if (*b && strstr(b, "Invalid Vendor")) snprintf(b, sz, "PNP vendor ID %.3s (not in the PNP ID registry)", id);
}
static void pnp_name(const char *id, char *b, size_t sz)
{
    const char *k = pnp_known(id);
    char v[200];
    if (k) { snprintf(b, sz, "%s", k); return; }
    pnp_vendor(id, v, sizeof v);
    snprintf(b, sz, "%s%s", *v ? v : "Unknown", *v ? " device" : "");
}

static void s_pnp_dev(srs_out *o)
{
    static const int tabs[] = { 90, 190, 450, 560 };
    static const char *const head[] = { "Device", "ID", "Description", "Driver", "Resources" };
    static const int hc[] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL };
    char **e;
    int n, i;
    so_siv_header(o, "PnP Devices");
    so_tabs(o, tabs, 4);
    so_row(o, 5, head, hc);
    n = dir_list("/sys/bus/pnp/devices", &e);
    for (i = 0; i < n; i++) {
        char dir[256], id[64], nm[256], dr[64], *res, flat[256] = "", *p, *sv;
        const char *c[5];
        static const int col[] = { C_TEAL, C_PURPLE, C_VALUE, C_GREEN, C_GRAY };
        snprintf(dir, sizeof dir, "/sys/bus/pnp/devices/%s", e[i]);
        at(dir, "id", id, sizeof id);
        pnp_name(id, nm, sizeof nm);
        link_base(dir, "driver", dr, sizeof dr);
        {
            char rp[PATH_MAX];
            snprintf(rp, sizeof rp, "%s/resources", dir);
            if (rd_all(rp, &res) >= 0) {
                for (p = strtok_r(res, "\n", &sv); p; p = strtok_r(NULL, "\n", &sv))
                    if (strncmp(p, "state", 5) && !strstr(p, "disabled"))
                        snprintf(flat + strlen(flat), sizeof flat - strlen(flat), "%s%s", *flat ? ", " : "", p);
                free(res);
            }
        }
        c[0] = e[i]; c[1] = id; c[2] = nm; c[3] = *dr ? dr : "(none)"; c[4] = flat;
        so_row(o, 5, c, col);
    }
    if (n <= 0) so_note(o, "no PnP devices");
    dir_free(e, n);
    so_note(o, "I/O addresses in pnp resources are visible to all users; irq/dma as configured by the firmware.");
}

/* ACPI namespace devices: hid, path, status, physical node */
static void s_pnp_man(srs_out *o)
{
    static const int tabs[] = { 130, 380, 470, 650 };
    static const char *const head[] = { "Device", "ACPI Path", "_STA", "Description", "Bound to" };
    static const int hc[] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL };
    char **e;
    int n, i, present = 0;
    so_siv_header(o, "PnP Manager (ACPI namespace devices)");
    so_tabs(o, tabs, 4);
    so_row(o, 5, head, hc);
    n = dir_list("/sys/bus/acpi/devices", &e);
    for (i = 0; i < n; i++) {
        char dir[256], path[160] = "", st[32] = "", sts[48], nm[256], ph[64] = "";
        const char *c[5];
        long s;
        static const int col[] = { C_TEAL, C_VALUE, C_PURPLE, C_VALUE, C_GRAY };
        snprintf(dir, sizeof dir, "/sys/bus/acpi/devices/%s", e[i]);
        at(dir, "path", path, sizeof path);
        if (at(dir, "status", st, sizeof st)) { s = atol(st); snprintf(sts, sizeof sts, "0x%02lX%s", s, (s & 1) ? "" : " absent"); present += (s & 1) != 0; }
        else { snprintf(sts, sizeof sts, "-"); present++; }
        pnp_name(e[i], nm, sizeof nm);
        link_base(dir, "physical_node", ph, sizeof ph);
        c[0] = e[i]; c[1] = path; c[2] = sts; c[3] = nm; c[4] = ph;
        so_row(o, 5, c, col);
    }
    so_par(o);
    so_f(o, C_TEAL, "Total of %d ACPI devices, %d present (no _STA = present)", n < 0 ? 0 : n, present); so_par(o);
    dir_free(e, n);
}

/* [pnp] - resource map per PnP device */
static void s_pnp(srs_out *o)
{
    static const int tabs[] = { 160 };
    char **e;
    int n, i;
    so_siv_header(o, "PnP Manager Resource Map");
    so_tabs(o, tabs, 1);
    n = dir_list("/sys/bus/pnp/devices", &e);
    for (i = 0; i < n; i++) {
        char dir[256], id[64], nm[256], fw[64], *res;
        snprintf(dir, sizeof dir, "/sys/bus/pnp/devices/%s", e[i]);
        at(dir, "id", id, sizeof id);
        pnp_name(id, nm, sizeof nm);
        link_base(dir, "firmware_node", fw, sizeof fw);
        so_b(o, C_BLACK, e[i]); so_tab(o); so_f(o, C_VALUE, "%s  %s  (ACPI %s)", id, nm, fw); so_par(o);
        snprintf(dir + strlen(dir), sizeof dir - strlen(dir), "/resources");
        if (rd_all(dir, &res) >= 0) { so_lines(o, C_PURPLE, res); free(res); }
        so_par(o);
    }
    if (n <= 0) so_note(o, "no PnP devices");
    dir_free(e, n);
}

static void s_pnp_info(srs_out *o)
{
    static const int tabs[] = { 170 };
    char path[512], *t, **e;
    int l, lines = 0, i, n;
    so_siv_header(o, "PnP ID Database");
    so_tabs(o, tabs, 1);
    snprintf(path, sizeof path, "%s/pnp.ids", plat_hwdata_dir());
    if ((l = rd_all(path, &t)) < 0) { so_err(o, "pnp.ids not found"); return; }
    for (i = 0; i < l; i++) lines += t[i] == '\n';
    free(t);
    so_kv(o, "Database", C_BLACK, path);
    so_kv(o, "SIV equivalent", C_GRAY, "pnpdevs.txt");
    so_kvf(o, "Records", C_VALUE, "%d EISA/PnP vendor prefixes", lines);
    so_kvf(o, "Built-in", C_VALUE, "standard PNPxxxx / ACPIxxxx / LNXxxxx device names");
    so_par(o);
    so_b(o, C_VALUE, "IDs on this system"); so_par(o);
    n = dir_list("/sys/bus/acpi/devices", &e);
    for (i = 0; i < n; i++) {
        char id[32], nm[256];
        int j, dup = 0;
        snprintf(id, sizeof id, "%.*s", (int)strcspn(e[i], ":"), e[i]);
        for (j = 0; j < i; j++) if (!strncmp(e[j], id, strlen(id)) && e[j][strlen(id)] == ':') dup = 1;
        if (dup) continue;
        pnp_name(id, nm, sizeof nm);
        so_kv(o, id, C_VALUE, nm);
    }
    dir_free(e, n);
}

static void s_isa_bus(srs_out *o)
{
    static const int tabs[] = { 160 };
    char **e;
    int n, i;
    pci_load();
    so_siv_header(o, "ISA / LPC Bus");
    so_tabs(o, tabs, 1);
    for (i = 0; i < g_npci; i++)
        if ((g_pci[i].cls >> 8) == 0x0601) so_kvf(o, "LPC bridge", C_BLACK, "%s  %s %s  driver %s", g_pci[i].slot, g_pci[i].vname, g_pci[i].dname, *g_pci[i].driver ? g_pci[i].driver : "(none)");
    n = dir_list("/sys/bus/isa/devices", &e);
    so_kvf(o, "ISA bus devices", C_VALUE, "%d (legacy isa_driver probes)", n < 0 ? 0 : n);
    for (i = 0; i < n; i++) so_kv(o, "", C_VALUE, e[i]);
    dir_free(e, n);
    so_par(o);
    so_b(o, C_VALUE, "LPC devices enumerated by ACPI PnP"); so_par(o);
    s_pnp(o);
    so_b(o, C_VALUE, "Super I/O"); so_par(o);
    {
        char **h;
        int nh = dir_list("/sys/class/hwmon", &h);
        for (i = 0; i < nh; i++) {
            char nm[64], dv[128] = "";
            rd_fmt(nm, sizeof nm, "/sys/class/hwmon/%s/name", h[i]);
            if (strncmp(nm, "nct", 3) && strncmp(nm, "it8", 3) && strncmp(nm, "f718", 4)) continue;
            { char p[128]; snprintf(p, sizeof p, "/sys/class/hwmon/%s", h[i]); link_base(p, "device", dv, sizeof dv); }
            so_kvf(o, h[i], C_VALUE, "%s  (%s)", nm, dv);
        }
        dir_free(h, nh);
    }
}

/* ======================================================================
 * sections: udev database (devices, enum, class, properties)
 * ====================================================================== */
static int cmp_ud(const void *a, const void *b) { return strcmp(((const udrec *)a)->path, ((const udrec *)b)->path); }

static void s_devices(srs_out *o)
{
    static const int tabs[] = { 390, 520, 660 };
    static const char *const head[] = { "Device", "Subsystem", "Driver", "Node" };
    static const int hc[] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL };
    int i;
    ud_load();
    so_siv_header(o, "All Devices");
    if (!g_nud) { so_err(o, "udevadm info --export-db returned nothing"); return; }
    so_tabs(o, tabs, 3);
    so_row(o, 4, head, hc);
    for (i = 0; i < g_nud; i++) {
        char su[48], dr[48], nd[96];
        const char *c[4];
        static const int col[] = { C_VALUE, C_PURPLE, C_GREEN, C_GRAY };
        ud_get(&g_ud[i], "U: ", su, sizeof su);
        ud_get(&g_ud[i], "E: DRIVER=", dr, sizeof dr);
        ud_get(&g_ud[i], "N: ", nd, sizeof nd);
        c[0] = g_ud[i].path + (strncmp(g_ud[i].path, "/devices/", 9) ? 0 : 9); c[1] = su; c[2] = dr; c[3] = nd;
        so_row(o, 4, c, col);
    }
    so_par(o);
    so_f(o, C_TEAL, "Total of %d devices in the udev database", g_nud); so_par(o);
}

static void s_enum(srs_out *o)
{
    udrec *v;
    int i;
    ud_load();
    so_siv_header(o, "System Enum (device tree)");
    if (!g_nud || !(v = malloc(sizeof *v * (size_t)g_nud))) { so_err(o, "udev database unavailable"); return; }
    memcpy(v, g_ud, sizeof *v * (size_t)g_nud);
    qsort(v, (size_t)g_nud, sizeof *v, cmp_ud);
    for (i = 0; i < g_nud; i++) {
        const char *p = v[i].path;
        char su[48], b[400];
        int d = 0;
        for (; *p; p++) d += *p == '/';
        ud_get(&v[i], "U: ", su, sizeof su);
        snprintf(b, sizeof b, "%*s%s  [%s]", (d > 1 ? d - 1 : 0) * 2, "", bname(v[i].path), su);
        so_m(o, d <= 2 ? C_BLACK : C_VALUE, b); so_par(o);
    }
    free(v);
}

static void s_class(srs_out *o)
{
    static const int tabs[] = { 200, 260 };
    char **e;
    int n, i;
    so_siv_header(o, "System Device Classes");
    so_tabs(o, tabs, 2);
    n = dir_list("/sys/class", &e);
    for (i = 0; i < n; i++) {
        char p[300], **s;
        int ns, j;
        char names[200] = "";
        snprintf(p, sizeof p, "/sys/class/%s", e[i]);
        ns = dir_list(p, &s);
        for (j = 0; j < ns && strlen(names) < 150; j++) snprintf(names + strlen(names), sizeof names - strlen(names), "%s%s", j ? " " : "", s[j]);
        if (j < ns) snprintf(names + strlen(names), sizeof names - strlen(names), " ...");
        so_t(o, C_TEAL, e[i]); so_tab(o); so_f(o, C_PURPLE, "%d", ns < 0 ? 0 : ns); so_tab(o); so_t(o, C_VALUE, names); so_par(o);
        dir_free(s, ns);
    }
    dir_free(e, n);
}

/* /sys/class entries with details: mode 0 interfaces (=class), 1 interfacex, 2 int-ids, 3 int-names, 4 int-owner */
static void class_walk(srs_out *o, int mode)
{
    static const int tabs[] = { 300, 420, 600 };
    char **e;
    int n, i;
    so_tabs(o, tabs, 3);
    n = dir_list("/sys/class", &e);
    for (i = 0; i < n; i++) {
        char p[300], **s;
        int ns, j;
        snprintf(p, sizeof p, "/sys/class/%s", e[i]);
        if ((ns = dir_list(p, &s)) <= 0) continue;
        for (j = 0; j < ns; j++) {
            char ep[600], devt[32] = "", dv[96] = "", dr[64] = "", md[64] = "", nm[300];
            snprintf(ep, sizeof ep, "%s/%s", p, s[j]);
            snprintf(nm, sizeof nm, "%s/%s", e[i], s[j]);
            at(ep, "dev", devt, sizeof devt);
            if (mode == 2 && !*devt) continue;
            link_base(ep, "device", dv, sizeof dv);
            { char dp[700]; snprintf(dp, sizeof dp, "%s/device", ep); link_base(dp, "driver", dr, sizeof dr); if (mode == 4) mod_of(dp, md, sizeof md); }
            so_t(o, C_TEAL, nm); so_tab(o);
            if (mode == 1) { so_t(o, C_PURPLE, devt); so_tab(o); so_t(o, C_VALUE, dv); so_tab(o); so_t(o, C_GREEN, dr); }
            else if (mode == 2) {
                char real[PATH_MAX], sl[200] = "";
                const udrec *r = NULL;
                so_t(o, C_PURPLE, devt); so_tab(o);
                if (realpath(ep, real) && !strncmp(real, "/sys", 4)) r = ud_find(real + 4);
                if (r) ud_get(r, "N: ", sl, sizeof sl);
                so_f(o, C_VALUE, "%s%s", *sl ? "/dev/" : "", sl);
            } else if (mode == 3) {
                char real[PATH_MAX], sl[200] = "";
                const udrec *r = NULL;
                if (realpath(ep, real) && !strncmp(real, "/sys", 4)) r = ud_find(real + 4);
                if (r) ud_get(r, "S: ", sl, sizeof sl);
                so_t(o, C_VALUE, *sl ? sl : "-");
            } else if (mode == 4) { so_t(o, C_GREEN, *dr ? dr : "-"); so_tab(o); so_t(o, C_PURPLE, md); so_tab(o); so_t(o, C_GRAY, dv); }
            so_par(o);
        }
        dir_free(s, ns);
    }
    dir_free(e, n);
}
static void s_interfaces(srs_out *o) { s_class(o); }
static void s_interfacex(srs_out *o) { so_siv_header(o, "Device Interfaces (class / dev_t / device / driver)"); class_walk(o, 1); }
static void s_int_ids(srs_out *o) { so_siv_header(o, "Interface IDs (dev_t / device node)"); class_walk(o, 2); }
static void s_int_names(srs_out *o) { so_siv_header(o, "Interface Names (udev symlinks)"); class_walk(o, 3); }
static void s_int_owner(srs_out *o) { so_siv_header(o, "Interface Owners"); class_walk(o, 4); }

/* [properties] / [properties:<dev>] */
static void s_properties(srs_out *o)
{
    static const int tabs[] = { 260 };
    int i;
    ud_load();
    so_siv_header(o, "Device Properties");
    so_tabs(o, tabs, 1);
    if (arg(o)) {
        const udrec *r = ud_find(arg(o));
        if (!r) { so_err(o, "No such device in the udev database (use a sysfs name, e.g. properties:0000:03:00.0)"); return; }
        ud_props(o, r);
        return;
    }
    for (i = 0; i < g_nud; i++) {
        char su[32];
        ud_get(&g_ud[i], "U: ", su, sizeof su);
        if (!strcmp(su, "pci") || !strcmp(su, "usb")) { char dt[32]; ud_get(&g_ud[i], "E: DEVTYPE=", dt, sizeof dt); if (strcmp(dt, "usb_interface")) ud_props(o, &g_ud[i]); }
    }
    so_note(o, "PCI and USB devices shown; any device: properties:<name>, e.g. properties:nvme0n1");
}

/* [property] / [property:KEY] - one key across all devices */
static void s_property(srs_out *o)
{
    static const int tabs[] = { 390 };
    const char *k = arg(o) ? arg(o) : "ID_MODEL_FROM_DATABASE";
    char pfx[160];
    int i, n = 0;
    ud_load();
    so_siv_header(o, "Device Property");
    so_tabs(o, tabs, 1);
    so_kv(o, "Property", C_BLACK, k);
    so_par(o);
    snprintf(pfx, sizeof pfx, "E: %s=", k);
    for (i = 0; i < g_nud; i++) {
        char v[400];
        if (!ud_get(&g_ud[i], pfx, v, sizeof v)) continue;
        so_kv(o, g_ud[i].path + (strncmp(g_ud[i].path, "/devices/", 9) ? 0 : 9), C_VALUE, masked_key(k) ? "********" : v);
        n++;
    }
    so_par(o);
    so_f(o, C_TEAL, "%d device%s have %s  (other keys: property:<KEY>, see [devpropkey])", n, n == 1 ? "" : "s", k); so_par(o);
}

typedef struct { char k[64]; int n; } kcount;
static int cmp_kc(const void *a, const void *b) { return strcmp(((const kcount *)a)->k, ((const kcount *)b)->k); }
static void s_devpropkey(srs_out *o)
{
    static const int tabs[] = { 330 };
    kcount *v = NULL;
    int nv = 0, cap = 0, i, j;
    ud_load();
    so_siv_header(o, "Device Property Keys");
    so_tabs(o, tabs, 1);
    for (i = 0; i < g_nud; i++) {
        const char *p = ud_body(&g_ud[i]);
        while (p && *p) {
            if (!strncmp(p, "E: ", 3)) {
                char k[64];
                snprintf(k, sizeof k, "%.*s", (int)strcspn(p + 3, "=\n"), p + 3);
                for (j = 0; j < nv && strcmp(v[j].k, k); j++) ;
                if (j < nv) v[j].n++;
                else {
                    if (nv >= cap) { kcount *nn = realloc(v, sizeof *nn * (size_t)(cap = cap ? cap * 2 : 256)); if (!nn) break; v = nn; }
                    snprintf(v[nv].k, sizeof v[nv].k, "%s", k); v[nv++].n = 1;
                }
            }
            p = strchr(p, '\n');
            if (p) p++;
        }
    }
    if (v) qsort(v, (size_t)nv, sizeof *v, cmp_kc);
    for (i = 0; i < nv; i++) so_kvf(o, v[i].k, C_VALUE, "%d device%s", v[i].n, v[i].n == 1 ? "" : "s");
    so_par(o);
    so_f(o, C_TEAL, "Total of %d keys across %d devices", nv, g_nud); so_par(o);
    free(v);
}

/* [ghost-devices] - udev database entries whose device no longer exists */
static void s_ghost(srs_out *o)
{
    static const int tabs[] = { 200 };
    char **e;
    int n, i, g = 0;
    so_siv_header(o, "Ghost (non-present) Devices");
    so_tabs(o, tabs, 1);
    n = dir_list("/run/udev/data", &e);
    for (i = 0; i < n; i++) {
        char p[400] = "", *nm = e[i];
        if ((nm[0] == 'c' || nm[0] == 'b') && isdigit((unsigned char)nm[1]))
            snprintf(p, sizeof p, "/sys/dev/%s/%s", nm[0] == 'c' ? "char" : "block", nm + 1);
        else if (nm[0] == '+') {
            char sub[64], *c = strchr(nm + 1, ':');
            if (!c) continue;
            snprintf(sub, sizeof sub, "%.*s", (int)(c - nm - 1), nm + 1);
            if (!strcmp(sub, "module") || !strcmp(sub, "bus") || !strcmp(sub, "class")) snprintf(p, sizeof p, "/sys/%s/%s", sub, c + 1);
            else if (!strcmp(sub, "drivers")) {
                const char *c2 = strchr(c + 1, ':');
                if (!c2) continue;
                snprintf(p, sizeof p, "/sys/bus/%.*s/drivers/%s", (int)(c2 - c - 1), c + 1, c2 + 1);
            } else {
                char sb[128], sc[128];
                snprintf(sb, sizeof sb, "/sys/bus/%s", sub);
                snprintf(sc, sizeof sc, "/sys/class/%s", sub);
                if (!path_exists(sb) && !path_exists(sc)) continue;   /* subsystem with no sysfs dir: cannot check */
                snprintf(p, sizeof p, "%s/devices/%s", sb, c + 1);
                if (!path_exists(p)) snprintf(p, sizeof p, "%s/%s", sc, c + 1);
            }
        } else continue;
        if (path_exists(p)) continue;
        so_kv(o, e[i], C_ORANGE, "stale udev record (device not present)");
        g++;
    }
    dir_free(e, n);
    if (!g) so_note(o, "No stale entries in /run/udev/data: Linux drops records when devices go away.");
    so_note(o, "Windows keeps non-present devices in the registry; Linux has no persistent device store beyond udev/hwdb.");
}

static void s_codevice(srs_out *o)
{
    static const char *const dirs[] = { "/usr/lib/udev/rules.d", "/etc/udev/rules.d", "/run/udev/rules.d", NULL };
    int d;
    so_siv_header(o, "Co-device installers (Linux: udev rules)");
    for (d = 0; dirs[d]; d++) {
        char **e;
        int n = dir_list(dirs[d], &e), i;
        so_b(o, C_VALUE, dirs[d]); so_f(o, C_GRAY, "  (%d)", n < 0 ? 0 : n); so_par(o);
        for (i = 0; i < n; i++) { so_t(o, C_VALUE, "  "); so_t(o, C_VALUE, e[i]); so_par(o); }
        dir_free(e, n);
    }
}

/* ======================================================================
 * sections: I/O ports, memory, interrupts, ACPI PM, EC, GPIO
 * ====================================================================== */
static void s_io_ports(srs_out *o) { so_siv_header(o, "I/O Ports"); res_map(o, "/proc/ioports", "ioports.txt", NULL, -1); }

static void s_io_dump(srs_out *o)
{
    so_siv_header(o, "I/O Port Dump");
    so_note(o, "srs never reads /dev/port (raw port reads can wedge SMBus/EC state). I/O port assignments instead:");
    so_par(o);
    res_map(o, "/proc/ioports", "ioports.txt", NULL, -1);
}

static void interrupts(srs_out *o)
{
    char *t, *p, *sv;
    int first = 1;
    if (rd_all("/proc/interrupts", &t) < 0) return;
    for (p = strtok_r(t, "\n", &sv); p; p = strtok_r(NULL, "\n", &sv)) {
        char irq[16], *q = p, rest[300] = "";
        unsigned long long sum = 0;
        if (first) { first = 0; continue; }
        while (*q == ' ') q++;
        snprintf(irq, sizeof irq, "%.*s", (int)strcspn(q, ":"), q);
        q = strchr(q, ':');
        if (!q) continue;
        q++;
        for (;;) {
            char *e;
            unsigned long long v;
            while (*q == ' ') q++;
            if (!isdigit((unsigned char)*q)) break;
            v = strtoull(q, &e, 10);
            sum += v;
            q = e;
        }
        snprintf(rest, sizeof rest, "%s", q);
        {
            char line[400];
            snprintf(line, sizeof line, "%-6s %14llu  %s", irq, sum, rest);
            so_m(o, isdigit((unsigned char)irq[0]) ? C_VALUE : C_GRAY, line); so_par(o);
        }
    }
    free(t);
}

static void s_sys_res(srs_out *o)
{
    char *d;
    so_siv_header(o, "System Resources");
    so_b(o, C_VALUE, "Interrupts (IRQ, total count, controller / devices)"); so_par(o);
    interrupts(o);
    so_par(o);
    so_b(o, C_VALUE, "DMA channels"); so_par(o);
    if (rd_all("/proc/dma", &d) >= 0) { so_lines(o, C_VALUE, d); free(d); }
    so_par(o);
    so_b(o, C_VALUE, "I/O ports"); so_par(o);
    res_map(o, "/proc/ioports", "ioports.txt", NULL, -1);
    so_b(o, C_VALUE, "Memory map"); so_par(o);
    res_map(o, "/proc/iomem", "iomem.txt", NULL, 1);
}

static void s_sys_hal(srs_out *o)
{
    so_siv_header(o, "System HAL Resources");
    so_b(o, C_VALUE, "Physical memory map (top level)"); so_par(o);
    res_map(o, "/proc/iomem", "iomem.txt", NULL, 0);
    so_b(o, C_VALUE, "I/O port map (top level)"); so_par(o);
    res_map(o, "/proc/ioports", "ioports.txt", NULL, 0);
}

static void s_pci_hal(srs_out *o)
{
    static const char *const pat[] = { "PCI Bus", "PCI ECAM", "MMCONFIG", "pcie", "PCI", NULL };
    so_siv_header(o, "PCI HAL (root bus windows)");
    so_b(o, C_VALUE, "Memory windows"); so_par(o);
    res_map(o, "/proc/iomem", "iomem.txt", pat, 2);
    so_b(o, C_VALUE, "I/O windows"); so_par(o);
    res_map(o, "/proc/ioports", "ioports.txt", pat, 1);
    {
        char **e;
        int n = dir_list("/sys/class/pci_bus", &e), i;
        so_kvf(o, "PCI buses", C_TEAL, "%d", n < 0 ? 0 : n);
        for (i = 0; i < n; i++) so_t(o, C_VALUE, i ? " " : ""), so_t(o, C_VALUE, e[i]);
        so_par(o);
        dir_free(e, n);
    }
}

static void s_pci_mmio(srs_out *o)
{
    static const char *const pat[] = { "ECAM", "MMCONFIG", NULL };
    so_siv_header(o, "PCI MMIO (enhanced configuration access)");
    so_kv(o, "MCFG table", C_VALUE, path_exists("/sys/firmware/acpi/tables/MCFG") ? "present (/sys/firmware/acpi/tables/MCFG)" : "absent");
    if (so_root_file("acpi-tables/MCFG")) {
        FILE *f = fopen(so_root_file("acpi-tables/MCFG"), "rb");
        unsigned char b[64];
        if (f) {
            size_t l = fread(b, 1, sizeof b, f);
            fclose(f);
            if (l >= 60) {
                unsigned long long base = 0;
                int k;
                for (k = 7; k >= 0; k--) base = base << 8 | b[44 + k];
                so_kvf(o, "ECAM base", C_BLACK, "0x%llX  segment %u  buses %u-%u", base, b[52] | b[53] << 8, b[54], b[55]);
            }
        }
    } else { so_t(o, C_ORANGE, "ECAM base address: " NEEDS_ROOT); so_par(o); }
    so_par(o);
    res_map(o, "/proc/iomem", "iomem.txt", pat, -1);
}

static void s_pm1_area(srs_out *o)
{
    static const char *const pat[] = { "ACPI", "PM1", "PM_TMR", "GPE", NULL };
    const char *f = so_root_file("acpi-tables/FACP");
    so_siv_header(o, "ACPI PM1 / PM2 / Timer / GPE blocks");
    if (f) {
        unsigned char b[256];
        FILE *fp = fopen(f, "rb");
        size_t l = fp ? fread(b, 1, sizeof b, fp) : 0;
        static const char *const nm[] = { "PM1a_EVT_BLK", "PM1b_EVT_BLK", "PM1a_CNT_BLK", "PM1b_CNT_BLK", "PM2_CNT_BLK", "PM_TMR_BLK", "GPE0_BLK", "GPE1_BLK" };
        static const int lenoff[] = { 88, 88, 89, 89, 90, 91, 92, 93 };
        static const int tabs[] = { 170 };
        int k;
        if (fp) fclose(fp);
        so_tabs(o, tabs, 1);
        for (k = 0; l >= 94 && k < 8; k++) {
            unsigned v = b[56 + k * 4] | b[57 + k * 4] << 8 | (unsigned)b[58 + k * 4] << 16 | (unsigned)b[59 + k * 4] << 24;
            so_kvf(o, nm[k], v ? C_VALUE : C_GRAY, "0x%04X  length %u", v, b[lenoff[k]]);
        }
        so_note(o, "From the FADT (srs-root cache). Register contents are not read (no /dev/port).");
    } else {
        so_t(o, C_ORANGE, "FADT block addresses: " NEEDS_ROOT); so_par(o);
    }
    so_par(o);
    res_map(o, "/proc/ioports", "ioports.txt", pat, -1);
    {
        char *t;
        if (rd_all("/sys/firmware/acpi/pm_profile", &t) >= 0) { so_kvf(o, "PM profile", C_VALUE, "%.*s", (int)strcspn(t, "\n"), t); free(t); }
    }
}

static void s_embedded(srs_out *o)
{
    static const int tabs[] = { 170 };
    static const char *const pat[] = { "EC ", "ec", "0062", "0066", NULL };
    char p[128], ph[64] = "";
    so_siv_header(o, "Embedded Controller");
    so_tabs(o, tabs, 1);
    if (!path_exists("/sys/bus/acpi/devices/PNP0C09:00")) { so_kv(o, "EC", C_ORANGE, "No ACPI PNP0C09 device"); return; }
    rd_str("/sys/bus/acpi/devices/PNP0C09:00/path", p, sizeof p);
    link_base("/sys/bus/acpi/devices/PNP0C09:00", "physical_node", ph, sizeof ph);
    so_kv(o, "ACPI device", C_BLACK, "PNP0C09:00 (ACPI Embedded Controller)");
    so_kv(o, "Namespace path", C_VALUE, p);
    so_kv(o, "Bound to", C_VALUE, *ph ? ph : "(ACPI core EC driver)");
    so_kv(o, "ECDT table", C_VALUE, path_exists("/sys/firmware/acpi/tables/ECDT") ? "present" : "absent (EC described in DSDT only)");
    so_kv(o, "ec_sys debugfs", C_VALUE, path_exists("/sys/module/ec_sys") ? "module loaded (/sys/kernel/debug/ec, root)" : "module not loaded");
    so_par(o);
    res_map(o, "/proc/ioports", "ioports.txt", pat, -1);
    so_note(o, "EC RAM is never read by srs (port 0x62/0x66 traffic races the firmware).");
}

static void s_io_gpio(srs_out *o)
{
    static const int tabs[] = { 130 };
    char **e;
    int n, i;
    so_siv_header(o, "GPIO Controllers");
    so_tabs(o, tabs, 1);
    n = dir_list("/sys/bus/gpio/devices", &e);
    for (i = 0; i < n; i++) {
        char dir[256], rp[PATH_MAX], par[256], dr[64] = "", hid[64] = "";
        snprintf(dir, sizeof dir, "/sys/bus/gpio/devices/%s", e[i]);
        if (!realpath(dir, rp)) continue;
        snprintf(par, sizeof par, "%.*s", (int)(strrchr(rp, '/') - rp), rp);
        link_base(par, "driver", dr, sizeof dr);
        snprintf(hid, sizeof hid, "%s", bname(par));
        { char nm[256]; char id[32]; snprintf(id, sizeof id, "%.*s", (int)strcspn(hid, ":"), hid); pnp_name(id, nm, sizeof nm);
          so_kvf(o, e[i], C_VALUE, "%s  %s  driver %s", hid, nm, *dr ? dr : "(none)"); }
    }
    if (n <= 0) so_note(o, "no GPIO chips");
    dir_free(e, n);
    so_par(o);
    so_head(o, "Pin states");
    {
        const char *rf = so_root_file("gpio.txt");
        char *txt = NULL;
        if (rf && rd_all(rf, &txt) > 0) {
            so_lines(o, C_VALUE, txt);
            so_par(o);
            so_note(o, "The kernel's own GPIO table (debugfs gpio), copied once by srs-root: lines in use, their direction and level. Read-only.");
        } else if (rf) so_note(o, "The kernel's GPIO table is empty.");
        else if (so_root_file("collected.txt")) { so_t(o, C_ORANGE, "Not in the root cache (debugfs not mounted, or the cache is older): run Menu > Run as root again"); so_par(o); }
        else { so_t(o, C_ORANGE, NEEDS_ROOT); so_par(o); }
        free(txt);
    }
}

static void s_io_mmio(srs_out *o)
{
    static const char *const pat[] = { "AMDI", "IOAPIC", "HPET", "Local APIC", "amd_iommu", "pnp", "Reserved", NULL };
    so_siv_header(o, "AMD PM2 / ACPI MMIO");
    so_note(o, "FCH ACPI MMIO (0xFED80000 block: PM, SMBus, GPIO, IOMUX, misc) is described below; registers are not read (no /dev/mem).");
    so_par(o);
    res_map(o, "/proc/iomem", "iomem.txt", pat, -1);
}

static void s_io_asus(srs_out *o)
{
    static const int tabs[] = { 170 };
    static const char *const pat[] = { "nct", "asus", "it87", NULL };
    char v[128] = "", b[128] = "", **h;
    int n, i;
    so_siv_header(o, "ASUS I/O");
    so_tabs(o, tabs, 1);
    rd_str("/sys/class/dmi/id/board_vendor", v, sizeof v);
    rd_str("/sys/class/dmi/id/board_name", b, sizeof b);
    so_kvf(o, "Board", C_BLACK, "%s %s", v, b);
    n = dir_list("/sys/class/hwmon", &h);
    for (i = 0; i < n; i++) {
        char nm[64];
        rd_fmt(nm, sizeof nm, "/sys/class/hwmon/%s/name", h[i]);
        if (!strncmp(nm, "nct", 3) || strstr(nm, "asus")) so_kvf(o, h[i], C_VALUE, "%s (kernel driver - sensors in [sensors])", nm);
    }
    dir_free(h, n);
    so_kv(o, "asus-wmi", C_VALUE, path_exists("/sys/module/asus_wmi") ? "loaded" : "not loaded");
    so_kv(o, "asus_ec_sensors", C_VALUE, path_exists("/sys/module/asus_ec_sensors") ? "loaded" : "not loaded");
    so_par(o);
    res_map(o, "/proc/ioports", "ioports.txt", pat, -1);
    so_note(o, "Super I/O registers are accessed only by the kernel driver; srs does not touch /dev/port.");
}

/* ======================================================================
 * sections: SMBus / I2C (names only, never opened)
 * ====================================================================== */
static void s_smb(srs_out *o)
{
    static const int tabs[] = { 130, 470 };
    char **e;
    int n, i;
    pci_load();
    so_siv_header(o, "SMBus / I2C Adapters");
    so_tabs(o, tabs, 2);
    for (i = 0; i < g_npci; i++)
        if ((g_pci[i].cls >> 8) == 0x0C05) so_kvf(o, "Controller", C_BLACK, "%s  %s %s rev %02X  driver %s", g_pci[i].slot, g_pci[i].vname, g_pci[i].dname, g_pci[i].rev, g_pci[i].driver);
    so_par(o);
    n = dir_list("/sys/bus/i2c/devices", &e);
    for (i = 0; i < n; i++) {
        char nm[128], dir[256], par[64] = "";
        if (strncmp(e[i], "i2c-", 4)) continue;
        snprintf(dir, sizeof dir, "/sys/bus/i2c/devices/%s", e[i]);
        if (!at(dir, "name", nm, sizeof nm)) continue;
        link_base(dir, "device", par, sizeof par);
        so_t(o, C_TEAL, e[i]); so_tab(o); so_t(o, strstr(nm, "SMBus") ? C_BLACK : C_VALUE, nm); so_tab(o); so_t(o, C_GRAY, par); so_par(o);
    }
    dir_free(e, n);
    so_par(o);
    so_note(o, "Adapter names from sysfs only; /dev/i2c-* is never opened and no bus is scanned.");
}

static void s_smb_bus(srs_out *o)
{
    static const int tabs[] = { 130, 300, 450 };
    static const char *const head[] = { "Client", "Name", "Driver", "Adapter" };
    static const int hc[] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL };
    char **e;
    int n, i, k = 0;
    so_siv_header(o, "SMBus Devices (kernel-bound I2C clients)");
    so_tabs(o, tabs, 3);
    so_row(o, 4, head, hc);
    n = dir_list("/sys/bus/i2c/devices", &e);
    for (i = 0; i < n; i++) {
        char dir[256], nm[64] = "", dr[64] = "", ad[160] = "", an[160] = "";
        const char *c[4];
        static const int col[] = { C_TEAL, C_VALUE, C_GREEN, C_GRAY };
        if (!strncmp(e[i], "i2c-", 4) && isdigit((unsigned char)e[i][4])) continue;
        snprintf(dir, sizeof dir, "/sys/bus/i2c/devices/%s", e[i]);
        at(dir, "name", nm, sizeof nm);
        link_base(dir, "driver", dr, sizeof dr);
        { char rp[PATH_MAX]; if (realpath(dir, rp)) { char *s = strrchr(rp, '/'); if (s) { *s = 0; snprintf(ad, sizeof ad, "%s", bname(rp)); } } }
        if (!strncmp(ad, "i2c-", 4)) { char p[256]; snprintf(p, sizeof p, "/sys/bus/i2c/devices/%s/name", ad); rd_str(p, an, sizeof an); }
        snprintf(ad + strlen(ad), sizeof ad - strlen(ad), "%s%s", *an ? " " : "", an);
        c[0] = e[i]; c[1] = nm; c[2] = *dr ? dr : "(none)"; c[3] = ad;
        so_row(o, 4, c, col);
        k++;
    }
    dir_free(e, n);
    so_par(o);
    so_f(o, C_TEAL, "Total of %d I2C client devices (bus-addr; 0x50-0x57 = SPD5118 hub on DDR5)", k); so_par(o);
}

static void s_smb_sts(srs_out *o)
{
    static const char *const pat[] = { "piix4", "smbus", NULL };
    so_siv_header(o, "SMBus Controller Status");
    res_map(o, "/proc/ioports", "ioports.txt", pat, -1);
    so_note(o, "Status registers are not read: touching the PIIX4 SMBus ports races the kernel driver and RGB/sensor daemons.");
}

static void s_smbus_setup(srs_out *o)
{
    static const int tabs[] = { 200 };
    char **e;
    int n, i;
    so_siv_header(o, "SMBus Setup");
    so_tabs(o, tabs, 1);
    so_kv(o, "Probing", C_ORANGE, "disabled - srs never scans SMBus/I2C slaves (CoolerControl/liquidctl/OpenRGB and the kernel own the bus)");
    so_kv(o, "Data source", C_VALUE, "kernel-instantiated clients (/sys/bus/i2c/devices/<bus>-<addr>)");
    n = dir_list("/sys/bus/i2c/drivers", &e);
    for (i = 0; i < n; i++) {
        char p[256], **s;
        int ns, j, cnt = 0;
        snprintf(p, sizeof p, "/sys/bus/i2c/drivers/%s", e[i]);
        if ((ns = dir_list(p, &s)) > 0) { for (j = 0; j < ns; j++) cnt += isdigit((unsigned char)s[j][0]) != 0; dir_free(s, ns); }
        so_kvf(o, i ? "" : "I2C drivers", C_VALUE, "%-20s %d client%s", e[i], cnt, cnt == 1 ? "" : "s");
    }
    dir_free(e, n);
}

static void s_smbus_system(srs_out *o)
{
    static const int tabs[] = { 200 };
    char **e;
    int n, i, nodes = 0;
    so_siv_header(o, "System SMBus Access");
    so_tabs(o, tabs, 1);
    so_kv(o, "i2c-dev module", C_VALUE, path_exists("/sys/module/i2c_dev") ? "loaded (/dev/i2c-* nodes exist)" : "not loaded (no /dev/i2c-* nodes)");
    n = dir_list("/sys/class/i2c-dev", &e);
    for (i = 0; i < n; i++) {
        char p[64];
        struct stat st;
        snprintf(p, sizeof p, "/dev/%s", e[i]);
        if (!stat(p, &st)) { so_kvf(o, p, C_VALUE, "mode %04o uid %u gid %u", st.st_mode & 07777, st.st_uid, st.st_gid); nodes++; }
    }
    dir_free(e, n);
    so_kvf(o, "Device nodes", C_VALUE, "%d (stat only, never opened)", nodes);
    so_kv(o, "piix4_smbus", C_VALUE, path_exists("/sys/module/i2c_piix4") ? "loaded" : "not loaded");
}

/* ======================================================================
 * sections: serial / COM / ports
 * ====================================================================== */
static const char *uart_name(long t)
{
    static const char *const n[] = { "unknown", "8250", "16450", "16550", "16550A", "Cirrus", "16650", "16650V2", "16750", "Startech", "16C950", "16654", "16850", "RSA" };
    return t >= 0 && t < (long)(sizeof n / sizeof *n) ? n[t] : "other";
}
static void s_serial(srs_out *o)
{
    static const int tabs[] = { 90, 170, 230, 340, 470 };
    static const char *const head[] = { "Port", "I/O", "IRQ", "UART", "Base clock", "Device" };
    static const int hc[] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL };
    char **e, *d;
    int n, i, real = 0, stubs = 0;
    so_siv_header(o, "Serial COM Ports");
    so_tabs(o, tabs, 5);
    so_row(o, 6, head, hc);
    n = dir_list("/sys/class/tty", &e);
    for (i = 0; i < n; i++) {
        char dir[256], t[16] = "", port[32] = "", irq[16] = "", clk[32] = "", dv[64] = "", hz[32] = "";
        const char *c[6];
        static const int col[] = { C_TEAL, C_VALUE, C_VALUE, C_GREEN, C_VALUE, C_GRAY };
        snprintf(dir, sizeof dir, "/sys/class/tty/%s", e[i]);
        { char p[300]; snprintf(p, sizeof p, "%s/device", dir); if (!path_exists(p)) continue; }
        link_base(dir, "device", dv, sizeof dv);
        if (at(dir, "type", t, sizeof t) && atol(t) == 0) { stubs++; continue; }   /* 8250 placeholder (PORT_UNKNOWN) */
        at(dir, "port", port, sizeof port); at(dir, "irq", irq, sizeof irq); at(dir, "uartclk", clk, sizeof clk);
        if (*clk) snprintf(hz, sizeof hz, "%.4f MHz", atof(clk) / 1e6);
        c[0] = e[i]; c[1] = port; c[2] = irq; c[3] = *t ? uart_name(atol(t)) : "usb/other"; c[4] = hz; c[5] = dv;
        so_row(o, 6, c, col);
        real++;
    }
    dir_free(e, n);
    so_par(o);
    so_f(o, C_TEAL, "%d real port%s; %d unconfigured 8250 placeholders (nr_uarts) hidden", real, real == 1 ? "" : "s", stubs); so_par(o);
    so_par(o);
    so_b(o, C_VALUE, "TTY drivers (/proc/tty/drivers)"); so_par(o);
    if (rd_all("/proc/tty/drivers", &d) >= 0) { so_lines(o, C_VALUE, d); free(d); }
    so_note(o, "Line statistics (/proc/tty/driver/serial) need root.");
}
static void s_arbiter(srs_out *o)
{
    static const char *const pat[] = { "serial", NULL };
    s_serial(o);
    so_par(o);
    so_b(o, C_VALUE, "COM resources (Linux equivalent of the COM port arbiter)"); so_par(o);
    res_map(o, "/proc/ioports", "ioports.txt", pat, -1);
}

/* ======================================================================
 * absent hardware with a useful probe
 * ====================================================================== */
static void s_thunder(srs_out *o)
{
    static const int tabs[] = { 170 };
    int i, usb4 = 0;
    pci_load();
    so_siv_header(o, "Thunderbolt / USB4");
    so_tabs(o, tabs, 1);
    so_kv(o, "/sys/bus/thunderbolt", C_VALUE, path_exists("/sys/bus/thunderbolt") ? "present" : "absent (thunderbolt module not loaded)");
    for (i = 0; i < g_npci; i++)
        if ((g_pci[i].cls >> 8) == 0x0C03 && (g_pci[i].cls & 0xFF) == 0x40) { so_kvf(o, "USB4 host", C_BLACK, "%s  %s %s", g_pci[i].slot, g_pci[i].vname, g_pci[i].dname); usb4++; }
        else if (strstr(g_pci[i].dname, "Thunderbolt") && (g_pci[i].cls >> 8) != 0x0604) { so_kvf(o, "Thunderbolt", C_BLACK, "%s  %s %s", g_pci[i].slot, g_pci[i].vname, g_pci[i].dname); usb4++; }
    {   /* domains and routers the thunderbolt driver enumerated */
        char **nm, p[400], a[128], b[128], gen[16], nv[32], sec[32];
        int n = dir_list("/sys/bus/thunderbolt/devices", &nm), k;
        for (k = 0; k < n; k++) {
            if (!strncmp(nm[k], "domain", 6)) {
                snprintf(p, sizeof p, "/sys/bus/thunderbolt/devices/%s/security", nm[k]);
                if (!rd_str(p, sec, sizeof sec)) snprintf(sec, sizeof sec, "-");
                so_kvf(o, nm[k], C_TEAL, "security level %s", sec);
                usb4++;
                continue;
            }
            if (strchr(nm[k], ':') || strchr(nm[k], '.')) continue;   /* services / retimers */
            rd_fmt(a, sizeof a, "/sys/bus/thunderbolt/devices/%s/vendor_name", nm[k]);
            rd_fmt(b, sizeof b, "/sys/bus/thunderbolt/devices/%s/device_name", nm[k]);
            if (!rd_fmt(gen, sizeof gen, "/sys/bus/thunderbolt/devices/%s/generation", nm[k])) snprintf(gen, sizeof gen, "?");
            if (!rd_fmt(nv, sizeof nv, "/sys/bus/thunderbolt/devices/%s/nvm_version", nm[k])) snprintf(nv, sizeof nv, "-");
            if (atoi(gen) >= 4) so_kvf(o, nm[k], C_BLACK, "%s %s  (USB4, NVM %s)", a, b, nv);
            else so_kvf(o, nm[k], C_BLACK, "%s %s  (Thunderbolt %s, NVM %s)", a, b, gen, nv);
        }
        dir_free(nm, n);
    }
    if (!usb4) {
        so_kv(o, "USB4 controller", C_ORANGE, "none enumerated on the PCI bus");
        so_note(o, "If the board or a card has a Thunderbolt / USB4 host, it is disabled in firmware or not powered up on");
        so_note(o, "this boot, so no Thunderbolt/USB4 domain exists.");
    }
}
static void s_firewire(srs_out *o)
{
    int i, n = 0;
    pci_load();
    so_siv_header(o, "FireWire (IEEE 1394)");
    for (i = 0; i < g_npci; i++) if ((g_pci[i].cls >> 8) == 0x0C00) { so_kvf(o, "1394 controller", C_BLACK, "%s %s", g_pci[i].slot, g_pci[i].dname); n++; }
    so_kv(o, "/sys/bus/firewire", C_VALUE, path_exists("/sys/bus/firewire") ? "present" : "absent");
    if (!n) so_note(o, "No PCI class 0C00 (FireWire) controller present.");
}
static void s_pcm(srs_out *o)
{
    int i, n = 0;
    pci_load();
    so_siv_header(o, "PCMCIA / CardBus");
    for (i = 0; i < g_npci; i++) if ((g_pci[i].cls >> 8) == 0x0607 || (g_pci[i].cls >> 8) == 0x0605) n++;
    so_kv(o, "/sys/class/pcmcia_socket", C_VALUE, path_exists("/sys/class/pcmcia_socket") ? "present" : "absent");
    so_kvf(o, "CardBus/PCMCIA bridges", C_VALUE, "%d (PCI class 0605/0607)", n);
}
static void s_pci_flag(srs_out *o)
{
    char c[4096], *p, *sv;
    int n = 0;
    so_siv_header(o, "PCI Flags (kernel command line)");
    rd_str("/proc/cmdline", c, sizeof c);
    for (p = strtok_r(c, " ", &sv); p; p = strtok_r(NULL, " ", &sv))
        if (!strncmp(p, "pci", 3) || strstr(p, "iommu") || !strncmp(p, "pcie", 4) || strstr(p, "aspm")) { so_kv(o, "", C_VALUE, p); n++; }
    if (!n) so_note(o, "No pci=/pcie_*/iommu options on the kernel command line (Windows: HAL PCI frigs).");
}
static void s_npu(srs_out *o)
{
    char **e;
    int n;
    so_siv_header(o, "NPU Names");
    n = dir_list("/sys/class/accel", &e);
    so_kvf(o, "/sys/class/accel", C_VALUE, "%d device%s", n < 0 ? 0 : n, n == 1 ? "" : "s");
    dir_free(e, n);
    so_kv(o, "amdxdna", C_VALUE, path_exists("/sys/module/amdxdna") ? "loaded" : "not loaded");
    so_note(o, "Desktop Ryzen 9000 (Granite Ridge) has no XDNA NPU.");
}

const srs_item sec_bus[] = {
    { "arbiter", "hardware", "COM port resource arbiter", B_WINONLY, 0, "/sys/class/tty; /proc/ioports", s_arbiter, 0, "bus" },
    { "chipsets", "hardware", "Chipsets", B_LINUX, 0, "/sys/bus/pci/devices (class 0600/0601/0C05/0806)", s_chipsets, 0, "bus" },
    { "class", "hardware", "System device classes", B_LINUX, 0, "/sys/class", s_class, 0, "bus" },
    { "codevice", "hardware", "Co-device installers", B_WINONLY, 0, "udev rules.d", s_codevice, 0, "bus" },
    { "compatible", "hardware", "Compatible IDs", B_LINUX, 0, "modalias (pci, usb interfaces, acpi)", s_compatible, 0, "bus" },
    { "device-ids", "hardware", "Device IDs", B_LINUX, 0, "sysfs IDs + pci.ids/usb.ids chain", s_device_ids, 0, "bus" },
    { "devices", "hardware", "All devices", B_LINUX, 0, "udevadm info --export-db", s_devices, 0, "bus" },
    { "dev-owner", "hardware", "Device owners", B_LINUX, 0, "/sys/bus/{pci,usb}/devices/*/driver", s_dev_owner, 0, "bus" },
    { "devpropkey", "hardware", "Device property keys", B_LINUX, 0, "udevadm info --export-db", s_devpropkey, 0, "bus" },
    { "embedded", "hardware", "Embedded controller I/O", B_LINUX, 1, "/sys/bus/acpi/devices/PNP0C09:00; ioports.txt", s_embedded, 0, "bus" },
    { "enum", "hardware", "System enum (device tree)", B_LINUX, 0, "udevadm info --export-db", s_enum, 0, "bus" },
    { "firewire", "hardware", "FireWire", B_ABSENT, 0, "/sys/bus/firewire; PCI class 0C00", s_firewire, 0, "bus" },
    { "ghost-devices", "hardware", "Ghost (non-present) devices", B_WINONLY, 0, "/run/udev/data vs sysfs", s_ghost, 0, "bus" },
    { "hid-all", "hardware", "HID all devices", B_LINUX, 0, "/sys/bus/hid/devices", s_hid_all, 0, "bus" },
    { "hid-bus", "hardware", "HID bus", B_LINUX, 0, "/sys/bus/hid/devices", s_hid_bus, 0, "bus" },
    { "hid-tree", "hardware", "HID tree", B_LINUX, 0, "/sys/bus/hid/devices; /sys/bus/usb/devices", s_hid_tree, 0, "bus" },
    { "interfaces", "hardware", "Device interfaces", B_LINUX, 0, "/sys/class", s_interfaces, 0, "bus" },
    { "interfacex", "hardware", "Device interfaces (extended)", B_LINUX, 0, "/sys/class/*/*", s_interfacex, 0, "bus" },
    { "int-ids", "hardware", "Interface IDs", B_LINUX, 0, "/sys/class/*/*/dev; udev", s_int_ids, 0, "bus" },
    { "int-names", "hardware", "Interface names", B_LINUX, 0, "/sys/class/*/*; udev symlinks", s_int_names, 0, "bus" },
    { "int-owner", "hardware", "Interface owner", B_LINUX, 0, "/sys/class/*/*/device/driver", s_int_owner, 0, "bus" },
    { "io-asus", "hardware", "ASUS I/O", B_LINUX, 1, "hwmon nct6775/asus; ioports.txt", s_io_asus, 0, "bus" },
    { "io-dump", "hardware", "I/O port dump", B_LINUX, 1, "ioports.txt (no /dev/port)", s_io_dump, 0, "bus" },
    { "io-gpio", "hardware", "I/O GPIO", B_LINUX, 0, "/sys/bus/gpio/devices; gpio.txt cache (debugfs gpio, srs-root)", s_io_gpio, 0, "bus" },
    { "io-mmio", "hardware", "I/O PM2 MMIO", B_LINUX, 1, "/proc/iomem; iomem.txt", s_io_mmio, 0, "bus" },
    { "io-ports", "hardware", "I/O ports", B_LINUX, 1, "/proc/ioports; ioports.txt", s_io_ports, 0, "bus" },
    { "isa-bus", "hardware", "ISA bus", B_LINUX, 0, "/sys/bus/pnp/devices; /sys/bus/isa", s_isa_bus, 0, "bus" },
    { "mch", "hardware", "Chipset MCH", B_LINUX, 1, "/sys/bus/pci/devices/0000:00:00.0; pci-all.txt", s_mch, 0, "bus" },
    { "npu-names", "hardware", "NPU names", B_ABSENT, 0, "/sys/class/accel", s_npu, 0, "bus" },
    { "pci-all", "hardware", "PCI all", B_LINUX, 1, "/sys/bus/pci/devices; pci-all.txt", s_pci_all, 0, "bus" },
    { "pci-bars", "hardware", "PCI BARs", B_LINUX, 0, "/sys/bus/pci/devices/*/resource", s_pci_bars, 0, "bus" },
    { "pci-bus", "hardware", "PCI bus", B_LINUX, 0, "/sys/bus/pci/devices; pci.ids", s_pci_bus, 0, "bus" },
    { "pci-dev", "hardware", "PCI devices", B_LINUX, 0, "/sys/bus/pci/devices; pci.ids", s_pci_dev, 0, "bus" },
    { "pci-flag", "windows", "PCI flags", B_WINONLY, 0, "/proc/cmdline", s_pci_flag, 0, "bus" },
    { "pci-hal", "hardware", "PCI HAL", B_LINUX, 1, "/proc/iomem; /proc/ioports; iomem.txt", s_pci_hal, 0, "bus" },
    { "pci-info", "hardware", "PCI ID database", B_LINUX, 0, "pci.ids", s_pci_info, 0, "bus" },
    { "pci-mmio", "hardware", "PCI MMIO", B_LINUX, 1, "MCFG; /proc/iomem; acpi-tables/MCFG", s_pci_mmio, 0, "bus" },
    { "pcm-all", "hardware", "PCMCIA all", B_ABSENT, 0, "/sys/class/pcmcia_socket", s_pcm, 0, "bus" },
    { "pcm-bus", "hardware", "PCMCIA bus", B_ABSENT, 0, "/sys/class/pcmcia_socket", s_pcm, 0, "bus" },
    { "pm1-area", "hardware", "I/O PM1 data", B_LINUX, 1, "acpi-tables/FACP; /proc/ioports", s_pm1_area, 0, "bus" },
    { "pnp", "hardware", "PnP manager", B_LINUX, 0, "/sys/bus/pnp/devices/*/resources", s_pnp, 0, "bus" },
    { "pnp-dev", "hardware", "PnP devices", B_LINUX, 0, "/sys/bus/pnp/devices", s_pnp_dev, 0, "bus" },
    { "pnp-info", "hardware", "PnP ID database", B_LINUX, 0, "pnp.ids", s_pnp_info, 0, "bus" },
    { "pnp-man", "hardware", "PnP manager details", B_LINUX, 0, "/sys/bus/acpi/devices", s_pnp_man, 0, "bus" },
    { "ports", "hardware", "COM ports", B_LINUX, 0, "/sys/class/tty; /proc/tty/drivers", s_serial, 0, "bus" },
    { "properties", "hardware", "Device properties", B_LINUX, 0, "udevadm info --export-db", s_properties, 0, "bus" },
    { "property", "hardware", "Device property", B_LINUX, 0, "udevadm info --export-db", s_property, 0, "bus" },
    { "serial", "hardware", "Serial COM ports", B_LINUX, 0, "/sys/class/tty; /proc/tty/drivers", s_serial, 0, "bus" },
    { "si-usb", "hardware", "SI USB", B_LINUX, 0, "/sys/bus/usb/devices (root hubs)", s_si_usb, 0, "bus" },
    { "smb", "hardware", "SMBus", B_LINUX, 0, "/sys/bus/i2c/devices/i2c-*/name", s_smb, 0, "bus" },
    { "smb-bus", "hardware", "SMBus devices", B_LINUX, 0, "/sys/bus/i2c/devices", s_smb_bus, 0, "bus" },
    { "smb-sts", "hardware", "SMBus status", B_LINUX, 1, "ioports.txt", s_smb_sts, 0, "bus" },
    { "smbus-setup", "hardware", "SMBus setup", B_LINUX, 0, "/sys/bus/i2c/drivers", s_smbus_setup, 0, "bus" },
    { "smbus-system", "hardware", "System SMBus access", B_LINUX, 0, "/sys/class/i2c-dev (stat only)", s_smbus_system, 0, "bus" },
    { "sys-hal", "hardware", "System HAL resources", B_LINUX, 1, "/proc/iomem; /proc/ioports", s_sys_hal, 0, "bus" },
    { "sys-res", "hardware", "System resources", B_LINUX, 1, "/proc/interrupts; /proc/dma; /proc/ioports; /proc/iomem", s_sys_res, 0, "bus" },
    { "thunder", "hardware", "Thunderbolt", B_LINUX, 0, "/sys/bus/thunderbolt; PCI class 0C0340", s_thunder, 0, "bus" },
    { "translation", "hardware", "Bus address translation", B_LINUX, 0, "/sys/bus/pci/devices/*/resource", s_translation, 0, "bus" },
    { "uid-info", "hardware", "UID database", B_LINUX, 0, "usb.ids", s_usb_info, 0, "bus" },
    { "usb-all", "hardware", "USB all", B_LINUX, 0, "/sys/bus/usb/devices", s_usb_all, 0, "bus" },
    { "usb-bus", "hardware", "USB bus", B_LINUX, 0, "/sys/bus/usb/devices; usb.ids", s_usb_bus, 0, "bus" },
    { "usb-dev", "hardware", "USB devices", B_LINUX, 0, "/sys/bus/usb/devices", s_usb_dev, 0, "bus" },
    { "usb-flag", "windows", "USB flags", B_LINUX, 0, "/sys/module/usbcore/parameters", s_usb_flags, 0, "bus" },
    { "usb-flags", "hardware", "USB flags detail", B_LINUX, 0, "/sys/module/usbcore/parameters; */quirks", s_usb_flags, 0, "bus" },
    { "usb-info", "hardware", "USB ID database", B_LINUX, 0, "usb.ids", s_usb_info, 0, "bus" },
    { "usb-names", "hardware", "USB names", B_LINUX, 0, "/sys/bus/usb/devices/*/product; usb.ids", s_usb_names, 0, "bus" },
    { "usb-owner", "hardware", "USB owner", B_LINUX, 0, "/sys/bus/usb/devices/*/driver", s_usb_owner, 0, "bus" },
    { "usb-tree", "hardware", "USB tree", B_LINUX, 0, "/sys/bus/usb/devices", s_usb_tree, 0, "bus" },
    { "wpd-tree", "hardware", "WPD tree (portable devices)", B_LINUX, 0, "USB class 06; udev ID_MTP_DEVICE", s_wpd_tree, 0, "bus" },
    { NULL, NULL, NULL, B_LINUX, 0, NULL, NULL, 0, NULL }
};
