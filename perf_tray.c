/* perf_tray.c - live telemetry tray meters, the shared sampler ("hub") and the Task Manager settings
 * (see perf_tray.h). Each meter is its own StatusNotifierItem (perf_sni.c) whose picture is drawn
 * with Cairo at 22, 32 and 64 px from the latest perf_data snapshot.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#include "perf_tray.h"
#include "perf_sni.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <glib/gstdio.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

static int tlog(void) { const char *e = getenv("SRS_TEST_TRAY_LOG"); return e && *e && strcmp(e, "0"); }

/* ======================================================================
 * settings
 * ====================================================================== */
#define GROUP "Task Manager"
static perf_prefs g_p = { PT_SPEED_NORMAL, PT_PAGE_SUMMARY, 0, 0, "cpu,mem,gpu0", "", 0, 1000, 0 };
static int g_p_loaded;
typedef struct { perf_prefs_cb cb; void *user; } listener;
static GArray *g_listen;

static char *ini_path(void) { return g_build_filename(g_get_user_config_dir(), "srs", "SRSDEF.ini", NULL); }

static void prefs_load(void)
{
    GKeyFile *kf = g_key_file_new();
    char *f = ini_path();
    g_p_loaded = 1;
    if (g_key_file_load_from_file(kf, f, G_KEY_FILE_KEEP_COMMENTS, NULL)) {
        static const char *const ik[] = { "Update Speed", "Start Page", "Per Core Units", "Telemetry Icons", "Summon Shortcut", "Tray Position" };
        int *iv[] = { &g_p.speed, &g_p.start_page, &g_p.per_core_units, &g_p.icons_on, &g_p.shortcut_bound, &g_p.tray_first };
        char *s;
        for (unsigned k = 0; k < G_N_ELEMENTS(ik); k++) {
            GError *e = NULL;
            int v = g_key_file_get_integer(kf, GROUP, ik[k], &e);
            if (!e) *iv[k] = v;
            g_clear_error(&e);
        }
        if (g_p.speed < PT_SPEED_LOW || g_p.speed > PT_SPEED_LOWEST) g_p.speed = PT_SPEED_NORMAL;
        if (g_p.start_page < 0 || g_p.start_page >= PT_PAGE_COUNT) g_p.start_page = PT_PAGE_SUMMARY;
        g_p.per_core_units = !!g_p.per_core_units;
        g_p.icons_on = !!g_p.icons_on;
        g_p.shortcut_bound = !!g_p.shortcut_bound;
        g_p.tray_first = !!g_p.tray_first;
        if ((s = g_key_file_get_string(kf, GROUP, "Telemetry Meters", NULL))) { snprintf(g_p.meters, sizeof g_p.meters, "%s", s); g_free(s); }
        if ((s = g_key_file_get_string(kf, GROUP, "Disk Space Mount", NULL))) { snprintf(g_p.disk_mount, sizeof g_p.disk_mount, "%s", s); g_free(s); }
        {
            GError *e = NULL;
            int v = g_key_file_get_integer(kf, "GPU Top", "Update ms", &e);
            if (!e) g_p.gpu_top_ms = perf_speed_choice_ms(perf_speed_choice_of_ms(v));   /* one of the choices */
            g_clear_error(&e);
        }
    }
    g_key_file_free(kf);
    g_free(f);
}

/* load the file again first, so SRS's own [SRSDEF] keys (and anything else) are kept as they are on disk */
static void prefs_save(void)
{
    GKeyFile *kf = g_key_file_new();
    char *f = ini_path(), *dir = g_path_get_dirname(f);
    g_mkdir_with_parents(dir, 0755);
    g_key_file_load_from_file(kf, f, G_KEY_FILE_KEEP_COMMENTS, NULL);
    g_key_file_set_integer(kf, GROUP, "Update Speed", g_p.speed);
    g_key_file_set_integer(kf, GROUP, "Start Page", g_p.start_page);
    g_key_file_set_integer(kf, GROUP, "Per Core Units", g_p.per_core_units);
    g_key_file_set_integer(kf, GROUP, "Telemetry Icons", g_p.icons_on);
    g_key_file_set_string(kf, GROUP, "Telemetry Meters", g_p.meters);
    g_key_file_set_string(kf, GROUP, "Disk Space Mount", g_p.disk_mount);
    g_key_file_set_integer(kf, GROUP, "Summon Shortcut", g_p.shortcut_bound);
    g_key_file_set_integer(kf, GROUP, "Tray Position", g_p.tray_first);
    g_key_file_set_integer(kf, "GPU Top", "Update ms", g_p.gpu_top_ms);
    g_key_file_save_to_file(kf, f, NULL);
    g_key_file_free(kf);
    g_free(dir);
    g_free(f);
}

const perf_prefs *perf_prefs_get(void) { if (!g_p_loaded) prefs_load(); return &g_p; }
int perf_prefs_interval_ms(void)
{
    const perf_prefs *p = perf_prefs_get();
    return p->speed == PT_SPEED_LOW ? 2000 : p->speed == PT_SPEED_HIGH ? 500 : p->speed == PT_SPEED_HIGHEST ? 250
         : p->speed == PT_SPEED_LOWEST ? 5000 : 1000;
}

const char *perf_page_name(int page)
{
    static const char *const n[PT_PAGE_COUNT] = { "Summary", "Performance", "Processes", "Startup Apps", "Users", "Services" };
    return page >= 0 && page < PT_PAGE_COUNT ? n[page] : "";
}

double perf_prefs_cpu_value(double whole_pct, int ncpu)
{
    return perf_prefs_get()->per_core_units && ncpu > 0 ? whole_pct * ncpu : whole_pct;
}

void perf_prefs_listen(perf_prefs_cb cb, void *user)
{
    listener l = { cb, user };
    if (!g_listen) g_listen = g_array_new(FALSE, FALSE, sizeof(listener));
    g_array_append_val(g_listen, l);
}

void perf_prefs_unlisten(perf_prefs_cb cb, void *user)
{
    for (guint i = 0; g_listen && i < g_listen->len; i++) {
        listener *l = &g_array_index(g_listen, listener, i);
        if (l->cb == cb && l->user == user) { g_array_remove_index(g_listen, i); return; }
    }
}

static void hub_interval_changed(void);
static void tray_prefs_changed(void);

static void prefs_changed(void)
{
    prefs_save();
    hub_interval_changed();
    tray_prefs_changed();
    for (guint i = 0; g_listen && i < g_listen->len; i++) {
        listener l = g_array_index(g_listen, listener, i);
        l.cb(l.user);
    }
}

/* ======================================================================
 * disk space (statvfs on local block-device file systems; the hub thread polls it every 10 samples)
 * ====================================================================== */
typedef struct { char mnt[256]; long long used, total; } fs_use;

static int local_fs(const char *dev, const char *type)
{
    static const char *const ok[] = { "ext2", "ext3", "ext4", "btrfs", "xfs", "f2fs", "vfat", "exfat", "ntfs", "ntfs3",
                                       "fuseblk", "zfs", "bcachefs", "jfs", "reiserfs", "nilfs2", NULL };
    if (strncmp(dev, "/dev/", 5) && strcmp(type, "zfs")) return 0;
    for (int i = 0; ok[i]; i++) if (!strcmp(type, ok[i])) return 1;
    return 0;
}

/* mount points of local file systems, one per device (the first mount of each), "/" first */
static GPtrArray *mount_list(void)
{
    GPtrArray *mp = g_ptr_array_new_with_free_func(g_free), *devs = g_ptr_array_new_with_free_func(g_free);
    char *txt = NULL, **lines;
    if (!g_file_get_contents("/proc/self/mounts", &txt, NULL, NULL)) { g_ptr_array_free(devs, TRUE); return mp; }
    lines = g_strsplit(txt, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char dev[256], mnt[256], type[64], opts[512];
        int seen = 0;
        if (sscanf(lines[i], "%255s %255s %63s %511s", dev, mnt, type, opts) != 4 || !local_fs(dev, type)) continue;
        if (!strncmp(opts, "ro", 2) && (opts[2] == ',' || !opts[2])) continue;   /* read-only: never fills up */
        for (guint k = 0; k < devs->len; k++) if (!strcmp(g_ptr_array_index(devs, k), dev)) seen = 1;
        if (seen) {   /* a btrfs subvolume or a bind mount of a device already listed; prefer "/" */
            if (!strcmp(mnt, "/")) for (guint k = 0; k < devs->len; k++)
                if (!strcmp(g_ptr_array_index(devs, k), dev)) { g_free(mp->pdata[k]); mp->pdata[k] = g_strdup("/"); }
            continue;
        }
        /* /proc/self/mounts escapes spaces as \040 */
        g_ptr_array_add(devs, g_strdup(dev));
        g_ptr_array_add(mp, g_strcompress(mnt));
    }
    for (guint k = 1; k < mp->len; k++)
        if (!strcmp(g_ptr_array_index(mp, k), "/")) { gpointer t = mp->pdata[0]; mp->pdata[0] = mp->pdata[k]; mp->pdata[k] = t; }
    g_strfreev(lines);
    g_free(txt);
    g_ptr_array_free(devs, TRUE);
    return mp;
}

static int fs_stat(const char *mnt, fs_use *u)
{
    struct statvfs v;
    if (statvfs(mnt, &v) || !v.f_blocks) return -1;
    snprintf(u->mnt, sizeof u->mnt, "%s", mnt);
    u->total = (long long)v.f_blocks * (long long)v.f_frsize;
    u->used = u->total - (long long)v.f_bfree * (long long)v.f_frsize;
    return 0;
}

/* want = "" -> the fullest file system of at least 2 GiB (so a small EFI partition never wins), not counting
   removable media under /run/media or /media */
static int fs_pick(const char *want, fs_use *out)
{
    GPtrArray *mp;
    double best = -1;
    int found = -1;
    if (want && *want) return fs_stat(want, out);
    mp = mount_list();
    for (guint i = 0; i < mp->len; i++) {
        fs_use u;
        const char *m = g_ptr_array_index(mp, i);
        /* removable media (desktop automounts) are often full by design: only when chosen by name */
        if (g_str_has_prefix(m, "/run/media/") || g_str_has_prefix(m, "/media/")) continue;
        if (fs_stat(m, &u) || u.total < (2LL << 30)) continue;
        if ((double)u.used / u.total > best) { best = (double)u.used / u.total; *out = u; found = 0; }
    }
    g_ptr_array_free(mp, TRUE);
    return found;
}

/* ======================================================================
 * the shared sampler
 * ====================================================================== */
typedef struct { guint id; perf_hub_cb cb; void *user; int procs; } hub_sub;
typedef struct { int stop; } hub_ctx;
static struct {
    GMutex mu;
    GCond cv;
    hub_ctx *ctx;                  /* the running thread's, NULL = none */
    int interval;
    pd_snap *pub, *cur;            /* pub: written by the thread; cur: the GUI thread's copy */
    int fresh, have;
    guint idle;
    GArray *subs;
    guint next_id;
    int procs;                     /* 1 = a subscriber needs the process list (else pd_sample skips the walk) */
    char want_mnt[256];
    fs_use space;                  /* written by the thread under mu */
    int space_ok;
    fs_use gspace;                 /* the GUI thread's copy (taken with each sample) */
    int gspace_ok;
    guint test_timer;
} H;

static int test_feed(void) { const char *e = getenv("SRS_TEST_TRAY_FEED"); return e && !strcmp(e, "sine"); }

static void hub_dispatch_cur(void)
{
    /* subscribers may unsubscribe inside their callback: walk a copy */
    GArray *c = g_array_copy(H.subs);
    for (guint i = 0; i < c->len; i++) {
        hub_sub *s = &g_array_index(c, hub_sub, i);
        int still = 0;
        for (guint k = 0; k < H.subs->len; k++) if (g_array_index(H.subs, hub_sub, k).id == s->id) still = 1;
        if (still) s->cb(H.cur, s->user);
    }
    g_array_unref(c);
}

static gboolean hub_dispatch(gpointer d)
{
    (void)d;
    g_mutex_lock(&H.mu);
    H.idle = 0;
    if (H.fresh && H.pub) {
        pd_snap *t = H.cur;   /* swap: the thread copies into the older one next time */
        H.cur = H.pub;
        H.pub = t;
        H.fresh = 0;
        H.have = 1;
    }
    H.gspace = H.space;
    H.gspace_ok = H.space_ok;
    g_mutex_unlock(&H.mu);
    if (H.have && H.subs && H.subs->len) hub_dispatch_cur();
    return G_SOURCE_REMOVE;
}

static gpointer hub_thread(gpointer d)
{
    hub_ctx *ctx = d;
    pd_sampler *s = pd_new(1);
    int tick = 0;
    char last_want[256] = "";
    g_mutex_lock(&H.mu);
    while (!ctx->stop) {
        int ms = H.interval, procs = H.procs;
        char want[256];
        const pd_snap *sn;
        fs_use u;
        int fs_ok = -1;
        snprintf(want, sizeof want, "%s", H.want_mnt);
        g_mutex_unlock(&H.mu);
        if (s) pd_set_procs(s, procs);
        sn = s ? pd_sample(s, 0) : NULL;
        if (tick++ % 10 == 0 || strcmp(want, last_want)) { fs_ok = fs_pick(want, &u); snprintf(last_want, sizeof last_want, "%s", want); }
        g_mutex_lock(&H.mu);
        if (ctx->stop) break;
        if (fs_ok == 0) { H.space = u; H.space_ok = 1; }
        if (sn) {
            pd_snap_copy(H.pub, sn);   /* reuses H.pub's process buffer */
            H.fresh = 1;
            if (!H.idle) H.idle = g_idle_add(hub_dispatch, NULL);
        }
        {
            gint64 end = g_get_monotonic_time() + (gint64)ms * 1000;
            while (!ctx->stop && H.interval == ms && g_cond_wait_until(&H.cv, &H.mu, end)) {}
        }
    }
    g_mutex_unlock(&H.mu);
    pd_free(s);
    g_free(ctx);
    return NULL;
}

/* tests: a synthetic machine (16 CPUs, 1 GPU), so the icons can be checked without /proc or a GPU */
static gboolean hub_test_tick(gpointer d)
{
    static double t;
    pd_snap *s = H.cur;
    (void)d;
    t += 1;
    s->seq++;
    s->dt = H.interval / 1000.0;
    s->ncpu = 16; s->cores = 8; s->sockets = 1;
    snprintf(s->cpu_model, sizeof s->cpu_model, "Test CPU 16 threads");
    s->cpu = 0;
    for (int i = 0; i < s->ncpu; i++) {
        s->cu[i] = (float)(40 + 38 * sin(t / 3.0 + i * 0.7));
        s->ck[i] = (float)(5 + 4 * sin(t / 5.0 + i));
        pd_ring_push(&s->hu[i], s->cu[i]);
        pd_ring_push(&s->hk[i], s->ck[i]);
        s->cpu += (s->cu[i] + s->ck[i]) / s->ncpu;
    }
    s->cpu_user = s->cpu * 0.85; s->cpu_kernel = s->cpu * 0.15;
    s->mhz = 4200 + 600 * sin(t / 4); s->mhz_peak = 5400;
    s->cpu_temp = 55 + 25 * sin(t / 6);
    snprintf(s->cpu_temp_src, sizeof s->cpu_temp_src, "Tctl");
    pd_ring_push(&s->h_cpu, s->cpu);
    pd_ring_push(&s->h_ctemp, s->cpu_temp);
    s->mem_total = 32LL << 30;
    s->mem_used = (long long)((0.45 + 0.2 * sin(t / 7)) * s->mem_total);
    s->mem_avail = s->mem_total - s->mem_used;
    s->mem_cached = 6LL << 30;
    s->swap_total = 8LL << 30; s->swap_used = 256LL << 20;
    pd_ring_push(&s->h_mem, 100.0 * s->mem_used / s->mem_total);
    s->ndisk = 1;
    snprintf(s->disk[0].name, sizeof s->disk[0].name, "nvme0n1");
    s->disk[0].busy = 50 + 50 * sin(t / 2);
    s->disk[0].rd = 80e6 * (1 + sin(t / 2)); s->disk[0].wr = 20e6 * (1 + cos(t / 3));
    s->disk_busy = s->disk[0].busy; s->disk_rd = s->disk[0].rd; s->disk_wr = s->disk[0].wr;
    s->nnet = 1;
    snprintf(s->net[0].name, sizeof s->net[0].name, "enp5s0");
    snprintf(s->net[0].kind, sizeof s->net[0].kind, "Ethernet");
    s->net[0].up = 1;
    s->net_rx = s->net[0].rx = 5e6 * (1 + sin(t / 2.5));
    s->net_tx = s->net[0].tx = 1e6 * (1 + cos(t / 1.5));
    s->ngpu = 1;
    {
        pd_gpu *g = &s->gpu[0];
        snprintf(g->dev.name, sizeof g->dev.name, "Test GPU");
        snprintf(g->dev.pci, sizeof g->dev.pci, "0000:03:00.0");
        g->open = 1; g->have = 1;
        g->busy = (int)(50 + 49 * sin(t / 2.2));
        g->vram_total = 16LL << 30;
        g->vram_used = (long long)((0.3 + 0.25 * sin(t / 9)) * g->vram_total);
        g->gtt_total = 16LL << 30; g->gtt_used = 300LL << 20;
        g->edge = (int)(50 + 30 * sin(t / 5)); g->junction = g->edge + 15; g->memtemp = g->edge + 8;
        g->power = 80 + 200 * (g->busy / 100.0); g->power_cap = 340;
        g->sclk = 2900; g->mclk = 2518;
        snprintf(g->pstate, sizeof g->pstate, "D0");
        pd_ring_push(&g->h_busy, g->busy);
        pd_ring_push(&g->h_temp, g->edge);
        s->gpu_busy = g->busy; s->gpu_temp = g->edge;
    }
    if (fs_pick(H.want_mnt, &H.gspace) == 0) H.gspace_ok = 1;
    H.have = 1;
    hub_dispatch_cur();
    return G_SOURCE_CONTINUE;
}

static void hub_start(void)
{
    if (!H.pub) {
        H.pub = g_new0(pd_snap, 1);
        H.cur = g_new0(pd_snap, 1);
    }
    H.interval = perf_prefs_interval_ms();
    snprintf(H.want_mnt, sizeof H.want_mnt, "%s", perf_prefs_get()->disk_mount);
    if (test_feed()) {
        if (!H.test_timer) H.test_timer = g_timeout_add(H.interval, hub_test_tick, NULL);
        return;
    }
    if (H.ctx) return;
    H.ctx = g_new0(hub_ctx, 1);
    g_thread_unref(g_thread_new("srs-perf-hub", hub_thread, H.ctx));
}

static void hub_stop(void)
{
    if (H.test_timer) { g_source_remove(H.test_timer); H.test_timer = 0; }
    g_mutex_lock(&H.mu);
    if (H.ctx) { H.ctx->stop = 1; H.ctx = NULL; g_cond_broadcast(&H.cv); }   /* the thread frees its ctx */
    g_mutex_unlock(&H.mu);
}

static void hub_procs_update(void)
{
    int need = 0;
    for (guint i = 0; H.subs && i < H.subs->len; i++) need |= g_array_index(H.subs, hub_sub, i).procs;
    g_mutex_lock(&H.mu);
    H.procs = need;
    g_mutex_unlock(&H.mu);
}

static void hub_interval_changed(void)
{
    g_mutex_lock(&H.mu);
    H.interval = perf_prefs_interval_ms();
    snprintf(H.want_mnt, sizeof H.want_mnt, "%s", perf_prefs_get()->disk_mount);
    g_cond_broadcast(&H.cv);
    g_mutex_unlock(&H.mu);
    if (H.test_timer) { g_source_remove(H.test_timer); H.test_timer = g_timeout_add(H.interval, hub_test_tick, NULL); }
}

guint perf_hub_subscribe(perf_hub_cb cb, void *user, int need_procs)
{
    hub_sub s = { ++H.next_id, cb, user, need_procs };
    if (!H.subs) H.subs = g_array_new(FALSE, FALSE, sizeof(hub_sub));
    g_array_append_val(H.subs, s);
    hub_procs_update();
    if (H.subs->len == 1) hub_start();
    else if (H.have && H.cur) cb(H.cur, user);   /* a late subscriber gets the last sample at once */
    return s.id;
}

void perf_hub_unsubscribe(guint id)
{
    for (guint i = 0; H.subs && i < H.subs->len; i++)
        if (g_array_index(H.subs, hub_sub, i).id == id) {
            g_array_remove_index(H.subs, i);
            hub_procs_update();
            if (!H.subs->len) hub_stop();
            return;
        }
}

const pd_snap *perf_hub_last(void) { return H.have ? H.cur : NULL; }

/* ======================================================================
 * meters
 * ====================================================================== */
enum { MK_CPU, MK_CORES, MK_CPUHIST, MK_CPUTEMP, MK_MEM, MK_GPU, MK_GPUHIST, MK_GPUTEMP, MK_VRAM, MK_DISK, MK_SPACE, MK_NET, MK_N };
static const struct { const char *id, *label; int per_gpu; } g_kind[MK_N] = {
    { "cpu", "CPU Meter", 0 }, { "cores", "CPU Cores", 0 }, { "cpuhist", "CPU History", 0 }, { "cputemp", "CPU Temp", 0 },
    { "mem", "Memory Used", 0 }, { "gpu", "Meter", 1 }, { "gpuhist", "History", 1 }, { "gputemp", "Temp", 1 },
    { "vram", "VRAM Used", 1 }, { "disk", "Disk Activity", 0 }, { "space", "Disk Space", 0 }, { "net", "Network", 0 } };

#define NSIZES 3
static const int g_sizes[NSIZES] = { 22, 32, 64 };

typedef struct {
    char id[16];
    int kind, gpu;
    psni *sni;
    cairo_surface_t *surf[NSIZES];
    char sig[128];                 /* what the picture showed last; same -> not redrawn */
    int temp, temp_age;            /* GPU Temp: the last reading, held a few samples when one comes back empty */
    int lost;                      /* was registered, then the tray went away */
} meter;

static struct {
    perf_tray_hooks hk;
    GPtrArray *meters;             /* meter* in menu order, only the chosen ones */
    guint sub;
    int ngpu;                      /* GPUs seen (the last snapshot, or the device list before it) */
    float rx[PD_HIST], tx[PD_HIST];
    int nrt;
    unsigned long long last_seq;
    int started;
    int watcher;                   /* the desktop's tray (StatusNotifierWatcher) was on the bus when the icons were made */
    int first;                     /* the Tray Position the icons were made with */
} T;

static int gpu_count(void)
{
    const pd_snap *s = perf_hub_last();
    if (s) return s->ngpu;
    if (!T.ngpu) {
        if (test_feed()) T.ngpu = 1;
        else {
            gt_device *l = g_new0(gt_device, GT_MAX_DEV);
            int n = gt_device_list(l, GT_MAX_DEV);
            g_free(l);
            T.ngpu = n > PD_MAX_GPU ? PD_MAX_GPU : n < 0 ? 0 : n;
        }
    }
    return T.ngpu;
}

int perf_tray_meter_count(void) { return 8 + 4 * gpu_count(); }

/* menu order: CPU Meter, CPU Cores, CPU History, CPU Temp, Memory Used, per GPU (Meter, History, Temp, VRAM), Disk Activity, Disk Space, Network */
static void meter_at(int i, int *kind, int *gpu)
{
    int ng = gpu_count();
    *gpu = 0;
    if (i < 5) { *kind = i; return; }
    if (i < 5 + 4 * ng) { *kind = MK_GPU + (i - 5) % 4; *gpu = (i - 5) / 4; return; }
    *kind = MK_DISK + (i - 5 - 4 * ng);
}

static void kind_id(int kind, int gpu, char *buf, size_t len)
{
    if (g_kind[kind].per_gpu) snprintf(buf, len, "%s%d", g_kind[kind].id, gpu);
    else snprintf(buf, len, "%s", g_kind[kind].id);
}

static void kind_label(int kind, int gpu, char *buf, size_t len)
{
    if (kind == MK_VRAM) { if (gpu_count() > 1) snprintf(buf, len, "GPU %d VRAM Used", gpu + 1); else snprintf(buf, len, "VRAM Used"); }
    else if (g_kind[kind].per_gpu) {
        if (gpu_count() > 1) snprintf(buf, len, "GPU %d %s", gpu + 1, g_kind[kind].label);
        else snprintf(buf, len, "GPU %s", g_kind[kind].label);
    } else snprintf(buf, len, "%s", g_kind[kind].label);
}

const char *perf_tray_meter_id(int i, char *buf, size_t len) { int k, g; meter_at(i, &k, &g); kind_id(k, g, buf, len); return buf; }
const char *perf_tray_meter_label(int i, char *buf, size_t len) { int k, g; meter_at(i, &k, &g); kind_label(k, g, buf, len); return buf; }

static int chosen(const char *id)
{
    char **v = g_strsplit(perf_prefs_get()->meters, ",", -1);
    int on = 0;
    for (int i = 0; v[i]; i++) if (!strcmp(g_strstrip(v[i]), id)) on = 1;
    g_strfreev(v);
    return on;
}

static void set_chosen(const char *id, int on)
{
    GString *s = g_string_new(NULL);
    int n = perf_tray_meter_count();
    for (int i = 0; i < n; i++) {   /* rebuilt in menu order; ids of GPUs not present now are kept */
        char mid[16];
        perf_tray_meter_id(i, mid, sizeof mid);
        if (strcmp(mid, id) ? chosen(mid) : on) g_string_append_printf(s, "%s%s", s->len ? "," : "", mid);
    }
    {
        char **v = g_strsplit(g_p.meters, ",", -1);
        for (int i = 0; v[i]; i++) {
            int known = 0;
            g_strstrip(v[i]);
            for (int k = 0; k < n && !known; k++) { char mid[16]; if (!strcmp(perf_tray_meter_id(k, mid, sizeof mid), v[i])) known = 1; }
            if (!known && *v[i] && strcmp(v[i], id)) g_string_append_printf(s, "%s%s", s->len ? "," : "", v[i]);
        }
        g_strfreev(v);
    }
    snprintf(g_p.meters, sizeof g_p.meters, "%s", s->str);
    g_string_free(s, TRUE);
}

/* ---------------------------------------------------------------- drawing */
typedef struct { double r, g, b; } rgb;
static const rgb C_CPU = { 0.29, 0.64, 1.00 }, C_MEM = { 0.72, 0.50, 1.00 }, C_GPU = { 0.36, 0.84, 0.42 },
                 C_VRAM = { 0.20, 0.78, 0.70 }, C_DISK = { 1.00, 0.72, 0.30 }, C_SPACE = { 0.55, 0.74, 0.86 },
                 C_RX = { 0.30, 0.80, 1.00 }, C_TX = { 1.00, 0.55, 0.25 };

static rgb heat(double c)
{
    rgb g = { 0.45, 0.90, 0.45 }, y = { 1.00, 0.85, 0.25 }, r = { 1.00, 0.35, 0.30 };
    return c < 60 ? g : c < 80 ? y : r;
}

/* the plate every meter sits on: dark and nearly opaque, with a light rim, so it reads on light and dark panels */
static void plate(cairo_t *cr, int sz)
{
    double r = sz * 0.16, m = sz >= 32 ? 1.0 : 0.5, w = sz - 2 * m;
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_new_sub_path(cr);
    cairo_arc(cr, m + w - r, m + r, r, -M_PI / 2, 0);
    cairo_arc(cr, m + w - r, m + w - r, r, 0, M_PI / 2);
    cairo_arc(cr, m + r, m + w - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, m + r, m + r, r, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0.10, 0.11, 0.13, 0.92);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, sz >= 32 ? 1.5 : 1.0);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.28);
    cairo_stroke(cr);
}

/* the inner drawing box (inside the rim) */
static void inner(int sz, double *x, double *y, double *w, double *h)
{
    double p = sz >= 64 ? 5 : sz >= 32 ? 3 : 2;
    *x = p; *y = p; *w = sz - 2 * p; *h = sz - 2 * p;
}

/* a number over the meter: white with a dark outline; fits the width */
static void number(cairo_t *cr, int sz, const char *txt, rgb col, double frac_h, double room_right)
{
    cairo_text_extents_t te;
    double fs = sz * frac_h, ix, iy, iw, ih;
    inner(sz, &ix, &iy, &iw, &ih);
    iw -= room_right;
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, fs);
    cairo_text_extents(cr, txt, &te);
    if (te.width > iw + 1) { fs *= (iw + 1) / te.width; cairo_set_font_size(cr, fs); cairo_text_extents(cr, txt, &te); }
    cairo_move_to(cr, (sz - room_right) / 2.0 - te.width / 2 - te.x_bearing, sz / 2.0 - te.height / 2 - te.y_bearing);
    cairo_text_path(cr, txt);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_width(cr, sz >= 32 ? 3.0 : 2.0);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.85);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgb(cr, col.r, col.g, col.b);
    cairo_fill(cr);
}

/* a level meter: the plate fills from the bottom, with the percentage on top */
static void draw_bar(cairo_t *cr, int sz, double pct, rgb c)
{
    double x, y, w, h, f;
    char t[8];
    plate(cr, sz);
    inner(sz, &x, &y, &w, &h);
    f = pct < 0 ? 0 : pct > 100 ? 1 : pct / 100.0;
    cairo_rectangle(cr, x, y, w, h);
    cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.18);
    cairo_fill(cr);
    cairo_rectangle(cr, x, y + h * (1 - f), w, h * f);
    cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.85);
    cairo_fill(cr);
    if (pct < 0) snprintf(t, sizeof t, "-");
    else snprintf(t, sizeof t, "%d", (int)lround(pct > 999 ? 999 : pct));
    number(cr, sz, t, (rgb){ 1, 1, 1 }, 0.52, 0);
}

/* a history: area under the last samples, newest at the right */
static void draw_hist(cairo_t *cr, int sz, const pd_ring *r, rgb c, double scale_max)
{
    double x, y, w, h;
    int n, cols, first;
    plate(cr, sz);
    inner(sz, &x, &y, &w, &h);
    cairo_rectangle(cr, x, y, w, h);
    cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.12);
    cairo_fill(cr);
    n = r ? r->n : 0;
    cols = (int)w;                 /* one sample per pixel column */
    if (cols > PD_HIST) cols = PD_HIST;
    first = n > cols ? n - cols : 0;
    if (n - first < 2) return;
    cairo_move_to(cr, x + w, y + h);
    for (int i = n - 1, k = 0; i >= first; i--, k++) {
        double v = pd_ring_at(r, i), px = x + w - k * (w / (cols - 1 > 0 ? cols - 1 : 1)) ;
        if (isnan(v)) v = 0;
        v = v / scale_max;
        if (v > 1) v = 1;
        if (v < 0) v = 0;
        cairo_line_to(cr, px, y + h - v * h);
    }
    cairo_line_to(cr, x + w - (n - 1 - first) * (w / (cols - 1 > 0 ? cols - 1 : 1)), y + h);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.55);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, fmin(1, c.r + 0.25), fmin(1, c.g + 0.25), fmin(1, c.b + 0.25), 1);
    cairo_set_line_width(cr, sz >= 32 ? 1.5 : 1.0);
    cairo_stroke(cr);
}

/* tiny per-CPU bars; with more CPUs than pixel columns neighbours are averaged */
static void draw_cores(cairo_t *cr, int sz, const pd_snap *s)
{
    double x, y, w, h;
    int n = s->ncpu > PD_MAX_CPU ? PD_MAX_CPU : s->ncpu, cols, gap;
    plate(cr, sz);
    inner(sz, &x, &y, &w, &h);
    if (n <= 0) return;
    cols = n;
    while (cols > (int)w) cols = (cols + 1) / 2;
    gap = w / cols >= 3 ? 1 : 0;
    for (int c = 0; c < cols; c++) {
        int a = c * n / cols, b = (c + 1) * n / cols;
        double v = 0, bx = x + c * w / cols, bw = w / cols - gap;
        for (int i = a; i < b; i++) v += s->cu[i] + s->ck[i];
        v = b > a ? v / (b - a) / 100.0 : 0;
        if (v > 1) v = 1;
        if (v < 0) v = 0;
        cairo_rectangle(cr, bx, y, bw > 0.5 ? bw : 0.5, h);
        cairo_set_source_rgba(cr, C_CPU.r, C_CPU.g, C_CPU.b, 0.16);
        cairo_fill(cr);
        cairo_rectangle(cr, bx, y + h * (1 - v), bw > 0.5 ? bw : 0.5, h * v);
        cairo_set_source_rgba(cr, C_CPU.r, C_CPU.g, C_CPU.b, 0.95);
        cairo_fill(cr);
    }
}

static void draw_temp(cairo_t *cr, int sz, double c)
{
    char t[8];
    plate(cr, sz);
    if (isnan(c) || c < -50) snprintf(t, sizeof t, "-");
    else snprintf(t, sizeof t, "%d", (int)lround(c));
    number(cr, sz, t, isnan(c) ? (rgb){ 0.8, 0.8, 0.8 } : heat(c), 0.56, sz * 0.16);   /* room for the degree ring */
    {   /* a small degree ring top right, in the same colour */
        rgb h = isnan(c) ? (rgb){ 0.8, 0.8, 0.8 } : heat(c);
        double r = sz * 0.07;
        cairo_new_sub_path(cr);
        cairo_arc(cr, sz - sz * 0.18, sz * 0.24, r, 0, 2 * M_PI);
        cairo_set_line_width(cr, sz >= 32 ? 1.6 : 1.1);
        cairo_set_source_rgb(cr, h.r, h.g, h.b);
        cairo_stroke(cr);
    }
}

/* receive (filled) and send (line) over the last samples, scaled to the larger of the two */
static void draw_net(cairo_t *cr, int sz)
{
    double x, y, w, h, mx = 64 * 1024;
    int cols, first;
    plate(cr, sz);
    inner(sz, &x, &y, &w, &h);
    for (int i = 0; i < T.nrt; i++) { mx = fmax(mx, T.rx[i]); mx = fmax(mx, T.tx[i]); }
    cols = (int)w < PD_HIST ? (int)w : PD_HIST;
    first = T.nrt > cols ? T.nrt - cols : 0;
    if (T.nrt - first < 2) return;
    for (int pass = 0; pass < 2; pass++) {
        const float *v = pass ? T.tx : T.rx;
        rgb c = pass ? C_TX : C_RX;
        double step = w / (cols - 1 > 0 ? cols - 1 : 1);
        cairo_new_path(cr);
        for (int i = T.nrt - 1, k = 0; i >= first; i--, k++) {
            double px = x + w - k * step, py = y + h - fmin(1, v[i] / mx) * h;
            if (!k) cairo_move_to(cr, px, py); else cairo_line_to(cr, px, py);
        }
        if (!pass) {
            cairo_line_to(cr, x + w - (T.nrt - 1 - first) * step, y + h);
            cairo_line_to(cr, x + w, y + h);
            cairo_close_path(cr);
            cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.55);
            cairo_fill(cr);
        } else {
            cairo_set_source_rgb(cr, c.r, c.g, c.b);
            cairo_set_line_width(cr, sz >= 32 ? 1.8 : 1.2);
            cairo_stroke(cr);
        }
    }
}

static double pct(long long a, long long b) { return a >= 0 && b > 0 ? 100.0 * a / b : -1; }

/* the GPU's temperature for the meter: edge, else junction; a sample without one (the card dozing between
   reads) keeps the last value for up to 5 samples instead of blinking to '-'. -999 = none */
static int gpu_temp(meter *m, const pd_gpu *g, unsigned long long seq)
{
    int t = !g || !g->open || !g->have ? -999 : g->edge >= 0 ? g->edge : g->junction >= 0 ? g->junction : -999;
    if (t != -999) { m->temp = t; m->temp_age = 0; return t; }
    if (m->temp_age < 5 && m->temp > -999 && g && g->open) { m->temp_age += (seq != 0); return m->temp; }
    m->temp_age = 99;
    return -999;
}

/* one meter's picture at one size */
static void draw(meter *m, const pd_snap *s, cairo_t *cr, int sz)
{
    const pd_gpu *g = m->gpu < s->ngpu ? &s->gpu[m->gpu] : NULL;
    switch (m->kind) {
    case MK_CPU: draw_bar(cr, sz, s->cpu, C_CPU); break;
    case MK_CORES: draw_cores(cr, sz, s); break;
    case MK_CPUHIST: draw_hist(cr, sz, &s->h_cpu, C_CPU, 100); break;
    case MK_CPUTEMP: draw_temp(cr, sz, s->cpu_temp); break;
    case MK_MEM: draw_bar(cr, sz, pct(s->mem_used, s->mem_total), C_MEM); break;
    case MK_GPU: draw_bar(cr, sz, g && g->open && g->have ? g->busy : -1, C_GPU); break;
    case MK_GPUHIST: draw_hist(cr, sz, g ? &g->h_busy : NULL, C_GPU, 100); break;
    case MK_GPUTEMP: draw_temp(cr, sz, m->temp_age < 6 && m->temp > -999 ? m->temp : NAN); break;
    case MK_VRAM: draw_bar(cr, sz, g && g->have ? pct(g->vram_used, g->vram_total) : -1, C_VRAM); break;
    case MK_DISK: draw_bar(cr, sz, s->ndisk ? s->disk_busy : -1, C_DISK); break;
    case MK_SPACE: draw_bar(cr, sz, H.gspace_ok ? pct(H.gspace.used, H.gspace.total) : -1, C_SPACE); break;
    case MK_NET: draw_net(cr, sz); break;
    }
}

/* what the picture depends on, rounded the way it is drawn (same signature = same pixels, so no redraw) */
static void signature(meter *m, const pd_snap *s, char *out, size_t len)
{
    const pd_gpu *g = m->gpu < s->ngpu ? &s->gpu[m->gpu] : NULL;
    switch (m->kind) {
    case MK_CPU: snprintf(out, len, "%ld", lround(s->cpu)); break;
    case MK_CPUTEMP: snprintf(out, len, "%ld", isnan(s->cpu_temp) ? -999L : lround(s->cpu_temp)); break;
    case MK_MEM: snprintf(out, len, "%ld", lround(pct(s->mem_used, s->mem_total) * 10)); break;
    case MK_GPU: snprintf(out, len, "%d", g && g->open && g->have ? g->busy : -1); break;
    case MK_GPUTEMP: snprintf(out, len, "%d", gpu_temp(m, g, s->seq)); break;
    case MK_VRAM: snprintf(out, len, "%ld", g && g->have ? lround(pct(g->vram_used, g->vram_total) * 10) : -1L); break;
    case MK_DISK: snprintf(out, len, "%ld", s->ndisk ? lround(s->disk_busy) : -1L); break;
    case MK_SPACE: snprintf(out, len, "%ld", H.space_ok ? lround(pct(H.space.used, H.space.total) * 10) : -1L); break;
    default: snprintf(out, len, "%llu", s->seq); break;   /* the moving ones change every sample */
    }
}

/* ---------------------------------------------------------------- tooltips */
static void fmt_b(double b, char *o, int n) { pd_fmt_bytes(b, o, n); }

static void tooltip(const meter *m, const pd_snap *s, char *title, size_t tl, GString *b)
{
    const pd_gpu *g = m->gpu < s->ngpu ? &s->gpu[m->gpu] : NULL;
    char lab[48], x1[32], x2[32], x3[32];
    int pc = perf_prefs_get()->per_core_units;
    double whole = pc ? s->ncpu * 100.0 : 100.0;
    kind_label(m->kind, m->gpu, lab, sizeof lab);
    switch (m->kind) {
    case MK_CPU: case MK_CPUHIST: case MK_CORES:
        snprintf(title, tl, "%s: %.0f %%", lab, perf_prefs_cpu_value(s->cpu, s->ncpu));
        g_string_append_printf(b, "Total %.1f %% of %.0f %% (User %.1f %%, Kernel %.1f %%)\n", perf_prefs_cpu_value(s->cpu, s->ncpu), whole,
                               perf_prefs_cpu_value(s->cpu_user, s->ncpu), perf_prefs_cpu_value(s->cpu_kernel, s->ncpu));
        if (s->mhz > 0) { pd_fmt_mhz(s->mhz, x1, sizeof x1); g_string_append_printf(b, "%d Logical CPUs, %s Average\n", s->ncpu, x1); }
        if (m->kind == MK_CORES)
            for (int i = 0; i < s->ncpu && i < PD_MAX_CPU; i++)
                g_string_append_printf(b, "CPU %d %.0f %%%s", i, s->cu[i] + s->ck[i], i % 4 == 3 || i == s->ncpu - 1 ? "\n" : ",  ");
        if (m->kind == MK_CPUHIST && s->h_cpu.n)
            g_string_append_printf(b, "Last %d Samples: Peak %.0f %%\n", s->h_cpu.n, perf_prefs_cpu_value(pd_ring_max(&s->h_cpu), s->ncpu));
        if (*s->cpu_model) g_string_append(b, s->cpu_model);
        break;
    case MK_CPUTEMP:
        if (isnan(s->cpu_temp)) { snprintf(title, tl, "%s: Not Available", lab); g_string_append(b, "No CPU temperature sensor was found."); }
        else {
            snprintf(title, tl, "%s: %.1f °C", lab, s->cpu_temp);
            if (*s->cpu_temp_src) g_string_append_printf(b, "Sensor: %s", s->cpu_temp_src);
            if (s->h_ctemp.n) g_string_append_printf(b, "\nLast %d Samples: Peak %.1f °C", s->h_ctemp.n, pd_ring_max(&s->h_ctemp));
        }
        break;
    case MK_MEM:
        fmt_b((double)s->mem_used, x1, sizeof x1); fmt_b((double)s->mem_total, x2, sizeof x2);
        snprintf(title, tl, "%s: %.0f %%", lab, pct(s->mem_used, s->mem_total));
        g_string_append_printf(b, "In Use %s of %s (%.1f %%)\n", x1, x2, pct(s->mem_used, s->mem_total));
        fmt_b((double)s->mem_avail, x1, sizeof x1); fmt_b((double)s->mem_cached, x2, sizeof x2);
        g_string_append_printf(b, "Available %s, Cached %s", x1, x2);
        if (s->swap_total > 0) {
            fmt_b((double)s->swap_used, x1, sizeof x1); fmt_b((double)s->swap_total, x2, sizeof x2);
            g_string_append_printf(b, "\nSwap %s of %s", x1, x2);
        }
        break;
    case MK_GPU: case MK_GPUHIST: case MK_GPUTEMP: case MK_VRAM:
        if (!g) { snprintf(title, tl, "%s: Not Available", lab); g_string_append(b, "This graphics card is not present now."); break; }
        if (!g->open || !g->have) {
            snprintf(title, tl, "%s: %s", lab, g->open ? "Reading..." : "Asleep");
            g_string_append_printf(b, "%s\n%s", g->dev.name, *g->why ? g->why : "The card is powered down; the meter does not wake it.");
            break;
        }
        if (m->kind == MK_GPUTEMP) {
            if (m->temp > -999 && m->temp_age < 6) snprintf(title, tl, "%s: %d °C", lab, m->temp);
            else snprintf(title, tl, "%s: Not Available", lab);
        }
        else if (m->kind == MK_VRAM) snprintf(title, tl, "%s: %.0f %%", lab, pct(g->vram_used, g->vram_total));
        else snprintf(title, tl, "%s: %d %%", lab, g->busy);
        g_string_append_printf(b, "%s (%s)\n", g->dev.name, g->dev.pci);
        if (g->busy >= 0) g_string_append_printf(b, "Busy %d %%\n", g->busy);
        if (g->vram_total > 0) {
            fmt_b((double)g->vram_used, x1, sizeof x1); fmt_b((double)g->vram_total, x2, sizeof x2);
            g_string_append_printf(b, "VRAM %s of %s (%.1f %%)\n", x1, x2, pct(g->vram_used, g->vram_total));
        }
        if (m->kind == MK_VRAM && g->gtt_total > 0) {
            fmt_b((double)g->gtt_used, x1, sizeof x1); fmt_b((double)g->gtt_total, x2, sizeof x2);
            g_string_append_printf(b, "GTT %s of %s\n", x1, x2);
        }
        if (g->edge >= 0) {
            g_string_append_printf(b, "Edge %d °C", g->edge);
            if (g->junction >= 0) g_string_append_printf(b, ", Junction %d °C", g->junction);
            if (g->memtemp >= 0) g_string_append_printf(b, ", Memory %d °C", g->memtemp);
            g_string_append_c(b, '\n');
        }
        if (g->power >= 0) {
            g_string_append_printf(b, "Power %.0f W", g->power);
            if (g->power_cap > 0) g_string_append_printf(b, " of %d W", g->power_cap);
            g_string_append_c(b, '\n');
        }
        if (g->sclk > 0) g_string_append_printf(b, "GPU Clock %d MHz, Memory Clock %d MHz", g->sclk, g->mclk);
        if (m->kind == MK_GPUHIST && g->h_busy.n) g_string_append_printf(b, "\nLast %d Samples: Peak %.0f %%", g->h_busy.n, pd_ring_max(&g->h_busy));
        break;
    case MK_DISK:
        snprintf(title, tl, "%s: %.0f %%", lab, s->disk_busy);
        pd_fmt_rate(s->disk_rd, x1, sizeof x1); pd_fmt_rate(s->disk_wr, x2, sizeof x2);
        g_string_append_printf(b, "Busiest Disk %.0f %%; Read %s, Write %s", s->disk_busy, x1, x2);
        for (int i = 0; i < s->ndisk && i < PD_MAX_DISK; i++) {
            pd_fmt_rate(s->disk[i].rd, x1, sizeof x1); pd_fmt_rate(s->disk[i].wr, x2, sizeof x2);
            g_string_append_printf(b, "\n%s: %.0f %%, Read %s, Write %s", s->disk[i].name, s->disk[i].busy, x1, x2);
        }
        break;
    case MK_SPACE:
        if (!H.gspace_ok) { snprintf(title, tl, "%s: Not Available", lab); g_string_append(b, "No local file system could be read."); break; }
        fmt_b((double)H.gspace.used, x1, sizeof x1); fmt_b((double)H.gspace.total, x2, sizeof x2);
        fmt_b((double)(H.gspace.total - H.gspace.used), x3, sizeof x3);
        snprintf(title, tl, "%s: %s %.0f %%", lab, H.gspace.mnt, pct(H.gspace.used, H.gspace.total));
        g_string_append_printf(b, "%s: %s Used of %s (%.1f %%), %s Free\n%s", H.gspace.mnt, x1, x2, pct(H.gspace.used, H.gspace.total), x3,
                               *perf_prefs_get()->disk_mount ? "The mount chosen in Settings." : "The fullest local file system.");
        break;
    case MK_NET:
        pd_fmt_bits(s->net_rx, x1, sizeof x1); pd_fmt_bits(s->net_tx, x2, sizeof x2);
        snprintf(title, tl, "%s: Receive %s, Send %s", lab, x1, x2);
        g_string_append(b, "Receive (Filled) and Send (Line), Last Minute");
        for (int i = 0; i < s->nnet && i < PD_MAX_NET; i++) {
            if (!s->net[i].up) continue;
            pd_fmt_bits(s->net[i].rx, x1, sizeof x1); pd_fmt_bits(s->net[i].tx, x2, sizeof x2);
            g_string_append_printf(b, "\n%s (%s): Receive %s, Send %s", s->net[i].name, s->net[i].kind, x1, x2);
        }
        break;
    }
}

/* ---------------------------------------------------------------- icons */
static void png_dump(const meter *m, int k)
{
    const char *dir = getenv("SRS_TEST_TRAY_PNG_DIR");
    char *f;
    if (!dir || !*dir) return;
    f = g_strdup_printf("%s/%s-%d.png", dir, m->id, g_sizes[k]);
    cairo_surface_write_to_png(m->surf[k], f);
    g_free(f);
}

static void meter_update(meter *m, const pd_snap *s)
{
    char sig[128], title[160];
    GString *body;
    if (!m->sni) return;
    signature(m, s, sig, sizeof sig);
    if (strcmp(sig, m->sig)) {
        snprintf(m->sig, sizeof m->sig, "%s", sig);
        for (int k = 0; k < NSIZES; k++) {
            cairo_t *cr;
            if (!m->surf[k]) m->surf[k] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, g_sizes[k], g_sizes[k]);
            cr = cairo_create(m->surf[k]);
            draw(m, s, cr, g_sizes[k]);
            cairo_destroy(cr);
            png_dump(m, k);
        }
        if (psni_set_icon(m->sni, m->surf, NSIZES) && tlog()) fprintf(stderr, "SRS-TEST: tray %s new icon %s\n", m->id, sig);
    }
    body = g_string_new(NULL);
    title[0] = 0;
    tooltip(m, s, title, sizeof title, body);
    while (body->len && body->str[body->len - 1] == '\n') g_string_truncate(body, body->len - 1);
    psni_set_tooltip(m->sni, title, body->str);
    g_string_free(body, TRUE);
}

static void on_snap(const pd_snap *s, void *user)
{
    (void)user;
    if (s->seq != T.last_seq) {   /* one network history point per sample */
        T.last_seq = s->seq;
        if (T.nrt == PD_HIST) { memmove(T.rx, T.rx + 1, sizeof T.rx[0] * (PD_HIST - 1)); memmove(T.tx, T.tx + 1, sizeof T.tx[0] * (PD_HIST - 1)); T.nrt--; }
        T.rx[T.nrt] = (float)s->net_rx;
        T.tx[T.nrt] = (float)s->net_tx;
        T.nrt++;
    }
    if (s->ngpu != T.ngpu) { T.ngpu = s->ngpu; tray_prefs_changed(); return; }   /* a card appeared or went: rebuild */
    for (guint i = 0; T.meters && i < T.meters->len; i++) meter_update(g_ptr_array_index(T.meters, i), s);
}

/* ---------------------------------------------------------------- menu and clicks */
enum { MI_OPEN = 1, MI_SETTINGS, MI_QUIT, MI_HEADER, MI_CLICK = 90, MI_METER = 100 };   /* MI_CLICK: a left click, not a menu entry */

static void present_token(const char *token)
{
#ifdef GDK_WINDOWING_WAYLAND
    GdkDisplay *d = gdk_display_get_default();
    if (token && *token && d && GDK_IS_WAYLAND_DISPLAY(d)) gdk_wayland_display_set_startup_notification_id(d, token);
#else
    (void)token;
#endif
}

static void open_srs(const char *token)
{
    present_token(token);
    if (T.hk.open_performance) T.hk.open_performance();
    else if (T.hk.show_main) T.hk.show_main();
}

static void build_menus(void);
static void rebuild_icons(void);

typedef struct { int id; char token[256]; } menu_job;

static gboolean menu_idle(gpointer d)
{
    menu_job *j = d;
    if (j->id == MI_CLICK) open_srs(j->token);   /* left click: the Performance tab */
    else if (j->id == MI_OPEN) { present_token(j->token); if (T.hk.show_main) T.hk.show_main(); }   /* the main window */
    else if (j->id == MI_SETTINGS) { present_token(j->token); perf_tray_settings_dialog(NULL); }
    else if (j->id == MI_QUIT) { if (T.hk.quit) T.hk.quit(); else gtk_main_quit(); }
    else if (j->id >= MI_METER && j->id < MI_METER + perf_tray_meter_count()) {
        char mid[16];
        perf_tray_meter_id(j->id - MI_METER, mid, sizeof mid);
        set_chosen(mid, !chosen(mid));
        prefs_changed();
        /* the last meter gone while SRS is hidden: show SRS, or there is no way back */
        if ((!T.meters || !T.meters->len) && T.hk.windows_hidden && T.hk.windows_hidden() && T.hk.show_main) T.hk.show_main();
    }
    g_free(j);
    return G_SOURCE_REMOVE;
}

/* every reaction runs from an idle callback: a menu click may remove the very icon it came from */
static void on_menu(psni *it, int id, const char *token, void *user)
{
    menu_job *j = g_new0(menu_job, 1);
    (void)it; (void)user;
    j->id = id;
    snprintf(j->token, sizeof j->token, "%s", token ? token : "");
    g_idle_add(menu_idle, j);
}

static void on_activate(psni *it, int x, int y, const char *token, void *user)
{
    (void)x; (void)y;
    on_menu(it, MI_CLICK, token, user);
}

static void on_registered(psni *it, int on, void *user)
{
    meter *m = user;
    (void)it;
    /* the tray went away (a plasmashell restart): the icons register again by themselves when it is back */
    m->lost = !on;
    if (on) T.watcher = 1;
}

static void build_menus(void)
{
    int n = perf_tray_meter_count(), k = 0;
    psni_menu_item *mi = g_new0(psni_menu_item, n + 8);
    char (*lab)[48] = g_malloc0(sizeof *lab * (size_t)n);
    mi[k++] = (psni_menu_item){ MI_OPEN, "_Open SRS", -1, 1 };
    mi[k++] = (psni_menu_item){ MI_SETTINGS, "_Settings...", -1, 1 };
    mi[k++] = (psni_menu_item){ 50, NULL, -1, 1 };
    mi[k++] = (psni_menu_item){ MI_HEADER, "Notification Area Items", -1, 0 };
    for (int i = 0; i < n; i++) {
        char mid[16];
        perf_tray_meter_label(i, lab[i], sizeof lab[i]);
        mi[k++] = (psni_menu_item){ MI_METER + i, lab[i], chosen(perf_tray_meter_id(i, mid, sizeof mid)), 1 };
    }
    mi[k++] = (psni_menu_item){ 51, NULL, -1, 1 };
    mi[k++] = (psni_menu_item){ MI_QUIT, "_Quit SRS", -1, 1 };
    for (guint i = 0; T.meters && i < T.meters->len; i++) psni_set_menu(((meter *)g_ptr_array_index(T.meters, i))->sni, mi, k);
    g_free(lab);
    g_free(mi);
}

static void meter_free(gpointer p)
{
    meter *m = p;
    psni_free(m->sni);
    for (int k = 0; k < NSIZES; k++) if (m->surf[k]) cairo_surface_destroy(m->surf[k]);
    g_free(m);
}

/* make the set of icons match the settings: keep the ones still chosen (no flicker), add / remove the rest */
static void rebuild_icons(void)
{
    GPtrArray *old = T.meters, *now = g_ptr_array_new_with_free_func(meter_free);
    int on = perf_prefs_get()->icons_on && T.started, n = perf_tray_meter_count();
    static const psni_cbs cbs = { on_activate, on_activate, on_menu, on_registered };
    /* Plasma's tray sorts by Category, then by Title: every meter is "Hardware" (like SRS's own icon) and its Title starts
       "SRS <rank>", rank = its place among the chosen meters in the Settings order, so the icons sit together in that order.
       The Id stays the same whatever the rank ("srs-meter-cpu"): Plasma keeps "Always shown" per Id.
       Tray Position First: Category "Other" + a leading space; a tray reads the Category only when an item registers,
       so a change of position makes every icon anew. */
    if (old && T.first != perf_prefs_get()->tray_first) {
        for (guint k = 0; k < old->len; k++) if (old->pdata[k]) { meter_free(old->pdata[k]); old->pdata[k] = NULL; }
    }
    T.first = perf_prefs_get()->tray_first;
    for (int i = 0; on && i < n; i++) {
        char mid[16], lab[48], sid[32], title[72];
        meter *m = NULL;
        perf_tray_meter_id(i, mid, sizeof mid);
        if (!chosen(mid)) continue;
        perf_tray_meter_label(i, lab, sizeof lab);
        snprintf(title, sizeof title, "%s %02u %s", perf_tray_title_prefix(), now->len + 1, lab);
        for (guint k = 0; old && k < old->len; k++) {
            meter *o = g_ptr_array_index(old, k);
            if (o && !strcmp(o->id, mid)) { m = o; old->pdata[k] = NULL; psni_set_title(m->sni, title); break; }
        }
        if (!m) {
            m = g_new0(meter, 1);
            snprintf(m->id, sizeof m->id, "%s", mid);
            meter_at(i, &m->kind, &m->gpu);
            m->temp = -999;
            m->temp_age = 99;
            snprintf(sid, sizeof sid, "srs-meter-%s", mid);
            m->sni = psni_new(sid, title, &cbs, m);
            if (!m->sni) { g_free(m); continue; }
            psni_set_category(m->sni, perf_tray_category());
        }
        g_ptr_array_add(now, m);
    }
    if (old) {
        for (guint k = 0; k < old->len; k++) if (old->pdata[k]) meter_free(old->pdata[k]);
        g_ptr_array_set_free_func(old, NULL);
        g_ptr_array_free(old, TRUE);
    }
    T.meters = now;
    if (now->len) {   /* registering is asynchronous: remember whether a tray exists, so SRS may hide at once (--gui-tray) */
        GDBusConnection *b = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
        GVariant *r = b ? g_dbus_connection_call_sync(b, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                                      "NameHasOwner", g_variant_new("(s)", "org.kde.StatusNotifierWatcher"),
                                                      G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL) : NULL;
        T.watcher = 0;
        if (r) { g_variant_get(r, "(b)", &T.watcher); g_variant_unref(r); }
        if (b) g_object_unref(b);
    }
    build_menus();
    if (now->len && !T.sub) T.sub = perf_hub_subscribe(on_snap, NULL, 0);
    else if (!now->len && T.sub) { perf_hub_unsubscribe(T.sub); T.sub = 0; }
    else if (now->len && perf_hub_last()) on_snap(perf_hub_last(), NULL);   /* new icons get a picture now */
    if (tlog()) fprintf(stderr, "SRS-TEST: tray meters %u (%s)\n", now->len, perf_prefs_get()->icons_on ? perf_prefs_get()->meters : "off");
}

static void tray_prefs_changed(void) { if (T.started) rebuild_icons(); }

void perf_tray_set_hooks(const perf_tray_hooks *h) { if (h) T.hk = *h; }

void perf_tray_init(void)
{
    T.started = 1;
    rebuild_icons();
}

/* registered now, or (just after start) icons waiting for a tray that is on the bus */
int perf_tray_active(void)
{
    int any = 0;
    for (guint i = 0; T.meters && i < T.meters->len; i++) {
        meter *m = g_ptr_array_index(T.meters, i);
        if (psni_registered(m->sni)) return 1;
        if (!m->lost) any = 1;
    }
    return any && T.watcher;
}

void perf_tray_shutdown(void)
{
    T.started = 0;
    rebuild_icons();
}

/* ======================================================================
 * summon shortcut: org.freedesktop.portal.GlobalShortcuts
 * ====================================================================== */
#define PORTAL "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define GS_IFACE "org.freedesktop.portal.GlobalShortcuts"
#define SHORTCUT_ID "summon-srs"

static struct {
    GDBusConnection *bus;
    char *session;
    guint act_sub;
    int binding;                   /* 1 = the user pressed Bind (ask the desktop); 0 = restore only */
    GtkWidget *status, *button;    /* the settings page's, when open */
    char trigger[128];
} S;

static void shortcut_ui(void);

typedef void (*resp_fn)(guint32 code, GVariant *res);
typedef struct { guint sub; resp_fn fn; } resp_wait;

static void on_response(GDBusConnection *c, const char *sender, const char *path, const char *iface, const char *sig,
                        GVariant *p, gpointer d)
{
    resp_wait *w = d;
    guint32 code = 2;
    GVariant *res = NULL;
    (void)sender; (void)path; (void)iface; (void)sig;
    g_variant_get(p, "(u@a{sv})", &code, &res);
    g_dbus_connection_signal_unsubscribe(c, w->sub);
    w->fn(code, res);
    g_variant_unref(res);
    g_free(w);
}

/* call a portal method whose answer comes as a Request::Response signal; options get our handle_token */
static void portal_request(const char *method, GVariant *args, resp_fn fn)
{
    static int n;
    char token[32], *sender, *path;
    resp_wait *w = g_new0(resp_wait, 1);
    snprintf(token, sizeof token, "srs%d_%d", (int)getpid(), ++n);
    sender = g_strdup(g_dbus_connection_get_unique_name(S.bus) + 1);
    for (char *p = sender; *p; p++) if (*p == '.') *p = '_';
    path = g_strdup_printf("/org/freedesktop/portal/desktop/request/%s/%s", sender, token);
    w->fn = fn;
    w->sub = g_dbus_connection_signal_subscribe(S.bus, PORTAL, "org.freedesktop.portal.Request", "Response", path, NULL,
                                                G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, on_response, w, NULL);
    /* the caller's tuple ends with an a{sv}; rebuild it with handle_token added */
    {
        gsize nc = g_variant_n_children(args);
        GVariant **kids = g_new0(GVariant *, nc);
        GVariantDict opts;
        GVariant *last;
        g_variant_ref_sink(args);
        for (gsize i = 0; i + 1 < nc; i++) kids[i] = g_variant_get_child_value(args, i);
        last = g_variant_get_child_value(args, nc - 1);
        g_variant_dict_init(&opts, last);
        g_variant_unref(last);
        g_variant_dict_insert(&opts, "handle_token", "s", token);
        kids[nc - 1] = g_variant_ref_sink(g_variant_dict_end(&opts));
        g_dbus_connection_call(S.bus, PORTAL, PORTAL_PATH, GS_IFACE, method, g_variant_new_tuple(kids, nc), NULL,
                               G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
        for (gsize i = 0; i < nc; i++) g_variant_unref(kids[i]);
        g_free(kids);
        g_variant_unref(args);
    }
    g_free(sender);
    g_free(path);
}

static void on_activated(GDBusConnection *c, const char *sender, const char *path, const char *iface, const char *sig,
                         GVariant *p, gpointer d)
{
    const char *session = NULL, *id = NULL;
    GVariant *opts = NULL;
    const char *tok = "";
    (void)c; (void)sender; (void)path; (void)iface; (void)sig; (void)d;
    g_variant_get(p, "(&o&st@a{sv})", &session, &id, NULL, &opts);
    g_variant_lookup(opts, "activation_token", "&s", &tok);
    if (S.session && !strcmp(session, S.session) && !strcmp(id, SHORTCUT_ID)) open_srs(tok);
    g_variant_unref(opts);
}

static void read_trigger(GVariant *res)
{
    GVariant *list = g_variant_lookup_value(res, "shortcuts", G_VARIANT_TYPE("a(sa{sv})"));
    S.trigger[0] = 0;
    if (!list) return;
    for (gsize i = 0; i < g_variant_n_children(list); i++) {
        const char *id = NULL, *t = NULL;
        GVariant *props = NULL;
        g_variant_get_child(list, i, "(&s@a{sv})", &id, &props);
        if (!strcmp(id, SHORTCUT_ID) && g_variant_lookup(props, "trigger_description", "&s", &t)) snprintf(S.trigger, sizeof S.trigger, "%s", t);
        else if (!strcmp(id, SHORTCUT_ID)) snprintf(S.trigger, sizeof S.trigger, "(Set in the Desktop's Settings)");
        g_variant_unref(props);
    }
    g_variant_unref(list);
}

static void bound(guint32 code, GVariant *res)
{
    if (code == 0 && res) {
        read_trigger(res);
        g_p.shortcut_bound = S.trigger[0] != 0;
    } else if (S.binding) {
        g_p.shortcut_bound = 0;   /* cancelled */
    }
    S.binding = 0;
    prefs_save();
    shortcut_ui();
}

static void listed(guint32 code, GVariant *res)
{
    if (code == 0) read_trigger(res);
    if (!S.trigger[0] && S.binding) {   /* not known to the desktop yet: ask it (it shows its own dialog) */
        GVariantBuilder sc;
        GVariantDict p;
        g_variant_builder_init(&sc, G_VARIANT_TYPE("a(sa{sv})"));
        g_variant_dict_init(&p, NULL);
        g_variant_dict_insert(&p, "description", "s", "Show SRS (Task Manager)");
        g_variant_dict_insert(&p, "preferred_trigger", "s", "CTRL+SHIFT+Escape");
        g_variant_builder_add(&sc, "(s@a{sv})", SHORTCUT_ID, g_variant_dict_end(&p));
        portal_request("BindShortcuts", g_variant_new("(o@a(sa{sv})s@a{sv})", S.session, g_variant_builder_end(&sc), "",
                                                      g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0)), bound);
        return;
    }
    bound(S.trigger[0] ? 0 : 1, res);
}

static void session_made(guint32 code, GVariant *res)
{
    const char *h = NULL;
    if (code != 0 || !g_variant_lookup(res, "session_handle", "&s", &h)) {
        if (S.binding) g_p.shortcut_bound = 0;
        S.binding = 0;
        snprintf(S.trigger, sizeof S.trigger, "%s", "");
        shortcut_ui();
        return;
    }
    g_free(S.session);
    S.session = g_strdup(h);
    if (!S.act_sub)
        S.act_sub = g_dbus_connection_signal_subscribe(S.bus, PORTAL, GS_IFACE, "Activated", PORTAL_PATH, NULL,
                                                       G_DBUS_SIGNAL_FLAGS_NONE, on_activated, NULL, NULL);
    portal_request("ListShortcuts", g_variant_new("(o@a{sv})", S.session, g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0)), listed);
}

static int shortcut_start(int ask)
{
    GVariantDict o;
    char tok[32];
    if (!S.bus) S.bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (!S.bus) return -1;
    S.binding = ask;
    if (S.session) { listed(1, NULL); return 0; }
    snprintf(tok, sizeof tok, "srs_session_%d", (int)getpid());
    g_variant_dict_init(&o, NULL);
    g_variant_dict_insert(&o, "session_handle_token", "s", tok);
    portal_request("CreateSession", g_variant_new("(@a{sv})", g_variant_dict_end(&o)), session_made);
    return 0;
}

static void shortcut_stop(void)
{
    if (S.session && S.bus)
        g_dbus_connection_call(S.bus, PORTAL, S.session, "org.freedesktop.portal.Session", "Close", NULL, NULL,
                               G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    g_clear_pointer(&S.session, g_free);
    S.trigger[0] = 0;
    g_p.shortcut_bound = 0;
    prefs_save();
    shortcut_ui();
}

void perf_tray_shortcut_restore(void)
{
    if (perf_prefs_get()->shortcut_bound && !getenv("SRS_TEST_TRAY_FEED")) shortcut_start(0);
}

/* ======================================================================
 * settings page
 * ====================================================================== */
static GtkWidget *g_dlg;
static struct { GtkWidget *meters_box, *mount, *close_cb, *auto_cb; GPtrArray *checks; } W;

static void shortcut_ui(void)
{
    if (!S.status) return;
    if (S.binding) gtk_label_set_text(GTK_LABEL(S.status), "Waiting for the desktop...");
    else if (g_p.shortcut_bound && S.trigger[0]) {
        char *t = g_strdup_printf("Bound: %s", S.trigger);
        gtk_label_set_text(GTK_LABEL(S.status), t);
        g_free(t);
    } else if (g_p.shortcut_bound) gtk_label_set_text(GTK_LABEL(S.status), "Bound (the desktop has not confirmed it yet)");
    else gtk_label_set_text(GTK_LABEL(S.status), "Not Bound");
    if (S.button) gtk_button_set_label(GTK_BUTTON(S.button), g_p.shortcut_bound ? "Stop Using t_he Shortcut" : "_Bind Global Shortcut...");
}

static void on_status_gone(GtkWidget *w, gpointer d) { (void)w; (void)d; S.status = S.button = NULL; }

static void on_bind(GtkButton *b, gpointer d)
{
    (void)b; (void)d;
    if (g_p.shortcut_bound) { shortcut_stop(); return; }
    if (shortcut_start(1)) { gtk_label_set_text(GTK_LABEL(S.status), "No session bus: the desktop cannot be asked."); return; }
    shortcut_ui();
}

static const int g_choice_ms[PT_SPEED_CHOICES] = { 250, 500, 1000, 2000, 5000 };
static const int g_choice_speed[PT_SPEED_CHOICES] = { PT_SPEED_HIGHEST, PT_SPEED_HIGH, PT_SPEED_NORMAL, PT_SPEED_LOW, PT_SPEED_LOWEST };
int perf_speed_choice_ms(int i) { return i >= 0 && i < PT_SPEED_CHOICES ? g_choice_ms[i] : 1000; }
const char *perf_speed_choice_label(int i)
{
    static const char *const l[PT_SPEED_CHOICES] = { "Every 0.25 s", "Every 0.5 s", "Every 1 s (Normal)", "Every 2 s", "Every 5 s" };
    return i >= 0 && i < PT_SPEED_CHOICES ? l[i] : l[2];
}
int perf_speed_choice_of_ms(int ms)
{
    int i, best = 2;
    for (i = 0; i < PT_SPEED_CHOICES; i++) if (abs(g_choice_ms[i] - ms) < abs(g_choice_ms[best] - ms)) best = i;
    return best;
}
int perf_prefs_gpu_top_ms(void) { return perf_prefs_get()->gpu_top_ms; }

/* Plasma 6 (plasma-workspace applets/systemtray/sortedsystemtraymodel.cpp, SortedSystemTrayModel::lessThanSystemTray):
   Notifications first, then by Category in the order UnknownCategory, ApplicationStatus, Communications, SystemServices,
   Hardware (a category not in that list counts as UnknownCategory), then by the display name (an SNI's Title) with
   QString::localeAwareCompare, where a leading space sorts before letters and digits. */
const char *perf_tray_category(void) { return perf_prefs_get()->tray_first ? "Other" : "Hardware"; }
const char *perf_tray_title_prefix(void) { return perf_prefs_get()->tray_first ? " SRS" : "SRS"; }

void perf_prefs_set_tray_first(int on)
{
    perf_prefs_get();
    if (!!on == g_p.tray_first) return;
    g_p.tray_first = !!on;
    prefs_changed();
}
void perf_prefs_set_gpu_top_ms(int ms)
{
    perf_prefs_get();
    ms = perf_speed_choice_ms(perf_speed_choice_of_ms(ms));
    if (ms == g_p.gpu_top_ms) return;
    g_p.gpu_top_ms = ms;
    prefs_changed();
}
static void on_gt_speed(GtkComboBox *c, gpointer d)
{
    int i = gtk_combo_box_get_active(c);
    (void)d;
    if (i >= 0 && i < PT_SPEED_CHOICES) perf_prefs_set_gpu_top_ms(perf_speed_choice_ms(i));
}
static void on_speed(GtkComboBox *c, gpointer d)
{
    int i = gtk_combo_box_get_active(c);
    (void)d;
    if (i < 0 || i >= PT_SPEED_CHOICES) return;
    g_p.speed = g_choice_speed[i];
    prefs_changed();
}
static void on_page(GtkComboBox *c, gpointer d) { (void)d; g_p.start_page = gtk_combo_box_get_active(c); prefs_changed(); }
static void on_units(GtkToggleButton *t, gpointer d) { (void)d; g_p.per_core_units = gtk_toggle_button_get_active(t); prefs_changed(); }
static void on_icons(GtkToggleButton *t, gpointer d)
{
    (void)d;
    g_p.icons_on = gtk_toggle_button_get_active(t);
    if (W.meters_box) gtk_widget_set_sensitive(W.meters_box, g_p.icons_on);
    prefs_changed();
}
static void on_meter(GtkToggleButton *t, gpointer d)
{
    set_chosen(d, gtk_toggle_button_get_active(t));
    prefs_changed();
}
static void on_mount(GtkComboBox *c, gpointer d)
{
    const char *id = gtk_combo_box_get_active_id(c);
    (void)d;
    snprintf(g_p.disk_mount, sizeof g_p.disk_mount, "%s", id && strcmp(id, "*") ? id : "");
    prefs_changed();
}
static void on_position(GtkComboBox *c, gpointer d) { (void)d; perf_prefs_set_tray_first(gtk_combo_box_get_active(c) == 1); }
static void on_close_tray(GtkToggleButton *t, gpointer d) { (void)d; if (T.hk.set_close_to_tray) T.hk.set_close_to_tray(gtk_toggle_button_get_active(t)); }
static void on_autostart(GtkToggleButton *t, gpointer d) { (void)d; if (T.hk.set_autostart) T.hk.set_autostart(gtk_toggle_button_get_active(t)); }

static GtkWidget *note(const char *text)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 70);
    gtk_label_set_width_chars(GTK_LABEL(l), 56);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), "dim-label");
    return l;
}

static GtkWidget *heading(const char *text)
{
    GtkWidget *l = gtk_label_new(NULL);
    char *m = g_markup_printf_escaped("<b>%s</b>", text);
    gtk_label_set_markup(GTK_LABEL(l), m);
    g_free(m);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_widget_set_margin_top(l, 6);
    return l;
}

static GtkWidget *row_label(const char *mn, GtkWidget *target)
{
    GtkWidget *l = gtk_label_new_with_mnemonic(mn);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), target);
    return l;
}

static void on_page_gone(GtkWidget *w, gpointer d)
{
    (void)w; (void)d;
    if (W.checks) { g_ptr_array_free(W.checks, TRUE); W.checks = NULL; }
    memset(&W, 0, sizeof W);
}

GtkWidget *perf_tray_settings_widget(void)
{
    GtkWidget *g = gtk_grid_new(), *w, *flow;
    int r = 0, n;
    perf_prefs_get();
    gtk_grid_set_row_spacing(GTK_GRID(g), 4);
    gtk_grid_set_column_spacing(GTK_GRID(g), 10);
    gtk_container_set_border_width(GTK_CONTAINER(g), 10);
    if (W.checks) g_ptr_array_free(W.checks, TRUE);
    memset(&W, 0, sizeof W);
    W.checks = g_ptr_array_new_with_free_func(g_free);
    g_signal_connect(g, "destroy", G_CALLBACK(on_page_gone), NULL);

    gtk_grid_attach(GTK_GRID(g), heading("General"), 0, r++, 2, 1);
    w = gtk_combo_box_text_new();
    for (int i = 0; i < PT_SPEED_CHOICES; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w), perf_speed_choice_label(i));
    gtk_combo_box_set_active(GTK_COMBO_BOX(w), perf_speed_choice_of_ms(perf_prefs_interval_ms()));
    gtk_widget_set_tooltip_text(w, "How often the Performance tab and the tray meters read the PC. The Big Top's card tabs (GPU, SMI) "
                                   "have their own Update setting in their top row, with the same choices.");
    g_signal_connect(w, "changed", G_CALLBACK(on_speed), NULL);
    gtk_grid_attach(GTK_GRID(g), row_label("Performance and Tray _Update Speed", w), 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(g), w, 1, r++, 1, 1);
    w = gtk_combo_box_text_new();   /* GPU Top's own rate, here too so every live-update rate is on one page */
    for (int i = 0; i < PT_SPEED_CHOICES; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w), perf_speed_choice_label(i));
    gtk_combo_box_set_active(GTK_COMBO_BOX(w), perf_speed_choice_of_ms(g_p.gpu_top_ms));
    gtk_widget_set_tooltip_text(w, "How often The Big Top window reads the graphics cards for its GPU and SMI tabs (the same as its "
                                   "Update drop-down). 1 s is amdgpu_top's default; the graphs always show the last 30 s.");
    g_signal_connect(w, "changed", G_CALLBACK(on_gt_speed), NULL);
    gtk_grid_attach(GTK_GRID(g), row_label("Big Top Upd_ate Speed", w), 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(g), w, 1, r++, 1, 1);
    w = gtk_combo_box_text_new();
    for (int i = 0; i < PT_PAGE_COUNT; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w), perf_page_name(i));
    gtk_combo_box_set_active(GTK_COMBO_BOX(w), g_p.start_page);
    g_signal_connect(w, "changed", G_CALLBACK(on_page), NULL);
    gtk_grid_attach(GTK_GRID(g), row_label("Default Start Pag_e", w), 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(g), w, 1, r++, 1, 1);
    w = gtk_check_button_new_with_mnemonic("Per-Core CPU Units (100 % = _One Logical CPU)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), g_p.per_core_units);
    g_signal_connect(w, "toggled", G_CALLBACK(on_units), NULL);
    gtk_grid_attach(GTK_GRID(g), w, 0, r++, 2, 1);
    gtk_grid_attach(GTK_GRID(g), note("On: one fully busy logical CPU is 100 %, so all of them together reach "
                                      "the number of logical CPUs x 100 %. Off: the whole CPU is 100 %."), 0, r++, 2, 1);

    gtk_grid_attach(GTK_GRID(g), heading("Notification Area"), 0, r++, 2, 1);
    w = gtk_check_button_new_with_mnemonic("Live _Telemetry Icons");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), g_p.icons_on);
    g_signal_connect(w, "toggled", G_CALLBACK(on_icons), NULL);
    gtk_grid_attach(GTK_GRID(g), w, 0, r++, 2, 1);
    W.meters_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    flow = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(flow), 16);
    gtk_grid_set_column_homogeneous(GTK_GRID(flow), TRUE);
    gtk_widget_set_margin_start(flow, 20);
    n = perf_tray_meter_count();
    for (int i = 0, rows = (n + 2) / 3; i < n; i++) {   /* three columns, filled top to bottom */
        char lab[48], *mid = g_malloc(16);
        perf_tray_meter_label(i, lab, sizeof lab);
        perf_tray_meter_id(i, mid, 16);
        g_ptr_array_add(W.checks, mid);
        w = gtk_check_button_new_with_label(lab);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), chosen(mid));
        g_signal_connect(w, "toggled", G_CALLBACK(on_meter), mid);
        gtk_grid_attach(GTK_GRID(flow), w, i / rows, i % rows, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(W.meters_box), flow, FALSE, FALSE, 0);
    {
        GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GPtrArray *mp = mount_list();
        W.mount = gtk_combo_box_text_new();
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(W.mount), "*", "The Fullest File System");
        for (guint i = 0; i < mp->len; i++)
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(W.mount), g_ptr_array_index(mp, i), g_ptr_array_index(mp, i));
        if (*g_p.disk_mount && !gtk_combo_box_set_active_id(GTK_COMBO_BOX(W.mount), g_p.disk_mount)) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(W.mount), g_p.disk_mount, g_p.disk_mount);   /* not mounted now: kept */
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(W.mount), g_p.disk_mount);
        }
        if (!*g_p.disk_mount) gtk_combo_box_set_active_id(GTK_COMBO_BOX(W.mount), "*");
        g_signal_connect(W.mount, "changed", G_CALLBACK(on_mount), NULL);
        gtk_widget_set_margin_start(h, 20);
        gtk_box_pack_start(GTK_BOX(h), row_label("Disk Space _Mount", W.mount), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(h), W.mount, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(W.meters_box), h, FALSE, FALSE, 0);
        g_ptr_array_free(mp, TRUE);
    }
    gtk_widget_set_sensitive(W.meters_box, g_p.icons_on);
    gtk_grid_attach(GTK_GRID(g), W.meters_box, 0, r++, 2, 1);
    gtk_grid_attach(GTK_GRID(g), note("Each meter you choose gets its own icon in the notification area and keeps updating "
                                      "while SRS is hidden, next to SRS's own icon and in this order. Left-click an icon to "
                                      "open SRS; right-click it to change the selection. Plasma: right-click the tray arrow > "
                                      "Configure System Tray > Entries to set the SRS meters to Always shown."), 0, r++, 2, 1);
    w = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w), "Normal (With the Hardware Icons)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w), "First");
    gtk_combo_box_set_active(GTK_COMBO_BOX(w), g_p.tray_first);
    g_signal_connect(w, "changed", G_CALLBACK(on_position), NULL);
    gtk_grid_attach(GTK_GRID(g), row_label("Tray Positio_n", w), 0, r, 1, 1);
    gtk_grid_attach(GTK_GRID(g), w, 1, r++, 1, 1);
    gtk_grid_attach(GTK_GRID(g), note("The desktop decides the final order. Plasma sorts its tray by kind, then by name; "
                                      "First puts SRS's icons in the kind it sorts first (only Notifications stays ahead), "
                                      "but another app that does the same can still sort next to or before them. "
                                      "Normal keeps them with the volume, network and battery icons."), 0, r++, 2, 1);

    gtk_grid_attach(GTK_GRID(g), heading("Window"), 0, r++, 2, 1);
    W.close_cb = gtk_check_button_new_with_mnemonic("_Keep Running in the Tray When the Window Is Closed");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(W.close_cb), T.hk.get_close_to_tray && T.hk.get_close_to_tray());
    gtk_widget_set_sensitive(W.close_cb, T.hk.set_close_to_tray != NULL);
    g_signal_connect(W.close_cb, "toggled", G_CALLBACK(on_close_tray), NULL);
    gtk_grid_attach(GTK_GRID(g), W.close_cb, 0, r++, 2, 1);
    W.auto_cb = gtk_check_button_new_with_mnemonic("Start Minimised to the Tra_y at Login");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(W.auto_cb), T.hk.get_autostart && T.hk.get_autostart());
    gtk_widget_set_sensitive(W.auto_cb, T.hk.set_autostart != NULL);
    g_signal_connect(W.auto_cb, "toggled", G_CALLBACK(on_autostart), NULL);
    gtk_grid_attach(GTK_GRID(g), W.auto_cb, 0, r++, 2, 1);
    gtk_grid_attach(GTK_GRID(g), note("Off by default: closing the window quits SRS. On: closing hides it; the tray "
                                      "icon's menu has Quit. The same choices are in Menu > Options."), 0, r++, 2, 1);
    {
        GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        S.button = gtk_button_new_with_mnemonic("_Bind Global Shortcut...");
        S.status = gtk_label_new(NULL);
        gtk_label_set_xalign(GTK_LABEL(S.status), 0);
        g_signal_connect(S.button, "clicked", G_CALLBACK(on_bind), NULL);
        g_signal_connect(S.status, "destroy", G_CALLBACK(on_status_gone), NULL);
        gtk_box_pack_start(GTK_BOX(h), S.button, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(h), S.status, FALSE, FALSE, 0);
        gtk_grid_attach(GTK_GRID(g), row_label("Summon Shortcut", S.button), 0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(g), h, 1, r++, 1, 1);
        gtk_grid_attach(GTK_GRID(g), note("Not bound by default. The desktop asks you to confirm the keys (Ctrl+Shift+Esc "
                                          "is suggested) and keeps the binding; keys it already uses are not taken."), 0, r++, 2, 1);
        shortcut_ui();
    }
    return g;
}

static void on_dlg_gone(GtkWidget *w, gpointer d) { (void)w; (void)d; g_dlg = NULL; }
static void on_dlg_guide(GtkButton *b, gpointer d) { (void)b; (void)d; if (T.hk.open_guide && g_dlg) T.hk.open_guide(GTK_WINDOW(g_dlg), "tray-meters"); }
static void on_dlg_response(GtkDialog *dlg, int r, gpointer d) { (void)d; if (r != GTK_RESPONSE_HELP) gtk_widget_destroy(GTK_WIDGET(dlg)); }
static gboolean on_dlg_key(GtkWidget *w, GdkEventKey *ev, gpointer d)
{
    (void)w; (void)d;
    if (ev->keyval != GDK_KEY_F1) return FALSE;
    on_dlg_guide(NULL, NULL);
    return TRUE;
}

void perf_tray_settings_dialog(GtkWindow *parent)
{
    GtkWidget *area, *sw;
    if (g_dlg) { gtk_window_present(GTK_WINDOW(g_dlg)); return; }
    g_dlg = gtk_dialog_new_with_buttons("Task Manager Settings", parent, GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_role(GTK_WINDOW(g_dlg), "srs-task-manager-settings");
    g_signal_connect(g_dlg, "response", G_CALLBACK(on_dlg_response), NULL);
    g_signal_connect(g_dlg, "destroy", G_CALLBACK(on_dlg_gone), NULL);
    area = gtk_dialog_get_content_area(GTK_DIALOG(g_dlg));
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sw), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(sw), 700);
    gtk_container_add(GTK_CONTAINER(sw), perf_tray_settings_widget());
    gtk_box_pack_start(GTK_BOX(area), sw, TRUE, TRUE, 0);
    if (T.hk.open_guide) {   /* the same Guide button as SRS's item tabs, at the guide's Task Manager section */
        GtkWidget *b = gtk_button_new(), *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_box_pack_start(GTK_BOX(hb), gtk_image_new_from_icon_name("help-browser-symbolic", GTK_ICON_SIZE_BUTTON), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hb), gtk_label_new_with_mnemonic("_Guide"), FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(b), hb);
        gtk_widget_set_tooltip_text(b, "Open the user guide at the Task Manager settings and tray meters (F1)");
        g_signal_connect(b, "clicked", G_CALLBACK(on_dlg_guide), NULL);
        gtk_dialog_add_action_widget(GTK_DIALOG(g_dlg), b, GTK_RESPONSE_HELP);
        g_signal_connect(g_dlg, "key-press-event", G_CALLBACK(on_dlg_key), NULL);
    }
    gtk_widget_show_all(g_dlg);
}
