/* bench_storage.c - Storage benchmark: sequential and random read / write speed of the drive that
 * holds a folder you choose (see bench.h). Written for SRS.
 *
 * Safe by design:
 *  - works only on its own test file in the chosen folder: created with O_EXCL and unlinked at once,
 *    so it has no name while the test runs and the space comes back as soon as the test ends -
 *    also after a crash, a kill or Stop. Existing files are never opened.
 *  - refuses folders in RAM (tmpfs), on network or FUSE file systems and read-only media; USB drives
 *    only when allowed; needs twice the test size free.
 *  - O_DIRECT (no page cache) where the file system allows it; otherwise buffered I/O with the cache
 *    dropped before reads, and the result says so.
 * Queue depth: Linux native AIO (io_submit), Q requests in flight per thread, T threads. */
#define _GNU_SOURCE
#include "bench.h"
#include "burner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <linux/aio_abi.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <unistd.h>

static const char *volatile g_st_stage = "";
const char *storage_stage(void) { return g_st_stage; }

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ======================================================================
 * where is the folder: mount, file system, drive, transport, temperature
 * ====================================================================== */
static void rd1(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "r");
    out[0] = 0;
    if (!f) return;
    if (fgets(out, (int)n, f)) out[strcspn(out, "\n")] = 0;
    fclose(f);
    while (*out && out[strlen(out) - 1] == ' ') out[strlen(out) - 1] = 0;
}

/* /dev/mapper/x, /dev/dm-3, /dev/nvme0n1p2 -> the whole disk's name in /sys/block ("nvme0n1") */
static int disk_of(const char *dev, char *disk, size_t dn, int depth)
{
    char real[PATH_MAX], sys[PATH_MAX + 64], sreal[PATH_MAX];
    const char *b;
    if (depth > 4 || !realpath(dev, real) || strncmp(real, "/dev/", 5)) return 0;
    b = strrchr(real, '/') + 1;
    snprintf(sys, sizeof sys, "/sys/class/block/%s", b);
    if (!realpath(sys, sreal)) return 0;
    if (!strncmp(b, "dm-", 3) || !strncmp(b, "md", 2)) {   /* device mapper / md RAID: follow the first member */
        glob_t g;
        int ok = 0;
        snprintf(sys, sizeof sys, "/sys/class/block/%s/slaves/*", b);
        if (glob(sys, 0, NULL, &g) == 0 && g.gl_pathc) {
            char d2[PATH_MAX + 8];
            snprintf(d2, sizeof d2, "/dev/%s", strrchr(g.gl_pathv[0], '/') + 1);
            ok = disk_of(d2, disk, dn, depth + 1);
        }
        globfree(&g);
        return ok;
    }
    snprintf(sys, sizeof sys, "%s/partition", sreal);
    if (access(sys, F_OK) == 0) {   /* a partition: the disk is the parent directory */
        char *s = strrchr(sreal, '/');
        if (s) *s = 0;
        b = strrchr(sreal, '/') ? strrchr(sreal, '/') + 1 : sreal;
    }
    snprintf(disk, dn, "%s", b);
    return 1;
}

int storage_probe(const char *dir, storage_where *w)
{
    char real[PATH_MAX], l[4096], best[PATH_MAX] = "";
    FILE *f;
    struct statvfs sv;
    size_t bl = 0;
    memset(w, 0, sizeof *w);
    if (!realpath(dir, real)) { snprintf(w->problem, sizeof w->problem, "the folder does not exist"); return 0; }
    snprintf(w->folder, sizeof w->folder, "%s", real);
    if ((f = fopen("/proc/self/mountinfo", "r"))) {
        while (fgets(l, sizeof l, f)) {   /* id parent maj:min root mountpoint opts ... - fstype source superopts */
            char mp[PATH_MAX], fs[64], src[PATH_MAX], *dash = strstr(l, " - ");
            size_t ml;
            if (!dash || sscanf(l, "%*s %*s %*s %*s %4095s", mp) != 1 || sscanf(dash + 3, "%63s %4095s", fs, src) != 2) continue;
            ml = strlen(mp);
            if (strncmp(real, mp, ml) || (real[ml] && real[ml] != '/' && strcmp(mp, "/"))) continue;
            if (ml >= bl) {
                bl = ml;
                snprintf(best, sizeof best, "%s", mp);
                snprintf(w->fstype, sizeof w->fstype, "%s", fs);
                snprintf(w->source, sizeof w->source, "%s", src);
            }
        }
        fclose(f);
    }
    snprintf(w->mount, sizeof w->mount, "%s", best);
    if (statvfs(real, &sv) == 0) {
        w->free_bytes = (double)sv.f_bavail * sv.f_frsize;
        w->readonly = (sv.f_flag & ST_RDONLY) != 0;
    }
    {
        static const char *const bad[] = { "tmpfs", "ramfs", "devtmpfs", "proc", "sysfs", "overlay", "squashfs", "iso9660", "udf",
                                           "nfs", "nfs4", "cifs", "smb3", "smbfs", "9p", "afs", "ceph", "sshfs", "davfs", NULL };
        int i;
        for (i = 0; bad[i]; i++) if (!strcmp(w->fstype, bad[i])) w->not_a_drive = 1;
        if (!strncmp(w->fstype, "fuse", 4)) w->not_a_drive = 1;
        if (!strcmp(w->fstype, "tmpfs") || !strcmp(w->fstype, "ramfs")) w->in_ram = 1;
    }
    if (disk_of(w->source, w->disk, sizeof w->disk, 0)) {
        char p[PATH_MAX + 128], sreal[PATH_MAX], v[64];
        glob_t g;
        snprintf(p, sizeof p, "/sys/block/%s/device/model", w->disk);
        rd1(p, w->model, sizeof w->model);
        if (!w->model[0]) { snprintf(p, sizeof p, "/sys/block/%s/device/name", w->disk); rd1(p, w->model, sizeof w->model); }
        snprintf(p, sizeof p, "/sys/block/%s", w->disk);
        if (realpath(p, sreal) && strstr(sreal, "/usb")) w->usb = 1;
        snprintf(p, sizeof p, "/sys/block/%s/queue/rotational", w->disk);
        rd1(p, v, sizeof v);
        w->rotational = v[0] == '1';
        snprintf(p, sizeof p, "/sys/block/%s/removable", w->disk);
        rd1(p, v, sizeof v);
        w->removable = v[0] == '1';
        snprintf(w->transport, sizeof w->transport, "%s", w->usb ? "USB" : !strncmp(w->disk, "nvme", 4) ? "NVMe" :
                 !strncmp(w->disk, "mmcblk", 6) ? "SD / eMMC" : w->rotational ? "SATA/SAS hard disk" : "SATA/SAS SSD");
        /* temperature: NVMe has hwmon under the controller; SATA through the drivetemp driver */
        snprintf(p, sizeof p, "/sys/block/%s/device/hwmon*/temp1_input", w->disk);
        if (glob(p, 0, NULL, &g) || !g.gl_pathc) {
            globfree(&g);
            snprintf(p, sizeof p, "/sys/block/%s/device/hwmon/hwmon*/temp1_input", w->disk);
            if (glob(p, 0, NULL, &g)) g.gl_pathc = 0;
        }
        if (g.gl_pathc) snprintf(w->temp_path, sizeof w->temp_path, "%s", g.gl_pathv[0]);
        globfree(&g);
    }
    if (w->not_a_drive) snprintf(w->problem, sizeof w->problem, w->in_ram ? "this folder is in RAM (%s), not on a drive" :
                                 "this folder is on a %s file system (network, FUSE or read-only image), not directly on a drive", w->fstype);
    else if (w->readonly) snprintf(w->problem, sizeof w->problem, "this folder is on a read-only file system");
    else if (access(real, W_OK)) snprintf(w->problem, sizeof w->problem, "you cannot write to this folder");
    return !w->problem[0];
}

double storage_temp_c(const storage_where *w)
{
    char v[32];
    if (!w->temp_path[0]) return 0;
    rd1(w->temp_path, v, sizeof v);
    return atof(v) / 1000.0;
}

/* ======================================================================
 * Linux native AIO (raw system calls: no library needed)
 * ====================================================================== */
static long io_setup_(unsigned n, aio_context_t *c) { return syscall(__NR_io_setup, n, c); }
static long io_destroy_(aio_context_t c) { return syscall(__NR_io_destroy, c); }
static long io_submit_(aio_context_t c, long n, struct iocb **b) { return syscall(__NR_io_submit, c, n, b); }
static long io_getevents_(aio_context_t c, long mn, long mx, struct io_event *e, struct timespec *t)
{
    return syscall(__NR_io_getevents, c, mn, mx, e, t);
}

static uint64_t xs64(uint64_t *s) { *s ^= *s << 13; *s ^= *s >> 7; *s ^= *s << 17; return *s; }

typedef struct {
    int fd, write, random, qd, idx, nthreads;
    size_t bs;
    uint64_t file_bytes;
    double secs;
    volatile int *cancel;
    _Atomic uint64_t *done_bytes;
    _Atomic int *stop;
    double *lat;              /* Q1T1: per-request latency samples (us), may be NULL */
    size_t nlat, maxlat;
    int err;
    pthread_t t;
} sjob;

static void *st_worker(void *arg)
{
    sjob *j = arg;
    aio_context_t ctx = 0;
    struct iocb *cb = calloc((size_t)j->qd, sizeof *cb), **cbp = calloc((size_t)j->qd, sizeof *cbp);
    struct io_event *ev = calloc((size_t)j->qd, sizeof *ev);
    double *t_sub = calloc((size_t)j->qd, sizeof *t_sub);
    unsigned char *busy = calloc((size_t)j->qd, 1);   /* request slot in flight */
    unsigned char *buf = NULL;
    uint64_t seed = 0x9E3779B97F4A7C15ull ^ ((uint64_t)j->idx * 0x100000001B3ull), blocks = j->file_bytes / j->bs;
    uint64_t seqpos = (blocks / (uint64_t)j->nthreads) * (uint64_t)j->idx;   /* threads start at different places */
    int i, inflight = 0;
    if (!cb || !cbp || !ev || !t_sub || !busy || posix_memalign((void **)&buf, 4096, j->bs * (size_t)j->qd) || io_setup_((unsigned)j->qd, &ctx)) {
        j->err = errno ? errno : ENOMEM;
        goto out;
    }
    for (i = 0; i < (int)(j->bs * (size_t)j->qd); i++) buf[i] = (unsigned char)xs64(&seed);   /* incompressible */
    for (i = 0; i < j->qd; i++) {
        cb[i].aio_fildes = (uint32_t)j->fd;
        cb[i].aio_lio_opcode = j->write ? IOCB_CMD_PWRITE : IOCB_CMD_PREAD;
        cb[i].aio_buf = (uint64_t)(uintptr_t)(buf + (size_t)i * j->bs);
        cb[i].aio_nbytes = j->bs;
        cb[i].aio_data = (uint64_t)i;
        cbp[i] = &cb[i];
    }
    for (;;) {
        long n, k;
        int stop = atomic_load(j->stop) || (j->cancel && *j->cancel);
        /* keep qd requests in flight until told to stop */
        for (i = 0; !stop && i < j->qd; i++) {
            if (busy[i]) continue;
            cb[i].aio_offset = (int64_t)((j->random ? xs64(&seed) % blocks : seqpos++ % blocks) * j->bs);
            t_sub[i] = now_s();
            if (io_submit_(ctx, 1, &cbp[i]) != 1) { j->err = errno ? errno : EIO; stop = 1; break; }
            busy[i] = 1;
            inflight++;
        }
        if (!inflight) break;
        n = io_getevents_(ctx, 1, j->qd, ev, NULL);
        if (n < 0) { if (errno == EINTR) continue; j->err = errno; break; }
        for (k = 0; k < n; k++) {
            int slot = (int)ev[k].data;
            if (ev[k].res < 0) j->err = (int)-ev[k].res;
            else atomic_fetch_add(j->done_bytes, (uint64_t)ev[k].res);
            if (j->lat && j->nlat < j->maxlat) j->lat[j->nlat++] = (now_s() - t_sub[slot]) * 1e6;
            busy[slot] = 0;
            inflight--;
        }
        if (j->err) atomic_store(j->stop, 1);
    }
out:
    if (ctx) io_destroy_(ctx);
    free(buf); free(cb); free(cbp); free(ev); free(t_sub); free(busy);
    return NULL;
}

/* ======================================================================
 * one test: T threads x Q requests of bs bytes for secs seconds; MB/s average and peak
 * ====================================================================== */
typedef struct { double mbs, peak, iops, lat_mean, lat_p50, lat_p95, lat_p99, lat_max; int err; } st_res;

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

static void run_test(int fd, uint64_t fbytes, int write, int random, size_t bs, int qd, int nt, double secs, int want_lat,
                     volatile int *cancel, st_res *r)
{
    sjob js[64];
    _Atomic uint64_t done = 0;
    _Atomic int stop = 0;
    double t0, t_end, last_t, peak = 0, el;
    uint64_t last_b = 0, total;
    int i;
    memset(r, 0, sizeof *r);
    if (nt > 64) nt = 64;
    for (i = 0; i < nt; i++) {
        memset(&js[i], 0, sizeof js[i]);
        js[i].fd = fd; js[i].write = write; js[i].random = random; js[i].qd = qd; js[i].idx = i; js[i].nthreads = nt;
        js[i].bs = bs; js[i].file_bytes = fbytes; js[i].secs = secs; js[i].cancel = cancel; js[i].done_bytes = &done; js[i].stop = &stop;
        if (want_lat) { js[i].maxlat = 2000000; js[i].lat = malloc(js[i].maxlat * sizeof(double)); }
    }
    t0 = last_t = now_s();
    for (i = 0; i < nt; i++) pthread_create(&js[i].t, NULL, st_worker, &js[i]);
    for (;;) {   /* sample the progress every 50 ms: peak = best 250 ms window */
        struct timespec d = { 0, 50000000L };
        double t;
        uint64_t b;
        nanosleep(&d, NULL);
        t = now_s();
        b = atomic_load(&done);
        if (t - last_t >= 0.25) {
            double v = (double)(b - last_b) / (t - last_t) / 1e6;
            if (v > peak) peak = v;
            last_t = t;
            last_b = b;
        }
        if (t - t0 >= secs || (cancel && *cancel) || atomic_load(&stop)) break;
    }
    atomic_store(&stop, 1);
    for (i = 0; i < nt; i++) pthread_join(js[i].t, NULL);
    if (write) fdatasync(fd);   /* the writes count only once they are on the drive */
    t_end = now_s();
    el = t_end - t0;
    total = atomic_load(&done);
    r->mbs = el > 0 ? (double)total / el / 1e6 : 0;
    r->peak = peak > r->mbs ? peak : r->mbs;
    r->iops = el > 0 ? (double)total / (double)bs / el : 0;
    for (i = 0; i < nt; i++) if (js[i].err && !r->err) r->err = js[i].err;
    if (want_lat) {
        size_t n = 0, k;
        double *all, sum = 0;
        for (i = 0; i < nt; i++) n += js[i].nlat;
        if (n && (all = malloc(n * sizeof *all))) {
            n = 0;
            for (i = 0; i < nt; i++) { memcpy(all + n, js[i].lat, js[i].nlat * sizeof *all); n += js[i].nlat; }
            qsort(all, n, sizeof *all, cmp_d);
            for (k = 0; k < n; k++) sum += all[k];
            r->lat_mean = sum / (double)n;
            r->lat_p50 = all[n / 2];
            r->lat_p95 = all[(size_t)((double)n * 0.95)];
            r->lat_p99 = all[(size_t)((double)n * 0.99) < n ? (size_t)((double)n * 0.99) : n - 1];
            r->lat_max = all[n - 1];
            free(all);
        }
    }
    for (i = 0; i < nt; i++) free(js[i].lat);
}

/* ======================================================================
 * the benchmark
 * ====================================================================== */
typedef struct { const char *name, *code; size_t bs; int qd, nt, random; } st_def;
/* profiles: Standard = the usual three loads; Light = the same with at most 4 threads */
static const st_def st_std[3] = { { "Sequential 1 MiB", "Q8T1", 1 << 20, 8, 1, 0 }, { "4K random", "Q32T16", 4096, 32, 16, 1 },
                                  { "4K random", "Q1T1", 4096, 1, 1, 1 } };
static const st_def st_light[3] = { { "Sequential 1 MiB", "Q8T1", 1 << 20, 8, 1, 0 }, { "4K random", "Q8T4", 4096, 8, 4, 1 },
                                    { "4K random", "Q1T1", 4096, 1, 1, 1 } };

typedef struct {
    storage_where w;
    const st_def *def;
    st_res r[3][2];           /* [test][read, write] */
    int direct, profile, cancelled, err;
    double secs, size_mb, t_before, t_after;
    char when[32];
} stctx;

static void fmt_mbs(char *b, size_t n, double v) { if (v <= 0) snprintf(b, n, "-"); else snprintf(b, n, v >= 100 ? "%.0f MB/s" : "%.1f MB/s", v); }

static void st_render(srs_out *o, void *c)
{
    static const int tabs[] = { 190, 300, 420, 540, 660 }, tabs2[] = { 210 };
    stctx *x = c;
    int t, k;
    so_siv_header(o, "Storage benchmark (read and write speed of the drive that holds the test folder)");
    so_tabs(o, tabs, 5);
    so_row(o, 6, (const char *const[]){ "Test", "", "Average", "Peak", "IOPS", "" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (t = 0; t < 3; t++)
        for (k = 0; k < 2; k++) {
            char name[64], a[32], p[32], io[32];
            st_res *r = &x->r[t][k];
            snprintf(name, sizeof name, "%s (%s)", x->def[t].name, x->def[t].code);
            fmt_mbs(a, sizeof a, r->mbs);
            fmt_mbs(p, sizeof p, r->peak);
            if (x->def[t].bs == 4096 && r->iops > 0) snprintf(io, sizeof io, "%.0f", r->iops); else snprintf(io, sizeof io, " ");
            so_row(o, 6, (const char *const[]){ k ? "" : name, k ? "Write" : "Read", a, p, io, "" },
                   (const int[]){ C_LABEL, C_TEAL, C_GREEN, C_BLACK, C_BLACK, C_BLACK });
        }
    so_par(o);
    so_tabs(o, tabs2, 1);
    for (k = 0; k < 2; k++) {
        st_res *r = &x->r[2][k];
        if (r->lat_mean > 0)
            so_kvf(o, k ? "Write latency (4K Q1T1)" : "Read latency (4K Q1T1)", C_VALUE,
                   "mean %.0f us, median %.0f us, 95%% under %.0f us, 99%% under %.0f us, slowest %.0f us",
                   r->lat_mean, r->lat_p50, r->lat_p95, r->lat_p99, r->lat_max);
    }
    if (x->cancelled) so_kv(o, "Status", C_ORANGE, "Stopped before the end - the empty rows were not measured");
    if (x->err) so_kvf(o, "Error", C_RED, "%s", strerror(x->err));
    so_kv(o, "Drive", C_VALUE, x->w.model[0] ? x->w.model : x->w.disk[0] ? x->w.disk : "unknown");
    so_kvf(o, "Connection", C_VALUE, "%s%s", x->w.transport[0] ? x->w.transport : "unknown", x->w.removable ? ", removable" : "");
    so_kvf(o, "Folder", C_VALUE, "%s (%s, mounted at %s)", x->w.folder, x->w.fstype, x->w.mount);
    so_kvf(o, "Test file", C_VALUE, "%.0f MB, %s; %.1f s per test, %s profile", x->size_mb,
           x->direct ? "direct I/O (O_DIRECT, no page cache)" : "buffered I/O (this file system refused O_DIRECT; the page cache was dropped before reads)",
           x->secs, x->profile ? "Light" : "Standard");
    if (x->t_before > 0) so_kvf(o, "Drive temperature", C_VALUE, "%.0f °C before, %.0f °C after", x->t_before, x->t_after);
    so_kv(o, "Run", C_GRAY, x->when);
    so_kv(o, "Build", C_GRAY, bench_build_tag());
    so_note(o, "1 MB = 1,000,000 bytes. Q = requests in flight per thread, T = threads. Average = the whole test, peak = the best "
               "quarter second. Drive health (SMART) is on the Storage page.");
    so_board_footer(o);
}

int bench_storage(const storage_cfg *cfg, srs_out *o, volatile int *cancel, char *why, size_t wn)
{
    stctx *x;
    char path[PATH_MAX + 64];
    int fd, t, k, rc = 0;
    uint64_t bytes;
    unsigned char *buf;
    time_t now = time(NULL);
    char params[160], res[256];
    if (why && wn) why[0] = 0;
    if (!(x = calloc(1, sizeof *x))) return -1;
    if (!storage_probe(cfg->dir, &x->w)) { if (why) snprintf(why, wn, "%s", x->w.problem); free(x); return -1; }
    if (x->w.usb && !cfg->allow_usb) {
        if (why) snprintf(why, wn, "the folder is on a USB drive; allow USB drives to test it");
        free(x);
        return -1;
    }
    bytes = (uint64_t)cfg->size_mb * 1000000ull / (1u << 20) * (1u << 20);   /* whole MiB */
    if (bytes < (8u << 20)) bytes = 8u << 20;
    if (x->w.free_bytes < (double)bytes * 2 + 512e6) {
        if (why) snprintf(why, wn, "not enough free space: the test needs %.1f GB free, there are %.1f GB", ((double)bytes * 2 + 512e6) / 1e9, x->w.free_bytes / 1e9);
        free(x);
        return -1;
    }
    x->def = cfg->profile ? st_light : st_std;
    x->profile = cfg->profile;
    x->secs = cfg->secs;
    x->size_mb = (double)bytes / 1e6;
    /* the test file: our own, created new (O_EXCL), unlinked at once so nothing is ever left behind */
    {
        int tries;
        fd = -1;
        for (tries = 0; tries < 20 && fd < 0; tries++) {
            snprintf(path, sizeof path, "%s/.srs-storage-test-%d-%d.tmp", x->w.folder, (int)getpid(), tries);
            fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_DIRECT | O_CLOEXEC, 0600);
            x->direct = fd >= 0;
            if (fd < 0 && errno == EINVAL) { fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600); x->direct = 0; }
            if (fd < 0 && errno != EEXIST) break;
        }
        if (fd < 0) { if (why) snprintf(why, wn, "cannot create the test file: %s", strerror(errno)); free(x); return -1; }
        unlink(path);
    }
    x->t_before = storage_temp_c(&x->w);
    /* fill the file once with incompressible data (reads must not hit unwritten, all-zero extents) */
    g_st_stage = "Creating the test file";
    if (posix_memalign((void **)&buf, 4096, 1u << 20)) { close(fd); free(x); return -1; }
    {
        uint64_t s = 0x243F6A8885A308D3ull, off;
        size_t i;
        for (off = 0; off < bytes && !(cancel && *cancel); off += 1u << 20) {
            for (i = 0; i < (1u << 20) / 8; i++) ((uint64_t *)buf)[i] = xs64(&s);
            if (pwrite(fd, buf, 1u << 20, (off_t)off) != (ssize_t)(1u << 20)) { x->err = errno; break; }
        }
        fdatasync(fd);
    }
    free(buf);
    for (t = 0; t < 3 && !x->err; t++)
        for (k = 0; k < 2 && !x->err; k++) {
            static char st[96];
            if (cancel && *cancel) { x->cancelled = 1; break; }
            snprintf(st, sizeof st, "%s (%s) %s", x->def[t].name, x->def[t].code, k ? "write" : "read");
            g_st_stage = st;
            if (!k && !x->direct) posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
            run_test(fd, bytes, k, x->def[t].random, x->def[t].bs, x->def[t].qd, x->def[t].nt, cfg->secs, x->def[t].qd == 1 && x->def[t].nt == 1,
                     cancel, &x->r[t][k]);
            if (x->r[t][k].err) x->err = x->r[t][k].err;
            if (cancel && *cancel) { x->cancelled = 1; memset(&x->r[t][k], 0, sizeof x->r[t][k]); }
        }
    x->t_after = storage_temp_c(&x->w);
    close(fd);   /* the unlinked file's space is freed here */
    g_st_stage = "";
    strftime(x->when, sizeof x->when, "%Y-%m-%d %H:%M:%S", localtime(&now));
    bench_save_render("storage", st_render, x, o);
    snprintf(params, sizeof params, "drive=%s fs=%s size=%.0fMB seconds=%.1f profile=%s%s", x->w.model[0] ? x->w.model : x->w.disk,
             x->w.fstype, x->size_mb, x->secs, x->profile ? "light" : "standard", x->direct ? "" : " buffered");
    snprintf(res, sizeof res, "seq %.0f/%.0f, 4K-%s %.0f/%.0f, 4K-Q1T1 %.1f/%.1f MB/s (read/write)%s", x->r[0][0].mbs, x->r[0][1].mbs,
             x->def[1].code, x->r[1][0].mbs, x->r[1][1].mbs, x->r[2][0].mbs, x->r[2][1].mbs, x->cancelled ? " STOPPED" : x->err ? " ERROR" : "");
    bench_history_add("storage", params, res);
    rc = x->cancelled ? 1 : x->err ? -2 : 0;
    if (x->err && why) snprintf(why, wn, "I/O error: %s", strerror(x->err));
    free(x);
    return rc;
}

#else   /* not Linux */
const char *storage_stage(void) { return ""; }
int storage_probe(const char *dir, storage_where *w) { (void)dir; memset(w, 0, sizeof *w); snprintf(w->problem, sizeof w->problem, "Linux only"); return 0; }
double storage_temp_c(const storage_where *w) { (void)w; return 0; }
int bench_storage(const storage_cfg *cfg, srs_out *o, volatile int *cancel, char *why, size_t wn)
{
    (void)cfg; (void)o; (void)cancel;
    if (why) snprintf(why, wn, "Linux only");
    return -1;
}
#endif
