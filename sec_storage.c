/* sec_storage.c - SIV storage sections on Linux:
 * [volumes] [vol-list] [points] [mapping] [drive-tree] [fat] [ntfs] [drives] [hidden] [geometry]
 * [sectors] [partitions] [performance] [scsi] [usb-disks] [dsk-api] [smart] [smart-all] [smart-sdm]
 * [stornvme] [secnvme] [spaces] [raid] [jmb-raid] [jms-raid] [mega-raid] [rocket-raid] [vroc]
 * Sources (all read-only, never opens a block device): /proc/self/mountinfo, statvfs, /sys/block,
 * /sys/class/block, /sys/class/nvme, /sys/class/scsi_device, /sys/fs/{btrfs,ext4,xfs},
 * /proc/fs/ntfs3, /proc/diskstats, /proc/mdstat, the udev database (/run/udev/data/b*),
 * hwmon (nvme, drivetemp), and the srs-root cache (smart-<dev>.txt, nvme-<dev>.txt).
 * Per-object views: volumes:<mount|dev>, fat:<..>, ntfs:<..>, smart:<dev>, smart-all:<dev>,
 * partitions:<disk>, drive-tree:<disk>, drives:<disk>. */
#define _GNU_SOURCE
#include "sec.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#define MAXD 64

/* ======================================================================
 * small helpers
 * ====================================================================== */
static int arg_set(srs_out *o) { return o->arg && *o->arg; }

static const char *base_name(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }

static int starts(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }

static void commas(long long v, char *b, size_t sz)
{
    char t[40];
    int n, i, j = 0, neg = v < 0;
    snprintf(t, sizeof t, "%lld", neg ? -v : v);
    n = (int)strlen(t);
    if (sz < 2) return;
    if (neg) b[j++] = '-';
    for (i = 0; i < n && (size_t)j < sz - 2; i++) {
        if (i && (n - i) % 3 == 0) b[j++] = ',';
        b[j++] = t[i];
    }
    b[j] = 0;
}

static void bytes_str(double v, char *b, size_t sz) { if (v <= 0) snprintf(b, sz, "0"); else fmt_bytes(v, b, sz); }

/* decode udev "\x20" escapes */
static void udev_dec(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == '\\' && r[1] == 'x' && isxdigit((unsigned char)r[2]) && isxdigit((unsigned char)r[3])) {
            char h[3] = { r[2], r[3], 0 };
            *w++ = (char)strtol(h, NULL, 16);
            r += 4;
        } else *w++ = *r++;
    }
    *w = 0;
}

static void trim(char *s)
{
    char *p = s;
    size_t l;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    l = strlen(s);
    while (l && (s[l - 1] == ' ' || s[l - 1] == '\t' || s[l - 1] == '\n')) s[--l] = 0;
}

/* ---- udev database: /run/udev/data/b<maj>:<min> ---- */
static int udev_get(const char *majmin, const char *key, char *buf, size_t sz)
{
    char p[96], *t, *l;
    size_t kl = strlen(key);
    int ok = 0;
    buf[0] = 0;
    if (!majmin || !*majmin) return 0;
    snprintf(p, sizeof p, "/run/udev/data/b%s", majmin);
    if (rd_all(p, &t) < 0) return 0;
    for (l = t; l && *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        if (!strncmp(l, "E:", 2) && !strncmp(l + 2, key, kl) && l[2 + kl] == '=') {
            snprintf(buf, sz, "%s", l + 3 + kl);
            ok = 1;
            break;
        }
        l = e ? e + 1 : NULL;
    }
    free(t);
    if (ok && strstr(key, "_ENC")) udev_dec(buf);
    return ok;
}

/* udev symlinks ("S:" lines) starting with prefix, joined by sep */
static int udev_links(const char *majmin, const char *prefix, char *buf, size_t sz, const char *sep)
{
    char p[96], *t, *l;
    int n = 0;
    buf[0] = 0;
    snprintf(p, sizeof p, "/run/udev/data/b%s", majmin);
    if (rd_all(p, &t) < 0) return 0;
    for (l = t; l && *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        if (!strncmp(l, "S:", 2) && starts(l + 2, prefix) && !strchr(l + 2 + strlen(prefix), '/')) {
            size_t u = strlen(buf);
            snprintf(buf + u, sz - u, "%s%s", n ? sep : "", l + 2);
            n++;
        }
        l = e ? e + 1 : NULL;
    }
    free(t);
    return n;
}

/* ======================================================================
 * block devices
 * ====================================================================== */
typedef struct {
    char name[64], majmin[16], disk[64];
    int is_part, partno, removable, rotational, ro;
    long long sectors, start;          /* 512-byte units */
    char model[128], vendor[64], serial[192], rev[64], bus[16], state[32];
    char fstype[32], fsver[32], label[128], uuid[80], ptype[64], pname[96];
    char syspath[PATH_MAX];
} bdev;

static int is_phys(const char *n)
{
    return !(starts(n, "loop") || starts(n, "ram") || starts(n, "zram") || starts(n, "dm-") || starts(n, "md") || starts(n, "nbd"));
}

static const char *bus_of(const char *disk)
{
    char p[300], rp[PATH_MAX];
    snprintf(p, sizeof p, "/sys/block/%s", disk);
    if (!realpath(p, rp)) return "?";
    if (strstr(rp, "/usb")) return "USB";
    if (starts(disk, "nvme")) return "NVMe";
    if (strstr(rp, "/ata")) return "SATA";
    if (strstr(rp, "/virtual/")) return "Virtual";
    if (strstr(rp, "/mmc")) return "MMC";
    if (strstr(rp, "/virtio")) return "VirtIO";
    return "SCSI";
}

/* the disk a partition belongs to */
static void disk_of(const char *dev, char *disk, size_t sz)
{
    char p[300], rp[PATH_MAX];
    snprintf(disk, sz, "%s", dev);
    snprintf(p, sizeof p, "/sys/class/block/%s/partition", dev);
    if (!path_exists(p)) return;
    snprintf(p, sizeof p, "/sys/class/block/%s", dev);
    if (realpath(p, rp)) {
        char *s = strrchr(rp, '/');
        if (s) { *s = 0; snprintf(disk, sz, "%s", base_name(rp)); }
    }
}

static void bdev_load(const char *name, bdev *b)
{
    char p[400], t[256];
    long v;
    memset(b, 0, sizeof *b);
    snprintf(b->name, sizeof b->name, "%s", name);
    rd_fmt(b->majmin, sizeof b->majmin, "/sys/class/block/%s/dev", name);
    disk_of(name, b->disk, sizeof b->disk);
    b->is_part = strcmp(b->disk, name) != 0;
    snprintf(p, sizeof p, "/sys/class/block/%s/size", name);
    if (rd_long(p, &v)) b->sectors = v;
    snprintf(p, sizeof p, "/sys/class/block/%s/start", name);
    if (rd_long(p, &v)) b->start = v;
    snprintf(p, sizeof p, "/sys/class/block/%s/partition", name);
    if (rd_long(p, &v)) b->partno = (int)v;
    snprintf(p, sizeof p, "/sys/class/block/%s/ro", name);
    if (rd_long(p, &v)) b->ro = (int)v;
    snprintf(p, sizeof p, "/sys/block/%s/removable", b->disk);
    if (rd_long(p, &v)) b->removable = (int)v;
    snprintf(p, sizeof p, "/sys/block/%s/queue/rotational", b->disk);
    if (rd_long(p, &v)) b->rotational = (int)v;
    snprintf(b->bus, sizeof b->bus, "%s", bus_of(b->disk));
    rd_fmt(b->model, sizeof b->model, "/sys/block/%s/device/model", b->disk);
    rd_fmt(b->vendor, sizeof b->vendor, "/sys/block/%s/device/vendor", b->disk);
    if (!rd_fmt(b->rev, sizeof b->rev, "/sys/block/%s/device/rev", b->disk))
        rd_fmt(b->rev, sizeof b->rev, "/sys/block/%s/device/firmware_rev", b->disk);
    rd_fmt(b->state, sizeof b->state, "/sys/block/%s/device/state", b->disk);
    trim(b->model); trim(b->vendor); trim(b->rev);
    if (!*b->model && starts(b->disk, "zram")) snprintf(b->model, sizeof b->model, "zram compressed RAM disk");
    if (!*b->model && starts(b->disk, "dm-")) { rd_fmt(t, sizeof t, "/sys/block/%s/dm/name", b->disk); snprintf(b->model, sizeof b->model, "device-mapper %s", t); }
    {   /* serial: udev of the whole disk, then sysfs */
        char dmm[16];
        rd_fmt(dmm, sizeof dmm, "/sys/block/%s/dev", b->disk);
        {
            char um[128];
            if (udev_get(dmm, "ID_MODEL_ENC", um, sizeof um)) { trim(um); if (strlen(um) > strlen(b->model)) snprintf(b->model, sizeof b->model, "%s", um); }
        }
        if (!strcmp(b->vendor, "ATA")) b->vendor[0] = 0;
        if (!udev_get(dmm, "ID_SERIAL_SHORT", b->serial, sizeof b->serial))
            rd_fmt(b->serial, sizeof b->serial, "/sys/block/%s/device/serial", b->disk);
        trim(b->serial);
        if (strlen(b->serial) > 40) { b->serial[37] = b->serial[38] = b->serial[39] = '.'; b->serial[40] = 0; }
    }
    udev_get(b->majmin, "ID_FS_TYPE", b->fstype, sizeof b->fstype);
    udev_get(b->majmin, "ID_FS_VERSION", b->fsver, sizeof b->fsver);
    udev_get(b->majmin, "ID_FS_LABEL_ENC", b->label, sizeof b->label);
    udev_get(b->majmin, "ID_FS_UUID", b->uuid, sizeof b->uuid);
    udev_get(b->majmin, "ID_PART_ENTRY_TYPE", b->ptype, sizeof b->ptype);
    udev_get(b->majmin, "ID_PART_ENTRY_NAME", b->pname, sizeof b->pname);
    udev_dec(b->pname);
    snprintf(p, sizeof p, "/sys/class/block/%s", name);
    if (!realpath(p, b->syspath)) b->syspath[0] = 0;
}

/* all whole disks under /sys/block (phys_only: skip loop/zram/dm/md) */
static int disks_list(char ***names, int phys_only)
{
    char **n;
    int c = dir_list("/sys/block", &n), i, k = 0;
    if (c < 0) { *names = NULL; return 0; }
    for (i = 0; i < c; i++) {
        if (starts(n[i], "loop") || starts(n[i], "ram") || (phys_only && !is_phys(n[i]))) { free(n[i]); continue; }
        n[k++] = n[i];
    }
    *names = n;
    return k;
}

/* partitions of a disk, sorted by partition number */
static int parts_of(const char *disk, char out[][64], int max)
{
    char **n, p[400];
    int c, i, k = 0;
    snprintf(p, sizeof p, "/sys/block/%s", disk);
    c = dir_list(p, &n);
    for (i = 0; i < c && k < max; i++) {
        snprintf(p, sizeof p, "/sys/block/%s/%s/partition", disk, n[i]);
        if (path_exists(p)) snprintf(out[k++], 64, "%s", n[i]);
    }
    dir_free(n, c);
    for (i = 1; i < k; i++) {   /* insertion sort by number */
        char t[64];
        long a, b;
        int j = i;
        while (j > 0) {
            snprintf(p, sizeof p, "/sys/block/%s/%s/partition", disk, out[j - 1]); a = 0; rd_long(p, &a);
            snprintf(p, sizeof p, "/sys/block/%s/%s/partition", disk, out[j]); b = 0; rd_long(p, &b);
            if (a <= b) break;
            memcpy(t, out[j], 64); memcpy(out[j], out[j - 1], 64); memcpy(out[j - 1], t, 64);
            j--;
        }
    }
    return k;
}

static const char *drive_type(const bdev *b)
{
    if (starts(b->disk, "sr")) return "CD Drive";
    if (starts(b->disk, "zram") || starts(b->disk, "ram")) return "RAM Disk";
    if (starts(b->disk, "loop") || starts(b->disk, "dm-") || starts(b->disk, "md")) return "Virtual";
    if (b->removable) return "Removable";
    return "Fixed";
}

static const char *media_of(const bdev *b)
{
    if (starts(b->disk, "sr")) return "Optical";
    if (b->sectors == 0 && b->removable) return "No media";
    if (starts(b->disk, "zram")) return "RAM";
    return b->rotational ? (strcmp(b->bus, "USB") ? "HDD" : "HDD/Flash") : "SSD";
}

static void disk_desc(const bdev *b, char *o, size_t sz)
{
    char m[256];
    snprintf(m, sizeof m, "%s%s%s", b->vendor, *b->vendor ? " " : "", b->model);
    trim(m);
    if (*b->serial) snprintf(o, sz, "%s (%s)", *m ? m : b->disk, b->serial);
    else snprintf(o, sz, "%s", *m ? m : b->disk);
}

/* ======================================================================
 * mounts
 * ====================================================================== */
typedef struct {
    char mp[512], root[256], fstype[48], src[256], opts[256], super[512], dev[64];
    int kind;   /* 0 pseudo, 1 local block fs, 2 network */
} mnt;

static void unoct(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == '\\' && r[1] >= '0' && r[1] <= '3' && r[2] >= '0' && r[2] <= '7' && r[3] >= '0' && r[3] <= '7') {
            *w++ = (char)((r[1] - '0') * 64 + (r[2] - '0') * 8 + (r[3] - '0'));
            r += 4;
        } else *w++ = *r++;
    }
    *w = 0;
}

static int is_netfs(const char *t)
{
    static const char *const n[] = { "nfs", "nfs4", "cifs", "smb3", "smbfs", "sshfs", "fuse.sshfs", "9p", "ceph", "glusterfs",
                                     "fuse.rclone", "davfs", "fuse.glusterfs", "afs", NULL };
    int i;
    for (i = 0; n[i]; i++) if (!strcmp(t, n[i])) return 1;
    return 0;
}

static int mounts_load(mnt **out)
{
    char *t, *l;
    int n = 0, cap = 64;
    mnt *m = calloc((size_t)cap, sizeof *m);
    *out = m;
    if (!m || rd_all("/proc/self/mountinfo", &t) < 0) return 0;
    for (l = t; l && *l;) {
        char *e = strchr(l, '\n'), *f[64], *s, *sv;
        int nf = 0, dash = -1, i;
        mnt x;
        if (e) *e = 0;
        for (s = strtok_r(l, " ", &sv); s && nf < 64; s = strtok_r(NULL, " ", &sv)) f[nf++] = s;
        for (i = 6; i < nf; i++) if (!strcmp(f[i], "-")) { dash = i; break; }
        l = e ? e + 1 : NULL;
        if (dash < 0 || dash + 2 >= nf) continue;
        memset(&x, 0, sizeof x);
        snprintf(x.root, sizeof x.root, "%s", f[3]); unoct(x.root);
        snprintf(x.mp, sizeof x.mp, "%s", f[4]); unoct(x.mp);
        snprintf(x.opts, sizeof x.opts, "%s", f[5]);
        snprintf(x.fstype, sizeof x.fstype, "%s", f[dash + 1]);
        snprintf(x.src, sizeof x.src, "%s", f[dash + 2]); unoct(x.src);
        if (dash + 3 < nf) snprintf(x.super, sizeof x.super, "%s", f[dash + 3]);
        if (is_netfs(x.fstype)) x.kind = 2;
        else if (starts(x.src, "/dev/") && strcmp(x.fstype, "devtmpfs") && strncmp(x.fstype, "fuse", 4)) {
            char rp[PATH_MAX], p[300];
            if (realpath(x.src, rp)) {
                snprintf(x.dev, sizeof x.dev, "%s", base_name(rp));
                snprintf(p, sizeof p, "/sys/class/block/%s", x.dev);
                if (path_exists(p)) x.kind = 1;
            }
        }
        if (n == cap) {
            mnt *nm = realloc(m, sizeof *m * (size_t)(cap *= 2));
            if (!nm) break;
            m = nm;
        }
        m[n++] = x;
    }
    free(t);
    *out = m;
    return n;
}

typedef struct { double total, avail, freeb, bsize, frsize, files, ffree, namemax; int ok, ro; } vstat;

static void vol_stat(const mnt *m, vstat *v)
{
    struct statvfs s;
    memset(v, 0, sizeof *v);
    if (m->kind != 1) return;   /* never statvfs network mounts (a dead server would hang the caller) */
    if (statvfs(m->mp, &s) != 0) return;
    v->ok = 1;
    v->frsize = s.f_frsize ? (double)s.f_frsize : (double)s.f_bsize;
    v->bsize = (double)s.f_bsize;
    v->total = (double)s.f_blocks * v->frsize;
    v->avail = (double)s.f_bavail * v->frsize;
    v->freeb = (double)s.f_bfree * v->frsize;
    v->files = (double)s.f_files;
    v->ffree = (double)s.f_ffree;
    v->namemax = (double)s.f_namemax;
    v->ro = (s.f_flag & ST_RDONLY) != 0;
}

/* SIV-style volume serial: FAT "XXXX-XXXX" as is, NTFS low 32 bits, else the UUID */
static void vol_serial(const bdev *b, char *o, size_t sz)
{
    size_t l = strlen(b->uuid);
    if (!l) { snprintf(o, sz, "-"); return; }
    if ((!strcmp(b->fstype, "ntfs") || !strcmp(b->fstype, "ntfs3")) && l == 16)
        snprintf(o, sz, "%.4s-%.4s", b->uuid + 8, b->uuid + 12);
    else snprintf(o, sz, "%s", b->uuid);
}

static const char *mnt_label(const mnt *m, const bdev *b) { return m->kind == 2 ? m->src : (*b->label ? b->label : "<none>"); }

static int mnt_match(const mnt *m, const char *a)
{
    if (!a || !*a) return 1;
    if (!strcmp(m->mp, a) || !strcmp(m->dev, a) || !strcmp(m->src, a)) return 1;
    if (starts(a, "/dev/") && !strcmp(m->dev, a + 5)) return 1;
    return 0;
}

/* ======================================================================
 * [volumes]
 * ====================================================================== */
static void fs_detail(srs_out *o, const mnt *m, const bdev *b);

static void vol_detail(srs_out *o, const mnt *m)
{
    static const int tabs[] = { 200 };
    bdev b, d;
    vstat v;
    char s1[64], s2[64], s3[64], t[600];
    memset(&b, 0, sizeof b);
    memset(&d, 0, sizeof d);
    if (m->kind == 1) { bdev_load(m->dev, &b); bdev_load(b.disk, &d); }
    vol_stat(m, &v);
    snprintf(t, sizeof t, "Volume %s", m->mp);
    so_siv_header(o, t);
    so_tabs(o, tabs, 1);
    so_kv(o, "Mount point", C_TEAL, m->mp);
    so_kv(o, "Source", C_VALUE, m->src);
    if (m->kind == 1) {
        disk_desc(&d, t, sizeof t);
        so_kvf(o, "Device", C_VALUE, "/dev/%s  (%s)", m->dev, b.majmin);
        so_kvf(o, "Physical disk", C_BLACK, "/dev/%s  %s", d.name, t);
        so_kvf(o, "Drive type", C_ORANGE, "%s  %s  %s", drive_type(&d), d.bus, media_of(&d));
    } else so_kv(o, "Drive type", C_ORANGE, m->kind == 2 ? "Network" : "Pseudo / virtual");
    so_kvf(o, "Format", C_VALUE, "%s%s%s", m->fstype, *b.fsver ? "  version " : "", b.fsver);
    so_kv(o, "Volume label", C_ORANGE, mnt_label(m, &b));
    vol_serial(&b, t, sizeof t);
    so_kv(o, "Volume serial / UUID", C_GREEN, t);
    if (*b.pname || *b.ptype) so_kvf(o, "Partition", C_VALUE, "#%d  %s  type %s", b.partno, *b.pname ? b.pname : "<no name>", b.ptype);
    if (strcmp(m->root, "/")) so_kv(o, "Subvolume / bind root", C_PURPLE, m->root);
    so_kv(o, "Mount options", C_GRAY, m->opts);
    so_kv(o, "Super options", C_GRAY, m->super);
    if (v.ok) {
        bytes_str(v.total, s1, sizeof s1); bytes_str(v.avail, s2, sizeof s2); bytes_str(v.freeb, s3, sizeof s3);
        so_kv(o, "Total", C_VALUE, s1);
        so_kvf(o, "Free", C_GREEN, "%s available  (%s incl. reserved)", s2, s3);
        bytes_str(v.total - v.freeb, s1, sizeof s1);
        so_kvf(o, "Used", C_VALUE, "%s  %.0f%%", s1, v.total > 0 ? 100.0 * (v.total - v.freeb) / v.total : 0.0);
        so_kvf(o, "Block / fragment size", C_VALUE, "%.0f / %.0f bytes", v.bsize, v.frsize);
        commas((long long)v.files, s1, sizeof s1); commas((long long)v.ffree, s2, sizeof s2);
        so_kvf(o, "Inodes", C_VALUE, "%s total, %s free", v.files > 0 ? s1 : "n/a", v.files > 0 ? s2 : "n/a");
        so_kvf(o, "Max name length", C_VALUE, "%.0f", v.namemax);
        so_kv(o, "Access", v.ro ? C_ORANGE : C_GREEN, v.ro ? "read-only" : "read/write");
    } else if (m->kind == 2) so_note(o, "Network mount: sizes not queried (statvfs on a stalled server would block).");
    if (m->kind == 1) { so_par(o); fs_detail(o, m, &b); }
}

static void s_volumes(srs_out *o)
{
    static const int tabs[] = { 340, 520, 620, 680, 750, 820, 870, 920, 1000 };
    static const char *const ttype[] = { "CD Drive", "Removable", "Fixed", "RAM Disk", "Virtual", "Network" };
    mnt *m;
    int n = mounts_load(&m), i, j, k, nt = (int)(sizeof ttype / sizeof *ttype);
    int cnt[6] = { 0 }, ucnt[6] = { 0 };
    double tf[6] = { 0 }, tt[6] = { 0 }, tu[6] = { 0 };
    char seen[MAXD * 4][64];
    int nseen = 0;
    if (!arg_set(o)) so_siv_header(o, "Volumes");
    if (arg_set(o)) {
        for (i = 0; i < n; i++) if (m[i].kind && mnt_match(&m[i], o->arg)) { vol_detail(o, &m[i]); free(m); return; }
        so_err(o, "No mounted volume matches that mount point or device.");
        free(m);
        return;
    }
    so_tabs(o, tabs, 9);
    {
        static const char *const h[] = { "Drive and Path", "Volume Label", "Format", "Quota", "Free", "Total", "Used", "Cluster", "Drive Type", "Volume Serial" };
        static const int hc[] = { C_TEAL, C_ORANGE, C_ORANGE, C_BLACK, C_GREEN, C_VALUE, C_GREEN, C_PURPLE, C_ORANGE, C_PURPLE };
        so_row(o, 10, h, hc);
        so_par(o);
    }
    for (i = 0; i < n; i++) {
        bdev b, d;
        vstat v;
        char fr[32], to[32], us[16], cl[16], ser[96], fmt[64], dt[32];
        const char *cells[10];
        static const int cc[] = { C_TEAL, C_ORANGE, C_ORANGE, C_BLACK, C_GREEN, C_VALUE, C_GREEN, C_PURPLE, C_ORANGE, C_GREEN };
        int ti, dup = 0;
        if (!m[i].kind) continue;
        for (j = 0; j < i; j++) if (m[j].kind && !strcmp(m[j].mp, m[i].mp)) dup = 1;
        if (dup) continue;
        memset(&b, 0, sizeof b);
        memset(&d, 0, sizeof d);
        if (m[i].kind == 1) { bdev_load(m[i].dev, &b); bdev_load(b.disk, &d); snprintf(dt, sizeof dt, "%s", drive_type(&d)); }
        else snprintf(dt, sizeof dt, "Network");
        vol_stat(&m[i], &v);
        snprintf(fmt, sizeof fmt, "%s%s", m[i].fstype, (v.ok && v.ro) || strstr(m[i].opts, "ro,") == m[i].opts || !strcmp(m[i].opts, "ro") ? " (R)" : "");
        if (v.ok) {
            bytes_str(v.avail, fr, sizeof fr); bytes_str(v.total, to, sizeof to);
            snprintf(us, sizeof us, "%.0f%%", v.total > 0 ? 100.0 * (v.total - v.freeb) / v.total : 0.0);
            bytes_str(v.bsize, cl, sizeof cl);
        } else { snprintf(fr, sizeof fr, "-"); snprintf(to, sizeof to, "-"); us[0] = 0; snprintf(cl, sizeof cl, "-"); }
        vol_serial(&b, ser, sizeof ser);
        if (m[i].kind == 2) snprintf(ser, sizeof ser, "-");
        cells[0] = m[i].mp; cells[1] = mnt_label(&m[i], &b); cells[2] = fmt; cells[3] = "";
        cells[4] = fr; cells[5] = to; cells[6] = us; cells[7] = cl; cells[8] = dt; cells[9] = ser;
        so_row(o, 10, cells, cc);
        for (ti = 0; ti < nt; ti++) if (!strcmp(ttype[ti], dt)) break;
        if (ti < nt) {
            const char *key = m[i].kind == 1 ? (*b.uuid ? b.uuid : m[i].dev) : m[i].src;
            int s = 0;
            cnt[ti]++;
            for (k = 0; k < nseen; k++) if (!strcmp(seen[k], key)) s = 1;
            if (!s && nseen < MAXD * 4) {   /* count each filesystem once (btrfs subvolumes, bind mounts) */
                snprintf(seen[nseen++], 64, "%s", key);
                ucnt[ti]++;
                if (v.ok) { tf[ti] += v.avail; tt[ti] += v.total; tu[ti] += v.total - v.freeb; }
            }
        }
    }
    so_par(o);
    for (k = 0; k < nt; k++) {
        char c[32], f[32], t[32], u[16];
        const char *cells[10];
        static const int cc[] = { C_TEAL, C_ORANGE, C_TEAL, C_BLACK, C_GREEN, C_VALUE, C_GREEN, C_PURPLE, C_GRAY, C_GRAY };
        if (!cnt[k]) continue;
        snprintf(c, sizeof c, "%d (%d)", cnt[k], ucnt[k]);
        bytes_str(tf[k], f, sizeof f); bytes_str(tt[k], t, sizeof t);
        if (tt[k] > 0) snprintf(u, sizeof u, "%.0f%%", 100.0 * tu[k] / tt[k]); else u[0] = 0;
        cells[0] = "Total"; cells[1] = ttype[k]; cells[2] = c; cells[3] = ""; cells[4] = f; cells[5] = t; cells[6] = u;
        cells[7] = ""; cells[8] = ttype[k]; cells[9] = "";
        so_row(o, 10, cells, cc);
    }
    so_par(o);
    so_note(o, "Count = mount points (distinct filesystems). Free = available to users. (R) = mounted read-only.");
    so_note(o, "Per-volume detail: volumes:<mount point or device>, e.g. volumes:/ ; filesystem stats: [ntfs] [fat].");
    free(m);
}

/* ======================================================================
 * filesystem-specific statistics ([fat] [ntfs] and volume detail)
 * ====================================================================== */
static void kv_file(srs_out *o, const char *label, int c, const char *path)
{
    char b[512];
    if (rd_str(path, b, sizeof b)) so_kv(o, label, c, b);
}

static void btrfs_detail(srs_out *o, const char *fsid)
{
    static const char *const at[] = { "data", "metadata", "system" };
    static const char *const prof[] = { "single", "dup", "raid0", "raid1", "raid1c3", "raid1c4", "raid10", "raid5", "raid6", NULL };
    char d[300], p[400], b[1024], s1[32], s2[32], s3[32], s4[32];
    char **n;
    int c, i, k;
    long v;
    snprintf(d, sizeof d, "/sys/fs/btrfs/%s", fsid);
    if (!path_exists(d)) { so_note(o, "btrfs: /sys/fs/btrfs/<fsid> not found"); return; }
    so_kv(o, "btrfs fsid", C_PURPLE, fsid);
    snprintf(p, sizeof p, "%s/label", d); kv_file(o, "Label", C_ORANGE, p);
    snprintf(p, sizeof p, "%s/nodesize", d); kv_file(o, "Node size", C_VALUE, p);
    snprintf(p, sizeof p, "%s/sectorsize", d); kv_file(o, "Sector size", C_VALUE, p);
    snprintf(p, sizeof p, "%s/checksum", d); kv_file(o, "Checksum", C_VALUE, p);
    snprintf(p, sizeof p, "%s/generation", d); kv_file(o, "Generation", C_VALUE, p);
    snprintf(p, sizeof p, "%s/features", d);
    c = dir_list(p, &n); b[0] = 0;
    for (i = 0; i < c; i++) { size_t u = strlen(b); snprintf(b + u, sizeof b - u, "%s%s", i ? " " : "", n[i]); }
    dir_free(n, c);
    so_kv(o, "Features", C_TEAL, b);
    snprintf(p, sizeof p, "%s/devices", d);
    c = dir_list(p, &n); b[0] = 0;
    for (i = 0; i < c; i++) { size_t u = strlen(b); snprintf(b + u, sizeof b - u, "%s/dev/%s", i ? " " : "", n[i]); }
    dir_free(n, c);
    so_kvf(o, "Devices", C_BLACK, "%d  %s", c < 0 ? 0 : c, b);
    for (i = 0; i < 3; i++) {
        long tb = 0, bu = 0, dt = 0, du = 0;
        const char *pr = "?";
        snprintf(p, sizeof p, "%s/allocation/%s/total_bytes", d, at[i]); rd_long(p, &tb);
        snprintf(p, sizeof p, "%s/allocation/%s/bytes_used", d, at[i]); rd_long(p, &bu);
        snprintf(p, sizeof p, "%s/allocation/%s/disk_total", d, at[i]); rd_long(p, &dt);
        snprintf(p, sizeof p, "%s/allocation/%s/disk_used", d, at[i]); rd_long(p, &du);
        for (k = 0; prof[k]; k++) { snprintf(p, sizeof p, "%s/allocation/%s/%s", d, at[i], prof[k]); if (path_exists(p)) { pr = prof[k]; break; } }
        bytes_str((double)tb, s1, sizeof s1); bytes_str((double)bu, s2, sizeof s2);
        bytes_str((double)dt, s3, sizeof s3); bytes_str((double)du, s4, sizeof s4);
        snprintf(b, sizeof b, "%-7s  allocated %s, used %s (%.0f%%)  raw %s / %s", pr, s1, s2, tb > 0 ? 100.0 * (double)bu / (double)tb : 0.0, s4, s3);
        snprintf(p, sizeof p, "Allocation %s", at[i]);
        so_kv(o, p, C_VALUE, b);
    }
    snprintf(p, sizeof p, "%s/allocation/global_rsv_size", d);
    if (rd_long(p, &v)) { bytes_str((double)v, s1, sizeof s1); so_kv(o, "Global reserve", C_VALUE, s1); }
    snprintf(p, sizeof p, "%s/devinfo", d);
    c = dir_list(p, &n);
    for (i = 0; i < c; i++) {
        char *t;
        snprintf(p, sizeof p, "%s/devinfo/%s/error_stats", d, n[i]);
        if (rd_all(p, &t) >= 0) {
            char *q;
            for (q = t; *q; q++) if (*q == '\n') *q = ' ';
            trim(t);
            snprintf(b, sizeof b, "Device %s errors", n[i]);
            so_kv(o, b, strstr(t, "errs 0") && !strpbrk(t, "123456789") ? C_GREEN : C_ORANGE, t);
            free(t);
        }
    }
    dir_free(n, c);
    snprintf(p, sizeof p, "%s/commit_stats", d);
    {
        char *t;
        if (rd_all(p, &t) >= 0) { char *q; for (q = t; *q; q++) if (*q == '\n') *q = ' '; trim(t); so_kv(o, "Commit stats", C_GRAY, t); free(t); }
    }
}

static void ext4_detail(srs_out *o, const char *dev)
{
    static const char *const f[] = { "lifetime_write_kbytes", "session_write_kbytes", "errors_count", "first_error_time", "last_error_time",
                                      "delayed_allocation_blocks", "reserved_clusters", "inode_readahead_blks", "mb_stream_req", "journal_task", NULL };
    char p[300];
    int i;
    snprintf(p, sizeof p, "/sys/fs/ext4/%s", dev);
    if (!path_exists(p)) { so_note(o, "ext4: /sys/fs/ext4/<dev> not found"); return; }
    for (i = 0; f[i]; i++) { snprintf(p, sizeof p, "/sys/fs/ext4/%s/%s", dev, f[i]); kv_file(o, f[i], C_VALUE, p); }
}

static void xfs_detail(srs_out *o, const char *dev)
{
    char p[300], *t;
    snprintf(p, sizeof p, "/sys/fs/xfs/%s", dev);
    if (!path_exists(p)) { so_note(o, "xfs: /sys/fs/xfs/<dev> not found"); return; }
    snprintf(p, sizeof p, "/sys/fs/xfs/%s/log/log_head_lsn", dev); kv_file(o, "Log head LSN", C_VALUE, p);
    snprintf(p, sizeof p, "/sys/fs/xfs/%s/log/log_tail_lsn", dev); kv_file(o, "Log tail LSN", C_VALUE, p);
    snprintf(p, sizeof p, "/sys/fs/xfs/%s/stats/stats", dev);
    if (rd_all(p, &t) >= 0) { so_lines(o, C_BLACK, t); free(t); }
}

static void ntfs3_detail(srs_out *o, const char *dev)
{
    static const char *const lab[] = { "NTFS version", "Cluster size", "Clusters", "MFT records", "MFT records in use", "Dirty (runtime)", "Dirty flag (on disk)" };
    char p[300], *t, *l, *sv;
    int i = 0;
    snprintf(p, sizeof p, "/proc/fs/ntfs3/%s/volinfo", dev);
    if (rd_all(p, &t) < 0) { so_note(o, "ntfs3: /proc/fs/ntfs3/<dev>/volinfo not available"); return; }
    for (l = strtok_r(t, "\n", &sv); l && i < 7; l = strtok_r(NULL, "\n", &sv), i++) {
        char c[64];
        if (i >= 2 && i <= 4) { commas(atoll(l), c, sizeof c); so_kv(o, lab[i], C_VALUE, c); }
        else so_kv(o, lab[i], i >= 5 ? (strcmp(l, "clean") ? C_ORANGE : C_GREEN) : C_VALUE, l);
    }
    free(t);
    snprintf(p, sizeof p, "/proc/fs/ntfs3/%s/label", dev);
    kv_file(o, "Label (ntfs3)", C_ORANGE, p);
}

static void fat_detail(srs_out *o, const mnt *m, const bdev *b)
{
    vstat v;
    char s[64];
    vol_stat(m, &v);
    so_kv(o, "FAT type", C_VALUE, *b->fsver ? b->fsver : m->fstype);
    if (v.ok && v.frsize > 0) {
        commas((long long)(v.total / v.frsize), s, sizeof s); so_kv(o, "Clusters", C_VALUE, s);
        commas((long long)(v.freeb / v.frsize), s, sizeof s); so_kv(o, "Free clusters", C_GREEN, s);
        bytes_str(v.frsize, s, sizeof s); so_kv(o, "Cluster size", C_VALUE, s);
    }
    so_kv(o, "Serial", C_PURPLE, *b->uuid ? b->uuid : "-");
}

static void fs_detail(srs_out *o, const mnt *m, const bdev *b)
{
    const char *t = m->fstype;
    if (!strcmp(t, "btrfs")) btrfs_detail(o, b->uuid);
    else if (!strcmp(t, "ext4") || !strcmp(t, "ext3") || !strcmp(t, "ext2")) ext4_detail(o, m->dev);
    else if (!strcmp(t, "xfs")) xfs_detail(o, m->dev);
    else if (!strcmp(t, "ntfs3")) ntfs3_detail(o, m->dev);
    else if (!strcmp(t, "vfat") || !strcmp(t, "exfat") || !strcmp(t, "msdos")) fat_detail(o, m, b);
    else so_note(o, "No filesystem-specific statistics exported by this filesystem driver.");
}

/* list mounted filesystems of the given types (one per filesystem), with stats */
/* file-system type mounted at mp ("" if none) */
static void mount_fstype(const char *mp, char *out, size_t sz)
{
    mnt *m;
    int n = mounts_load(&m), i;
    *out = 0;
    for (i = 0; i < n; i++) if (!strcmp(m[i].mp, mp)) snprintf(out, sz, "%s", m[i].fstype);
    free(m);
}

static int fs_stats(srs_out *o, const char *const *types, const char *what)
{
    static const int tabs[] = { 200 };
    mnt *m;
    int n = mounts_load(&m), i, j, k, shown = 0;
    so_tabs(o, tabs, 1);
    for (i = 0; i < n; i++) {
        bdev b;
        int ok = 0, dup = 0;
        char t[700], sz[32];
        vstat v;
        if (m[i].kind != 1) continue;
        if (!types) ok = 1;
        else for (k = 0; types[k]; k++) if (!strcmp(m[i].fstype, types[k])) ok = 1;
        if (!ok || (arg_set(o) && !mnt_match(&m[i], o->arg))) continue;
        for (j = 0; j < i; j++) if (m[j].kind == 1 && !strcmp(m[j].dev, m[i].dev)) dup = 1;
        if (dup && !arg_set(o)) continue;
        bdev_load(m[i].dev, &b);
        vol_stat(&m[i], &v);
        bytes_str(v.total, sz, sizeof sz);
        snprintf(t, sizeof t, "Statistics for %s (%s)  %s  /dev/%s  %s", m[i].mp, *b.label ? b.label : "<none>", m[i].fstype, m[i].dev, sz);
        so_b(o, C_VALUE, t); so_par(o);
        fs_detail(o, &m[i], &b);
        so_par(o);
        shown++;
        if (arg_set(o)) break;
    }
    if (!shown) { so_kvf(o, "Status", C_ORANGE, "No mounted %s volumes%s", what, arg_set(o) ? " match that mount point/device" : ""); }
    free(m);
    return shown;
}

static void s_fat(srs_out *o)
{
    static const char *const t[] = { "vfat", "exfat", "msdos", NULL };
    so_siv_header(o, "FAT Statistics");
    fs_stats(o, arg_set(o) ? NULL : t, "FAT/exFAT");
    so_note(o, "Source: statvfs + udev ID_FS_* (the kernel vfat/exfat drivers export no statistics in sysfs).");
}

static void s_ntfs(srs_out *o)
{
    static const char *const t[] = { "ntfs3", "ntfs", NULL };
    static const char *const nat[] = { "btrfs", "ext4", "ext3", "ext2", "xfs", "f2fs", NULL };
    if (arg_set(o)) {   /* ntfs:/mountpoint - title after the file system that is really there */
        char ti[96], ty[32] = "";
        mount_fstype(o->arg, ty, sizeof ty);
        snprintf(ti, sizeof ti, "%s Statistics", *ty ? ty : "File System");
        so_siv_header(o, ti);
    } else so_siv_header(o, "NTFS Statistics");
    fs_stats(o, arg_set(o) ? NULL : t, "NTFS");
    if (!arg_set(o)) {
        so_par(o);
        so_b(o, C_VALUE, "Native Linux filesystems"); so_par(o); so_par(o);
        fs_stats(o, nat, "btrfs/ext4/xfs");
    }
    so_note(o, "Sources: /proc/fs/ntfs3/<dev>/volinfo, /sys/fs/btrfs/<fsid>/, /sys/fs/ext4/<dev>/, /sys/fs/xfs/<dev>/ (read-only).");
}

/* ======================================================================
 * [vol-list] [points] [mapping] [drive-tree]
 * ====================================================================== */
static void mounts_of(const mnt *m, int n, const char *dev, char *o, size_t sz)
{
    int i;
    o[0] = 0;
    for (i = 0; i < n; i++)
        if (m[i].kind == 1 && !strcmp(m[i].dev, dev)) {
            size_t u = strlen(o);
            snprintf(o + u, sz - u, "%s%s", u ? " " : "", m[i].mp);
        }
}

static void swap_check(const char *dev, char *o, size_t sz)
{
    char *t, needle[80];
    if (*o || rd_all("/proc/swaps", &t) < 0) return;
    snprintf(needle, sizeof needle, "/dev/%s ", dev);
    if (strstr(t, needle)) snprintf(o, sz, "[SWAP]");
    free(t);
}

static void s_vol_list(srs_out *o)
{
    static const int tabs[] = { 120, 200, 260, 450, 760, 840 };
    static const char *const h[] = { "Name", "FSType", "Version", "Label", "UUID", "Size", "Mount points" };
    static const int cc[] = { C_TEAL, C_ORANGE, C_VALUE, C_ORANGE, C_PURPLE, C_VALUE, C_GREEN };
    char **dn;
    int nd = disks_list(&dn, 0), i, j, nm;
    mnt *m;
    nm = mounts_load(&m);
    so_siv_header(o, "Volume List");
    so_tabs(o, tabs, 6);
    so_row(o, 7, h, cc);
    so_par(o);
    for (i = 0; i < nd; i++) {
        char pn[64][64], nm2[80], sz[32], mp[1024];
        int np = parts_of(dn[i], pn, 64);
        for (j = -1; j < np; j++) {
            bdev b;
            const char *cells[7];
            bdev_load(j < 0 ? dn[i] : pn[j], &b);
            snprintf(nm2, sizeof nm2, "%s%s", j < 0 ? "" : "  ", b.name);
            bytes_str((double)b.sectors * 512.0, sz, sizeof sz);
            mounts_of(m, nm, b.name, mp, sizeof mp);
            swap_check(b.name, mp, sizeof mp);
            cells[0] = nm2; cells[1] = b.fstype; cells[2] = b.fsver; cells[3] = b.label; cells[4] = b.uuid; cells[5] = sz; cells[6] = mp;
            so_row(o, 7, cells, cc);
        }
    }
    dir_free(dn, nd);
    free(m);
    so_par(o);
    so_note(o, "Source: /sys/class/block + udev database (blkid-free), /proc/self/mountinfo, /proc/swaps.");
}

static void s_points(srs_out *o)
{
    static const int tabs[] = { 390, 640, 730 };
    static const char *const h[] = { "Mount Point", "Source", "Type", "Options" };
    static const int hc[] = { C_TEAL, C_VALUE, C_ORANGE, C_GRAY };
    mnt *m;
    int n = mounts_load(&m), i, pass, cnt[3] = { 0 };
    so_siv_header(o, "Mount Points");
    so_tabs(o, tabs, 3);
    so_row(o, 4, h, hc);
    for (pass = 1; pass <= 3; pass++) {
        int kind = pass == 3 ? 0 : pass;
        so_par(o);
        so_b(o, C_PURPLE, kind == 1 ? "Block device filesystems" : kind == 2 ? "Network filesystems" : "Virtual / pseudo filesystems");
        so_par(o);
        for (i = 0; i < n; i++) {
            const char *cells[4];
            static const int cc[] = { C_TEAL, C_VALUE, C_ORANGE, C_GRAY };
            static const int cg[] = { C_GRAY, C_GRAY, C_GRAY, C_GRAY };
            char op[96];
            if (m[i].kind != kind) continue;
            snprintf(op, sizeof op, "%s", m[i].opts);
            if (strlen(m[i].opts) > 90) snprintf(op + 87, sizeof op - 87, "...");
            cells[0] = m[i].mp; cells[1] = m[i].src; cells[2] = m[i].fstype; cells[3] = op;
            so_row(o, 4, cells, kind ? cc : cg);
            cnt[kind]++;
        }
    }
    so_par(o);
    so_kvf(o, "Totals", C_BLACK, "%d block, %d network, %d virtual mount points", cnt[1], cnt[2], cnt[0]);
    free(m);
}

static void s_mapping(srs_out *o)
{
    static const int tabs[] = { 40, 160, 420 };
    char **dn;
    int nd = disks_list(&dn, 0), i, j, nm;
    mnt *m;
    nm = mounts_load(&m);
    so_siv_header(o, "Disk Mapping");
    so_tabs(o, tabs, 3);
    for (i = 0; i < nd; i++) {
        bdev d;
        char t[4096], pn[64][64];
        int np = parts_of(dn[i], pn, 64);
        bdev_load(dn[i], &d);
        disk_desc(&d, t, sizeof t);
        so_b(o, C_VALUE, "Disk Drive /dev/"); so_b(o, C_VALUE, d.name); so_t(o, C_BLACK, "  "); so_t(o, C_BLACK, t);
        so_par(o);
        if (udev_links(d.majmin, "disk/by-id/", t, sizeof t, "\n")) {
            char *l, *sv;
            for (l = strtok_r(t, "\n", &sv); l; l = strtok_r(NULL, "\n", &sv)) { so_tab(o); so_t(o, C_LABEL, "Symbolic Link"); so_tab(o); so_t(o, C_PURPLE, l); so_par(o); }
        }
        if (udev_links(d.majmin, "disk/by-path/", t, sizeof t, "  ")) { so_tab(o); so_t(o, C_LABEL, "Path"); so_tab(o); so_t(o, C_PURPLE, t); so_par(o); }
        if (!np) {
            mounts_of(m, nm, d.name, t, sizeof t);
            swap_check(d.name, t, sizeof t);
            so_tab(o); so_t(o, C_LABEL, "Point"); so_tab(o); so_t(o, C_GREEN, *t ? t : "  No Volumes"); so_par(o);
        }
        for (j = 0; j < np; j++) {
            bdev b;
            char mp[1024], pu[128];
            bdev_load(pn[j], &b);
            mounts_of(m, nm, b.name, mp, sizeof mp);
            swap_check(b.name, mp, sizeof mp);
            udev_get(b.majmin, "ID_PART_ENTRY_UUID", pu, sizeof pu);
            so_tab(o); so_t(o, C_TEAL, b.name); so_tab(o);
            so_f(o, C_ORANGE, "%s %s", *b.fstype ? b.fstype : "-", *b.label ? b.label : "<none>");
            so_tab(o); so_t(o, C_GREEN, *mp ? mp : "Not mounted"); so_par(o);
            if (*pu) { so_tab(o); so_tab(o); so_t(o, C_GRAY, "PARTUUID"); so_tab(o); so_t(o, C_GRAY, pu); so_par(o); }
            if (*b.uuid) { so_tab(o); so_tab(o); so_t(o, C_GRAY, "UUID"); so_tab(o); so_t(o, C_GRAY, b.uuid); so_par(o); }
        }
        so_par(o);
    }
    dir_free(dn, nd);
    free(m);
}

static void tree_holders(srs_out *o, const char *dev, int depth, const mnt *m, int nm)
{
    char p[300], **n;
    int c, i;
    if (depth > 6) return;
    snprintf(p, sizeof p, "/sys/class/block/%s/holders", dev);
    c = dir_list(p, &n);
    for (i = 0; i < c; i++) {
        bdev h;
        char sz[32], mp[1024], ind[64];
        bdev_load(n[i], &h);
        bytes_str((double)h.sectors * 512.0, sz, sizeof sz);
        mounts_of(m, nm, h.name, mp, sizeof mp);
        snprintf(ind, sizeof ind, "%*s", depth * 4, "");
        so_t(o, C_GRAY, ind); so_t(o, C_GRAY, "`-> "); so_t(o, C_TEAL, h.name); so_t(o, C_VALUE, "  ");
        so_f(o, C_VALUE, "%s  %s  %s", sz, h.model, mp);
        so_par(o);
        tree_holders(o, n[i], depth + 1, m, nm);
    }
    dir_free(n, c);
}

static void s_drive_tree(srs_out *o)
{
    char **dn;
    int nd = disks_list(&dn, 0), i, j, nm;
    mnt *m;
    nm = mounts_load(&m);
    so_siv_header(o, "Drive Tree");
    for (i = 0; i < nd; i++) {
        bdev d;
        char t[512], sz[32], pn[64][64], pt[32];
        int np;
        if (arg_set(o) && strcmp(o->arg, dn[i])) continue;
        np = parts_of(dn[i], pn, 64);
        bdev_load(dn[i], &d);
        bytes_str((double)d.sectors * 512.0, sz, sizeof sz);
        disk_desc(&d, t, sizeof t);
        udev_get(d.majmin, "ID_PART_TABLE_TYPE", pt, sizeof pt);
        so_b(o, C_TEAL, d.name);
        so_f(o, C_BLACK, "  %s  %s %s  %s  %s", sz, d.bus, drive_type(&d), t, *pt ? pt : "");
        so_par(o);
        if (!np) {
            char mp[1024];
            mounts_of(m, nm, d.name, mp, sizeof mp);
            swap_check(d.name, mp, sizeof mp);
            if (*d.fstype || *mp) { so_t(o, C_GRAY, "    (whole disk) "); so_f(o, C_ORANGE, "%s %s  ", d.fstype, d.label); so_t(o, C_GREEN, mp); so_par(o); }
        }
        for (j = 0; j < np; j++) {
            bdev b;
            char psz[32], mp[1024];
            bdev_load(pn[j], &b);
            bytes_str((double)b.sectors * 512.0, psz, sizeof psz);
            mounts_of(m, nm, b.name, mp, sizeof mp);
            swap_check(b.name, mp, sizeof mp);
            so_t(o, C_GRAY, j == np - 1 ? "  `-- " : "  |-- ");
            so_t(o, C_TEAL, b.name);
            so_f(o, C_VALUE, "  %s  ", psz);
            so_f(o, C_ORANGE, "%s  %s  ", *b.fstype ? b.fstype : "-", *b.label ? b.label : "");
            so_t(o, C_GREEN, mp);
            so_par(o);
            tree_holders(o, b.name, 2, m, nm);
        }
        tree_holders(o, d.name, 1, m, nm);
    }
    dir_free(dn, nd);
    free(m);
}

/* ======================================================================
 * [drives] [hidden] [geometry] [sectors] [partitions]
 * ====================================================================== */
static void location_of(const bdev *d, char *o, size_t sz)
{
    if (!udev_get(d->majmin, "ID_PATH", o, sz)) snprintf(o, sz, "-");
}

static void characteristics(const bdev *d, char *o, size_t sz)
{
    char t[64], sz2[32];
    long v;
    o[0] = 0;
    bytes_str((double)d->sectors * 512.0, sz2, sizeof sz2);
    snprintf(o, sz, "%s %s", sz2, media_of(d));
    if (udev_get(d->majmin, "ID_ATA_ROTATION_RATE_RPM", t, sizeof t) && atoi(t) > 0) { size_t u = strlen(o); snprintf(o + u, sz - u, " %srpm", t); }
    if (rd_fmt(t, sizeof t, "/sys/block/%s/queue/discard_max_bytes", d->disk) && atoll(t) > 0) { size_t u = strlen(o); snprintf(o + u, sz - u, " TRIM"); }
    if (rd_fmt(t, sizeof t, "/sys/block/%s/queue/write_cache", d->disk) && !strcmp(t, "write back")) { size_t u = strlen(o); snprintf(o + u, sz - u, " WriteCache"); }
    { char p[300]; snprintf(p, sizeof p, "/sys/block/%s/ro", d->disk); if (rd_long(p, &v) && v) { size_t u = strlen(o); snprintf(o + u, sz - u, " RO"); } }
}

static void drives_row(srs_out *o, const bdev *d)
{
    static const int cc[] = { C_TEAL, C_ORANGE, C_PURPLE, C_ORANGE, C_BLACK, C_VALUE, C_GREEN, C_GRAY };
    char loc[160], desc[400], ch[160], st[32];
    const char *cells[8];
    location_of(d, loc, sizeof loc);
    disk_desc(d, desc, sizeof desc);
    characteristics(d, ch, sizeof ch);
    if (starts(d->disk, "nvme")) {
        char ctl[64];
        snprintf(ctl, sizeof ctl, "%s", d->disk);
        ctl[strcspn(ctl + 4, "n") + 4] = 0;
        rd_fmt(st, sizeof st, "/sys/class/nvme/%s/state", ctl);
    } else snprintf(st, sizeof st, "%s", *d->state ? d->state : "-");
    cells[0] = d->name; cells[1] = d->bus; cells[2] = loc; cells[3] = drive_type(d); cells[4] = desc;
    cells[5] = *d->rev ? d->rev : "-"; cells[6] = st; cells[7] = ch;
    so_row(o, 8, cells, cc);
}

static void s_drives(srs_out *o)
{
    static const int tabs[] = { 80, 140, 450, 540, 950, 1030, 1100 };
    static const char *const h[] = { "Drive", "Bus", "Location", "Type", "Vendor and Product (Serial Number)", "Revision", "State", "Characteristic" };
    static const int hc[] = { C_TEAL, C_ORANGE, C_PURPLE, C_ORANGE, C_BLACK, C_VALUE, C_GREEN, C_GRAY };
    char **dn;
    int nd = disks_list(&dn, 1), i, pass, cnt = 0;
    so_siv_header(o, "Disk Drives");
    so_tabs(o, tabs, 7);
    so_row(o, 8, h, hc);
    for (pass = 0; pass < 2; pass++) {       /* disks, then optical drives (SIV lists CdRom after) */
        so_par(o);
        for (i = 0; i < nd; i++) {
            bdev d;
            if ((pass == 1) != starts(dn[i], "sr")) continue;
            if (arg_set(o) && strcmp(o->arg, dn[i])) continue;
            bdev_load(dn[i], &d);
            drives_row(o, &d);
            cnt++;
        }
    }
    dir_free(dn, nd);
    so_par(o);
    so_kvf(o, "Drives", C_BLACK, "%d physical drives (loop/zram/device-mapper excluded, see [drive-tree])", cnt);
    so_board_footer(o);
}

static void s_hidden(srs_out *o)
{
    static const int tabs[] = { 120, 260 };
    char **dn, **n;
    int nd = disks_list(&dn, 0), i, c, hid = 0, nom = 0;
    so_siv_header(o, "Hidden Disk Drives");
    so_tabs(o, tabs, 2);
    so_b(o, C_VALUE, "Block devices flagged hidden (/sys/block/*/hidden)"); so_par(o);
    for (i = 0; i < nd; i++) {
        long v = 0;
        char p[300];
        snprintf(p, sizeof p, "/sys/block/%s/hidden", dn[i]);
        if (rd_long(p, &v) && v) { so_kv(o, dn[i], C_ORANGE, "hidden (NVMe multipath path device)"); hid++; }
    }
    if (!hid) so_note(o, "  none - no hidden (multipath path) block devices");
    so_par(o);
    so_b(o, C_VALUE, "Devices present but without media (size 0)"); so_par(o);
    for (i = 0; i < nd; i++) {
        bdev d;
        char desc[300];
        bdev_load(dn[i], &d);
        if (d.sectors) continue;
        disk_desc(&d, desc, sizeof desc);
        so_tab(o); so_t(o, C_TEAL, d.name); so_tab(o); so_f(o, C_GRAY, "%s  %s  %s", d.bus, drive_type(&d), desc); so_par(o);
        nom++;
    }
    if (!nom) so_note(o, "  none");
    so_par(o);
    so_b(o, C_VALUE, "NVMe generic (char) namespaces, not block devices"); so_par(o);
    c = dir_list("/dev", &n);
    {
        char b[512] = "";
        for (i = 0; i < c; i++)
            if (starts(n[i], "ng") && isdigit((unsigned char)n[i][2])) { size_t u = strlen(b); snprintf(b + u, sizeof b - u, "%s/dev/%s", *b ? " " : "", n[i]); }
        so_tab(o); so_t(o, C_PURPLE, *b ? b : "none"); so_par(o);
    }
    dir_free(n, c);
    dir_free(dn, nd);
}

static void s_geometry(srs_out *o)
{
    static const int tabs[] = { 420, 490, 570, 630, 700, 760, 900, 990 };
    static const char *const h[] = { "Physical Drive Description", "Media", "Cylinders", "Tracks", "Sectors", "Bytes", "Total Size", "Size (1000)", "Size (1024)" };
    static const int hc[] = { C_TEAL, C_ORANGE, C_VALUE, C_VALUE, C_VALUE, C_VALUE, C_BLACK, C_GREEN, C_GREEN };
    char **dn;
    int nd = disks_list(&dn, 1), i;
    double tot = 0;
    so_siv_header(o, "Drive Geometry");
    so_tabs(o, tabs, 8);
    so_row(o, 9, h, hc);
    so_par(o);
    for (i = 0; i < nd; i++) {
        bdev d;
        char desc[400], cy[32], tb[48], s1[32], s2[32], lb[16];
        const char *cells[9];
        long lbs = 512;
        double bytes;
        char p[300];
        bdev_load(dn[i], &d);
        snprintf(p, sizeof p, "/sys/block/%s/queue/logical_block_size", d.name);
        rd_long(p, &lbs);
        bytes = (double)d.sectors * 512.0;
        tot += bytes;
        snprintf(desc, sizeof desc, "/dev/%s ", d.name);
        disk_desc(&d, desc + strlen(desc), sizeof desc - strlen(desc));
        commas((long long)(bytes / (double)lbs / (255.0 * 63.0)), cy, sizeof cy);
        commas((long long)bytes, tb, sizeof tb);
        if (bytes >= 1e12) snprintf(s1, sizeof s1, "%.2fTB", bytes / 1e12); else snprintf(s1, sizeof s1, "%.2fGB", bytes / 1e9);
        bytes_str(bytes, s2, sizeof s2);
        snprintf(lb, sizeof lb, "%ld", lbs);
        cells[0] = desc; cells[1] = media_of(&d); cells[2] = cy; cells[3] = "255"; cells[4] = "63"; cells[5] = lb;
        cells[6] = tb; cells[7] = s1; cells[8] = s2;
        so_row(o, 9, cells, NULL);
    }
    dir_free(dn, nd);
    so_par(o);
    {
        char tb[48], s1[32], s2[32];
        const char *cells[9];
        commas((long long)tot, tb, sizeof tb);
        if (tot >= 1e12) snprintf(s1, sizeof s1, "%.2fTB", tot / 1e12); else snprintf(s1, sizeof s1, "%.2fGB", tot / 1e9);
        bytes_str(tot, s2, sizeof s2);
        cells[0] = ""; cells[1] = "Total"; cells[2] = cells[3] = cells[4] = cells[5] = ""; cells[6] = tb; cells[7] = s1; cells[8] = s2;
        so_row(o, 9, cells, NULL);
    }
    so_par(o);
    so_note(o, "Linux addresses disks by LBA; cylinders/tracks/sectors are the legacy 255-head 63-sector translation.");
}

static void s_sectors(srs_out *o)
{
    static const int tabs[] = { 100, 170, 240, 310, 390, 470, 550, 630, 720 };
    static const char *const h[] = { "Disk", "Logical", "Physical", "HW Sector", "Min I/O", "Optimal I/O", "Align Ofs", "Discard Gran", "Max I/O", "Scheduler" };
    static const int hc[] = { C_TEAL, C_VALUE, C_VALUE, C_VALUE, C_VALUE, C_VALUE, C_PURPLE, C_VALUE, C_VALUE, C_ORANGE };
    static const char *const f[] = { "queue/logical_block_size", "queue/physical_block_size", "queue/hw_sector_size", "queue/minimum_io_size",
                                     "queue/optimal_io_size", "alignment_offset", "queue/discard_granularity", "queue/max_sectors_kb" };
    char **dn;
    int nd = disks_list(&dn, 0), i, k;
    so_siv_header(o, "Disk Sector Sizes and Offsets");
    so_tabs(o, tabs, 9);
    so_row(o, 10, h, hc);
    so_par(o);
    for (i = 0; i < nd; i++) {
        char v[9][32], sch[128], *s, *e;
        const char *cells[10];
        for (k = 0; k < 8; k++) if (!rd_fmt(v[k], sizeof v[k], "/sys/block/%s/%s", dn[i], f[k])) snprintf(v[k], sizeof v[k], "-");
        if (strcmp(v[7], "-")) { size_t u = strlen(v[7]); snprintf(v[7] + u, sizeof v[7] - u, "KB"); }
        sch[0] = 0;
        rd_fmt(sch, sizeof sch, "/sys/block/%s/queue/scheduler", dn[i]);
        s = strchr(sch, '['); e = s ? strchr(s, ']') : NULL;
        if (s && e) { *e = 0; memmove(sch, s + 1, strlen(s + 1) + 1); }
        cells[0] = dn[i];
        for (k = 0; k < 8; k++) cells[k + 1] = v[k];
        cells[9] = *sch ? sch : "-";
        so_row(o, 10, cells, NULL);
    }
    so_par(o);
    so_b(o, C_VALUE, "Partition alignment"); so_par(o); so_par(o);
    for (i = 0; i < nd; i++) {
        char pn[64][64];
        int np = parts_of(dn[i], pn, 64), j;
        long pbs = 512;
        char p[300];
        snprintf(p, sizeof p, "/sys/block/%s/queue/physical_block_size", dn[i]);
        rd_long(p, &pbs);
        for (j = 0; j < np; j++) {
            bdev b;
            long long off;
            char cs[48];
            bdev_load(pn[j], &b);
            off = b.start * 512;
            commas(off, cs, sizeof cs);
            so_t(o, C_TEAL, b.name); so_tab(o);
            so_f(o, C_VALUE, "start sector %lld", b.start); so_tab(o); so_tab(o); so_tab(o);
            so_f(o, C_VALUE, "offset %s", cs); so_tab(o); so_tab(o); so_tab(o);
            so_check(o, pbs > 0 && off % pbs == 0); so_t(o, C_LABEL, "Physical ");
            so_check(o, off % 4096 == 0); so_t(o, C_LABEL, "4K ");
            so_check(o, off % (1024 * 1024) == 0); so_t(o, C_LABEL, "1MiB");
            so_par(o);
        }
    }
    dir_free(dn, nd);
}

static const char *ptype_name(const char *t)
{
    static const char *const m[][2] = {
        { "c12a7328-f81f-11d2-ba4b-00a0c93ec93b", "EFI System" }, { "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7", "Microsoft basic data" },
        { "e3c9e316-0b5c-4db8-817d-f92df00215ae", "Microsoft reserved" }, { "de94bba4-06d1-4d40-a16a-bfd50179d6ac", "Windows recovery" },
        { "0fc63daf-8483-4772-8e79-3d69d8477de4", "Linux filesystem" }, { "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f", "Linux swap" },
        { "4f68bce3-e8cd-4db1-96e7-fbcaf984b709", "Linux root (x86-64)" }, { "e6d6d379-f507-44c2-a23c-238f2a3df928", "Linux LVM" },
        { "a19d880f-05fc-4d3b-a006-743f0f84911e", "Linux RAID" }, { "933ac7e1-2eb4-4f13-b844-0e14e2aef915", "Linux home" },
        { "bc13c2ff-59e6-4262-a352-b275fd6f7172", "Linux extended boot" }, { "21686148-6449-6e6f-744e-656564454649", "BIOS boot" },
        { "ca7d7ccb-63ed-4c53-861c-1742536059cc", "LUKS" }, { "3b8f8425-20e0-4f3b-907f-1a25a76f98e8", "Linux /srv" },
        { "0x7", "NTFS/exFAT/HPFS" }, { "0xb", "FAT32" }, { "0xc", "FAT32 (LBA)" }, { "0xe", "FAT16 (LBA)" }, { "0x6", "FAT16" },
        { "0x83", "Linux" }, { "0x82", "Linux swap" }, { "0xef", "EFI (FAT)" }, { "0xee", "GPT protective" }, { "0x5", "Extended" },
        { "0xf", "Extended (LBA)" }, { "0x17", "Hidden NTFS" }, { "0x1b", "Hidden FAT32" }, { "0x1c", "Hidden FAT32 (LBA)" }, { "0x27", "Hidden recovery" },
        { "0x8e", "Linux LVM" }, { "0xfd", "Linux RAID" },
    };
    size_t i;
    for (i = 0; i < sizeof m / sizeof *m; i++) if (!strcasecmp(t, m[i][0])) return m[i][1];
    return t;
}

static void s_partitions(srs_out *o)
{
    static const int tabs[] = { 30, 130, 330, 490, 570, 720, 800, 960, 1010 };
    static const char *const h[] = { "#", "Device", "Type", "Partition Start", "about", "Partition Length", "about", "Name", "Hidden", "Boot" };
    static const int hc[] = { C_VALUE, C_TEAL, C_ORANGE, C_VALUE, C_GREEN, C_VALUE, C_GREEN, C_PURPLE, C_GRAY, C_GRAY };
    char **dn;
    int nd = disks_list(&dn, 0), i, j;
    so_siv_header(o, "Partitions");
    so_tabs(o, tabs, 9);
    for (i = 0; i < nd; i++) {
        bdev d;
        char pn[64][64], desc[400], pt[32], pu[80];
        int np;
        if (arg_set(o) && strcmp(o->arg, dn[i])) continue;
        np = parts_of(dn[i], pn, 64);
        bdev_load(dn[i], &d);
        if (!np && !arg_set(o)) continue;
        disk_desc(&d, desc, sizeof desc);
        udev_get(d.majmin, "ID_PART_TABLE_TYPE", pt, sizeof pt);
        udev_get(d.majmin, "ID_PART_TABLE_UUID", pu, sizeof pu);
        so_b(o, C_TEAL, "/dev/"); so_b(o, C_TEAL, d.name); so_t(o, C_BLACK, "  "); so_t(o, C_BLACK, desc);
        so_f(o, C_PURPLE, "   %s  Signature %s", *pt ? pt : "no table", *pu ? pu : "-");
        so_par(o);
        so_row(o, 10, h, hc);
        for (j = 0; j < np; j++) {
            bdev b;
            char num[8], st[48], sa[32], ln[48], la[32], fl[32], hid[8], boot[16];
            const char *cells[10];
            int tid;
            bdev_load(pn[j], &b);
            snprintf(num, sizeof num, "%d", b.partno);
            commas(b.start * 512, st, sizeof st); bytes_str((double)b.start * 512.0, sa, sizeof sa);
            commas(b.sectors * 512, ln, sizeof ln); bytes_str((double)b.sectors * 512.0, la, sizeof la);
            udev_get(b.majmin, "ID_PART_ENTRY_FLAGS", fl, sizeof fl);
            tid = (int)strtol(b.ptype, NULL, 16);
            snprintf(hid, sizeof hid, "%s", starts(b.ptype, "0x") && (tid & 0xF0) == 0x10 ? "Yes" : "No");
            if (!strcasecmp(b.ptype, "c12a7328-f81f-11d2-ba4b-00a0c93ec93b") || !strcmp(b.ptype, "0xef")) snprintf(boot, sizeof boot, "ESP");
            else if (!strcmp(fl, "0x80")) snprintf(boot, sizeof boot, "Active");
            else snprintf(boot, sizeof boot, "No");
            cells[0] = num; cells[1] = b.name; cells[2] = *b.ptype ? ptype_name(b.ptype) : "-"; cells[3] = st; cells[4] = sa;
            cells[5] = ln; cells[6] = la; cells[7] = b.pname; cells[8] = hid; cells[9] = boot;
            so_row(o, 10, cells, hc);
        }
        if (!np) so_note(o, "  no partition table (whole-disk filesystem or empty)");
        so_par(o);
    }
    dir_free(dn, nd);
    so_note(o, "Source: /sys/class/block/<part>/{start,size,partition} + udev ID_PART_ENTRY_* (no raw sector reads).");
}

/* ======================================================================
 * [performance] - /proc/diskstats deltas
 * ====================================================================== */
typedef struct { char name[32]; unsigned long long rio, rmerge, rsec, wio, wmerge, wsec, inflight, ticks; } dstat;

static int diskstats(dstat *d, int max)
{
    char *t, *l, *sv;
    int n = 0;
    if (rd_all("/proc/diskstats", &t) < 0) return 0;
    for (l = strtok_r(t, "\n", &sv); l && n < max; l = strtok_r(NULL, "\n", &sv)) {
        dstat x;
        unsigned mj, mn;
        unsigned long long rt, wt;
        memset(&x, 0, sizeof x);
        if (sscanf(l, "%u %u %31s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &mj, &mn, x.name, &x.rio, &x.rmerge, &x.rsec, &rt,
                   &x.wio, &x.wmerge, &x.wsec, &wt, &x.inflight, &x.ticks) != 13) continue;
        {
            char p[300];
            snprintf(p, sizeof p, "/sys/block/%s", x.name);
            if (!path_exists(p) || !is_phys(x.name)) continue;
        }
        d[n++] = x;
    }
    free(t);
    return n;
}

static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9; }

static void s_performance(srs_out *o)
{
    static dstat prev[MAXD];
    static int nprev;
    static double tprev;
    static const int tabs[] = { 360, 450, 540, 630, 720, 810, 900 };
    dstat cur[MAXD];
    int n, i, j;
    double t, dt;
    char hd[200];
    time_t tt = time(NULL);
    struct tm tm;
    localtime_r(&tt, &tm);
    if (!nprev || now_s() - tprev > 10.0) {
        struct timespec ts = { 0, 250000000 };
        nprev = diskstats(prev, MAXD);
        tprev = now_s();
        nanosleep(&ts, NULL);
    }
    n = diskstats(cur, MAXD);
    t = now_s();
    dt = t - tprev;
    if (dt <= 0) dt = 1e-3;
    strftime(hd, sizeof hd, "Disk Performance @ %Y-%m-%d %H:%M:%S", &tm);
    so_siv_header(o, hd);
    so_tabs(o, tabs, 7);
    {
        static const char *const h1[] = { "Disk Drive Description and Serial Number", "Read Size", "Read Delta", "Read Count", "Read Delta", "Merge Count", "Merge Delta", "" };
        static const char *const h2[] = { "     Type    Location", "Write Size", "Write Delta", "Write Count", "Write Delta", "Queue Depth", "Busy %", "" };
        static const int hc[] = { C_TEAL, C_VALUE, C_GREEN, C_VALUE, C_GREEN, C_VALUE, C_GREEN, C_GRAY };
        so_row(o, 7, h1, hc);
        so_row(o, 7, h2, hc);
    }
    for (i = 0; i < n; i++) {
        dstat *p = NULL;
        bdev d;
        char desc[400], loc[200], c[7][40];
        const char *cells[7];
        static const int cc[] = { C_BLACK, C_VALUE, C_GREEN, C_VALUE, C_GREEN, C_VALUE, C_GREEN };
        static const int cc2[] = { C_ORANGE, C_VALUE, C_GREEN, C_VALUE, C_GREEN, C_VALUE, C_GREEN };
        for (j = 0; j < nprev; j++) if (!strcmp(prev[j].name, cur[i].name)) p = &prev[j];
        bdev_load(cur[i].name, &d);
        so_par(o);
        snprintf(desc, sizeof desc, "%s  ", d.name);
        disk_desc(&d, desc + strlen(desc), sizeof desc - strlen(desc));
        bytes_str((double)cur[i].rsec * 512.0, c[0], sizeof c[0]);
        bytes_str(p ? (double)(cur[i].rsec - p->rsec) * 512.0 / dt : 0, c[1], sizeof c[1]);
        { size_t u = strlen(c[1]); snprintf(c[1] + u, sizeof c[1] - u, "/s"); }
        commas((long long)cur[i].rio, c[2], sizeof c[2]);
        snprintf(c[3], sizeof c[3], "%.0f/s", p ? (double)(cur[i].rio - p->rio) / dt : 0.0);
        commas((long long)cur[i].rmerge, c[4], sizeof c[4]);
        snprintf(c[5], sizeof c[5], "%.0f/s", p ? (double)(cur[i].rmerge - p->rmerge) / dt : 0.0);
        cells[0] = desc; for (j = 0; j < 6; j++) cells[j + 1] = c[j];
        so_row(o, 7, cells, cc);
        location_of(&d, loc, sizeof loc);
        snprintf(desc, sizeof desc, "     %-9s %s", drive_type(&d), loc);
        bytes_str((double)cur[i].wsec * 512.0, c[0], sizeof c[0]);
        bytes_str(p ? (double)(cur[i].wsec - p->wsec) * 512.0 / dt : 0, c[1], sizeof c[1]);
        { size_t u = strlen(c[1]); snprintf(c[1] + u, sizeof c[1] - u, "/s"); }
        commas((long long)cur[i].wio, c[2], sizeof c[2]);
        snprintf(c[3], sizeof c[3], "%.0f/s", p ? (double)(cur[i].wio - p->wio) / dt : 0.0);
        snprintf(c[4], sizeof c[4], "%llu", cur[i].inflight);
        snprintf(c[5], sizeof c[5], "%.1f%%", p ? (double)(cur[i].ticks - p->ticks) / (dt * 10.0) : 0.0);
        cells[0] = desc; for (j = 0; j < 6; j++) cells[j + 1] = c[j];
        so_row(o, 7, cells, cc2);
    }
    so_par(o);
    so_kvf(o, "Interval", C_GRAY, "%.2fs  (deltas per second; source /proc/diskstats)", dt);
    memcpy(prev, cur, sizeof cur[0] * (size_t)n);
    nprev = n;
    tprev = t;
    so_board_footer(o);
}

/* ======================================================================
 * [scsi] [usb-disks] [dsk-api]
 * ====================================================================== */
static const char *scsi_type(int t)
{
    static const char *const n[] = { "Direct-Access", "Sequential", "Printer", "Processor", "WORM", "CD/DVD", "Scanner", "Optical", "Medium Changer",
                                     "Communications", "?", "?", "RAID", "Enclosure", "RBC", "OCRW", "Bridge", "OSD", "ZBC" };
    return t >= 0 && t < (int)(sizeof n / sizeof *n) ? n[t] : "?";
}

static void first_entry(const char *dir, char *o, size_t sz)
{
    char **n;
    int c = dir_list(dir, &n);
    o[0] = 0;
    if (c > 0) snprintf(o, sz, "%s", n[0]);
    dir_free(n, c);
}

static void s_scsi(srs_out *o)
{
    static const int tabs[] = { 90, 200, 290, 420, 590, 650, 730, 790 };
    static const char *const h[] = { "H:C:T:L", "Type", "Vendor", "Product", "Revision", "Block", "Generic", "Host", "State" };
    static const int hc[] = { C_TEAL, C_ORANGE, C_VALUE, C_BLACK, C_VALUE, C_TEAL, C_PURPLE, C_ORANGE, C_GREEN };
    char **n;
    int c = dir_list("/sys/class/scsi_device", &n), i;
    so_siv_header(o, "SCSI Devices");
    so_tabs(o, tabs, 8);
    so_row(o, 9, h, hc);
    so_par(o);
    for (i = 0; i < c; i++) {
        char p[300], ty[16], ve[32], mo[64], rv[16], bl[32], sg[32], ho[64], st[32], hn[16];
        const char *cells[9];
        snprintf(p, sizeof p, "/sys/class/scsi_device/%s/device", n[i]);
        ty[0] = ve[0] = mo[0] = rv[0] = st[0] = ho[0] = 0;
        rd_fmt(ty, sizeof ty, "%s/type", p);
        rd_fmt(ve, sizeof ve, "%s/vendor", p);
        rd_fmt(mo, sizeof mo, "%s/model", p);
        rd_fmt(rv, sizeof rv, "%s/rev", p);
        rd_fmt(st, sizeof st, "%s/state", p);
        { char d2[340]; snprintf(d2, sizeof d2, "%s/block", p); first_entry(d2, bl, sizeof bl); snprintf(d2, sizeof d2, "%s/scsi_generic", p); first_entry(d2, sg, sizeof sg); }
        snprintf(hn, sizeof hn, "%d", atoi(n[i]));
        rd_fmt(ho, sizeof ho, "/sys/class/scsi_host/host%s/proc_name", hn);
        { char hb[96]; snprintf(hb, sizeof hb, "host%s %s", hn, ho); snprintf(ho, sizeof ho, "%s", hb); }
        cells[0] = n[i]; cells[1] = scsi_type(*ty ? atoi(ty) : -1); cells[2] = ve; cells[3] = mo; cells[4] = rv;
        cells[5] = *bl ? bl : "-"; cells[6] = *sg ? sg : "-"; cells[7] = ho; cells[8] = st;
        so_row(o, 9, cells, hc);
    }
    if (c <= 0) so_note(o, "No SCSI devices (/sys/class/scsi_device is empty)");
    dir_free(n, c);
    so_par(o);
    {
        char **hs;
        int hc2 = dir_list("/sys/class/scsi_host", &hs), k;
        char b[1024] = "";
        for (k = 0; k < hc2; k++) {
            char pn[64];
            size_t u = strlen(b);
            rd_fmt(pn, sizeof pn, "/sys/class/scsi_host/%s/proc_name", hs[k]);
            snprintf(b + u, sizeof b - u, "%s%s=%s", k ? "  " : "", hs[k], pn);
        }
        dir_free(hs, hc2);
        so_kvf(o, "SCSI hosts", C_GRAY, "%d  %s", hc2 < 0 ? 0 : hc2, b);
    }
    so_board_footer(o);
}

/* the USB device directory (has idVendor) above a block device */
static int usb_dev_dir(const char *disk, char *o, size_t sz)
{
    char p[300], rp[PATH_MAX], t[PATH_MAX + 20];
    snprintf(p, sizeof p, "/sys/block/%s", disk);
    if (!realpath(p, rp)) return 0;
    while (strlen(rp) > 12) {
        char *s = strrchr(rp, '/');
        snprintf(t, sizeof t, "%s/idVendor", rp);
        if (path_exists(t)) { snprintf(o, sz, "%s", rp); return 1; }
        if (!s) break;
        *s = 0;
    }
    return 0;
}

static void s_usb_disks(srs_out *o)
{
    static const int tabs[] = { 60, 210, 480, 600, 680, 780, 900 };
    static const char *const h[] = { "Disk", "Vendor and Product", "USB Bridge (Manufacturer Product)", "VID:PID", "Speed", "Driver", "Size", "Port" };
    static const int hc[] = { C_TEAL, C_BLACK, C_ORANGE, C_PURPLE, C_GREEN, C_VALUE, C_VALUE, C_GRAY };
    char **dn;
    int nd = disks_list(&dn, 1), i, cnt = 0;
    so_siv_header(o, "USB Disk Drives");
    so_tabs(o, tabs, 7);
    so_row(o, 8, h, hc);
    so_par(o);
    for (i = 0; i < nd; i++) {
        bdev d;
        char ud[PATH_MAX], br[200], mf[96], pr[96], vid[16], pid[16], vp[40], sp[32], drv[64], sz[32], m[PATH_MAX], p[PATH_MAX + 40], rp[PATH_MAX];
        const char *cells[8];
        if (!usb_dev_dir(dn[i], ud, sizeof ud)) continue;
        bdev_load(dn[i], &d);
        mf[0] = pr[0] = vid[0] = pid[0] = sp[0] = drv[0] = 0;
        rd_fmt(mf, sizeof mf, "%s/manufacturer", ud);
        rd_fmt(pr, sizeof pr, "%s/product", ud);
        rd_fmt(vid, sizeof vid, "%s/idVendor", ud);
        rd_fmt(pid, sizeof pid, "%s/idProduct", ud);
        rd_fmt(sp, sizeof sp, "%s/speed", ud);
        snprintf(br, sizeof br, "%s %s", mf, pr);
        trim(br);
        snprintf(vp, sizeof vp, "%s:%s", vid, pid);
        if (*sp) { size_t u = strlen(sp); snprintf(sp + u, sizeof sp - u, "M"); }
        /* the interface driver: usb-storage or uas */
        snprintf(p, sizeof p, "/sys/block/%s", dn[i]);
        if (realpath(p, rp)) {
            char *s = strstr(rp + strlen(ud), "/host");
            if (s) { *s = 0; snprintf(p, sizeof p, "%s/driver", rp); if (realpath(p, m)) snprintf(drv, sizeof drv, "%s", base_name(m)); }
        }
        snprintf(m, sizeof m, "%s %s", d.vendor, d.model);
        trim(m);
        if (*d.serial) { size_t u = strlen(m); snprintf(m + u, sizeof m - u, " (%s)", d.serial); }
        bytes_str((double)d.sectors * 512.0, sz, sizeof sz);
        if (!d.sectors) snprintf(sz, sizeof sz, "no media");
        cells[0] = d.name; cells[1] = m; cells[2] = *br ? br : "-"; cells[3] = vp; cells[4] = sp; cells[5] = drv; cells[6] = sz; cells[7] = base_name(ud);
        so_row(o, 8, cells, hc);
        cnt++;
    }
    dir_free(dn, nd);
    if (!cnt) so_note(o, "No USB Disk Drives found");
    so_par(o);
    so_kvf(o, "USB disks", C_BLACK, "%d", cnt);
    so_note(o, "Source: /sys/block/* resolved to the USB device (idVendor/idProduct/manufacturer/product/speed); devices are not opened.");
}

static int in_path(const char *exe)
{
    const char *pp = getenv("PATH");
    char d[4096], *s, *sv, f[4200];
    if (!pp) pp = "/usr/bin:/bin:/usr/sbin:/sbin";
    snprintf(d, sizeof d, "%s:/usr/sbin:/sbin", pp);
    for (s = strtok_r(d, ":", &sv); s; s = strtok_r(NULL, ":", &sv)) {
        snprintf(f, sizeof f, "%s/%s", s, exe);
        if (access(f, X_OK) == 0) return 1;
    }
    return 0;
}

static void s_dsk_api(srs_out *o)
{
    static const int tabs[] = { 200, 400, 600 };
    static const char *const tools[] = { "smartctl", "nvme", "hdparm", "sdparm", "sg_inq", "lsscsi", "blkid", "lsblk", "udisksctl", "mdadm", "lvm", "btrfs", "skdump", "fio", NULL };
    static const char *const ifs[][2] = { { "/dev/bsg", "SCSI bsg" }, { "/sys/class/nvme", "NVMe class" }, { "/sys/class/scsi_generic", "SCSI generic (sg)" },
                                          { "/run/udev/data", "udev database" }, { "/sys/class/hwmon", "hwmon (drivetemp/nvme)" }, { "/dev/mapper/control", "device-mapper" },
                                          { "/proc/mdstat", "md RAID" }, { "/sys/fs/btrfs", "btrfs sysfs" } };
    int i, k = 0, found = 0;
    size_t j;
    so_siv_header(o, "Disk API Routines");
    so_tabs(o, tabs, 3);
    so_b(o, C_VALUE, "Disk tools (Linux equivalents of SIV's vendor disk SDK routines)"); so_par(o); so_par(o);
    for (i = 0; tools[i]; i++) {
        int on = in_path(tools[i]);
        found += on;
        if (k) so_tab(o);
        so_check(o, on); so_t(o, on ? C_VALUE : C_GRAY, tools[i]);
        if (++k == 4) { so_par(o); k = 0; }
    }
    if (k) so_par(o);
    so_par(o);
    so_f(o, C_BLACK, "Found %d of %d disk tools in PATH", found, i);
    so_par(o); so_par(o);
    so_b(o, C_VALUE, "Kernel interfaces"); so_par(o); so_par(o);
    k = 0;
    for (j = 0; j < sizeof ifs / sizeof *ifs; j++) {
        int on = path_exists(ifs[j][0]);
        if (k) so_tab(o);
        so_check(o, on); so_t(o, on ? C_VALUE : C_GRAY, ifs[j][1]);
        if (++k == 4) { so_par(o); k = 0; }
    }
    if (k) so_par(o);
    so_par(o);
    so_note(o, "SIV's dsk-api lists JMicron SDK (jmapi_x64.dll) entry points; on Linux the kernel drivers and these tools replace it.");
}

/* ======================================================================
 * NVMe [stornvme] [secnvme]
 * ====================================================================== */
static void hwmon_temps(const char *dir, char *o, size_t sz)
{
    char **n, p[600];
    int c = dir_list(dir, &n), i;
    o[0] = 0;
    for (i = 0; i < c; i++) {
        long v;
        char lab[64];
        size_t u;
        if (!starts(n[i], "temp") || !strstr(n[i], "_input")) continue;
        snprintf(p, sizeof p, "%s/%s", dir, n[i]);
        if (!rd_long(p, &v)) continue;
        snprintf(p, sizeof p, "%s/%.*s_label", dir, (int)(strstr(n[i], "_input") - n[i]), n[i]);
        if (!rd_str(p, lab, sizeof lab)) snprintf(lab, sizeof lab, "%.*s", (int)(strstr(n[i], "_input") - n[i]), n[i]);
        u = strlen(o);
        snprintf(o + u, sz - u, "%s%s %.1fC", u ? "  " : "", lab, (double)v / 1000.0);
    }
    dir_free(n, c);
}

/* hwmon directory of a disk: nvme controller hwmonN or SCSI drivetemp device/hwmon/hwmonN */
static int disk_hwmon(const char *disk, char *o, size_t sz)
{
    char p[300], h[64];
    if (starts(disk, "nvme")) {
        char ctl[64], **n;
        int c, i, ok = 0;
        snprintf(ctl, sizeof ctl, "%s", disk);
        ctl[strcspn(ctl + 4, "n") + 4] = 0;
        snprintf(p, sizeof p, "/sys/class/nvme/%s", ctl);
        c = dir_list(p, &n);
        for (i = 0; i < c && !ok; i++) if (starts(n[i], "hwmon")) { snprintf(o, sz, "%s/%s", p, n[i]); ok = 1; }
        dir_free(n, c);
        return ok;
    }
    snprintf(p, sizeof p, "/sys/block/%s/device/hwmon", disk);
    first_entry(p, h, sizeof h);
    if (!*h) return 0;
    snprintf(o, sz, "%s/%s", p, h);
    return 1;
}

static void nvme_ctrl(srs_out *o, const char *c)
{
    static const char *const f[][2] = { { "model", "Model" }, { "serial", "Serial number" }, { "firmware_rev", "Firmware" }, { "state", "State" },
                                        { "transport", "Transport" }, { "cntlid", "Controller ID" }, { "cntrltype", "Controller type" },
                                        { "subsysnqn", "Subsystem NQN" }, { "queue_count", "I/O queues" }, { "sqsize", "SQ size" },
                                        { "numa_node", "NUMA node" }, { "kato", "Keep-alive" } };
    char p[300], b[512], b2[64], hw[400], rp[PATH_MAX], **n;
    size_t i;
    int k, cn;
    snprintf(b, sizeof b, "/dev/%s", c);
    so_b(o, C_TEAL, b); so_par(o);
    for (i = 0; i < sizeof f / sizeof *f; i++) {
        snprintf(p, sizeof p, "/sys/class/nvme/%s/%s", c, f[i][0]);
        if (rd_str(p, b, sizeof b)) so_kv(o, f[i][1], i == 0 ? C_BLACK : i == 3 ? C_GREEN : C_VALUE, b);
    }
    snprintf(p, sizeof p, "/sys/class/nvme/%s/device", c);
    if (realpath(p, rp)) so_kv(o, "PCI address", C_PURPLE, base_name(rp));
    b[0] = b2[0] = 0;
    rd_fmt(b, sizeof b, "/sys/class/nvme/%s/device/vendor", c);
    rd_fmt(b2, sizeof b2, "/sys/class/nvme/%s/device/device", c);
    if (*b) so_kvf(o, "PCI ID", C_PURPLE, "%s:%s", b + (starts(b, "0x") ? 2 : 0), b2 + (starts(b2, "0x") ? 2 : 0));
    {
        char cs[64] = "", cw[16] = "", ms[64] = "", mw[16] = "";
        rd_fmt(cs, sizeof cs, "/sys/class/nvme/%s/device/current_link_speed", c);
        rd_fmt(cw, sizeof cw, "/sys/class/nvme/%s/device/current_link_width", c);
        rd_fmt(ms, sizeof ms, "/sys/class/nvme/%s/device/max_link_speed", c);
        rd_fmt(mw, sizeof mw, "/sys/class/nvme/%s/device/max_link_width", c);
        if (*cs) so_kvf(o, "PCIe link", strcmp(cs, ms) || strcmp(cw, mw) ? C_ORANGE : C_GREEN, "%s x%s  (max %s x%s)", cs, cw, ms, mw);
    }
    snprintf(p, sizeof p, "/sys/class/nvme/%s/device/driver", c);
    if (realpath(p, rp)) so_kv(o, "Driver", C_VALUE, base_name(rp));
    snprintf(p, sizeof p, "/sys/class/nvme/%s", c);
    cn = dir_list(p, &n);
    for (k = 0; k < cn; k++) {
        if (starts(n[k], c) && strchr(n[k] + strlen(c), 'n')) {
            long sz = 0, lbs = 512;
            char s1[32], q[400];
            snprintf(q, sizeof q, "/sys/class/nvme/%s/%s/size", c, n[k]); rd_long(q, &sz);
            snprintf(q, sizeof q, "/sys/class/nvme/%s/%s/queue/logical_block_size", c, n[k]); rd_long(q, &lbs);
            bytes_str((double)sz * 512.0, s1, sizeof s1);
            snprintf(q, sizeof q, "Namespace %s", n[k]);
            so_kvf(o, q, C_TEAL, "%s  LBA %ld bytes", s1, lbs);
        }
        if (starts(n[k], "hwmon")) {
            snprintf(hw, sizeof hw, "/sys/class/nvme/%s/%s", c, n[k]);
            hwmon_temps(hw, b, sizeof b);
            so_kv(o, "Temperatures", C_ORANGE, b);
        }
    }
    dir_free(n, cn);
}

static void root_dump(srs_out *o, const char *dev, int full)
{
    char f[80], *t;
    const char *p;
    snprintf(f, sizeof f, "nvme-%s.txt", dev);
    p = so_root_file(f);
    if (!p) return;
    if (rd_all(p, &t) >= 0) {
        so_t(o, C_PURPLE, full ? "nvme smart-log (srs-root cache)" : "SMART log available in the srs-root cache, see [smart]");
        so_par(o);
        if (full) so_lines(o, C_BLACK, t);
        free(t);
    }
}

static void nvme_list(srs_out *o, int samsung_only)
{
    static const int tabs[] = { 200 };
    char **n;
    int c = dir_list("/sys/class/nvme", &n), i, shown = 0, any_cache = 0;
    so_tabs(o, tabs, 1);
    for (i = 0; i < c; i++) {
        char v[32] = "", m[128] = "", ns[80];
        rd_fmt(v, sizeof v, "/sys/class/nvme/%s/device/vendor", n[i]);
        rd_fmt(m, sizeof m, "/sys/class/nvme/%s/model", n[i]);
        if (samsung_only && strcmp(v, "0x144d") && !strstr(m, "Samsung")) continue;
        nvme_ctrl(o, n[i]);
        snprintf(ns, sizeof ns, "%sn1", n[i]);
        if (so_root_file((snprintf(m, sizeof m, "nvme-%s.txt", ns), m)) || so_root_file((snprintf(m, sizeof m, "nvme-%s.txt", n[i]), m))) any_cache = 1;
        root_dump(o, ns, 0);
        root_dump(o, n[i], 0);
        so_par(o);
        shown++;
    }
    dir_free(n, c);
    if (!shown) so_kv(o, "Status", C_ORANGE, samsung_only ? "No Samsung NVMe controllers found" : "No NVMe controllers found");
    if (shown && !any_cache) so_kv(o, "Identify/SMART", C_GRAY, "Needs root: Menu > Run as root (srs-root)");
    so_note(o, "Linux uses the in-kernel nvme driver for all vendors (Samsung's secnvme / Microsoft's stornvme are Windows-only).");
}

static void s_stornvme(srs_out *o) { so_siv_header(o, "NVMe Controllers (nvme driver)"); nvme_list(o, 0); so_board_footer(o); }
static void s_secnvme(srs_out *o) { so_siv_header(o, "Samsung NVMe Controllers"); nvme_list(o, 1); so_board_footer(o); }

/* ======================================================================
 * SMART [smart] [smart-all] [smart-sdm]
 * ====================================================================== */
static int smart_cache(const char *disk, char *path, size_t sz)
{
    char f[96], ctl[64];
    const char *p;
    snprintf(f, sizeof f, "smart-%s.txt", disk);
    if ((p = so_root_file(f))) { snprintf(path, sz, "%s", p); return 1; }
    snprintf(f, sizeof f, "nvme-%s.txt", disk);
    if ((p = so_root_file(f))) { snprintf(path, sz, "%s", p); return 2; }
    if (starts(disk, "nvme")) {
        snprintf(ctl, sizeof ctl, "%s", disk);
        ctl[strcspn(ctl + 4, "n") + 4] = 0;
        snprintf(f, sizeof f, "smart-%s.txt", ctl);
        if ((p = so_root_file(f))) { snprintf(path, sz, "%s", p); return 1; }
        snprintf(f, sizeof f, "nvme-%s.txt", ctl);
        if ((p = so_root_file(f))) { snprintf(path, sz, "%s", p); return 2; }
    }
    return 0;
}

/* pull a value out of a smartctl / nvme smart-log text: first line containing key; last token (or text after ':') */
static int cache_val(const char *txt, const char *key, int last_tok, char *o, size_t sz)
{
    const char *p = strstr(txt, key), *e, *s;
    char line[512];
    o[0] = 0;
    if (!p) return 0;
    e = strchr(p, '\n');
    snprintf(line, sizeof line, "%.*s", (int)(e ? e - p : (long)strlen(p)), p);
    if (last_tok) {
        char *q;
        trim(line);
        q = strrchr(line, ' ');
        snprintf(o, sz, "%s", q ? q + 1 : line);
        /* smartctl raw values like "12345 (Average 12)" or "2134h+05m" */
        return 1;
    }
    s = strchr(line, ':');
    snprintf(o, sz, "%s", s ? s + 1 : line);
    trim(o);
    return 1;
}

typedef struct { char health[64], hours[48], cycles[48], temp[48]; } smartsum;

static void smart_summary(const char *disk, smartsum *s)
{
    char path[1100], *t;
    int k = smart_cache(disk, path, sizeof path);
    memset(s, 0, sizeof *s);
    if (!k || rd_all(path, &t) < 0) return;
    if (!cache_val(t, "self-assessment test result", 0, s->health, sizeof s->health))
        if (!cache_val(t, "SMART Health Status", 0, s->health, sizeof s->health))
            if (cache_val(t, "critical_warning", 0, s->health, sizeof s->health))
                snprintf(s->health, sizeof s->health, "%s", atoi(s->health) == 0 ? "OK (no critical warning)" : "CRITICAL WARNING");
    if (!cache_val(t, "Power On Hours:", 0, s->hours, sizeof s->hours))
        if (!cache_val(t, "power_on_hours", 0, s->hours, sizeof s->hours))
            if (cache_val(t, "Power_On_Hours", 1, s->hours, sizeof s->hours)) {}
    if (!cache_val(t, "Power Cycles:", 0, s->cycles, sizeof s->cycles))
        if (!cache_val(t, "power_cycles", 0, s->cycles, sizeof s->cycles))
            cache_val(t, "Power_Cycle_Count", 1, s->cycles, sizeof s->cycles);
    if (!cache_val(t, "Temperature:", 0, s->temp, sizeof s->temp))
        cache_val(t, "Current Temperature:", 0, s->temp, sizeof s->temp);
    free(t);
}

static void smart_capable(const bdev *d, char *o, size_t sz)
{
    char a[16], b[16];
    if (starts(d->disk, "nvme")) { snprintf(o, sz, "NVMe log"); return; }
    if (udev_get(d->majmin, "ID_ATA_FEATURE_SET_SMART", a, sizeof a)) {
        udev_get(d->majmin, "ID_ATA_FEATURE_SET_SMART_ENABLED", b, sizeof b);
        snprintf(o, sz, "%s", atoi(b) ? "Enabled" : "Disabled");
        return;
    }
    snprintf(o, sz, "%s", !strcmp(d->bus, "USB") ? "unknown (USB bridge)" : "-");
}

static void s_smart(srs_out *o)
{
    static const int tabs[] = { 80, 400, 470, 550, 640, 880, 980 };
    static const char *const h[] = { "Drive", "Model (Serial)", "Bus", "Size", "SMART", "Temperature", "Health", "Power On" };
    static const int hc[] = { C_TEAL, C_BLACK, C_ORANGE, C_VALUE, C_GREEN, C_ORANGE, C_GREEN, C_VALUE };
    char **dn;
    int nd = disks_list(&dn, 1), i, cached = 0;
    if (arg_set(o)) {   /* smart:<dev> = the all-details view of one drive */
        dir_free(dn, nd);
        so_siv_header(o, "SMART Drive Information");
        goto one;
    }
    so_siv_header(o, "SMART Drive Information");
    so_tabs(o, tabs, 7);
    so_row(o, 8, h, hc);
    so_par(o);
    for (i = 0; i < nd; i++) {
        bdev d;
        smartsum s;
        char desc[400], sz[32], cap[32], hw[400], tm[200], path[1100];
        const char *cells[8];
        if (starts(dn[i], "sr")) continue;
        bdev_load(dn[i], &d);
        if (!d.sectors) continue;
        disk_desc(&d, desc, sizeof desc);
        bytes_str((double)d.sectors * 512.0, sz, sizeof sz);
        smart_capable(&d, cap, sizeof cap);
        smart_summary(d.name, &s);
        if (smart_cache(d.name, path, sizeof path)) {   /* srs-root leaves a "skipped:" placeholder for USB disks */
            char first[64] = "";
            rd_str(path, first, sizeof first);
            cached += strncmp(first, "skipped", 7) != 0;
        }
        tm[0] = 0;
        if (disk_hwmon(d.name, hw, sizeof hw)) { char q[480]; long v; snprintf(q, sizeof q, "%s/temp1_input", hw); if (rd_long(q, &v)) snprintf(tm, sizeof tm, "%.1fC", (double)v / 1000.0); }
        if (!*tm && *s.temp) snprintf(tm, sizeof tm, "%s", s.temp);
        cells[0] = d.name; cells[1] = desc; cells[2] = d.bus; cells[3] = sz; cells[4] = cap; cells[5] = *tm ? tm : "-";
        cells[6] = *s.health ? s.health : "-"; cells[7] = *s.hours ? s.hours : "-";
        so_row(o, 8, cells, hc);
    }
    dir_free(dn, nd);
    so_par(o);
    if (!cached) so_kv(o, "Health / attributes", C_ORANGE, "Needs root: Menu > Run as root (srs-root)");
    else so_kvf(o, "srs-root cache", C_GRAY, "%d drive%s (smart-<dev>.txt / nvme-<dev>.txt)", cached, cached == 1 ? "" : "s");
    so_note(o, "Temperatures: nvme / drivetemp hwmon. Details: [smart-all] or smart:<dev>.");
    return;
one:
    {
        static const int t1[] = { 260 };
        bdev d;
        char path[1100], desc[400], hw[400], tb[600], *t;
        char p[300];
        snprintf(p, sizeof p, "/sys/block/%s", o->arg);
        if (strchr(o->arg, '/') || !path_exists(p)) { so_err(o, "No such disk"); return; }
        so_tabs(o, t1, 1);
        bdev_load(o->arg, &d);
        disk_desc(&d, desc, sizeof desc);
        so_b(o, C_TEAL, d.name); so_t(o, C_BLACK, "  "); so_t(o, C_BLACK, desc); so_par(o);
        so_kvf(o, "Bus / type", C_ORANGE, "%s  %s  %s", d.bus, drive_type(&d), media_of(&d));
        so_kv(o, "Firmware", C_VALUE, d.rev);
        if (disk_hwmon(d.name, hw, sizeof hw)) { hwmon_temps(hw, tb, sizeof tb); so_kv(o, "Temperatures", C_ORANGE, tb); }
        if (starts(d.name, "nvme")) {
            char ctl[64];
            snprintf(ctl, sizeof ctl, "%s", d.name);
            ctl[strcspn(ctl + 4, "n") + 4] = 0;
            so_kv(o, "Controller state", C_GREEN, (rd_fmt(tb, sizeof tb, "/sys/class/nvme/%s/state", ctl), tb));
        } else {   /* ATA feature sets known to udev (from IDENTIFY at hotplug) */
            static const char *const fs[][2] = { { "ID_ATA_FEATURE_SET_SMART", "SMART" }, { "ID_ATA_FEATURE_SET_SMART_ENABLED", "SMART enabled" },
                                                 { "ID_ATA_FEATURE_SET_SECURITY", "Security" }, { "ID_ATA_FEATURE_SET_SECURITY_FROZEN", "Security frozen" },
                                                 { "ID_ATA_FEATURE_SET_PM_ENABLED", "Power management" }, { "ID_ATA_FEATURE_SET_APM_ENABLED", "APM" },
                                                 { "ID_ATA_FEATURE_SET_PUIS_ENABLED", "Power-up in standby" }, { "ID_ATA_WRITE_CACHE_ENABLED", "Write cache" },
                                                 { "ID_ATA_READ_LOOKAHEAD_ENABLED", "Read look-ahead" }, { "ID_ATA_FEATURE_SET_HPA_ENABLED", "HPA" },
                                                 { "ID_ATA_DOWNLOAD_MICROCODE", "Download microcode" }, { "ID_ATA_SATA_SIGNAL_RATE_GEN2", "SATA 3Gb/s" },
                                                 { "ID_ATA_SATA_SIGNAL_RATE_GEN1", "SATA 1.5Gb/s" } };
            size_t k;
            int any = 0;
            for (k = 0; k < sizeof fs / sizeof *fs; k++) {
                char v[16];
                if (!udev_get(d.majmin, fs[k][0], v, sizeof v)) continue;
                so_t(o, C_LABEL, fs[k][1]); so_tab(o); so_check(o, atoi(v) != 0); so_par(o);
                any = 1;
            }
            if (udev_get(d.majmin, "ID_WWN", tb, sizeof tb)) so_kv(o, "WWN", C_PURPLE, tb);
            if (!any) so_kv(o, "ATA features", C_GRAY, "not reported (no ATA IDENTIFY via udev; USB bridges usually hide it)");
        }
        so_par(o);
        if (smart_cache(d.name, path, sizeof path) && rd_all(path, &t) >= 0) {
            so_kv(o, "Source", C_PURPLE, path);
            so_par(o);
            so_lines(o, C_BLACK, t);
            free(t);
        } else so_kv(o, "SMART data", C_ORANGE, "Needs root: Menu > Run as root (srs-root)");
    }
}

static void s_smart_all(srs_out *o)
{
    char **dn;
    int nd = disks_list(&dn, 1), i;
    if (arg_set(o)) { s_smart(o); dir_free(dn, nd); return; }
    so_siv_header(o, "SMART All Drives");
    for (i = 0; i < nd; i++) {
        char p[300];
        long v = 0;
        if (starts(dn[i], "sr")) continue;
        snprintf(p, sizeof p, "/sys/block/%s/size", dn[i]);
        if (!rd_long(p, &v) || !v) continue;
        /* render the per-drive view without its own header */
        {
            static const int t1[] = { 260 };
            bdev d;
            char path[1100], desc[400], hw[400], tb[600], *t;
            so_tabs(o, t1, 1);
            bdev_load(dn[i], &d);
            disk_desc(&d, desc, sizeof desc);
            so_b(o, C_TEAL, d.name); so_t(o, C_BLACK, "  "); so_t(o, C_BLACK, desc); so_par(o);
            so_kvf(o, "Bus / type", C_ORANGE, "%s  %s  %s", d.bus, drive_type(&d), media_of(&d));
            so_kv(o, "Firmware", C_VALUE, d.rev);
            if (disk_hwmon(d.name, hw, sizeof hw)) { hwmon_temps(hw, tb, sizeof tb); so_kv(o, "Temperatures", C_ORANGE, tb); }
            smart_capable(&d, tb, sizeof tb);
            so_kv(o, "SMART", C_GREEN, tb);
            if (smart_cache(d.name, path, sizeof path) && rd_all(path, &t) >= 0) {
                so_kv(o, "Source", C_PURPLE, path);
                so_lines(o, C_BLACK, t);
                free(t);
            } else so_kv(o, "SMART data", C_ORANGE, "Needs root: Menu > Run as root (srs-root)");
            so_par(o);
        }
    }
    dir_free(dn, nd);
    so_note(o, "Per-drive view with ATA feature flags: smart:<dev>, e.g. smart:sda");
}

static void s_smart_sdm(srs_out *o)
{
    static const int tabs[] = { 80, 380, 470, 560, 650, 740, 860 };
    static const char *const h[] = { "Drive", "Model", "Current", "Lowest", "Highest", "Critical", "Power On", "Cycles" };
    static const int hc[] = { C_TEAL, C_BLACK, C_ORANGE, C_GREEN, C_RED, C_RED, C_VALUE, C_VALUE };
    char **dn;
    int nd = disks_list(&dn, 1), i;
    so_siv_header(o, "SMART Drive Monitoring");
    so_tabs(o, tabs, 7);
    so_row(o, 8, h, hc);
    so_par(o);
    for (i = 0; i < nd; i++) {
        bdev d;
        smartsum s;
        char hw[400], c[4][32], q[480];
        const char *cells[8];
        static const char *const f[] = { "temp1_input", "temp1_lowest", "temp1_highest", "temp1_crit" };
        int k;
        if (starts(dn[i], "sr")) continue;
        bdev_load(dn[i], &d);
        if (!d.sectors) continue;
        smart_summary(d.name, &s);
        for (k = 0; k < 4; k++) snprintf(c[k], sizeof c[k], "-");
        if (disk_hwmon(d.name, hw, sizeof hw))
            for (k = 0; k < 4; k++) {
                long v;
                snprintf(q, sizeof q, "%s/%s", hw, f[k]);
                if (rd_long(q, &v) && v != 0 && v > -273000 && v < 200000) snprintf(c[k], sizeof c[k], "%.1fC", (double)v / 1000.0);
            }
        if (!strcmp(c[0], "-") && *s.temp) snprintf(c[0], sizeof c[0], "%s", s.temp);
        cells[0] = d.name; cells[1] = *d.model ? d.model : "-"; cells[2] = c[0]; cells[3] = c[1]; cells[4] = c[2]; cells[5] = c[3];
        cells[6] = *s.hours ? s.hours : "-"; cells[7] = *s.cycles ? s.cycles : "-";
        so_row(o, 8, cells, hc);
    }
    dir_free(dn, nd);
    so_par(o);
    so_note(o, "Temperatures live from nvme/drivetemp hwmon; power-on hours and cycles from the srs-root SMART cache.");
    if (!so_root_file("smart-sda.txt") && !so_root_file("nvme-nvme0n1.txt") && !so_root_file("smart-nvme0n1.txt"))
        so_note(o, "Needs root: Menu > Run as root (srs-root) for power-on hours / cycles.");
}

/* ======================================================================
 * [spaces] [raid] + absent vendor RAID
 * ====================================================================== */
static int dm_list(srs_out *o)
{
    char **dn;
    int nd = disks_list(&dn, 0), i, cnt = 0;
    for (i = 0; i < nd; i++) {
        char name[128] = "", uuid[160] = "", p[300], **sl, b[512] = "", sz[32];
        int c, k;
        bdev d;
        if (!starts(dn[i], "dm-") && !starts(dn[i], "md")) continue;
        rd_fmt(name, sizeof name, "/sys/block/%s/dm/name", dn[i]);
        rd_fmt(uuid, sizeof uuid, "/sys/block/%s/dm/uuid", dn[i]);
        if (!*uuid) rd_fmt(uuid, sizeof uuid, "/sys/block/%s/md/level", dn[i]);
        snprintf(p, sizeof p, "/sys/block/%s/slaves", dn[i]);
        c = dir_list(p, &sl);
        for (k = 0; k < c; k++) { size_t u = strlen(b); snprintf(b + u, sizeof b - u, "%s%s", k ? " " : "", sl[k]); }
        dir_free(sl, c);
        bdev_load(dn[i], &d);
        bytes_str((double)d.sectors * 512.0, sz, sizeof sz);
        so_t(o, C_TEAL, dn[i]); so_tab(o);
        so_f(o, C_BLACK, "%s  %s  ", *name ? name : "-", sz);
        so_f(o, C_PURPLE, "%s%s  on %s", starts(uuid, "LVM-") ? "LVM " : starts(uuid, "CRYPT-") ? "dm-crypt " : "", uuid, *b ? b : "-");
        so_par(o);
        cnt++;
    }
    dir_free(dn, nd);
    return cnt;
}

static int btrfs_pools(srs_out *o)
{
    char **n;
    int c = dir_list("/sys/fs/btrfs", &n), i, cnt = 0;
    for (i = 0; i < c; i++) {
        char p[300], lab[128] = "", t[64];
        if (!strcmp(n[i], "features") || strlen(n[i]) < 32) continue;
        rd_fmt(lab, sizeof lab, "/sys/fs/btrfs/%s/label", n[i]);
        snprintf(t, sizeof t, "btrfs %s", *lab ? lab : "<none>");
        so_b(o, C_VALUE, t); so_par(o);
        snprintf(p, sizeof p, "%s", n[i]);
        btrfs_detail(o, p);
        so_par(o);
        cnt++;
    }
    dir_free(n, c);
    return cnt;
}

static void s_spaces(srs_out *o)
{
    static const int tabs[] = { 200 };
    int nb, nd;
    so_siv_header(o, "Storage Spaces");
    so_tabs(o, tabs, 1);
    so_b(o, C_TEAL, "btrfs filesystems (pooled storage)"); so_par(o); so_par(o);
    nb = btrfs_pools(o);
    if (!nb) so_note(o, "  none mounted");
    so_b(o, C_TEAL, "device-mapper / LVM / md volumes"); so_par(o); so_par(o);
    nd = dm_list(o);
    if (!nd) so_note(o, "  none");
    so_par(o);
    so_kvf(o, "Pools", C_BLACK, "%d btrfs, %d dm/md", nb, nd);
    so_note(o, "Source: /sys/fs/btrfs/<fsid>/, /sys/block/dm-*/dm, /sys/block/md*/md (no root needed for this view).");
}

static void s_raid(srs_out *o)
{
    static const int tabs[] = { 120 };
    char *t;
    int nd;
    so_siv_header(o, "RAID");
    so_tabs(o, tabs, 1);
    so_b(o, C_TEAL, "md software RAID (/proc/mdstat)"); so_par(o); so_par(o);
    if (rd_all("/proc/mdstat", &t) >= 0) { so_lines(o, C_BLACK, t); free(t); }
    else so_note(o, "  md driver not loaded - no md RAID arrays");
    so_par(o);
    so_b(o, C_TEAL, "device-mapper (LVM / dm-raid / dm-crypt)"); so_par(o); so_par(o);
    nd = dm_list(o);
    if (!nd) so_note(o, "  no device-mapper devices");
    so_par(o);
    so_b(o, C_TEAL, "btrfs multi-device profiles"); so_par(o); so_par(o);
    {
        char **n;
        int c = dir_list("/sys/fs/btrfs", &n), i, k;
        for (i = 0; i < c; i++) {
            static const char *const prof[] = { "single", "dup", "raid0", "raid1", "raid1c3", "raid1c4", "raid10", "raid5", "raid6", NULL };
            static const char *const at[] = { "data", "metadata", "system" };
            char lab[128] = "", b[256] = "", p[400], **dv;
            int a, nd2;
            if (strlen(n[i]) < 32) continue;
            rd_fmt(lab, sizeof lab, "/sys/fs/btrfs/%s/label", n[i]);
            for (a = 0; a < 3; a++)
                for (k = 0; prof[k]; k++) {
                    snprintf(p, sizeof p, "/sys/fs/btrfs/%s/allocation/%s/%s", n[i], at[a], prof[k]);
                    if (path_exists(p)) { size_t u = strlen(b); snprintf(b + u, sizeof b - u, "%s%s=%s", a ? "  " : "", at[a], prof[k]); }
                }
            snprintf(p, sizeof p, "/sys/fs/btrfs/%s/devices", n[i]);
            nd2 = dir_list(p, &dv);
            dir_free(dv, nd2);
            so_kvf(o, *lab ? lab : n[i], C_VALUE, "%d device%s  %s", nd2, nd2 == 1 ? "" : "s", b);
        }
        dir_free(n, c);
    }
    so_par(o);
    so_note(o, "SIV's CSMI RAID view needs a Windows CSMI driver; Linux RAID is md / dm / btrfs as shown.");
    so_board_footer(o);
}

/* vendor RAID controllers: look for their PCI (and USB) vendor IDs */
static int pci_scan(srs_out *o, const char *vendor, const char *class_prefix)
{
    char **n;
    int c = dir_list("/sys/bus/pci/devices", &n), i, cnt = 0;
    for (i = 0; i < c; i++) {
        char v[16] = "", d[16] = "", cl[16] = "", p[400], rp[PATH_MAX];
        rd_fmt(v, sizeof v, "/sys/bus/pci/devices/%s/vendor", n[i]);
        rd_fmt(cl, sizeof cl, "/sys/bus/pci/devices/%s/class", n[i]);
        if (strcmp(v, vendor) || (class_prefix && !starts(cl, class_prefix))) continue;
        rd_fmt(d, sizeof d, "/sys/bus/pci/devices/%s/device", n[i]);
        snprintf(p, sizeof p, "/sys/bus/pci/devices/%s/driver", n[i]);
        so_t(o, C_TEAL, n[i]); so_tab(o);
        so_f(o, C_VALUE, "%s:%s class %s driver %s", v + 2, d + 2, cl, realpath(p, rp) ? base_name(rp) : "-");
        so_par(o);
        cnt++;
    }
    dir_free(n, c);
    return cnt;
}

static int usb_scan(srs_out *o, const char *vid)
{
    char **n;
    int c = dir_list("/sys/bus/usb/devices", &n), i, cnt = 0;
    for (i = 0; i < c; i++) {
        char v[16] = "", pd[16] = "", pr[96] = "";
        rd_fmt(v, sizeof v, "/sys/bus/usb/devices/%s/idVendor", n[i]);
        if (strcmp(v, vid)) continue;
        rd_fmt(pd, sizeof pd, "/sys/bus/usb/devices/%s/idProduct", n[i]);
        rd_fmt(pr, sizeof pr, "/sys/bus/usb/devices/%s/product", n[i]);
        so_t(o, C_TEAL, n[i]); so_tab(o); so_f(o, C_VALUE, "USB %s:%s %s", v, pd, pr); so_par(o);
        cnt++;
    }
    dir_free(n, c);
    return cnt;
}

static void vendor_raid(srs_out *o, const char *title, const char *vname, const char *pci_vid, const char *cls, const char *usb_vid, const char *linux_tool)
{
    static const int tabs[] = { 150 };
    int n = 0;
    so_siv_header(o, title);
    so_tabs(o, tabs, 1);
    so_kvf(o, "Checked", C_PURPLE, "PCI vendor %s%s%s%s%s (%s)", pci_vid, cls ? " class " : "", cls ? cls : "", usb_vid ? ", USB vendor " : "", usb_vid ? usb_vid : "", vname);
    so_par(o);
    n += pci_scan(o, pci_vid, cls);
    if (usb_vid) {
        /* a vendor's USB storage bridges (e.g. JMicron USB-NVMe enclosures) are not RAID controllers */
        char u[16];
        int nu;
        snprintf(u, sizeof u, "%s", usb_vid);
        nu = usb_scan(o, u);
        if (nu) so_kvf(o, "USB bridges", C_GRAY, "%d %s USB bridge%s above: storage bridges, RAID only if the bridge firmware is set up for it", nu, vname, nu == 1 ? "" : "s");
    }
    if (!n) so_kv(o, "Status", C_ORANGE, "No RAID controller of this kind present");
    else so_kvf(o, "Status", C_GREEN, "%d PCI controller%s found; Linux manages these with %s", n, n == 1 ? "" : "s", linux_tool);
}

static void s_jmb_raid(srs_out *o) { vendor_raid(o, "JMicron JMB RAID", "JMicron", "0x197b", NULL, "152d", "the ahci driver + md/dm"); }
static void s_jms_raid(srs_out *o) { vendor_raid(o, "JMicron NVMe RAID", "JMicron", "0x197b", NULL, "152d", "uas/usb-storage (bridge RAID is configured in its firmware)"); }
static void s_mega_raid(srs_out *o) { vendor_raid(o, "LSI MegaRAID", "LSI/Broadcom", "0x1000", NULL, NULL, "megaraid_sas + storcli"); }
static void s_rocket_raid(srs_out *o) { vendor_raid(o, "RocketRAID", "HighPoint", "0x1103", NULL, NULL, "the HighPoint rr* driver"); }
static void s_vroc(srs_out *o) { vendor_raid(o, "Intel VROC", "Intel RAID/VMD", "0x8086", "0x0104", NULL, "vmd + mdadm (IMSM)"); }

/* ====================================================================== */
const srs_item sec_storage[] = {
    { "volumes", "windows", "Volumes", B_LINUX, 0, "/proc/self/mountinfo, statvfs, udev ID_FS_*", s_volumes, 0, "storage" },
    { "vol-list", "windows", "Volume list", B_LINUX, 0, "/sys/class/block, udev database, mountinfo", s_vol_list, 0, "storage" },
    { "points", "windows", "Mount points", B_LINUX, 0, "/proc/self/mountinfo", s_points, 0, "storage" },
    { "mapping", "windows", "Disk mapping", B_LINUX, 0, "udev by-id/by-path links, mountinfo", s_mapping, 0, "storage" },
    { "drive-tree", "windows", "Drive tree", B_LINUX, 0, "/sys/class/block (+holders)", s_drive_tree, 0, "storage" },
    { "fat", "windows", "FAT statistics", B_LINUX, 0, "statvfs, udev ID_FS_*", s_fat, 0, "storage" },
    { "ntfs", "windows", "NTFS statistics", B_LINUX, 0, "/proc/fs/ntfs3, /sys/fs/{btrfs,ext4,xfs}", s_ntfs, 0, "storage" },
    { "drives", "windows", "Disk drives", B_LINUX, 0, "/sys/block, /sys/class/nvme, udev", s_drives, 0, "storage" },
    { "hidden", "windows", "Hidden disks", B_LINUX, 0, "/sys/block/*/hidden, /dev/ng*", s_hidden, 0, "storage" },
    { "geometry", "windows", "Drive geometry", B_LINUX, 0, "/sys/block/*/size, queue/logical_block_size", s_geometry, 0, "storage" },
    { "sectors", "windows", "Disk sector sizes", B_LINUX, 0, "/sys/block/*/queue, alignment_offset, partition start", s_sectors, 0, "storage" },
    { "partitions", "windows", "Partitions", B_LINUX, 0, "/sys/class/block/*/{start,size}, udev ID_PART_*", s_partitions, 0, "storage" },
    { "performance", "windows", "Disk performance", B_LINUX, 0, "/proc/diskstats", s_performance, 1000, "storage" },
    { "scsi", "windows", "SCSI devices", B_LINUX, 0, "/sys/class/scsi_device, scsi_host", s_scsi, 0, "storage" },
    { "usb-disks", "windows", "USB disk drives", B_LINUX, 0, "/sys/block -> USB device sysfs", s_usb_disks, 0, "storage" },
    { "dsk-api", "windows", "Disk API routines", B_WINONLY, 0, "PATH tools (smartctl nvme hdparm ...), kernel interfaces", s_dsk_api, 0, "storage" },
    { "smart", "windows", "SMART drives", B_LINUX, 1, "srs-root smart-<dev>.txt / nvme-<dev>.txt; hwmon", s_smart, 0, "storage" },
    { "smart-all", "windows", "SMART all drives", B_LINUX, 1, "srs-root smart-<dev>.txt (smartctl -x)", s_smart_all, 0, "storage" },
    { "smart-sdm", "windows", "SMART drive monitor", B_LINUX, 1, "nvme/drivetemp hwmon + srs-root SMART cache", s_smart_sdm, 2000, "storage" },
    { "stornvme", "windows", "NVMe (stornvme)", B_LINUX, 1, "/sys/class/nvme, nvme-<dev>.txt", s_stornvme, 0, "storage" },
    { "secnvme", "windows", "Samsung NVMe (secnvme)", B_LINUX, 1, "/sys/class/nvme (vendor 144d), nvme-<dev>.txt", s_secnvme, 0, "storage" },
    { "spaces", "windows", "Storage spaces", B_LINUX, 0, "/sys/fs/btrfs, dm/md sysfs", s_spaces, 0, "storage" },
    { "raid", "windows", "CSMI RAID", B_ABSENT, 0, "/proc/mdstat, /sys/block/dm-*/dm, btrfs profiles", s_raid, 0, "storage" },
    { "jmb-raid", "windows", "JMicron JMB RAID", B_ABSENT, 0, "PCI vendor 197b / USB 152d", s_jmb_raid, 0, "storage" },
    { "jms-raid", "windows", "JMicron NVMe RAID", B_ABSENT, 0, "PCI vendor 197b / USB 152d", s_jms_raid, 0, "storage" },
    { "mega-raid", "windows", "LSI MegaRAID", B_ABSENT, 0, "PCI vendor 1000", s_mega_raid, 0, "storage" },
    { "rocket-raid", "windows", "RocketRAID", B_ABSENT, 0, "PCI vendor 1103", s_rocket_raid, 0, "storage" },
    { "vroc", "windows", "Intel VROC", B_ABSENT, 0, "PCI 8086 class 0104 (RAID/VMD)", s_vroc, 0, "storage" },
    { NULL, NULL, NULL, B_LINUX, 0, NULL, NULL, 0, NULL }
};
