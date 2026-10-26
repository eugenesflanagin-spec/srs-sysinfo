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
    "usage: sysreport-search [-h] [--version] {id,find,devices,-save,--list,burn,bench,ipi,latency} ...\n";

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
    "Benchmarks and stress (load the CPU; run only when you ask):\n"
    "  burn [--threads N | --cpus 0,2,5] [--seconds S] [--mode same|swap] [--limit C] [--cool S] [--cycles N]\n"
    "        CPU burn-in with a temperature limit (default 10 s, limit 90 C)\n"
    "  bench {ac,as,dd,dw,ec,ep,lc,lp,pc,pp} [--seconds S]\n"
    "        Dhrystone / Whetstone in a SIV benchmark mode (S = 0.1..60 s per measurement)\n"
    "  ipi [--cpus N] [--rounds R]\n"
    "        core-to-core (cache-line ping-pong) latency matrix\n"
    "  latency [--max MB]\n"
    "        memory latency by block size, up to MB (1..4096, default 256)\n"
#endif
    "\n"
    "srs-gui only: --gui-open=ITEM[,ITEM|burnin|stress|bench|ipi|latency|sensors|idlookup|find]\n"
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
    if (burn_start(&c)) { fprintf(stderr, "burn: nothing to run (no CPUs selected or already running)\n"); return 2; }
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

/* bench MODE [--seconds S] | ipi [--cpus N] [--rounds R] | latency [--max MB] - explicit runs only */
static int cmd_bench(int argc, char **argv)
{
    srs_out o;
    so_text_init(&o, stdout);
    if (!strcmp(argv[1], "bench")) {
        double secs = 1;
        int m, i;
        if (argc < 3) return usage_error("bench {ac,as,dd,dw,ec,ep,lc,lp,pc,pp} [--seconds S]");
        for (i = 3; i + 1 < argc; i++) if (!strcmp(argv[i], "--seconds")) secs = atof(argv[++i]);
        if (secs < 0.1 || secs > 60) return usage_error("--seconds must be 0.1..60");
        for (m = 0; m < BM_COUNT; m++)
            if (!strcmp(argv[2], bench_mode_id((bench_mode)m) + 10)) return bench_run((bench_mode)m, secs, &o) ? 1 : 0;
        return usage_error("bench: unknown mode");
    }
    if (!strcmp(argv[1], "ipi")) {
        int cpus = 0, rounds = 20000, i;
        for (i = 2; i + 1 < argc; i++) {
            if (!strcmp(argv[i], "--cpus")) cpus = atoi(argv[++i]);
            else if (!strcmp(argv[i], "--rounds")) rounds = atoi(argv[++i]);
        }
        return bench_ipi(cpus, rounds, &o) ? 1 : 0;
    }
    {
        long mb = 256;
        int i;
        for (i = 2; i + 1 < argc; i++) if (!strcmp(argv[i], "--max")) mb = atol(argv[++i]);
        if (mb < 1 || mb > 4096) return usage_error("--max must be 1..4096 MB");
        return bench_latency(mb * 1024 * 1024, &o) ? 1 : 0;
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
