/* cli.c - command-line front end; output matches the original Python tool. */
#include "srs.h"
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
    "usage: sysreport-search [-h] [--version] {id,find,devices,-save,--list,burn,bench,ipi,latency,pi,crypto,prime,cachemem,storage,os-latency,compute,work,net,gpu,gpu-burn,gpu-compare} ...\n";

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
    "Benchmarks and stress (load the CPU, the drive, the network or the GPU; run only when you ask):\n"
    "  burn [--threads N | --cpus 0,2,5] [--seconds S] [--mode same|swap] [--limit C] [--cool S] [--cycles N]\n"
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
    "  crypto [--threads N] [--seconds S] [--yes]\n"
    "        AES-128 and SHA-256 speed, plain C and with AES-NI / SHA-NI (default all threads, 1 s);\n"
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
    "          [--code auto|c|sse2|avx2|avx512] [--single] [--yes]\n"
    "        CPU Compute: matrix multiply, peak multiply-add, FFT, N-body, Mandelbrot, ray tracer in GFLOPS /\n"
    "        Mpixels/s, 1 thread and N threads (default all), every answer checked; ~10 s, all threads 100%\n"
    "        busy (--single: one thread); asks first, --yes starts at once\n"
    "  work [--threads N] [--seconds S] [--only compress,sort,hash,text,interp] [--libs zlib,zstd,lz4,xz]\n"
    "       [--single] [--part-kib K] [--yes]\n"
    "        Everyday work: compression (system zlib / zstd / lz4 / xz), sorting, checksums, text processing\n"
    "        and a bytecode interpreter, 1 thread and N threads (default all), every result checked;\n"
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
    "           [--limit C] [--list] [--yes]\n"
    "        GPU Burnin: repeated matrix multiplications at full GPU load, compute errors counted\n"
    "        (0 = PASS), live temperature / power / clock; stops at the temperature limit (default\n"
    "        100 C), on a driver error or a GPU that stops answering; never changes clocks, voltages\n"
    "        or fans (default 60 s); asks first, --yes starts at once\n"
    "  gpu-compare [--backends vulkan,hip,cuda,opencl] [--seconds S] [--device N] [--yes]\n"
    "        GPU Compute comparison: the same tests (matrix multiply with the own kernel and the vendor\n"
    "        library, multiply-add, copy, upload / download, launch time) through every installed back end\n"
    "        on the same GPU, side by side: fastest marked, % of Vulkan; asks first, --yes starts at once\n"
    "  prime [--limit 1e8|1e9|1e10|N] [--threads N] [--yes]\n"
    "        count the primes up to the limit, checked against the known count (default 1e9, all threads);\n"
    "        asks first, --yes starts at once\n"
#endif
    "\n"
    "srs-gui only: --gui-open=ITEM[,ITEM|burnin|stress|bench|ipi|latency|pi|crypto|prime|cachemem|storagebench|oslatency|compute|work|netbench|gpubench|gpuburn|gpucompare|sensors|idlookup|find]\n"
    "        open those panels / tools at start (also SRS_OPEN=...), --gui-tray start in the tray\n"
    "        --gui-classic start in the classic SIV-style main window, --gui-new in the new one\n"
    "        --gui-open=power-cost the Power & cost page; --power-recorder record its history with no window\n"
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
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t g_int;
static void on_int(int s) { (void)s; g_int = 1; }

/* burn [--threads N | --cpus 0,2,5] [--seconds S] [--mode same|swap] [--limit C] [--cool S] [--cycles N] */
static int cmd_burn(int argc, char **argv)
{
    burn_cfg c;
    burn_status st;
    int i, threads = -1, rc;
    const char *cpus = NULL;
    burn_cfg_default(&c);
    c.burn_s = 10;
    c.cool_s = 0;
    for (i = 2; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--threads") && v) { threads = atoi(v); i++; }
        else if (!strcmp(a, "--cpus") && v) { cpus = v; i++; }
        else if (!strcmp(a, "--seconds") && v) { c.burn_s = atof(v); i++; }
        else if (!strcmp(a, "--cool") && v) { c.cool_s = atof(v); i++; }
        else if (!strcmp(a, "--cycles") && v) { c.cycles = atoi(v); i++; }
        else if (!strcmp(a, "--limit") && v) { c.temp_limit_c = atof(v); i++; }
        else if (!strcmp(a, "--mode") && v) { c.mode = !strcmp(v, "swap") ? BURN_SWAP : BURN_SAME; i++; }
        else return usage_error("burn [--threads N | --cpus 0,2,5] [--seconds S] [--mode same|swap] [--limit C] [--cool S] [--cycles N]");
    }
    if (c.burn_s <= 0 || c.burn_s > 750.0 * 3600) return usage_error("--seconds must be 1..2700000");
    if (cpus) {
        char tmp[1024], *t;
        memset(c.enabled, 0, sizeof c.enabled);
        snprintf(tmp, sizeof tmp, "%s", cpus);
        for (t = strtok(tmp, ","); t; t = strtok(NULL, ",")) { int n = atoi(t); if (n >= 0 && n < c.ncpu) c.enabled[n] = 1; }
    } else if (threads > 0) {
        for (i = 0; i < c.ncpu; i++) c.enabled[i] = i < threads;
    }
    signal(SIGINT, on_int);
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
        fprintf(stderr, "%s: not started - %s\n%*s  Run it from a terminal to be asked, or pass --yes to start without asking.\n",
                cmd, what, (int)strlen(cmd), "");
        return 0;
    }
    fprintf(stderr, "%s\nStart now? [y/N] ", what);
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
        return bench_ipi(cpus, rounds, &o) ? 1 : 0;
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
                 "                                [--code auto|c|sse2|avx2|avx512] [--single] [--yes]");
            return 0;
        }
        if (need && !v) { fprintf(stderr, "compute: %s needs a value\n", a); return 2; }
        if (need) i++;
        if (!strcmp(a, "--threads")) c.threads = atoi(v);
        else if (!strcmp(a, "--seconds")) c.seconds = atof(v);
        else if (!strcmp(a, "--single")) c.single_only = 1;
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
    if (!cp_available()) { bench_compute(&c, &o, NULL); return 2; }
    snprintf(what, sizeof what, "CPU Compute runs matrix multiply, multiply-add, FFT, N-body, Mandelbrot and a ray tracer for about %.0f s,\n"
             "%s%s\nAn unstable overclock or undervolt can crash the PC under this load (--single keeps it light).",
             cp_estimate_seconds(&c), c.single_only ? "on one thread only." : "on one thread and then on all chosen threads at once: every CPU 100% busy.",
             cp_isa_best() == CP_ISA_AVX512 && (c.isa == CP_ISA_AUTO || c.isa == CP_ISA_AVX512) ? " It uses AVX-512, which draws a lot of power." : "");
    if (!yes && !ask_start("compute", what)) return 2;
    rc = bench_compute(&c, &o, NULL);
    if (rc == -1) fprintf(stderr, "compute: not started - CPU Burnin / Stress is running, or out of memory\n");
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
                 "                             [--libs zlib,zstd,lz4,xz] [--single] [--part-kib K] [--yes]");
            return 0;
        }
        if (need && !v) { fprintf(stderr, "work: %s needs a value\n", a); return 2; }
        if (need) i++;
        if (!strcmp(a, "--threads")) c.threads = atoi(v);
        else if (!strcmp(a, "--seconds")) c.seconds = atof(v);
        else if (!strcmp(a, "--part-kib")) c.part_kib = atol(v);
        else if (!strcmp(a, "--single")) c.single_only = 1;
        else if (!strcmp(a, "--yes") || !strcmp(a, "-y")) yes = 1;
        else if (!strcmp(a, "--only")) { if (!parse_ids("work", "--only", v, wk_group_id, WK_NGROUP, &c.run_mask)) return 2; }
        else if (!strcmp(a, "--libs")) { if (!parse_ids("work", "--libs", v, wk_lib_id, WK_NLIB, &c.codec_mask)) return 2; }
        else { fprintf(stderr, "work: unknown option '%s'\n", a); return 2; }
    }
    if (c.seconds < 0.1 || c.seconds > 60) return usage_error("work: --seconds must be 0.1..60");
    if (c.part_kib < 64 || c.part_kib > 16384) return usage_error("work: --part-kib must be 64..16384");
    if (c.threads < 0 || c.threads > BURN_MAXCPU) return usage_error("work: --threads must be 1 or more (0 = all)");
    if (!wk_available()) { bench_work(&c, &o, NULL); return 2; }
    snprintf(what, sizeof what, "Everyday work runs compression, sorting, checksum, text and interpreter tests for about %.0f s,\n"
             "%s\nThe test data is generated; none of your files are read or written (only the result is saved).",
             wk_estimate_seconds(&c), c.single_only ? "on one thread only." :
             "on one thread and then on all chosen threads at once: every CPU 100% busy, up to ~100 MB of memory per thread.");
    if (!yes && !ask_start("work", what)) return 2;
    rc = bench_work(&c, &o, NULL);
    if (rc == -1) fprintf(stderr, "work: not started - CPU Burnin / Stress or another run is active, or out of memory\n");
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
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { puts(usage); return 0; }
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
        "  --seconds S  GPU time per test, 0.1..10 (default 1)\n"
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
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { puts(usage); return 0; }
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
    if (c.seconds < 0.1 || c.seconds > 10) return usage_error("gpu: --seconds must be 0.1..10");
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
    fprintf(stderr, "\r%5.0f / %.0f s  %s%s: %7.1f GFLOPS  %ld error%s  edge %s  hotspot %s  %s  %s   ",
            s->elapsed, s->total, d->label, s->ndev > 1 ? " (+ more)" : "", d->gflops, err, err == 1 ? "" : "s",
            gb_c(t, sizeof t, d->now.edge_c, "C"), gb_c(j, sizeof j, d->now.junction_c, "C"),
            gb_c(p, sizeof p, d->now.power_w, "W"), gb_c(c, sizeof c, d->now.clock_mhz, "MHz"));
    fflush(stderr);
}

/* gpu-burn [--seconds N] [--backend ...] [--device N|all] [--fp32|--fp64] [--limit C] [--list] [--yes] */
static int cmd_gpu_burn(int argc, char **argv)
{
    static const char usage[] =
        "usage: sysreport-search gpu-burn [--seconds N] [--backend vulkan|hip|cuda|opencl|auto] [--device N|all]\n"
        "                                 [--fp32|--fp64] [--limit C] [--list] [--yes]\n"
        "  --seconds N   how long, 1..86400 (default 60)\n"
        "  --backend B   vulkan, hip (ROCm), cuda, opencl or auto (default: Vulkan when it has a GPU)\n"
        "  --device N    the device number of that back end (see --list), or all (default: the first)\n"
        "  --fp32/--fp64 the precision of the matrix multiplications (default fp32)\n"
        "  --limit C     stop when the GPU reaches this temperature in C, 50..120 (default 100;\n"
        "                hotspot / junction when the card reports it, else edge)\n"
        "  --list        list the devices of each installed back end and exit (starts each runtime;\n"
        "                runs no work on a GPU)\n"
        "  --yes         start without asking\n"
        "GPU Burnin never changes clocks, voltages or fans. Ctrl+C stops it.";
    srs_out o;
    gpuburn_cfg c;
    char why[300], what[1200], dev[300], bk[80];
    int i, yes = 0, list = 0, rc;
    gpuburn_cfg_default(&c);
    so_text_init(&o, stdout);
    for (i = 2; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        int need = !strcmp(a, "--seconds") || !strcmp(a, "--backend") || !strcmp(a, "--device") || !strcmp(a, "--limit") ||
                   !strcmp(a, "--limit-c");
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { puts(usage); return 0; }
        if (need && !v) { fprintf(stderr, "gpu-burn: %s needs a value\n", a); return 2; }
        if (need) i++;
        if (!strcmp(a, "--seconds")) c.seconds = atof(v);
        else if (!strcmp(a, "--limit") || !strcmp(a, "--limit-c")) c.limit_c = atof(v);
        else if (!strcmp(a, "--fp32")) c.prec = GP_FP32;
        else if (!strcmp(a, "--fp64")) c.prec = GP_FP64;
        else if (!strcmp(a, "--list")) list = 1;
        else if (!strcmp(a, "--yes") || !strcmp(a, "-y")) yes = 1;
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
    if (c.limit_c < 50 || c.limit_c > 120) return usage_error("gpu-burn: --limit must be 50..120 (C)");
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
        if (n > 0 && c.device >= 0 && c.device < n) snprintf(dev, sizeof dev, "%.250s", d[c.device].name);
        else if (n > 0 && c.device == -1) snprintf(dev, sizeof dev, "%.250s", d[0].name);
        else if (n <= 0 && c.backend == GB_VULKAN) {
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
        "  --seconds S   GPU time per test, 0.1..10 (default 1)\n"
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
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { puts(usage); return 0; }
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
    if (c.seconds < 0.1 || c.seconds > 10) return usage_error("gpu-compare: --seconds must be 0.1..10");
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
            if (!strcmp(a, "--help") || !strcmp(a, "-h")) { puts("usage: sysreport-search pi [--digits 128k|1m|..|32m|16ki|..|1mi|..|32mi|N] [--threads N] [--method chudnovsky|agm] [--save-digits FILE] [--yes]"); return 0; }
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
        for (i = 2; i < argc; i++) {
            const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
            if (!strcmp(a, "--help") || !strcmp(a, "-h")) { puts("usage: sysreport-search crypto [--threads N] [--seconds S] [--yes]"); return 0; }
            if (!strcmp(a, "--yes") || !strcmp(a, "-y")) { yes = 1; continue; }
            if (!v) return usage_error("crypto: option needs a value");
            if (!strcmp(a, "--threads")) threads = atoi(v);
            else if (!strcmp(a, "--seconds")) secs = atof(v);
            else return usage_error("crypto: unknown option");
            i++;
        }
        if (secs < 0.1 || secs > 60) return usage_error("--seconds must be 0.1..60");
        q.kind = BA_CRYPTO; q.threads = threads; q.secs = secs;
        if (!yes && !ask_bench("crypto", &q)) return 2;
        return bench_crypto(threads, secs, &o) ? 1 : 0;
    }
    {
        double lim = 1e9;
        int rc;
        for (i = 2; i < argc; i++) {
            const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
            if (!strcmp(a, "--help") || !strcmp(a, "-h")) { puts("usage: sysreport-search prime [--limit 100..1e11, e.g. 1g] [--threads N] [--yes]"); return 0; }
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
