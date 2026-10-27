/* bench_gpucompare.c - GPU Compute: Vulkan vs HIP vs CUDA vs OpenCL (see bench_gpucompare.h). Written for SRS.
 * Columns = installed back ends that have the chosen GPU (OpenCL: one per platform); rows = the same tests
 * with the same sizes. Per test: a warm-up, then work in pieces of about 25 ms until the chosen GPU time is
 * used up; every piece is checked (GEMM against the first result, the others against what was written). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "bench.h"
#include "bench_gpu.h"
#include "bench_gpucompare.h"
#include "burner.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *const gpucmp_row_name[GC_N] = {
    "FP32 matrix multiply, own kernel", "FP32 matrix multiply, vendor library", "FP16 matrix multiply, own kernel", "FP16 matrix multiply, vendor library",
    "FP64 matrix multiply, own kernel", "FP64 matrix multiply, vendor library", "FP32 multiply-add chain (peak)", "FP64 multiply-add chain (peak)",
    "Video memory copy", "Upload (system to video memory)", "Download (video to system memory)", "Kernel launch round trip (lower is better)"
};
const char *const gpucmp_row_unit[GC_N] = { "GFLOPS", "GFLOPS", "GFLOPS", "GFLOPS", "GFLOPS", "GFLOPS", "GFLOPS", "GFLOPS", "GB/s", "GB/s", "GB/s", "µs" };

void gpucmp_cfg_default(gpucmp_cfg *c)
{
    memset(c, 0, sizeof *c);
    c->seconds = 1.0;
    c->device = -1;
}

#ifdef __linux__
static const char *volatile g_stage = "";
static char g_stage_buf[2][200];   /* two buffers: the GUI timer reads one while the next is written */
static int g_stage_i;

/* the stage text for the GUI timer, written into the buffer the reader is not on */
static void set_stage_text(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_stage_text(const char *fmt, ...)
{
    va_list ap;
    char *b = g_stage_buf[g_stage_i ^= 1];
    va_start(ap, fmt);
    vsnprintf(b, sizeof g_stage_buf[0], fmt, ap);
    va_end(ap);
    g_stage = b;
}
const char *gpucmp_stage(void) { return g_stage; }

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

typedef struct { int kind, prec, variant; } rowdef;
static const rowdef rows[GC_N] = {
    { GK_GEMM, GP_FP32, GV_OWN }, { GK_GEMM, GP_FP32, GV_LIB }, { GK_GEMM, GP_FP16, GV_OWN }, { GK_GEMM, GP_FP16, GV_LIB },
    { GK_GEMM, GP_FP64, GV_OWN }, { GK_GEMM, GP_FP64, GV_LIB }, { GK_FMA, GP_FP32, GV_OWN }, { GK_FMA, GP_FP64, GV_OWN },
    { GK_COPY, GP_FP32, GV_OWN }, { GK_UPLOAD, GP_FP32, GV_OWN }, { GK_DOWNLOAD, GP_FP32, GV_OWN }, { GK_LATENCY, GP_FP32, GV_OWN }
};

typedef struct {
    gpucmp_result *r;
    gpucmp_cfg c;
    double gemm_n, mem_bytes, lat_rounds, fma_groups;
    volatile int *cancel;
    int wrong, failed, stopped;
    char when[32], fail[200];
} cx;

static void set_cell(gpucmp_col *col, int row, int state, double v, const char *note)
{
    col->state[row] = state;
    col->v[row] = v;
    if (note) snprintf(col->note[row], sizeof col->note[row], "%.150s", note);
}

/* one test on one open GPU. Returns 0, or the GBP_* code that ends the column (GPU hung or lost) */
static int measure(cx *x, const gb_ops *ops, gb_ctx *ctx, gpucmp_col *col, int row)
{
    const rowdef *d = &rows[row];
    double work = 0, secs = 0, tot_w = 0, tot_s = 0, size = 0, t_end;
    unsigned maxreps = 1, reps = 1;
    long wrong = 0, w;
    int rc;
    switch (d->kind) {
    case GK_GEMM: size = x->gemm_n; break;
    case GK_FMA: size = x->fma_groups; break;
    case GK_LATENCY: size = x->lat_rounds; break;
    default: size = x->mem_bytes;
    }
    rc = ops->prep(ctx, d->kind, d->prec, d->variant, (size_t)size, &work, &maxreps);
    if (rc == GBP_UNSUPPORTED) { set_cell(col, row, CS_NA, 0, ops->error(ctx)); return 0; }
    if (rc) { set_cell(col, row, CS_FAILED, 0, ops->error(ctx)); return rc == GBP_ERROR ? 0 : rc; }
    if (d->kind == GK_LATENCY) {
        reps = (unsigned)x->lat_rounds;
        if (reps > maxreps) reps = maxreps;
        rc = ops->run(ctx, 4, &secs, &w);   /* warm-up: first launches pay for lazy driver set-up */
        if (!rc) { rc = ops->run(ctx, reps, &secs, &wrong); wrong += w; }
        if (rc) { set_cell(col, row, CS_FAILED, 0, ops->error(ctx)); ops->release(ctx); return rc == GBP_ERROR ? 0 : rc; }
        set_cell(col, row, wrong ? CS_WRONG : CS_OK, secs * 1e6, wrong ? "the kernel launches did not all run" : NULL);
        ops->release(ctx);
        return 0;
    }
    rc = ops->run(ctx, 1, &secs, &wrong);   /* warm-up, also the first check */
    while (!rc && secs < 0.020 && reps < maxreps) {   /* grow one piece to ~25 ms */
        double f = secs > 0 ? 0.025 / secs : 64;
        unsigned nr;
        if (f > 64) f = 64;
        if (f < 2) f = 2;
        nr = (unsigned)((double)reps * f);
        reps = nr > maxreps || nr < reps ? maxreps : nr;
        rc = ops->run(ctx, reps, &secs, &w);
        wrong += w;
        if (x->cancel && *x->cancel) break;
    }
    t_end = now_s() + x->c.seconds * 4 + 5;
    {
        double gpu = 0;
        while (!rc && gpu < x->c.seconds && now_s() < t_end && !(x->cancel && *x->cancel)) {
            rc = ops->run(ctx, reps, &secs, &w);
            if (rc) break;
            wrong += w;
            if (secs <= 0) secs = 1e-9;
            tot_w += work * reps;
            tot_s += secs;
            gpu += secs;
        }
    }
    if (rc) { set_cell(col, row, CS_FAILED, 0, ops->error(ctx)); ops->release(ctx); return rc == GBP_ERROR ? 0 : rc; }
    if (x->cancel && *x->cancel) { set_cell(col, row, CS_STOPPED, 0, NULL); ops->release(ctx); return 0; }
    if (wrong) set_cell(col, row, CS_WRONG, 0, "the GPU returned a wrong result");
    else if (tot_s > 0) set_cell(col, row, CS_OK, tot_w / tot_s / 1e9, NULL);
    else set_cell(col, row, CS_FAILED, 0, "no piece of work finished");
    ops->release(ctx);
    return 0;
}

/* ---------------------------------------------------------------- columns */
static void add_missing(cx *x, int b, const char *hint)
{
    gpucmp_col *c;
    if (x->r->ncol >= GPUCMP_MAXCOL) return;
    c = &x->r->col[x->r->ncol++];
    memset(c, 0, sizeof *c);
    c->backend = b;
    snprintf(c->info.label, sizeof c->info.label, "%s", gb_name[b]);
    snprintf(c->hint, sizeof c->hint, "%s", hint);
}

static void add_column(cx *x, const gb_info *i)
{
    gpucmp_col *c;
    if (x->r->ncol >= GPUCMP_MAXCOL) return;
    c = &x->r->col[x->r->ncol++];
    memset(c, 0, sizeof *c);
    c->shown = 1;
    c->backend = i->backend;
    c->info = *i;
}

/* the columns: Vulkan's chosen device first (it fixes "the GPU"), then every device of the other back ends at the same PCI address */
static int build_columns(cx *x, char *why, size_t wn)
{
    gb_info vk[GB_LISTMAX], oth[GB_LISTMAX];
    int nvk = 0, vsel = -1, b, i;
    char pci[16] = "", w[200];
    unsigned mask = x->c.backends ? x->c.backends : (1u << GB_N) - 1;
    if (mask & (1u << GB_VULKAN)) {
        if (!gb_vulkan.available(w, sizeof w)) add_missing(x, GB_VULKAN, w);
        else {
            nvk = gb_vulkan.list(vk, GB_LISTMAX, w, sizeof w);
            if (nvk <= 0) add_missing(x, GB_VULKAN, w[0] ? w : "Vulkan found no device");
            else {
                vsel = x->c.device;
                if (vsel < 0) { vsel = 0; for (i = 0; i < nvk; i++) if (!vk[i].is_cpu) { vsel = i; break; } }
                if (vsel >= nvk) { snprintf(why, wn, "there is no Vulkan device %d (found %d)", vsel, nvk); return -1; }
                snprintf(pci, sizeof pci, "%s", vk[vsel].pci);
                add_column(x, &vk[vsel]);
            }
        }
    }
    for (b = GB_HIP; b < GB_N; b++) {
        int n, k, hit = 0;
        if (!(mask & (1u << b))) continue;
        if (!gb_table[b]->available(w, sizeof w)) { add_missing(x, b, w); continue; }
        n = gb_table[b]->list(oth, GB_LISTMAX, w, sizeof w);
        if (n <= 0) { add_missing(x, b, w[0] ? w : "no device found"); continue; }
        for (k = 0; k < n; k++) {
            int same;
            if (vsel >= 0 && pci[0]) same = !strcmp(oth[k].pci, pci);
            else if (vsel >= 0) same = !vk[vsel].is_cpu && !oth[k].is_cpu && n == 1;
            else same = !oth[k].is_cpu;
            if (same) { add_column(x, &oth[k]); hit++; }
        }
        if (!hit) {
            if (pci[0]) add_missing(x, b, "installed, but it does not see the GPU that was chosen");
            else add_missing(x, b, "installed, but SRS cannot tell which of its devices is the chosen GPU");
        }
    }
    if (!x->r->ncol) { snprintf(why, wn, "no back end was selected"); return -1; }
    return 0;
}

/* ---------------------------------------------------------------- result */
static void fmt_val(char *b, size_t n, int row, double v)
{
    if (row == GC_LATENCY) snprintf(b, n, v >= 100 ? "%.0f" : "%.1f", v);
    else snprintf(b, n, v >= 100 ? "%.0f" : "%.2f", v);
}

/* the column that is fastest in a row (-1 none); latency: the lowest */
static int fastest(const gpucmp_result *r, int row)
{
    int c, best = -1;
    for (c = 0; c < r->ncol; c++) {
        if (!r->col[c].shown || r->col[c].state[row] != CS_OK) continue;
        if (best < 0 || (row == GC_LATENCY ? r->col[c].v[row] < r->col[best].v[row] : r->col[c].v[row] > r->col[best].v[row])) best = c;
    }
    return best;
}

static void cmp_render(srs_out *o, void *ctx)
{
    static const int tabs2[] = { 140 };
    int tabs[GPUCMP_MAXCOL + 1];
    const char *cells[GPUCMP_MAXCOL + 2];
    int colors[GPUCMP_MAXCOL + 2];
    cx *x = ctx;
    gpucmp_result *r = x->r;
    int c, row, vk = -1;
    char buf[GPUCMP_MAXCOL][48];
    so_siv_header(o, "GPU Compute: Vulkan vs HIP vs CUDA vs OpenCL on the same GPU");
    for (c = 0; c < r->ncol; c++) if (r->col[c].shown && r->col[c].backend == GB_VULKAN) vk = c;
    for (c = 0; c <= r->ncol; c++) tabs[c] = 360 + c * 150;   /* the longest row name with its unit is about 50 characters */
    so_tabs(o, tabs, r->ncol + 1);
    cells[0] = "Test";
    colors[0] = C_LABEL;
    for (c = 0; c < r->ncol; c++) { cells[c + 1] = r->col[c].info.label; colors[c + 1] = C_LABEL; }
    so_row(o, r->ncol + 1, cells, colors);
    for (row = 0; row < GC_N; row++) {
        int best = fastest(r, row), any = 0;
        char head[120];
        for (c = 0; c < r->ncol; c++) if (r->col[c].shown && r->col[c].state[row] != CS_NOTRUN) any = 1;
        if (!any) continue;
        snprintf(head, sizeof head, "%s (%s)", gpucmp_row_name[row], gpucmp_row_unit[row]);
        cells[0] = head;
        colors[0] = C_TEAL;
        for (c = 0; c < r->ncol; c++) {
            const gpucmp_col *k = &r->col[c];
            char *b = buf[c];
            colors[c + 1] = C_GREEN;
            if (!k->shown) { snprintf(b, 48, "%s", strncmp(k->hint, "not installed", 13) ? "no device" : "not installed"); colors[c + 1] = C_GRAY; }
            else switch (k->state[row]) {
            case CS_OK: {
                char v[24], pc[24] = "";
                fmt_val(v, sizeof v, row, k->v[row]);
                if (vk >= 0 && c != vk && r->col[vk].state[row] == CS_OK && k->v[row] > 0 && r->col[vk].v[row] > 0)
                    snprintf(pc, sizeof pc, " (%.0f%%)", (row == GC_LATENCY ? r->col[vk].v[row] / k->v[row] : k->v[row] / r->col[vk].v[row]) * 100.0);
                snprintf(b, 48, "%s%s%s", v, pc, c == best ? " *" : "");
                break;
            }
            case CS_WRONG: snprintf(b, 48, "WRONG RESULT"); colors[c + 1] = C_RED; break;
            case CS_NA: snprintf(b, 48, "not available"); colors[c + 1] = C_GRAY; break;
            case CS_FAILED: snprintf(b, 48, "failed"); colors[c + 1] = C_RED; break;
            case CS_STOPPED: snprintf(b, 48, "stopped"); colors[c + 1] = C_ORANGE; break;
            case CS_SKIPPED: snprintf(b, 48, "not run"); colors[c + 1] = C_GRAY; break;
            default: snprintf(b, 48, "-"); colors[c + 1] = C_GRAY;
            }
            cells[c + 1] = b;
        }
        so_row(o, r->ncol + 1, cells, colors);
    }
    so_par(o);
    so_tabs(o, tabs2, 1);
    if (x->stopped) so_kv(o, "Status", C_ORANGE, "Stopped before the end - empty cells were not measured");
    if (x->wrong) so_kv(o, "Check", C_RED, "At least one result was wrong: that cell is an error, not a speed");
    if (x->failed) so_kvf(o, "Error", C_RED, "%s", x->fail);
    for (c = 0; c < r->ncol; c++) {
        const gpucmp_col *k = &r->col[c];
        if (!k->shown) { so_kvf(o, k->info.label, C_GRAY, "%s", k->hint); continue; }   /* the hint says "not installed - ..." or why there is no device */
        so_kvf(o, k->info.label, C_VALUE, "%s (%s)", k->info.device, k->info.kind);
        so_kvf(o, "  software", C_GRAY, "%s%s", k->info.runtime, k->info.has_lib ? "" : "; no vendor GEMM library");
        if (k->aborted[0]) so_kv(o, "  ended early", C_RED, k->aborted);
        for (row = 0; row < GC_N; row++) {
            int r2, again = 0;
            if ((k->state[row] == CS_FAILED || k->state[row] == CS_WRONG) && k->note[row][0]) so_kvf(o, "  problem", C_RED, "%s: %s", gpucmp_row_name[row], k->note[row]);
            else if (k->state[row] == CS_NA && k->note[row][0]) {   /* why a test is not available (said once per reason) */
                for (r2 = 0; r2 < row; r2++) if (k->state[r2] == CS_NA && !strcmp(k->note[r2], k->note[row])) again = 1;
                if (!again) so_kvf(o, "  not available", C_GRAY, "%s: %s", gpucmp_row_name[row], k->note[row]);
            }
        }
    }
    so_kvf(o, "Sizes", C_VALUE, "matrices %.0f x %.0f; %.0f MiB memory tests; %.1f s of GPU time per test", x->gemm_n, x->gemm_n, x->mem_bytes / 1048576.0, x->c.seconds);
    so_kv(o, "Run", C_GRAY, x->when);
    so_kv(o, "Build", C_GRAY, bench_build_tag());
    so_note(o, "* = fastest in the row. (%) = that column's speed compared with Vulkan (above 100% = faster than Vulkan; for the launch round trip, "
               "above 100% = quicker). Own kernel = SRS's plain kernel, the same work on every back end; vendor library = hipBLAS, cuBLAS or CLBlast. "
               "Every result is checked; a wrong one is shown as an error.");
    so_note(o, "Compare results only between alike SRS builds and the same GPU driver and runtime versions: an update of either can change them. "
               "1 GB = 1,000,000,000 bytes; GFLOPS = billions of floating-point operations per second (a multiply-add counts as 2).");
    so_board_footer(o);
}

int bench_gpucompare(const gpucmp_cfg *cfg, gpucmp_result *res, srs_out *o, volatile int *cancel, char *why, size_t wn)
{
    const char *who = NULL;
    cx *x;
    gpucmp_result *r;
    int c, row, rc = 0;
    time_t now = time(NULL);
    if (why && wn) why[0] = 0;
    if (burn_active()) { if (why) snprintf(why, wn, "CPU Burnin / Stress is running; start the GPU comparison when it has finished"); return -1; }
    if (!gpu_claim("GPU Compute comparison", &who)) { if (why) gpu_claim_text(who, why, wn); return -1; }
    if (burn_active()) { gpu_release(); if (why) snprintf(why, wn, "CPU Burnin / Stress is running; start the GPU test when it has finished"); return -1; }   /* it started at the same moment */
    x = calloc(1, sizeof *x);
    r = res ? res : calloc(1, sizeof *r);
    if (!x || !r) { free(x); if (!res) free(r); if (why) snprintf(why, wn, "out of memory"); gpu_release(); return -1; }
    memset(r, 0, sizeof *r);
    x->r = r;
    x->c = *cfg;
    x->cancel = cancel;
    if (x->c.seconds < 0.05) x->c.seconds = 0.05;
    if (x->c.seconds > 10) x->c.seconds = 10;
    x->gemm_n = x->c.tiny ? 64 : 1024;
    x->mem_bytes = x->c.tiny ? 1048576.0 : 256.0 * 1048576.0;
    x->lat_rounds = x->c.tiny ? 10 : 500;
    x->fma_groups = x->c.tiny ? 16 : 4096;
    g_stage = "Looking for the GPU back ends";
    if (build_columns(x, why ? why : x->fail, why ? wn : sizeof x->fail)) { g_stage = ""; free(x); if (!res) free(r); gpu_release(); return -1; }
    for (c = 0; c < r->ncol && !(cancel && *cancel) && !x->failed; c++) {   /* a hung or lost GPU is not touched again */
        gpucmp_col *col = &r->col[c];
        const gb_ops *ops;
        gb_ctx *ctx = NULL;
        char w[240];
        double mem;
        if (!col->shown) continue;
        ops = gb_table[col->backend];
        set_stage_text("%s: opening", col->info.label);
        if (ops->open(&ctx, &col->info, x->c.software_only, w, sizeof w)) {
            snprintf(col->aborted, sizeof col->aborted, "%.190s", w);
            for (row = 0; row < GC_N; row++) col->state[row] = CS_FAILED;
            continue;
        }
        mem = col->info.mem_bytes;
        if (mem > 0 && x->mem_bytes * 4 > mem && !x->c.tiny) x->mem_bytes = mem / 4;
        for (row = 0; row < GC_N; row++) {
            int rc2;
            if (cancel && *cancel) { x->stopped = 1; break; }
            set_stage_text("%s: %s", col->info.label, gpucmp_row_name[row]);
            rc2 = measure(x, ops, ctx, col, row);
            if (rc2) {   /* hung or lost: this GPU is not touched again */
                snprintf(col->aborted, sizeof col->aborted, "%s", ops->error(ctx));
                snprintf(x->fail, sizeof x->fail, "%s: %s", col->info.label, col->aborted);
                x->failed = 1;
                break;
            }
        }
        ops->close(ctx);
    }
    if (x->failed)   /* every test that did not run after the failure says so, in this column and the ones after it */
        for (c = 0; c < r->ncol; c++) {
            gpucmp_col *col = &r->col[c];
            int any = 0;
            if (!col->shown) continue;
            for (row = 0; row < GC_N; row++) if (col->state[row] == CS_NOTRUN) { col->state[row] = CS_SKIPPED; any = 1; }
            if (any && !col->aborted[0]) snprintf(col->aborted, sizeof col->aborted, "not run: the GPU failed earlier in this run (%.150s)", x->fail);
        }
    if (cancel && *cancel) x->stopped = 1;
    for (c = 0; c < r->ncol; c++)
        for (row = 0; row < GC_N; row++) {
            if (r->col[c].state[row] == CS_WRONG) x->wrong = 1;
            if (r->col[c].state[row] == CS_STOPPED) x->stopped = 1;
        }
    g_stage = "";
    strftime(x->when, sizeof x->when, "%Y-%m-%d %H:%M:%S", localtime(&now));
    bench_save_render("gpucompare", cmp_render, x, o);
    {
        char params[900], resl[900];
        size_t pl, rl;
        snprintf(params, sizeof params, "seconds=%.1f matrix=%.0f mem=%.0fMiB", x->c.seconds, x->gemm_n, x->mem_bytes / 1048576.0);
        resl[0] = 0;
        for (c = 0; c < r->ncol; c++) {
            const gpucmp_col *k = &r->col[c];
            pl = strlen(params);
            if (!k->shown) { snprintf(params + pl, sizeof params - pl, " | %s: not installed", k->info.label); continue; }
            snprintf(params + pl, sizeof params - pl, " | %s: %.60s, %.110s", k->info.label, k->info.device, k->info.runtime);
            rl = strlen(resl);
            snprintf(resl + rl, sizeof resl - rl, "%s%s gemm32 %.0f gflops, fma32 %.0f gflops, copy %.1f GB/s, latency %.1f us", rl ? "; " : "", k->info.label,
                     k->state[GC_GEMM32_OWN] == CS_OK ? k->v[GC_GEMM32_OWN] : 0.0, k->state[GC_FMA32] == CS_OK ? k->v[GC_FMA32] : 0.0,
                     k->state[GC_COPY] == CS_OK ? k->v[GC_COPY] : 0.0, k->state[GC_LATENCY] == CS_OK ? k->v[GC_LATENCY] : 0.0);
        }
        rl = strlen(resl);
        snprintf(resl + rl, sizeof resl - rl, "%s", x->failed ? " FAILED" : x->stopped ? " STOPPED" : x->wrong ? " WRONG" : "");
        bench_history_add("gpucompare", params, resl);
    }
    rc = x->failed ? -2 : x->stopped ? 1 : x->wrong ? 2 : 0;
    if (x->failed && why) snprintf(why, wn, "%s", x->fail);
    free(x);
    if (!res) free(r);
    gpu_release();
    return rc;
}

#else   /* not Linux */
const char *gpucmp_stage(void) { return ""; }
int bench_gpucompare(const gpucmp_cfg *c, gpucmp_result *r, srs_out *o, volatile int *cancel, char *why, size_t wn)
{
    (void)c; (void)r; (void)o; (void)cancel;
    if (why) snprintf(why, wn, "Linux only");
    return -1;
}
#endif
