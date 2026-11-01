/* cli.c - command-line front end; output matches the original Python tool. */
#include "srs.h"
#include "lists.h"
#include "mem_timings.h"
#ifdef SRS_HAVE_SECTIONS
#include "sec.h"
#endif
#include <strings.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#define ISATTY_STDOUT() _isatty(_fileno(stdout))
#else
#include <unistd.h>
#define ISATTY_STDOUT() isatty(1)
#endif

static const char *USAGE =
    "usage: sysreport-search [-h] [--version] {id,find,devices,-save,--list,burn,bench,ipi,latency,pi,crypto,prime,cachemem,storage,os-latency,compute,work,net,gpu,gpu-burn,gpu-compare,cuda-toolkit,gpu-top,big-top,ir,update-lists} ...\n";

static const char *HELP =
    "SIV-style search for the native system report (srs-sysreport).\n"
    "\n"
    "  id VVVV[:DDDD[:SSSS:ssss]][:RR] [--bus pci|usb] [--mode tree|scan|info]\n"
    "        hierarchical device-ID lookup, most specific key first\n"
    "  find PATTERN [--report DIR] [-i] [-F] [--max N] [--color auto|always|never]\n"
    "        regex search across a report's sections with highlighted matches\n"
    "  devices [--bus pci|usb|all]\n"
    "        resolve every local PCI/USB device\n"
#ifdef SRS_HAVE_SECTIONS
    "\n"
    "System information items (SIV syntax; quote the argument in zsh/fish, [..] is a glob):\n"
    "  -save[=[item][item:arg]...][=FILE|STDOUT] [--redact]\n"
    "        render items as text (no items = SIV's default set, [all] = every item)\n"
    "        --redact masks serial numbers, UUIDs, MAC addresses (vendor prefix kept),\n"
    "        boot/machine IDs and the host name, for reports you share\n"
    "  --list\n"
    "        every item: id, category, bucket (linux/absent/windows-only), group, title\n"
    "  guide [ITEM] [--print]\n"
    "        open the user guide at ITEM's section (PDF viewer, else the HTML guide);\n"
    "        --print only shows the section and the command that would run\n"
#ifdef __linux__
    "\n"
    "Benchmarks and Stress (load the CPU, the drive, the network or the GPU; run only when you ask):\n"
    "  burn [--threads N | --cpus 0,2,5] [--seconds S] [--mode same|swap] [--limit C] [--accept-high-limit] [--cool S] [--cycles N]\n"
    "        CPU burn-in with a temperature limit (default 10 s, limit 90 C)\n"
    "  bench {ac,as,dd,dw,ec,ep,lc,lp,pc,pp} [--seconds S] [--yes]\n"
    "        Dhrystone / Whetstone in a SIV benchmark mode (S = 0.1..60 s per measurement);\n"
    "        asks first, --yes starts at once\n"
    "  ipi [--cpus N] [--rounds R] [--yes]\n"
    "        core-to-core (cache-line ping-pong) latency matrix; asks first, --yes starts at once\n"
    "  latency [--max MB] [--yes]\n"
    "        memory latency by block size, up to MB (1..4096, default 256); asks first, --yes starts at once\n"
    "  pi [--digits 128k|1m|..|32m|16ki|..|1mi|..|32mi|N] [--threads N] [--method chudnovsky|agm] [--save-digits FILE]\n"
    "     [--yes]\n"
    "        SRS Pi: pi to N decimal places (default 1m, 1 thread), every digit checked;\n"
    "        ki / mi = SuperPi sizes (powers of two): 1mi = 1,048,576 digits, 32mi = 33,554,432;\n"
    "        asks first (with the time and the memory it needs), --yes starts at once\n"
    "  crypto [--threads N] [--seconds S] [--all-threads-only] [--yes]\n"
    "        AES-128 and SHA-256 speed, plain C and with AES-NI / SHA-NI (default all threads, 1 s);\n"
    "        --all-threads-only skips the 1-thread pass (the GUI's All Cores; about half the time);\n"
    "        asks first, --yes starts at once\n"
    "  cachemem [--threads N] [--seconds S] [--isa avx512|avx2|sse2] [--mem-mib M] [--only mem,l1,l2,l3]\n"
    "           [--tests read,write,copy,latency] [--mib] [--mem-write auto|clzero|stream] [--yes]\n"
    "        Cache & Memory benchmark: Read / Write / Copy and Latency of the RAM and L1-L3 (all threads,\n"
    "        ~20 s; asks first, --yes starts at once)\n"
    "  storage --dir FOLDER [--size MB] [--seconds S] [--profile standard|light] [--allow-usb] [--yes]\n"
    "        read / write speed of the drive holding FOLDER, on a temporary test file (default 1024 MB,\n"
    "        5 s per test); never touches existing files; asks first, --yes starts at once\n"
    "  os-latency [--samples N] [--tick-seconds S] [--only syscall,switch,spawn,wake,timer,memory,files] [--yes]\n"
    "        how quickly the OS answers: system calls, context switches, thread / process start, wake-ups,\n"
    "        timer lateness (with the CPU scheduler in use), page faults, file create / stat / unlink\n"
    "        (default 20000 samples, 3 s tick test; ~6 s on one or two CPUs; asks first, --yes starts at once)\n"
    "  compute [--threads N] [--seconds S] [--tests sgemm,dgemm,fma-sp,fma-dp,fft-sp,fft-dp,nbody,mandelbrot,raytrace]\n"
    "          [--code auto|c|sse2|avx2|avx512] [--single | --all-threads-only] [--yes]\n"
    "        CPU Compute: matrix multiply, peak multiply-add, FFT, N-body, Mandelbrot, ray tracer in GFLOPS /\n"
    "        Mpixels/s, 1 thread and N threads (default all), every answer checked; ~10 s, all threads 100%\n"
    "        busy (--single: one thread; --all-threads-only: N threads only, as All Cores); asks first,\n"
    "        --yes starts at once\n"
    "  work [--threads N] [--seconds S] [--only compress,sort,hash,text,interp] [--libs zlib,zstd,lz4,xz]\n"
    "       [--single | --all-threads-only] [--part-kib K] [--yes]\n"
    "        Everyday work: compression (system zlib / zstd / lz4 / xz), sorting, checksums, text processing\n"
    "        and a bytecode interpreter, 1 thread and N threads (default all; --all-threads-only: N threads\n"
    "        only, as All Cores), every result checked;\n"
    "        generated data only; asks first, --yes starts at once\n"
    "  net [--local] [--internet] [--seconds S] [--streams N] [--samples N] [--max-mb MB] [-4|-6]\n"
    "      [--server URL] [--hosts HOST[:PORT],...] [--yes] [--yes-internet]\n"
    "        Network: loopback TCP / UDP speed and round trip (default; nothing leaves the computer);\n"
    "        --internet adds DNS, connect, HTTPS, download and upload tests against speed.cloudflare.com\n"
    "        (up to ~1000 MB of data). Asks first; --yes answers the loopback question, the internet test\n"
    "        asks separately and only --yes-internet skips that question\n"
    "  gpu [--device N] [--seconds S] [--only fp32,fp16,fp64,int32,read,write,copy,upload,download,latency]\n"
    "      [--mem-mib M] [--list] [--yes]\n"
    "        GPU compute (Vulkan): FP32 / FP16 / FP64 / INT32 multiply-add (GFLOPS / GIOPS), video memory\n"
    "        read / write / copy and upload / download (GB/s), dispatch latency (us), every result checked;\n"
    "        ~15 s at full GPU load; Vulkan is loaded at run time; asks first, --yes starts at once\n"
    "  gpu-burn [--seconds N] [--backend vulkan|hip|cuda|opencl|auto] [--device N|all] [--fp32|--fp64]\n"
    "           [--limit C] [--accept-high-limit] [--list] [--yes]\n"
    "        GPU Burnin: repeated matrix multiplications at full GPU load, compute errors counted\n"
    "        (0 = PASS), live temperature / power / clock; stops at the temperature limit (default\n"
    "        100 C), on a driver error or a GPU that stops answering; never changes clocks, voltages\n"
    "        or fans (default 60 s); asks first, --yes starts at once\n"
    "  gpu-compare [--backends vulkan,hip,cuda,opencl] [--seconds S] [--device N] [--yes]\n"
    "        GPU Compute comparison: the same tests (matrix multiply with the own kernel and the vendor\n"
    "        library, multiply-add, copy, upload / download, launch time) through every installed back end\n"
    "        on the same GPU, side by side: fastest marked, % of Vulkan; asks first, --yes starts at once\n"
    "  cuda-toolkit [--check|--install|--ask-again|--never-ask]\n"
    "        the optional NVIDIA CUDA toolkit (its runtime compiler, NVRTC): whether it is installed, what it\n"
    "        is for, the install command for this distribution; --install runs that command after you type\n"
    "        yes (needs a terminal; warns and asks twice when ZLUDA is in use). SRS's CUDA tests are built\n"
    "        in, so it is optional. --ask-again / --never-ask: whether the GUI may ask about it\n"
    "  gpu-top [--list|--dump|--gpu-metrics|--drm-info|--pp-table|--process|--xdna|--smi|--tui] [-J] [--device N]\n"
    "          [--no-pc] [--interval MS] [-n COUNT]       (big-top is the same command)\n"
    "        The Big Top's GPU part: activity, GRBM blocks, VRAM, sensors, gpu_metrics and processes of every GPU (AMD like\n"
    "        amdgpu_top; NVIDIA through NVML); read-only, changes nothing\n"
    "  ir [--status|--list|--send REMOTE BUTTON [--repeat N]|--send-code HEX [--nec]|--learn [--save REMOTE BUTTON]|\n"
    "     --probe|--reset|--export FILE|--import FILE [--replace]] [--dry-run] [--yes]\n"
    "        IR Blaster: ELK Smart / OCRUSTAR USB infrared dongles - learn and send remote-control codes, kept in IR\n"
    "        Remote's remotes.json (shared with that app); --send and --learn ask first, --yes starts at once\n"
    "  update-lists [--list] [--yes]\n"
    "        download fresh pci.ids, usb.ids and pnp.ids from their official sources into\n"
    "        ~/.local/share/srs/lists (no root); SRS then uses the newer of these and the system's copies.\n"
    "        Each download is checked first; a failed one leaves the copy in use. --list shows the copies in\n"
    "        use and their versions; asks first, --yes downloads at once\n"
    "  prime [--limit 1e8|1e9|1e10|1e11|N] [--threads N] [--yes]\n"
    "        count the primes up to the limit, checked against the known count (default 1e9, all threads);\n"
    "        asks first, --yes starts at once\n"
#endif
    "\n"
    "srs-gui only: --gui-open=ITEM[,ITEM|stressburn|burnin|stress|bench|ipi|latency|pi|crypto|prime|cachemem|storagebench|oslatency|compute|work|netbench|gpubench|gpuburn|gpucompare|cudatoolkit|loadterms|sensors|idlookup|find]\n"
    "        open those panels / tools at start (also SRS_OPEN=...), --gui-tray start in the tray\n"
    "        --gui-classic start in the classic SIV-style main window, --gui-new in the new one\n"
    "        --gui-open=power-cost the Power & Cost page; --power-recorder record its history with no window\n"
#endif
    ;

static int usage_error(const char *msg)
{
    fputs(USAGE, stderr);
    fprintf(stderr, "sysreport-search: error: %s\n", msg);
    return 2;
}

typedef struct {
    const char *pos[4]; int npos;
    const char *bus, *mode, *report, *color;
    int icase, fixed, max;
    char err[200];
} opts;

static int parse(int argc, char **argv, int first, opts *o)
{
    int i;
    for (i = first; i < argc; i++) {
        const char *a = argv[i], *val = NULL;
        const char *eq = strchr(a, '=');
        char name[32] = "";
        if (a[0] == '-' && a[1] == '-' && a[2]) {
            size_t l = eq ? (size_t)(eq - a) : strlen(a);
            snprintf(name, sizeof name, "%.*s", (int)l, a);
            if (eq) val = eq + 1;
        }
        if (!strcmp(a, "-i") || !strcmp(a, "--ignore-case")) { o->icase = 1; continue; }
        if (!strcmp(a, "-F") || !strcmp(a, "--fixed")) { o->fixed = 1; continue; }
        if (!strcmp(name, "--bus") || !strcmp(name, "--mode") || !strcmp(name, "--report") ||
            !strcmp(name, "--max") || !strcmp(name, "--color")) {
            if (!val) {
                if (i + 1 >= argc) { snprintf(o->err, sizeof o->err, "argument %s: expected one argument", name); return -1; }
                val = argv[++i];
            }
            if (!strcmp(name, "--bus")) o->bus = val;
            else if (!strcmp(name, "--mode")) o->mode = val;
            else if (!strcmp(name, "--report")) o->report = val;
            else if (!strcmp(name, "--color")) o->color = val;
            else {
                char *end;
                o->max = (int)strtol(val, &end, 10);
                if (*end || !*val) { snprintf(o->err, sizeof o->err, "argument --max: invalid int value: '%s'", val); return -1; }
            }
            continue;
        }
        if (a[0] == '-' && a[1] && !(a[1] >= '0' && a[1] <= '9')) {
            snprintf(o->err, sizeof o->err, "unrecognized arguments: %s", a);
            return -1;
        }
        if (o->npos == 4) { snprintf(o->err, sizeof o->err, "unrecognized arguments: %s", a); return -1; }
        o->pos[o->npos++] = a;
    }
    return 0;
}

static int choice(const char *v, const char *a, const char *b, const char *c)
{
    return !strcmp(v, a) || !strcmp(v, b) || (c && !strcmp(v, c));
}

static int cmd_id(opts *o)
{
    enum srs_bus bus = !strcmp(o->bus, "usb") ? BUS_USB : BUS_PCI;
    char err[256], recs[32];
    srs_db db;
    srs_result r;
    int i;

    if (!strcmp(o->mode, "info")) {
        srs_info in;
        int rc = srs_info_lookup(bus, o->pos[0], &in);
        printf("Query      %s\n", in.query);
        if (in.err[0]) printf("Error      %s\n", in.err);
        printf("Vendor     %s\n", in.vendor[0] ? in.vendor : "-");
        printf("Resolved   %s\n", in.model[0] ? in.model : "-");
        printf("info mode (%s) in %.2f ms\n", strncmp(in.query, "SetupAPI", 8) ? "systemd-hwdb" : "SetupAPI", in.ms);
        return rc;
    }
    if (srs_db_load(&db, bus)) {
        fprintf(stderr, "cannot find %s.ids (beside the program or in the hwdata directory)\n", srs_bus_name(bus));
        return 2;
    }
    if (srs_resolve(&db, o->pos[0], !strcmp(o->mode, "scan") ? MODE_SCAN : MODE_TREE, &r, err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        srs_db_free(&db);
        return 2;
    }
    for (i = 0; i < r.nchain; i++)
        printf("  %c %-24s %s\n", i == r.matched ? '<' : ' ', r.chain[i], r.name[i][0] ? r.name[i] : "(no entry)");
    printf("Vendor     %s\n", r.vendor[0] ? r.vendor : "-");
    if (r.subvendor[0]) printf("Subsystem  %s\n", r.subvendor);
    printf("Stock      %s\n", r.stock[0] ? r.stock : "-");
    if (r.resolved[0]) printf("Resolved   %s\n", r.resolved);
    else printf("Resolved   No Matches for %s\n", o->pos[0]);
    srs_thousands(db.n, recs, sizeof recs);
    printf("%s mode: %s records loaded in %.1f ms, lookup %.3f ms\n", o->mode, recs, db.load_ms, r.lookup_ms);
    srs_db_free(&db);
    return r.resolved[0] ? 0 : 1;
}

static void print_highlighted(const srs_hit *h, int color)
{
    int k, at = 0;
    if (!color) { fputs(h->text, stdout); return; }
    for (k = 0; k < h->nspan; k++) {
        fwrite(h->text + at, 1, (size_t)(h->spans[k].so - at), stdout);
        fputs("\033[1;30;43m", stdout);
        fwrite(h->text + h->spans[k].so, 1, (size_t)(h->spans[k].eo - h->spans[k].so), stdout);
        fputs("\033[0m", stdout);
        at = h->spans[k].eo;
    }
    fputs(h->text + at, stdout);
}

static int cmd_find(opts *o)
{
    srs_findres fr;
    char err[1400];
    int i, j, color;
    int rc = srs_find(o->report, o->pos[0], o->icase, o->fixed, o->max, &fr, err, sizeof err);
    if (rc) { fprintf(stderr, "%s\n", err); return rc; }
    color = !strcmp(o->color, "always") || (!strcmp(o->color, "auto") && ISATTY_STDOUT());
    printf("Searching %s for \"%s\"\n", fr.report, o->pos[0]);
    for (i = 0; i < fr.nsec; i++) {
        const srs_section *s = &fr.secs[i];
        printf("\n== %s - %d Match%s\n", s->file, s->count, s->count != 1 ? "es" : "");
        for (j = 0; j < s->nshown; j++) {
            printf("%7d: ", s->hits[j].lineno);
            print_highlighted(&s->hits[j], color);
            putchar('\n');
        }
        if (s->count > o->max) printf("         ... %d more (raise --max)\n", s->count - o->max);
    }
    if (!fr.total) {
        printf("No Matches for %s\n", o->pos[0]);
        srs_find_free(&fr);
        return 1;
    }
    printf("\n%d Matches in %d of %d sections\n", fr.total, fr.nsec, fr.nfiles);
    srs_find_free(&fr);
    return 0;
}

static int cmd_devices(opts *o)
{
    enum srs_bus buses[2];
    int nb = 0, b, unresolved = 0;
    if (strcmp(o->bus, "usb")) buses[nb++] = BUS_PCI;
    if (strcmp(o->bus, "pci")) buses[nb++] = BUS_USB;
    for (b = 0; b < nb; b++) {
        srs_db db;
        srs_dev *devs;
        char recs[32], bus[4];
        int n, i;
        if (srs_db_load(&db, buses[b])) {
            fprintf(stderr, "cannot find %s.ids\n", srs_bus_name(buses[b]));
            return 2;
        }
        n = srs_devices(buses[b], &devs);
        if (n < 0) n = 0;
        snprintf(bus, sizeof bus, "%s", buses[b] == BUS_USB ? "USB" : "PCI");
        srs_thousands(db.n, recs, sizeof recs);
        printf("[%s] %d devices, %s records (%s)\n", bus, n, recs, db.path);
        printf("%-14s %-26s %-20s %-28s Resolved\n", "Slot", "ID", "Matched", "Vendor");
        for (i = 0; i < n; i++) {
            srs_result r;
            char err[128], vend[160], name[600];
            int vendor_only;
            if (srs_resolve(&db, devs[i].key, MODE_TREE, &r, err, sizeof err)) { memset(&r, 0, sizeof r); r.matched = -1; }
            vendor_only = r.matched < 0 || !strchr(r.chain[r.matched], ':');
            snprintf(name, sizeof name, "%s", vendor_only ? "Unknown Device" : r.resolved);
            if (r.matched >= 0 && r.subvendor[0]) {
                const char *m = r.chain[r.matched];
                int colons = 0;
                for (; *m; m++) colons += *m == ':';
                if (colons < 3) {
                    size_t l = strlen(name);
                    snprintf(name + l, sizeof name - l, "  [subsystem: %s]", r.subvendor);
                }
            }
            unresolved += vendor_only;
            srs_utf8_pad(vend, sizeof vend, r.vendor[0] ? r.vendor : "Unknown Vendor", 28);
            printf("%-14s %-26s %-20s %s %s\n", devs[i].slot, devs[i].key,
                   r.matched >= 0 ? r.chain[r.matched] : "-", vend, name);
        }
        putchar('\n');
        free(devs);
        srs_db_free(&db);
    }
    printf("%d device(s) resolved only to vendor or not at all\n", unresolved);
    return 0;
}

#if defined(__linux__) && defined(SRS_HAVE_SECTIONS)
#include "burner.h"
static void cuda_toolkit_hint(void);   /* the NVIDIA CUDA toolkit note after a CUDA run (cuda-toolkit command, below) */
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t g_int;
static void on_int(int s) { (void)s; g_int = 1; }

/* burn [--threads N | --cpus 0,2,5] [--seconds S] [--mode same|swap] [--limit C] [--cool S] [--cycles N] */
/* 2.3.1: the short "use at your own risk" notice of every load test (the GUI asks once, with I Agree / I Disagree) */
static const char g_risk[] = "Use at your own risk: SRS comes without any warranty, and its authors are not responsible for any damage "
                             "(see the licence, GNU GPL v2).";
static const char g_risk_yes[] = "--yes starts without asking; using it counts as agreeing to that.";
static void help_risk(void) { puts(g_risk); puts(g_risk_yes); }
/* 2.4: stop limits 50..100 C as before; 101..110 only with --accept-high-limit (the GUI asks once, Yes / No) */
static const char g_hot_help[] = "--accept-high-limit  allow a --limit of 101..110 C: above 100 C many CPUs and GPUs are already at or near\n"
                                 "                     their own maximum; you accept the risk of running there";
static int limit_ok(const char *cmd, double lim, int accept)
{
    if (lim < 50 || lim > 110) { fprintf(stderr, "%s: --limit must be 50..110 (C)\n", cmd); return 0; }
    if (lim > 100 && !accept) {
        fprintf(stderr, "%s: a --limit over 100 C needs --accept-high-limit (above 100 C many CPUs and GPUs are already at or near "
                        "their own maximum; using it means you accept that risk)\n", cmd);
        return 0;
    }
    return 1;
}

static int cmd_burn(int argc, char **argv)
{
    burn_cfg c;
    burn_status st;
    int i, threads = -1, rc, hot = 0;
    const char *cpus = NULL;
    burn_cfg_default(&c);
    c.burn_s = 10;
    c.cool_s = 0;
    for (i = 2; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            puts("usage: sysreport-search burn [--threads N | --cpus 0,2,5] [--seconds S] [--mode same|swap] [--limit C] [--accept-high-limit]\n"
                 "                             [--cool S] [--cycles N]\n"
                 "  starts at once (no question): makes the chosen CPUs 100 % busy, stops at the temperature limit (default 90 C,\n"
                 "  50..100; up to 110 with --accept-high-limit) or Ctrl+C");
            puts(g_hot_help);
            puts(g_risk);
            puts("Starting it counts as agreeing to that.");
            return 0;
        }
        if (!strcmp(a, "--threads") && v) { threads = atoi(v); i++; }
        else if (!strcmp(a, "--cpus") && v) { cpus = v; i++; }
        else if (!strcmp(a, "--seconds") && v) { c.burn_s = atof(v); i++; }
        else if (!strcmp(a, "--cool") && v) { c.cool_s = atof(v); i++; }
        else if (!strcmp(a, "--cycles") && v) { c.cycles = atoi(v); i++; }
        else if (!strcmp(a, "--limit") && v) { c.temp_limit_c = atof(v); i++; }
        else if (!strcmp(a, "--accept-high-limit")) hot = 1;
        else if (!strcmp(a, "--mode") && v) { c.mode = !strcmp(v, "swap") ? BURN_SWAP : BURN_SAME; i++; }
        else return usage_error("burn [--threads N | --cpus 0,2,5] [--seconds S] [--mode same|swap] [--limit C] [--accept-high-limit] [--cool S] [--cycles N]");
    }
    if (c.burn_s <= 0 || c.burn_s > 750.0 * 3600) return usage_error("--seconds must be 1..2700000");
    if (!limit_ok("burn", c.temp_limit_c, hot)) return 2;
    if (cpus) {
        char tmp[1024], *t;
        memset(c.enabled, 0, sizeof c.enabled);
        snprintf(tmp, sizeof tmp, "%s", cpus);
        for (t = strtok(tmp, ","); t; t = strtok(NULL, ",")) { int n = atoi(t); if (n >= 0 && n < c.ncpu) c.enabled[n] = 1; }
    } else if (threads > 0) {
        for (i = 0; i < c.ncpu; i++) c.enabled[i] = i < threads;
    }
    signal(SIGINT, on_int);
    fprintf(stderr, "%s\n", g_risk);
    burn_tick(&st);                      /* prime the CPU-usage counters */
    if (burn_start(&c)) { fprintf(stderr, "burn: nothing to run (no CPUs selected, already running, or a GPU burn-in / benchmark is running)\n"); return 2; }
    burn_tick(&st);
    printf("%s, %s mode, %.0f s burn, limit %.0f C\n", st.status, c.mode == BURN_SWAP ? "swap" : "same", c.burn_s, c.temp_limit_c);
    while (burn_active()) {
        sleep(1);
        if (g_int) burn_stop();
        burn_tick(&st);
        printf("%6.0fs  load %5.1f%%  %s %.1f C  %s %.1f C  CPU %.0f MHz  GPU %.0f%% %.1f C  %s\n", st.elapsed, st.cur.cpu_load,
               st.cur.cpu_temp_name, st.cur.cpu_temp, st.cur.sio_name, st.cur.sio_temp, st.cur.cpu_mhz, st.cur.gpu_load,
               st.cur.gpu_temp, burn_active() ? (st.state == BURN_COOLING ? "Cooling" : "Burning") : st.status);
        fflush(stdout);
    }
    printf("\n%-8s %-8s %-8s %10s %10s %10s %10s  %s\n", "Thread", "CPU", "Passes", "MWIPS", "Average", "Min", "Max", "Affinity");
    for (i = 0; i < st.nthreads; i++)
        printf("%-8d CPU-%-4d %-8ld %10.0f %10.0f %10.0f %10.0f  %s\n", i, st.th[i].cpu, st.th[i].passes, st.th[i].mwips_last,
               st.th[i].mwips_avg, st.th[i].mwips_min, st.th[i].mwips_max, st.th[i].apic_errors ? "FAIL" : "ok");
    printf("Maximum: load %.1f%%  CPU %.1f C  board %.1f C  %.0f MHz\nStatus: %s\n", st.max.cpu_load, st.max.cpu_temp,
           st.max.sio_temp, st.max.cpu_mhz, st.status);
    {
        char path[1100];
        if (burn_save_summary(&st, path, sizeof path) == 0) printf("Summary saved to %s\n", path);
    }
    rc = st.state == BURN_PASS ? 0 : st.state == BURN_STOPPED ? 130 : 1;
    return rc;
}
#include "bench.h"

/* ask before a benchmark starts: 1 = yes. Without a terminal it does not start (pass --yes). */
static int ask_start(const char *cmd, const char *what)
{
    char ans[16];
    if (!isatty(0)) {
        fprintf(stderr, "%s: not started - %s\n%s\n%*s  Run it from a terminal to be asked, or pass --yes to start without asking "
                "(that counts as agreeing).\n", cmd, what, g_risk, (int)strlen(cmd), "");
        return 0;
    }
    fprintf(stderr, "%s\n%s\nStart now (and agree)? [y/N] ", what, g_risk);
    fflush(stderr);
    if (!fgets(ans, sizeof ans, stdin) || (ans[0] != 'y' && ans[0] != 'Y')) { fprintf(stderr, "%s: not started\n", cmd); return 0; }
    return 1;
}

/* ask with the shared benchmark question (bench_ask_text), wrapped for a terminal: 1 = start */
static int ask_bench(const char *cmd, const bench_ask *a)
{
    char title[200], body[2048], what[2400];
    size_t i, k = 0, col = strlen(cmd) + 16, last = 0;   /* the first line starts after "cmd: not started - " */
    if (bench_ask_text(a, title, sizeof title, body, sizeof body) < 0) return 1;   /* nothing would run: the run says so */
    for (i = 0; body[i] && k + 2 < sizeof what; i++) {   /* wrap at about 100 columns on spaces */
        what[k] = body[i];
        if (body[i] == '\n') col = last = 0;
        else if (body[i] == ' ') last = k;
        if (++col > 100 && last && what[last] == ' ') { what[last] = '\n'; col = k - last; last = 0; }
        k++;
    }
    what[k] = 0;
    return ask_start(cmd, what);
}

/* bench MODE [--seconds S] | ipi [--cpus N] [--rounds R] | latency [--max MB] - explicit runs only; asks first unless --yes */
static int cmd_bench(int argc, char **argv)
{
    srs_out o;
    bench_ask q;
    const char *cmd = argv[1], *mode = NULL;
    double secs = 1;
    long mb = 256;
    int i, m, yes = 0, cpus = 0, rounds = 20000;
    memset(&q, 0, sizeof q);
    so_text_init(&o, stdout);
    for (i = 2; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        int need = (!strcmp(cmd, "bench") && !strcmp(a, "--seconds")) ||
                   (!strcmp(cmd, "ipi") && (!strcmp(a, "--cpus") || !strcmp(a, "--rounds"))) ||
                   (!strcmp(cmd, "latency") && !strcmp(a, "--max"));
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            puts(!strcmp(cmd, "bench") ? "usage: sysreport-search bench {ac,as,dd,dw,ec,ep,lc,lp,pc,pp} [--seconds S] [--yes]" :
                 !strcmp(cmd, "ipi") ? "usage: sysreport-search ipi [--cpus N] [--rounds R] [--yes]" :
                 "usage: sysreport-search latency [--max MB] [--yes]");
            help_risk();
            return 0;
        }
        if (!strcmp(a, "--yes") || !strcmp(a, "-y")) { yes = 1; continue; }
        if (need && !v) { fprintf(stderr, "%s: %s needs a value\n", cmd, a); return 2; }
        if (need) i++;
        if (!strcmp(a, "--seconds") && need) secs = atof(v);
        else if (!strcmp(a, "--cpus") && need) cpus = atoi(v);
        else if (!strcmp(a, "--rounds") && need) rounds = atoi(v);
        else if (!strcmp(a, "--max") && need) mb = atol(v);
        else if (!strcmp(cmd, "bench") && !mode && a[0] != '-') mode = a;
        else { fprintf(stderr, "%s: unknown option '%s'\n", cmd, a); return 2; }
    }
    if (!strcmp(cmd, "bench")) {
        if (!mode) return usage_error("bench {ac,as,dd,dw,ec,ep,lc,lp,pc,pp} [--seconds S]");
        if (secs < 0.1 || secs > 60) return usage_error("--seconds must be 0.1..60");
        for (m = 0; m < BM_COUNT; m++)
            if (!strcmp(mode, bench_mode_id((bench_mode)m) + 10)) {
                q.kind = BA_BENCH; q.mode = m; q.secs = secs;
                if (!yes && !ask_bench("bench", &q)) return 2;
                return bench_run((bench_mode)m, secs, &o) ? 1 : 0;
            }
        return usage_error("bench: unknown mode");
    }
    if (!strcmp(cmd, "ipi")) {
        q.kind = BA_IPI; q.cpus = cpus; q.rounds = rounds;
        if (!yes && !ask_bench("ipi", &q)) return 2;
        i = bench_ipi(cpus, rounds, &o);
        return i < 0 ? 2 : i ? 1 : 0;   /* -1: not started (one CPU: no pair) */
    }
    if (mb < 1 || mb > 4096) return usage_error("--max must be 1..4096 MB");
    q.kind = BA_LAT; q.max_mb = (double)mb;
    if (!yes && !ask_bench("latency", &q)) return 2;
    return bench_latency(mb * 1024 * 1024, &o) ? 1 : 0;
}

/* "1m" -> 1000000, "128k" -> 128000, "1e9" -> 10^9, plain numbers as they are; 0 = not a number */
static double parse_count(const char *s)
{
    char *e;
    double v = strtod(s, &e);
    if (e == s || v <= 0) return 0;
    if (*e == 'k' || *e == 'K') { v *= 1e3; e++; }
    else if (*e == 'm' || *e == 'M') { v *= 1e6; e++; }
    else if (*e == 'g' || *e == 'G') { v *= 1e9; e++; }
    if (*e == 'i' && e > s && strchr("kKmMgG", e[-1])) {   /* binary: 1ki = 1024, 1mi = 1,048,576 (SuperPi sizes) */
        v = v / (e[-1] == 'k' || e[-1] == 'K' ? 1e3 : e[-1] == 'm' || e[-1] == 'M' ? 1e6 : 1e9) *
            (e[-1] == 'k' || e[-1] == 'K' ? 1024.0 : e[-1] == 'm' || e[-1] == 'M' ? 1048576.0 : 1073741824.0);
        e++;
    }
    return *e ? 0 : v;
}

#include "bench_cachemem.h"

/* "a,b,c" -> bit mask over names[]; 0 and a message for an unknown word */
static int cm_parse_list(const char *opt, const char *v, const char *const *names, const int *bits, const char *valid, unsigned *mask)
{
    char w[32];
    while (*v) {
        size_t l = strcspn(v, ","), k;
        int found = 0;
        snprintf(w, sizeof w, "%.*s", (int)(l < sizeof w - 1 ? l : sizeof w - 1), v);
        for (k = 0; names[k]; k++) if (!strcmp(w, names[k])) { *mask |= 1u << bits[k]; found = 1; }
        if (l && !found) { fprintf(stderr, "cachemem: unknown %s value '%s' (valid: %s)\n", opt, w, valid); return 0; }
        v += l + (v[l] == ',');
    }
    if (!*mask) { fprintf(stderr, "cachemem: %s needs a value (valid: %s)\n", opt, valid); return 0; }
    return 1;
}

/* cachemem [options] - Cache & Memory benchmark; asks before it starts unless --yes */
static int cmd_cachemem(int argc, char **argv)
{
    srs_out o;
    cm_cfg c;
    unsigned lvmask = 0, temask = 0;
    int i, lv, te, yes = 0, rc;
    so_text_init(&o, stdout);
    cm_cfg_default(&c);
    for (i = 2; i < argc; i++) {
        static const char *const lvn[] = { "mem", "l1", "l2", "l3", NULL }, *const ten[] = { "read", "write", "copy", "latency", "lat", NULL };
        static const int lvb[] = { CM_MEM, CM_L1, CM_L2, CM_L3 }, teb[] = { CM_READ, CM_WRITE, CM_COPY, CM_LAT, CM_LAT };
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        int need = !strcmp(a, "--threads") || !strcmp(a, "--seconds") || !strcmp(a, "--mem-mib") || !strcmp(a, "--mem-write") ||
                   !strcmp(a, "--isa") || !strcmp(a, "--only") || !strcmp(a, "--tests");
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            puts("usage: sysreport-search cachemem [--threads N] [--seconds S] [--isa avx512|avx2|sse2] [--mem-mib M]\n"
                 "                                 [--only mem,l1,l2,l3] [--tests read,write,copy,latency] [--mib]\n"
                 "                                 [--mem-write auto|clzero|stream] [--yes]");
            help_risk();
            return 0;
        }
        if (need && !v) { fprintf(stderr, "cachemem: %s needs a value\n", a); return 2; }
        if (need) i++;
        if (!strcmp(a, "--threads")) c.threads = atoi(v);
        else if (!strcmp(a, "--seconds")) c.seconds = atof(v);
        else if (!strcmp(a, "--mem-mib")) c.mem_mib = atol(v);
        else if (!strcmp(a, "--mib")) c.mb = 1048576.0;
        else if (!strcmp(a, "--yes") || !strcmp(a, "-y")) yes = 1;
        else if (!strcmp(a, "--mem-write")) {
            if (!strcmp(v, "stream")) c.mem_write = CM_MW_STREAM;
            else if (!strcmp(v, "clzero")) c.mem_write = CM_MW_CLZERO;
            else if (!strcmp(v, "auto")) c.mem_write = CM_MW_AUTO;
            else { fprintf(stderr, "cachemem: unknown --mem-write value '%s' (valid: auto, clzero, stream)\n", v); return 2; }
        } else if (!strcmp(a, "--isa")) {
            if (!strcmp(v, "avx512")) c.isa = CM_ISA_AVX512;
            else if (!strcmp(v, "avx2")) c.isa = CM_ISA_AVX2;
            else if (!strcmp(v, "sse2")) c.isa = CM_ISA_SSE2;
            else { fprintf(stderr, "cachemem: unknown --isa value '%s' (valid: avx512, avx2, sse2)\n", v); return 2; }
        } else if (!strcmp(a, "--only")) {
            if (!cm_parse_list("--only", v, lvn, lvb, "mem, l1, l2, l3", &lvmask)) return 2;
        } else if (!strcmp(a, "--tests")) {
            if (!cm_parse_list("--tests", v, ten, teb, "read, write, copy, latency (or lat)", &temask)) return 2;
        } else { fprintf(stderr, "cachemem: unknown option '%s'\n", a); return 2; }
    }
    if (c.seconds < 0.1 || c.seconds > 60) return usage_error("cachemem: --seconds must be 0.1..60");
    if (lvmask || temask) {
        if (!lvmask) lvmask = 0xF;
        if (!temask) temask = 0xF;
        for (lv = 0; lv < CM_NLEVEL; lv++)
            for (te = 0; te < CM_NTEST; te++)
                if ((lvmask >> lv & 1) && (temask >> te & 1)) c.run_mask |= 1u << (lv * CM_NTEST + te);
    }
    if (!cm_available()) { bench_cachemem(&c, &o, NULL); return 1; }
    if (!yes) {
        char ans[16];
        if (!isatty(0)) {
            fprintf(stderr, "cachemem: not started - it loads every CPU thread and the memory bus for about 20 s.\n"
                            "          Run it from a terminal to be asked, or pass --yes to start without asking.\n");
            return 2;
        }
        fprintf(stderr, "The Cache & Memory benchmark loads every CPU thread and the memory bus for about 20 seconds.\n"
                        "Close other programs (and other benchmarks) for accurate results. Start now? [y/N] ");
        fflush(stderr);
        if (!fgets(ans, sizeof ans, stdin) || (ans[0] != 'y' && ans[0] != 'Y')) { fprintf(stderr, "cachemem: not started\n"); return 2; }
    }
    rc = bench_cachemem(&c, &o, NULL);
    if (rc < 0) fprintf(stderr, "cachemem: could not allocate the test memory\n");
    return rc == 0 ? 0 : 1;
}

/* storage --dir FOLDER ... - explicit runs only; asks first */
static int cmd_storage(int argc, char **argv)
{
    srs_out o;
    storage_cfg c;
    storage_where w;
    char why[300];
    int i, yes = 0, rc;
    memset(&c, 0, sizeof c);
    c.size_mb = 1024;
    c.secs = 5;
    so_text_init(&o, stdout);
    for (i = 2; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        int need = !strcmp(a, "--dir") || !strcmp(a, "--size") || !strcmp(a, "--seconds") || !strcmp(a, "--profile");
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            puts("usage: sysreport-search storage --dir FOLDER [--size MB] [--seconds S] [--profile standard|light] [--allow-usb] [--yes]");
            help_risk();
            return 0;
        }
        if (need && !v) { fprintf(stderr, "storage: %s needs a value\n", a); return 2; }
        if (need) i++;
        if (!strcmp(a, "--dir")) c.dir = v;
        else if (!strcmp(a, "--size")) c.size_mb = atof(v);
        else if (!strcmp(a, "--seconds")) c.secs = atof(v);
        else if (!strcmp(a, "--profile")) {
            if (!strcmp(v, "light")) c.profile = 1;
            else if (!strcmp(v, "standard")) c.profile = 0;
            else return usage_error("storage: --profile is standard or light");
        } else if (!strcmp(a, "--allow-usb")) c.allow_usb = 1;
        else if (!strcmp(a, "--yes") || !strcmp(a, "-y")) yes = 1;
        else { fprintf(stderr, "storage: unknown option '%s'\n", a); return 2; }
    }
    if (!c.dir) return usage_error("storage: --dir FOLDER is required (a folder on the drive to test)");
    if (c.size_mb < 16 || c.size_mb > 65536) return usage_error("storage: --size must be 16..65536 MB");
    if (c.secs < 0.5 || c.secs > 60) return usage_error("storage: --seconds must be 0.5..60");
    if (!storage_probe(c.dir, &w)) { fprintf(stderr, "storage: not started - %s\n", w.problem); return 2; }
    if (w.usb && !c.allow_usb) {
        fprintf(stderr, "storage: not started - %s is on a USB drive (%s). Pass --allow-usb to test it anyway.\n", w.folder, w.model);
        return 2;
    }
    if (!yes) {
        char ans[16];
        if (!isatty(0)) { fprintf(stderr, "storage: not started - run it from a terminal to be asked, or pass --yes\n"); return 2; }
        fprintf(stderr, "Storage benchmark on %s (%s, %s, file system %s).\n"
                        "It writes a new temporary test file of %.0f MB in %s and then reads and rewrites it\n"
                        "for %.0f s per test (6 tests). Existing files are not touched; the test file is removed at the end.\n"
                        "%sStart now? [y/N] ", w.model[0] ? w.model : w.disk, w.transport, w.folder, w.fstype, c.size_mb, w.folder, c.secs,
                w.rotational ? "This is a hard disk: the random tests will be slow and noisy. " : "");
        fflush(stderr);
        if (!fgets(ans, sizeof ans, stdin) || (ans[0] != 'y' && ans[0] != 'Y')) { fprintf(stderr, "storage: not started\n"); return 2; }
    }
    rc = bench_storage(&c, &o, NULL, why, sizeof why);
    if (rc < 0 && why[0]) fprintf(stderr, "storage: %s\n", why);
    return rc == 0 ? 0 : rc == -1 ? 2 : 1;
}

#include "bench_os.h"

/* os-latency [options] - OS latency benchmark; light (one or two CPUs, about 6 s); asks first unless --yes */
static int cmd_os_latency(int argc, char **argv)
{
    srs_out o;
    os_cfg c;
    int i, rc, yes = 0;
    char what[400];
    os_cfg_default(&c);
    so_text_init(&o, stdout);
    for (i = 2; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        int need = !strcmp(a, "--samples") || !strcmp(a, "--tick-seconds") || !strcmp(a, "--only");
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            puts("usage: sysreport-search os-latency [--samples N] [--tick-seconds S]\n"
                 "                                   [--only syscall,switch,spawn,wake,timer,memory,files] [--yes]");
            help_risk();
            return 0;
        }
        if (need && !v) { fprintf(stderr, "os-latency: %s needs a value\n", a); return 2; }
        if (need) i++;
        if (!strcmp(a, "--samples")) c.samples = atol(v);
        else if (!strcmp(a, "--tick-seconds")) c.jitter_secs = atof(v);
        else if (!strcmp(a, "--only")) {
            char buf[256], *t, *sp;
            snprintf(buf, sizeof buf, "%s", v);
            for (t = strtok_r(buf, ",", &sp); t; t = strtok_r(NULL, ",", &sp)) {
                int g;
                for (g = 0; g < OS_NGROUP && strcmp(t, os_group_id(g)); g++) ;
                if (g == OS_NGROUP) {
                    fprintf(stderr, "os-latency: unknown --only value '%s' (valid: syscall, switch, spawn, wake, timer, memory, files)\n", t);
                    return 2;
                }
                c.run_mask |= 1u << g;
            }
            if (!c.run_mask) return usage_error("os-latency: --only needs at least one test group");
        } else if (!strcmp(a, "--yes") || !strcmp(a, "-y")) yes = 1;
        else { fprintf(stderr, "os-latency: unknown option '%s'\n", a); return 2; }
    }
    if (c.samples < 100 || c.samples > 1000000) return usage_error("os-latency: --samples must be 100..1000000");
    if (c.jitter_secs < 0.1 || c.jitter_secs > 60) return usage_error("os-latency: --tick-seconds must be 0.1..60");
    if (!os_available()) { bench_os(&c, NULL, &o, NULL); return 2; }
    snprintf(what, sizeof what, "The OS latency benchmark times system calls, task switches, thread and program starts, wake-ups,\n"
             "timers, page faults and small-file operations for about %.0f s on one or two CPUs (light load).\n"
             "Nothing on the system is changed; the file test uses a temporary folder in ~/.cache/srs that is removed.",
             1 + 2.4 * (double)c.samples / 20000 + (!c.run_mask || (c.run_mask >> OS_G_TIMER & 1) ? c.jitter_secs : 0));
    if (!yes && !ask_start("os-latency", what)) return 2;
    rc = bench_os(&c, NULL, &o, NULL);
    return rc == 0 ? 0 : rc == 1 ? 1 : 2;
}

#include "bench_compute.h"

/* compute [options] - CPU Compute benchmark; asks first unless --yes */
static int cmd_compute(int argc, char **argv)
{
    srs_out o;
    cp_cfg c;
    int i, rc, yes = 0;
    char what[600];
    cp_cfg_default(&c);
    so_text_init(&o, stdout);
    for (i = 2; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        int need = !strcmp(a, "--threads") || !strcmp(a, "--seconds") || !strcmp(a, "--tests") || !strcmp(a, "--code");
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            puts("usage: sysreport-search compute [--threads N] [--seconds S]\n"
                 "                                [--tests sgemm,dgemm,fma-sp,fma-dp,fft-sp,fft-dp,nbody,mandelbrot,raytrace]\n"
                 "                                [--code auto|c|sse2|avx2|avx512] [--single | --all-threads-only] [--yes]\n"
                 "  --all-threads-only  skip the 1-thread pass (the GUI's All Cores): only the N-threads column, about half the time");
            help_risk();
            return 0;
        }
        if (need && !v) { fprintf(stderr, "compute: %s needs a value\n", a); return 2; }
        if (need) i++;
        if (!strcmp(a, "--threads")) c.threads = atoi(v);
        else if (!strcmp(a, "--seconds")) c.seconds = atof(v);
        else if (!strcmp(a, "--single")) c.single_only = 1;
        else if (!strcmp(a, "--all-threads-only")) c.multi_only = 1;
        else if (!strcmp(a, "--yes") || !strcmp(a, "-y")) yes = 1;
        else if (!strcmp(a, "--code")) {
            int k;
            for (k = 0; k < CP_NISA && strcmp(v, cp_isa_id(k)); k++) ;
            if (k == CP_NISA) { fprintf(stderr, "compute: unknown --code value '%s' (valid: auto, c, sse2, avx2, avx512)\n", v); return 2; }
            c.isa = k;
        } else if (!strcmp(a, "--tests")) {
            char buf[256], *t, *sp;
            snprintf(buf, sizeof buf, "%s", v);
            for (t = strtok_r(buf, ",", &sp); t; t = strtok_r(NULL, ",", &sp)) {
                int k;
                for (k = 0; k < CP_NTEST && strcmp(t, cp_test_id[k]); k++) ;
                if (k == CP_NTEST) {
                    fprintf(stderr, "compute: unknown --tests value '%s' (valid: sgemm, dgemm, fma-sp, fma-dp, fft-sp, fft-dp, nbody, mandelbrot, raytrace)\n", t);
                    return 2;
                }
                c.run_mask |= 1u << k;
            }
            if (!c.run_mask) return usage_error("compute: --tests needs at least one test");
        } else { fprintf(stderr, "compute: unknown option '%s'\n", a); return 2; }
    }
    if (c.seconds < 0.1 || c.seconds > 60) return usage_error("compute: --seconds must be 0.1..60");
    if (c.threads < 0 || c.threads > BURN_MAXCPU) return usage_error("compute: --threads must be 1 or more (0 = all)");
    if (c.single_only && c.multi_only) return usage_error("compute: --single and --all-threads-only exclude each other");
    if (!cp_available()) { bench_compute(&c, &o, NULL); return 2; }
    snprintf(what, sizeof what, "CPU Compute runs matrix multiply, multiply-add, FFT, N-body, Mandelbrot and a ray tracer for about %.0f s,\n"
             "%s%s\nAn unstable overclock or undervolt can crash the PC under this load (--single keeps it light).",
             cp_estimate_seconds(&c), c.single_only ? "on one thread only." : c.multi_only ? "on all chosen threads at once only (the 1-thread pass is skipped): every CPU 100% busy."
             : "on one thread and then on all chosen threads at once: every CPU 100% busy.",
             cp_isa_best() == CP_ISA_AVX512 && (c.isa == CP_ISA_AUTO || c.isa == CP_ISA_AVX512) ? " It uses AVX-512, which draws a lot of power." : "");
    if (!yes && !ask_start("compute", what)) return 2;
    rc = bench_compute(&c, &o, NULL);
    if (rc == -1) fprintf(stderr, "compute: not started - Stress & Burnin is running, or out of memory\n");
    return rc == 0 ? 0 : rc == -1 ? 2 : 1;
}

#include "bench_work.h"

/* "a,b" -> bits over ids[0..n-1]; 0 and a message for an unknown word */
static int parse_ids(const char *cmd, const char *opt, const char *v, const char *const *ids, int n, unsigned *mask)
{
    char buf[256], *t, *sp;
    snprintf(buf, sizeof buf, "%s", v);
    for (t = strtok_r(buf, ",", &sp); t; t = strtok_r(NULL, ",", &sp)) {
        int k;
        for (k = 0; k < n && strcmp(t, ids[k]); k++) ;
        if (k == n) {
            int i;
            fprintf(stderr, "%s: unknown %s value '%s' (valid:", cmd, opt, t);
            for (i = 0; i < n; i++) fprintf(stderr, "%s %s", i ? "," : "", ids[i]);
            fprintf(stderr, ")\n");
            return 0;
        }
        *mask |= 1u << k;
    }
    if (!*mask) { fprintf(stderr, "%s: %s needs a value\n", cmd, opt); return 0; }
    return 1;
}

/* work [options] - Everyday work benchmark; asks first unless --yes */
static int cmd_work(int argc, char **argv)
{
    srs_out o;
    wk_cfg c;
    int i, rc, yes = 0;
    char what[600];
    wk_cfg_default(&c);
    so_text_init(&o, stdout);
    for (i = 2; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        int need = !strcmp(a, "--threads") || !strcmp(a, "--seconds") || !strcmp(a, "--only") || !strcmp(a, "--libs") ||
                   !strcmp(a, "--part-kib");
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            puts("usage: sysreport-search work [--threads N] [--seconds S] [--only compress,sort,hash,text,interp]\n"
                 "                             [--libs zlib,zstd,lz4,xz] [--single | --all-threads-only] [--part-kib K] [--yes]\n"
                 "  --all-threads-only  skip the 1-thread pass (the GUI's All Cores): only the N-threads column, about half the time");
            help_risk();
            return 0;
        }
        if (need && !v) { fprintf(stderr, "work: %s needs a value\n", a); return 2; }
        if (need) i++;
        if (!strcmp(a, "--threads")) c.threads = atoi(v);
        else if (!strcmp(a, "--seconds")) c.seconds = atof(v);
        else if (!strcmp(a, "--part-kib")) c.part_kib = atol(v);
        else if (!strcmp(a, "--single")) c.single_only = 1;
        else if (!strcmp(a, "--all-threads-only")) c.multi_only = 1;
        else if (!strcmp(a, "--yes") || !strcmp(a, "-y")) yes = 1;
        else if (!strcmp(a, "--only")) { if (!parse_ids("work", "--only", v, wk_group_id, WK_NGROUP, &c.run_mask)) return 2; }
        else if (!strcmp(a, "--libs")) { if (!parse_ids("work", "--libs", v, wk_lib_id, WK_NLIB, &c.codec_mask)) return 2; }
        else { fprintf(stderr, "work: unknown option '%s'\n", a); return 2; }
    }
    if (c.seconds < 0.1 || c.seconds > 60) return usage_error("work: --seconds must be 0.1..60");
    if (c.part_kib < 64 || c.part_kib > 16384) return usage_error("work: --part-kib must be 64..16384");
    if (c.threads < 0 || c.threads > BURN_MAXCPU) return usage_error("work: --threads must be 1 or more (0 = all)");
    if (c.single_only && c.multi_only) return usage_error("work: --single and --all-threads-only exclude each other");
    if (!wk_available()) { bench_work(&c, &o, NULL); return 2; }
    snprintf(what, sizeof what, "Everyday work runs compression, sorting, checksum, text and interpreter tests for about %.0f s,\n"
             "%s\nThe test data is generated; none of your files are read or written (only the result is saved).",
             wk_estimate_seconds(&c), c.single_only ? "on one thread only." : c.multi_only ?
             "on all chosen threads at once only (the 1-thread pass is skipped): every CPU 100% busy, up to ~100 MB of memory per thread." :
             "on one thread and then on all chosen threads at once: every CPU 100% busy, up to ~100 MB of memory per thread.");
    if (!yes && !ask_start("work", what)) return 2;
    rc = bench_work(&c, &o, NULL);
    if (rc == -1) fprintf(stderr, "work: not started - Stress & Burnin or another run is active, or out of memory\n");
    return rc == 0 ? 0 : rc == -1 ? 2 : 1;
}

#include "bench_net.h"

/* net [--local] [--internet] ... - loopback by default; asks first; the internet part asks separately */
static int cmd_net(int argc, char **argv)
{
    static const char usage[] =
        "usage: sysreport-search net [--local] [--internet] [--seconds S] [--streams N] [--samples N] [--max-mb MB] [-4|-6]\n"
        "                            [--server URL] [--hosts HOST[:PORT],...] [--yes] [--yes-internet]\n"
        "  --local         loopback TCP / UDP speed and round trip (the default; nothing leaves the computer)\n"
        "  --internet      DNS, connect, HTTPS, download and upload tests against speed.cloudflare.com\n"
        "  --yes           start the loopback test without asking\n"
        "  --yes-internet  start the internet test without asking (it uses up to ~4 x --max-mb MB of data)";
    srs_out o;
    net_cfg c;
    net_where w;
    char why[300], ask[2048], what[500];
    int i, yes = 0, yes_inet = 0, rc, chose = 0;
    net_cfg_default(&c);
    c.local = 0;
    so_text_init(&o, stdout);
    for (i = 2; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        int need = !strcmp(a, "--seconds") || !strcmp(a, "--streams") || !strcmp(a, "--samples") || !strcmp(a, "--max-mb") ||
                   !strcmp(a, "--server") || !strcmp(a, "--hosts");
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { puts(usage); help_risk(); return 0; }
        if (need && !v) { fprintf(stderr, "net: %s needs a value\n", a); return 2; }
        if (need) i++;
        if (!strcmp(a, "--local")) { c.local = 1; chose = 1; }
        else if (!strcmp(a, "--internet")) { c.internet = 1; chose = 1; }
        else if (!strcmp(a, "--seconds")) c.secs = atof(v);
        else if (!strcmp(a, "--streams")) c.streams = atoi(v);
        else if (!strcmp(a, "--samples")) c.samples = atoi(v);
        else if (!strcmp(a, "--max-mb")) c.max_mb = atof(v);
        else if (!strcmp(a, "--server")) c.server = v;
        else if (!strcmp(a, "--hosts")) c.hosts = v;
        else if (!strcmp(a, "-4")) c.ipv = 4;
        else if (!strcmp(a, "-6")) c.ipv = 6;
        else if (!strcmp(a, "--yes") || !strcmp(a, "-y")) yes = 1;
        else if (!strcmp(a, "--yes-internet")) yes_inet = 1;
        else { fprintf(stderr, "net: unknown option '%s'\n", a); return 2; }
    }
    if (!chose) c.local = 1;   /* never the internet unless asked for */
    if (c.secs < 0.1 || c.secs > 60) return usage_error("net: --seconds must be 0.1..60");
    if (c.streams < 1 || c.streams > NET_MAX_STREAMS) return usage_error("net: --streams must be 1..32");
    if (c.samples < 1 || c.samples > 100) return usage_error("net: --samples must be 1..100");
    if (c.max_mb < 0) return usage_error("net: --max-mb must be 0 (no cap) or more");
    if (c.internet && !net_probe(&c, &w)) { fprintf(stderr, "net: internet test not started - %s\n", w.problem); return 2; }
    if (c.local) {
        snprintf(what, sizeof what, "The network test on this computer sends data between two programs on this PC (loopback, 127.0.0.1):\n"
                 "TCP speed with 1 and %d streams, UDP speed and loss, round trip time - about %.0f s, up to %d CPUs busy.\n"
                 "Nothing leaves the computer.%s", c.streams, 3 * c.secs + 2 * (c.secs < 1 ? c.secs : 1) + 1, c.streams + 1,
                 c.internet ? " (The internet test is asked about separately.)" : "");
        if (!yes && !ask_start("net", what)) return 2;
    }
    if (c.internet) {   /* its own question: --yes does not answer it */
        net_ask_text(&c, &w, ask, sizeof ask);
        fprintf(stderr, "%s\n", ask);   /* what it contacts and how much data, also with --yes-internet */
        if (!yes_inet) {
            char ans[16];
            if (!isatty(0)) {
                fprintf(stderr, "net: internet test not started - run it from a terminal to be asked, or pass --yes-internet\n");
                return 2;
            }
            fprintf(stderr, "Run the internet speed test now? [y/N] ");
            fflush(stderr);
            if (!fgets(ans, sizeof ans, stdin) || (ans[0] != 'y' && ans[0] != 'Y')) {
                fprintf(stderr, "net: internet test not started\n");
                if (!c.local) return 2;
                c.internet = 0;
            }
        }
    }
    why[0] = 0;
    rc = bench_net(&c, &o, NULL, why, sizeof why);
    if (rc < 0 && why[0]) fprintf(stderr, "net: %s\n", why);
    return rc == 0 ? 0 : rc == -1 ? 2 : 1;
}

#include "bench_gpu.h"

/* parse "--device N" for the GPU commands; -1 = default */
static int gpu_parse_device(const char *cmd, const char *v, int *dev)
{
    char *e;
    long n = strtol(v, &e, 10);
    if (*e || n < 0 || n >= GPU_MAXDEV) { fprintf(stderr, "%s: --device must be a number 0..%d (see --list)\n", cmd, GPU_MAXDEV - 1); return 0; }
    *dev = (int)n;
    return 1;
}

/* gpu [--device N] [--seconds S] [--only ...] [--mem-mib M] [--list] [--yes] - Vulkan compute benchmark; asks first */
static int cmd_gpu(int argc, char **argv)
{
    static const char usage[] =
        "usage: sysreport-search gpu [--device N] [--seconds S] [--only fp32,fp16,fp64,int32,read,write,copy,upload,download,latency]\n"
        "                            [--mem-mib M] [--list] [--yes]\n"
        "  --device N   the Vulkan device to test (number from --list; default: the first discrete GPU)\n"
        "  --seconds S  GPU time per test, 0.1..60 (default 1)\n"
        "  --only ...   run only these tests (comma-separated)\n"
        "  --mem-mib M  buffer size of the memory and transfer tests in MiB, 1..4096 (default: 256, less on small cards)\n"
        "  --list       list the Vulkan devices and exit (runs nothing on the GPU)\n"
        "  --yes        start without asking";
    srs_out o;
    gpu_cfg c;
    gpu_dev d[GPU_MAXDEV];
    char why[300], what[900];
    int i, yes = 0, list = 0, rc, n, k, ntests = 0;
    gpu_cfg_default(&c);
    so_text_init(&o, stdout);
    for (i = 2; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        int need = !strcmp(a, "--device") || !strcmp(a, "--seconds") || !strcmp(a, "--only") || !strcmp(a, "--mem-mib");
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { puts(usage); help_risk(); return 0; }
        if (need && !v) { fprintf(stderr, "gpu: %s needs a value\n", a); return 2; }
        if (need) i++;
        if (!strcmp(a, "--device")) { if (!gpu_parse_device("gpu", v, &c.device)) return 2; }
        else if (!strcmp(a, "--seconds")) c.seconds = atof(v);
        else if (!strcmp(a, "--mem-mib")) c.mem_mib = atof(v);
        else if (!strcmp(a, "--list")) list = 1;
        else if (!strcmp(a, "--yes") || !strcmp(a, "-y")) yes = 1;
        else if (!strcmp(a, "--only")) {
            char buf[256], *t, *sp;
            snprintf(buf, sizeof buf, "%s", v);
            for (t = strtok_r(buf, ",", &sp); t; t = strtok_r(NULL, ",", &sp)) {
                for (k = 0; k < GT_N && strcmp(t, gpu_test_id[k]); k++) ;
                if (k == GT_N) {
                    fprintf(stderr, "gpu: unknown --only value '%s' (valid: fp32, fp16, fp64, int32, read, write, copy, upload, download, latency)\n", t);
                    return 2;
                }
                c.tests |= 1u << k;
            }
            if (!c.tests) return usage_error("gpu: --only needs at least one test");
        } else { fprintf(stderr, "gpu: unknown option '%s'\n", a); return 2; }
    }
    if (c.seconds < 0.1 || c.seconds > 60) return usage_error("gpu: --seconds must be 0.1..60");
    if (c.mem_mib < 0 || c.mem_mib > 4096) return usage_error("gpu: --mem-mib must be 1..4096 (0 = auto)");
    gpu_quiet_layers();   /* main thread, before any Vulkan call: no overlays (MangoHud ...) in the run */
    if (getenv("SRS_GPU_SOFTWARE_ONLY") && !strcmp(getenv("SRS_GPU_SOFTWARE_ONLY"), "1")) c.software_only = 1;
    n = gpu_list(d, GPU_MAXDEV, why, sizeof why);
    if (n < 0) { fprintf(stderr, "gpu: not available - %s\n", why); return 2; }
    if (list) {
        if (n == 0) printf("No Vulkan device found%s%s\n", why[0] ? ": " : "", why);
        for (i = 0; i < n; i++)
            printf("%d  %s  (%s, %.1f GB video memory%s%s)\n", i, d[i].name, d[i].type, d[i].vram_bytes / 1e9,
                   d[i].driver[0] ? ", " : "", d[i].driver);
        return 0;
    }
    if (n == 0) { fprintf(stderr, "gpu: not started - no Vulkan device found%s%s\n", why[0] ? ": " : "", why); return 2; }
    if (c.device >= n) { fprintf(stderr, "gpu: not started - there is no device %d (see --list)\n", c.device); return 2; }
    k = c.device >= 0 ? c.device : gpu_default_device(d, n);
    for (i = 0; i < GT_N; i++) if (!c.tests || (c.tests >> i & 1)) ntests++;
    snprintf(what, sizeof what, "GPU compute benchmark on %s: runs short GPU kernels and copies for about %.0f s\n"
             "(%d tests, %.1f s of GPU time each, in pieces of about 25 ms). The GPU works at full load while a test runs.\n"
             "An unstable GPU overclock or undervolt can crash the PC or the display driver under this load.",
             d[k].name, ntests * c.seconds * 1.5 + 2, ntests, c.seconds);
    if (!yes && !ask_start("gpu", what)) return 2;
    why[0] = 0;
    rc = bench_gpu(&c, &o, NULL, why, sizeof why);
    if (rc < 0 && why[0]) fprintf(stderr, "gpu: %s%s\n", rc == -1 ? "not started - " : "", why);
    return rc == 0 ? 0 : rc == -1 ? 2 : 1;
}

#include "bench_gpuburn.h"

static volatile int g_gpu_cancel;
static void on_int_gpu(int s) { (void)s; g_gpu_cancel = 1; }

/* "-" when the sensor is not available, else "47 C" */
static const char *gb_c(char *b, size_t n, double v, const char *unit)
{
    if (v < 0) snprintf(b, n, "-");
    else snprintf(b, n, "%.0f %s", v, unit);
    return b;
}

/* live line on a terminal, about twice a second (called on the thread that runs bench_gpuburn) */
static void gpuburn_live(const gpuburn_snap *s, void *ud)
{
    char t[24], j[24], p[24], c[24];
    long err = 0;
    int i;
    const gpuburn_dev *d = &s->dev[0];
    (void)ud;
    if (!s->ndev) return;
    for (i = 0; i < s->ndev; i++) err += s->dev[i].errors;
    fprintf(stderr, "\r%5.0f / %.0f s  %s%s: %7.1f GFLOPS  %ld error%s  edge %s  hotspot %s  %s  %s%s   ",
            s->elapsed, s->total, d->label, s->ndev > 1 ? " (+ more)" : "", d->gflops, err, err == 1 ? "" : "s",
            gb_c(t, sizeof t, d->now.edge_c, "C"), gb_c(j, sizeof j, d->now.junction_c, "C"),
            gb_c(p, sizeof p, d->now.power_w, "W"), gb_c(c, sizeof c, d->now.clock_mhz, "MHz"),
            d->no_temp ? "  (no temperature sensor: limit not active)" : "");
    fflush(stderr);
}

/* gpu-burn [--seconds N] [--backend ...] [--device N|all] [--fp32|--fp64] [--limit C] [--list] [--yes] */
static int cmd_gpu_burn(int argc, char **argv)
{
    static const char usage[] =
        "usage: sysreport-search gpu-burn [--seconds N] [--backend vulkan|hip|cuda|opencl|auto] [--device N|all]\n"
        "                                 [--fp32|--fp64] [--limit C] [--accept-high-limit] [--list] [--yes]\n"
        "  --seconds N   how long, 1..86400 (default 60)\n"
        "  --backend B   vulkan, hip (ROCm), cuda, opencl or auto (default: Vulkan when it has a GPU)\n"
        "  --device N    the device number of that back end (see --list), or all (default: the first)\n"
        "  --fp32/--fp64 the precision of the matrix multiplications (default fp32)\n"
        "  --limit C     stop when the GPU reaches this temperature in C, 50..100 (default 100;\n"
        "                hotspot / junction when the card reports it, else edge); up to 110 with\n"
        "  --accept-high-limit  (above 100 C many GPUs are already at or near their own maximum)\n"
        "  --list        list the devices of each installed back end and exit (starts each runtime;\n"
        "                runs no work on a GPU)\n"
        "  --yes         start without asking\n"
        "GPU Burnin never changes clocks, voltages or fans. Ctrl+C stops it.";
    srs_out o;
    gpuburn_cfg c;
    char why[300], what[1200], dev[300], bk[80];
    int i, yes = 0, list = 0, rc, hot = 0;
    gpuburn_cfg_default(&c);
    so_text_init(&o, stdout);
    for (i = 2; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        int need = !strcmp(a, "--seconds") || !strcmp(a, "--backend") || !strcmp(a, "--device") || !strcmp(a, "--limit") ||
                   !strcmp(a, "--limit-c");
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { puts(usage); help_risk(); return 0; }
        if (need && !v) { fprintf(stderr, "gpu-burn: %s needs a value\n", a); return 2; }
        if (need) i++;
        if (!strcmp(a, "--seconds")) c.seconds = atof(v);
        else if (!strcmp(a, "--limit") || !strcmp(a, "--limit-c")) c.limit_c = atof(v);
        else if (!strcmp(a, "--fp32")) c.prec = GP_FP32;
        else if (!strcmp(a, "--fp64")) c.prec = GP_FP64;
        else if (!strcmp(a, "--list")) list = 1;
        else if (!strcmp(a, "--yes") || !strcmp(a, "-y")) yes = 1;
        else if (!strcmp(a, "--accept-high-limit")) hot = 1;
        else if (!strcmp(a, "--device")) {
            if (!strcmp(v, "all")) c.device = GPUBURN_ALL;
            else if (!gpu_parse_device("gpu-burn", v, &c.device)) return 2;
        } else if (!strcmp(a, "--backend")) {
            int k;
            if (!strcmp(v, "auto")) c.backend = GPUBURN_AUTO;
            else {
                for (k = 0; k < GB_N && strcmp(v, gb_id[k]); k++) ;
                if (k == GB_N) { fprintf(stderr, "gpu-burn: unknown --backend '%s' (valid: vulkan, hip, cuda, opencl, auto)\n", v); return 2; }
                c.backend = k;
            }
        } else { fprintf(stderr, "gpu-burn: unknown option '%s'\n", a); return 2; }
    }
    if (c.seconds < 1 || c.seconds > 86400) return usage_error("gpu-burn: --seconds must be 1..86400");
    if (!limit_ok("gpu-burn", c.limit_c, hot)) return 2;
    gpu_quiet_layers();
    if (getenv("SRS_GPU_SOFTWARE_ONLY") && !strcmp(getenv("SRS_GPU_SOFTWARE_ONLY"), "1")) c.software_only = 1;
    if (list) {
        for (i = 0; i < GB_N; i++) {
            gb_info l[GB_LISTMAX];
            int n, k;
            if (c.backend != GPUBURN_AUTO && c.backend != i) continue;
            n = gpuburn_list(i, l, GB_LISTMAX, why, sizeof why);
            printf("%s:", gb_name[i]);
            if (n < 0) { printf(" %s\n", why); continue; }
            if (n == 0) { printf(" no device%s%s\n", why[0] ? " - " : "", why); continue; }
            putchar('\n');
            for (k = 0; k < n; k++)
                printf("  %d  %s  (%s, %s%s%s)\n", k, l[k].device, l[k].label, l[k].kind, l[k].runtime[0] ? ", " : "", l[k].runtime);
        }
        return 0;
    }
    /* name the device without starting a HIP / CUDA / OpenCL runtime before the user said yes:
       only the Vulkan device list (no work is sent) is read here */
    snprintf(bk, sizeof bk, "%s", c.backend == GPUBURN_AUTO ? "Vulkan (or the first back end that has a GPU)" : gb_name[c.backend]);
    if (c.device == GPUBURN_ALL) snprintf(dev, sizeof dev, "every GPU of that back end");
    else if (c.device >= 0) snprintf(dev, sizeof dev, "device %d", c.device);
    else snprintf(dev, sizeof dev, "the first GPU");
    if (c.backend == GB_VULKAN || c.backend == GPUBURN_AUTO) {
        gpu_dev d[GPU_MAXDEV];
        int n = gpu_list(d, GPU_MAXDEV, why, sizeof why);
        int k, gpu = -1;
        for (k = 0; k < n; k++) if (d[k].dtype != GPU_CPU) { gpu = k; break; }   /* the default skips software renderers */
        if (n > 0 && c.device >= 0 && c.device < n) snprintf(dev, sizeof dev, "%.250s", d[c.device].name);
        else if (n > 0 && c.device == -1 && (gpu >= 0 || c.software_only)) snprintf(dev, sizeof dev, "%.250s", d[gpu >= 0 ? gpu : 0].name);
        if (n > 0 && gpu < 0 && !c.software_only && c.backend == GB_VULKAN) {
            fprintf(stderr, "gpu-burn: not started - Vulkan lists no GPU, only %s (a software renderer, which GPU Burnin does not burn)\n", d[0].name);
            return 2;
        }
        if (n <= 0 && c.backend == GB_VULKAN) {
            fprintf(stderr, "gpu-burn: not started - no Vulkan device: %s\n", why[0] ? why : "none found");
            return 2;
        }
    }
    snprintf(what, sizeof what,
             "GPU Burnin on %s via %s for %.0f s: keeps the GPU fully busy with large %s matrix multiplications\n"
             "(high power draw and heat; the fans will speed up) and counts compute errors.\n"
             "It stops by itself at %.0f C, on a driver error, when the GPU stops answering, or on Ctrl+C.\n"
             "It never changes clocks, voltages or fans. A bad GPU overclock or undervolt can crash the PC under this load,\n"
             "and a weak power supply or poor cooling can make it shut down.",
             dev, bk, c.seconds, c.prec == GP_FP64 ? "FP64" : "FP32", c.limit_c);
    if (!yes && !ask_start("gpu-burn", what)) return 2;
    if (isatty(2)) c.cb = gpuburn_live;
    signal(SIGINT, on_int_gpu);
    why[0] = 0;
    rc = bench_gpuburn(&c, &o, &g_gpu_cancel, why, sizeof why);
    signal(SIGINT, SIG_DFL);
    if (c.cb) fputc('\n', stderr);
    if (rc < 0 && why[0]) fprintf(stderr, "gpu-burn: %s%s\n", rc == -1 ? "not started - " : "ended early - ", why);
    else if (rc >= 0 && why[0]) fprintf(stderr, "gpu-burn: note - %s\n", why);   /* All GPUs: a card that could not start */
    if (c.backend == GB_CUDA) cuda_toolkit_hint();
    return rc == 0 ? 0 : rc == -1 ? 2 : 1;
}

#include "bench_gpucompare.h"

/* gpu-compare [--backends vulkan,hip,cuda,opencl] [--seconds S] [--device N] [--yes] */
static int cmd_gpu_compare(int argc, char **argv)
{
    static const char usage[] =
        "usage: sysreport-search gpu-compare [--backends vulkan,hip,cuda,opencl] [--seconds S] [--device N] [--yes]\n"
        "  --backends L  the back ends to compare (comma-separated; default: every installed one);\n"
        "                --backend B is the same for one\n"
        "  --seconds S   GPU time per test, 0.1..60 (default 1)\n"
        "  --device N    the Vulkan device that names the GPU (see gpu --list); the other back ends\n"
        "                use the same GPU, found by its PCI address (default: the first discrete GPU)\n"
        "  --yes         start without asking";
    srs_out o;
    gpucmp_cfg c;
    char why[300], what[1400], list[200] = "", hint[300], dev[300] = "the default GPU";
    int i, yes = 0, rc, nb = 0;
    gpucmp_cfg_default(&c);
    so_text_init(&o, stdout);
    for (i = 2; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        int need = !strcmp(a, "--backends") || !strcmp(a, "--backend") || !strcmp(a, "--seconds") || !strcmp(a, "--device");
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { puts(usage); help_risk(); return 0; }
        if (need && !v) { fprintf(stderr, "gpu-compare: %s needs a value\n", a); return 2; }
        if (need) i++;
        if (!strcmp(a, "--seconds")) c.seconds = atof(v);
        else if (!strcmp(a, "--device")) { if (!gpu_parse_device("gpu-compare", v, &c.device)) return 2; }
        else if (!strcmp(a, "--yes") || !strcmp(a, "-y")) yes = 1;
        else if (!strcmp(a, "--backends") || !strcmp(a, "--backend")) {
            char buf[256], *t, *sp;
            snprintf(buf, sizeof buf, "%s", v);
            for (t = strtok_r(buf, ",", &sp); t; t = strtok_r(NULL, ",", &sp)) {
                int k;
                if (!strcmp(t, "auto") || !strcmp(t, "all")) { c.backends = 0; continue; }
                for (k = 0; k < GB_N && strcmp(t, gb_id[k]); k++) ;
                if (k == GB_N) { fprintf(stderr, "gpu-compare: unknown back end '%s' (valid: vulkan, hip, cuda, opencl)\n", t); return 2; }
                c.backends |= 1u << k;
            }
        } else { fprintf(stderr, "gpu-compare: unknown option '%s'\n", a); return 2; }
    }
    if (c.seconds < 0.1 || c.seconds > 60) return usage_error("gpu-compare: --seconds must be 0.1..60");
    gpu_quiet_layers();
    if (getenv("SRS_GPU_SOFTWARE_ONLY") && !strcmp(getenv("SRS_GPU_SOFTWARE_ONLY"), "1")) c.software_only = 1;
    for (i = 0; i < GB_N; i++) {   /* dlopen only: which back ends are installed */
        if (c.backends && !(c.backends >> i & 1)) continue;
        if (!gb_table[i]->available(hint, sizeof hint)) continue;
        snprintf(list + strlen(list), sizeof list - strlen(list), "%s%s", nb ? ", " : "", gb_name[i]);
        nb++;
    }
    if (!nb) { fprintf(stderr, "gpu-compare: not started - none of the chosen back ends is installed\n"); return 2; }
    {
        gpu_dev d[GPU_MAXDEV];
        int n = gpu_list(d, GPU_MAXDEV, why, sizeof why);
        if (n > 0) {
            int k = c.device >= 0 && c.device < n ? c.device : gpu_default_device(d, n);
            snprintf(dev, sizeof dev, "%.250s", d[k].name);
        }
    }
    snprintf(what, sizeof what,
             "GPU Compute comparison: runs the same tests (matrix multiply FP32 / FP16 / FP64 with the own kernel and the\n"
             "vendor library, multiply-add, memory copy, upload, download, launch time) through %s on %s,\n"
             "about %.1f s of GPU time per test and back end - up to about %.0f s in all, at full GPU load.\n"
             "HIP, CUDA and OpenCL start their own runtimes. An unstable GPU overclock or undervolt can crash the PC under this load.",
             list, dev, c.seconds, GC_N * nb * c.seconds * 1.5 + 3);
    if (!yes && !ask_start("gpu-compare", what)) return 2;
    signal(SIGINT, on_int_gpu);
    g_gpu_cancel = 0;
    why[0] = 0;
    rc = bench_gpucompare(&c, NULL, &o, &g_gpu_cancel, why, sizeof why);
    signal(SIGINT, SIG_DFL);
    if (rc < 0 && why[0]) fprintf(stderr, "gpu-compare: %s%s\n", rc == -1 ? "not started - " : "", why);
    if (strstr(list, gb_name[GB_CUDA])) cuda_toolkit_hint();
    return rc == 0 ? 0 : rc == -1 ? 2 : 1;
}

/* pi | crypto | prime - explicit runs only; asks first unless --yes */
static int cmd_bench2(int argc, char **argv)
{
    srs_out o;
    bench_ask q;
    int i, threads = 0, yes = 0;
    memset(&q, 0, sizeof q);
    so_text_init(&o, stdout);
    if (!strcmp(argv[1], "pi")) {
        double digits = 1e6;
        int method = PI_CHUDNOVSKY, rc;
        const char *save = NULL;
        char *d = NULL;
        threads = 1;
        for (i = 2; i < argc; i++) {
            const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
            if (!strcmp(a, "--help") || !strcmp(a, "-h")) { puts("usage: sysreport-search pi [--digits 128k|1m|..|32m|16ki|..|1mi|..|32mi|N] [--threads N] [--method chudnovsky|agm] [--save-digits FILE] [--yes]"); help_risk(); return 0; }
            if (!strcmp(a, "--yes") || !strcmp(a, "-y")) { yes = 1; continue; }
            if (!v) return usage_error("pi: option needs a value");
            if (!strcmp(a, "--digits")) digits = parse_count(v);
            else if (!strcmp(a, "--threads")) threads = atoi(v);
            else if (!strcmp(a, "--method")) {
                if (!strcmp(v, "agm") || !strcmp(v, "gauss-legendre")) method = PI_AGM;
                else if (!strcmp(v, "chudnovsky")) method = PI_CHUDNOVSKY;
                else return usage_error("pi: --method is chudnovsky or agm");
            } else if (!strcmp(a, "--save-digits")) save = v;
            else return usage_error("pi: unknown option");
            i++;
        }
        if (digits < 100 || digits > PI_MAXDIGITS) return usage_error("pi: --digits must be 100..32mi");
        q.kind = BA_PI; q.digits = digits; q.threads = threads; q.algo = method;
        if (!yes && !ask_bench("pi", &q)) return 2;
        rc = bench_pi((size_t)digits, threads, method, &o, NULL, save ? &d : NULL);
        if (rc == -3) fprintf(stderr, "pi: out of memory\n");
        if (d && save) {
            FILE *f = fopen(save, "w");
            if (f) { fputs(d, f); fclose(f); } else fprintf(stderr, "pi: cannot write %s\n", save);
        }
        free(d);
        return rc == 0 ? 0 : 1;
    }
    if (!strcmp(argv[1], "crypto")) {
        double secs = 1;
        int all_only = 0;
        for (i = 2; i < argc; i++) {
            const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
            if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
                puts("usage: sysreport-search crypto [--threads N] [--seconds S] [--all-threads-only] [--yes]\n"
                     "  --all-threads-only  skip the 1-thread pass (the GUI's All Cores): only the N-threads column, about half the time");
                help_risk();
                return 0;
            }
            if (!strcmp(a, "--yes") || !strcmp(a, "-y")) { yes = 1; continue; }
            if (!strcmp(a, "--all-threads-only")) { all_only = 1; continue; }
            if (!v) return usage_error("crypto: option needs a value");
            if (!strcmp(a, "--threads")) threads = atoi(v);
            else if (!strcmp(a, "--seconds")) secs = atof(v);
            else return usage_error("crypto: unknown option");
            i++;
        }
        if (secs < 0.1 || secs > 60) return usage_error("--seconds must be 0.1..60");
        q.kind = BA_CRYPTO; q.threads = threads; q.secs = secs; q.all_only = all_only;
        if (!yes && !ask_bench("crypto", &q)) return 2;
        return bench_crypto(threads, secs, all_only, &o) ? 1 : 0;
    }
    {
        double lim = 1e9;
        int rc;
        for (i = 2; i < argc; i++) {
            const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
            if (!strcmp(a, "--help") || !strcmp(a, "-h")) { puts("usage: sysreport-search prime [--limit 1e8|1e9|1e10|1e11|N] [--threads N] [--yes]\n"
                                                                    "  --limit N  count the primes up to N: any number from 100 to 1e11 (e.g. 5e9 or 1g; default 1e9)"); help_risk(); return 0; }
            if (!strcmp(a, "--yes") || !strcmp(a, "-y")) { yes = 1; continue; }
            if (!v) return usage_error("prime: option needs a value");
            if (!strcmp(a, "--limit")) lim = parse_count(v);
            else if (!strcmp(a, "--threads")) threads = atoi(v);
            else return usage_error("prime: unknown option");
            i++;
        }
        if (lim < 100 || lim > 1e11) return usage_error("prime: --limit must be 100..1e11");
        q.kind = BA_PRIME; q.limit = (double)(uint64_t)(lim + 0.5); q.threads = threads;
        if (!yes && !ask_bench("prime", &q)) return 2;
        rc = bench_prime((uint64_t)(lim + 0.5), threads, &o, NULL);
        return rc == 0 ? 0 : 1;
    }
}

#include "cuda_toolkit.h"
#include <sys/wait.h>
#include <errno.h>

/* after a run that used CUDA: when the NVIDIA runtime compiler is missing and could matter, say once how to get it.
   The CLI never installs anything without the cuda-toolkit command and a typed yes. */
static void cuda_toolkit_hint(void)
{
    ctk_state st;
    ctk_plan pl;
    if (!ctk_ask_get()) return;   /* "Don't ask again" (GUI or cuda-toolkit --never-ask) also silences this note */
    ctk_detect(&st, 0);
    if (!st.relevant) return;
    ctk_plan_install(&pl);
    fprintf(stderr, "note: the NVIDIA CUDA runtime compiler (NVRTC) is not installed. SRS used its built-in CUDA tests, so it is "
                    "optional%s.\n      %s\n", st.cu.zluda ? " (and NOT recommended with ZLUDA: it can break ZLUDA)" : "", ctk_later_hint());
    if (!pl.manual) fprintf(stderr, "      Command: %s %s\n", ctk_root_tool() ? ctk_root_tool() : "sudo", pl.cmd);
}

static int yes_typed(const char *prompt)
{
    char ans[32];
    fprintf(stderr, "%s", prompt);
    fflush(stderr);
    if (!fgets(ans, sizeof ans, stdin)) return 0;
    ans[strcspn(ans, "\r\n")] = 0;
    return !strcmp(ans, "yes") || !strcmp(ans, "YES") || !strcmp(ans, "Yes");
}

/* update-lists [--list] [--yes] (2.4) */
static int cmd_update_lists(int argc, char **argv)
{
    int i, yes = 0, list = 0, n;
    char rep[2000];
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--yes") || !strcmp(argv[i], "-y")) yes = 1;
        else if (!strcmp(argv[i], "--list")) list = 1;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            puts("usage: sysreport-search update-lists [--list] [--yes]\n"
                 "  downloads pci.ids, usb.ids and pnp.ids from their official sources into ~/.local/share/srs/lists;\n"
                 "  SRS uses the newer of these and the system's (hwdata) copies. --list: the copies in use");
            return 0;
        } else { fprintf(stderr, "update-lists: unknown option '%s'\n", argv[i]); return 2; }
    }
    for (i = 0; i < SRS_NLISTS; i++) {
        char p[1200], v[32] = "";
        const char *src = "";
        int have = srs_ids_path(srs_lists[i].file, p, sizeof p, &src) != NULL;
        if (have) { srs_ids_version(p, v, sizeof v); if (!*v) snprintf(v, sizeof v, "%ld entries", srs_ids_entries(p)); }
        printf("%-9s %-13s %s  [%s]\n", srs_lists[i].file, have ? v : "missing", have ? p : "", src);
    }
    if (list) return 0;
    if (!yes) {
        char a[16] = "";
        printf("\nDownload fresh copies from:\n");
        for (i = 0; i < SRS_NLISTS; i++) printf("  %s\n", srs_lists[i].url);
        printf("Go on? [y/N] ");
        fflush(stdout);
        if (!isatty(0) || !fgets(a, sizeof a, stdin) || (a[0] != 'y' && a[0] != 'Y')) { puts("not downloaded"); return 1; }
    }
    n = srs_lists_update(-1, rep, sizeof rep);
    fputs(rep, stdout);
    return n > 0 ? 0 : 1;
}

/* cuda-toolkit [--check|--install|--ask-again|--never-ask] */
static int cmd_cuda_toolkit(int argc, char **argv)
{
    static const char usage[] =
        "usage: sysreport-search cuda-toolkit [--check|--install|--ask-again|--never-ask]\n"
        "  --check       (default) what the toolkit is for, whether it is installed, the install command\n"
        "  --install     run the install command after you type yes (in a terminal only; asks twice with ZLUDA)\n"
        "  --ask-again   let the GUI ask about it again (the first time a CUDA test starts)\n"
        "  --never-ask   the GUI never asks (the same as its \"Don't ask again\" button)\n"
        "Nothing is installed without --install and a typed yes. SRS_CTK_DRY_RUN=1 shows the command and runs nothing.";
    const char *mode = "--check";
    ctk_state st;
    ctk_plan pl;
    char b[2000], why[600], ini[1100];
    int i, status = -1;
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { puts(usage); return 0; }
        if (!strcmp(argv[i], "--check") || !strcmp(argv[i], "--install") || !strcmp(argv[i], "--ask-again") || !strcmp(argv[i], "--never-ask")) mode = argv[i];
        else { fprintf(stderr, "cuda-toolkit: unknown option '%s'\n%s\n", argv[i], usage); return 2; }
    }
    if (!strcmp(mode, "--ask-again") || !strcmp(mode, "--never-ask")) {
        int on = !strcmp(mode, "--ask-again");
        if (ctk_ask_set(on)) { fprintf(stderr, "cuda-toolkit: could not write %s\n", ctk_ini_path(ini, sizeof ini)); return 1; }
        printf("The GUI %s about the NVIDIA CUDA toolkit (%s).\n", on ? "will ask again" : "will not ask", ctk_ini_path(ini, sizeof ini));
        return 0;
    }
    ctk_detect(&st, 0);
    ctk_plan_install(&pl);
    ctk_status_text(&st, &pl, b, sizeof b);
    printf("NVIDIA CUDA toolkit (optional)\n\n%s\n\n%s", ctk_explain(), b);
    if (pl.manual) printf("How to install: %s\n", pl.manual_text);
    else if (pl.manual_text[0]) printf("Note: %s\n", pl.manual_text);
    if (st.cu.zluda) printf("\nWARNING: %s\n", ctk_zluda_warning());
    printf("The GUI %s about it the first time a CUDA test starts.\n", ctk_ask_get() ? "asks" : "does not ask (\"Don't ask again\"; --ask-again turns it back on)");
    if (strcmp(mode, "--install")) {
        if (!st.cu.nvrtc) printf("%s\n", ctk_later_hint());
        return 0;
    }
    if (st.cu.nvrtc) { printf("\nThe runtime compiler is already installed: nothing to do.\n"); return 0; }
    if (pl.manual) { fprintf(stderr, "\ncuda-toolkit: SRS has no install command for %s; follow the steps above.\n", pl.distro); return 1; }
    if (ctk_precheck(&pl, 1, why, sizeof why)) { fprintf(stderr, "\ncuda-toolkit: not started - %s\n", why); return 1; }
    if (!isatty(0) || !isatty(2)) {
        fprintf(stderr, "\ncuda-toolkit: not started - --install asks for a typed yes and runs only in a terminal.\n"
                        "              Or run it yourself: %s %s\n", ctk_root_tool() ? ctk_root_tool() : "sudo", pl.cmd);
        return 2;
    }
    /* (ctk_precheck refused when there is no sudo / pkexec in a system folder, so ctk_root_path() is set from here on) */
    fprintf(stderr, "\nThis runs, as administrator: %s %s\n(%s)\nPrograms used: %s and %s\n", ctk_root_tool(), pl.cmd, pl.size, ctk_root_path(), pl.exe);
    if (!yes_typed("Type yes to install it, anything else to cancel: ")) { fprintf(stderr, "cuda-toolkit: cancelled, nothing installed. %s\n", ctk_later_hint()); return 2; }
    if (st.cu.zluda && !yes_typed("ZLUDA is in use and may stop working or be removed. Type yes AGAIN to install anyway: ")) {
        fprintf(stderr, "cuda-toolkit: cancelled, nothing installed (recommended with ZLUDA).\n");
        return 2;
    }
    if (ctk_dry_run()) { printf("dry run (SRS_CTK_DRY_RUN): would run: %s %s (as: %s %s", ctk_root_tool(), pl.cmd, ctk_root_path(), pl.exe);
                         for (i = 1; i < pl.argc; i++) printf(" %s", pl.argv[i]);
                         printf(")\n");
                         return 0; }
    {
        const char *av[12];
        int n = 0, k;
        pid_t p;
        av[n++] = ctk_root_path();   /* full paths in a system folder: never a sudo or package manager found in PATH */
        av[n++] = pl.exe;
        for (k = 1; k < pl.argc && n < 11; k++) av[n++] = pl.argv[k];
        av[n] = NULL;
        fflush(stdout);
        if ((p = fork()) == 0) { execv(av[0], (char *const *)av); _exit(127); }
        if (p > 0) {
            int ws = 0;
            while (waitpid(p, &ws, 0) < 0 && errno == EINTR) {}
            status = WIFEXITED(ws) ? WEXITSTATUS(ws) : 128 + (WIFSIGNALED(ws) ? WTERMSIG(ws) : 0);
        }
    }
    ctk_detect(&st, 1);
    ctk_result_text(&pl, status, &st, b, sizeof b);
    printf("\n%s\n", b);
    return status == 0 && st.cu.nvrtc ? 0 : 1;
}
#endif

#if defined(SRS_HAVE_SECTIONS) && defined(__linux__)
/* ------------------------------------------------------------------ ir: the IR Blaster (ELK Smart / OCRUSTAR
   USB infrared dongles; protocol from elksmart-comm by Přemysl Eric Janouch, 0BSD). Codes live in IR Remote's
   remotes.json. --send and --learn use the dongle and ask first unless --yes; the rest only read. */
#include "ir.h"

static int ir_ask(const char *what)
{
    char a[16] = "";
    if (!isatty(0)) { fprintf(stderr, "ir: not started - run it from a terminal to be asked, or pass --yes\n"); return 0; }
    fprintf(stderr, "%s [y/N] ", what);
    fflush(stderr);
    return fgets(a, sizeof a, stdin) && (a[0] == 'y' || a[0] == 'Y');
}

static int ir_ready_or_say(void)
{
    ir_dongle d;
    char s[700];
    if (ir_detect(&d) == IR_READY) return 1;
    ir_status_text(&d, s, sizeof s);
    fprintf(stderr, "ir: %s\n", s);
    if (d.state == IR_NOPERM) fprintf(stderr, "    The rule is one line:\n    %s\n", IR_UDEV_LINE);
    return 0;
}

static int cmd_ir(int argc, char **argv)
{
    static const char usage[] =
        "usage: sysreport-search ir [--status | --list | --send REMOTE BUTTON [--repeat N] | --send-code HEX [--nec]\n"
        "                            [--frequency HZ] [--repeat N] | --learn [--save REMOTE BUTTON] [--timeout S] [--frequency HZ] |\n"
        "                            --probe | --reset | --export FILE | --import FILE [--replace]] [--dry-run] [--huffman] [--yes]\n"
        "  IR Blaster: ELK Smart / OCRUSTAR USB infrared dongles (" IR_SUPPORTED_IDS ").\n"
        "  The codes are IR Remote's (~/.config/irremote/remotes.json), shared with that app.\n"
        "  --status     (default) is a dongle plugged in, may it be used (read-only: sysfs)\n"
        "  --list       the remotes and their codes\n"
        "  --send       press a saved button (REMOTE and BUTTON as listed; case does not matter)\n"
        "  --send-code  send a hex code (--nec: NEC address+command pairs, 4 hex digits per press)\n"
        "  --learn      wait for one button of a remote (hold it within 1 cm of the dongle) and print its code;\n"
        "               --save REMOTE BUTTON also stores it (a new remote / button is made when needed)\n"
        "  --probe      ask the dongle to identify itself (sends no infrared)\n"
        "  --reset      USB reset, for a dongle that stopped answering (left in learn mode)\n"
        "  --export / --import   copy the codes to / from a file (import merges; --replace replaces them all)\n"
        "  --dry-run    with --send / --send-code: only print the USB packets, nothing is sent (--huffman: for a\n"
        "               dongle that uses the compressed protocol)\n"
        "  --send and --learn ask first; --yes starts at once.";
    enum { M_STATUS, M_LIST, M_SEND, M_SENDCODE, M_LEARN, M_PROBE, M_RESET, M_EXPORT, M_IMPORT } mode = M_STATUS;
    const char *a1 = NULL, *a2 = NULL, *save_r = NULL, *save_b = NULL;
    unsigned long freq = 0;
    int i, repeat = 1, yes = 0, dry = 0, nec = 0, huff = 0, replace = 0, nmodes = 0;
    double timeout = 20;
    char msg[1200], err[400];
    for (i = 2; i < argc; i++) {
        const char *a = argv[i];
        int more = argc - i - 1;
#define IR_NEED(n) do { if (more < (n)) { fprintf(stderr, "ir: %s needs %s\n%s\n", a, (n) == 2 ? "two values" : "a value", usage); return 2; } } while (0)
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { puts(usage); return 0; }
        else if (!strcmp(a, "--status")) { mode = M_STATUS; nmodes++; }
        else if (!strcmp(a, "--list")) { mode = M_LIST; nmodes++; }
        else if (!strcmp(a, "--send")) { IR_NEED(2); mode = M_SEND; a1 = argv[++i]; a2 = argv[++i]; nmodes++; }
        else if (!strcmp(a, "--send-code")) { IR_NEED(1); mode = M_SENDCODE; a1 = argv[++i]; nmodes++; }
        else if (!strcmp(a, "--learn")) { mode = M_LEARN; nmodes++; }
        else if (!strcmp(a, "--probe")) { mode = M_PROBE; nmodes++; }
        else if (!strcmp(a, "--reset")) { mode = M_RESET; nmodes++; }
        else if (!strcmp(a, "--export")) { IR_NEED(1); mode = M_EXPORT; a1 = argv[++i]; nmodes++; }
        else if (!strcmp(a, "--import")) { IR_NEED(1); mode = M_IMPORT; a1 = argv[++i]; nmodes++; }
        else if (!strcmp(a, "--save")) { IR_NEED(2); save_r = argv[++i]; save_b = argv[++i]; }
        else if (!strcmp(a, "--repeat") || !strcmp(a, "-r")) { IR_NEED(1); repeat = atoi(argv[++i]); if (repeat < 1 || repeat > 100) { fprintf(stderr, "ir: --repeat needs 1..100\n"); return 2; } }
        else if (!strcmp(a, "--frequency") || !strcmp(a, "-f")) { IR_NEED(1); freq = strtoul(argv[++i], NULL, 10); if (freq < 10000 || freq > 100000) { fprintf(stderr, "ir: --frequency needs 10000..100000 (Hz)\n"); return 2; } }
        else if (!strcmp(a, "--timeout") || !strcmp(a, "-t")) { IR_NEED(1); timeout = atof(argv[++i]); if (timeout < 1 || timeout > 600) { fprintf(stderr, "ir: --timeout needs 1..600 (s)\n"); return 2; } }
        else if (!strcmp(a, "--nec") || !strcmp(a, "-n")) nec = 1;
        else if (!strcmp(a, "--dry-run")) dry = 1;
        else if (!strcmp(a, "--huffman")) huff = 1;
        else if (!strcmp(a, "--replace")) replace = 1;
        else if (!strcmp(a, "--yes") || !strcmp(a, "-y")) yes = 1;
        else { fprintf(stderr, "ir: unknown option '%s'\n%s\n", a, usage); return 2; }
#undef IR_NEED
    }
    if (nmodes > 1) { fprintf(stderr, "ir: choose one of --status, --list, --send, --send-code, --learn, --probe, --reset, --export, --import\n"); return 2; }
    if (save_r && mode != M_LEARN) { fprintf(stderr, "ir: --save goes with --learn\n"); return 2; }
    if (dry && mode != M_SEND && mode != M_SENDCODE) { fprintf(stderr, "ir: --dry-run goes with --send or --send-code\n"); return 2; }
    if (mode == M_STATUS) {
        ir_dongle d;
        ir_detect(&d);
        ir_status_text(&d, msg, sizeof msg);
        printf("%s\n", msg);
        if (d.state != IR_ABSENT) printf("Device   %s\nSysfs    %s\n", d.node, d.sysdir);
        {
            char rule[600];
            const char *have = ir_udev_rule_found(rule, sizeof rule);
            printf("Rule     %s\n", have ? have : "not installed (" IR_UDEV_RULE " from elksmart-comm-git or IR Remote, or a copy in /etc/udev/rules.d)");
            if (d.state == IR_NOPERM && !have && !d.nonode) printf("         %s\n", IR_UDEV_LINE);
        }
        printf("Codes    %s\n", ir_codes_path());
        return d.state == IR_READY ? 0 : 1;
    }
    if (mode == M_PROBE) { int ok = ir_probe(msg, sizeof msg); fprintf(ok ? stdout : stderr, "%s\n", msg); return !ok; }
    if (mode == M_RESET) { int ok = ir_reset(msg, sizeof msg); fprintf(ok ? stdout : stderr, "%s\n", msg); return !ok; }
    if (mode == M_SENDCODE) {
        if (dry) {
            char *out = ir_dry_run(a1, nec, freq, huff, msg, sizeof msg);
            if (!out) { fprintf(stderr, "ir: %s\n", msg); return 1; }
            fputs(out, stdout);
            printf("%s\n", msg);
            free(out);
            return 0;
        }
        if (ir_code_problem(a1, nec)) { fprintf(stderr, "ir: %s\n", ir_code_problem(a1, nec)); return 1; }
        if (!ir_ready_or_say()) return 1;
        snprintf(msg, sizeof msg, "Send this %s code (%zu hex digits, %lu Hz)%s through the IR blaster now?", nec ? "NEC" : "learned",
                 strlen(a1), freq ? freq : IR_DEFAULT_FREQ, repeat > 1 ? " several times" : "");
        if (!yes && !ir_ask(msg)) { fprintf(stderr, "not sent\n"); return 1; }
        if (!ir_send(a1, nec, freq, repeat, NULL, msg, sizeof msg)) { fprintf(stderr, "ir: %s\n", msg); return 1; }
        printf("%s\n", msg);
        return 0;
    }
    {
        int exists = 0, r, b;
        const char *path = ir_codes_path();
        ir_doc *doc = ir_doc_load(path, &exists, err, sizeof err);
        if (!doc) { fprintf(stderr, "ir: %s\n", err); return 1; }
        if (mode == M_LIST) {
            if (!exists) printf("No codes yet (%s does not exist). Learn one: sysreport-search ir --learn --save REMOTE BUTTON\n", path);
            for (r = 0; r < ir_nremotes(doc); r++) {
                printf("%s\n", ir_remote_name(doc, r));
                for (b = 0; b < ir_nbuttons(doc, r); b++) {
                    ir_button bt;
                    ir_button_at(doc, r, b, &bt);
                    printf("  %-24s %-11s %5zu bytes  %6lu Hz%s%s\n", bt.name, *bt.code ? bt.type : "not learned", strlen(bt.code) / 2, bt.freq,
                           *bt.shortcut ? "  " : "", bt.shortcut);
                }
            }
            ir_doc_free(doc);
            return 0;
        }
        if (mode == M_EXPORT) {
            int ok = ir_doc_export(doc, a1, err, sizeof err);
            if (ok) printf("Exported %d remote%s to %s\n", ir_nremotes(doc), ir_nremotes(doc) == 1 ? "" : "s", a1);
            else fprintf(stderr, "ir: %s\n", err);
            ir_doc_free(doc);
            return !ok;
        }
        if (mode == M_IMPORT) {
            int ex2 = 0, ok;
            ir_doc *in = ir_doc_load(a1, &ex2, err, sizeof err);
            if (!in || !ex2) { fprintf(stderr, "ir: can't import %s: %s\n", a1, in ? "no such file" : err); ir_doc_free(in); ir_doc_free(doc); return 1; }
            if (replace) { ir_doc_free(doc); doc = in; in = NULL; ok = 1; }
            else ok = ir_doc_merge(doc, in);
            ok = ok && ir_doc_save(doc, path, err, sizeof err);
            if (ok) printf("Imported %d remote%s%s into %s\n", ir_nremotes(in ? in : doc), ir_nremotes(in ? in : doc) == 1 ? "" : "s",
                           replace ? " (replacing all)" : " (merged)", path);
            else fprintf(stderr, "ir: %s\n", *err ? err : "out of memory");
            ir_doc_free(in);
            ir_doc_free(doc);
            return !ok;
        }
        if (mode == M_SEND) {
            ir_button bt;
            int ok;
            if ((r = ir_find_remote(doc, a1)) < 0) {
                fprintf(stderr, "ir: no remote called \"%s\" (have:", a1);
                for (b = 0; b < ir_nremotes(doc); b++) fprintf(stderr, "%s \"%s\"", b ? "," : "", ir_remote_name(doc, b));
                fprintf(stderr, "%s)\n", ir_nremotes(doc) ? "" : " none");
                ir_doc_free(doc);
                return 1;
            }
            if ((b = ir_find_button(doc, r, a2)) < 0) {
                fprintf(stderr, "ir: \"%s\" has no button \"%s\" (have:", ir_remote_name(doc, r), a2);
                for (i = 0; i < ir_nbuttons(doc, r); i++) { ir_button_at(doc, r, i, &bt); fprintf(stderr, "%s \"%s\"", i ? "," : "", bt.name); }
                fprintf(stderr, "%s)\n", ir_nbuttons(doc, r) ? "" : " none");
                ir_doc_free(doc);
                return 1;
            }
            ir_button_at(doc, r, b, &bt);
            if (!*bt.code) { fprintf(stderr, "ir: %s / %s isn't learned yet - learn it first\n", ir_remote_name(doc, r), bt.name); ir_doc_free(doc); return 1; }
            if (dry) {
                char *out = ir_dry_run(bt.code, !strcmp(bt.type, "nec"), bt.freq, huff, msg, sizeof msg);
                ok = out != NULL;
                if (out) { printf("Would send %s / %s (%s code, %zu bytes, %lu Hz) x%d\n", ir_remote_name(doc, r), bt.name, bt.type, strlen(bt.code) / 2, bt.freq, repeat);
                           fputs(out, stdout); printf("%s\n", msg); free(out); }
                else fprintf(stderr, "ir: %s\n", msg);
                ir_doc_free(doc);
                return !ok;
            }
            if (!ir_ready_or_say()) { ir_doc_free(doc); return 1; }
            snprintf(msg, sizeof msg, "Send %s / %s%s through the IR blaster now?", ir_remote_name(doc, r), bt.name, repeat > 1 ? " several times" : "");
            if (!yes && !ir_ask(msg)) { fprintf(stderr, "not sent\n"); ir_doc_free(doc); return 1; }
            ok = ir_send(bt.code, !strcmp(bt.type, "nec"), bt.freq, repeat, NULL, msg, sizeof msg);
            if (ok) printf("%s %s / %s\n", msg, ir_remote_name(doc, r), bt.name);
            else fprintf(stderr, "ir: %s\n", msg);
            ir_doc_free(doc);
            return !ok;
        }
        /* M_LEARN */
        ir_doc_free(doc);
        if (!ir_ready_or_say()) return 1;
        if (!yes && !ir_ask("Put the IR blaster in learn mode now (it waits for one remote button)?")) { fprintf(stderr, "not started\n"); return 1; }
        {
            char code[8192], hint[256];
            int ok;
            fprintf(stderr, "Hold the remote within 1 cm of the dongle and press the button (waiting up to %.0f s)...\n", timeout);
            ok = ir_learn(timeout, NULL, code, sizeof code, hint, sizeof hint, msg, sizeof msg);
            if (!ok) { fprintf(stderr, "ir: %s\n", msg); return 1; }
            printf("%s\n", code);
            if (*hint) fprintf(stderr, "Attempted pulse decode: %s\n", hint);
            if (save_r) {
                if (!(doc = ir_doc_load(path, &exists, err, sizeof err))) { fprintf(stderr, "ir: not saved: %s\n", err); return 1; }   /* fresh: IR Remote may have changed it */
                r = ir_remote_add(doc, save_r);
                b = r >= 0 ? ir_button_set(doc, r, save_b, code, 0, freq) : -1;
                ok = b >= 0 && ir_doc_save(doc, path, err, sizeof err);
                if (ok) fprintf(stderr, "Saved as %s / %s\n", ir_remote_name(doc, r), save_b);
                else fprintf(stderr, "ir: not saved: %s\n", b < 0 ? "a remote and a button need a name" : err);
                ir_doc_free(doc);
                return !ok;
            }
        }
        return 0;
    }
}
#endif

#if defined(SRS_HAVE_SECTIONS) && defined(__linux__)
/* ------------------------------------------------------------------ gpu-top (a C reimplementation of
   amdgpu_top by Umio Yasuno, MIT; NVIDIA through NVML). Read-only: it never changes a clock, a fan or a limit. */
#include "gpu_top.h"
#include <termios.h>
#include <poll.h>
#include <time.h>

static volatile sig_atomic_t gt_stop;
static void gt_on_sig(int s) { (void)s; gt_stop = 1; }

static void gt_print_sections(const gt_info *in)
{
    for (int i = 0; i < in->nsect; i++) {
        const gt_section *s = &in->sect[i];
        int w = 0, list = s->n && !s->kv[0].label[0];
        for (int k = 0; k < s->n; k++) if ((int)strlen(s->kv[k].label) > w) w = (int)strlen(s->kv[k].label);
        if (!strcmp(s->title, "Power Profiles") || !strcmp(s->title, "pp_feature_mask")) {
            if (!s->n) continue;
            printf("\n%s: [", !strcmp(s->title, "Power Profiles") ? "Supported Power Profiles" : "pp_feature_mask");
            for (int k = 0; k < s->n; k++) printf("%s\"%s\"", k ? ", " : "", s->kv[k].value);
            printf("]\n");
            continue;
        }
        if (!strcmp(s->title, "Hardware IP") || !strcmp(s->title, "IP Discovery") || !strcmp(s->title, "Firmware") ||
            !strcmp(s->title, "Video Caps") || !strcmp(s->title, "VBIOS")) {
            printf("\n%s:\n", !strcmp(s->title, "Hardware IP") ? "Hardware IP info" : !strcmp(s->title, "IP Discovery") ? "IP Discovery table" :
                   !strcmp(s->title, "Firmware") ? "Firmware info" : !strcmp(s->title, "Video Caps") ? "Video caps (WIDTHxHEIGHT)" : "VBIOS info");
            if (!strcmp(s->title, "IP Discovery")) printf("    die_id: %2d\n", 0);
            for (int k = 0; k < s->n; k++) {
                if (!strcmp(s->title, "IP Discovery")) printf("        %-10s %s\n", s->kv[k].label, s->kv[k].value);
                else if (!strcmp(s->title, "VBIOS")) printf("    %-7s: %s\n", s->kv[k].label, s->kv[k].value);
                else if (!strcmp(s->title, "Video Caps")) printf("    %-10s: %s\n", s->kv[k].label, s->kv[k].value);
                else printf("    %-8s %s\n", s->kv[k].label, s->kv[k].value);
            }
            continue;
        }
        printf("\n");
        for (int k = 0; k < s->n; k++) {
            if (list) printf("    %s\n", s->kv[k].value);
            else printf("%-*s: %s\n", w, s->kv[k].label, s->kv[k].value);
        }
    }
}

static int gt_parse_int(const char *opt, const char *v, int lo, int hi, int *out)
{
    char *e;
    long x = v ? strtol(v, &e, 10) : 0;
    if (!v || !*v || *e || x < lo || x > hi) {
        fprintf(stderr, "gpu-top: %s needs a number %d..%d\n", opt, lo, hi);
        return -1;
    }
    *out = (int)x;
    return 0;
}

/* "used/total" in MiB, "____/____" when the card did not say (no driver data: not 0 of -1) */
static const char *gt_mib2(char *b, size_t n, long long used, long long total, int w)
{
    if (used == GT_NA || total == GT_NA || total <= 0) snprintf(b, n, "%*s/%*s", w, "____", w, "____");
    else snprintf(b, n, "%*lld/%*lld", w, used >> 20, w, total >> 20);
    return b;
}

static void gt_smi_header(void)
{
    printf("GPU %-25s %10s|%-16s|%18s|\n", "Name", "", " PCI Bus", "    VRAM Usage    ");
    printf("SCLK    MCLK    VDDGFX  Power           | GFX%% UMC%%Media%%| GTT (RAM) Usage  |\n");
    printf("GPU/MEM_T  %-7s %-57s|\n", "Fan", "Throttle_Status");
    printf("-----------------------------------------------------------------------------------\n");
}

static void gt_smi_line(int i, const gt_info *in, const gt_stat *s)
{
    char thr[128] = "";
    double pw = s->power_avg >= 0 ? s->power_avg : s->power_input;
    char vm[32], gm[32];
    printf("#%-2d [%-25.25s](%7s)| %s   |%s MiB |\n", i, in->dev.name, in->gfx_target, in->dev.pci, gt_mib2(vm, sizeof vm, s->vram_used, s->vram_total, 6));
    if (s->sclk != GT_NA) printf("%4dMHz ", s->sclk); else printf("____MHz ");
    if (s->mclk != GT_NA) printf("%4dMHz ", s->mclk); else printf("____MHz ");
    if (s->vddgfx != GT_NA) printf("%4dmV ", s->vddgfx); else printf("____mV ");
    if (pw >= 0 && s->power_cap != GT_NA) printf(" %3.0f/%3dW ", pw, s->power_cap);
    else if (pw >= 0) printf(" %3.0f/___W ", pw);
    else printf(" ___/___W ");
    printf("       |");
    for (int k = 0; k < 3; k++) {
        int u = k == 0 ? s->gfx : k == 1 ? s->umc : s->media;
        if (u != GT_NA) printf(" %3d%%", u); else printf(" ___%%");
    }
    printf(" |%s MiB |\n", gt_mib2(gm, sizeof gm, s->gtt_used, s->gtt_total, 6));
    {
        int t = s->junction != GT_NA ? s->junction : s->edge;
        if (t != GT_NA && s->memtemp != GT_NA) printf("%3dC/%3dC", t, s->memtemp);
        else if (t != GT_NA) printf("%3dC/___C", t);
        else printf("___C/___C");
    }
    if (s->fan_rpm != GT_NA) printf("  %4dRPM ", s->fan_rpm); else printf("  ____RPM ");
    if ((s->metrics.present && s->metrics.v[GT_M_INDEP_THROTTLE_STATUS] != GT_NA) || s->nthr) {
        snprintf(thr, sizeof thr, "[");
        for (int k = 0; k < s->nthr; k++) { strncat(thr, k ? ", " : "", sizeof thr - strlen(thr) - 1); strncat(thr, s->thr[k], sizeof thr - strlen(thr) - 1); }
        strncat(thr, "]", sizeof thr - strlen(thr) - 1);
    } else if (s->idle_d3 && s->pci_power_state[0]) snprintf(thr, sizeof thr, "N/A (%s)", s->pci_power_state);
    else snprintf(thr, sizeof thr, "N/A");
    printf("%-57.57s|\n", thr);
}

static void gt_bar(const char *label, int pct, int width)
{
    int n = pct < 0 ? 0 : pct > 100 ? width : pct * width / 100;
    printf(" %-34.34s [", label);
    for (int i = 0; i < width; i++) putchar(i < n ? '|' : ' ');
    if (pct == GT_NA) printf("]  n/a\n"); else printf("] %3d%%\n", pct);
}

static void gt_tui_draw(int ndev, gt_dev **dv, gt_stat *st, int no_pc, int interval)
{
    printf("\033[H\033[2J");
    printf("SRS GPU Top - %d GPU%s, every %d ms (q or Ctrl+C quits)%s\n", ndev, ndev == 1 ? "" : "s", interval,
           no_pc ? ", no GRBM counters" : "");
    for (int i = 0; i < ndev; i++) {
        const gt_info *in = gt_info_of(dv[i]);
        const gt_stat *s = &st[i];
        double pw = s->power_avg >= 0 ? s->power_avg : s->power_input;
        printf("\n#%d %s (%s) %s%s\n", i, in->dev.name, in->dev.pci, in->gfx_target, s->idle_d3 ? "  [idle - not woken]" : "");
        gt_bar("GFX", s->gfx, 40);
        gt_bar("Memory", s->umc, 40);
        gt_bar("Media", s->media, 40);
        if (s->pc_ok && !no_pc) {
            printf(" GRBM\n");
            for (int k = 0; k < s->ngrbm; k++) gt_bar(s->grbm[k].name, s->grbm[k].pct, 30);
            printf(" GRBM2\n");
            for (int k = 0; k < s->ngrbm2; k++) gt_bar(s->grbm2[k].name, s->grbm2[k].pct, 30);
        } else if (!no_pc) printf(" GRBM / GRBM2: not available on this card\n");
        {
            char a[32], b[32], c[32];
            printf(" VRAM %s MiB   CPU-Visible %s MiB   GTT %s MiB\n", gt_mib2(a, sizeof a, s->vram_used, s->vram_total, 6),
                   gt_mib2(b, sizeof b, s->vis_vram_used, s->vis_vram_total, 6), gt_mib2(c, sizeof c, s->gtt_used, s->gtt_total, 6));
        }
        {
            char t[8][16];
            int v[8] = { s->sclk, s->mclk, s->fclk, s->vddgfx, s->edge, s->junction, s->memtemp, s->fan_rpm };
            for (int k = 0; k < 8; k++) { if (v[k] == GT_NA) snprintf(t[k], sizeof t[k], "____"); else snprintf(t[k], sizeof t[k], "%4d", v[k]); }
            printf(" SCLK %s MHz  MCLK %s MHz  FCLK %s MHz  VDDGFX %s mV  Power ", t[0], t[1], t[2], t[3]);
            if (pw >= 0) printf("%3.0f", pw); else printf("___");
            if (s->power_cap != GT_NA) printf("/%3d W\n", s->power_cap); else printf("/___ W\n");
            printf(" Temp. edge %s C  junction %s C  memory %s C  Fan %s RPM", t[4], t[5], t[6], t[7]);
            if (s->pcie_gen != GT_NA) printf("  PCIe Gen%dx%d", s->pcie_gen, s->pcie_width);
            printf("  %s  %s\n", s->pci_power_state, s->power_profile);
        }
        if (s->nthr) {
            printf(" Throttle:");
            for (int k = 0; k < s->nthr; k++) printf(" %s", s->thr[k]);
            printf("\n");
        }
        printf(" %-20s %8s %6s %6s %4s %4s %4s %4s %4s\n", "Process", "PID", "VRAM", "GTT", "CPU", "GFX", "COMP", "DMA", "MEDIA");
        for (int k = 0; k < s->nproc && k < 12; k++) {
            const gt_proc *p = &s->proc[k];
            int v[5] = { p->cpu, p->gfx, p->compute, p->dma, p->media };
            printf(" %-20.20s %8d %5lldM %5lldM", p->name, p->pid, p->vram_kib >> 10, p->gtt_kib >> 10);
            for (int c = 0; c < 5; c++) { if (v[c] == GT_NA) printf("    -"); else printf(" %4d", v[c]); }
            printf("\n");
        }
    }
    fflush(stdout);
}

static int cmd_gpu_top(int argc, char **argv)
{
    static const char usage[] =
        "usage: sysreport-search gpu-top [--list | --dump | --gpu-metrics | --drm-info | --pp-table | --process | --xdna |\n"
        "                                 --smi | --tui] [-J|--json] [--device N [--wake]] [--pc] [--interval MS] [-n COUNT]\n"
        "  (also: sysreport-search big-top ...)\n"
        "  The Big Top's GPU part: what each GPU is doing now - a C reimplementation of amdgpu_top (AMD) with NVIDIA through NVML.\n"
        "  Read-only: it never changes a clock, a voltage, a fan or a power limit.\n"
        "  --list         the GPUs (number, name, PCI bus, vendor, driver, device nodes)\n"
        "  --dump         device info: ASIC, shaders, clocks, VRAM, caches, sensors, IP versions, firmware, video caps, VBIOS\n"
        "  --gpu-metrics  the decoded gpu_metrics table (every field the card reports)\n"
        "  --drm-info     display connectors, modes and their properties\n"
        "  --pp-table     the PowerPlay table (Radeon RX 5000 / 6000 / 7000 only)\n"
        "  --process      the processes using each GPU and their VRAM / GTT\n"
        "  --xdna         the AMD XDNA NPU (Ryzen AI), where there is one\n"
        "  --smi          a compact table, one block per GPU (after one interval)\n"
        "  --tui          a live terminal view (default in a terminal); q or Ctrl+C quits\n"
        "  -J, --json     JSON in amdgpu_top's shape; alone it prints one sample per interval (-n COUNT, 0 = forever)\n"
        "  --device N     only GPU N (number from --list)\n"
        "  --wake         with --device N: wake GPU N up and read it when it is asleep (runtime suspended); without it a\n"
        "                 sleeping card is not read (\"asleep (runtime suspended) - not read\"), so it can stay asleep\n"
        "  --pc           read the GRBM / GRBM2 performance counters (--smi, --tui, the -J stream). Off by default, as in the\n"
        "                 window: reading them can keep the GPU out of its power-saving states (--no-pc is accepted, a no-op)\n"
        "  --interval MS  sampling interval, 100..60000 ms (default 1000)";
    enum { M_NONE, M_LIST, M_DUMP, M_METRICS, M_DRM, M_PPT, M_PROC, M_XDNA, M_SMI, M_TUI, M_CAPTURE } mode = M_NONE;
    int json = 0, dev = -1, interval = 1000, count = 0, count_set = 0, n, first, last, rc = 0, wake = 0;
    gt_opts op;
    gt_device list[GT_MAX_DEV];
    const char *capture = NULL;
    memset(&op, 0, sizeof op);
    op.no_pc = 1;   /* GRBM off by default, as in the window (2.5): --pc turns it on */
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        int m = M_NONE;
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { puts(usage); return 0; }
        if (!strcmp(a, "--list")) m = M_LIST;
        else if (!strcmp(a, "--dump") || !strcmp(a, "-d")) m = M_DUMP;
        else if (!strcmp(a, "--gpu-metrics")) m = M_METRICS;
        else if (!strcmp(a, "--drm-info")) m = M_DRM;
        else if (!strcmp(a, "--pp-table")) m = M_PPT;
        else if (!strcmp(a, "--process")) m = M_PROC;
        else if (!strcmp(a, "--xdna")) m = M_XDNA;
        else if (!strcmp(a, "--smi")) m = M_SMI;
        else if (!strcmp(a, "--tui")) m = M_TUI;
        else if (!strcmp(a, "--capture-info")) { if (!v) { fprintf(stderr, "gpu-top: --capture-info needs a file\n%s\n", usage); return 2; } capture = v; i++; m = M_CAPTURE; }
        else if (!strcmp(a, "-J") || !strcmp(a, "--json")) { json = 1; continue; }
        else if (!strcmp(a, "--no-pc")) { op.no_pc = 1; continue; }   /* the default since 2.5; kept for amdgpu_top users */
        else if (!strcmp(a, "--pc")) { op.no_pc = 0; continue; }
        else if (!strcmp(a, "--wake")) { wake = 1; continue; }
        else if (!strcmp(a, "--device") || !strcmp(a, "-i")) { if (gt_parse_int(a, v, 0, GT_MAX_DEV - 1, &dev)) return 2; i++; continue; }
        else if (!strcmp(a, "--interval") || !strcmp(a, "-s")) { if (gt_parse_int(a, v, 100, 60000, &interval)) return 2; i++; continue; }
        else if (!strcmp(a, "-n") || !strcmp(a, "--count")) { if (gt_parse_int(a, v, 0, 1000000, &count)) return 2; count_set = 1; i++; continue; }
        else { fprintf(stderr, "gpu-top: unknown option '%s'\n%s\n", a, usage); return 2; }
        if (mode != M_NONE && (int)mode != m) { fprintf(stderr, "gpu-top: choose one of --list, --dump, --gpu-metrics, --drm-info, --pp-table, --process, --xdna, --smi, --tui\n"); return 2; }
        mode = m;
    }
    if (mode == M_TUI && json) { fprintf(stderr, "gpu-top: --tui and --json do not go together\n"); return 2; }
    if (mode == M_NONE) mode = json ? M_NONE : (isatty(1) ? M_TUI : M_SMI);
    if (mode == M_XDNA) {
        char b[8192] = "";
        int r = gt_xdna_text(b, sizeof b);
        if (json) { printf("{\"xdna\":%s}\n", r ? "null" : "\"present\""); return 0; }
        fputs(b, stdout);
        return 0;
    }
    n = gt_device_list(list, GT_MAX_DEV);
    if (n == 0) { fprintf(stderr, "gpu-top: no GPU found\n"); return 1; }
    if (dev >= n) { fprintf(stderr, "gpu-top: there is no GPU %d (see --list)\n", dev); return 2; }
    first = dev >= 0 ? dev : 0;
    last = dev >= 0 ? dev : n - 1;
    if (mode == M_LIST) {
        if (json) printf("[");
        for (int i = first; i <= last; i++) {
            const gt_device *g = &list[i];
            char nm[600], drv[200];
            gt_json_escape(nm, sizeof nm, g->name);   /* names come from pci.ids / amdgpu.ids / NVML: escape them */
            gt_json_escape(drv, sizeof drv, g->driver);
            if (json) printf("%s{\"index\":%d,\"render\":\"%s\",\"card\":\"%s\",\"pci\":\"%s\",\"DeviceID\":%u,\"RevisionID\":%u,"
                             "\"DeviceName\":\"%s\",\"Vendor\":\"%s\",\"Driver\":\"%s\",\"active\":%s}",
                             i > first ? "," : "", i, g->render, g->card, g->pci, g->device_id, g->revision_id, nm,
                             gt_vendor_name(g->vendor), drv, g->active ? "true" : "false");
            else printf("#%d: %s (%s)\n    vendor %s (0x%04x), device 0x%04x rev 0x%02x, driver %s\n    card %s, render %s%s\n",
                        i, g->name, g->pci, gt_vendor_name(g->vendor), g->vendor_id, g->device_id, g->revision_id,
                        g->driver[0] ? g->driver : "(none)", g->card[0] ? g->card : "-", g->render[0] ? g->render : "-",
                        g->active ? "" : "\n    asleep (runtime suspended): not read by the other pages (--device N --wake reads it)");
        }
        if (json) printf("]\n");
        return 0;
    }
    {
        gt_dev *dv[GT_MAX_DEV];
        int idx[GT_MAX_DEV];   /* the --list number of each opened GPU */
        gt_stat *st = calloc(GT_MAX_DEV, sizeof *st);
        char *buf = malloc(1 << 20);
        int nd = 0;
        char err[200];
        if (!st || !buf) { free(st); free(buf); fprintf(stderr, "gpu-top: out of memory\n"); return 1; }
        /* one-shot pages never read GRBM registers */
        if (mode != M_SMI && mode != M_TUI && !(json && mode == M_NONE)) op.no_pc = 1;
        for (int i = first; i <= last; i++) {
            gt_dev *d;
            if (!list[i].active && !(dev >= 0 && wake)) {   /* opening it would wake it (a laptop's dGPU): only when asked */
                fprintf(stderr, "gpu-top: #%d %s: asleep (runtime suspended) - not read%s\n", i, list[i].pci,
                        dev >= 0 ? "; add --wake to wake it up and read it" : " (--device N --wake reads it)");
                if (dev >= 0) { free(st); free(buf); return 2; }
                continue;
            }
            d = gt_open(i, &op, err, sizeof err);
            if (!d) { fprintf(stderr, "gpu-top: #%d %s: %s\n", i, list[i].pci, err); rc = 1; continue; }
            idx[nd] = i;
            dv[nd++] = d;
        }
        if (!nd) { fprintf(stderr, "gpu-top: no GPU was read\n"); free(st); free(buf); return 1; }
        if (mode == M_CAPTURE) {
            rc = gt_capture_info(dv[0], capture);
            if (rc) fprintf(stderr, "gpu-top: could not write %s (AMD GPUs only)\n", capture);
        } else if (mode == M_DUMP || mode == M_METRICS || mode == M_PROC) {
            for (int i = 0; i < nd; i++) gt_finish(dv[i], 0, &st[i]);
            if (json) {
                int np = 0;
                printf("[");
                for (int i = 0; i < nd; i++) {
                    const gt_info *in = gt_info_of(dv[i]);
                    buf[0] = 0;
                    if (mode == M_DUMP) gt_json_dump(dv[i], &st[i], buf, 1 << 20);
                    else if (mode == M_METRICS) {
                        if (!st[i].metrics.present) continue;
                        char nm[600];
                        gt_json_escape(nm, sizeof nm, in->dev.name);
                        printf("%s{\"device_path\":{\"render\":\"%s\",\"card\":\"%s\",\"pci\":\"%s\",\"DeviceID\":%u,\"RevisionID\":%u,"
                               "\"DeviceName\":\"%s\"},\"gpu_metrics\":", np++ ? "," : "", in->dev.render, in->dev.card, in->dev.pci,
                               in->dev.device_id, in->dev.revision_id, nm);
                        gt_json_metrics(&st[i].metrics, buf, 1 << 20);
                        printf("%s}", buf);
                        continue;
                    } else gt_json_device(dv[i], &st[i], 1, buf, 1 << 20);
                    printf("%s%s", np++ ? "," : "", buf);
                }
                printf("]\n");
            } else {
                printf("SRS GPU Top %s (amdgpu_top-compatible)\n", SRS_VERSION);
                for (int i = 0; i < nd; i++) {
                    const gt_info *in = gt_info_of(dv[i]);
                    const gt_stat *s = &st[i];
                    if (mode == M_PROC) {
                        char vm[32], gm[32];
                        printf("\n%s (%s), VRAM %s MiB, GTT %s MiB\n", in->dev.pci, in->dev.name, gt_mib2(vm, sizeof vm, s->vram_used, s->vram_total, 5),
                               gt_mib2(gm, sizeof gm, s->gtt_used, s->gtt_total, 5));
                        for (int k = 0; k < s->nproc; k++) {
                            const gt_proc *p = &s->proc[k];
                            long long vt = s->vram_total >> 20, tt = s->gtt_total >> 20;
                            printf("    %-15s (%7d)%s, VRAM %5lld MiB (%3lld%%), GTT %5lld MiB (%3lld%%)\n", p->name, p->pid,
                                   p->is_kfd ? " KFD" : "", p->vram_kib >> 10, vt > 0 ? (p->vram_kib >> 10) * 100 / vt : 0,
                                   p->gtt_kib >> 10, tt > 0 ? (p->gtt_kib >> 10) * 100 / tt : 0);
                        }
                        if (!s->nproc) printf("    (no process has this GPU open, or none that this user may look at)\n");
                        continue;
                    }
                    printf("\n--------\n#%d %s (%s, 0x%X:0x%X)\n", idx[i], in->dev.name, in->dev.pci, in->dev.device_id, in->dev.revision_id);
                    if (mode == M_DUMP) {
                        if (in->drm_version[0]) printf("drm version: %s\n", in->drm_version);
                        gt_print_sections(in);
                        if (s->metrics.present) printf("\nGPU Metrics Version: v%d.%d\n", s->metrics.format_rev, s->metrics.content_rev);
                    } else {
                        buf[0] = 0;
                        gt_metrics_text(&s->metrics, buf, 1 << 20);
                        printf("\n%s", buf);
                    }
                }
            }
        } else if (mode == M_DRM || mode == M_PPT) {
            for (int i = 0; i < nd; i++) {
                buf[0] = 0;
                if (mode == M_DRM) gt_drm_info_text(dv[i], buf, 1 << 20); else gt_pp_table_text(dv[i], buf, 1 << 20);
                if (mode == M_PPT) printf("\n--------\n#%d ", idx[i]);
                fputs(buf, stdout);
            }
        } else if (mode == M_SMI) {
            /* every GPU over the same interval: the GRBM reads of all of them in one loop */
            int step = 10, steps = interval / step, rest = interval % step;   /* amdgpu_top's 10 ms reads; 250 ms = 25 steps */
            for (int i = 0; i < nd; i++) gt_pc_begin(dv[i]);
            for (int k = 0; k < steps; k++) {
                struct timespec ts = { 0, (long)step * 1000000L };
                for (int i = 0; i < nd; i++) gt_pc_read(dv[i]);
                nanosleep(&ts, NULL);
            }
            if (rest) { struct timespec ts = { 0, (long)rest * 1000000L }; nanosleep(&ts, NULL); }
            for (int i = 0; i < nd; i++) gt_finish(dv[i], interval, &st[i]);
            if (json) {
                printf("[");
                for (int i = 0; i < nd; i++) { buf[0] = 0; gt_json_device(dv[i], &st[i], op.no_pc, buf, 1 << 20); printf("%s%s", i ? "," : "", buf); }
                printf("]\n");
            } else {
                gt_smi_header();
                for (int i = 0; i < nd; i++) gt_smi_line(idx[i], gt_info_of(dv[i]), &st[i]);
            }
        } else {   /* JSON stream or the TUI */
            struct termios old, raw;
            int tty = mode == M_TUI && isatty(0);
            long it = 0;
            if (!count_set) count = 0;
            signal(SIGINT, gt_on_sig);
            signal(SIGTERM, gt_on_sig);
            if (tty && tcgetattr(0, &old) == 0) {
                raw = old;
                raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
                tcsetattr(0, TCSANOW, &raw);
                printf("\033[?1049h");
            } else tty = 0;
            for (int i = 0; i < nd; i++) gt_finish(dv[i], 0, &st[i]);   /* the first fdinfo pass: percent from the next one */
            while (!gt_stop) {
                /* each GPU sampled over the same interval: GRBM reads for all of them in one loop */
                long long t0;
                int step = 10, steps = interval / step, rest = interval % step;   /* 10 ms reads: the interval exactly */
                for (int i = 0; i < nd; i++) gt_pc_begin(dv[i]);
                t0 = 0;
                for (int k = 0; k < steps && !gt_stop; k++) {
                    struct pollfd pf = { 0, POLLIN, 0 };
                    for (int i = 0; i < nd; i++) gt_pc_read(dv[i]);
                    if (tty && poll(&pf, 1, step) > 0) {
                        char c;
                        if (read(0, &c, 1) == 1 && (c == 'q' || c == 'Q' || c == 3)) gt_stop = 1;
                    } else if (!tty) {
                        struct timespec ts = { step / 1000, (long)(step % 1000) * 1000000L };
                        nanosleep(&ts, NULL);
                    }
                    t0 += step;
                }
                if (rest && !gt_stop) { struct timespec ts = { 0, (long)rest * 1000000L }; nanosleep(&ts, NULL); t0 += rest; }
                if (gt_stop) break;
                for (int i = 0; i < nd; i++) gt_finish(dv[i], (int)t0, &st[i]);
                if (mode == M_TUI) gt_tui_draw(nd, dv, st, op.no_pc, interval);
                else {
                    printf("{\"devices\":[");
                    for (int i = 0; i < nd; i++) { buf[0] = 0; gt_json_device(dv[i], &st[i], op.no_pc, buf, 1 << 20); printf("%s%s", i ? "," : "", buf); }
                    printf("],\"devices_len\":%d,\"period\":{\"duration\":%d,\"unit\":\"ms\"},\"title\":\"SRS GPU Top\"}\n", nd, (int)t0);
                    fflush(stdout);
                }
                if (count && ++it >= count) break;
            }
            if (tty) { printf("\033[?1049l"); tcsetattr(0, TCSANOW, &old); }
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
        }
        for (int i = 0; i < nd; i++) gt_close(dv[i]);
        free(st);
        free(buf);
    }
    return rc;
}
#endif

int srs_cli_main(int argc, char **argv)
{
    opts o;
    const char *cmd;
    memset(&o, 0, sizeof o);
    o.max = 20;
    o.color = "auto";
    o.mode = "tree";
    {   /* --redact may appear anywhere (SIV-style "-save=..." is a single argument) */
        int i, j;
        for (i = j = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--redact")) {
#ifdef SRS_HAVE_SECTIONS
                srs_redact = 1;
#endif
                continue;
            }
            argv[j++] = argv[i];
        }
        argc = j;
        argv[argc] = NULL;
    }
    if (argc < 2) return usage_error("the following arguments are required: cmd");
    cmd = argv[1];
#ifdef SRS_HAVE_SECTIONS
    /* SIV syntax: -SAVE[=<item>[<item>]...][=<filename>]  (filename STDOUT = console) */
    if (!strncasecmp(cmd, "-save", 5) && (cmd[5] == 0 || cmd[5] == '=')) {
        const char *a = cmd[5] ? cmd + 6 : "", *items = NULL, *file = "STDOUT";
        char ibuf[8192];
        if (*a == '[') {
            const char *eq = strstr(a, "]=");
            if (eq) { snprintf(ibuf, sizeof ibuf, "%.*s", (int)(eq - a + 1), a); file = eq + 2; }
            else snprintf(ibuf, sizeof ibuf, "%s", a);
            items = ibuf;
        } else if (*a) {
            file = a;
        }
        return srs_save(items, file);
    }
#ifdef __linux__
    {   /* the commands that load the PC: one at a time across SRS processes (bench.lock, see bench.h) */
        static const char *const load[] = { "burn", "bench", "ipi", "latency", "pi", "crypto", "prime", "cachemem", "storage",
                                            "os-latency", "compute", "work", "net", "gpu", "gpu-burn", "gpu-compare", NULL };
        int k, is_load = 0, help = 0, yes = 0, rc;
        for (k = 0; load[k]; k++) if (!strcmp(cmd, load[k])) is_load = 1;
        for (k = 2; k < argc; k++) {
            if (!strcmp(argv[k], "-h") || !strcmp(argv[k], "--help")) help = 1;
            if (!strcmp(argv[k], "--yes") || !strcmp(argv[k], "-y")) yes = 1;
        }
        if (is_load && !help) {
            char who[100], what[300];
            int pid = 0;
            if (bench_lock_other(who, sizeof who, &pid)) {
                snprintf(what, sizeof what, "another SRS benchmark (%s, PID %d) is running; two loads at once skew each other's results.", who, pid);
                if (yes) fprintf(stderr, "%s: warning - %s Starting anyway (--yes).\n", cmd, what);
                else if (!ask_start(cmd, what)) return 2;
            }
            bench_lock_take(cmd);
            if (!strcmp(cmd, "burn")) rc = cmd_burn(argc, argv);
            else if (!strcmp(cmd, "bench") || !strcmp(cmd, "ipi") || !strcmp(cmd, "latency")) rc = cmd_bench(argc, argv);
            else if (!strcmp(cmd, "pi") || !strcmp(cmd, "crypto") || !strcmp(cmd, "prime")) rc = cmd_bench2(argc, argv);
            else if (!strcmp(cmd, "cachemem")) rc = cmd_cachemem(argc, argv);
            else if (!strcmp(cmd, "storage")) rc = cmd_storage(argc, argv);
            else if (!strcmp(cmd, "os-latency")) rc = cmd_os_latency(argc, argv);
            else if (!strcmp(cmd, "compute")) rc = cmd_compute(argc, argv);
            else if (!strcmp(cmd, "work")) rc = cmd_work(argc, argv);
            else if (!strcmp(cmd, "net")) rc = cmd_net(argc, argv);
            else if (!strcmp(cmd, "gpu")) rc = cmd_gpu(argc, argv);
            else if (!strcmp(cmd, "gpu-burn")) rc = cmd_gpu_burn(argc, argv);
            else rc = cmd_gpu_compare(argc, argv);
            bench_lock_release();
            return rc;
        }
    }
    if (!strcmp(cmd, "burn")) return cmd_burn(argc, argv);
    if (!strcmp(cmd, "bench") || !strcmp(cmd, "ipi") || !strcmp(cmd, "latency")) return cmd_bench(argc, argv);
    if (!strcmp(cmd, "pi") || !strcmp(cmd, "crypto") || !strcmp(cmd, "prime")) return cmd_bench2(argc, argv);
    if (!strcmp(cmd, "cachemem")) return cmd_cachemem(argc, argv);
    if (!strcmp(cmd, "storage")) return cmd_storage(argc, argv);
    if (!strcmp(cmd, "os-latency")) return cmd_os_latency(argc, argv);
    if (!strcmp(cmd, "compute")) return cmd_compute(argc, argv);
    if (!strcmp(cmd, "work")) return cmd_work(argc, argv);
    if (!strcmp(cmd, "net")) return cmd_net(argc, argv);
    if (!strcmp(cmd, "gpu")) return cmd_gpu(argc, argv);
    if (!strcmp(cmd, "gpu-burn")) return cmd_gpu_burn(argc, argv);
    if (!strcmp(cmd, "gpu-compare")) return cmd_gpu_compare(argc, argv);
    if (!strcmp(cmd, "cuda-toolkit")) return cmd_cuda_toolkit(argc, argv);
    if (!strcmp(cmd, "gpu-top") || !strcmp(cmd, "big-top")) return cmd_gpu_top(argc, argv);   /* big-top: the window's name since 2.5 */
    if (!strcmp(cmd, "ir")) return cmd_ir(argc, argv);
    if (!strcmp(cmd, "update-lists")) return cmd_update_lists(argc, argv);
    if (!strcmp(cmd, "intel-imc-dump")) return mt_intel_dump(stdout);   /* srs-root only (root): Intel memory timings, read-only */
#endif
    if (!strcmp(cmd, "guide")) {
        const char *id = NULL;
        int i, dry = 0, ok;
        char msg[8000];
        for (i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "--print")) dry = 1;
            else if (argv[i][0] == '-') return usage_error("guide [ITEM] [--print]");
            else id = argv[i];
        }
        if (id && !srs_section_find(id)) { fprintf(stderr, "guide: unknown item '%s' (see --list)\n", id); return 2; }
        ok = srs_guide_open(id, dry, msg, sizeof msg);
        if (dry && ok) printf("Item     %s\nSection  %s\nCommand  %s\n", id ? id : "(start)", srs_guide_anchor(id), msg);
        else if (*msg) fprintf(ok ? stdout : stderr, "%s\n", msg);
        return ok ? 0 : 1;
    }
    if (!strcmp(cmd, "--list")) {
        int i;
        static const char *bk[] = { "linux", "absent", "windows-only" };
        for (i = 0; i < srs_section_count(); i++) {
            const srs_item *s = srs_section_at(i);
            printf("%-20s %-9s %-13s %-8s %s%s\n", s->id, s->category, bk[s->bucket], s->group ? s->group : "",
                   s->title, s->fn ? "" : (s->bucket == B_LINUX ? "  [stub]" : ""));
        }
        return 0;
    }
#endif
    if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help")) { fputs(USAGE, stdout); fputs("\n", stdout); fputs(HELP, stdout); return 0; }
    if (!strcmp(cmd, "--version")) { printf("sysreport-search %s (%s)\nInspired by SIV64X (System Information Viewer) by Ray Hinchliffe, RH Software (rh-software.com),\nbuilt to improve on it; free for the Linux and open-source community.\nIndependent project, not affiliated with SIV or its author. GPL-2.0-or-later.\nHome page: https://github.com/eugenesflanagin-spec/srs-sysinfo\n", SRS_VERSION, plat_build_info()); return 0; }
    if (strcmp(cmd, "id") && strcmp(cmd, "find") && strcmp(cmd, "devices")) {
        char m[200];
        snprintf(m, sizeof m, "argument cmd: invalid choice: '%s' (choose from 'id', 'find', 'devices')", cmd);
        return usage_error(m);
    }
    o.bus = !strcmp(cmd, "devices") ? "all" : "pci";
    if (parse(argc, argv, 2, &o)) return usage_error(o.err);
    if (!strcmp(cmd, "devices")) {
        if (!choice(o.bus, "pci", "usb", "all")) return usage_error("argument --bus: invalid choice");
        if (o.npos) return usage_error("unrecognized arguments");
        return cmd_devices(&o);
    }
    if (o.npos != 1) return usage_error(!strcmp(cmd, "id") ? "the following arguments are required: id"
                                                          : "the following arguments are required: pattern");
    if (!strcmp(cmd, "id")) {
        if (!choice(o.bus, "pci", "usb", NULL)) return usage_error("argument --bus: invalid choice");
        if (!choice(o.mode, "tree", "scan", "info")) return usage_error("argument --mode: invalid choice");
        return cmd_id(&o);
    }
    if (!choice(o.color, "auto", "always", "never")) return usage_error("argument --color: invalid choice");
    return cmd_find(&o);
}

#ifndef SRS_NO_MAIN
int main(int argc, char **argv) { return srs_cli_main(argc, argv); }
#endif
